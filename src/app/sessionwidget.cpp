#include "sessionwidget.h"
#include "icons.h"

#include <QApplication>
#include <QActionGroup>
#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFontDatabase>
#include <QInputDialog>
#include <QLineEdit>
#include <QTextDocument>
#ifdef SSHLX_HAVE_PRINT
#include <QPrintDialog>
#include <QPrinter>
#endif
#include <QDesktopServices>
#include <QFontDialog>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <cerrno>
#include <cstring>

#include "configpush.h"
#include "configpushbar.h"
#include "configpushdialog.h"
#include "dialogkit.h"
#include "eventlogdialog.h"
#include "findbar.h"
#include "fontspec.h"
#include "highlightruledialog.h"
#include "highlightrules.h"
#include "keyring.h"
#include "asciisender.h"
#include "hexview.h"
#include "keybindings.h"
#include "logonrunner.h"
#include "logrotate.h"
#include "macros.h"
#include "modemtransfer.h"
#include "promptbar.h"
#include "scriptrecorder.h"
#include "settingsdialog.h"
#include "terminallook.h"
#include "terminalview.h"

// ---------------------------------------------------------------------------
// Clipboard routing. PuTTY numbers clipboards per platform; on Unix PRIMARY
// and CLIPBOARD are real, CUSTOM_1..3 are named X selections that Qt cannot
// address, so they map to CLIPBOARD (ARCHITECTURE-NOTES.md section 7).
// One session at a time owns each clipboard; when another session or
// application takes it, the owner is told so it can deselect, as PuTTY does.
// ---------------------------------------------------------------------------
namespace {
QClipboard::Mode modeFor(int clip)
{
    if (clip == SSHLX_CLIP_PRIMARY &&
        QGuiApplication::clipboard()->supportsSelection())
        return QClipboard::Selection;
    return QClipboard::Clipboard;
}

struct ClipboardRouter : QObject {
    QHash<int, QPointer<SessionWidget>> owner;     // QClipboard::Mode -> owner
    QHash<int, int> ownerClip;                     // mode -> PuTTY clipboard id
    QHash<int, sshlx_session *> ownerSession;
    bool writing = false;

    ClipboardRouter()
    {
        connect(QGuiApplication::clipboard(), &QClipboard::changed, this,
                [this](QClipboard::Mode mode) {
            if (writing)
                return;
            lose(mode);
        });
    }
    void lose(QClipboard::Mode mode)
    {
        QPointer<SessionWidget> o = owner.take(mode);
        sshlx_session *s = ownerSession.take(mode);
        int clip = ownerClip.take(mode);
        if (o && s)
            sshlx_session_clipboard_lost(s, clip);
    }
    static ClipboardRouter &get()
    {
        static ClipboardRouter *r = new ClipboardRouter;
        return *r;
    }
};
} // namespace

// ---------------------------------------------------------------------------
// C callbacks
// ---------------------------------------------------------------------------
struct SessionTrampolines {
    static SessionWidget *W(void *ctx) { return static_cast<SessionWidget *>(ctx); }

    static bool begin_draw(void *ctx) { return W(ctx)->m_view->beginDraw(); }
    static void draw_text(void *ctx, int col, int row, int ncells,
                          const uint32_t *text, int len, const sshlx_text_style *st)
    { W(ctx)->m_view->drawText(col, row, ncells, text, len, st); }
    static void draw_cursor_shape(void *ctx, int col, int row, int ncells,
                                  int shape, bool passive, bool right,
                                  sshlx_rgb colour, int la)
    { W(ctx)->m_view->drawCursorShape(col, row, ncells, shape, passive, right, colour, la); }
    static void draw_trust_sigil(void *ctx, int col, int row)
    { W(ctx)->m_view->drawTrustSigil(col, row); }
    static void end_draw(void *ctx) { W(ctx)->m_view->endDraw(); }
    static void set_cursor_pos(void *ctx, int col, int row)
    { W(ctx)->m_view->setCursorCell(col, row); }
    static void set_scrollbar(void *ctx, int total, int start, int page)
    {
        SessionWidget *w = W(ctx);
        w->m_ignoreScroll = true;
        w->m_scroll->setRange(0, std::max(0, total - page));
        w->m_scroll->setPageStep(page);
        w->m_scroll->setValue(start);
        w->m_ignoreScroll = false;
    }
    static void set_raw_mouse_mode(void *, bool) {}
    static void set_raw_mouse_pointer(void *ctx, bool raw)
    { W(ctx)->m_view->setRawMousePointer(raw); }
    static void bell(void *ctx, int mode)
    {
        // BELL_DEFAULT = 1: system beep. Visual bell is drawn by the
        // terminal itself; other modes (wave file, PC speaker) are
        // Windows-only in PuTTY.
        if (mode == 1)
            QApplication::beep();
        emit W(ctx)->bellRang();
    }
    static void set_title(void *ctx, const char *t)
    {
        SessionWidget *w = W(ctx);
        w->m_serverTitle = QString::fromUtf8(t);
        if (!w->m_customTitle.isEmpty())
            return;
        w->m_title = w->m_serverTitle;
        emit w->titleChanged(w->m_title);
    }
    static void set_icon_title(void *ctx, const char *t)
    { W(ctx)->setToolTip(QString::fromUtf8(t)); }
    static void request_resize(void *ctx, int, int)
    {
        // A tab cannot change its own size: answer as PuTTY does when a
        // resize cannot be honoured (ARCHITECTURE-NOTES.md section 7).
        sshlx_session *s = W(ctx)->m_session;
        QTimer::singleShot(0, W(ctx), [s] { sshlx_session_resize_done(s); });
    }
    static void set_minimised(void *, bool) {}
    static void set_maximised(void *, bool) {}
    static void move_window(void *, int, int) {}
    static void set_zorder(void *, bool) {}
    static void refresh(void *ctx) { W(ctx)->m_view->update(); }
    static void output_data(void *ctx, const char *data, size_t len)
    {
        W(ctx)->m_received += qint64(len);
        if (W(ctx)->m_raw) {
            W(ctx)->m_raw(data, len);
            return;
        }
        SessionWidget *w = W(ctx);
        w->m_findStale = true;
        if (w->m_push->running())
            w->m_push->feed(data, len);
        if (w->m_logon->running())
            w->m_logon->feed(data, len);
        if (w->m_recorder)
            w->m_recorder->output(data, len);
        if (w->receivers(SIGNAL(outputReceived(QByteArray))) > 0)
            emit w->outputReceived(QByteArray(data, qsizetype(len)));
        // "sz" or "rz" just started on the remote side. Search across the
        // chunk boundary, then wait for a repeat of the header no more.
        if (!w->m_zOffered && !w->m_filePanel) {
            const QByteArray look = w->m_zTail + QByteArray(data, qsizetype(len));
            bool receiving = false;
            if (ModemTransfer::findZmodemStart(look, &receiving) >= 0
                && !w->m_push->running()
                && QSettings().value("transfers/zmodemAuto", true).toBool()) {
                w->m_zOffered = true;
                QTimer::singleShot(0, w, [w, receiving] { w->offerZmodem(receiving); });
            }
        }
        w->m_zTail = (w->m_zTail + QByteArray(data, qsizetype(len))).right(5);
    }
    static void palette_changed(void *ctx, sshlx_rgb bg)
    { W(ctx)->m_view->setDefaultBackground(QColor(bg.r, bg.g, bg.b)); }
    static void clip_write(void *ctx, int clip, const char *utf8, bool deselect)
    { W(ctx)->writeClipboard(clip, QString::fromUtf8(utf8), deselect); }
    static void clip_request_paste(void *ctx, int clip)
    { W(ctx)->requestPaste(clip); }
    static void state_changed(void *ctx, sshlx_state st) { W(ctx)->onState(st); }
    static void connection_fatal(void *ctx, const char *msg, int err)
    { W(ctx)->onFatal(QString::fromUtf8(msg), err); }
    static void nonfatal(void *ctx, const char *msg)
    { W(ctx)->m_lastError = QString::fromUtf8(msg); }
    static void event_log(void *ctx, const char *line)
    {
        SessionWidget *w = W(ctx);
        if (w->m_eventLog)
            w->m_eventLog->appendLine(QString::fromUtf8(line));
        // PuTTY says what it agreed on only here.
        static const QRegularExpression cipher("Initialised (.+) (outbound|inbound) encryption");
        const QRegularExpressionMatch m = cipher.match(QString::fromUtf8(line));
        if (m.hasMatch())
            (m.captured(2) == "outbound" ? w->m_cipherOut : w->m_cipherIn) = m.captured(1);
    }
    static void logging_error(void *, const char *) {}
    static void specials_changed(void *) {}
    static void busy(void *ctx, int status) { W(ctx)->m_view->setBusy(status != 0); }
    static void exited(void *ctx, int code, bool close)
    { W(ctx)->onExited(code, close); }
    static void prompt(void *ctx, const sshlx_prompt_request *req)
    { W(ctx)->onPrompt(req); }
    static void prompt_cancelled(void *ctx, sshlx_prompt_id id)
    { W(ctx)->m_prompts->cancel(id); }
    static bool window_pixel_size(void *ctx, int *w, int *h)
    {
        *w = W(ctx)->m_view->width();
        *h = W(ctx)->m_view->height();
        return true;
    }
    static bool window_id(void *ctx, long *id)
    {
        if (QGuiApplication::platformName() != "xcb")
            return false;
        *id = static_cast<long>(W(ctx)->window()->winId());
        return true;
    }
};

// ---------------------------------------------------------------------------
static QList<SessionWidget *> &allSessions()
{
    static QList<SessionWidget *> list;
    return list;
}

static SessionWidget::Mirror &mirror()
{
    static SessionWidget::Mirror m;
    return m;
}

void SessionWidget::setMirror(Mirror f)
{
    mirror() = std::move(f);
}

static HighlightSet &highlights()
{
    static HighlightSet set;
    return set;
}

int SessionWidget::protocolId(const char *name)
{
    for (size_t i = 0; i < sshlx_protocol_count(); i++)
        if (!strcmp(sshlx_protocol_get(i)->name, name))
            return sshlx_protocol_get(i)->id;
    return -1;
}

static int confInt(const sshlx_conf *c, const char *k)
{ return sshlx_conf_get_int(c, sshlx_conf_key_find(k)); }
static bool confBool(const sshlx_conf *c, const char *k)
{ return sshlx_conf_get_bool(c, sshlx_conf_key_find(k)); }
static QString confStr(const sshlx_conf *c, const char *k)
{
    const char *s = sshlx_conf_get_str(c, sshlx_conf_key_find(k));
    return QString::fromUtf8(s ? s : "");
}

SessionWidget::SessionWidget(const sshlx_conf *conf, const QString &displayName,
                             QWidget *parent)
    : QWidget(parent), m_displayName(displayName)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    m_prompts = new PromptBar;
    v->addWidget(m_prompts);

    m_reconnectBar = new QFrame;
    m_reconnectBar->setObjectName("ReconnectBar");
    m_reconnectBar->setFrameShape(QFrame::StyledPanel);
    auto *rb = new QHBoxLayout(m_reconnectBar);
    m_reconnectLabel = new QLabel;
    m_reconnectLabel->setWordWrap(true);
    m_reconnectLabel->setTextFormat(Qt::RichText);
    m_reconnectButton = new QPushButton(Icons::get("reconnect"), tr("&Reconnect"));
    m_reconnectButton->setProperty("role", "primary");
    auto *closeButton = new QPushButton(tr("Close tab"));
    rb->addWidget(m_reconnectLabel, 1);
    rb->addWidget(m_reconnectButton);
    rb->addWidget(closeButton);
    m_reconnectBar->setVisible(false);
    v->addWidget(m_reconnectBar);

    m_push = new ConfigPush(this);
    m_pushBar = new ConfigPushBar(m_push);
    v->addWidget(m_pushBar);

    m_findBar = new FindBar;
    v->addWidget(m_findBar);

    // A status line for what runs in this tab on its own: logon actions and
    // file transfers through the terminal.
    m_taskBar = new QFrame;
    m_taskBar->setObjectName("TaskBar");
    m_taskBar->setFrameShape(QFrame::StyledPanel);
    auto *tb = new QHBoxLayout(m_taskBar);
    tb->setContentsMargins(8, 3, 8, 3);
    m_taskLabel = new QLabel;
    m_taskProgress = new QProgressBar;
    m_taskProgress->setMaximumWidth(220);
    m_taskProgress->setMaximumHeight(14);
    m_taskProgress->setTextVisible(false);
    m_taskStop = new QPushButton(tr("&Stop"));
    m_taskClose = new QPushButton(tr("Close"));
    tb->addWidget(m_taskLabel, 1);
    tb->addWidget(m_taskProgress);
    tb->addWidget(m_taskStop);
    tb->addWidget(m_taskClose);
    m_taskBar->setVisible(false);
    v->addWidget(m_taskBar);
    m_modem = new ModemTransfer(this);
    m_logon = new LogonRunner(this);
    m_modem->setBacklog([this] { return sendBacklog(); });
    m_ascii = new AsciiSender(this);
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);

    m_termRow = new QHBoxLayout;
    m_termRow->setSpacing(0);
    m_view = new TerminalView;
    m_view->setAccessibleName(tr("Terminal"));
    m_scroll = new QScrollBar(Qt::Vertical);
    m_termRow->addWidget(m_view, 1);
    m_termRow->addWidget(m_scroll);
    v->addLayout(m_termRow, 1);
    setFocusProxy(m_view);

    connect(m_reconnectButton, &QPushButton::clicked, this, &SessionWidget::reconnect);
    connect(closeButton, &QPushButton::clicked, this, &SessionWidget::closeRequested);
    connect(m_findBar, &FindBar::queryChanged, this, &SessionWidget::runFind);
    connect(m_findBar, &FindBar::next, this, [this] { stepFind(+1); });
    connect(m_findBar, &FindBar::previous, this, [this] { stepFind(-1); });
    connect(m_findBar, &FindBar::closed, this, &SessionWidget::closeFind);
    connect(m_scroll, &QScrollBar::valueChanged, this, [this](int v) {
        // The marked match moves with the text when the view is scrolled.
        if (m_matchIndex >= 0 && m_matchIndex < m_matches.size()) {
            const FindMatch &m = m_matches[m_matchIndex];
            const int row = m.row - v;
            m_view->setFindCurrent(row >= 0 && row < m_view->rows() ? row : -1, m.start, m.len);
        }
        if (!m_ignoreScroll && m_session)
            sshlx_session_scroll(m_session, +1, v);
    });
    connect(m_view, &TerminalView::fontSizeStep, this, &SessionWidget::fontStep);
    connect(m_view, &TerminalView::rightClicked, this, &SessionWidget::onRightClick);
    connect(m_view, &TerminalView::linkActivated, this, [](const QString &url) {
        QDesktopServices::openUrl(QUrl(url));
    });
    connect(m_push, &ConfigPush::send, this, [this](const QString &line) {
        sshlx_session_text(m_session, (line + '\r').toUtf8().constData());
    });
    connect(m_push, &ConfigPush::sendKeys, this, [this](const QString &keys) {
        sshlx_session_text(m_session, keys.toUtf8().constData());
    });
    connect(m_view, &TerminalView::filesDropped, this, &SessionWidget::handleDroppedFiles);
    connect(m_ascii, &AsciiSender::send, this, &SessionWidget::sendRaw);
    connect(m_ascii, &AsciiSender::progress, this, [this](int line, int total) {
        m_taskLabel->setText(tr("Sending text: line %1 of %2").arg(line).arg(total));
        m_taskProgress->setRange(0, total);
        m_taskProgress->setValue(line);
    });
    connect(m_ascii, &AsciiSender::finished, this, [this](bool complete) {
        m_view->setInputBlocked(false);
        closeTask(complete ? tr("Text file sent.") : tr("Sending stopped."));
        m_view->setFocus();
    });
    connect(m_idleTimer, &QTimer::timeout, this, [this] {
        if (canSendCommands() && !m_antiIdle.text.isEmpty())
            sshlx_session_text(m_session, KeyBindings::decodeText(m_antiIdle.text).toUtf8().constData());
        noteInput();
    });
    connect(m_view, &TerminalView::keySent, this, &SessionWidget::noteInput);
    connect(m_view, &TerminalView::keySent, this, &SessionWidget::mirrorKey);
    connect(m_view, &TerminalView::textSent, this, &SessionWidget::mirrorText);
    connect(m_view, &TerminalView::keyBound, this, &SessionWidget::runKeyBinding);
    m_logTimer = new QTimer(this);
    m_logTimer->setInterval(5000);
    connect(m_logTimer, &QTimer::timeout, this, &SessionWidget::checkLogRotation);
    m_logTimer->start();
    connect(m_taskClose, &QPushButton::clicked, m_taskBar, &QWidget::hide);
    connect(m_taskStop, &QPushButton::clicked, this, [this] {
        if (m_modem->running())
            m_modem->cancel();
        else if (m_ascii->running())
            m_ascii->stop();
        else
            m_logon->abort(tr("Stopped."));
    });
    connect(m_modem, &ModemTransfer::send, this, &SessionWidget::sendRaw);
    connect(m_modem, &ModemTransfer::progress, this,
            [this](const QString &file, qint64 done, qint64 total) {
        const QString protocol = tr("%1: ").arg(m_taskLabel->property("protocol").toString());
        m_taskLabel->setText(total > 0 ? protocol + tr("%1, %2 of %3 KB").arg(file).arg(done / 1024).arg(total / 1024)
                                       : protocol + tr("%1, %2 KB").arg(file).arg(done / 1024));
        m_taskProgress->setRange(0, total > 0 ? 1000 : 0);
        if (total > 0)
            m_taskProgress->setValue(int(done * 1000 / total));
    });
    connect(m_modem, &ModemTransfer::finished, this, [this](bool ok, const QString &message) {
        setRawReceiver(nullptr);
        m_view->setInputBlocked(false);
        m_zOffered = false;
        m_zTail.clear();
        closeTask(ok ? message : tr("Transfer failed: %1").arg(message));
        m_view->setFocus();
    });
    connect(m_logon, &LogonRunner::send, this, [this](const QString &line) {
        sshlx_session_text(m_session, (line + '\r').toUtf8().constData());
    });
    connect(m_logon, &LogonRunner::sendPassword, this, &SessionWidget::runLogonPassword);
    connect(m_logon, &LogonRunner::progress, this,
            [this](int step, int total, const QString &wait) {
        showTask(wait.isEmpty() ? tr("Logon actions: step %1 of %2").arg(step).arg(total)
                                : tr("Logon actions: step %1 of %2, waiting for \"%3\"")
                                      .arg(step).arg(total).arg(wait),
                 true);
        m_taskProgress->setRange(0, total);
        m_taskProgress->setValue(step - 1);
    });
    connect(m_logon, &LogonRunner::finished, this, [this](bool ok, const QString &message) {
        if (ok) {
            m_taskBar->hide();
            afterLogon();
        } else {
            closeTask(tr("Logon actions stopped: %1").arg(message));
        }
    });
    connect(m_pushBar, &ConfigPushBar::inputBlockChanged, m_view, &TerminalView::setInputBlocked);
    connect(m_pushBar, &ConfigPushBar::passwordMenuRequested, this, &SessionWidget::buildPasswordMenu);
    connect(m_pushBar, &ConfigPushBar::closed, m_view, [this] { m_view->setFocus(); });
    connect(m_prompts, &PromptBar::answered, this,
            [this](sshlx_prompt_id id, sshlx_answer a) {
        if (m_session)
            sshlx_session_answer_prompt(m_session, id, a);
        if (!m_prompts->hasPending())
            m_view->setFocus();
    });
    connect(m_prompts, &PromptBar::passwordAnswered, this,
            [this](sshlx_prompt_id id, const QString &pw, bool provided) {
        if (!m_session)
            return;
        QByteArray b = pw.toUtf8();
        sshlx_session_answer_password(m_session, id, provided ? b.constData() : nullptr);
        b.fill('\0');
        m_view->setFocus();
    });

    sshlx_session_callbacks cb{};
    cb.ctx = this;
#define T(name) cb.name = &SessionTrampolines::name
    T(begin_draw); T(draw_text); T(draw_cursor_shape); T(draw_trust_sigil);
    T(end_draw); T(set_cursor_pos); T(set_scrollbar); T(set_raw_mouse_mode);
    T(set_raw_mouse_pointer); T(bell); T(set_title); T(set_icon_title);
    T(request_resize); T(set_minimised); T(set_maximised); T(move_window);
    T(set_zorder); T(refresh); T(output_data); T(palette_changed); T(clip_write);
    T(clip_request_paste); T(state_changed); T(connection_fatal); T(nonfatal);
    T(event_log); T(logging_error); T(specials_changed); T(busy); T(exited);
    T(prompt); T(prompt_cancelled); T(window_pixel_size); T(window_id);
#undef T

    int cols = confInt(conf, "TermWidth"), rows = confInt(conf, "TermHeight");
    m_session = sshlx_session_new(conf, &cb, cols, rows);
    if (allSessions().isEmpty())
        highlights().setRules(HighlightSet::load());
    allSessions().append(this);
    applyMousePrefs();
    m_view->setHighlights(HighlightSet::enabled() ? &highlights() : nullptr);
    m_view->setInitialGrid(cols, rows);
    m_view->setSession(m_session);
    m_title = m_serverTitle = displayName;
    applyConfToView();
    m_view->setDefaultBackground(QColor(sshlx_session_default_bg(m_session).r,
                                        sshlx_session_default_bg(m_session).g,
                                        sshlx_session_default_bg(m_session).b));
}

SessionWidget::~SessionWidget()
{
    allSessions().removeAll(this);
    delete m_recorder;
    if (m_eventLog)
        m_eventLog->deleteLater();
    if (m_session) {
        m_view->setSession(nullptr);
        sshlx_session_free(m_session);   // deferred inside the engine
        m_session = nullptr;
    }
}

void SessionWidget::applyConfToView()
{
    const sshlx_conf *c = sshlx_session_conf(m_session);
    TerminalFonts f;
    f.normal = confStr(c, "Font");
    f.bold = confStr(c, "BoldFont");
    f.wide = confStr(c, "WideFont");
    f.wideBold = confStr(c, "WideBoldFont");
    f.shadowBold = confBool(c, "ShadowBold");
    f.shadowBoldOffset = confInt(c, "ShadowBoldOffset");
    f.border = confInt(c, "WindowBorder");
    m_view->setFonts(f);
    m_scroll->setVisible(confBool(c, "ScrollBar"));
    bool left = confBool(c, "ScrollbarOnLeft");
    m_termRow->removeWidget(m_scroll);
    m_termRow->insertWidget(left ? 0 : 1, m_scroll);
}

const sshlx_conf *SessionWidget::conf() const
{
    return sshlx_session_conf(m_session);
}

int SessionWidget::protocol() const
{
    return sshlx_conf_get_int(conf(), sshlx_conf_key_protocol());
}

QIcon SessionWidget::iconForProtocol(int proto)
{
    QString name;
    for (size_t i = 0; i < sshlx_protocol_count(); i++)
        if (sshlx_protocol_get(i)->id == proto)
            name = QString::fromUtf8(sshlx_protocol_get(i)->name);
    if (name.isEmpty())
        name = "raw";
    return Icons::get("proto-" + name);
}

QIcon SessionWidget::protocolIcon() const
{
    return iconForProtocol(protocol());
}

sshlx_state SessionWidget::state() const
{
    return sshlx_session_state(m_session);
}

void SessionWidget::connectSession()
{
    m_lastError.clear();
    m_lastErrno = 0;
    updateReconnectBar();
    sshlx_session_connect(m_session);
    updateReconnectBar();
}

void SessionWidget::disconnectSession()
{
    sshlx_session_disconnect(m_session);
}

void SessionWidget::reconnect()
{
    m_lastError.clear();
    m_lastErrno = 0;
    if (sshlx_session_state(m_session) == SSHLX_STATE_IDLE)
        sshlx_session_connect(m_session);
    else
        sshlx_session_reconnect(m_session);
    updateReconnectBar();
    m_view->setFocus();
}

QString SessionWidget::closeWarning() const
{
    if (m_push->running())
        return tr("A configuration is still being sent to this device.");
    if (m_modem->running())
        return tr("A file is still being transferred.");
    if (m_panelBusy && m_panelBusy())
        return tr("Files are still being transferred.");
    if (isFileTransfer())
        return {};
    char *w = sshlx_session_close_warning(m_session);
    if (!w)
        return {};
    QString s = QString::fromUtf8(w);
    sshlx_free(w);
    return s;
}

void SessionWidget::setRawReceiver(std::function<void(const char *, size_t)> f)
{
    m_raw = std::move(f);
    sshlx_session_set_raw(m_session, bool(m_raw));
}

void SessionWidget::sendRaw(const QByteArray &data)
{
    sshlx_session_send_bytes(m_session, data.constData(), size_t(data.size()));
}

bool SessionWidget::sendReady() const { return sshlx_session_send_ready(m_session); }

size_t SessionWidget::sendBacklog() const { return sshlx_session_send_backlog(m_session); }

void SessionWidget::setFilePanel(QWidget *panel, std::function<bool()> busy)
{
    m_filePanel = panel;
    m_panelBusy = std::move(busy);
    panel->setParent(this);
    panel->hide();
    qobject_cast<QVBoxLayout *>(layout())->addWidget(panel, 1);
}

void SessionWidget::showFilePanel(bool show)
{
    if (!m_filePanel)
        return;
    m_view->setVisible(!show);
    m_scroll->setVisible(!show);
    m_filePanel->setVisible(show);
    // Hidden widgets still leave their row its share of the height.
    qobject_cast<QVBoxLayout *>(layout())->setStretchFactor(m_termRow, show ? 0 : 1);
}

void SessionWidget::onState(sshlx_state st)
{
    if (st == SSHLX_STATE_CONNECTING)
        m_loggedIn = false;
    else if (st == SSHLX_STATE_CONNECTED)
        m_loggedIn = true;
    if (st == SSHLX_STATE_DISCONNECTED) {
        m_prompts->clearAll();
        m_push->connectionLost();
        m_modem->connectionLost();
        m_ascii->stop();
        m_idleTimer->stop();
        m_connectedAt = QDateTime();
        if (m_logon->running())
            m_logon->abort(tr("The connection was lost."));
    } else if (st == SSHLX_STATE_CONNECTED && !m_filePanel) {
        const QList<LogonStep> steps = m_logonSteps ? m_logonSteps() : QList<LogonStep>();
        m_logOpened = QDateTime::currentDateTime();
        m_logPath = expandedLogPath(m_logOpened);
        m_connectedAt = m_logOpened;
        m_antiIdle = m_antiIdleSource ? m_antiIdleSource() : AntiIdle();
        noteInput();
        if (!steps.isEmpty())
            m_logon->start(steps);
        else
            QTimer::singleShot(0, this, &SessionWidget::afterLogon);   // not inside the engine
    }
    updateReconnectBar();
    emit stateChanged(st);
}

void SessionWidget::onFatal(const QString &msg, int err)
{
    m_lastError = msg;
    m_lastErrno = err;
    updateReconnectBar();
}

void SessionWidget::onExited(int code, bool closeRequested)
{
    if (m_lastError.isEmpty())
        m_lastError = tr("Session ended (exit code %1).").arg(code);
    updateReconnectBar();
    QSettings st;
    // PuTTY closes the window here; sshLX keeps the tab unless the user
    // opted into PuTTY's behaviour. A local shell left with "exit" closes,
    // as in any terminal.
    const bool local = protocol() == protocolId("localshell");
    if (closeRequested && (st.value("behaviour/closeTabOnExit", false).toBool()
                           || (local && code == 0)))
        emit exitedWithClose();
}

void SessionWidget::updateReconnectBar()
{
    bool disc = m_session &&
        sshlx_session_state(m_session) == SSHLX_STATE_DISCONNECTED;
    if (!disc || !m_reconnectBarEnabled) {
        m_reconnectBar->setVisible(false);
        return;
    }
    const bool local = protocol() == protocolId("localshell");
    m_reconnectButton->setText(local ? tr("&Restart") : tr("&Reconnect"));
    QString text = local ? tr("<b>Shell ended.</b>") : tr("<b>Disconnected.</b>");
    if (!m_lastError.isEmpty())
        text += " " + m_lastError.toHtmlEscaped().replace("\n", "<br>");
    if (m_lastErrno == EACCES && protocol() == protocolId("serial")) {
        // Serial line permission problem: explain the usual fix.
        text += tr("<br><br>Your user is not allowed to open this serial "
                   "device. On most Linux distributions serial ports belong "
                   "to the <b>dialout</b> group (<b>uucp</b> on Arch and "
                   "some others). Add yourself with<br>"
                   "<tt>sudo usermod -aG dialout $USER</tt> (or "
                   "<tt>uucp</tt>), then log out and back in.");
    }
    m_reconnectLabel->setText(text);
    m_reconnectBar->setVisible(true);
}

void SessionWidget::onPrompt(const sshlx_prompt_request *req)
{
    if (req->kind == SSHLX_PROMPT_PASSWORD && m_passwordLookup) {
        const sshlx_conf *c = conf();
        QString user = confStr(c, "UserName");
        QString host = QString::fromUtf8(req->host ? req->host : "");
        int port = req->port;
        sshlx_prompt_id id = req->id;
        QPointer<SessionWidget> self(this);
        m_passwordLookup(user, host, port, req->passphrase, [self, id](bool found, const QString &pw) {
            if (!self || !self->m_session)
                return;
            QByteArray b = pw.toUtf8();
            sshlx_session_answer_password(self->m_session, id,
                                          found ? b.constData() : nullptr);
            b.fill('\0');
        });
        return;
    }
    m_prompts->addRequest(req);
}

void SessionWidget::setPasswordLookup(PasswordLookup f)
{
    m_passwordLookup = std::move(f);
    sshlx_session_set_password_hook(m_session, bool(m_passwordLookup));
}

void SessionWidget::writeClipboard(int clip, const QString &text, bool)
{
    ClipboardRouter &r = ClipboardRouter::get();
    QClipboard::Mode mode = modeFor(clip);
    if (r.owner.value(mode) && r.owner.value(mode) != this)
        r.lose(mode);
    r.writing = true;
    QGuiApplication::clipboard()->setText(text, mode);
    r.writing = false;
    r.owner[mode] = this;
    r.ownerClip[mode] = clip;
    r.ownerSession[mode] = m_session;
}

void SessionWidget::requestPaste(int clip)
{
    QClipboard::Mode mode = modeFor(clip);
    QPointer<SessionWidget> self(this);
    // Deliver asynchronously, as GTK's clipboard request does.
    QTimer::singleShot(0, this, [self, mode] {
        if (!self || !self->m_session)
            return;
        self->deliverPaste(QGuiApplication::clipboard()->text(mode));
    });
}

// Several lines would run as commands the moment they land, unless the
// program has bracketed paste on. A stray right click must not do that.
void SessionWidget::deliverPaste(const QString &text)
{
    if (m_push->running() || m_locked)
        return;
    int lines = 0;
    for (const QString &l : text.split('\n'))
        if (!l.trimmed().isEmpty())
            lines++;
    if (lines > 1 && !sshlx_session_bracketed_paste(m_session) &&
        QSettings().value("terminal/confirmMultilinePaste", true).toBool()) {
        QPointer<SessionWidget> self(this);
        int choice = DialogKit::message(
            window(), DialogKit::Kind::Question, tr("Paste %1 lines?").arg(lines),
            tr("Each line runs as soon as it arrives. To send them one at a time and stop "
               "on errors, use Paste Configuration."),
            {tr("Cancel"), tr("Paste All at Once"), tr("Paste Configuration...")}, 2,
            text.left(4000));
        if (!self || choice == 0 || choice < 0)
            return;
        if (choice == 2) {
            pasteConfiguration(text);
            return;
        }
    }
    sshlx_session_paste(m_session, text.toUtf8().constData());
    if (m_recorder)
        m_recorder->typed(text);
}

void SessionWidget::pasteConfiguration(const QString &text)
{
    if (m_push->running() || m_locked)
        return;
    if (state() != SSHLX_STATE_CONNECTED) {
        DialogKit::warning(window(), tr("Paste Configuration"), tr("This session is not connected."));
        return;
    }
    if (m_pushDialog) {
        if (!text.isNull())
            m_pushDialog->setText(text);
        m_pushDialog->raise();
        m_pushDialog->activateWindow();
        return;
    }
    auto *dlg = new ConfigPushDialog(text.isNull() ? QGuiApplication::clipboard()->text() : text,
                                     m_title, window());
    m_pushDialog = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, this, [this, dlg] {
        if (state() != SSHLX_STATE_CONNECTED) {
            DialogKit::warning(window(), tr("Paste Configuration"),
                               tr("This session is not connected."));
        } else if (!m_push->running() && !m_locked) {
            m_push->start(dlg->lines(), dlg->options());
            m_view->setFocus();
        }
    });
    dlg->show();
}

void SessionWidget::findInTerminal()
{
    m_findStale = true;
    m_findBar->open();
    if (!m_findBar->text().isEmpty())
        runFind();
}

// PuTTY keeps its scrollback to itself, so read it the way the screen gets
// it: have the engine draw each page, without painting, and take the text.
void SessionWidget::refreshFindIndex()
{
    if (!m_findStale)
        return;
    const int top = m_scroll->value(), page = m_view->rows();
    const int total = m_scroll->maximum() + page;
    m_findRows.clear();
    m_view->setCapturing(true);
    for (int y = 0; y < total; y += page) {
        sshlx_session_show_line(m_session, y);
        for (int r = 0; r < page && y + r < total; r++)
            m_findRows.append(m_view->rowText(r));
    }
    m_view->setCapturing(false);
    sshlx_session_show_line(m_session, top);
    m_findStale = false;
}

void SessionWidget::runFind()
{
    const QString text = m_findBar->text();
    m_view->setFind(text, m_findBar->matchCase());
    m_matches.clear();
    m_matchIndex = -1;
    m_view->setFindCurrent(-1, 0, 0);
    if (text.isEmpty()) {
        m_findBar->setResult(QString());
        return;
    }
    refreshFindIndex();
    HighlightRule rule;
    rule.pattern = text;
    rule.matchCase = m_findBar->matchCase();
    rule.wholeWord = false;
    const QRegularExpression re = HighlightSet::compile(rule);
    const int bottom = m_scroll->value() + m_view->rows() - 1;
    for (int r = 0; r < m_findRows.size(); r++) {
        for (auto it = re.globalMatch(m_findRows[r]); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            m_matches.append({r, int(m.capturedStart()), int(m.capturedLength())});
            if (r <= bottom)
                m_matchIndex = m_matches.size() - 1;   // the nearest above what is shown
        }
    }
    if (m_matches.isEmpty()) {
        m_findBar->setResult(tr("No matches"));
        return;
    }
    if (m_matchIndex < 0)
        m_matchIndex = 0;
    showMatch();
}

void SessionWidget::stepFind(int delta)
{
    if (m_findStale)
        runFind();
    if (m_matches.isEmpty())
        return;
    m_matchIndex = (m_matchIndex + delta + m_matches.size()) % m_matches.size();
    showMatch();
}

void SessionWidget::showMatch()
{
    const FindMatch &m = m_matches[m_matchIndex];
    const int page = m_view->rows();
    int top = m_scroll->value();
    if (m.row < top || m.row >= top + page) {
        top = qBound(0, m.row - page / 2, m_scroll->maximum());
        sshlx_session_show_line(m_session, top);
    }
    m_view->setFindCurrent(m.row - top, m.start, m.len);
    m_findBar->setResult(tr("%1 of %2").arg(m_matchIndex + 1).arg(m_matches.size()));
}

void SessionWidget::closeFind()
{
    m_findBar->hide();
    m_matches.clear();
    m_matchIndex = -1;
    m_view->setFind(QString(), false);
    m_view->setFindCurrent(-1, 0, 0);
    sshlx_session_show_line(m_session, m_scroll->maximum());
    m_view->setFocus();
}

void SessionWidget::applyMousePrefs()
{
    QSettings st;
    sshlx_session_set_mouse_ui(m_session, st.value("terminal/copyOnSelect", true).toBool(),
                               st.value("terminal/rightClick", "paste").toString() != "extend");
}

void SessionWidget::reloadMousePrefs()
{
    for (SessionWidget *s : allSessions())
        s->applyMousePrefs();
}

static QString &adHocWord()
{
    static QString word;
    return word;
}

void SessionWidget::setAdHocHighlight(const QString &text)
{
    adHocWord() = text;
    reloadHighlights();
}

void SessionWidget::reloadHighlights()
{
    QList<HighlightRule> rules;
    if (HighlightSet::enabled())
        rules = HighlightSet::load();
    if (!adHocWord().isEmpty()) {
        HighlightRule r;
        r.pattern = adHocWord();
        r.wholeWord = false;
        r.fg = QStringLiteral("#000000");
        r.bg = QStringLiteral("#ffd54f");
        rules.prepend(r);      // first, so it wins over any keyword
    }
    highlights().setRules(rules);
    for (SessionWidget *s : allSessions())
        s->m_view->setHighlights(rules.isEmpty() ? nullptr : &highlights());
}

void SessionWidget::addKeyword()
{
    HighlightRule rule;
    char *text = sshlx_session_selection_text(m_session);
    rule.pattern = QString::fromUtf8(text ? text : "").section('\n', 0, 0).trimmed();
    sshlx_free(text);
    QPointer<SessionWidget> self(this);
    HighlightRuleDialog dlg(rule, window());
    if (dlg.exec() != QDialog::Accepted)
        return;
    QList<HighlightRule> rules = HighlightSet::load();
    rules.prepend(dlg.rule());    // above the generic patterns, or they would take it
    HighlightSet::save(rules);
    HighlightSet::setEnabled(true);
    reloadHighlights();
}

void SessionWidget::onRightClick(const QPoint &globalPos, const QPoint &cell, bool ctrl)
{
    if (!ctrl && QSettings().value("terminal/rightClick", "paste").toString() == "paste") {
        paste();
        return;
    }
    QMenu menu;
    buildContextMenu(&menu, m_view->linkAt(cell));
    menu.exec(globalPos);
}

void SessionWidget::fontStep(int delta)
{
    changeConf([delta](sshlx_conf *c) {
        for (const char *k : {"Font", "BoldFont", "WideFont", "WideBoldFont"}) {
            int key = sshlx_conf_key_find(k);
            QString name = QString::fromUtf8(sshlx_conf_get_str(c, key));
            if (!name.isEmpty())
                sshlx_conf_set_str(c, key, FontSpec::resized(name, delta).toUtf8().constData());
        }
    });
}

void SessionWidget::changeConf(const std::function<void(sshlx_conf *)> &edit)
{
    sshlx_conf *c = sshlx_conf_copy(conf());
    edit(c);
    sshlx_session_reconfigure(m_session, c);
    sshlx_conf_free(c);
    applyConfToView();
}

void SessionWidget::buildAppearanceMenu(QMenu *menu)
{
    QPointer<SessionWidget> self(this);

    QMenu *schemes = menu->addMenu(Icons::get("palette"), tr("&Colour Scheme"));
    auto *group = new QActionGroup(schemes);
    const int current = TerminalLook::currentScheme(conf());
    const QList<TerminalLook::Scheme> list = TerminalLook::schemes();
    for (int i = 0; i < list.size(); i++) {
        const TerminalLook::Scheme &s = list[i];
        QAction *a = schemes->addAction(TerminalLook::swatch(s), s.name, this, [self, s] {
            if (self)
                self->changeConf([s](sshlx_conf *c) { TerminalLook::applyScheme(c, s); });
        });
        a->setCheckable(true);
        a->setChecked(i == current);
        // With an icon, styles draw no check mark: show the current one in bold.
        if (i == current) {
            QFont f = a->font();
            f.setBold(true);
            a->setFont(f);
        }
        group->addAction(a);
    }
    if (current < 0) {
        schemes->addSeparator();
        QAction *custom = schemes->addAction(tr("Custom (from settings)"));
        custom->setCheckable(true);
        custom->setChecked(true);
        custom->setEnabled(false);
    }
    schemes->addSeparator();
    schemes->addAction(tr("&Save Colours as Scheme..."), this, [self] {
        if (!self)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(self->window(), tr("Save Colour Scheme"),
                                                   tr("Scheme name:"), QLineEdit::Normal,
                                                   QString(), &ok).trimmed();
        if (!ok || name.isEmpty() || !self)
            return;
        if (!TerminalLook::saveUserScheme(TerminalLook::schemeFromConf(self->conf(), name)))
            DialogKit::warning(self->window(), tr("Save Colour Scheme"),
                               tr("\"%1\" is a built-in scheme. Choose another name.").arg(name));
    });
    schemes->addAction(tr("&Import Scheme..."), this, [self] {
        if (!self)
            return;
        const QString path = QFileDialog::getOpenFileName(self->window(), tr("Import Colour Scheme"), {},
                                                          tr("sshLX colour schemes (*.json)"));
        if (path.isEmpty() || !self)
            return;
        TerminalLook::Scheme s;
        QString error;
        if (!TerminalLook::importScheme(path, &s, &error) || !TerminalLook::saveUserScheme(s))
            DialogKit::error(self->window(), tr("Import Colour Scheme"),
                             error.isEmpty() ? tr("A built-in scheme has that name.") : error);
        else
            self->changeConf([s](sshlx_conf *c) { TerminalLook::applyScheme(c, s); });
    });
    schemes->addAction(tr("E&xport Colours..."), this, [self] {
        if (!self)
            return;
        const QString path = QFileDialog::getSaveFileName(self->window(), tr("Export Colour Scheme"),
                                                          QStringLiteral("scheme.json"),
                                                          tr("sshLX colour schemes (*.json)"));
        if (path.isEmpty() || !self)
            return;
        QString error;
        const TerminalLook::Scheme s = TerminalLook::schemeFromConf(self->conf(), QFileInfo(path).completeBaseName());
        if (!TerminalLook::exportScheme(s, path, &error))
            DialogKit::error(self->window(), tr("Export Colour Scheme"), error);
    });
    const QList<TerminalLook::Scheme> mine = TerminalLook::userSchemes();
    if (!mine.isEmpty()) {
        QMenu *del = schemes->addMenu(tr("&Delete Scheme"));
        for (const TerminalLook::Scheme &s : mine)
            del->addAction(s.name, this, [n = s.name] { TerminalLook::removeUserScheme(n); });
    }

    const QFont font = FontSpec::fromPutty(
        QString::fromUtf8(sshlx_conf_get_str(conf(), sshlx_conf_key_find("Font")))).font;
    QMenu *text = menu->addMenu(tr("&Text"));
    text->addAction(tr("&Larger"), this, [this] { fontStep(+1); });
    text->addAction(tr("&Smaller"), this, [this] { fontStep(-1); });
    text->addSeparator();
    text->addAction(tr("&Font... (%1, %2 pt)").arg(font.family()).arg(font.pointSize()), this,
                    [self, font] {
        if (!self)
            return;
        bool ok = false;
        QFont f = QFontDialog::getFont(&ok, font, self->window(), tr("Terminal Font"),
                                       QFontDialog::MonospacedFonts);
        if (!ok || !self)
            return;
        const QString name = FontSpec::toPutty(f);
        self->changeConf([name](sshlx_conf *c) {
            sshlx_conf_set_str(c, sshlx_conf_key_find("Font"), name.toUtf8().constData());
            // Bold variants of the old font would no longer match.
            sshlx_conf_set_str(c, sshlx_conf_key_find("BoldFont"), "");
            sshlx_conf_set_str(c, sshlx_conf_key_find("WideBoldFont"), "");
        });
    });

    QMenu *cursor = menu->addMenu(tr("C&ursor"));
    auto *shapes = new QActionGroup(cursor);
    const int type = sshlx_conf_get_int(conf(), sshlx_conf_key_find("cursor_type"));
    const QStringList names{tr("&Block"), tr("&Underline"), tr("&Vertical Line")};
    for (int i = 0; i < names.size(); i++) {
        QAction *a = cursor->addAction(names[i], this, [self, i] {
            if (self)
                self->changeConf([i](sshlx_conf *c) {
                    sshlx_conf_set_int(c, sshlx_conf_key_find("cursor_type"), i);
                });
        });
        a->setCheckable(true);
        a->setChecked(type == i);
        shapes->addAction(a);
    }
    cursor->addSeparator();
    QAction *blink = cursor->addAction(tr("Blin&king"));
    blink->setCheckable(true);
    blink->setChecked(sshlx_conf_get_bool(conf(), sshlx_conf_key_find("blink_cur")));
    connect(blink, &QAction::toggled, this, [self](bool on) {
        if (self)
            self->changeConf([on](sshlx_conf *c) {
                sshlx_conf_set_bool(c, sshlx_conf_key_find("blink_cur"), on);
            });
    });

    menu->addSeparator();
    QAction *save = menu->addAction(Icons::get("save"), tr("Save to &Connection"), this,
                                    &SessionWidget::saveAppearanceRequested);
    save->setEnabled(!m_connectionId.isEmpty());
    if (m_connectionId.isEmpty())
        save->setToolTip(tr("This tab was not opened from the connection tree."));
    menu->addAction(tr("Use for &New Sessions"), this,
                    &SessionWidget::defaultAppearanceRequested);
}

void SessionWidget::copy() { sshlx_session_copy(m_session, SSHLX_CLIP_CLIPBOARD); }
void SessionWidget::paste() { sshlx_session_request_paste(m_session, SSHLX_CLIP_CLIPBOARD); }
void SessionWidget::copyAll() { sshlx_session_copy_all(m_session); }
void SessionWidget::selectAll() { sshlx_session_select_all(m_session); }
void SessionWidget::clearTerminal() { sshlx_session_clear_terminal(m_session); }

QString SessionWidget::allText()
{
    char *text = sshlx_session_all_text(m_session);
    QString s = QString::fromUtf8(text ? text : "");
    sshlx_free(text);
    return s;
}

void SessionWidget::saveTerminalText()
{
    const QString path = QFileDialog::getSaveFileName(window(), tr("Save Terminal Text"),
                                                      QString(m_title).replace('/', '-') + QStringLiteral(".txt"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) ||
        f.write(allText().toUtf8() + '\n') < 0)
        DialogKit::error(window(), tr("Save Terminal Text"), f.errorString());
}

#ifdef SSHLX_HAVE_PRINT
void SessionWidget::printTo(QPrinter *printer)
{
    QTextDocument doc(allText());
    doc.setDefaultFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    doc.print(printer);
}

void SessionWidget::printTerminalText()
{
    QPrinter printer;
    QPrintDialog dlg(&printer, window());
    if (dlg.exec() == QDialog::Accepted)
        printTo(&printer);
}
#endif

void SessionWidget::changeTitle()
{
    bool ok = false;
    const QString text = QInputDialog::getText(window(), tr("Change Terminal Title"),
                                               tr("Title (empty to follow the server again):"),
                                               QLineEdit::Normal, m_customTitle, &ok);
    if (ok)
        setCustomTitle(text);
}

void SessionWidget::setCustomTitle(const QString &text)
{
    m_customTitle = text.trimmed();
    m_title = m_customTitle.isEmpty() ? m_serverTitle : m_customTitle;
    emit titleChanged(m_title);
}

void SessionWidget::runMacro(const QString &text)
{
    if (m_push->running() || m_locked)
        return;
    QStringList lines = text.split('\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.size() > 1)
        pasteConfiguration(lines.join('\n'));
    else if (!lines.isEmpty()) {
        sshlx_session_text(m_session, (lines.first() + '\r').toUtf8().constData());
        if (m_recorder)
            m_recorder->typed(lines.first() + '\r');
    }
}

bool SessionWidget::canSendCommands() const
{
    return state() == SSHLX_STATE_CONNECTED && !m_filePanel && !m_push->running()
        && !m_modem->running() && !m_ascii->running() && !m_locked;
}

void SessionWidget::sendCommand(const QString &line)
{
    if (!canSendCommands())
        return;
    sshlx_session_text(m_session, (line + '\r').toUtf8().constData());
    if (m_recorder)
        m_recorder->typed(line + '\r');
}

void SessionWidget::sendCommands(const QStringList &lines)
{
    if (!canSendCommands())
        return;
    m_push->start(lines, ConfigPush::Options());
    if (m_recorder) {
        m_recorder->note(tr("%n line(s) sent one at a time:", nullptr, int(lines.size())));
        for (const QString &l : lines)
            m_recorder->typed(l + '\r');
    }
}

void SessionWidget::typeText(const QString &text)
{
    if (canSendCommands())
        sshlx_session_text(m_session, text.toUtf8().constData());
}

void SessionWidget::setLocked(bool on)
{
    if (on == m_locked || (on && m_filePanel))
        return;
    m_locked = on;
    m_view->setLocked(on);
    if (on)
        m_logon->stop();
    emit lockChanged(on);
}

void SessionWidget::noteInput()
{
    if (m_antiIdle.seconds > 0 && !m_antiIdle.text.isEmpty()
        && state() == SSHLX_STATE_CONNECTED)
        m_idleTimer->start(m_antiIdle.seconds * 1000);
}

SessionWidget::Status SessionWidget::status() const
{
    Status st;
    for (size_t i = 0; i < sshlx_protocol_count(); i++)
        if (sshlx_protocol_get(i)->id == protocol())
            st.protocol = QString::fromUtf8(sshlx_protocol_get(i)->display_name);
    st.cipherOut = m_cipherOut;
    st.cipherIn = m_cipherIn;
    st.received = m_received;
    if (m_connectedAt.isValid() && state() == SSHLX_STATE_CONNECTED)
        st.connectedSecs = m_connectedAt.secsTo(QDateTime::currentDateTime());
    sshlx_session_get_size(m_session, &st.cols, &st.rows);
    return st;
}

void SessionWidget::showHexView()
{
    if (!m_hexView) {
        m_hexView = new HexViewDialog(tr("%1: Received Data").arg(m_title), window());
        m_hexView->setAttribute(Qt::WA_DeleteOnClose);
        connect(this, &SessionWidget::outputReceived, m_hexView.data(), &HexViewDialog::addData);
    }
    m_hexView->show();
    m_hexView->raise();
}

QDialog *SessionWidget::hexViewForTests() const { return m_hexView; }

bool SessionWidget::sendingAscii() const { return m_ascii->running(); }

void SessionWidget::sendAsciiFile(const QString &path, int lineDelayMs, int charDelayMs)
{
    if (!canSendCommands())
        return;
    QString error;
    if (!m_ascii->start(path, {lineDelayMs, charDelayMs}, &error)) {
        DialogKit::error(window(), tr("Send ASCII"), error);
        return;
    }
    showTask(tr("Sending text..."), true);
    m_view->setInputBlocked(true);
}

void SessionWidget::askAsciiFile()
{
    const QString path = QFileDialog::getOpenFileName(window(), tr("Send ASCII File"));
    if (path.isEmpty())
        return;
    QSettings st;
    QPointer<SessionWidget> self(this);
    bool ok = false;
    const int line = QInputDialog::getInt(window(), tr("Send ASCII File"),
                                          tr("Pause after each line (ms):"),
                                          st.value("transfers/asciiLineMs", 100).toInt(), 0, 60000, 10, &ok);
    if (!ok || !self)
        return;
    const int chr = QInputDialog::getInt(window(), tr("Send ASCII File"),
                                         tr("Pause after each character (ms):"),
                                         st.value("transfers/asciiCharMs", 0).toInt(), 0, 5000, 1, &ok);
    if (!ok || !self)
        return;
    st.setValue("transfers/asciiLineMs", line);
    st.setValue("transfers/asciiCharMs", chr);
    sendAsciiFile(path, line, chr);
}

void SessionWidget::handleDroppedFiles(const QStringList &paths)
{
    if (paths.isEmpty() || !canSendCommands())
        return;
    using P = ModemTransfer::Protocol;
    const bool ssh = protocol() == protocolId("ssh");
    QStringList names;
    for (const QString &p : paths)
        names << QFileInfo(p).fileName();
    QStringList buttons{tr("Cancel"), tr("Zmodem"), tr("Ymodem")};
    if (ssh)
        buttons << tr("SFTP");
    QPointer<SessionWidget> self(this);
    const int choice = DialogKit::message(window(), DialogKit::Kind::Question,
                                          tr("Send %n file(s)?", nullptr, int(paths.size())),
                                          tr("Send %1 to this session. Zmodem starts the receiver "
                                             "itself. For Ymodem, start it on the other side first.")
                                              .arg(names.join(", ")),
                                          buttons, ssh ? 3 : 1);
    if (!self)
        return;
    if (choice == 1) {
        sendCommand("rz");
        sendModem(int(P::Zmodem), paths);
    } else if (choice == 2) {
        sendModem(int(P::Ymodem), paths);
    } else if (choice == 3) {
        emit sftpUploadRequested(paths);
    }
}

void SessionWidget::setTabColour(const QString &colour)
{
    m_tabColour = colour;
    emit identityChanged();
}

void SessionWidget::setGroup(const QString &name)
{
    m_group = name.trimmed();
    emit identityChanged();
}

void SessionWidget::mirrorKey(const sshlx_key_event *ev, int qtKey)
{
    if (m_recorder) {
        if (qtKey == Qt::Key_Return || qtKey == Qt::Key_Enter)
            m_recorder->typed(QStringLiteral("\r"));
        else if (qtKey == Qt::Key_Backspace)
            m_recorder->typed(QString(QChar(0x7f)));
        else if (ev->text && *ev->text)
            m_recorder->typed(QString::fromUtf8(ev->text));
        else if (qtKey >= Qt::Key_Left && qtKey <= Qt::Key_Down)
            m_recorder->note(tr("An arrow key was pressed here, which is not recorded."));
    }
    if (!mirror())
        return;
    for (SessionWidget *t : mirror()(this))
        if (t != this && t->canSendCommands())
            sshlx_session_key(t->m_session, ev);
}

void SessionWidget::mirrorText(const QString &text)
{
    if (m_recorder)
        m_recorder->typed(text);
    if (!mirror())
        return;
    for (SessionWidget *t : mirror()(this))
        if (t != this && t->canSendCommands())
            t->typeText(text);
}

void SessionWidget::runKeyBinding(const KeyBinding &b)
{
    switch (b.kind) {
    case KeyBinding::Text: {
        const QString text = KeyBindings::decodeText(b.value);
        typeText(text);
        mirrorText(text);
        break;
    }
    case KeyBinding::Macro:
        emit macroRequested(b.value);
        break;
    case KeyBinding::Script:
        emit scriptRequested(b.value);
        break;
    }
}

void SessionWidget::afterLogon()
{
    const QString path = m_logonScript ? m_logonScript() : QString();
    if (!path.isEmpty() && state() == SSHLX_STATE_CONNECTED)
        emit logonScriptRequested(path);
}

void SessionWidget::startRecording()
{
    if (!m_recorder && !m_filePanel)
        m_recorder = new ScriptRecorder;
}

QString SessionWidget::stopRecording()
{
    if (!m_recorder)
        return {};
    const QString script = m_recorder->steps() ? m_recorder->script() : QString();
    delete m_recorder;
    m_recorder = nullptr;
    return script;
}

bool SessionWidget::logEnabled() const
{
    return confInt(conf(), "LogType") != 0 && !confStr(conf(), "LogFileName").isEmpty();
}

QString SessionWidget::expandedLogPath(const QDateTime &when) const
{
    return LogRotate::expandName(confStr(conf(), "LogFileName"), confStr(conf(), "HostName"),
                                 confInt(conf(), "PortNumber"), when);
}

void SessionWidget::setLogPaused(bool paused)
{
    if (paused == m_logPaused || !logEnabled())
        return;
    m_logPaused = paused;
    sshlx_session_log_set_paused(m_session, paused);
}

static LogRotate::Policy rotationPolicy()
{
    QSettings st;
    LogRotate::Policy p;
    p.maxBytes = st.value("logging/rotateMB", 0).toLongLong() * 1024 * 1024;
    p.maxHours = st.value("logging/rotateHours", 0).toInt();
    p.keep = st.value("logging/keep", 5).toInt();
    return p;
}

bool SessionWidget::rotateLog()
{
    if (!logEnabled() || state() != SSHLX_STATE_CONNECTED || !QFileInfo::exists(m_logPath))
        return false;
    const bool wasPaused = m_logPaused;
    if (!wasPaused)
        sshlx_session_log_set_paused(m_session, true);
    const QString rotated = LogRotate::rotate(m_logPath, rotationPolicy(), QDateTime::currentDateTime());
    m_logOpened = QDateTime::currentDateTime();
    m_logPath = expandedLogPath(m_logOpened);
    if (!wasPaused)
        sshlx_session_log_set_paused(m_session, false);
    return !rotated.isEmpty();
}

void SessionWidget::checkLogRotation()
{
    if (m_logPaused || !logEnabled() || state() != SSHLX_STATE_CONNECTED)
        return;
    const LogRotate::Policy p = rotationPolicy();
    if (p.active() && LogRotate::due(p, QFileInfo(m_logPath).size(), m_logOpened,
                                     QDateTime::currentDateTime()))
        rotateLog();
}

bool SessionWidget::logonRunning() const { return m_logon->running(); }

bool SessionWidget::transferRunning() const { return m_modem->running(); }

void SessionWidget::showTask(const QString &text, bool canStop)
{
    m_taskLabel->setText(text);
    m_taskProgress->setVisible(canStop);
    m_taskStop->setVisible(canStop);
    m_taskClose->setVisible(!canStop);
    m_taskBar->setVisible(true);
}

void SessionWidget::closeTask(const QString &finalText)
{
    if (finalText.isEmpty())
        m_taskBar->hide();
    else
        showTask(finalText, false);
}

void SessionWidget::beginModem()
{
    m_logon->stop();
    m_view->setInputBlocked(true);
    setRawReceiver([this](const char *data, size_t len) { m_modem->feed(data, len); });
    m_taskProgress->setRange(0, 0);
}

bool SessionWidget::sendModem(int protocol, const QStringList &files, bool resume)
{
    if (!canSendCommands() || files.isEmpty())
        return false;
    const auto p = ModemTransfer::Protocol(protocol);
    m_taskLabel->setProperty("protocol", ModemTransfer::protocolName(p));
    showTask(tr("%1: waiting for the other end...").arg(ModemTransfer::protocolName(p)), true);
    beginModem();
    m_modem->startSend(p, files, resume);
    // The receiver was started first, so its "ready" went past while a file
    // was being picked, and it may not repeat that for ten seconds and more.
    if (p != ModemTransfer::Protocol::Zmodem && p != ModemTransfer::Protocol::Kermit
        && !m_zTail.isEmpty()) {
        const char last = m_zTail.back();
        const char before = m_zTail.size() > 1 ? m_zTail[m_zTail.size() - 2] : '\n';
        if ((last == 'C' && (before == 'C' || !QChar::isLetterOrNumber(uchar(before))))
            || last == '\x15')
            m_modem->feed(&last, 1);
    }
    return true;
}

bool SessionWidget::receiveModem(int protocol, const QString &directory, const QString &saveAs)
{
    if (!canSendCommands())
        return false;
    const auto p = ModemTransfer::Protocol(protocol);
    m_taskLabel->setProperty("protocol", ModemTransfer::protocolName(p));
    showTask(tr("%1: waiting for the other end...").arg(ModemTransfer::protocolName(p)), true);
    beginModem();
    m_modem->startReceive(p, directory, saveAs);
    return true;
}

static QString downloadFolder()
{
    const QString d = QSettings().value("transfers/downloadDir").toString();
    return d.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) : d;
}

// The remote side is waiting. Nothing arrives on disk without being accepted
// here: a file could just as well be sent by something the user only "cat"ed.
void SessionWidget::offerZmodem(bool receiving)
{
    const auto proto = int(ModemTransfer::Protocol::Zmodem);
    QPointer<SessionWidget> self(this);
    auto decline = [self] {
        if (self) {
            self->sendRaw(QByteArray(8, '\x18') + QByteArray(8, '\b'));
            self->m_zOffered = false;
            self->m_zTail.clear();
        }
    };
    if (!canSendCommands()) {
        m_zOffered = false;
        return;
    }
    if (receiving) {
        QString dir = downloadFolder();
        while (true) {
            const int choice = DialogKit::message(
                window(), DialogKit::Kind::Question, tr("Receive files with Zmodem?"),
                tr("The remote side wants to send files. They would be saved in:\n%1").arg(dir),
                {tr("Cancel"), tr("Change Folder..."), tr("Receive")}, 2);
            if (!self)
                return;
            if (choice == 2)
                break;
            if (choice != 1) {
                decline();
                return;
            }
            const QString picked = QFileDialog::getExistingDirectory(window(), tr("Save Files In"), dir);
            if (!self)
                return;
            if (!picked.isEmpty()) {
                dir = picked;
                QSettings().setValue("transfers/downloadDir", dir);
            }
        }
        if (!receiveModem(proto, dir))
            decline();
        return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(window(), tr("Send Files with Zmodem"));
    if (!self)
        return;
    if (files.isEmpty() || !sendModem(proto, files))
        decline();
}

void SessionWidget::buildModemMenu(QMenu *menu)
{
    using P = ModemTransfer::Protocol;
    const bool can = canSendCommands();
    for (P p : {P::Xmodem, P::Xmodem1k, P::Ymodem, P::Zmodem, P::Kermit}) {
        const QString name = ModemTransfer::protocolName(p);
        menu->addAction(tr("Send with %1...").arg(name), this, [this, p, name] {
            const bool single = p == P::Xmodem || p == P::Xmodem1k;
            const QStringList files =
                single ? QStringList{QFileDialog::getOpenFileName(window(), tr("Send with %1").arg(name))}
                       : QFileDialog::getOpenFileNames(window(), tr("Send with %1").arg(name));
            if (files.isEmpty() || files.first().isEmpty())
                return;
            // Unlike the others, the Zmodem sender starts its receiver.
            if (p == P::Zmodem)
                sendCommand("rz");
            sendModem(int(p), files);
        })->setEnabled(can);
    }
    menu->addAction(tr("Resume with Zmodem..."), this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(window(), tr("Resume with Zmodem"));
        if (files.isEmpty())
            return;
        sendCommand("rz -r");
        sendModem(int(P::Zmodem), files, true);
    })->setEnabled(can);
    menu->addAction(tr("Send ASCII File..."), this, &SessionWidget::askAsciiFile)->setEnabled(can);
    menu->addSeparator();
    for (P p : {P::Xmodem, P::Ymodem, P::Zmodem, P::Kermit}) {
        const QString name = ModemTransfer::protocolName(p);
        menu->addAction(tr("Receive with %1...").arg(name), this, [this, p, name] {
            if (p == P::Xmodem) {
                const QString path = QFileDialog::getSaveFileName(window(), tr("Receive with Xmodem"),
                                                                  downloadFolder());
                if (!path.isEmpty())
                    receiveModem(int(p), QFileInfo(path).path(), QFileInfo(path).fileName());
                return;
            }
            const QString dir = QFileDialog::getExistingDirectory(
                window(), tr("Receive with %1").arg(name), downloadFolder());
            if (!dir.isEmpty()) {
                QSettings().setValue("transfers/downloadDir", dir);
                receiveModem(int(p), dir);
            }
        })->setEnabled(can);
    }
}

void SessionWidget::sendPasswordQuietly(const QString &credentialId,
                                        std::function<void(bool, const QString &)> done)
{
    const QString account = m_credentialAccount ? m_credentialAccount(credentialId) : QString();
    if (account.isEmpty()) {
        done(false, tr("no saved credential is linked"));
        return;
    }
    QPointer<SessionWidget> self(this);
    Keyring::read(account, [self, done](bool found, const QString &pw) {
        if (!self || self->state() != SSHLX_STATE_CONNECTED) {
            done(false, tr("the session has ended"));
        } else if (!found) {
            done(false, tr("the credential has no password in the keyring"));
        } else {
            sshlx_session_text(self->m_session, (pw + '\r').toUtf8().constData());
            done(true, QString());
        }
    });
}

void SessionWidget::runLogonPassword(const QString &credential)
{
    // A password sent to anything but a hidden prompt lands on screen and in
    // the shell's history.
    if (!ConfigPush::looksLikePasswordPrompt(m_logon->lastLine())) {
        m_logon->abort(tr("the password was not sent, as \"%1\" is not a password prompt.")
                           .arg(m_logon->lastLine().trimmed()));
        return;
    }
    QPointer<SessionWidget> self(this);
    sendPasswordQuietly(credential, [self](bool ok, const QString &error) {
        if (!self)
            return;
        if (ok)
            self->m_logon->stepDone();
        else
            self->m_logon->abort(error);
    });
}

bool SessionWidget::atPasswordPrompt() const
{
    return ConfigPush::looksLikePasswordPrompt(m_view->rowText(m_view->cursorRow()));
}

QString SessionWidget::cursorLine() const
{
    return m_view->rowText(m_view->cursorRow());
}

void SessionWidget::buildPasswordMenu(QMenu *menu)
{
    const QList<SavedPassword> saved = m_savedPasswords ? m_savedPasswords() : QList<SavedPassword>();
    for (const SavedPassword &p : saved)
        menu->addAction(p.name, this, [this, p] { sendSavedPassword(p.name, p.account); });
    if (saved.isEmpty())
        menu->addAction(tr("No saved credentials"))->setEnabled(false);
}

void SessionWidget::sendSavedPassword(const QString &name, const QString &account)
{
    if (m_locked)
        return;
    // Sent to anything but a hidden prompt it would land on screen and in
    // the shell's history.
    if (!ConfigPush::looksLikePasswordPrompt(m_view->rowText(m_view->cursorRow()))) {
        QPointer<SessionWidget> alive(this);
        int choice = DialogKit::message(
            window(), DialogKit::Kind::Warning, tr("Send the saved password?"),
            tr("This line does not look like a password prompt, so the password may "
               "appear on screen."),
            {tr("Cancel"), tr("Send Anyway")}, 0);
        if (!alive || choice != 1)
            return;
    }
    QPointer<SessionWidget> self(this);
    Keyring::read(account, [self, name](bool found, const QString &pw) {
        if (!self)
            return;
        if (!found) {
            DialogKit::warning(self->window(), tr("Send Saved Password"),
                               tr("\"%1\" has no password in the keyring.").arg(name));
        } else if (self->state() != SSHLX_STATE_CONNECTED) {
            return;
        } else if (self->m_pushBar->waitingForAnswer()) {
            self->m_pushBar->answerSecret(pw);
        } else if (!self->m_push->running()) {
            sshlx_session_text(self->m_session, (pw + '\r').toUtf8().constData());
            if (self->m_recorder)
                self->m_recorder->password(name);
        }
    });
}

void SessionWidget::clearScrollback() { sshlx_session_clear_scrollback(m_session); }
void SessionWidget::resetTerminal() { sshlx_session_reset_terminal(m_session); }
void SessionWidget::focusTerminal() { m_view->setFocus(); }

void SessionWidget::showEventLog()
{
    if (!m_eventLog) {
        m_eventLog = new EventLogDialog(tr("%1 Event Log").arg(m_title), window());
        const char *const *lines;
        size_t n = sshlx_session_event_log(m_session, &lines);
        for (size_t i = 0; i < n; i++)
            m_eventLog->appendLine(QString::fromUtf8(lines[i]));
    }
    m_eventLog->show();
    m_eventLog->raise();
    m_eventLog->activateWindow();
}

void SessionWidget::changeSettings()
{
    sshlx_conf *c = sshlx_session_reconfig_conf(m_session);
    auto *dlg = new SettingsDialog(c, true, sshlx_session_cfg_info(m_session),
                                   tr("sshLX Reconfiguration - %1").arg(m_title),
                                   window(), /*takeOwnership=*/true);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<SessionWidget> self(this);
    connect(dlg, &SettingsDialog::accepted, this, [self, dlg] {
        if (!self)
            return;
        sshlx_session_reconfigure(self->m_session, dlg->conf());
        self->applyConfToView();
    });
    dlg->setWindowModality(Qt::WindowModal);
    dlg->open();
}

void SessionWidget::buildContextMenu(QMenu *menu, const QString &link)
{
    if (!link.isEmpty()) {
        menu->addAction(tr("&Open Link"), this, [link] { QDesktopServices::openUrl(QUrl(link)); });
        menu->addAction(tr("Copy Lin&k"), this, [link] {
            QGuiApplication::clipboard()->setText(link);
        });
        menu->addSeparator();
    }
    const bool canSend = canSendCommands();
    menu->addAction(Icons::get("copy"), tr("&Copy"), this, &SessionWidget::copy);
    menu->addAction(Icons::get("paste"), tr("&Paste"), this, &SessionWidget::paste);
    menu->addAction(Icons::get("paste"), tr("Paste Con&figuration..."), this,
                    [this] { pasteConfiguration(); })->setEnabled(canSend);
    menu->addAction(tr("Select A&ll"), this, &SessionWidget::selectAll);
    menu->addAction(tr("Copy &All to Clipboard"), this, &SessionWidget::copyAll);
    menu->addSeparator();
    menu->addAction(Icons::get("search"), tr("Find in Termina&l..."), this,
                    &SessionWidget::findInTerminal);
    menu->addAction(Icons::get("save"), tr("Sa&ve Terminal Text..."), this,
                    &SessionWidget::saveTerminalText);
#ifdef SSHLX_HAVE_PRINT
    menu->addAction(tr("Prin&t Terminal Text..."), this, &SessionWidget::printTerminalText);
#endif
    menu->addSeparator();
    menu->addAction(Icons::get("palette"), tr("Add &Keyword..."), this, &SessionWidget::addKeyword);
    QAction *keywords = menu->addAction(tr("Highlight Key&words"));
    keywords->setCheckable(true);
    keywords->setChecked(HighlightSet::enabled());
    connect(keywords, &QAction::toggled, this, [](bool on) {
        HighlightSet::setEnabled(on);
        reloadHighlights();
    });
    menu->addSeparator();
    menu->addAction(tr("Clea&r Terminal"), this, &SessionWidget::clearTerminal);
    menu->addAction(tr("C&lear Scrollback"), this, &SessionWidget::clearScrollback);
    menu->addAction(tr("Rese&t Terminal"), this, &SessionWidget::resetTerminal);
    menu->addSeparator();

    QMenu *passwords = menu->addMenu(Icons::get("lock"), tr("Send Saved Pass&word"));
    buildPasswordMenu(passwords);
    passwords->setEnabled(state() == SSHLX_STATE_CONNECTED
                          && (!m_push->running() || m_pushBar->waitingForAnswer()));

    menu->addAction(Icons::get("export"), tr("File &Transfer"), this,
                    &SessionWidget::fileTransferRequested)
        ->setEnabled(protocol() == protocolId("ssh"));

    buildModemMenu(menu->addMenu(Icons::get("usb"), tr("Xmodem, Ymodem, Zmodem, Kermit")));
    menu->addAction(tr("Received Data (He&x)"), this, &SessionWidget::showHexView);

    QAction *lockAct = menu->addAction(Icons::get("lock"), tr("Loc&k Session"));
    lockAct->setCheckable(true);
    lockAct->setChecked(m_locked);
    lockAct->setEnabled(!m_filePanel);
    connect(lockAct, &QAction::toggled, this, &SessionWidget::setLocked);
    if (logEnabled()) {
        QMenu *logMenu = menu->addMenu(Icons::get("save"), tr("Session Lo&g"));
        QAction *pause = logMenu->addAction(tr("&Pause Logging"));
        pause->setCheckable(true);
        pause->setChecked(m_logPaused);
        connect(pause, &QAction::toggled, this, &SessionWidget::setLogPaused);
        logMenu->addAction(tr("&Rotate Now"), this, &SessionWidget::rotateLog);
    }
    QAction *rec = menu->addAction(Icons::get("script"), recording() ? tr("Stop Recording Script...")
                                                                     : tr("Record Script"));
    connect(rec, &QAction::triggered, this, &SessionWidget::recordToggleRequested);
    rec->setEnabled(!m_filePanel);

    QMenu *macros = menu->addMenu(Icons::get("script"), tr("E&xecute Macro"));
    addMacrosToMenu(macros, loadMacros(), canSend, [this](const Macro &m) {
        if (m.script.isEmpty())
            runMacro(m.text);
        else
            emit scriptRequested(m.script);
    });
    if (!macros->isEmpty())
        macros->addSeparator();
    macros->addAction(tr("&Manage Macros..."), this, [this] {
        MacroDialog dlg(window());
        dlg.exec();
    });

    const sshlx_special *sp;
    size_t n = sshlx_session_specials(m_session, &sp);
    if (n) {
        QMenu *specials = menu->addMenu(Icons::get("keyboard"), tr("S&pecial Commands"));
        QList<QMenu *> stack{specials};
        for (size_t i = 0; i < n; i++) {
            QMenu *cur = stack.last();
            switch (sp[i].kind) {
            case SSHLX_SPECIAL_ITEM: {
                int index = sp[i].index;
                QPointer<SessionWidget> self(this);
                cur->addAction(QString::fromUtf8(sp[i].name), this, [self, index] {
                    if (self)
                        sshlx_session_send_special(self->m_session, index);
                });
                break;
            }
            case SSHLX_SPECIAL_SEPARATOR:
                cur->addSeparator();
                break;
            case SSHLX_SPECIAL_SUBMENU:
                stack.append(cur->addMenu(QString::fromUtf8(sp[i].name)));
                break;
            case SSHLX_SPECIAL_END_SUBMENU:
                if (stack.size() > 1)
                    stack.removeLast();
                break;
            }
        }
    }
    menu->addAction(Icons::get("connection-new"), tr("D&uplicate Tab"), this, [this] {
        emit duplicateRequested(m_displayName);
    });
    menu->addAction(tr("Change Terminal Tit&le..."), this, &SessionWidget::changeTitle);
    menu->addSeparator();
    buildAppearanceMenu(menu->addMenu(Icons::get("palette"), tr("&Appearance")));
    menu->addAction(Icons::get("event-log"), tr("&Event Log"), this, &SessionWidget::showEventLog);
    menu->addAction(Icons::get("settings"), tr("Change &Settings..."), this, &SessionWidget::changeSettings);
    QAction *restart = menu->addAction(Icons::get("reconnect"), tr("R&estart Session"), this, &SessionWidget::reconnect);
    restart->setEnabled(state() == SSHLX_STATE_DISCONNECTED);
    QAction *disc = menu->addAction(Icons::get("disconnect"), tr("&Disconnect"), this, &SessionWidget::disconnectSession);
    disc->setEnabled(state() == SSHLX_STATE_CONNECTED || state() == SSHLX_STATE_CONNECTING);
    menu->addSeparator();
    menu->addAction(Icons::get("close"), tr("Close Tab"), this, &SessionWidget::closeRequested);
}
