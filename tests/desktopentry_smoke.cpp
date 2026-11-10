#include "desktopentry.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTest>
#include <QTemporaryDir>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 2;
    const QString marker = temp.path() + "/launched.txt";
    const QString desktop = temp.path() + "/demo-app.desktop";
    QFile file(desktop);
    if (!file.open(QIODevice::WriteOnly)) {
        qCritical() << "fixture open failed";
        return 2;
    }
    const QByteArray content = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Demo\n"
        "Name[zh_CN]=测试应用\n"
        "Exec=/bin/sh -c 'printf \"%s\" \"$1\" > %1' sh %f\n"
        "Icon=applications-system\n"
        "MimeType=text/plain;image/png;\n"
        "Terminal=false\n").arg(marker).toUtf8();
    file.write(content);
    file.close();

    const DesktopEntry entry = readDesktopEntry(desktop);
    if (!entry.valid || entry.name != QStringLiteral("测试应用")
        || entry.icon != QStringLiteral("applications-system")
        || !entry.mimeTypes.contains(QStringLiteral("text/plain"))) {
        qCritical() << "parse failed" << entry.valid << entry.name << entry.icon;
        return 1;
    }
    QString error;
    if (!launchDesktopEntry(desktop, &error)) {
        qCritical() << "launch failed" << error;
        return 1;
    }
    for (int i = 0; i < 50; ++i) {
        if (QFileInfo::exists(marker))
            break;
        QTest::qWait(100);
    }
    if (!QFileInfo::exists(marker)) {
        qCritical() << "launched marker missing";
        return 1;
    }
    const QString fileMarker = temp.path() + "/open-with.txt";
    const QString withDesktop = temp.path() + "/open-with.desktop";
    QFile withFile(withDesktop);
    if (!withFile.open(QIODevice::WriteOnly)) {
        qCritical() << "open-with fixture open failed";
        return 2;
    }
    const QByteArray withContent = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=OpenWith\n"
        "Exec=/bin/sh -c 'printf \"%s\" \"$1\" > %1' sh %f\n"
        "Terminal=false\n").arg(fileMarker).toUtf8();
    withFile.write(withContent);
    withFile.close();
    const QString sourceFile = temp.path() + "/open-with-source.txt";
    QFile source(sourceFile);
    if (!source.open(QIODevice::WriteOnly) || source.write("x") != 1) {
        qCritical() << "open-with source failed";
        return 2;
    }
    source.close();
    error.clear();
    if (!launchDesktopEntryWithFile(withDesktop, sourceFile, &error)) {
        qCritical() << "open-with launch failed" << error;
        return 1;
    }
    for (int i = 0; i < 50; ++i) {
        if (QFileInfo::exists(fileMarker))
            break;
        QTest::qWait(100);
    }
    if (!QFileInfo::exists(fileMarker)) {
        qCritical() << "open-with marker missing";
        return 1;
    }

    qInfo() << "DESKTOPENTRY_SMOKE_OK";
    return 0;
}
