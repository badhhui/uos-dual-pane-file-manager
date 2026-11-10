#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

struct FullTextHit
{
    QString name;
    QString path;
    QString extension;
    qint64 size = 0;
    qint64 mtimeMs = 0;
};

enum class WssAvailability {
    NotInstalled,
    NoIndex,
    NotIndexed,
    Ready,
    Error
};

struct WssScopeStatus
{
    WssAvailability availability = WssAvailability::NotInstalled;
    QString rootPath;
    QString message;
};

class FullTextSearch
{
public:
    static bool isInstalled();
    static QString databasePath();
    static QString simpleLibraryPath();
    static WssScopeStatus scopeStatus(const QString &directory);
    static QVector<FullTextHit> search(const QString &directory, const QString &query,
                                       int limit, QString *error = nullptr);
};
