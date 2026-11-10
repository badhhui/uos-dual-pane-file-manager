#include "singleinstance.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    SingleInstance primary;
    if (!primary.isPrimary()) {
        qCritical("primary instance server failed");
        return 1;
    }
    QString received;
    primary.setRevealHandler([&app, &received](const QString &path) {
        received = path;
        app.quit();
    });
    SingleInstance secondary;
    if (secondary.isPrimary()) {
        qCritical("secondary unexpectedly became primary");
        return 1;
    }
    if (!secondary.sendMessage(QStringLiteral("/tmp/reveal-target.docx"))) {
        qCritical("secondary reveal send failed");
        return 1;
    }
    QTimer::singleShot(3000, &app, &QCoreApplication::quit);
    app.exec();
    if (received != QStringLiteral("/tmp/reveal-target.docx")) {
        qCritical() << "reveal payload missing" << received;
        return 1;
    }
    qInfo("SINGLEINSTANCE_SMOKE_OK");
    return 0;
}
