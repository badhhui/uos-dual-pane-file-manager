#pragma once

#include <QDialog>

class TransferManager;
class QTableWidget;

class TransferDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TransferDialog(TransferManager *manager, QWidget *parent = nullptr);
    void refresh();

private:
    TransferManager *m_manager;
    QTableWidget *m_table;
};
