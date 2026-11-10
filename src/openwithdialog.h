#pragma once

#include <QDialog>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QListWidget;

bool chooseApplicationAndOpen(QWidget *parent, const QString &filePath);

class OpenWithDialog : public QDialog
{
    Q_OBJECT

public:
    struct Application {
        QString name;
        QString desktopPath;
        QString icon;
        bool isDefault = false;
    };

    OpenWithDialog(const QString &filePath, const QVector<Application> &applications,
                   bool canSetDefault, QWidget *parent = nullptr);

    QString selectedDesktopPath() const;
    bool setAsDefault() const;

private:
    void acceptSelection();

    QString m_filePath;
    QListWidget *m_list;
    QCheckBox *m_defaultCheck = nullptr;
};
