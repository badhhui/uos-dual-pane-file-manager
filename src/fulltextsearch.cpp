#include "fulltextsearch.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

#include <sqlite3.h>

namespace {
QString normalizedDirectory(const QString &path)
{
    QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (clean.endsWith(QLatin1Char('/')) && clean.size() > 1)
        clean.chop(1);
    return clean;
}

QString escapeLike(const QString &path)
{
    QString escaped = path;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    escaped.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return escaped;
}

bool isInside(const QString &path, const QString &root)
{
    return path == root || path.startsWith(root.endsWith(QLatin1Char('/'))
                                               ? root
                                               : root + QLatin1Char('/'));
}

QString sqliteError(sqlite3 *db)
{
    return db ? QString::fromUtf8(sqlite3_errmsg(db)) : QStringLiteral("SQLite 错误");
}

bool openDatabase(sqlite3 **db, QString *error)
{
    const QString path = FullTextSearch::databasePath();
    if (!QFileInfo::exists(path)) {
        if (error) *error = QStringLiteral("文搜搜尚未建立索引");
        return false;
    }
    if (sqlite3_open_v2(path.toUtf8().constData(), db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        if (error) *error = sqliteError(*db);
        sqlite3_close(*db);
        *db = nullptr;
        return false;
    }
    sqlite3_busy_timeout(*db, 3000);
    sqlite3_exec(*db, "PRAGMA query_only=ON;", nullptr, nullptr, nullptr);
    return true;
}

bool loadSimpleExtension(sqlite3 *db, QString *error)
{
    const QString library = FullTextSearch::simpleLibraryPath();
    if (library.isEmpty() || !QFileInfo::exists(library)) {
        if (error) *error = QStringLiteral("文搜搜分词组件不可用");
        return false;
    }
    if (sqlite3_enable_load_extension(db, 1) != SQLITE_OK) {
        if (error) *error = sqliteError(db);
        return false;
    }
    char *extensionError = nullptr;
    if (sqlite3_load_extension(db, library.toUtf8().constData(), "sqlite3_simple_init",
                               &extensionError) != SQLITE_OK) {
        if (error) {
            *error = QStringLiteral("无法加载文搜搜分词组件：%1")
                         .arg(QString::fromUtf8(extensionError ? extensionError : ""));
        }
        sqlite3_free(extensionError);
        return false;
    }
    return true;
}

QString buildQuery(const QString &query)
{
    const QString normalized = query.normalized(QString::NormalizationForm_KC).simplified();
    QStringList quoted;
    const QStringList terms = normalized.split(QLatin1Char(' '), QString::SkipEmptyParts);
    for (QString term : terms) {
        term.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        quoted.append(QStringLiteral("\"%1\"").arg(term));
    }
    return quoted.join(QStringLiteral(" AND "));
}
}

bool FullTextSearch::isInstalled()
{
    const QString db = qEnvironmentVariable("WENSOUSOU_DB");
    const QString simple = qEnvironmentVariable("WENSOUSOU_SIMPLE_LIB");
    if (!db.isEmpty() && !simple.isEmpty())
        return true;
    if (QFileInfo::exists(QStringLiteral("/opt/wensousou/parser/parser-worker.jar")))
        return true;
    return !QStandardPaths::findExecutable(QStringLiteral("wensousou")).isEmpty();
}

QString FullTextSearch::databasePath()
{
    const QString overridden = qEnvironmentVariable("WENSOUSOU_DB");
    if (!overridden.isEmpty())
        return overridden;
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(data).filePath(QStringLiteral("wensousou/index.db"));
}

QString FullTextSearch::simpleLibraryPath()
{
    const QString overridden = qEnvironmentVariable("WENSOUSOU_SIMPLE_LIB");
    if (!overridden.isEmpty())
        return overridden;
    const QStringList candidates = {
        QStringLiteral("/opt/wensousou/lib/libsimple.so"),
        QStringLiteral("/opt/wensousou/lib/libsimple-linux-aarch64.so"),
        QStringLiteral("/usr/lib/wensousou/libsimple.so")
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QString();
}

WssScopeStatus FullTextSearch::scopeStatus(const QString &directory)
{
    WssScopeStatus status;
    if (!isInstalled()) {
        status.availability = WssAvailability::NotInstalled;
        status.message = QStringLiteral("安装文搜搜并建立索引后可用");
        return status;
    }
    const QString dbPath = databasePath();
    if (!QFileInfo::exists(dbPath)) {
        status.availability = WssAvailability::NoIndex;
        status.message = QStringLiteral("文搜搜尚未建立索引");
        return status;
    }
    if (simpleLibraryPath().isEmpty()) {
        status.availability = WssAvailability::Error;
        status.message = QStringLiteral("文搜搜分词组件不可用");
        return status;
    }

    sqlite3 *db = nullptr;
    QString error;
    if (!openDatabase(&db, &error)) {
        status.availability = WssAvailability::Error;
        status.message = error;
        return status;
    }
    const QString scope = normalizedDirectory(directory);
    QStringList roots;
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT path FROM roots;", -1, &statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW) {
            const unsigned char *text = sqlite3_column_text(statement, 0);
            if (text)
                roots.append(normalizedDirectory(QString::fromUtf8(
                    reinterpret_cast<const char *>(text))));
        }
    } else {
        error = sqliteError(db);
    }
    sqlite3_finalize(statement);
    sqlite3_close(db);
    if (!error.isEmpty()) {
        status.availability = WssAvailability::Error;
        status.message = error;
        return status;
    }
    QString bestRoot;
    for (const QString &root : roots) {
        if (isInside(scope, root) && root.size() > bestRoot.size())
            bestRoot = root;
    }
    if (bestRoot.isEmpty()) {
        status.availability = WssAvailability::NotIndexed;
        status.message = QStringLiteral("当前文件夹未加入文搜搜索引");
        return status;
    }
    status.availability = WssAvailability::Ready;
    status.rootPath = bestRoot;
    return status;
}

QVector<FullTextHit> FullTextSearch::search(const QString &directory, const QString &query,
                                            int limit, QString *error)
{
    QVector<FullTextHit> hits;
    const QString ftsQuery = buildQuery(query);
    if (ftsQuery.isEmpty())
        return hits;
    const WssScopeStatus scope = scopeStatus(directory);
    if (scope.availability != WssAvailability::Ready) {
        if (error) *error = scope.message;
        return hits;
    }
    sqlite3 *db = nullptr;
    QString dbError;
    if (!openDatabase(&db, &dbError)) {
        if (error) *error = dbError;
        return hits;
    }
    if (!loadSimpleExtension(db, &dbError)) {
        if (error) *error = dbError;
        sqlite3_close(db);
        return hits;
    }
    const QString scopePath = normalizedDirectory(directory);
    const QString prefix = escapeLike(scopePath) + QStringLiteral("/%");
    const QByteArray sql = QByteArrayLiteral(
        "SELECT d.filename,d.path,d.extension,d.size,d.mtime_ms "
        "FROM documents_fts JOIN documents d ON d.id=documents_fts.rowid "
        "WHERE documents_fts MATCH ? AND d.status='ok' "
        "AND (d.path = ? OR d.path LIKE ? ESCAPE '\\') "
        "ORDER BY documents_fts.rank,d.mtime_ms DESC LIMIT ?;");
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql.constData(), -1, &statement, nullptr) != SQLITE_OK) {
        if (error) *error = sqliteError(db);
        sqlite3_close(db);
        return hits;
    }
    const QByteArray queryUtf8 = ftsQuery.toUtf8();
    const QByteArray scopeUtf8 = scopePath.toUtf8();
    const QByteArray prefixUtf8 = prefix.toUtf8();
    sqlite3_bind_text(statement, 1, queryUtf8.constData(), queryUtf8.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, scopeUtf8.constData(), scopeUtf8.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, prefixUtf8.constData(), prefixUtf8.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 4, qBound(1, limit, 2000));
    while (sqlite3_step(statement) == SQLITE_ROW) {
        FullTextHit hit;
        const unsigned char *name = sqlite3_column_text(statement, 0);
        const unsigned char *path = sqlite3_column_text(statement, 1);
        const unsigned char *extension = sqlite3_column_text(statement, 2);
        hit.name = name ? QString::fromUtf8(reinterpret_cast<const char *>(name)) : QString();
        hit.path = path ? QString::fromUtf8(reinterpret_cast<const char *>(path)) : QString();
        hit.extension = extension ? QString::fromUtf8(reinterpret_cast<const char *>(extension))
                                  : QString();
        hit.size = sqlite3_column_int64(statement, 3);
        hit.mtimeMs = sqlite3_column_int64(statement, 4);
        if (!hit.path.isEmpty())
            hits.append(hit);
    }
    if (sqlite3_errcode(db) != SQLITE_OK && sqlite3_errcode(db) != SQLITE_DONE) {
        if (error) *error = sqliteError(db);
    }
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return hits;
}
