#pragma once

#include <QFontMetrics>
#include <QStyledItemDelegate>

// Card-style item delegate for the icon grid, mirroring the Deepin/UOS file
// manager: a soft rounded card per item, centered icon, and a centered file
// name drawn on up to two lines with a middle ellipsis when it still overflows.
class FileIconDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit FileIconDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    static void splitTwoLines(const QFontMetrics &metrics, const QString &text, int width,
                              QString *firstLine, QString *secondLine, bool *twoLines);
};
