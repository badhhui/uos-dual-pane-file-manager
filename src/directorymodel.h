#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QString>
#include <QHash>
#include <QSet>
#include <QVector>

class IconProvider;

struct FileEntry
{
    QString name;
    QString path;
    QString suffix;
    bool isDir = false;
    bool isSymLink = false;
    bool isDesktopFile = false;
    qint64 size = 0;
    QDateTime modified;
    QString displayName;
    QString desktopIcon;
    QString desktopExec;
};

class DirectoryModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        NameColumn = 0,
        SizeColumn,
        DateColumn,
        ColumnCount
    };

    explicit DirectoryModel(IconProvider *icons, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    void setDirectory(const QString &path, bool force = false);
    QString directory() const;
    bool isLoading() const;
    QString lastError() const;
    const QVector<FileEntry> &visibleEntries() const;
    bool entryAt(int row, FileEntry *out) const;
    int rowForPath(const QString &path) const;
    void setFilter(const QString &text);
    QString filter() const;
    int totalEntryCount() const;
    void setSearchMode(const QString &rootPath, const QString &query, bool showHidden);
    void setFullTextResults(const QString &rootPath, const QString &query,
                           const QVector<FileEntry> &entries);
    bool isSearchMode() const;
    bool isFullTextSearch() const;
    QString searchQuery() const;
    bool toggleFolderExpanded(int row);
    bool isFolderExpanded(int row) const;
    int depthForRow(int row) const;
    bool canExpandRow(int row) const;
    void setShowHidden(bool show);
    bool showHidden() const;

    // Called from the background loader on the application thread.
    void applyLoadResult(quint64 token, const QString &path,
                         const QVector<FileEntry> &entries, const QString &error);
    void applySearchResult(quint64 token, const QString &path,
                           const QVector<FileEntry> &entries, const QString &error);

signals:
    void loadingChanged(bool loading);
    void directoryLoaded(const QString &path);
    void loadFailed(const QString &path, const QString &error);
    void searchFinished(const QString &query, int count);

private:
    void rebuildVisible();
    void appendTreeEntry(const FileEntry &entry, int depth);
    QVector<FileEntry> loadChildren(const QString &path) const;
    static bool compareEntries(const FileEntry &left, const FileEntry &right,
                               int column, Qt::SortOrder order);
    IconProvider *m_icons;
    QString m_path;
    QString m_filter;
    QString m_error;
    QVector<FileEntry> m_allEntries;
    QVector<FileEntry> m_visibleEntries;
    QVector<int> m_rowDepths;
    QSet<QString> m_expandedPaths;
    QHash<QString, QVector<FileEntry>> m_expandedChildren;
    quint64 m_token = 0;
    bool m_loading = false;
    bool m_showHidden = false;
    bool m_searchMode = false;
    QString m_searchQuery;
    bool m_fullTextSearch = false;
    int m_sortColumn = NameColumn;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};
