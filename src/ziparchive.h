#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QString>
#include <QVector>

struct ZipEntryInfo
{
    QString name;
    quint64 compressedSize = 0;
    quint64 uncompressedSize = 0;
    QDateTime modified;
    bool isDir = false;
    quint32 localHeaderOffset = 0;
    quint16 method = 0;
};

class ZipArchive
{
public:
    bool open(const QString &path, QString *error = nullptr);
    const QVector<ZipEntryInfo> &entries() const;
    bool contains(const QString &name) const;
    QByteArray readFile(const QString &name, QString *error = nullptr) const;

private:
    mutable QFile m_file;
    QVector<ZipEntryInfo> m_entries;
};
