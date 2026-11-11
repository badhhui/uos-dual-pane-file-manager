#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class QLocalServer;

// Deepin/UOS-style single-instance forwarding: every secondary launch opens an
// additional window inside the primary process (dock re-launch, "show item"
// calls from WPS, plain directory arguments — all become new windows).
class SingleInstance : public QObject
{
public:
    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    bool isPrimary() const;

    // Ask the primary instance to open a new window. `directory` seeds the left
    // pane (empty = default start folder), `revealItem` is a file to select
    // after opening, `rightDirectory` optionally seeds the right pane.
    bool sendOpenWindow(const QString &directory, const QString &revealItem,
                        const QString &rightDirectory = QString());

    void setNewWindowHandler(
        const std::function<void(const QString &dir, const QString &right,
                                 const QString &item)> &handler);

private:
    struct PendingOpen {
        QString dir;
        QString right;
        QString item;
    };

    void handleConnection();
    void deliverOpen(const PendingOpen &open);

    bool m_primary = false;
    QString m_serverName;
    QLocalServer *m_server = nullptr;
    QVector<PendingOpen> m_pending;
    std::function<void(const QString &dir, const QString &right,
                       const QString &item)> m_handler;
};
