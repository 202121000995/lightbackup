package main

import (
	"bufio"
	"bytes"
	"context"
	"crypto/subtle"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync"
	"time"
)

type task struct {
	Name        string `json:"name"`
	Enabled     bool   `json:"enabled"`
	DateFolder  bool   `json:"date_folder"`
	Time        string `json:"time"`
	SourcePath  string `json:"source_path"`
	Destination string `json:"destination"`
}

type tasksFile struct {
	Comment string `json:"_comment,omitempty"`
	Tasks   []task `json:"tasks"`
}

type rcloneConfig struct {
	Remote string            `json:"remote"`
	Env    map[string]string `json:"env"`
	Flags  []string          `json:"flags"`
}

type destinationConfig struct {
	Name       string       `json:"name"`
	Type       string       `json:"type"`
	RemotePath string       `json:"remote_path,omitempty"`
	LocalPath  string       `json:"local_path,omitempty"`
	Rclone     rcloneConfig `json:"rclone,omitempty"`
}

type destination struct {
	FileName    string            `json:"file_name"`
	Name        string            `json:"name"`
	Type        string            `json:"type"`
	Address     string            `json:"address"`
	Path        string            `json:"path"`
	LocalPath   string            `json:"local_path"`
	Remote      string            `json:"remote"`
	Environment map[string]string `json:"environment"`
	Flags       []string          `json:"flags"`
	Raw         json.RawMessage   `json:"raw,omitempty"`
}

type logEntry struct {
	Time    string `json:"time"`
	Task    string `json:"task"`
	Message string `json:"message"`
}

type app struct {
	dataDir    string
	rclonePath string
	bindUser   string
	bindPass   string

	mu               sync.Mutex
	tasks            []task
	destinations     []destination
	logs             []logEntry
	queue            []int
	currentTaskIndex int
	currentTaskName  string
	currentCancel    context.CancelFunc
	triggeredTimes   map[string]bool
	wakeRunner       chan struct{}
}

type stateResponse struct {
	Tasks        []task        `json:"tasks"`
	Destinations []destination `json:"destinations"`
	Logs         []logEntry    `json:"logs"`
	Queue        []int         `json:"queue"`
	CurrentIndex int           `json:"current_index"`
	CurrentTask  string        `json:"current_task"`
	RclonePath   string        `json:"rclone_path"`
	DataDir      string        `json:"data_dir"`
}

var timePattern = regexp.MustCompile(`^\d{2}:\d{2}:\d{2}$`)

func main() {
	addr := flag.String("addr", ":8080", "HTTP listen address")
	dataDir := flag.String("data-dir", ".", "directory containing tasks.json, configs and logs")
	rclone := flag.String("rclone", "", "path to rclone binary; defaults to ./rclone, ./rclone.exe or PATH")
	user := flag.String("user", "", "optional basic auth username")
	pass := flag.String("pass", "", "optional basic auth password")
	flag.Parse()

	absDataDir, err := filepath.Abs(*dataDir)
	if err != nil {
		log.Fatal(err)
	}
	resolvedRclone, err := resolveRclone(absDataDir, *rclone)
	if err != nil {
		log.Fatal(err)
	}

	application := &app{
		dataDir:          absDataDir,
		rclonePath:       resolvedRclone,
		bindUser:         *user,
		bindPass:         *pass,
		currentTaskIndex: -1,
		triggeredTimes:   make(map[string]bool),
		wakeRunner:       make(chan struct{}, 1),
	}
	if err := application.loadAll(); err != nil {
		log.Fatal(err)
	}

	go application.schedulerLoop()
	go application.runnerLoop()

	mux := http.NewServeMux()
	mux.HandleFunc("/", application.handleIndex)
	mux.HandleFunc("/api/state", application.handleState)
	mux.HandleFunc("/api/tasks", application.handleTasks)
	mux.HandleFunc("/api/run", application.handleRun)
	mux.HandleFunc("/api/stop", application.handleStop)
	mux.HandleFunc("/api/reload", application.handleReload)
	mux.HandleFunc("/api/destinations", application.handleDestinations)
	mux.HandleFunc("/api/destinations/", application.handleDestinationByName)

	server := &http.Server{
		Addr:              *addr,
		Handler:           application.withAuth(mux),
		ReadHeaderTimeout: 10 * time.Second,
	}
	log.Printf("LightBackup Server listening on http://%s, data dir: %s", *addr, absDataDir)
	log.Fatal(server.ListenAndServe())
}

func resolveRclone(dataDir, configured string) (string, error) {
	candidates := []string{}
	if configured != "" {
		candidates = append(candidates, configured)
	}
	candidates = append(candidates,
		filepath.Join(dataDir, "rclone"),
		filepath.Join(dataDir, "rclone.exe"),
	)
	for _, candidate := range candidates {
		if info, err := os.Stat(candidate); err == nil && !info.IsDir() {
			return candidate, nil
		}
	}
	if found, err := exec.LookPath("rclone"); err == nil {
		return found, nil
	}
	return "", errors.New("没有找到 rclone：请安装 rclone，或用 -rclone 指定路径")
}

func (a *app) withAuth(next http.Handler) http.Handler {
	if a.bindUser == "" && a.bindPass == "" {
		return next
	}
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		user, pass, ok := r.BasicAuth()
		if !ok ||
			subtle.ConstantTimeCompare([]byte(user), []byte(a.bindUser)) != 1 ||
			subtle.ConstantTimeCompare([]byte(pass), []byte(a.bindPass)) != 1 {
			w.Header().Set("WWW-Authenticate", `Basic realm="LightBackup"`)
			http.Error(w, "需要登录", http.StatusUnauthorized)
			return
		}
		next.ServeHTTP(w, r)
	})
}

func (a *app) loadAll() error {
	if err := os.MkdirAll(filepath.Join(a.dataDir, "configs"), 0755); err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Join(a.dataDir, "logs"), 0755); err != nil {
		return err
	}
	destinations, err := loadDestinations(filepath.Join(a.dataDir, "configs"))
	if err != nil {
		return err
	}
	tasks, err := loadTasks(filepath.Join(a.dataDir, "tasks.json"), destinations)
	if err != nil {
		return err
	}
	a.mu.Lock()
	a.destinations = destinations
	a.tasks = tasks
	a.mu.Unlock()
	return nil
}

func loadTasks(path string, destinations []destination) ([]task, error) {
	body, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		defaultDestination := ""
		if len(destinations) > 0 {
			defaultDestination = destinations[0].Name
		}
		return []task{{
			Name:        "任务 1",
			Enabled:     false,
			DateFolder:  true,
			Time:        "23:55:55",
			SourcePath:  "/data/source",
			Destination: defaultDestination,
		}}, nil
	}
	if err != nil {
		return nil, err
	}

	var rows []task
	if len(bytes.TrimSpace(body)) > 0 && bytes.TrimSpace(body)[0] == '[' {
		if err := json.Unmarshal(body, &rows); err != nil {
			return nil, err
		}
	} else {
		var file tasksFile
		if err := json.Unmarshal(body, &file); err != nil {
			return nil, err
		}
		rows = file.Tasks
	}
	for i := range rows {
		normalizeTask(&rows[i])
	}
	return rows, nil
}

func loadDestinations(configDir string) ([]destination, error) {
	files, err := filepath.Glob(filepath.Join(configDir, "*.json"))
	if err != nil {
		return nil, err
	}
	sort.Strings(files)
	destinations := make([]destination, 0, len(files))
	for _, path := range files {
		body, err := os.ReadFile(path)
		if err != nil {
			return nil, err
		}
		destination, err := parseDestination(filepath.Base(path), body)
		if err != nil {
			return nil, fmt.Errorf("%s: %w", path, err)
		}
		if destination.Name != "" {
			destinations = append(destinations, destination)
		}
	}
	return destinations, nil
}

func parseDestination(fileName string, body []byte) (destination, error) {
	var config destinationConfig
	if err := json.Unmarshal(body, &config); err != nil {
		return destination{}, err
	}
	env := config.Rclone.Env
	if env == nil {
		env = map[string]string{}
	}
	d := destination{
		FileName:    fileName,
		Name:        config.Name,
		Type:        strings.ToLower(config.Type),
		Path:        config.RemotePath,
		LocalPath:   config.LocalPath,
		Remote:      config.Rclone.Remote,
		Environment: env,
		Flags:       append([]string(nil), config.Rclone.Flags...),
		Raw:         json.RawMessage(body),
	}
	if d.Flags == nil {
		d.Flags = []string{}
	}
	switch d.Type {
	case "ftp":
		d.Address = envValue(env, "HOST")
	case "webdav":
		d.Address = envValue(env, "URL")
	case "awss3":
		d.Address = envValue(env, "REGION")
	case "qiniu", "cfr2":
		d.Address = envValue(env, "ENDPOINT")
	case "local":
		d.Address = "本机文件夹"
		d.Path = d.LocalPath
	}
	d.Address = strings.TrimSuffix(strings.TrimPrefix(strings.TrimPrefix(d.Address, "https://"), "http://"), "/")
	return d, nil
}

func envValue(env map[string]string, suffix string) string {
	needle := "_" + strings.ToUpper(suffix)
	for key, value := range env {
		if strings.HasSuffix(strings.ToUpper(key), needle) {
			return value
		}
	}
	return ""
}

func normalizeTask(t *task) {
	t.Name = strings.TrimSpace(t.Name)
	if t.Name == "" {
		t.Name = "任务"
	}
	if !t.DateFolder {
		t.DateFolder = false
	}
	t.Time = strings.TrimSpace(t.Time)
	if !validTime(t.Time) {
		t.Time = "23:55:55"
	}
	t.SourcePath = strings.TrimSpace(t.SourcePath)
	t.Destination = strings.TrimSpace(t.Destination)
}

func validTime(value string) bool {
	if !timePattern.MatchString(value) {
		return false
	}
	_, err := time.Parse("15:04:05", value)
	return err == nil
}

func (a *app) handleIndex(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_, _ = io.WriteString(w, indexHTML)
}

func (a *app) handleState(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	a.writeJSON(w, a.snapshot())
}

func (a *app) snapshot() stateResponse {
	a.mu.Lock()
	defer a.mu.Unlock()
	return stateResponse{
		Tasks:        append([]task(nil), a.tasks...),
		Destinations: append([]destination(nil), a.destinations...),
		Logs:         append([]logEntry{}, a.logs...),
		Queue:        append([]int{}, a.queue...),
		CurrentIndex: a.currentTaskIndex,
		CurrentTask:  a.currentTaskName,
		RclonePath:   a.rclonePath,
		DataDir:      a.dataDir,
	}
}

func (a *app) handleTasks(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var payload struct {
		Tasks []task `json:"tasks"`
	}
	if err := json.NewDecoder(r.Body).Decode(&payload); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	for i := range payload.Tasks {
		payload.Tasks[i].Time = strings.TrimSpace(payload.Tasks[i].Time)
		if !validTime(payload.Tasks[i].Time) {
			http.Error(w, "定时时间格式应为 HH:mm:ss", http.StatusBadRequest)
			return
		}
		normalizeTask(&payload.Tasks[i])
	}
	if err := a.saveTasks(payload.Tasks); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	a.appendLog("设置", "任务设置已保存")
	a.writeJSON(w, a.snapshot())
}

func (a *app) saveTasks(rows []task) error {
	a.mu.Lock()
	a.tasks = append([]task(nil), rows...)
	a.mu.Unlock()
	file := tasksFile{
		Comment: "任务由 LightBackup Server 保存；目的地详细参数位于 configs 目录。",
		Tasks:   rows,
	}
	return writeJSONFile(filepath.Join(a.dataDir, "tasks.json"), file)
}

func (a *app) handleRun(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var payload struct {
		Indexes []int `json:"indexes"`
		Enabled bool  `json:"enabled"`
	}
	if err := json.NewDecoder(r.Body).Decode(&payload); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	a.mu.Lock()
	indexes := append([]int(nil), payload.Indexes...)
	if payload.Enabled {
		indexes = indexes[:0]
		for i, row := range a.tasks {
			if row.Enabled {
				indexes = append(indexes, i)
			}
		}
	}
	added := a.queueTasksLocked(indexes)
	a.mu.Unlock()
	if added > 0 {
		a.signalRunner()
	}
	a.writeJSON(w, a.snapshot())
}

func (a *app) handleStop(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	a.mu.Lock()
	cancel := a.currentCancel
	a.queue = nil
	a.mu.Unlock()
	if cancel != nil {
		cancel()
		a.appendLog("系统", "已请求停止当前任务并清空等待队列")
	}
	a.writeJSON(w, a.snapshot())
}

func (a *app) handleReload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if err := a.loadAll(); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	a.appendLog("配置", "已重新加载任务和目的地配置")
	a.writeJSON(w, a.snapshot())
}

func (a *app) handleDestinations(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodPost:
		var payload struct {
			FileName string          `json:"file_name"`
			Content  json.RawMessage `json:"content"`
		}
		if err := json.NewDecoder(r.Body).Decode(&payload); err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		var pretty bytes.Buffer
		if err := json.Indent(&pretty, payload.Content, "", "  "); err != nil {
			http.Error(w, "目的地 JSON 格式不正确", http.StatusBadRequest)
			return
		}
		parsed, err := parseDestination("", pretty.Bytes())
		if err != nil || parsed.Name == "" || parsed.Type == "" {
			http.Error(w, "目的地配置必须包含 name 和 type", http.StatusBadRequest)
			return
		}
		fileName := safeFileName(payload.FileName)
		if fileName == "" {
			fileName = safeFileName(parsed.Name) + ".json"
		}
		if fileName == ".json" {
			fileName = "destination.json"
		}
		path := filepath.Join(a.dataDir, "configs", fileName)
		if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		if err := os.WriteFile(path, append(pretty.Bytes(), '\n'), 0600); err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		if err := a.loadAll(); err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		a.appendLog("配置", "目的地配置已保存："+fileName)
		a.writeJSON(w, a.snapshot())
	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

func (a *app) handleDestinationByName(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodDelete {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	fileName := safeFileName(strings.TrimPrefix(r.URL.Path, "/api/destinations/"))
	if fileName == "" {
		http.Error(w, "missing destination file", http.StatusBadRequest)
		return
	}
	if err := os.Remove(filepath.Join(a.dataDir, "configs", fileName)); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	if err := a.loadAll(); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	a.appendLog("配置", "目的地配置已删除："+fileName)
	a.writeJSON(w, a.snapshot())
}

func (a *app) queueTasksLocked(indexes []int) int {
	added := 0
	for _, index := range indexes {
		if index < 0 || index >= len(a.tasks) {
			continue
		}
		if index == a.currentTaskIndex || containsInt(a.queue, index) {
			continue
		}
		a.queue = append(a.queue, index)
		added++
	}
	return added
}

func containsInt(rows []int, value int) bool {
	for _, row := range rows {
		if row == value {
			return true
		}
	}
	return false
}

func (a *app) signalRunner() {
	select {
	case a.wakeRunner <- struct{}{}:
	default:
	}
}

func (a *app) schedulerLoop() {
	ticker := time.NewTicker(500 * time.Millisecond)
	defer ticker.Stop()
	for range ticker.C {
		now := time.Now()
		currentTime := now.Format("15:04:05")
		currentDate := now.Format("2006-01-02")
		a.mu.Lock()
		if len(a.triggeredTimes) > 1000 {
			a.triggeredTimes = make(map[string]bool)
		}
		var indexes []int
		for i, row := range a.tasks {
			if !row.Enabled || row.Time != currentTime {
				continue
			}
			key := fmt.Sprintf("%s|%s|%d", currentDate, currentTime, i)
			if a.triggeredTimes[key] {
				continue
			}
			a.triggeredTimes[key] = true
			indexes = append(indexes, i)
		}
		added := a.queueTasksLocked(indexes)
		a.mu.Unlock()
		if added > 0 {
			a.signalRunner()
		}
	}
}

func (a *app) runnerLoop() {
	for range a.wakeRunner {
		for {
			index, row, dest, ok := a.nextRunnable()
			if !ok {
				break
			}
			a.runTask(index, row, dest)
		}
	}
}

func (a *app) nextRunnable() (int, task, destination, bool) {
	for {
		a.mu.Lock()
		if len(a.queue) == 0 {
			a.currentTaskIndex = -1
			a.currentTaskName = ""
			a.currentCancel = nil
			a.mu.Unlock()
			return 0, task{}, destination{}, false
		}
		index := a.queue[0]
		a.queue = a.queue[1:]
		if index < 0 || index >= len(a.tasks) {
			a.mu.Unlock()
			continue
		}
		row := a.tasks[index]
		dest, found := a.destinationByNameLocked(row.Destination)
		a.currentTaskIndex = index
		a.currentTaskName = row.Name
		a.mu.Unlock()
		if !found {
			a.appendLog(row.Name, "失败：找不到目的地“"+row.Destination+"”")
			continue
		}
		return index, row, dest, true
	}
}

func (a *app) destinationByNameLocked(name string) (destination, bool) {
	for _, row := range a.destinations {
		if row.Name == name {
			return row, true
		}
	}
	return destination{}, false
}

func (a *app) runTask(index int, row task, dest destination) {
	source := strings.TrimSpace(row.SourcePath)
	if info, err := os.Stat(source); err != nil || !info.IsDir() {
		a.appendLog(row.Name, "失败：源文件夹不存在："+source)
		return
	}
	target, err := buildTarget(row, dest)
	if err != nil {
		a.appendLog(row.Name, "失败："+err.Error())
		return
	}
	args := []string{"copy", source, target, "--create-empty-src-dirs", "--stats=5s", "--stats-one-line"}
	args = append(args, dest.Flags...)

	ctx, cancel := context.WithCancel(context.Background())
	a.mu.Lock()
	if a.currentTaskIndex == index {
		a.currentCancel = cancel
	}
	a.mu.Unlock()
	defer cancel()

	cmd := exec.CommandContext(ctx, a.rclonePath, args...)
	cmd.Env = append(os.Environ(), envList(dest.Environment)...)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		a.appendLog(row.Name, "无法读取 rclone 输出："+err.Error())
		return
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		a.appendLog(row.Name, "无法读取 rclone 错误输出："+err.Error())
		return
	}

	a.appendLog(row.Name, "开始备份到 "+row.Destination)
	if err := cmd.Start(); err != nil {
		a.appendLog(row.Name, "无法启动 rclone："+err.Error())
		return
	}

	var wg sync.WaitGroup
	wg.Add(2)
	go a.scanOutput(&wg, row.Name, stdout)
	go a.scanOutput(&wg, row.Name, stderr)
	waitErr := cmd.Wait()
	wg.Wait()

	a.mu.Lock()
	if a.currentTaskIndex == index {
		a.currentCancel = nil
	}
	a.mu.Unlock()

	if ctx.Err() == context.Canceled {
		a.appendLog(row.Name, "备份已停止")
		return
	}
	if waitErr != nil {
		a.appendLog(row.Name, "备份失败："+waitErr.Error())
		return
	}
	a.appendLog(row.Name, "备份完成")
}

func buildTarget(row task, dest destination) (string, error) {
	archivePath := appendRemotePath(time.Now().Format("2006-01-02"), pathSafeSegment(row.Name))
	if dest.Type == "local" {
		if strings.TrimSpace(dest.LocalPath) == "" {
			return "", errors.New("本地目的地未填写 local_path")
		}
		target := dest.LocalPath
		if row.DateFolder {
			target = filepath.Join(target, filepath.FromSlash(archivePath))
		}
		if isSubpathOrSame(row.SourcePath, target) {
			return "", errors.New("本地目的地不能是源文件夹或其子文件夹")
		}
		return target, nil
	}
	if dest.Remote == "" {
		return "", errors.New("目的地缺少 rclone.remote")
	}
	remotePath := dest.Path
	if row.DateFolder {
		remotePath = appendRemotePath(remotePath, archivePath)
	}
	return dest.Remote + ":" + remotePath, nil
}

func isSubpathOrSame(source, target string) bool {
	sourceAbs, err := filepath.Abs(source)
	if err != nil {
		return false
	}
	targetAbs, err := filepath.Abs(target)
	if err != nil {
		return false
	}
	sourceAbs = filepath.Clean(sourceAbs)
	targetAbs = filepath.Clean(targetAbs)
	if sourceAbs == targetAbs {
		return true
	}
	rel, err := filepath.Rel(sourceAbs, targetAbs)
	return err == nil && rel != "." && !strings.HasPrefix(rel, ".."+string(filepath.Separator)) && rel != ".."
}

func (a *app) scanOutput(wg *sync.WaitGroup, taskName string, reader io.Reader) {
	defer wg.Done()
	scanner := bufio.NewScanner(reader)
	buffer := make([]byte, 0, 64*1024)
	scanner.Buffer(buffer, 1024*1024)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line != "" {
			a.appendLog(taskName, line)
		}
	}
}

func envList(env map[string]string) []string {
	rows := make([]string, 0, len(env))
	for key, value := range env {
		rows = append(rows, key+"="+value)
	}
	return rows
}

func appendRemotePath(base, child string) string {
	cleanBase := strings.Trim(strings.TrimSpace(base), "/")
	cleanChild := strings.Trim(strings.TrimSpace(child), "/")
	if cleanBase == "" {
		return cleanChild
	}
	if cleanChild == "" {
		return cleanBase
	}
	return cleanBase + "/" + cleanChild
}

func pathSafeSegment(text string) string {
	replacer := strings.NewReplacer("\\", "_", "/", "_", ":", "_", "*", "_", "?", "_", "\"", "_", "<", "_", ">", "_", "|", "_")
	result := strings.TrimSpace(replacer.Replace(text))
	for strings.Contains(result, "__") {
		result = strings.ReplaceAll(result, "__", "_")
	}
	if result == "" {
		return "task"
	}
	return result
}

func safeFileName(text string) string {
	text = filepath.Base(strings.TrimSpace(text))
	var builder strings.Builder
	for _, ch := range text {
		if (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' {
			builder.WriteRune(ch)
		} else if ch > 127 {
			builder.WriteRune(ch)
		}
	}
	value := strings.Trim(builder.String(), ".")
	if value == "" {
		return ""
	}
	if !strings.HasSuffix(strings.ToLower(value), ".json") {
		value += ".json"
	}
	return value
}

func writeJSONFile(path string, value any) error {
	var buffer bytes.Buffer
	encoder := json.NewEncoder(&buffer)
	encoder.SetEscapeHTML(false)
	encoder.SetIndent("", "  ")
	if err := encoder.Encode(value); err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, buffer.Bytes(), 0600); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

func (a *app) appendLog(taskName, message string) {
	entry := logEntry{
		Time:    time.Now().Format("2006-01-02 15:04:05"),
		Task:    taskName,
		Message: message,
	}
	a.mu.Lock()
	a.logs = append(a.logs, entry)
	if len(a.logs) > 500 {
		a.logs = append([]logEntry(nil), a.logs[len(a.logs)-500:]...)
	}
	a.mu.Unlock()

	logDir := filepath.Join(a.dataDir, "logs")
	_ = os.MkdirAll(logDir, 0755)
	line := fmt.Sprintf("%s\t%s\t%s\n", entry.Time, entry.Task, entry.Message)
	file := filepath.Join(logDir, time.Now().Format("2006-01-02")+".log")
	f, err := os.OpenFile(file, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0600)
	if err != nil {
		return
	}
	defer f.Close()
	_, _ = f.WriteString(line)
}

func (a *app) writeJSON(w http.ResponseWriter, value any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	encoder := json.NewEncoder(w)
	encoder.SetEscapeHTML(false)
	if err := encoder.Encode(value); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
	}
}
