#pragma once

#include <QHeaderView>

class FileHeaderView : public QHeaderView
{
    Q_OBJECT

public:
    explicit FileHeaderView(Qt::Orientation orientation, QWidget *parent = nullptr);

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
};
