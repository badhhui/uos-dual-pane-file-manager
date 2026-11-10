#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

struct TrashItem
{
    QString name;
    QString trashPath;
    QString originalPath;
    QDateTime deleted;
};

bool sendToTrash(const QStringList &paths, QString *error);
bool deletePathsPermanently(const QStringList &paths, QString *error);
QString trashFilesPath();
QVector<TrashItem> listTrashItems(QString *error = nullptr);
bool restoreTrashItems(const QStringList &trashPaths, QString *error);
bool emptyTrash(QString *error);
bool createNewFolder(const QString &parentPath, const QString &name, QString *error);
bool createNewFile(const QString &parentPath, const QString &name, QString *error);
bool renamePath(const QString &oldPath, const QString &newPath, QString *error);
bool ejectStorage(const QString &devicePath, const QString &mountPath, QString *error);
bool applyPermissions(const QStringList &paths, int mode, bool admin, QString *error);
bool applyOwnership(const QStringList &paths, const QString &owner, const QString &group,
                    bool admin, QString *error);
bool canEscalateWithPkexec();
