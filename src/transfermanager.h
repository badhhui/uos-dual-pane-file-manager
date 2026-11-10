#pragma once

#include "transfertask.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QVector>

class TransferManager : public QObject
{
    Q_OBJECT

public:
    enum class TaskStatus {
        Queued,
        Preparing,
        Running,
        Paused,
        Cancelling,
        Done,
        Failed,
        Cancelled
    };

    struct Snapshot {
        int id = -1;
        TransferOperation operation = TransferOperation::Copy;
        QStringList sources;
        QString destination;
        ConflictActions conflicts;
        TaskStatus status = TaskStatus::Queued;
        QString statusText;
        qint64 doneBytes = 0;
        qint64 totalBytes = 0;
        int doneFiles = 0;
        int totalFiles = 0;
        QString currentName;
        QString error;
        qint64 bytesPerSecond = 0;
        int remainingSeconds = -1;
        bool active = false;
        bool paused = false;
    };

    explicit TransferManager(QObject *parent = nullptr);
    ~TransferManager() override;

    int enqueue(TransferOperation operation, const QStringList &sources,
                const QString &destination, QString *error = nullptr,
                const ConflictActions &conflicts = ConflictActions());
    QVector<Snapshot> tasks() const;
    Snapshot current() const;
    bool hasActive() const;
    int queuedCount() const;
    void pauseCurrent();
    void resumeCurrent();
    void cancelCurrent();
    void shutdown();

signals:
    void tasksChanged();
    void currentChanged();
    void taskFinished(int id, bool success, const QString &error);

private:
    struct Record {
        int id = -1;
        TransferOperation operation = TransferOperation::Copy;
        QStringList sources;
        QString destination;
        ConflictActions conflicts;
        TaskStatus status = TaskStatus::Queued;
        qint64 doneBytes = 0;
        qint64 totalBytes = 0;
        int doneFiles = 0;
        int totalFiles = 0;
        QString currentName;
        QString error;
        qint64 bytesPerSecond = 0;
        int remainingSeconds = -1;
        QElapsedTimer progressClock;
        bool active = false;
        bool paused = false;
        TransferTask *task = nullptr;
    };

    void startNext();
    void onTaskProgress(int id, qint64 doneBytes, qint64 totalBytes,
                        int doneFiles, int totalFiles, const QString &currentName);
    void onTaskFinished(int id, bool success, bool canceled, const QString &error);
    Snapshot snapshotFor(const Record &record) const;
    static QString statusText(TaskStatus status);
    static QString operationText(TransferOperation operation);
    int findRecord(int id) const;

    int m_nextId = 1;
    QVector<Record> m_records;
    TransferTask *m_current = nullptr;
    int m_currentIndex = -1;
};
