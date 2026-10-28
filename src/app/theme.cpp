#include "theme.h"

#include <QApplication>
#include <QHash>
#include <QPalette>
#include <QSettings>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include "icons.h"

namespace Theme {

namespace {
// Fusion, minus the stock icons the desktop theme would put on dialog and
// message-box buttons (sshLX's buttons are styled, not decorated).
class AppStyle : public QProxyStyle {
public:
    AppStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}
    int styleHint(StyleHint hint, const QStyleOption *opt, const QWidget *w,
                  QStyleHintReturn *ret) const override
    {
        if (hint == SH_DialogButtonBox_ButtonsHaveIcons)
            return 0;
        return QProxyStyle::styleHint(hint, opt, w, ret);
    }
    QIcon standardIcon(StandardPixmap sp, const QStyleOption *opt, const QWidget *w) const override
    {
        switch (sp) {
        case SP_DialogOkButton: case SP_DialogCancelButton: case SP_DialogApplyButton:
        case SP_DialogCloseButton: case SP_DialogSaveButton: case SP_DialogDiscardButton:
        case SP_DialogYesButton: case SP_DialogNoButton: case SP_DialogResetButton:
        case SP_DialogHelpButton: case SP_DialogOpenButton:
            return QIcon();
        case SP_LineEditClearButton:
            // The desktop's red "edit-clear" clashes; use the app's line icon.
            return Icons::get(QStringLiteral("close"));
        default:
            return QProxyStyle::standardIcon(sp, opt, w);
        }
    }
};

struct Colors {
    QString window, base, altBase, menu, button, buttonHover, border, borderStrong,
        text, muted, disabled, hover, hoverSoft, selection, selectedText, accent,
        scroll, scrollHover, tabActive, tooltip;
    QString card, navBg, accentHover, accentPressed;
    QString tabBar;   // dock title bars, behind the tabs
};

const Colors &dark()
{
    static const Colors c{
        "#2b2d30", "#1f2023", "#25272a", "#2f3134", "#36383c", "#3e4146",
        "#3d4045", "#55595f", "#dfe1e5", "#9a9ea6", "#6c7078", "#3a3d42",
        "#303236", "#2f4a77", "#ffffff", "#4a8af4", "#4a4d53", "#5d6168",
        "#1f2023", "#2f3134",
        "#303236", "#26282b", "#5b97f6", "#3b74d9",
        "#222326"};
    return c;
}

const Colors &light()
{
    static const Colors c{
        "#f3f4f6", "#ffffff", "#f7f8fa", "#ffffff", "#ffffff", "#f0f2f5",
        "#d5d8de", "#b8bdc6", "#1f2328", "#5f6670", "#a4a9b1", "#e6e9ee",
        "#eef0f3", "#d3e3fd", "#0b1f3a", "#2f6fe4", "#c4c8cf", "#a8adb6",
        "#ffffff", "#ffffff",
        "#ffffff", "#eceef2", "#4482ec", "#255fcc",
        "#e4e7eb"};
    return c;
}

const Colors &current()
{
    return isDark() ? dark() : light();
}

QString fill(QString qss, const Colors &c)
{
    const QHash<QString, QString> map{
        {"window", c.window}, {"base", c.base}, {"altbase", c.altBase},
        {"menu", c.menu}, {"button", c.button}, {"buttonhover", c.buttonHover},
        {"border", c.border}, {"borderstrong", c.borderStrong}, {"text", c.text},
        {"muted", c.muted}, {"disabled", c.disabled}, {"hover", c.hover},
        {"hoversoft", c.hoverSoft}, {"selection", c.selection},
        {"selectedtext", c.selectedText}, {"accent", c.accent}, {"scroll", c.scroll},
        {"scrollhover", c.scrollHover}, {"tabactive", c.tabActive}, {"tooltip", c.tooltip},
        {"card", c.card}, {"navbg", c.navBg}, {"accenthover", c.accentHover},
        {"accentpressed", c.accentPressed}, {"tabbar", c.tabBar}};
    for (auto it = map.begin(); it != map.end(); ++it)
        qss.replace('%' + it.key() + '%', it.value());
    return qss;
}

QString iconUrl(const QString &name)
{
    return QStringLiteral("url(%1)").arg(Icons::path(name));
}

const char *kAppQss = R"(
QMenuBar { background: %window%; padding: 3px 6px; border-bottom: 1px solid %border%; }
QMenuBar::item { padding: 5px 10px; margin: 0 1px; border-radius: 6px; background: transparent; color: %text%; }
QMenuBar::item:selected { background: %hover%; }
QMenuBar::item:pressed { background: %hover%; }
QMenu { background: %menu%; border: 1px solid %border%; border-radius: 8px; padding: 6px 0; }
QMenu::item { padding: 6px 36px 6px 12px; margin: 1px 6px; border-radius: 6px; color: %text%; }
QMenu::item:selected { background: %hover%; }
QMenu::item:disabled { color: %disabled%; }
QMenu::icon { left: 10px; }
QMenu::separator { height: 1px; background: %border%; margin: 5px 12px; }
QMenu::indicator { width: 14px; height: 14px; left: 8px; }

QToolBar { background: %window%; border: none; padding: 4px 6px; spacing: 3px; }
QToolBar::separator { width: 1px; background: %border%; margin: 5px 6px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 4px; color: %text%; }
QToolButton:hover { background: %hover%; }
QToolButton:pressed { background: %selection%; }
QToolButton:checked { background: %selection%; border-color: %accent%; }
QToolButton:disabled { color: %disabled%; }

QPushButton { background: %button%; color: %text%; border: 1px solid %border%; border-radius: 6px; padding: 6px 16px; min-height: 20px; }
QPushButton:hover { background: %buttonhover%; border-color: %borderstrong%; }
QPushButton:pressed { background: %selection%; }
QPushButton:default { border-color: %accent%; }
QPushButton:disabled { color: %disabled%; background: %window%; }
QPushButton:flat { background: transparent; border: none; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit, QTextBrowser, QKeySequenceEdit {
    background: %base%; color: %text%; border: 1px solid %border%; border-radius: 6px;
    padding: 5px 8px; selection-background-color: %accent%; selection-color: #ffffff; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: %accent%; }
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: %disabled%; }
/* In-place editors (rename in the tree, property grid cells) sit in a row
   only a line tall: drop the form padding so the text is not clipped. */
#qt_scrollarea_viewport > QLineEdit, #qt_scrollarea_viewport > QWidget > QLineEdit,
#qt_scrollarea_viewport > QComboBox, #qt_scrollarea_viewport > QSpinBox {
    padding: 0 4px; margin: 0; border-radius: 4px; border-color: %accent%; }
#qt_scrollarea_viewport > QComboBox::drop-down { width: 18px; }
#qt_scrollarea_viewport > QSpinBox::up-button, #qt_scrollarea_viewport > QSpinBox::down-button { width: 14px; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: %chevron%; width: 14px; height: 14px; }
QComboBox QAbstractItemView { background: %menu%; border: 1px solid %border%; border-radius: 6px; padding: 4px; outline: 0; selection-background-color: %hover%; selection-color: %text%; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { border: none; width: 18px; }

QTreeView, QListView, QTableView, QTreeWidget, QListWidget, QTableWidget {
    background: %base%; alternate-background-color: %altbase%; color: %text%;
    border: 1px solid %border%; border-radius: 6px; outline: 0; }
QTreeView::item, QListView::item { padding: 4px 2px; }
QTreeView::item:hover, QListView::item:hover { background: %hoversoft%; }
QTreeView::item:selected, QListView::item:selected, QTableView::item:selected { background: %selection%; color: %selectedtext%; }
QTreeView::branch:selected { background: %selection%; }
QHeaderView { background: %window%; border: none; }
QHeaderView::section { background: %window%; color: %muted%; padding: 6px 8px; border: none;
    border-bottom: 1px solid %border%; border-right: 1px solid %border%; }
QHeaderView::section:last { border-right: none; }
QTableCornerButton::section { background: %window%; border: none; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 2px 2px 2px 0; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0 2px 2px 2px; }
QScrollBar::handle { background: %scroll%; border-radius: 4px; }
QScrollBar::handle:vertical { min-height: 32px; margin: 0 2px; }
QScrollBar::handle:horizontal { min-width: 32px; margin: 2px 0; }
QScrollBar::handle:hover { background: %scrollhover%; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QTabWidget::pane { border: 1px solid %border%; border-radius: 8px; top: -1px; background: %window%; }
QTabBar::tab { padding: 7px 16px; color: %muted%; background: transparent; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: %text%; border-bottom-color: %accent%; }
QTabBar::tab:hover { color: %text%; }

QGroupBox { border: 1px solid %border%; border-radius: 8px; margin-top: 16px; padding: 12px 10px 10px 10px; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; color: %muted%; }
QCheckBox, QRadioButton { spacing: 8px; color: %text%; }
QToolTip { background: %tooltip%; color: %text%; border: 1px solid %border%; border-radius: 6px; padding: 5px 8px; }
QStatusBar { background: %window%; color: %muted%; border-top: 1px solid %border%; }
QStatusBar::item { border: none; }
QSplitter::handle { background: %border%; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QLabel { color: %text%; }
QLabel:disabled { color: %disabled%; }

QDialog { background: %window%; }
QPushButton[role="primary"] { background: %accent%; color: #ffffff; border: 1px solid %accent%; font-weight: 600; }
QPushButton[role="primary"]:hover { background: %accenthover%; border-color: %accenthover%; }
QPushButton[role="primary"]:pressed { background: %accentpressed%; }
QPushButton[role="primary"]:disabled { background: %button%; color: %disabled%; border-color: %border%; }
QPushButton[role="secondary"] { background: %button%; }
QFrame#Card { background: %card%; border: 1px solid %border%; border-radius: 10px; }
QFrame#Card QLabel#CardTitle { color: %text%; }
QLabel#Hint { color: %muted%; }
QLabel#DialogTitle { color: %text%; }
QWidget#DialogFooter { border-top: 1px solid %border%; }
QListWidget#Nav, QTreeWidget#Nav { background: transparent; border: none; padding: 0; }
QListWidget#Nav::item, QTreeWidget#Nav::item { padding: 7px 10px; border-radius: 7px; margin: 1px 0; color: %text%; }
QListWidget#Nav::item:selected, QTreeWidget#Nav::item:selected { background: %selection%; color: %selectedtext%; }
QListWidget#Nav::item:hover:!selected, QTreeWidget#Nav::item:hover:!selected { background: %hoversoft%; }
QWidget#NavPane { background: %navbg%; border-right: 1px solid %border%; }
QScrollArea#PageScroll, QScrollArea#PageScroll > QWidget > QWidget { background: transparent; }

QCheckBox::indicator, QTreeView::indicator, QTableView::indicator, QListView::indicator {
    width: 16px; height: 16px; border: 1px solid %borderstrong%; border-radius: 4px; background: %base%; }
QCheckBox::indicator:hover, QTreeView::indicator:hover { border-color: %accent%; }
QCheckBox::indicator:checked, QTreeView::indicator:checked, QTableView::indicator:checked, QListView::indicator:checked {
    background: %accent%; border-color: %accent%; image: %check%; }
QCheckBox::indicator:disabled { background: %window%; border-color: %border%; }
QRadioButton::indicator { width: 16px; height: 16px; border: 1px solid %borderstrong%; border-radius: 8px; background: %base%; }
QRadioButton::indicator:hover { border-color: %accent%; }
QRadioButton::indicator:checked { background: %accent%; border-color: %accent%; image: %radiodot%; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: %chevronup%; width: 12px; height: 12px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: %chevron%; width: 12px; height: 12px; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { background: %hover%; border-radius: 4px; }

QFrame#PromptBar, QFrame#DeviceBanner, QFrame#ReconnectBar {
    background: %menu%; border: 1px solid %border%; border-radius: 8px; margin: 4px; }
)";

const char *kDockQss = R"(
ads--CDockContainerWidget { background: %window%; }
ads--CDockContainerWidget > QSplitter { padding: 0; }
ads--CDockContainerWidget ads--CDockSplitter::handle { background: %border%; }
ads--CDockAreaWidget { background: %window%; }
ads--CDockAreaTitleBar { background: %tabbar%; border: none; min-height: 36px; padding-left: 4px; }

/* Tabs: rounded on top, on a darker bar; the current one takes the
   content's colour so it reads as part of it, and the focused one also
   gets an accent cap. */
ads--CDockWidgetTab { background: transparent; border: none; border-top: 2px solid transparent;
    border-top-left-radius: 7px; border-top-right-radius: 7px;
    margin: 5px 1px 0 1px; padding: 0 2px 0 10px; min-height: 28px; }
ads--CDockWidgetTab:hover { background: %hoversoft%; }
ads--CDockWidgetTab QLabel { color: %muted%; padding: 0 4px 0 2px; }
ads--CDockWidgetTab:hover QLabel { color: %text%; }
ads--CDockWidgetTab[activeTab="true"] { background: %window%; }
ads--CDockWidgetTab[activeTab="true"] QLabel { color: %text%; }
ads--CDockWidgetTab[focused="true"] { background: %window%; border-top: 2px solid %accent%; }
ads--CDockWidgetTab[focused="true"] QLabel { color: %text%; }

#tabCloseButton { background: none; border: none; border-radius: 5px; margin: 0 2px; padding: 3px;
    qproperty-icon: %close%; qproperty-iconSize: 12px; }
#tabCloseButton:hover { background: %hover%; }
#tabCloseButton:pressed { background: %selection%; }

ads--CTitleBarButton { background: transparent; border: none; border-radius: 5px; padding: 4px; margin: 0 1px; }
ads--CTitleBarButton:hover { background: %hover%; }
ads--CTitleBarButton:pressed { background: %selection%; }
#tabsMenuButton { qproperty-icon: %chevron%; qproperty-iconSize: 16px; }
#tabsMenuButton::menu-indicator { image: none; }
#detachGroupButton { qproperty-icon: %detach%; qproperty-iconSize: 15px; }
#dockAreaCloseButton { qproperty-icon: %close%; qproperty-iconSize: 15px; }

ads--CDockWidget { background: %window%; border: none; }
QScrollArea#dockWidgetScrollArea { padding: 0; border: none; }

ads--CFloatingWidgetTitleBar { background: %window%; border-bottom: 1px solid %border%;
    qproperty-maximizeIcon: %maximize%; qproperty-normalIcon: %restore%; }
#floatingTitleLabel { color: %text%; }
#floatingTitleCloseButton, #floatingTitleMaximizeButton { qproperty-iconSize: 15px; border: none; margin: 3px; padding: 3px; border-radius: 5px; }
#floatingTitleCloseButton { qproperty-icon: %close%; }
#floatingTitleCloseButton:hover, #floatingTitleMaximizeButton:hover { background: %hover%; }
)";

QPalette makePalette(const Colors &c, bool darkMode)
{
    QPalette p;
    auto set = [&](QPalette::ColorRole r, const QString &v) {
        p.setColor(QPalette::All, r, QColor(v));
    };
    set(QPalette::Window, c.window);
    set(QPalette::WindowText, c.text);
    set(QPalette::Base, c.base);
    set(QPalette::AlternateBase, c.altBase);
    set(QPalette::ToolTipBase, c.tooltip);
    set(QPalette::ToolTipText, c.text);
    set(QPalette::PlaceholderText, c.muted);
    set(QPalette::Text, c.text);
    set(QPalette::Button, c.button);
    set(QPalette::ButtonText, c.text);
    set(QPalette::BrightText, darkMode ? "#ff7b72" : "#cf222e");
    set(QPalette::Highlight, c.selection);
    set(QPalette::HighlightedText, c.selectedText);
    set(QPalette::Link, c.accent);
    set(QPalette::LinkVisited, c.accent);
    set(QPalette::Accent, c.accent);
    // Bevel roles derived from the window colour (used by Fusion and ADS).
    const QColor w(c.window);
    p.setColor(QPalette::All, QPalette::Light, darkMode ? w.lighter(130) : w.lighter(110));
    p.setColor(QPalette::All, QPalette::Midlight, darkMode ? w.lighter(115) : w.darker(103));
    p.setColor(QPalette::All, QPalette::Mid, darkMode ? w.darker(120) : w.darker(115));
    p.setColor(QPalette::All, QPalette::Dark, darkMode ? w.darker(150) : w.darker(135));
    p.setColor(QPalette::All, QPalette::Shadow, darkMode ? w.darker(250) : w.darker(200));
    for (QPalette::ColorRole r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, r, QColor(c.disabled));
    return p;
}
} // namespace

Mode mode()
{
    return Mode(QSettings().value("ui/theme", int(System)).toInt());
}

void setMode(Mode m)
{
    QSettings().setValue("ui/theme", int(m));
}

bool isDark()
{
    switch (mode()) {
    case Dark: return true;
    case Light: return false;
    default: break;
    }
    if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
        return true;
    if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light)
        return false;
    // Unknown: judge by the platform palette's window colour.
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

void apply()
{
    static bool styleSet = false;
    if (!styleSet) {
        QApplication::setStyle(new AppStyle);
        styleSet = true;
    }
    const bool d = isDark();
    const Colors &c = current();
    QApplication::setPalette(makePalette(c, d));
    Icons::clearCache();
    QString qss = fill(QString::fromLatin1(kAppQss), c);
    qss.replace("%chevron%", iconUrl("chevron-down"));
    qss.replace("%chevronup%", iconUrl("chevron-up"));
    qss.replace("%check%", QStringLiteral("url(:/icons/check-white.svg)"));
    qss.replace("%radiodot%", QStringLiteral("url(:/icons/radio-dot.svg)"));
    qApp->setStyleSheet(qss);
}

QString dockStyleSheet()
{
    QString qss = fill(QString::fromLatin1(kDockQss), current());
    qss.replace("%close%", iconUrl("close"));
    qss.replace("%chevron%", iconUrl("chevron-down"));
    qss.replace("%detach%", iconUrl("detach"));
    qss.replace("%maximize%", iconUrl("maximize"));
    qss.replace("%restore%", iconUrl("restore"));
    return qss;
}

QColor accent()
{
    return QColor(current().accent);
}

QColor muted()
{
    return QColor(current().muted);
}

} // namespace Theme
