#include "transfermanager.h"

#include "formatters.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>

TransferManager::TransferManager(QObject *parent)
    : QObject(parent)
{
}

TransferManager::~TransferManager()
{
    shutdown();
}

int TransferManager::enqueue(TransferOperation operation, const QStringList &sources,
                             const QString &destination, QString *error,
                             const ConflictActions &conflicts)
{
    if (sources.isEmpty()) {
        if (error) *error = QStringLiteral("没有选择要传输的项目");
        return -1;
    }
    QFileInfo destinationInfo(destination);
    if (!destinationInfo.exists() || !destinationInfo.isDir()) {
        if (error) *error = QStringLiteral("目标文件夹不存在");
        return -1;
    }
    if (!destinationInfo.isWritable()) {
        if (error) *error = QStringLiteral("目标文件夹没有写入权限");
        return -1;
    }
    for (const QString &source : sources) {
        QFileInfo sourceInfo(source);
        if (!sourceInfo.exists()) {
            if (error) *error = QStringLiteral("源文件不存在：%1").arg(source);
            return -1;
        }
        if (sourceInfo.isDir()) {
            const QString canonicalSource = sourceInfo.canonicalFilePath();
            const QString canonicalDestination = destinationInfo.canonicalFilePath();
            const bool lexicalInside = isPathInside(destination, source);
            const bool canonicalInside = !canonicalSource.isEmpty()
                && !canonicalDestination.isEmpty()
                && isPathInside(canonicalDestination, canonicalSource);
            if (lexicalInside || canonicalInside) {
                if (error) *error = QStringLiteral("不能将文件夹传输到它自身内部");
                return -1;
            }
        }
    }

    Record record;
    record.id = m_nextId++;
    record.operation = operation;
    record.sources = sources;
    record.destination = destination;
    record.conflicts = conflicts;
    record.status = TaskStatus::Queued;
    m_records.append(record);
    emit tasksChanged();
    if (!m_current)
        startNext();
    return record.id;
}

QVector<TransferManager::Snapshot> TransferManager::tasks() const
{
    QVector<Snapshot> result;
    result.reserve(m_records.size());
    for (const Record &record : m_records)
        result.append(snapshotFor(record));
    return result;
}

TransferManager::Snapshot TransferManager::current() const
{
    if (m_currentIndex >= 0 && m_currentIndex < m_records.size())
        return snapshotFor(m_records.at(m_currentIndex));
    return Snapshot();
}

bool TransferManager::hasActive() const
{
    return m_current != nullptr;
}

int TransferManager::queuedCount() const
{
    int count = 0;
    for (const Record &record : m_records) {
        if (record.status == TaskStatus::Queued)
            ++count;
    }
    return count;
}

void TransferManager::startNext()
{
    if (m_current)
        return;
    int index = -1;
    for (int i = 0; i < m_records.size(); ++i) {
        if (m_records.at(i).status == TaskStatus::Queued) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    Record &record = m_records[index];
    record.status = TaskStatus::Preparing;
    record.error.clear();
    record.active = true;
    record.paused = false;
    record.doneBytes = 0;
    record.doneFiles = 0;
    record.currentName.clear();
    record.bytesPerSecond = 0;
    record.remainingSeconds = -1;
    record.progressClock.start();

    auto *task = new TransferTask(record.id, record.operation, record.sources,
                                  record.destination, record.conflicts);
    task->setParent(this);
    record.task = task;
    m_current = task;
    m_currentIndex = index;

    connect(task, &TransferTask::progress, this, &TransferManager::onTaskProgress);
    connect(task, &TransferTask::finished, this, &TransferManager::onTaskFinished);
    connect(task, &QThread::finished, task, &QObject::deleteLater);

    emit currentChanged();
    emit tasksChanged();
    task->start();
}

void TransferManager::onTaskProgress(int id, qint64 doneBytes, qint64 totalBytes,
                                     int doneFiles, int totalFiles, const QString &currentName)
{
    int index = findRecord(id);
    if (index < 0)
        return;
    Record &record = m_records[index];
    record.doneBytes = doneBytes;
    record.totalBytes = totalBytes;
    record.doneFiles = doneFiles;
    record.totalFiles = totalFiles;
    if (!currentName.isEmpty())
        record.currentName = currentName;
    if (record.status == TaskStatus::Preparing && (doneBytes > 0 || totalBytes > 0 || doneFiles > 0))
        record.status = TaskStatus::Running;
    if (record.status == TaskStatus::Running && record.paused)
        record.status = TaskStatus::Paused;
    if (record.progressClock.isValid() && record.progressClock.elapsed() >= 250
        && record.doneBytes > 0) {
        const double seconds = record.progressClock.elapsed() / 1000.0;
        if (seconds > 0.1)
            record.bytesPerSecond = static_cast<qint64>(record.doneBytes / seconds);
        if (record.bytesPerSecond > 0 && record.totalBytes > record.doneBytes)
            record.remainingSeconds = static_cast<int>((record.totalBytes - record.doneBytes)
                                                       / record.bytesPerSecond);
        else if (record.totalBytes > 0 && record.doneBytes >= record.totalBytes)
            record.remainingSeconds = 0;
    }
    emit currentChanged();
    emit tasksChanged();
}

void TransferManager::onTaskFinished(int id, bool success, bool canceled, const QString &error)
{
    int index = findRecord(id);
    if (index < 0)
        return;
    Record &record = m_records[index];
    record.active = false;
    record.paused = false;
    record.task = nullptr;
    if (canceled)
        record.status = TaskStatus::Cancelled;
    else if (success)
        record.status = TaskStatus::Done;
    else
        record.status = TaskStatus::Failed;
    if (!error.isEmpty())
        record.error = error;
    if (record.status == TaskStatus::Done) {
        record.doneBytes = record.totalBytes;
        record.doneFiles = record.totalFiles;
    }

    const bool wasCurrent = (index == m_currentIndex);
    if (wasCurrent) {
        m_current = nullptr;
        m_currentIndex = -1;
    }
    emit currentChanged();
    emit tasksChanged();
    emit taskFinished(id, success && !canceled, record.error);
    if (wasCurrent)
        startNext();
}

void TransferManager::pauseCurrent()
{
    if (!m_current || m_currentIndex < 0)
        return;
    if (m_records[m_currentIndex].status == TaskStatus::Done
        || m_records[m_currentIndex].status == TaskStatus::Failed
        || m_records[m_currentIndex].status == TaskStatus::Cancelled)
        return;
    m_current->setPaused(true);
    m_records[m_currentIndex].paused = true;
    m_records[m_currentIndex].status = TaskStatus::Paused;
    emit currentChanged();
    emit tasksChanged();
}

void TransferManager::resumeCurrent()
{
    if (!m_current || m_currentIndex < 0)
        return;
    if (m_records[m_currentIndex].status != TaskStatus::Paused)
        return;
    m_current->setPaused(false);
    m_records[m_currentIndex].paused = false;
    m_records[m_currentIndex].status = TaskStatus::Running;
    emit currentChanged();
    emit tasksChanged();
}

void TransferManager::cancelCurrent()
{
    if (!m_current || m_currentIndex < 0)
        return;
    Record &record = m_records[m_currentIndex];
    if (record.status == TaskStatus::Done || record.status == TaskStatus::Failed
        || record.status == TaskStatus::Cancelled)
        return;
    record.status = TaskStatus::Cancelling;
    m_current->cancel();
    emit currentChanged();
    emit tasksChanged();
}

void TransferManager::shutdown()
{
    if (m_current) {
        m_current->cancel();
        if (!m_current->wait(15000)) {
            m_current->terminate();
            m_current->wait(2000);
        }
        m_current = nullptr;
        m_currentIndex = -1;
    }
}

TransferManager::Snapshot TransferManager::snapshotFor(const Record &record) const
{
    Snapshot snapshot;
    snapshot.id = record.id;
    snapshot.operation = record.operation;
    snapshot.sources = record.sources;
    snapshot.destination = record.destination;
    snapshot.status = record.status;
    snapshot.statusText = statusText(record.status);
    snapshot.doneBytes = record.doneBytes;
    snapshot.totalBytes = record.totalBytes;
    snapshot.doneFiles = record.doneFiles;
    snapshot.totalFiles = record.totalFiles;
    snapshot.currentName = record.currentName;
    snapshot.error = record.error;
    snapshot.bytesPerSecond = record.bytesPerSecond;
    snapshot.remainingSeconds = record.remainingSeconds;
    snapshot.active = record.active;
    snapshot.paused = record.paused;
    return snapshot;
}

QString TransferManager::statusText(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Queued: return QStringLiteral("等待中");
    case TaskStatus::Preparing: return QStringLiteral("准备中");
    case TaskStatus::Running: return QStringLiteral("进行中");
    case TaskStatus::Paused: return QStringLiteral("已暂停");
    case TaskStatus::Cancelling: return QStringLiteral("正在取消");
    case TaskStatus::Done: return QStringLiteral("已完成");
    case TaskStatus::Failed: return QStringLiteral("失败");
    case TaskStatus::Cancelled: return QStringLiteral("已取消");
    }
    return QString();
}

QString TransferManager::operationText(TransferOperation operation)
{
    return operation == TransferOperation::Copy ? QStringLiteral("复制") : QStringLiteral("移动");
}

int TransferManager::findRecord(int id) const
{
    for (int i = 0; i < m_records.size(); ++i) {
        if (m_records.at(i).id == id)
            return i;
    }
    return -1;
}
