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
            if (taskTable && taskTable->currentRow() >= 0)
                saveCurrentTask(false);
            saveTasks();
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
        if (!taskTable || !enabledCheck || !timeEdit || !pathEdit || !destinationCombo) return;
        const int row = taskTable->currentRow();
        if (row < 0 || row >= tasks.size()) return;
        const Task &task = tasks.at(row);
        enabledCheck->setChecked(task.enabled);
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
        Task &task = tasks[row];
        task.enabled = enabledCheck->isChecked();
        task.time = timeText;
        task.sourcePath = QDir::toNativeSeparators(pathEdit->text().trimmed());
        task.destination = destinationCombo->currentText();
        refreshTaskTable(row);
        saveTasks();
        appendLog(task.name, QStringLiteral("任务设置已保存"));
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
        form->addWidget(label(QStringLiteral("定时")), 0, 1);
        timeEdit = new QLineEdit("23:55:55");
        form->addWidget(timeEdit, 0, 2);
        auto testTask = new QPushButton(QStringLiteral("测试"));
        form->addWidget(testTask, 0, 3);
        form->addWidget(label(QStringLiteral("备份目标路径")), 1, 0);
        pathEdit = new QLineEdit;
        form->addWidget(pathEdit, 1, 1, 1, 2);
        auto choosePath = new QPushButton(QStringLiteral("选择"));
        form->addWidget(choosePath, 1, 3);
        form->addWidget(label(QStringLiteral("备份目的地")), 2, 0);
        destinationCombo = new QComboBox;
        form->addWidget(destinationCombo, 2, 1, 1, 3);
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
        auto refreshDest = new QPushButton(QStringLiteral("刷新"));
        auto openDest = new QPushButton(QStringLiteral("打开目录"));
        addDest->setFixedWidth(70);
        refreshDest->setFixedWidth(70);
        openDest->setFixedWidth(92);
        top->addWidget(addDest);
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
            if (row < 0 || row >= destinations.size()) return;
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(destinations.at(row).filePath)))
                QMessageBox::warning(this, QStringLiteral("打开配置"),
                                     QStringLiteral("无法打开目的地配置文件。"));
        });
        connect(refreshDest, &QPushButton::clicked, this, [=]() {
            loadDestinationConfigs();
            populateDestinationControls();
            appendLog(QStringLiteral("配置"),
                      QStringLiteral("目的地配置已刷新，共 %1 个").arg(destinations.size()));
        });
        connect(addDest, &QPushButton::clicked, this, [=]() {
            QDir().mkpath(appDir + "/configs");
            QString filePath = appDir + "/configs/new-destination.json";
            int suffix = 2;
            while (QFileInfo::exists(filePath))
                filePath = appDir + QString("/configs/new-destination-%1.json").arg(suffix++);
            QJsonObject env;
            env.insert("RCLONE_CONFIG_NEW_BACKUP_TYPE", "ftp");
            env.insert("RCLONE_CONFIG_NEW_BACKUP_HOST", "192.168.1.10");
            env.insert("RCLONE_CONFIG_NEW_BACKUP_USER", "backup_user");
            env.insert("RCLONE_CONFIG_NEW_BACKUP_PASS", "请填入 rclone obscure 生成的密码");
            env.insert("RCLONE_CONFIG_NEW_BACKUP_PORT", "21");
            QJsonObject rclone;
            rclone.insert("remote", "new_backup");
            rclone.insert("env", env);
            rclone.insert("flags", QJsonArray());
            QJsonObject root;
            root.insert("_important", QStringLiteral(
                "复制本文件创建同协议目的地时，name、remote、RCLONE_CONFIG_前缀必须各自不同。"));
            root.insert("name", QStringLiteral("新目的地"));
            root.insert("type", "ftp");
            root.insert("remote_path", "backups/001");
            root.insert("rclone", rclone);
            QSaveFile file(filePath);
            if (file.open(QFile::WriteOnly)) {
                file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
                file.commit();
                loadDestinationConfigs();
                populateDestinationControls();
                appendLog(QStringLiteral("配置"), QStringLiteral("已创建目的地配置模板"));
                if (!QDesktopServices::openUrl(QUrl::fromLocalFile(filePath)))
                    QMessageBox::information(this, QStringLiteral("新建目的地"),
                                             QStringLiteral("配置模板已创建在 configs 目录。"));
            }
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
        if (destination.type == "local") {
            target = destination.localPath;
            if (target.isEmpty()) {
                appendLog(task.name, QStringLiteral("失败：本地目的地未填写 local_path"));
                startNextTask();
                return;
            }
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
            target = destination.remote + ":" + destination.path;
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
