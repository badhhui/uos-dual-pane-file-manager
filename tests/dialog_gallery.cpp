#include "moderndialog.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QTest>

namespace {
QVector<DialogAction> conflictActions()
{
    return {
        DialogAction(QStringLiteral("ConflictCopyButton"), QStringLiteral("保留两者"),
                     QStringLiteral("自动生成新文件名"), DialogActionRole::Primary),
        DialogAction(QStringLiteral("ConflictReplaceButton"), QStringLiteral("替换目标"),
                     QStringLiteral("删除目标同名文件后写入"), DialogActionRole::Destructive),
        DialogAction(QStringLiteral("ConflictSkipButton"), QStringLiteral("跳过"),
                     QStringLiteral("保留目标，不复制"), DialogActionRole::Secondary),
        DialogAction(QStringLiteral("ConflictCancelButton"), QStringLiteral("取消"),
                     QString(), DialogActionRole::Cancel)
    };
}

QVector<DialogAction> confirmActions()
{
    return {
        DialogAction(QStringLiteral("accept"), QStringLiteral("移到回收站"),
                     QString(), DialogActionRole::Destructive),
        DialogAction(QStringLiteral("cancel"), QStringLiteral("取消"),
                     QString(), DialogActionRole::Cancel)
    };
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Theme::instance().applyToApplication();
    QDir().mkpath(QStringLiteral("/work/file-manager-native/designs"));
    const QVector<DialogStyle> styles = {DialogStyle::Card, DialogStyle::Accent,
                                         DialogStyle::Sheet};
    const QString suffix[3] = {QStringLiteral("card"), QStringLiteral("accent"),
                               QStringLiteral("sheet")};
    for (int i = 0; i < styles.size(); ++i) {
        const DialogStyle style = styles.at(i);
        ModernDialog conflict(style, DialogTone::Warning,
                              QStringLiteral("目标位置已有同名项目"),
                              QStringLiteral("源文件：项目说明.pdf\n目标位置：/home/user/文档/项目说明.pdf\n\n"
                                             "请选择处理方式。"),
                              conflictActions(), QStringLiteral("应用到全部冲突"),
                              QStringLiteral("ConflictCopyButton"));
        conflict.show();
        QTest::qWait(150);
        conflict.adjustSize();
        conflict.grab().save(QStringLiteral("/work/file-manager-native/designs/dialog-conflict-%1.png")
                                 .arg(suffix[i]));
        conflict.close();

        ModernDialog confirm(style, DialogTone::Danger,
                             QStringLiteral("移到回收站？"),
                             QStringLiteral("「项目说明.pdf」将被移到回收站，可以稍后从回收站还原。"),
                             confirmActions(), QString(), QStringLiteral("cancel"));
        confirm.show();
        QTest::qWait(150);
        confirm.adjustSize();
        confirm.grab().save(QStringLiteral("/work/file-manager-native/designs/dialog-confirm-%1.png")
                                .arg(suffix[i]));
        confirm.close();
    }
    return 0;
}
