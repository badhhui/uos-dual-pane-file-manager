#pragma once

#include <QListView>

class QRubberBand;

// Icon-grid presentation of the file list. Shares the DirectoryModel and the
// selection model with the table view, so selection, operations and shortcuts
// behave identically in both modes.
class FileIconView : public QListView
{
    Q_OBJECT

public:
    explicit FileIconView(QWidget *parent = nullptr);

signals:
    void filesDropped(const QStringList &paths, Qt::DropAction action);
    void copyToOtherRequested();
    void previewRequested();

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    static QStringList localFilePaths(const QMimeData *mimeData);
    void applyRubberBandSelection();
    QRubberBand *m_rubberBand = nullptr;
    QPoint m_rubberOriginContent;
    bool m_rubberActive = false;
};
