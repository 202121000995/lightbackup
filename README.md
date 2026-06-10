# 轻量化定时备份 LightBackup

面向 Windows 7 / Windows Server 2008 R2 的绿色版定时备份工具。

界面使用 Qt Widgets 编写，实际复制和上传由随包的 `rclone.exe` 完成。每个 JSON 配置文件代表一个独立备份目的地，所以同一协议可以配置多个不同 IP、区域或 Endpoint。

## 下载运行

发布包位于：

```text
release-ready/LightBackup-Win7-x86.zip
```

解压后运行：

```text
LightBackup/LightBackup.exe
```

注意：必须保留整个 `LightBackup` 目录，不能只复制 EXE。

## 已支持功能

- 任务列表新增、删除、保存
- 任务选择后自动读取到下方设置区域
- 任务配置持久化到 `tasks.json`
- 定时执行，时间格式为 `HH:mm:ss`
- 立即执行所有已开启任务
- 测试当前选中任务
- 执行中可点击状态按钮停止当前任务并清空等待队列
- 运行日志显示、清空、打开日志目录
- 目的地配置库刷新、打开目录、新建模板
- 右侧选择目的地后自动带入当前任务的目的地下拉框
- 双击目的地配置行可打开对应 JSON 文件
- 内置 `rclone.exe`
- 支持本地复制、FTP、WebDAV、Cloudflare R2、AWS S3、七牛对象存储

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

## Win7 兼容处理

构建后会执行：

- `tools/patch_win7_import.py`：把不兼容的 `GetSystemTimePreciseAsFileTime` 静态入口替换为 Win7 可用入口
- `tools/embed_icon.py`：写入 EXE 图标资源

## 注意

当前使用 rclone `copy` 模式：只复制新增或变更文件，不删除目的地中多余文件。
