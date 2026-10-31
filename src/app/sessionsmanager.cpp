#include "sessionsmanager.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "dialogkit.h"
#include "documentarea.h"
#include "icons.h"
#include "mainwindow.h"
#include "sessionwidget.h"

Q_DECLARE_METATYPE(SessionWidget *)

SessionsManager::SessionsManager(DocumentArea *docs, MainWindow *window)
    : QWidget(window), m_docs(docs), m_window(window)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filter sessions..."));
    m_filter->setClearButtonEnabled(true);
    m_filter->setAccessibleName(tr("Filter sessions"));
    v->addWidget(m_filter);
    m_list = new QTreeWidget;
    m_list->setAccessibleName(tr("Active sessions"));
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({tr("Session"), tr("State"), tr("Group")});
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    v->addWidget(m_list, 1);
    m_count = new QLabel;
    v->addWidget(m_count);

    auto *row = new QHBoxLayout;
    auto tool = [&](const QString &icon, const QString &tip, auto fn) {
        auto *b = new QToolButton;
        b->setIcon(Icons::get(icon));
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        connect(b, &QToolButton::clicked, this, fn);
        row->addWidget(b);
    };
    tool("reconnect", tr("Reconnect"), [this] { reconnectSelected(); });
    tool("disconnect", tr("Disconnect"), [this] { disconnectSelected(); });
    tool("lock", tr("Lock"), [this] { setSelectedLocked(true); });
    auto *unlock = new QToolButton;
    unlock->setText(tr("Unlock"));
    connect(unlock, &QToolButton::clicked, this, [this] { setSelectedLocked(false); });
    row->addWidget(unlock);
    tool("close", tr("Close"), [this] { closeSelected(); });
    row->addStretch(1);
    v->addLayout(row);

    m_refresh = new QTimer(this);
    m_refresh->setSingleShot(true);
    m_refresh->setInterval(50);
    connect(m_refresh, &QTimer::timeout, this, &SessionsManager::refresh);
    connect(m_filter, &QLineEdit::textChanged, this, &SessionsManager::refresh);
    connect(docs, &DocumentArea::sessionCountChanged, this, &SessionsManager::refreshSoon);
    connect(docs, &DocumentArea::currentSessionChanged, this, &SessionsManager::refreshSoon);
    connect(m_list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *it) {
        m_docs->activate(it->data(0, Qt::UserRole).value<SessionWidget *>());
    });
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QList<SessionWidget *> tabs = selected();
        if (tabs.isEmpty())
            return;
        QMenu menu;
        menu.addAction(Icons::get("reconnect"), tr("Reconnect"), this, &SessionsManager::reconnectSelected);
        menu.addAction(Icons::get("disconnect"), tr("Disconnect"), this, &SessionsManager::disconnectSelected);
        menu.addAction(Icons::get("lock"), tr("Lock"), this, [this] { setSelectedLocked(true); });
        menu.addAction(tr("Unlock"), this, [this] { setSelectedLocked(false); });
        m_docs->buildGroupMenu(tabs, menu.addMenu(tr("Tab Group")));
        menu.addSeparator();
        menu.addAction(Icons::get("close"), tr("Close"), this, [this] { closeSelected(); });
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });
    refresh();
}

void SessionsManager::refreshSoon()
{
    m_refresh->start();
}

void SessionsManager::refresh()
{
    const QList<SessionWidget *> keep = selected();
    const QList<SessionWidget *> all = m_docs->sessions();
    const QString filter = m_filter->text().trimmed();
    SessionWidget *cur = m_docs->currentSession();
    m_list->clear();
    int shown = 0;
    for (SessionWidget *s : all) {
        QString state;
        switch (s->state()) {
        case SSHLX_STATE_CONNECTED: state = tr("Connected"); break;
        case SSHLX_STATE_CONNECTING: state = tr("Connecting"); break;
        case SSHLX_STATE_DISCONNECTED: state = tr("Disconnected"); break;
        default: state = tr("Not connected");
        }
        if (s->locked())
            state += tr(", locked");
        const QStringList row{s->title(), state, s->group()};
        if (!filter.isEmpty() && !row.join('\n').contains(filter, Qt::CaseInsensitive))
            continue;
        auto *it = new QTreeWidgetItem(m_list, row);
        it->setData(0, Qt::UserRole, QVariant::fromValue(s));
        it->setIcon(0, s->locked() ? Icons::get("lock") : s->protocolIcon());
        if (s == cur) {
            QFont f = it->font(0);
            f.setBold(true);
            it->setFont(0, f);
        }
        it->setSelected(keep.contains(s));
        shown++;
    }
    m_count->setText(filter.isEmpty() ? tr("%n session(s)", nullptr, int(all.size()))
                                      : tr("%1 of %2 sessions").arg(shown).arg(all.size()));
}

QList<SessionWidget *> SessionsManager::selected() const
{
    const QList<SessionWidget *> live = m_docs->sessions();
    QList<SessionWidget *> out;
    for (QTreeWidgetItem *it : m_list->selectedItems()) {
        auto *s = it->data(0, Qt::UserRole).value<SessionWidget *>();
        if (live.contains(s))
            out.append(s);
    }
    return out;
}

void SessionsManager::disconnectSelected()
{
    for (SessionWidget *s : selected())
        if (s->state() == SSHLX_STATE_CONNECTED || s->state() == SSHLX_STATE_CONNECTING)
            s->disconnectSession();
}

void SessionsManager::reconnectSelected()
{
    for (SessionWidget *s : selected())
        if (s->state() == SSHLX_STATE_DISCONNECTED)
            s->reconnect();
}

void SessionsManager::setSelectedLocked(bool locked)
{
    for (SessionWidget *s : selected())
        s->setLocked(locked);
}

void SessionsManager::closeSelected(bool ask)
{
    const QList<SessionWidget *> tabs = selected();
    int live = 0;
    for (SessionWidget *s : tabs)
        if (!s->closeWarning().isEmpty())
            live++;
    if (ask && live && !DialogKit::confirm(window(), tr("Close sessions?"),
                                           tr("%n running session(s) will be closed.", nullptr, live),
                                           tr("Close"), tr("Cancel"), false, true))
        return;
    for (SessionWidget *s : tabs)
        m_window->closeTabNow(s);
}
