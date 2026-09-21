#include "mainwindow.h"

#include <QComboBox>
#include <QDataStream>
#include <QHostAddress>
#include <QPushButton>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

namespace {
constexpr quint32 Magic = 0x53504152U;
constexpr quint16 Version = 2U;
constexpr quint16 Status = 3U;
constexpr quint16 SetAlgorithm = 4U;
constexpr quint16 SetSweep = 10U;
constexpr quint16 CalibrateBegin = 11U;
constexpr quint16 CalibrateStep = 12U;
constexpr quint16 CalibrateStatus = 13U;
constexpr quint16 SetGrain = 14U;
constexpr quint16 GetTrace = 16U;
constexpr quint16 GetApplicationResult = 17U;
constexpr quint16 Response = 0x8000U;
}

class MockService : public QObject
{
    Q_OBJECT
public:
    explicit MockService(bool workflow = false) : workflow_(workflow)
    {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            client_ = server_.nextPendingConnection();
            ++connectionCount_;
            receiveBuffer_.clear();
            connect(client_, &QTcpSocket::readyRead, this, &MockService::readRequests);
        });
    }

    bool listen(quint16 port = 0)
    {
        return server_.listen(QHostAddress::LocalHost, port);
    }

    quint16 port() const { return server_.serverPort(); }
    int connectionCount() const { return connectionCount_; }
    bool sawRunningStatus() const { return sawRunningStatus_; }
    bool sawRestartedTrace() const { return sawRestartedTrace_; }
    bool sawApplicationResult() const { return sawApplicationResult_; }
    QStringList calibrationSteps() const { return calibrationSteps_; }
    void setDone(bool done) { done_ = done; }

    void dropConnection()
    {
        server_.close();
        if (client_) {
            client_->disconnect(this);
            client_->abort();
            client_->deleteLater();
            client_ = nullptr;
        }
    }

private:
    void sendResponse(const QByteArray &payload)
    {
        QByteArray packet;
        QDataStream stream(&packet, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::BigEndian);
        stream << Magic << Version << Response << static_cast<quint32>(payload.size());
        packet.append(payload);
        client_->write(packet);
    }

    void readRequests()
    {
        receiveBuffer_.append(client_->readAll());
        while (receiveBuffer_.size() >= 12) {
            QDataStream stream(receiveBuffer_.left(12));
            stream.setByteOrder(QDataStream::BigEndian);
            quint32 magic, length;
            quint16 version, command;
            stream >> magic >> version >> command >> length;
            if (magic != Magic || version != Version || length > 4096U) {
                client_->abort();
                return;
            }
            if (receiveBuffer_.size() < 12 + static_cast<int>(length))
                return;
            const QByteArray payload = receiveBuffer_.mid(12, static_cast<int>(length));
            receiveBuffer_.remove(0, 12 + static_cast<int>(length));

            if (workflow_) {
                if (command == Status) {
                    if (currentCalibration_.isEmpty())
                        sendResponse("state=IDLE algorithm=correlation job=legacy progress=0 total=0 scenario=rice_demo grain=rice calibrated=0 error=");
                    else
                        sendResponse(QStringLiteral("state=DONE algorithm=correlation job=cal_%1 progress=101 total=101 scenario=rice_demo grain=rice calibrated=%2 error=")
                                         .arg(currentCalibration_).arg(calibrationMask_ == 15).toUtf8());
                } else if (command == CalibrateStatus) {
                    const char *next = calibrationMask_ == 0 ? "load" : calibrationMask_ == 1 ? "open" :
                                       calibrationMask_ == 3 ? "short" : calibrationMask_ == 7 ? "thru" : "done";
                    sendResponse(QStringLiteral("mask=%1&ready=%2&next=%3")
                                     .arg(calibrationMask_).arg(calibrationMask_ == 15).arg(next).toUtf8());
                } else if (command == SetSweep || command == SetAlgorithm || command == SetGrain) {
                    sendResponse("ok");
                } else if (command == CalibrateBegin) {
                    calibrationMask_ = 0;
                    currentCalibration_.clear();
                    calibrationSteps_.clear();
                    sendResponse("ok&next=load");
                } else if (command == CalibrateStep) {
                    currentCalibration_ = QString::fromUtf8(payload);
                    calibrationSteps_.append(currentCalibration_);
                    if (payload == "load") calibrationMask_ |= 1;
                    else if (payload == "open") calibrationMask_ |= 2;
                    else if (payload == "short") calibrationMask_ |= 4;
                    else if (payload == "thru") calibrationMask_ |= 8;
                    sendResponse("ok");
                } else {
                    sendResponse("error=unexpected_command");
                }
                continue;
            }

            if (command == Status) {
                sawRunningStatus_ = sawRunningStatus_ || !done_;
                sendResponse(done_
                    ? QByteArray("state=DONE algorithm=correlation job=sweep progress=1001 total=1001 scenario=rice_demo grain=rice calibrated=1 error=")
                    : QByteArray("state=RUNNING algorithm=correlation job=sweep progress=10 total=1001 scenario=rice_demo grain=rice calibrated=1 error="));
            } else if (command == CalibrateStatus) {
                sendResponse("mask=15&ready=1&next=done");
            } else if (command == GetTrace) {
                if (payload.contains("representation=raw") &&
                    payload.contains("channel=s11") &&
                    payload.contains("offset=0"))
                    sawRestartedTrace_ = true;
                sendResponse("error=test_page_complete");
            } else if (command == GetApplicationResult) {
                sawApplicationResult_ = true;
                sendResponse("error=test_result_complete");
            } else {
                sendResponse("error=unexpected_command");
            }
        }
    }

    QTcpServer server_;
    QTcpSocket *client_ = nullptr;
    QByteArray receiveBuffer_;
    int connectionCount_ = 0;
    bool done_ = false;
    bool sawRunningStatus_ = false;
    bool sawRestartedTrace_ = false;
    bool sawApplicationResult_ = false;
    bool workflow_ = false;
    int calibrationMask_ = 0;
    QString currentCalibration_;
    QStringList calibrationSteps_;
};

class GuiReconnectTest : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        QSettings settings(QStringLiteral("SParamResearch"), QStringLiteral("SParamGui"));
        hadHost_ = settings.contains(QStringLiteral("connection/host"));
        hadPort_ = settings.contains(QStringLiteral("connection/port"));
        savedHost_ = settings.value(QStringLiteral("connection/host"));
        savedPort_ = settings.value(QStringLiteral("connection/port"));
    }

    void cleanup()
    {
        QSettings settings(QStringLiteral("SParamResearch"), QStringLiteral("SParamGui"));
        if (hadHost_) settings.setValue(QStringLiteral("connection/host"), savedHost_);
        else settings.remove(QStringLiteral("connection/host"));
        if (hadPort_) settings.setValue(QStringLiteral("connection/port"), savedPort_);
        else settings.remove(QStringLiteral("connection/port"));
    }

    void oneClickCalibrationUsesChineseBusinessChoices()
    {
        MockService service(true);
        QVERIFY(service.listen());
        QSettings settings(QStringLiteral("SParamResearch"), QStringLiteral("SParamGui"));
        settings.setValue(QStringLiteral("connection/host"), QStringLiteral("127.0.0.1"));
        settings.setValue(QStringLiteral("connection/port"), service.port());

        MainWindow window;
        window.show();
        auto *object = window.findChild<QComboBox *>(QStringLiteral("measurementObject"));
        QVERIFY(object);
        QCOMPARE(object->count(), 2);
        QCOMPARE(object->itemText(0), QStringLiteral("大米"));
        QCOMPARE(object->itemData(0).toString(), QStringLiteral("rice_demo"));
        QCOMPARE(object->itemText(1), QStringLiteral("绿豆"));
        QCOMPARE(object->itemData(1).toString(), QStringLiteral("mung_bean_demo"));

        QTRY_COMPARE_WITH_TIMEOUT(service.connectionCount(), 1, 3000);
        auto *calibrate = window.findChild<QPushButton *>(QStringLiteral("calibrateButton"));
        auto *startSweep = window.findChild<QPushButton *>(QStringLiteral("startSweepButton"));
        QVERIFY(calibrate);
        QVERIFY(startSweep);
        QVERIFY(!startSweep->isEnabled());
        QTest::mouseClick(calibrate, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(service.calibrationSteps(),
                                  QStringList({"load", "open", "short", "thru"}), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(startSweep->isEnabled(), 3000);

        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("断开")) QTest::mouseClick(button, Qt::LeftButton);
        QTest::qWait(100);
    }

    void reconnectRequiresExplicitHistoricalResultLoad()
    {
        MockService service;
        QVERIFY(service.listen());
        const quint16 port = service.port();

        QSettings settings(QStringLiteral("SParamResearch"), QStringLiteral("SParamGui"));
        settings.setValue(QStringLiteral("connection/host"), QStringLiteral("127.0.0.1"));
        settings.setValue(QStringLiteral("connection/port"), port);

        MainWindow window;
        window.show();

        QTRY_COMPARE_WITH_TIMEOUT(service.connectionCount(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(service.sawRunningStatus(), 3000);

        service.dropConnection();
        service.setDone(true);
        QVERIFY(service.listen(port));

        QTRY_COMPARE_WITH_TIMEOUT(service.connectionCount(), 2, 5000);
        QTest::qWait(500);
        QVERIFY(!service.sawRestartedTrace());
        auto *loadButton = window.findChild<QPushButton *>(QStringLiteral("loadResultsButton"));
        QVERIFY(loadButton);
        QTRY_VERIFY_WITH_TIMEOUT(loadButton->isEnabled(), 3000);
        QTest::mouseClick(loadButton, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(service.sawRestartedTrace(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(service.sawApplicationResult(), 5000);
        QPushButton *disconnectButton = nullptr;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("断开")) disconnectButton = button;
        QVERIFY(disconnectButton);
        QTest::mouseClick(disconnectButton, Qt::LeftButton);
        QTest::qWait(200);
    }

private:
    bool hadHost_ = false;
    bool hadPort_ = false;
    QVariant savedHost_;
    QVariant savedPort_;
};

QTEST_MAIN(GuiReconnectTest)
#include "test_gui_reconnect.moc"
