轻量化定时备份 - Windows 7 绿色版
================================

一、运行环境

1. 支持 Windows 7、Windows Server 2008 R2 及更高版本。
2. 直接运行 LightBackup.exe，不需要安装 Qt、VC 运行库或系统补丁。
3. 必须保留整个软件目录，不能只复制 EXE。
4. 建议把软件解压到普通可写目录，例如 D:\LightBackup。

二、第一次测试

1. 打开 configs\local.example.json，把 local_path 改成备份目标文件夹。
2. 启动软件，选择一条任务。
3. 点击“选择”，选择需要备份的源文件夹。
4. 在“备份目的地”中选择“本地备份”。
5. 点击任务区域的“保存”，再点击“测试”。
6. 界面下方显示执行过程，完整日志保存在 logs 目录。

三、任务规则

1. tasks.json 保存所有任务；界面修改后会自动写入。
2. “测试”只执行当前选中的任务，不要求任务处于开启状态。
3. “立即执行”执行所有已经开启的任务。
4. 开启任务后，软件保持运行，到达 HH:mm:ss 指定时间会自动执行。
5. 软件退出后不会在后台运行。
6. 执行中点击右上角状态按钮，可以停止当前任务并清空等待队列。
7. 勾选“按日期目录”后，备份会进入 2026-06-11\任务名 这样的日期目录，避免覆盖固定目录。

四、目的地配置

configs 目录中的每一个 JSON 文件代表一个独立目的地。

- ftp1.json、ftp2.json：两个不同 FTP 示例
- qiniu-east.json、qiniu-south.json：两个不同七牛区域示例
- awss3.example.json：AWS S3 示例
- cloudflare-r2.example.json：Cloudflare R2 示例
- webdav.example.json：WebDAV 示例
- local.example.json：本地文件夹复制示例

同一协议建立多个目的地时，必须分别使用不同的：

- 文件名
- name
- rclone.remote
- RCLONE_CONFIG_... 环境变量前缀

每个配置文件顶部的 _important 和 _unique_rules 都写有对应说明。
右侧目的地列表选中后，会自动带入当前任务的目的地下拉框；双击可打开配置文件。
点击“新建”或“编辑”可以在界面里修改常用连接参数，保存后会自动写回对应 JSON 文件。
切换协议时，编辑窗口只显示当前协议需要的字段，避免把 FTP、WebDAV、S3、七牛等参数混填。

五、密码

FTP 和 WebDAV 的 PASS 建议填写 rclone 加密后的字符串。
在命令提示符中进入软件目录，执行：

    rclone.exe obscure "你的真实密码"

把输出内容填入配置文件的 PASS 字段。
S3、七牛和 R2 的密钥按各配置文件中的字段填写。

六、目录说明

- LightBackup.exe：主程序
- rclone.exe：实际复制和上传程序
- configs：目的地配置
- tasks.json：任务配置
- logs：运行日志（首次运行后自动创建）
- platforms、fonts、DLL、style.qss：程序运行组件，请勿删除

注意：copy 模式只复制新增或变更文件，不会删除目的地中多余的文件。
