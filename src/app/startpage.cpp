#include "startpage.h"

#include <QApplication>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "icons.h"
#include "shortcuts.h"

StartPage::StartPage(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->addStretch(2);

    m_logo = new QLabel;
    m_logo->setAlignment(Qt::AlignCenter);
    outer->addWidget(m_logo);
    outer->addSpacing(10);

    auto *title = new QLabel(QStringLiteral("sshLX"));
    title->setObjectName("StartTitle");
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 2.2);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    outer->addWidget(title);

    auto *sub = new QLabel(tr("SSH, Telnet, Rlogin, SUPDUP, Raw and serial sessions "
                              "on the PuTTY core"));
    sub->setObjectName("StartMuted");
    sub->setAlignment(Qt::AlignCenter);
    sub->setWordWrap(true);
    outer->addWidget(sub);
    outer->addSpacing(28);

    // Action cards: icon tile, title, one-line description.
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
        h->setContentsMargins(14, 10, 16, 10);
        h->setSpacing(12);
        auto *tile = new QLabel;
        tile->setObjectName("StartActionIcon");
        tile->setFixedSize(38, 38);
        tile->setAlignment(Qt::AlignCenter);
        m_icons.append({tile, icon});
        h->addWidget(tile);
        auto *texts = new QVBoxLayout;
        texts->setSpacing(1);
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
    card("connection-new", tr("New connection"), tr("Add a connection to the tree"),
         &StartPage::newConnection, 0, 0);
    card("quick-connect", tr("Quick connect"), tr("PuTTY's dialog, without saving"),
         &StartPage::quickConnect, 0, 1);
    card("import", tr("Import PuTTY sessions"), tr("Bring saved sessions into the tree"),
         &StartPage::importSessions, 1, 0);
    card("settings", tr("Options"), tr("Theme, shortcuts, serial, passwords"),
         &StartPage::options, 1, 1);
    // A button's size hint ignores the layout inside it: size the cards to
    // the widest content, all the same width.
    int width = 280;
    for (QPushButton *b : cards)
        width = qMax(width, b->layout()->sizeHint().width());
    for (QPushButton *b : cards)
        b->setMinimumWidth(width);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addLayout(grid);
    row->addStretch(1);
    outer->addLayout(row);
    outer->addSpacing(28);

    auto *keysRow = new QHBoxLayout;
    keysRow->addStretch(1);
    m_keys = new QHBoxLayout;
    m_keys->setSpacing(4);
    keysRow->addLayout(m_keys);
    keysRow->addStretch(1);
    outer->addLayout(keysRow);
    outer->addSpacing(10);

    auto *hint = new QLabel(tr("Double-click a connection to open it  ·  "
                               "drag tabs to split the view"));
    hint->setObjectName("StartMuted");
    hint->setAlignment(Qt::AlignCenter);
    outer->addWidget(hint);
    outer->addStretch(3);

    refreshIcons();
}

void StartPage::refreshIcons()
{
    m_logo->setPixmap(QApplication::windowIcon().pixmap(88, 88));
    for (const auto &[label, name] : m_icons)
        label->setPixmap(Icons::get(name).pixmap(22, 22));
}

void StartPage::refreshShortcuts()
{
    while (QLayoutItem *it = m_keys->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    const struct { const char *id; QString label; } shown[] = {
        {"tabs.next", tr("next tab")},
        {"tabs.close", tr("close tab")},
        {"tools.sendNext", tr("send next key to the terminal")},
    };
    bool first = true;
    for (const auto &s : shown) {
        QKeySequence seq;
        for (const Shortcuts::Entry &e : Shortcuts::instance().entries())
            if (e.id == QLatin1String(s.id))
                seq = e.seq;
        if (seq.isEmpty())
            continue;   // cleared in Options: nothing to show
        if (!first)
            m_keys->addSpacing(18);
        first = false;
        // "Ctrl+Shift+W" -> [Ctrl] [Shift] [W]; a trailing "+" is the key itself.
        QString text = QKeySequence(seq[0]).toString(QKeySequence::NativeText);
        QStringList keys = text.split('+', Qt::SkipEmptyParts);
        if (text.endsWith(QLatin1String("++")))
            keys << QStringLiteral("+");
        for (const QString &k : keys) {
            auto *cap = new QLabel(k);
            cap->setObjectName("Keycap");
            m_keys->addWidget(cap);
        }
        auto *l = new QLabel(s.label);
        l->setObjectName("StartMuted");
        m_keys->addSpacing(4);
        m_keys->addWidget(l);
    }
}

void StartPage::showEvent(QShowEvent *e)
{
    // Shortcuts are registered after the page is built and can change in
    // Options; read them whenever the page comes into view.
    refreshShortcuts();
    QWidget::showEvent(e);
}

void StartPage::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::PaletteChange)
        refreshIcons();   // line icons are drawn in the palette's text colour
    QWidget::changeEvent(e);
}
