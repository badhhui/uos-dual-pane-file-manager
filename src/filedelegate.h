#pragma once

#include <QSet>
#include <QStyledItemDelegate>

class FileRowDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit FileRowDelegate(QObject *parent = nullptr);
    void setActive(bool active);
    void setClipboardFeedback(const QSet<QString> &paths, bool cut);
    void setFeedbackOpacity(qreal opacity);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

private:
    bool m_active = false;
    QSet<QString> m_feedbackPaths;
    bool m_feedbackCut = false;
    qreal m_feedbackOpacity = 0.0;
};
