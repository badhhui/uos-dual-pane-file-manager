#pragma once

#include <QDialog>
#include <QStringList>

class QPushButton;
class QTableWidget;

class TrashDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TrashDialog(QWidget *parent = nullptr);

signals:
    void trashChanged();

private:
    void refresh();
    void restoreSelected();
    void empty();
    QStringList selectedTrashPaths() const;

    QTableWidget *m_table;
    QPushButton *m_restoreButton;
    QPushButton *m_emptyButton;
};
