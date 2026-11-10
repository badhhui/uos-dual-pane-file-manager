#include "singleinstance.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

#include <unistd.h>

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    QDir().mkpath(runtime);
    const QString directory = QDir(runtime).filePath(
        QStringLiteral("uos-dual-pane-file-manager-%1").arg(static_cast<qulonglong>(getuid())));
    QDir().mkpath(directory);
    m_serverName = QDir(directory).filePath(QStringLiteral("instance.sock"));

    QLocalSocket probe;
    probe.connectToServer(m_serverName);
    if (probe.waitForConnected(250)) {
        m_primary = false;
        probe.disconnectFromServer();
        return;
    }

    QLocalServer::removeServer(m_serverName);
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_serverName)) {
        m_primary = false;
        m_server->deleteLater();
        m_server = nullptr;
        return;
    }
    m_primary = true;
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::handleConnection);
}

SingleInstance::~SingleInstance()
{
    if (m_server) {
        m_server->close();
        QLocalServer::removeServer(m_serverName);
    }
}

bool SingleInstance::isPrimary() const
{
    return m_primary;
}

void SingleInstance::setRevealHandler(const std::function<void(const QString &)> &handler)
{
    m_handler = handler;
    const QStringList pending = m_pending;
    m_pending.clear();
    for (const QString &path : pending)
        deliver(path);
}

bool SingleInstance::sendMessage(const QString &path)
{
    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    if (!socket.waitForConnected(800))
        return false;
    QJsonObject object;
    object.insert(QStringLiteral("action"), path.isEmpty() ? QStringLiteral("activate")
                                                          : QStringLiteral("reveal"));
    object.insert(QStringLiteral("path"), path);
    QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    payload.append('\n');
    socket.write(payload);
    const bool ok = socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return ok;
}

void SingleInstance::handleConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            const QByteArray data = socket->readAll();
            const int newline = data.indexOf('\n');
            if (newline < 0)
                return;
            const QJsonDocument document = QJsonDocument::fromJson(data.left(newline));
            if (!document.isObject())
                return;
            const QJsonObject object = document.object();
            const QString action = object.value(QStringLiteral("action")).toString();
            const QString path = object.value(QStringLiteral("path")).toString();
            if (action == QStringLiteral("reveal") && !path.isEmpty())
                deliver(path);
            else
                deliver(QString());
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void SingleInstance::deliver(const QString &path)
{
    if (m_handler) {
        m_handler(path);
    } else {
        if (!m_pending.contains(path))
            m_pending.append(path);
    }
}
