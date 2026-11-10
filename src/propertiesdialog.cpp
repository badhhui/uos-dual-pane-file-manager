#include "propertiesdialog.h"

#include "fileoperations.h"
#include "formatters.h"
#include "theme.h"
#include "uidialogs.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {
QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("SectionTitle"));
    return label;
}
}

PropertiesDialog::PropertiesDialog(const QStringList &paths, QWidget *parent)
    : QDialog(parent)
    , m_paths(paths)
{
    setObjectName(QStringLiteral("PropertiesDialog"));
    setWindowTitle(QStringLiteral("属性"));
    setMinimumWidth(560);

    const QFileInfo first(paths.isEmpty() ? QString() : paths.first());
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(12);

    auto *title = new QLabel(paths.size() == 1 ? first.fileName()
                                               : QStringLiteral("已选择 %1 个项目").arg(paths.size()), this);
    title->setObjectName(QStringLiteral("DialogTitle"));
    layout->addWidget(title);
    if (paths.size() == 1) {
        auto *pathLabel = new QLabel(first.absoluteFilePath(), this);
        pathLabel->setObjectName(QStringLiteral("DialogSubtitle"));
        pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        pathLabel->setWordWrap(true);
        layout->addWidget(pathLabel);
    }

    auto *summary = new QLabel(this);
    QString summaryText;
    if (paths.size() == 1) {
        QString type = first.isDir() ? QStringLiteral("文件夹") : QStringLiteral("文件");
        if (!first.isDir() && !first.suffix().isEmpty())
            type = QStringLiteral("%1 文件").arg(first.suffix().toUpper());
        summaryText = QStringLiteral("类型：%1\n大小：%2\n修改时间：%3")
                          .arg(type,
                               first.isDir() ? QStringLiteral("—") : formatSize(first.size()),
                               formatDateTime(first.lastModified()));
    } else {
        qint64 total = 0;
        bool hasDir = false;
        for (const QString &path : paths) {
            const QFileInfo info(path);
            if (info.isDir())
                hasDir = true;
            else
                total += info.size();
        }
        summaryText = hasDir ? QStringLiteral("包含文件夹，权限和属主将应用到所有选中项目")
                             : QStringLiteral("合计大小：%1\n权限和属主将应用到所有选中项目")
                                   .arg(formatSize(total));
    }
    summary->setText(summaryText);
    summary->setObjectName(QStringLiteral("DialogSubtitle"));
    summary->setWordWrap(true);
    layout->addWidget(summary);

    layout->addWidget(sectionTitle(QStringLiteral("权限"), this));
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(7);
    grid->addWidget(new QLabel(QStringLiteral("对象"), this), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("读取"), this), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("写入"), this), 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("执行"), this), 0, 3);
    const QStringList rowNames = {QStringLiteral("所有者"), QStringLiteral("用户组"),
                                  QStringLiteral("其他")};
    const QFile::Permissions perms = first.permissions();
    const QFileDevice::Permission ownerRead = QFileDevice::ReadOwner;
    const QFileDevice::Permission ownerWrite = QFileDevice::WriteOwner;
    const QFileDevice::Permission ownerExec = QFileDevice::ExeOwner;
    const QFileDevice::Permission groupRead = QFileDevice::ReadGroup;
    const QFileDevice::Permission groupWrite = QFileDevice::WriteGroup;
    const QFileDevice::Permission groupExec = QFileDevice::ExeGroup;
    const QFileDevice::Permission otherRead = QFileDevice::ReadOther;
    const QFileDevice::Permission otherWrite = QFileDevice::WriteOther;
    const QFileDevice::Permission otherExec = QFileDevice::ExeOther;
    const QFileDevice::Permission readBits[3] = {ownerRead, groupRead, otherRead};
    const QFileDevice::Permission writeBits[3] = {ownerWrite, groupWrite, otherWrite};
    const QFileDevice::Permission execBits[3] = {ownerExec, groupExec, otherExec};
    for (int row = 0; row < 3; ++row) {
        grid->addWidget(new QLabel(rowNames.at(row), this), row + 1, 0);
        auto *read = new QCheckBox(this);
        auto *write = new QCheckBox(this);
        auto *exec = new QCheckBox(this);
        read->setChecked(perms.testFlag(readBits[row]));
        write->setChecked(perms.testFlag(writeBits[row]));
        exec->setChecked(perms.testFlag(execBits[row]));
        m_readChecks.append(read);
        m_writeChecks.append(write);
        m_execChecks.append(exec);
        grid->addWidget(read, row + 1, 1);
        grid->addWidget(write, row + 1, 2);
        grid->addWidget(exec, row + 1, 3);
        connect(read, &QCheckBox::toggled, this, &PropertiesDialog::updateModeLabel);
        connect(write, &QCheckBox::toggled, this, &PropertiesDialog::updateModeLabel);
        connect(exec, &QCheckBox::toggled, this, &PropertiesDialog::updateModeLabel);
    }
    layout->addLayout(grid);
    m_modeLabel = new QLabel(this);
    m_modeLabel->setObjectName(QStringLiteral("DialogSubtitle"));
    layout->addWidget(m_modeLabel);

    auto *ownerRow = new QHBoxLayout();
    ownerRow->setSpacing(10);
    ownerRow->addWidget(new QLabel(QStringLiteral("属主"), this));
    m_ownerEdit = new QLineEdit(first.owner(), this);
    m_ownerEdit->setPlaceholderText(QStringLiteral("用户名"));
    ownerRow->addWidget(m_ownerEdit, 1);
    ownerRow->addWidget(new QLabel(QStringLiteral("用户组"), this));
    m_groupEdit = new QLineEdit(first.group(), this);
    m_groupEdit->setPlaceholderText(QStringLiteral("组名"));
    ownerRow->addWidget(m_groupEdit, 1);
    layout->addLayout(ownerRow);

    m_adminCheck = new QCheckBox(QStringLiteral("使用管理员权限应用（会弹出授权窗口）"), this);
    m_adminCheck->setVisible(canEscalateWithPkexec());
    layout->addWidget(m_adminCheck);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto *close = new QPushButton(QStringLiteral("关闭"), this);
    m_applyButton = new QPushButton(QStringLiteral("应用"), this);
    for (QPushButton *button : {close, m_applyButton}) {
        button->setIcon(QIcon());
        button->setIconSize(QSize(0, 0));
        button->setMinimumHeight(32);
    }
    close->setAutoDefault(false);
    m_applyButton->setDefault(true);
    buttons->addWidget(close);
    buttons->addWidget(m_applyButton);
    layout->addLayout(buttons);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_applyButton, &QPushButton::clicked, this, &PropertiesDialog::applyChanges);

    m_initialMode = selectedMode();
    updateModeLabel();
}

int PropertiesDialog::selectedMode() const
{
    int mode = 0;
    for (int index = 0; index < 3; ++index) {
        const int shift = (2 - index) * 3;
        if (m_readChecks.at(index)->isChecked())
            mode |= 4 << shift;
        if (m_writeChecks.at(index)->isChecked())
            mode |= 2 << shift;
        if (m_execChecks.at(index)->isChecked())
            mode |= 1 << shift;
    }
    return mode;
}

void PropertiesDialog::updateModeLabel()
{
    m_modeLabel->setText(QStringLiteral("权限值：0%1")
                             .arg(QString::number(selectedMode(), 8).rightJustified(3, QLatin1Char('0'))));
}

void PropertiesDialog::applyChanges()
{
    if (m_paths.isEmpty())
        return;
    const bool admin = m_adminCheck && m_adminCheck->isChecked();
    const QFileInfo first(m_paths.first());
    QString error;
    if (selectedMode() != m_initialMode) {
        if (!applyPermissions(m_paths, selectedMode(), admin, &error)) {
            UiDialogs::showWarning(this, QStringLiteral("修改权限"), error);
            return;
        }
    }
    const QString owner = m_ownerEdit->text().trimmed();
    const QString group = m_groupEdit->text().trimmed();
    if (owner != first.owner() || group != first.group()) {
        if (!applyOwnership(m_paths, owner, group, admin, &error)) {
            UiDialogs::showWarning(this, QStringLiteral("修改属主"), error);
            return;
        }
    }
    emit changed();
    accept();
}
