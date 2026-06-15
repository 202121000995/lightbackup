# 轻量化定时备份 LightBackup

面向 Windows 7 / Windows Server 2008 R2 的绿色版定时备份工具，同时新增 Linux Server Web 版。

Windows 桌面界面使用 Qt Widgets 编写，Linux 服务器版使用浏览器管理。实际复制和上传都由 `rclone` 完成。每个 JSON 配置文件代表一个独立备份目的地，所以同一协议可以配置多个不同 IP、区域或 Endpoint。

## 下载运行

Linux Server Web 版一条命令安装：

```bash
curl -fsSL https://github.com/202121000995/lightbackup/releases/latest/download/install-linux.sh | sudo bash
```

更新 Linux Server Web 版：

```bash
curl -fsSL https://github.com/202121000995/lightbackup/releases/latest/download/update-linux.sh | sudo bash
```

如果服务器访问 GitHub 很慢，可以给下载地址套一层 GitHub 加速代理，并把同一个代理传给安装脚本：

```bash
curl -fsSL https://gh-proxy.com/https://github.com/202121000995/lightbackup/releases/latest/download/install-linux.sh | sudo GITHUB_PROXY=https://gh-proxy.com/ bash
```

更新时同理：

```bash
curl -fsSL https://gh-proxy.com/https://github.com/202121000995/lightbackup/releases/latest/download/update-linux.sh | sudo GITHUB_PROXY=https://gh-proxy.com/ bash
```

代理域名不是固定要求；如果你的网络里 `gh-proxy.com` 不可用，可以替换成自己可用的 GitHub 加速服务。安装完成后浏览器打开：

```text
http://服务器IP:8080
```

安装脚本会在缺少 `rclone` 时尝试通过系统包管理器安装。若你要手动管理 rclone，可以先安装好 rclone，或把 rclone 二进制放到 `/opt/lightbackup/rclone`；也可以用 `INSTALL_RCLONE=0` 禁止自动安装。

Windows 7 绿色版发布包位于：

```text
release-ready/LightBackup-Win7-x86.zip
```

解压后运行：

```text
LightBackup/LightBackup.exe
```

注意：必须保留整个 `LightBackup` 目录，不能只复制 EXE。

## 已支持功能

Windows 桌面版：

- 任务列表新增、删除、保存
- 任务选择后自动读取到下方设置区域
- 任务配置持久化到 `tasks.json`
- 定时执行，时间格式为 `HH:mm:ss`
- 立即执行所有已开启任务
- 测试当前选中任务
- 可按日期目录保留历史版本，例如 `2026-06-11/任务 1/`
- 执行中可点击状态按钮停止当前任务并清空等待队列
- 运行日志显示、清空、打开日志目录
- 目的地配置库刷新、打开目录、新建、编辑
- 右侧选择目的地后自动带入当前任务的目的地下拉框
- 双击目的地配置行可直接编辑常用连接参数
- 目的地编辑窗口会按协议只显示需要填写的参数，避免混填
- 内置 `rclone.exe`
- 支持本地复制、FTP、WebDAV、Cloudflare R2、AWS S3、七牛对象存储

Linux Server Web 版：

- 浏览器管理任务列表、定时时间、源目录、目的地和按日期目录
- 浏览器查看、保存、新建和删除 `configs/*.json` 目的地配置
- 立即执行所有已开启任务，或单独测试某个任务
- 执行中可停止当前任务并清空等待队列
- 常驻进程内置定时器，适合 systemd 托管
- 运行日志写入 `logs/yyyy-mm-dd.log`，网页保留最近 500 行
- 继续使用同一套 `tasks.json` 和 `configs/*.json`

## 目录结构

```text
LightBackup.exe            主程序
rclone.exe                 实际复制/上传程序
configs/                   目的地配置文件
tasks.json                 任务配置
logs/                      运行日志，首次运行后生成
platforms/                 Qt 平台插件
fonts/                     界面字体
*.dll                      Qt/MinGW 运行组件
style.qss                  界面样式
README.txt                 用户说明
```

## 目的地配置

`configs` 目录中每个 JSON 文件就是一个目的地：

- `ftp1.json`、`ftp2.json`：两个 FTP 示例
- `qiniu-east.json`、`qiniu-south.json`：两个七牛区域示例
- `awss3.example.json`：AWS S3 示例
- `cloudflare-r2.example.json`：Cloudflare R2 示例
- `webdav.example.json`：WebDAV 示例
- `local.example.json`：本地复制示例

同一协议配置多个目的地时，必须分别使用不同的：

- 文件名
- `name`
- `rclone.remote`
- `RCLONE_CONFIG_...` 环境变量前缀

每个示例配置都带有 `_important` 和 `_unique_rules` 字段，方便忘记规则时直接看配置文件。
切换协议时，编辑窗口只显示当前协议需要的字段：本地只显示本地目录，FTP 只显示主机、端口、用户和密码，WebDAV 只显示 URL、用户、密码和 Vendor，对象存储只显示 Bucket/目录、区域或 Endpoint、Access Key 和 Secret Key。

## 密码

FTP 和 WebDAV 的 `PASS` 建议填写 rclone 加密后的字符串。在 Windows 命令提示符中进入软件目录执行：

```bat
rclone.exe obscure "你的真实密码"
```

把输出填入配置中的 `RCLONE_CONFIG_..._PASS`。

## 构建

当前 Windows 版使用：

- C++ / Qt 5.12.12 Widgets
- Zig C++ 交叉编译器
- Windows 32 位 PE
- 目标兼容 Windows 7 / Windows Server 2008 R2

本地构建脚本：

```bash
scripts/build-win7.sh
```

该脚本依赖本机已有的 `tools/Qt/5.12.12`、`tools/zig-macos-aarch64-0.11.0` 和 `tools/rclone-v1.62.2-windows-386/rclone.exe`。这些大体积工具链不会提交到仓库。

Linux Server Web 版使用 Go 标准库，无第三方依赖：

```bash
scripts/build-linux-server.sh
```

生成文件：

```text
release-linux/lightbackup-server
```

服务器上准备目录，例如：

```bash
sudo mkdir -p /opt/lightbackup/configs /opt/lightbackup/logs
sudo cp -r configs/*.json /opt/lightbackup/configs/
sudo cp packaging/tasks.json /opt/lightbackup/tasks.json
sudo cp release-linux/lightbackup-server /usr/local/bin/
```

确保服务器已安装 `rclone`，或把 rclone 二进制放到 `/opt/lightbackup/rclone`。启动：

```bash
lightbackup-server -addr :8080 -data-dir /opt/lightbackup
```

浏览器打开：

```text
http://服务器IP:8080
```

公网或多人环境建议加登录参数：

```bash
lightbackup-server -addr :8080 -data-dir /opt/lightbackup -user admin -pass "请改成强密码"
```

systemd 示例位于：

```text
packaging/lightbackup-server.service
```

## Win7 兼容处理

构建后会执行：

- `tools/patch_win7_import.py`：把不兼容的 `GetSystemTimePreciseAsFileTime` 静态入口替换为 Win7 可用入口
- `tools/embed_icon.py`：写入 EXE 图标资源

## 注意

当前使用 rclone `copy` 模式：只复制新增或变更文件，不删除目的地中多余文件。
