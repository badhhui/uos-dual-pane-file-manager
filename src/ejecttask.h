#pragma once

#include <QThread>

class EjectTask : public QThread
{
    Q_OBJECT

public:
    EjectTask(const QString &devicePath, const QString &mountPath, QObject *parent = nullptr);

signals:
    void result(bool success, const QString &error);

protected:
    void run() override;

private:
    QString m_devicePath;
    QString m_mountPath;
};
