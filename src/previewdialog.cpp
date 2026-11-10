#include "previewdialog.h"

#include "formatters.h"
#include "iconprovider.h"
#include "theme.h"
#include "uidialogs.h"
#include "ziparchive.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QTimer>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeDatabase>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QShortcut>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>
#include <QXmlStreamReader>

namespace {
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
}

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
    auto *hint = new QLabel(QStringLiteral("↑ / ↓ 切换文件"), this);
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

    auto *spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    spaceShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(spaceShortcut, &QShortcut::activated, this, &QDialog::accept);
    auto *upShortcut = new QShortcut(QKeySequence(Qt::Key_Up), this);
    upShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(upShortcut, &QShortcut::activated, this, [this]() {
        if (m_paths.size() <= 1)
            return;
        m_index = (m_index - 1 + m_paths.size()) % m_paths.size();
        loadCurrent();
    });
    auto *downShortcut = new QShortcut(QKeySequence(Qt::Key_Down), this);
    downShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(downShortcut, &QShortcut::activated, this, [this]() {
        if (m_paths.size() <= 1)
            return;
        m_index = (m_index + 1) % m_paths.size();
        loadCurrent();
    });
    setFocus();
}

void PreviewDialog::loadCurrent()
{
    if (m_paths.isEmpty())
        return;
    clearContent();
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

void PreviewDialog::showHtml(const QString &html)
{
    clearContent();
    auto *browser = new QTextBrowser(m_content);
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
    ZipArchive archive;
    QString error;
    if (!archive.open(path, &error))
        return false;
    const QByteArray xml = archive.readFile(QStringLiteral("word/document.xml"), &error);
    if (xml.isEmpty())
        return false;
    QXmlStreamReader reader(xml);
    QString html;
    QString paragraph;
    QString style;
    bool inParagraph = false;
    int characters = 0;
    while (!reader.atEnd() && characters < 180000) {
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
        } else if (reader.isEndElement() && reader.name() == QStringLiteral("p") && inParagraph) {
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
    if (reader.hasError() || html.trimmed().isEmpty())
        return false;
    auto *pageHost = new QWidget(m_content);
    pageHost->setObjectName(QStringLiteral("PreviewPageHost"));
    pageHost->setStyleSheet(QStringLiteral("QWidget#PreviewPageHost { background:#E8EDF4; }"));
    auto *hostLayout = new QHBoxLayout(pageHost);
    hostLayout->setContentsMargins(36, 20, 36, 20);
    hostLayout->addStretch(1);
    auto *page = new QFrame(pageHost);
    page->setObjectName(QStringLiteral("PreviewPage"));
    page->setFixedWidth(720);
    page->setStyleSheet(QStringLiteral("QFrame#PreviewPage { background:white; border-radius:4px; }"));
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto *browser = new QTextBrowser(page);
    browser->setObjectName(QStringLiteral("PreviewDocument"));
    browser->setOpenExternalLinks(false);
    browser->setFrameShape(QFrame::NoFrame);
    browser->setStyleSheet(QStringLiteral("QTextBrowser { background:white; border:0; }"));
    browser->document()->setDocumentMargin(46);
    browser->setHtml(QStringLiteral("<html><head>%1</head><body>%2</body></html>")
                         .arg(documentStyle(), html));
    pageLayout->addWidget(browser, 1);
    hostLayout->addWidget(page, 1);
    hostLayout->addStretch(1);
    m_contentLayout->addWidget(pageHost, 1);
    return true;
}

bool PreviewDialog::buildXlsxPreview(const QString &path)
{
    ZipArchive archive;
    QString error;
    if (!archive.open(path, &error))
        return false;
    QString sheetName;
    for (const ZipEntryInfo &entry : archive.entries()) {
        if (entry.name.startsWith(QStringLiteral("xl/worksheets/sheet"))
            && entry.name.endsWith(QStringLiteral(".xml"))) {
            sheetName = entry.name;
            break;
        }
    }
    if (sheetName.isEmpty())
        return false;
    const QByteArray sharedXml = archive.readFile(QStringLiteral("xl/sharedStrings.xml"), &error);
    QStringList sharedStrings;
    if (!sharedXml.isEmpty()) {
        QXmlStreamReader shared(sharedXml);
        bool inString = false;
        QString current;
        while (!shared.atEnd()) {
            shared.readNext();
            if (shared.isStartElement() && shared.name() == QStringLiteral("si")) {
                inString = true;
                current.clear();
            } else if (inString && shared.isStartElement() && shared.name() == QStringLiteral("t")) {
                current += shared.readElementText();
            } else if (shared.isEndElement() && shared.name() == QStringLiteral("si")) {
                sharedStrings.append(current);
                inString = false;
            }
        }
    }
    const QByteArray sheetXml = archive.readFile(sheetName, &error);
    if (sheetXml.isEmpty())
        return false;
    QXmlStreamReader reader(sheetXml);
    QVector<QVector<QString>> cells;
    int maxRow = 0;
    int maxColumn = 0;
    QString cellRef;
    QString cellType;
    QString value;
    QString inlineValue;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            if (name == QStringLiteral("c")) {
                cellRef = reader.attributes().value(QStringLiteral("r")).toString();
                cellType = reader.attributes().value(QStringLiteral("t")).toString();
                value.clear();
                inlineValue.clear();
            } else if (name == QStringLiteral("v")) {
                value = reader.readElementText();
            } else if (name == QStringLiteral("t") && cellType == QStringLiteral("inlineStr")) {
                inlineValue += reader.readElementText();
            }
        } else if (reader.isEndElement() && reader.name() == QStringLiteral("c")) {
            const int column = columnFromReference(cellRef);
            const int row = rowFromReference(cellRef) - 1;
            if (row >= 0 && row < 200 && column >= 0 && column < 40) {
                QString cellValue = cellType == QStringLiteral("inlineStr") ? inlineValue : value;
                if (cellType == QStringLiteral("s")) {
                    const int index = cellValue.toInt();
                    if (index >= 0 && index < sharedStrings.size())
                        cellValue = sharedStrings.at(index);
                }
                if (!cellValue.isEmpty()) {
                    if (cells.size() <= row)
                        cells.resize(row + 1);
                    if (cells[row].size() <= column)
                        cells[row].resize(column + 1);
                    cells[row][column] = cellValue;
                    maxRow = qMax(maxRow, row + 1);
                    maxColumn = qMax(maxColumn, column + 1);
                }
            }
        }
    }
    if (maxRow == 0 || maxColumn == 0)
        return false;
    auto *caption = new QLabel(QStringLiteral("工作表预览 · 前 %1 行").arg(maxRow), m_content);
    caption->setObjectName(QStringLiteral("DialogSubtitle"));
    caption->setContentsMargins(14, 10, 14, 4);
    m_contentLayout->addWidget(caption);
    auto *table = new QTableWidget(maxRow, maxColumn, m_content);
    table->setObjectName(QStringLiteral("PreviewSheet"));
    table->setShowGrid(true);
    table->setAlternatingRowColors(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->verticalHeader()->setDefaultSectionSize(26);
    for (int column = 0; column < maxColumn; ++column)
        table->setHorizontalHeaderItem(column, new QTableWidgetItem(cellReference(column, 0).chopped(1)));
    for (int row = 0; row < maxRow; ++row) {
        for (int column = 0; column < maxColumn; ++column) {
            const QString text = row < cells.size() && column < cells[row].size()
                ? cells[row][column] : QString();
            table->setItem(row, column, new QTableWidgetItem(text));
        }
    }
    table->resizeColumnsToContents();
    m_contentLayout->addWidget(table, 1);
    return true;
}

bool PreviewDialog::buildPptxPreview(const QString &path)
{
    ZipArchive archive;
    QString error;
    if (!archive.open(path, &error))
        return false;
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
        return false;

    auto *scroll = new QScrollArea(m_content);
    scroll->setObjectName(QStringLiteral("PreviewSlides"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background:#E8EDF4; border:0; }"));
    auto *host = new QWidget(scroll);
    auto *hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(24, 20, 24, 20);
    hostLayout->setSpacing(18);
    int count = 0;
    for (const SlideEntry &slideEntry : slides) {
        if (count++ >= 30)
            break;
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
            } else if (inParagraph && reader.isStartElement() && reader.name() == QStringLiteral("t")) {
                current += reader.readElementText();
            } else if (reader.isEndElement() && reader.name() == QStringLiteral("p") && inParagraph) {
                if (!current.trimmed().isEmpty())
                    paragraphs.append(current);
                inParagraph = false;
            }
        }
        auto *slide = new QFrame(host);
        slide->setObjectName(QStringLiteral("SlidePreview"));
        slide->setFixedHeight(300);
        slide->setStyleSheet(QStringLiteral("QFrame#SlidePreview { background:#FFFFFF; "
                                            "border:1px solid #DCE3EC; border-radius:8px; }"));
        auto *slideLayout = new QVBoxLayout(slide);
        slideLayout->setContentsMargins(34, 28, 34, 28);
        slideLayout->setSpacing(10);
        auto *title = new QLabel(paragraphs.isEmpty() ? QStringLiteral("空白幻灯片")
                                                      : paragraphs.first(), slide);
        title->setStyleSheet(QStringLiteral("background:transparent; color:#1F2937;"));
        QFont titleFont = title->font();
        titleFont.setPixelSize(20);
        titleFont.setBold(true);
        title->setFont(titleFont);
        title->setWordWrap(true);
        slideLayout->addWidget(title);
        QString body;
        for (int i = 1; i < paragraphs.size(); ++i) {
            if (!body.isEmpty())
                body += QLatin1Char('\n');
            body += paragraphs.at(i);
        }
        auto *bodyLabel = new QLabel(body, slide);
        bodyLabel->setWordWrap(true);
        bodyLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        bodyLabel->setObjectName(QStringLiteral("DialogSubtitle"));
        bodyLabel->setStyleSheet(QStringLiteral("background:transparent; color:#475569;"));
        slideLayout->addWidget(bodyLabel, 1);
        auto *number = new QLabel(QStringLiteral("幻灯片 %1").arg(slideEntry.number), slide);
        number->setObjectName(QStringLiteral("DialogSubtitle"));
        slideLayout->addWidget(number, 0, Qt::AlignRight);
        hostLayout->addWidget(slide);
    }
    hostLayout->addStretch(1);
    scroll->setWidget(host);
    m_contentLayout->addWidget(scroll, 1);
    return true;
}

bool PreviewDialog::buildOdtPreview(const QString &path)
{
    ZipArchive archive;
    QString error;
    if (!archive.open(path, &error))
        return false;
    const QByteArray xml = archive.readFile(QStringLiteral("content.xml"), &error);
    if (xml.isEmpty())
        return false;
    QXmlStreamReader reader(xml);
    QString html;
    QString paragraph;
    bool inParagraph = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            if (name == QStringLiteral("p") || name == QStringLiteral("h")) {
                inParagraph = true;
                paragraph.clear();
            } else if (inParagraph && name == QStringLiteral("span")) {
                // Text is nested below; read via characters.
            }
        } else if (reader.isCharacters() && inParagraph) {
            paragraph += reader.text().toString().toHtmlEscaped();
        } else if (reader.isEndElement() && (reader.name() == QStringLiteral("p")
                                             || reader.name() == QStringLiteral("h"))
                   && inParagraph) {
            if (!paragraph.trimmed().isEmpty())
                html += QStringLiteral("<p>%1</p>").arg(paragraph);
            inParagraph = false;
        }
    }
    if (html.isEmpty())
        return false;
    auto *browser = new QTextBrowser(m_content);
    browser->setObjectName(QStringLiteral("PreviewDocument"));
    browser->setFrameShape(QFrame::NoFrame);
    browser->setHtml(QStringLiteral("<html><head>%1</head><body><div class=\"page\">%2</div></body></html>")
                         .arg(documentStyle(), html));
    m_contentLayout->addWidget(browser, 1);
    return true;
}

bool PreviewDialog::buildWpsPreview(const QString &path, const QString &suffix)
{
    ZipArchive archive;
    if (archive.open(path, nullptr)) {
        if (suffix == QStringLiteral("wps") && archive.contains(QStringLiteral("word/document.xml")))
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
    const QString cacheKey = QString::fromLatin1(QCryptographicHash::hash(
        (source.absoluteFilePath() + QString::number(source.size())
         + QString::number(source.lastModified().toMSecsSinceEpoch())).toUtf8(),
        QCryptographicHash::Sha1).toHex());
    const QString cachePath = QDir(cacheDir).filePath(cacheKey + QStringLiteral(".html"));
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

bool PreviewDialog::buildPdfPreview(const QString &path)
{
    const QString pdftoppm = QStandardPaths::findExecutable(QStringLiteral("pdftoppm"));
    const QString pdftotext = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
    if (pdftoppm.isEmpty()) {
        if (!pdftotext.isEmpty()) {
            QProcess process;
            process.start(pdftotext, QStringList() << QStringLiteral("-layout") << path
                                                    << QStringLiteral("-"));
            if (process.waitForStarted(1000) && process.waitForFinished(8000)
                && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
                const QString text = QString::fromLocal8Bit(process.readAllStandardOutput());
                if (!text.trimmed().isEmpty()) {
                    auto *view = new QPlainTextEdit(m_content);
                    view->setObjectName(QStringLiteral("PreviewPdfText"));
                    view->setReadOnly(true);
                    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
                    view->setPlainText(QStringLiteral("PDF 文本预览（未安装 pdftoppm，"
                                                      "仅显示文本层）\n\n") + text);
                    m_contentLayout->addWidget(view, 1);
                    return true;
                }
            }
        }
        return false;
    }

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
            this, [this, path](int exitCode, QProcess::ExitStatus exitStatus) {
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
        QStringList pages;
        if (exitStatus == QProcess::NormalExit && exitCode == 0 && temp) {
            pages = QDir(temp->path()).entryList(QStringList() << QStringLiteral("*.png"),
                                                 QDir::Files, QDir::Name);
        }
        const QString tempPath = temp ? temp->path() : QString();
        if (!pages.isEmpty()) {
            QVector<QPixmap> pixmaps;
            for (const QString &name : pages) {
                QPixmap pixmap(QDir(tempPath).filePath(name));
                if (!pixmap.isNull())
                    pixmaps.append(pixmap);
            }
            if (!pixmaps.isEmpty()) {
                delete temp;
                clearContent();
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
                for (const QPixmap &pixmap : pixmaps) {
                    auto *label = new QLabel(pageHost);
                    label->setObjectName(QStringLiteral("PreviewPdfPage"));
                    QPixmap scaled = pixmap;
                    if (scaled.width() > 820)
                        scaled = scaled.scaledToWidth(820, Qt::SmoothTransformation);
                    label->setPixmap(scaled);
                    label->setAlignment(Qt::AlignCenter);
                    label->setStyleSheet(QStringLiteral(
                        "QLabel { background:white; border:0; }"));
                    pageLayout->addWidget(label, 0, Qt::AlignHCenter);
                }
                scroll->setWidget(pageHost);
                m_contentLayout->addWidget(scroll, 1);
                return;
            }
        }
        delete temp;
        clearContent();
        const QString pdftotext = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
        if (!pdftotext.isEmpty()) {
            QProcess textProcess;
            textProcess.start(pdftotext, QStringList() << QStringLiteral("-layout") << path
                                                        << QStringLiteral("-"));
            if (textProcess.waitForStarted(1000) && textProcess.waitForFinished(8000)
                && textProcess.exitStatus() == QProcess::NormalExit
                && textProcess.exitCode() == 0) {
                const QString text = QString::fromLocal8Bit(textProcess.readAllStandardOutput());
                if (!text.trimmed().isEmpty()) {
                    auto *view = new QPlainTextEdit(m_content);
                    view->setObjectName(QStringLiteral("PreviewPdfText"));
                    view->setReadOnly(true);
                    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
                    view->setPlainText(QStringLiteral("PDF 文本预览（页面渲染失败）\n\n") + text);
                    m_contentLayout->addWidget(view, 1);
                    return;
                }
            }
        }
        buildFallbackPreview(path, QStringLiteral("PDF 渲染失败，请安装 poppler-utils"));
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
    ZipArchive archive;
    if (!archive.open(path, nullptr))
        return false;
    QStringList paragraphs;
    QVector<QByteArray> images;
    for (const ZipEntryInfo &entry : archive.entries()) {
        if (entry.isDir)
            continue;
        const QString lower = entry.name.toLower();
        if (lower.endsWith(QStringLiteral(".xml"))) {
            const QByteArray xml = archive.readFile(entry.name);
            if (xml.isEmpty())
                continue;
            QXmlStreamReader reader(xml);
            while (!reader.atEnd()) {
                reader.readNext();
                if (!reader.isStartElement())
                    continue;
                const QString name = reader.name().toString();
                if (name == QStringLiteral("TextCode") || name == QStringLiteral("Text")
                    || name == QStringLiteral("Title")) {
                    const QString text = reader.readElementText().trimmed();
                    if (!text.isEmpty() && !paragraphs.contains(text))
                        paragraphs.append(text);
                }
            }
        } else if (lower.endsWith(QStringLiteral(".png"))
                   || lower.endsWith(QStringLiteral(".jpg"))
                   || lower.endsWith(QStringLiteral(".jpeg"))
                   || lower.endsWith(QStringLiteral(".bmp"))) {
            const QByteArray data = archive.readFile(entry.name);
            if (!data.isEmpty() && images.size() < 6)
                images.append(data);
        }
    }
    if (!paragraphs.isEmpty()) {
        QString html;
        for (const QString &paragraph : paragraphs)
            html += QStringLiteral("<p>%1</p>").arg(paragraph.toHtmlEscaped());
        auto *browser = new QTextBrowser(m_content);
        browser->setObjectName(QStringLiteral("PreviewDocument"));
        browser->setFrameShape(QFrame::NoFrame);
        browser->setStyleSheet(QStringLiteral("QTextBrowser { background:white; border:0; }"));
        browser->document()->setDocumentMargin(40);
        browser->setHtml(QStringLiteral(
            "<html><head>%1</head><body><div class=\"page\"><p style=\"color:#8a94a3\">"
            "OFD 文本预览（版式可能不完整）</p>%2</div></body></html>")
                             .arg(documentStyle(), html));
        m_contentLayout->addWidget(browser, 1);
        return true;
    }
    if (!images.isEmpty()) {
        auto *scroll = new QScrollArea(m_content);
        scroll->setObjectName(QStringLiteral("PreviewOfd"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        auto *host = new QWidget(scroll);
        auto *layout = new QVBoxLayout(host);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(12);
        layout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        for (const QByteArray &data : images) {
            QPixmap pixmap;
            if (!pixmap.loadFromData(data))
                continue;
            auto *label = new QLabel(host);
            label->setObjectName(QStringLiteral("PreviewOfdPage"));
            if (pixmap.width() > 820)
                pixmap = pixmap.scaledToWidth(820, Qt::SmoothTransformation);
            label->setPixmap(pixmap);
            label->setStyleSheet(QStringLiteral("QLabel { background:white; border:0; }"));
            layout->addWidget(label, 0, Qt::AlignHCenter);
        }
        scroll->setWidget(host);
        if (host->layout()->count() > 0) {
            m_contentLayout->addWidget(scroll, 1);
            return true;
        }
    }
    return false;
}

bool PreviewDialog::buildArchivePreview(const QString &path, const QString &suffix)
{


    struct ArchiveRow {
        QString name;
        qint64 size = 0;
        QDateTime modified;
        bool isDir = false;
    };
    QVector<ArchiveRow> rows;
    if (suffix == QStringLiteral("zip")) {
        ZipArchive archive;
        QString error;
        if (!archive.open(path, &error))
            return false;
        for (const ZipEntryInfo &entry : archive.entries()) {
            ArchiveRow row;
            row.name = entry.name;
            row.size = entry.uncompressedSize;
            row.modified = entry.modified;
            row.isDir = entry.isDir;
            rows.append(row);
        }
    } else if (suffix == QStringLiteral("tar") || suffix == QStringLiteral("tgz")) {
        const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar"));
        if (tar.isEmpty())
            return false;
        QProcess process;
        process.start(tar, QStringList() << QStringLiteral("-tf") << path);
        if (!process.waitForStarted(2500) || !process.waitForFinished(5000)
            || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            return false;
        const QStringList names = QString::fromLocal8Bit(process.readAllStandardOutput())
                                      .split(QLatin1Char('\n'), QString::SkipEmptyParts);
        for (const QString &name : names) {
            ArchiveRow row;
            row.name = name;
            row.isDir = name.endsWith(QLatin1Char('/'));
            rows.append(row);
        }
    } else {
        return false;
    }
    if (rows.isEmpty())
        return false;
    QSet<QString> directoryPaths;
    for (const ArchiveRow &row : rows) {
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
        for (const ArchiveRow &row : rows) {
            if (row.isDir && row.name == directory + QLatin1Char('/')) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            ArchiveRow row;
            row.name = directory;
            row.isDir = true;
            rows.append(row);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const ArchiveRow &a, const ArchiveRow &b) {
        if (a.isDir != b.isDir)
            return a.isDir;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    auto *caption = new QLabel(QStringLiteral("压缩包内容 · %1 项").arg(rows.size()), m_content);
    caption->setObjectName(QStringLiteral("DialogSubtitle"));
    caption->setContentsMargins(14, 10, 14, 4);
    m_contentLayout->addWidget(caption);
    auto *table = new QTableWidget(rows.size(), 3, m_content);
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
    for (int row = 0; row < rows.size(); ++row) {
        const ArchiveRow &entry = rows.at(row);
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
    return true;
}
bool PreviewDialog::buildImagePreview(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (!reader.canRead())
        return false;
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid() && (sourceSize.width() > 1600 || sourceSize.height() > 1200))
        reader.setScaledSize(sourceSize.scaled(QSize(1600, 1200), Qt::KeepAspectRatio));
    const QImage image = reader.read();
    if (image.isNull())
        return false;
    auto *label = new QLabel(m_content);
    label->setAlignment(Qt::AlignCenter);
    label->setPixmap(QPixmap::fromImage(image).scaled(860, 540, Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
    m_contentLayout->addWidget(label, 1);
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
    text->setPlainText(QString::fromUtf8(data));
    if (QFileInfo(path).size() > data.size())
        text->appendPlainText(QStringLiteral("\n\n[仅预览前 512 KB]"));
    m_contentLayout->addWidget(text, 1);
    return true;
}

void PreviewDialog::buildDirectoryPreview(const QString &path)
{
    const QFileInfo info(path);
    auto *folder = new QLabel(QStringLiteral("文件夹预览\n\n%1\n\n项目数：%2")
                                  .arg(info.absoluteFilePath())
                                  .arg(QDir(info.absoluteFilePath()).entryList(
                                      QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).size()),
                              m_content);
    folder->setAlignment(Qt::AlignCenter);
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

void PreviewDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        accept();
        return;
    }
    QDialog::keyPressEvent(event);
}
