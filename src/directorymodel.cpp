#include "directorymodel.h"

#include "desktopentry.h"

#include "formatters.h"
#include "iconprovider.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QMetaObject>
#include <algorithm>
#include <memory>

namespace {
struct LoadResult
{
    quint64 token = 0;
    QString path;
    QVector<FileEntry> entries;
    QString error;
};

class DirectoryLoader : public QRunnable
{
public:
    DirectoryLoader(QPointer<DirectoryModel> model, quint64 token, QString path, bool showHidden)
        : m_model(model), m_token(token), m_path(path), m_showHidden(showHidden)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        auto result = std::make_shared<LoadResult>();
        result->token = m_token;
        result->path = m_path;

        QFileInfo dirInfo(m_path);
        if (!dirInfo.exists()) {
            result->error = QStringLiteral("文件夹不存在：%1").arg(m_path);
        } else if (!dirInfo.isDir()) {
            result->error = QStringLiteral("目标不是文件夹：%1").arg(m_path);
        } else if (!dirInfo.isReadable()) {
            result->error = QStringLiteral("没有权限读取此文件夹");
        } else {
            QDir directory(m_path);
            QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
            if (m_showHidden)
                filters |= QDir::Hidden;
            const QFileInfoList list = directory.entryInfoList(filters, QDir::NoSort);
            result->entries.reserve(list.size());
            for (const QFileInfo &info : list) {
                if (info.fileName() == QStringLiteral(".") || info.fileName() == QStringLiteral(".."))
                    continue;
                FileEntry entry;
                entry.name = info.fileName();
                entry.path = info.absoluteFilePath();
                entry.suffix = info.suffix().toLower();
                entry.isDir = info.isDir();
                entry.isSymLink = info.isSymLink();
                entry.size = entry.isDir ? 0 : info.size();
                entry.modified = info.lastModified();
                if (!entry.isDir && entry.suffix == QStringLiteral("desktop")) {
                    const DesktopEntry desktop = readDesktopEntry(entry.path);
                    if (desktop.valid) {
                        entry.isDesktopFile = true;
                        entry.displayName = desktop.name;
                        entry.desktopIcon = desktop.icon;
                        entry.desktopExec = desktop.exec;
                    }
                }
                result->entries.append(entry);
            }
        }

        QCoreApplication *app = QCoreApplication::instance();
        if (!app)
            return;
        QPointer<DirectoryModel> guard = m_model;
        QMetaObject::invokeMethod(app, [guard, result]() {
            if (guard)
                guard->applyLoadResult(result->token, result->path, result->entries, result->error);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<DirectoryModel> m_model;
    quint64 m_token;
    QString m_path;
    bool m_showHidden = false;
};

class SearchLoader : public QRunnable
{
public:
    SearchLoader(QPointer<DirectoryModel> model, quint64 token, QString root,
                 QString query, bool showHidden)
        : m_model(model), m_token(token), m_root(root), m_query(query), m_showHidden(showHidden)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        auto result = std::make_shared<LoadResult>();
        result->token = m_token;
        result->path = m_root;
        QFileInfo rootInfo(m_root);
        if (!rootInfo.exists() || !rootInfo.isDir()) {
            result->error = QStringLiteral("文件夹不存在：%1").arg(m_root);
        } else if (!rootInfo.isReadable()) {
            result->error = QStringLiteral("没有权限读取此文件夹");
        } else {
            QDir rootDir(m_root);
            QDir::Filters filters = QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot
                | QDir::NoSymLinks;
            if (m_showHidden)
                filters |= QDir::Hidden;
            QDirIterator iterator(m_root, filters, QDirIterator::Subdirectories);
            while (iterator.hasNext() && result->entries.size() < 5000) {
                const QString path = iterator.next();
                const QFileInfo info(path);
                if (!info.fileName().contains(m_query, Qt::CaseInsensitive))
                    continue;
                FileEntry entry;
                entry.name = info.fileName();
                entry.path = path;
                entry.suffix = info.suffix().toLower();
                entry.isDir = info.isDir();
                entry.isSymLink = info.isSymLink();
                entry.size = entry.isDir ? 0 : info.size();
                entry.modified = info.lastModified();
                entry.displayName.clear();
                if (!entry.isDir && entry.suffix == QStringLiteral("desktop")) {
                    const DesktopEntry desktop = readDesktopEntry(entry.path);
                    if (desktop.valid) {
                        entry.isDesktopFile = true;
                        entry.displayName = desktop.name;
                        entry.desktopIcon = desktop.icon;
                        entry.desktopExec = desktop.exec;
                    }
                }
                result->entries.append(entry);
            }
        }
        QCoreApplication *app = QCoreApplication::instance();
        if (!app)
            return;
        QPointer<DirectoryModel> guard = m_model;
        QMetaObject::invokeMethod(app, [guard, result]() {
            if (guard)
                guard->applySearchResult(result->token, result->path, result->entries, result->error);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<DirectoryModel> m_model;
    quint64 m_token;
    QString m_root;
    QString m_query;
    bool m_showHidden = false;
};
}

DirectoryModel::DirectoryModel(IconProvider *icons, QObject *parent)
    : QAbstractTableModel(parent)
    , m_icons(icons)
{
}

int DirectoryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_visibleEntries.size();
}

int DirectoryModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant DirectoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleEntries.size())
        return QVariant();
    const FileEntry &entry = m_visibleEntries.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case NameColumn:
            return entry.displayName.isEmpty() ? entry.name : entry.displayName;
        case SizeColumn:
            return entry.isDir ? QStringLiteral("—") : formatSize(entry.size);
        case DateColumn:
            return formatDateTime(entry.modified);
        default:
            return QVariant();
        }
    }
    if (role == Qt::DecorationRole && index.column() == NameColumn) {
        if (entry.isDesktopFile)
            return m_icons->desktopIcon(entry.desktopIcon);
        return m_icons->fileIcon(entry.isDir, entry.suffix);
    }
    if (role == Qt::TextAlignmentRole) {
        if (index.column() == SizeColumn)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (role == Qt::ToolTipRole) {
        if (entry.isDesktopFile && !entry.displayName.isEmpty())
            return entry.displayName;
        return entry.name;
    }
    if (role == Qt::UserRole)
        return entry.path;
    if (role == Qt::UserRole + 1)
        return entry.isDir;
    if (role == Qt::UserRole + 2)
        return index.row() < m_rowDepths.size() ? m_rowDepths.at(index.row()) : 0;
    if (role == Qt::UserRole + 3)
        return !m_searchMode && m_expandedPaths.contains(entry.path);
    if (role == Qt::UserRole + 4)
        return !m_searchMode && entry.isDir;
    return QVariant();
}

QVariant DirectoryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();
    switch (section) {
    case NameColumn:
        return QStringLiteral("名称");
    case SizeColumn:
        return QStringLiteral("大小");
    case DateColumn:
        return QStringLiteral("修改日期");
    default:
        return QVariant();
    }
}

Qt::ItemFlags DirectoryModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

void DirectoryModel::setDirectory(const QString &path, bool force)
{
    if (!force && path == m_path && (m_loading || !m_allEntries.isEmpty()))
        return;
    const QString clean = QDir::cleanPath(path);
    m_token += 1;
    const quint64 token = m_token;
    m_path = clean;
    m_searchMode = false;
    m_fullTextSearch = false;
    m_searchQuery.clear();
    m_error.clear();
    m_allEntries.clear();
    m_visibleEntries.clear();
    m_rowDepths.clear();
    m_expandedPaths.clear();
    m_expandedChildren.clear();
    beginResetModel();
    endResetModel();
    m_loading = true;
    emit loadingChanged(true);

    auto *loader = new DirectoryLoader(QPointer<DirectoryModel>(this), token, clean, m_showHidden);
    QThreadPool::globalInstance()->start(loader);
}

QString DirectoryModel::directory() const
{
    return m_path;
}

bool DirectoryModel::isLoading() const
{
    return m_loading;
}

QString DirectoryModel::lastError() const
{
    return m_error;
}

const QVector<FileEntry> &DirectoryModel::visibleEntries() const
{
    return m_visibleEntries;
}

bool DirectoryModel::entryAt(int row, FileEntry *out) const
{
    if (!out || row < 0 || row >= m_visibleEntries.size())
        return false;
    *out = m_visibleEntries.at(row);
    return true;
}

int DirectoryModel::rowForPath(const QString &path) const
{
    for (int i = 0; i < m_visibleEntries.size(); ++i) {
        if (m_visibleEntries.at(i).path == path)
            return i;
    }
    return -1;
}

void DirectoryModel::setFilter(const QString &text)
{
    const QString normalized = text.trimmed();
    if (normalized == m_filter)
        return;
    m_filter = normalized;
    beginResetModel();
    rebuildVisible();
    endResetModel();
}

QString DirectoryModel::filter() const
{
    return m_filter;
}

int DirectoryModel::totalEntryCount() const
{
    return m_allEntries.size();
}

void DirectoryModel::setSearchMode(const QString &rootPath, const QString &query,
                                     bool showHidden)
{
    const QString clean = QDir::cleanPath(rootPath);
    m_showHidden = showHidden;
    m_token += 1;
    const quint64 token = m_token;
    m_path = clean;
    m_searchMode = true;
    m_fullTextSearch = false;
    m_searchQuery = query;
    m_error.clear();
    m_allEntries.clear();
    m_visibleEntries.clear();
    m_rowDepths.clear();
    m_expandedPaths.clear();
    m_expandedChildren.clear();
    beginResetModel();
    endResetModel();
    m_loading = true;
    emit loadingChanged(true);
    auto *loader = new SearchLoader(QPointer<DirectoryModel>(this), token, clean, query, showHidden);
    QThreadPool::globalInstance()->start(loader);
}

bool DirectoryModel::isSearchMode() const
{
    return m_searchMode;
}

QString DirectoryModel::searchQuery() const
{
    return m_searchQuery;
}

void DirectoryModel::applySearchResult(quint64 token, const QString &path,
                                       const QVector<FileEntry> &entries, const QString &error)
{
    if (token != m_token || path != m_path)
        return;
    m_loading = false;
    if (!error.isEmpty()) {
        m_error = error;
        beginResetModel();
        m_allEntries.clear();
        m_visibleEntries.clear();
        endResetModel();
        emit loadingChanged(false);
        emit loadFailed(path, error);
        return;
    }
    beginResetModel();
    m_error.clear();
    m_expandedPaths.clear();
    m_expandedChildren.clear();
    m_rowDepths.clear();
    m_allEntries = entries;
    std::sort(m_allEntries.begin(), m_allEntries.end(), [this](const FileEntry &a, const FileEntry &b) {
        return compareEntries(a, b, m_sortColumn, m_sortOrder);
    });
    for (auto it = m_expandedChildren.begin(); it != m_expandedChildren.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(),
                  [this](const FileEntry &a, const FileEntry &b) {
            return compareEntries(a, b, m_sortColumn, m_sortOrder);
        });
    }
    rebuildVisible();
    endResetModel();
    emit loadingChanged(false);
    emit directoryLoaded(path);
    emit searchFinished(m_searchQuery, m_allEntries.size());
}

void DirectoryModel::setFullTextResults(const QString &rootPath, const QString &query,
                                       const QVector<FileEntry> &entries)
{
    m_token += 1;
    m_path = QDir::cleanPath(rootPath);
    m_searchMode = true;
    m_fullTextSearch = true;
    m_searchQuery = query;
    m_loading = false;
    beginResetModel();
    m_error.clear();
    m_expandedPaths.clear();
    m_expandedChildren.clear();
    m_rowDepths.clear();
    m_allEntries = entries;
    std::sort(m_allEntries.begin(), m_allEntries.end(), [this](const FileEntry &a, const FileEntry &b) {
        return compareEntries(a, b, m_sortColumn, m_sortOrder);
    });
    for (auto it = m_expandedChildren.begin(); it != m_expandedChildren.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(),
                  [this](const FileEntry &a, const FileEntry &b) {
            return compareEntries(a, b, m_sortColumn, m_sortOrder);
        });
    }
    rebuildVisible();
    endResetModel();
    emit loadingChanged(false);
    emit directoryLoaded(m_path);
    emit searchFinished(m_searchQuery, m_allEntries.size());
}

bool DirectoryModel::isFullTextSearch() const
{
    return m_fullTextSearch;
}

void DirectoryModel::setShowHidden(bool show)
{
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    if (!m_path.isEmpty()) {
        if (m_searchMode && !m_fullTextSearch)
            setSearchMode(m_path, m_searchQuery, show);
        else if (!m_fullTextSearch)
            setDirectory(m_path, true);
    }
}

bool DirectoryModel::showHidden() const
{
    return m_showHidden;
}

void DirectoryModel::applyLoadResult(quint64 token, const QString &path,
                                     const QVector<FileEntry> &entries, const QString &error)
{
    if (token != m_token || path != m_path)
        return;
    m_loading = false;
    if (!error.isEmpty()) {
        m_error = error;
        beginResetModel();
        m_allEntries.clear();
        m_visibleEntries.clear();
        endResetModel();
        emit loadingChanged(false);
        emit loadFailed(path, error);
        return;
    }
    beginResetModel();
    m_error.clear();
    m_expandedPaths.clear();
    m_expandedChildren.clear();
    m_rowDepths.clear();
    m_allEntries = entries;
    std::sort(m_allEntries.begin(), m_allEntries.end(), [this](const FileEntry &a, const FileEntry &b) {
        return compareEntries(a, b, m_sortColumn, m_sortOrder);
    });
    for (auto it = m_expandedChildren.begin(); it != m_expandedChildren.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(),
                  [this](const FileEntry &a, const FileEntry &b) {
            return compareEntries(a, b, m_sortColumn, m_sortOrder);
        });
    }
    rebuildVisible();
    endResetModel();
    emit loadingChanged(false);
    emit directoryLoaded(path);
}

void DirectoryModel::rebuildVisible()
{
    m_visibleEntries.clear();
    m_rowDepths.clear();
    m_visibleEntries.reserve(m_allEntries.size());
    const QString needle = m_filter.toCaseFolded();
    for (const FileEntry &entry : m_allEntries) {
        const bool matchName = entry.name.toCaseFolded().contains(needle);
        const bool matchDisplay = !entry.displayName.isEmpty()
            && entry.displayName.toCaseFolded().contains(needle);
        if (needle.isEmpty() || matchName || matchDisplay)
            appendTreeEntry(entry, 0);
    }
}

void DirectoryModel::appendTreeEntry(const FileEntry &entry, int depth)
{
    m_visibleEntries.append(entry);
    m_rowDepths.append(depth);
    if (m_searchMode || !entry.isDir || depth >= 32
        || !m_expandedPaths.contains(entry.path))
        return;
    const QVector<FileEntry> children = m_expandedChildren.value(entry.path);
    for (const FileEntry &child : children) {
        const QString needle = m_filter.toCaseFolded();
        if (!needle.isEmpty()
            && !child.name.toCaseFolded().contains(needle)
            && !child.displayName.toCaseFolded().contains(needle))
            continue;
        appendTreeEntry(child, depth + 1);
    }
}

QVector<FileEntry> DirectoryModel::loadChildren(const QString &path) const
{
    QVector<FileEntry> entries;
    QDir directory(path);
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
    if (m_showHidden)
        filters |= QDir::Hidden;
    const QFileInfoList list = directory.entryInfoList(filters, QDir::NoSort);
    entries.reserve(list.size());
    for (const QFileInfo &info : list) {
        FileEntry entry;
        entry.name = info.fileName();
        entry.path = info.absoluteFilePath();
        entry.suffix = info.suffix().toLower();
        entry.isDir = info.isDir();
        entry.isSymLink = info.isSymLink();
        entry.size = entry.isDir ? 0 : info.size();
        entry.modified = info.lastModified();
        if (!entry.isDir && entry.suffix == QStringLiteral("desktop")) {
            const DesktopEntry desktop = readDesktopEntry(entry.path);
            if (desktop.valid) {
                entry.isDesktopFile = true;
                entry.displayName = desktop.name;
                entry.desktopIcon = desktop.icon;
                entry.desktopExec = desktop.exec;
            }
        }
        entries.append(entry);
    }
    std::sort(entries.begin(), entries.end(), [this](const FileEntry &a, const FileEntry &b) {
        return compareEntries(a, b, m_sortColumn, m_sortOrder);
    });
    return entries;
}

bool DirectoryModel::toggleFolderExpanded(int row)
{
    if (row < 0 || row >= m_visibleEntries.size() || m_searchMode)
        return false;
    const FileEntry entry = m_visibleEntries.at(row);
    if (!entry.isDir)
        return false;
    const bool expand = !m_expandedPaths.contains(entry.path);
    if (expand) {
        if (!m_expandedChildren.contains(entry.path))
            m_expandedChildren.insert(entry.path, loadChildren(entry.path));
        m_expandedPaths.insert(entry.path);
    } else {
        m_expandedPaths.remove(entry.path);
        const QString prefix = entry.path + QLatin1Char('/');
        for (auto it = m_expandedPaths.begin(); it != m_expandedPaths.end();) {
            if (it->startsWith(prefix))
                it = m_expandedPaths.erase(it);
            else
                ++it;
        }
    }
    beginResetModel();
    rebuildVisible();
    endResetModel();
    return expand;
}

bool DirectoryModel::isFolderExpanded(int row) const
{
    if (row < 0 || row >= m_visibleEntries.size() || m_searchMode)
        return false;
    return m_expandedPaths.contains(m_visibleEntries.at(row).path);
}

int DirectoryModel::depthForRow(int row) const
{
    return row >= 0 && row < m_rowDepths.size() ? m_rowDepths.at(row) : 0;
}

bool DirectoryModel::canExpandRow(int row) const
{
    return row >= 0 && row < m_visibleEntries.size() && !m_searchMode
        && m_visibleEntries.at(row).isDir;
}

void DirectoryModel::sort(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= ColumnCount)
        return;
    m_sortColumn = column;
    m_sortOrder = order;
    if (m_allEntries.isEmpty())
        return;
    beginResetModel();
    std::sort(m_allEntries.begin(), m_allEntries.end(), [this](const FileEntry &a, const FileEntry &b) {
        return compareEntries(a, b, m_sortColumn, m_sortOrder);
    });
    for (auto it = m_expandedChildren.begin(); it != m_expandedChildren.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(),
                  [this](const FileEntry &a, const FileEntry &b) {
            return compareEntries(a, b, m_sortColumn, m_sortOrder);
        });
    }
    rebuildVisible();
    endResetModel();
}

bool DirectoryModel::compareEntries(const FileEntry &left, const FileEntry &right,
                                    int column, Qt::SortOrder order)
{
    if (left.isDir != right.isDir)
        return left.isDir;
    int result = 0;
    switch (column) {
    case SizeColumn:
        if (left.size != right.size)
            result = left.size < right.size ? -1 : 1;
        break;
    case DateColumn:
        if (left.modified != right.modified)
            result = left.modified < right.modified ? -1 : 1;
        break;
    case NameColumn:
    default: {
        const QString leftName = left.displayName.isEmpty() ? left.name : left.displayName;
        const QString rightName = right.displayName.isEmpty() ? right.name : right.displayName;
        result = QString::localeAwareCompare(leftName, rightName);
        if (result == 0)
            result = QString::compare(leftName, rightName, Qt::CaseInsensitive);
        if (result == 0)
            result = QString::localeAwareCompare(left.name, right.name);
        break;
    }
    }
    if (result == 0)
        result = QString::localeAwareCompare(left.name, right.name);
    if (order == Qt::DescendingOrder)
        result = -result;
    return result < 0;
}
