#include "mainwindow.h"

#include <QChart>
#include <QChartView>
#include <QComboBox>
#include <QDataStream>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLegend>
#include <QLineEdit>
#include <QLineSeries>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QScatterSeries>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
const char *const CalibrationStandards[] = {"load", "open", "short", "thru"};

QString calibrationName(const QString &standard)
{
    if (standard == QStringLiteral("load")) return QStringLiteral("负载");
    if (standard == QStringLiteral("open")) return QStringLiteral("开路");
    if (standard == QStringLiteral("short")) return QStringLiteral("短路");
    if (standard == QStringLiteral("thru")) return QStringLiteral("直通");
    return standard;
}

QString grainForScenario(const QString &scenario)
{
    return scenario == QStringLiteral("mung_bean_demo")
               ? QStringLiteral("mung_bean") : QStringLiteral("rice");
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    QSettings settings(QStringLiteral("SParamResearch"), QStringLiteral("SParamGui"));
    targetHost_ = settings.value(QStringLiteral("connection/host"), targetHost_).toString();
    targetPort_ = static_cast<quint16>(
        settings.value(QStringLiteral("connection/port"), targetPort_).toUInt());

    buildUi();
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(300);
    connect(pollTimer_, &QTimer::timeout, this, &MainWindow::pollStatus);
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setInterval(1000);
    connect(reconnectTimer_, &QTimer::timeout, this, &MainWindow::reconnectIfNeeded);

    connect(&client_, &ProtocolClient::connectedChanged, this, [this](bool connected) {
        statusPending_ = false;
        connectionLabel_->setText(connected ? QStringLiteral("已连接")
                                            : connectionWanted_ ? QStringLiteral("重连中")
                                                                : QStringLiteral("未连接"));
        connectionLabel_->setProperty("connected", connected);
        connectionLabel_->style()->unpolish(connectionLabel_);
        connectionLabel_->style()->polish(connectionLabel_);
        if (connected) {
            reconnectTimer_->stop();
            recoveringConnection_ = true;
            pollTimer_->start();
            pollStatus();
        } else {
            pollTimer_->stop();
            fetchingResults_ = false;
            currentSweepOwned_ = false;
            calibrationRequestPending_ = false;
            startSweepButton_->setEnabled(false);
            if (connectionWanted_)
                reconnectTimer_->start();
        }
    });
    connect(&client_, &ProtocolClient::protocolError, this,
            [this](const QString &message) { appendLog(QStringLiteral("网络错误：") + message); });

    resize(1180, 760);
    setWindowTitle(QStringLiteral("S 参数测量与应用"));
    QTimer::singleShot(0, this, [this] {
        connectionWanted_ = true;
        connectButton_->setText(QStringLiteral("断开"));
        connectToConfiguredTarget();
    });
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralRoot"));
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *header = new QFrame;
    header->setObjectName(QStringLiteral("appHeader"));
    header->setFixedHeight(72);
    auto *headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(24, 12, 24, 10);
    headerLayout->setSpacing(1);
    auto *title = new QLabel(QStringLiteral("S 参数测量与应用系统"));
    title->setObjectName(QStringLiteral("appTitle"));
    auto *subtitle = new QLabel(QStringLiteral("测量配置、校准与曲线分析"));
    subtitle->setObjectName(QStringLiteral("appSubtitle"));
    headerLayout->addWidget(title);
    headerLayout->addWidget(subtitle);
    rootLayout->addWidget(header);

    auto *content = new QWidget;
    auto *layout = new QHBoxLayout(content);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(16);
    layout->addWidget(buildControlPanel(), 0);

    auto *tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("chartTabs"));
    tabs->addTab(makeChart(QStringLiteral("S11 幅度"), &rawS11_, &calS11_), QStringLiteral("S11"));
    tabs->addTab(makeChart(QStringLiteral("S21 幅度"), &rawS21_, &calS21_), QStringLiteral("S21"));
    layout->addWidget(tabs, 1);
    rootLayout->addWidget(content, 1);
    setCentralWidget(central);
}

QWidget *MainWindow::buildControlPanel()
{
    auto *panel = new QWidget;
    panel->setObjectName(QStringLiteral("controlPanel"));
    panel->setMinimumWidth(390);
    panel->setMaximumWidth(440);
    auto *layout = new QVBoxLayout(panel);

    auto *connectionBox = new QGroupBox(QStringLiteral("连接与状态"));
    auto *connectionLayout = new QGridLayout(connectionBox);
    connectButton_ = new QPushButton(QStringLiteral("连接"));
    settingsButton_ = new QPushButton(QStringLiteral("连接设置"));
    connectionLabel_ = new QLabel(QStringLiteral("未连接"));
    connectionLabel_->setObjectName(QStringLiteral("connectionState"));
    connectionLabel_->setProperty("connected", false);
    serviceLabel_ = new QLabel(QStringLiteral("等待连接"));
    serviceLabel_->setObjectName(QStringLiteral("serviceStatus"));
    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    connectionLayout->addWidget(connectButton_, 0, 0);
    connectionLayout->addWidget(settingsButton_, 0, 1);
    connectionLayout->addWidget(connectionLabel_, 0, 2);
    connectionLayout->addWidget(serviceLabel_, 1, 0, 1, 3);
    connectionLayout->addWidget(progressBar_, 2, 0, 1, 3);
    connect(connectButton_, &QPushButton::clicked, this, [this] {
        if (connectionWanted_) {
            connectionWanted_ = false;
            recoveringConnection_ = false;
            calibrationAutoActive_ = false;
            reconnectTimer_->stop();
            client_.disconnectFromHost();
            connectButton_->setText(QStringLiteral("连接"));
        } else {
            connectionWanted_ = true;
            connectButton_->setText(QStringLiteral("断开"));
            connectToConfiguredTarget();
        }
    });
    connect(settingsButton_, &QPushButton::clicked, this, &MainWindow::openConnectionSettings);
    layout->addWidget(connectionBox);

    auto *configBox = new QGroupBox(QStringLiteral("测量配置"));
    auto *form = new QFormLayout(configBox);
    scenarioCombo_ = new QComboBox;
    scenarioCombo_->setObjectName(QStringLiteral("measurementObject"));
    scenarioCombo_->addItem(QStringLiteral("大米"), QStringLiteral("rice_demo"));
    scenarioCombo_->addItem(QStringLiteral("绿豆"), QStringLiteral("mung_bean_demo"));
    startEdit_ = new QLineEdit(QStringLiteral("1450"));
    stopEdit_ = new QLineEdit(QStringLiteral("1550"));
    stepEdit_ = new QLineEdit(QStringLiteral("0.1"));
    algorithmCombo_ = new QComboBox;
    algorithmCombo_->addItem(QStringLiteral("相关法"), QStringLiteral("correlation"));
    algorithmCombo_->addItem(QStringLiteral("FFT"), QStringLiteral("fft"));
    form->addRow(QStringLiteral("测量对象"), scenarioCombo_);
    form->addRow(QStringLiteral("起始频率 MHz"), startEdit_);
    form->addRow(QStringLiteral("终止频率 MHz"), stopEdit_);
    form->addRow(QStringLiteral("步进 MHz"), stepEdit_);
    form->addRow(QStringLiteral("算法"), algorithmCombo_);
    connect(scenarioCombo_, &QComboBox::currentIndexChanged, this, [this] {
        if (scenarioCombo_->currentData().toString() == QStringLiteral("mung_bean_demo")) {
            startEdit_->setText(QStringLiteral("1400"));
            stopEdit_->setText(QStringLiteral("1500"));
        } else {
            startEdit_->setText(QStringLiteral("1450"));
            stopEdit_->setText(QStringLiteral("1550"));
        }
        stepEdit_->setText(QStringLiteral("0.1"));
        markConfigurationChanged(true);
    });
    for (QLineEdit *edit : {startEdit_, stopEdit_, stepEdit_})
        connect(edit, &QLineEdit::textChanged, this,
                [this] { markConfigurationChanged(true); });
    connect(algorithmCombo_, &QComboBox::currentIndexChanged, this,
            [this] { markConfigurationChanged(true); });
    layout->addWidget(configBox);

    auto *calibrationBox = new QGroupBox(QStringLiteral("校准"));
    auto *calibrationLayout = new QHBoxLayout(calibrationBox);
    calibrationButton_ = new QPushButton(QStringLiteral("一键校准"));
    calibrationButton_->setObjectName(QStringLiteral("calibrateButton"));
    calibrationButton_->setProperty("role", QStringLiteral("primary"));
    calibrationLabel_ = new QLabel(QStringLiteral("未校准"));
    calibrationLayout->addWidget(calibrationButton_);
    calibrationLayout->addWidget(calibrationLabel_, 1);
    connect(calibrationButton_, &QPushButton::clicked, this,
            &MainWindow::startAutomaticCalibration);
    layout->addWidget(calibrationBox);

    auto *actions = new QHBoxLayout;
    startSweepButton_ = new QPushButton(QStringLiteral("开始扫频"));
    startSweepButton_->setObjectName(QStringLiteral("startSweepButton"));
    startSweepButton_->setProperty("role", QStringLiteral("primary"));
    startSweepButton_->setEnabled(false);
    auto *stopButton = new QPushButton(QStringLiteral("停止"));
    stopButton->setProperty("role", QStringLiteral("danger"));
    auto *exportButton = new QPushButton(QStringLiteral("导出 CSV"));
    actions->addWidget(startSweepButton_);
    actions->addWidget(stopButton);
    actions->addWidget(exportButton);
    connect(startSweepButton_, &QPushButton::clicked, this, [this] {
        if (!calibrated_)
            return;
        clearDisplayedResults();
        resultStateLabel_->setText(QStringLiteral("测量中"));
        configureBackend([this] {
            client_.request(StartSweep, {}, [this](const QString &response) {
                appendLog(response);
                if (response == QStringLiteral("ok")) {
                    waitingSweep_ = true;
                    currentSweepOwned_ = true;
                    startSweepButton_->setEnabled(false);
                }
            });
        });
    });
    connect(stopButton, &QPushButton::clicked, this, [this] {
        client_.request(Stop, {}, [this](const QString &response) { appendLog(response); });
    });
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportCsv);
    layout->addLayout(actions);

    auto *resultBox = new QGroupBox(QStringLiteral("应用结果"));
    auto *resultLayout = new QFormLayout(resultBox);
    resultStateLabel_ = new QLabel(QStringLiteral("等待测量"));
    resultStateLabel_->setObjectName(QStringLiteral("resultState"));
    f0Label_ = new QLabel(QStringLiteral("--"));
    qLabel_ = new QLabel(QStringLiteral("--"));
    moistureLabel_ = new QLabel(QStringLiteral("--"));
    f0Label_->setProperty("metric", true);
    qLabel_->setProperty("metric", true);
    moistureLabel_->setProperty("metric", true);
    loadResultsButton_ = new QPushButton(QStringLiteral("加载结果"));
    loadResultsButton_->setObjectName(QStringLiteral("loadResultsButton"));
    loadResultsButton_->setEnabled(false);
    resultLayout->addRow(QStringLiteral("状态"), resultStateLabel_);
    resultLayout->addRow(QStringLiteral("f0"), f0Label_);
    resultLayout->addRow(QStringLiteral("Q"), qLabel_);
    resultLayout->addRow(QStringLiteral("湿度"), moistureLabel_);
    resultLayout->addRow(loadResultsButton_);
    connect(loadResultsButton_, &QPushButton::clicked, this, &MainWindow::fetchAllResults);
    layout->addWidget(resultBox);

    auto *logButton = new QPushButton(QStringLiteral("运行日志"));
    logButton->setCheckable(true);
    logView_ = new QListWidget;
    logView_->setVisible(false);
    logView_->setMaximumHeight(130);
    connect(logButton, &QPushButton::toggled, logView_, &QWidget::setVisible);
    layout->addWidget(logButton);
    layout->addWidget(logView_);
    layout->addStretch(1);
    return panel;
}

QChartView *MainWindow::makeChart(const QString &title, QLineSeries **raw,
                                  QLineSeries **calibrated)
{
    *raw = new QLineSeries;
    *calibrated = new QLineSeries;
    (*raw)->setName(QStringLiteral("原始"));
    (*calibrated)->setName(QStringLiteral("校准后"));
    (*raw)->setPen(QPen(QColor(QStringLiteral("#9EA6AF")), 2.0));
    (*calibrated)->setPen(QPen(QColor(QStringLiteral("#B4232D")), 2.6));
    auto *chart = new QChart;
    chart->setTitle(title);
    chart->setTitleFont(QFont(QStringLiteral("Microsoft YaHei UI"), 12, QFont::DemiBold));
    chart->setBackgroundBrush(QColor(QStringLiteral("#FFFFFF")));
    chart->setBackgroundRoundness(10);
    chart->setMargins(QMargins(18, 12, 18, 14));
    chart->legend()->setAlignment(Qt::AlignTop);
    chart->addSeries(*raw);
    chart->addSeries(*calibrated);
    auto *axisX = new QValueAxis;
    axisX->setTitleText(QStringLiteral("频率 / MHz"));
    auto *axisY = new QValueAxis;
    axisY->setTitleText(QStringLiteral("幅度 / dB"));
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    (*raw)->attachAxis(axisX);
    (*raw)->attachAxis(axisY);
    (*calibrated)->attachAxis(axisX);
    (*calibrated)->attachAxis(axisY);
    axisX->setGridLinePen(QPen(QColor(QStringLiteral("#E9EDF1")), 1));
    axisY->setGridLinePen(QPen(QColor(QStringLiteral("#E9EDF1")), 1));
    axisX->setLabelsColor(QColor(QStringLiteral("#5F6872")));
    axisY->setLabelsColor(QColor(QStringLiteral("#5F6872")));
    if (title.startsWith(QStringLiteral("S21"))) {
        f0Marker_ = new QScatterSeries;
        f0Marker_->setName(QStringLiteral("f0"));
        f0Marker_->setMarkerSize(11.0);
        f0Marker_->setColor(QColor(QStringLiteral("#B4232D")));
        f0Marker_->setBorderColor(QColor(QStringLiteral("#FFFFFF")));
        chart->addSeries(f0Marker_);
        f0Marker_->attachAxis(axisX);
        f0Marker_->attachAxis(axisY);
    }
    auto *view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    return view;
}

QByteArray MainWindow::sweepPayload() const
{
    bool okStart, okStop, okStep;
    double start = startEdit_->text().toDouble(&okStart) * 1e6;
    double stop = stopEdit_->text().toDouble(&okStop) * 1e6;
    double step = stepEdit_->text().toDouble(&okStep) * 1e6;
    if (!okStart || !okStop || !okStep || start <= 0.0 || stop < start || step <= 0.0)
        return {};
    double intervals = (stop - start) / step;
    if (std::fabs(intervals - std::round(intervals)) > 1e-6 * std::max(1.0, intervals) ||
        std::round(intervals) + 1.0 > 4096.0)
        return {};
    return QStringLiteral("start_hz=%1&stop_hz=%2&step_hz=%3&scenario=%4&samples=256")
        .arg(start, 0, 'f', 0).arg(stop, 0, 'f', 0).arg(step, 0, 'f', 0)
        .arg(scenarioCombo_->currentData().toString()).toUtf8();
}

void MainWindow::configureBackend(std::function<void()> completion)
{
    QByteArray sweep = sweepPayload();
    if (!client_.isConnected() || sweep.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("配置错误"),
                             QStringLiteral("请检查连接、频率范围、步进和频点数量"));
        return;
    }
    client_.request(SetSweep, sweep, [this, completion](const QString &response) {
        if (response != QStringLiteral("ok")) { appendLog(response); return; }
        client_.request(SetAlgorithm, algorithmCombo_->currentData().toByteArray(),
                        [this, completion](const QString &algorithmResponse) {
            if (algorithmResponse != QStringLiteral("ok")) { appendLog(algorithmResponse); return; }
            const QString scenario = scenarioCombo_->currentData().toString();
            client_.request(SetGrain, grainForScenario(scenario).toUtf8(),
                            [this, completion](const QString &grainResponse) {
                if (grainResponse != QStringLiteral("ok")) { appendLog(grainResponse); return; }
                completion();
            });
        });
    });
}

void MainWindow::openConnectionSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("连接设置"));
    auto *layout = new QFormLayout(&dialog);
    auto *host = new QLineEdit(targetHost_);
    auto *port = new QSpinBox;
    port->setRange(1, 65535);
    port->setValue(targetPort_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addRow(QStringLiteral("板卡地址"), host);
    layout->addRow(QStringLiteral("端口"), port);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || host->text().trimmed().isEmpty())
        return;
    targetHost_ = host->text().trimmed();
    targetPort_ = static_cast<quint16>(port->value());
    QSettings settings(QStringLiteral("SParamResearch"), QStringLiteral("SParamGui"));
    settings.setValue(QStringLiteral("connection/host"), targetHost_);
    settings.setValue(QStringLiteral("connection/port"), targetPort_);
    if (connectionWanted_) {
        client_.disconnectFromHost();
        QTimer::singleShot(100, this, &MainWindow::connectToConfiguredTarget);
    }
}

void MainWindow::connectToConfiguredTarget()
{
    if (connectionWanted_ && !client_.isConnected() && !client_.isConnecting())
        client_.connectToHost(targetHost_, targetPort_);
}

void MainWindow::reconnectIfNeeded()
{
    if (connectionWanted_ && !client_.isConnected() && !client_.isConnecting()) {
        appendLog(QStringLiteral("正在重新连接"));
        connectToConfiguredTarget();
    }
}

QMap<QString, QString> MainWindow::parseFields(const QString &text)
{
    QMap<QString, QString> fields;
    const QString firstLine = text.section('\n', 0, 0);
    for (const QString &part : firstLine.split('&')) {
        int separator = part.indexOf('=');
        if (separator > 0)
            fields.insert(part.left(separator), part.mid(separator + 1));
    }
    return fields;
}

void MainWindow::pollStatus()
{
    if (statusPending_ || !client_.isConnected())
        return;
    statusPending_ = true;
    client_.request(Status, {}, [this](const QString &response) {
        statusPending_ = false;
        QMap<QString, QString> fields;
        for (const QString &part : response.split(' ')) {
            int separator = part.indexOf('=');
            if (separator > 0) fields.insert(part.left(separator), part.mid(separator + 1));
        }
        int total = fields.value(QStringLiteral("total")).toInt();
        int progress = fields.value(QStringLiteral("progress")).toInt();
        progressBar_->setValue(total > 0 ? progress * 100 / total : 0);
        updateChineseStatus(fields);
        if (recoveringConnection_) {
            recoveringConnection_ = false;
            recoverFromStatus(fields);
            return;
        }
        if (fields.value(QStringLiteral("state")) == QStringLiteral("DONE"))
            handleFinishedJob(response);
    });
}

void MainWindow::recoverFromStatus(const QMap<QString, QString> &fields)
{
    const QString state = fields.value(QStringLiteral("state"));
    const QString job = fields.value(QStringLiteral("job"));
    waitingCalibration_ = false;
    waitingSweep_ = false;
    if (job.startsWith(QStringLiteral("cal_")) && state == QStringLiteral("RUNNING"))
        waitingCalibration_ = true;
    updateCalibrationStatus();
    if (job == QStringLiteral("sweep")) {
        if (state == QStringLiteral("RUNNING")) {
            waitingSweep_ = true;
            resultStateLabel_->setText(QStringLiteral("测量中"));
        } else if (state == QStringLiteral("DONE")) {
            resultStateLabel_->setText(QStringLiteral("已完成测量"));
            loadResultsButton_->setEnabled(true);
        }
    }
}

void MainWindow::handleFinishedJob(const QString &status)
{
    if (waitingCalibration_ && status.contains(QStringLiteral("job=cal_"))) {
        waitingCalibration_ = false;
        calibrationRequestPending_ = false;
        updateCalibrationStatus();
    } else if (waitingSweep_ && status.contains(QStringLiteral("job=sweep"))) {
        waitingSweep_ = false;
        startSweepButton_->setEnabled(calibrated_);
        if (currentSweepOwned_)
            fetchAllResults();
        else {
            resultStateLabel_->setText(QStringLiteral("已完成测量"));
            loadResultsButton_->setEnabled(true);
        }
        currentSweepOwned_ = false;
    }
}

void MainWindow::updateCalibrationStatus()
{
    client_.request(CalibrateStatus, {}, [this](const QString &response) {
        auto fields = parseFields(response);
        nextCalibration_ = fields.value(QStringLiteral("next"));
        calibrated_ = fields.value(QStringLiteral("ready")) == QStringLiteral("1");
        if (calibrated_) {
            calibrationAutoActive_ = false;
            calibrationRequestPending_ = false;
            calibrationLabel_->setText(QStringLiteral("已校准"));
            calibrationButton_->setEnabled(true);
            calibrationButton_->setText(QStringLiteral("重新校准"));
            startSweepButton_->setEnabled(client_.isConnected());
        } else if (calibrationAutoActive_) {
            calibrationLabel_->setText(QStringLiteral("正在校准：") + calibrationName(nextCalibration_));
            if (!waitingCalibration_ && !calibrationRequestPending_)
                startNextCalibrationStep();
        } else {
            calibrationLabel_->setText(QStringLiteral("未校准"));
            calibrationButton_->setEnabled(client_.isConnected());
            calibrationButton_->setText(QStringLiteral("一键校准"));
            startSweepButton_->setEnabled(false);
        }
    });
}

void MainWindow::startAutomaticCalibration()
{
    if (!client_.isConnected() || !confirmFrequencyCoverage())
        return;
    clearDisplayedResults();
    resultStateLabel_->setText(QStringLiteral("等待测量"));
    configureBackend([this] {
        client_.request(CalibrateBegin, {}, [this](const QString &response) {
            appendLog(response);
            if (!response.startsWith(QStringLiteral("ok")))
                return;
            calibrated_ = false;
            calibrationAutoActive_ = true;
            calibrationRequestPending_ = false;
            waitingCalibration_ = false;
            nextCalibration_ = QStringLiteral("load");
            calibrationButton_->setEnabled(false);
            startSweepButton_->setEnabled(false);
            startNextCalibrationStep();
        });
    });
}

void MainWindow::startNextCalibrationStep()
{
    if (!calibrationAutoActive_ || waitingCalibration_ || calibrationRequestPending_ ||
        nextCalibration_ == QStringLiteral("done"))
        return;
    int step = 0;
    for (; step < 4; ++step)
        if (nextCalibration_ == QString::fromLatin1(CalibrationStandards[step])) break;
    calibrationLabel_->setText(QStringLiteral("正在校准 %1/4：%2")
                                   .arg(step + 1).arg(calibrationName(nextCalibration_)));
    calibrationRequestPending_ = true;
    client_.request(CalibrateStep, nextCalibration_.toUtf8(), [this](const QString &response) {
        calibrationRequestPending_ = false;
        appendLog(response);
        if (response == QStringLiteral("ok"))
            waitingCalibration_ = true;
        else {
            calibrationAutoActive_ = false;
            calibrationLabel_->setText(QStringLiteral("校准失败：") + calibrationName(nextCalibration_));
            calibrationButton_->setEnabled(true);
        }
    });
}

void MainWindow::markConfigurationChanged(bool invalidatesCalibration)
{
    clearDisplayedResults();
    resultStateLabel_->setText(QStringLiteral("配置已修改，等待测量"));
    if (invalidatesCalibration) {
        calibrated_ = false;
        calibrationAutoActive_ = false;
        calibrationLabel_->setText(QStringLiteral("需要校准"));
        calibrationButton_->setText(QStringLiteral("一键校准"));
        startSweepButton_->setEnabled(false);
    }
}

void MainWindow::clearDisplayedResults()
{
    for (QLineSeries *series : {rawS11_, calS11_, rawS21_, calS21_})
        if (series) series->clear();
    if (f0Marker_) f0Marker_->clear();
    rawS11Points_.clear(); calS11Points_.clear();
    rawS21Points_.clear(); calS21Points_.clear();
    if (f0Label_) f0Label_->setText(QStringLiteral("--"));
    if (qLabel_) qLabel_->setText(QStringLiteral("--"));
    if (moistureLabel_) moistureLabel_->setText(QStringLiteral("--"));
    if (loadResultsButton_) loadResultsButton_->setEnabled(false);
}

bool MainWindow::confirmFrequencyCoverage()
{
    bool okStart, okStop;
    double start = startEdit_->text().toDouble(&okStart);
    double stop = stopEdit_->text().toDouble(&okStop);
    if (!okStart || !okStop)
        return true;
    const bool mung = scenarioCombo_->currentData().toString() == QStringLiteral("mung_bean_demo");
    const double f0 = mung ? 1433.8 : 1500.0;
    const double q = mung ? 36.8 : 50.0;
    const double halfBandwidth = f0 / q / 2.0;
    if (start <= f0 - halfBandwidth && stop >= f0 + halfBandwidth)
        return true;
    return QMessageBox::question(
               this, QStringLiteral("频段提示"),
               QStringLiteral("当前频段可能无法覆盖完整峰值和两侧 -3 dB 点，Q 和湿度可能无法计算。是否继续？"),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void MainWindow::updateChineseStatus(const QMap<QString, QString> &fields)
{
    const QString state = fields.value(QStringLiteral("state"));
    const QString job = fields.value(QStringLiteral("job"));
    if (state == QStringLiteral("RUNNING") && job == QStringLiteral("sweep"))
        serviceLabel_->setText(QStringLiteral("正在扫频 %1%").arg(progressBar_->value()));
    else if (state == QStringLiteral("RUNNING") && job.startsWith(QStringLiteral("cal_")))
        serviceLabel_->setText(QStringLiteral("正在校准"));
    else if (state == QStringLiteral("ERROR"))
        serviceLabel_->setText(QStringLiteral("测量失败"));
    else if (state == QStringLiteral("DONE") && job == QStringLiteral("sweep"))
        serviceLabel_->setText(QStringLiteral("已完成测量"));
    else
        serviceLabel_->setText(QStringLiteral("设备就绪"));
}

void MainWindow::fetchTrace(const QString &representation, const QString &channel,
                            QLineSeries *series, QList<QPointF> *storage,
                            std::function<void()> completion, int offset)
{
    QByteArray payload = QStringLiteral("representation=%1&channel=%2&offset=%3&count=32")
                             .arg(representation, channel).arg(offset).toUtf8();
    client_.request(GetTrace, payload, [=](const QString &response) mutable {
        if (response.startsWith(QStringLiteral("error="))) {
            appendLog(response); completion(); return;
        }
        auto fields = parseFields(response);
        int total = fields.value(QStringLiteral("total")).toInt();
        int received = fields.value(QStringLiteral("count")).toInt();
        if (offset == 0) { series->clear(); storage->clear(); }
        const QStringList lines = response.split('\n', Qt::SkipEmptyParts);
        for (int index = 1; index < lines.size(); ++index) {
            const QStringList columns = lines[index].split(',');
            if (columns.size() < 5) continue;
            QPointF point(columns[0].toDouble() / 1e6, columns[3].toDouble());
            series->append(point); storage->append(point);
        }
        if (offset + received < total)
            fetchTrace(representation, channel, series, storage, completion, offset + received);
        else
            completion();
    });
}

void MainWindow::fetchAllResults()
{
    if (fetchingResults_ || !client_.isConnected())
        return;
    fetchingResults_ = true;
    loadResultsButton_->setEnabled(false);
    fetchTrace(QStringLiteral("raw"), QStringLiteral("s11"), rawS11_, &rawS11Points_, [this] {
        fetchTrace(QStringLiteral("raw"), QStringLiteral("s21"), rawS21_, &rawS21Points_, [this] {
            fetchTrace(QStringLiteral("calibrated"), QStringLiteral("s11"), calS11_, &calS11Points_, [this] {
                fetchTrace(QStringLiteral("calibrated"), QStringLiteral("s21"), calS21_, &calS21Points_, [this] {
                    auto adjustChart = [this](QLineSeries *raw, QLineSeries *cal) {
                        double minimum = 1e9, maximum = -1e9;
                        for (QLineSeries *series : {raw, cal})
                            for (const QPointF &point : series->points()) {
                                minimum = std::min(minimum, point.y());
                                maximum = std::max(maximum, point.y());
                            }
                        if (minimum <= maximum) {
                            double padding = std::max(1.0, (maximum - minimum) * 0.12);
                            const auto axes = raw->chart()->axes();
                            if (axes.size() >= 2) {
                                double start = raw->points().isEmpty() ? startEdit_->text().toDouble()
                                                                       : raw->points().first().x();
                                double stop = raw->points().isEmpty() ? stopEdit_->text().toDouble()
                                                                      : raw->points().last().x();
                                qobject_cast<QValueAxis *>(axes.first())->setRange(start, stop);
                                qobject_cast<QValueAxis *>(axes.last())->setRange(
                                    minimum - padding, maximum + padding);
                            }
                        }
                    };
                    adjustChart(rawS11_, calS11_);
                    adjustChart(rawS21_, calS21_);
                    client_.request(GetApplicationResult, {}, [this](const QString &response) {
                        fetchingResults_ = false;
                        if (response.startsWith(QStringLiteral("error="))) {
                            resultStateLabel_->setText(QStringLiteral("结果不可用"));
                            appendLog(response);
                            return;
                        }
                        auto fields = parseFields(response);
                        f0Label_->setText(QString::number(fields.value(QStringLiteral("f0_hz")).toDouble() / 1e9,
                                                           'f', 6) + QStringLiteral(" GHz"));
                        if (f0Marker_ && !calS21_->points().isEmpty()) {
                            const double f0Mhz = fields.value(QStringLiteral("f0_hz")).toDouble() / 1e6;
                            QPointF nearest = calS21_->points().first();
                            for (const QPointF &point : calS21_->points())
                                if (std::fabs(point.x() - f0Mhz) < std::fabs(nearest.x() - f0Mhz))
                                    nearest = point;
                            f0Marker_->replace(QList<QPointF>{nearest});
                        }
                        qLabel_->setText(fields.value(QStringLiteral("q")));
                        const QString moisture = fields.value(QStringLiteral("moisture"));
                        moistureLabel_->setText(moisture.isEmpty() ? QStringLiteral("--")
                                                                  : moisture + QStringLiteral(" %"));
                        resultStateLabel_->setText(QStringLiteral("已完成测量"));
                    });
                });
            });
        });
    });
}

void MainWindow::exportCsv()
{
    if (rawS21Points_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("没有结果"), QStringLiteral("请先完成一次扫频"));
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出曲线"),
                                                QStringLiteral("sparam_trace.csv"),
                                                QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }
    QTextStream out(&file);
    out << "frequency_mhz,raw_s11_db,raw_s21_db,cal_s11_db,cal_s21_db\n";
    for (int i = 0; i < rawS21Points_.size(); ++i) {
        out << rawS21Points_[i].x() << ','
            << (i < rawS11Points_.size() ? rawS11Points_[i].y() : 0.0) << ','
            << rawS21Points_[i].y() << ','
            << (i < calS11Points_.size() ? calS11Points_[i].y() : 0.0) << ','
            << (i < calS21Points_.size() ? calS21Points_[i].y() : 0.0) << '\n';
    }
    appendLog(QStringLiteral("已导出：") + path);
}

void MainWindow::appendLog(const QString &message)
{
    if (logView_)
        logView_->addItem(message);
}
