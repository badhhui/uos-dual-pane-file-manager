#pragma once

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

class PropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PropertiesDialog(const QStringList &paths, QWidget *parent = nullptr);

signals:
    void changed();

private:
    void applyChanges();
    void updateModeLabel();
    int selectedMode() const;
    int m_initialMode = 0;

    QStringList m_paths;
    QList<QCheckBox *> m_readChecks;
    QList<QCheckBox *> m_writeChecks;
    QList<QCheckBox *> m_execChecks;
    QLabel *m_modeLabel = nullptr;
    QLineEdit *m_ownerEdit = nullptr;
    QLineEdit *m_groupEdit = nullptr;
    QCheckBox *m_adminCheck = nullptr;
    QPushButton *m_applyButton = nullptr;
};
