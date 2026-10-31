#include "tabcolours.h"

#include <QColorDialog>
#include <QCoreApplication>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QVariantMap>

namespace TabColours {

static QString tr(const char *s)
{
    return QCoreApplication::translate("TabColours", s);
}

QIcon swatch(const QString &colour)
{
    QPixmap px(16, 16);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(128, 128, 128));
    p.setBrush(QColor(colour));
    p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
    return QIcon(px);
}

void fillMenu(QMenu *menu, const QString &current, QWidget *dialogParent,
              const std::function<void(const QString &)> &picked)
{
    static const QList<QPair<const char *, const char *>> colours{
        {"Red", "#e5484d"},    {"Orange", "#f76b15"}, {"Yellow", "#d9a800"},
        {"Green", "#30a46c"},  {"Teal", "#12a594"},   {"Blue", "#3b82f6"},
        {"Purple", "#8e4ec6"}, {"Pink", "#d6409f"},   {"Grey", "#8b8d98"}};
    for (const auto &[name, hex] : colours) {
        QAction *a = menu->addAction(swatch(hex), tr(name), menu, [picked, h = QString(hex)] { picked(h); });
        if (current == hex) {
            QFont f = a->font();
            f.setBold(true);     // with an icon, styles draw no check mark
            a->setFont(f);
        }
    }
    menu->addSeparator();
    menu->addAction(tr("Custom..."), menu, [picked, current, dialogParent] {
        const QColor c = QColorDialog::getColor(current.isEmpty() ? QColor("#3b82f6") : QColor(current),
                                                dialogParent, tr("Colour"));
        if (c.isValid())
            picked(c.name());
    });
    QAction *none = menu->addAction(tr("No Colour"), menu, [picked] { picked(QString()); });
    none->setEnabled(!current.isEmpty());
}

QString pillStyle(const QString &colour)
{
    const QColor c(colour);
    if (!c.isValid())
        return {};
    return QStringLiteral("QToolButton { border-color: %1; }"
                          "QToolButton:checked { background: %1; border-color: %1; color: %2; }")
        .arg(c.name(), c.lightnessF() > 0.65 ? QStringLiteral("#000000") : QStringLiteral("#ffffff"));
}

static QVariantMap groups()
{
    return QSettings().value("tabGroups/colours").toMap();
}

QString groupColour(const QString &group)
{
    return group.isEmpty() ? QString() : groups().value(group).toString();
}

void setGroupColour(const QString &group, const QString &colour)
{
    QVariantMap m = groups();
    if (colour.isEmpty())
        m.remove(group);
    else
        m.insert(group, colour);
    QSettings().setValue("tabGroups/colours", m);
}

}
