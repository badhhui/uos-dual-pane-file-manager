#pragma once

#include "directorymodel.h"
#include "fulltextsearch.h"

#include <QFrame>
#include <QSet>
#include <QStringList>
#include <QVector>

class DirectoryModel;
class FileHeaderView;
class FileRowDelegate;
class FileTableView;
class IconProvider;
class QLabel;
class QModelIndex;
class QMouseEvent;
class QPoint;
class QTableView;
class QToolButton;
class QVBoxLayout;
class QVariantAnimation;
class QResizeEvent;
class QHBoxLayout;
class QFileSystemWatcher;
class QTimer;
class QLineEdit;
class QComboBox;
class QWidget;

class FilePane : public QFrame
{
    Q_OBJECT

public:
    explicit FilePane(IconProvider *icons, QWidget *parent = nullptr);
    ~FilePane() override;

    bool navigateTo(const QString &path, bool addHistory = true);
    void goBack();
    void goForward();
    void goUp();
    void refresh();
    void revealPath(const QString &path);
    void setActive(bool active);
    void setHeaderHighlight(bool enabled);
    void setShowHidden(bool show);
    void beginSearch();
    void endSearch();
    bool isSearching() const;
    void applyFullTextResults(quint64 token, const QString &query,
                              const QVector<FullTextHit> &hits, const QString &error);
    void showContentSearchRequirement(const WssScopeStatus &status);
    void setMountEjecting(const QString &mountPath);
    void clearMountEjecting();
    bool showHidden() const;
    bool isActive() const;
    void updateTheme();

    QString currentPath() const;
    QString displayPath() const;
    QStringList selectedPaths() const;
    QVector<FileEntry> selectedEntries() const;
    int selectedCount() const;
    qint64 selectedFileBytes() const;
    bool hasSelection() const;

    bool canGoBack() const;
    bool canGoForward() const;
    bool canGoUp() const;
    QString searchFilter() const;
    void setSearchFilter(const QString &text);
    void focusView();
    void openSelected();
    DirectoryModel *directoryModel() const;
    QTableView *tableView() const;

    void showContextMenuAt(const QPoint &globalPos);
    void flashClipboardFeedback(bool cut);

signals:
    void activationRequested(FilePane *pane);
    void pathChanged(FilePane *pane);
    void selectionChanged(FilePane *pane);
    void navigationChanged(FilePane *pane);
    void statusMessage(const QString &message);
    void filterChanged(FilePane *pane);
    void contextMenuRequested(FilePane *pane, const QPoint &globalPos);
    void newFolderRequested(FilePane *pane);
    void propertiesRequested(FilePane *pane, const QString &path);
    void deleteRequested(FilePane *pane);
    void renameRequested(FilePane *pane);
    void copyRequested(FilePane *pane);
    void cutRequested(FilePane *pane);
    void pasteRequested(FilePane *pane);
    void dropTransferRequested(const QStringList &sources, const QString &destination, bool move);
    void copyToOtherPaneRequested(FilePane *pane);
    void previewRequested(FilePane *pane, const QString &path);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onDoubleClicked(const QModelIndex &index);
    void onSelectionChanged();
    void onDirectoryLoaded(const QString &path);
    void onLoadFailed(const QString &path, const QString &error);
    void onLoadingChanged(bool loading);
    void onHeaderClicked(int logicalIndex);

private:
    void buildUi();
    void loadPath(const QString &path);
    void updateBreadcrumbs();
    void updateFooter();
    void updateSelectionLabels();
    void clearBreadcrumbLayout();
    void updateLocationIcon();
    void appendToHistory(const QString &path);
    void setSort(int column, Qt::SortOrder order);
    void updateColumnWidths();
    bool openEntry(const QModelIndex &index);
    void applyActiveStyle();
    void updateWatcher();
    void captureRefreshState();
    void restoreRefreshState();
    void startRecursiveSearch();
    void startFullTextSearch();
    void runSearch();
    void selectPendingReveal();
    void toggleFolderDisclosure(const QString &path);
    void hideSearchBar();

    IconProvider *m_icons;
    DirectoryModel *m_model;
    FileTableView *m_view;
    FileHeaderView *m_header;
    FileRowDelegate *m_delegate;
    QFrame *m_headerFrame;
    QLabel *m_locationIcon;
    QHBoxLayout *m_breadcrumbLayout;
    QWidget *m_breadcrumbHost;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_searchMode = nullptr;
    QToolButton *m_searchClose = nullptr;
    QTimer *m_searchDebounce = nullptr;
    bool m_searchActive = false;
    bool m_searchFullText = false;
    bool m_fullTextLoading = false;
    quint64 m_fullTextToken = 0;
    QString m_searchNotice;
    QString m_ejectingMount;
    QString m_pendingRevealPath;
    QTimer *m_revealTimer = nullptr;
    int m_revealAttempts = 0;
    QLabel *m_countLabel;
    QLabel *m_selectionLabel;
    QString m_path;
    QString m_searchFilter;
    QVector<QString> m_history;
    int m_historyIndex = -1;
    bool m_active = false;
    bool m_headerHighlight = true;
    bool m_loading = false;
    int m_sortColumn = 0;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    QVariantAnimation *m_clipboardAnimation = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QStringList m_restorePaths;
    QString m_restoreCurrent;
    int m_restoreScroll = -1;
    bool m_columnsCustomized = false;
    bool m_adjustingColumns = false;
};
