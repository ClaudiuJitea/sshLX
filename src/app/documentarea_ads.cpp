#include "documentarea.h"
#include "icons.h"

#include <QBoxLayout>
#include <QToolButton>
#include <QUuid>

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <FloatingDockContainer.h>

#include "sessionwidget.h"
#include "sshlx_engine.h"

using namespace ads;

DocumentArea::DocumentArea(CDockManager *manager, QObject *parent)
    : QObject(parent), m_mgr(manager)
{
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
    dw->setWindowTitle(t);
    dw->setToolTip(s->title());
    dw->setIcon(s->state() == SSHLX_STATE_DISCONNECTED
                    ? Icons::get("disconnected") : s->protocolIcon());
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
        dw->setFloating();
        activate(s);
    }
}

void DocumentArea::setHomeDock(CDockWidget *dw)
{
    m_home = dw;
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
        } else {
            // A side panel: left of the session area, as it starts out.
            m_mgr->addDockWidget(LeftDockWidgetArea, dw, targetArea(QString()));
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
