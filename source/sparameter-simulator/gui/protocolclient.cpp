#include "protocolclient.h"

#include <QDataStream>

namespace {
constexpr quint32 Magic = 0x53504152U;
constexpr quint16 Version = 2U;
constexpr quint16 ResponseCommand = 0x8000U;
constexpr quint32 MaxPayload = 4096U;
}

ProtocolClient::ProtocolClient(QObject *parent) : QObject(parent)
{
    connect(&socket_, &QTcpSocket::connected, this, [this] {
        sendNext();
    });
    connect(&socket_, &QTcpSocket::disconnected, this, [this] {
        failAll(QStringLiteral("连接已断开"));
    });
    connect(&socket_, &QTcpSocket::stateChanged, this,
            [this](QAbstractSocket::SocketState state) {
        if (state == QAbstractSocket::ConnectedState)
            emit connectedChanged(true);
        else if (state == QAbstractSocket::UnconnectedState)
            emit connectedChanged(false);
    });
    connect(&socket_, &QTcpSocket::readyRead, this, &ProtocolClient::onReadyRead);
    connect(&socket_, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        emit protocolError(socket_.errorString());
    });
}

void ProtocolClient::connectToHost(const QString &host, quint16 port)
{
    socket_.abort();
    receiveBuffer_.clear();
    socket_.connectToHost(host, port);
}

void ProtocolClient::disconnectFromHost()
{
    socket_.abort();
}

bool ProtocolClient::isConnected() const
{
    return socket_.state() == QAbstractSocket::ConnectedState;
}

bool ProtocolClient::isConnecting() const
{
    return socket_.state() == QAbstractSocket::HostLookupState ||
           socket_.state() == QAbstractSocket::ConnectingState;
}

void ProtocolClient::request(quint16 command, const QByteArray &payload,
                             std::function<void(const QString &)> callback)
{
    if (payload.size() > static_cast<int>(MaxPayload)) {
        emit protocolError(QStringLiteral("请求负载超过协议上限"));
        return;
    }
    queue_.enqueue({command, payload, std::move(callback)});
    sendNext();
}

void ProtocolClient::sendNext()
{
    if (waitingResponse_ || queue_.isEmpty() || !isConnected())
        return;
    active_ = queue_.dequeue();
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << Magic << Version << active_.command
           << static_cast<quint32>(active_.payload.size());
    packet.append(active_.payload);
    waitingResponse_ = true;
    socket_.write(packet);
}

void ProtocolClient::onReadyRead()
{
    receiveBuffer_.append(socket_.readAll());
    while (receiveBuffer_.size() >= 12) {
        QByteArray headerBytes = receiveBuffer_.left(12);
        QDataStream stream(headerBytes);
        stream.setByteOrder(QDataStream::BigEndian);
        quint32 magic, length;
        quint16 version, command;
        stream >> magic >> version >> command >> length;
        if (magic != Magic || version != Version || command != ResponseCommand ||
            length > MaxPayload) {
            socket_.abort();
            failAll(QStringLiteral("服务端返回了无效协议帧"));
            return;
        }
        if (receiveBuffer_.size() < 12 + static_cast<int>(length))
            return;
        QByteArray payload = receiveBuffer_.mid(12, static_cast<int>(length));
        receiveBuffer_.remove(0, 12 + static_cast<int>(length));
        auto callback = std::move(active_.callback);
        waitingResponse_ = false;
        if (callback)
            callback(QString::fromUtf8(payload));
        sendNext();
    }
}

void ProtocolClient::failAll(const QString &message)
{
    queue_.clear();
    receiveBuffer_.clear();
    waitingResponse_ = false;
    emit protocolError(message);
}
