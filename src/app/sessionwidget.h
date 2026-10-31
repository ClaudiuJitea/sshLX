// SessionWidget - one tab: a PuTTY session (engine handle), its terminal
// view, scrollbar, in-tab prompt bar and reconnect bar.
#pragma once

#include <QDateTime>
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
class QDialog;
class QFrame;
class QHBoxLayout;
class QMenu;
class EventLogDialog;
class LogonRunner;
class ScriptRecorder;
struct KeyBinding;
class ModemTransfer;
class QProgressBar;
class QTimer;
struct LogonStep;

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
    // File transfer. The receiver takes the remote output instead of the
    // terminal, which stays for the login.
    void setRawReceiver(std::function<void(const char *, size_t)> f);
    void sendRaw(const QByteArray &data);
    size_t sendBacklog() const;
    bool sendReady() const;
    // The panel replaces the terminal once shown; the tab owns it.
    void setFilePanel(QWidget *panel, std::function<bool()> busy);
    void showFilePanel(bool show);
    bool isFileTransfer() const { return m_filePanel != nullptr; }
    void setReconnectBarEnabled(bool on) { m_reconnectBarEnabled = on; updateReconnectBar(); }
    QString lastError() const { return m_lastError; }
    // The login finished, whatever became of the session after it.
    bool loggedIn() const { return m_loggedIn; }
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
    // For several tabs at once, so without Paste Configuration's review dialog.
    bool canSendCommands() const;
    // Locked tabs take no typing, paste, command, macro or script.
    bool locked() const { return m_locked; }
    void setLocked(bool on);
    QString group() const { return m_group; }
    void setGroup(const QString &name);
    // Live typing: the tabs that also get what is typed in this one.
    using Mirror = std::function<QList<SessionWidget *>(SessionWidget *source)>;
    static void setMirror(Mirror f);
    // Script recording in this tab; stop returns the script.
    void startRecording();
    QString stopRecording();
    bool recording() const { return m_recorder != nullptr; }
    // Session log, when the connection has one.
    bool logEnabled() const;
    bool logPaused() const { return m_logPaused; }
    void setLogPaused(bool paused);
    bool rotateLog();
    QString logPath() const { return m_logPath; }
    using ScriptPath = std::function<QString()>;
    void setLogonScript(ScriptPath f) { m_logonScript = std::move(f); }
    void sendCommand(const QString &line);
    void sendCommands(const QStringList &lines);
    // Types text as given, for scripts.
    void typeText(const QString &text);
    // Xmodem, Ymodem and Zmodem through the terminal. The remote side must
    // be started first (sz, rz, xmodem...), except that sendModem types "rz"
    // for Zmodem. Both return false when something else is using the tab.
    bool sendModem(int protocol, const QStringList &files, bool resume = false);
    bool receiveModem(int protocol, const QString &directory, const QString &saveAs = QString());
    bool transferRunning() const;
    // Run after each connect: the connection's logon actions, resolved then
    // so a change applies to the next connect.
    using LogonSteps = std::function<QList<LogonStep>()>;
    void setLogonSteps(LogonSteps f) { m_logonSteps = std::move(f); }
    // A credential id to its keyring account; empty id: this tab's credential.
    using CredentialAccount = std::function<QString(const QString &id)>;
    void setCredentialAccount(CredentialAccount f) { m_credentialAccount = std::move(f); }
    bool logonRunning() const;
    // Answers a password prompt without showing or asking; done reports why not.
    void sendPasswordQuietly(const QString &credentialId,
                             std::function<void(bool ok, const QString &error)> done);
    bool atPasswordPrompt() const;
    QString cursorLine() const;
    QString allTextForScripts() { return allText(); }
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
    // A word marked in every tab until cleared, not saved anywhere.
    static void setAdHocHighlight(const QString &text);
    // What a status line shows.
    struct Status {
        QString protocol, cipherOut, cipherIn;
        qint64 received = 0;
        qint64 connectedSecs = -1;      // -1: not connected
        int cols = 0, rows = 0;
    };
    Status status() const;
    void showHexView();
    QDialog *hexViewForTests() const;
    void sendAsciiFile(const QString &path, int lineDelayMs, int charDelayMs);
    bool sendingAscii() const;
    // The dropped-file question.
    void handleDroppedFiles(const QStringList &paths);
    // Text sent after this many idle seconds; 0: none. Resolved at each connect.
    struct AntiIdle { QString text; int seconds = 0; };
    using AntiIdleSource = std::function<AntiIdle()>;
    void setAntiIdle(AntiIdleSource f) { m_antiIdleSource = std::move(f); }

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
    void fileTransferRequested();
    void scriptRequested(const QString &path);
    void macroRequested(const QString &name);
    void logonScriptRequested(const QString &path);
    void recordToggleRequested();
    void lockChanged(bool locked);
    void sftpUploadRequested(const QStringList &paths);
    // Everything received, for scripts. Not connected: no copy is made.
    void outputReceived(const QByteArray &data);
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
    void buildModemMenu(QMenu *menu);
    void showTask(const QString &text, bool canStop);
    void closeTask(const QString &finalText = QString());
    void beginModem();
    void offerZmodem(bool receiving);
    void runLogonPassword(const QString &credential);
    void mirrorKey(const sshlx_key_event *ev, int qtKey);
    void mirrorText(const QString &text);
    void runKeyBinding(const KeyBinding &b);
    void afterLogon();
    void noteInput();
    void askAsciiFile();
    void checkLogRotation();
    QString expandedLogPath(const QDateTime &when) const;

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
    QFrame *m_taskBar;
    QLabel *m_taskLabel;
    QProgressBar *m_taskProgress;
    QPushButton *m_taskStop, *m_taskClose;
    ModemTransfer *m_modem;
    LogonRunner *m_logon;
    LogonSteps m_logonSteps;
    CredentialAccount m_credentialAccount;
    QByteArray m_zTail;              // the end of the last output, for a split header
    bool m_zOffered = false;
    bool m_locked = false, m_logPaused = false;
    QString m_group, m_logPath;
    QDateTime m_logOpened;
    QTimer *m_logTimer;
    ScriptRecorder *m_recorder = nullptr;
    ScriptPath m_logonScript;
    class AsciiSender *m_ascii;
    AntiIdleSource m_antiIdleSource;
    AntiIdle m_antiIdle;
    QTimer *m_idleTimer;
    QString m_cipherOut, m_cipherIn;
    qint64 m_received = 0;
    QDateTime m_connectedAt;
    QPointer<class HexViewDialog> m_hexView;
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
    std::function<void(const char *, size_t)> m_raw;
    QWidget *m_filePanel = nullptr;
    std::function<bool()> m_panelBusy;
    bool m_reconnectBarEnabled = true, m_loggedIn = false;
    PasswordLookup m_passwordLookup;
    SavedPasswords m_savedPasswords;
};
