#pragma once

#include "transferconflict.h"

#include <QString>

class QWidget;

namespace UiDialogs {
QString getText(QWidget *parent, const QString &title, const QString &label,
                const QString &initialText, bool *accepted);
void showWarning(QWidget *parent, const QString &title, const QString &text);
void showInfo(QWidget *parent, const QString &title, const QString &text);
bool confirm(QWidget *parent, const QString &title, const QString &text,
             const QString &acceptText = QStringLiteral("确定"),
             const QString &rejectText = QStringLiteral("取消"),
             bool defaultReject = true);

struct ConflictChoice {
    ConflictAction action = ConflictAction::KeepBoth;
    bool applyToAll = false;
};

bool resolveConflict(QWidget *parent, const QString &sourceName, const QString &targetPath,
                     bool sourceIsDir, bool targetIsDir, bool samePath, ConflictChoice *choice);
}
