#include "dialogkit.h"

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace DialogKit {

static void scaleFont(QWidget *w, double factor, bool bold)
{
    QFont f = w->font();
    f.setPointSizeF(f.pointSizeF() * factor);
    f.setBold(bold);
    w->setFont(f);
}

QWidget *header(const QString &title, const QString &subtitle)
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 4);
    v->setSpacing(2);
    auto *t = new QLabel(title);
    t->setObjectName("DialogTitle");
    scaleFont(t, 1.35, true);
    v->addWidget(t);
    if (!subtitle.isEmpty())
        v->addWidget(hint(subtitle));
    return w;
}

QFrame *card(const QString &title, QLayout *content)
{
    auto *f = new QFrame;
    f->setObjectName("Card");
    auto *v = new QVBoxLayout(f);
    v->setContentsMargins(16, 12, 16, 14);
    v->setSpacing(10);
    if (!title.isEmpty()) {
        auto *t = new QLabel(title);
        t->setObjectName("CardTitle");
        scaleFont(t, 1.0, true);
        v->addWidget(t);
    }
    content->setContentsMargins(0, 0, 0, 0);
    v->addLayout(content);
    return f;
}

QLabel *hint(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("Hint");
    l->setWordWrap(true);
    l->setTextFormat(Qt::AutoText);
    return l;
}

QWidget *withHint(QWidget *control, const QString &h)
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    v->addWidget(control);
    QLabel *l = hint(h);
    l->setContentsMargins(26, 0, 0, 0);   // under the checkbox text
    v->addWidget(l);
    return w;
}

static QPushButton *button(const QString &text, const char *role)
{
    auto *b = new QPushButton(text);
    b->setProperty("role", role);
    b->setAutoDefault(false);
    b->setMinimumWidth(96);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QPushButton *primaryButton(const QString &text)
{
    QPushButton *b = button(text, "primary");
    b->setDefault(true);
    return b;
}

QPushButton *secondaryButton(const QString &text)
{
    return button(text, "secondary");
}

QPushButton *dangerButton(const QString &text)
{
    // No red buttons: destructive actions use the normal primary look and
    // are made safe by defaulting to the cancel choice instead.
    return primaryButton(text);
}

QWidget *footer(const QList<QPushButton *> &buttons, QWidget *leftWidget)
{
    auto *w = new QWidget;
    w->setObjectName("DialogFooter");
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 12, 0, 0);
    h->setSpacing(8);
    if (leftWidget)
        h->addWidget(leftWidget);
    h->addStretch(1);
    for (QPushButton *b : buttons)
        h->addWidget(b);
    return w;
}

void polish(QDialogButtonBox *box)
{
    for (QAbstractButton *b : box->buttons()) {
        b->setIcon(QIcon());
        auto *pb = qobject_cast<QPushButton *>(b);
        if (!pb)
            continue;
        pb->setMinimumWidth(96);
        pb->setCursor(Qt::PointingHandCursor);
        QDialogButtonBox::ButtonRole r = box->buttonRole(b);
        pb->setProperty("role", (r == QDialogButtonBox::AcceptRole ||
                                 r == QDialogButtonBox::YesRole) ? "primary" : "secondary");
    }
}

void frame(QVBoxLayout *layout)
{
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(14);
}

// ---- message dialogs -----------------------------------------------------------
int message(QWidget *parent, Kind kind, const QString &title, const QString &text,
            const QStringList &buttons, int defaultIndex, const QString &details,
            bool dangerPrimary)
{
    QDialog d(parent);
    d.setWindowTitle(title);
    d.setModal(true);
    d.setMinimumWidth(440);
    auto *v = new QVBoxLayout(&d);
    frame(v);

    auto *row = new QHBoxLayout;
    row->setSpacing(16);
    auto *icon = new QLabel;
    const char *name = kind == Kind::Warning ? "msg-warning" : kind == Kind::Error ? "msg-error"
                     : kind == Kind::Question ? "msg-question" : "msg-info";
    icon->setPixmap(QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(name)))
                        .pixmap(40, 40, QIcon::Normal));
    icon->setAlignment(Qt::AlignTop);
    row->addWidget(icon, 0, Qt::AlignTop);
    auto *col = new QVBoxLayout;
    col->setSpacing(6);
    auto *t = new QLabel(title);
    scaleFont(t, 1.15, true);
    t->setWordWrap(true);
    col->addWidget(t);
    auto *body = new QLabel(text);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->setMinimumWidth(320);
    col->addWidget(body);
    row->addLayout(col, 1);
    v->addLayout(row);

    QPlainTextEdit *det = nullptr;
    QToolButton *toggle = nullptr;
    if (!details.isEmpty()) {
        det = new QPlainTextEdit(details);
        det->setReadOnly(true);
        det->setMinimumHeight(180);
        det->setVisible(false);
        v->addWidget(det);
        toggle = new QToolButton;
        toggle->setText(QObject::tr("Show details"));
        toggle->setCheckable(true);
        QObject::connect(toggle, &QToolButton::toggled, &d, [det, toggle, &d](bool on) {
            det->setVisible(on);
            toggle->setText(on ? QObject::tr("Hide details") : QObject::tr("Show details"));
            d.adjustSize();
        });
    }

    int chosen = -1;
    QList<QPushButton *> btns;
    for (int i = 0; i < buttons.size(); i++) {
        QPushButton *b = i == defaultIndex
            ? (dangerPrimary ? dangerButton(buttons[i]) : primaryButton(buttons[i]))
            : secondaryButton(buttons[i]);
        QObject::connect(b, &QPushButton::clicked, &d, [&d, &chosen, i] {
            chosen = i;
            d.accept();
        });
        btns << b;
    }
    // Primary on the right, as GNOME/KDE do.
    if (defaultIndex >= 0 && defaultIndex < btns.size())
        btns.move(defaultIndex, btns.size() - 1);
    v->addWidget(footer(btns, toggle));
    d.exec();
    return chosen;
}

void info(QWidget *parent, const QString &title, const QString &text)
{
    message(parent, Kind::Info, title, text, {QObject::tr("OK")});
}

void warning(QWidget *parent, const QString &title, const QString &text)
{
    message(parent, Kind::Warning, title, text, {QObject::tr("OK")});
}

void error(QWidget *parent, const QString &title, const QString &text)
{
    message(parent, Kind::Error, title, text, {QObject::tr("OK")});
}

bool confirm(QWidget *parent, const QString &title, const QString &text,
             const QString &yes, const QString &no, bool defaultYes, bool destructive)
{
    const QString noText = no.isEmpty() ? QObject::tr("Cancel") : no;
    // Buttons: [no, yes]; the default is the safe choice unless asked.
    int r = message(parent, Kind::Question, title, text, {noText, yes},
                    defaultYes ? 1 : 0, QString(), destructive && defaultYes);
    return r == 1;
}

void notify(QWidget *parent, Kind kind, const QString &title, const QString &text)
{
    // Same look, but non-modal: built on the heap and shown.
    auto *d = new QDialog(parent);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(title);
    auto *v = new QVBoxLayout(d);
    frame(v);
    auto *row = new QHBoxLayout;
    row->setSpacing(16);
    auto *icon = new QLabel;
    icon->setPixmap(QIcon(kind == Kind::Error ? ":/icons/msg-error.svg"
                          : kind == Kind::Warning ? ":/icons/msg-warning.svg"
                                                  : ":/icons/msg-info.svg").pixmap(40, 40));
    row->addWidget(icon, 0, Qt::AlignTop);
    auto *body = new QLabel(text);
    body->setWordWrap(true);
    body->setMinimumWidth(320);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(body, 1);
    v->addLayout(row);
    QPushButton *ok = primaryButton(QObject::tr("OK"));
    QObject::connect(ok, &QPushButton::clicked, d, &QDialog::close);
    v->addWidget(footer({ok}));
    d->show();
}

} // namespace DialogKit
