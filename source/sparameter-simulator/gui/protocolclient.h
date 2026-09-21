#ifndef PROTOCOLCLIENT_H
#define PROTOCOLCLIENT_H

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QQueue>
#include <QTcpSocket>

#include <functional>

class ProtocolClient : public QObject
{
    Q_OBJECT
public:
    explicit ProtocolClient(QObject *parent = nullptr);

    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();
    bool isConnected() const;
    bool isConnecting() const;
    void request(quint16 command, const QByteArray &payload,
                 std::function<void(const QString &)> callback);

signals:
    void connectedChanged(bool connected);
    void protocolError(const QString &message);

private slots:
    void onReadyRead();

private:
    struct PendingRequest {
        quint16 command;
        QByteArray payload;
        std::function<void(const QString &)> callback;
    };

    void sendNext();
    void failAll(const QString &message);

    QTcpSocket socket_;
    QByteArray receiveBuffer_;
    QQueue<PendingRequest> queue_;
    PendingRequest active_{};
    bool waitingResponse_ = false;
};

#endif
