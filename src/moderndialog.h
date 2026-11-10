#pragma once

#include <QDialog>
#include <QVector>

class QCheckBox;
class QFrame;
class QLabel;
class QPushButton;

enum class DialogStyle {
    Card,
    Accent,
    Sheet
};

enum class DialogTone {
    Info,
    Warning,
    Danger,
    Question
};

enum class DialogActionRole {
    Primary,
    Secondary,
    Destructive,
    Cancel
};

struct DialogAction {
    DialogAction() = default;
    DialogAction(const QString &actionId, const QString &actionText,
                 const QString &actionDescription = QString(),
                 DialogActionRole actionRole = DialogActionRole::Secondary)
        : id(actionId), text(actionText), description(actionDescription), role(actionRole) {}

    QString id;
    QString text;
    QString description;
    DialogActionRole role = DialogActionRole::Secondary;
};

class ModernDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ModernDialog(DialogStyle style, DialogTone tone, const QString &title,
                           const QString &message, const QVector<DialogAction> &actions,
                           const QString &extraCheckText = QString(),
                           const QString &defaultActionId = QString(),
                           QWidget *parent = nullptr);

    QString selectedId() const;
    bool extraChecked() const;

    static DialogStyle currentStyle();
    static void setCurrentStyle(DialogStyle style);
    static QString styleName(DialogStyle style);

protected:
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void centerOnParent();
    QPushButton *createActionButton(const DialogAction &action, bool stacked);

    DialogStyle m_style;
    QString m_selectedId;
    QCheckBox *m_extraCheck = nullptr;
    QFrame *m_card = nullptr;
    QString m_defaultActionId;
};
