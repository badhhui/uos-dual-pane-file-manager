#include "fileoperations.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {
bool writeFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("data\n") == 5;
}
}

int main(int argc, char **argv)
{
    QTemporaryDir temp;
    if (!temp.isValid())
        return 2;
    const QString root = temp.path();
    qputenv("XDG_DATA_HOME", (root + "/xdg-data").toUtf8());
    QCoreApplication app(argc, argv);
    if (!writeFile(root + "/a.txt")) {
        qCritical() << "fixture failed";
        return 2;
    }
    QString error;
    if (!createNewFolder(root, QStringLiteral("新文件夹"), &error)
        || !QFileInfo(root + "/新文件夹").isDir()) {
        qCritical() << "mkdir failed" << error;
        return 1;
    }
    if (!createNewFile(root, QStringLiteral("新建文本.txt"), &error)
        || !QFileInfo(root + "/新建文本.txt").isFile()) {
        qCritical() << "new file failed" << error;
        return 1;
    }
    if (!renamePath(root + "/a.txt", root + "/b.txt", &error)
        || QFileInfo::exists(root + "/a.txt") || !QFileInfo::exists(root + "/b.txt")) {
        qCritical() << "rename failed" << error;
        return 1;
    }
    if (!sendToTrash(QStringList() << root + "/b.txt", &error)
        || QFileInfo::exists(root + "/b.txt")) {
        qCritical() << "trash failed" << error;
        return 1;
    }
    if (!sendToTrash(QStringList() << root + "/新文件夹", &error)
        || QFileInfo::exists(root + "/新文件夹")) {
        qCritical() << "trash dir failed" << error;
        return 1;
    }
    const QString trash = trashFilesPath();
    if (!QFileInfo::exists(trash + "/b.txt") && !QFileInfo::exists(trash + "/新文件夹")) {
        qCritical() << "trash destination missing" << trash;
        return 1;
    }
    const QVector<TrashItem> items = listTrashItems(&error);
    if (items.size() < 2) {
        qCritical() << "trash listing failed" << items.size() << error;
        return 1;
    }
    QStringList restore;
    for (const TrashItem &item : items) {
        if (item.name == QStringLiteral("b.txt") || item.name == QStringLiteral("新文件夹"))
            restore.append(item.trashPath);
    }
    if (restore.size() != 2 || !restoreTrashItems(restore, &error)
        || !QFileInfo::exists(root + "/b.txt") || !QFileInfo(root + "/新文件夹").isDir()) {
        qCritical() << "trash restore failed" << restore.size() << error;
        return 1;
    }
    if (!sendToTrash(QStringList() << root + "/b.txt", &error)
        || !emptyTrash(&error) || !listTrashItems().isEmpty()) {
        qCritical() << "empty trash failed" << error;
        return 1;
    }
    if (!writeFile(root + "/perm.txt") || !applyPermissions(QStringList() << root + "/perm.txt",
                                                            0600, false, &error)) {
        qCritical() << "chmod 0600 failed" << error;
        return 1;
    }
    QFile::Permissions mode = QFileInfo(root + "/perm.txt").permissions();
    if (!mode.testFlag(QFileDevice::ReadOwner) || !mode.testFlag(QFileDevice::WriteOwner)
        || mode.testFlag(QFileDevice::ReadGroup) || mode.testFlag(QFileDevice::ReadOther)) {
        qCritical() << "chmod 0600 verification failed";
        return 1;
    }
    if (!applyPermissions(QStringList() << root + "/perm.txt", 0644, false, &error)
        || !QFileInfo(root + "/perm.txt").permissions().testFlag(QFileDevice::ReadOther)) {
        qCritical() << "chmod 0644 failed" << error;
        return 1;
    }
    qInfo() << "FILEOPS_SMOKE_OK";
    return 0;
}
