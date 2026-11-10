#include "theme.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStringList>

Theme &Theme::instance()
{
    static Theme theme;
    return theme;
}

Theme::Theme()
    : m_dark(false)
{
    const QStringList preferred = {
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Noto Sans CJK JP"),
        QStringLiteral("Source Han Sans SC"),
        QStringLiteral("Noto Sans SC"),
        QStringLiteral("WenQuanYi Micro Hei"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("PingFang SC"),
        QStringLiteral("Sans Serif")
    };
    const QStringList available = QFontDatabase().families();
    for (const QString &family : preferred) {
        if (available.contains(family, Qt::CaseInsensitive)) {
            m_fontFamily = family;
            break;
        }
    }
    if (m_fontFamily.isEmpty())
        m_fontFamily = QStringLiteral("Sans Serif");
    resetColors();
}

bool Theme::isDark() const { return m_dark; }

QColor Theme::window() const { return m_window; }
QColor Theme::surface() const { return m_surface; }
QColor Theme::sidebar() const { return m_sidebar; }
QColor Theme::line() const { return m_line; }
QColor Theme::lineStrong() const { return m_lineStrong; }
QColor Theme::text() const { return m_text; }
QColor Theme::textSecondary() const { return m_textSecondary; }
QColor Theme::textMuted() const { return m_textMuted; }
QColor Theme::accent() const { return m_accent; }
QColor Theme::accentSoft() const { return m_accentSoft; }
QColor Theme::selected() const { return m_selected; }
QColor Theme::selectedInactive() const { return m_selectedInactive; }
QColor Theme::hover() const { return m_hover; }
QColor Theme::field() const { return m_field; }
QColor Theme::paneHeaderActive() const { return m_paneHeaderActive; }
QColor Theme::success() const { return m_success; }
QColor Theme::warning() const { return m_warning; }
QColor Theme::danger() const { return m_danger; }
QColor Theme::scrollbar() const { return m_scrollbar; }
QColor Theme::menuBackground() const { return m_menuBackground; }
QColor Theme::alternateRow() const { return m_alternateRow; }
QString Theme::fontFamily() const { return m_fontFamily; }

void Theme::setDark(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    resetColors();
}

void Theme::resetColors()
{
    if (!m_dark) {
        m_window = QColor(QStringLiteral("#FBFCFE"));
        m_surface = QColor(QStringLiteral("#FFFFFF"));
        m_sidebar = QColor(QStringLiteral("#F5F8FC"));
        m_line = QColor(QStringLiteral("#E8EDF4"));
        m_lineStrong = QColor(QStringLiteral("#DBE3ED"));
        m_text = QColor(QStringLiteral("#1C2635"));
        m_textSecondary = QColor(QStringLiteral("#5B687A"));
        m_textMuted = QColor(QStringLiteral("#8E99A8"));
        m_accent = QColor(QStringLiteral("#2168F5"));
        m_accentSoft = QColor(QStringLiteral("#EBF2FF"));
        m_selected = QColor(QStringLiteral("#BED5FF"));
        m_selectedInactive = QColor(QStringLiteral("#D6E1F1"));
        m_hover = QColor(QStringLiteral("#F7FAFF"));
        m_field = QColor(QStringLiteral("#F4F7FB"));
        m_paneHeaderActive = QColor(QStringLiteral("#F2F7FF"));
        m_success = QColor(QStringLiteral("#149E68"));
        m_warning = QColor(QStringLiteral("#B4740E"));
        m_danger = QColor(QStringLiteral("#DF4651"));
        m_scrollbar = QColor(QStringLiteral("#D6DEE9"));
        m_menuBackground = QColor(QStringLiteral("#FFFFFF"));
        m_alternateRow = QColor(QStringLiteral("#F8FAFD"));
    } else {
        m_window = QColor(QStringLiteral("#151A23"));
        m_surface = QColor(QStringLiteral("#181E28"));
        m_sidebar = QColor(QStringLiteral("#121821"));
        m_line = QColor(QStringLiteral("#26303D"));
        m_lineStrong = QColor(QStringLiteral("#34404F"));
        m_text = QColor(QStringLiteral("#E9EEF6"));
        m_textSecondary = QColor(QStringLiteral("#A9B5C5"));
        m_textMuted = QColor(QStringLiteral("#7D8A9B"));
        m_accent = QColor(QStringLiteral("#73A2FF"));
        m_accentSoft = QColor(115, 162, 255, 28);
        m_selected = QColor(82, 132, 255, 44);
        m_selectedInactive = QColor(255, 255, 255, 14);
        m_hover = QColor(255, 255, 255, 9);
        m_field = QColor(QStringLiteral("#1D242F"));
        m_paneHeaderActive = QColor(54, 104, 205, 28);
        m_success = QColor(QStringLiteral("#47CF92"));
        m_warning = QColor(QStringLiteral("#E7AD55"));
        m_danger = QColor(QStringLiteral("#FF7079"));
        m_scrollbar = QColor(QStringLiteral("#3A4554"));
        m_menuBackground = QColor(QStringLiteral("#1A202B"));
        m_alternateRow = QColor(QStringLiteral("#1B222D"));
    }
}

QString Theme::cssColor(const QColor &color) const
{
    return color.name(QColor::HexRgb);
}

QString Theme::cssColorAlpha(const QColor &color) const
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alphaF(), 0, 'f', 3);
}

void Theme::applyToApplication() const
{
    qApp->setStyleSheet(styleSheet());
    QPalette palette;
    palette.setColor(QPalette::Window, m_window);
    palette.setColor(QPalette::WindowText, m_text);
    palette.setColor(QPalette::Base, m_surface);
    palette.setColor(QPalette::AlternateBase, m_field);
    palette.setColor(QPalette::Text, m_text);
    palette.setColor(QPalette::Button, m_surface);
    palette.setColor(QPalette::ButtonText, m_textSecondary);
    palette.setColor(QPalette::Highlight, m_accent);
    palette.setColor(QPalette::HighlightedText, QColor(Qt::white));
    palette.setColor(QPalette::ToolTipBase, m_menuBackground);
    palette.setColor(QPalette::ToolTipText, m_text);
    qApp->setPalette(palette);
    QFont font = qApp->font();
    font.setFamily(m_fontFamily);
    font.setPointSizeF(10.5);
    qApp->setFont(font);
}

QString Theme::styleSheet() const
{
    const QString primary = cssColor(m_text);
    const QString secondary = cssColor(m_textSecondary);
    const QString muted = cssColor(m_textMuted);
    const QString accent = cssColor(m_accent);
    const QString line = cssColor(m_line);
    const QString lineStrong = cssColor(m_lineStrong);
    const QString surface = cssColor(m_surface);
    const QString window = cssColor(m_window);
    const QString sidebar = cssColor(m_sidebar);
    const QString field = cssColor(m_field);
    const QString hover = cssColorAlpha(m_hover);
    const QString selected = cssColorAlpha(m_selected);
    const QString accentSoft = cssColorAlpha(m_accentSoft);
    const QString paneHead = cssColorAlpha(m_paneHeaderActive);
    const QString scroll = cssColor(m_scrollbar);
    const QString menu = cssColor(m_menuBackground);
    const QString danger = cssColor(m_danger);
    const QString success = cssColor(m_success);

    const QString values[21] = {
        m_fontFamily, window, primary, surface, line, secondary, hover, field, muted,
        lineStrong, accent, QColor(m_accent).lighter(108).name(), QColor(m_accent).lighter(155).name(),
        sidebar, accentSoft, paneHead, surface, menu, scroll, danger, success
    };
    QString result = QStringLiteral(R"QSS(
* {
    font-family: "%1";
    font-size: 13px;
    outline: none;
}
QMainWindow, QWidget#MainCentral {
    background: %2;
    color: %3;
}
QFrame#TopToolbar {
    background: %4;
    border-bottom: 1px solid %5;
}
QToolButton {
    background: transparent;
    border: 0;
    border-radius: 6px;
    padding: 5px;
    color: %6;
}
QToolButton:hover {
    background: %7;
    color: %3;
}
QToolButton:pressed {
    background: %8;
}
QToolButton:disabled {
    color: %9;
}
QToolButton#NewButton {
    border: 1px solid %10;
    background: %4;
    padding: 6px 10px;
    color: %6;
    font-size: 13px;
}
QToolButton#NewButton:hover {
    background: %7;
}
QPushButton#CopyButton {
    background: %11;
    color: #FFFFFF;
    border: 0;
    border-radius: 7px;
    padding: 7px 13px;
    font-size: 13px;
    font-weight: 600;
}
QPushButton#CopyButton:hover {
    background: %12;
}
QPushButton#CopyButton:disabled {
    background: %13;
    color: #FFFFFF;
}
QLineEdit#SearchInput {
    background: %8;
    border: 1px solid %5;
    border-radius: 7px;
    padding: 6px 9px;
    color: %3;
    selection-background-color: %11;
    font-size: 13px;
}
QLineEdit#SearchInput:focus {
    border: 1px solid %11;
}
QFrame#Sidebar {
    background: %14;
    border-right: 1px solid %5;
}
QScrollArea#SidebarScroll, QWidget#SidebarContent, QScrollArea#SidebarScroll > QWidget > QWidget {
    background: transparent;
}
QLabel#SideSection {
    color: %9;
    font-size: 11px;
    font-weight: 600;
    padding: 10px 12px 4px 12px;
}
QToolButton#SideButton {
    color: %6;
    border: 0;
    border-radius: 7px;
    padding: 7px 11px;
    text-align: left;
    font-size: 13px;
}
QToolButton#SideButton:hover {
    background: %7;
    color: %3;
}
QToolButton#SideButton[active="true"] {
    background: %15;
    color: %11;
    font-weight: 600;
}
QFrame#Pane {
    background: %4;
}
QFrame#PaneHeader {
    background: %4;
    border-bottom: 1px solid %5;
}
QFrame#PaneHeader[active="true"] {
    background: %16;
    border-top: 2px solid %11;
}
QToolButton#CrumbButton {
    color: %9;
    border-radius: 4px;
    padding: 2px 4px;
    font-size: 12px;
}
QToolButton#CrumbButton:hover {
    color: %11;
    background: %7;
}
QLabel#CrumbSep {
    color: %9;
    font-size: 12px;
    padding: 0 1px;
}
QLabel#CrumbCurrent {
    color: %3;
    font-size: 12px;
    font-weight: 600;
}
QLabel#LocationIcon {
    background: %8;
    border-radius: 7px;
}
QTableView#FileTable {
    background: %4;
    border: 0;
    gridline-color: transparent;
    selection-background-color: transparent;
    selection-color: %3;
    font-size: 13px;
}
QTableView#FileTable::item {
    border: 0;
}
QHeaderView::section {
    background: %4;
    color: %9;
    border: 0;
    border-bottom: 1px solid %5;
    padding: 7px 5px;
    font-size: 11px;
    font-weight: 500;
}
QHeaderView::section:hover {
    color: %6;
}
QTableCornerButton::section {
    background: %4;
    border: 0;
}
QFrame#PaneFooter {
    background: %4;
    border-top: 1px solid %5;
}
QToolButton#CrumbButton, QToolButton#CrumbMore {
    background: transparent;
    border: 0;
    padding: 2px 4px;
}
QToolButton#CrumbButton:hover, QToolButton#CrumbMore:hover {
    background: transparent;
    color: %11;
}
QToolButton#CrumbMore::menu-indicator {
    image: none;
    width: 0px;
}
QLabel#FooterCount, QLabel#FooterSelection {
    color: %6;
    font-size: 12px;
    font-weight: 500;
}
QLabel#FooterSelection[selected="true"] {
    color: %11;
}
QFrame#TransferBar {
    background: %17;
    border-top: 1px solid %5;
}
QProgressBar {
    background: %8;
    border: 0;
    border-radius: 2px;
    text-align: center;
}
QProgressBar::chunk {
    background: %11;
    border-radius: 2px;
}
QProgressBar#TransferProgress {
    min-height: 5px;
    max-height: 5px;
}
QProgressBar#TaskProgress {
    min-height: 8px;
    max-height: 8px;
    border-radius: 4px;
    background: %8;
}
QProgressBar#TaskProgress::chunk {
    background: %11;
    border-radius: 4px;
}
QLabel#TaskProgressLabel {
    color: %11;
    font-size: 10px;
    font-weight: 600;
}
QToolButton#DeviceEject {
    background: transparent;
    border: 0;
    border-radius: 5px;
}
QToolButton#DeviceEject:hover {
    background: %7;
}
QLabel#TransferTitle {
    color: %3;
    font-size: 12px;
    font-weight: 600;
}
QLabel#TransferSubtitle {
    color: %9;
    font-size: 10px;
}
QLabel#TransferDetails {
    color: %9;
    font-size: 11px;
}
QLabel#TransferPercent {
    color: %11;
    font-size: 11px;
    font-weight: 600;
}
QLabel#TransferIcon {
    background: %15;
    border-radius: 8px;
}
QToolButton#TransferControl {
    background: transparent;
    border: 0;
    border-radius: 5px;
}
QToolButton#TransferControl:hover {
    background: %7;
}
QDialog#TransferDialog {
    background: %4;
}
QLabel#DialogTitle {
    color: %3;
    font-size: 16px;
    font-weight: 600;
}
QLabel#DialogSubtitle {
    color: %9;
    font-size: 11px;
}
QTableWidget#TransferTable {
    background: %4;
    border: 0;
    gridline-color: transparent;
    color: %3;
    font-size: 12px;
}
QTableWidget#TransferTable::item {
    border-bottom: 1px solid %5;
    padding: 3px 6px;
}
QTableWidget#TransferTable QHeaderView::section {
    background: %4;
    color: %9;
    border: 0;
    border-bottom: 1px solid %5;
    padding: 7px 6px;
    font-size: 11px;
}
QStatusBar {
    background: %4;
    color: %9;
    border-top: 1px solid %5;
    font-size: 10px;
}
QStatusBar::item {
    border: 0;
}
QStatusBar QLabel {
    color: %9;
    font-size: 10px;
}
QSplitter::handle {
    background: %5;
    width: 1px;
}
QMenu {
    background: %18;
    border: 1px solid %10;
    border-radius: 8px;
    padding: 5px;
    color: %3;
}
QMenu::item {
    border-radius: 5px;
    padding: 6px 28px 6px 28px;
}
QMenu::item:selected {
    background: %15;
    color: %3;
}
QMenu::separator {
    height: 1px;
    background: %5;
    margin: 5px 7px;
}
QDialog, QMessageBox, QInputDialog {
    background: %4;
    color: %3;
}
QPushButton {
    background: %4;
    border: 1px solid %10;
    border-radius: 6px;
    padding: 6px 13px;
    color: %6;
}
QPushButton:hover {
    background: %7;
    color: %3;
}
QPushButton:default {
    background: %11;
    color: #fff;
    border: 1px solid %11;
}
QLabel {
    color: %3;
}
QLineEdit {
    background: %8;
    border: 1px solid %10;
    border-radius: 6px;
    padding: 5px 7px;
    color: %3;
    selection-background-color: %11;
}
QComboBox, QSpinBox {
    background: %8;
    border: 1px solid %10;
    border-radius: 6px;
    padding: 4px 6px;
    color: %3;
}
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: %19;
    min-height: 28px;
    border-radius: 4px;
    margin: 2px;
}
QScrollBar::handle:vertical:hover {
    background: %9;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
}
QScrollBar::handle:horizontal {
    background: %19;
    min-width: 28px;
    border-radius: 4px;
    margin: 2px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
}
QToolTip {
    background: %18;
    color: %3;
    border: 1px solid %10;
    padding: 4px 6px;
}
QLabel#DangerText {
    color: %20;
}

QLabel#SuccessText {
    color: %21;
}
QToolButton#TasksButton {
    background: transparent;
    border: 0;
    border-radius: 6px;
}
QToolButton#TasksButton:hover {
    background: %7;
}
QToolButton#SearchScopeButton {
    background: transparent;
    border: 1px solid %10;
    border-radius: 6px;
    color: %9;
    font-size: 12px;
    padding: 3px 8px;
    min-height: 28px;
}
QToolButton#SearchScopeButton:hover {
    background: %7;
}
QToolButton#BookmarkButton, QToolButton#HiddenButton {
    background: transparent;
    border: 0;
    border-radius: 6px;
}
QToolButton#BookmarkButton:hover, QToolButton#HiddenButton:hover {
    background: %7;
}
QToolButton#BookmarkButton:checked, QToolButton#HiddenButton:checked {
    background: %15;
}
)QSS");
    for (int i = 21; i >= 1; --i)
        result.replace(QStringLiteral("%") + QString::number(i), values[i - 1]);
    return result;
}
