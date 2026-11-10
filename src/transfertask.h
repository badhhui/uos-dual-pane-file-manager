#pragma once

#include "transferconflict.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>

enum class TransferOperation {
    Copy,
    Move
};

class TransferTask : public QThread
{
    Q_OBJECT

public:
    TransferTask(int id, TransferOperation operation, const QStringList &sources,
                 const QString &destination, const ConflictActions &conflicts = ConflictActions(),
                 QObject *parent = nullptr);
    ~TransferTask() override;

    int taskId() const;
    TransferOperation operation() const;
    void setPaused(bool paused);
    bool isPaused() const;
    void cancel();

signals:
    void progress(int taskId, qint64 doneBytes, qint64 totalBytes,
                  int doneFiles, int totalFiles, const QString &currentName);
    void finished(int taskId, bool success, bool canceled, const QString &error);

protected:
    void run() override;

private:
    bool waitIfPaused();
    bool validateDestination(QString &error) const;
    bool countPath(const QString &path, qint64 &bytes, int &files, QString &error);
    bool countDirectory(const QString &path, qint64 &bytes, int &files, QString &error);
    bool copyOne(const QString &source, const QString &target, QString &error);
    bool copyDirectory(const QString &source, const QString &target, QString &error);
    bool copyDirectoryMerge(const QString &source, const QString &target, QString &error);
    bool copyFile(const QString &source, const QString &target, QString &error);
    bool copySymlink(const QString &source, const QString &target, QString &error);
    bool removeExistingTarget(const QString &path, QString &error);
    bool removePath(const QString &path, QString &error) const;
    void cleanupCurrentTarget();
    void emitProgress(const QString &currentName, bool force = false);

    int m_id;
    TransferOperation m_operation;
    QStringList m_sources;
    QString m_destination;
    ConflictActions m_conflicts;
    mutable QMutex m_mutex;
    QWaitCondition m_waitCondition;
    bool m_paused = false;
    bool m_canceled = false;
    qint64 m_totalBytes = 0;
    int m_totalFiles = 0;
    qint64 m_doneBytes = 0;
    int m_doneFiles = 0;
    QString m_currentSource;
    QString m_currentTarget;
    bool m_sourceRemovalStarted = false;
    bool m_targetIsNew = true;
    bool m_mergeCurrent = false;
    QElapsedTimer m_lastEmit;
};
