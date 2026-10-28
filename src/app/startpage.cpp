#include "startpage.h"

#include <QApplication>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "icons.h"

StartPage::StartPage(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->addStretch(2);

    auto *logo = new QLabel;
    logo->setPixmap(QApplication::windowIcon().pixmap(96, 96));
    logo->setAlignment(Qt::AlignCenter);
    outer->addWidget(logo);

    auto *title = new QLabel(QStringLiteral("sshLX"));
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 2.2);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    outer->addWidget(title);

    auto *sub = new QLabel(tr("SSH, Telnet, Rlogin, SUPDUP, Raw and serial sessions "
                              "on the PuTTY core"));
    sub->setAlignment(Qt::AlignCenter);
    sub->setForegroundRole(QPalette::PlaceholderText);
    outer->addWidget(sub);
    outer->addSpacing(24);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(12);
    auto button = [&](const QString &icon, const QString &text, const QString &hint,
                      void (StartPage::*sig)(), int row, int col) {
        auto *b = new QPushButton(Icons::get(icon), text);
        b->setIconSize(QSize(24, 24));
        b->setMinimumSize(220, 44);
        b->setToolTip(hint);
        connect(b, &QPushButton::clicked, this, sig);
        grid->addWidget(b, row, col);
    };
    button("connection-new", tr("New connection"),
           tr("Create a connection in the tree"), &StartPage::newConnection, 0, 0);
    button("quick-connect", tr("Quick connect..."),
           tr("PuTTY's configuration dialog, without saving"), &StartPage::quickConnect, 0, 1);
    button("import", tr("Import PuTTY sessions"),
           tr("Add saved PuTTY sessions to the tree"), &StartPage::importSessions, 1, 0);
    button("settings", tr("Options..."), tr("Shortcuts, serial, passwords"),
           &StartPage::options, 1, 1);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addLayout(grid);
    row->addStretch(1);
    outer->addLayout(row);
    outer->addSpacing(24);

    auto *hints = new QLabel(tr(
        "Double-click a connection to open it &nbsp;·&nbsp; drag tabs to split the view<br>"
        "<b>Ctrl+Tab</b> next tab &nbsp;·&nbsp; <b>Ctrl+Shift+W</b> close tab "
        "&nbsp;·&nbsp; <b>Ctrl+Shift+F12</b> send next shortcut to the terminal"));
    hints->setTextFormat(Qt::RichText);
    hints->setAlignment(Qt::AlignCenter);
    hints->setForegroundRole(QPalette::PlaceholderText);
    outer->addWidget(hints);
    outer->addStretch(3);
}
