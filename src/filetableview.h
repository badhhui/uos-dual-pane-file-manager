#pragma once

#include <QTableView>

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

private:
    static QStringList localFilePaths(const QMimeData *mimeData);
    bool m_dropActive = false;
};
