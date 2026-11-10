#pragma once

#include <QString>
#include <QStringList>

struct DesktopEntry
{
    bool valid = false;
    QString type;
    QString name;
    QString icon;
    QString exec;
    QStringList mimeTypes;
    bool terminal = false;
    bool noDisplay = false;
    bool hidden = false;
};

DesktopEntry readDesktopEntry(const QString &desktopFilePath);
bool launchDesktopEntry(const QString &desktopFilePath, QString *error = nullptr);
bool launchDesktopEntryWithFile(const QString &desktopFilePath, const QString &filePath,
                                QString *error = nullptr);
