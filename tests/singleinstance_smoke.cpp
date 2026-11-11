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
    QString receivedDir;
    QString receivedRight;
    QString receivedItem;
    primary.setNewWindowHandler(
        [&app, &receivedDir, &receivedRight, &receivedItem](const QString &dir,
                                                            const QString &right,
                                                            const QString &item) {
            receivedDir = dir;
            receivedRight = right;
            receivedItem = item;
            app.quit();
        });
    SingleInstance secondary;
    if (secondary.isPrimary()) {
        qCritical("secondary unexpectedly became primary");
        return 1;
    }
    if (!secondary.sendOpenWindow(QStringLiteral("/tmp"), QStringLiteral("/tmp/reveal-target.docx"),
                                  QStringLiteral("/tmp/right"))) {
        qCritical("secondary open-window send failed");
        return 1;
    }
    QTimer::singleShot(3000, &app, &QCoreApplication::quit);
    app.exec();
    if (receivedDir != QStringLiteral("/tmp")
        || receivedRight != QStringLiteral("/tmp/right")
        || receivedItem != QStringLiteral("/tmp/reveal-target.docx")) {
        qCritical() << "open-window payload missing" << receivedDir << receivedRight
                    << receivedItem;
        return 1;
    }
    qInfo("SINGLEINSTANCE_SMOKE_OK");
    return 0;
}
