#pragma once

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QString>

class IconProvider
{
public:
    enum class DeviceKind {
        System,
        Data,
        Usb,
        External
    };

    static IconProvider &instance();

    QIcon uiIcon(const QString &name, const QColor &color);
    QIcon fileIcon(bool isDirectory, const QString &suffix);
    QIcon desktopIcon(const QString &iconName);
    QIcon deviceIcon(DeviceKind kind);
    QIcon appIcon();
    QIcon appFallbackIcon();

private:
    IconProvider();
    QPixmap tintedPixmap(const QString &resource, const QColor &color);
    QIcon buildFileIcon(bool isDirectory, const QString &suffix);
    QIcon buildDesktopIcon(const QString &iconName);
    QIcon buildDeviceIcon(DeviceKind kind);
    QString resolveIconFile(const QString &iconName) const;

    QHash<QString, QIcon> m_uiCache;
    QHash<QString, QIcon> m_fileCache;
    QHash<QString, QIcon> m_desktopCache;
    QHash<int, QIcon> m_deviceCache;
    QIcon m_appFallback;
    QIcon m_appIcon;
};
