#include "filedelegate.h"

#include "directorymodel.h"
#include "theme.h"

#include <QFontMetrics>
#include <QIcon>
#include <QPainter>

FileRowDelegate::FileRowDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void FileRowDelegate::setActive(bool active)
{
    m_active = active;
}

void FileRowDelegate::setClipboardFeedback(const QSet<QString> &paths, bool cut)
{
    m_feedbackPaths = paths;
    m_feedbackCut = cut;
}

void FileRowDelegate::setFeedbackOpacity(qreal opacity)
{
    m_feedbackOpacity = opacity;
}

void FileRowDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const
{
    const Theme &theme = Theme::instance();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(option.rect);

    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    QColor background = (index.row() % 2 == 1) ? theme.alternateRow() : theme.surface();
    if (selected)
        background = m_active ? theme.selected() : theme.selectedInactive();
    else if (hovered)
        background = theme.hover();
    painter->fillRect(option.rect, background);

    if (selected && m_active && index.column() == DirectoryModel::NameColumn) {
        painter->fillRect(QRect(option.rect.left(), option.rect.top(), 2, option.rect.height()),
                          theme.accent());
    }

    if (m_feedbackOpacity > 0.0 && !m_feedbackPaths.isEmpty()
        && m_feedbackPaths.contains(index.data(Qt::UserRole).toString())) {
        QColor pulse = m_feedbackCut ? QColor(QStringLiteral("#F59E0B")) : theme.accent();
        pulse.setAlphaF(0.30 * m_feedbackOpacity);
        painter->fillRect(option.rect, pulse);
        QColor edge = m_feedbackCut ? QColor(QStringLiteral("#D97706")) : theme.accent();
        edge.setAlphaF(0.85 * m_feedbackOpacity);
        painter->fillRect(QRect(option.rect.left(), option.rect.bottom() - 2,
                                option.rect.width(), 2), edge);
    }

    QFont font = option.font;
    font.setPixelSize(13);
    painter->setFont(font);
    painter->setPen(theme.text());

    const QRect cell = option.rect.adjusted(0, 0, 0, 0);
    if (index.column() == DirectoryModel::NameColumn) {
        const int depth = index.data(Qt::UserRole + 2).toInt();
        const int indent = depth * 16;
        const int left = cell.left() + indent;
        if (index.data(Qt::UserRole + 4).toBool()) {
            const qreal centerY = cell.center().y();
            const bool expanded = index.data(Qt::UserRole + 3).toBool();
            QPen disclosurePen(theme.textSecondary(), 1.6);
            disclosurePen.setCapStyle(Qt::RoundCap);
            disclosurePen.setJoinStyle(Qt::RoundJoin);
            painter->setPen(disclosurePen);
            painter->setBrush(Qt::NoBrush);
            QPolygonF chevron;
            if (expanded) {
                chevron << QPointF(left + 6, centerY - 2)
                        << QPointF(left + 11, centerY + 3)
                        << QPointF(left + 16, centerY - 2);
            } else {
                chevron << QPointF(left + 6, centerY - 4)
                        << QPointF(left + 11, centerY)
                        << QPointF(left + 6, centerY + 4);
            }
            painter->drawPolyline(chevron);
        }
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        if (!icon.isNull()) {
            const QRect iconRect(cell.left() + indent + 18, cell.center().y() - 10, 20, 20);
            icon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal,
                       selected ? QIcon::On : QIcon::Off);
        }
        const QRect textRect(cell.left() + indent + 44, cell.top(),
                             cell.width() - indent - 52, cell.height());
        QFont nameFont = font;
        nameFont.setWeight(QFont::Medium);
        painter->setFont(nameFont);
        const QString name = index.data(Qt::DisplayRole).toString();
        const QString elided = QFontMetrics(nameFont).elidedText(
            name, Qt::ElideMiddle, textRect.width());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elided);
    } else {
        QFont metaFont = font;
        metaFont.setPixelSize(12);
        painter->setFont(metaFont);
        painter->setPen(theme.textMuted());
        const QString text = index.data(Qt::DisplayRole).toString();
        const QString elided = QFontMetrics(metaFont).elidedText(
            text, Qt::ElideRight, cell.width() - 18);
        const Qt::Alignment alignment = index.column() == DirectoryModel::SizeColumn
            ? (Qt::AlignRight | Qt::AlignVCenter)
            : (Qt::AlignLeft | Qt::AlignVCenter);
        const QRect textRect = index.column() == DirectoryModel::SizeColumn
            ? cell.adjusted(8, 0, -12, 0)
            : cell.adjusted(10, 0, -6, 0);
        painter->drawText(textRect, alignment, elided);
    }
    painter->restore();
}

QSize FileRowDelegate::sizeHint(const QStyleOptionViewItem &option,
                                const QModelIndex &index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(120, 24);
}
