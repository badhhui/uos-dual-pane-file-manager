#include "transferdialog.h"

#include "formatters.h"
#include "iconprovider.h"
#include "theme.h"
#include "transfermanager.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QWidget>
#include <QProgressBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

TransferDialog::TransferDialog(TransferManager *manager, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_table(nullptr)
{
    setObjectName(QStringLiteral("TransferDialog"));
    setWindowTitle(QStringLiteral("传输中心"));
    setWindowIcon(IconProvider::instance().appIcon());
    resize(820, 480);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("传输中心"), this);
    title->setObjectName(QStringLiteral("DialogTitle"));
    layout->addWidget(title);
    auto *subtitle = new QLabel(QStringLiteral("复制与移动任务会依次执行；可在任务栏暂停、继续或取消当前任务。"), this);
    subtitle->setObjectName(QStringLiteral("DialogSubtitle"));
    layout->addWidget(subtitle);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("TransferTable"));
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(QStringList()
        << QStringLiteral("任务") << QStringLiteral("目标") << QStringLiteral("状态")
        << QStringLiteral("速度 / 剩余") << QStringLiteral("进度"));
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setAlternatingRowColors(false);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setDefaultSectionSize(46);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_table->setColumnWidth(2, 100);
    m_table->setColumnWidth(3, 160);
    m_table->setColumnWidth(4, 150);
    layout->addWidget(m_table, 1);

    auto *closeButton = new QPushButton(QStringLiteral("关闭"), this);
    closeButton->setIcon(QIcon());
    closeButton->setIconSize(QSize(0, 0));
    connect(closeButton, &QPushButton::clicked, this, &QDialog::hide);
    layout->addWidget(closeButton, 0, Qt::AlignRight);

    if (m_manager)
        connect(m_manager, &TransferManager::tasksChanged, this, &TransferDialog::refresh);
    refresh();
}

void TransferDialog::refresh()
{
    if (!m_table || !m_manager)
        return;
    const QVector<TransferManager::Snapshot> tasks = m_manager->tasks();
    m_table->setRowCount(0);
    if (tasks.isEmpty()) {
        m_table->setRowCount(1);
        m_table->setSpan(0, 0, 1, 5);
        auto *empty = new QTableWidgetItem(QStringLiteral("暂无传输任务"));
        empty->setTextAlignment(Qt::AlignCenter);
        empty->setForeground(Theme::instance().textMuted());
        m_table->setItem(0, 0, empty);
        return;
    }
    for (const TransferManager::Snapshot &task : tasks) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        const QString operation = task.operation == TransferOperation::Copy
            ? QStringLiteral("复制") : QStringLiteral("移动");
        auto *nameItem = new QTableWidgetItem(
            QStringLiteral("%1 %2 个项目").arg(operation).arg(task.sources.size()));
        if (task.active) {
            QFont font = nameItem->font();
            font.setWeight(QFont::DemiBold);
            nameItem->setFont(font);
        }
        nameItem->setToolTip(task.sources.join(QLatin1Char('\n')));
        nameItem->setForeground(task.active ? Theme::instance().text() : Theme::instance().textSecondary());

        auto *destinationItem = new QTableWidgetItem(compactPath(task.destination, 2));
        destinationItem->setToolTip(task.destination);
        destinationItem->setForeground(Theme::instance().textMuted());

        QString status = task.statusText;
        if (task.status == TransferManager::TaskStatus::Failed && !task.error.isEmpty())
            status = QStringLiteral("失败：%1").arg(task.error);
        auto *statusItem = new QTableWidgetItem(status);
        statusItem->setToolTip(task.error.isEmpty() ? task.statusText : task.error);
        if (task.status == TransferManager::TaskStatus::Failed)
            statusItem->setForeground(Theme::instance().danger());
        else if (task.status == TransferManager::TaskStatus::Done)
            statusItem->setForeground(Theme::instance().success());
        else if (task.active)
            statusItem->setForeground(Theme::instance().accent());
        else
            statusItem->setForeground(Theme::instance().textMuted());

        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, destinationItem);
        m_table->setItem(row, 2, statusItem);

        QString speedText;
        if (task.bytesPerSecond > 0)
            speedText = formatSpeed(task.bytesPerSecond);
        if (task.remainingSeconds >= 0) {
            if (!speedText.isEmpty())
                speedText += QStringLiteral(" · ");
            speedText += QStringLiteral("剩余 %1").arg(formatDuration(task.remainingSeconds));
        }
        auto *speedItem = new QTableWidgetItem(speedText.isEmpty() ? QStringLiteral("—") : speedText);
        speedItem->setForeground(Theme::instance().textMuted());
        m_table->setItem(row, 3, speedItem);

        auto *progressHost = new QWidget(m_table);
        progressHost->setObjectName(QStringLiteral("TaskProgressHost"));
        auto *progressLayout = new QHBoxLayout(progressHost);
        progressLayout->setContentsMargins(6, 0, 8, 0);
        progressLayout->setSpacing(7);
        progressLayout->setAlignment(Qt::AlignVCenter);

        auto *progress = new QProgressBar(progressHost);
        progress->setObjectName(QStringLiteral("TaskProgress"));
        progress->setTextVisible(false);
        progress->setFixedHeight(8);
        progress->setRange(0, 100);
        progress->setValue(0);

        qint64 done = task.doneBytes;
        qint64 total = task.totalBytes;
        if (total <= 0) {
            done = task.doneFiles;
            total = task.totalFiles;
        }
        QString percentText;
        if (total > 0) {
            const int percent = static_cast<int>(qBound<qint64>(0, done * 100 / total, 100));
            progress->setValue(percent);
            percentText = QStringLiteral("%1%").arg(percent);
        } else if (task.status == TransferManager::TaskStatus::Done) {
            progress->setValue(100);
            percentText = QStringLiteral("100%");
        } else if (task.status == TransferManager::TaskStatus::Queued
                   || task.status == TransferManager::TaskStatus::Preparing) {
            progress->setRange(0, 0);
            percentText = QStringLiteral("—");
        } else {
            percentText = QStringLiteral("—");
        }

        auto *percent = new QLabel(percentText, progressHost);
        percent->setObjectName(QStringLiteral("TaskProgressLabel"));
        percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        percent->setMinimumWidth(38);
        progressLayout->addWidget(progress, 1);
        progressLayout->addWidget(percent);
        m_table->setCellWidget(row, 4, progressHost);
    }
}
