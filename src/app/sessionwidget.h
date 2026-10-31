// SessionWidget - one tab: a PuTTY session (engine handle), its terminal
// view, scrollbar, in-tab prompt bar and reconnect bar.
#pragma once

#include <QIcon>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

#include "sshlx_engine.h"

class TerminalView;
class PromptBar;
class ConfigPush;
class ConfigPushBar;
class FindBar;
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
    sshlx_session *sessionForTests() const { return m_session; }
    // Text for a "really close?" prompt, or empty.
    QString closeWarning() const;

    // linkAt: the link right-clicked on, offered first in the menu.
    void buildContextMenu(QMenu *menu, const QString &link = QString());
    // Colour scheme, font and cursor, changed live on this tab.
    void buildAppearanceMenu(QMenu *menu);
    void buildPasswordMenu(QMenu *menu);
    // Applies edit to a copy of the session's conf and reconfigures with it.
    void changeConf(const std::function<void(sshlx_conf *)> &edit);
    void showEventLog();
    void changeSettings();
    void copy();
    void paste();
    void copyAll();
    // Opens the review dialog, then sends text line by line (see ConfigPush).
    void pasteConfiguration(const QString &text = QString());
    void findInTerminal();
    void selectAll();
    void clearTerminal();
    void saveTerminalText();
#ifdef SSHLX_HAVE_PRINT
    void printTerminalText();
    void printTo(class QPrinter *printer);
#endif
    // Your own tab title; empty goes back to the server's.
    void changeTitle();
    void setCustomTitle(const QString &text);
    void runMacro(const QString &text);
    // Add Keyword: starts from the selected text.
    void addKeyword();
    void clearScrollback();
    void resetTerminal();
    void focusTerminal();

    // Optional keyring support: provider(user, host, port) -> password.
    using PasswordLookup = std::function<void(const QString &user,
        const QString &host, int port, bool passphrase,
        std::function<void(bool found, const QString &pw)> done)>;
    void setPasswordLookup(PasswordLookup f);

    // Send Saved Password, for prompts inside the session such as sudo. The
    // provider lists the credentials, the one linked to this tab first.
    struct SavedPassword {
        QString name, account;
    };
    using SavedPasswords = std::function<QList<SavedPassword>()>;
    void setSavedPasswords(SavedPasswords f) { m_savedPasswords = std::move(f); }
    void sendSavedPassword(const QString &name, const QString &account);

    // Options > Terminal changed: apply the mouse settings to every tab.
    static void reloadMousePrefs();
    // The keyword list or its on/off switch changed: repaint every tab.
    static void reloadHighlights();

    static QIcon iconForProtocol(int proto);
    // A back end's PuTTY protocol number from its id ("serial", "localshell"...).
    static int protocolId(const char *name);

signals:
    void titleChanged(const QString &title);
    void stateChanged(sshlx_state state);
    void closeRequested();
    void bellRang();
    void duplicateRequested(const QString &displayName);
    void exitedWithClose();
    // What the tab can be reopened from changed (for the crash breadcrumb).
    void identityChanged();
    // Appearance menu: keep this tab's look for its connection, or for
    // every new session.
    void saveAppearanceRequested();
    void defaultAppearanceRequested();

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
    QString allText();
    void deliverPaste(const QString &text);
    void applyMousePrefs();
    void refreshFindIndex();
    void runFind();
    void stepFind(int delta);
    void showMatch();
    void closeFind();
    void onRightClick(const QPoint &globalPos, const QPoint &cell, bool ctrl);

    sshlx_session *m_session = nullptr;
    QString m_customTitle, m_serverTitle;
    QString m_displayName, m_title, m_connectionId, m_serialById, m_savedSession;
    TerminalView *m_view;
    QScrollBar *m_scroll;
    QHBoxLayout *m_termRow;
    PromptBar *m_prompts;
    ConfigPush *m_push;
    ConfigPushBar *m_pushBar;
    FindBar *m_findBar;
    struct FindMatch { int row, start, len; };
    QStringList m_findRows;       // every row of the buffer, oldest first
    QList<FindMatch> m_matches;
    int m_matchIndex = -1;
    bool m_findStale = true;      // output arrived since m_findRows was read
    QFrame *m_reconnectBar;
    QLabel *m_reconnectLabel;
    QPushButton *m_reconnectButton;
    QString m_lastError;
    int m_lastErrno = 0;
    bool m_ignoreScroll = false;
    QPointer<EventLogDialog> m_eventLog;
    PasswordLookup m_passwordLookup;
    SavedPasswords m_savedPasswords;
};
