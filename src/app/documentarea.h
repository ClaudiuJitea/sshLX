// DocumentArea - where session tabs live, built on the Qt Advanced Docking
// System: tabs can be reordered, dragged into further dock areas (side by
// side or stacked), and floated as separate windows. The rest of the
// application only talks to this interface.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>

class SessionWidget;
class QMenu;
namespace ads {
class CDockManager;
class CDockWidget;
class CDockAreaWidget;
class CFloatingDockContainer;
}

class DocumentArea : public QObject {
    Q_OBJECT
public:
    explicit DocumentArea(ads::CDockManager *manager, QObject *parent = nullptr);
    ~DocumentArea() override;

    // Add a session tab. dockName is the dock widget's object name (stable
    // across runs for layout restore); empty = generate one. panel names a
    // dock area to open in (created on first use); empty = the current area.
    void addSession(SessionWidget *s, const QString &panel = QString(),
                    const QString &dockName = QString());
    // Removes and deletes the session widget.
    void removeSession(SessionWidget *s);
    SessionWidget *currentSession() const;
    // In visual order: dock areas, then tabs within each area.
    QList<SessionWidget *> sessions() const;
    void activate(SessionWidget *s);
    // The session part of a tab's right-click menu (reconnect, appearance,
    // split...); ADS adds Detach and Close below it.
    void buildTabMenu(SessionWidget *s, QMenu *menu);
    void nextTab(int direction);
    void updateTab(SessionWidget *s);
    QString dockName(SessionWidget *s) const;

    QByteArray saveLayout() const;
    bool restoreLayout(const QByteArray &state);
    // Move the current tab into a new dock area beside/below its own.
    void splitCurrent(Qt::Orientation o);
    void floatCurrent();
    // Dock the current floating tab's window back into the main window.
    void attachCurrent();
    // Dock every tab of a floating window back into the main window:
    // sessions to the session area, side panels beside the session area.
    void attach(ads::CFloatingDockContainer *fc);
    ads::CDockWidget *dockFor(SessionWidget *s) const { return m_docks.value(s); }
    // The permanent page that anchors the session area; new tabs open
    // next to it when no session is current.
    void setHomeDock(ads::CDockWidget *dw);
    // The side panels (connections, config...) in their top-to-bottom
    // order; re-attaching a panel puts it back into that column.
    void setSidePanels(const QList<ads::CDockWidget *> &panels);
    // Moves the side panels docked in the main window into one column at
    // the left or right edge, keeping their order, tabs and sizes.
    void setSidePanelsRight(bool right);
    bool sidePanelsRight() const { return m_panelsRight; }

signals:
    void currentSessionChanged(SessionWidget *s);
    void closeRequested(SessionWidget *s);
    void sessionCountChanged(int count);

private:
    ads::CDockAreaWidget *targetArea(const QString &panel) const;
    // Adds the re-attach button to a floating window's title bar.
    void decorateFloating(ads::CFloatingDockContainer *fc);
    ads::CDockAreaWidget *sidePanelArea(ads::CDockWidget *except) const;

    ads::CDockManager *m_mgr;
    QHash<SessionWidget *, ads::CDockWidget *> m_docks;
    QPointer<SessionWidget> m_current;
    QHash<QString, QPointer<ads::CDockAreaWidget>> m_panels;
    QPointer<ads::CDockWidget> m_home;
    QList<QPointer<ads::CDockWidget>> m_sidePanels;
    bool m_panelsRight = false;
};
