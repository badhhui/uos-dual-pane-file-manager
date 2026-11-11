#include "previewdialog.h"

#include "formatters.h"
#include "iconprovider.h"
#include "theme.h"
#include "uidialogs.h"
#include "ziparchive.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QMimeDatabase>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextCodec>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>
#include <QWheelEvent>
#include <QXmlStreamReader>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

// Shared with the header for the out-of-line showArchiveTable declaration.
struct ArchiveRow {
    QString name;
    qint64 size = 0;
    QDateTime modified;
    bool isDir = false;
};

struct ArchiveRowList {
    bool ok = false;
    QString error;
    QVector<ArchiveRow> rows;
    int totalCount = 0;
    bool truncated = false;
    bool hasMeta = false;
};

namespace {

// Limits protecting the UI from oversized or hostile content.
constexpr qint64 kMaxXmlBytes = 32 * 1024 * 1024;       // single XML entry inside a container
constexpr int kMaxArchiveRows = 5000;                    // rows shown in archive tables
constexpr int kMaxSheetCount = 10;                       // xlsx sheets parsed
constexpr int kMaxSheetRows = 200;                       // xlsx rows shown per sheet
constexpr int kMaxSheetColumns = 40;                     // xlsx columns shown per sheet
constexpr int kMaxDocxChars = 180000;                    // document characters shown
constexpr int kMaxSlides = 30;                           // pptx slides shown
constexpr int kDirectoryListingLimit = 20;               // folder entries listed
constexpr qint64 kMaxImageBytes = 200 * 1024 * 1024;     // refuse absurd image files

bool looksLikeText(const QString &path, const QByteArray &data)
{
    if (data.contains('\0'))
        return false;
    QMimeDatabase database;
    const QString mime = database.mimeTypeForFile(path).name();
    if (mime.startsWith(QStringLiteral("text/")) || mime.contains(QStringLiteral("json"))
        || mime.contains(QStringLiteral("xml")) || mime.contains(QStringLiteral("script")))
        return true;
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QStringList textSuffixes = {QStringLiteral("txt"), QStringLiteral("md"),
        QStringLiteral("log"), QStringLiteral("ini"), QStringLiteral("conf"),
        QStringLiteral("cfg"), QStringLiteral("yaml"), QStringLiteral("yml"),
        QStringLiteral("json"), QStringLiteral("xml"), QStringLiteral("html"),
        QStringLiteral("css"), QStringLiteral("js"), QStringLiteral("py"),
        QStringLiteral("sh"), QStringLiteral("cpp"), QStringLiteral("h"),
        QStringLiteral("c"), QStringLiteral("csv")};
    return textSuffixes.contains(suffix);
}

// Decode a byte buffer trying UTF-8 (strict) first, then GB18030 / Big5,
// so that legacy Chinese text files are no longer shown as mojibake.
QString decodeTextData(const QByteArray &data)
{
    if (data.startsWith("\xef\xbb\xbf"))
        return QString::fromUtf8(data.mid(3));
    QTextCodec *utf16le = QTextCodec::codecForName("UTF-16LE");
    if (data.startsWith("\xff\xfe") && utf16le)
        return utf16le->toUnicode(data.mid(2));
    QTextCodec *utf16be = QTextCodec::codecForName("UTF-16BE");
    if (data.startsWith("\xfe\xff") && utf16be)
        return utf16be->toUnicode(data.mid(2));

    QTextCodec *utf8 = QTextCodec::codecForMib(106);
    if (utf8) {
        QTextCodec::ConverterState state;
        utf8->toUnicode(data.constData(), data.size(), &state);
        if (state.invalidChars == 0)
            return QString::fromUtf8(data);
    }
    for (const char *name : {"GB18030", "Big5"}) {
        QTextCodec *codec = QTextCodec::codecForName(name);
        if (!codec)
            continue;
        QTextCodec::ConverterState state;
        const QString decoded = codec->toUnicode(data.constData(), data.size(), &state);
        if (state.invalidChars == 0 && !decoded.isEmpty())
            return decoded;
    }
    return QString::fromUtf8(data);
}

QString xmlText(QXmlStreamReader &reader)
{
    return reader.readElementText().toHtmlEscaped();
}

QString attributeValue(const QXmlStreamAttributes &attributes, const QString &localName)
{
    for (const QXmlStreamAttribute &attribute : attributes) {
        if (attribute.name() == localName)
            return attribute.value().toString();
    }
    return QString();
}

int columnFromReference(const QString &reference)
{
    int column = 0;
    for (const QChar ch : reference) {
        if (ch.isLetter())
            column = column * 26 + (ch.toUpper().unicode() - 'A' + 1);
        else
            break;
    }
    return qMax(0, column - 1);
}

int rowFromReference(const QString &reference)
{
    int index = 0;
    while (index < reference.size() && reference.at(index).isLetter())
        ++index;
    return qMax(1, reference.mid(index).toInt());
}

QString cellReference(int column, int row)
{
    QString letters;
    int value = column + 1;
    while (value > 0) {
        const int remainder = (value - 1) % 26;
        letters.prepend(QChar('A' + remainder));
        value = (value - 1) / 26;
    }
    return letters + QString::number(row + 1);
}

// ---------------------------------------------------------------- xlsx helpers

bool isBuiltinDateFormat(int numFmtId)
{
    return (numFmtId >= 14 && numFmtId <= 22) || (numFmtId >= 27 && numFmtId <= 36)
        || (numFmtId >= 45 && numFmtId <= 47) || (numFmtId >= 50 && numFmtId <= 58);
}

int decimalsInFormatCode(const QString &code)
{
    const int dot = code.indexOf(QLatin1Char('.'));
    if (dot < 0)
        return 0;
    int decimals = 0;
    for (int i = dot + 1; i < code.size(); ++i) {
        const QChar ch = code.at(i);
        if (ch == QLatin1Char(';'))
            break;
        if (ch == QLatin1Char('0') || ch == QLatin1Char('#'))
            ++decimals;
        else
            break;
    }
    return decimals;
}

QString formatGroupedNumber(double value, int decimals)
{
    QString result;
    result.reserve(32);
    const bool negative = value < 0;
    const double magnitude = std::fabs(value);
    QString digits = QString::number(magnitude, 'f', decimals);
    const int dotIndex = digits.indexOf(QLatin1Char('.'));
    const QString fraction = dotIndex >= 0 ? digits.mid(dotIndex) : QString();
    if (dotIndex >= 0)
        digits.truncate(dotIndex);
    int count = 0;
    for (int i = digits.size() - 1; i >= 0; --i) {
        result.prepend(digits.at(i));
        if (++count % 3 == 0 && i > 0)
            result.prepend(QLatin1Char(','));
    }
    if (!fraction.isEmpty())
        result += fraction;
    if (negative)
        result.prepend(QLatin1Char('-'));
    return result;
}

QString formatExcelDate(double value)
{
    const double whole = std::floor(value);
    const double fraction = value - whole;
    if (whole < 0 || whole > 2958465)  // beyond year 9999
        return QString::number(value);
    const QDate date = QDate(1899, 12, 30).addDays(static_cast<qint64>(whole));
    if (!date.isValid())
        return QString::number(value);
    if (fraction <= 1e-6)
        return date.toString(QStringLiteral("yyyy-MM-dd"));
    const QTime time = QTime(0, 0).addMSecs(qRound64(fraction * 86400000.0));
    if (!time.isValid())
        return date.toString(QStringLiteral("yyyy-MM-dd"));
    return QDateTime(date, time).toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
}

QString formatExcelValue(const QString &raw, int numFmtId, const QString &formatCode)
{
    bool numericOk = false;
    const double value = raw.toDouble(&numericOk);
    if (!numericOk || raw.isEmpty())
        return raw;
    if (isBuiltinDateFormat(numFmtId))
        return formatExcelDate(value);
    const bool percent = numFmtId == 9 || numFmtId == 10
        || formatCode.contains(QLatin1Char('%'));
    if (percent)
        return QString::number(value * 100.0, 'f', decimalsInFormatCode(formatCode))
            + QStringLiteral("%");
    const int decimals = decimalsInFormatCode(formatCode);
    if (numFmtId == 3 || numFmtId == 4 || formatCode.contains(QLatin1Char(',')))
        return formatGroupedNumber(value, decimals);
    if (formatCode.contains(QLatin1Char('.')) || formatCode.contains(QLatin1Char('0')))
        return QString::number(value, 'f', decimals);
    return raw;
}

// ---------------------------------------------------------------- cache pruning

qint64 directorySizeOnDisk(const QDir &dir)
{
    qint64 total = 0;
    const QFileInfoList items = dir.entryInfoList(QDir::Files | QDir::Dirs
                                                  | QDir::NoDotAndDotDot);
    for (const QFileInfo &item : items) {
        if (item.isDir())
            total += directorySizeOnDisk(QDir(item.absoluteFilePath()));
        else
            total += item.size();
    }
    return total;
}

// Keeps preview caches bounded: entries older than 7 days are dropped, and the
// total footprint is kept under 200 MB by evicting least-recently-used items.
void prunePreviewCache(const QString &rootPath)
{
    QDir root(rootPath);
    if (!root.exists())
        return;
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-7);
    const QFileInfoList items = root.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    QVector<QPair<QFileInfo, qint64>> kept;
    kept.reserve(items.size());
    for (const QFileInfo &item : items) {
        if (item.lastModified() < cutoff) {
            if (item.isDir())
                QDir(item.absoluteFilePath()).removeRecursively();
            else
                QFile::remove(item.absoluteFilePath());
            continue;
        }
        kept.append(qMakePair(item, item.isDir()
            ? directorySizeOnDisk(QDir(item.absoluteFilePath())) : item.size()));
    }
    qint64 total = 0;
    for (const auto &entry : kept)
        total += entry.second;
    const qint64 kMaxCacheBytes = 200 * 1024 * 1024;
    for (int i = kept.size() - 1; i >= 0 && total > kMaxCacheBytes; --i) {
        const QFileInfo &item = kept.at(i).first;
        total -= kept.at(i).second;
        if (item.isDir())
            QDir(item.absoluteFilePath()).removeRecursively();
        else
            QFile::remove(item.absoluteFilePath());
    }
}

// ---------------------------------------------------------------- PDF helpers

int pdfPageCount(const QString &pdfinfo, const QString &path)
{
    QProcess process;
    process.start(pdfinfo, QStringList() << path);
    if (!process.waitForStarted(1500) || !process.waitForFinished(3000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return -1;
    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    static const QRegularExpression expression(QStringLiteral("Pages:\\s*(\\d+)"));
    const QRegularExpressionMatch match = expression.match(output);
    if (!match.hasMatch())
        return -1;
    return match.captured(1).toInt();
}

QString previewCacheKey(const QFileInfo &source, const QString &kind)
{
    const QByteArray key = QCryptographicHash::hash(
        (kind + QLatin1Char(':') + source.absoluteFilePath()
         + QString::number(source.size())
         + QString::number(source.lastModified().toMSecsSinceEpoch())).toUtf8(),
        QCryptographicHash::Sha1).toHex();
    return QString::fromLatin1(key);
}

// ---------------------------------------------------------------- widgets

// QTextBrowser wrapper refusing to fetch anything from the network, so converted
// office documents cannot trigger remote requests while being previewed.
class SafeTextBrowser : public QTextBrowser
{
public:
    explicit SafeTextBrowser(QWidget *parent = nullptr)
        : QTextBrowser(parent)
    {
    }

protected:
    QVariant loadResource(int type, const QUrl &name) override
    {
        const QString scheme = name.scheme().toLower();
        if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
            || scheme == QStringLiteral("ftp"))
            return QVariant();
        return QTextBrowser::loadResource(type, name);
    }
};

// Image viewer with wheel zoom, drag-to-pan and double-click fit/1:1 toggle.
class ImageViewer : public QScrollArea
{
public:
    explicit ImageViewer(const QImage &image, QWidget *parent = nullptr)
        : QScrollArea(parent)
        , m_image(image)
    {
        m_label = new QLabel(this);
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setStyleSheet(QStringLiteral("QLabel { background:white; }"));
        setWidget(m_label);
        setWidgetResizable(false);
        setFrameShape(QFrame::NoFrame);
        setObjectName(QStringLiteral("PreviewImage"));
        setStyleSheet(QStringLiteral("QScrollArea { background:#F3F6FA; border:0; }"));
        fitToWindow();
    }

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        double delta = event->angleDelta().y();
        if (delta == 0)
            delta = event->pixelDelta().y();
        if (delta == 0) {
            QScrollArea::wheelEvent(event);
            return;
        }
        setZoom(m_zoom * (delta > 0 ? 1.2 : 1.0 / 1.2));
        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton
            && (horizontalScrollBar()->isVisible() || verticalScrollBar()->isVisible())) {
            m_panning = true;
            m_lastPos = event->pos();
            viewport()->setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QScrollArea::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_panning) {
            const QPoint delta = event->pos() - m_lastPos;
            m_lastPos = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            event->accept();
            return;
        }
        QScrollArea::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_panning) {
            m_panning = false;
            viewport()->setCursor(Qt::ArrowCursor);
            event->accept();
            return;
        }
        QScrollArea::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (m_fitMode)
                setZoom(1.0);
            else
                fitToWindow();
            event->accept();
            return;
        }
        QScrollArea::mouseDoubleClickEvent(event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QScrollArea::resizeEvent(event);
        if (m_fitMode)
            fitToWindow();
    }

private:
    void setZoom(double zoom)
    {
        m_zoom = qBound(0.05, zoom, 8.0);
        m_fitMode = false;
        applyPixmap();
    }

    void fitToWindow()
    {
        if (m_image.isNull())
            return;
        const QSize avail = viewport()->size() - QSize(12, 12);
        if (avail.width() <= 0 || avail.height() <= 0)
            return;
        const double zoom = qMin(double(avail.width()) / m_image.width(),
                                 double(avail.height()) / m_image.height());
        m_zoom = qBound(0.05, zoom, 1.0);
        m_fitMode = true;
        applyPixmap();
    }

    void applyPixmap()
    {
        if (m_image.isNull())
            return;
        const QSize scaled(qRound(m_image.width() * m_zoom),
                           qRound(m_image.height() * m_zoom));
        m_label->setPixmap(QPixmap::fromImage(m_image).scaled(scaled, Qt::KeepAspectRatio,
                                                              Qt::SmoothTransformation));
        m_label->resize(scaled);
        m_label->setMinimumSize(1, 1);
    }

    QImage m_image;
    QLabel *m_label = nullptr;
    double m_zoom = 1.0;
    bool m_fitMode = true;
    bool m_panning = false;
    QPoint m_lastPos;
};

class BusySpinner : public QWidget
{
public:
    explicit BusySpinner(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(46, 46);
        m_timer.setInterval(40);
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            m_angle = (m_angle + 28) % 360;
            update();
        });
        m_timer.start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(Theme::instance().accent(), 3.0);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawArc(QRectF(7, 7, width() - 14, height() - 14),
                        m_angle * 16, 105 * 16);
    }

private:
    QTimer m_timer;
    int m_angle = 0;
};

// ---------------------------------------------------------------- CSV parsing

QVector<QStringList> parseCsv(const QString &text, QChar delimiter)
{
    QVector<QStringList> rows;
    QStringList current;
    QString field;
    bool inQuotes = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (inQuotes) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += ch;
            }
        } else if (ch == QLatin1Char('"') && field.isEmpty()) {
            inQuotes = true;
        } else if (ch == delimiter) {
            current.append(field);
            field.clear();
        } else if (ch == QLatin1Char('\r')) {
            continue;
        } else if (ch == QLatin1Char('\n')) {
            current.append(field);
            field.clear();
            if (!(current.size() == 1 && current.first().isEmpty()))
                rows.append(current);
            current.clear();
        } else {
            field += ch;
        }
    }
    current.append(field);
    if (!(current.size() == 1 && current.first().isEmpty()))
        rows.append(current);
    return rows;
}

QChar detectCsvDelimiter(const QString &text)
{
    const int firstLineEnd = text.indexOf(QLatin1Char('\n'));
    const QString line = firstLineEnd < 0 ? text : text.left(firstLineEnd);
    const int tabs = line.count(QLatin1Char('\t'));
    const int commas = line.count(QLatin1Char(','));
    const int semicolons = line.count(QLatin1Char(';'));
    if (tabs > 0 && tabs >= commas && tabs >= semicolons)
        return QLatin1Char('\t');
    if (semicolons > commas && semicolons > 0)
        return QLatin1Char(';');
    return QLatin1Char(',');
}

// ---------------------------------------------------------------- parse payloads

struct DocxParseResult {
    bool ok = false;
    QString html;
    bool truncated = false;
};

struct XlsxSheetData {
    QString name;
    QVector<QVector<QString>> cells;
    int maxRow = 0;
    int maxColumn = 0;
    int totalRows = 0;
    bool truncated = false;
};

struct XlsxParseResult {
    bool ok = false;
    QVector<XlsxSheetData> sheets;
    int sheetCount = 0;
    bool sheetLimit = false;
};

struct PptxSlideData {
    int number = 0;
    QString title;
    QString body;
};

struct PptxParseResult {
    bool ok = false;
    QVector<PptxSlideData> slides;
    int totalSlides = 0;
    bool truncated = false;
};

struct OdtParagraph {
    QString text;
    int level = 0;  // 0 = normal paragraph, 1..3 = heading levels
};

struct OdtParseResult {
    bool ok = false;
    QVector<OdtParagraph> paragraphs;
    bool truncated = false;
};

struct OfdParseResult {
    bool ok = false;
    QStringList paragraphs;
    QVector<QByteArray> images;
};

struct PdfTextResult {
    bool ok = false;
    QString text;
};

// ---------------------------------------------------------------- docx parsing

DocxParseResult parseDocx(const QString &path)
{
    DocxParseResult result;
    ZipArchive archive;
    if (!archive.open(path, nullptr))
        return result;
    const ZipEntryInfo *entry = nullptr;
    for (const ZipEntryInfo &info : archive.entries()) {
        if (info.name == QStringLiteral("word/document.xml")) {
            entry = &info;
            break;
        }
    }
    if (!entry || entry->uncompressedSize > kMaxXmlBytes)
        return result;
    const QByteArray xml = archive.readFile(QStringLiteral("word/document.xml"), nullptr);
    if (xml.isEmpty())
        return result;
    QXmlStreamReader reader(xml);
    QString html;
    QString paragraph;
    QString style;
    bool inParagraph = false;
    int characters = 0;
    while (!reader.atEnd() && characters < kMaxDocxChars) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            if (name == QStringLiteral("p")) {
                inParagraph = true;
                paragraph.clear();
                style.clear();
            } else if (inParagraph && name == QStringLiteral("pStyle")) {
                style = attributeValue(reader.attributes(), QStringLiteral("val"));
            } else if (inParagraph && name == QStringLiteral("t")) {
                const QString text = xmlText(reader);
                paragraph += text;
                characters += text.size();
            } else if (inParagraph && name == QStringLiteral("tab")) {
                paragraph += QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;");
            } else if (inParagraph && name == QStringLiteral("br")) {
                paragraph += QStringLiteral("<br/>");
            }
        } else if (reader.isEndElement() && reader.name() == QStringLiteral("p")
                   && inParagraph) {
            inParagraph = false;
            if (paragraph.trimmed().isEmpty())
                continue;
            QString tag = QStringLiteral("p");
            if (style.contains(QStringLiteral("Title"), Qt::CaseInsensitive)
                || style.contains(QStringLiteral("标题"), Qt::CaseInsensitive))
                tag = QStringLiteral("h1");
            else if (style.contains(QStringLiteral("Heading1"), Qt::CaseInsensitive)
                     || style.contains(QStringLiteral("Heading 1"), Qt::CaseInsensitive))
                tag = QStringLiteral("h2");
            else if (style.contains(QStringLiteral("Heading"), Qt::CaseInsensitive))
                tag = QStringLiteral("h3");
            html += QStringLiteral("<%1>%2</%1>").arg(tag, paragraph);
        }
    }
    const bool truncated = characters >= kMaxDocxChars;
    if (reader.hasError() || html.trimmed().isEmpty())
        return result;
    if (truncated)
        html += QStringLiteral("<p style=\"color:#8a94a3\">[文档较长，仅预览前 %1 字符]</p>")
            .arg(kMaxDocxChars);
    result.ok = true;
    result.html = html;
    result.truncated = truncated;
    return result;
}

// ---------------------------------------------------------------- xlsx parsing

void parseSharedStrings(const ZipArchive &archive, QStringList &sharedStrings)
{
    const QByteArray sharedXml = archive.readFile(QStringLiteral("xl/sharedStrings.xml"),
                                                  nullptr);
    if (sharedXml.isEmpty() || sharedXml.size() > kMaxXmlBytes)
        return;
    QXmlStreamReader shared(sharedXml);
    bool inString = false;
    QString current;
    while (!shared.atEnd()) {
        shared.readNext();
        if (shared.isStartElement() && shared.name() == QStringLiteral("si")) {
            inString = true;
            current.clear();
        } else if (inString && shared.isStartElement()
                   && shared.name() == QStringLiteral("t")) {
            current += shared.readElementText();
        } else if (shared.isEndElement() && shared.name() == QStringLiteral("si")) {
            sharedStrings.append(current);
            inString = false;
        }
    }
}

void parseSheet(const QByteArray &sheetXml, const QStringList &sharedStrings,
                const QVector<int> &xfNumFmtIds,
                const QHash<int, QString> &customFormats, XlsxSheetData &sheet)
{
    QXmlStreamReader reader(sheetXml);
    QString cellRef;
    QString cellType;
    QString styleIndex;
    QString value;
    QString inlineValue;
    bool dimensionRead = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            if (name == QStringLiteral("dimension") && !dimensionRead) {
                dimensionRead = true;
                const QString ref = reader.attributes().value(QStringLiteral("ref"))
                    .toString();
                const int colon = ref.indexOf(QLatin1Char(':'));
                if (colon > 0)
                    sheet.totalRows = rowFromReference(ref.mid(colon + 1));
                else if (!ref.isEmpty())
                    sheet.totalRows = rowFromReference(ref);
            } else if (name == QStringLiteral("c")) {
                cellRef = reader.attributes().value(QStringLiteral("r")).toString();
                cellType = reader.attributes().value(QStringLiteral("t")).toString();
                styleIndex = reader.attributes().value(QStringLiteral("s")).toString();
                value.clear();
                inlineValue.clear();
            } else if (name == QStringLiteral("v")) {
                value = reader.readElementText();
            } else if (name == QStringLiteral("t")
                       && cellType == QStringLiteral("inlineStr")) {
                inlineValue += reader.readElementText();
            }
        } else if (reader.isEndElement() && reader.name() == QStringLiteral("c")) {
            const int column = columnFromReference(cellRef);
            const int row = rowFromReference(cellRef) - 1;
            if (row < 0 || row >= kMaxSheetRows || column < 0 || column >= kMaxSheetColumns)
                continue;
            QString cellValue = cellType == QStringLiteral("inlineStr") ? inlineValue : value;
            if (cellType == QStringLiteral("s")) {
                const int index = cellValue.toInt();
                if (index >= 0 && index < sharedStrings.size())
                    cellValue = sharedStrings.at(index);
            } else if (cellType == QStringLiteral("b")) {
                cellValue = cellValue == QStringLiteral("1")
                    ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
            } else if (cellType == QStringLiteral("str") || cellType.isEmpty()) {
                const int style = styleIndex.toInt();
                const int numFmtId = xfNumFmtIds.value(style, 0);
                cellValue = formatExcelValue(cellValue, numFmtId,
                                             customFormats.value(numFmtId));
            }
            if (!cellValue.isEmpty()) {
                if (sheet.cells.size() <= row)
                    sheet.cells.resize(row + 1);
                if (sheet.cells[row].size() <= column)
                    sheet.cells[row].resize(column + 1);
                sheet.cells[row][column] = cellValue;
                sheet.maxRow = qMax(sheet.maxRow, row + 1);
                sheet.maxColumn = qMax(sheet.maxColumn, column + 1);
            }
        }
    }
}

XlsxParseResult parseXlsx(const QString &path)
{
    XlsxParseResult result;
    ZipArchive archive;
    if (!archive.open(path, nullptr))
        return result;

    // workbook.xml gives the real sheet order and names.
    QVector<QPair<QString, QString>> sheetList;  // name, relationship id
    const QByteArray workbookXml = archive.readFile(QStringLiteral("xl/workbook.xml"),
                                                    nullptr);
    if (!workbookXml.isEmpty() && workbookXml.size() <= kMaxXmlBytes) {
        QXmlStreamReader reader(workbookXml);
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement() && reader.name() == QStringLiteral("sheet")) {
                const QString name = attributeValue(reader.attributes(),
                                                    QStringLiteral("name"));
                const QString rid = attributeValue(reader.attributes(),
                                                   QStringLiteral("id"));
                if (!rid.isEmpty())
                    sheetList.append(qMakePair(name.isEmpty()
                        ? QStringLiteral("工作表") : name, rid));
            }
        }
    }

    QHash<QString, QString> relTargets;
    const QByteArray relsXml = archive.readFile(QStringLiteral("xl/_rels/workbook.xml.rels"),
                                                nullptr);
    if (!relsXml.isEmpty() && relsXml.size() <= kMaxXmlBytes) {
        QXmlStreamReader reader(relsXml);
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement() && reader.name() == QStringLiteral("Relationship")) {
                const QString id = attributeValue(reader.attributes(),
                                                  QStringLiteral("Id"));
                QString target = attributeValue(reader.attributes(),
                                                QStringLiteral("Target"));
                if (!id.isEmpty() && !target.isEmpty()) {
                    if (target.startsWith(QLatin1Char('/')))
                        target.remove(0, 1);
                    else if (!target.startsWith(QStringLiteral("xl/")))
                        target.prepend(QStringLiteral("xl/"));
                    relTargets.insert(id, target);
                }
            }
        }
    }

    // Number formats: builtin ids are interpreted directly; custom codes are
    // collected from styles.xml.
    QHash<int, QString> customFormats;
    QVector<int> xfNumFmtIds;
    const QByteArray stylesXml = archive.readFile(QStringLiteral("xl/styles.xml"), nullptr);
    if (!stylesXml.isEmpty() && stylesXml.size() <= kMaxXmlBytes) {
        QXmlStreamReader reader(stylesXml);
        bool inCellXfs = false;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement()) {
                const QString name = reader.name().toString();
                if (name == QStringLiteral("numFmt")) {
                    const int id = attributeValue(reader.attributes(),
                                                  QStringLiteral("numFmtId")).toInt();
                    const QString code = attributeValue(reader.attributes(),
                                                        QStringLiteral("formatCode"));
                    if (id > 0 && !code.isEmpty())
                        customFormats.insert(id, code);
                } else if (name == QStringLiteral("cellXfs")) {
                    inCellXfs = true;
                } else if (inCellXfs && name == QStringLiteral("xf")) {
                    xfNumFmtIds.append(attributeValue(reader.attributes(),
                                                      QStringLiteral("numFmtId")).toInt());
                }
            } else if (reader.isEndElement()
                       && reader.name() == QStringLiteral("cellXfs")) {
                inCellXfs = false;
            }
        }
    }

    QStringList sharedStrings;
    parseSharedStrings(archive, sharedStrings);

    auto appendSheetData = [&](const QString &name, const QString &target) {
        const ZipEntryInfo *entry = nullptr;
        for (const ZipEntryInfo &info : archive.entries()) {
            if (info.name == target) {
                entry = &info;
                break;
            }
        }
        if (!entry || entry->uncompressedSize > kMaxXmlBytes)
            return;
        const QByteArray sheetXml = archive.readFile(target, nullptr);
        if (sheetXml.isEmpty())
            return;
        XlsxSheetData sheet;
        sheet.name = name;
        parseSheet(sheetXml, sharedStrings, xfNumFmtIds, customFormats, sheet);
        if (sheet.totalRows > sheet.maxRow && sheet.maxRow > 0)
            sheet.truncated = true;
        if (sheet.maxRow > 0 && sheet.maxColumn > 0)
            result.sheets.append(sheet);
    };

    if (!sheetList.isEmpty()) {
        result.sheetCount = sheetList.size();
        result.sheetLimit = sheetList.size() > kMaxSheetCount;
        int index = 0;
        for (const auto &entry : sheetList) {
            if (index++ >= kMaxSheetCount)
                break;
            const QString target = relTargets.value(entry.second);
            if (target.isEmpty())
                continue;
            appendSheetData(entry.first, target);
        }
    }
    if (result.sheets.isEmpty()) {
        // Fall back to scanning worksheets directly when workbook.xml is missing.
        const QRegularExpression sheetExpression(
            QStringLiteral("^xl/worksheets/sheet\\d+\\.xml$"));
        int index = 0;
        for (const ZipEntryInfo &entry : archive.entries()) {
            if (index++ >= kMaxSheetCount)
                break;
            if (!sheetExpression.match(entry.name).hasMatch())
                continue;
            appendSheetData(QFileInfo(entry.name).completeBaseName(), entry.name);
        }
        result.sheetLimit = false;
    }
    result.ok = !result.sheets.isEmpty();
    return result;
}

// ---------------------------------------------------------------- pptx parsing

PptxParseResult parsePptx(const QString &path)
{
    PptxParseResult result;
    ZipArchive archive;
    if (!archive.open(path, nullptr))
        return result;
    struct SlideEntry { int number; QString name; };
    QVector<SlideEntry> slides;
    const QRegularExpression expression(QStringLiteral("^ppt/slides/slide(\\d+)\\.xml$"));
    for (const ZipEntryInfo &entry : archive.entries()) {
        const QRegularExpressionMatch match = expression.match(entry.name);
        if (match.hasMatch())
            slides.append({match.captured(1).toInt(), entry.name});
    }
    std::sort(slides.begin(), slides.end(), [](const SlideEntry &a, const SlideEntry &b) {
        return a.number < b.number;
    });
    if (slides.isEmpty())
        return result;
    result.totalSlides = slides.size();
    result.truncated = slides.size() > kMaxSlides;
    QString error;
    int count = 0;
    for (const SlideEntry &slideEntry : slides) {
        if (count++ >= kMaxSlides)
            break;
        const ZipEntryInfo *entry = nullptr;
        for (const ZipEntryInfo &info : archive.entries()) {
            if (info.name == slideEntry.name) {
                entry = &info;
                break;
            }
        }
        if (!entry || entry->uncompressedSize > kMaxXmlBytes)
            continue;
        const QByteArray xml = archive.readFile(slideEntry.name, &error);
        if (xml.isEmpty())
            continue;
        QXmlStreamReader reader(xml);
        QStringList paragraphs;
        QString current;
        bool inParagraph = false;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement() && reader.name() == QStringLiteral("p")) {
                inParagraph = true;
                current.clear();
            } else if (inParagraph && reader.isStartElement()
                       && reader.name() == QStringLiteral("t")) {
                current += reader.readElementText();
            } else if (reader.isEndElement() && reader.name() == QStringLiteral("p")
                       && inParagraph) {
                if (!current.trimmed().isEmpty())
                    paragraphs.append(current);
                inParagraph = false;
            }
        }
        PptxSlideData slide;
        slide.number = slideEntry.number;
        if (!paragraphs.isEmpty())
            slide.title = paragraphs.first();
        for (int i = 1; i < paragraphs.size(); ++i) {
            if (!slide.body.isEmpty())
                slide.body += QLatin1Char('\n');
            slide.body += paragraphs.at(i);
        }
        result.slides.append(slide);
    }
    result.ok = !result.slides.isEmpty();
    return result;
}

// ---------------------------------------------------------------- odt parsing

OdtParseResult parseOdt(const QString &path)
{
    OdtParseResult result;
    ZipArchive archive;
    if (!archive.open(path, nullptr))
        return result;
    const ZipEntryInfo *entry = nullptr;
    for (const ZipEntryInfo &info : archive.entries()) {
        if (info.name == QStringLiteral("content.xml")) {
            entry = &info;
            break;
        }
    }
    if (!entry || entry->uncompressedSize > kMaxXmlBytes)
        return result;
    const QByteArray xml = archive.readFile(QStringLiteral("content.xml"), nullptr);
    if (xml.isEmpty())
        return result;
    QXmlStreamReader reader(xml);
    QVector<OdtParagraph> paragraphs;
    QString paragraph;
    int level = 0;
    bool inParagraph = false;
    int characters = 0;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            if (name == QStringLiteral("p") || name == QStringLiteral("h")) {
                inParagraph = true;
                paragraph.clear();
                level = 0;
                if (name == QStringLiteral("h")) {
                    const QString outline = attributeValue(reader.attributes(),
                        QStringLiteral("outline-level"));
                    bool ok = false;
                    const int value = outline.toInt(&ok);
                    level = ok ? qBound(1, value, 3) : 1;
                }
            } else if (inParagraph && name == QStringLiteral("line-break")) {
                paragraph += QLatin1Char('\n');
            } else if (inParagraph && name == QStringLiteral("s")) {
                bool ok = false;
                int spaces = attributeValue(reader.attributes(),
                                            QStringLiteral("c")).toInt(&ok);
                if (!ok || spaces <= 0)
                    spaces = 1;
                paragraph += QString(spaces, QLatin1Char(' '));
            } else if (inParagraph && name == QStringLiteral("tab")) {
                paragraph += QStringLiteral("    ");
            }
        } else if (reader.isCharacters() && inParagraph) {
            const QString text = reader.text().toString();
            paragraph += text;
            characters += text.size();
        } else if (reader.isEndElement() && (reader.name() == QStringLiteral("p")
                                             || reader.name() == QStringLiteral("h"))
                   && inParagraph) {
            inParagraph = false;
            if (!paragraph.trimmed().isEmpty()) {
                OdtParagraph item;
                item.text = paragraph;
                item.level = level;
                paragraphs.append(item);
            }
        }
        if (characters >= kMaxDocxChars) {
            result.truncated = true;
            break;
        }
    }
    if (paragraphs.isEmpty())
        return result;
    result.ok = true;
    result.paragraphs = paragraphs;
    return result;
}

// ---------------------------------------------------------------- ofd parsing

OfdParseResult parseOfd(const QString &path)
{
    OfdParseResult result;
    ZipArchive archive;
    if (!archive.open(path, nullptr))
        return result;
    // Sort XML entries so the text follows page order (Doc_0/Pages/Page_N/...).
    QStringList xmlNames;
    QStringList imageNames;
    for (const ZipEntryInfo &entry : archive.entries()) {
        if (entry.isDir)
            continue;
        const QString lower = entry.name.toLower();
        if (lower.endsWith(QStringLiteral(".xml")))
            xmlNames.append(entry.name);
        else if (lower.endsWith(QStringLiteral(".png"))
                 || lower.endsWith(QStringLiteral(".jpg"))
                 || lower.endsWith(QStringLiteral(".jpeg"))
                 || lower.endsWith(QStringLiteral(".bmp")))
            imageNames.append(entry.name);
    }
    std::sort(xmlNames.begin(), xmlNames.end());
    std::sort(imageNames.begin(), imageNames.end());
    QSet<QString> seen;
    int characters = 0;
    for (const QString &name : xmlNames) {
        const ZipEntryInfo *entry = nullptr;
        for (const ZipEntryInfo &info : archive.entries()) {
            if (info.name == name) {
                entry = &info;
                break;
            }
        }
        if (!entry || entry->uncompressedSize > kMaxXmlBytes)
            continue;
        const QByteArray xml = archive.readFile(name, nullptr);
        if (xml.isEmpty())
            continue;
        QXmlStreamReader reader(xml);
        while (!reader.atEnd()) {
            reader.readNext();
            if (!reader.isStartElement())
                continue;
            const QString element = reader.name().toString();
            if (element == QStringLiteral("TextCode")
                || element == QStringLiteral("Text")
                || element == QStringLiteral("Title")) {
                const QString text = reader.readElementText().trimmed();
                if (text.isEmpty() || seen.contains(text))
                    continue;
                seen.insert(text);
                result.paragraphs.append(text);
                characters += text.size();
            }
        }
        if (characters >= kMaxDocxChars || result.paragraphs.size() >= 5000)
            break;
    }
    for (const QString &name : imageNames) {
        if (result.images.size() >= 6)
            break;
        const QByteArray data = archive.readFile(name, nullptr);
        if (!data.isEmpty())
            result.images.append(data);
    }
    result.ok = !result.paragraphs.isEmpty() || !result.images.isEmpty();
    return result;
}

// ---------------------------------------------------------------- archives

ArchiveRowList parseZipArchive(const QString &path)
{
    ArchiveRowList result;
    ZipArchive archive;
    QString error;
    if (!archive.open(path, &error)) {
        result.error = error;
        return result;
    }
    result.ok = true;
    result.hasMeta = true;
    result.totalCount = archive.entries().size();
    result.truncated = result.totalCount > kMaxArchiveRows;
    for (const ZipEntryInfo &entry : archive.entries()) {
        if (result.rows.size() >= kMaxArchiveRows)
            break;
        ArchiveRow row;
        row.name = entry.name;
        row.size = static_cast<qint64>(entry.uncompressedSize);
        row.modified = entry.modified;
        row.isDir = entry.isDir;
        result.rows.append(row);
    }
    return result;
}

ArchiveRowList parseTarListing(const QString &tar, const QString &path)
{
    ArchiveRowList result;
    QProcess process;
    process.start(tar, QStringList() << QStringLiteral("-tf") << path);
    if (!process.waitForStarted(2500) || !process.waitForFinished(15000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.error = QStringLiteral("无法读取 tar 归档列表");
        return result;
    }
    const QStringList names = QString::fromLocal8Bit(process.readAllStandardOutput())
                                  .split(QLatin1Char('\n'), QString::SkipEmptyParts);
    result.ok = true;
    result.totalCount = names.size();
    result.truncated = names.size() > kMaxArchiveRows;
    for (const QString &name : names) {
        if (result.rows.size() >= kMaxArchiveRows)
            break;
        ArchiveRow row;
        row.name = name;
        row.isDir = name.endsWith(QLatin1Char('/'));
        result.rows.append(row);
    }
    return result;
}

ArchiveRowList parseSevenZipListing(const QString &sevenz, const QString &path)
{
    ArchiveRowList result;
    QProcess process;
    process.start(sevenz, QStringList() << QStringLiteral("l") << QStringLiteral("-slt")
                                        << path);
    if (!process.waitForStarted(2500) || !process.waitForFinished(20000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.error = QStringLiteral("无法读取压缩包列表（7z）");
        return result;
    }
    const QStringList lines = QString::fromLocal8Bit(process.readAllStandardOutput())
        .split(QLatin1Char('\n'));
    bool entriesStarted = false;
    ArchiveRow current;
    bool haveCurrent = false;
    auto flush = [&]() {
        if (haveCurrent) {
            ++result.totalCount;
            if (result.rows.size() < kMaxArchiveRows)
                result.rows.append(current);
        }
        haveCurrent = false;
        current = ArchiveRow();
    };
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QStringLiteral("----------"))) {
            flush();
            entriesStarted = true;
            continue;
        }
        if (!entriesStarted)
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == QStringLiteral("Path")) {
            flush();
            haveCurrent = true;
            current.name = value;
        } else if (!haveCurrent) {
            continue;
        } else if (key == QStringLiteral("Size")) {
            current.size = value.toLongLong();
        } else if (key == QStringLiteral("Modified")) {
            current.modified = QDateTime::fromString(value,
                QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        } else if (key == QStringLiteral("Attributes")) {
            current.isDir = value.startsWith(QLatin1Char('D'));
        }
    }
    flush();
    if (result.totalCount == 0) {
        result.error = QStringLiteral("压缩包内没有可列出的条目");
        return result;
    }
    result.ok = true;
    result.hasMeta = true;
    result.truncated = result.totalCount > kMaxArchiveRows;
    return result;
}

}  // namespace

// =====================================================================
// PreviewDialog
// =====================================================================

PreviewDialog::PreviewDialog(const QString &path, QWidget *parent)
    : PreviewDialog(QStringList() << path, path, parent)
{
}

PreviewDialog::PreviewDialog(const QStringList &paths, const QString &currentPath,
                             QWidget *parent)
    : QDialog(parent)
    , m_paths(paths)
{
    if (m_paths.isEmpty())
        m_paths.append(QString());
    m_index = m_paths.indexOf(currentPath);
    if (m_index < 0)
        m_index = 0;
    setObjectName(QStringLiteral("PreviewDialog"));
    resize(940, 680);
    buildShell();
    prunePreviewCache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                      + QStringLiteral("/preview"));
    prunePreviewCache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                      + QStringLiteral("/preview-pdf"));
    loadCurrent();
}

PreviewDialog::~PreviewDialog()
{
    cancelWpsConversion();
    cancelPdfRender();
}

QString PreviewDialog::currentPath() const
{
    return m_path;
}

void PreviewDialog::setPaths(const QStringList &paths, const QString &currentPath)
{
    m_paths = paths;
    if (m_paths.isEmpty())
        m_paths.append(QString());
    m_index = m_paths.indexOf(currentPath);
    if (m_index < 0)
        m_index = 0;
    loadCurrent();
}

void PreviewDialog::buildShell()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);

    auto *header = new QHBoxLayout();
    header->setSpacing(12);
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(48, 48);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    header->addWidget(m_iconLabel);
    auto *titles = new QVBoxLayout();
    titles->setSpacing(1);
    m_nameLabel = new QLabel(this);
    m_nameLabel->setObjectName(QStringLiteral("DialogTitle"));
    titles->addWidget(m_nameLabel);
    m_metaLabel = new QLabel(this);
    m_metaLabel->setObjectName(QStringLiteral("DialogSubtitle"));
    m_metaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titles->addWidget(m_metaLabel);
    header->addLayout(titles, 1);
    layout->addLayout(header);

    m_content = new QFrame(this);
    m_content->setObjectName(QStringLiteral("PreviewContent"));
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    layout->addWidget(m_content, 1);

    auto *buttons = new QHBoxLayout();
    auto *hint = new QLabel(QStringLiteral("↑ / ↓ 切换文件 · 图片支持滚轮缩放"), this);
    hint->setObjectName(QStringLiteral("DialogSubtitle"));
    buttons->addWidget(hint);
    buttons->addStretch(1);
    m_openButton = new QPushButton(QStringLiteral("用默认应用打开"), this);
    auto *close = new QPushButton(QStringLiteral("关闭"), this);
    for (QPushButton *button : {m_openButton, close}) {
        button->setIcon(QIcon());
        button->setIconSize(QSize(0, 0));
        button->setMinimumHeight(32);
    }
    close->setDefault(true);
    buttons->addWidget(m_openButton);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(m_openButton, &QPushButton::clicked, this, [this]() {
        if (!m_path.isEmpty() && !QDesktopServices::openUrl(QUrl::fromLocalFile(m_path)))
            UiDialogs::showWarning(this, QStringLiteral("打开文件"),
                                   QStringLiteral("没有可用的打开方式。"));
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    setFocus();
}

// Arrow keys and space are handled in keyPressEvent instead of QShortcut so
// that read-only content widgets keep their keys: the focused text/table view
// consumes arrow keys for scrolling/navigation, and any unmapped key bubbles
// up to here to switch files or close the preview.
void PreviewDialog::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    if (key == Qt::Key_Escape || key == Qt::Key_Space) {
        accept();
        return;
    }
    if (m_paths.size() > 1) {
        if (key == Qt::Key_Up) {
            m_index = (m_index - 1 + m_paths.size()) % m_paths.size();
            loadCurrent();
            return;
        }
        if (key == Qt::Key_Down) {
            m_index = (m_index + 1) % m_paths.size();
            loadCurrent();
            return;
        }
    }
    QDialog::keyPressEvent(event);
}

void PreviewDialog::loadCurrent()
{
    if (m_paths.isEmpty())
        return;
    clearContent();
    ++m_generation;
    m_path = m_paths.at(qBound(0, m_index, m_paths.size() - 1));
    const QFileInfo info(m_path);
    setWindowTitle(QStringLiteral("预览 · %1").arg(info.fileName()));
    m_iconLabel->setPixmap(IconProvider::instance().fileIcon(info.isDir(),
                                                             info.suffix().toLower())
                               .pixmap(42, 42));
    m_nameLabel->setText(info.fileName());
    if (info.isDir()) {
        m_metaLabel->setText(QStringLiteral("文件夹 · %1").arg(info.absolutePath()));
    } else {
        m_metaLabel->setText(QStringLiteral("%1 · %2 · %3")
                                 .arg(formatSize(info.size()), formatDateTime(info.lastModified()),
                                      info.absolutePath()));
    }

    const QString suffix = info.suffix().toLower();
    bool built = false;
    if (info.isDir()) {
        buildDirectoryPreview(m_path);
        built = true;
    } else if (suffix == QStringLiteral("csv") || suffix == QStringLiteral("tsv")) {
        built = buildCsvPreview(m_path, suffix);
        if (!built)
            built = buildTextPreview(m_path);
    } else if (suffix == QStringLiteral("docx") || suffix == QStringLiteral("xlsx")
               || suffix == QStringLiteral("pptx") || suffix == QStringLiteral("odt")) {
        built = buildDocumentPreview(m_path, suffix);
        if (!built && (suffix == QStringLiteral("docx") || suffix == QStringLiteral("xlsx")))
            built = buildWpsPreview(m_path, suffix);
    } else if (suffix == QStringLiteral("pdf")) {
        built = buildPdfPreview(m_path);
    } else if (suffix == QStringLiteral("ofd")) {
        built = buildOfdPreview(m_path);
    } else if (suffix == QStringLiteral("doc") || suffix == QStringLiteral("xls")
               || suffix == QStringLiteral("ppt") || suffix == QStringLiteral("wps")
               || suffix == QStringLiteral("et") || suffix == QStringLiteral("dps")) {
        built = buildWpsPreview(m_path, suffix);
    } else if (suffix == QStringLiteral("zip") || suffix == QStringLiteral("tar")
               || suffix == QStringLiteral("tgz") || suffix == QStringLiteral("7z")
               || suffix == QStringLiteral("rar")
               || info.fileName().endsWith(QStringLiteral(".tar.gz"), Qt::CaseInsensitive)) {
        built = buildArchivePreview(m_path, info.fileName().endsWith(QStringLiteral(".tar.gz"),
                                                                     Qt::CaseInsensitive)
                                               ? QStringLiteral("tar")
                                               : suffix);
    } else {
        built = buildImagePreview(m_path);
        if (!built)
            built = buildTextPreview(m_path);
    }
    if (!built)
        buildFallbackPreview(m_path, QStringLiteral("此文件类型暂不支持内置预览"));
}

template<typename T>
void PreviewDialog::runAsync(const QString &path, std::function<T()> work,
                             std::function<void(const T &)> render)
{
    const int generation = m_generation;
    auto *watcher = new QFutureWatcher<T>(this);
    QObject::connect(watcher, &QFutureWatcherBase::finished, this,
        [this, watcher, generation, path, render]() {
            watcher->deleteLater();
            if (generation != m_generation || m_path != path)
                return;
            render(watcher->result());
        });
    watcher->setFuture(QtConcurrent::run(work));
}

void PreviewDialog::showHtml(const QString &html)
{
    clearContent();
    auto *browser = new SafeTextBrowser(m_content);
    browser->setObjectName(QStringLiteral("PreviewDocument"));
    browser->setOpenExternalLinks(false);
    browser->setFrameShape(QFrame::NoFrame);
    browser->setStyleSheet(QStringLiteral("QTextBrowser { background:white; border:0; }"));
    browser->document()->setDocumentMargin(34);
    browser->setHtml(html);
    m_contentLayout->addWidget(browser, 1);
}

void PreviewDialog::showBusySpinner(const QString &text)
{
    auto *container = new QWidget(m_content);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->setAlignment(Qt::AlignCenter);
    auto *spinner = new BusySpinner(container);
    layout->addWidget(spinner, 0, Qt::AlignHCenter);
    auto *label = new QLabel(text, container);
    label->setObjectName(QStringLiteral("DialogSubtitle"));
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label, 0, Qt::AlignHCenter);
    m_contentLayout->addWidget(container, 1, Qt::AlignCenter);
}

void PreviewDialog::cancelWpsConversion()
{
    if (m_wpsProcess) {
        disconnect(m_wpsProcess, nullptr, this, nullptr);
        if (m_wpsProcess->state() != QProcess::NotRunning) {
            m_wpsProcess->kill();
            m_wpsProcess->waitForFinished(1500);
        }
        m_wpsProcess->deleteLater();
        m_wpsProcess = nullptr;
    }
    if (m_wpsTemp) {
        delete m_wpsTemp;
        m_wpsTemp = nullptr;
    }
    m_wpsPendingPath.clear();
}

void PreviewDialog::clearContent()
{
    cancelWpsConversion();
    cancelPdfRender();
    while (QLayoutItem *item = m_contentLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

QString PreviewDialog::documentStyle() const
{
    return QStringLiteral(
        "<style>"
        "body { background:#F3F6FA; color:#1F2937; font-size:13px; line-height:1.75; }"
        "h1 { font-size:28px; font-weight:700; margin:8px 0 22px; }"
        "h2 { font-size:20px; font-weight:600; margin:22px 0 12px; }"
        "h3 { font-size:16px; font-weight:600; margin:18px 0 8px; }"
        "p { margin:0 0 11px; }"
        ".page { background:#FFFFFF; padding:42px 48px; }"
        "</style>");
}

bool PreviewDialog::buildDocumentPreview(const QString &path, const QString &suffix)
{
    if (suffix == QStringLiteral("docx"))
        return buildDocxPreview(path);
    if (suffix == QStringLiteral("xlsx"))
        return buildXlsxPreview(path);
    if (suffix == QStringLiteral("pptx"))
        return buildPptxPreview(path);
    if (suffix == QStringLiteral("odt"))
        return buildOdtPreview(path);
    return false;
}

bool PreviewDialog::buildDocxPreview(const QString &path)
{
    if (!QFileInfo::exists(path))
        return false;
    showBusySpinner(QStringLiteral("正在解析文档…"));
    runAsync<DocxParseResult>(path, [path]() { return parseDocx(path); },
        [this, path](const DocxParseResult &result) {
            if (!result.ok) {
                clearContent();
                if (!buildWpsPreview(path, QStringLiteral("docx")))
                    buildFallbackPreview(path, QStringLiteral("无法解析此文档"));
                return;
            }
            clearContent();
            auto *pageHost = new QWidget(m_content);
            pageHost->setObjectName(QStringLiteral("PreviewPageHost"));
            pageHost->setStyleSheet(
                QStringLiteral("QWidget#PreviewPageHost { background:#E8EDF4; }"));
            auto *hostLayout = new QHBoxLayout(pageHost);
            hostLayout->setContentsMargins(36, 20, 36, 20);
            hostLayout->addStretch(1);
            auto *page = new QFrame(pageHost);
            page->setObjectName(QStringLiteral("PreviewPage"));
            page->setFixedWidth(720);
            page->setStyleSheet(
                QStringLiteral("QFrame#PreviewPage { background:white; border-radius:4px; }"));
            auto *pageLayout = new QVBoxLayout(page);
            pageLayout->setContentsMargins(0, 0, 0, 0);
            auto *browser = new SafeTextBrowser(page);
            browser->setObjectName(QStringLiteral("PreviewDocument"));
            browser->setOpenExternalLinks(false);
            browser->setFrameShape(QFrame::NoFrame);
            browser->setStyleSheet(
                QStringLiteral("QTextBrowser { background:white; border:0; }"));
            browser->document()->setDocumentMargin(46);
            browser->setHtml(QStringLiteral("<html><head>%1</head><body>%2</body></html>")
                                 .arg(documentStyle(), result.html));
            pageLayout->addWidget(browser, 1);
            hostLayout->addWidget(page, 1);
            hostLayout->addStretch(1);
            m_contentLayout->addWidget(pageHost, 1);
        });
    return true;
}

bool PreviewDialog::buildXlsxPreview(const QString &path)
{
    if (!QFileInfo::exists(path))
        return false;
    showBusySpinner(QStringLiteral("正在解析表格…"));
    runAsync<XlsxParseResult>(path, [path]() { return parseXlsx(path); },
        [this, path](const XlsxParseResult &result) {
            if (!result.ok) {
                clearContent();
                if (!buildWpsPreview(path, QStringLiteral("xlsx")))
                    buildFallbackPreview(path, QStringLiteral("无法解析此表格"));
                return;
            }
            clearContent();
            if (result.sheetLimit) {
                auto *caption = new QLabel(
                    QStringLiteral("工作表预览 · 显示前 %1 个，共 %2 个工作表")
                        .arg(result.sheets.size()).arg(result.sheetCount), m_content);
                caption->setObjectName(QStringLiteral("DialogSubtitle"));
                caption->setContentsMargins(14, 10, 14, 4);
                m_contentLayout->addWidget(caption);
            }
            auto *tabs = new QTabWidget(m_content);
            tabs->setObjectName(QStringLiteral("PreviewSheets"));
            tabs->setDocumentMode(true);
            for (const XlsxSheetData &sheet : result.sheets) {
                auto *page = new QWidget(tabs);
                auto *pageLayout = new QVBoxLayout(page);
                pageLayout->setContentsMargins(0, 0, 0, 0);
                pageLayout->setSpacing(0);
                QString captionText = QStringLiteral("%1 · 共 %2 行 × %3 列")
                    .arg(sheet.name).arg(qMax(sheet.totalRows, sheet.maxRow))
                    .arg(sheet.maxColumn);
                if (sheet.truncated || sheet.maxRow > kMaxSheetRows
                    || sheet.maxColumn > kMaxSheetColumns)
                    captionText += QStringLiteral(" · 仅显示前 %1 行 × %2 列")
                        .arg(kMaxSheetRows).arg(kMaxSheetColumns);
                auto *caption = new QLabel(captionText, page);
                caption->setObjectName(QStringLiteral("DialogSubtitle"));
                caption->setContentsMargins(14, 10, 14, 4);
                pageLayout->addWidget(caption);
                auto *table = new QTableWidget(sheet.maxRow, sheet.maxColumn, page);
                table->setObjectName(QStringLiteral("PreviewSheet"));
                table->setShowGrid(true);
                table->setAlternatingRowColors(false);
                table->setEditTriggers(QAbstractItemView::NoEditTriggers);
                table->setSelectionMode(QAbstractItemView::NoSelection);
                table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
                table->verticalHeader()->setDefaultSectionSize(26);
                for (int column = 0; column < sheet.maxColumn; ++column)
                    table->setHorizontalHeaderItem(column,
                        new QTableWidgetItem(cellReference(column, 0).chopped(1)));
                for (int row = 0; row < sheet.maxRow; ++row) {
                    for (int column = 0; column < sheet.maxColumn; ++column) {
                        const QString text = row < sheet.cells.size()
                            && column < sheet.cells[row].size()
                            ? sheet.cells[row][column] : QString();
                        table->setItem(row, column, new QTableWidgetItem(text));
                    }
                }
                table->resizeColumnsToContents();
                pageLayout->addWidget(table, 1);
                tabs->addTab(page, sheet.name);
            }
            m_contentLayout->addWidget(tabs, 1);
        });
    return true;
}

bool PreviewDialog::buildPptxPreview(const QString &path)
{
    if (!QFileInfo::exists(path))
        return false;
    showBusySpinner(QStringLiteral("正在解析演示文稿…"));
    runAsync<PptxParseResult>(path, [path]() { return parsePptx(path); },
        [this, path](const PptxParseResult &result) {
            if (!result.ok) {
                clearContent();
                buildFallbackPreview(path, QStringLiteral("无法解析此演示文稿"));
                return;
            }
            clearContent();
            if (result.truncated) {
                auto *caption = new QLabel(
                    QStringLiteral("幻灯片预览 · 显示前 %1 页，共 %2 页")
                        .arg(result.slides.size()).arg(result.totalSlides), m_content);
                caption->setObjectName(QStringLiteral("DialogSubtitle"));
                caption->setContentsMargins(14, 10, 14, 4);
                m_contentLayout->addWidget(caption);
            }
            auto *scroll = new QScrollArea(m_content);
            scroll->setObjectName(QStringLiteral("PreviewSlides"));
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setStyleSheet(QStringLiteral("QScrollArea { background:#E8EDF4; border:0; }"));
            auto *host = new QWidget(scroll);
            auto *hostLayout = new QVBoxLayout(host);
            hostLayout->setContentsMargins(24, 20, 24, 20);
            hostLayout->setSpacing(18);
            for (const PptxSlideData &slideData : result.slides) {
                auto *slide = new QFrame(host);
                slide->setObjectName(QStringLiteral("SlidePreview"));
                slide->setFixedHeight(300);
                slide->setStyleSheet(QStringLiteral("QFrame#SlidePreview { background:#FFFFFF; "
                                                    "border:1px solid #DCE3EC; border-radius:8px; }"));
                auto *slideLayout = new QVBoxLayout(slide);
                slideLayout->setContentsMargins(34, 28, 34, 28);
                slideLayout->setSpacing(10);
                auto *title = new QLabel(slideData.title.isEmpty()
                                             ? QStringLiteral("空白幻灯片") : slideData.title,
                                         slide);
                title->setStyleSheet(QStringLiteral("background:transparent; color:#1F2937;"));
                QFont titleFont = title->font();
                titleFont.setPixelSize(20);
                titleFont.setBold(true);
                title->setFont(titleFont);
                title->setWordWrap(true);
                slideLayout->addWidget(title);
                auto *bodyLabel = new QLabel(slideData.body, slide);
                bodyLabel->setWordWrap(true);
                bodyLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
                bodyLabel->setObjectName(QStringLiteral("DialogSubtitle"));
                bodyLabel->setStyleSheet(QStringLiteral("background:transparent; color:#475569;"));
                slideLayout->addWidget(bodyLabel, 1);
                auto *number = new QLabel(QStringLiteral("幻灯片 %1").arg(slideData.number),
                                          slide);
                number->setObjectName(QStringLiteral("DialogSubtitle"));
                slideLayout->addWidget(number, 0, Qt::AlignRight);
                hostLayout->addWidget(slide);
            }
            hostLayout->addStretch(1);
            scroll->setWidget(host);
            m_contentLayout->addWidget(scroll, 1);
        });
    return true;
}

bool PreviewDialog::buildOdtPreview(const QString &path)
{
    if (!QFileInfo::exists(path))
        return false;
    showBusySpinner(QStringLiteral("正在解析文档…"));
    runAsync<OdtParseResult>(path, [path]() { return parseOdt(path); },
        [this, path](const OdtParseResult &result) {
            if (!result.ok) {
                clearContent();
                buildFallbackPreview(path, QStringLiteral("无法解析此文档"));
                return;
            }
            clearContent();
            QString html;
            for (const OdtParagraph &paragraph : result.paragraphs) {
                QString text = paragraph.text.toHtmlEscaped();
                text.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
                if (paragraph.level == 1)
                    html += QStringLiteral("<h1>%1</h1>").arg(text);
                else if (paragraph.level == 2)
                    html += QStringLiteral("<h2>%1</h2>").arg(text);
                else if (paragraph.level == 3)
                    html += QStringLiteral("<h3>%1</h3>").arg(text);
                else
                    html += QStringLiteral("<p>%1</p>").arg(text);
            }
            if (result.truncated)
                html += QStringLiteral("<p style=\"color:#8a94a3\">[文档较长，仅预览前 %1 字符]</p>")
                    .arg(kMaxDocxChars);
            auto *browser = new SafeTextBrowser(m_content);
            browser->setObjectName(QStringLiteral("PreviewDocument"));
            browser->setFrameShape(QFrame::NoFrame);
            browser->setHtml(QStringLiteral(
                "<html><head>%1</head><body><div class=\"page\">%2</div></body></html>")
                    .arg(documentStyle(), html));
            m_contentLayout->addWidget(browser, 1);
        });
    return true;
}

bool PreviewDialog::buildWpsPreview(const QString &path, const QString &suffix)
{
    // wps/et/dps files are sometimes plain OOXML containers in disguise.
    // (docx/xlsx are NOT delegated here to avoid a parse-failure loop; the
    // async docx/xlsx builders retry through this path themselves.)
    ZipArchive archive;
    if (archive.open(path, nullptr)) {
        if (suffix == QStringLiteral("wps")
            && archive.contains(QStringLiteral("word/document.xml")))
            return buildDocxPreview(path);
        if (suffix == QStringLiteral("et")) {
            for (const ZipEntryInfo &entry : archive.entries()) {
                if (entry.name.startsWith(QStringLiteral("xl/worksheets/sheet")))
                    return buildXlsxPreview(path);
            }
        }
        if (suffix == QStringLiteral("dps")
            && archive.contains(QStringLiteral("ppt/slides/slide1.xml")))
            return buildPptxPreview(path);
    }

    const QString soffice = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    const QString libreoffice = QStandardPaths::findExecutable(QStringLiteral("libreoffice"));
    const QString converter = !soffice.isEmpty() ? soffice : libreoffice;
    if (converter.isEmpty())
        return buildWpsTextFallback(path);

    const QFileInfo source(path);
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/preview");
    QDir().mkpath(cacheDir);
    const QString cachePath = QDir(cacheDir).filePath(previewCacheKey(source,
        QStringLiteral("wps")) + QStringLiteral(".html"));
    if (QFileInfo::exists(cachePath)) {
        QFile cached(cachePath);
        if (cached.open(QIODevice::ReadOnly)) {
            const QString html = QString::fromUtf8(cached.readAll());
            cached.close();
            if (!html.isEmpty()) {
                showHtml(html);
                return true;
            }
        }
    }

    showBusySpinner(QStringLiteral("正在加载办公文档…"));

    m_wpsTemp = new QTemporaryDir();
    if (!m_wpsTemp->isValid()) {
        delete m_wpsTemp;
        m_wpsTemp = nullptr;
        clearContent();
        return buildWpsTextFallback(path);
    }
    m_wpsPendingPath = path;
    m_wpsProcess = new QProcess(this);
    connect(m_wpsProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, path, cachePath](int exitCode, QProcess::ExitStatus exitStatus) {
        QProcess *process = m_wpsProcess;
        m_wpsProcess = nullptr;
        if (process)
            process->deleteLater();
        QTemporaryDir *temp = m_wpsTemp;
        m_wpsTemp = nullptr;
        const bool stillCurrent = m_path == path && m_wpsPendingPath == path;
        m_wpsPendingPath.clear();
        if (!stillCurrent) {
            delete temp;
            return;
        }
        QString html;
        if (exitStatus == QProcess::NormalExit && exitCode == 0 && temp) {
            const QFileInfoList outputs = QDir(temp->path()).entryInfoList(
                QStringList() << QStringLiteral("*.html"), QDir::Files);
            if (!outputs.isEmpty()) {
                QFile file(outputs.first().absoluteFilePath());
                if (file.open(QIODevice::ReadOnly)) {
                    html = QString::fromUtf8(file.readAll());
                    file.close();
                }
            }
        }
        if (!html.isEmpty()) {
            QFile cache(cachePath);
            if (cache.open(QIODevice::WriteOnly | QIODevice::Truncate))
                cache.write(html.toUtf8());
        }
        delete temp;
        if (!html.isEmpty()) {
            showHtml(html);
        } else {
            clearContent();
            if (!buildWpsTextFallback(path))
                buildFallbackPreview(path, QStringLiteral("无法解析此办公文档"));
        }
    });
    connect(m_wpsProcess, &QProcess::errorOccurred, this, [this, path](QProcess::ProcessError) {
        if (!m_wpsProcess || m_wpsProcess->state() != QProcess::NotRunning)
            return;
        QProcess *process = m_wpsProcess;
        m_wpsProcess = nullptr;
        if (process)
            process->deleteLater();
        if (m_path == path && m_wpsPendingPath == path) {
            m_wpsPendingPath.clear();
            clearContent();
            if (!buildWpsTextFallback(path))
                buildFallbackPreview(path, QStringLiteral("无法解析此办公文档"));
        }
    });
    m_wpsProcess->start(converter, QStringList() << QStringLiteral("--headless")
                                                  << QStringLiteral("--convert-to")
                                                  << QStringLiteral("html")
                                                  << QStringLiteral("--outdir")
                                                  << m_wpsTemp->path() << path);
    if (m_wpsProcess->state() == QProcess::NotRunning) {
        clearContent();
        return buildWpsTextFallback(path);
    }
    QTimer::singleShot(20000, this, [this, path]() {
        if (m_wpsProcess && m_wpsPendingPath == path
            && m_wpsProcess->state() != QProcess::NotRunning)
            m_wpsProcess->kill();
    });
    return true;
}

bool PreviewDialog::buildWpsTextFallback(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.read(4 * 1024 * 1024);
    file.close();
    QStringList extracted;
    auto collect = [&extracted](const QString &candidate) {
        const QStringList lines = candidate.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                                  QString::SkipEmptyParts);
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.size() >= 3 && !extracted.contains(trimmed))
                extracted.append(trimmed);
            if (extracted.size() >= 4000)
                break;
        }
    };
    QString utf16;
    utf16.reserve(data.size() / 2);
    for (int i = 0; i + 1 < data.size(); i += 2) {
        const ushort value = static_cast<uchar>(data.at(i))
            | (static_cast<ushort>(static_cast<uchar>(data.at(i + 1))) << 8);
        const QChar ch(value);
        if (ch.isPrint() || ch == QLatin1Char('\n') || ch == QLatin1Char('\r')
            || ch == QLatin1Char('\t'))
            utf16.append(ch);
        else
            utf16.append(QLatin1Char('\n'));
        if (utf16.size() > 500000)
            break;
    }
    collect(utf16);
    QString utf8;
    utf8.reserve(data.size());
    for (char raw : data) {
        const uchar byte = static_cast<uchar>(raw);
        if (byte == 0)
            utf8.append(QLatin1Char('\n'));
        else if (byte >= 32 || byte == '\n' || byte == '\r' || byte == '\t')
            utf8.append(QChar(byte));
        else
            utf8.append(QLatin1Char('\n'));
        if (utf8.size() > 500000)
            break;
    }
    collect(QString::fromUtf8(utf8.toUtf8()));
    if (extracted.isEmpty())
        return false;
    auto *text = new QPlainTextEdit(m_content);
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setPlainText(QStringLiteral("办公文档文本内容（版式可能不完整）\n\n")
                       + extracted.join(QLatin1Char('\n')));
    m_contentLayout->addWidget(text, 1);
    return true;
}

void PreviewDialog::cancelPdfRender()
{
    if (m_pdfProcess) {
        disconnect(m_pdfProcess, nullptr, this, nullptr);
        if (m_pdfProcess->state() != QProcess::NotRunning) {
            m_pdfProcess->kill();
            m_pdfProcess->waitForFinished(1500);
        }
        m_pdfProcess->deleteLater();
        m_pdfProcess = nullptr;
    }
    if (m_pdfTemp) {
        delete m_pdfTemp;
        m_pdfTemp = nullptr;
    }
    m_pdfPendingPath.clear();
}

void PreviewDialog::showPdfPages(const QVector<QPixmap> &pages, int totalPages)
{
    clearContent();
    if (totalPages > pages.size()) {
        auto *caption = new QLabel(QStringLiteral("PDF 预览 · 前 %1 页 / 共 %2 页")
                                       .arg(pages.size()).arg(totalPages), m_content);
        caption->setObjectName(QStringLiteral("DialogSubtitle"));
        caption->setContentsMargins(14, 10, 14, 4);
        m_contentLayout->addWidget(caption);
    }
    auto *scroll = new QScrollArea(m_content);
    scroll->setObjectName(QStringLiteral("PreviewPdf"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    auto *pageHost = new QWidget(scroll);
    pageHost->setObjectName(QStringLiteral("PreviewPdfPages"));
    auto *pageLayout = new QVBoxLayout(pageHost);
    pageLayout->setContentsMargins(16, 16, 16, 16);
    pageLayout->setSpacing(12);
    pageLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    for (const QPixmap &pixmap : pages) {
        auto *label = new QLabel(pageHost);
        label->setObjectName(QStringLiteral("PreviewPdfPage"));
        QPixmap scaled = pixmap;
        if (scaled.width() > 820)
            scaled = scaled.scaledToWidth(820, Qt::SmoothTransformation);
        label->setPixmap(scaled);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("QLabel { background:white; border:0; }"));
        pageLayout->addWidget(label, 0, Qt::AlignHCenter);
    }
    scroll->setWidget(pageHost);
    m_contentLayout->addWidget(scroll, 1);
}

bool PreviewDialog::buildPdfPreview(const QString &path)
{
    const QString pdftoppm = QStandardPaths::findExecutable(QStringLiteral("pdftoppm"));
    const QString pdftotext = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));

    auto runTextFallback = [this, pdftotext, path](const QString &reason) {
        if (pdftotext.isEmpty()) {
            clearContent();
            buildFallbackPreview(path, reason);
            return;
        }
        showBusySpinner(QStringLiteral("正在提取 PDF 文本…"));
        runAsync<PdfTextResult>(path, [pdftotext, path]() {
            PdfTextResult result;
            QProcess process;
            process.start(pdftotext, QStringList() << QStringLiteral("-layout") << path
                                                    << QStringLiteral("-"));
            if (process.waitForStarted(1000) && process.waitForFinished(8000)
                && process.exitStatus() == QProcess::NormalExit
                && process.exitCode() == 0) {
                const QString text = QString::fromUtf8(process.readAllStandardOutput());
                if (!text.trimmed().isEmpty()) {
                    result.ok = true;
                    result.text = text;
                }
            }
            return result;
        }, [this, path, reason](const PdfTextResult &result) {
            clearContent();
            if (result.ok) {
                auto *view = new QPlainTextEdit(m_content);
                view->setObjectName(QStringLiteral("PreviewPdfText"));
                view->setReadOnly(true);
                view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
                view->setPlainText(QStringLiteral("PDF 文本预览（%1）\n\n").arg(reason)
                                       + result.text);
                m_contentLayout->addWidget(view, 1);
            } else {
                buildFallbackPreview(path,
                                     QStringLiteral("PDF 渲染失败，请安装 poppler-utils"));
            }
        });
    };

    if (pdftoppm.isEmpty()) {
        runTextFallback(QStringLiteral("未安装 pdftoppm，仅显示文本层"));
        return true;
    }

    const QFileInfo source(path);
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/preview-pdf");
    const QString cachePageDir = QDir(cacheRoot).filePath(previewCacheKey(source,
        QStringLiteral("pdf")));

    // Reuse a previous render when the file has not changed.
    const QStringList cachedPages = QDir(cachePageDir).entryList(
        QStringList() << QStringLiteral("page-*.png"), QDir::Files, QDir::Name);
    if (!cachedPages.isEmpty()) {
        QVector<QPixmap> pixmaps;
        for (const QString &name : cachedPages) {
            QPixmap pixmap(QDir(cachePageDir).filePath(name));
            if (!pixmap.isNull())
                pixmaps.append(pixmap);
        }
        if (!pixmaps.isEmpty()) {
            showPdfPages(pixmaps, pdfPageCount(
                QStandardPaths::findExecutable(QStringLiteral("pdfinfo")), path));
            return true;
        }
    }
    if (!QDir().mkpath(cachePageDir))
        return false;

    const QString pdfinfo = QStandardPaths::findExecutable(QStringLiteral("pdfinfo"));
    m_pdfTemp = new QTemporaryDir();
    if (!m_pdfTemp->isValid()) {
        delete m_pdfTemp;
        m_pdfTemp = nullptr;
        return false;
    }
    const QString prefix = QDir(m_pdfTemp->path()).filePath(QStringLiteral("page"));
    showBusySpinner(QStringLiteral("正在渲染 PDF…"));
    m_pdfPendingPath = path;
    m_pdfProcess = new QProcess(this);
    connect(m_pdfProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, path, cachePageDir, pdfinfo, runTextFallback](int exitCode,
                                                                QProcess::ExitStatus exitStatus) {
        QProcess *process = m_pdfProcess;
        m_pdfProcess = nullptr;
        if (process)
            process->deleteLater();
        QTemporaryDir *temp = m_pdfTemp;
        m_pdfTemp = nullptr;
        const bool stillCurrent = m_path == path && m_pdfPendingPath == path;
        m_pdfPendingPath.clear();
        if (!stillCurrent) {
            delete temp;
            return;
        }
        QVector<QPixmap> pixmaps;
        if (exitStatus == QProcess::NormalExit && exitCode == 0 && temp) {
            const QStringList pages = QDir(temp->path()).entryList(
                QStringList() << QStringLiteral("page-*.png"), QDir::Files, QDir::Name);
            for (const QString &name : pages) {
                QPixmap pixmap(QDir(temp->path()).filePath(name));
                if (!pixmap.isNull())
                    pixmaps.append(pixmap);
            }
        }
        if (!pixmaps.isEmpty()) {
            // Persist rendered pages so re-opening the same file is instant.
            QDir cacheDir(cachePageDir);
            const QStringList pages = QDir(temp->path()).entryList(
                QStringList() << QStringLiteral("page-*.png"), QDir::Files, QDir::Name);
            for (const QString &name : pages)
                QFile::copy(QDir(temp->path()).filePath(name), cacheDir.filePath(name));
        }
        delete temp;
        if (!pixmaps.isEmpty()) {
            showPdfPages(pixmaps, pdfPageCount(pdfinfo, path));
            return;
        }
        runTextFallback(QStringLiteral("页面渲染失败"));
    });
    m_pdfProcess->start(pdftoppm, QStringList()
        << QStringLiteral("-png") << QStringLiteral("-r") << QStringLiteral("100")
        << QStringLiteral("-f") << QStringLiteral("1") << QStringLiteral("-l") << QStringLiteral("8")
        << path << prefix);
    if (m_pdfProcess->state() == QProcess::NotRunning) {
        clearContent();
        return false;
    }
    QTimer::singleShot(30000, this, [this, path]() {
        if (m_pdfProcess && m_pdfPendingPath == path
            && m_pdfProcess->state() != QProcess::NotRunning)
            m_pdfProcess->kill();
    });
    return true;
}

bool PreviewDialog::buildOfdPreview(const QString &path)
{
    if (!QFileInfo::exists(path))
        return false;
    showBusySpinner(QStringLiteral("正在解析 OFD 文档…"));
    runAsync<OfdParseResult>(path, [path]() { return parseOfd(path); },
        [this, path](const OfdParseResult &result) {
            if (!result.paragraphs.isEmpty()) {
                clearContent();
                QString html;
                for (const QString &paragraph : result.paragraphs)
                    html += QStringLiteral("<p>%1</p>").arg(paragraph.toHtmlEscaped());
                auto *browser = new SafeTextBrowser(m_content);
                browser->setObjectName(QStringLiteral("PreviewDocument"));
                browser->setFrameShape(QFrame::NoFrame);
                browser->setStyleSheet(
                    QStringLiteral("QTextBrowser { background:white; border:0; }"));
                browser->document()->setDocumentMargin(40);
                browser->setHtml(QStringLiteral(
                    "<html><head>%1</head><body><div class=\"page\">"
                    "<p style=\"color:#8a94a3\">OFD 文本预览（版式可能不完整）</p>%2"
                    "</div></body></html>").arg(documentStyle(), html));
                m_contentLayout->addWidget(browser, 1);
                return;
            }
            if (!result.images.isEmpty()) {
                clearContent();
                auto *scroll = new QScrollArea(m_content);
                scroll->setObjectName(QStringLiteral("PreviewOfd"));
                scroll->setFrameShape(QFrame::NoFrame);
                scroll->setWidgetResizable(true);
                auto *host = new QWidget(scroll);
                auto *layout = new QVBoxLayout(host);
                layout->setContentsMargins(16, 16, 16, 16);
                layout->setSpacing(12);
                layout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
                for (const QByteArray &data : result.images) {
                    QPixmap pixmap;
                    if (!pixmap.loadFromData(data))
                        continue;
                    auto *label = new QLabel(host);
                    label->setObjectName(QStringLiteral("PreviewOfdPage"));
                    if (pixmap.width() > 820)
                        pixmap = pixmap.scaledToWidth(820, Qt::SmoothTransformation);
                    label->setPixmap(pixmap);
                    label->setStyleSheet(
                        QStringLiteral("QLabel { background:white; border:0; }"));
                    layout->addWidget(label, 0, Qt::AlignHCenter);
                }
                scroll->setWidget(host);
                if (host->layout()->count() > 0) {
                    m_contentLayout->addWidget(scroll, 1);
                    return;
                }
            }
            clearContent();
            buildFallbackPreview(path, QStringLiteral("无法解析此 OFD 文档"));
        });
    return true;
}

bool PreviewDialog::buildArchivePreview(const QString &path, const QString &suffix)
{
    showBusySpinner(QStringLiteral("正在读取压缩包…"));
    auto showError = [this, path](const ArchiveRowList &result) {
        clearContent();
        buildFallbackPreview(path, result.error.isEmpty()
            ? QStringLiteral("无法读取压缩包") : result.error);
    };
    if (suffix == QStringLiteral("zip")) {
        runAsync<ArchiveRowList>(path, [path]() { return parseZipArchive(path); },
            [this, showError](const ArchiveRowList &result) {
                if (!result.ok)
                    showError(result);
                else
                    showArchiveTable(result);
            });
        return true;
    }
    if (suffix == QStringLiteral("tar") || suffix == QStringLiteral("tgz")) {
        const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar"));
        if (tar.isEmpty()) {
            clearContent();
            buildFallbackPreview(path, QStringLiteral("未找到 tar 工具，无法预览"));
            return true;
        }
        runAsync<ArchiveRowList>(path, [tar, path]() { return parseTarListing(tar, path); },
            [this, showError](const ArchiveRowList &result) {
                if (!result.ok)
                    showError(result);
                else
                    showArchiveTable(result);
            });
        return true;
    }
    const QString sevenz = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (sevenz.isEmpty()) {
        clearContent();
        buildFallbackPreview(path, QStringLiteral("预览 %1 文件需要安装 p7zip（7z）")
                                 .arg(suffix.toUpper()));
        return true;
    }
    runAsync<ArchiveRowList>(path, [sevenz, path]() {
        return parseSevenZipListing(sevenz, path);
    }, [this, showError](const ArchiveRowList &result) {
        if (!result.ok)
            showError(result);
        else
            showArchiveTable(result);
    });
    return true;
}

void PreviewDialog::showArchiveTable(const ArchiveRowList &rows)
{
    clearContent();
    if (rows.rows.isEmpty()) {
        buildFallbackPreview(m_path, QStringLiteral("压缩包内没有可显示的条目"));
        return;
    }
    QVector<ArchiveRow> sorted = rows.rows;
    QSet<QString> directoryPaths;
    for (const ArchiveRow &row : sorted) {
        QString name = row.name;
        if (name.endsWith(QLatin1Char('/')))
            name.chop(1);
        if (row.isDir && !name.isEmpty())
            directoryPaths.insert(name);
        const QStringList parts = name.split(QLatin1Char('/'), QString::SkipEmptyParts);
        QString prefix;
        for (int i = 0; i + 1 < parts.size(); ++i) {
            if (!prefix.isEmpty())
                prefix += QLatin1Char('/');
            prefix += parts.at(i);
            directoryPaths.insert(prefix);
        }
    }
    for (const QString &directory : directoryPaths) {
        bool exists = false;
        for (const ArchiveRow &row : sorted) {
            if (row.isDir && row.name == directory + QLatin1Char('/')) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            ArchiveRow row;
            row.name = directory;
            row.isDir = true;
            sorted.append(row);
        }
    }
    std::sort(sorted.begin(), sorted.end(), [](const ArchiveRow &a, const ArchiveRow &b) {
        if (a.isDir != b.isDir)
            return a.isDir;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    QString captionText = QStringLiteral("压缩包内容 · 共 %1 项").arg(rows.totalCount);
    if (rows.truncated)
        captionText += QStringLiteral(" · 仅显示前 %1 项").arg(rows.rows.size());
    auto *caption = new QLabel(captionText, m_content);
    caption->setObjectName(QStringLiteral("DialogSubtitle"));
    caption->setContentsMargins(14, 10, 14, 4);
    m_contentLayout->addWidget(caption);
    auto *table = new QTableWidget(sorted.size(), 3, m_content);
    table->setObjectName(QStringLiteral("PreviewArchive"));
    table->setHorizontalHeaderLabels(QStringList()
        << QStringLiteral("名称") << QStringLiteral("大小") << QStringLiteral("修改时间"));
    table->verticalHeader()->setVisible(false);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setIconSize(QSize(20, 20));
    table->verticalHeader()->setDefaultSectionSize(24);
    QPalette tablePalette = table->palette();
    tablePalette.setColor(QPalette::AlternateBase, Theme::instance().alternateRow());
    table->setPalette(tablePalette);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    for (int row = 0; row < sorted.size(); ++row) {
        const ArchiveRow &entry = sorted.at(row);
        const QString suffix = QFileInfo(entry.name).suffix().toLower();
        const QString displayName = entry.isDir && entry.name.endsWith(QLatin1Char('/'))
            ? entry.name.chopped(1) : entry.name;
        auto *name = new QTableWidgetItem(
            IconProvider::instance().fileIcon(entry.isDir, suffix), displayName);
        auto *size = new QTableWidgetItem(entry.isDir ? QStringLiteral("—")
                                                       : formatSize(entry.size));
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *date = new QTableWidgetItem(entry.modified.isValid()
                                               ? formatDateTime(entry.modified)
                                               : QStringLiteral("—"));
        date->setForeground(Theme::instance().textMuted());
        table->setItem(row, 0, name);
        table->setItem(row, 1, size);
        table->setItem(row, 2, date);
    }
    m_contentLayout->addWidget(table, 1);
}

bool PreviewDialog::buildImagePreview(const QString &path)
{
    if (QFileInfo(path).size() > kMaxImageBytes)
        return false;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (!reader.canRead())
        return false;
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid() && (sourceSize.width() > 2400 || sourceSize.height() > 1800))
        reader.setScaledSize(sourceSize.scaled(QSize(2400, 1800), Qt::KeepAspectRatio));
    const QImage image = reader.read();
    if (image.isNull())
        return false;
    m_contentLayout->addWidget(new ImageViewer(image, m_content), 1);
    return true;
}

bool PreviewDialog::buildTextPreview(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.read(512 * 1024);
    file.close();
    if (!looksLikeText(path, data))
        return false;
    auto *text = new QPlainTextEdit(m_content);
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setPlainText(decodeTextData(data));
    if (QFileInfo(path).size() > data.size())
        text->appendPlainText(QStringLiteral("\n\n[仅预览前 512 KB]"));
    m_contentLayout->addWidget(text, 1);
    return true;
}

bool PreviewDialog::buildCsvPreview(const QString &path, const QString &suffix)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.read(512 * 1024);
    file.close();
    const QString text = decodeTextData(data);
    const QChar delimiter = suffix == QStringLiteral("tsv")
        ? QLatin1Char('\t') : detectCsvDelimiter(text);
    const QVector<QStringList> rows = parseCsv(text, delimiter);
    if (rows.isEmpty())
        return false;
    int maxColumns = 1;
    for (const QStringList &row : rows)
        maxColumns = qMax(maxColumns, row.size());
    maxColumns = qMin(maxColumns, kMaxSheetColumns);
    const int shownRows = qMin(rows.size(), kMaxSheetRows);
    const QString kind = suffix == QStringLiteral("tsv")
        ? QStringLiteral("TSV") : QStringLiteral("CSV");
    auto *caption = new QLabel(QStringLiteral("%1 预览 · 前 %2 行 / 共 %3 行")
        .arg(kind).arg(shownRows).arg(rows.size()), m_content);
    caption->setObjectName(QStringLiteral("DialogSubtitle"));
    caption->setContentsMargins(14, 10, 14, 4);
    m_contentLayout->addWidget(caption);
    auto *table = new QTableWidget(shownRows, maxColumns, m_content);
    table->setObjectName(QStringLiteral("PreviewSheet"));
    table->setShowGrid(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->verticalHeader()->setDefaultSectionSize(26);
    for (int row = 0; row < shownRows; ++row) {
        for (int column = 0; column < maxColumns && column < rows.at(row).size(); ++column)
            table->setItem(row, column, new QTableWidgetItem(rows.at(row).at(column)));
    }
    table->resizeColumnsToContents();
    m_contentLayout->addWidget(table, 1);
    return true;
}

void PreviewDialog::buildDirectoryPreview(const QString &path)
{
    QDirIterator iterator(path, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    QStringList names;
    int total = 0;
    while (iterator.hasNext()) {
        iterator.next();
        ++total;
        if (names.size() < kDirectoryListingLimit) {
            const QFileInfo item = iterator.fileInfo();
            names.append(item.isDir() ? item.fileName() + QStringLiteral("/")
                                      : item.fileName());
        }
        if (total >= kMaxArchiveRows)
            break;
    }
    QString listing;
    for (const QString &name : names)
        listing += name + QLatin1Char('\n');
    if (total > kDirectoryListingLimit)
        listing += QStringLiteral("… 共 %1 项").arg(total);
    auto *folder = new QLabel(QStringLiteral("文件夹预览\n\n%1\n\n项目数：%2\n\n%3")
                                  .arg(QFileInfo(path).absoluteFilePath())
                                  .arg(total)
                                  .arg(listing.trimmed()),
                              m_content);
    folder->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    folder->setWordWrap(true);
    folder->setObjectName(QStringLiteral("DialogSubtitle"));
    m_contentLayout->addWidget(folder, 1);
}

void PreviewDialog::buildFallbackPreview(const QString &path, const QString &reason)
{
    auto *label = new QLabel(QStringLiteral("%1\n\n%2\n\n%3")
                                 .arg(QFileInfo(path).fileName(), reason,
                                      QFileInfo(path).absoluteFilePath()),
                             m_content);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setObjectName(QStringLiteral("DialogSubtitle"));
    m_contentLayout->addWidget(label, 1);
}
