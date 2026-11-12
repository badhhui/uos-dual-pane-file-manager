#include "iconprovider.h"

#include "formatters.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace {
enum class Family {
    Word,
    Sheet,
    Slides,
    Text,
    Pdf,
    Image,
    Video,
    Audio,
    Archive,
    Disk,
    Installer,
    Font,
    Book,
    Database,
    Torrent,
    Cad,
    Vector,
    Contact,
    Calendar,
    Certificate,
    Mindmap,
    Code,
    Fig,
    Other
};

struct FamilyStyle {
    QColor main;
    QColor soft;
    QColor glyph;
    QStringList extensions;
};

struct FamilySpec {
    Family family;
    QStringList extensions;
    QColor main;
    QColor soft;
    QColor glyph;
};

// Single source of truth for suffix → family/color mapping, covering the file
// types commonly seen on UOS (mirroring what the bundled file manager shows).
const QVector<FamilySpec> &familyTable()
{
    static const QVector<FamilySpec> table = {
        {Family::Word, {QStringLiteral("doc"), QStringLiteral("docx"), QStringLiteral("wps"),
                        QStringLiteral("wpt"), QStringLiteral("odt"), QStringLiteral("rtf"),
                        QStringLiteral("dot"), QStringLiteral("dotx"), QStringLiteral("docm"),
                        QStringLiteral("uof")},
         QColor(QStringLiteral("#2F6BFF")), QColor(QStringLiteral("#E7EEFF")), QColor(QStringLiteral("#2F6BFF"))},
        {Family::Sheet, {QStringLiteral("xls"), QStringLiteral("xlsx"), QStringLiteral("et"),
                         QStringLiteral("xlt"), QStringLiteral("xltx"), QStringLiteral("xlsm"),
                         QStringLiteral("csv"), QStringLiteral("ods"), QStringLiteral("numbers")},
         QColor(QStringLiteral("#1FA56A")), QColor(QStringLiteral("#E4F7EE")), QColor(QStringLiteral("#1FA56A"))},
        {Family::Slides, {QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("dps"),
                          QStringLiteral("odp"), QStringLiteral("key"), QStringLiteral("pps"),
                          QStringLiteral("ppsx"), QStringLiteral("pot"), QStringLiteral("potx")},
         QColor(QStringLiteral("#F08A2E")), QColor(QStringLiteral("#FFF0E1")), QColor(QStringLiteral("#F08A2E"))},
        {Family::Text, {QStringLiteral("txt"), QStringLiteral("text"), QStringLiteral("md"),
                        QStringLiteral("markdown"), QStringLiteral("log"), QStringLiteral("rst"),
                        QStringLiteral("ini"), QStringLiteral("conf"), QStringLiteral("cfg")},
         QColor(QStringLiteral("#64748B")), QColor(QStringLiteral("#EEF2F7")), QColor(QStringLiteral("#64748B"))},
        {Family::Pdf, {QStringLiteral("pdf"), QStringLiteral("djvu"), QStringLiteral("djv")},
         QColor(QStringLiteral("#E5484D")), QColor(QStringLiteral("#FDEBEC")), QColor(QStringLiteral("#E5484D"))},
        {Family::Image, {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
                         QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("webp"),
                         QStringLiteral("svg"), QStringLiteral("tif"), QStringLiteral("tiff"),
                         QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("avif"),
                         QStringLiteral("jfif"), QStringLiteral("ico"), QStringLiteral("psd"),
                         QStringLiteral("dng"), QStringLiteral("cr2"), QStringLiteral("nef"),
                         QStringLiteral("arw")},
         QColor(QStringLiteral("#0EA5C9")), QColor(QStringLiteral("#E1F6FB")), QColor(QStringLiteral("#0EA5C9"))},
        {Family::Video, {QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("mov"),
                         QStringLiteral("avi"), QStringLiteral("webm"), QStringLiteral("m4v"),
                         QStringLiteral("wmv"), QStringLiteral("flv"), QStringLiteral("mpg"),
                         QStringLiteral("mpeg"), QStringLiteral("3gp"), QStringLiteral("ts"),
                         QStringLiteral("rmvb"), QStringLiteral("rm"), QStringLiteral("vob"),
                         QStringLiteral("m2ts"), QStringLiteral("ogv")},
         QColor(QStringLiteral("#4F46E5")), QColor(QStringLiteral("#EAE8FF")), QColor(QStringLiteral("#4F46E5"))},
        {Family::Audio, {QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"),
                         QStringLiteral("ogg"), QStringLiteral("m4a"), QStringLiteral("aac"),
                         QStringLiteral("opus"), QStringLiteral("wma"), QStringLiteral("aiff"),
                         QStringLiteral("amr"), QStringLiteral("mid"), QStringLiteral("midi"),
                         QStringLiteral("ape"), QStringLiteral("mka")},
         QColor(QStringLiteral("#8B5CF6")), QColor(QStringLiteral("#F1ECFF")), QColor(QStringLiteral("#8B5CF6"))},
        {Family::Archive, {QStringLiteral("zip"), QStringLiteral("rar"), QStringLiteral("7z"),
                           QStringLiteral("tar"), QStringLiteral("gz"), QStringLiteral("bz2"),
                           QStringLiteral("bz"), QStringLiteral("xz"), QStringLiteral("lzma"),
                           QStringLiteral("zst"), QStringLiteral("tgz"), QStringLiteral("tbz2"),
                           QStringLiteral("jar"), QStringLiteral("cab"), QStringLiteral("arj")},
         QColor(QStringLiteral("#D97706")), QColor(QStringLiteral("#FFF3DF")), QColor(QStringLiteral("#D97706"))},
        {Family::Disk, {QStringLiteral("iso"), QStringLiteral("img"), QStringLiteral("dmg"),
                        QStringLiteral("vhd"), QStringLiteral("vdi"), QStringLiteral("mds"),
                        QStringLiteral("nrg")},
         QColor(QStringLiteral("#5E81AC")), QColor(QStringLiteral("#E9EFF6")), QColor(QStringLiteral("#5E81AC"))},
        {Family::Installer, {QStringLiteral("exe"), QStringLiteral("msi"), QStringLiteral("apk"),
                             QStringLiteral("appimage"), QStringLiteral("deb"), QStringLiteral("rpm")},
         QColor(QStringLiteral("#9333EA")), QColor(QStringLiteral("#F3E8FF")), QColor(QStringLiteral("#9333EA"))},
        {Family::Font, {QStringLiteral("ttf"), QStringLiteral("otf"), QStringLiteral("ttc"),
                        QStringLiteral("woff"), QStringLiteral("woff2"), QStringLiteral("eot"),
                        QStringLiteral("fon")},
         QColor(QStringLiteral("#EC4899")), QColor(QStringLiteral("#FDE9F3")), QColor(QStringLiteral("#EC4899"))},
        {Family::Book, {QStringLiteral("epub"), QStringLiteral("mobi"), QStringLiteral("azw"),
                        QStringLiteral("azw3"), QStringLiteral("chm"), QStringLiteral("hlp"),
                        QStringLiteral("caj")},
         QColor(QStringLiteral("#14B8A6")), QColor(QStringLiteral("#E6FAF7")), QColor(QStringLiteral("#14B8A6"))},
        {Family::Database, {QStringLiteral("db"), QStringLiteral("sqlite"), QStringLiteral("sqlite3"),
                            QStringLiteral("dbf"), QStringLiteral("mdb"), QStringLiteral("accdb"),
                            QStringLiteral("sql")},
         QColor(QStringLiteral("#0F766E")), QColor(QStringLiteral("#E0F2F0")), QColor(QStringLiteral("#0F766E"))},
        {Family::Torrent, {QStringLiteral("torrent")},
         QColor(QStringLiteral("#F59E0B")), QColor(QStringLiteral("#FEF6E0")), QColor(QStringLiteral("#F59E0B"))},
        {Family::Cad, {QStringLiteral("dwg"), QStringLiteral("dxf"), QStringLiteral("dwf")},
         QColor(QStringLiteral("#1E40AF")), QColor(QStringLiteral("#E3ECFC")), QColor(QStringLiteral("#1E40AF"))},
        {Family::Vector, {QStringLiteral("ai"), QStringLiteral("eps")},
         QColor(QStringLiteral("#A21CAF")), QColor(QStringLiteral("#F9EAFB")), QColor(QStringLiteral("#A21CAF"))},
        {Family::Contact, {QStringLiteral("vcf"), QStringLiteral("vcard")},
         QColor(QStringLiteral("#0284C7")), QColor(QStringLiteral("#E3F2FC")), QColor(QStringLiteral("#0284C7"))},
        {Family::Calendar, {QStringLiteral("ics"), QStringLiteral("ical")},
         QColor(QStringLiteral("#CA8A04")), QColor(QStringLiteral("#FBF0D7")), QColor(QStringLiteral("#CA8A04"))},
        {Family::Certificate, {QStringLiteral("pem"), QStringLiteral("crt"), QStringLiteral("cer"),
                               QStringLiteral("p12"), QStringLiteral("pfx"), QStringLiteral("der")},
         QColor(QStringLiteral("#4D7C0F")), QColor(QStringLiteral("#EFF7E4")), QColor(QStringLiteral("#4D7C0F"))},
        {Family::Mindmap, {QStringLiteral("xmind"), QStringLiteral("mm"), QStringLiteral("mmap"),
                           QStringLiteral("mind")},
         QColor(QStringLiteral("#C026D3")), QColor(QStringLiteral("#F9E7FC")), QColor(QStringLiteral("#C026D3"))},
        {Family::Code, {QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("zsh"),
                        QStringLiteral("py"), QStringLiteral("js"), QStringLiteral("ts"),
                        QStringLiteral("c"), QStringLiteral("cpp"), QStringLiteral("h"),
                        QStringLiteral("hpp"), QStringLiteral("cc"), QStringLiteral("java"),
                        QStringLiteral("go"), QStringLiteral("rs"), QStringLiteral("rb"),
                        QStringLiteral("php"), QStringLiteral("pl"), QStringLiteral("lua"),
                        QStringLiteral("json"), QStringLiteral("xml"), QStringLiteral("html"),
                        QStringLiteral("css"), QStringLiteral("yml"), QStringLiteral("yaml"),
                        QStringLiteral("toml"), QStringLiteral("bat"), QStringLiteral("cmd"),
                        QStringLiteral("ps1")},
         QColor(QStringLiteral("#1F2937")), QColor(QStringLiteral("#E8F8EE")), QColor(QStringLiteral("#22C55E"))},
        {Family::Fig, {QStringLiteral("fig")},
         QColor(QStringLiteral("#111827")), QColor(QStringLiteral("#F0F3F8")), QColor(QStringLiteral("#111827"))},
    };
    return table;
}

const FamilySpec &specForSuffix(const QString &suffix)
{
    static const FamilySpec other = {Family::Other, QStringList(), QColor(QStringLiteral("#64748B")),
                                     QColor(QStringLiteral("#F1F5F9")), QColor(QStringLiteral("#64748B"))};
    const QString ext = suffix.toLower();
    for (const FamilySpec &spec : familyTable()) {
        if (spec.extensions.contains(ext))
            return spec;
    }
    return other;
}

FamilyStyle styleForSuffix(const QString &suffix)
{
    const FamilySpec &spec = specForSuffix(suffix);
    FamilyStyle style;
    style.main = spec.main;
    style.soft = spec.soft;
    style.glyph = spec.glyph;
    style.extensions = spec.extensions;
    return style;
}

Family familyForSuffix(const QString &suffix)
{
    return specForSuffix(suffix).family;
}

void drawFolder(QPainter &painter)
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 30, 60, 28));
    painter.drawRoundedRect(QRectF(12, 18, 43, 39), 8, 8);
    QLinearGradient back(8, 12, 52, 52);
    back.setColorAt(0.0, QColor(QStringLiteral("#FFD987")));
    back.setColorAt(1.0, QColor(QStringLiteral("#EAA23A")));
    QPainterPath tab;
    tab.moveTo(7, 17);
    tab.cubicTo(7, 13, 10, 11, 14, 11);
    tab.lineTo(25, 11);
    tab.lineTo(30, 17);
    tab.lineTo(53, 17);
    tab.cubicTo(55, 17, 57, 19, 57, 22);
    tab.lineTo(57, 48);
    tab.cubicTo(57, 51, 55, 53, 52, 53);
    tab.lineTo(12, 53);
    tab.cubicTo(9, 53, 7, 51, 7, 48);
    tab.closeSubpath();
    painter.fillPath(tab, back);

    QLinearGradient front(8, 20, 52, 55);
    front.setColorAt(0.0, QColor(QStringLiteral("#FFE9B7")));
    front.setColorAt(0.55, QColor(QStringLiteral("#FFC767")));
    front.setColorAt(1.0, QColor(QStringLiteral("#F3A63C")));
    QPainterPath body;
    body.moveTo(7, 23);
    body.lineTo(57, 23);
    body.lineTo(57, 48);
    body.cubicTo(57, 51, 55, 53, 52, 53);
    body.lineTo(12, 53);
    body.cubicTo(9, 53, 7, 51, 7, 48);
    body.closeSubpath();
    painter.fillPath(body, front);

    painter.setPen(QPen(QColor(255, 255, 255, 115), 1.2));
    painter.drawLine(QPointF(12, 28), QPointF(52, 28));
    painter.setPen(QPen(QColor(201, 134, 38, 90), 0.8));
    painter.drawLine(QPointF(12, 22), QPointF(52, 22));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 250, 238, 235));
    QPainterPath paper;
    paper.moveTo(17, 15);
    paper.lineTo(31, 15);
    paper.lineTo(35, 20);
    paper.lineTo(17, 20);
    paper.closeSubpath();
    painter.drawPath(paper);
    painter.restore();
}

void drawFamilyGlyph(QPainter &painter, Family family, const QRectF &area, const FamilyStyle &style)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor color = style.glyph;
    const QPointF center = area.center();

    if (family == Family::Sheet) {
        const QRectF grid(area.left() + 1, area.top() + 2, area.width() - 2, area.height() - 4);
        painter.setPen(QPen(color, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(grid, 1.5, 1.5);
        painter.drawLine(QPointF(grid.left(), grid.center().y()), QPointF(grid.right(), grid.center().y()));
        painter.drawLine(QPointF(grid.center().x(), grid.top()), QPointF(grid.center().x(), grid.bottom()));
        painter.drawLine(QPointF(grid.left() + grid.width() * 0.5, grid.top()), QPointF(grid.left() + grid.width() * 0.5, grid.bottom()));
        painter.drawLine(QPointF(grid.left(), grid.top() + grid.height() * 0.5), QPointF(grid.right(), grid.top() + grid.height() * 0.5));
    } else if (family == Family::Slides) {
        const QRectF screen(area.left() + 1, area.top() + 2, area.width() - 2, area.height() - 7);
        painter.setPen(QPen(color, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(screen, 2, 2);
        painter.drawLine(QPointF(screen.left() + 4, screen.top() + 5), QPointF(screen.left() + 10, screen.top() + 5));
        painter.drawLine(QPointF(screen.left() + 4, screen.top() + 9), QPointF(screen.left() + 8, screen.top() + 9));
        painter.drawLine(QPointF(center.x(), screen.bottom()), QPointF(center.x(), screen.bottom() + 4));
        painter.drawLine(QPointF(center.x() - 4, screen.bottom() + 4), QPointF(center.x() + 4, screen.bottom() + 4));
    } else if (family == Family::Image) {
        painter.setPen(QPen(color, 1.4));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(area.left() + 1, area.top() + 2, area.width() - 2, area.height() - 4), 2, 2);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(area.left() + 6, area.top() + 7), 1.8, 1.8);
        QPainterPath mountain;
        mountain.moveTo(area.left() + 2, area.bottom() - 3);
        mountain.lineTo(area.left() + 8, area.top() + 10);
        mountain.lineTo(area.left() + 13, area.top() + 14);
        mountain.lineTo(area.left() + 16, area.top() + 9);
        mountain.lineTo(area.right() - 2, area.bottom() - 3);
        mountain.closeSubpath();
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawPath(mountain);
    } else if (family == Family::Video) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(center, 8, 8);
        QPolygonF triangle;
        triangle << QPointF(center.x() - 2.5, center.y() - 4.5)
                 << QPointF(center.x() + 5, center.y())
                 << QPointF(center.x() - 2.5, center.y() + 4.5);
        painter.setBrush(Qt::white);
        painter.drawPolygon(triangle);
    } else if (family == Family::Audio) {
        painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(area.left() + 5, area.top() + 5), QPointF(area.left() + 5, area.bottom() - 6));
        painter.drawLine(QPointF(area.left() + 5, area.top() + 5), QPointF(area.right() - 3, area.top() + 2));
        painter.drawLine(QPointF(area.right() - 3, area.top() + 2), QPointF(area.right() - 3, area.bottom() - 9));
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(area.left() + 2, area.bottom() - 5), 3.2, 2.4);
        painter.drawEllipse(QPointF(area.right() - 6, area.bottom() - 8), 3.2, 2.4);
    } else if (family == Family::Archive) {
        painter.setPen(QPen(color, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(area.left() + 1, area.top() + 1, area.width() - 2, area.height() - 2), 2, 2);
        painter.drawLine(QPointF(center.x(), area.top() + 1), QPointF(center.x(), area.bottom() - 1));
        for (int i = 0; i < 4; ++i) {
            const qreal y = area.top() + 3 + i * 3.5;
            painter.drawLine(QPointF(center.x() - 3, y), QPointF(center.x() + 3, y));
        }
    } else if (family == Family::Code) {
        painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(area.left() + 4, area.top() + 4), QPointF(area.left() + 9, area.center().y()));
        painter.drawLine(QPointF(area.left() + 9, area.center().y()), QPointF(area.left() + 4, area.bottom() - 2));
        painter.drawLine(QPointF(area.left() + 12, area.top() + 4), QPointF(area.left() + 12, area.bottom() - 2));
        painter.drawLine(QPointF(area.left() + 16, area.bottom() - 2), QPointF(area.right() - 2, area.bottom() - 2));
    } else if (family == Family::Fig) {
        const QList<QColor> colors = {QColor(QStringLiteral("#F24E1E")), QColor(QStringLiteral("#FF7262")), QColor(QStringLiteral("#A259FF")), QColor(QStringLiteral("#1ABCFE")), QColor(QStringLiteral("#0ACF83"))};
        painter.setPen(Qt::NoPen);
        const qreal size = 4.6;
        const qreal gap = 1.2;
        const qreal startX = area.left() + 5;
        const qreal startY = area.top() + 4;
        painter.setBrush(colors[0]); painter.drawRoundedRect(QRectF(startX, startY, size, size), 1.2, 1.2);
        painter.setBrush(colors[1]); painter.drawRoundedRect(QRectF(startX + size + gap, startY, size, size), 1.2, 1.2);
        painter.setBrush(colors[2]); painter.drawRoundedRect(QRectF(startX, startY + size + gap, size, size), 1.2, 1.2);
        painter.setBrush(colors[3]); painter.drawEllipse(QPointF(startX + size + gap + size / 2.0, startY + size + gap + size / 2.0), size / 2.0, size / 2.0);
        painter.setBrush(colors[4]); painter.drawRoundedRect(QRectF(startX, startY + 2 * (size + gap), size, size), 1.2, 1.2);
    } else if (family == Family::Book) {
        // Open book: two facing pages with a center spine.
        painter.setPen(QPen(color, 1.3));
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(area.left(), area.top() + 3,
                                       area.width() / 2.0 - 1, area.height() - 5), 1.5, 1.5);
        painter.drawRoundedRect(QRectF(center.x() + 1, area.top() + 3,
                                       area.width() / 2.0 - 1, area.height() - 5), 1.5, 1.5);
        painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(center.x(), area.top() + 2), QPointF(center.x(), area.bottom() - 1));
    } else if (family == Family::Font) {
        QFont glyphFont = QApplication::font();
        glyphFont.setBold(true);
        glyphFont.setPixelSize(int(area.height() * 0.92));
        painter.setFont(glyphFont);
        painter.setPen(QPen(color, 1));
        painter.drawText(area, Qt::AlignCenter, QStringLiteral("Aa"));
    } else if (family == Family::Disk) {
        painter.setPen(QPen(color, 1.6));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, 9, 9);
        painter.drawEllipse(center, 3, 3);
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(center, 1.2, 1.2);
        painter.setPen(QPen(QColor(255, 255, 255, 170), 1.4));
        painter.drawArc(QRectF(center.x() - 9, center.y() - 9, 18, 18), 40 * 16, 70 * 16);
    } else if (family == Family::Installer) {
        painter.setPen(QPen(color, 1.4));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(area.left() + 1, area.top() + 4,
                                       area.width() - 2, area.height() - 6), 2, 2);
        painter.drawLine(QPointF(area.left() + 1, area.top() + 9), QPointF(area.right() - 1, area.top() + 9));
        painter.drawLine(QPointF(center.x(), area.top() + 4), QPointF(center.x(), area.top() + 9));
        painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(center.x(), area.top() + 12), QPointF(center.x(), area.bottom() - 4));
        QPolygonF arrowHead;
        arrowHead << QPointF(center.x() - 3, area.bottom() - 6)
                  << QPointF(center.x() + 3, area.bottom() - 6)
                  << QPointF(center.x(), area.bottom() - 1.5);
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(arrowHead);
    } else if (family == Family::Database) {
        painter.setPen(QPen(color, 1.4));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(center.x() - 9, area.top() + 1, 18, 6));
        painter.drawLine(QPointF(area.left() + 4, area.top() + 4), QPointF(area.left() + 4, area.bottom() - 3));
        painter.drawLine(QPointF(area.right() - 4, area.top() + 4), QPointF(area.right() - 4, area.bottom() - 3));
        painter.drawArc(QRectF(center.x() - 9, area.bottom() - 9, 18, 6), 180 * 16, 180 * 16);
        painter.drawArc(QRectF(center.x() - 9, area.center().y() - 2, 18, 6), 180 * 16, 180 * 16);
    } else if (family == Family::Torrent) {
        painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(center.x(), area.top() + 2), QPointF(center.x(), area.bottom() - 5));
        QPolygonF torrentHead;
        torrentHead << QPointF(center.x() - 3.5, area.bottom() - 7)
                    << QPointF(center.x() + 3.5, area.bottom() - 7)
                    << QPointF(center.x(), area.bottom() - 2);
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(torrentHead);
        painter.setPen(QPen(color, 1.6));
        painter.drawLine(QPointF(area.left() + 3, area.bottom() - 1), QPointF(area.right() - 3, area.bottom() - 1));
    } else if (family == Family::Cad) {
        painter.setPen(QPen(color, 1.4));
        painter.setBrush(Qt::NoBrush);
        QPainterPath setSquare;
        setSquare.moveTo(area.left() + 2, area.bottom() - 2);
        setSquare.lineTo(area.right() - 4, area.bottom() - 2);
        setSquare.lineTo(area.right() - 4, area.top() + 2);
        setSquare.closeSubpath();
        painter.drawPath(setSquare);
        painter.drawLine(QPointF(area.left() + 5, area.bottom() - 5), QPointF(area.right() - 7, area.bottom() - 5));
        painter.drawEllipse(QPointF(center.x() + 1, center.y() + 1), 2.2, 2.2);
    } else if (family == Family::Vector) {
        painter.setPen(QPen(color, 1.3));
        painter.setBrush(Qt::NoBrush);
        QPainterPath nib;
        nib.moveTo(center.x(), area.top() + 2);
        nib.cubicTo(area.right() - 2, area.top() + 6, area.right() - 3, area.bottom() - 4,
                    center.x() + 2, area.bottom() - 2);
        nib.lineTo(center.x() - 2, area.bottom() - 2);
        nib.cubicTo(area.left() + 3, area.bottom() - 4, area.left() + 2, area.top() + 6,
                    center.x(), area.top() + 2);
        painter.drawPath(nib);
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(center.x(), area.bottom() - 4), 1.6, 1.6);
    } else if (family == Family::Contact) {
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(center.x(), area.top() + 6), 4, 4);
        QPainterPath bust;
        bust.moveTo(area.left() + 3, area.bottom() - 1);
        bust.cubicTo(area.left() + 4, area.bottom() - 8, area.right() - 4, area.bottom() - 8,
                     area.right() - 3, area.bottom() - 1);
        bust.closeSubpath();
        painter.drawPath(bust);
    } else if (family == Family::Calendar) {
        painter.setPen(QPen(color, 1.4));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(area.left() + 1, area.top() + 3,
                                       area.width() - 2, area.height() - 5), 2, 2);
        painter.drawLine(QPointF(area.left() + 1, area.top() + 8), QPointF(area.right() - 1, area.top() + 8));
        painter.drawLine(QPointF(area.left() + 7, area.top() + 1), QPointF(area.left() + 7, area.top() + 5));
        painter.drawLine(QPointF(area.right() - 7, area.top() + 1), QPointF(area.right() - 7, area.top() + 5));
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(center.x() - 1.5, center.y() + 1, 3, 3), 0.8, 0.8);
    } else if (family == Family::Certificate) {
        painter.setPen(QPen(color, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(center.x(), area.top() + 7), 5.5, 5.5);
        painter.drawLine(QPointF(center.x() - 2, area.top() + 12), QPointF(center.x() - 3, area.bottom() - 1));
        painter.drawLine(QPointF(center.x() + 2, area.top() + 12), QPointF(center.x() + 3, area.bottom() - 1));
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(center.x(), area.top() + 7), 2, 2);
    } else if (family == Family::Mindmap) {
        painter.setPen(QPen(color, 1.3));
        painter.setBrush(color);
        painter.drawEllipse(QPointF(center.x() - 5, area.top() + 5), 3, 3);
        painter.drawEllipse(QPointF(center.x() + 5, area.top() + 5), 3, 3);
        painter.drawEllipse(QPointF(center.x(), area.bottom() - 4), 3, 3);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(center.x() - 3.5, area.top() + 6.5), QPointF(center.x() + 3.5, area.top() + 6.5));
        painter.drawLine(QPointF(center.x() - 3.5, area.top() + 6.5), QPointF(center.x() - 1, area.bottom() - 5.5));
        painter.drawLine(QPointF(center.x() + 3.5, area.top() + 6.5), QPointF(center.x() + 1, area.bottom() - 5.5));
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(center.x(), area.center().y()), 2.2, 2.2);
    } else {
        painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(area.left() + 3, area.top() + 4), QPointF(area.right() - 4, area.top() + 4));
        painter.drawLine(QPointF(area.left() + 3, area.top() + 9), QPointF(area.right() - 8, area.top() + 9));
        painter.drawLine(QPointF(area.left() + 3, area.top() + 14), QPointF(area.right() - 3, area.top() + 14));
    }
    painter.restore();
}

void drawFileStack(QPainter &painter, const QString &suffix)
{
    const Family family = familyForSuffix(suffix);
    const FamilyStyle style = styleForSuffix(suffix);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Two soft sheets behind the front page.
    painter.save();
    painter.translate(33, 33);
    painter.rotate(8.5);
    painter.translate(-33, -33);
    painter.setPen(QPen(style.main.lighter(125), 0.8));
    painter.setBrush(style.soft);
    painter.drawRoundedRect(QRectF(14, 7, 35, 44), 4, 4);
    painter.restore();

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(15, 30, 60, 26));
    painter.drawRoundedRect(QRectF(11, 11, 40, 46), 5, 5);

    QPainterPath page;
    page.moveTo(14, 9);
    page.lineTo(35, 9);
    page.lineTo(49, 23);
    page.lineTo(49, 51);
    page.cubicTo(49, 54, 47, 56, 44, 56);
    page.lineTo(14, 56);
    page.cubicTo(11, 56, 9, 54, 9, 51);
    page.lineTo(9, 14);
    page.cubicTo(9, 11, 11, 9, 14, 9);
    page.closeSubpath();
    painter.setBrush(QColor(QStringLiteral("#FFFFFF")));
    painter.setPen(QPen(QColor(QStringLiteral("#DCE4EF")), 1.0));
    painter.drawPath(page);

    QPainterPath fold;
    fold.moveTo(35, 9);
    fold.lineTo(35, 23);
    fold.lineTo(49, 23);
    fold.closeSubpath();
    painter.setBrush(QColor(QStringLiteral("#F0F4FA")));
    painter.setPen(QPen(QColor(QStringLiteral("#DCE4EF")), 0.8));
    painter.drawPath(fold);

    drawFamilyGlyph(painter, family, QRectF(15, 17, 27, 22), style);

    QString label = suffix.trimmed().toUpper();
    if (label.isEmpty())
        label = QStringLiteral("FILE");
    if (label.size() > 4)
        label = label.left(4);
    qreal badgeWidth = 33;
    if (label.size() <= 3)
        badgeWidth = 29;
    if (label.size() <= 2)
        badgeWidth = 24;
    if (label == QStringLiteral("FILE"))
        badgeWidth = 33;
    const QRectF badge(49 - badgeWidth, 39.5, badgeWidth, 15.5);
    painter.setPen(Qt::NoPen);
    painter.setBrush(style.main);
    painter.drawRoundedRect(badge, 3.5, 3.5);
    QFont font = QApplication::font();
    font.setBold(true);
    font.setPixelSize(label.size() >= 4 ? 8 : 9);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(badge, Qt::AlignCenter, label);
    painter.restore();
}

void drawDriveBody(QPainter &painter, const QColor &top, const QColor &bottom, const QColor &accent)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(10, 25, 50, 28));
    painter.drawRoundedRect(QRectF(8, 21, 48, 31), 7, 7);
    QLinearGradient body(10, 18, 48, 50);
    body.setColorAt(0.0, top);
    body.setColorAt(1.0, bottom);
    painter.setBrush(body);
    painter.drawRoundedRect(QRectF(7, 18, 50, 31), 7, 7);
    painter.setBrush(QColor(255, 255, 255, 45));
    painter.drawRoundedRect(QRectF(10, 20, 44, 7), 4, 4);
    painter.setBrush(QColor(8, 24, 46, 190));
    painter.drawRoundedRect(QRectF(11, 35, 42, 10), 4, 4);
    painter.setPen(QPen(QColor(255, 255, 255, 85), 0.9));
    painter.drawLine(QPointF(15, 38), QPointF(31, 38));
    painter.drawLine(QPointF(15, 41), QPointF(25, 41));
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawEllipse(QPointF(47, 40), 2.1, 2.1);
    painter.setBrush(QColor(190, 205, 225));
    painter.drawRoundedRect(QRectF(17, 49, 30, 2.2), 1, 1);
}

void drawSystemDevice(QPainter &painter)
{
    drawDriveBody(painter, QColor(QStringLiteral("#5AA4FF")), QColor(QStringLiteral("#1C4ED8")), QColor(QStringLiteral("#7CFFB2")));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#FFFFFF")));
    painter.drawRoundedRect(QRectF(16, 24, 11, 8), 1.5, 1.5);
    painter.setPen(QPen(QColor(QStringLiteral("#1C4ED8")), 1.1));
    painter.drawLine(QPointF(21.5, 24), QPointF(21.5, 32));
    painter.drawLine(QPointF(16, 28), QPointF(27, 28));
}

void drawDataDevice(QPainter &painter)
{
    drawDriveBody(painter, QColor(QStringLiteral("#4FE3B0")), QColor(QStringLiteral("#0E8A62")), QColor(QStringLiteral("#B8FFE4")));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#FFFFFF")));
    QPainterPath folder;
    folder.moveTo(15, 25);
    folder.lineTo(21, 25);
    folder.lineTo(23, 28);
    folder.lineTo(28, 28);
    folder.cubicTo(29, 28, 30, 29, 30, 30);
    folder.lineTo(30, 33);
    folder.cubicTo(30, 34, 29, 34, 28, 34);
    folder.lineTo(16, 34);
    folder.cubicTo(15, 34, 14, 33, 14, 32);
    folder.lineTo(14, 26);
    folder.cubicTo(14, 25, 15, 25, 15, 25);
    painter.drawPath(folder);
}

void drawUsbDevice(QPainter &painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(10, 25, 50, 25));
    painter.drawRoundedRect(QRectF(21, 20, 25, 36), 6, 6);
    painter.setBrush(QColor(QStringLiteral("#D8E1EC")));
    painter.drawRoundedRect(QRectF(25, 7, 14, 13), 2.5, 2.5);
    painter.setBrush(QColor(QStringLiteral("#8896A8")));
    painter.drawRoundedRect(QRectF(27, 10, 10, 7), 1, 1);
    painter.setBrush(QColor(QStringLiteral("#42566E")));
    painter.drawRect(QRectF(29, 12, 2, 3));
    painter.drawRect(QRectF(33, 12, 2, 3));
    QLinearGradient body(22, 18, 45, 55);
    body.setColorAt(0.0, QColor(QStringLiteral("#FFB65C")));
    body.setColorAt(1.0, QColor(QStringLiteral("#E36E12")));
    painter.setBrush(body);
    painter.drawRoundedRect(QRectF(20, 18, 24, 37), 7, 7);
    painter.setBrush(QColor(255, 255, 255, 45));
    painter.drawRoundedRect(QRectF(23, 21, 18, 5), 2.5, 2.5);
    painter.setBrush(QColor(QStringLiteral("#2557C9")));
    painter.drawRoundedRect(QRectF(20, 40, 24, 5), 2, 2);
    painter.setBrush(QColor(QStringLiteral("#B9FFDC")));
    painter.drawEllipse(QPointF(32, 50), 1.8, 1.8);
    painter.restore();
}

void drawExternalDevice(QPainter &painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(QStringLiteral("#6D28D9")), 3.2, Qt::SolidLine, Qt::RoundCap));
    QPainterPath cable;
    cable.moveTo(21, 24);
    cable.cubicTo(21, 12, 39, 12, 39, 24);
    painter.drawPath(cable);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#D8E1EC")));
    painter.drawRoundedRect(QRectF(35, 22, 8, 8), 2, 2);
    drawDriveBody(painter, QColor(QStringLiteral("#A78BFA")), QColor(QStringLiteral("#6D28D9")), QColor(QStringLiteral("#F5D0FE")));
    painter.setPen(QPen(QColor(255, 255, 255, 210), 1.6));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(32, 30), 5.2, 5.2);
    painter.setPen(QPen(QColor(255, 255, 255, 145), 1.2));
    painter.drawArc(QRectF(26.8, 24.8, 10.4, 10.4), 30 * 16, 220 * 16);
    painter.restore();
}

void drawAppFallback(QPainter &painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 20, 50, 28));
    painter.drawRoundedRect(QRectF(7, 8, 50, 50), 12, 12);
    QLinearGradient gradient(7, 7, 57, 57);
    gradient.setColorAt(0.0, QColor(QStringLiteral("#6D8BFF")));
    gradient.setColorAt(1.0, QColor(QStringLiteral("#3B4BC8")));
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(6, 6, 52, 52), 13, 13);
    painter.setBrush(QColor(QStringLiteral("#FFFFFF")));
    painter.drawRoundedRect(QRectF(15, 18, 34, 27), 5, 5);
    painter.setBrush(QColor(QStringLiteral("#E9EEF9")));
    painter.drawRoundedRect(QRectF(15, 18, 34, 7), 4, 4);
    painter.setBrush(QColor(QStringLiteral("#4A63E8")));
    painter.drawEllipse(QPointF(20, 21.5), 1.8, 1.8);
    painter.setBrush(QColor(QStringLiteral("#DDE5FF")));
    painter.drawRoundedRect(QRectF(20, 29, 10, 10), 2, 2);
    painter.drawRoundedRect(QRectF(34, 29, 10, 10), 2, 2);
    painter.restore();
}
}

IconProvider &IconProvider::instance()
{
    static IconProvider provider;
    return provider;
}

IconProvider::IconProvider() = default;

QIcon IconProvider::uiIcon(const QString &name, const QColor &color)
{
    const QString key = name + QLatin1Char('|') + color.name(QColor::HexArgb);
    auto it = m_uiCache.constFind(key);
    if (it != m_uiCache.constEnd())
        return it.value();
    QPixmap pixmap = tintedPixmap(QStringLiteral(":/icons/ui/%1.png").arg(name), color);
    QIcon icon(pixmap);
    m_uiCache.insert(key, icon);
    return icon;
}

QPixmap IconProvider::tintedPixmap(const QString &resource, const QColor &color)
{
    QPixmap source(resource);
    if (source.isNull())
        return QPixmap();
    QPixmap tinted(source.size());
    tinted.fill(Qt::transparent);
    QPainter painter(&tinted);
    painter.drawPixmap(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    painter.end();
    return tinted;
}

QIcon IconProvider::fileIcon(bool isDirectory, const QString &suffix)
{
    const QString key = QStringLiteral("%1|%2").arg(isDirectory ? QStringLiteral("dir") : QStringLiteral("file"), suffix.toLower());
    auto it = m_fileCache.constFind(key);
    if (it != m_fileCache.constEnd())
        return it.value();
    QIcon icon = buildFileIcon(isDirectory, suffix.toLower());
    m_fileCache.insert(key, icon);
    return icon;
}

QIcon IconProvider::buildFileIcon(bool isDirectory, const QString &suffix)
{
    QPixmap canvas(128, 128);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(2.0, 2.0);
    if (isDirectory)
        drawFolder(painter);
    else
        drawFileStack(painter, suffix);
    painter.end();

    QIcon icon;
    icon.addPixmap(canvas, QIcon::Normal, QIcon::Off);
    icon.addPixmap(canvas.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation), QIcon::Normal, QIcon::Off);
    icon.addPixmap(canvas.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation), QIcon::Normal, QIcon::Off);
    return icon;
}

QIcon IconProvider::desktopIcon(const QString &iconName)
{
    const QString key = iconName.trimmed();
    auto it = m_desktopCache.constFind(key);
    if (it != m_desktopCache.constEnd())
        return it.value();
    QIcon icon = buildDesktopIcon(key);
    m_desktopCache.insert(key, icon);
    return icon;
}

QIcon IconProvider::buildDesktopIcon(const QString &iconName)
{
    if (!iconName.isEmpty()) {
        const QIcon themed = QIcon::fromTheme(iconName);
        if (!themed.isNull())
            return themed;
        const QString path = resolveIconFile(iconName);
        if (!path.isEmpty()) {
            QIcon icon(path);
            if (!icon.isNull())
                return icon;
        }
    }
    return appFallbackIcon();
}

QString IconProvider::resolveIconFile(const QString &iconName) const
{
    QString name = iconName.trimmed();
    if (name.isEmpty())
        return QString();
    if (name.startsWith(QStringLiteral("file://")))
        name = QUrl(name).toLocalFile();
    if (name.startsWith(QLatin1Char('/')) && QFileInfo::exists(name))
        return name;
    if (name.contains(QLatin1Char('/'))) {
        const QFileInfo direct(name);
        if (direct.exists())
            return direct.absoluteFilePath();
    }

    QStringList names;
    names.append(name);
    if (!name.endsWith(QStringLiteral(".png")) && !name.endsWith(QStringLiteral(".svg"))
        && !name.endsWith(QStringLiteral(".xpm"))) {
        names.append(name + QStringLiteral(".png"));
        names.append(name + QStringLiteral(".svg"));
        names.append(name + QStringLiteral(".xpm"));
    }

    QStringList roots;
    roots.append(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/icons"));
    roots.append(QDir::homePath() + QStringLiteral("/.local/share/icons"));
    roots.append(QStringLiteral("/usr/share/icons"));
    roots.append(QStringLiteral("/usr/local/share/icons"));

    QStringList themes;
    themes.append(QIcon::themeName());
    themes.append(QStringLiteral("hicolor"));
    themes.append(QStringLiteral("bloom"));
    themes.append(QStringLiteral("deepin"));
    themes.append(QStringLiteral("Papirus"));
    themes.append(QStringLiteral("Adwaita"));
    themes.append(QStringLiteral("breeze"));
    themes.removeAll(QString());
    themes.removeDuplicates();

    const QStringList sizes = {
        QStringLiteral("64x64"), QStringLiteral("128x128"), QStringLiteral("48x48"),
        QStringLiteral("256x256"), QStringLiteral("32x32"), QStringLiteral("96x96"),
        QStringLiteral("72x72"), QStringLiteral("512x512"), QStringLiteral("24x24"),
        QStringLiteral("scalable"),
        QStringLiteral("64"), QStringLiteral("128"), QStringLiteral("48"),
        QStringLiteral("256"), QStringLiteral("32"), QStringLiteral("96"),
        QStringLiteral("72"), QStringLiteral("512"), QStringLiteral("24"),
        QStringLiteral("scalable")
    };
    const QStringList categories = {
        QStringLiteral("apps"), QStringLiteral("applications"),
        QStringLiteral("places"), QStringLiteral("devices"), QStringLiteral("mimetypes")
    };

    for (const QString &root : roots) {
        for (const QString &theme : themes) {
            for (const QString &size : sizes) {
                for (const QString &category : categories) {
                    for (const QString &candidateName : names) {
                        const QString candidate = QDir(root).filePath(
                            theme + QLatin1Char('/') + size + QLatin1Char('/') + category + QLatin1Char('/') + candidateName);
                        if (QFileInfo::exists(candidate))
                            return candidate;
                    }
                }
            }
            for (const QString &category : categories) {
                for (const QString &size : sizes) {
                    for (const QString &candidateName : names) {
                        const QString candidate = QDir(root).filePath(
                            theme + QLatin1Char('/') + category + QLatin1Char('/') + size + QLatin1Char('/') + candidateName);
                        if (QFileInfo::exists(candidate))
                            return candidate;
                    }
                }
            }
            for (const QString &category : categories) {
                for (const QString &candidateName : names) {
                    const QString candidate = QDir(root).filePath(
                        theme + QLatin1Char('/') + category + QLatin1Char('/') + candidateName);
                    if (QFileInfo::exists(candidate))
                        return candidate;
                }
            }
        }
    }

    for (const QString &candidateName : names) {
        const QString candidate = QDir(QStringLiteral("/usr/share/pixmaps")).filePath(candidateName);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QString();
}

QIcon IconProvider::deviceIcon(DeviceKind kind)
{
    const int key = static_cast<int>(kind);
    auto it = m_deviceCache.constFind(key);
    if (it != m_deviceCache.constEnd())
        return it.value();
    QIcon icon = buildDeviceIcon(kind);
    m_deviceCache.insert(key, icon);
    return icon;
}

QIcon IconProvider::buildDeviceIcon(DeviceKind kind)
{
    QPixmap canvas(128, 128);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(2.0, 2.0);
    switch (kind) {
    case DeviceKind::System:
        drawSystemDevice(painter);
        break;
    case DeviceKind::Data:
        drawDataDevice(painter);
        break;
    case DeviceKind::Usb:
        drawUsbDevice(painter);
        break;
    case DeviceKind::External:
        drawExternalDevice(painter);
        break;
    }
    painter.end();
    QIcon icon;
    icon.addPixmap(canvas, QIcon::Normal, QIcon::Off);
    icon.addPixmap(canvas.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation), QIcon::Normal, QIcon::Off);
    icon.addPixmap(canvas.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation), QIcon::Normal, QIcon::Off);
    return icon;
}

QIcon IconProvider::appFallbackIcon()
{
    if (!m_appFallback.isNull())
        return m_appFallback;
    QPixmap canvas(128, 128);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(2.0, 2.0);
    drawAppFallback(painter);
    painter.end();
    m_appFallback = QIcon(canvas);
    return m_appFallback;
}

QIcon IconProvider::appIcon()
{
    if (!m_appIcon.isNull())
        return m_appIcon;
    const QPixmap base(QStringLiteral(":/icons/appicon.png"));
    if (base.isNull())
        return QIcon();
    const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
    for (int size : sizes) {
        m_appIcon.addPixmap(base.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation),
                            QIcon::Normal, QIcon::Off);
    }
    return m_appIcon;
}
