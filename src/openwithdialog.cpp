#include "openwithdialog.h"

#include "desktopentry.h"
#include "iconprovider.h"
#include "theme.h"
#include "uidialogs.h"

#include <QCheckBox>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMimeDatabase>
#include <QMimeType>
#include <QProcess>
#include <QSet>
#include <algorithm>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {
QString defaultDesktopForMime(const QString &mimeName)
{
    const QString xdgMime = QStandardPaths::findExecutable(QStringLiteral("xdg-mime"));
    if (xdgMime.isEmpty())
        return QString();
    QProcess process;
    process.start(xdgMime, QStringList() << QStringLiteral("query")
                                         << QStringLiteral("default") << mimeName);
    if (!process.waitForStarted(2000) || !process.waitForFinished(3000))
        return QString();
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

bool setDefaultDesktopForMime(const QString &desktopPath, const QString &mimeName, QString *error)
{
    const QString xdgMime = QStandardPaths::findExecutable(QStringLiteral("xdg-mime"));
    if (xdgMime.isEmpty()) {
        if (error) *error = QStringLiteral("系统缺少 xdg-mime，无法设置默认应用");
        return false;
    }
    QProcess process;
    process.start(xdgMime, QStringList() << QStringLiteral("default")
                                         << QFileInfo(desktopPath).fileName() << mimeName);
    if (!process.waitForStarted(2500) || !process.waitForFinished(4000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) *error = QStringLiteral("设置默认打开方式失败");
        return false;
    }
    return true;
}
}

bool chooseApplicationAndOpen(QWidget *parent, const QString &filePath)
{
    if (!QFileInfo::exists(filePath)) {
        UiDialogs::showWarning(parent, QStringLiteral("打开方式"), QStringLiteral("文件不存在。"));
        return false;
    }
    QMimeDatabase mimeDatabase;
    const QMimeType mime = mimeDatabase.mimeTypeForFile(filePath);
    const QString defaultDesktop = defaultDesktopForMime(mime.name());

    QVector<OpenWithDialog::Application> applications;
    QSet<QString> seen;
    const QStringList directories = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &directory : directories) {
        QDirIterator it(directory, QStringList() << QStringLiteral("*.desktop"),
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString key = QFileInfo(path).completeBaseName();
            if (seen.contains(key))
                continue;
            const DesktopEntry entry = readDesktopEntry(path);
            if (!entry.valid || entry.type != QStringLiteral("Application")
                || entry.noDisplay || entry.hidden || entry.exec.isEmpty()) {
                continue;
            }
            bool supports = false;
            for (const QString &mimeName : entry.mimeTypes) {
                const QMimeType candidate = mimeDatabase.mimeTypeForName(mimeName);
                if (candidate.isValid()
                    && (candidate.inherits(mime.name()) || mime.inherits(candidate.name()))) {
                    supports = true;
                    break;
                }
            }
            if (!supports)
                continue;
            seen.insert(key);
            OpenWithDialog::Application application;
            application.name = entry.name;
            application.desktopPath = path;
            application.icon = entry.icon;
            application.isDefault = !defaultDesktop.isEmpty()
                && QFileInfo(defaultDesktop).fileName() == QFileInfo(path).fileName();
            applications.append(application);
        }
    }
    if (applications.isEmpty()) {
        UiDialogs::showWarning(parent, QStringLiteral("打开方式"),
                               QStringLiteral("没有找到支持此文件类型的应用。"));
        return false;
    }
    std::sort(applications.begin(), applications.end(),
              [](const OpenWithDialog::Application &a, const OpenWithDialog::Application &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    const bool canSetDefault = !QStandardPaths::findExecutable(QStringLiteral("xdg-mime")).isEmpty();
    OpenWithDialog dialog(filePath, applications, canSetDefault, parent);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    const QString selected = dialog.selectedDesktopPath();
    if (selected.isEmpty())
        return false;
    if (dialog.setAsDefault()) {
        QString defaultError;
        if (!setDefaultDesktopForMime(selected, mime.name(), &defaultError))
            UiDialogs::showWarning(parent, QStringLiteral("设置默认应用"), defaultError);
    }
    QString error;
    if (!launchDesktopEntryWithFile(selected, filePath, &error)) {
        UiDialogs::showWarning(parent, QStringLiteral("打开方式"), error);
        return false;
    }
    return true;
}

OpenWithDialog::OpenWithDialog(const QString &filePath,
                               const QVector<Application> &applications,
                               bool canSetDefault, QWidget *parent)
    : QDialog(parent)
    , m_filePath(filePath)
    , m_list(nullptr)
{
    setWindowTitle(QStringLiteral("打开方式"));
    resize(520, 440);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("选择打开「%1」的应用").arg(QFileInfo(filePath).fileName()), this);
    title->setObjectName(QStringLiteral("DialogTitle"));
    layout->addWidget(title);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("OpenWithList"));
    m_list->setIconSize(QSize(28, 28));
    for (const Application &application : applications) {
        auto *item = new QListWidgetItem(IconProvider::instance().desktopIcon(application.icon),
                                         application.isDefault
                                             ? QStringLiteral("%1（默认）").arg(application.name)
                                             : application.name);
        item->setData(Qt::UserRole, application.desktopPath);
        m_list->addItem(item);
        if (application.isDefault) {
            m_list->setCurrentItem(item);
            item->setSelected(true);
        }
    }
    if (!m_list->currentItem() && m_list->count() > 0)
        m_list->setCurrentRow(0);
    layout->addWidget(m_list, 1);

    m_defaultCheck = new QCheckBox(QStringLiteral("以后用此应用打开此类型文件"), this);
    m_defaultCheck->setVisible(canSetDefault);
    layout->addWidget(m_defaultCheck);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("取消"), this);
    auto *open = new QPushButton(QStringLiteral("打开"), this);
    for (QPushButton *button : {cancel, open}) {
        button->setIcon(QIcon());
        button->setIconSize(QSize(0, 0));
        button->setMinimumHeight(32);
    }
    cancel->setAutoDefault(false);
    open->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(open);
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(open, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        accept();
    });
}

QString OpenWithDialog::selectedDesktopPath() const
{
    if (!m_list || !m_list->currentItem())
        return QString();
    return m_list->currentItem()->data(Qt::UserRole).toString();
}

bool OpenWithDialog::setAsDefault() const
{
    return m_defaultCheck && m_defaultCheck->isChecked();
}
