#include "mainwindow.h"

#include "directorymodel.h"
#include "fileoperations.h"
#include "ejecttask.h"
#include "openwithdialog.h"
#include "previewdialog.h"
#include "propertiesdialog.h"
#include "filepane.h"
#include "formatters.h"
#include "iconprovider.h"
#include "theme.h"
#include "transferdialog.h"
#include "trashdialog.h"
#include "transfermanager.h"
#include "uidialogs.h"

#include <QAction>
#include <algorithm>
#include <QThreadPool>
#include <QSysInfo>
#include <QSignalBlocker>
#include <functional>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QProcess>
#include <QProgressBar>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStorageInfo>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <QSet>

namespace {
void clearLayout(QLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        if (QLayout *child = item->layout())
            clearLayout(child);
        delete item;
    }
}


bool onRemovableStorage(const QStringList &paths)
{
    for (const QString &path : paths) {
        const QStorageInfo storage(path);
        if (!storage.isValid() || !storage.isReady())
            continue;
        const QString root = QDir::cleanPath(storage.rootPath());
        if (root.startsWith(QStringLiteral("/media/"))
            || root.startsWith(QStringLiteral("/run/media/"))
            || root.startsWith(QStringLiteral("/mnt/")))
            return true;
    }
    return false;
}

bool isArchivePath(const QString &path)
{
    const QString lower = path.toLower();
    return lower.endsWith(QStringLiteral(".zip"))
        || lower.endsWith(QStringLiteral(".tar"))
        || lower.endsWith(QStringLiteral(".tar.gz"))
        || lower.endsWith(QStringLiteral(".tgz"))
        || lower.endsWith(QStringLiteral(".tar.bz2"))
        || lower.endsWith(QStringLiteral(".tbz2"))
        || lower.endsWith(QStringLiteral(".tar.xz"))
        || lower.endsWith(QStringLiteral(".txz"))
        || lower.endsWith(QStringLiteral(".7z"))
        || lower.endsWith(QStringLiteral(".rar"));
}

QString archiveManagerPath()
{
    const QString overridden = qEnvironmentVariable("UOS_ARCHIVE_MANAGER");
    if (!overridden.isEmpty())
        return overridden;
    return QStandardPaths::findExecutable(QStringLiteral("deepin-compressor"));
}

QString namesPreview(const QStringList &paths, int limit = 3)
{
    QStringList names;
    for (int i = 0; i < paths.size() && i < limit; ++i)
        names.append(QFileInfo(paths.at(i)).fileName());
    if (paths.size() > limit)
        names.append(QStringLiteral("等 %1 项").arg(paths.size()));
    return names.join(QStringLiteral("、"));
}
}

MainWindow::MainWindow(const QString &startLeft, const QString &startRight, QWidget *parent)
    : QMainWindow(parent)
    , m_startLeft(startLeft)
    , m_startRight(startRight)
{
    QSettings settings;
    Theme::instance().applyToApplication();

    setWindowIcon(IconProvider::instance().appIcon());
    setWindowTitle(QStringLiteral("文件管理器"));
    resize(1400, 860);
    setMinimumSize(1024, 640);

    const QByteArray geometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    } else if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        const QSize desired(qMin(1400, available.width() - 80), qMin(860, available.height() - 80));
        resize(desired);
        move(available.center() - rect().center());
    }

    m_transfers = new TransferManager(this);
    loadBookmarks();
    m_showHidden = settings.value(QStringLiteral("ui/showHidden"), false).toBool();
    buildUi();
    buildShortcuts();

    const QString home = QDir::homePath();
    QString left = m_startLeft.isEmpty()
        ? settings.value(QStringLiteral("paths/left"), home).toString() : m_startLeft;
    QString right = m_startRight.isEmpty()
        ? settings.value(QStringLiteral("paths/right"), home).toString() : m_startRight;
    if (left.isEmpty() || !QFileInfo(left).isDir())
        left = home;
    if (right.isEmpty() || !QFileInfo(right).isDir())
        right = home;

    m_left->setShowHidden(m_showHidden);
    m_right->setShowHidden(m_showHidden);
    updateHiddenButton();
    m_left->navigateTo(left, true);
    m_right->navigateTo(right, true);
    setActivePane(m_left);
    updateActionStates();
    m_left->focusView();
    setSinglePane(settings.value(QStringLiteral("ui/singlePane"), false).toBool(), false);
    updateBookmarkButton();
    m_deviceSignature = deviceSignature();
    m_deviceTimer = new QTimer(this);
    m_deviceTimer->setInterval(3000);
    connect(m_deviceTimer, &QTimer::timeout, this, &MainWindow::refreshDevicesIfChanged);
    m_deviceTimer->start();
}

MainWindow::~MainWindow()
{
    if (m_ejectTask) {
        m_ejectTask->wait(20000);
        m_ejectTask = nullptr;
    }
    if (m_transfers)
        m_transfers->shutdown();
    // Let background directory scans finish before the pane models are destroyed.
    QThreadPool::globalInstance()->waitForDone();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("MainCentral"));
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    buildToolbar();
    rootLayout->addWidget(m_toolbar);

    auto *content = new QWidget(central);
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_sidebar = new QFrame(content);
    m_sidebar->setObjectName(QStringLiteral("Sidebar"));
    m_sidebar->setFixedWidth(226);
    auto *sidebarLayout = new QVBoxLayout(m_sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    auto *scroll = new QScrollArea(m_sidebar);
    scroll->setObjectName(QStringLiteral("SidebarScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sideContent = new QWidget(scroll);
    m_sideContent->setObjectName(QStringLiteral("SidebarContent"));
    m_sideLayout = new QVBoxLayout(m_sideContent);
    m_sideLayout->setContentsMargins(9, 10, 9, 10);
    m_sideLayout->setSpacing(2);
    scroll->setWidget(m_sideContent);
    sidebarLayout->addWidget(scroll);
    contentLayout->addWidget(m_sidebar);

    m_splitter = new QSplitter(Qt::Horizontal, content);
    m_splitter->setObjectName(QStringLiteral("PaneSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);
    m_left = new FilePane(&IconProvider::instance(), m_splitter);
    m_right = new FilePane(&IconProvider::instance(), m_splitter);
    m_splitter->addWidget(m_left);
    m_splitter->addWidget(m_right);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes(QList<int>() << 590 << 590);
    contentLayout->addWidget(m_splitter, 1);
    rootLayout->addWidget(content, 1);
    buildTransferBar(rootLayout);

    setCentralWidget(central);
    buildStatusBar();
    refreshSidebar();

    connect(m_left, &FilePane::activationRequested, this, &MainWindow::setActivePane);
    connect(m_right, &FilePane::activationRequested, this, &MainWindow::setActivePane);
    connect(m_left, &FilePane::pathChanged, this, [this](FilePane *) {
        updateSidebarActive();
        updateActionStates();
    });
    connect(m_right, &FilePane::pathChanged, this, [this](FilePane *) {
        updateSidebarActive();
        updateActionStates();
    });
    connect(m_left, &FilePane::selectionChanged, this, [this](FilePane *pane) {
        if (pane == m_active) updateActionStates();
    });
    connect(m_right, &FilePane::selectionChanged, this, [this](FilePane *pane) {
        if (pane == m_active) updateActionStates();
    });
    connect(m_left, &FilePane::navigationChanged, this, [this](FilePane *pane) {
        if (pane == m_active) updateActionStates();
    });
    connect(m_right, &FilePane::navigationChanged, this, [this](FilePane *pane) {
        if (pane == m_active) updateActionStates();
    });
    connect(m_left, &FilePane::statusMessage, this, [this](const QString &message) {
        statusBar()->showMessage(message, 6000);
    });
    connect(m_right, &FilePane::statusMessage, this, [this](const QString &message) {
        statusBar()->showMessage(message, 6000);
    });
    connect(m_left, &FilePane::dropTransferRequested, this,
            [this](const QStringList &sources, const QString &destination, bool move) {
        setActivePane(m_left);
        handleDropTransfer(sources, destination, move);
    });
    connect(m_right, &FilePane::dropTransferRequested, this,
            [this](const QStringList &sources, const QString &destination, bool move) {
        setActivePane(m_right);
        handleDropTransfer(sources, destination, move);
    });
    connect(m_left, &FilePane::contextMenuRequested, this, &MainWindow::showContextMenu);
    connect(m_right, &FilePane::contextMenuRequested, this, &MainWindow::showContextMenu);
    connect(m_left, &FilePane::deleteRequested, this, [this](FilePane *pane) {
        setActivePane(pane);
        trashSelected();
    });
    connect(m_right, &FilePane::deleteRequested, this, [this](FilePane *pane) {
        setActivePane(pane);
        trashSelected();
    });
    connect(m_left, &FilePane::renameRequested, this, [this](FilePane *pane) {
        setActivePane(pane);
        renameSelected();
    });
    connect(m_right, &FilePane::renameRequested, this, [this](FilePane *pane) {
        setActivePane(pane);
        renameSelected();
    });
    connect(m_left, &FilePane::copyRequested, this, [this](FilePane *pane) {
        setActivePane(pane);
        copySelectionToClipboard(false);
    });
    connect(m_right, &FilePane::copyRequested, this, [this](FilePane *pane) {
        setActivePane(pane);
        copySelectionToClipboard(false);
    });
    connect(m_left, &FilePane::cutRequested, this, [this](FilePane *pane) {
        setActivePane(pane);
        copySelectionToClipboard(true);
    });
    connect(m_right, &FilePane::cutRequested, this, [this](FilePane *pane) {
        setActivePane(pane);
        copySelectionToClipboard(true);
    });
    connect(m_left, &FilePane::pasteRequested, this, [this](FilePane *pane) {
        setActivePane(pane);
        pasteClipboard();
    });
    connect(m_right, &FilePane::pasteRequested, this, [this](FilePane *pane) {
        setActivePane(pane);
        pasteClipboard();
    });
    connect(m_left, &FilePane::previewRequested, this, [this](FilePane *pane, const QString &path) {
        setActivePane(pane);
        showPreviewFile(path);
    });
    connect(m_right, &FilePane::previewRequested, this, [this](FilePane *pane, const QString &path) {
        setActivePane(pane);
        showPreviewFile(path);
    });
    connect(m_left, &FilePane::copyToOtherPaneRequested, this, [this](FilePane *pane) {
        setActivePane(pane);
        startTransfer(false);
    });
    connect(m_right, &FilePane::copyToOtherPaneRequested, this, [this](FilePane *pane) {
        setActivePane(pane);
        startTransfer(false);
    });

    connect(m_transfers, &TransferManager::currentChanged, this, &MainWindow::updateTransferBar);
    connect(m_transfers, &TransferManager::tasksChanged, this, &MainWindow::updateTransferTaskButton);
    connect(m_transfers, &TransferManager::taskFinished, this,
            [this](int, bool success, const QString &error) {
        if (m_left)
            m_left->refresh();
        if (m_right)
            m_right->refresh();
        if (success)
            statusBar()->showMessage(QStringLiteral("传输完成"), 4000);
        else if (!error.isEmpty() && error != QStringLiteral("已取消"))
            statusBar()->showMessage(QStringLiteral("传输失败：%1").arg(error), 8000);
        else
            statusBar()->showMessage(QStringLiteral("传输已取消"), 4000);
        updateTransferBar();
        updateTransferTaskButton();
    });
    updateTransferBar();
    updateTransferTaskButton();
}

void MainWindow::buildToolbar()
{
    m_toolbar = new QFrame(this);
    m_toolbar->setObjectName(QStringLiteral("TopToolbar"));
    m_toolbar->setFixedHeight(62);
    auto *layout = new QHBoxLayout(m_toolbar);
    layout->setContentsMargins(14, 0, 14, 0);
    layout->setSpacing(3);

    auto addTool = [this, layout](const QString &icon, const QString &tooltip) {
        auto *button = new QToolButton(m_toolbar);
        button->setIcon(IconProvider::instance().uiIcon(icon, Theme::instance().textSecondary()));
        button->setIconSize(QSize(20, 20));
        button->setFixedSize(34, 34);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(tooltip);
        m_toolbarIcons.append(qMakePair(button, icon));
        layout->addWidget(button);
        return button;
    };

    m_backButton = addTool(QStringLiteral("arrow-left"), QStringLiteral("后退 (Alt+←)"));
    m_forwardButton = addTool(QStringLiteral("arrow-right"), QStringLiteral("前进 (Alt+→)"));
    m_upButton = addTool(QStringLiteral("arrow-up"), QStringLiteral("上一级 (Alt+↑)"));
    m_refreshButton = addTool(QStringLiteral("refresh"), QStringLiteral("刷新 (Ctrl+R)"));
    layout->addSpacing(5);

    m_newFolderButton = addTool(QStringLiteral("folder-plus"),
                                QStringLiteral("新建文件夹"));
    connect(m_newFolderButton, &QToolButton::clicked, this, &MainWindow::createFolder);
    m_newFileButton = addTool(QStringLiteral("file-plus"),
                              QStringLiteral("新建文件"));
    connect(m_newFileButton, &QToolButton::clicked, this, &MainWindow::createTextFile);
    m_terminalButton = addTool(QStringLiteral("terminal"),
                               QStringLiteral("在终端打开当前文件夹"));
    m_terminalButton->setObjectName(QStringLiteral("TerminalButton"));
    connect(m_terminalButton, &QToolButton::clicked, this, [this]() {
        if (m_active)
            openTerminalAt(m_active->currentPath());
    });

    m_newWindowButton = addTool(QStringLiteral("external"), QStringLiteral("新窗口 (Ctrl+N)"));
    m_newWindowButton->setObjectName(QStringLiteral("NewWindowButton"));
    connect(m_newWindowButton, &QToolButton::clicked, this, [this]() {
        openNewWindow(m_active ? m_active->currentPath() : QString());
    });

    m_viewModeButton = new QToolButton(m_toolbar);
    m_viewModeButton->setObjectName(QStringLiteral("ViewModeButton"));
    m_viewModeButton->setIcon(IconProvider::instance().uiIcon(QStringLiteral("columns"), Theme::instance().textSecondary()));
    m_viewModeButton->setIconSize(QSize(20, 20));
    m_viewModeButton->setFixedSize(34, 34);
    m_viewModeButton->setCursor(Qt::PointingHandCursor);
    m_viewModeButton->setFocusPolicy(Qt::NoFocus);
    m_viewModeButton->setCheckable(true);
    m_viewModeButton->setChecked(true);
    m_viewModeButton->setToolTip(QStringLiteral("切换为单栏"));
    connect(m_viewModeButton, &QToolButton::toggled, this, [this](bool checked) {
        setSinglePane(!checked);
    });
    layout->addWidget(m_viewModeButton);

    m_bookmarkButton = new QToolButton(m_toolbar);
    m_bookmarkButton->setObjectName(QStringLiteral("BookmarkButton"));
    m_bookmarkButton->setIcon(IconProvider::instance().uiIcon(QStringLiteral("star"), Theme::instance().textSecondary()));
    m_bookmarkButton->setIconSize(QSize(20, 20));
    m_bookmarkButton->setFixedSize(34, 34);
    m_bookmarkButton->setCursor(Qt::PointingHandCursor);
    m_bookmarkButton->setFocusPolicy(Qt::NoFocus);
    m_bookmarkButton->setCheckable(true);
    m_bookmarkButton->setToolTip(QStringLiteral("添加/移除当前文件夹书签"));
    connect(m_bookmarkButton, &QToolButton::clicked, this, &MainWindow::toggleBookmark);
    layout->addWidget(m_bookmarkButton);

    m_hiddenButton = new QToolButton(m_toolbar);
    m_hiddenButton->setObjectName(QStringLiteral("HiddenButton"));
    m_hiddenButton->setIcon(IconProvider::instance().uiIcon(QStringLiteral("eye"), Theme::instance().textSecondary()));
    m_hiddenButton->setIconSize(QSize(20, 20));
    m_hiddenButton->setFixedSize(34, 34);
    m_hiddenButton->setCursor(Qt::PointingHandCursor);
    m_hiddenButton->setFocusPolicy(Qt::NoFocus);
    m_hiddenButton->setCheckable(true);
    m_hiddenButton->setToolTip(QStringLiteral("显示隐藏文件 (Ctrl+H)"));
    connect(m_hiddenButton, &QToolButton::clicked, this, &MainWindow::toggleHiddenFiles);
    layout->addWidget(m_hiddenButton);
    layout->addStretch(1);

    m_searchButton = new QToolButton(m_toolbar);
    m_searchButton->setObjectName(QStringLiteral("SearchButton"));
    m_searchButton->setIcon(IconProvider::instance().uiIcon(
        QStringLiteral("search"), Theme::instance().textSecondary()));
    m_searchButton->setIconSize(QSize(20, 20));
    m_searchButton->setFixedSize(34, 34);
    m_searchButton->setCursor(Qt::PointingHandCursor);
    m_searchButton->setFocusPolicy(Qt::NoFocus);
    m_searchButton->setToolTip(QStringLiteral("搜索当前文件夹及子文件夹 (Ctrl+F)"));
    connect(m_searchButton, &QToolButton::clicked, this, &MainWindow::focusSearch);
    layout->addWidget(m_searchButton);
    m_toolbarIcons.append(qMakePair(m_searchButton, QStringLiteral("search")));

    m_tasksButton = new QToolButton(m_toolbar);
    m_tasksButton->setObjectName(QStringLiteral("TasksButton"));
    m_tasksButton->setIcon(IconProvider::instance().uiIcon(QStringLiteral("transfer"), Theme::instance().textSecondary()));
    m_tasksButton->setIconSize(QSize(20, 20));
    m_tasksButton->setFixedSize(34, 34);
    m_tasksButton->setCursor(Qt::PointingHandCursor);
    m_tasksButton->setFocusPolicy(Qt::NoFocus);
    m_tasksButton->setToolTip(QStringLiteral("传输中心"));
    connect(m_tasksButton, &QToolButton::clicked, this, &MainWindow::openTransferCenter);
    layout->addWidget(m_tasksButton);
    m_toolbarIcons.append(qMakePair(m_tasksButton, QStringLiteral("transfer")));

    connect(m_backButton, &QToolButton::clicked, this, &MainWindow::navigateBack);
    connect(m_forwardButton, &QToolButton::clicked, this, &MainWindow::navigateForward);
    connect(m_upButton, &QToolButton::clicked, this, &MainWindow::navigateUp);
    connect(m_refreshButton, &QToolButton::clicked, this, &MainWindow::refreshActive);

    updateToolbarIcons();
}

void MainWindow::buildSidebar()
{
    refreshSidebar();
}

void MainWindow::refreshSidebar()
{
    if (!m_sideLayout)
        return;
    clearLayout(m_sideLayout);
    m_sideEntries.clear();

    addSectionLabel(QStringLiteral("快捷访问"));
    const QString home = QDir::homePath();
    addSideButton(QStringLiteral("home"), QStringLiteral("主目录"), home);

    struct QuickPath {
        QStandardPaths::StandardLocation location;
        const char *icon;
        const char *label;
    };
    const QuickPath quickPaths[] = {
        {QStandardPaths::DesktopLocation, "desktop", "桌面"},
        {QStandardPaths::DocumentsLocation, "file-text", "文档"},
        {QStandardPaths::PicturesLocation, "image", "图片"},
        {QStandardPaths::MusicLocation, "music", "音乐"},
        {QStandardPaths::MoviesLocation, "video", "视频"},
        {QStandardPaths::DownloadLocation, "download", "下载"}
    };
    QSet<QString> added;
    added.insert(QDir::cleanPath(home));
    for (const QuickPath &quick : quickPaths) {
        const QString path = QStandardPaths::writableLocation(quick.location);
        if (path.isEmpty() || !QFileInfo(path).isDir())
            continue;
        const QString clean = QDir::cleanPath(path);
        if (added.contains(clean))
            continue;
        added.insert(clean);
        addSideButton(QString::fromLatin1(quick.icon), QString::fromUtf8(quick.label), clean);
    }
    const QString trashPath = trashFilesPath();
    if (!added.contains(QDir::cleanPath(trashPath))) {
        addSideButton(QStringLiteral("trash"), QStringLiteral("回收站"), trashPath);
        if (QToolButton *button = m_sideEntries.last().button) {
            disconnect(button, &QToolButton::clicked, nullptr, nullptr);
            connect(button, &QToolButton::clicked, this, &MainWindow::openTrashDialog);
            button->setToolTip(QStringLiteral("查看回收站"));
        }
    }

    if (!m_bookmarks.isEmpty()) {
        addSectionLabel(QStringLiteral("书签"));
        for (const QString &bookmark : m_bookmarks) {
            const QString label = bookmarkLabel(bookmark);
            addSideButton(QStringLiteral("star"), label, bookmark);
            if (QToolButton *button = m_sideEntries.last().button) {
                button->setToolTip(bookmark);
                button->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(button, &QWidget::customContextMenuRequested, this,
                        [this, button, bookmark](const QPoint &) {
                    QMenu menu(this);
                    QAction *renameAction = menu.addAction(QStringLiteral("重命名书签…"));
                    connect(renameAction, &QAction::triggered, this, [this, button, bookmark]() {
                        bool accepted = false;
                        const QString initial = bookmarkLabel(bookmark);
                        const QString name = UiDialogs::getText(
                            this, QStringLiteral("重命名书签"), QStringLiteral("书签名称"),
                            initial, &accepted);
                        if (!accepted)
                            return;
                        const QString trimmed = name.trimmed();
                        if (trimmed.isEmpty())
                            m_bookmarkNames.remove(bookmark);
                        else
                            m_bookmarkNames.insert(bookmark, trimmed);
                        saveBookmarks();
                        refreshSidebar();
                    });
                    QAction *removeAction = menu.addAction(QStringLiteral("移除书签"));
                    connect(removeAction, &QAction::triggered, this, [this, bookmark]() {
                        m_bookmarks.removeAll(bookmark);
                        m_bookmarkNames.remove(bookmark);
                        saveBookmarks();
                        refreshSidebar();
                        updateBookmarkButton();
                    });
                    menu.exec(button->mapToGlobal(button->rect().bottomLeft()));
                });
            }
        }
    }

    addSectionLabel(QStringLiteral("设备"));
    const QStorageInfo systemStorage = QStorageInfo::root();
    const QStorageInfo dataStorage(QDir::homePath());
    addSideButton(QStringLiteral("drive"), QStringLiteral("系统盘"), QStringLiteral("/"),
                  IconProvider::instance().deviceIcon(IconProvider::DeviceKind::System));
    if (QToolButton *systemButton = m_sideEntries.last().button) {
        systemButton->setToolTip(QStringLiteral("系统盘 /\n可用 %1 / 共 %2\n%3")
            .arg(formatSize(systemStorage.bytesAvailable()),
                 formatSize(systemStorage.bytesTotal()),
                 QString::fromLocal8Bit(systemStorage.device())));
    }
    addSideButton(QStringLiteral("home"), QStringLiteral("数据盘"), QDir::homePath(),
                  IconProvider::instance().deviceIcon(IconProvider::DeviceKind::Data));
    if (QToolButton *dataButton = m_sideEntries.last().button) {
        dataButton->setToolTip(QStringLiteral("数据盘 %1\n可用 %2 / 共 %3")
            .arg(QDir::homePath(),
                 formatSize(dataStorage.bytesAvailable()),
                 formatSize(dataStorage.bytesTotal())));
    }

    QStringList seenRemovable;
    const QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &volume : volumes) {
        if (!volume.isValid() || !volume.isReady())
            continue;
        const QString root = QDir::cleanPath(volume.rootPath());
        if (root.isEmpty() || root == QStringLiteral("/") || root == QDir::cleanPath(QDir::homePath()))
            continue;
        if (!QFileInfo(root).isDir())
            continue;
        const bool removableMount = root.startsWith(QStringLiteral("/media/"))
            || root.startsWith(QStringLiteral("/run/media/"))
            || root.startsWith(QStringLiteral("/mnt/"));
        if (!removableMount)
            continue;
        const QString device = QString::fromLocal8Bit(volume.device());
        QString label = volume.displayName().trimmed();
        if (label.isEmpty())
            label = volume.name().trimmed();
        if (label.isEmpty())
            label = QFileInfo(root).fileName();
        if (label.contains(QStringLiteral("_dde_data"), Qt::CaseInsensitive)
            || root.contains(QStringLiteral("_dde_data"), Qt::CaseInsensitive)
            || device.contains(QStringLiteral("_dde_data"), Qt::CaseInsensitive))
            continue;
        if (label.isEmpty())
            label = QStringLiteral("可移动磁盘");
        if (seenRemovable.contains(root))
            continue;
        seenRemovable.append(root);
        const bool looksLikeDisk = label.contains(QStringLiteral("移动")) ||
            label.contains(QStringLiteral("硬盘")) ||
            label.contains(QStringLiteral("HDD"), Qt::CaseInsensitive) ||
            label.contains(QStringLiteral("SSD"), Qt::CaseInsensitive);
        const auto kind = looksLikeDisk ? IconProvider::DeviceKind::External
                                        : IconProvider::DeviceKind::Usb;
        addRemovableDeviceRow(QStringLiteral("usb"), label, root, device,
                              IconProvider::instance().deviceIcon(kind));
        if (QToolButton *button = m_sideEntries.last().button) {
            button->setToolTip(QStringLiteral("%1\n可用 %2 / 共 %3\n%4")
                .arg(root, formatSize(volume.bytesAvailable()),
                     formatSize(volume.bytesTotal()), device));
        }
    }
    m_sideLayout->addStretch(1);
    updateSidebarActive();
    m_deviceSignature = deviceSignature();
}

void MainWindow::addRemovableDeviceRow(const QString &iconName, const QString &label,
                                       const QString &mountPath, const QString &devicePath,
                                       const QIcon &customIcon)
{
    auto *row = new QWidget(m_sideContent);
    row->setObjectName(QStringLiteral("DeviceRow"));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto *button = new QToolButton(row);
    button->setObjectName(QStringLiteral("SideButton"));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIcon(customIcon.isNull()
                        ? IconProvider::instance().uiIcon(iconName, Theme::instance().textSecondary())
                        : customIcon);
    button->setIconSize(QSize(19, 19));
    button->setText(label);
    button->setToolTip(mountPath);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setFixedHeight(36);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(true);
    layout->addWidget(button, 1);

    auto *eject = new QToolButton(row);
    eject->setObjectName(QStringLiteral("DeviceEject"));
    eject->setIcon(IconProvider::instance().uiIcon(QStringLiteral("eject"),
                                                   Theme::instance().textMuted()));
    eject->setIconSize(QSize(15, 15));
    eject->setFixedSize(27, 27);
    eject->setCursor(Qt::PointingHandCursor);
    eject->setFocusPolicy(Qt::NoFocus);
    eject->setToolTip(QStringLiteral("弹出 %1").arg(label));
    layout->addWidget(eject, 0, Qt::AlignVCenter);

    m_sideLayout->addWidget(row);
    SideEntry entry;
    entry.button = button;
    entry.path = QDir::cleanPath(mountPath);
    entry.iconName = iconName;
    entry.customIcon = customIcon;
    entry.ejectButton = eject;
    m_sideEntries.append(entry);

    connect(button, &QToolButton::clicked, this, [this, mountPath]() {
        if (!m_active)
            return;
        if (m_active->navigateTo(mountPath, true))
            m_active->focusView();
    });
    connect(eject, &QToolButton::clicked, this, [this, mountPath, label, devicePath]() {
        ejectRemovableDevice(mountPath, label, devicePath);
    });
    auto setupMenu = [this, mountPath, label, devicePath](QWidget *widget) {
        widget->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(widget, &QWidget::customContextMenuRequested, this,
                [this, mountPath, label, devicePath](const QPoint &pos) {
            showDeviceMenu(mountPath, label, devicePath, QCursor::pos());
        });
    };
    setupMenu(button);
    setupMenu(eject);
}

void MainWindow::showDeviceMenu(const QString &mountPath, const QString &label,
                                const QString &devicePath, const QPoint &globalPos)
{
    QMenu menu(this);
    QAction *openAction = menu.addAction(
        IconProvider::instance().uiIcon(QStringLiteral("folder"), Theme::instance().textSecondary()),
        QStringLiteral("打开"));
    QAction *ejectAction = menu.addAction(
        IconProvider::instance().uiIcon(QStringLiteral("eject"), Theme::instance().textSecondary()),
        QStringLiteral("弹出 %1").arg(label));
    connect(openAction, &QAction::triggered, this, [this, mountPath]() {
        if (m_active && m_active->navigateTo(mountPath, true))
            m_active->focusView();
    });
    connect(ejectAction, &QAction::triggered, this, [this, mountPath, label, devicePath]() {
        ejectRemovableDevice(mountPath, label, devicePath);
    });
    menu.exec(globalPos);
}

void MainWindow::ejectRemovableDevice(const QString &mountPath, const QString &label,
                                      const QString &devicePath)
{
    if (m_ejectTask) {
        statusBar()->showMessage(QStringLiteral("正在弹出，请稍候…"), 2500);
        return;
    }
    if (m_transfers && m_transfers->hasActive()) {
        UiDialogs::showWarning(this, QStringLiteral("弹出磁盘"),
                             QStringLiteral("有传输任务正在进行，请先在传输中心完成或取消任务后再弹出。"));
        return;
    }
    if (!UiDialogs::confirm(this, QStringLiteral("弹出磁盘"),
                            QStringLiteral("弹出「%1」？请确保没有程序正在读写该设备。").arg(label),
                            QStringLiteral("弹出"), QStringLiteral("取消"), true))
        return;

    if (m_left)
        m_left->setMountEjecting(mountPath);
    if (m_right)
        m_right->setMountEjecting(mountPath);
    statusBar()->showMessage(QStringLiteral("正在弹出：%1…").arg(label), 10000);
    m_ejectTask = new EjectTask(devicePath, mountPath, this);
    connect(m_ejectTask, &EjectTask::result, this,
            [this, mountPath, label](bool success, const QString &error) {
        if (success) {
            if (m_left && isPathInside(m_left->currentPath(), mountPath))
                m_left->navigateTo(QDir::homePath(), true);
            if (m_right && isPathInside(m_right->currentPath(), mountPath))
                m_right->navigateTo(QDir::homePath(), true);
            if (m_left)
                m_left->clearMountEjecting();
            if (m_right)
                m_right->clearMountEjecting();
            refreshSidebar();
            if (m_active)
                m_active->focusView();
            statusBar()->showMessage(QStringLiteral("已弹出：%1").arg(label), 4000);
        } else {
            if (m_left)
                m_left->clearMountEjecting();
            if (m_right)
                m_right->clearMountEjecting();
            UiDialogs::showWarning(this, QStringLiteral("弹出磁盘"),
                                 error.isEmpty() ? QStringLiteral("弹出失败") : error);
        }
    });
    EjectTask *task = m_ejectTask;
    connect(task, &QThread::finished, task, &QObject::deleteLater);
    connect(task, &QThread::finished, this, [this, task]() {
        if (m_ejectTask == task)
            m_ejectTask = nullptr;
    });
    task->start();
}

void MainWindow::loadBookmarks()
{
    QSettings settings;
    m_bookmarks = settings.value(QStringLiteral("bookmarks/paths")).toStringList();
    m_bookmarks.removeDuplicates();
    m_bookmarks.erase(std::remove_if(m_bookmarks.begin(), m_bookmarks.end(),
        [](const QString &path) { return path.trimmed().isEmpty(); }), m_bookmarks.end());
    m_bookmarkNames.clear();
    const QVariantMap names = settings.value(QStringLiteral("bookmarks/names")).toMap();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        const QString name = it.value().toString().trimmed();
        if (!name.isEmpty())
            m_bookmarkNames.insert(it.key(), name);
    }
}

QString MainWindow::bookmarkLabel(const QString &path) const
{
    const QString custom = m_bookmarkNames.value(path).trimmed();
    if (!custom.isEmpty())
        return custom;
    QString label = QFileInfo(path).fileName();
    if (path == QDir::homePath())
        label = QStringLiteral("主目录");
    else if (path == QStringLiteral("/"))
        label = QStringLiteral("系统盘");
    return label.isEmpty() ? path : label;
}

void MainWindow::saveBookmarks()
{
    QSettings settings;
    settings.setValue(QStringLiteral("bookmarks/paths"), m_bookmarks);
    QVariantMap names;
    for (auto it = m_bookmarkNames.constBegin(); it != m_bookmarkNames.constEnd(); ++it)
        names.insert(it.key(), it.value());
    settings.setValue(QStringLiteral("bookmarks/names"), names);
}

void MainWindow::toggleBookmark()
{
    if (!m_active)
        return;
    const QString path = QDir::cleanPath(m_active->currentPath());
    if (path.isEmpty())
        return;
    if (m_bookmarks.contains(path)) {
        m_bookmarks.removeAll(path);
        m_bookmarkNames.remove(path);
    } else {
        m_bookmarks.append(path);
    }
    saveBookmarks();
    refreshSidebar();
    updateBookmarkButton();
    statusBar()->showMessage(m_bookmarks.contains(path) ? QStringLiteral("已添加书签")
                                                        : QStringLiteral("已移除书签"), 2000);
}

void MainWindow::updateBookmarkButton()
{
    if (!m_bookmarkButton)
        return;
    bool bookmarked = false;
    if (m_active) {
        const QString path = QDir::cleanPath(m_active->currentPath());
        bookmarked = m_bookmarks.contains(path);
    }
    QSignalBlocker blocker(m_bookmarkButton);
    m_bookmarkButton->setChecked(bookmarked);
    m_bookmarkButton->setIcon(IconProvider::instance().uiIcon(
        QStringLiteral("star"),
        bookmarked ? Theme::instance().accent() : Theme::instance().textSecondary()));
    m_bookmarkButton->setToolTip(bookmarked ? QStringLiteral("移除当前文件夹书签")
                                            : QStringLiteral("添加当前文件夹书签"));
}

void MainWindow::toggleHiddenFiles()
{
    m_showHidden = !m_showHidden;
    QSettings settings;
    settings.setValue(QStringLiteral("ui/showHidden"), m_showHidden);
    if (m_left)
        m_left->setShowHidden(m_showHidden);
    if (m_right)
        m_right->setShowHidden(m_showHidden);
    updateHiddenButton();
    statusBar()->showMessage(m_showHidden ? QStringLiteral("已显示隐藏文件")
                                           : QStringLiteral("已隐藏隐藏文件"), 2000);
}

void MainWindow::updateHiddenButton()
{
    if (!m_hiddenButton)
        return;
    QSignalBlocker blocker(m_hiddenButton);
    m_hiddenButton->setChecked(m_showHidden);
    m_hiddenButton->setIcon(IconProvider::instance().uiIcon(
        QStringLiteral("eye"),
        m_showHidden ? Theme::instance().accent() : Theme::instance().textSecondary()));
    m_hiddenButton->setToolTip(m_showHidden ? QStringLiteral("隐藏隐藏文件 (Ctrl+H)")
                                             : QStringLiteral("显示隐藏文件 (Ctrl+H)"));
}

QString MainWindow::deviceSignature() const
{
    QStringList parts;
    const QString home = QDir::cleanPath(QDir::homePath());
    for (const QStorageInfo &volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid() || !volume.isReady())
            continue;
        const QString root = QDir::cleanPath(volume.rootPath());
        if (root.isEmpty() || root == QStringLiteral("/") || root == home)
            continue;
        if (!root.startsWith(QStringLiteral("/media/"))
            && !root.startsWith(QStringLiteral("/run/media/"))
            && !root.startsWith(QStringLiteral("/mnt/")))
            continue;
        const QString device = QString::fromLocal8Bit(volume.device());
        const QString label = volume.displayName().trimmed() + volume.name().trimmed();
        if (root.contains(QStringLiteral("_dde_data"), Qt::CaseInsensitive)
            || device.contains(QStringLiteral("_dde_data"), Qt::CaseInsensitive)
            || label.contains(QStringLiteral("_dde_data"), Qt::CaseInsensitive))
            continue;
        parts.append(root + QLatin1Char('|') + device);
    }
    parts.sort();
    return parts.join(QLatin1Char('\n'));
}

void MainWindow::refreshDevicesIfChanged()
{
    const QString current = deviceSignature();
    if (current == m_deviceSignature)
        return;
    m_deviceSignature = current;
    refreshSidebar();
}

void MainWindow::addSectionLabel(const QString &text)
{

    auto *label = new QLabel(text, m_sideContent);
    label->setObjectName(QStringLiteral("SideSection"));
    m_sideLayout->addWidget(label);
}

void MainWindow::addSideButton(const QString &iconName, const QString &label, const QString &path,
                               const QIcon &customIcon)
{
    if (path.isEmpty())
        return;
    auto *button = new QToolButton(m_sideContent);
    button->setObjectName(QStringLiteral("SideButton"));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIcon(customIcon.isNull()
                        ? IconProvider::instance().uiIcon(iconName, Theme::instance().textSecondary())
                        : customIcon);
    button->setIconSize(QSize(19, 19));
    button->setText(label);
    button->setToolTip(path);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setFixedHeight(36);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(true);
    connect(button, &QToolButton::clicked, this, [this, path]() {
        if (!m_active)
            return;
        if (!QFileInfo(path).isDir()) {
            if (path == trashFilesPath()) {
                QDir().mkpath(path);
                QDir().mkpath(QFileInfo(path).absolutePath() + QStringLiteral("/info"));
            } else {
                statusBar()->showMessage(QStringLiteral("路径不存在：%1").arg(path), 5000);
                return;
            }
        }
        if (m_active->navigateTo(path, true))
            m_active->focusView();
    });
    m_sideLayout->addWidget(button);
    SideEntry entry;
    entry.button = button;
    entry.path = QDir::cleanPath(path);
    entry.iconName = iconName;
    entry.customIcon = customIcon;
    m_sideEntries.append(entry);
}

void MainWindow::buildStatusBar()
{
    QStatusBar *bar = new QStatusBar(this);
    bar->setSizeGripEnabled(false);
    setStatusBar(bar);
}

void MainWindow::buildShortcuts()
{
    auto addShortcut = [this](const QKeySequence &sequence, const std::function<void()> &handler) {
        auto *shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, this, handler);
    };
    addShortcut(QKeySequence(Qt::Key_F6), [this]() {
        if (m_singlePane) {
            statusBar()->showMessage(QStringLiteral("当前为单栏模式，可从工具栏切换双栏"), 3000);
            return;
        }
        if (!m_active) return;
        FilePane *target = otherPane(m_active);
        setActivePane(target);
        target->focusView();
    });
    addShortcut(QKeySequence::Refresh, [this]() { refreshActive(); });
    addShortcut(QKeySequence::Find, [this]() { focusSearch(); });
    addShortcut(QKeySequence(QStringLiteral("Ctrl+L")), [this]() { openLocationDialog(); });
    addShortcut(QKeySequence(QStringLiteral("Ctrl+D")), [this]() { toggleBookmark(); });
    addShortcut(QKeySequence(QStringLiteral("Ctrl+H")), [this]() { toggleHiddenFiles(); });
    addShortcut(QKeySequence(QStringLiteral("Ctrl+N")), [this]() { openNewWindow(); });
    addShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), [this]() { navigateBack(); });
    addShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), [this]() { navigateForward(); });
    addShortcut(QKeySequence(Qt::ALT | Qt::Key_Up), [this]() { navigateUp(); });
}

void MainWindow::updateSidebarActive()
{
    if (!m_active) {
        for (const SideEntry &entry : m_sideEntries) {
            entry.button->setProperty("active", false);
            entry.button->style()->unpolish(entry.button);
            entry.button->style()->polish(entry.button);
            entry.button->setIcon(entry.customIcon.isNull()
                ? IconProvider::instance().uiIcon(entry.iconName, Theme::instance().textSecondary())
                : entry.customIcon);
        }
        return;
    }
    const QString current = QDir::cleanPath(m_active->currentPath());
    int bestIndex = -1;
    int bestLength = -1;
    bool bestIsDevice = false;
    for (int i = 0; i < m_sideEntries.size(); ++i) {
        const SideEntry &entry = m_sideEntries.at(i);
        if (!isPathInside(current, entry.path))
            continue;
        const bool isDevice = !entry.customIcon.isNull();
        if (entry.path.size() > bestLength
            || (entry.path.size() == bestLength && isDevice && !bestIsDevice)) {
            bestLength = entry.path.size();
            bestIndex = i;
            bestIsDevice = isDevice;
        }
    }
    for (int i = 0; i < m_sideEntries.size(); ++i) {
        SideEntry &entry = m_sideEntries[i];
        const bool active = i == bestIndex;
        entry.button->setProperty("active", active);
        entry.button->style()->unpolish(entry.button);
        entry.button->style()->polish(entry.button);
        entry.button->setIcon(entry.customIcon.isNull()
            ? IconProvider::instance().uiIcon(
                  entry.iconName, active ? Theme::instance().accent() : Theme::instance().textSecondary())
            : entry.customIcon);
    }
}

void MainWindow::setActivePane(FilePane *pane)
{
    if (!pane)
        return;
    m_active = pane;
    if (m_left)
        m_left->setActive(m_left == pane);
    if (m_right)
        m_right->setActive(m_right == pane);
    updateSidebarActive();
    updateActionStates();
}

FilePane *MainWindow::otherPane(FilePane *pane) const
{
    if (pane == m_left)
        return m_right;
    if (pane == m_right)
        return m_left;
    return nullptr;
}


void MainWindow::updateActionStates()
{
    if (!m_active)
        return;
    m_backButton->setEnabled(m_active->canGoBack());
    m_forwardButton->setEnabled(m_active->canGoForward());
    m_upButton->setEnabled(m_active->canGoUp());
    m_refreshButton->setEnabled(!m_active->currentPath().isEmpty());

    updateSidebarActive();
    updateWindowTitle();
    updateBookmarkButton();
}

void MainWindow::updateWindowTitle()
{
    if (!m_active || m_active->currentPath().isEmpty()) {
        setWindowTitle(QStringLiteral("文件管理器"));
        return;
    }
    setWindowTitle(QStringLiteral("%1 — 文件管理器")
                       .arg(compactPath(m_active->currentPath(), 3)));
}

void MainWindow::updateToolbarIcons()
{
    for (const auto &entry : m_toolbarIcons) {
        entry.first->setIcon(IconProvider::instance().uiIcon(entry.second, Theme::instance().textSecondary()));
        entry.first->setIconSize(QSize(20, 20));
    }
    updateBookmarkButton();
    updateHiddenButton();
    updateViewModeButton();
    if (m_transferBar) {
        if (QLabel *icon = m_transferBar->findChild<QLabel *>(QStringLiteral("TransferIcon")))
            icon->setPixmap(IconProvider::instance().uiIcon(
                QStringLiteral("transfer"), Theme::instance().accent()).pixmap(20, 20));
        if (m_pauseButton) {
            const auto snapshot = m_transfers ? m_transfers->current() : TransferManager::Snapshot();
            const bool paused = snapshot.status == TransferManager::TaskStatus::Paused;
            m_pauseButton->setIcon(IconProvider::instance().uiIcon(
                paused ? QStringLiteral("play") : QStringLiteral("pause"), Theme::instance().textSecondary()));
        }
        if (m_cancelButton)
            m_cancelButton->setIcon(IconProvider::instance().uiIcon(
                QStringLiteral("close"), Theme::instance().textSecondary()));
        updateTransferBar();
    }
}

void MainWindow::applyTheme()
{
    Theme::instance().applyToApplication();
    updateToolbarIcons();
    refreshSidebar();
    if (m_left)
        m_left->updateTheme();
    if (m_right)
        m_right->updateTheme();
    updateActionStates();
}

void MainWindow::setDarkMode(bool dark)
{
    if (Theme::instance().isDark() == dark) {
        applyTheme();
        return;
    }
    Theme::instance().setDark(dark);
    applyTheme();
}

void MainWindow::openPaths(const QString &leftPath, const QString &rightPath)
{
    const QString home = QDir::homePath();
    const QString left = (!leftPath.isEmpty() && QFileInfo(leftPath).isDir()) ? leftPath : home;
    const QString right = (!rightPath.isEmpty() && QFileInfo(rightPath).isDir()) ? rightPath : home;
    m_left->setShowHidden(m_showHidden);
    m_right->setShowHidden(m_showHidden);
    updateHiddenButton();
    m_left->navigateTo(left, true);
    m_right->navigateTo(right, true);
    setActivePane(m_left);
    updateActionStates();
    m_left->focusView();
}

FilePane *MainWindow::leftPane() const { return m_left; }
FilePane *MainWindow::rightPane() const { return m_right; }
FilePane *MainWindow::activePane() const { return m_active; }

void MainWindow::updateViewModeButton()
{
    if (!m_viewModeButton)
        return;
    QSignalBlocker blocker(m_viewModeButton);
    m_viewModeButton->setChecked(!m_singlePane);
    m_viewModeButton->setIcon(IconProvider::instance().uiIcon(
        m_singlePane ? QStringLiteral("list") : QStringLiteral("columns"),
        Theme::instance().textSecondary()));
    m_viewModeButton->setToolTip(m_singlePane ? QStringLiteral("切换为双栏")
                                             : QStringLiteral("切换为单栏"));
}

MainWindow *MainWindow::openNewWindow(const QString &startLeft, const QString &startRight,
                                      const QString &revealItem)
{
    QString left = startLeft;
    if (left.isEmpty() && !revealItem.isEmpty()
        && QFileInfo(revealItem).isFile())
        left = QFileInfo(revealItem).absolutePath();
    auto *window = new MainWindow(left, startRight);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    if (window->windowHandle())
        window->windowHandle()->setIcon(IconProvider::instance().appIcon());
    window->raise();
    window->activateWindow();
    if (!revealItem.isEmpty()) {
        const QString target = revealItem;
        QTimer::singleShot(0, window, [window, target]() { window->revealPath(target); });
    }
    return window;
}

void MainWindow::revealPath(const QString &path)
{
    showNormal();
    if (isMinimized())
        setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
    QApplication::setActiveWindow(this);
    if (!m_active)
        setActivePane(m_left);
    if (!m_active)
        return;
    if (path.isEmpty()) {
        m_active->focusView();
        return;
    }
    const QFileInfo info(path);
    if (!info.exists()) {
        statusBar()->showMessage(QStringLiteral("位置不存在：%1").arg(path), 5000);
        return;
    }
    m_active->revealPath(info.absoluteFilePath());
}

void MainWindow::setSinglePane(bool single, bool persist)
{

    if (!m_left || !m_right)
        return;
    m_singlePane = single;
    m_right->setVisible(!single);
    if (m_left)
        m_left->setHeaderHighlight(!single);
    if (m_right)
        m_right->setHeaderHighlight(!single);
    if (single && m_active == m_right)
        m_active = m_left;
    if (m_active)
        setActivePane(m_active);
    updateViewModeButton();
    updateActionStates();
    if (single)
        m_left->focusView();
    if (persist) {
        QSettings settings;
        settings.setValue(QStringLiteral("ui/singlePane"), single);
    }
}

void MainWindow::navigateBack()
{
    if (m_active) m_active->goBack();
}

void MainWindow::navigateForward()
{
    if (m_active) m_active->goForward();
}

void MainWindow::navigateUp()
{
    if (m_active) m_active->goUp();
}

void MainWindow::refreshActive()
{
    if (m_active) m_active->refresh();
}

void MainWindow::focusSearch()
{
    if (m_active)
        m_active->beginSearch();
}
void MainWindow::openLocationDialog()
{
    if (!m_active)
        return;
    bool ok = false;
    const QString path = UiDialogs::getText(
        this, QStringLiteral("前往文件夹"), QStringLiteral("输入文件夹路径："),
        m_active->currentPath(), &ok);
    if (!ok || path.trimmed().isEmpty())
        return;
    if (!m_active->navigateTo(path.trimmed(), true))
        statusBar()->showMessage(QStringLiteral("无法打开该路径"), 4000);
}

void MainWindow::createFolder()
{
    if (!m_active)
        return;
    bool ok = false;
    const QString name = UiDialogs::getText(this, QStringLiteral("新建文件夹"),
                                            QStringLiteral("文件夹名称："),
                                            QStringLiteral("新建文件夹"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    QString error;
    if (!createNewFolder(m_active->currentPath(), name.trimmed(), &error)) {
        UiDialogs::showWarning(this, QStringLiteral("新建文件夹"), error);
        return;
    }
    m_active->refresh();
    statusBar()->showMessage(QStringLiteral("已创建文件夹：%1").arg(name.trimmed()), 3500);
}

void MainWindow::createTextFile()
{
    if (!m_active)
        return;
    bool ok = false;
    const QString name = UiDialogs::getText(this, QStringLiteral("新建文本文件"),
                                            QStringLiteral("文件名称："),
                                            QStringLiteral("新建文本.txt"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    QString error;
    if (!createNewFile(m_active->currentPath(), name.trimmed(), &error)) {
        UiDialogs::showWarning(this, QStringLiteral("新建文本文件"), error);
        return;
    }
    m_active->refresh();
    statusBar()->showMessage(QStringLiteral("已创建文件：%1").arg(name.trimmed()), 3500);
}

void MainWindow::renameSelected()
{
    if (!m_active)
        return;
    const QVector<FileEntry> entries = m_active->selectedEntries();
    if (entries.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("请先选择要重命名的项目"), 3000);
        return;
    }
    if (entries.size() != 1) {
        statusBar()->showMessage(QStringLiteral("一次只能重命名一个项目"), 3000);
        return;
    }
    const FileEntry entry = entries.first();
    bool ok = false;
    const QString newName = UiDialogs::getText(this, QStringLiteral("重命名"),
                                               QStringLiteral("新名称："),
                                               entry.name, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName == entry.name)
        return;
    if (newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\'))) {
        UiDialogs::showWarning(this, QStringLiteral("重命名"), QStringLiteral("名称不能包含斜杠。"));
        return;
    }
    const QString parent = QFileInfo(entry.path).absolutePath();
    const QString target = QDir(parent).filePath(newName.trimmed());
    QString error;
    if (!renamePath(entry.path, target, &error)) {
        UiDialogs::showWarning(this, QStringLiteral("重命名"), error);
        return;
    }
    m_active->refresh();
    statusBar()->showMessage(QStringLiteral("已重命名为：%1").arg(newName.trimmed()), 3500);
}

void MainWindow::copySelectionToClipboard(bool cut)
{
    if (!m_active)
        return;
    const QStringList paths = m_active->selectedPaths();
    if (paths.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("请先选择要%1的项目").arg(cut ? QStringLiteral("剪切") : QStringLiteral("复制")), 3000);
        return;
    }
    m_clipboardPaths = paths;
    m_clipboardCut = cut;

    auto *mime = new QMimeData();
    QList<QUrl> urls;
    for (const QString &path : paths)
        urls.append(QUrl::fromLocalFile(path));
    mime->setUrls(urls);
    // 同时提供 text/plain，兼容只读取纯文本的编辑器和其他非文件管理器应用。
    // 文件管理器/桌面环境优先读取 text/uri-list 与 GNOME 剪贴板格式。
    QStringList plainPaths;
    for (const QString &path : paths)
        plainPaths.append(path);
    mime->setText(plainPaths.join(QLatin1Char('\n')));
    QByteArray special = cut ? QByteArray("cut\n") : QByteArray("copy\n");
    for (const QUrl &url : urls)
        special += url.toString(QUrl::FullyEncoded).toUtf8() + '\n';
    mime->setData(QStringLiteral("x-special/gnome-copied-files"), special);
    QApplication::clipboard()->setMimeData(mime, QClipboard::Clipboard);
    if (m_active)
        m_active->flashClipboardFeedback(cut);

    statusBar()->showMessage(QStringLiteral("已%1 %2 个项目").arg(
        cut ? QStringLiteral("剪切") : QStringLiteral("复制")).arg(paths.size()), 3000);
}

bool MainWindow::clipboardHasFiles() const
{
    if (!m_clipboardPaths.isEmpty())
        return true;
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasUrls())
        return false;
    const QList<QUrl> urls = mime->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile() && QFileInfo::exists(url.toLocalFile()))
            return true;
    }
    return false;
}

void MainWindow::pasteClipboard()
{
    if (!m_active || !m_transfers)
        return;
    QStringList paths = m_clipboardPaths;
    bool cut = m_clipboardCut;
    if (paths.isEmpty()) {
        const QMimeData *mime = QApplication::clipboard()->mimeData();
        if (mime && mime->hasUrls()) {
            const QList<QUrl> urls = mime->urls();
            for (const QUrl &url : urls) {
                if (url.isLocalFile())
                    paths.append(url.toLocalFile());
            }
        }
        cut = false;
    }
    paths.removeDuplicates();
    if (paths.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("剪贴板中没有可粘贴的文件"), 3000);
        return;
    }
    const QString destination = m_active->currentPath();
    if (destination.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("当前没有可用的目标文件夹"), 3000);
        return;
    }
    if (cut) {
        bool allSameParent = true;
        for (const QString &path : paths) {
            if (QFileInfo(path).absolutePath() != QDir::cleanPath(destination)) {
                allSameParent = false;
                break;
            }
        }
        if (allSameParent) {
            m_clipboardPaths.clear();
            m_clipboardCut = false;
            statusBar()->showMessage(QStringLiteral("源目录和目标目录相同"), 3000);
            return;
        }
    }

    QString error;
    bool canceled = false;
    if (!queueTransfer(cut ? TransferOperation::Move : TransferOperation::Copy,
                       paths, destination, &error, &canceled)) {
        if (!canceled)
            UiDialogs::showWarning(this, QStringLiteral("粘贴"), error);
        return;
    }
    if (cut) {
        m_clipboardPaths.clear();
        m_clipboardCut = false;
        QApplication::clipboard()->clear();
    }
    statusBar()->showMessage(cut ? QStringLiteral("已添加移动任务") : QStringLiteral("已添加复制任务"), 3000);
    updateTransferBar();
    updateTransferTaskButton();
}

void MainWindow::trashSelected()
{
    if (!m_active)
        return;
    const QVector<FileEntry> entries = m_active->selectedEntries();
    if (entries.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("请先选择要删除的项目"), 3000);
        return;
    }
    QStringList paths;
    for (const FileEntry &entry : entries)
        paths.append(entry.path);

    if (onRemovableStorage(paths)) {
        const QString question = entries.size() == 1
            ? QStringLiteral("将从可移动磁盘彻底删除「%1」？此操作不会进入回收站，且无法恢复。")
                  .arg(entries.first().name)
            : QStringLiteral("将从可移动磁盘彻底删除选中的 %1 个项目？此操作不会进入回收站，且无法恢复。")
                  .arg(entries.size());
        if (!UiDialogs::confirm(this, QStringLiteral("彻底删除"), question,
                                QStringLiteral("彻底删除"), QStringLiteral("取消"), false))
            return;
        QString error;
        if (!deletePathsPermanently(paths, &error)) {
            UiDialogs::showWarning(this, QStringLiteral("彻底删除"), error);
            return;
        }
        m_left->refresh();
        m_right->refresh();
        statusBar()->showMessage(QStringLiteral("已从可移动磁盘彻底删除：%1")
                                     .arg(namesPreview(paths)), 4000);
        return;
    }

    const QString question = entries.size() == 1
        ? QStringLiteral("将「%1」移到回收站？").arg(entries.first().name)
        : QStringLiteral("将选中的 %1 个项目移到回收站？").arg(entries.size());
    if (!UiDialogs::confirm(this, QStringLiteral("移到回收站"), question,
                            QStringLiteral("移到回收站"), QStringLiteral("取消"), false))
        return;
    QString error;
    if (!sendToTrash(paths, &error)) {
        UiDialogs::showWarning(this, QStringLiteral("移到回收站"), error);
        return;
    }
    m_left->refresh();
    m_right->refresh();
    statusBar()->showMessage(QStringLiteral("已移到回收站：%1").arg(namesPreview(paths)), 4000);
}

void MainWindow::deleteSelectedPermanently()
{
    if (!m_active)
        return;
    const QVector<FileEntry> entries = m_active->selectedEntries();
    if (entries.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("请先选择要彻底删除的项目"), 3000);
        return;
    }
    QStringList paths;
    for (const FileEntry &entry : entries)
        paths.append(entry.path);
    const QString question = entries.size() == 1
        ? QStringLiteral("彻底删除「%1」？此操作不会进入回收站，且无法恢复。")
              .arg(entries.first().name)
        : QStringLiteral("彻底删除选中的 %1 个项目？此操作不会进入回收站，且无法恢复。")
              .arg(entries.size());
    if (!UiDialogs::confirm(this, QStringLiteral("彻底删除"), question,
                            QStringLiteral("彻底删除"), QStringLiteral("取消"), false))
        return;
    QString error;
    if (!deletePathsPermanently(paths, &error)) {
        UiDialogs::showWarning(this, QStringLiteral("彻底删除"), error);
        return;
    }
    m_left->refresh();
    m_right->refresh();
    statusBar()->showMessage(QStringLiteral("已彻底删除：%1").arg(namesPreview(paths)), 4000);
}

void MainWindow::showProperties(const QStringList &paths)
{
    QStringList targets = paths;
    if (targets.isEmpty() && m_active)
        targets = m_active->selectedPaths();
    if (targets.isEmpty() && m_active)
        targets << m_active->currentPath();
    targets.removeDuplicates();
    if (targets.isEmpty())
        return;
    PropertiesDialog dialog(targets, this);
    connect(&dialog, &PropertiesDialog::changed, this, [this]() {
        if (m_left)
            m_left->refresh();
        if (m_right)
            m_right->refresh();
    });
    dialog.exec();
}

void MainWindow::buildTransferBar(QVBoxLayout *rootLayout)
{
    m_transferBar = new QFrame(this);
    m_transferBar->setObjectName(QStringLiteral("TransferBar"));
    m_transferBar->setFixedHeight(58);
    auto *layout = new QHBoxLayout(m_transferBar);
    layout->setContentsMargins(18, 7, 14, 7);
    layout->setSpacing(12);

    auto *icon = new QLabel(m_transferBar);
    icon->setFixedSize(34, 34);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(IconProvider::instance().uiIcon(
        QStringLiteral("transfer"), Theme::instance().accent()).pixmap(20, 20));
    icon->setObjectName(QStringLiteral("TransferIcon"));
    layout->addWidget(icon);

    auto *texts = new QVBoxLayout();
    texts->setSpacing(1);
    m_transferTitle = new QLabel(QStringLiteral("准备传输"), m_transferBar);
    m_transferTitle->setObjectName(QStringLiteral("TransferTitle"));
    m_transferSubtitle = new QLabel(QString(), m_transferBar);
    m_transferSubtitle->setObjectName(QStringLiteral("TransferSubtitle"));
    m_transferSubtitle->setMaximumWidth(560);
    m_transferSubtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    texts->addWidget(m_transferTitle);
    texts->addWidget(m_transferSubtitle);
    layout->addLayout(texts, 1);

    m_transferProgress = new QProgressBar(m_transferBar);
    m_transferProgress->setObjectName(QStringLiteral("TransferProgress"));
    m_transferProgress->setFixedWidth(240);
    m_transferProgress->setTextVisible(false);
    m_transferProgress->setRange(0, 100);
    layout->addWidget(m_transferProgress);

    m_transferPercent = new QLabel(QStringLiteral("0%"), m_transferBar);
    m_transferPercent->setObjectName(QStringLiteral("TransferPercent"));
    m_transferPercent->setFixedWidth(40);
    layout->addWidget(m_transferPercent);

    m_transferDetails = new QLabel(m_transferBar);
    m_transferDetails->setObjectName(QStringLiteral("TransferDetails"));
    m_transferDetails->setFixedWidth(145);
    m_transferDetails->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(m_transferDetails);

    m_pauseButton = new QToolButton(m_transferBar);
    m_pauseButton->setObjectName(QStringLiteral("TransferControl"));
    m_pauseButton->setIcon(IconProvider::instance().uiIcon(QStringLiteral("pause"), Theme::instance().textSecondary()));
    m_pauseButton->setIconSize(QSize(17, 17));
    m_pauseButton->setFixedSize(30, 30);
    m_pauseButton->setCursor(Qt::PointingHandCursor);
    m_pauseButton->setFocusPolicy(Qt::NoFocus);
    m_pauseButton->setToolTip(QStringLiteral("暂停"));
    layout->addWidget(m_pauseButton);

    m_cancelButton = new QToolButton(m_transferBar);
    m_cancelButton->setObjectName(QStringLiteral("TransferControl"));
    m_cancelButton->setIcon(IconProvider::instance().uiIcon(QStringLiteral("close"), Theme::instance().textSecondary()));
    m_cancelButton->setIconSize(QSize(17, 17));
    m_cancelButton->setFixedSize(30, 30);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setFocusPolicy(Qt::NoFocus);
    m_cancelButton->setToolTip(QStringLiteral("取消"));
    layout->addWidget(m_cancelButton);

    connect(m_pauseButton, &QToolButton::clicked, this, [this]() {
        if (!m_transfers)
            return;
        const TransferManager::Snapshot snapshot = m_transfers->current();
        if (snapshot.id < 0)
            return;
        if (snapshot.status == TransferManager::TaskStatus::Paused)
            m_transfers->resumeCurrent();
        else
            m_transfers->pauseCurrent();
    });
    connect(m_cancelButton, &QToolButton::clicked, this, [this]() {
        if (m_transfers)
            m_transfers->cancelCurrent();
    });

    m_transferBar->hide();
    rootLayout->addWidget(m_transferBar);
}

void MainWindow::showPreviewFile(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path) || !m_active)
        return;
    QStringList paths;
    const QVector<FileEntry> entries = m_active->directoryModel()->visibleEntries();
    for (const FileEntry &entry : entries)
        paths.append(entry.path);
    int index = paths.indexOf(path);
    if (index < 0) {
        paths = QStringList() << path;
        index = 0;
    }
    Q_UNUSED(index)
    // Non-modal quick-look style preview: an already open preview is re-targeted
    // in place instead of blocking the main window with a second modal dialog.
    if (m_previewDialog && m_previewDialog->isVisible()) {
        m_previewDialog->setPaths(paths, path);
        m_previewDialog->raise();
        m_previewDialog->activateWindow();
        return;
    }
    m_previewDialog = new PreviewDialog(paths, path, this);
    m_previewDialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_previewDialog, &QObject::destroyed, this, [this]() {
        m_previewDialog = nullptr;
    });
    m_previewDialog->show();
    m_previewDialog->raise();
    m_previewDialog->activateWindow();
}

void MainWindow::extractArchive(const QString &archivePath)
{
    const QString manager = archiveManagerPath();
    if (manager.isEmpty()) {
        UiDialogs::showWarning(this, QStringLiteral("归档管理器"),
                               QStringLiteral("系统未安装归档管理器（deepin-compressor）。"));
        return;
    }
    const QFileInfo info(archivePath);
    if (!info.exists()) {
        UiDialogs::showWarning(this, QStringLiteral("解压"),
                               QStringLiteral("压缩包不存在。"));
        return;
    }
    QStringList arguments;
    arguments << info.absoluteFilePath() << QStringLiteral("extract");
    if (!QProcess::startDetached(manager, arguments, info.absolutePath())) {
        UiDialogs::showWarning(this, QStringLiteral("归档管理器"),
                               QStringLiteral("无法启动归档管理器。"));
    }
}

void MainWindow::compressSelection()
{
    if (!m_active)
        return;
    const QString manager = archiveManagerPath();
    if (manager.isEmpty()) {
        UiDialogs::showWarning(this, QStringLiteral("归档管理器"),
                               QStringLiteral("系统未安装归档管理器（deepin-compressor）。"));
        return;
    }
    const QStringList paths = m_active->selectedPaths();
    if (paths.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("请先选择要压缩的项目"), 3000);
        return;
    }
    QStringList arguments;
    for (const QString &path : paths)
        arguments << QFileInfo(path).absoluteFilePath();
    arguments << QStringLiteral("compress");
    const QString workingDirectory = QFileInfo(paths.first()).absolutePath();
    if (!QProcess::startDetached(manager, arguments, workingDirectory)) {
        UiDialogs::showWarning(this, QStringLiteral("归档管理器"),
                               QStringLiteral("无法启动归档管理器。"));
    }
}

void MainWindow::openTerminalAt(const QString &path)
{

    const QFileInfo info(path);
    QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (directory.isEmpty() || !QFileInfo(directory).isDir()) {
        UiDialogs::showWarning(this, QStringLiteral("在终端打开"),
                               QStringLiteral("当前文件夹不可用。"));
        return;
    }
    directory = QDir::cleanPath(directory);

    struct Candidate {
        const char *program;
        QStringList arguments;
    };
    const QVector<Candidate> candidates = {
        { "deepin-terminal", { QStringLiteral("-w"), directory } },
        { "x-terminal-emulator", {} },
        { "gnome-terminal", { QStringLiteral("--working-directory"), directory } },
        { "qterminal", { QStringLiteral("--workdir"), directory } },
        { "konsole", { QStringLiteral("--workdir"), directory } },
        { "xfce4-terminal", { QStringLiteral("--working-directory=") + directory } },
        { "mate-terminal", { QStringLiteral("--working-directory=") + directory } },
        { "xterm", { QStringLiteral("-cd"), directory } }
    };
    for (const Candidate &candidate : candidates) {
        const QString program = QStandardPaths::findExecutable(
            QString::fromLatin1(candidate.program));
        if (program.isEmpty())
            continue;
        qint64 pid = 0;
        if (QProcess::startDetached(program, candidate.arguments, directory, &pid)) {
            statusBar()->showMessage(QStringLiteral("已在终端打开：%1").arg(directory), 3000);
            return;
        }
    }
    UiDialogs::showWarning(this, QStringLiteral("在终端打开"),
                           QStringLiteral("未找到可用的终端程序。"));
}

void MainWindow::openWithFile(const QString &path)
{


    chooseApplicationAndOpen(this, path);
}

void MainWindow::openTrashDialog()
{
    TrashDialog dialog(this);
    connect(&dialog, &TrashDialog::trashChanged, this, [this]() {
        if (m_left)
            m_left->refresh();
        if (m_right)
            m_right->refresh();
    });
    dialog.exec();
}

void MainWindow::openTransferCenter()
{

    if (!m_transfers)
        return;
    if (!m_transferDialog)
        m_transferDialog = new TransferDialog(m_transfers, this);
    m_transferDialog->refresh();
    m_transferDialog->show();
    m_transferDialog->raise();
    m_transferDialog->activateWindow();
}

void MainWindow::updateTransferBar()
{
    if (!m_transferBar || !m_transfers)
        return;
    const TransferManager::Snapshot task = m_transfers->current();
    if (task.id < 0) {
        m_transferBar->hide();
        return;
    }
    m_transferBar->show();
    const QString operation = task.operation == TransferOperation::Copy
        ? QStringLiteral("复制") : QStringLiteral("移动");
    QString title;
    if (task.status == TransferManager::TaskStatus::Paused)
        title = QStringLiteral("已暂停 · 正在%1 %2 个项目").arg(operation).arg(task.sources.size());
    else if (task.status == TransferManager::TaskStatus::Preparing || task.status == TransferManager::TaskStatus::Queued)
        title = QStringLiteral("正在准备%1 %2 个项目").arg(operation).arg(task.sources.size());
    else if (task.status == TransferManager::TaskStatus::Cancelling)
        title = QStringLiteral("正在取消…");
    else
        title = QStringLiteral("正在%1 %2 个项目").arg(operation).arg(task.sources.size());
    m_transferTitle->setText(title);

    QString subtitle = QStringLiteral("%1  →  %2")
        .arg(namesPreview(task.sources, 2), compactPath(task.destination, 2));
    if (!task.currentName.isEmpty() && task.status == TransferManager::TaskStatus::Running)
        subtitle = QStringLiteral("%1  ·  %2").arg(task.currentName, subtitle);
    QFontMetrics metrics(m_transferSubtitle->font());
    m_transferSubtitle->setText(metrics.elidedText(subtitle, Qt::ElideMiddle, 540));

    qint64 done = task.doneBytes;
    qint64 total = task.totalBytes;
    if (total <= 0) {
        done = task.doneFiles;
        total = task.totalFiles;
    }
    if (total > 0) {
        const int percent = static_cast<int>(qBound<qint64>(0, done * 100 / total, 100));
        m_transferProgress->setRange(0, 100);
        m_transferProgress->setValue(percent);
        m_transferPercent->setText(QStringLiteral("%1%").arg(percent));
    } else {
        m_transferProgress->setRange(0, 0);
        m_transferPercent->setText(QStringLiteral("—"));
    }
    QString detailText;
    if (task.bytesPerSecond > 0)
        detailText = formatSpeed(task.bytesPerSecond);
    if (task.remainingSeconds >= 0) {
        if (!detailText.isEmpty())
            detailText += QStringLiteral(" · ");
        detailText += formatDuration(task.remainingSeconds);
    }
    if (m_transferDetails)
        m_transferDetails->setText(detailText);

    const bool paused = task.status == TransferManager::TaskStatus::Paused;
    m_pauseButton->setIcon(IconProvider::instance().uiIcon(
        paused ? QStringLiteral("play") : QStringLiteral("pause"), Theme::instance().textSecondary()));
    m_pauseButton->setToolTip(paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    m_pauseButton->setEnabled(task.status == TransferManager::TaskStatus::Running
                              || task.status == TransferManager::TaskStatus::Preparing
                              || task.status == TransferManager::TaskStatus::Paused);
    m_cancelButton->setEnabled(task.status != TransferManager::TaskStatus::Cancelling);
    updateTransferTaskButton();
}

void MainWindow::updateTransferTaskButton()
{
    if (!m_tasksButton || !m_transfers)
        return;
    const int active = m_transfers->hasActive() ? 1 : 0;
    const int queued = m_transfers->queuedCount();
    if (active + queued > 0)
        m_tasksButton->setToolTip(QStringLiteral("传输中心 · %1 个任务").arg(active + queued));
    else
        m_tasksButton->setToolTip(QStringLiteral("传输中心"));
}

void MainWindow::startTransfer(bool move)
{
    if (!m_active || !m_transfers)
        return;
    if (m_singlePane) {
        statusBar()->showMessage(QStringLiteral("单栏模式下请用 Ctrl+C / Ctrl+V 复制，或先切换双栏"), 3500);
        return;
    }
    if (!m_active->hasSelection()) {
        statusBar()->showMessage(QStringLiteral("请先选择要传输的项目"), 3000);
        return;
    }
    FilePane *target = otherPane(m_active);
    if (!target || target->currentPath().isEmpty()) {
        UiDialogs::showWarning(this, QStringLiteral("传输"), QStringLiteral("目标窗格没有可用的文件夹。"));
        return;
    }
    const QStringList sources = m_active->selectedPaths();
    if (sources.isEmpty())
        return;
    QString error;
    bool canceled = false;
    if (!queueTransfer(move ? TransferOperation::Move : TransferOperation::Copy,
                       sources, target->currentPath(), &error, &canceled)) {
        if (!canceled)
            UiDialogs::showWarning(this, QStringLiteral("传输"), error);
        return;
    }
    if (m_transfers->hasActive())
        statusBar()->showMessage(move ? QStringLiteral("移动任务已加入队列") : QStringLiteral("复制任务已加入队列"), 3500);
    updateTransferBar();
    updateTransferTaskButton();
}

void MainWindow::selectAllActive()
{
    if (!m_active)
        return;
    m_active->tableView()->selectAll();
}

bool MainWindow::prepareConflictActions(const QStringList &sources, const QString &destination,
                                         ConflictActions *actions)
{
    if (!actions)
        return false;
    QStringList conflicts;
    for (const QString &source : sources) {
        QFileInfo sourceInfo(source);
        const QString target = QDir(destination).filePath(sourceInfo.fileName());
        const QFileInfo targetInfo(target);
        if (targetInfo.exists() || targetInfo.isSymLink())
            conflicts.append(source);
    }
    if (conflicts.isEmpty())
        return true;

    for (int index = 0; index < conflicts.size(); ++index) {
        const QString source = conflicts.at(index);
        if (actions->contains(source))
            continue;
        const QFileInfo sourceInfo(source);
        const QString target = QDir(destination).filePath(sourceInfo.fileName());
        const QFileInfo targetInfo(target);
        UiDialogs::ConflictChoice choice;
        const bool samePath = QFileInfo(source).absoluteFilePath()
            == QFileInfo(target).absoluteFilePath();
        if (!UiDialogs::resolveConflict(this, sourceInfo.fileName(), target,
                                        sourceInfo.isDir() && !sourceInfo.isSymLink(),
                                        targetInfo.isDir() && !targetInfo.isSymLink(),
                                        samePath, &choice)) {
            return false;
        }
        actions->insert(source, choice.action);
        if (choice.applyToAll) {
            for (int rest = index + 1; rest < conflicts.size(); ++rest)
                actions->insert(conflicts.at(rest), choice.action);
            break;
        }
    }
    return true;
}

bool MainWindow::queueTransfer(TransferOperation operation, const QStringList &sources,
                               const QString &destination, QString *error, bool *canceled)
{
    if (canceled)
        *canceled = false;
    if (!m_transfers) {
        if (error) *error = QStringLiteral("传输管理器不可用");
        return false;
    }
    ConflictActions actions;
    if (!prepareConflictActions(sources, destination, &actions)) {
        if (canceled)
            *canceled = true;
        if (error) *error = QStringLiteral("已取消");
        return false;
    }
    const int id = m_transfers->enqueue(operation, sources, destination, error, actions);
    return id >= 0;
}

void MainWindow::handleDropTransfer(const QStringList &sources, const QString &destination, bool move)
{

    if (!m_transfers || sources.isEmpty())
        return;
    if (move) {
        bool sameParent = true;
        for (const QString &source : sources) {
            if (QFileInfo(source).absolutePath() != QDir::cleanPath(destination)) {
                sameParent = false;
                break;
            }
        }
        if (sameParent) {
            statusBar()->showMessage(QStringLiteral("源文件夹和目标文件夹相同"), 2500);
            return;
        }
    }
    QString error;
    bool canceled = false;
    if (!queueTransfer(move ? TransferOperation::Move : TransferOperation::Copy,
                       sources, destination, &error, &canceled)) {
        if (!canceled)
            UiDialogs::showWarning(this, QStringLiteral("拖放传输"), error);
        return;
    }
    statusBar()->showMessage(move ? QStringLiteral("已添加移动任务") : QStringLiteral("已添加复制任务"), 2500);
    updateTransferBar();
    updateTransferTaskButton();
}

void MainWindow::showContextMenu(FilePane *pane, const QPoint &globalPos)
{

    if (!pane)
        return;
    setActivePane(pane);
    const QVector<FileEntry> entries = pane->selectedEntries();
    QMenu menu(this);
    const QColor iconColor = Theme::instance().textSecondary();

    if (!entries.isEmpty()) {
        QAction *copyAction = menu.addAction(
            IconProvider::instance().uiIcon(QStringLiteral("copy"), iconColor),
            QStringLiteral("复制\tCtrl+C"));
        QAction *cutAction = menu.addAction(
            IconProvider::instance().uiIcon(QStringLiteral("move"), iconColor),
            QStringLiteral("剪切\tCtrl+X"));
        connect(copyAction, &QAction::triggered, this, [this]() { copySelectionToClipboard(false); });
        connect(cutAction, &QAction::triggered, this, [this]() { copySelectionToClipboard(true); });
    }
    if (clipboardHasFiles()) {
        QAction *pasteAction = menu.addAction(QStringLiteral("粘贴\tCtrl+V"));
        connect(pasteAction, &QAction::triggered, this, &MainWindow::pasteClipboard);
    }
    if (!entries.isEmpty() || clipboardHasFiles())
        menu.addSeparator();

    if (entries.size() == 1) {
        QAction *openAction = menu.addAction(
            IconProvider::instance().uiIcon(QStringLiteral("external"), iconColor),
            QStringLiteral("打开"));
        connect(openAction, &QAction::triggered, this, [pane]() { pane->openSelected(); });
        QAction *previewAction = menu.addAction(
            IconProvider::instance().uiIcon(QStringLiteral("eye"), iconColor),
            QStringLiteral("预览	空格"));
        connect(previewAction, &QAction::triggered, this, [this, path = entries.first().path]() {
            showPreviewFile(path);
        });
        if (!entries.first().isDir) {
            QAction *openWithAction = menu.addAction(
                IconProvider::instance().uiIcon(QStringLiteral("external"), iconColor),
                QStringLiteral("打开方式…"));
            connect(openWithAction, &QAction::triggered, this, [this, path = entries.first().path]() {
                openWithFile(path);
            });
        }
        menu.addSeparator();
    } else if (entries.isEmpty()) {
        QAction *newAction = menu.addAction(
            IconProvider::instance().uiIcon(QStringLiteral("folder-plus"), iconColor),
            QStringLiteral("新建文件夹"));
        QAction *refreshAction = menu.addAction(
            IconProvider::instance().uiIcon(QStringLiteral("refresh"), iconColor),
            QStringLiteral("刷新"));
        QAction *terminalAction = menu.addAction(
            IconProvider::instance().uiIcon(QStringLiteral("terminal"), iconColor),
            QStringLiteral("在终端打开"));
        QAction *selectAllAction = menu.addAction(QStringLiteral("全选"));
        QAction *hiddenAction = menu.addAction(QStringLiteral("显示隐藏文件"));
        hiddenAction->setCheckable(true);
        hiddenAction->setChecked(m_showHidden);
        connect(hiddenAction, &QAction::triggered, this, &MainWindow::toggleHiddenFiles);
        connect(newAction, &QAction::triggered, this, &MainWindow::createFolder);
        connect(refreshAction, &QAction::triggered, pane, &FilePane::refresh);
        connect(terminalAction, &QAction::triggered, this, [this, pane]() {
            openTerminalAt(pane->currentPath());
        });
        connect(selectAllAction, &QAction::triggered, this, &MainWindow::selectAllActive);
        menu.addSeparator();
        QAction *propertiesAction = menu.addAction(
            IconProvider::instance().uiIcon(QStringLiteral("info"), iconColor),
            QStringLiteral("当前文件夹属性"));
        connect(propertiesAction, &QAction::triggered, this, [this, pane]() {
            showProperties(QStringList() << pane->currentPath());
        });
        menu.exec(globalPos);
        return;
    }

    if (!entries.isEmpty()) {
        if (!m_singlePane) {
            FilePane *target = otherPane(pane);
            const QString targetName = target == m_right ? QStringLiteral("右栏") : QStringLiteral("左栏");
            QAction *copyToPaneAction = menu.addAction(
                IconProvider::instance().uiIcon(QStringLiteral("copy"), iconColor),
                QStringLiteral("复制到%1").arg(targetName));
            QAction *moveToPaneAction = menu.addAction(
                IconProvider::instance().uiIcon(QStringLiteral("move"), iconColor),
                QStringLiteral("移动到%1").arg(targetName));
            connect(copyToPaneAction, &QAction::triggered, this, [this]() { startTransfer(false); });
            connect(moveToPaneAction, &QAction::triggered, this, [this]() { startTransfer(true); });
            menu.addSeparator();
        }
        if (entries.size() == 1) {
            QAction *renameAction = menu.addAction(
                IconProvider::instance().uiIcon(QStringLiteral("edit"), iconColor),
                QStringLiteral("重命名"));
            connect(renameAction, &QAction::triggered, this, &MainWindow::renameSelected);
        }
        QStringList paths;
        for (const FileEntry &entry : entries)
            paths.append(entry.path);
        if (entries.size() == 1 && isArchivePath(entries.first().path)) {
            const QString archivePath = entries.first().path;
            QAction *extractAction = menu.addAction(
                IconProvider::instance().uiIcon(QStringLiteral("archive"), iconColor),
                QStringLiteral("解压"));
            connect(extractAction, &QAction::triggered, this, [this, archivePath]() {
                extractArchive(archivePath);
            });
        }
        QAction *compressAction = menu.addAction(
            IconProvider::instance().uiIcon(QStringLiteral("archive"), iconColor),
            QStringLiteral("压缩"));
        connect(compressAction, &QAction::triggered, this, &MainWindow::compressSelection);
        menu.addSeparator();
        const bool removable = onRemovableStorage(paths);
        if (!removable) {
            QAction *trashAction = menu.addAction(
                IconProvider::instance().uiIcon(QStringLiteral("trash"), iconColor),
                QStringLiteral("移到回收站"));
            connect(trashAction, &QAction::triggered, this, &MainWindow::trashSelected);
        }
        QAction *deleteAction = menu.addAction(
            IconProvider::instance().uiIcon(QStringLiteral("trash"), iconColor),
            QStringLiteral("彻底删除"));
        connect(deleteAction, &QAction::triggered, this, &MainWindow::deleteSelectedPermanently);
        QAction *propertiesAction = menu.addAction(
            IconProvider::instance().uiIcon(QStringLiteral("info"), iconColor),
            QStringLiteral("属性"));
        connect(propertiesAction, &QAction::triggered, this, [this, paths]() { showProperties(paths); });
        menu.addSeparator();
    }

    QAction *newAction = menu.addAction(
        IconProvider::instance().uiIcon(QStringLiteral("folder-plus"), iconColor),
        QStringLiteral("新建文件夹"));
    QAction *refreshAction = menu.addAction(
        IconProvider::instance().uiIcon(QStringLiteral("refresh"), iconColor),
        QStringLiteral("刷新"));
    QAction *hiddenAction = menu.addAction(QStringLiteral("显示隐藏文件"));
    hiddenAction->setCheckable(true);
    hiddenAction->setChecked(m_showHidden);
    connect(hiddenAction, &QAction::triggered, this, &MainWindow::toggleHiddenFiles);
    connect(newAction, &QAction::triggered, this, &MainWindow::createFolder);
    connect(refreshAction, &QAction::triggered, pane, &FilePane::refresh);
    connect(hiddenAction, &QAction::triggered, this, &MainWindow::toggleHiddenFiles);
    menu.exec(globalPos);
}



void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_transfers && m_transfers->hasActive()) {
        if (!UiDialogs::confirm(this, QStringLiteral("正在传输"),
                                QStringLiteral("当前有传输任务正在进行。退出会取消任务，确定要退出吗？"),
                                QStringLiteral("退出"), QStringLiteral("取消"), true)) {
            event->ignore();
            return;
        }
        m_transfers->shutdown();
    }
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("ui/singlePane"), m_singlePane);
    if (m_left)
        settings.setValue(QStringLiteral("paths/left"), m_left->currentPath());
    if (m_right)
        settings.setValue(QStringLiteral("paths/right"), m_right->currentPath());
    QMainWindow::closeEvent(event);
}
