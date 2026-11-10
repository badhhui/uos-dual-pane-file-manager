#pragma once

#include <QHash>
#include <QString>

enum class ConflictAction {
    KeepBoth,
    Replace,
    Skip,
    Merge
};

using ConflictActions = QHash<QString, ConflictAction>;
