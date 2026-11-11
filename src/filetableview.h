#pragma once

#include <QTableView>

class QRubberBand;

class FileTableView : public QTableView
{
    Q_OBJECT

public:
    explicit FileTableView(QWidget *parent = nullptr);

signals:
    void filesDropped(const QStringList &paths, Qt::DropAction action);
    void copyToOtherRequested();
    void previewRequested();
    void folderDisclosureRequested(const QString &path);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    static QStringList localFilePaths(const QMimeData *mimeData);
    void applyRubberBandSelection();
    bool m_dropActive = false;
    // Rubber-band (marquee) selection state. Pressing on an unselected row or
    // empty space starts a band; pressing on an already selected row keeps the
    // built-in item drag for moving files to the other pane.
    QRubberBand *m_rubberBand = nullptr;
    QPoint m_rubberOrigin;
    bool m_rubberActive = false;
};
