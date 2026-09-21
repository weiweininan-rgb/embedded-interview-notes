#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "protocolclient.h"

#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QPointF>

#include <functional>

class QChart;
class QChartView;
class QComboBox;
class QLabel;
class QLineEdit;
class QLineSeries;
class QListWidget;
class QScatterSeries;
class QPushButton;
class QProgressBar;
class QSpinBox;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    enum Command : quint16 {
        Stop = 2,
        Status = 3,
        SetAlgorithm = 4,
        SetSweep = 10,
        CalibrateBegin = 11,
        CalibrateStep = 12,
        CalibrateStatus = 13,
        SetGrain = 14,
        StartSweep = 15,
        GetTrace = 16,
        GetApplicationResult = 17,
    };

    void buildUi();
    QWidget *buildControlPanel();
    QChartView *makeChart(const QString &title, QLineSeries **raw,
                          QLineSeries **calibrated);
    QByteArray sweepPayload() const;
    void configureBackend(std::function<void()> completion);
    void openConnectionSettings();
    void connectToConfiguredTarget();
    void reconnectIfNeeded();
    void pollStatus();
    void recoverFromStatus(const QMap<QString, QString> &fields);
    void handleFinishedJob(const QString &status);
    void fetchAllResults();
    void fetchTrace(const QString &representation, const QString &channel,
                    QLineSeries *series, QList<QPointF> *storage,
                    std::function<void()> completion, int offset = 0);
    void updateCalibrationStatus();
    void startAutomaticCalibration();
    void startNextCalibrationStep();
    void markConfigurationChanged(bool invalidatesCalibration);
    void clearDisplayedResults();
    bool confirmFrequencyCoverage();
    void updateChineseStatus(const QMap<QString, QString> &fields);
    void exportCsv();
    void appendLog(const QString &message);
    static QMap<QString, QString> parseFields(const QString &text);

    ProtocolClient client_;
    QTimer *pollTimer_ = nullptr;
    QTimer *reconnectTimer_ = nullptr;
    bool connectionWanted_ = false;
    bool recoveringConnection_ = false;
    bool fetchingResults_ = false;
    bool statusPending_ = false;
    bool waitingCalibration_ = false;
    bool waitingSweep_ = false;
    bool calibrationAutoActive_ = false;
    bool calibrationRequestPending_ = false;
    bool calibrated_ = false;
    bool currentSweepOwned_ = false;

    QPushButton *connectButton_ = nullptr;
    QPushButton *settingsButton_ = nullptr;
    QLabel *connectionLabel_ = nullptr;
    QLabel *serviceLabel_ = nullptr;
    QProgressBar *progressBar_ = nullptr;
    QLineEdit *startEdit_ = nullptr;
    QLineEdit *stopEdit_ = nullptr;
    QLineEdit *stepEdit_ = nullptr;
    QComboBox *scenarioCombo_ = nullptr;
    QComboBox *algorithmCombo_ = nullptr;
    QLabel *calibrationLabel_ = nullptr;
    QPushButton *calibrationButton_ = nullptr;
    QPushButton *startSweepButton_ = nullptr;
    QPushButton *loadResultsButton_ = nullptr;
    QLabel *resultStateLabel_ = nullptr;
    QLabel *f0Label_ = nullptr;
    QLabel *qLabel_ = nullptr;
    QLabel *moistureLabel_ = nullptr;
    QListWidget *logView_ = nullptr;
    QLineSeries *rawS11_ = nullptr;
    QLineSeries *calS11_ = nullptr;
    QLineSeries *rawS21_ = nullptr;
    QLineSeries *calS21_ = nullptr;
    QScatterSeries *f0Marker_ = nullptr;
    QList<QPointF> rawS11Points_;
    QList<QPointF> calS11Points_;
    QList<QPointF> rawS21Points_;
    QList<QPointF> calS21Points_;
    QString nextCalibration_ = QStringLiteral("load");
    QString targetHost_ = QStringLiteral("127.0.0.1");
    quint16 targetPort_ = 9000;
};

#endif
