#include "filepane.h"

#include "directorymodel.h"
#include "desktopentry.h"
#include "filedelegate.h"
#include "fileheaderview.h"
#include "fileiconview.h"
#include "filetableview.h"
#include "formatters.h"
#include "iconprovider.h"
#include "theme.h"
#include "uidialogs.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMenu>
#include <QShortcut>
#include <QStyle>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QTableView>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

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

QLabel *makeMetaLabel(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("FooterCount"));
    return label;
}

class FullTextSearchTask : public QRunnable
{
public:
    FullTextSearchTask(QPointer<FilePane> pane, quint64 token, QString directory,
                       QString query)
        : m_pane(pane), m_token(token), m_directory(directory), m_query(query)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QString error;
        const QVector<FullTextHit> hits = FullTextSearch::search(m_directory, m_query, 500, &error);
        QCoreApplication *app = QCoreApplication::instance();
        if (!app)
            return;
        QPointer<FilePane> guard = m_pane;
        QMetaObject::invokeMethod(app, [guard, token = m_token, query = m_query,
                                        hits, error]() {
            if (guard)
                guard->applyFullTextResults(token, query, hits, error);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<FilePane> m_pane;
    quint64 m_token;
    QString m_directory;
    QString m_query;
};
}

FilePane::FilePane(IconProvider *icons, QWidget *parent)
    : QFrame(parent)
    , m_icons(icons)
    , m_model(nullptr)
    , m_view(nullptr)
    , m_header(nullptr)
    , m_delegate(nullptr)
    , m_headerFrame(nullptr)
    , m_locationIcon(nullptr)
    , m_breadcrumbLayout(nullptr)
    , m_breadcrumbHost(nullptr)
    , m_countLabel(nullptr)
    , m_selectionLabel(nullptr)
{
    setObjectName(QStringLiteral("Pane"));
    setFrameShape(QFrame::NoFrame);
    buildUi();
}

FilePane::~FilePane() = default;

void FilePane::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_headerFrame = new QFrame(this);
    m_headerFrame->setObjectName(QStringLiteral("PaneHeader"));
    m_headerFrame->setFixedHeight(54);
    auto *headerLayout = new QHBoxLayout(m_headerFrame);
    headerLayout->setContentsMargins(14, 0, 10, 0);
    headerLayout->setSpacing(7);

    m_locationIcon = new QLabel(m_headerFrame);
    m_locationIcon->setObjectName(QStringLiteral("LocationIcon"));
    m_locationIcon->setFixedSize(28, 28);
    m_locationIcon->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(m_locationIcon);

    m_breadcrumbHost = new QWidget(m_headerFrame);
    m_breadcrumbHost->setMinimumWidth(0);
    m_breadcrumbHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_breadcrumbLayout = new QHBoxLayout(m_breadcrumbHost);
    m_breadcrumbLayout->setContentsMargins(0, 0, 0, 0);
    m_breadcrumbLayout->setSpacing(1);
    headerLayout->addWidget(m_breadcrumbHost, 1);

    m_searchEdit = new QLineEdit(m_headerFrame);
    m_searchEdit->setObjectName(QStringLiteral("PaneSearchInput"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索当前文件夹及子文件夹"));
    m_searchEdit->setClearButtonEnabled(false);
    m_searchEdit->setMinimumWidth(0);
    m_searchEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_searchEdit->hide();
    headerLayout->addWidget(m_searchEdit, 1);

    m_searchMode = new QComboBox(m_headerFrame);
    m_searchMode->setObjectName(QStringLiteral("PaneSearchMode"));
    m_searchMode->addItem(QStringLiteral("文件名"), QStringLiteral("filename"));
    m_searchMode->addItem(QStringLiteral("文件内容"), QStringLiteral("fulltext"));
    m_searchMode->setFixedWidth(88);
    m_searchMode->setFocusPolicy(Qt::NoFocus);
    m_searchMode->hide();
    headerLayout->addWidget(m_searchMode);

    m_searchClose = new QToolButton(m_headerFrame);
    m_searchClose->setObjectName(QStringLiteral("PaneSearchClose"));
    m_searchClose->setIcon(IconProvider::instance().uiIcon(
        QStringLiteral("close"), Theme::instance().textSecondary()));
    m_searchClose->setIconSize(QSize(16, 16));
    m_searchClose->setFixedSize(28, 28);
    m_searchClose->setCursor(Qt::PointingHandCursor);
    m_searchClose->setFocusPolicy(Qt::NoFocus);
    m_searchClose->setToolTip(QStringLiteral("关闭搜索"));
    m_searchClose->hide();
    headerLayout->addWidget(m_searchClose);

    setMinimumWidth(0);
    m_headerFrame->setMinimumWidth(0);

    layout->addWidget(m_headerFrame);

    m_model = new DirectoryModel(m_icons, this);
    m_view = new FileTableView(this);
    m_view->setObjectName(QStringLiteral("FileTable"));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setShowGrid(false);
    m_view->setWordWrap(false);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSortingEnabled(false);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setMouseTracking(true);
    m_view->viewport()->setAttribute(Qt::WA_Hover, true);
    m_view->verticalHeader()->setVisible(false);
    m_view->verticalHeader()->setDefaultSectionSize(28);
    m_header = new FileHeaderView(Qt::Horizontal, m_view);
    m_view->setHorizontalHeader(m_header);
    m_delegate = new FileRowDelegate(this);
    m_view->setItemDelegate(m_delegate);
    m_view->setModel(m_model);
    m_grid = new FileIconView(this);
    m_grid->setModel(m_model);
    // Share the selection model so both presentations always show the same
    // selection and every operation (copy, delete, reveal...) keeps working.
    if (QItemSelectionModel *sharedSelection = m_view->selectionModel())
        m_grid->setSelectionModel(sharedSelection);
    m_viewStack = new QStackedWidget(this);
    m_viewStack->setObjectName(QStringLiteral("FileViewStack"));
    m_viewStack->addWidget(m_view);
    m_viewStack->addWidget(m_grid);
    m_header->setSectionResizeMode(DirectoryModel::NameColumn, QHeaderView::Interactive);
    m_header->setSectionResizeMode(DirectoryModel::SizeColumn, QHeaderView::Interactive);
    m_header->setSectionResizeMode(DirectoryModel::DateColumn, QHeaderView::Interactive);
    m_header->resizeSection(DirectoryModel::NameColumn, 320);
    m_header->resizeSection(DirectoryModel::SizeColumn, 88);
    m_header->resizeSection(DirectoryModel::DateColumn, 158);
    m_header->setSortIndicator(DirectoryModel::NameColumn, Qt::AscendingOrder);
    m_header->setSortIndicatorShown(true);
    m_view->installEventFilter(this);
    m_grid->installEventFilter(this);
    m_header->installEventFilter(this);
    layout->addWidget(m_viewStack, 1);

    auto addViewShortcuts = [this](QWidget *view) {
        auto *deleteShortcut = new QShortcut(QKeySequence::Delete, view);
        deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(deleteShortcut, &QShortcut::activated, this, [this]() {
            emit activationRequested(this);
            emit deleteRequested(this);
        });
        auto *renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), view);
        renameShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(renameShortcut, &QShortcut::activated, this, [this]() {
            emit activationRequested(this);
            emit renameRequested(this);
        });
        auto *copyShortcut = new QShortcut(QKeySequence::Copy, view);
        copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(copyShortcut, &QShortcut::activated, this, [this]() {
            emit activationRequested(this);
            emit copyRequested(this);
        });
        auto *cutShortcut = new QShortcut(QKeySequence::Cut, view);
        cutShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(cutShortcut, &QShortcut::activated, this, [this]() {
            emit activationRequested(this);
            emit cutRequested(this);
        });
        auto *pasteShortcut = new QShortcut(QKeySequence::Paste, view);
        pasteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(pasteShortcut, &QShortcut::activated, this, [this]() {
            emit activationRequested(this);
            emit pasteRequested(this);
        });
    };
    addViewShortcuts(m_view);
    addViewShortcuts(m_grid);
    connect(m_view, &FileTableView::copyToOtherRequested, this, [this]() {
        emit activationRequested(this);
        emit copyToOtherPaneRequested(this);
    });

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("PaneFooter"));
    footer->setFixedHeight(34);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 0, 16, 0);
    footerLayout->setSpacing(8);
    m_countLabel = makeMetaLabel(QStringLiteral("0 个项目"), footer);
    m_selectionLabel = makeMetaLabel(QString(), footer);
    m_selectionLabel->setObjectName(QStringLiteral("FooterSelection"));
    footerLayout->addWidget(m_countLabel);
    footerLayout->addStretch(1);
    footerLayout->addWidget(m_selectionLabel);
    layout->addWidget(footer);

    m_clipboardAnimation = new QVariantAnimation(this);
    m_clipboardAnimation->setDuration(850);
    m_clipboardAnimation->setStartValue(1.0);
    m_clipboardAnimation->setEndValue(0.0);
    m_clipboardAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_clipboardAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_delegate->setFeedbackOpacity(value.toReal());
        m_view->viewport()->update();
    });
    connect(m_clipboardAnimation, &QVariantAnimation::finished, this, [this]() {
        m_delegate->setClipboardFeedback(QSet<QString>(), false);
        m_delegate->setFeedbackOpacity(0.0);
        m_view->viewport()->update();
    });

    connect(m_view, &QTableView::doubleClicked, this, &FilePane::onDoubleClicked);
    connect(m_view, &QTableView::clicked, this, [this]() { emit activationRequested(this); });
    auto handleContextMenu = [this](QAbstractItemView *view, const QPoint &pos) {
        const QModelIndex hit = view->indexAt(pos);
        if (hit.isValid()) {
            if (!view->selectionModel()->isSelected(hit)) {
                view->selectionModel()->select(
                    hit, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                view->setCurrentIndex(hit);
            }
        } else {
            view->clearSelection();
        }
        emit activationRequested(this);
        emit contextMenuRequested(this, view->viewport()->mapToGlobal(pos));
    };
    connect(m_view, &QTableView::customContextMenuRequested, this,
            [this, handleContextMenu](const QPoint &pos) { handleContextMenu(m_view, pos); });
    connect(m_view, &FileTableView::previewRequested, this, [this]() {
        const QModelIndex current = m_view->currentIndex();
        if (!current.isValid())
            return;
        FileEntry entry;
        if (!m_model->entryAt(current.row(), &entry))
            return;
        emit activationRequested(this);
        emit previewRequested(this, entry.path);
    });
    connect(m_view, &FileTableView::folderDisclosureRequested,
            this, &FilePane::toggleFolderDisclosure);
    connect(m_view, &FileTableView::filesDropped, this,
            [this](const QStringList &paths, Qt::DropAction action) {
        if (paths.isEmpty() || m_path.isEmpty())
            return;
        emit activationRequested(this);
        emit dropTransferRequested(paths, m_path, action == Qt::MoveAction);
    });
    // Icon-grid view mirrors the list view behaviours.
    connect(m_grid, &QListView::doubleClicked, this, &FilePane::onDoubleClicked);
    connect(m_grid, &QListView::clicked, this, [this]() { emit activationRequested(this); });
    connect(m_grid, &QListView::customContextMenuRequested, this,
            [this, handleContextMenu](const QPoint &pos) { handleContextMenu(m_grid, pos); });
    connect(m_grid, &FileIconView::previewRequested, this, [this]() {
        const QModelIndex current = m_grid->currentIndex();
        if (!current.isValid())
            return;
        FileEntry entry;
        if (!m_model->entryAt(current.row(), &entry))
            return;
        emit activationRequested(this);
        emit previewRequested(this, entry.path);
    });
    connect(m_grid, &FileIconView::filesDropped, this,
            [this](const QStringList &paths, Qt::DropAction action) {
        if (paths.isEmpty() || m_path.isEmpty())
            return;
        emit activationRequested(this);
        emit dropTransferRequested(paths, m_path, action == Qt::MoveAction);
    });
    connect(m_grid, &FileIconView::copyToOtherRequested, this, [this]() {
        emit activationRequested(this);
        emit copyToOtherPaneRequested(this);
    });
    connect(m_grid, &QAbstractItemView::pressed, this, [this]() {
        stopPendingReveal();
    });
    connect(m_header, &QHeaderView::sectionClicked, this, &FilePane::onHeaderClicked);
    connect(m_header, &QHeaderView::sectionClicked, this, [this]() { emit activationRequested(this); });
    connect(m_model, &DirectoryModel::directoryLoaded, this, &FilePane::onDirectoryLoaded);
    connect(m_model, &DirectoryModel::loadFailed, this, &FilePane::onLoadFailed);
    connect(m_model, &DirectoryModel::loadingChanged, this, &FilePane::onLoadingChanged);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &FilePane::onSelectionChanged);
    m_watcher = new QFileSystemWatcher(this);
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(250);
    connect(m_refreshTimer, &QTimer::timeout, this, &FilePane::refresh);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &path) {
        if (QDir::cleanPath(path) == QDir::cleanPath(m_path) && m_refreshTimer)
            m_refreshTimer->start();
    });
    m_revealTimer = new QTimer(this);
    m_revealTimer->setSingleShot(true);
    m_revealTimer->setInterval(120);
    connect(m_revealTimer, &QTimer::timeout, this, &FilePane::selectPendingReveal);
    // Any user interaction with the view cancels a pending/active reveal so it
    // never fights the user's own selection changes.
    connect(m_view, &QAbstractItemView::pressed, this, [this]() {
        stopPendingReveal();
    });

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(320);
    connect(m_searchDebounce, &QTimer::timeout, this, &FilePane::runSearch);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        if (m_searchActive && m_searchDebounce)
            m_searchDebounce->start();
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        if (m_searchDebounce)
            m_searchDebounce->stop();
        runSearch();
    });
    connect(m_searchClose, &QToolButton::clicked, this, &FilePane::endSearch);
    connect(m_searchMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_searchActive)
            return;
        const bool contentMode = m_searchMode
            && m_searchMode->currentData().toString() == QStringLiteral("fulltext");
        if (contentMode) {
            const WssScopeStatus status = FullTextSearch::scopeStatus(m_path);
            if (status.availability != WssAvailability::Ready)
                showContentSearchRequirement(status);
        }
        m_searchNotice.clear();
        if (m_searchDebounce)
            m_searchDebounce->stop();
        runSearch();
    });

    updateTheme();
    applyActiveStyle();
}

bool FilePane::navigateTo(const QString &path, bool addHistory)
{
    QFileInfo info(path);
    if (!info.exists()) {
        emit statusMessage(QStringLiteral("文件夹不存在：%1").arg(path));
        return false;
    }
    if (!info.isDir()) {
        emit statusMessage(QStringLiteral("目标不是文件夹：%1").arg(path));
        return false;
    }
    if (!info.isReadable()) {
        emit statusMessage(QStringLiteral("没有权限读取此文件夹"));
        return false;
    }
    const QString clean = QDir::cleanPath(info.absoluteFilePath());
    if (clean == m_path && !m_model->isLoading()) {
        refresh();
        return true;
    }
    if (m_searchActive)
        hideSearchBar();
    m_restorePaths.clear();
    m_restoreCurrent.clear();
    m_restoreScroll = -1;
    m_path = clean;
    m_searchFilter.clear();
    emit filterChanged(this);
    loadPath(clean);
    if (addHistory)
        appendToHistory(clean);
    updateBreadcrumbs();
    updateLocationIcon();
    emit pathChanged(this);
    emit navigationChanged(this);
    return true;
}

void FilePane::goBack()
{
    if (!canGoBack())
        return;
    --m_historyIndex;
    if (m_searchActive)
        hideSearchBar();
    m_restorePaths.clear();
    m_restoreCurrent.clear();
    m_restoreScroll = -1;
    m_path = m_history.at(m_historyIndex);
    m_searchFilter.clear();
    emit filterChanged(this);
    loadPath(m_path);
    updateBreadcrumbs();
    updateLocationIcon();
    emit pathChanged(this);
    emit navigationChanged(this);
}

void FilePane::goForward()
{
    if (!canGoForward())
        return;
    ++m_historyIndex;
    if (m_searchActive)
        hideSearchBar();
    m_restorePaths.clear();
    m_restoreCurrent.clear();
    m_restoreScroll = -1;
    m_path = m_history.at(m_historyIndex);
    m_searchFilter.clear();
    emit filterChanged(this);
    loadPath(m_path);
    updateBreadcrumbs();
    updateLocationIcon();
    emit pathChanged(this);
    emit navigationChanged(this);
}

void FilePane::goUp()
{
    QDir directory(m_path);
    if (!directory.cdUp())
        return;
    navigateTo(directory.absolutePath(), true);
}

void FilePane::refresh()
{
    if (m_path.isEmpty())
        return;
    if (!m_ejectingMount.isEmpty() && isPathInside(m_path, m_ejectingMount))
        return;
    if (m_refreshTimer)
        m_refreshTimer->stop();
    if (m_model->isLoading()) {
        if (m_refreshTimer)
            m_refreshTimer->start();
        return;
    }
    if (m_searchActive && m_searchEdit && !m_searchEdit->text().trimmed().isEmpty()) {
        runSearch();
        updateWatcher();
        emit navigationChanged(this);
        return;
    }
    if (!m_searchActive)
        hideSearchBar();
    captureRefreshState();
    m_model->setFilter(m_searchFilter);
    m_model->setDirectory(m_path, true);
    updateBreadcrumbs();
    updateLocationIcon();
    updateWatcher();
    emit navigationChanged(this);
}

void FilePane::updateWatcher()
{
    if (!m_watcher)
        return;
    const QStringList watched = m_watcher->directories();
    if (!watched.isEmpty())
        m_watcher->removePaths(watched);
    if (!m_path.isEmpty() && QFileInfo(m_path).isDir())
        m_watcher->addPath(m_path);
}

void FilePane::captureRefreshState()
{
    m_restorePaths = selectedPaths();
    m_restoreCurrent = m_view->currentIndex().data(Qt::UserRole).toString();
    m_restoreScroll = currentView()->verticalScrollBar()
        ? currentView()->verticalScrollBar()->value() : -1;
}

void FilePane::restoreRefreshState()
{
    if (m_restorePaths.isEmpty() && m_restoreCurrent.isEmpty() && m_restoreScroll < 0)
        return;
    if (!m_view->selectionModel())
        return;
    QItemSelection selection;
    for (const QString &path : m_restorePaths) {
        const int row = m_model->rowForPath(path);
        if (row < 0)
            continue;
        const QModelIndex index = m_model->index(row, DirectoryModel::NameColumn);
        selection.select(index, index);
    }
    if (!selection.isEmpty())
        m_view->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect
                                                    | QItemSelectionModel::Rows);
    const int currentRow = m_model->rowForPath(m_restoreCurrent);
    if (currentRow >= 0) {
        const QModelIndex current = m_model->index(currentRow, DirectoryModel::NameColumn);
        m_view->setCurrentIndex(current);
        if (selection.isEmpty())
            currentView()->scrollTo(current, QAbstractItemView::EnsureVisible);
    }
    if (m_restoreScroll >= 0 && currentView()->verticalScrollBar())
        currentView()->verticalScrollBar()->setValue(m_restoreScroll);
    m_restorePaths.clear();
    m_restoreCurrent.clear();
    m_restoreScroll = -1;
}

void FilePane::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    applyActiveStyle();
}

bool FilePane::isActive() const
{
    return m_active;
}

void FilePane::setHeaderHighlight(bool enabled)
{
    if (m_headerHighlight == enabled)
        return;
    m_headerHighlight = enabled;
    applyActiveStyle();
}

void FilePane::setShowHidden(bool show)
{
    if (!m_model || m_model->showHidden() == show)
        return;
    captureRefreshState();
    m_model->setShowHidden(show);
    updateFooter();
}

bool FilePane::showHidden() const
{
    return m_model && m_model->showHidden();
}

void FilePane::beginSearch()
{
    if (!m_searchEdit || !m_searchClose)
        return;
    if (!m_searchActive) {
        m_searchActive = true;
        if (m_breadcrumbHost)
            m_breadcrumbHost->hide();
        m_searchEdit->show();
        if (m_searchMode)
            m_searchMode->show();
        m_searchClose->show();
        QSignalBlocker blocker(m_searchEdit);
        m_searchEdit->clear();
    }
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
    if (m_searchMode && m_searchMode->currentData().toString() == QStringLiteral("fulltext")) {
        const WssScopeStatus status = FullTextSearch::scopeStatus(m_path);
        if (status.availability != WssAvailability::Ready)
            showContentSearchRequirement(status);
    }
}

void FilePane::endSearch()
{
    if (!m_searchActive)
        return;
    hideSearchBar();
    m_model->setDirectory(m_path, true);
    updateFooter();
    currentView()->setFocus();
}

bool FilePane::isSearching() const
{
    return m_searchActive;
}

void FilePane::revealPath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return;
    const QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    m_pendingRevealPath = info.isDir() ? QString() : info.absoluteFilePath();
    m_revealAttempts = 0;
    if (m_revealTimer)
        m_revealTimer->stop();
    if (m_searchActive || m_model->isSearchMode()
        || QDir::cleanPath(directory) != QDir::cleanPath(m_path)) {
        if (m_searchActive)
            hideSearchBar();
        if (!navigateTo(directory, true)) {
            m_pendingRevealPath.clear();
            return;
        }
    }
    if (!m_pendingRevealPath.isEmpty()) {
        selectPendingReveal();
        if (!m_pendingRevealPath.isEmpty() && m_revealTimer)
            m_revealTimer->start();
    } else {
        currentView()->setFocus();
    }
}

void FilePane::stopPendingReveal()
{
    m_pendingRevealPath.clear();
    m_revealAttempts = 0;
    m_revealStableTicks = 0;
    if (m_revealTimer)
        m_revealTimer->stop();
}

void FilePane::selectPendingReveal()
{
    if (m_pendingRevealPath.isEmpty())
        return;
    if (m_model->isLoading()) {
        if (m_revealAttempts++ < 40 && m_revealTimer)
            m_revealTimer->start();
        else
            stopPendingReveal();
        return;
    }
    const int row = m_model->rowForPath(m_pendingRevealPath);
    if (row < 0) {
        if (m_revealAttempts++ < 40 && m_revealTimer)
            m_revealTimer->start();
        else
            stopPendingReveal();
        return;
    }
    const QModelIndex index = m_model->index(row, 0);
    if (!m_view->selectionModel()->isSelected(index)) {
        // (Re-)assert the reveal selection: a directory reload that raced the
        // reveal resets the model and clears the selection, so keep applying
        // it until it holds steady.
        m_view->selectionModel()->clearSelection();
        selectRowInCurrentView(row);
        m_view->setCurrentIndex(index);
        currentView()->scrollTo(index, QAbstractItemView::PositionAtCenter);
        m_view->selectionModel()->select(index, QItemSelectionModel::Select
                                                    | QItemSelectionModel::Rows);
        m_revealStableTicks = 0;
    } else if (++m_revealStableTicks >= 15) {
        // Selection has held steady for ~1.8s — stop re-asserting.
        stopPendingReveal();
        currentView()->setFocus();
        return;
    }
    currentView()->setFocus();
    if (m_revealTimer)
        m_revealTimer->start();
}

void FilePane::setMountEjecting(const QString &mountPath)
{
    m_ejectingMount = QDir::cleanPath(mountPath);
    if (m_refreshTimer)
        m_refreshTimer->stop();
}

void FilePane::clearMountEjecting()
{
    m_ejectingMount.clear();
}

void FilePane::hideSearchBar()
{
    m_searchActive = false;
    if (m_searchDebounce)
        m_searchDebounce->stop();
    if (m_searchEdit) {
        QSignalBlocker blocker(m_searchEdit);
        m_searchEdit->clear();
        m_searchEdit->hide();
    }
    if (m_searchMode)
        m_searchMode->hide();
    if (m_searchClose)
        m_searchClose->hide();
    m_searchFullText = false;
    m_fullTextLoading = false;
    m_searchNotice.clear();
    ++m_fullTextToken;
    if (m_breadcrumbHost)
        m_breadcrumbHost->show();
}

void FilePane::toggleFolderDisclosure(const QString &path)
{
    if (!m_model)
        return;
    const int row = m_model->rowForPath(path);
    if (row < 0 || !m_model->canExpandRow(row))
        return;
    m_model->toggleFolderExpanded(row);
    updateFooter();
    const int currentRow = m_model->rowForPath(path);
    if (currentRow >= 0) {
        const QModelIndex index = m_model->index(currentRow, DirectoryModel::NameColumn);
        selectRowInCurrentView(currentRow);
        m_view->setCurrentIndex(index);
        currentView()->scrollTo(index, QAbstractItemView::EnsureVisible);
    }
    currentView()->setFocus();
}

void FilePane::showContentSearchRequirement(const WssScopeStatus &status)
{

    QString message;
    switch (status.availability) {
    case WssAvailability::NotInstalled:
        message = QStringLiteral("使用「文件内容」搜索前，请先安装文搜搜，"
                                 "并在文搜搜中添加索引目录、建立索引。");
        break;
    case WssAvailability::NoIndex:
        message = QStringLiteral("文搜搜已安装，但还没有建立索引。"
                                 "请先打开文搜搜建立索引，再使用「文件内容」搜索。");
        break;
    case WssAvailability::NotIndexed:
        message = QStringLiteral("当前文件夹未加入文搜搜的索引目录。"
                                 "请先在文搜搜中把该文件夹加入索引，"
                                 "再使用「文件内容」搜索。");
        break;
    case WssAvailability::Error:
    case WssAvailability::Ready:
        if (status.availability == WssAvailability::Error)
            message = status.message;
        break;
    }
    if (!message.isEmpty())
        UiDialogs::showWarning(this, QStringLiteral("文件内容搜索"), message);
}

void FilePane::runSearch()
{
    if (!m_searchActive || !m_searchEdit)
        return;
    if (m_searchMode && m_searchMode->currentData().toString() == QStringLiteral("fulltext"))
        startFullTextSearch();
    else
        startRecursiveSearch();
}

void FilePane::startFullTextSearch()
{
    if (!m_model || !m_searchEdit || !m_searchActive)
        return;
    const QString query = m_searchEdit->text().trimmed();
    const WssScopeStatus status = FullTextSearch::scopeStatus(m_path);
    if (query.isEmpty()) {
        m_searchFullText = false;
        m_fullTextLoading = false;
        m_searchNotice = status.availability == WssAvailability::Ready
            ? QString() : status.message;
        if (m_model->isSearchMode())
            m_model->setDirectory(m_path, true);
        updateFooter();
        return;
    }
    if (status.availability != WssAvailability::Ready) {
        m_searchFullText = true;
        m_fullTextLoading = false;
        m_searchNotice = status.message;
        ++m_fullTextToken;
        m_model->setFullTextResults(m_path, query, QVector<FileEntry>());
        updateFooter();
        return;
    }
    m_searchFullText = true;
    m_fullTextLoading = true;
    m_searchNotice.clear();
    const quint64 token = ++m_fullTextToken;
    updateFooter();
    QThreadPool::globalInstance()->start(new FullTextSearchTask(
        QPointer<FilePane>(this), token, m_path, query));
}

void FilePane::applyFullTextResults(quint64 token, const QString &query,
                                    const QVector<FullTextHit> &hits, const QString &error)
{
    if (token != m_fullTextToken || !m_searchActive || !m_searchFullText)
        return;
    m_fullTextLoading = false;
    if (!error.isEmpty()) {
        m_searchNotice = error;
        m_model->setFullTextResults(m_path, query, QVector<FileEntry>());
        updateFooter();
        return;
    }
    QVector<FileEntry> entries;
    entries.reserve(hits.size());
    for (const FullTextHit &hit : hits) {
        FileEntry entry;
        entry.name = hit.name;
        entry.path = hit.path;
        entry.suffix = hit.extension;
        entry.size = hit.size;
        entry.modified = QDateTime::fromMSecsSinceEpoch(hit.mtimeMs);
        entries.append(entry);
    }
    m_searchNotice.clear();
    m_model->setFullTextResults(m_path, query, entries);
    updateFooter();
}

void FilePane::startRecursiveSearch()
{
    if (!m_model || !m_searchEdit || !m_searchActive)
        return;
    const QString query = m_searchEdit->text().trimmed();
    m_searchFullText = false;
    m_fullTextLoading = false;
    m_searchNotice.clear();
    if (query.isEmpty()) {
        if (m_model->isSearchMode())
            m_model->setDirectory(m_path, true);
        updateFooter();
        return;
    }
    captureRefreshState();
    m_model->setSearchMode(m_path, query, m_model->showHidden());
    updateFooter();
}

void FilePane::updateTheme()
{
    updateLocationIcon();
    updateBreadcrumbs();
    m_delegate->setActive(m_active);
    m_header->viewport()->update();
    m_view->viewport()->update();
    applyActiveStyle();
}

QString FilePane::currentPath() const
{
    return m_path;
}

QString FilePane::displayPath() const
{
    return homeDisplayPath(m_path);
}

QStringList FilePane::selectedPaths() const
{
    QStringList paths;
    if (!m_view->selectionModel())
        return paths;
    const QModelIndexList rows = m_view->selectionModel()->selectedRows(DirectoryModel::NameColumn);
    for (const QModelIndex &index : rows) {
        FileEntry entry;
        if (m_model->entryAt(index.row(), &entry))
            paths.append(entry.path);
    }
    return paths;
}

QVector<FileEntry> FilePane::selectedEntries() const
{
    QVector<FileEntry> entries;
    if (!m_view->selectionModel())
        return entries;
    const QModelIndexList rows = m_view->selectionModel()->selectedRows(DirectoryModel::NameColumn);
    for (const QModelIndex &index : rows) {
        FileEntry entry;
        if (m_model->entryAt(index.row(), &entry))
            entries.append(entry);
    }
    return entries;
}

int FilePane::selectedCount() const
{
    return selectedEntries().size();
}

qint64 FilePane::selectedFileBytes() const
{
    qint64 total = 0;
    const QVector<FileEntry> entries = selectedEntries();
    for (const FileEntry &entry : entries) {
        if (!entry.isDir)
            total += entry.size;
    }
    return total;
}

bool FilePane::hasSelection() const
{
    return m_view->selectionModel() && m_view->selectionModel()->hasSelection();
}

bool FilePane::canGoBack() const
{
    return m_historyIndex > 0;
}

bool FilePane::canGoForward() const
{
    return m_historyIndex >= 0 && m_historyIndex + 1 < m_history.size();
}

bool FilePane::canGoUp() const
{
    return !m_path.isEmpty() && QDir(m_path).absolutePath() != QDir::rootPath();
}

QString FilePane::searchFilter() const
{
    return m_searchFilter;
}

void FilePane::setSearchFilter(const QString &text)
{
    m_searchFilter = text;
    m_model->setFilter(text);
    updateFooter();
}

void FilePane::focusView()
{
    currentView()->setFocus(Qt::OtherFocusReason);
    emit activationRequested(this);
}

void FilePane::openSelected()
{
    if (!m_view->currentIndex().isValid())
        return;
    openEntry(m_view->currentIndex());
}

DirectoryModel *FilePane::directoryModel() const
{
    return m_model;
}

QTableView *FilePane::tableView() const
{
    return m_view;
}

QListView *FilePane::iconView() const
{
    return m_grid;
}

QAbstractItemView *FilePane::currentView() const
{
    return m_gridMode ? static_cast<QAbstractItemView *>(m_grid)
                      : static_cast<QAbstractItemView *>(m_view);
}

void FilePane::selectRowInCurrentView(int row)
{
    if (row < 0 || !m_view->selectionModel())
        return;
    const QModelIndex index = m_model->index(row, DirectoryModel::NameColumn);
    m_view->selectionModel()->select(index, QItemSelectionModel::Select
                                             | QItemSelectionModel::Rows);
}

void FilePane::setGridMode(bool grid)
{
    if (m_gridMode == grid)
        return;
    if (!m_grid || !m_viewStack)
        return;
    // Carry the scroll position across presentations for a smooth switch.
    QAbstractItemView *from = currentView();
    QAbstractItemView *to = grid ? static_cast<QAbstractItemView *>(m_grid)
                                 : static_cast<QAbstractItemView *>(m_view);
    const QModelIndex current = m_view->currentIndex();
    const int scroll = from->verticalScrollBar() ? from->verticalScrollBar()->value() : -1;
    m_gridMode = grid;
    m_viewStack->setCurrentWidget(grid ? static_cast<QWidget *>(m_grid)
                                       : static_cast<QWidget *>(m_view));
    if (current.isValid())
        to->scrollTo(current, QAbstractItemView::PositionAtCenter);
    else if (scroll >= 0 && to->verticalScrollBar())
        to->verticalScrollBar()->setValue(scroll);
    updateColumnWidths();
    if (isVisible())
        to->setFocus(Qt::OtherFocusReason);
}

bool FilePane::isGridMode() const
{
    return m_gridMode;
}

void FilePane::showContextMenuAt(const QPoint &globalPos)
{
    emit contextMenuRequested(this, globalPos);
}

void FilePane::flashClipboardFeedback(bool cut)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty() || !m_clipboardAnimation)
        return;
    QSet<QString> feedback;
    for (const QString &path : paths)
        feedback.insert(path);
    m_delegate->setClipboardFeedback(feedback, cut);
    m_delegate->setFeedbackOpacity(1.0);
    m_view->viewport()->update();
    m_clipboardAnimation->stop();
    m_clipboardAnimation->setStartValue(1.0);
    m_clipboardAnimation->setEndValue(0.0);
    m_clipboardAnimation->start();
}

void FilePane::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    updateColumnWidths();
}

void FilePane::updateColumnWidths()
{
    if (!m_view || !m_header || m_columnsCustomized)
        return;
    const int width = m_view->viewport()->width();
    if (width <= 0)
        return;
    const int fixed = m_header->sectionSize(DirectoryModel::SizeColumn)
        + m_header->sectionSize(DirectoryModel::DateColumn) + 2;
    const int nameWidth = qMax(180, width - fixed);
    m_adjustingColumns = true;
    m_header->resizeSection(DirectoryModel::NameColumn, nameWidth);
    m_adjustingColumns = false;
}

void FilePane::mousePressEvent(QMouseEvent *event)
{
    emit activationRequested(this);
    QFrame::mousePressEvent(event);
}

bool FilePane::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view || watched == m_grid || watched == m_header) {
        if (event->type() == QEvent::FocusIn) {
            emit activationRequested(this);
        } else if ((watched == m_view || watched == m_grid)
                   && event->type() == QEvent::KeyPress) {
            QKeyEvent *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                openSelected();
                return true;
            }
            if (key->key() == Qt::Key_Backspace) {
                goUp();
                return true;
            }
        }
    }
    return QFrame::eventFilter(watched, event);
}

void FilePane::onDoubleClicked(const QModelIndex &index)
{
    openEntry(index);
}

void FilePane::onSelectionChanged()
{
    updateSelectionLabels();
    emit selectionChanged(this);
}

void FilePane::onDirectoryLoaded(const QString &path)
{
    if (path == m_path) {
        updateFooter();
        updateBreadcrumbs();
        restoreRefreshState();
        // A load completion resets the model; re-assert a pending reveal that
        // raced the reset so the revealed file stays selected.
        if (!m_pendingRevealPath.isEmpty())
            selectPendingReveal();
        emit navigationChanged(this);
        if (!m_searchFilter.isEmpty() && m_model->rowCount() == 0)
            emit statusMessage(QStringLiteral("没有找到匹配的文件"));
    }
}

void FilePane::onLoadFailed(const QString &path, const QString &error)
{
    if (path == m_path && (m_ejectingMount.isEmpty() || !isPathInside(path, m_ejectingMount)))
        emit statusMessage(error);
    m_restorePaths.clear();
    m_restoreCurrent.clear();
    m_restoreScroll = -1;
    updateFooter();
}

void FilePane::onLoadingChanged(bool loading)
{
    m_loading = loading;
    updateFooter();
}

void FilePane::onHeaderClicked(int logicalIndex)
{
    if (logicalIndex < 0 || logicalIndex >= DirectoryModel::ColumnCount)
        return;
    Qt::SortOrder order = Qt::AscendingOrder;
    if (logicalIndex == m_sortColumn)
        order = (m_sortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;
    setSort(logicalIndex, order);
}

void FilePane::loadPath(const QString &path)
{
    m_model->setFilter(m_searchFilter);
    m_model->setDirectory(path, true);
    updateWatcher();
    updateFooter();
}

void FilePane::updateBreadcrumbs()
{
    if (!m_breadcrumbLayout)
        return;
    clearLayout(m_breadcrumbLayout);
    if (m_path.isEmpty())
        return;

    const QString home = QDir::homePath();
    QStringList names;
    QVector<QString> paths;
    if (m_path == home || m_path.startsWith(home + QLatin1Char('/'))) {
        names.append(QStringLiteral("主目录"));
        paths.append(home);
        const QString relative = m_path.mid(home.size());
        const QStringList parts = relative.split(QLatin1Char('/'), QString::SkipEmptyParts);
        QString built = home;
        for (const QString &part : parts) {
            built += QLatin1Char('/') + part;
            names.append(part);
            paths.append(built);
        }
    } else {
        names.append(QStringLiteral("/"));
        paths.append(QStringLiteral("/"));
        const QStringList parts = m_path.split(QLatin1Char('/'), QString::SkipEmptyParts);
        QString built;
        for (const QString &part : parts) {
            built += QLatin1Char('/') + part;
            names.append(part);
            paths.append(built);
        }
    }

    QVector<int> displayIndices;
    if (names.size() <= 5) {
        for (int i = 0; i < names.size(); ++i)
            displayIndices.append(i);
    } else {
        displayIndices.append(0);
        displayIndices.append(-1);
        for (int i = names.size() - 3; i < names.size(); ++i)
            displayIndices.append(i);
    }

    auto addButton = [this, &names, &paths](int index) {
        QToolButton *button = new QToolButton(m_breadcrumbHost);
        button->setObjectName(QStringLiteral("CrumbButton"));
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        button->setMinimumWidth(0);
        button->setMaximumWidth(115);
        button->setText(QFontMetrics(button->font()).elidedText(names.at(index), Qt::ElideMiddle, 95));
        button->setToolTip(paths.at(index));
        const QString target = paths.at(index);
        connect(button, &QToolButton::clicked, this, [this, target]() {
            emit activationRequested(this);
            navigateTo(target, true);
        });
        m_breadcrumbLayout->addWidget(button, 0);
    };

    for (int position = 0; position < displayIndices.size(); ++position) {
        if (position > 0) {
            QLabel *separator = new QLabel(QStringLiteral("›"), m_breadcrumbHost);
            separator->setObjectName(QStringLiteral("CrumbSep"));
            m_breadcrumbLayout->addWidget(separator);
        }
        const int index = displayIndices.at(position);
        if (index == -1) {
            QToolButton *more = new QToolButton(m_breadcrumbHost);
            more->setObjectName(QStringLiteral("CrumbMore"));
            more->setAutoRaise(true);
            more->setCursor(Qt::PointingHandCursor);
            more->setFocusPolicy(Qt::NoFocus);
            more->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            more->setFixedWidth(32);
            more->setText(QStringLiteral("…"));
            more->setToolTip(QStringLiteral("显示中间路径"));
            QMenu *menu = new QMenu(more);
            const int lastShown = names.size() - 4;
            for (int i = 1; i <= lastShown; ++i) {
                QAction *action = menu->addAction(names.at(i));
                const QString target = paths.at(i);
                connect(action, &QAction::triggered, this, [this, target]() {
                    emit activationRequested(this);
                    navigateTo(target, true);
                });
            }
            more->setMenu(menu);
            more->setPopupMode(QToolButton::InstantPopup);
            m_breadcrumbLayout->addWidget(more);
        } else if (index == names.size() - 1) {
            QLabel *current = new QLabel(names.at(index), m_breadcrumbHost);
            current->setObjectName(QStringLiteral("CrumbCurrent"));
            current->setToolTip(m_path);
            current->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            current->setMinimumWidth(0);
            current->setMaximumWidth(230);
            current->setText(QFontMetrics(current->font()).elidedText(names.at(index), Qt::ElideMiddle, 210));
            m_breadcrumbLayout->addWidget(current, 0);
        } else {
            addButton(index);
        }
    }
    m_breadcrumbLayout->addStretch(1);

    updateLocationIcon();
}

void FilePane::updateFooter()
{
    if (!m_countLabel)
        return;
    if (!m_searchNotice.isEmpty()) {
        m_countLabel->setText(m_searchNotice);
    } else if (m_fullTextLoading) {
        m_countLabel->setText(QStringLiteral("正在搜索文件内容…"));
    } else if (m_loading) {
        m_countLabel->setText(m_model && m_model->isSearchMode()
                                  ? QStringLiteral("正在搜索…")
                                  : QStringLiteral("正在读取…"));
    } else if (m_model && m_model->isFullTextSearch()) {
        m_countLabel->setText(QStringLiteral("文件内容找到 %1 项").arg(m_model->rowCount()));
    } else if (m_model && m_model->isSearchMode()) {
        m_countLabel->setText(QStringLiteral("找到 %1 项").arg(m_model->rowCount()));
    } else {
        const int count = m_model->rowCount();
        if (!m_searchFilter.isEmpty())
            m_countLabel->setText(QStringLiteral("找到 %1 个项目").arg(count));
        else
            m_countLabel->setText(QStringLiteral("%1 个项目").arg(count));
    }
    updateSelectionLabels();
}

void FilePane::updateSelectionLabels()
{
    const QVector<FileEntry> entries = selectedEntries();
    if (entries.isEmpty()) {
        m_selectionLabel->clear();
        m_selectionLabel->setProperty("selected", false);
    } else {
        QString text = QStringLiteral("已选 %1 项").arg(entries.size());
        bool hasDir = false;
        for (const FileEntry &entry : entries) {
            if (entry.isDir) {
                hasDir = true;
                break;
            }
        }
        if (!hasDir) {
            qint64 bytes = 0;
            for (const FileEntry &entry : entries)
                bytes += entry.size;
            text += QStringLiteral(" · %1").arg(formatSize(bytes));
        }
        m_selectionLabel->setText(text);
        m_selectionLabel->setProperty("selected", true);
    }
    m_selectionLabel->style()->unpolish(m_selectionLabel);
    m_selectionLabel->style()->polish(m_selectionLabel);
}

void FilePane::clearBreadcrumbLayout()
{
    clearLayout(m_breadcrumbLayout);
}

void FilePane::updateLocationIcon()
{
    if (!m_locationIcon)
        return;
    QString name = QStringLiteral("folder");
    if (m_path.startsWith(QStringLiteral("/media/"))
        || m_path.startsWith(QStringLiteral("/run/media/"))
        || m_path.startsWith(QStringLiteral("/mnt/"))) {
        name = QStringLiteral("usb");
    }
    const QColor color = m_active ? Theme::instance().accent() : Theme::instance().textSecondary();
    m_locationIcon->setPixmap(m_icons->uiIcon(name, color).pixmap(18, 18));
}

void FilePane::appendToHistory(const QString &path)
{
    if (m_historyIndex >= 0 && m_historyIndex < m_history.size()
        && m_history.at(m_historyIndex) == path) {
        return;
    }
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    m_history.append(path);
    m_historyIndex = m_history.size() - 1;
}

void FilePane::setSort(int column, Qt::SortOrder order)
{
    m_sortColumn = column;
    m_sortOrder = order;
    m_header->setSortIndicator(column, order);
    m_header->setSortIndicatorShown(true);
    m_model->sort(column, order);
    m_header->viewport()->update();
    m_view->viewport()->update();
}

bool FilePane::openEntry(const QModelIndex &index)
{
    FileEntry entry;
    if (!m_model->entryAt(index.row(), &entry))
        return false;
    if (entry.isDir) {
        navigateTo(entry.path, true);
        return true;
    }
    if (entry.isDesktopFile) {
        QString error;
        if (!launchDesktopEntry(entry.path, &error)) {
            emit statusMessage(error.isEmpty() ? QStringLiteral("无法启动应用") : error);
            return false;
        }
        return true;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(entry.path))) {
        emit statusMessage(QStringLiteral("无法打开文件：%1").arg(entry.name));
        return false;
    }
    return true;
}

void FilePane::applyActiveStyle()
{
    const bool highlight = m_active && m_headerHighlight;
    setProperty("active", highlight);
    style()->unpolish(this);
    style()->polish(this);
    if (m_headerFrame) {
        m_headerFrame->setProperty("active", highlight);
        m_headerFrame->style()->unpolish(m_headerFrame);
        m_headerFrame->style()->polish(m_headerFrame);
        m_headerFrame->update();
    }
    if (m_delegate)
        m_delegate->setActive(m_active);
    updateLocationIcon();
    if (m_view)
        m_view->viewport()->update();
}
