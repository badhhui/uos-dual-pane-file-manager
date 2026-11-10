#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QLocalServer;

class SingleInstance : public QObject
{
public:
    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    bool isPrimary() const;
    bool sendMessage(const QString &path);
    void setRevealHandler(const std::function<void(const QString &)> &handler);

private:
    void handleConnection();
    void deliver(const QString &path);

    bool m_primary = false;
    QString m_serverName;
    QLocalServer *m_server = nullptr;
    QStringList m_pending;
    std::function<void(const QString &)> m_handler;
};
