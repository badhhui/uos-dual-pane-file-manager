#include "ziparchive.h"

#include <QFileInfo>
#include <QtEndian>
#include <zlib.h>

namespace {
quint16 read16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(data.constData() + offset));
}

quint32 read32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + offset));
}

QDateTime dosDateTime(quint16 dosTime, quint16 dosDate)
{
    if (dosDate == 0)
        return QDateTime();
    const QDate date(1980 + (dosDate >> 9), (dosDate >> 5) & 0x0f, dosDate & 0x1f);
    const QTime time((dosTime >> 11) & 0x1f, (dosTime >> 5) & 0x3f, (dosTime & 0x1f) * 2);
    if (!date.isValid() || !time.isValid())
        return QDateTime();
    return QDateTime(date, time);
}

const ZipEntryInfo *findEntry(const QVector<ZipEntryInfo> &entries, const QString &name)
{
    for (const ZipEntryInfo &entry : entries) {
        if (entry.name == name)
            return &entry;
    }
    return nullptr;
}
}

bool ZipArchive::open(const QString &path, QString *error)
{
    m_entries.clear();
    m_file.close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法打开压缩包");
        return false;
    }
    const qint64 size = m_file.size();
    if (size < 22) {
        if (error) *error = QStringLiteral("压缩包格式无效");
        return false;
    }
    const int tailSize = static_cast<int>(qMin<qint64>(size, 65557));
    m_file.seek(size - tailSize);
    const QByteArray tail = m_file.read(tailSize);
    int eocd = -1;
    for (int i = tail.size() - 22; i >= 0; --i) {
        if (read32(tail, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        if (error) *error = QStringLiteral("找不到压缩包目录");
        return false;
    }
    const quint16 count = read16(tail, eocd + 10);
    const quint32 centralOffset = read32(tail, eocd + 16);
    const quint32 centralSize = read32(tail, eocd + 12);
    if (count == 0xffff || centralOffset == 0xffffffff || centralSize == 0xffffffff) {
        if (error) *error = QStringLiteral("暂不支持 ZIP64 压缩包");
        return false;
    }
    if (!m_file.seek(centralOffset)) {
        if (error) *error = QStringLiteral("无法读取压缩包目录");
        return false;
    }
    const QByteArray central = m_file.read(centralSize);
    if (central.size() != static_cast<int>(centralSize)) {
        if (error) *error = QStringLiteral("压缩包目录不完整");
        return false;
    }
    int offset = 0;
    for (quint16 i = 0; i < count; ++i) {
        if (offset + 46 > central.size() || read32(central, offset) != 0x02014b50)
            break;
        ZipEntryInfo entry;
        const quint16 flags = read16(central, offset + 8);
        entry.method = read16(central, offset + 10);
        entry.modified = dosDateTime(read16(central, offset + 12), read16(central, offset + 14));
        entry.compressedSize = read32(central, offset + 20);
        entry.uncompressedSize = read32(central, offset + 24);
        const quint16 nameLength = read16(central, offset + 28);
        const quint16 extraLength = read16(central, offset + 30);
        const quint16 commentLength = read16(central, offset + 32);
        entry.localHeaderOffset = read32(central, offset + 42);
        if (offset + 46 + nameLength > central.size())
            break;
        const QByteArray rawName = central.mid(offset + 46, nameLength);
        entry.name = (flags & 0x0800) ? QString::fromUtf8(rawName)
                                     : QString::fromLocal8Bit(rawName);
        entry.name.replace(QLatin1Char('\\'), QLatin1Char('/'));
        entry.isDir = entry.name.endsWith(QLatin1Char('/'));
        if (entry.compressedSize == 0xffffffff || entry.uncompressedSize == 0xffffffff) {
            if (error) *error = QStringLiteral("暂不支持 ZIP64 压缩包");
            return false;
        }
        m_entries.append(entry);
        offset += 46 + nameLength + extraLength + commentLength;
    }
    return true;
}

const QVector<ZipEntryInfo> &ZipArchive::entries() const
{
    return m_entries;
}

bool ZipArchive::contains(const QString &name) const
{
    return findEntry(m_entries, name) != nullptr;
}

QByteArray ZipArchive::readFile(const QString &name, QString *error) const
{
    const ZipEntryInfo *entry = findEntry(m_entries, name);
    if (!entry || entry->isDir) {
        if (error) *error = QStringLiteral("压缩包中不存在该文件");
        return QByteArray();
    }
    if (entry->compressedSize > 200ull * 1024 * 1024
        || entry->uncompressedSize > 200ull * 1024 * 1024) {
        if (error) *error = QStringLiteral("文件过大，无法预览");
        return QByteArray();
    }
    if (!const_cast<QFile &>(m_file).seek(entry->localHeaderOffset)) {
        if (error) *error = QStringLiteral("无法定位压缩包内容");
        return QByteArray();
    }
    const QByteArray local = m_file.read(30);
    if (local.size() != 30 || read32(local, 0) != 0x04034b50) {
        if (error) *error = QStringLiteral("压缩包内容损坏");
        return QByteArray();
    }
    const quint16 nameLength = read16(local, 26);
    const quint16 extraLength = read16(local, 28);
    if (!m_file.seek(entry->localHeaderOffset + 30 + nameLength + extraLength)) {
        if (error) *error = QStringLiteral("无法读取压缩包内容");
        return QByteArray();
    }
    const QByteArray compressed = m_file.read(static_cast<qint64>(entry->compressedSize));
    if (compressed.size() != static_cast<int>(entry->compressedSize)) {
        if (error) *error = QStringLiteral("压缩包内容不完整");
        return QByteArray();
    }
    if (entry->method == 0)
        return compressed;
    if (entry->method != 8) {
        if (error) *error = QStringLiteral("不支持的压缩算法");
        return QByteArray();
    }
    QByteArray output;
    output.resize(static_cast<int>(entry->uncompressedSize));
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        if (error) *error = QStringLiteral("无法解压文件");
        return QByteArray();
    }
    const int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (result != Z_STREAM_END) {
        if (error) *error = QStringLiteral("解压失败");
        return QByteArray();
    }
    output.resize(static_cast<int>(stream.total_out));
    return output;
}
