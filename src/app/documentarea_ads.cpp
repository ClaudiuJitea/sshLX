#include "documentarea.h"
#include "tabcolours.h"

#include <QInputDialog>
#include <QLineEdit>
#include "icons.h"

#include <QBoxLayout>
#include <QSplitter>
#include <QToolButton>
#include <QUuid>

#include <DockAreaWidget.h>
#include <DockComponentsFactory.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <DockWidgetTab.h>
#include <FloatingDockContainer.h>
#include <QMenu>

#include "sessionwidget.h"
#include "sshlx_engine.h"

using namespace ads;

namespace {

// A session tab's right-click menu: the session's own commands above ADS's
// Detach / Close / Close Others.
class SessionTab : public CDockWidgetTab {
public:
    SessionTab(CDockWidget *dw, DocumentArea *docs) : CDockWidgetTab(dw), m_docs(docs) {}

protected:
    QMenu *buildContextMenu(QMenu *menu) override
    {
        if (!menu)
            menu = new QMenu(this);
        auto *s = qobject_cast<SessionWidget *>(dockWidget()->widget());
        if (s && m_docs)
            m_docs->buildTabMenu(s, menu);
        CDockWidgetTab::buildContextMenu(menu);
        // Icons for ADS's own entries, to match the rest of the menu.
        for (QAction *a : menu->actions()) {
            if (a->text() == QLatin1String("Detach"))
                a->setIcon(Icons::get("float"));
            else if (a->text() == QLatin1String("Close"))
                a->setIcon(Icons::get("close"));
        }
        return menu;
    }

private:
    QPointer<DocumentArea> m_docs;
};

class TabFactory : public CDockComponentsFactory {
public:
    explicit TabFactory(DocumentArea *docs) : m_docs(docs) {}
    CDockWidgetTab *createDockWidgetTab(CDockWidget *dw) const override
    {
        return new SessionTab(dw, m_docs);
    }

private:
    DocumentArea *m_docs;
};

} // namespace

DocumentArea::DocumentArea(CDockManager *manager, QObject *parent)
    : QObject(parent), m_mgr(manager)
{
    m_mgr->setComponentsFactory(new TabFactory(this));
    connect(m_mgr, &CDockManager::focusedDockWidgetChanged, this,
            [this](CDockWidget *, CDockWidget *now) {
        auto *s = now ? qobject_cast<SessionWidget *>(now->widget()) : nullptr;
        if (s && s != m_current) {
            m_current = s;
            emit currentSessionChanged(s);
        }
    });
    connect(m_mgr, &CDockManager::floatingWidgetCreated, this, &DocumentArea::decorateFloating);
}

DocumentArea::~DocumentArea() = default;

CDockAreaWidget *DocumentArea::targetArea(const QString &panel) const
{
    if (!panel.isEmpty()) {
        CDockAreaWidget *a = m_panels.value(panel);
        if (a)
            return a;
        return nullptr;
    }
    // The area of the current session tab, else any area holding sessions.
    if (m_current && m_docks.contains(m_current)) {
        CDockWidget *dw = m_docks.value(m_current);
        if (dw->dockAreaWidget() && !dw->isFloating())
            return dw->dockAreaWidget();
    }
    for (CDockWidget *dw : m_docks)
        if (dw->dockAreaWidget() && !dw->isFloating() && !dw->isClosed())
            return dw->dockAreaWidget();
    if (m_home && !m_home->isClosed() && !m_home->isFloating())
        return m_home->dockAreaWidget();
    return nullptr;
}

void DocumentArea::addSession(SessionWidget *s, const QString &panel, const QString &dockName)
{
    auto *dw = new CDockWidget(m_mgr, s->title());
    dw->setObjectName(dockName.isEmpty()
                          ? "session:" + QUuid::createUuid().toString(QUuid::WithoutBraces)
                          : dockName);
    dw->setWidget(s, CDockWidget::ForceNoScrollArea);
    dw->setIcon(s->protocolIcon());
    // Closing goes through MainWindow (close warnings), not straight to ADS.
    dw->setFeature(CDockWidget::CustomCloseHandling, true);
    dw->setFeature(CDockWidget::DockWidgetForceCloseWithArea, false);
    m_docks.insert(s, dw);
    connect(dw, &CDockWidget::closeRequested, this, [this, s] { emit closeRequested(s); });
    connect(s, &QObject::destroyed, this, [this, s] { m_docks.remove(s); });

    CDockAreaWidget *area = targetArea(panel);
    if (area) {
        m_mgr->addDockWidgetTabToArea(dw, area);
    } else {
        // First session (or a new named panel): a new area right of the
        // side panels / current sessions.
        CDockAreaWidget *a = m_mgr->addDockWidget(RightDockWidgetArea, dw,
                                                  panel.isEmpty() ? nullptr : targetArea(QString()));
        if (!panel.isEmpty())
            m_panels.insert(panel, a);
    }
    updateTab(s);
    activate(s);
    emit sessionCountChanged(int(m_docks.size()));
}

void DocumentArea::removeSession(SessionWidget *s)
{
    CDockWidget *dw = m_docks.take(s);
    if (dw) {
        m_mgr->removeDockWidget(dw);
        dw->takeWidget();
        dw->deleteLater();
    }
    if (m_current == s)
        m_current = nullptr;
    s->setParent(nullptr);
    s->deleteLater();
    if (SessionWidget *c = currentSession())
        emit currentSessionChanged(c);
    else
        emit currentSessionChanged(nullptr);
    emit sessionCountChanged(int(m_docks.size()));
}

SessionWidget *DocumentArea::currentSession() const
{
    if (m_current && m_docks.contains(m_current))
        return m_current;
    if (CDockWidget *f = m_mgr->focusedDockWidget())
        if (auto *s = qobject_cast<SessionWidget *>(f->widget()))
            return s;
    QList<SessionWidget *> all = sessions();
    return all.isEmpty() ? nullptr : all.first();
}

QList<SessionWidget *> DocumentArea::sessions() const
{
    QList<SessionWidget *> r;
    // Docked areas in layout order, then floating ones.
    for (CDockAreaWidget *a : m_mgr->openedDockAreas())
        for (int i = 0; i < a->dockWidgetsCount(); i++)
            if (auto *s = qobject_cast<SessionWidget *>(a->dockWidget(i)->widget()))
                if (m_docks.contains(s) && !r.contains(s))
                    r << s;
    for (auto it = m_docks.begin(); it != m_docks.end(); ++it)
        if (!r.contains(it.key()))
            r << it.key();
    return r;
}

void DocumentArea::buildTabMenu(SessionWidget *s, QMenu *menu)
{
    QPointer<SessionWidget> ps(s);
    CDockWidget *dw = m_docks.value(s);
    const bool local = s->protocol() == SessionWidget::protocolId("localshell");
    const sshlx_state st = s->state();

    if (st == SSHLX_STATE_DISCONNECTED) {
        menu->addAction(Icons::get("reconnect"), local ? tr("&Restart Shell") : tr("&Reconnect"),
                        s, &SessionWidget::reconnect);
    } else {
        // Reconnecting a live session: drop it first, then connect again.
        menu->addAction(Icons::get("reconnect"), local ? tr("&Restart Shell") : tr("&Reconnect"),
                        s, [ps] {
            if (!ps)
                return;
            ps->disconnectSession();
            ps->reconnect();
        });
        menu->addAction(Icons::get("disconnect"), tr("&Disconnect"), s,
                        &SessionWidget::disconnectSession);
    }
    menu->addAction(Icons::get("connection-new"), tr("D&uplicate"), s,
                    [ps] { if (ps) emit ps->duplicateRequested(ps->displayName()); });
    if (!s->isFileTransfer() && s->protocol() == SessionWidget::protocolId("ssh"))
        menu->addAction(Icons::get("export"), tr("File &Transfer"), s,
                        &SessionWidget::fileTransferRequested);
    if (!s->isFileTransfer()) {
        buildGroupMenu({s}, menu->addMenu(tr("Tab &Group")));
        buildColourMenu({s}, menu->addMenu(Icons::get("palette"), tr("Tab Co&lour")));
        QAction *lock = menu->addAction(Icons::get("lock"), tr("Loc&k Session"));
        lock->setCheckable(true);
        lock->setChecked(s->locked());
        connect(lock, &QAction::toggled, s, &SessionWidget::setLocked);
    }
    menu->addSeparator();
    s->buildAppearanceMenu(menu->addMenu(Icons::get("palette"), tr("&Appearance")));
    menu->addAction(Icons::get("settings"), tr("Change &Settings..."), s,
                    &SessionWidget::changeSettings);
    menu->addAction(Icons::get("event-log"), tr("&Event Log"), s, &SessionWidget::showEventLog);
    menu->addSeparator();
    // Split and re-attach act on the current tab: make this one current.
    const bool splittable = dw && dw->dockAreaWidget()
        && dw->dockAreaWidget()->dockWidgetsCount() > 1;
    QAction *right = menu->addAction(Icons::get("split-right"), tr("Split &Right"), this,
                                     [this, ps] {
        if (ps) { activate(ps); splitCurrent(Qt::Horizontal); }
    });
    QAction *down = menu->addAction(Icons::get("split-down"), tr("Split &Down"), this,
                                    [this, ps] {
        if (ps) { activate(ps); splitCurrent(Qt::Vertical); }
    });
    right->setEnabled(splittable);
    down->setEnabled(splittable);
    if (dw && dw->isInFloatingContainer())
        menu->addAction(Icons::get("attach"), tr("Re-&attach to Main Window"), this, [this, ps] {
            if (ps) { activate(ps); attachCurrent(); }
        });
    menu->addSeparator();
}

void DocumentArea::activate(SessionWidget *s)
{
    CDockWidget *dw = m_docks.value(s);
    if (!dw)
        return;
    dw->toggleView(true);
    dw->setAsCurrentTab();
    dw->raise();
    m_mgr->setDockWidgetFocused(dw);
    m_current = s;
    s->focusTerminal();
    emit currentSessionChanged(s);
}

void DocumentArea::nextTab(int direction)
{
    QList<SessionWidget *> all = sessions();
    if (all.size() < 2)
        return;
    int i = all.indexOf(currentSession());
    activate(all[(i + direction + all.size()) % all.size()]);
}

void DocumentArea::updateTab(SessionWidget *s)
{
    CDockWidget *dw = m_docks.value(s);
    if (!dw)
        return;
    // PuTTY's default title is "<host> - <appname>"; the suffix only takes
    // tab space. Titles set by the server are kept as they are.
    QString t = s->title();
    if (t.endsWith(QLatin1String(" - sshLX")))
        t.chop(8);
    if (!s->group().isEmpty())
        t = QStringLiteral("[%1] %2").arg(s->group(), t);
    dw->setWindowTitle(t);
    dw->setToolTip(s->locked() ? tr("%1 (locked)").arg(s->title()) : s->title());
    const QColor c(colourFor(s));
    if (CDockWidgetTab *tab = dw->tabWidget()) {
        // Its own sheet takes priority over the dock manager's. Tinted, with a
        // solid bar, so the active tab stays the one that stands out.
        const QString rgb = QStringLiteral("%1, %2, %3").arg(c.red()).arg(c.green()).arg(c.blue());
        tab->setStyleSheet(!c.isValid() ? QString() : QStringLiteral(
            "ads--CDockWidgetTab { background: rgba(%1, 55); border-top: 3px solid %2; margin-top: 3px; }"
            "ads--CDockWidgetTab:hover { background: rgba(%1, 85); }"
            "ads--CDockWidgetTab[activeTab=\"true\"] { background: rgba(%1, 120); }"
            "ads--CDockWidgetTab[focused=\"true\"] { background: rgba(%1, 150); border-top: 3px solid %2; margin-top: 3px; }")
            .arg(rgb, c.name()));
    }
    dw->setIcon(s->locked() ? Icons::get("lock")
                : s->state() == SSHLX_STATE_DISCONNECTED ? Icons::get("disconnected")
                                                         : s->protocolIcon());
}

QString DocumentArea::colourFor(SessionWidget *s) const
{
    if (!s->tabColour().isEmpty())
        return s->tabColour();
    const QString group = TabColours::groupColour(s->group());
    return group.isEmpty() ? s->connectionColour() : group;
}

void DocumentArea::buildColourMenu(const QList<SessionWidget *> &tabs, QMenu *menu)
{
    QList<QPointer<SessionWidget>> list;
    for (SessionWidget *s : tabs)
        list.append(s);
    TabColours::fillMenu(menu, tabs.size() == 1 ? tabs.first()->tabColour() : QString(),
                         m_mgr->window(), [list](const QString &colour) {
        for (const QPointer<SessionWidget> &s : list)
            if (s)
                s->setTabColour(colour);
    });
}

void DocumentArea::buildGroupColourMenu(const QString &group, QMenu *menu)
{
    TabColours::fillMenu(menu, TabColours::groupColour(group), m_mgr->window(),
                         [this, group](const QString &colour) {
        TabColours::setGroupColour(group, colour);
        for (SessionWidget *s : sessions())
            if (s->group() == group)
                s->setGroup(group);      // tells everyone who shows it
    });
}

QStringList DocumentArea::groupNames() const
{
    QStringList names;
    for (SessionWidget *s : sessions())
        if (!s->group().isEmpty() && !names.contains(s->group()))
            names.append(s->group());
    names.sort(Qt::CaseInsensitive);
    return names;
}

void DocumentArea::buildGroupMenu(const QList<SessionWidget *> &tabs, QMenu *menu)
{
    QList<QPointer<SessionWidget>> list;
    for (SessionWidget *s : tabs)
        list.append(s);
    auto assign = [list](const QString &name) {
        for (const QPointer<SessionWidget> &s : list)
            if (s)
                s->setGroup(name);
    };
    menu->addAction(tr("&New Group..."), this, [this, assign] {
        bool ok = false;
        const QString name = QInputDialog::getText(m_mgr->window(), tr("New Tab Group"),
                                                   tr("Group name:"), QLineEdit::Normal,
                                                   QString(), &ok).trimmed();
        if (ok && !name.isEmpty())
            assign(name);
    });
    const QStringList names = groupNames();
    if (!names.isEmpty())
        menu->addSeparator();
    for (const QString &n : names)
        menu->addAction(n, this, [assign, n] { assign(n); });
    menu->addSeparator();
    menu->addAction(tr("No Group"), this, [assign] { assign(QString()); });
    if (tabs.size() == 1 && !tabs.first()->group().isEmpty()) {
        menu->addSeparator();
        buildGroupColourMenu(tabs.first()->group(),
                             menu->addMenu(tr("Group &Colour")));
    }
}

QString DocumentArea::dockName(SessionWidget *s) const
{
    CDockWidget *dw = m_docks.value(s);
    return dw ? dw->objectName() : QString();
}

QByteArray DocumentArea::saveLayout() const
{
    return m_mgr->saveState();
}

bool DocumentArea::restoreLayout(const QByteArray &state)
{
    return !state.isEmpty() && m_mgr->restoreState(state);
}

void DocumentArea::splitCurrent(Qt::Orientation o)
{
    SessionWidget *s = currentSession();
    CDockWidget *dw = s ? m_docks.value(s) : nullptr;
    if (!dw || !dw->dockAreaWidget())
        return;
    CDockAreaWidget *from = dw->dockAreaWidget();
    if (from->dockWidgetsCount() < 2)
        return;   // nothing to split away from
    m_mgr->addDockWidget(o == Qt::Horizontal ? RightDockWidgetArea : BottomDockWidgetArea,
                         dw, from);
    activate(s);
}

void DocumentArea::floatCurrent()
{
    SessionWidget *s = currentSession();
    if (CDockWidget *dw = s ? m_docks.value(s) : nullptr) {
        // Qt activates the new window as it is shown; asking again (raise)
        // only leaves an unused activation token on Wayland.
        dw->setFloating();
        m_current = s;
        s->focusTerminal();
        emit currentSessionChanged(s);
    }
}

void DocumentArea::setHomeDock(CDockWidget *dw)
{
    m_home = dw;
}

void DocumentArea::setSidePanels(const QList<CDockWidget *> &panels)
{
    m_sidePanels.clear();
    for (CDockWidget *dw : panels)
        m_sidePanels << dw;
}

void DocumentArea::setSidePanelsRight(bool right)
{
    m_panelsRight = right;
    // Panels docked in the main window (closed ones too, so that they
    // reopen on the new side), top to bottom; floating ones stay put.
    QList<CDockWidget *> docked;
    for (CDockWidget *dw : m_sidePanels)
        if (dw && dw->dockAreaWidget() && !dw->isInFloatingContainer())
            docked << dw;
    if (docked.isEmpty())
        return;
    auto top = [](CDockWidget *dw) {
        return dw->dockAreaWidget()->mapTo(dw->dockAreaWidget()->window(), QPoint()).y();
    };
    std::stable_sort(docked.begin(), docked.end(),
                     [&](CDockWidget *a, CDockWidget *b) { return top(a) < top(b); });

    // Panels sharing an area stay tabbed together; remember what was shown.
    QList<CDockAreaWidget *> oldAreas;
    QList<CDockWidget *> shown;
    QList<int> heights;
    int width = 0;
    for (CDockWidget *dw : docked) {
        CDockAreaWidget *a = dw->dockAreaWidget();
        if (oldAreas.contains(a))
            continue;
        oldAreas << a;
        shown << a->currentDockWidget();
        heights << a->height();
        if (a->isVisible())
            width = std::max(width, a->width());
    }
    QHash<CDockWidget *, bool> closed;
    for (CDockWidget *dw : docked)
        closed[dw] = dw->isClosed();

    QList<CDockAreaWidget *> newAreas(oldAreas.size(), nullptr);
    for (CDockWidget *dw : docked) {
        const int g = oldAreas.indexOf(dw->dockAreaWidget());
        if (newAreas[g])
            m_mgr->addDockWidgetTabToArea(dw, newAreas[g]);
        else if (g > 0 && newAreas[g - 1])
            newAreas[g] = m_mgr->addDockWidget(BottomDockWidgetArea, dw, newAreas[g - 1]);
        else
            newAreas[g] = m_mgr->addDockWidget(right ? RightDockWidgetArea : LeftDockWidgetArea, dw);
    }
    for (CDockWidget *dw : docked)
        if (closed[dw])
            dw->toggleView(false);
    for (int g = 0; g < newAreas.size(); ++g)
        if (newAreas[g] && shown[g])
            newAreas[g]->setCurrentDockWidget(shown[g]);

    if (newAreas.size() > 1)
        m_mgr->setSplitterSizes(newAreas.first(), heights);
    // The column's width: the first horizontal splitter above the column.
    for (QWidget *w = newAreas.first(); width > 0 && w && w->parentWidget(); w = w->parentWidget()) {
        auto *sp = qobject_cast<QSplitter *>(w->parentWidget());
        if (!sp || sp->orientation() != Qt::Horizontal)
            continue;
        QList<int> sizes = sp->sizes();
        const int i = sp->indexOf(w);
        int total = 0;
        for (int v : sizes)
            total += v;
        const int rest = total - sizes[i];
        if (sizes.size() > 1 && rest > 0 && total > width) {
            for (int &v : sizes)
                v = v * (total - width) / rest;
            sizes[i] = width;
            sp->setSizes(sizes);
        }
        break;
    }
}

CDockAreaWidget *DocumentArea::sidePanelArea(CDockWidget *except) const
{
    // A docked area of the main window (the manager lists only its own
    // areas, not floating ones) holding another side panel.
    for (CDockAreaWidget *a : m_mgr->openedDockAreas()) {
        CDockWidget *cur = a->currentDockWidget();
        if (!cur || cur == except || cur == m_home)
            continue;
        if (!qobject_cast<SessionWidget *>(cur->widget()))
            return a;
    }
    return nullptr;
}

void DocumentArea::attachCurrent()
{
    SessionWidget *s = currentSession();
    CDockWidget *dw = s ? m_docks.value(s) : nullptr;
    if (CFloatingDockContainer *fc = dw ? dw->floatingDockContainer() : nullptr) {
        attach(fc);
        activate(s);
    }
}

void DocumentArea::attach(CFloatingDockContainer *fc)
{
    const QList<CDockWidget *> widgets = fc->dockWidgets();
    for (CDockWidget *dw : widgets) {
        if (dw->isClosed())
            continue;
        if (qobject_cast<SessionWidget *>(dw->widget())) {
            if (CDockAreaWidget *area = targetArea(QString()))
                m_mgr->addDockWidgetTabToArea(dw, area);
            else
                m_mgr->addDockWidget(RightDockWidgetArea, dw);
        } else if (CDockAreaWidget *beside = sidePanelArea(dw)) {
            // Back into the side column, in the usual order of the panels.
            int mine = m_sidePanels.indexOf(dw);
            int theirs = m_sidePanels.indexOf(beside->currentDockWidget());
            m_mgr->addDockWidget(mine >= 0 && theirs >= 0 && mine < theirs
                                     ? TopDockWidgetArea : BottomDockWidgetArea,
                                 dw, beside);
        } else {
            // The only side panel: beside the session area, on the panels' side.
            m_mgr->addDockWidget(m_panelsRight ? RightDockWidgetArea : LeftDockWidgetArea,
                                 dw, targetArea(QString()));
        }
    }
}

void DocumentArea::decorateFloating(CFloatingDockContainer *fc)
{
    // Only ADS's own title bar (used on Wayland and KWin) has a layout to
    // extend; with a native title bar the View menu entry remains.
    QWidget *bar = fc->titleBarWidget();
    auto *layout = bar ? qobject_cast<QBoxLayout *>(bar->layout()) : nullptr;
    auto *maximize = bar ? bar->findChild<QToolButton *>("floatingTitleMaximizeButton") : nullptr;
    if (!layout || !maximize)
        return;
    auto *btn = new QToolButton(bar);
    btn->setObjectName("floatingTitleAttachButton");
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolTip(tr("Re-attach to Main Window"));
    btn->setIcon(Icons::get("attach"));
    btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->insertWidget(layout->indexOf(maximize), btn);
    connect(btn, &QToolButton::clicked, this, [this, fc] {
        // Re-docking deletes the floating window; leave its click handler first.
        QMetaObject::invokeMethod(this, [this, fc = QPointer<CFloatingDockContainer>(fc)] {
            if (fc)
                attach(fc);
        }, Qt::QueuedConnection);
    });
}
