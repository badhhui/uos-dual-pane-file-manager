#pragma once

#include <QColor>
#include <QString>

class Theme
{
public:
    static Theme &instance();

    bool isDark() const;
    void setDark(bool dark);

    QColor window() const;
    QColor surface() const;
    QColor sidebar() const;
    QColor line() const;
    QColor lineStrong() const;
    QColor text() const;
    QColor textSecondary() const;
    QColor textMuted() const;
    QColor accent() const;
    QColor accentSoft() const;
    QColor selected() const;
    QColor selectedInactive() const;
    QColor hover() const;
    QColor field() const;
    QColor paneHeaderActive() const;
    QColor success() const;
    QColor warning() const;
    QColor danger() const;
    QColor scrollbar() const;
    QColor menuBackground() const;
    QColor alternateRow() const;
    QString fontFamily() const;

    QString styleSheet() const;
    void applyToApplication() const;

private:
    Theme();
    QString cssColor(const QColor &color) const;
    QColor m_window;
    QColor m_surface;
    QColor m_sidebar;
    QColor m_line;
    QColor m_lineStrong;
    QColor m_text;
    QColor m_textSecondary;
    QColor m_textMuted;
    QColor m_accent;
    QColor m_accentSoft;
    QColor m_selected;
    QColor m_selectedInactive;
    QColor m_hover;
    QColor m_field;
    QColor m_paneHeaderActive;
    QColor m_success;
    QColor m_warning;
    QColor m_danger;
    QColor m_scrollbar;
    QColor m_menuBackground;
    QColor m_alternateRow;
    QString m_fontFamily;
    bool m_dark;
    void resetColors();
    QString cssColorAlpha(const QColor &color) const;
};
