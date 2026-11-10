#include "fileheaderview.h"

#include "directorymodel.h"
#include "theme.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPolygonF>

FileHeaderView::FileHeaderView(Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent)
{
    setSectionsClickable(true);
    setHighlightSections(false);
    setSortIndicatorShown(false);
    setFixedHeight(34);
}

void FileHeaderView::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    if (!rect.isValid() || !model())
        return;
    const Theme &theme = Theme::instance();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->fillRect(rect, theme.surface());
    painter->setPen(theme.line());
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());
    painter->setPen(theme.lineStrong());
    painter->drawLine(QPointF(rect.right(), rect.top() + 7),
                      QPointF(rect.right(), rect.bottom() - 7));

    QFont font = painter->font();
    font.setPixelSize(11);
    font.setWeight(QFont::Medium);
    painter->setFont(font);
    painter->setPen(theme.textMuted());
    const QString text = model()->headerData(logicalIndex, Qt::Horizontal, Qt::DisplayRole).toString();

    if (logicalIndex == DirectoryModel::NameColumn) {
        const QRect textRect = rect.adjusted(46, 0, -12, 0);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
    } else if (logicalIndex == DirectoryModel::SizeColumn) {
        const QRect textRect = rect.adjusted(8, 0, -14, 0);
        painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, text);
    } else {
        const QRect textRect = rect.adjusted(10, 0, -10, 0);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }

    if (isSortIndicatorShown() && sortIndicatorSection() == logicalIndex) {
        const bool ascending = sortIndicatorOrder() == Qt::AscendingOrder;
        const int x = logicalIndex == DirectoryModel::SizeColumn
            ? rect.left() + 8
            : rect.left() + 12;
        const QPointF center(x, rect.center().y());
        QPolygonF arrow;
        if (ascending)
            arrow << QPointF(center.x() - 4, center.y() + 2)
                  << QPointF(center.x() + 4, center.y() + 2)
                  << QPointF(center.x(), center.y() - 3);
        else
            arrow << QPointF(center.x() - 4, center.y() - 2)
                  << QPointF(center.x() + 4, center.y() - 2)
                  << QPointF(center.x(), center.y() + 3);
        painter->setPen(Qt::NoPen);
        painter->setBrush(theme.accent());
        painter->drawPolygon(arrow);
    }
    painter->restore();
}
