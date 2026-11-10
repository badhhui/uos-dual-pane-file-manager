#pragma once

#include <QDateTime>
#include <QString>

QString formatSize(qint64 bytes);
QString formatDateTime(const QDateTime &dateTime);
QString formatCount(qint64 count);
QString formatSpeed(qint64 bytesPerSecond);
QString formatDuration(int seconds);
QString homeDisplayPath(const QString &absolutePath);
QString compactPath(const QString &absolutePath, int maxSegments = 4);
bool isPathInside(const QString &child, const QString &parent);
QString uniqueDestinationPath(const QString &directory, const QString &name);
QString fileExtensionLabel(const QString &suffix);
