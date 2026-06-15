package main

const indexHTML = `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>轻量化定时备份</title>
  <style>
    :root {
      color-scheme: light;
      --ink: #1f2933;
      --muted: #667085;
      --line: #d8dee7;
      --panel: #ffffff;
      --page: #f3f6f8;
      --primary: #2563eb;
      --primary-dark: #1746a2;
      --ok: #0f8b68;
      --warn: #b25e09;
      --danger: #c2413a;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "Noto Sans CJK SC", "Microsoft YaHei", sans-serif;
      color: var(--ink);
      background: var(--page);
      letter-spacing: 0;
    }
    header {
      display: flex;
      align-items: center;
      gap: 18px;
      min-height: 68px;
      padding: 12px 22px;
      background: #ffffff;
      border-bottom: 1px solid var(--line);
      position: sticky;
      top: 0;
      z-index: 10;
    }
    h1 {
      font-size: 20px;
      margin: 0;
      font-weight: 700;
    }
    .sub {
      color: var(--muted);
      font-size: 13px;
      margin-top: 2px;
    }
    .status {
      margin-left: auto;
      display: flex;
      gap: 8px;
      align-items: center;
      color: var(--muted);
      font-size: 13px;
      min-width: 0;
    }
    .pill {
      border: 1px solid var(--line);
      border-radius: 999px;
      padding: 5px 10px;
      background: #f9fafb;
      color: var(--ink);
      white-space: nowrap;
    }
    main {
      width: min(1380px, calc(100vw - 28px));
      margin: 14px auto;
      display: grid;
      grid-template-columns: 1.08fr .92fr;
      gap: 14px;
    }
    section {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      min-width: 0;
    }
    .section-head {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 12px 14px;
      border-bottom: 1px solid var(--line);
    }
    h2 {
      margin: 0;
      font-size: 15px;
      font-weight: 700;
    }
    .grow { flex: 1; }
    button {
      appearance: none;
      border: 1px solid var(--line);
      background: #fff;
      color: var(--ink);
      border-radius: 6px;
      height: 32px;
      padding: 0 12px;
      font: inherit;
      cursor: pointer;
      white-space: nowrap;
    }
    button:hover { border-color: #9aa7b8; background: #f8fafc; }
    button.primary { border-color: var(--primary); background: var(--primary); color: white; }
    button.primary:hover { background: var(--primary-dark); }
    button.danger { border-color: #f2b8b5; color: var(--danger); }
    button.small { height: 28px; padding: 0 9px; font-size: 13px; }
    input, select, textarea {
      width: 100%;
      border: 1px solid var(--line);
      border-radius: 6px;
      min-height: 32px;
      padding: 6px 8px;
      color: var(--ink);
      background: #fff;
      font: inherit;
    }
    input[type="checkbox"] { width: 18px; min-height: 18px; }
    textarea {
      height: 280px;
      resize: vertical;
      font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
      font-size: 12px;
      line-height: 1.45;
    }
    table {
      width: 100%;
      border-collapse: collapse;
      table-layout: fixed;
    }
    th, td {
      border-bottom: 1px solid #edf0f5;
      padding: 8px;
      vertical-align: middle;
      font-size: 13px;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    th {
      color: #475467;
      background: #fbfcfe;
      font-weight: 600;
      text-align: left;
      position: sticky;
      top: 69px;
      z-index: 2;
    }
    tr.selected { background: #edf5ff; }
    .table-wrap { overflow: auto; max-height: 460px; }
    .tasks th:nth-child(1), .tasks td:nth-child(1) { width: 44px; text-align: center; }
    .tasks th:nth-child(2), .tasks td:nth-child(2) { width: 44px; text-align: center; }
    .tasks th:nth-child(3), .tasks td:nth-child(3) { width: 100px; }
    .tasks th:nth-child(4), .tasks td:nth-child(4) { width: 96px; }
    .tasks th:nth-child(6), .tasks td:nth-child(6) { width: 132px; }
    .tasks th:nth-child(7), .tasks td:nth-child(7) { width: 108px; }
    .destinations th:nth-child(1), .destinations td:nth-child(1) { width: 116px; }
    .destinations th:nth-child(2), .destinations td:nth-child(2) { width: 74px; }
    .destinations th:nth-child(3), .destinations td:nth-child(3) { width: 140px; }
    .editor {
      display: grid;
      grid-template-columns: 160px 1fr;
      gap: 10px;
      padding: 12px 14px 14px;
      align-items: start;
    }
    .editor label { color: var(--muted); font-size: 13px; padding-top: 7px; }
    .editor textarea { grid-column: 1 / -1; }
    .log {
      grid-column: 1 / -1;
    }
    .log-list {
      height: 230px;
      overflow: auto;
      padding: 6px 0;
      font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
      font-size: 12px;
      line-height: 1.55;
    }
    .log-row {
      display: grid;
      grid-template-columns: 150px 120px 1fr;
      gap: 10px;
      padding: 3px 14px;
      border-bottom: 1px solid #f2f4f7;
    }
    .log-row .time, .log-row .task { color: var(--muted); }
    .message { overflow-wrap: anywhere; }
    .empty {
      color: var(--muted);
      padding: 18px 14px;
      font-size: 13px;
    }
    @media (max-width: 980px) {
      header { align-items: flex-start; flex-direction: column; gap: 8px; }
      .status { margin-left: 0; flex-wrap: wrap; }
      main { grid-template-columns: 1fr; width: calc(100vw - 18px); }
      th { top: 0; }
      .editor { grid-template-columns: 1fr; }
      .editor label { padding-top: 0; }
      .log-row { grid-template-columns: 1fr; gap: 1px; }
    }
  </style>
</head>
<body>
  <header>
    <div>
      <h1>轻量化定时备份</h1>
      <div class="sub">Linux Server Web 控制台</div>
    </div>
    <div class="status">
      <span class="pill" id="runStatus">待机中</span>
      <span id="paths"></span>
      <button id="reloadBtn">刷新</button>
      <button class="danger" id="stopBtn">停止</button>
    </div>
  </header>
  <main>
    <section>
      <div class="section-head">
        <h2>备份任务</h2>
        <div class="grow"></div>
        <button id="addTaskBtn">新建</button>
        <button id="saveTasksBtn">保存设置</button>
        <button class="primary" id="runEnabledBtn">立即执行</button>
      </div>
      <div class="table-wrap">
        <table class="tasks">
          <thead>
            <tr>
              <th>开</th><th>日</th><th>任务</th><th>时间</th><th>源目录</th><th>目的地</th><th>操作</th>
            </tr>
          </thead>
          <tbody id="taskRows"></tbody>
        </table>
      </div>
    </section>
    <section>
      <div class="section-head">
        <h2>目的地配置</h2>
        <div class="grow"></div>
        <button id="newLocalBtn">本地</button>
        <button id="newFtpBtn">FTP</button>
        <button id="newWebdavBtn">WebDAV</button>
        <button id="newS3Btn">S3</button>
      </div>
      <div class="table-wrap">
        <table class="destinations">
          <thead><tr><th>名称</th><th>协议</th><th>地址 / 区域</th><th>目录</th></tr></thead>
          <tbody id="destinationRows"></tbody>
        </table>
      </div>
      <div class="editor">
        <label for="destinationFile">配置文件</label>
        <input id="destinationFile" placeholder="例如 ftp1.json">
        <textarea id="destinationJSON" spellcheck="false"></textarea>
        <div></div>
        <div>
          <button id="saveDestinationBtn">保存目的地</button>
          <button class="danger" id="deleteDestinationBtn">删除目的地</button>
        </div>
      </div>
    </section>
    <section class="log">
      <div class="section-head">
        <h2>运行日志</h2>
        <div class="grow"></div>
        <button id="clearViewBtn">清空显示</button>
      </div>
      <div class="log-list" id="logRows"></div>
    </section>
  </main>
  <script>
    const state = { tasks: [], destinations: [], selectedDestination: -1 };
    const $ = (id) => document.getElementById(id);

    function destinationOptions(selected) {
      return state.destinations.map((d) => {
        const value = escapeHTML(d.name || "");
        const isSelected = d.name === selected ? " selected" : "";
        return "<option value=\"" + value + "\"" + isSelected + ">" + value + "</option>";
      }).join("");
    }

    function escapeHTML(value) {
      return String(value ?? "").replace(/[&<>"']/g, (ch) => ({
        "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
      }[ch]));
    }

    function readTasksFromTable() {
      return Array.from(document.querySelectorAll("#taskRows tr")).map((row) => ({
        enabled: row.querySelector("[data-field=enabled]").checked,
        date_folder: row.querySelector("[data-field=date_folder]").checked,
        name: row.querySelector("[data-field=name]").value.trim(),
        time: row.querySelector("[data-field=time]").value.trim(),
        source_path: row.querySelector("[data-field=source_path]").value.trim(),
        destination: row.querySelector("[data-field=destination]").value
      }));
    }

    function render() {
      $("runStatus").textContent = state.current_task ? "执行中：" + state.current_task : "待机中";
      $("paths").textContent = "数据目录 " + (state.data_dir || "") + " · rclone " + (state.rclone_path || "");
      renderTasks();
      renderDestinations();
      renderLogs();
    }

    function renderTasks() {
      if (!state.tasks.length) {
        $("taskRows").innerHTML = "<tr><td colspan=\"7\" class=\"empty\">还没有任务</td></tr>";
        return;
      }
      $("taskRows").innerHTML = state.tasks.map((task, index) =>
        "<tr>" +
          "<td><input data-field=\"enabled\" type=\"checkbox\" " + (task.enabled ? "checked" : "") + "></td>" +
          "<td><input data-field=\"date_folder\" type=\"checkbox\" " + (task.date_folder ? "checked" : "") + "></td>" +
          "<td><input data-field=\"name\" value=\"" + escapeHTML(task.name) + "\"></td>" +
          "<td><input data-field=\"time\" value=\"" + escapeHTML(task.time) + "\"></td>" +
          "<td><input data-field=\"source_path\" value=\"" + escapeHTML(task.source_path) + "\"></td>" +
          "<td><select data-field=\"destination\">" + destinationOptions(task.destination) + "</select></td>" +
          "<td>" +
            "<button class=\"small\" data-run=\"" + index + "\">测试</button> " +
            "<button class=\"small danger\" data-delete=\"" + index + "\">删除</button>" +
          "</td>" +
        "</tr>"
      ).join("");
      document.querySelectorAll("[data-run]").forEach((button) => {
        button.addEventListener("click", async () => {
          await saveTasks(false);
          await postJSON("/api/run", { indexes: [Number(button.dataset.run)] });
          await refresh();
        });
      });
      document.querySelectorAll("[data-delete]").forEach((button) => {
        button.addEventListener("click", () => {
          state.tasks = readTasksFromTable();
          state.tasks.splice(Number(button.dataset.delete), 1);
          renderTasks();
        });
      });
    }

    function renderDestinations() {
      if (!state.destinations.length) {
        $("destinationRows").innerHTML = "<tr><td colspan=\"4\" class=\"empty\">configs 目录中还没有目的地</td></tr>";
        return;
      }
      $("destinationRows").innerHTML = state.destinations.map((destination, index) =>
        "<tr class=\"" + (index === state.selectedDestination ? "selected" : "") + "\" data-destination=\"" + index + "\">" +
          "<td>" + escapeHTML(destination.name) + "</td>" +
          "<td>" + escapeHTML(destination.type) + "</td>" +
          "<td title=\"" + escapeHTML(destination.address) + "\">" + escapeHTML(destination.address) + "</td>" +
          "<td title=\"" + escapeHTML(destination.path) + "\">" + escapeHTML(destination.path) + "</td>" +
        "</tr>"
      ).join("");
      document.querySelectorAll("[data-destination]").forEach((row) => {
        row.addEventListener("click", () => selectDestination(Number(row.dataset.destination)));
      });
      if (state.selectedDestination < 0 && state.destinations.length) {
        selectDestination(0);
      }
    }

    function selectDestination(index) {
      state.selectedDestination = index;
      const destination = state.destinations[index];
      $("destinationFile").value = destination?.file_name || "";
      $("destinationJSON").value = destination?.raw ? JSON.stringify(destination.raw, null, 2) : "";
      document.querySelectorAll("[data-destination]").forEach((row) => {
        row.classList.toggle("selected", Number(row.dataset.destination) === index);
      });
    }

    function renderLogs() {
      if (!state.logs.length) {
        $("logRows").innerHTML = "<div class=\"empty\">暂无日志</div>";
        return;
      }
      $("logRows").innerHTML = state.logs.map((entry) =>
        "<div class=\"log-row\">" +
          "<div class=\"time\">" + escapeHTML(entry.time) + "</div>" +
          "<div class=\"task\">" + escapeHTML(entry.task) + "</div>" +
          "<div class=\"message\">" + escapeHTML(entry.message) + "</div>" +
        "</div>"
      ).join("");
      $("logRows").scrollTop = $("logRows").scrollHeight;
    }

    async function refresh() {
      const response = await fetch("/api/state");
      if (!response.ok) throw new Error(await response.text());
      const payload = await response.json();
      state.tasks = payload.tasks || [];
      state.destinations = payload.destinations || [];
      state.logs = payload.logs || [];
      state.current_task = payload.current_task;
      state.rclone_path = payload.rclone_path;
      state.data_dir = payload.data_dir;
      if (state.selectedDestination >= state.destinations.length) state.selectedDestination = -1;
      render();
    }

    async function postJSON(url, body) {
      const response = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body)
      });
      if (!response.ok) throw new Error(await response.text());
      return response.json();
    }

    async function saveTasks(showAlert = true) {
      state.tasks = readTasksFromTable();
      await postJSON("/api/tasks", { tasks: state.tasks });
      if (showAlert) alert("任务已保存");
    }

    function destinationTemplate(type) {
      const remote = type === "ftp" ? "ftp_backup" : type === "webdav" ? "webdav_backup" : "s3_backup";
      const prefix = "RCLONE_CONFIG_" + remote.toUpperCase() + "_";
      if (type === "local") {
        return {
          name: "本地备份",
          type: "local",
          local_path: "/data/backups",
          copy: { mode: "copy", create_if_missing: true, overwrite: true }
        };
      }
      if (type === "ftp") {
        return {
          name: "FTP",
          type: "ftp",
          remote_path: "backups/001",
          rclone: {
            remote,
            env: {
              [prefix + "TYPE"]: "ftp",
              [prefix + "HOST"]: "127.0.0.1",
              [prefix + "USER"]: "backup_user",
              [prefix + "PASS"]: "your_obscured_password",
              [prefix + "PORT"]: "21"
            },
            flags: []
          }
        };
      }
      if (type === "webdav") {
        return {
          name: "WebDAV",
          type: "webdav",
          remote_path: "backup/project-a",
          rclone: {
            remote,
            env: {
              [prefix + "TYPE"]: "webdav",
              [prefix + "URL"]: "https://dav.example.com/remote.php/dav/files/your_user",
              [prefix + "VENDOR"]: "other",
              [prefix + "USER"]: "backup_user",
              [prefix + "PASS"]: "your_obscured_password"
            },
            flags: []
          }
        };
      }
      return {
        name: "S3",
        type: "awss3",
        remote_path: "your-bucket/backups/project-a",
        rclone: {
          remote,
          env: {
            [prefix + "TYPE"]: "s3",
            [prefix + "PROVIDER"]: "AWS",
            [prefix + "ACCESS_KEY_ID"]: "",
            [prefix + "SECRET_ACCESS_KEY"]: "",
            [prefix + "REGION"]: "ap-east-1",
            [prefix + "ACL"]: "private"
          },
          flags: []
        }
      };
    }

    $("addTaskBtn").addEventListener("click", () => {
      state.tasks = readTasksFromTable();
      const number = state.tasks.length + 1;
      state.tasks.push({
        name: "任务 " + number,
        enabled: false,
        date_folder: true,
        time: "23:55:55",
        source_path: "/data/source",
        destination: state.destinations[0]?.name || ""
      });
      renderTasks();
    });
    $("saveTasksBtn").addEventListener("click", () => saveTasks().catch((err) => alert(err.message)));
    $("runEnabledBtn").addEventListener("click", async () => {
      try {
        await saveTasks(false);
        await postJSON("/api/run", { enabled: true });
        await refresh();
      } catch (err) { alert(err.message); }
    });
    $("stopBtn").addEventListener("click", async () => {
      try { await postJSON("/api/stop", {}); await refresh(); } catch (err) { alert(err.message); }
    });
    $("reloadBtn").addEventListener("click", async () => {
      try { await postJSON("/api/reload", {}); await refresh(); } catch (err) { alert(err.message); }
    });
    $("clearViewBtn").addEventListener("click", () => { state.logs = []; renderLogs(); });
    $("newLocalBtn").addEventListener("click", () => {
      state.selectedDestination = -1;
      $("destinationFile").value = "local.json";
      $("destinationJSON").value = JSON.stringify(destinationTemplate("local"), null, 2);
    });
    $("newFtpBtn").addEventListener("click", () => {
      state.selectedDestination = -1;
      $("destinationFile").value = "ftp.json";
      $("destinationJSON").value = JSON.stringify(destinationTemplate("ftp"), null, 2);
    });
    $("newWebdavBtn").addEventListener("click", () => {
      state.selectedDestination = -1;
      $("destinationFile").value = "webdav.json";
      $("destinationJSON").value = JSON.stringify(destinationTemplate("webdav"), null, 2);
    });
    $("newS3Btn").addEventListener("click", () => {
      state.selectedDestination = -1;
      $("destinationFile").value = "s3.json";
      $("destinationJSON").value = JSON.stringify(destinationTemplate("s3"), null, 2);
    });
    $("saveDestinationBtn").addEventListener("click", async () => {
      try {
        const content = JSON.parse($("destinationJSON").value);
        await postJSON("/api/destinations", { file_name: $("destinationFile").value.trim(), content });
        state.selectedDestination = -1;
        await refresh();
        alert("目的地已保存");
      } catch (err) { alert(err.message); }
    });
    $("deleteDestinationBtn").addEventListener("click", async () => {
      const file = $("destinationFile").value.trim();
      if (!file || !confirm("删除 " + file + "？")) return;
      try {
        const response = await fetch("/api/destinations/" + encodeURIComponent(file), { method: "DELETE" });
        if (!response.ok) throw new Error(await response.text());
        state.selectedDestination = -1;
        await refresh();
      } catch (err) { alert(err.message); }
    });
    refresh().catch((err) => alert(err.message));
    setInterval(() => refresh().catch(() => {}), 5000);
  </script>
</body>
</html>`
