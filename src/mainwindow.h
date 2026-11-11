#pragma once

#include <QIcon>
#include <QMainWindow>
#include <QPoint>
#include <QPair>
#include "transferconflict.h"

#include <QHash>
#include <QStringList>
#include <QVector>

class FilePane;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QFrame;
class QAction;
class QMenu;
class QProgressBar;
class QSplitter;
class QVBoxLayout;
class TransferManager;
class TransferDialog;
class EjectTask;
class PreviewDialog;
enum class TransferOperation;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &startLeft = QString(),
                        const QString &startRight = QString(),
                        QWidget *parent = nullptr);
    ~MainWindow() override;

    void openPaths(const QString &leftPath, const QString &rightPath);
    void setDarkMode(bool dark);
    // Creates an additional heap-allocated window (WA_DeleteOnClose) and shows it.
    static MainWindow *openNewWindow(const QString &startLeft = QString(),
                                     const QString &startRight = QString());
    FilePane *leftPane() const;
    FilePane *rightPane() const;
    FilePane *activePane() const;
    void revealPath(const QString &path);
    void extractArchive(const QString &archivePath);
    void compressSelection();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct SideEntry {
        QToolButton *button = nullptr;
        QString path;
        QString iconName;
        QIcon customIcon;
        QToolButton *ejectButton = nullptr;
    };

    void buildUi();
    void buildToolbar();
    void buildSidebar();
    void refreshSidebar();
    void refreshDevicesIfChanged();
    QString deviceSignature() const;
    void loadBookmarks();
    QString bookmarkLabel(const QString &path) const;
    void saveBookmarks();
    void toggleBookmark();
    void updateBookmarkButton();
    void toggleHiddenFiles();
    void updateHiddenButton();
    void buildStatusBar();
    void buildTransferBar(QVBoxLayout *rootLayout);
    void openTransferCenter();
    void openTrashDialog();
    void openWithFile(const QString &path);
    void openTerminalAt(const QString &path);
    void showPreviewFile(const QString &path);
    void updateTransferBar();
    void updateTransferTaskButton();
    void buildShortcuts();
    void setSinglePane(bool single, bool persist = true);
    void updateViewModeButton();
    void updateSidebarActive();
    void setActivePane(FilePane *pane);
    FilePane *otherPane(FilePane *pane) const;
    void updateActionStates();
    void updateWindowTitle();
    void applyTheme();
    void addSideButton(const QString &iconName, const QString &label, const QString &path,
                       const QIcon &customIcon = QIcon());
    void addRemovableDeviceRow(const QString &iconName, const QString &label,
                               const QString &mountPath, const QString &devicePath,
                               const QIcon &customIcon);
    void ejectRemovableDevice(const QString &mountPath, const QString &label,
                              const QString &devicePath);
    void showDeviceMenu(const QString &mountPath, const QString &label,
                        const QString &devicePath, const QPoint &globalPos);
    void addSectionLabel(const QString &text);
    void updateToolbarIcons();

    void navigateBack();
    void navigateForward();
    void navigateUp();
    void refreshActive();
    void focusSearch();
    void openLocationDialog();
    void createFolder();
    void createTextFile();
    void renameSelected();
    void copySelectionToClipboard(bool cut);
    void pasteClipboard();
    bool clipboardHasFiles() const;
    void trashSelected();
    void deleteSelectedPermanently();
    void showProperties(const QStringList &paths = QStringList());
    void startTransfer(bool move);
    void showContextMenu(FilePane *pane, const QPoint &globalPos);
    void handleDropTransfer(const QStringList &sources, const QString &destination, bool move);
    bool queueTransfer(TransferOperation operation, const QStringList &sources,
                       const QString &destination, QString *error, bool *canceled);
    bool prepareConflictActions(const QStringList &sources, const QString &destination,
                                ConflictActions *actions);
    void selectAllActive();

    QFrame *m_toolbar = nullptr;
    QToolButton *m_backButton = nullptr;
    QToolButton *m_forwardButton = nullptr;
    QToolButton *m_upButton = nullptr;
    QToolButton *m_refreshButton = nullptr;
    QToolButton *m_newFolderButton = nullptr;
    QToolButton *m_newFileButton = nullptr;
    QToolButton *m_tasksButton = nullptr;
    QToolButton *m_terminalButton = nullptr;
    QToolButton *m_viewModeButton = nullptr;
    QToolButton *m_bookmarkButton = nullptr;
    QToolButton *m_hiddenButton = nullptr;
    QToolButton *m_newWindowButton = nullptr;
    QToolButton *m_searchButton = nullptr;
    QTimer *m_deviceTimer = nullptr;
    EjectTask *m_ejectTask = nullptr;
    QVector<QPair<QToolButton *, QString>> m_toolbarIcons;

    QFrame *m_sidebar = nullptr;
    QSplitter *m_splitter = nullptr;
    QWidget *m_sideContent = nullptr;
    QVBoxLayout *m_sideLayout = nullptr;
    QVector<SideEntry> m_sideEntries;

    FilePane *m_left = nullptr;
    FilePane *m_right = nullptr;
    FilePane *m_active = nullptr;

    TransferManager *m_transfers = nullptr;
    TransferDialog *m_transferDialog = nullptr;
    QFrame *m_transferBar = nullptr;
    QLabel *m_transferTitle = nullptr;
    QLabel *m_transferSubtitle = nullptr;
    QLabel *m_transferPercent = nullptr;
    QLabel *m_transferDetails = nullptr;
    QProgressBar *m_transferProgress = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_cancelButton = nullptr;
    QString m_startLeft;
    QString m_startRight;
    QStringList m_clipboardPaths;
    bool m_clipboardCut = false;
    bool m_singlePane = false;
    bool m_showHidden = false;
    QStringList m_bookmarks;
    QHash<QString, QString> m_bookmarkNames;
    QString m_deviceSignature;
    PreviewDialog *m_previewDialog = nullptr;
};
