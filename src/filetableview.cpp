#include "filetableview.h"

#include "directorymodel.h"
#include "theme.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QRubberBand>
#include <QScrollBar>
#include <QUrl>

FileTableView::FileTableView(QWidget *parent)
    : QTableView(parent)
{
    setAcceptDrops(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
    setDragDropOverwriteMode(false);
    setDropIndicatorShown(false);
}

QStringList FileTableView::localFilePaths(const QMimeData *mimeData)
{
    QStringList paths;
    if (!mimeData || !mimeData->hasUrls())
        return paths;
    const QList<QUrl> urls = mimeData->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            const QString path = url.toLocalFile();
            if (!path.isEmpty())
                paths.append(path);
        }
    }
    paths.removeDuplicates();
    return paths;
}

void FileTableView::startDrag(Qt::DropActions supportedActions)
{
    Q_UNUSED(supportedActions)
    if (!selectionModel())
        return;
    QStringList paths;
    const QModelIndexList rows = selectionModel()->selectedRows(0);
    for (const QModelIndex &index : rows) {
        const QString path = index.data(Qt::UserRole).toString();
        if (!path.isEmpty())
            paths.append(path);
    }
    if (paths.isEmpty())
        return;

    auto *mime = new QMimeData();
    QList<QUrl> urls;
    for (const QString &path : paths)
        urls.append(QUrl::fromLocalFile(path));
    mime->setUrls(urls);

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    if (!rows.isEmpty()) {
        const QIcon icon = rows.first().data(Qt::DecorationRole).value<QIcon>();
        const QPixmap pixmap = icon.pixmap(64, 64);
        if (!pixmap.isNull()) {
            drag->setPixmap(pixmap);
            drag->setHotSpot(QPoint(pixmap.width() / 2, pixmap.height() / 2));
        }
    }
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
}

void FileTableView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->source() == this || localFilePaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    m_dropActive = true;
    viewport()->update();
}

void FileTableView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->source() == this || localFilePaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void FileTableView::dragLeaveEvent(QDragLeaveEvent *event)
{
    QTableView::dragLeaveEvent(event);
    m_dropActive = false;
    viewport()->update();
}

void FileTableView::dropEvent(QDropEvent *event)
{
    m_dropActive = false;
    viewport()->update();
    if (event->source() == this) {
        event->ignore();
        return;
    }
    const QStringList paths = localFilePaths(event->mimeData());
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }
    // 只在松开鼠标的这一刻读取 Alt 状态；拖动过程中按下 Alt 也能生效。
    // 默认复制，Alt = 移动；Shift/Ctrl 不再改变动作。
    const bool move = event->keyboardModifiers().testFlag(Qt::AltModifier);
    event->setDropAction(move ? Qt::MoveAction : Qt::CopyAction);
    event->accept();
    emit filesDropped(paths, move ? Qt::MoveAction : Qt::CopyAction);
}

void FileTableView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QModelIndex index = indexAt(event->pos());
        if (index.isValid() && index.column() == DirectoryModel::NameColumn
            && index.data(Qt::UserRole + 4).toBool()) {
            const QRect rect = visualRect(index);
            const int depth = index.data(Qt::UserRole + 2).toInt();
            const int left = rect.left() + depth * 16;
            if (event->pos().x() >= left && event->pos().x() < left + 18) {
                const QString path = index.data(Qt::UserRole).toString();
                if (!path.isEmpty()) {
                    emit folderDisclosureRequested(path);
                    event->accept();
                    return;
                }
            }
        }
        // Rubber-band (marquee) selection: pressing on an unselected row or on
        // empty space bands instead of starting the built-in item drag. Item
        // dragging stays available when pressing an already selected row.
        const bool onSelected = index.isValid() && selectionModel()
            && selectionModel()->isSelected(index);
        if (!onSelected) {
            setDragEnabled(false);
            m_rubberActive = true;
            m_rubberOrigin = event->pos();
            if (!m_rubberBand)
                m_rubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());
            m_rubberBand->setGeometry(QRect(m_rubberOrigin, QSize()));
            m_rubberBand->show();
        }
    }
    QTableView::mousePressEvent(event);
}

void FileTableView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_rubberActive || !m_rubberBand) {
        QTableView::mouseMoveEvent(event);
        return;
    }
    const QPoint pos = event->pos();
    // Auto-scroll when dragging near the viewport edges.
    QScrollBar *scroll = verticalScrollBar();
    constexpr int kEdge = 24;
    if (pos.y() < kEdge)
        scroll->setValue(scroll->value() - (kEdge - pos.y()) / 2);
    else if (pos.y() > viewport()->height() - kEdge)
        scroll->setValue(scroll->value() + (pos.y() - (viewport()->height() - kEdge)) / 2);

    m_rubberBand->setGeometry(QRect(m_rubberOrigin, pos).normalized());

    // Select every row intersecting the band. Row geometry helpers work for
    // scrolled-out rows too, so the selection stays stable while auto-scrolling.
    const QRect band = m_rubberBand->geometry();
    const QAbstractItemModel *tableModel = model();
    QItemSelection selection;
    for (int row = 0; row < tableModel->rowCount(); ++row) {
        const int y = rowViewportPosition(row);
        const int height = rowHeight(row);
        if (height <= 0)
            continue;
        if (y + height < band.top() || y > band.bottom())
            continue;
        selection.select(tableModel->index(row, 0),
                         tableModel->index(row, tableModel->columnCount() - 1));
    }
    if (selectionModel())
        selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect
                                                 | QItemSelectionModel::Rows);
    event->accept();
}

void FileTableView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_rubberActive) {
        m_rubberActive = false;
        if (m_rubberBand)
            m_rubberBand->hide();
        setDragEnabled(true);
    }
    QTableView::mouseReleaseEvent(event);
}

void FileTableView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier) {
        emit previewRequested();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F5 && event->modifiers() == Qt::NoModifier) {
        emit copyToOtherRequested();
        event->accept();
        return;
    }
    QTableView::keyPressEvent(event);
}

void FileTableView::paintEvent(QPaintEvent *event)
{
    QTableView::paintEvent(event);
    if (!m_dropActive)
        return;
    const Theme &theme = Theme::instance();
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen border(theme.accent(), 2, Qt::DashLine);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(viewport()->rect().adjusted(2, 2, -3, -3));

    QFont labelFont = painter.font();
    labelFont.setPixelSize(12);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    const QString label = QStringLiteral("松开以复制到当前文件夹  ·  按住 Alt 移动");
    const QFontMetrics metrics(labelFont);
    const int width = metrics.horizontalAdvance(label) + 28;
    const QRect textRect((viewport()->width() - width) / 2, 10, width, 30);
    painter.setPen(Qt::NoPen);
    QColor backdrop = theme.accentSoft();
    painter.setBrush(backdrop);
    painter.drawRoundedRect(textRect, 8, 8);
    painter.setPen(theme.accent());
    painter.drawText(textRect, Qt::AlignCenter, label);
}
