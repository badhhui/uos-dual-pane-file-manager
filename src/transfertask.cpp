#include "transfertask.h"

#include "formatters.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QVector>

namespace {
const int kChunkSize = 512 * 1024;
}

TransferTask::TransferTask(int id, TransferOperation operation, const QStringList &sources,
                           const QString &destination, const ConflictActions &conflicts,
                           QObject *parent)
    : QThread(parent)
    , m_id(id)
    , m_operation(operation)
    , m_sources(sources)
    , m_destination(destination)
    , m_conflicts(conflicts)
{
    setObjectName(QStringLiteral("TransferTask-%1").arg(id));
}

TransferTask::~TransferTask()
{
    if (isRunning()) {
        cancel();
        wait(5000);
    }
}

int TransferTask::taskId() const
{
    return m_id;
}

TransferOperation TransferTask::operation() const
{
    return m_operation;
}

void TransferTask::setPaused(bool paused)
{
    QMutexLocker locker(&m_mutex);
    m_paused = paused;
    if (!paused)
        m_waitCondition.wakeAll();
}

bool TransferTask::isPaused() const
{
    QMutexLocker locker(&m_mutex);
    return m_paused;
}

void TransferTask::cancel()
{
    QMutexLocker locker(&m_mutex);
    m_canceled = true;
    m_waitCondition.wakeAll();
}

bool TransferTask::waitIfPaused()
{
    QMutexLocker locker(&m_mutex);
    while (m_paused && !m_canceled)
        m_waitCondition.wait(&m_mutex);
    return !m_canceled;
}

bool TransferTask::validateDestination(QString &error) const
{
    QFileInfo destinationInfo(m_destination);
    if (!destinationInfo.exists() || !destinationInfo.isDir()) {
        error = QStringLiteral("目标文件夹不存在：%1").arg(m_destination);
        return false;
    }
    if (!destinationInfo.isWritable()) {
        error = QStringLiteral("目标文件夹没有写入权限");
        return false;
    }
    for (const QString &source : m_sources) {
        QFileInfo info(source);
        if (!info.exists()) {
            error = QStringLiteral("源文件不存在：%1").arg(source);
            return false;
        }
        if (QDir::cleanPath(source) == QDir::rootPath()) {
            error = QStringLiteral("不支持复制或移动系统根目录");
            return false;
        }
        if (info.isDir()) {
            const QString canonicalSource = info.canonicalFilePath();
            const QString canonicalDestination = destinationInfo.canonicalFilePath();
            const bool lexicalInside = isPathInside(m_destination, source);
            const bool canonicalInside = !canonicalSource.isEmpty()
                && !canonicalDestination.isEmpty()
                && isPathInside(canonicalDestination, canonicalSource);
            if (lexicalInside || canonicalInside) {
                error = QStringLiteral("不能将文件夹复制到它自身内部");
                return false;
            }
        }
    }
    return true;
}

bool TransferTask::countPath(const QString &path, qint64 &bytes, int &files, QString &error)
{
    if (!waitIfPaused())
        return false;
    QFileInfo info(path);
    if (!info.exists()) {
        error = QStringLiteral("文件不存在：%1").arg(path);
        return false;
    }
    if (!info.isSymLink() && info.isDir())
        return countDirectory(path, bytes, files, error);
    if (!info.isSymLink() && !info.isFile()) {
        error = QStringLiteral("不支持的特殊文件：%1").arg(path);
        return false;
    }
    ++files;
    if (!info.isSymLink() && info.isFile())
        bytes += info.size();
    return true;
}

bool TransferTask::countDirectory(const QString &path, qint64 &bytes, int &files, QString &error)
{
    QDir directory(path);
    if (!directory.isReadable()) {
        error = QStringLiteral("没有权限读取：%1").arg(path);
        return false;
    }
    const QFileInfoList entries = directory.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
    for (const QFileInfo &entry : entries) {
        if (!waitIfPaused())
            return false;
        if (entry.isSymLink()) {
            ++files;
        } else if (entry.isDir()) {
            if (!countDirectory(entry.absoluteFilePath(), bytes, files, error))
                return false;
        } else {
            ++files;
            bytes += entry.size();
        }
    }
    return true;
}

void TransferTask::run()
{
    m_lastEmit.start();
    QString error;
    bool canceled = false;

    if (!validateDestination(error)) {
        emit finished(m_id, false, false, error);
        return;
    }

    struct Plan {
        QString source;
        QString target;
        ConflictAction action = ConflictAction::KeepBoth;
        bool sourceIsDir = false;
    };
    QVector<Plan> plans;
    for (const QString &source : m_sources) {
        QFileInfo sourceInfo(source);
        if (!sourceInfo.exists()) {
            error = QStringLiteral("源文件不存在：%1").arg(source);
            emit finished(m_id, false, false, error);
            return;
        }
        const QString target = QDir(m_destination).filePath(sourceInfo.fileName());
        const QFileInfo targetInfo(target);
        const bool targetExists = targetInfo.exists() || targetInfo.isSymLink();
        const ConflictAction action = m_conflicts.value(source, ConflictAction::KeepBoth);
        if (targetExists && action == ConflictAction::Skip)
            continue;
        Plan plan;
        plan.source = source;
        plan.target = target;
        plan.action = action;
        plan.sourceIsDir = sourceInfo.isDir() && !sourceInfo.isSymLink();
        plans.append(plan);
    }

    bool scanOk = true;
    for (const Plan &plan : plans) {
        if (!countPath(plan.source, m_totalBytes, m_totalFiles, error)) {
            scanOk = false;
            break;
        }
    }
    if (!scanOk) {
        if (!waitIfPaused())
            emit finished(m_id, false, true, QStringLiteral("已取消"));
        else
            emit finished(m_id, false, false, error.isEmpty() ? QStringLiteral("无法读取源文件") : error);
        return;
    }

    emit progress(m_id, 0, m_totalBytes, 0, m_totalFiles, QStringLiteral("准备传输…"));

    for (const Plan &plan : plans) {
        if (!waitIfPaused()) {
            canceled = true;
            break;
        }
        QFileInfo sourceInfo(plan.source);
        if (!sourceInfo.exists()) {
            error = QStringLiteral("源文件不存在：%1").arg(plan.source);
            break;
        }
        m_currentSource = plan.source;
        m_currentTarget = plan.target;
        m_sourceRemovalStarted = false;
        m_targetIsNew = false;
        m_mergeCurrent = false;

        QFileInfo targetInfo(plan.target);
        bool targetExists = targetInfo.exists() || targetInfo.isSymLink();
        ConflictAction action = plan.action;
        if (targetExists
            && QFileInfo(plan.source).absoluteFilePath() == QFileInfo(plan.target).absoluteFilePath()
            && (action == ConflictAction::Replace || action == ConflictAction::Merge)) {
            action = ConflictAction::KeepBoth;
        }
        if (targetExists && action == ConflictAction::Replace) {
            m_currentTarget.clear();
            if (!removeExistingTarget(plan.target, error))
                break;
            targetExists = false;
            m_currentTarget = plan.target;
        }

        bool ok = false;
        if (targetExists && action == ConflictAction::Merge && plan.sourceIsDir
            && targetInfo.isDir() && !targetInfo.isSymLink()) {
            m_mergeCurrent = true;
            m_targetIsNew = false;
            m_currentTarget = plan.target;
            ok = copyDirectoryMerge(plan.source, plan.target, error);
        } else {
            QString target = plan.target;
            if (targetExists)
                target = uniqueDestinationPath(m_destination, sourceInfo.fileName());
            m_currentTarget = target;
            m_targetIsNew = true;
            if (plan.sourceIsDir) {
                ok = QDir().mkpath(target);
                if (!ok)
                    error = QStringLiteral("无法创建目标文件夹：%1").arg(target);
                else
                    ok = copyDirectory(plan.source, target, error);
            } else {
                ok = copyOne(plan.source, target, error);
            }
        }
        if (!ok) {
            if (!waitIfPaused())
                canceled = true;
            break;
        }

        if (m_operation == TransferOperation::Move) {
            if (!waitIfPaused()) {
                canceled = true;
                break;
            }
            m_sourceRemovalStarted = true;
            if (!removePath(plan.source, error)) {
                break;
            }
        }
        m_currentSource.clear();
        m_currentTarget.clear();
        m_targetIsNew = true;
        m_mergeCurrent = false;
    }

    if (canceled) {
        cleanupCurrentTarget();
        emit finished(m_id, false, true, QStringLiteral("已取消"));
        return;
    }
    if (!error.isEmpty()) {
        cleanupCurrentTarget();
        emit finished(m_id, false, false, error);
        return;
    }

    emit progress(m_id, m_totalBytes, m_totalBytes, m_totalFiles, m_totalFiles, QString());
    emit finished(m_id, true, false, QString());
}
bool TransferTask::removeExistingTarget(const QString &path, QString &error)
{
    QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return true;
    if (info.isSymLink() || !info.isDir()) {
        if (!QFile::remove(path)) {
            error = QStringLiteral("无法删除目标同名文件：%1").arg(path);
            return false;
        }
        return true;
    }
    if (!QDir(path).removeRecursively()) {
        error = QStringLiteral("无法删除目标同名文件夹：%1").arg(path);
        return false;
    }
    return true;
}

bool TransferTask::copyDirectoryMerge(const QString &source, const QString &target, QString &error)
{
    if (!waitIfPaused())
        return false;
    QDir sourceDir(source);
    if (!sourceDir.isReadable()) {
        error = QStringLiteral("没有权限读取：%1").arg(source);
        return false;
    }
    if (!QDir().mkpath(target)) {
        error = QStringLiteral("无法创建目标文件夹：%1").arg(target);
        return false;
    }
    const QFileInfoList entries = sourceDir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
    for (const QFileInfo &entry : entries) {
        if (!waitIfPaused())
            return false;
        QString childTarget = QDir(target).filePath(entry.fileName());
        QFileInfo targetInfo(childTarget);
        const bool exists = targetInfo.exists() || targetInfo.isSymLink();
        if (entry.isSymLink()) {
            if (exists)
                childTarget = uniqueDestinationPath(target, entry.fileName());
            if (!copySymlink(entry.absoluteFilePath(), childTarget, error))
                return false;
        } else if (entry.isDir()) {
            if (exists && targetInfo.isDir() && !targetInfo.isSymLink()) {
                if (!copyDirectoryMerge(entry.absoluteFilePath(), childTarget, error))
                    return false;
            } else {
                if (exists)
                    childTarget = uniqueDestinationPath(target, entry.fileName());
                if (!copyDirectory(entry.absoluteFilePath(), childTarget, error))
                    return false;
            }
        } else if (entry.isFile()) {
            if (exists)
                childTarget = uniqueDestinationPath(target, entry.fileName());
            if (!copyFile(entry.absoluteFilePath(), childTarget, error))
                return false;
        } else {
            error = QStringLiteral("不支持的特殊文件：%1").arg(entry.absoluteFilePath());
            return false;
        }
    }
    return true;
}

bool TransferTask::copyOne(const QString &source, const QString &target, QString &error)
{
    QFileInfo info(source);
    if (info.isSymLink())
        return copySymlink(source, target, error);
    if (info.isDir())
        return copyDirectory(source, target, error);
    if (!info.isFile()) {
        error = QStringLiteral("不支持的特殊文件：%1").arg(source);
        return false;
    }
    return copyFile(source, target, error);
}

bool TransferTask::copyDirectory(const QString &source, const QString &target, QString &error)
{
    if (!waitIfPaused())
        return false;
    QDir sourceDir(source);
    if (!sourceDir.isReadable()) {
        error = QStringLiteral("没有权限读取：%1").arg(source);
        return false;
    }
    if (!QDir().mkpath(target)) {
        error = QStringLiteral("无法创建目标文件夹：%1").arg(target);
        return false;
    }
    const QFileInfoList entries = sourceDir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
    for (const QFileInfo &entry : entries) {
        if (!waitIfPaused())
            return false;
        const QString childTarget = QDir(target).filePath(entry.fileName());
        if (entry.isSymLink()) {
            if (!copySymlink(entry.absoluteFilePath(), childTarget, error))
                return false;
        } else if (entry.isDir()) {
            if (!copyDirectory(entry.absoluteFilePath(), childTarget, error))
                return false;
        } else if (entry.isFile()) {
            if (!copyFile(entry.absoluteFilePath(), childTarget, error))
                return false;
        } else {
            error = QStringLiteral("不支持的特殊文件：%1").arg(entry.absoluteFilePath());
            return false;
        }
    }
    return true;
}

bool TransferTask::copyFile(const QString &source, const QString &target, QString &error)
{
    if (!waitIfPaused())
        return false;
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("无法读取：%1").arg(source);
        return false;
    }
    if (QFileInfo::exists(target)) {
        error = QStringLiteral("目标已存在：%1").arg(target);
        return false;
    }
    QFile output(target);
    if (!output.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("无法写入：%1").arg(target);
        return false;
    }

    char buffer[kChunkSize];
    bool ok = true;
    while (true) {
        if (!waitIfPaused()) {
            ok = false;
            break;
        }
        const qint64 readBytes = input.read(buffer, kChunkSize);
        if (readBytes < 0) {
            error = QStringLiteral("读取失败：%1").arg(source);
            ok = false;
            break;
        }
        if (readBytes == 0)
            break;
        qint64 written = 0;
        while (written < readBytes) {
            const qint64 chunk = output.write(buffer + written, readBytes - written);
            if (chunk <= 0) {
                error = QStringLiteral("写入失败：%1").arg(target);
                ok = false;
                break;
            }
            written += chunk;
        }
        if (!ok)
            break;
        m_doneBytes += readBytes;
        emitProgress(QFileInfo(source).fileName());
    }
    output.close();
    input.close();
    if (!ok) {
        QFile::remove(target);
        return false;
    }
    QFile::setPermissions(target, QFileInfo(source).permissions());
    ++m_doneFiles;
    emitProgress(QFileInfo(source).fileName(), true);
    return true;
}

bool TransferTask::copySymlink(const QString &source, const QString &target, QString &error)
{
    if (!waitIfPaused())
        return false;
    if (QFileInfo::exists(target)) {
        error = QStringLiteral("目标已存在：%1").arg(target);
        return false;
    }
    const QString linkTarget = QFileInfo(source).symLinkTarget();
    if (!QFile::link(linkTarget, target)) {
        error = QStringLiteral("无法创建符号链接：%1").arg(target);
        return false;
    }
    ++m_doneFiles;
    emitProgress(QFileInfo(source).fileName(), true);
    return true;
}

bool TransferTask::removePath(const QString &path, QString &error) const
{
    QFileInfo info(path);
    if (info.isSymLink() || !info.isDir()) {
        if (!QFile::remove(path)) {
            error = QStringLiteral("已复制，但无法删除源文件：%1").arg(path);
            return false;
        }
        return true;
    }
    QDir directory(path);
    if (!directory.removeRecursively()) {
        error = QStringLiteral("已复制，但无法删除源文件夹：%1").arg(path);
        return false;
    }
    return true;
}

void TransferTask::cleanupCurrentTarget()
{
    if (m_currentTarget.isEmpty() || !m_targetIsNew)
        return;
    if (m_operation == TransferOperation::Move && m_sourceRemovalStarted) {
        // Source deletion has started; never delete the destination in that case.
        return;
    }
    QFileInfo targetInfo(m_currentTarget);
    if (targetInfo.isSymLink() || !targetInfo.isDir()) {
        QFile::remove(m_currentTarget);
    } else if (targetInfo.isDir()) {
        QDir(m_currentTarget).removeRecursively();
    }
}

void TransferTask::emitProgress(const QString &currentName, bool force)
{
    if (!force && m_lastEmit.isValid() && m_lastEmit.elapsed() < 100)
        return;
    if (m_lastEmit.isValid())
        m_lastEmit.restart();
    emit progress(m_id, m_doneBytes, m_totalBytes, m_doneFiles, m_totalFiles, currentName);
}
