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
namespace ads {
class CDockManager;
class CDockWidget;
class CDockAreaWidget;
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
    void nextTab(int direction);
    void updateTab(SessionWidget *s);
    QString dockName(SessionWidget *s) const;

    QByteArray saveLayout() const;
    bool restoreLayout(const QByteArray &state);
    // Move the current tab into a new dock area beside/below its own.
    void splitCurrent(Qt::Orientation o);
    void floatCurrent();
    ads::CDockWidget *dockFor(SessionWidget *s) const { return m_docks.value(s); }
    // The permanent page that anchors the session area; new tabs open
    // next to it when no session is current.
    void setHomeDock(ads::CDockWidget *dw);

signals:
    void currentSessionChanged(SessionWidget *s);
    void closeRequested(SessionWidget *s);
    void sessionCountChanged(int count);

private:
    ads::CDockAreaWidget *targetArea(const QString &panel) const;

    ads::CDockManager *m_mgr;
    QHash<SessionWidget *, ads::CDockWidget *> m_docks;
    QPointer<SessionWidget> m_current;
    QHash<QString, QPointer<ads::CDockAreaWidget>> m_panels;
    QPointer<ads::CDockWidget> m_home;
};
