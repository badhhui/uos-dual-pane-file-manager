#pragma once

#include <QDialog>
#include <QPixmap>
#include <QStringList>
#include <functional>

struct ArchiveRowList;

class QFrame;
class QLabel;
class QProcess;
class QPushButton;
class QTemporaryDir;
class QVBoxLayout;

class PreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreviewDialog(const QString &path, QWidget *parent = nullptr);
    PreviewDialog(const QStringList &paths, const QString &currentPath,
                  QWidget *parent = nullptr);
    ~PreviewDialog() override;

    QString currentPath() const;
    void setPaths(const QStringList &paths, const QString &currentPath);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void buildShell();
    void loadCurrent();
    bool buildDocumentPreview(const QString &path, const QString &suffix);
    bool buildDocxPreview(const QString &path);
    bool buildXlsxPreview(const QString &path);
    bool buildPptxPreview(const QString &path);
    bool buildOdtPreview(const QString &path);
    bool buildWpsPreview(const QString &path, const QString &suffix);
    bool buildWpsTextFallback(const QString &path);
    bool buildPdfPreview(const QString &path);
    bool buildOfdPreview(const QString &path);
    bool buildArchivePreview(const QString &path, const QString &suffix);
    bool buildImagePreview(const QString &path);
    bool buildTextPreview(const QString &path);
    bool buildCsvPreview(const QString &path, const QString &suffix);
    void buildFallbackPreview(const QString &path, const QString &reason);
    void buildDirectoryPreview(const QString &path);
    void showHtml(const QString &html);
    void showBusySpinner(const QString &text);
    void showPdfPages(const QVector<QPixmap> &pages, int totalPages);
    void showArchiveTable(const ArchiveRowList &rows);
    void clearContent();
    void cancelWpsConversion();
    void cancelPdfRender();
    QString documentStyle() const;

    template<typename T>
    void runAsync(const QString &path, std::function<T()> work,
                  std::function<void(const T &)> render);

    QStringList m_paths;
    int m_index = 0;
    int m_generation = 0;
    QString m_path;
    QFrame *m_content = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    QLabel *m_iconLabel = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_metaLabel = nullptr;
    QPushButton *m_openButton = nullptr;
    QProcess *m_wpsProcess = nullptr;
    QTemporaryDir *m_wpsTemp = nullptr;
    QString m_wpsPendingPath;
    QProcess *m_pdfProcess = nullptr;
    QTemporaryDir *m_pdfTemp = nullptr;
    QString m_pdfPendingPath;
};
