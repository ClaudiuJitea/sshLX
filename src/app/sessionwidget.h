// SessionWidget - one tab: a PuTTY session (engine handle), its terminal
// view, scrollbar, in-tab prompt bar and reconnect bar.
#pragma once

#include <QIcon>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>

#include "sshlx_engine.h"

class TerminalView;
class PromptBar;
class QScrollBar;
class QLabel;
class QPushButton;
class QFrame;
class QHBoxLayout;
class QMenu;
class EventLogDialog;

class SessionWidget : public QWidget {
    Q_OBJECT
public:
    // conf is copied. displayName is the saved-session / connection name.
    SessionWidget(const sshlx_conf *conf, const QString &displayName,
                  QWidget *parent = nullptr);
    ~SessionWidget() override;

    void connectSession();
    void disconnectSession();
    void reconnect();

    QString title() const { return m_title; }
    QString displayName() const { return m_displayName; }
    QString connectionId() const { return m_connectionId; }
    void setConnectionId(const QString &id) { m_connectionId = id; emit identityChanged(); }
    QString serialByIdPath() const { return m_serialById; }
    QString savedSessionName() const { return m_savedSession; }
    void setSavedSessionName(const QString &n) { m_savedSession = n; emit identityChanged(); }
    void setSerialByIdPath(const QString &p) { m_serialById = p; emit identityChanged(); }
    sshlx_state state() const;
    const sshlx_conf *conf() const;
    QIcon protocolIcon() const;
    int protocol() const;
    TerminalView *view() const { return m_view; }
    // Text for a "really close?" prompt, or empty.
    QString closeWarning() const;

    void buildContextMenu(QMenu *menu);
    void showEventLog();
    void changeSettings();
    void copy();
    void paste();
    void copyAll();
    void clearScrollback();
    void resetTerminal();
    void focusTerminal();

    // Optional keyring support: provider(user, host, port) -> password.
    using PasswordLookup = std::function<void(const QString &user,
        const QString &host, int port,
        std::function<void(bool found, const QString &pw)> done)>;
    void setPasswordLookup(PasswordLookup f);

    static QIcon iconForProtocol(int proto);

signals:
    void titleChanged(const QString &title);
    void stateChanged(sshlx_state state);
    void closeRequested();
    void bellRang();
    void duplicateRequested(const QString &displayName);
    void exitedWithClose();
    // What the tab can be reopened from changed (for the crash breadcrumb).
    void identityChanged();

private:
    friend struct SessionTrampolines;
    void applyConfToView();
    void onFatal(const QString &msg, int err);
    void onExited(int code, bool closeRequested);
    void onState(sshlx_state st);
    void updateReconnectBar();
    void fontStep(int delta);
    void onPrompt(const sshlx_prompt_request *req);
    void writeClipboard(int clip, const QString &text, bool mustDeselect);
    void requestPaste(int clip);

    sshlx_session *m_session = nullptr;
    QString m_displayName, m_title, m_connectionId, m_serialById, m_savedSession;
    TerminalView *m_view;
    QScrollBar *m_scroll;
    QHBoxLayout *m_termRow;
    PromptBar *m_prompts;
    QFrame *m_reconnectBar;
    QLabel *m_reconnectLabel;
    QPushButton *m_reconnectButton;
    QString m_lastError;
    int m_lastErrno = 0;
    bool m_ignoreScroll = false;
    QPointer<EventLogDialog> m_eventLog;
    PasswordLookup m_passwordLookup;
};
