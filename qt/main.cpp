#include <QtWidgets>

static QLabel *label(const QString &text, const char *name = nullptr) {
    QLabel *value = new QLabel(text);
    if (name) value->setObjectName(name);
    return value;
}

static void addShadow(QWidget *widget, int blur = 16, int y = 4,
                      const QColor &color = QColor(36, 56, 82, 22)) {
    auto effect = new QGraphicsDropShadowEffect(widget);
    effect->setBlurRadius(blur);
    effect->setOffset(0, y);
    effect->setColor(color);
    widget->setGraphicsEffect(effect);
}

class StableItemDelegate : public QStyledItemDelegate {
public:
    explicit StableItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        if (!option.state.testFlag(QStyle::State_Selected)) {
            QStyleOptionViewItem stable(option);
            stable.state &= ~QStyle::State_HasFocus;
            QStyledItemDelegate::paint(painter, stable, index);
            return;
        }

        QStyleOptionViewItem stable(option);
        initStyleOption(&stable, index);
        painter->save();
        painter->fillRect(stable.rect, QColor("#91c7f2"));
        painter->setPen(QColor("#0d365d"));
        painter->setFont(stable.font);
        const QRect textRect = stable.rect.adjusted(7, 0, -7, 0);
        const QString text = stable.fontMetrics.elidedText(
            stable.text, Qt::ElideRight, textRect.width());
        painter->drawText(textRect, stable.displayAlignment | Qt::AlignVCenter, text);
        painter->restore();
    }
};

static QString appFontFamily(const QString &appDir) {
    const QFontDatabase db;
    const QStringList preferred = {
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("Microsoft YaHei")
    };
    for (const QString &family : preferred) {
        if (db.families().contains(family)) return family;
    }

    const QStringList bundledFonts = {
        appDir + "/fonts/NotoSansCJKsc-Regular.otf",
        appDir + "/fonts/NotoSansSC-Regular.otf"
    };
    for (const QString &fontPath : bundledFonts) {
        const int id = QFontDatabase::addApplicationFont(fontPath);
        if (id >= 0) {
            const QStringList families = QFontDatabase::applicationFontFamilies(id);
            if (!families.isEmpty()) return families.first();
        }
    }

    if (db.families().contains(QStringLiteral("SimSun"))) return QStringLiteral("SimSun");
    return QApplication::font().family();
}

class LightBackupWindow : public QWidget {
public:
    explicit LightBackupWindow(const QString &fontFamily) {
        Q_UNUSED(fontFamily);
        appDir = QCoreApplication::applicationDirPath();
        setWindowTitle(QStringLiteral("轻量化定时备份"));
        QIcon windowIcon(appDir + "/LightBackup.png");
        windowIcon.addFile(appDir + "/LightBackup.ico");
        setWindowIcon(windowIcon);
        loadDestinationConfigs();
        loadTasks();
        resize(1040, 800);
        setMinimumSize(860, 720);

        QFile qss(appDir + "/style.qss");
        if (qss.open(QFile::ReadOnly)) setStyleSheet(QString::fromUtf8(qss.readAll()));

        auto root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto header = new QFrame;
        header->setObjectName("Header");
        header->setFixedHeight(70);
        addShadow(header, 10, 2, QColor(36, 56, 82, 13));
        auto headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(24, 10, 24, 10);
        auto icon = new QLabel;
        icon->setObjectName("HeaderIcon");
        icon->setFixedSize(36, 36);
        icon->setScaledContents(true);
        icon->setPixmap(QPixmap(appDir + "/LightBackup.png"));
        headerLayout->addWidget(icon);
        auto titleBox = new QVBoxLayout;
        titleBox->setSpacing(2);
        titleBox->addWidget(label(QStringLiteral("轻量化定时备份"), "Title"));
        titleBox->addWidget(label(QStringLiteral("任务选择目的地配置，支持同协议多实例"), "Subtitle"));
        headerLayout->addLayout(titleBox);
        headerLayout->addStretch();
        auto saveAll = new QPushButton(QStringLiteral("保存设置"));
        auto runAll = new QPushButton(QStringLiteral("立即执行"));
        statusButton = new QPushButton(QStringLiteral("待机中"));
        saveAll->setObjectName("HeaderButton");
        runAll->setObjectName("PrimaryButton");
        statusButton->setObjectName("StatusPill");
        saveAll->setFixedSize(116, 36);
        runAll->setFixedSize(116, 36);
        statusButton->setFixedSize(80, 32);
        statusButton->setFocusPolicy(Qt::NoFocus);
        addShadow(saveAll, 10, 2, QColor(36, 56, 82, 15));
        addShadow(runAll, 12, 3, QColor(37, 99, 235, 26));
        headerLayout->addWidget(saveAll);
        headerLayout->addWidget(runAll);
        auto divider = new QFrame;
        divider->setObjectName("TopDivider");
        divider->setFixedSize(1, 28);
        headerLayout->addWidget(divider);
        headerLayout->addWidget(statusButton);
        root->addWidget(header);
        connect(statusButton, &QPushButton::clicked, this, [=]() {
            if (!currentProcess) {
                QMessageBox::information(this, QStringLiteral("运行状态"),
                                         QStringLiteral("当前没有正在执行的任务。"));
                return;
            }
            if (QMessageBox::question(
                    this, QStringLiteral("停止任务"),
                    QStringLiteral("确定停止当前任务，并清空后续等待任务吗？"),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No) != QMessageBox::Yes) return;
            runQueue.clear();
            currentProcess->kill();
            appendLog(QStringLiteral("系统"), QStringLiteral("已请求停止当前任务"));
        });

        auto body = new QVBoxLayout;
        body->setContentsMargins(14, 12, 14, 12);
        body->setSpacing(12);
        root->addLayout(body);

        auto split = new QHBoxLayout;
        split->setSpacing(12);
        body->addLayout(split, 1);
        split->addWidget(taskCard(), 47);
        split->addWidget(destCard(), 53);
        body->addWidget(logCard());

        connect(saveAll, &QPushButton::clicked, this, [=]() {
            if (taskTable && taskTable->currentRow() >= 0) {
                if (!saveCurrentTask(false)) return;
            } else if (!saveTasks()) {
                QMessageBox::warning(this, QStringLiteral("保存设置"),
                                     QStringLiteral("保存任务配置失败。"));
                return;
            }
            appendLog(QStringLiteral("设置"), QStringLiteral("任务设置已保存"));
            saveAll->setText(QStringLiteral("已保存"));
            saveAll->setProperty("saved", true);
            saveAll->style()->unpolish(saveAll);
            saveAll->style()->polish(saveAll);
            statusButton->setText(QStringLiteral("已保存"));
            QTimer::singleShot(1200, this, [=]() {
                saveAll->setText(QStringLiteral("保存设置"));
                saveAll->setProperty("saved", false);
                saveAll->style()->unpolish(saveAll);
                saveAll->style()->polish(saveAll);
                if (!currentProcess) statusButton->setText(QStringLiteral("待机中"));
            });
        });
        connect(runAll, &QPushButton::clicked, this, [=]() {
            QList<int> enabledTasks;
            for (int i = 0; i < tasks.size(); ++i) {
                if (tasks.at(i).enabled) enabledTasks.append(i);
            }
            if (enabledTasks.isEmpty()) {
                QMessageBox::information(this, QStringLiteral("立即执行"),
                                         QStringLiteral("没有已开启的任务。"));
                return;
            }
            queueTasks(enabledTasks);
        });

        auto scheduleTimer = new QTimer(this);
        scheduleTimer->setInterval(500);
        connect(scheduleTimer, &QTimer::timeout, this, [=]() { checkSchedule(); });
        scheduleTimer->start();

        QTimer::singleShot(0, this, [=]() {
            appendLog(QStringLiteral("系统"), QStringLiteral("程序已启动"));
            appendLog(QStringLiteral("配置"),
                      QStringLiteral("已加载 %1 个目的地、%2 个任务")
                          .arg(destinations.size()).arg(tasks.size()));
        });
    }

private:
    struct Destination {
        QString name;
        QString type;
        QString address;
        QString path;
        QString filePath;
        QString remote;
        QString localPath;
        QJsonObject environment;
        QStringList flags;
    };

    struct Task {
        QString name;
        bool enabled = false;
        bool dateFolder = true;
        QString time = QStringLiteral("23:55:55");
        QString sourcePath;
        QString destination;
    };

    QString appDir;
    QList<Destination> destinations;
    QList<Task> tasks;
    QList<int> runQueue;
    QSet<QString> triggeredTimes;
    QTableWidget *taskTable = nullptr;
    QTableWidget *destinationTable = nullptr;
    QTableWidget *logTable = nullptr;
    QComboBox *destinationCombo = nullptr;
    QCheckBox *enabledCheck = nullptr;
    QCheckBox *dateFolderCheck = nullptr;
    QLineEdit *timeEdit = nullptr;
    QLineEdit *pathEdit = nullptr;
    QPushButton *statusButton = nullptr;
    QProcess *currentProcess = nullptr;
    QByteArray processOutput;
    int currentTaskIndex = -1;

    QString remoteEnvValue(const QJsonObject &root, const QString &suffix) const {
        const QJsonObject env = root.value("rclone").toObject().value("env").toObject();
        for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
            if (it.key().endsWith("_" + suffix, Qt::CaseInsensitive))
                return it.value().toString();
        }
        return QString();
    }

    QString destinationEnvValue(const Destination &destination, const QString &suffix) const {
        for (auto it = destination.environment.constBegin();
             it != destination.environment.constEnd(); ++it) {
            if (it.key().endsWith("_" + suffix, Qt::CaseInsensitive))
                return it.value().toString();
        }
        return QString();
    }

    QString envPrefix(const QString &remote) const {
        QString result;
        for (const QChar ch : remote.toUpper()) {
            if (ch.isLetterOrNumber())
                result.append(ch);
            else
                result.append('_');
        }
        while (result.contains("__")) result.replace("__", "_");
        result = result.trimmed();
        if (result.startsWith('_')) result.remove(0, 1);
        if (result.endsWith('_')) result.chop(1);
        return result.isEmpty() ? QStringLiteral("BACKUP") : result;
    }

    QString fileSafeName(const QString &text) const {
        QString result = text.trimmed().toLower();
        result.replace(QRegularExpression("[^a-z0-9\\u4e00-\\u9fa5_-]+"), "-");
        while (result.contains("--")) result.replace("--", "-");
        if (result.startsWith('-')) result.remove(0, 1);
        if (result.endsWith('-')) result.chop(1);
        return result.isEmpty() ? QStringLiteral("destination") : result;
    }

    QString pathSafeSegment(const QString &text) const {
        QString result = text.trimmed();
        result.replace(QRegularExpression("[\\\\/:*?\"<>|]+"), "_");
        while (result.contains("__")) result.replace("__", "_");
        return result.isEmpty() ? QStringLiteral("task") : result;
    }

    QString appendRemotePath(const QString &base, const QString &child) const {
        QString cleanedBase = base.trimmed();
        QString cleanedChild = child.trimmed();
        while (cleanedBase.endsWith('/')) cleanedBase.chop(1);
        while (cleanedChild.startsWith('/')) cleanedChild.remove(0, 1);
        if (cleanedBase.isEmpty()) return cleanedChild;
        if (cleanedChild.isEmpty()) return cleanedBase;
        return cleanedBase + "/" + cleanedChild;
    }

    void loadDestinationConfigs() {
        destinations.clear();
        QDir configDir(appDir + "/configs");
        const QFileInfoList files = configDir.entryInfoList(
            QStringList() << "*.json", QDir::Files, QDir::Name);
        for (const QFileInfo &fileInfo : files) {
            QFile file(fileInfo.absoluteFilePath());
            if (!file.open(QFile::ReadOnly)) continue;
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) continue;

            const QJsonObject root = document.object();
            const QJsonObject rclone = root.value("rclone").toObject();
            Destination destination;
            destination.name = root.value("name").toString(fileInfo.completeBaseName());
            destination.type = root.value("type").toString().toLower();
            destination.path = root.value("remote_path").toString();
            destination.localPath = root.value("local_path").toString();
            destination.filePath = fileInfo.absoluteFilePath();
            destination.remote = rclone.value("remote").toString();
            destination.environment = rclone.value("env").toObject();
            for (const QJsonValue &flag : rclone.value("flags").toArray())
                destination.flags.append(flag.toString());

            if (destination.type == "ftp")
                destination.address = remoteEnvValue(root, "HOST");
            else if (destination.type == "webdav")
                destination.address = remoteEnvValue(root, "URL");
            else if (destination.type == "awss3")
                destination.address = remoteEnvValue(root, "REGION");
            else if (destination.type == "qiniu" || destination.type == "cfr2")
                destination.address = remoteEnvValue(root, "ENDPOINT");
            else if (destination.type == "local") {
                destination.address = QStringLiteral("本机文件夹");
                destination.path = destination.localPath;
            }

            if (destination.address.startsWith("https://"))
                destination.address.remove(0, 8);
            else if (destination.address.startsWith("http://"))
                destination.address.remove(0, 7);
            while (destination.address.endsWith('/')) destination.address.chop(1);
            if (!destination.name.isEmpty()) destinations.append(destination);
        }
    }

    void loadTasks() {
        tasks.clear();
        QFile file(appDir + "/tasks.json");
        if (file.open(QFile::ReadOnly)) {
            const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
            QJsonArray rows;
            if (document.isArray())
                rows = document.array();
            else
                rows = document.object().value("tasks").toArray();
            for (const QJsonValue &value : rows) {
                const QJsonObject object = value.toObject();
                Task task;
                task.name = object.value("name").toString();
                task.enabled = object.value("enabled").toBool(false);
                task.dateFolder = object.value("date_folder").toBool(true);
                task.time = object.value("time").toString("23:55:55");
                task.sourcePath = object.value("source_path").toString();
                task.destination = object.value("destination").toString();
                if (!task.name.isEmpty()) tasks.append(task);
            }
        }
        if (tasks.isEmpty()) {
            Task first;
            first.name = QStringLiteral("任务 1");
            first.sourcePath = QStringLiteral("C:\\BackupSource");
            if (!destinations.isEmpty()) first.destination = destinations.first().name;
            tasks.append(first);
        }
    }

    bool saveTasks() {
        QJsonArray rows;
        for (const Task &task : tasks) {
            QJsonObject object;
            object.insert("name", task.name);
            object.insert("enabled", task.enabled);
            object.insert("date_folder", task.dateFolder);
            object.insert("time", task.time);
            object.insert("source_path", task.sourcePath);
            object.insert("destination", task.destination);
            rows.append(object);
        }
        QJsonObject root;
        root.insert("_comment", QStringLiteral("任务由程序保存；目的地详细参数位于 configs 目录。"));
        root.insert("tasks", rows);
        QSaveFile file(appDir + "/tasks.json");
        if (!file.open(QFile::WriteOnly)) return false;
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        return file.commit();
    }

    int destinationIndex(const QString &name) const {
        for (int i = 0; i < destinations.size(); ++i) {
            if (destinations.at(i).name == name) return i;
        }
        return -1;
    }

    void populateDestinationControls() {
        if (destinationTable) {
            destinationTable->setRowCount(destinations.size());
            for (int row = 0; row < destinations.size(); ++row) {
                const Destination &destination = destinations.at(row);
                destinationTable->setItem(row, 0, new QTableWidgetItem(destination.name));
                destinationTable->setItem(row, 1, new QTableWidgetItem(destination.type));
                destinationTable->setItem(row, 2, new QTableWidgetItem(destination.address));
                destinationTable->setItem(row, 3, new QTableWidgetItem(destination.path));
            }
        }
        if (destinationCombo) {
            const QString selected = destinationCombo->currentText();
            destinationCombo->clear();
            for (const Destination &destination : destinations)
                destinationCombo->addItem(destination.name);
            const int index = destinationCombo->findText(selected);
            destinationCombo->setCurrentIndex(index);
        }
    }

    void refreshTaskTable(int selectedRow = -1) {
        if (!taskTable) return;
        taskTable->setRowCount(tasks.size());
        for (int row = 0; row < tasks.size(); ++row) {
            const Task &task = tasks.at(row);
            taskTable->setItem(row, 0, new QTableWidgetItem(
                task.enabled ? QStringLiteral("开启") : QStringLiteral("关闭")));
            taskTable->setItem(row, 1, new QTableWidgetItem(task.name));
            taskTable->setItem(row, 2, new QTableWidgetItem(task.time));
            auto pathItem = new QTableWidgetItem(task.sourcePath);
            pathItem->setToolTip(task.sourcePath);
            taskTable->setItem(row, 3, pathItem);
            taskTable->setItem(row, 4, new QTableWidgetItem(
                task.destination.isEmpty() ? QStringLiteral("未选择目的地") : task.destination));
        }
        if (!tasks.isEmpty()) {
            const int row = selectedRow < 0 ? 0 : qBound(0, selectedRow, tasks.size() - 1);
            taskTable->selectRow(row);
        }
    }

    void loadSelectedTask() {
        if (!taskTable || !enabledCheck || !dateFolderCheck ||
            !timeEdit || !pathEdit || !destinationCombo) return;
        const int row = taskTable->currentRow();
        if (row < 0 || row >= tasks.size()) return;
        const Task &task = tasks.at(row);
        enabledCheck->setChecked(task.enabled);
        dateFolderCheck->setChecked(task.dateFolder);
        timeEdit->setText(task.time);
        pathEdit->setText(task.sourcePath);
        destinationCombo->setCurrentIndex(destinationCombo->findText(task.destination));
    }

    bool saveCurrentTask(bool showMessage = true) {
        const int row = taskTable ? taskTable->currentRow() : -1;
        if (row < 0 || row >= tasks.size()) {
            if (showMessage)
                QMessageBox::information(this, QStringLiteral("保存任务"),
                                         QStringLiteral("请先选择一个任务。"));
            return false;
        }
        const QString timeText = timeEdit->text().trimmed();
        if (!QTime::fromString(timeText, "HH:mm:ss").isValid()) {
            QMessageBox::warning(this, QStringLiteral("保存任务"),
                                 QStringLiteral("定时时间格式应为 HH:mm:ss，例如 23:55:55。"));
            return false;
        }
        const QString destinationName = destinationCombo->currentText().trimmed();
        if (destinationName.isEmpty() || destinationIndex(destinationName) < 0) {
            QMessageBox::warning(this, QStringLiteral("保存任务"),
                                 QStringLiteral("请先选择有效的备份目的地。"));
            return false;
        }
        Task &task = tasks[row];
        task.enabled = enabledCheck->isChecked();
        task.dateFolder = dateFolderCheck->isChecked();
        task.time = timeText;
        task.sourcePath = QDir::toNativeSeparators(pathEdit->text().trimmed());
        task.destination = destinationName;
        refreshTaskTable(row);
        saveTasks();
        appendLog(task.name, QStringLiteral("任务设置已保存"));
        return true;
    }

    bool editDestinationDialog(int row) {
        const bool creating = row < 0 || row >= destinations.size();
        Destination initial;
        if (!creating) initial = destinations.at(row);

        QDialog dialog(this);
        dialog.setWindowTitle(creating ? QStringLiteral("新建目的地")
                                       : QStringLiteral("编辑目的地"));
        dialog.resize(520, 430);
        auto layout = new QVBoxLayout(&dialog);
        auto form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignRight);
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(8);

        auto nameEdit = new QLineEdit(creating ? QStringLiteral("新目的地") : initial.name);
        auto typeCombo = new QComboBox;
        typeCombo->addItems(QStringList()
                            << "local" << "ftp" << "webdav" << "awss3" << "cfr2" << "qiniu");
        typeCombo->setCurrentText(creating ? "ftp" : initial.type);
        auto remoteEdit = new QLineEdit(creating ? "new_backup" : initial.remote);
        auto remotePathEdit = new QLineEdit(creating ? "backups/001" : initial.path);
        auto localPathEdit = new QLineEdit(creating ? "D:\\Backups" : initial.localPath);
        auto addressEdit = new QLineEdit(creating ? "192.168.1.10" :
            (initial.type == "ftp" ? destinationEnvValue(initial, "HOST") :
             initial.type == "webdav" ? destinationEnvValue(initial, "URL") :
             initial.type == "awss3" ? destinationEnvValue(initial, "REGION") :
             destinationEnvValue(initial, "ENDPOINT")));
        auto portEdit = new QLineEdit(creating ? "21" : destinationEnvValue(initial, "PORT"));
        auto userEdit = new QLineEdit(creating ? "backup_user" : destinationEnvValue(initial, "USER"));
        auto passEdit = new QLineEdit(creating ? QString() : destinationEnvValue(initial, "PASS"));
        auto accessKeyEdit = new QLineEdit(creating ? QString() :
                                           destinationEnvValue(initial, "ACCESS_KEY_ID"));
        auto secretKeyEdit = new QLineEdit(creating ? QString() :
                                           destinationEnvValue(initial, "SECRET_ACCESS_KEY"));
        auto vendorEdit = new QLineEdit(creating ? "other" : destinationEnvValue(initial, "VENDOR"));

        auto remotePathLabel = new QLabel(QStringLiteral("远端目录"));
        auto localPathLabel = new QLabel(QStringLiteral("本地目录"));
        auto addressLabel = new QLabel(QStringLiteral("地址"));
        auto portLabel = new QLabel(QStringLiteral("端口"));
        auto userLabel = new QLabel(QStringLiteral("用户"));
        auto passLabel = new QLabel(QStringLiteral("密码"));
        auto accessKeyLabel = new QLabel(QStringLiteral("Access Key"));
        auto secretKeyLabel = new QLabel(QStringLiteral("Secret Key"));
        auto vendorLabel = new QLabel(QStringLiteral("WebDAV Vendor"));

        form->addRow(QStringLiteral("名称"), nameEdit);
        form->addRow(QStringLiteral("协议"), typeCombo);
        form->addRow(QStringLiteral("remote 名称"), remoteEdit);
        form->addRow(remotePathLabel, remotePathEdit);
        form->addRow(localPathLabel, localPathEdit);
        form->addRow(addressLabel, addressEdit);
        form->addRow(portLabel, portEdit);
        form->addRow(userLabel, userEdit);
        form->addRow(passLabel, passEdit);
        form->addRow(accessKeyLabel, accessKeyEdit);
        form->addRow(secretKeyLabel, secretKeyEdit);
        form->addRow(vendorLabel, vendorEdit);
        layout->addLayout(form);

        auto hint = new QLabel(QStringLiteral(
            "同协议的多个目的地必须使用不同的名称、remote 名称和环境变量前缀。\n"
            "保存时程序会根据 remote 名称自动生成 RCLONE_CONFIG_ 前缀。"));
        hint->setWordWrap(true);
        hint->setObjectName("HintText");
        layout->addWidget(hint);

        auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        layout->addWidget(buttons);

        auto setRowVisible = [=](QWidget *labelWidget, QWidget *fieldWidget, bool visible) {
            if (labelWidget) labelWidget->setVisible(visible);
            fieldWidget->setVisible(visible);
        };

        auto updateVisibility = [&, initializing = true]() mutable {
            const QString type = typeCombo->currentText();
            const bool local = type == "local";
            const bool ftp = type == "ftp";
            const bool webdav = type == "webdav";
            const bool objectStore = type == "awss3" || type == "cfr2" || type == "qiniu";

            if (creating && !initializing) {
                if (type == "local") {
                    nameEdit->setText(QStringLiteral("本地备份"));
                    remoteEdit->clear();
                    remotePathEdit->clear();
                    localPathEdit->setText("D:\\Backups");
                    addressEdit->clear();
                } else if (type == "ftp") {
                    nameEdit->setText("FTP");
                    remoteEdit->setText("ftp_backup");
                    remotePathEdit->setText("backups/001");
                    addressEdit->setText("192.168.1.10");
                    portEdit->setText("21");
                    userEdit->setText("backup_user");
                    passEdit->clear();
                } else if (type == "webdav") {
                    nameEdit->setText("WebDAV");
                    remoteEdit->setText("webdav_backup");
                    remotePathEdit->setText("backup/project-a");
                    addressEdit->setText("https://dav.example.com/remote.php/dav/files/your_user");
                    userEdit->setText("backup_user");
                    passEdit->clear();
                    vendorEdit->setText("other");
                } else if (type == "awss3") {
                    nameEdit->setText("AWS S3");
                    remoteEdit->setText("aws_s3_backup");
                    remotePathEdit->setText("your-bucket/backups/project-a");
                    addressEdit->setText("ap-east-1");
                    accessKeyEdit->clear();
                    secretKeyEdit->clear();
                } else if (type == "cfr2") {
                    nameEdit->setText("Cloudflare R2");
                    remoteEdit->setText("r2_backup");
                    remotePathEdit->setText("your-bucket/backups/project-a");
                    addressEdit->setText("https://your_account_id.r2.cloudflarestorage.com");
                    accessKeyEdit->clear();
                    secretKeyEdit->clear();
                } else if (type == "qiniu") {
                    nameEdit->setText(QStringLiteral("七牛云"));
                    remoteEdit->setText("qiniu_backup");
                    remotePathEdit->setText("your-bucket/backups/project-a");
                    addressEdit->setText("https://s3-cn-east-1.qiniucs.com");
                    accessKeyEdit->clear();
                    secretKeyEdit->clear();
                }
            }

            remotePathLabel->setText(objectStore ? QStringLiteral("Bucket/目录")
                                                 : QStringLiteral("远端目录"));
            if (ftp) addressLabel->setText(QStringLiteral("FTP 主机"));
            else if (webdav) addressLabel->setText(QStringLiteral("WebDAV URL"));
            else if (type == "awss3") addressLabel->setText(QStringLiteral("AWS 区域"));
            else if (type == "cfr2") addressLabel->setText(QStringLiteral("R2 Endpoint"));
            else if (type == "qiniu") addressLabel->setText(QStringLiteral("七牛 Endpoint"));
            passLabel->setText(webdav ? QStringLiteral("WebDAV 密码") : QStringLiteral("FTP 密码"));

            remoteEdit->setPlaceholderText("例如 ftp_backup、qiniu_east");
            remotePathEdit->setPlaceholderText(objectStore ? "例如 bucket-name/backups/project-a"
                                                           : "例如 backups/project-a");
            localPathEdit->setPlaceholderText("例如 D:\\Backups");
            portEdit->setPlaceholderText("默认 21");
            userEdit->setPlaceholderText(ftp ? "FTP 用户名" : "WebDAV 用户名");
            passEdit->setPlaceholderText("建议填写 rclone obscure 生成的密码");
            accessKeyEdit->setPlaceholderText("对象存储 Access Key");
            secretKeyEdit->setPlaceholderText("对象存储 Secret Key");
            vendorEdit->setPlaceholderText("通常填 other");

            setRowVisible(form->labelForField(remoteEdit), remoteEdit, !local);
            setRowVisible(remotePathLabel, remotePathEdit, !local);
            setRowVisible(localPathLabel, localPathEdit, local);
            setRowVisible(addressLabel, addressEdit, !local);
            setRowVisible(portLabel, portEdit, ftp);
            setRowVisible(userLabel, userEdit, ftp || webdav);
            setRowVisible(passLabel, passEdit, ftp || webdav);
            setRowVisible(accessKeyLabel, accessKeyEdit, objectStore);
            setRowVisible(secretKeyLabel, secretKeyEdit, objectStore);
            setRowVisible(vendorLabel, vendorEdit, webdav);

            initializing = false;
            dialog.adjustSize();
        };
        connect(typeCombo, &QComboBox::currentTextChanged, &dialog, updateVisibility);
        updateVisibility();

        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
            const QString name = nameEdit->text().trimmed();
            const QString type = typeCombo->currentText();
            const QString remote = remoteEdit->text().trimmed();
            if (name.isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                     QStringLiteral("名称不能为空。"));
                return;
            }
            if (type != "local" && remote.isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                     QStringLiteral("非本地目的地必须填写 remote 名称。"));
                return;
            }
            const QString newPrefix = envPrefix(remote);
            for (int i = 0; i < destinations.size(); ++i) {
                if (!creating && i == row) continue;
                const Destination &existing = destinations.at(i);
                if (existing.name == name) {
                    QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                         QStringLiteral("目的地名称不能重复。"));
                    return;
                }
                if (type != "local" && existing.type != "local") {
                    if (existing.remote == remote || envPrefix(existing.remote) == newPrefix) {
                        QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                             QStringLiteral("目的地识别号不能重复。"));
                        return;
                    }
                }
            }
            if (type == "local" && localPathEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                     QStringLiteral("本地目的地必须填写本地目录。"));
                return;
            }
            if (type == "ftp" && addressEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                     QStringLiteral("FTP 必须填写主机地址。"));
                return;
            }
            if (type == "ftp" && userEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                     QStringLiteral("FTP 必须填写用户。"));
                return;
            }
            if (type == "ftp" && passEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                     QStringLiteral("FTP 必须填写密码。"));
                return;
            }
            if (type == "webdav" && addressEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                     QStringLiteral("WebDAV 必须填写 URL。"));
                return;
            }
            if (type == "webdav" && userEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                     QStringLiteral("WebDAV 必须填写用户。"));
                return;
            }
            if (type == "webdav" && passEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                     QStringLiteral("WebDAV 必须填写密码。"));
                return;
            }
            const bool objectStore = type == "awss3" || type == "cfr2" || type == "qiniu";
            if (objectStore && remotePathEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                     QStringLiteral("对象存储必须填写 Bucket/目录。"));
                return;
            }
            if (objectStore && addressEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                     QStringLiteral("对象存储必须填写区域或 Endpoint。"));
                return;
            }
            if (objectStore && accessKeyEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                     QStringLiteral("对象存储必须填写 Access Key。"));
                return;
            }
            if (objectStore && secretKeyEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("保存目的地"),
                                     QStringLiteral("对象存储必须填写 Secret Key。"));
                return;
            }
            dialog.accept();
        });

        if (dialog.exec() != QDialog::Accepted) return false;

        QJsonObject root;
        root.insert("_important", QStringLiteral(
            "同协议多个目的地必须分别使用不同文件、name、rclone.remote 和 RCLONE_CONFIG_前缀。"));
        QJsonArray rules;
        rules.append(QStringLiteral("本文件由界面生成；复制后请修改 name 和 remote。"));
        rules.append(QStringLiteral("环境变量前缀由 remote 自动生成，请勿与其他目的地重复。"));
        rules.append(QStringLiteral("每个目的地应填写自己的地址、账号、密钥和目录。"));
        root.insert("_unique_rules", rules);
        const QString type = typeCombo->currentText();
        root.insert("name", nameEdit->text().trimmed());
        root.insert("type", type);

        if (type == "local") {
            root.insert("local_path", QDir::toNativeSeparators(localPathEdit->text().trimmed()));
            QJsonObject copy;
            copy.insert("mode", "copy");
            copy.insert("create_if_missing", true);
            copy.insert("overwrite", true);
            root.insert("copy", copy);
        } else {
            const QString remote = remoteEdit->text().trimmed();
            const QString prefix = "RCLONE_CONFIG_" + envPrefix(remote) + "_";
            QJsonObject env;
            if (type == "ftp") {
                env.insert(prefix + "TYPE", "ftp");
                env.insert(prefix + "HOST", addressEdit->text().trimmed());
                env.insert(prefix + "USER", userEdit->text().trimmed());
                env.insert(prefix + "PASS", passEdit->text().trimmed());
                env.insert(prefix + "PORT", portEdit->text().trimmed().isEmpty()
                           ? "21" : portEdit->text().trimmed());
            } else if (type == "webdav") {
                env.insert(prefix + "TYPE", "webdav");
                env.insert(prefix + "URL", addressEdit->text().trimmed());
                env.insert(prefix + "VENDOR", vendorEdit->text().trimmed().isEmpty()
                           ? "other" : vendorEdit->text().trimmed());
                env.insert(prefix + "USER", userEdit->text().trimmed());
                env.insert(prefix + "PASS", passEdit->text().trimmed());
            } else {
                env.insert(prefix + "TYPE", "s3");
                env.insert(prefix + "PROVIDER",
                           type == "awss3" ? "AWS" : type == "cfr2" ? "Cloudflare" : "Qiniu");
                env.insert(prefix + "ACCESS_KEY_ID", accessKeyEdit->text().trimmed());
                env.insert(prefix + "SECRET_ACCESS_KEY", secretKeyEdit->text().trimmed());
                if (type == "awss3")
                    env.insert(prefix + "REGION", addressEdit->text().trimmed());
                else
                    env.insert(prefix + "ENDPOINT", addressEdit->text().trimmed());
                env.insert(prefix + "ACL", "private");
            }
            QJsonObject rclone;
            rclone.insert("remote", remote);
            rclone.insert("env", env);
            QJsonArray flags;
            for (const QString &flag : initial.flags) flags.append(flag);
            rclone.insert("flags", flags);
            root.insert("remote_path", remotePathEdit->text().trimmed());
            root.insert("rclone", rclone);
        }

        QDir().mkpath(appDir + "/configs");
        QString filePath = creating ? QString() : initial.filePath;
        if (filePath.isEmpty()) {
            filePath = appDir + "/configs/" + fileSafeName(nameEdit->text()) + ".json";
            int suffix = 2;
            while (QFileInfo::exists(filePath))
                filePath = appDir + QString("/configs/%1-%2.json")
                           .arg(fileSafeName(nameEdit->text())).arg(suffix++);
        }

        QSaveFile file(filePath);
        if (!file.open(QFile::WriteOnly)) {
            QMessageBox::warning(this, QStringLiteral("保存目的地"),
                                 QStringLiteral("无法写入配置文件。"));
            return false;
        }
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        if (!file.commit()) {
            QMessageBox::warning(this, QStringLiteral("保存目的地"),
                                 QStringLiteral("保存配置文件失败。"));
            return false;
        }
        loadDestinationConfigs();
        populateDestinationControls();
        const QString savedName = nameEdit->text().trimmed();
        if (!creating && initial.name != savedName) {
            bool changed = false;
            for (Task &task : tasks) {
                if (task.destination == initial.name) {
                    task.destination = savedName;
                    changed = true;
                }
            }
            if (changed) {
                saveTasks();
                refreshTaskTable(taskTable ? taskTable->currentRow() : -1);
                loadSelectedTask();
                appendLog(QStringLiteral("配置"),
                          QStringLiteral("已同步更新引用该目的地的任务"));
            }
        }
        const int newIndex = destinationIndex(savedName);
        if (destinationTable && newIndex >= 0) destinationTable->selectRow(newIndex);
        appendLog(QStringLiteral("配置"),
                  creating ? QStringLiteral("已新建目的地配置")
                           : QStringLiteral("已保存目的地配置"));
        return true;
    }

    QFrame *card() {
        auto frame = new QFrame;
        frame->setObjectName("Card");
        addShadow(frame, 12, 3, QColor(36, 56, 82, 15));
        return frame;
    }

    QWidget *taskCard() {
        auto frame = card();
        auto layout = new QVBoxLayout(frame);
        layout->setContentsMargins(14, 14, 14, 12);
        layout->setSpacing(10);

        auto top = new QHBoxLayout;
        top->addWidget(label(QStringLiteral("备份任务"), "SectionTitle"));
        top->addStretch();
        auto addTask = new QPushButton(QStringLiteral("+ 新建"));
        auto deleteTask = new QPushButton(QStringLiteral("删除"));
        auto saveTask = new QPushButton(QStringLiteral("保存"));
        addTask->setFixedWidth(70);
        deleteTask->setFixedWidth(70);
        saveTask->setFixedWidth(70);
        top->addWidget(addTask);
        top->addWidget(deleteTask);
        top->addWidget(saveTask);
        layout->addLayout(top);

        taskTable = new QTableWidget(0, 5);
        taskTable->setAlternatingRowColors(true);
        taskTable->setHorizontalHeaderLabels(
            QStringList() << QStringLiteral("状态") << QStringLiteral("任务")
                          << QStringLiteral("时间") << QStringLiteral("备份路径")
                          << QStringLiteral("目的地"));
        taskTable->horizontalHeader()->setStretchLastSection(false);
        taskTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        taskTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        taskTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
        taskTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        taskTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
        taskTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
        taskTable->verticalHeader()->hide();
        taskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        taskTable->setSelectionMode(QAbstractItemView::SingleSelection);
        taskTable->setFocusPolicy(Qt::NoFocus);
        taskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        taskTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        taskTable->setItemDelegate(new StableItemDelegate(taskTable));
        taskTable->setShowGrid(true);
        taskTable->setColumnWidth(0, 64);
        taskTable->setColumnWidth(1, 72);
        taskTable->setColumnWidth(2, 88);
        taskTable->setColumnWidth(4, 82);
        taskTable->verticalHeader()->setDefaultSectionSize(32);
        layout->addWidget(taskTable, 1);

        layout->addWidget(label(QStringLiteral("任务设置"), "SectionTitle"));
        auto form = new QGridLayout;
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(8);
        enabledCheck = new QCheckBox(QStringLiteral("开启备份"));
        form->addWidget(enabledCheck, 0, 0);
        dateFolderCheck = new QCheckBox(QStringLiteral("按日期目录"));
        form->addWidget(dateFolderCheck, 0, 1);
        form->addWidget(label(QStringLiteral("定时")), 0, 2);
        timeEdit = new QLineEdit("23:55:55");
        form->addWidget(timeEdit, 0, 3);
        auto testTask = new QPushButton(QStringLiteral("测试"));
        form->addWidget(testTask, 0, 4);
        form->addWidget(label(QStringLiteral("备份目标路径")), 1, 0);
        pathEdit = new QLineEdit;
        form->addWidget(pathEdit, 1, 1, 1, 3);
        auto choosePath = new QPushButton(QStringLiteral("选择"));
        form->addWidget(choosePath, 1, 4);
        form->addWidget(label(QStringLiteral("备份目的地")), 2, 0);
        destinationCombo = new QComboBox;
        form->addWidget(destinationCombo, 2, 1, 1, 4);
        layout->addLayout(form);
        populateDestinationControls();
        refreshTaskTable();
        loadSelectedTask();

        connect(taskTable, &QTableWidget::itemSelectionChanged,
                this, [=]() { loadSelectedTask(); });
        connect(saveTask, &QPushButton::clicked,
                this, [=]() { saveCurrentTask(); });
        connect(addTask, &QPushButton::clicked, this, [=]() {
            Task task;
            int number = 1;
            QSet<QString> names;
            for (const Task &existing : tasks) names.insert(existing.name);
            do {
                task.name = QStringLiteral("任务 %1").arg(number++);
            } while (names.contains(task.name));
            task.sourcePath = QStringLiteral("C:\\BackupSource");
            if (!destinations.isEmpty()) task.destination = destinations.first().name;
            tasks.append(task);
            refreshTaskTable(tasks.size() - 1);
            saveTasks();
            appendLog(task.name, QStringLiteral("已新建任务"));
        });
        connect(deleteTask, &QPushButton::clicked, this, [=]() {
            if (currentProcess || !runQueue.isEmpty()) {
                QMessageBox::information(this, QStringLiteral("删除任务"),
                                         QStringLiteral("任务执行中不能删除任务，请先停止或等待执行完成。"));
                return;
            }
            const int row = taskTable->currentRow();
            if (row < 0 || row >= tasks.size()) {
                QMessageBox::information(this, QStringLiteral("删除任务"),
                                         QStringLiteral("请先选择要删除的任务。"));
                return;
            }
            const QString name = tasks.at(row).name;
            if (QMessageBox::question(
                    this, QStringLiteral("删除任务"),
                    QStringLiteral("确定删除“%1”吗？").arg(name),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No) != QMessageBox::Yes) return;
            tasks.removeAt(row);
            refreshTaskTable(qMin(row, tasks.size() - 1));
            saveTasks();
            appendLog(name, QStringLiteral("任务已删除"));
        });
        connect(choosePath, &QPushButton::clicked, this, [=]() {
            const QString startPath = pathEdit->text().trimmed();
            const QString selected = QFileDialog::getExistingDirectory(
                this, QStringLiteral("选择需要备份的文件夹"),
                startPath.isEmpty() ? QDir::homePath() : startPath,
                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
            if (!selected.isEmpty())
                pathEdit->setText(QDir::toNativeSeparators(selected));
        });
        connect(testTask, &QPushButton::clicked, this, [=]() {
            if (!saveCurrentTask(false)) return;
            const int row = taskTable->currentRow();
            if (row >= 0) queueTasks(QList<int>() << row);
        });
        return frame;
    }

    QWidget *destCard() {
        auto frame = card();
        auto layout = new QVBoxLayout(frame);
        layout->setContentsMargins(14, 14, 14, 12);
        layout->setSpacing(10);
        auto top = new QHBoxLayout;
        top->addWidget(label(QStringLiteral("目的地配置库"), "SectionTitle"));
        top->addStretch();
        auto addDest = new QPushButton(QStringLiteral("+ 新建"));
        auto editDest = new QPushButton(QStringLiteral("编辑"));
        auto refreshDest = new QPushButton(QStringLiteral("刷新"));
        auto openDest = new QPushButton(QStringLiteral("打开目录"));
        addDest->setFixedWidth(70);
        editDest->setFixedWidth(70);
        refreshDest->setFixedWidth(70);
        openDest->setFixedWidth(92);
        top->addWidget(addDest);
        top->addWidget(editDest);
        top->addWidget(refreshDest);
        top->addWidget(openDest);
        layout->addLayout(top);

        destinationTable = new QTableWidget(0, 4);
        destinationTable->setAlternatingRowColors(true);
        destinationTable->setHorizontalHeaderLabels(
            QStringList() << QStringLiteral("名称") << QStringLiteral("协议")
                          << QStringLiteral("地址 / 区域") << QStringLiteral("远端目录"));
        destinationTable->horizontalHeader()->setStretchLastSection(false);
        destinationTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        destinationTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        destinationTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
        destinationTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        destinationTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
        destinationTable->verticalHeader()->hide();
        destinationTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        destinationTable->setSelectionMode(QAbstractItemView::SingleSelection);
        destinationTable->setFocusPolicy(Qt::NoFocus);
        destinationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        destinationTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        destinationTable->setItemDelegate(new StableItemDelegate(destinationTable));
        destinationTable->setShowGrid(true);
        destinationTable->setColumnWidth(0, 90);
        destinationTable->setColumnWidth(1, 68);
        destinationTable->setColumnWidth(2, 145);
        destinationTable->verticalHeader()->setDefaultSectionSize(32);
        populateDestinationControls();
        layout->addWidget(destinationTable, 1);

        auto hint = new QLabel(QStringLiteral(
            "提示：同一协议的不同 IP、区域、Endpoint 都作为独立目的地配置文件保存。\n"
            "例如：办公 FTP、机房 FTP、七牛华东、七牛华南，都可以被不同任务分别选择。"));
        hint->setWordWrap(true);
        hint->setObjectName("HintText");
        layout->addWidget(hint);

        connect(openDest, &QPushButton::clicked, this, [=]() { openDirectory("configs"); });
        connect(destinationTable, &QTableWidget::itemSelectionChanged, this, [=]() {
            const int row = destinationTable->currentRow();
            if (row < 0 || row >= destinations.size() || !destinationCombo) return;
            const int comboIndex = destinationCombo->findText(destinations.at(row).name);
            if (comboIndex >= 0) destinationCombo->setCurrentIndex(comboIndex);
        });
        connect(destinationTable, &QTableWidget::cellDoubleClicked, this, [=](int row, int) {
            editDestinationDialog(row);
        });
        connect(refreshDest, &QPushButton::clicked, this, [=]() {
            loadDestinationConfigs();
            populateDestinationControls();
            appendLog(QStringLiteral("配置"),
                      QStringLiteral("目的地配置已刷新，共 %1 个").arg(destinations.size()));
        });
        connect(addDest, &QPushButton::clicked, this, [=]() { editDestinationDialog(-1); });
        connect(editDest, &QPushButton::clicked, this, [=]() {
            const int row = destinationTable->currentRow();
            if (row < 0 || row >= destinations.size()) {
                QMessageBox::information(this, QStringLiteral("编辑目的地"),
                                         QStringLiteral("请先选择一个目的地。"));
                return;
            }
            editDestinationDialog(row);
        });
        return frame;
    }

    QWidget *logCard() {
        auto frame = card();
        frame->setFixedHeight(228);
        auto layout = new QVBoxLayout(frame);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(8);
        auto top = new QHBoxLayout;
        top->addWidget(label(QStringLiteral("运行日志"), "SectionTitle"));
        top->addStretch();
        auto clearLog = new QPushButton(QStringLiteral("清空"));
        auto openLog = new QPushButton(QStringLiteral("打开日志"));
        clearLog->setFixedWidth(70);
        openLog->setFixedWidth(92);
        top->addWidget(clearLog);
        top->addWidget(openLog);
        layout->addLayout(top);

        logTable = new QTableWidget(0, 3);
        logTable->setHorizontalHeaderLabels(
            QStringList() << QStringLiteral("时间") << QStringLiteral("任务")
                          << QStringLiteral("内容"));
        logTable->horizontalHeader()->setStretchLastSection(true);
        logTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
        logTable->verticalHeader()->hide();
        logTable->setFocusPolicy(Qt::NoFocus);
        logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        logTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        logTable->setItemDelegate(new StableItemDelegate(logTable));
        logTable->verticalHeader()->setDefaultSectionSize(30);
        logTable->setColumnWidth(0, 160);
        logTable->setColumnWidth(1, 120);
        layout->addWidget(logTable);

        connect(clearLog, &QPushButton::clicked, this, [=]() {
            if (!logTable->rowCount()) return;
            if (QMessageBox::question(
                    this, QStringLiteral("清空日志"),
                    QStringLiteral("确定清空当前显示的日志吗？日志文件仍会保留。"),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No) == QMessageBox::Yes)
                logTable->setRowCount(0);
        });
        connect(openLog, &QPushButton::clicked, this, [=]() { openDirectory("logs"); });
        return frame;
    }

    void openDirectory(const QString &name) {
        const QString path = appDir + "/" + name;
        if (!QDir().mkpath(path)) {
            QMessageBox::warning(this, QStringLiteral("打开目录"),
                                 QStringLiteral("无法创建目录：%1").arg(path));
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(path).absolutePath())))
            QMessageBox::warning(this, QStringLiteral("打开目录"),
                                 QStringLiteral("系统无法打开该目录。"));
    }

    void appendLog(const QString &task, const QString &message) {
        const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        if (logTable) {
            const int row = logTable->rowCount();
            logTable->insertRow(row);
            logTable->setItem(row, 0, new QTableWidgetItem(now));
            logTable->setItem(row, 1, new QTableWidgetItem(task));
            logTable->setItem(row, 2, new QTableWidgetItem(message));
            logTable->scrollToBottom();
            while (logTable->rowCount() > 500) logTable->removeRow(0);
        }

        QDir().mkpath(appDir + "/logs");
        QFile file(appDir + "/logs/" + QDate::currentDate().toString("yyyy-MM-dd") + ".log");
        if (file.open(QFile::WriteOnly | QFile::Append | QFile::Text)) {
            QTextStream stream(&file);
            stream.setCodec("UTF-8");
            stream << now << "\t" << task << "\t" << message << "\r\n";
        }
    }

    void queueTasks(const QList<int> &indexes) {
        for (int index : indexes) {
            if (index < 0 || index >= tasks.size()) continue;
            if (index == currentTaskIndex || runQueue.contains(index)) continue;
            runQueue.append(index);
        }
        if (!currentProcess) startNextTask();
    }

    void startNextTask() {
        if (runQueue.isEmpty()) {
            currentTaskIndex = -1;
            statusButton->setText(QStringLiteral("待机中"));
            return;
        }
        currentTaskIndex = runQueue.takeFirst();
        if (currentTaskIndex < 0 || currentTaskIndex >= tasks.size()) {
            startNextTask();
            return;
        }
        const Task task = tasks.at(currentTaskIndex);
        const int destinationRow = destinationIndex(task.destination);
        if (destinationRow < 0) {
            appendLog(task.name, QStringLiteral("失败：找不到目的地“%1”").arg(task.destination));
            startNextTask();
            return;
        }
        const Destination destination = destinations.at(destinationRow);
        if (task.sourcePath.isEmpty() || !QFileInfo(task.sourcePath).isDir()) {
            appendLog(task.name, QStringLiteral("失败：源文件夹不存在：%1").arg(task.sourcePath));
            startNextTask();
            return;
        }

        const QString rclonePath = appDir + "/rclone.exe";
        if (!QFileInfo::exists(rclonePath)) {
            appendLog(task.name, QStringLiteral("失败：发布目录中缺少 rclone.exe"));
            QMessageBox::warning(this, QStringLiteral("无法执行"),
                                 QStringLiteral("程序目录中缺少 rclone.exe。"));
            startNextTask();
            return;
        }

        QString target;
        const QString archivePath = appendRemotePath(
            QDate::currentDate().toString("yyyy-MM-dd"),
            pathSafeSegment(task.name));
        if (destination.type == "local") {
            target = destination.localPath;
            if (target.isEmpty()) {
                appendLog(task.name, QStringLiteral("失败：本地目的地未填写 local_path"));
                startNextTask();
                return;
            }
            if (task.dateFolder)
                target = QDir::toNativeSeparators(QDir(target).filePath(archivePath));
            QString sourceCheck = QDir::cleanPath(
                QDir::fromNativeSeparators(task.sourcePath)).toLower();
            QString targetCheck = QDir::cleanPath(
                QDir::fromNativeSeparators(target)).toLower();
            if (targetCheck == sourceCheck ||
                targetCheck.startsWith(sourceCheck + '/')) {
                appendLog(task.name, QStringLiteral("失败：本地目的地不能是源文件夹或其子文件夹"));
                QMessageBox::warning(
                    this, QStringLiteral("本地备份路径无效"),
                    QStringLiteral("本地目的地不能设置为源文件夹或源文件夹的子文件夹。"));
                startNextTask();
                return;
            }
        } else {
            if (destination.remote.isEmpty()) {
                appendLog(task.name, QStringLiteral("失败：目的地缺少 rclone.remote"));
                startNextTask();
                return;
            }
            QString remotePath = destination.path;
            if (task.dateFolder)
                remotePath = appendRemotePath(remotePath, archivePath);
            target = destination.remote + ":" + remotePath;
        }

        QStringList arguments;
        arguments << "copy" << task.sourcePath << target
                  << "--create-empty-src-dirs" << "--stats=5s" << "--stats-one-line";
        arguments.append(destination.flags);

        currentProcess = new QProcess(this);
        QProcess *process = currentProcess;
        processOutput.clear();
        process->setProcessChannelMode(QProcess::MergedChannels);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        for (auto it = destination.environment.constBegin();
             it != destination.environment.constEnd(); ++it)
            environment.insert(it.key(), it.value().toString());
        process->setProcessEnvironment(environment);

        connect(process, &QProcess::readyReadStandardOutput, this, [=]() {
            if (currentProcess != process) return;
            processOutput.append(process->readAllStandardOutput());
            int newline = -1;
            while ((newline = processOutput.indexOf('\n')) >= 0) {
                QByteArray line = processOutput.left(newline);
                processOutput.remove(0, newline + 1);
                line = line.trimmed();
                if (!line.isEmpty()) appendLog(task.name, QString::fromUtf8(line));
            }
        });
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [=](int exitCode, QProcess::ExitStatus exitStatus) {
            if (currentProcess != process) return;
            const QByteArray tail = processOutput.trimmed();
            if (!tail.isEmpty()) appendLog(task.name, QString::fromUtf8(tail));
            if (exitStatus == QProcess::NormalExit && exitCode == 0)
                appendLog(task.name, QStringLiteral("备份完成"));
            else
                appendLog(task.name, QStringLiteral("备份失败，退出代码 %1").arg(exitCode));
            currentProcess = nullptr;
            process->deleteLater();
            startNextTask();
        });
        connect(process, &QProcess::errorOccurred, this, [=](QProcess::ProcessError error) {
            if (currentProcess != process || error != QProcess::FailedToStart) return;
            appendLog(task.name, QStringLiteral("无法启动 rclone.exe"));
            currentProcess = nullptr;
            process->deleteLater();
            startNextTask();
        });

        statusButton->setText(QStringLiteral("执行中"));
        appendLog(task.name, QStringLiteral("开始备份到 %1").arg(task.destination));
        process->start(rclonePath, arguments);
    }

    void checkSchedule() {
        const QDateTime now = QDateTime::currentDateTime();
        const QString time = now.toString("HH:mm:ss");
        const QString date = now.toString("yyyy-MM-dd");
        if (triggeredTimes.size() > 1000) triggeredTimes.clear();
        for (int i = 0; i < tasks.size(); ++i) {
            const Task &task = tasks.at(i);
            if (!task.enabled || task.time != time) continue;
            const QString key = date + "|" + time + "|" + QString::number(i);
            if (triggeredTimes.contains(key)) continue;
            triggeredTimes.insert(key);
            queueTasks(QList<int>() << i);
        }
    }
};

int main(int argc, char *argv[]) {
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    const QString fontFamily = appFontFamily(QCoreApplication::applicationDirPath());
    QFont font(fontFamily, 10);
    font.setWeight(QFont::Normal);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);
    LightBackupWindow window(fontFamily);
    window.show();
    return app.exec();
}
