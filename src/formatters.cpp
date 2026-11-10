#include "formatters.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>

QString formatSize(qint64 bytes)
{
    const double kb = 1024.0;
    const double mb = kb * 1024.0;
    const double gb = mb * 1024.0;
    const double tb = gb * 1024.0;
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < mb)
        return QStringLiteral("%1 KB").arg(QString::number(bytes / kb, 'f', bytes < 10 * 1024 ? 1 : 0));
    if (bytes < gb)
        return QStringLiteral("%1 MB").arg(QString::number(bytes / mb, 'f', bytes < 100 * 1024 * 1024 ? 1 : 0));
    if (bytes < tb)
        return QStringLiteral("%1 GB").arg(QString::number(bytes / gb, 'f', 2));
    return QStringLiteral("%1 TB").arg(QString::number(bytes / tb, 'f', 2));
}

QString formatDateTime(const QDateTime &value)
{
    if (!value.isValid())
        return QStringLiteral("—");
    return value.toString(QStringLiteral("yyyy/MM/dd hh:mm:ss"));
}

QString formatCount(qint64 count)
{
    return QLocale(QLocale::Chinese, QLocale::China).toString(count);
}

QString formatSpeed(qint64 bytesPerSecond)
{
    if (bytesPerSecond <= 0)
        return QString();
    return QStringLiteral("%1/s").arg(formatSize(bytesPerSecond));
}

QString formatDuration(int seconds)
{
    if (seconds < 0)
        return QString();
    if (seconds < 60)
        return QStringLiteral("%1 秒").arg(seconds);
    if (seconds < 3600)
        return QStringLiteral("%1 分 %2 秒").arg(seconds / 60).arg(seconds % 60);
    return QStringLiteral("%1 小时 %2 分").arg(seconds / 3600).arg((seconds % 3600) / 60);
}

QString homeDisplayPath(const QString &absolutePath)
{
    if (absolutePath.isEmpty())
        return QStringLiteral("主目录");
    const QString home = QDir::homePath();
    if (absolutePath == home)
        return QStringLiteral("主目录");
    if (absolutePath.startsWith(home + QLatin1Char('/')))
        return QStringLiteral("主目录/") + absolutePath.mid(home.size() + 1);
    return QDir::toNativeSeparators(absolutePath);
}

QString compactPath(const QString &absolutePath, int maxSegments)
{
    if (absolutePath.isEmpty())
        return QString();
    QString text = homeDisplayPath(absolutePath);
    const QStringList parts = text.split(QLatin1Char('/'), QString::SkipEmptyParts);
    if (parts.size() <= maxSegments)
        return text;
    return QStringLiteral("…/") + parts.mid(parts.size() - maxSegments).join(QLatin1Char('/'));
}

bool isPathInside(const QString &child, const QString &parent)
{
    if (child.isEmpty() || parent.isEmpty())
        return false;
    QString cleanChild = QDir::cleanPath(child);
    QString cleanParent = QDir::cleanPath(parent);
    if (cleanChild == cleanParent)
        return true;
    if (!cleanParent.endsWith(QLatin1Char('/')))
        cleanParent += QLatin1Char('/');
    return cleanChild.startsWith(cleanParent, Qt::CaseSensitive);
}

QString uniqueDestinationPath(const QString &directory, const QString &name)
{
    QFileInfo info(name);
    QString base = info.completeBaseName();
    QString suffix;
    if (!info.suffix().isEmpty())
        suffix = QStringLiteral(".") + info.suffix();
    if (base.isEmpty()) {
        base = name;
        suffix.clear();
    }
    QString candidate = QDir(directory).filePath(name);
    int index = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = QDir(directory).filePath(QStringLiteral("%1 (%2)%3").arg(base).arg(index).arg(suffix));
        ++index;
    }
    return candidate;
}

QString fileExtensionLabel(const QString &suffix)
{
    const QString value = suffix.toUpper();
    if (value.size() <= 4)
        return value;
    return value.left(4);
}
