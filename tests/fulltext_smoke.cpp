#include "fulltextsearch.h"

#include <QCoreApplication>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const WssScopeStatus status = FullTextSearch::scopeStatus(QStringLiteral("/tmp/wss-scope"));
    qInfo() << "status" << static_cast<int>(status.availability) << status.rootPath << status.message;
    if (status.availability != WssAvailability::Ready)
        return 1;
    QString error;
    const QVector<FullTextHit> hits = FullTextSearch::search(
        QStringLiteral("/tmp/wss-scope"), QStringLiteral("测试"), 100, &error);
    qInfo() << "hits" << hits.size() << error;
    for (const FullTextHit &hit : hits)
        qInfo() << hit.name << hit.path;
    if (hits.size() != 2)
        return 1;
    const WssScopeStatus sub = FullTextSearch::scopeStatus(QStringLiteral("/tmp/wss-scope/sub"));
    if (sub.availability != WssAvailability::Ready)
        return 1;
    const QVector<FullTextHit> subHits = FullTextSearch::search(
        QStringLiteral("/tmp/wss-scope/sub"), QStringLiteral("测试"), 100, &error);
    qInfo() << "subHits" << subHits.size() << error;
    if (subHits.size() != 1 || !subHits.first().path.endsWith(QStringLiteral("预算表.xlsx")))
        return 1;
    const WssScopeStatus outside = FullTextSearch::scopeStatus(QStringLiteral("/tmp/not-indexed"));
    if (outside.availability != WssAvailability::NotIndexed)
        return 1;
    qInfo() << "FULLTEXT_SMOKE_OK";
    return 0;
}
