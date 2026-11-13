#include "fileicondelegate.h"

#include "theme.h"

#include <QApplication>
#include <QIcon>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

FileIconDelegate::FileIconDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

// Splits the name into at most two centered lines. When the name fits on one
// line it is left untouched; when it fits when wrapped the wrap point is chosen
// near the middle; otherwise the beginning goes on the first line and the end on
// the second, with the omitted middle replaced by an ellipsis.
void FileIconDelegate::splitTwoLines(const QFontMetrics &metrics, const QString &text, int width,
                                     QString *firstLine, QString *secondLine, bool *twoLines)
{
    *twoLines = false;
    *firstLine = text;
    secondLine->clear();
    if (width <= 0 || metrics.horizontalAdvance(text) <= width)
        return;

    const int middle = text.size() / 2;
    int bestSplit = -1;
    int bestDistance = text.size() + 1;
    for (int i = 1; i < text.size(); ++i) {
        if (metrics.horizontalAdvance(text.left(i)) > width)
            break;
        if (metrics.horizontalAdvance(text.mid(i)) > width)
            continue;
        const int distance = qAbs(i - middle);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestSplit = i;
        }
    }
    if (bestSplit > 0) {
        *firstLine = text.left(bestSplit);
        *secondLine = text.mid(bestSplit);
        *twoLines = true;
        return;
    }

    // Too long even for two lines: head on line one, ellipsis + tail on line two.
    QString head = text;
    while (head.size() > 1 && metrics.horizontalAdvance(head) > width)
        head.chop(1);
    QString tail = text.mid(head.size());
    while (!tail.isEmpty() && metrics.horizontalAdvance(QStringLiteral("…") + tail) > width)
        tail.remove(0, 1);
    if (tail.isEmpty() || head.isEmpty()) {
        *firstLine = metrics.elidedText(text, Qt::ElideMiddle, width);
        return;
    }
    *firstLine = head;
    *secondLine = QStringLiteral("…") + tail;
    *twoLines = true;
}

void FileIconDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool selected = opt.state & QStyle::State_Selected;
    const bool hovered = (opt.state & QStyle::State_MouseOver) && !selected;

    const QRect card = opt.rect.adjusted(3, 3, -3, -3);
    QColor background = Theme::instance().surface();
    QColor border = Theme::instance().line();
    if (selected) {
        background = Theme::instance().accentSoft();
        border = Theme::instance().accent();
    } else if (hovered) {
        background = Theme::instance().hover();
        border = Theme::instance().lineStrong();
    }
    painter->setPen(QPen(border, 1));
    painter->setBrush(background);
    painter->drawRoundedRect(QRectF(card).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    const int iconSide = 48;
    const QRect iconRect(QPoint(card.center().x() - iconSide / 2, card.top() + 9),
                         QSize(iconSide, iconSide));
    if (!icon.isNull()) {
        const QPixmap pixmap = icon.pixmap(QSize(iconSide, iconSide), QIcon::Normal, QIcon::Off);
        if (!pixmap.isNull())
            painter->drawPixmap(iconRect, pixmap);
    }

    QFont font = opt.font;
    font.setPixelSize(12);
    painter->setFont(font);
    const QFontMetrics metrics(font);
    const QString name = index.data(Qt::DisplayRole).toString();
    const int textWidth = card.width() - 10;
    QString firstLine;
    QString secondLine;
    bool twoLines = false;
    splitTwoLines(metrics, name, textWidth, &firstLine, &secondLine, &twoLines);

    painter->setPen(selected ? Theme::instance().accent() : Theme::instance().text());
    const int lineHeight = metrics.height();
    int y = iconRect.bottom() + 5;
    painter->drawText(QRect(card.left() + 5, y, textWidth, lineHeight),
                      Qt::AlignHCenter | Qt::AlignTop, firstLine);
    if (twoLines) {
        y += lineHeight - 3;
        painter->drawText(QRect(card.left() + 5, y, textWidth, lineHeight),
                          Qt::AlignHCenter | Qt::AlignTop, secondLine);
    }
    painter->restore();
}

QSize FileIconDelegate::sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(124, 116);
}
