#include "fileoperations.h"

#include "formatters.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QProcessEnvironment>
#include <QRegularExpression>

namespace {
QString trashRootPath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/Trash");
}

bool moveIntoXdgTrash(const QString &path, QString *error)
{
    QFileInfo info(path);
    if (!info.exists()) {
        if (error) *error = QStringLiteral("文件不存在：%1").arg(path);
        return false;
    }
    const QString root = trashRootPath();
    QDir rootDir(root);
    if (!rootDir.exists() && !QDir().mkpath(root))
    {
        if (error) *error = QStringLiteral("无法创建回收站目录");
        return false;
    }
    if (!QDir().mkpath(root + QStringLiteral("/files")) || !QDir().mkpath(root + QStringLiteral("/info"))) {
        if (error) *error = QStringLiteral("无法创建回收站目录");
        return false;
    }

    const QString originalName = info.fileName();
    QString target = QDir(root + QStringLiteral("/files")).filePath(originalName);
    QFileInfo targetInfo(target);
    QString base = targetInfo.completeBaseName();
    QString suffix = targetInfo.suffix().isEmpty() ? QString() : QStringLiteral(".") + targetInfo.suffix();
    int number = 1;
    while (QFileInfo::exists(target)) {
        target = QDir(root + QStringLiteral("/files")).filePath(
            QStringLiteral("%1 (%2)%3").arg(base).arg(number).arg(suffix));
        ++number;
    }
    if (!QFile::rename(path, target)) {
        if (!QDir().rename(path, target)) {
            if (error) *error = QStringLiteral("无法移动文件到回收站（可能存在跨分区限制）");
            return false;
        }
    }
    const QString infoPath = QDir(root + QStringLiteral("/info")).filePath(
        QFileInfo(target).fileName() + QStringLiteral(".trashinfo"));
    QFile infoFile(infoPath);
    if (!infoFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QFile::rename(target, path);
        if (error) *error = QStringLiteral("无法写入回收站信息");
        return false;
    }
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(info.absoluteFilePath()));
    const QByteArray content = QStringLiteral("[Trash Info]\nPath=%1\nDeletionDate=%2\n")
        .arg(encoded, QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddThh:mm:ss")))
        .toUtf8();
    if (infoFile.write(content) != content.size()) {
        infoFile.close();
        infoFile.remove();
        QFile::rename(target, path);
        if (error) *error = QStringLiteral("无法写入回收站信息");
        return false;
    }
    infoFile.close();
    return true;
}
}

QString trashFilesPath()
{
    return trashRootPath() + QStringLiteral("/files");
}

bool sendToTrash(const QStringList &paths, QString *error)
{
    if (paths.isEmpty()) {
        if (error) *error = QStringLiteral("没有选择文件");
        return false;
    }
    const QString gio = QStandardPaths::findExecutable(QStringLiteral("gio"));
    for (const QString &path : paths) {
        if (!QFileInfo::exists(path)) {
            if (error) *error = QStringLiteral("文件不存在：%1").arg(path);
            return false;
        }
        bool moved = false;
        if (!gio.isEmpty()) {
            QProcess process;
            process.start(gio, QStringList() << QStringLiteral("trash") << path);
            if (process.waitForStarted(2500) && process.waitForFinished(20000)
                && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
                moved = true;
            } else if (!QFileInfo::exists(path)) {
                // Some desktops move the file but return a non-zero status.
                moved = true;
            }
        }
        if (!moved) {
            if (!moveIntoXdgTrash(path, error))
                return false;
        }
    }
    return true;
}

bool deletePathsPermanently(const QStringList &paths, QString *error)
{
    if (paths.isEmpty()) {
        if (error) *error = QStringLiteral("没有选择文件");
        return false;
    }
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!info.exists() && !info.isSymLink()) {
            if (error) *error = QStringLiteral("文件不存在：%1").arg(path);
            return false;
        }
        bool removed = false;
        if (info.isDir() && !info.isSymLink())
            removed = QDir(path).removeRecursively();
        else
            removed = QFile::remove(path);
        if (!removed) {
            if (error) *error = QStringLiteral("无法彻底删除：%1").arg(path);
            return false;
        }
    }
    return true;
}

QVector<TrashItem> listTrashItems(QString *error)
{

    QVector<TrashItem> items;
    const QString root = trashRootPath();
    QDir infoDir(root + QStringLiteral("/info"));
    if (!infoDir.exists())
        return items;
    const QFileInfoList infos = infoDir.entryInfoList(
        QStringList() << QStringLiteral("*.trashinfo"), QDir::Files, QDir::Time);
    for (const QFileInfo &infoFile : infos) {
        QFile file(infoFile.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QTextStream stream(&file);
        stream.setCodec("UTF-8");
        bool inSection = false;
        QString original;
        QDateTime deleted;
        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (line == QStringLiteral("[Trash Info]")) {
                inSection = true;
                continue;
            }
            if (!inSection)
                continue;
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals <= 0)
                continue;
            const QString key = line.left(equals);
            const QString value = line.mid(equals + 1);
            if (key == QStringLiteral("Path"))
                original = QUrl::fromPercentEncoding(value.toUtf8());
            else if (key == QStringLiteral("DeletionDate"))
                deleted = QDateTime::fromString(value, Qt::ISODate);
        }
        file.close();
        if (original.isEmpty())
            continue;
        QString name = infoFile.fileName();
        if (name.endsWith(QStringLiteral(".trashinfo")))
            name.chop(10);
        TrashItem item;
        item.name = name;
        item.trashPath = QDir(root + QStringLiteral("/files")).filePath(name);
        item.originalPath = original;
        item.deleted = deleted;
        if (QFileInfo::exists(item.trashPath) || QFileInfo(item.trashPath).isSymLink())
            items.append(item);
    }
    if (error)
        error->clear();
    return items;
}

bool restoreTrashItems(const QStringList &trashPaths, QString *error)
{
    if (trashPaths.isEmpty()) {
        if (error) *error = QStringLiteral("没有选择要还原的项目");
        return false;
    }
    const QString root = trashRootPath();
    for (const QString &trashPath : trashPaths) {
        const QFileInfo trashInfo(trashPath);
        if (!trashInfo.exists() && !trashInfo.isSymLink()) {
            if (error) *error = QStringLiteral("回收站项目已不存在：%1").arg(trashPath);
            return false;
        }
        const QString name = trashInfo.fileName();
        QFile infoFile(root + QStringLiteral("/info/") + name + QStringLiteral(".trashinfo"));
        QString original;
        if (infoFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&infoFile);
            stream.setCodec("UTF-8");
            bool inSection = false;
            while (!stream.atEnd()) {
                const QString line = stream.readLine().trimmed();
                if (line == QStringLiteral("[Trash Info]")) {
                    inSection = true;
                    continue;
                }
                if (inSection && line.startsWith(QStringLiteral("Path="))) {
                    original = QUrl::fromPercentEncoding(
                        line.mid(QStringLiteral("Path=").size()).toUtf8());
                    break;
                }
            }
            infoFile.close();
        }
        if (original.isEmpty()) {
            if (error) *error = QStringLiteral("无法读取回收站原始路径：%1").arg(name);
            return false;
        }
        const QFileInfo originalInfo(original);
        QDir parent(originalInfo.absolutePath());
        if (!parent.exists()) {
            if (error) *error = QStringLiteral("原始文件夹不存在：%1").arg(parent.absolutePath());
            return false;
        }
        QString target = original;
        const QFileInfo targetInfo(target);
        if (targetInfo.exists() || targetInfo.isSymLink())
            target = uniqueDestinationPath(parent.absolutePath(), originalInfo.fileName());
        bool moved = QFile::rename(trashPath, target);
        if (!moved)
            moved = QDir().rename(trashPath, target);
        if (!moved) {
            if (error) *error = QStringLiteral("无法还原到：%1").arg(target);
            return false;
        }
        QFile::remove(infoFile.fileName());
    }
    return true;
}

bool emptyTrash(QString *error)
{
    const QString root = trashRootPath();
    QDir files(root + QStringLiteral("/files"));
    if (files.exists() && !files.removeRecursively()) {
        if (error) *error = QStringLiteral("无法清空回收站文件");
        return false;
    }
    QDir info(root + QStringLiteral("/info"));
    if (info.exists() && !info.removeRecursively()) {
        if (error) *error = QStringLiteral("无法清空回收站信息");
        return false;
    }
    QDir().mkpath(root + QStringLiteral("/files"));
    QDir().mkpath(root + QStringLiteral("/info"));
    return true;
}

bool createNewFolder(const QString &parentPath, const QString &name, QString *error)
{
    QDir parent(parentPath);
    if (!parent.exists()) {
        if (error) *error = QStringLiteral("当前文件夹不存在");
        return false;
    }
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")
        || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        if (error) *error = QStringLiteral("名称无效");
        return false;
    }
    if (QFileInfo::exists(parent.filePath(name))) {
        if (error) *error = QStringLiteral("同名文件或文件夹已存在");
        return false;
    }
    if (!parent.mkdir(name)) {
        if (error) *error = QStringLiteral("创建文件夹失败，请检查权限");
        return false;
    }
    return true;
}

bool createNewFile(const QString &parentPath, const QString &name, QString *error)
{
    QDir parent(parentPath);
    if (!parent.exists()) {
        if (error) *error = QStringLiteral("当前文件夹不存在");
        return false;
    }
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")
        || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        if (error) *error = QStringLiteral("名称无效");
        return false;
    }
    const QString path = parent.filePath(name);
    if (QFileInfo::exists(path)) {
        if (error) *error = QStringLiteral("同名文件或文件夹已存在");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error) *error = QStringLiteral("创建文件失败，请检查权限");
        return false;
    }
    file.close();
    return true;
}

bool renamePath(const QString &oldPath, const QString &newPath, QString *error)
{
    QFileInfo oldInfo(oldPath);
    if (!oldInfo.exists()) {
        if (error) *error = QStringLiteral("文件不存在");
        return false;
    }
    QFileInfo newInfo(newPath);
    if (newInfo.exists()) {
        if (error) *error = QStringLiteral("同名文件或文件夹已存在");
        return false;
    }
    if (newInfo.fileName().isEmpty() || newInfo.fileName() == QStringLiteral(".")
        || newInfo.fileName() == QStringLiteral("..")) {
        if (error) *error = QStringLiteral("名称无效");
        return false;
    }
    if (!QDir().rename(oldInfo.absoluteFilePath(), newInfo.absoluteFilePath())) {
        if (error) *error = QStringLiteral("重命名失败，请检查权限");
        return false;
    }
    return true;
}

bool ejectStorage(const QString &devicePath, const QString &mountPath, QString *error)
{
    if (mountPath.isEmpty()) {
        if (error) *error = QStringLiteral("没有可弹出的挂载点");
        return false;
    }
    const QString udisks = QStandardPaths::findExecutable(QStringLiteral("udisksctl"));
    if (!udisks.isEmpty() && devicePath.startsWith(QStringLiteral("/dev/"))) {
        QProcess unmount;
        unmount.start(udisks, QStringList() << QStringLiteral("unmount")
                                           << QStringLiteral("-b") << devicePath);
        if (unmount.waitForStarted(2500) && unmount.waitForFinished(12000)
            && unmount.exitStatus() == QProcess::NormalExit && unmount.exitCode() == 0) {
            // Powering off is a best-effort improvement for USB sticks.
            QProcess powerOff;
            powerOff.start(udisks, QStringList() << QStringLiteral("power-off")
                                                << QStringLiteral("-b") << devicePath);
            if (powerOff.waitForStarted(1500))
                powerOff.waitForFinished(5000);
            return true;
        }
        if (error) {
            *error = QString::fromLocal8Bit(unmount.readAllStandardError()).trimmed();
            if (error->isEmpty())
                *error = QStringLiteral("无法卸载该设备");
        }
    }

    const QString gio = QStandardPaths::findExecutable(QStringLiteral("gio"));
    if (!gio.isEmpty()) {
        const QString uri = QUrl::fromLocalFile(mountPath).toString(QUrl::FullyEncoded);
        QProcess eject;
        eject.start(gio, QStringList() << QStringLiteral("mount")
                                       << QStringLiteral("-e") << uri);
        if (eject.waitForStarted(2500) && eject.waitForFinished(12000)
            && eject.exitStatus() == QProcess::NormalExit && eject.exitCode() == 0)
            return true;
        if (error) {
            const QString detail = QString::fromLocal8Bit(eject.readAllStandardError()).trimmed();
            if (!detail.isEmpty())
                *error = detail;
        }
    }

    const QString umount = QStandardPaths::findExecutable(QStringLiteral("umount"));
    if (!umount.isEmpty()) {
        QProcess unmount;
        unmount.start(umount, QStringList() << mountPath);
        if (unmount.waitForStarted(2500) && unmount.waitForFinished(10000)
            && unmount.exitStatus() == QProcess::NormalExit && unmount.exitCode() == 0)
            return true;
        if (error && error->isEmpty()) {
            *error = QString::fromLocal8Bit(unmount.readAllStandardError()).trimmed();
        }
    }
    if (error && error->isEmpty())
        *error = QStringLiteral("系统没有可用的弹出方式（需要 udisksctl 或 gio）");
    return false;
}

bool canEscalateWithPkexec()
{
    return !QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty();
}

namespace {
bool runPrivilegedCommand(const QString &program, const QStringList &arguments, bool admin,
                          QString *error)
{
    QString executable = program;
    QStringList args = arguments;
    if (admin) {
        const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        if (pkexec.isEmpty()) {
            if (error) *error = QStringLiteral("系统缺少 pkexec，无法使用管理员权限");
            return false;
        }
        args.prepend(executable);
        executable = pkexec;
    }
    QProcess process;
    process.start(executable, args);
    if (!process.waitForStarted(3000) || !process.waitForFinished(15000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            *error = stderrText.isEmpty() ? QStringLiteral("操作失败，可能没有权限") : stderrText;
        }
        return false;
    }
    return true;
}
}

bool applyPermissions(const QStringList &paths, int mode, bool admin, QString *error)
{
    if (paths.isEmpty() || mode < 0 || mode > 07777) {
        if (error) *error = QStringLiteral("权限值无效");
        return false;
    }
    const QString chmod = QStandardPaths::findExecutable(QStringLiteral("chmod"));
    if (chmod.isEmpty()) {
        if (error) *error = QStringLiteral("系统缺少 chmod");
        return false;
    }
    const QString modeText = QString::number(mode, 8).rightJustified(3, QLatin1Char('0'));
    for (const QString &path : paths) {
        if (!runPrivilegedCommand(chmod, QStringList() << modeText << QStringLiteral("--") << path,
                                  admin, error))
            return false;
    }
    return true;
}

bool applyOwnership(const QStringList &paths, const QString &owner, const QString &group,
                    bool admin, QString *error)
{
    if (paths.isEmpty()) {
        if (error) *error = QStringLiteral("没有选择文件");
        return false;
    }
    const QString ownerText = owner.trimmed();
    const QString groupText = group.trimmed();
    if (ownerText.isEmpty() && groupText.isEmpty()) {
        if (error) *error = QStringLiteral("属主和用户组不能同时为空");
        return false;
    }
    const QRegularExpression validName(QStringLiteral("^[A-Za-z0-9_.][A-Za-z0-9_.-]*$"));
    if ((!ownerText.isEmpty() && !validName.match(ownerText).hasMatch())
        || (!groupText.isEmpty() && !validName.match(groupText).hasMatch())) {
        if (error) *error = QStringLiteral("属主或用户组名称无效");
        return false;
    }
    const QString chown = QStandardPaths::findExecutable(QStringLiteral("chown"));
    if (chown.isEmpty()) {
        if (error) *error = QStringLiteral("系统缺少 chown");
        return false;
    }
    const QString spec = ownerText + QLatin1Char(':') + groupText;
    for (const QString &path : paths) {
        if (!runPrivilegedCommand(chown, QStringList() << spec << QStringLiteral("--") << path,
                                  admin, error))
            return false;
    }
    return true;
}
