#include "startpage.h"

#include <QApplication>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "icons.h"

// A small upper-case section heading, left-aligned with its content.
static QLabel *sectionLabel(const QString &text)
{
    auto *l = new QLabel(text.toUpper());
    l->setObjectName("StartSection");
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    l->setFont(f);
    return l;
}

StartPage::StartPage(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->addStretch(2);

    m_logo = new QLabel;
    m_logo->setAlignment(Qt::AlignCenter);
    outer->addWidget(m_logo);
    outer->addSpacing(14);

    auto *title = new QLabel(QStringLiteral("sshLX"));
    title->setObjectName("StartTitle");
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.9);
    tf.setWeight(QFont::DemiBold);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    outer->addWidget(title);
    outer->addSpacing(2);

    auto *sub = new QLabel(tr("SSH, Telnet, Rlogin, SUPDUP, Raw and serial sessions "
                              "on the PuTTY core"));
    sub->setObjectName("StartMuted");
    sub->setAlignment(Qt::AlignCenter);
    sub->setWordWrap(true);
    outer->addWidget(sub);
    outer->addSpacing(36);

    // One column, as wide as the two cards: section heading, cards, then
    // the shortcut list, all left-aligned to the same edge.
    auto *column = new QVBoxLayout;
    column->setSpacing(0);
    column->addWidget(sectionLabel(tr("Get started")));
    column->addSpacing(10);

    // Action cards: monochrome icon, title, one-line description.
    QList<QPushButton *> cards;
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(12);
    auto card = [&](const QString &icon, const QString &text, const QString &hint,
                    void (StartPage::*sig)(), int row, int col) {
        auto *b = new QPushButton;
        b->setObjectName("StartAction");
        b->setCursor(Qt::PointingHandCursor);
        b->setAccessibleName(text);
        b->setAccessibleDescription(hint);
        auto *h = new QHBoxLayout(b);
        h->setContentsMargins(16, 12, 16, 12);
        h->setSpacing(14);
        auto *tile = new QLabel;
        tile->setObjectName("StartActionIcon");
        tile->setFixedSize(36, 36);
        tile->setAlignment(Qt::AlignCenter);
        m_icons.append({tile, icon});
        h->addWidget(tile);
        auto *texts = new QVBoxLayout;
        texts->setSpacing(2);
        auto *t = new QLabel(text);
        t->setObjectName("StartActionTitle");
        auto *d = new QLabel(hint);
        d->setObjectName("StartMuted");
        texts->addStretch(1);
        texts->addWidget(t);
        texts->addWidget(d);
        texts->addStretch(1);
        h->addLayout(texts, 1);
        for (QWidget *w : {static_cast<QWidget *>(tile), static_cast<QWidget *>(t),
                           static_cast<QWidget *>(d)})
            w->setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(b, &QPushButton::clicked, this, sig);
        grid->addWidget(b, row, col);
        cards << b;
    };
    card("start-new", tr("New connection"), tr("Add a connection to the tree"),
         &StartPage::newConnection, 0, 0);
    card("start-quick", tr("Quick connect"), tr("PuTTY's dialog, without saving"),
         &StartPage::quickConnect, 0, 1);
    card("start-import", tr("Import PuTTY sessions"), tr("Bring saved sessions into the tree"),
         &StartPage::importSessions, 1, 0);
    card("start-options", tr("Options"), tr("Theme, shortcuts, serial, passwords"),
         &StartPage::options, 1, 1);
    // A button's size hint ignores the layout inside it: size the cards to
    // the widest content, all the same width.
    int width = 280;
    for (QPushButton *b : cards)
        width = qMax(width, b->layout()->sizeHint().width());
    for (QPushButton *b : cards)
        b->setMinimumWidth(width);
    column->addLayout(grid);
    column->addSpacing(32);

    column->addWidget(sectionLabel(tr("Recent connections")));
    column->addSpacing(8);
    m_recent = new QVBoxLayout;
    m_recent->setSpacing(2);
    column->addLayout(m_recent);
    setRecent({});

    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *columnBox = new QWidget;
    columnBox->setLayout(column);
    columnBox->setFixedWidth(2 * width + grid->horizontalSpacing());
    column->setContentsMargins(0, 0, 0, 0);
    row->addWidget(columnBox);
    row->addStretch(1);
    outer->addLayout(row);
    outer->addStretch(3);

    refreshIcons();
}

void StartPage::refreshIcons()
{
    m_logo->setPixmap(QApplication::windowIcon().pixmap(72, 72));
    for (const auto &[label, name] : m_icons)
        label->setPixmap(Icons::get(name).pixmap(20, 20));
}

void StartPage::setRecent(const QList<Recent> &recent)
{
    while (QLayoutItem *it = m_recent->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    if (recent.isEmpty()) {
        auto *l = new QLabel(tr("Connections you open appear here. Double-click one in "
                                "the tree to start."));
        l->setObjectName("StartMuted");
        l->setWordWrap(true);
        m_recent->addWidget(l);
        return;
    }
    for (const Recent &r : recent) {
        // One flat row: icon, name, host on the right.
        auto *b = new QPushButton;
        b->setObjectName("StartRecent");
        b->setCursor(Qt::PointingHandCursor);
        b->setAccessibleName(r.name);
        b->setToolTip(tr("Open %1").arg(r.name));
        auto *h = new QHBoxLayout(b);
        h->setContentsMargins(10, 6, 10, 6);
        h->setSpacing(10);
        auto *icon = new QLabel;
        icon->setPixmap(r.icon.pixmap(16, 16));
        auto *name = new QLabel(r.name);
        name->setObjectName("StartRecentName");
        auto *detail = new QLabel(r.detail);
        detail->setObjectName("StartMuted");
        h->addWidget(icon);
        h->addWidget(name);
        h->addStretch(1);
        h->addWidget(detail);
        for (QWidget *w : {static_cast<QWidget *>(icon), static_cast<QWidget *>(name),
                           static_cast<QWidget *>(detail)})
            w->setAttribute(Qt::WA_TransparentForMouseEvents);
        const QString id = r.id;
        connect(b, &QPushButton::clicked, this, [this, id] { emit openRecent(id); });
        m_recent->addWidget(b);
    }
}

void StartPage::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::PaletteChange)
        refreshIcons();   // line icons are drawn in the palette's text colour
    QWidget::changeEvent(e);
}
