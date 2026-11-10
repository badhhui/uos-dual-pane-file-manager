#include "trashdialog.h"

#include "fileoperations.h"
#include "formatters.h"
#include "iconprovider.h"
#include "theme.h"
#include "uidialogs.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QPalette>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

TrashDialog::TrashDialog(QWidget *parent)
    : QDialog(parent)
    , m_table(nullptr)
    , m_restoreButton(nullptr)
    , m_emptyButton(nullptr)
{
    setObjectName(QStringLiteral("TrashDialog"));
    setWindowTitle(QStringLiteral("回收站"));
    resize(820, 500);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("回收站"), this);
    title->setObjectName(QStringLiteral("DialogTitle"));
    layout->addWidget(title);
    auto *subtitle = new QLabel(QStringLiteral("可以还原或永久清空已删除的项目。还原到原位置；如果原位置已有同名项目，会自动改名。"), this);
    subtitle->setObjectName(QStringLiteral("DialogSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("TrashTable"));
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(QStringList()
        << QStringLiteral("名称") << QStringLiteral("原位置") << QStringLiteral("删除时间"));
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::StrongFocus);
    m_table->setWordWrap(false);
    m_table->setIconSize(QSize(20, 20));
    m_table->verticalHeader()->setDefaultSectionSize(26);
    QPalette tablePalette = m_table->palette();
    tablePalette.setColor(QPalette::AlternateBase, Theme::instance().alternateRow());
    m_table->setPalette(tablePalette);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    m_restoreButton = new QPushButton(QStringLiteral("还原"), this);
    m_emptyButton = new QPushButton(QStringLiteral("清空回收站"), this);
    auto *close = new QPushButton(QStringLiteral("关闭"), this);
    for (QPushButton *button : {m_restoreButton, m_emptyButton, close}) {
        button->setIcon(QIcon());
        button->setIconSize(QSize(0, 0));
        button->setMinimumHeight(32);
    }
    buttons->addWidget(m_restoreButton);
    buttons->addWidget(m_emptyButton);
    buttons->addStretch(1);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(m_restoreButton, &QPushButton::clicked, this, &TrashDialog::restoreSelected);
    connect(m_emptyButton, &QPushButton::clicked, this, &TrashDialog::empty);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_table, &QTableWidget::doubleClicked, this, [this](const QModelIndex &) {
        restoreSelected();
    });

    refresh();
}

void TrashDialog::refresh()
{
    const QVector<TrashItem> items = listTrashItems();
    m_table->setRowCount(0);
    for (const TrashItem &item : items) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        const QFileInfo trashInfo(item.trashPath);
        auto *name = new QTableWidgetItem(
            IconProvider::instance().fileIcon(trashInfo.isDir(),
                                               trashInfo.suffix().toLower()),
            item.name);
        name->setData(Qt::UserRole, item.trashPath);
        name->setToolTip(item.name);
        auto *original = new QTableWidgetItem(item.originalPath);
        original->setToolTip(item.originalPath);
        original->setForeground(Theme::instance().textMuted());
        auto *deleted = new QTableWidgetItem(
            item.deleted.isValid() ? formatDateTime(item.deleted) : QStringLiteral("—"));
        deleted->setForeground(Theme::instance().textMuted());
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, original);
        m_table->setItem(row, 2, deleted);
    }
    const bool hasItems = m_table->rowCount() > 0;
    m_restoreButton->setEnabled(hasItems);
    m_emptyButton->setEnabled(hasItems);
}

QStringList TrashDialog::selectedTrashPaths() const
{
    QStringList paths;
    const QModelIndexList rows = m_table->selectionModel()->selectedRows(0);
    for (const QModelIndex &index : rows) {
        const QString path = index.data(Qt::UserRole).toString();
        if (!path.isEmpty())
            paths.append(path);
    }
    return paths;
}

void TrashDialog::restoreSelected()
{
    const QStringList paths = selectedTrashPaths();
    if (paths.isEmpty())
        return;
    QString error;
    if (!restoreTrashItems(paths, &error)) {
        UiDialogs::showWarning(this, QStringLiteral("还原"), error);
        return;
    }
    refresh();
    emit trashChanged();
}

void TrashDialog::empty()
{
    if (m_table->rowCount() == 0)
        return;
    if (!UiDialogs::confirm(this, QStringLiteral("清空回收站"),
                            QStringLiteral("清空后会永久删除回收站中的全部项目，无法恢复。确定清空吗？"),
                            QStringLiteral("清空"), QStringLiteral("取消"), true)) {
        return;
    }
    QString error;
    if (!emptyTrash(&error)) {
        UiDialogs::showWarning(this, QStringLiteral("清空回收站"), error);
        return;
    }
    refresh();
    emit trashChanged();
}
