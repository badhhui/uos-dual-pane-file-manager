#include "iconprovider.h"
#include "mainwindow.h"
#include "singleinstance.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QScreen>
#include <QStyleFactory>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#include <memory>

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("DualPaneFileManager"));
    QApplication::setApplicationName(QStringLiteral("uos-dual-pane-file-manager"));
    QApplication::setApplicationVersion(QStringLiteral("2.8.0"));
    QApplication::setApplicationDisplayName(QStringLiteral("文件管理器"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    if (QIcon::themeName().isEmpty()) {
        const QStringList themes = {QStringLiteral("bloom"), QStringLiteral("deepin"), QStringLiteral("Papirus"), QStringLiteral("Adwaita"), QStringLiteral("hicolor")};
        for (const QString &theme : themes) {
            if (QDir(QStringLiteral("/usr/share/icons/") + theme).exists()) {
                QIcon::setThemeName(theme);
                break;
            }
        }
    }
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    QApplication::setWindowIcon(IconProvider::instance().appIcon());
    QApplication::setDesktopFileName(QStringLiteral("uos-dual-pane-file-manager"));
    Theme::instance().applyToApplication();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("文件管理器 · 本地原生版"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption leftOption(
        QStringList() << QStringLiteral("l") << QStringLiteral("left"),
        QStringLiteral("左窗格起始目录"), QStringLiteral("path"));
    const QCommandLineOption rightOption(
        QStringList() << QStringLiteral("r") << QStringLiteral("right"),
        QStringLiteral("右窗格起始目录"), QStringLiteral("path"));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"), QStringLiteral("渲染截图到文件后退出"), QStringLiteral("path"));
    const QCommandLineOption darkOption(QStringLiteral("dark"), QStringLiteral("使用深色主题"));
    const QCommandLineOption sizeOption(
        QStringLiteral("window-size"), QStringLiteral("窗口尺寸，例如 1440x900"), QStringLiteral("WxH"));
    const QCommandLineOption revealOption(
        QStringList() << QStringLiteral("reveal") << QStringLiteral("select")
                      << QStringLiteral("show"),
        QStringLiteral("在文件管理器中显示并选中文件"), QStringLiteral("path"));
    parser.addOption(leftOption);
    parser.addOption(rightOption);
    parser.addOption(screenshotOption);
    parser.addOption(darkOption);
    parser.addOption(sizeOption);
    parser.addOption(revealOption);
    parser.addPositionalArgument(QStringLiteral("directory"), QStringLiteral("启动目录"), QStringLiteral("[directory]"));
    parser.process(app);

    auto localPath = [](QString value) {
        value = value.trimmed();
        if (value.startsWith(QStringLiteral("file://")))
            value = QUrl(value).toLocalFile();
        return value;
    };

    QString left = localPath(parser.value(leftOption));
    QString right = localPath(parser.value(rightOption));
    QString reveal = localPath(parser.value(revealOption));
    const QStringList positional = parser.positionalArguments();
    if (left.isEmpty() && !positional.isEmpty()) {
        const QString first = localPath(positional.first());
        QFileInfo info(first);
        left = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
        if (reveal.isEmpty() && info.isFile())
            reveal = info.absoluteFilePath();
    }
    if (right.isEmpty() && positional.size() > 1) {
        const QString second = localPath(positional.at(1));
        QFileInfo info(second);
        right = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    }

    std::unique_ptr<SingleInstance> singleInstance;
    if (!parser.isSet(screenshotOption)) {
        singleInstance.reset(new SingleInstance());
        if (!singleInstance->isPrimary()) {
            QString target = reveal;
            if (target.isEmpty() && !positional.isEmpty()) {
                const QString first = localPath(positional.first());
                if (QFileInfo(first).isDir())
                    target = QFileInfo(first).absoluteFilePath();
            }
            if (singleInstance->sendMessage(target))
                return 0;
            singleInstance.reset();
        }
    }

    MainWindow window(left, right);
    if (singleInstance) {
        singleInstance->setRevealHandler([&window](const QString &path) {
            window.revealPath(path);
        });
    }
    if (parser.isSet(darkOption))
        window.setDarkMode(true);

    if (parser.isSet(sizeOption)) {
        const QStringList parts = parser.value(sizeOption).split(QLatin1Char('x'), QString::SkipEmptyParts);
        if (parts.size() == 2) {
            bool okWidth = false;
            bool okHeight = false;
            const int width = parts.at(0).toInt(&okWidth);
            const int height = parts.at(1).toInt(&okHeight);
            if (okWidth && okHeight && width >= 1024 && height >= 640)
                window.resize(width, height);
        }
    }

    window.show();
    if (window.windowHandle())
        window.windowHandle()->setIcon(IconProvider::instance().appIcon());
    if (!reveal.isEmpty()) {
        const QString target = reveal;
        QTimer::singleShot(0, &window, [&window, target]() { window.revealPath(target); });
    }

    if (parser.isSet(screenshotOption)) {
        const QString output = parser.value(screenshotOption);
        QTimer::singleShot(1800, &window, [&window, output]() {
            QPixmap shot = window.grab();
            shot.save(output, "PNG");
            QApplication::quit();
        });
    }
    return app.exec();
}
