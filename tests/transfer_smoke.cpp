#include "formatters.h"
#include "transfertask.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

namespace {
bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(content) == content.size();
}

struct Result {
    bool success = false;
    bool canceled = false;
    QString error;
    bool pausedAtCheck = false;
    bool finishedBeforeResume = false;
};

Result runTask(TransferTask *task, bool pauseTest = false)
{
    Result result;
    QEventLoop loop;
    if (pauseTest)
        task->setPaused(true);
    QObject::connect(task, &TransferTask::finished,
                     [&result, &loop](int, bool success, bool canceled, const QString &error) {
        result.success = success;
        result.canceled = canceled;
        result.error = error;
        loop.quit();
    });
    task->start();
    if (pauseTest) {
        QTimer::singleShot(250, task, [&result, task]() {
            result.pausedAtCheck = task->isPaused();
            result.finishedBeforeResume = task->isFinished();
            task->setPaused(false);
        });
    }
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();
    task->wait(5000);
    return result;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid()) {
        qCritical() << "cannot create temp dir";
        return 2;
    }
    const QString root = temp.path();
    const QString sourceA = root + "/source-a";
    const QString sourceB = root + "/source-b";
    const QString destination = root + "/destination";
    QDir().mkpath(sourceA + "/nested");
    QDir().mkpath(sourceB);
    QDir().mkpath(destination);
    if (!writeFile(sourceA + "/alpha.txt", "alpha\n")
        || !writeFile(sourceA + "/nested/beta.bin", QByteArray(256 * 1024, 'B'))
        || !writeFile(sourceB + "/move-me.txt", "move me\n")) {
        qCritical() << "cannot create fixtures";
        return 2;
    }

    int failures = 0;
    {
        TransferTask task(1, TransferOperation::Copy, QStringList() << sourceA, destination);
        Result result = runTask(&task);
        if (!result.success || !QFileInfo::exists(destination + "/source-a/alpha.txt")
            || !QFileInfo::exists(destination + "/source-a/nested/beta.bin")) {
            qCritical() << "copy failed" << result.success << result.error;
            ++failures;
        } else {
            qInfo() << "copy ok";
        }
    }
    {
        TransferTask task(2, TransferOperation::Copy, QStringList() << sourceA, destination);
        Result result = runTask(&task);
        if (!result.success || !QFileInfo::exists(destination + "/source-a (1)/alpha.txt")) {
            qCritical() << "unique copy failed" << result.success << result.error;
            ++failures;
        } else {
            qInfo() << "unique destination ok";
        }
    }
    {
        TransferTask task(3, TransferOperation::Move, QStringList() << sourceB, destination);
        Result result = runTask(&task);
        if (!result.success || QFileInfo::exists(sourceB)
            || !QFileInfo::exists(destination + "/source-b/move-me.txt")) {
            qCritical() << "move failed" << result.success << result.error;
            ++failures;
        } else {
            qInfo() << "move ok";
        }
    }
    {
        // Copying a directory through a symlink that points inside itself must be rejected.
        QFile::link(sourceA, root + "/source-link");
        TransferTask task(6, TransferOperation::Copy,
                          QStringList() << sourceA, root + "/source-link");
        Result result = runTask(&task);
        if (result.success || result.error.isEmpty()) {
            qCritical() << "symlink containment guard failed" << result.success << result.error;
            ++failures;
        } else {
            qInfo() << "containment guard ok";
        }
    }
    {
        const QString pauseSource = root + "/pause-source";
        QDir().mkpath(pauseSource);
        QFile large(pauseSource + "/large.bin");
        if (large.open(QIODevice::WriteOnly)) {
            large.resize(32 * 1024 * 1024);
            large.close();
        }
        TransferTask task(4, TransferOperation::Copy, QStringList() << pauseSource, destination);
        Result result = runTask(&task, true);
        if (!result.success || !result.pausedAtCheck || result.finishedBeforeResume) {
            qCritical() << "pause test failed" << result.success << result.pausedAtCheck
                        << result.finishedBeforeResume << result.error;
            ++failures;
        } else {
            qInfo() << "pause/resume ok";
        }
    }
    {
        const QString cancelSource = root + "/cancel-source";
        QDir().mkpath(cancelSource);
        QFile large(cancelSource + "/large.bin");
        if (large.open(QIODevice::WriteOnly)) {
            large.resize(64 * 1024 * 1024);
            large.close();
        }
        TransferTask task(5, TransferOperation::Copy, QStringList() << cancelSource, destination);
        Result result;
        QEventLoop loop;
        QObject::connect(&task, &TransferTask::finished,
                         [&result, &loop](int, bool success, bool canceled, const QString &error) {
            result.success = success;
            result.canceled = canceled;
            result.error = error;
            loop.quit();
        });
        task.setPaused(true);
        task.start();
        QTimer::singleShot(80, &task, [&task]() { task.cancel(); });
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        loop.exec();
        task.wait(5000);
        if (!result.canceled || QFileInfo::exists(destination + "/cancel-source")) {
            qCritical() << "cancel cleanup failed" << result.success << result.canceled
                        << result.error;
            ++failures;
        } else {
            qInfo() << "cancel cleanup ok";
        }
    }

    {
        const QString conflictDir = root + "/conflicts";
        const QString sourceDir = conflictDir + "/source";
        const QString targetDir = conflictDir + "/target";
        QDir().mkpath(sourceDir);
        QDir().mkpath(targetDir);
        const QString sourceFile = sourceDir + "/same.txt";
        const QString targetFile = targetDir + "/same.txt";
        if (!writeFile(sourceFile, "new\n") || !writeFile(targetFile, "old\n")) {
            qCritical() << "conflict fixture failed";
            return 2;
        }
        ConflictActions replace;
        replace.insert(sourceFile, ConflictAction::Replace);
        TransferTask task(7, TransferOperation::Copy, QStringList() << sourceFile,
                          targetDir, replace);
        Result result = runTask(&task);
        QFile replaced(targetFile);
        if (!result.success || !replaced.open(QIODevice::ReadOnly)
            || replaced.readAll() != QByteArray("new\n")) {
            qCritical() << "replace conflict failed" << result.success << result.error;
            ++failures;
        } else {
            qInfo() << "replace conflict ok";
        }
    }
    {
        const QString conflictDir = root + "/conflicts-skip";
        const QString sourceDir = conflictDir + "/source";
        const QString targetDir = conflictDir + "/target";
        QDir().mkpath(sourceDir);
        QDir().mkpath(targetDir);
        const QString sourceFile = sourceDir + "/same.txt";
        const QString targetFile = targetDir + "/same.txt";
        if (!writeFile(sourceFile, "new\n") || !writeFile(targetFile, "old\n")) {
            qCritical() << "skip fixture failed";
            return 2;
        }
        ConflictActions skip;
        skip.insert(sourceFile, ConflictAction::Skip);
        TransferTask task(8, TransferOperation::Copy, QStringList() << sourceFile,
                          targetDir, skip);
        Result result = runTask(&task);
        QFile unchanged(targetFile);
        if (!result.success || !unchanged.open(QIODevice::ReadOnly)
            || unchanged.readAll() != QByteArray("old\n")) {
            qCritical() << "skip conflict failed" << result.success << result.error;
            ++failures;
        } else {
            qInfo() << "skip conflict ok";
        }
    }
    {
        const QString conflictDir = root + "/conflicts-both";
        const QString sourceDir = conflictDir + "/source";
        const QString targetDir = conflictDir + "/target";
        QDir().mkpath(sourceDir);
        QDir().mkpath(targetDir);
        const QString sourceFile = sourceDir + "/same.txt";
        if (!writeFile(sourceFile, "new\n") || !writeFile(targetDir + "/same.txt", "old\n")) {
            qCritical() << "keep-both fixture failed";
            return 2;
        }
        ConflictActions keep;
        keep.insert(sourceFile, ConflictAction::KeepBoth);
        TransferTask task(9, TransferOperation::Copy, QStringList() << sourceFile,
                          targetDir, keep);
        Result result = runTask(&task);
        if (!result.success || !QFileInfo::exists(targetDir + "/same (1).txt")) {
            qCritical() << "keep-both conflict failed" << result.success << result.error;
            ++failures;
        } else {
            qInfo() << "keep-both conflict ok";
        }
    }
    {
        const QString conflictDir = root + "/conflicts-merge";
        const QString sourceDir = conflictDir + "/source";
        const QString targetDir = conflictDir + "/target";
        QDir().mkpath(sourceDir);
        QDir().mkpath(targetDir);
        const QString sourceFolder = sourceDir + "/folder";
        const QString targetFolder = targetDir + "/folder";
        QDir().mkpath(sourceFolder);
        QDir().mkpath(targetFolder);
        if (!writeFile(sourceFolder + "/same.txt", "new\n")
            || !writeFile(sourceFolder + "/new.txt", "new\n")
            || !writeFile(targetFolder + "/same.txt", "old\n")
            || !writeFile(targetFolder + "/keep.txt", "keep\n")) {
            qCritical() << "merge fixture failed";
            return 2;
        }
        ConflictActions merge;
        merge.insert(sourceFolder, ConflictAction::Merge);
        TransferTask task(10, TransferOperation::Copy, QStringList() << sourceFolder,
                          targetDir, merge);
        Result result = runTask(&task);
        QFile oldSame(targetFolder + "/same.txt");
        if (!result.success || !oldSame.open(QIODevice::ReadOnly)
            || oldSame.readAll() != QByteArray("old\n")
            || !QFileInfo::exists(targetFolder + "/same (1).txt")
            || !QFileInfo::exists(targetFolder + "/new.txt")
            || !QFileInfo::exists(targetFolder + "/keep.txt")) {
            qCritical() << "merge conflict failed" << result.success << result.error;
            ++failures;
        } else {
            qInfo() << "merge conflict ok";
        }
    }

    {
        // Copying a file into its own folder with Replace must never delete the source.
        const QString sameDir = root + "/same-folder";
        QDir().mkpath(sameDir);
        const QString sourceFile = sameDir + "/same.txt";
        if (!writeFile(sourceFile, "same\n")) {
            qCritical() << "same-folder fixture failed";
            return 2;
        }
        ConflictActions replace;
        replace.insert(sourceFile, ConflictAction::Replace);
        TransferTask task(11, TransferOperation::Copy, QStringList() << sourceFile,
                          sameDir, replace);
        Result result = runTask(&task);
        if (!result.success || !QFileInfo::exists(sourceFile)
            || !QFileInfo::exists(sameDir + "/same (1).txt")) {
            qCritical() << "same-folder guard failed" << result.success << result.error;
            ++failures;
        } else {
            qInfo() << "same-folder guard ok";
        }
    }

    if (failures == 0) {
        qInfo() << "TRANSFER_SMOKE_OK";
        return 0;
    }
    qCritical() << "TRANSFER_SMOKE_FAILED" << failures;
    return 1;
}
