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
    // Forward a directory to the primary instance. With newWindow the primary
    // opens an additional window instead of revealing inside the current one.
    bool sendMessage(const QString &path, bool newWindow = false);
    void setRevealHandler(const std::function<void(const QString &)> &handler);
    void setNewWindowHandler(const std::function<void(const QString &)> &handler);

private:
    void handleConnection();
    void deliverReveal(const QString &path);
    void deliverNewWindow(const QString &path);

    bool m_primary = false;
    QString m_serverName;
    QLocalServer *m_server = nullptr;
    QStringList m_pendingReveal;
    QStringList m_pendingNewWindow;
    std::function<void(const QString &)> m_revealHandler;
    std::function<void(const QString &)> m_newWindowHandler;
};
