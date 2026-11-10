#include "uidialogs.h"

#include "moderndialog.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFileInfo>

namespace {
void stripButtonIcon(QPushButton *button)
{
    if (!button)
        return;
    button->setIcon(QIcon());
    button->setIconSize(QSize(0, 0));
}
}

QString UiDialogs::getText(QWidget *parent, const QString &title, const QString &label,
                           const QString &initialText, bool *accepted)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);

    auto *prompt = new QLabel(label, &dialog);
    prompt->setWordWrap(true);
    layout->addWidget(prompt);

    auto *edit = new QLineEdit(initialText, &dialog);
    edit->setMinimumWidth(360);
    edit->selectAll();
    layout->addWidget(edit);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("取消"), &dialog);
    auto *ok = new QPushButton(QStringLiteral("确定"), &dialog);
    stripButtonIcon(cancel);
    stripButtonIcon(ok);
    cancel->setAutoDefault(false);
    ok->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addLayout(buttons);

    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(edit, &QLineEdit::returnPressed, &dialog, &QDialog::accept);

    if (accepted)
        *accepted = (dialog.exec() == QDialog::Accepted);
    return edit->text();
}

void UiDialogs::showWarning(QWidget *parent, const QString &title, const QString &text)
{
    const QVector<DialogAction> actions = {
        {QStringLiteral("ok"), QStringLiteral("确定"), QString(), DialogActionRole::Primary}
    };
    ModernDialog dialog(ModernDialog::currentStyle(), DialogTone::Warning, title, text, actions,
                        QString(), QStringLiteral("ok"), parent);
    dialog.exec();
}

void UiDialogs::showInfo(QWidget *parent, const QString &title, const QString &text)
{
    const QVector<DialogAction> actions = {
        {QStringLiteral("ok"), QStringLiteral("确定"), QString(), DialogActionRole::Primary}
    };
    ModernDialog dialog(ModernDialog::currentStyle(), DialogTone::Info, title, text, actions,
                        QString(), QStringLiteral("ok"), parent);
    dialog.exec();
}

bool UiDialogs::confirm(QWidget *parent, const QString &title, const QString &text,
                        const QString &acceptText, const QString &rejectText,
                        bool defaultReject)
{
    const bool destructive = acceptText.contains(QStringLiteral("删除"))
        || acceptText.contains(QStringLiteral("清空"))
        || acceptText.contains(QStringLiteral("移到回收站"))
        || acceptText.contains(QStringLiteral("退出"))
        || acceptText.contains(QStringLiteral("移除"))
        || acceptText.contains(QStringLiteral("替换"));
    QVector<DialogAction> actions;
    actions.append({QStringLiteral("accept"), acceptText, QString(),
                    destructive ? DialogActionRole::Destructive : DialogActionRole::Primary});
    actions.append({QStringLiteral("cancel"), rejectText, QString(), DialogActionRole::Cancel});
    ModernDialog dialog(ModernDialog::currentStyle(),
                        destructive ? DialogTone::Danger : DialogTone::Question,
                        title, text, actions, QString(),
                        defaultReject ? QStringLiteral("cancel") : QStringLiteral("accept"), parent);
    return dialog.exec() == QDialog::Accepted
        && dialog.selectedId() == QStringLiteral("accept");
}

bool UiDialogs::resolveConflict(QWidget *parent, const QString &sourceName,
                                const QString &targetPath, bool sourceIsDir,
                                bool targetIsDir, bool samePath, ConflictChoice *choice)
{
    if (!choice)
        return false;
    QVector<DialogAction> actions;
    if (!samePath && sourceIsDir && targetIsDir) {
        actions.append({QStringLiteral("ConflictMergeButton"), QStringLiteral("合并文件夹"),
                        QStringLiteral("同名文件保留两者，不覆盖"),
                        DialogActionRole::Secondary});
    }
    actions.append({QStringLiteral("ConflictCopyButton"),
                    samePath ? QStringLiteral("复制并改名") : QStringLiteral("保留两者"),
                    samePath ? QStringLiteral("在当前文件夹生成副本")
                             : QStringLiteral("自动生成新文件名"),
                    DialogActionRole::Primary});
    if (!samePath) {
        actions.append({QStringLiteral("ConflictReplaceButton"), QStringLiteral("替换目标"),
                        QStringLiteral("删除目标同名项目后写入"),
                        DialogActionRole::Destructive});
        actions.append({QStringLiteral("ConflictSkipButton"), QStringLiteral("跳过"),
                        QStringLiteral("不传输这个项目"),
                        DialogActionRole::Secondary});
    }
    actions.append({QStringLiteral("ConflictCancelButton"), QStringLiteral("取消"),
                    QString(), DialogActionRole::Cancel});

    QString hint;
    if (samePath)
        hint = QStringLiteral("这是原位置。复制只会在当前文件夹生成一个改名副本，不会覆盖源文件。");
    else if (sourceIsDir && targetIsDir)
        hint = QStringLiteral("“合并”会把源文件夹内容并入目标文件夹；同名文件会保留两者，不会覆盖。");
    else
        hint = QStringLiteral("“替换”会删除目标中的同名项目后写入；“保留两者”会自动改名。");
    const QString message = QStringLiteral("源项目：%1\n目标位置：%2\n\n%3")
                                .arg(sourceName, targetPath, hint);
    ModernDialog dialog(ModernDialog::currentStyle(), DialogTone::Warning,
                        QStringLiteral("目标位置已有同名项目"), message, actions,
                        QStringLiteral("应用到全部冲突"), QStringLiteral("ConflictCopyButton"),
                        parent);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    choice->applyToAll = dialog.extraChecked();
    const QString id = dialog.selectedId();
    if (id == QStringLiteral("ConflictMergeButton"))
        choice->action = ConflictAction::Merge;
    else if (id == QStringLiteral("ConflictReplaceButton"))
        choice->action = ConflictAction::Replace;
    else if (id == QStringLiteral("ConflictSkipButton"))
        choice->action = ConflictAction::Skip;
    else
        choice->action = ConflictAction::KeepBoth;
    return true;
}
