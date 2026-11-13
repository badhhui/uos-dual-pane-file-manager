#include "fileiconview.h"

#include "directorymodel.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QRubberBand>
#include <QScrollBar>
#include <QUrl>

FileIconView::FileIconView(QWidget *parent)
    : QListView(parent)
{
    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setWordWrap(false);
    setUniformItemSizes(true);
    setIconSize(QSize(48, 48));
    setGridSize(QSize(108, 104));
    setSpacing(4);
    setTextElideMode(Qt::ElideMiddle);
    setFrameShape(QFrame::NoFrame);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setAcceptDrops(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
    setDragDropOverwriteMode(false);
    setDropIndicatorShown(false);
    setObjectName(QStringLiteral("FileIconGrid"));
}

QStringList FileIconView::localFilePaths(const QMimeData *mimeData)
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

void FileIconView::startDrag(Qt::DropActions supportedActions)
{
    Q_UNUSED(supportedActions)
    if (!selectionModel())
        return;
    QStringList paths;
    const QModelIndexList selected = selectionModel()->selectedIndexes();
    for (const QModelIndex &index : selected) {
        const QString path = index.data(Qt::UserRole).toString();
        if (!path.isEmpty())
            paths.append(path);
    }
    paths.removeDuplicates();
    if (paths.isEmpty())
        return;

    auto *mime = new QMimeData();
    QList<QUrl> urls;
    for (const QString &path : paths)
        urls.append(QUrl::fromLocalFile(path));
    mime->setUrls(urls);

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    if (!selected.isEmpty()) {
        const QIcon icon = selected.first().data(Qt::DecorationRole).value<QIcon>();
        const QPixmap pixmap = icon.pixmap(64, 64);
        if (!pixmap.isNull()) {
            drag->setPixmap(pixmap);
            drag->setHotSpot(QPoint(pixmap.width() / 2, pixmap.height() / 2));
        }
    }
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
}

void FileIconView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->source() == this || localFilePaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    viewport()->update();
}

void FileIconView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->source() == this || localFilePaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void FileIconView::dragLeaveEvent(QDragLeaveEvent *event)
{
    QListView::dragLeaveEvent(event);
    viewport()->update();
}

void FileIconView::dropEvent(QDropEvent *event)
{
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
    event->acceptProposedAction();
    emit filesDropped(paths, event->dropAction());
}

void FileIconView::keyPressEvent(QKeyEvent *event)
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
    QListView::keyPressEvent(event);
}

void FileIconView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QModelIndex index = indexAt(event->pos());
        // Shift/Ctrl keep the native selection gestures; a plain drag from an
        // unselected item or empty space starts the rubber band instead of an
        // item drag (same rule as the list view).
        const bool onSelected = index.isValid() && selectionModel()
            && selectionModel()->isSelected(index);
        const bool plainClick = event->modifiers() == Qt::NoModifier;
        if (!onSelected && plainClick) {
            setDragEnabled(false);
            m_rubberActive = true;
            m_rubberOriginContent = QPoint(
                event->pos().x() + (horizontalScrollBar() ? horizontalScrollBar()->value() : 0),
                event->pos().y() + (verticalScrollBar() ? verticalScrollBar()->value() : 0));
            if (!m_rubberBand)
                m_rubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());
            m_rubberBand->setGeometry(QRect(event->pos(), QSize()));
            m_rubberBand->show();
        }
    }
    QListView::mousePressEvent(event);
}

void FileIconView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_rubberActive || !m_rubberBand) {
        QListView::mouseMoveEvent(event);
        return;
    }
    const QPoint pos = event->pos();
    QScrollBar *scroll = verticalScrollBar();
    constexpr int kEdge = 24;
    if (pos.y() < kEdge)
        scroll->setValue(scroll->value() - (kEdge - pos.y()) / 2);
    else if (pos.y() > viewport()->height() - kEdge)
        scroll->setValue(scroll->value() + (pos.y() - (viewport()->height() - kEdge)) / 2);

    const int hScroll = horizontalScrollBar() ? horizontalScrollBar()->value() : 0;
    const int vScroll = scroll->value();
    const QPoint contentPos(pos.x() + hScroll, pos.y() + vScroll);
    const QRect contentBand = QRect(m_rubberOriginContent, contentPos).normalized();
    m_rubberBand->setGeometry(contentBand.translated(-hScroll, -vScroll));
    applyRubberBandSelection();
    event->accept();
}

void FileIconView::applyRubberBandSelection()
{
    if (!m_rubberBand || !selectionModel() || !model())
        return;
    const int hScroll = horizontalScrollBar() ? horizontalScrollBar()->value() : 0;
    const int vScroll = verticalScrollBar() ? verticalScrollBar()->value() : 0;
    const QRect contentBand = m_rubberBand->geometry().translated(hScroll, vScroll);
    const QAbstractItemModel *listModel = model();
    QItemSelection selection;
    for (int row = 0; row < listModel->rowCount(); ++row) {
        const QModelIndex index = listModel->index(row, 0);
        const QRect itemRect = visualRect(index);
        if (!itemRect.isValid())
            continue;
        const QRect contentRect = itemRect.translated(hScroll, vScroll);
        if (contentRect.intersects(contentBand))
            selection.select(index, index);
    }
    selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
}

void FileIconView::mouseReleaseEvent(QMouseEvent *event)
{
    const bool finishingBand = m_rubberActive;
    if (finishingBand) {
        m_rubberActive = false;
        if (m_rubberBand)
            m_rubberBand->hide();
        setDragEnabled(true);
    }
    QListView::mouseReleaseEvent(event);
    if (finishingBand) {
        // The base release may re-select based on the release position (or clear
        // when it lands outside any item); re-assert the band selection.
        applyRubberBandSelection();
    }
}
