#include "ejecttask.h"

#include "fileoperations.h"

EjectTask::EjectTask(const QString &devicePath, const QString &mountPath, QObject *parent)
    : QThread(parent)
    , m_devicePath(devicePath)
    , m_mountPath(mountPath)
{
    setObjectName(QStringLiteral("EjectTask"));
}

void EjectTask::run()
{
    QString error;
    const bool ok = ejectStorage(m_devicePath, m_mountPath, &error);
    emit result(ok, error);
}
