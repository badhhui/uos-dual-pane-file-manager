#include "moderndialog.h"

#include "iconprovider.h"
#include "theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {
const char *const kAccent = "#2168F5";
const char *const kText = "#1F2937";
const char *const kMuted = "#64748B";
const char *const kBorder = "#E2E8F0";

QString roleStyle(DialogActionRole role, bool stacked, DialogStyle style)
{
    const QString radius = style == DialogStyle::Sheet ? QStringLiteral("8px")
                                                          : QStringLiteral("10px");
    const QString padding = stacked ? QStringLiteral("12px 14px") : QStringLiteral("8px 18px");
    switch (role) {
    case DialogActionRole::Primary:
        return QStringLiteral("QPushButton { background:%1; color:white; border:0; border-radius:%2; "
                              "padding:%3; font-weight:600; } "
                              "QPushButton:hover { background:#1557D6; }")
            .arg(QLatin1String(kAccent), radius, padding);
    case DialogActionRole::Destructive:
        if (stacked)
            return QStringLiteral("QPushButton { background:#FFF5F5; color:#B91C1C; "
                                  "border:1px solid #FECACA; border-radius:%1; padding:%2; "
                                  "font-weight:600; text-align:left; } "
                                  "QPushButton:hover { background:#FEE2E2; }").arg(radius, padding);
        return QStringLiteral("QPushButton { background:#DC2626; color:white; border:0; "
                              "border-radius:%1; padding:%2; font-weight:600; } "
                              "QPushButton:hover { background:#B91C1C; }").arg(radius, padding);
    case DialogActionRole::Cancel:
        return QStringLiteral("QPushButton { background:transparent; color:%1; border:0; "
                              "border-radius:%2; padding:%3; } "
                              "QPushButton:hover { background:#F1F5F9; }").arg(QLatin1String(kMuted),
                                                                                radius, padding);
    case DialogActionRole::Secondary:
    default:
        return QStringLiteral("QPushButton { background:#F8FAFC; color:#334155; "
                              "border:1px solid %1; border-radius:%2; padding:%3; } "
                              "QPushButton:hover { background:#F1F5F9; }").arg(QLatin1String(kBorder),
                                                                                radius, padding);
    }
}

QString toneColor(DialogTone tone)
{
    switch (tone) {
    case DialogTone::Danger: return QStringLiteral("#DC2626");
    case DialogTone::Warning: return QStringLiteral("#D97706");
    case DialogTone::Question: return QStringLiteral("#2168F5");
    case DialogTone::Info:
    default: return QStringLiteral("#2168F5");
    }
}

QString toneSoftColor(DialogTone tone)
{
    switch (tone) {
    case DialogTone::Danger: return QStringLiteral("#FEF2F2");
    case DialogTone::Warning: return QStringLiteral("#FFF7ED");
    case DialogTone::Question: return QStringLiteral("#EAF1FF");
    case DialogTone::Info:
    default: return QStringLiteral("#EAF1FF");
    }
}

QString toneIcon(DialogTone tone)
{
    switch (tone) {
    case DialogTone::Danger:
    case DialogTone::Warning: return QStringLiteral("alert");
    case DialogTone::Question:
    case DialogTone::Info:
    default: return QStringLiteral("info");
    }
}
}

ModernDialog::ModernDialog(DialogStyle style, DialogTone tone, const QString &title,
                           const QString &message, const QVector<DialogAction> &actions,
                           const QString &extraCheckText, const QString &defaultActionId,
                           QWidget *parent)
    : QDialog(parent)
    , m_style(style)
    , m_defaultActionId(defaultActionId)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(26, 26, 26, 26);
    m_card = new QFrame(this);
    m_card->setObjectName(QStringLiteral("ModernDialogCard"));
    auto *shadow = new QGraphicsDropShadowEffect(m_card);
    shadow->setBlurRadius(42);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(15, 23, 42, 52));
    m_card->setGraphicsEffect(shadow);
    outer->addWidget(m_card);

    QString cardStyle;
    if (style == DialogStyle::Accent)
        cardStyle = QStringLiteral("QFrame#ModernDialogCard { background:white; border:0; border-radius:14px; }");
    else if (style == DialogStyle::Sheet)
        cardStyle = QStringLiteral("QFrame#ModernDialogCard { background:white; border:1px solid #E2E8F0; "
                                   "border-radius:12px; }");
    else
        cardStyle = QStringLiteral("QFrame#ModernDialogCard { background:white; border:1px solid #E2E8F0; "
                                   "border-radius:16px; }");
    m_card->setStyleSheet(cardStyle);

    auto *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    if (style == DialogStyle::Accent) {
        auto *accent = new QFrame(m_card);
        accent->setFixedHeight(4);
        accent->setStyleSheet(QStringLiteral("background:%1; border:0; border-top-left-radius:14px; "
                                             "border-top-right-radius:14px;").arg(toneColor(tone)));
        cardLayout->addWidget(accent);
    }

    auto *content = new QWidget(m_card);
    auto *layout = new QVBoxLayout(content);
    const int margin = style == DialogStyle::Sheet ? 18 : 22;
    layout->setContentsMargins(margin, style == DialogStyle::Accent ? 18 : margin,
                               margin, style == DialogStyle::Sheet ? 16 : margin);
    layout->setSpacing(style == DialogStyle::Sheet ? 8 : 12);

    auto *header = new QHBoxLayout();
    header->setSpacing(12);
    const int iconSize = style == DialogStyle::Sheet ? 26 : (style == DialogStyle::Accent ? 40 : 40);
    auto *icon = new QLabel(content);
    icon->setFixedSize(iconSize, iconSize);
    icon->setAlignment(Qt::AlignCenter);
    if (style == DialogStyle::Sheet)
        icon->setStyleSheet(QStringLiteral("background:transparent;"));
    else
        icon->setStyleSheet(QStringLiteral("background:%1; border-radius:%2px;")
                                .arg(toneSoftColor(tone)).arg(iconSize / 2));
    icon->setPixmap(IconProvider::instance().uiIcon(toneIcon(tone), QColor(toneColor(tone)))
                        .pixmap(style == DialogStyle::Sheet ? 22 : 20,
                               style == DialogStyle::Sheet ? 22 : 20));
    header->addWidget(icon, 0, Qt::AlignTop);

    auto *texts = new QVBoxLayout();
    texts->setSpacing(4);
    auto *titleLabel = new QLabel(title, content);
    titleLabel->setObjectName(QStringLiteral("ModernDialogTitle"));
    QFont titleFont = titleLabel->font();
    titleFont.setPixelSize(style == DialogStyle::Sheet ? 15 : 17);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setWordWrap(true);
    texts->addWidget(titleLabel);
    auto *messageLabel = new QLabel(message, content);
    messageLabel->setObjectName(QStringLiteral("ModernDialogMessage"));
    messageLabel->setWordWrap(true);
    messageLabel->setTextFormat(Qt::PlainText);
    messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    texts->addWidget(messageLabel);
    header->addLayout(texts, 1);
    layout->addLayout(header);

    if (!extraCheckText.isEmpty()) {
        m_extraCheck = new QCheckBox(extraCheckText, content);
        m_extraCheck->setObjectName(QStringLiteral("ModernDialogExtraCheck"));
        m_extraCheck->setStyleSheet(QStringLiteral(
            "QCheckBox { color:#64748B; spacing:8px; }"
            "QCheckBox::indicator { width:16px; height:16px; border:1px solid #CBD5E1; "
            "border-radius:4px; background:#FFFFFF; }"
            "QCheckBox::indicator:checked { background:#2168F5; border-color:#2168F5; "
            "image:url(:/icons/ui/check.png); }"));
        layout->addWidget(m_extraCheck);
    }

    bool stacked = style == DialogStyle::Accent || style == DialogStyle::Sheet;
    for (const DialogAction &action : actions) {
        if (!action.description.isEmpty())
            stacked = true;
    }

    auto *actionLayout = new QVBoxLayout();
    actionLayout->setSpacing(7);
    QVector<DialogAction> ordered = actions;
    if (stacked) {
        for (int i = 0; i < ordered.size(); ++i) {
            if (ordered.at(i).role == DialogActionRole::Cancel) {
                ordered.move(i, ordered.size() - 1);
                break;
            }
        }
    }
    QPushButton *defaultButton = nullptr;
    for (const DialogAction &action : ordered) {
        QPushButton *button = createActionButton(action, stacked);
        actionLayout->addWidget(button);
        if (action.id == m_defaultActionId)
            defaultButton = button;
        if (!defaultButton && action.role == DialogActionRole::Primary)
            defaultButton = button;
    }
    if (!defaultButton) {
        for (int i = 0; i < actionLayout->count(); ++i) {
            if (auto *button = qobject_cast<QPushButton *>(actionLayout->itemAt(i)->widget())) {
                defaultButton = button;
                break;
            }
        }
    }
    if (stacked) {
        layout->addLayout(actionLayout);
    } else {
        auto *buttonRow = new QHBoxLayout();
        buttonRow->addStretch(1);
        while (QLayoutItem *item = actionLayout->takeAt(0)) {
            buttonRow->addWidget(item->widget());
            delete item;
        }
        layout->addLayout(buttonRow);
    }
    if (defaultButton) {
        defaultButton->setDefault(true);
        defaultButton->setFocus();
    }

    cardLayout->addWidget(content);
    const int minWidth = style == DialogStyle::Sheet ? 500
        : (style == DialogStyle::Accent ? 440 : 400);
    setMinimumWidth(minWidth);
    adjustSize();
}

QPushButton *ModernDialog::createActionButton(const DialogAction &action, bool stacked)
{
    auto *button = new QPushButton(this);
    button->setObjectName(action.id);
    button->setCursor(Qt::PointingHandCursor);
    button->setIcon(QIcon());
    button->setIconSize(QSize(0, 0));
    QString text = action.text;
    if (stacked && !action.description.isEmpty())
        text += QLatin1Char('\n') + action.description;
    button->setText(text);
    button->setMinimumHeight(stacked ? (action.description.isEmpty() ? 38 : 62) : 38);
    if (m_style == DialogStyle::Sheet) {
        QString color = QStringLiteral("#334155");
        QString weight = QStringLiteral("500");
        if (action.role == DialogActionRole::Primary) {
            color = QStringLiteral("#2168F5");
            weight = QStringLiteral("600");
        } else if (action.role == DialogActionRole::Destructive) {
            color = QStringLiteral("#DC2626");
            weight = QStringLiteral("600");
        } else if (action.role == DialogActionRole::Cancel) {
            color = QStringLiteral("#64748B");
        }
        button->setStyleSheet(QStringLiteral(
            "QPushButton { background:transparent; color:%1; border:0; "
            "border-bottom:1px solid #E2E8F0; text-align:left; padding:12px 6px; "
            "font-weight:%2; }"
            "QPushButton:hover { background:#F8FAFC; }").arg(color, weight));
    } else if (stacked) {
        button->setStyleSheet(roleStyle(action.role, true, m_style)
                              + QStringLiteral(" QPushButton { text-align:left; }"));
    } else {
        button->setStyleSheet(roleStyle(action.role, false, m_style));
    }
    connect(button, &QPushButton::clicked, this, [this, action]() {
        m_selectedId = action.id;
        if (action.role == DialogActionRole::Cancel)
            reject();
        else
            accept();
    });
    return button;
}

QString ModernDialog::selectedId() const
{
    return m_selectedId;
}

bool ModernDialog::extraChecked() const
{
    return m_extraCheck && m_extraCheck->isChecked();
}

DialogStyle ModernDialog::currentStyle()
{
    QSettings settings;
    const int value = settings.value(QStringLiteral("ui/dialogStyle"), 0).toInt();
    if (value == 1)
        return DialogStyle::Accent;
    if (value == 2)
        return DialogStyle::Sheet;
    return DialogStyle::Card;
}

void ModernDialog::setCurrentStyle(DialogStyle style)
{
    QSettings settings;
    settings.setValue(QStringLiteral("ui/dialogStyle"), static_cast<int>(style));
}

QString ModernDialog::styleName(DialogStyle style)
{
    switch (style) {
    case DialogStyle::Accent: return QStringLiteral("顶部强调");
    case DialogStyle::Sheet: return QStringLiteral("操作清单");
    case DialogStyle::Card:
    default: return QStringLiteral("浮层卡片");
    }
}

void ModernDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    adjustSize();
    centerOnParent();
}

void ModernDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}

void ModernDialog::centerOnParent()
{
    QWidget *reference = parentWidget() ? parentWidget()->window() : nullptr;
    QRect area;
    if (reference)
        area = reference->geometry();
    else if (QScreen *screen = QApplication::primaryScreen())
        area = screen->availableGeometry();
    if (area.isValid())
        move(area.center() - rect().center());
}
