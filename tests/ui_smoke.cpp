#include "filepane.h"
#include "directorymodel.h"
#include "iconprovider.h"
#include "mainwindow.h"
#include "previewdialog.h"
#include "transfermanager.h"

#include <QApplication>
#include <QComboBox>
#include <QAction>
#include <QMenu>
#include <QProcess>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QTableWidget>
#include <QTextBrowser>
#include <QScrollArea>
#include <QClipboard>
#include <QMimeData>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QApplication>
#include <QComboBox>
#include <QAction>
#include <QMenu>
#include <QProcess>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QTableWidget>
#include <QTextBrowser>
#include <QScrollArea>
#include <QPushButton>
#include <QTimer>
#include <QTableView>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

namespace {
bool waitFor(const std::function<bool()> &condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (condition())
            return true;
        QTest::qWait(40);
    }
    return condition();
}

}


// QTest mouse/key events leave QApplication::keyboardModifiers() polluted
// (e.g. Ctrl after a ctrl-click), which makes QTableView::selectRow interpret
// the command as Toggle/Clear instead of ClearAndSelect. Select explicitly.
static void selectRowExplicit(QTableView *view, int row)
{
    if (row < 0 || !view->model())
        return;
    const QModelIndex index = view->model()->index(row, 0);
    view->selectionModel()->clearSelection();
    view->selectionModel()->select(index, QItemSelectionModel::Select
                                              | QItemSelectionModel::Rows);
    view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
}

int main(int argc, char **argv)
{
    qputenv("WENSOUSOU_DB", "/tmp/wss-fixture/index.db");
    qputenv("WENSOUSOU_SIMPLE_LIB", "/tmp/libsimple.so");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("DualPaneFileManagerTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DualPaneFileManagerTest"));
    QTemporaryDir temp;
    if (!temp.isValid()) {
        qCritical() << "no temp dir";
        return 2;
    }
    const QString leftDir = temp.path() + "/left";
    const QString rightDir = temp.path() + "/right";
    QDir().mkpath(leftDir);
    QDir().mkpath(rightDir);
    const QString content = QStringLiteral("native qt ui smoke test\n");
    QFile source(leftDir + "/ui-copy-test.txt");
    if (!source.open(QIODevice::WriteOnly)
        || source.write(content.toUtf8()) != content.toUtf8().size()) {
        qCritical() << "cannot create source";
        return 2;
    }
    source.close();

    MainWindow window(leftDir, rightDir);
    window.resize(1200, 760);
    window.show();
    if (window.windowIcon().isNull()) {
        qCritical() << "window icon is empty";
        return 1;
    }
    if (!window.windowTitle().contains(QStringLiteral("文件管理器"))) {
        qCritical() << "unexpected window title" << window.windowTitle();
        return 1;
    }

    // Extended file type icons: a known installer format must render a
    // distinct icon, not the generic unknown-file fallback.
    {
        const QIcon installer = IconProvider::instance().fileIcon(false, QStringLiteral("apk"));
        const QIcon unknown = IconProvider::instance().fileIcon(false, QStringLiteral("zzunknown"));
        if (installer.pixmap(64, 64).toImage() == unknown.pixmap(64, 64).toImage()) {
            qCritical() << "installer icon matches unknown fallback";
            return 1;
        }
    }

    if (!waitFor([&window]() {
            return window.leftPane()->directoryModel()->rowCount() > 0;
        }, 5000)) {
        qCritical() << "left pane did not load";
        return 1;
    }

    // Natural (numeric-aware) name sorting: 1.文件1 < 2.文件2 < 10.文件10.
    {
        const QString sortDir = temp.path() + QStringLiteral("/sort-natural");
        QDir().mkpath(sortDir);
        for (int n : {1, 2, 10}) {
            QFile numbered(sortDir + QStringLiteral("/%1.文件%1").arg(n));
            if (!numbered.open(QIODevice::WriteOnly) || numbered.write("x\n") != 2) {
                qCritical() << "cannot create natural sort fixture" << n;
                return 1;
            }
            numbered.close();
        }
        DirectoryModel sortModel(&IconProvider::instance());
        sortModel.setDirectory(sortDir, true);
        if (!waitFor([&sortModel]() {
                return sortModel.rowCount() > 0;
            }, 5000)) {
            qCritical() << "natural sort fixture directory did not load";
            return 1;
        }
        QStringList names;
        for (const FileEntry &entry : sortModel.visibleEntries())
            names.append(entry.name);
        const QStringList expected = QStringList()
            << QStringLiteral("1.文件1")
            << QStringLiteral("2.文件2")
            << QStringLiteral("10.文件10");
        if (names != expected) {
            qCritical() << "natural sort order wrong:" << names;
            return 1;
        }
    }

    // Rubber-band selection: press on an unselected row, drag down, release —
    // multiple rows must end up selected.
    {
        for (int i = 0; i < 3; ++i) {
            QFile bandFile(leftDir + QStringLiteral("/zz-band-fixture-%1.txt").arg(i));
            if (!bandFile.open(QIODevice::WriteOnly) || bandFile.write("x\n") != 2) {
                qCritical() << "cannot create band fixture" << i;
                return 1;
            }
            bandFile.close();
        }
        QTableView *bandView = window.leftPane()->tableView();
        if (!waitFor([&window]() {
                return window.leftPane()->directoryModel()->rowCount() >= 4;
            }, 5000)) {
            qCritical() << "band fixtures did not appear";
            return 1;
        }
        bandView->clearSelection();
        QTest::qWait(50);
        QTest::mousePress(bandView->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(30, 10));
        QTest::qWait(30);
        // QTest::mouseMove goes through QCursor::setPos which does not deliver
        // move events on the offscreen platform; send one directly instead.
        QMouseEvent bandMove(QEvent::MouseMove, QPoint(30, 70),
                             bandView->viewport()->mapToGlobal(QPoint(30, 70)),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(bandView->viewport(), &bandMove);
        QTest::qWait(30);
        // Release OUTSIDE any row (regression: base release used to clear the
        // band selection when the release index was invalid).
        QTest::mouseRelease(bandView->viewport(), Qt::LeftButton, Qt::NoModifier,
                            QPoint(30, 500));
        QTest::qWait(50);
        const int bandCount = bandView->selectionModel()->selectedRows(0).size();
        if (bandCount < 2) {
            qCritical() << "rubber band selection failed, selected" << bandCount;
            return 1;
        }
        bandView->clearSelection();
        QTest::qWait(30);
        // Shift+click extends the selection as a range from the current index.
        {
            const QAbstractItemModel *sortModel = bandView->model();
            bandView->selectionModel()->setCurrentIndex(sortModel->index(0, 0),
                                                        QItemSelectionModel::ClearAndSelect);
            QTest::qWait(30);
            QTest::mouseClick(bandView->viewport(), Qt::LeftButton, Qt::ShiftModifier,
                              QPoint(30, 60));
            QTest::qWait(30);
            const int rangeCount = bandView->selectionModel()->selectedRows(0).size();
            if (rangeCount < 3) {
                qCritical() << "shift range selection failed, selected" << rangeCount;
                return 1;
            }
        }
        // Ctrl+click toggles scattered files without losing previous selections.
        {
            QTest::mouseClick(bandView->viewport(), Qt::LeftButton, Qt::ControlModifier,
                              QPoint(30, 80));
            QTest::qWait(30);
            int toggledCount = bandView->selectionModel()->selectedRows(0).size();
            if (toggledCount < 4) {
                qCritical() << "ctrl toggle add failed, selected" << toggledCount;
                return 1;
            }
            QTest::mouseClick(bandView->viewport(), Qt::LeftButton, Qt::ControlModifier,
                              QPoint(30, 80));
            QTest::qWait(30);
            toggledCount = bandView->selectionModel()->selectedRows(0).size();
            if (toggledCount != 3) {
                qCritical() << "ctrl toggle remove failed, selected" << toggledCount;
                return 1;
            }
        }
        bandView->clearSelection();
        QTest::qWait(30);
    }

    // Multiple windows: openNewWindow creates an independent, closable window.
    {
        MainWindow *extra = MainWindow::openNewWindow(leftDir);
        if (!extra->isVisible()) {
            qCritical() << "new window not visible";
            return 1;
        }
        if (!waitFor([&extra]() {
                return extra->leftPane()->directoryModel()->rowCount() > 0;
            }, 5000)) {
            qCritical() << "new window did not load";
            return 1;
        }
        if (extra->leftPane()->directoryModel()->rowForPath(leftDir + "/ui-copy-test.txt") < 0) {
            qCritical() << "new window wrong directory";
            return 1;
        }
        extra->close();
        QTest::qWait(50);
        if (extra->isVisible()) {
            qCritical() << "new window did not close";
            return 1;
        }
        // Offscreen windows do not auto-activate a next window on close; give
        // focus back to the main window so later keyboard tests keep working.
        window.activateWindow();
        window.setFocus();
        window.leftPane()->tableView()->setFocus();
        QTest::qWait(80);
    }
    {
        // WPS-style "show item" flow: a new window opens and selects the file.
        const QString revealItem = leftDir + QStringLiteral("/ui-copy-test.txt");
        MainWindow *revealWindow = MainWindow::openNewWindow(leftDir, QString(), revealItem);
        if (!revealWindow->isVisible()) {
            qCritical() << "reveal window not visible";
            return 1;
        }
        if (!waitFor([&revealWindow]() {
                return revealWindow->leftPane()->directoryModel()->rowCount() > 0;
            }, 5000)) {
            qCritical() << "reveal window did not load";
            return 1;
        }
        bool sawSelection = false;
        waitFor([&revealWindow, &sawSelection]() {
            if (revealWindow->leftPane()->tableView()
                    ->selectionModel()->selectedRows(0).size() >= 1)
                sawSelection = true;
            return false;  // always poll the full window
            }, 5000);
        if (!sawSelection) {
            const DirectoryModel *revealModel = revealWindow->leftPane()->directoryModel();
            qCritical() << "reveal window did not select the item"
                        << "rows:" << revealModel->rowCount()
                        << "loading:" << revealModel->isLoading()
                        << "dir:" << revealModel->directory();
            return 1;
        }
        revealWindow->close();
        QTest::qWait(80);
        window.activateWindow();
        window.setFocus();
        window.leftPane()->tableView()->setFocus();
        QTest::qWait(80);
    }

    const QString externalPath = leftDir + "/watched-external.txt";
    {
        QFile external(externalPath);
        if (!external.open(QIODevice::WriteOnly) || external.write("external\n") != 9) {
            qCritical() << "cannot create external fixture";
            return 1;
        }
    }
    if (!waitFor([&window, &externalPath]() {
            return window.leftPane()->directoryModel()->rowForPath(externalPath) >= 0;
        }, 5000)) {
        qCritical() << "directory watcher did not refresh";
        return 1;
    }
    const QString hiddenPath = leftDir + "/.hidden-external.txt";
    {
        QFile hidden(hiddenPath);
        if (!hidden.open(QIODevice::WriteOnly) || hidden.write("hidden\n") != 7) {
            qCritical() << "cannot create hidden fixture";
            return 1;
        }
    }
    QTest::qWait(500);
    if (window.leftPane()->directoryModel()->rowForPath(hiddenPath) >= 0) {
        qCritical() << "hidden file visible while hidden";
        return 1;
    }
    QToolButton *hiddenButton = window.findChild<QToolButton *>(QStringLiteral("HiddenButton"));
    if (!hiddenButton) {
        qCritical() << "hidden button missing";
        return 1;
    }
    hiddenButton->click();
    if (!waitFor([&window, &hiddenPath]() {
            return window.leftPane()->directoryModel()->rowForPath(hiddenPath) >= 0;
        }, 5000)) {
        qCritical() << "hidden file toggle failed";
        return 1;
    }
    hiddenButton->click();
    QTest::qWait(200);

    QTableView *view = window.leftPane()->tableView();
    view->setFocus();
    selectRowExplicit(view, 0);
    QTest::qWait(120);

    QToolButton *bookmarkButton = window.findChild<QToolButton *>(QStringLiteral("BookmarkButton"));
    if (!bookmarkButton) {
        qCritical() << "bookmark button missing";
        return 1;
    }
    bookmarkButton->click();
    QTest::qWait(100);
    if (!bookmarkButton->isChecked()) {
        qCritical() << "bookmark was not added";
        return 1;
    }
    bookmarkButton->click();
    QTest::qWait(100);
    if (bookmarkButton->isChecked()) {
        qCritical() << "bookmark was not removed";
        return 1;
    }

    view->setFocus();
    selectRowExplicit(view, 0);
    QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
    QTest::qWait(80);
    {
        const QMimeData *clipboardMime = QApplication::clipboard()->mimeData();
        if (!clipboardMime || !clipboardMime->hasUrls() || !clipboardMime->hasText()
            || !clipboardMime->text().contains(QStringLiteral("ui-copy-test.txt"))) {
            qCritical() << "clipboard is missing interoperable URL/plain-text formats";
            return 1;
        }
    }
    QTableView *rightTargetView = window.rightPane()->tableView();
    rightTargetView->setFocus();
    QTest::qWait(80);
    QTest::keyClick(rightTargetView, Qt::Key_V, Qt::ControlModifier);

    QFrame *transferBar = window.findChild<QFrame *>(QStringLiteral("TransferBar"));
    if (!transferBar) {
        qCritical() << "transfer bar not found";
        return 1;
    }
    if (!transferBar->isVisible()) {
        qCritical() << "transfer bar did not become visible";
        return 1;
    }
    {
        QTest::qWait(100);
        window.grab().save(QStringLiteral("/work/file-manager-native/screenshots/native-transfer.png"), "PNG");
    }

    if (!waitFor([&rightDir, &content]() {
            QFile file(rightDir + "/ui-copy-test.txt");
            if (!file.open(QIODevice::ReadOnly))
                return false;
            return file.readAll() == content.toUtf8();
        }, 8000)) {
        qCritical() << "copied file not found or content mismatch";
        return 1;
    }

    TransferManager *manager = window.findChild<TransferManager *>();
    if (!manager) {
        qCritical() << "transfer manager not found";
        return 1;
    }
    if (!waitFor([manager]() {
            const QVector<TransferManager::Snapshot> tasks = manager->tasks();
            for (const TransferManager::Snapshot &task : tasks) {
                if (task.status == TransferManager::TaskStatus::Done)
                    return true;
            }
            return false;
        }, 5000)) {
        qCritical() << "transfer task did not finish";
        return 1;
    }

    // Re-copy the same source: a conflict dialog must appear and offer replace.
    bool conflictShown = false;
    QTimer::singleShot(300, [&conflictShown]() {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal)
            return;
        auto *replace = modal->findChild<QPushButton *>(QStringLiteral("ConflictReplaceButton"));
        if (replace) {
            conflictShown = true;
            replace->click();
        }
    });
    view->setFocus();
    selectRowExplicit(view, window.leftPane()->directoryModel()->rowForPath(leftDir + "/ui-copy-test.txt"));
    QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
    QTest::qWait(100);
    rightTargetView->setFocus();
    QTest::qWait(100);
    QTest::keyClick(rightTargetView, Qt::Key_V, Qt::ControlModifier);
    if (!conflictShown) {
        qCritical() << "same-name copy did not open the conflict dialog";
        return 1;
    }
    if (!waitFor([&rightDir, &content]() {
            QFile file(rightDir + "/ui-copy-test.txt");
            if (!file.open(QIODevice::ReadOnly))
                return false;
            return file.readAll() == content.toUtf8();
        }, 5000)) {
        qCritical() << "replace conflict copy failed";
        return 1;
    }

    // Clipboard copy and cut/paste through the real keyboard shortcuts.
    // Clipboard copy and cut/paste through the real keyboard shortcuts.
    const QString copySource = leftDir + "/clipboard-copy.txt";
    QFile copyFile(copySource);
    if (!copyFile.open(QIODevice::WriteOnly) || copyFile.write("clipboard copy\n") != 15) {
        qCritical() << "cannot create clipboard copy fixture";
        return 1;
    }
    copyFile.close();
    QTest::qWait(400);
    if (!waitFor([&window, &copySource]() {
            return window.leftPane()->directoryModel()->rowForPath(copySource) >= 0;
        }, 3000)) {
        qCritical() << "copy fixture did not appear";
        return 1;
    }
    QTableView *leftView = window.leftPane()->tableView();
    QTableView *rightView = window.rightPane()->tableView();
    if (!waitFor([&window, &copySource]() {
            return !window.leftPane()->directoryModel()->isLoading()
                && window.leftPane()->directoryModel()->rowForPath(copySource) >= 0;
        }, 3000)) {
        qCritical() << "copy fixture row not ready";
        return 1;
    }
    window.activateWindow();
    window.raise();
    QTest::qWait(100);
    leftView->setFocus();
    QTest::qWait(400);
    if (!QApplication::focusWidget()
        || (QApplication::focusWidget() != leftView
            && !leftView->isAncestorOf(QApplication::focusWidget()))) {
        qCritical() << "left view did not take focus after conflict dialog"
                    << QApplication::focusWidget();
        return 1;
    }
    leftView->clearSelection();
    selectRowExplicit(leftView, window.leftPane()->directoryModel()->rowForPath(copySource));
    QTest::qWait(150);
    if (window.leftPane()->selectedPaths() != QStringList() << copySource) {
        qCritical() << "copy selection is not exclusive" << window.leftPane()->selectedPaths();
        return 1;
    }
    QTest::keyClick(leftView, Qt::Key_C, Qt::ControlModifier);
    QTest::qWait(100);
    const QMimeData *copyMime = QApplication::clipboard()->mimeData();
    bool copyOnClipboard = false;
    if (copyMime) {
        for (const QUrl &url : copyMime->urls()) {
            if (url.toLocalFile() == copySource)
                copyOnClipboard = true;
        }
    }
    if (!copyOnClipboard) {
        qCritical() << "copy source was not placed on clipboard";
        return 1;
    }
    rightView->setFocus();
    QTest::qWait(100);
    QTest::keyClick(rightView, Qt::Key_V, Qt::ControlModifier);
    if (!waitFor([&rightDir]() {
            return QFileInfo::exists(rightDir + "/clipboard-copy.txt");
        }, 5000)) {
        qCritical() << "Ctrl+C/Ctrl+V did not copy";
        return 1;
    }

    const QString cutSource = leftDir + "/clipboard-cut.txt";
    QFile cutFile(cutSource);
    if (!cutFile.open(QIODevice::WriteOnly) || cutFile.write("clipboard cut\n") != 14) {
        qCritical() << "cannot create clipboard cut fixture";
        return 1;
    }
    cutFile.close();
    QTest::qWait(400);
    if (!waitFor([&window, &cutSource]() {
            return window.leftPane()->directoryModel()->rowForPath(cutSource) >= 0;
        }, 3000)) {
        qCritical() << "cut fixture did not appear";
        return 1;
    }
    if (!waitFor([&window, &cutSource]() {
            return !window.leftPane()->directoryModel()->isLoading()
                && window.leftPane()->directoryModel()->rowForPath(cutSource) >= 0;
        }, 3000)) {
        qCritical() << "cut fixture row not ready";
        return 1;
    }
    window.activateWindow();
    window.raise();
    QTest::qWait(100);
    leftView->setFocus();
    QTest::qWait(400);
    if (!QApplication::focusWidget()
        || (QApplication::focusWidget() != leftView
            && !leftView->isAncestorOf(QApplication::focusWidget()))) {
        qCritical() << "left view did not take focus after conflict dialog"
                    << QApplication::focusWidget();
        return 1;
    }
    leftView->clearSelection();
    selectRowExplicit(leftView, window.leftPane()->directoryModel()->rowForPath(cutSource));
    QTest::qWait(150);
    if (window.leftPane()->selectedPaths() != QStringList() << cutSource) {
        qCritical() << "cut selection is not exclusive" << window.leftPane()->selectedPaths();
        return 1;
    }
    QTest::keyClick(leftView, Qt::Key_X, Qt::ControlModifier);
    QTest::qWait(100);
    rightView->setFocus();
    QTest::qWait(100);
    QTest::keyClick(rightView, Qt::Key_V, Qt::ControlModifier);
    if (!waitFor([&cutSource, &rightDir]() {
            return !QFileInfo::exists(cutSource)
                && QFileInfo::exists(rightDir + "/clipboard-cut.txt");
        }, 5000)) {
        qCritical() << "Ctrl+X/Ctrl+V did not move";
        return 1;
    }

    // Verify single/dual pane toggle and that the hidden pane comes back.
    QToolButton *viewToggle = window.findChild<QToolButton *>(QStringLiteral("ViewModeButton"));
    if (!viewToggle) {
        qCritical() << "view mode button missing";
        return 1;
    }
    viewToggle->click();
    QTest::qWait(250);
    if (window.rightPane()->isVisible() || window.leftPane()->width() < 600) {
        qCritical() << "single pane layout failed";
        return 1;
    }
    selectRowExplicit(window.leftPane()->tableView(), 0);
    if (!window.leftPane()->isActive()) {
        qCritical() << "single pane selection is not active-styled";
        return 1;
    }
    viewToggle->click();
    QTest::qWait(250);
    if (!window.rightPane()->isVisible() || window.rightPane()->width() < 200) {
        qCritical() << "dual pane layout did not restore";
        return 1;
    }

    // Folder disclosure expands inline one level at a time.
    const QString treeParent = leftDir + QStringLiteral("/tree-parent");
    const QString treeChild = treeParent + QStringLiteral("/tree-child");
    QDir().mkpath(treeChild);
    QFile treeFile(treeParent + QStringLiteral("/child-file.txt"));
    if (!treeFile.open(QIODevice::WriteOnly) || treeFile.write("tree\n") != 5) {
        qCritical() << "tree file fixture failed";
        return 1;
    }
    treeFile.close();
    QFile treeGrandchild(treeChild + QStringLiteral("/grandchild.txt"));
    if (!treeGrandchild.open(QIODevice::WriteOnly) || treeGrandchild.write("deep\n") != 5) {
        qCritical() << "tree grandchild fixture failed";
        return 1;
    }
    treeGrandchild.close();
    window.leftPane()->navigateTo(leftDir, true);
    QTest::qWait(300);
    {
        auto *model = window.leftPane()->directoryModel();
        const int parentRow = model->rowForPath(treeParent);
        const int before = model->rowCount();
        if (parentRow < 0 || !model->canExpandRow(parentRow)) {
            qCritical() << "tree parent row missing";
            return 1;
        }
        if (!model->toggleFolderExpanded(parentRow) || model->rowCount() != before + 2) {
            qCritical() << "tree parent expansion count wrong" << before << model->rowCount();
            return 1;
        }
        if (model->rowForPath(treeChild + QStringLiteral("/grandchild.txt")) >= 0) {
            qCritical() << "tree expanded recursively without user action";
            return 1;
        }
        const int childRow = model->rowForPath(treeChild);
        if (childRow < 0 || model->depthForRow(childRow) != 1
            || !model->canExpandRow(childRow)) {
            qCritical() << "tree child row/depth wrong";
            return 1;
        }
        if (!model->toggleFolderExpanded(childRow)
            || model->rowForPath(treeChild + QStringLiteral("/grandchild.txt")) < 0) {
            qCritical() << "tree manual child expansion failed";
            return 1;
        }
        if (model->toggleFolderExpanded(model->rowForPath(treeParent))
            || model->rowForPath(treeChild) >= 0) {
            qCritical() << "tree collapse failed";
            return 1;
        }
    }

    // Archive actions must invoke the system archive manager.
    {
        const QString fakeManager = leftDir + QStringLiteral("/fake-archive-manager.sh");
        const QString marker = leftDir + QStringLiteral("/archive-manager-args.txt");
        QFile script(fakeManager);
        if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCritical() << "cannot create fake archive manager";
            return 1;
        }
        script.write("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$ARCHIVE_MANAGER_MARKER\"\n");
        script.close();
        QFile::setPermissions(fakeManager, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                             | QFileDevice::ExeOwner);
        qputenv("UOS_ARCHIVE_MANAGER", fakeManager.toUtf8());
        qputenv("ARCHIVE_MANAGER_MARKER", marker.toUtf8());

        const QString archivePath = leftDir + QStringLiteral("/ui-archive.zip");
        QFile archiveFile(archivePath);
        if (!archiveFile.open(QIODevice::WriteOnly) || archiveFile.write("archive\n") != 8) {
            qCritical() << "cannot create archive marker file";
            return 1;
        }
        archiveFile.close();
        QFile::remove(marker);
        window.extractArchive(archivePath);
        if (!waitFor([&marker]() { return QFileInfo::exists(marker); }, 3000)) {
            qCritical() << "archive manager extract was not invoked";
            return 1;
        }
        QFile markerFile(marker);
        if (!markerFile.open(QIODevice::ReadOnly)) {
            qCritical() << "cannot read archive manager marker";
            return 1;
        }
        const QStringList extractArgs = QString::fromUtf8(markerFile.readAll())
                                            .split(QLatin1Char('\n'), QString::SkipEmptyParts);
        markerFile.close();
        if (extractArgs.isEmpty()
            || extractArgs.last() != QStringLiteral("extract")
            || extractArgs.first() != archivePath) {
            qCritical() << "unexpected archive manager extract args" << extractArgs;
            return 1;
        }

        window.leftPane()->navigateTo(leftDir, true);
        QTest::qWait(300);
        auto *archiveModel = window.leftPane()->directoryModel();
        const int sourceRow = archiveModel->rowForPath(leftDir + QStringLiteral("/ui-copy-test.txt"));
        if (sourceRow < 0) {
            qCritical() << "archive compression source missing";
            return 1;
        }
        selectRowExplicit(window.leftPane()->tableView(), sourceRow);
        QFile::remove(marker);
        window.compressSelection();
        if (!waitFor([&marker]() { return QFileInfo::exists(marker); }, 3000)) {
            qCritical() << "archive manager compress was not invoked";
            return 1;
        }
        QFile markerFile2(marker);
        if (!markerFile2.open(QIODevice::ReadOnly)) {
            qCritical() << "cannot read archive manager compress marker";
            return 1;
        }
        const QStringList compressArgs = QString::fromUtf8(markerFile2.readAll())
                                             .split(QLatin1Char('\n'), QString::SkipEmptyParts);
        markerFile2.close();
        if (compressArgs.isEmpty()
            || compressArgs.last() != QStringLiteral("compress")
            || compressArgs.first() != leftDir + QStringLiteral("/ui-copy-test.txt")) {
            qCritical() << "unexpected archive manager compress args" << compressArgs;
            return 1;
        }
        qunsetenv("UOS_ARCHIVE_MANAGER");
        qunsetenv("ARCHIVE_MANAGER_MARKER");
    }

    // A deep path must not force the other pane to shrink.
    const int rightWidthBefore = window.rightPane()->width();
    QString deepPath = leftDir;
    for (int i = 0; i < 8; ++i) {
        deepPath += QStringLiteral("/depth-%1").arg(i);
        QDir().mkpath(deepPath);
    }
    if (!window.leftPane()->navigateTo(deepPath, true)) {
        qCritical() << "deep path navigation failed";
        return 1;
    }
    QTest::qWait(300);
    if (qAbs(window.rightPane()->width() - rightWidthBefore) > 50) {
        qCritical() << "deep path changed pane widths" << rightWidthBefore
                    << window.rightPane()->width();
        return 1;
    }
    window.leftPane()->navigateTo(leftDir, true);
    QTest::qWait(200);

    // Up/Down in preview reports file navigation.
    {
        const QStringList previewPaths = QStringList()
            << QStringLiteral("/work/file-manager-native/tests/preview-fixtures/report.docx")
            << QStringLiteral("/work/file-manager-native/tests/preview-fixtures/budget.xlsx");
        PreviewDialog navPreview(previewPaths, previewPaths.first());
        navPreview.show();
        QTest::qWait(100);
        QTest::keyClick(&navPreview, Qt::Key_Down);
        QTest::qWait(100);
        if (!navPreview.isVisible()
            || navPreview.currentPath() != previewPaths.at(1)) {
            qCritical() << "preview in-place navigation failed";
            return 1;
        }
        navPreview.close();
    }

    // Spacebar preview must open a non-modal preview and close cleanly.
    QTableView *previewView = window.leftPane()->tableView();
    const int previewRow = window.leftPane()->directoryModel()->rowForPath(leftDir + "/ui-copy-test.txt");
    if (previewRow < 0) {
        qCritical() << "preview fixture row missing";
        return 1;
    }
    previewView->setFocus();
    selectRowExplicit(previewView, previewRow);
    QTest::qWait(100);
    bool previewShown = false;
    QTimer::singleShot(250, [&previewShown]() {
        const QWidgetList tops = QApplication::topLevelWidgets();
        for (QWidget *widget : tops) {
            if (widget->objectName() == QStringLiteral("PreviewDialog")) {
                previewShown = true;
                widget->close();
            }
        }
    });
    QTest::keyClick(previewView, Qt::Key_Space);
    QTest::qWait(400);
    if (!previewShown) {
        qCritical() << "space preview did not open";
        return 1;
    }

    // Recursive search must find a file in a subdirectory.
    QDir().mkpath(leftDir + "/search-sub");
    QFile searchFixture(leftDir + "/search-sub/search-target.txt");
    if (!searchFixture.open(QIODevice::WriteOnly) || searchFixture.write("search\n") != 7) {
        qCritical() << "search fixture failed";
        return 1;
    }
    searchFixture.close();
    window.activateWindow();
    window.leftPane()->beginSearch();
    QTest::qWait(100);
    auto *paneSearch = window.leftPane()->findChild<QLineEdit *>(QStringLiteral("PaneSearchInput"));
    if (!paneSearch || !paneSearch->isVisible()) {
        qCritical() << "pane search input missing";
        return 1;
    }
    paneSearch->setText(QStringLiteral("search-target"));
    QTest::keyClick(paneSearch, Qt::Key_Return);
    if (!waitFor([&window]() {
            return window.leftPane()->directoryModel()->isSearchMode()
                && window.leftPane()->directoryModel()->rowCount() > 0;
        }, 5000)) {
        qCritical() << "recursive pane search found no result";
        return 1;
    }
    if (!window.leftPane()->directoryModel()->isSearchMode()) {
        qCritical() << "pane is not in search mode";
        return 1;
    }
    window.leftPane()->endSearch();
    QTest::qWait(200);
    QTest::qWait(100);

    // Office/archive previews should use the structured preview widgets.
    const QString fixtures = QStringLiteral("/work/file-manager-native/tests/preview-fixtures");
    {
        PreviewDialog docPreview(fixtures + "/report.docx");
        docPreview.show();
        QTest::qWait(150);
        if (!docPreview.findChild<QTextBrowser *>(QStringLiteral("PreviewDocument"))) {
            qCritical() << "docx preview missing";
            return 1;
        }
        docPreview.close();
    }
    {
        PreviewDialog sheetPreview(fixtures + "/budget.xlsx");
        sheetPreview.show();
        QTest::qWait(150);
        if (!sheetPreview.findChild<QTableWidget *>(QStringLiteral("PreviewSheet"))) {
            qCritical() << "xlsx preview missing";
            return 1;
        }
        sheetPreview.close();
    }
    {
        PreviewDialog slidePreview(fixtures + "/presentation.pptx");
        slidePreview.show();
        QTest::qWait(150);
        if (!slidePreview.findChild<QScrollArea *>(QStringLiteral("PreviewSlides"))) {
            qCritical() << "pptx preview missing";
            return 1;
        }
        slidePreview.close();
    }
    {
        PreviewDialog legacyDocPreview(fixtures + "/legacy.doc");
        legacyDocPreview.show();
        QTest::qWait(150);
        if (!legacyDocPreview.findChild<QPlainTextEdit *>()) {
            qCritical() << "legacy doc fallback preview missing";
            return 1;
        }
        legacyDocPreview.close();
    }
    {
        PreviewDialog legacyXlsPreview(fixtures + "/legacy.xls");
        legacyXlsPreview.show();
        QTest::qWait(150);
        if (!legacyXlsPreview.findChild<QPlainTextEdit *>()) {
            qCritical() << "legacy xls fallback preview missing";
            return 1;
        }
        legacyXlsPreview.close();
    }
    {
        PreviewDialog wpsPreview(fixtures + "/report.wps");
        wpsPreview.show();
        QTest::qWait(150);
        if (!wpsPreview.findChild<QTextBrowser *>(QStringLiteral("PreviewDocument"))) {
            qCritical() << "wps preview missing";
            return 1;
        }
        wpsPreview.close();
    }
    {
        PreviewDialog etPreview(fixtures + "/budget.et");
        etPreview.show();
        QTest::qWait(150);
        if (!etPreview.findChild<QTableWidget *>(QStringLiteral("PreviewSheet"))) {
            qCritical() << "et preview missing";
            return 1;
        }
        etPreview.close();
    }
    {
        PreviewDialog pdfPreview(fixtures + "/sample.pdf");
        pdfPreview.show();
        if (!waitFor([&pdfPreview]() {
                return pdfPreview.findChild<QScrollArea *>(QStringLiteral("PreviewPdf"))
                    || pdfPreview.findChild<QPlainTextEdit *>(QStringLiteral("PreviewPdfText"));
            }, 5000)) {
            qCritical() << "pdf preview missing";
            return 1;
        }
        pdfPreview.close();
    }
    {
        PreviewDialog ofdPreview(fixtures + "/sample.ofd");
        ofdPreview.show();
        QTest::qWait(150);
        if (!ofdPreview.findChild<QTextBrowser *>(QStringLiteral("PreviewDocument"))) {
            qCritical() << "ofd preview missing";
            return 1;
        }
        ofdPreview.close();
    }
    {
        PreviewDialog archivePreview(fixtures + "/archive.zip");
        archivePreview.show();
        QTest::qWait(150);
        if (!archivePreview.findChild<QTableWidget *>(QStringLiteral("PreviewArchive"))) {
            qCritical() << "zip preview missing";
            return 1;
        }
        archivePreview.close();
    }
    {
        // CSV files should render as a table with decoded (UTF-8) content.
        PreviewDialog csvPreview(fixtures + "/data.csv");
        csvPreview.show();
        QTest::qWait(150);
        QTableWidget *csvTable = csvPreview.findChild<QTableWidget *>(
            QStringLiteral("PreviewSheet"));
        if (!csvTable || csvTable->rowCount() < 3) {
            qCritical() << "csv preview missing";
            return 1;
        }
        csvPreview.close();
    }
    {
        // GBK-encoded text must be decoded correctly instead of showing mojibake.
        PreviewDialog gbkPreview(fixtures + "/chinese-gbk.txt");
        gbkPreview.show();
        QTest::qWait(150);
        QPlainTextEdit *gbkText = gbkPreview.findChild<QPlainTextEdit *>();
        if (!gbkText || !gbkText->toPlainText().contains(QStringLiteral("项目进度报告"))) {
            qCritical() << "gbk text preview mojibake or missing";
            return 1;
        }
        gbkPreview.close();
    }
    {
        // Images get the zoomable viewer instead of a static label.
        PreviewDialog imagePreview(fixtures + "/photo.png");
        imagePreview.show();
        QTest::qWait(150);
        if (!imagePreview.findChild<QWidget *>(QStringLiteral("PreviewImage"))) {
            qCritical() << "image preview missing";
            return 1;
        }
        imagePreview.close();
    }
    {
        // 7z archives are listed through the 7z command line when available.
        PreviewDialog sevenZipPreview(fixtures + "/archive.7z");
        sevenZipPreview.show();
        QTest::qWait(150);
        QTableWidget *sevenZipTable = sevenZipPreview.findChild<QTableWidget *>(
            QStringLiteral("PreviewArchive"));
        if (!QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty()
            && !sevenZipTable) {
            qCritical() << "7z preview missing";
            return 1;
        }
        sevenZipPreview.close();
    }

    // Full-text mode uses the WSS test database and current-directory scope check.
    QDir().mkpath(QStringLiteral("/tmp/wss-scope/sub"));
    if (!window.leftPane()->navigateTo(QStringLiteral("/tmp/wss-scope"), true)) {
        qCritical() << "fulltext scope navigation failed";
        return 1;
    }
    QTest::qWait(200);
    window.leftPane()->beginSearch();
    auto *searchMode = window.leftPane()->findChild<QComboBox *>(QStringLiteral("PaneSearchMode"));
    auto *fullSearchEdit = window.leftPane()->findChild<QLineEdit *>(QStringLiteral("PaneSearchInput"));
    if (!searchMode || !fullSearchEdit) {
        qCritical() << "fulltext search UI missing";
        return 1;
    }
    searchMode->setCurrentIndex(1);
    fullSearchEdit->setText(QStringLiteral("测试"));
    QTest::keyClick(fullSearchEdit, Qt::Key_Return);
    if (!waitFor([&window]() {
            return window.leftPane()->directoryModel()->isFullTextSearch()
                && window.leftPane()->directoryModel()->rowCount() == 2;
        }, 5000)) {
        qCritical() << "fulltext UI search failed";
        return 1;
    }
    window.leftPane()->endSearch();
    QTest::qWait(200);

    QDir().mkpath(leftDir);
    const QString revealTarget = leftDir + QStringLiteral("/reveal-target.txt");
    QFile revealFixture(revealTarget);
    if (!revealFixture.open(QIODevice::WriteOnly) || revealFixture.write("reveal\n") != 7) {
        qCritical() << "reveal fixture failed";
        return 1;
    }
    revealFixture.close();
    window.leftPane()->navigateTo(leftDir, true);
    QTest::qWait(300);
    window.leftPane()->revealPath(revealTarget);
    if (!waitFor([&window, revealTarget]() {
            const QModelIndex current = window.leftPane()->tableView()->currentIndex();
            FileEntry entry;
            return current.isValid()
                && window.leftPane()->directoryModel()->entryAt(current.row(), &entry)
                && entry.path == revealTarget;
        }, 3000)) {
        qCritical() << "external reveal did not select file";
        return 1;
    }

    QTest::qWait(300);
    selectRowExplicit(window.leftPane()->tableView(), 0);
    QTest::qWait(150);
    const QString shot = QStringLiteral("/work/file-manager-native/screenshots/native-selected.png");
    window.grab().save(shot, "PNG");
    qInfo() << "UI_SMOKE_OK" << shot;
    return 0;
}
