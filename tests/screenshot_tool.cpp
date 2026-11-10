#include "filepane.h"
#include "mainwindow.h"
#include "previewdialog.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QElapsedTimer>
#include <QTableView>
#include <QSettings>
#include <QLineEdit>
#include <QTest>
#include <QToolButton>
#include <QComboBox>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("DualPaneFileManagerShot"));
    QCoreApplication::setApplicationName(QStringLiteral("DualPaneFileManagerShot"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("left"), QStringLiteral("left"), QStringLiteral("path")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("right"), QStringLiteral("right"), QStringLiteral("path")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("output"), QStringLiteral("output"), QStringLiteral("path")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("select"), QStringLiteral("select row"), QStringLiteral("row")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("expand"), QStringLiteral("expand folder path"), QStringLiteral("path")));
    parser.addOption(QCommandLineOption(QStringLiteral("dark")));
    parser.addOption(QCommandLineOption(QStringLiteral("single")));
    parser.addOption(QCommandLineOption(QStringLiteral("bookmark")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("preview"), QStringLiteral("preview"), QStringLiteral("path")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("query"), QStringLiteral("pane query"), QStringLiteral("text")));
    parser.addOption(QCommandLineOption(QStringLiteral("fulltext")));
    parser.process(app);

    const QString left = parser.value(QStringLiteral("left"));
    const QString right = parser.value(QStringLiteral("right"));
    const QString output = parser.value(QStringLiteral("output"));
    const int selectedRow = parser.value(QStringLiteral("select")).toInt();
    const QString previewPath = parser.value(QStringLiteral("preview"));
    if (!previewPath.isEmpty()) {
        if (output.isEmpty())
            return 2;
        PreviewDialog dialog(previewPath);
        dialog.resize(940, 680);
        dialog.show();
        QTest::qWait(300);
        dialog.grab().save(output, "PNG");
        qInfo() << "PREVIEW_OK" << output;
        return 0;
    }
    if (left.isEmpty() || right.isEmpty() || output.isEmpty())
        return 2;

    if (parser.isSet(QStringLiteral("bookmark"))) {
        QSettings settings;
        settings.setValue(QStringLiteral("bookmarks/paths"), QStringList() << left);
    }
    MainWindow window(left, right);
    if (parser.isSet(QStringLiteral("dark")))
        window.setDarkMode(true);
    window.resize(1440, 900);
    if (parser.isSet(QStringLiteral("single"))) {
        if (auto *toggle = window.findChild<QToolButton *>(QStringLiteral("ViewModeButton")))
            toggle->click();
    }
    window.show();

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2200) {
        QTest::qWait(50);
        if (window.leftPane()->directoryModel()->rowCount() > 0)
            break;
    }
    QTest::qWait(500);
    if (selectedRow >= 0 && selectedRow < window.leftPane()->directoryModel()->rowCount()) {
        window.leftPane()->tableView()->selectRow(selectedRow);
        window.leftPane()->tableView()->setFocus();
        window.leftPane()->tableView()->scrollTo(
            window.leftPane()->tableView()->model()->index(selectedRow, 0));
    }
    const QString expandPath = parser.value(QStringLiteral("expand"));
    if (!expandPath.isEmpty()) {
        auto *model = window.leftPane()->directoryModel();
        const int row = model->rowForPath(expandPath);
        if (row >= 0 && model->canExpandRow(row)) {
            model->toggleFolderExpanded(row);
            QTest::qWait(300);
        }
    }
    const QString query = parser.value(QStringLiteral("query"));
    if (!query.isEmpty()) {
        window.leftPane()->beginSearch();
        if (parser.isSet(QStringLiteral("fulltext"))) {
            if (auto *mode = window.leftPane()->findChild<QComboBox *>(QStringLiteral("PaneSearchMode")))
                mode->setCurrentIndex(1);
        }
        if (auto *edit = window.leftPane()->findChild<QLineEdit *>(QStringLiteral("PaneSearchInput"))) {
            edit->setText(query);
            QTest::qWait(1200);
        }
    }
    QTest::qWait(300);
    window.grab().save(output, "PNG");
    qInfo() << "SCREENSHOT_OK" << output;
    return 0;
}
