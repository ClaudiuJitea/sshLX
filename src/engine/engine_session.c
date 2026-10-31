/*
 * engine_session.c - session lifecycle and the public per-session API.
 *
 * Mirrors the life of a GtkFrontend in PuTTY's unix/window.c:
 * new_session_window() -> start_backend() -> destroy_inst_connection()
 * -> restart_session_menuitem() -> delete_inst(), plus the mouse, paste
 * and reconfiguration paths. One sshlx_session per tab; sessions share
 * nothing except PuTTY's process-wide (context-keyed) event machinery.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "engine_internal.h"

/* The public clipboard numbering must match PuTTY's on Unix. */
typedef char sshlx_clip_check[
    ((int)SSHLX_CLIP_NULL == (int)CLIP_NULL &&
     (int)SSHLX_CLIP_LOCAL == (int)CLIP_LOCAL &&
     (int)SSHLX_CLIP_PRIMARY == (int)CLIP_PRIMARY &&
     (int)SSHLX_CLIP_CLIPBOARD == (int)CLIP_CLIPBOARD &&
     (int)SSHLX_CLIP_CUSTOM_1 == (int)CLIP_CUSTOM_1 &&
     (int)SSHLX_CLIP_CUSTOM_3 == (int)CLIP_CUSTOM_3) ? 1 : -1];

sshlx_session *sshlx_live_sessions;

void sshlx_prompts_cancel_backend(sshlx_session *s);

bool sshlx_session_usable(const sshlx_session *s)
{
    return s && !s->dead;
}

void sshlx_session_set_state(sshlx_session *s, sshlx_state st)
{
    if (s->state == st)
        return;
    s->state = st;
    UI_CALL(s, state_changed, st);
}

/* window.c cache_conf_values() */
void sshlx_session_cache_conf(sshlx_session *s)
{
    s->bold_style = conf_get_int(s->conf, CONF_bold_style);
    s->cursor_type = conf_get_int(s->conf, CONF_cursor_type);
    s->shadowbold = conf_get_bool(s->conf, CONF_shadowbold);
    s->shadowboldoffset = conf_get_int(s->conf, CONF_shadowboldoffset);
}

static void setup_ucs(sshlx_session *s)
{
    /* The UI renders with Unicode fonts, like GTK PuTTY's Pango
     * ("client:") fonts, whose public charset is CS_UTF8. */
    s->direct_to_font = init_ucs(&s->ucsdata,
                                 conf_get_str(s->conf, CONF_line_codepage),
                                 conf_get_bool(s->conf, CONF_utf8_override),
                                 CS_UTF8,
                                 conf_get_int(s->conf, CONF_vtmode));
}

sshlx_session *sshlx_session_new(const sshlx_conf *conf,
                                 const sshlx_session_callbacks *cb,
                                 int cols, int rows)
{
    sshlx_session *s = snew(sshlx_session);
    memset(s, 0, sizeof(*s));
    s->seat.vt = &sshlx_seat_vt;
    s->termwin.vt = &sshlx_termwin_vt;
    s->logpolicy.vt = &sshlx_logpolicy_vt;
    if (cb)
        s->cb = *cb;
    s->conf = conf_copy(CONF_OF(conf));
    s->alt_keycode = -1;            /* this one needs _not_ to be zero */
    s->state = SSHLX_STATE_IDLE;
    s->exit_code = -1;

    if (cols <= 0) cols = conf_get_int(s->conf, CONF_width);
    if (rows <= 0) rows = conf_get_int(s->conf, CONF_height);

    sshlx_session_cache_conf(s);
    setup_ucs(s);

    s->term = term_init(s->conf, &s->ucsdata, &s->termwin);
    sshlx_setup_clipboards(s);
    s->logctx = log_init(&s->logpolicy, s->conf);
    term_provide_logctx(s->term, s->logctx);
    term_size(s->term, rows, cols, conf_get_int(s->conf, CONF_savelines));

    s->next_live = sshlx_live_sessions;
    sshlx_live_sessions = s;
    return s;
}

/* window.c destroy_inst_connection() */
void sshlx_session_destroy_connection(sshlx_session *s)
{
    s->exited = true;
    sshlx_prompts_cancel_backend(s);
    if (s->ldisc) {
        ldisc_free(s->ldisc);
        s->ldisc = NULL;
    }
    if (s->backend) {
        backend_free(s->backend);
        s->backend = NULL;
    }
    if (s->term)
        term_provide_backend(s->term, NULL);
    s->specials_src = NULL;
    s->n_specials = 0;
    sshlx_session_set_state(s, SSHLX_STATE_DISCONNECTED);
    UI_CALL(s, specials_changed);
}

/* window.c start_backend() */
int sshlx_session_connect(sshlx_session *s)
{
    const struct BackendVtable *vt;
    char *error, *realhost;

    if (s->dead || s->backend)
        return s->backend ? 0 : -1;

    vt = backend_vt_from_proto(conf_get_int(s->conf, CONF_protocol));
    if (!vt) {
        s->exited = true;
        UI_CALL(s, connection_fatal, "Unknown protocol", 0);
        sshlx_session_set_state(s, SSHLX_STATE_DISCONNECTED);
        return -1;
    }

    s->exited = false;
    s->password_hook_declined = false;
    s->passphrase_hook_declined = false;
    sshlx_session_set_state(s, SSHLX_STATE_CONNECTING);
    seat_set_trust_status(&s->seat, true);
    errno = 0;
    error = backend_init(vt, &s->seat, &s->backend, s->logctx, s->conf,
                         conf_get_str(s->conf, CONF_host),
                         conf_get_int(s->conf, CONF_port),
                         &realhost,
                         conf_get_bool(s->conf, CONF_tcp_nodelay),
                         conf_get_bool(s->conf, CONF_tcp_keepalives));
    int saved_errno = errno;

    if (error) {
        /* The serial backend reports open() failures via strerror();
         * keep the errno so the UI can explain e.g. EACCES. */
        s->last_errno = (vt->protocol == PROT_SERIAL) ? saved_errno : 0;
        seat_connection_fatal(&s->seat, "Unable to open connection to %s:\n%s",
                              conf_dest(s->conf), error);
        sfree(error);
        s->exited = true;
        s->backend = NULL;
        sshlx_session_set_state(s, SSHLX_STATE_DISCONNECTED);
        sshlx_after_entry();
        return -1;
    }

    term_setup_window_titles(s->term, realhost);
    sfree(realhost);
    term_provide_backend(s->term, s->backend);
    s->ldisc = ldisc_create(s->conf, s->term, s->backend, &s->seat);
    if (s->ldisc)
        ldisc_echoedit_update(s->ldisc);

    /* Only the SSH backends announce "session started"; for the others
     * the session is usable as soon as the backend is up. */
    if (vt->protocol != PROT_SSH && vt->protocol != PROT_SSHCONN)
        sshlx_session_set_state(s, SSHLX_STATE_CONNECTED);
    s->specials_src = NULL;
    UI_CALL(s, specials_changed);
    sshlx_after_entry();
    return 0;
}

void sshlx_session_set_raw(sshlx_session *s, bool raw)
{
    s->raw_output = raw;
}

void sshlx_session_send_bytes(sshlx_session *s, const char *data, size_t len)
{
    if (s->dead || !s->backend)
        return;
    backend_send(s->backend, data, len);
    sshlx_after_entry();
}

bool sshlx_session_send_ready(sshlx_session *s)
{
    return !s->dead && s->backend && backend_sendok(s->backend);
}

size_t sshlx_session_send_backlog(sshlx_session *s)
{
    return s->dead || !s->backend ? 0 : backend_sendbuffer(s->backend);
}

void sshlx_session_disconnect(sshlx_session *s)
{
    if (s->dead || !s->backend)
        return;
    logevent(s->logctx, "Disconnected by user");
    sshlx_session_destroy_connection(s);
    sshlx_after_entry();
}

/* window.c restart_session_menuitem() */
int sshlx_session_reconnect(sshlx_session *s)
{
    if (s->dead || s->backend)
        return -1;
    logevent(s->logctx, "----- Session restarted -----");
    term_pwron(s->term, false);
    s->exit_code = -1;
    return sshlx_session_connect(s);
}

/* window.c delete_inst(), deferred to top level. */
static void really_free(void *vctx)
{
    sshlx_session *s = (sshlx_session *)vctx;

    for (sshlx_session **link = &sshlx_live_sessions; *link;
         link = &(*link)->next_live) {
        if (*link == s) {
            *link = s->next_live;
            break;
        }
    }

    sshlx_prompts_cancel_all(s);
    if (s->ldisc) {
        ldisc_free(s->ldisc);
        s->ldisc = NULL;
    }
    if (s->backend) {
        backend_free(s->backend);
        s->backend = NULL;
    }
    if (s->term) {
        term_free(s->term);
        s->term = NULL;
    }
    if (s->logctx) {
        log_free(s->logctx);
        s->logctx = NULL;
    }
    if (s->conf) {
        conf_free(s->conf);
        s->conf = NULL;
    }
    for (size_t i = 0; i < s->n_eventlog; i++)
        sfree(s->eventlog[i]);
    sfree(s->eventlog);
    sfree(s->specials);
    if (s->hook_password) {
        smemclr(s->hook_password, strlen(s->hook_password));
        sfree(s->hook_password);
    }

    /* Delete any toplevel callbacks or timers still pointing at us or
     * at the PuTTY-facing objects embedded in us. Done last, because the
     * frees above may themselves queue such callbacks. */
    delete_callbacks_for_context(s);
    delete_callbacks_for_context(&s->seat);
    delete_callbacks_for_context(&s->termwin);
    delete_callbacks_for_context(&s->logpolicy);
    expire_timer_context(s);
    sfree(s);
}

void sshlx_session_free(sshlx_session *s)
{
    if (!s || s->dead)
        return;
    /* From now on no UI callbacks are made (UI_CALL checks s->dead). */
    s->dead = true;
    s->draw_active = false;
    queue_toplevel_callback(really_free, s);
    sshlx_after_entry();
}

sshlx_state sshlx_session_state(const sshlx_session *s)
{
    return s->state;
}

int sshlx_session_exit_code(const sshlx_session *s)
{
    return s->exit_code;
}

const sshlx_conf *sshlx_session_conf(const sshlx_session *s)
{
    return SCONF_OF(s->conf);
}

int sshlx_session_cfg_info(sshlx_session *s)
{
    return s->backend ? backend_cfg_info(s->backend) : 0;
}

sshlx_conf *sshlx_session_reconfig_conf(sshlx_session *s)
{
    Conf *c = conf_copy(s->conf);
    term_pre_reconfig(s->term, c);
    return SCONF_OF(c);
}

static bool fontspec_differs(Conf *a, Conf *b, int key)
{
    return strcmp(conf_get_fontspec(a, key)->name,
                  conf_get_fontspec(b, key)->name) != 0;
}

/* window.c after_change_settings_dialog() */
int sshlx_session_reconfigure(sshlx_session *s, const sshlx_conf *newconf_in)
{
    if (s->dead)
        return 0;
    Conf *oldconf = s->conf, *newconf = conf_copy(CONF_OF(newconf_in));
    int ret = 0;

    s->conf = newconf;
    log_reconfig(s->logctx, s->conf);
    if (s->ldisc) {
        ldisc_configure(s->ldisc, s->conf);
        ldisc_echoedit_update(s->ldisc);
    }
    term_reconfig(s->term, s->conf);
    sshlx_setup_clipboards(s);
    if (s->backend)
        backend_reconfig(s->backend, s->conf);
    sshlx_session_cache_conf(s);

    if (fontspec_differs(oldconf, newconf, CONF_font) ||
        fontspec_differs(oldconf, newconf, CONF_boldfont) ||
        fontspec_differs(oldconf, newconf, CONF_widefont) ||
        fontspec_differs(oldconf, newconf, CONF_wideboldfont) ||
        strcmp(conf_get_str(oldconf, CONF_line_codepage),
               conf_get_str(newconf, CONF_line_codepage)) ||
        conf_get_bool(oldconf, CONF_utf8_override) !=
        conf_get_bool(newconf, CONF_utf8_override) ||
        conf_get_int(oldconf, CONF_vtmode) !=
        conf_get_int(newconf, CONF_vtmode) ||
        conf_get_bool(oldconf, CONF_shadowbold) !=
        conf_get_bool(newconf, CONF_shadowbold) ||
        conf_get_int(oldconf, CONF_shadowboldoffset) !=
        conf_get_int(newconf, CONF_shadowboldoffset)) {
        setup_ucs(s);
        ret |= 1;
    }
    if (conf_get_int(oldconf, CONF_width) != conf_get_int(newconf, CONF_width) ||
        conf_get_int(oldconf, CONF_height) != conf_get_int(newconf, CONF_height) ||
        conf_get_int(oldconf, CONF_window_border) !=
        conf_get_int(newconf, CONF_window_border) ||
        conf_get_bool(oldconf, CONF_scrollbar) !=
        conf_get_bool(newconf, CONF_scrollbar) ||
        conf_get_bool(oldconf, CONF_scrollbar_on_left) !=
        conf_get_bool(newconf, CONF_scrollbar_on_left)) {
        ret |= 2;
    } else if (conf_get_int(oldconf, CONF_savelines) !=
               conf_get_int(newconf, CONF_savelines)) {
        term_size(s->term, s->term->rows, s->term->cols,
                  conf_get_int(newconf, CONF_savelines));
    }
    term_invalidate(s->term);
    conf_free(oldconf);
    sshlx_after_entry();
    return ret;
}

char *sshlx_session_close_warning(sshlx_session *s)
{
    /* window.c delete_window() */
    if (s->exited || !s->backend ||
        !conf_get_bool(s->conf, CONF_warn_on_close))
        return NULL;
    char *additional = NULL;
    if (s->backend->vt->close_warn_text)
        additional = s->backend->vt->close_warn_text(s->backend);
    char *msg = dupprintf("Are you sure you want to close this session?%s%s",
                          additional ? "\n" : "",
                          additional ? additional : "");
    sfree(additional);
    return msg;
}

/* ---- size and painting ------------------------------------------------- */
void sshlx_session_resize(sshlx_session *s, int cols, int rows)
{
    if (s->dead || cols < 1 || rows < 1)
        return;
    term_size(s->term, rows, cols, conf_get_int(s->conf, CONF_savelines));
    sshlx_after_entry();
}

void sshlx_session_resize_done(sshlx_session *s)
{
    if (!s->dead)
        term_resize_request_completed(s->term);
    sshlx_after_entry();
}

void sshlx_session_paint(sshlx_session *s, int c0, int r0, int c1, int r1)
{
    if (s->dead)
        return;
    term_paint(s->term, c0, r0, c1, r1, true);
}

void sshlx_session_invalidate(sshlx_session *s)
{
    if (!s->dead)
        term_invalidate(s->term);
    sshlx_after_entry();
}

void sshlx_session_focus(sshlx_session *s, bool has_focus)
{
    if (!s->dead)
        term_set_focus(s->term, has_focus);
    sshlx_after_entry();
}

void sshlx_session_show_line(sshlx_session *s, int y)
{
    if (s->dead)
        return;
    term_scroll(s->term, 1, y);
    term_invalidate(s->term);
    term_update(s->term);
    sshlx_after_entry();
}

void sshlx_session_scroll(sshlx_session *s, int whence, int lines)
{
    if (s->dead)
        return;
    if (whence > 0 && !conf_get_bool(s->conf, CONF_scrollbar))
        return;   /* as scrollbar_moved() */
    term_scroll(s->term, whence, lines);
    sshlx_after_entry();
}

void sshlx_session_get_size(const sshlx_session *s, int *cols, int *rows)
{
    *cols = s->term->cols;
    *rows = s->term->rows;
}

sshlx_rgb sshlx_session_default_bg(const sshlx_session *s)
{
    sshlx_rgb out;
    out.r = s->palette[OSC4_COLOUR_bg].r;
    out.g = s->palette[OSC4_COLOUR_bg].g;
    out.b = s->palette[OSC4_COLOUR_bg].b;
    return out;
}

/* ---- keyboard -------------------------------------------------------------- */
sshlx_key_result sshlx_session_key(sshlx_session *s, const sshlx_key_event *ev)
{
    if (s->dead)
        return SSHLX_KEY_NOT_HANDLED;
    sshlx_key_result r = sshlx_keyboard_event(s, ev);
    sshlx_after_entry();
    return r;
}

/* window.c input_method_commit_event() */
void sshlx_session_text(sshlx_session *s, const char *utf8)
{
    if (s->dead || !utf8 || !*utf8)
        return;
    term_keyinput(s->term, CS_UTF8, utf8, (int)strlen(utf8));
    sshlx_after_entry();
}

void sshlx_session_preedit(sshlx_session *s, const char *utf8)
{
    if (s->dead)
        return;
    term_set_preedit_text(s->term, utf8 ? dupstr(utf8) : NULL);
    sshlx_after_entry();
}

void sshlx_session_send_raw(sshlx_session *s, const char *data, size_t len)
{
    if (s->dead || !s->ldisc)
        return;
    ldisc_send(s->ldisc, data, (int)len, true);
    sshlx_after_entry();
}

/* ---- mouse ------------------------------------------------------------------ */
static Mouse_Button translate_button(Mouse_Button button)
{
    if (button == MBT_LEFT)
        return MBT_SELECT;
    if (button == MBT_MIDDLE)
        return MBT_PASTE;
    if (button == MBT_RIGHT)
        return MBT_EXTEND;
    if (button == MBT_NOTHING)
        return MBT_NOTHING;
    return 0;                          /* shouldn't happen */
}

static Mouse_Button to_mbt(sshlx_mbutton b)
{
    switch (b) {
      case SSHLX_MB_LEFT: return MBT_LEFT;
      case SSHLX_MB_MIDDLE: return MBT_MIDDLE;
      case SSHLX_MB_RIGHT: return MBT_RIGHT;
      case SSHLX_MB_WHEEL_UP: return MBT_WHEEL_UP;
      case SSHLX_MB_WHEEL_DOWN: return MBT_WHEEL_DOWN;
      case SSHLX_MB_WHEEL_LEFT: return MBT_WHEEL_LEFT;
      case SSHLX_MB_WHEEL_RIGHT: return MBT_WHEEL_RIGHT;
      default: return MBT_NOTHING;
    }
}

static Mouse_Action to_ma(sshlx_maction a)
{
    switch (a) {
      case SSHLX_MA_CLICK: return MA_CLICK;
      case SSHLX_MA_2CLK: return MA_2CLK;
      case SSHLX_MA_3CLK: return MA_3CLK;
      case SSHLX_MA_DRAG: return MA_DRAG;
      case SSHLX_MA_RELEASE: return MA_RELEASE;
      case SSHLX_MA_MOVE: return MA_MOVE;
      default: return MA_NOTHING;
    }
}

#define SCROLL_INCREMENT_LINES 5

bool sshlx_session_mouse(sshlx_session *s, sshlx_mbutton b,
                         sshlx_maction a, int col, int row, uint32_t mods)
{
    if (s->dead)
        return false;
    bool shift = mods & SSHLX_MOD_SHIFT;
    bool ctrl = mods & SSHLX_MOD_CTRL;
    bool alt = mods & SSHLX_MOD_ALT;
    Mouse_Button button = to_mbt(b);
    Mouse_Action act = to_ma(a);

    noise_ultralight(NOISE_SOURCE_MOUSEPOS,
                     ((uint32_t)col << 16) | (uint32_t)row);

    if (a == SSHLX_MA_DRAG || a == SSHLX_MA_MOVE) {
        if (s->ui_right_button && button == MBT_RIGHT &&
            !(s->send_raw_mouse &&
              !(shift && conf_get_bool(s->conf, CONF_mouse_override))))
            return true;
        /* window.c motion_event() */
        term_mouse(s->term, button, translate_button(button), act,
                   col, row, shift, ctrl, alt);
        sshlx_after_entry();
        return true;
    }

    /* window.c button_internal() */
    noise_ultralight(NOISE_SOURCE_MOUSEBUTTON, (unsigned long)b);
    bool raw_mouse_mode =
        (s->send_raw_mouse &&
         !(shift && conf_get_bool(s->conf, CONF_mouse_override)));

    if (!raw_mouse_mode) {
        if (button == MBT_WHEEL_UP && act == MA_CLICK) {
            term_scroll(s->term, 0, -SCROLL_INCREMENT_LINES);
            sshlx_after_entry();
            return true;
        }
        if (button == MBT_WHEEL_DOWN && act == MA_CLICK) {
            term_scroll(s->term, 0, +SCROLL_INCREMENT_LINES);
            sshlx_after_entry();
            return true;
        }
    }

    if (button == MBT_RIGHT && ctrl) {
        /* Just in case this happened in mid-select */
        term_cancel_selection_drag(s->term);
        sshlx_after_entry();
        return false;   /* the UI shows its context menu */
    }

    if (button == MBT_RIGHT && s->ui_right_button && !raw_mouse_mode)
        return false;   /* the UI pastes or shows its menu */

    if (button == MBT_NOTHING)
        return false;
    if (raw_mouse_mode && act != MA_CLICK && act != MA_RELEASE)
        return true;   /* we ignore these in raw mouse mode */

    term_mouse(s->term, button, translate_button(button), act,
               col, row, shift, ctrl, alt);
    sshlx_after_entry();
    return true;
}

/* window.c scroll_internal() (GTK 3.4+ smooth scrolling) */
void sshlx_session_wheel(sshlx_session *s, double xdelta, double ydelta,
                         int col, int row, uint32_t mods)
{
    if (s->dead)
        return;
    bool shift = mods & SSHLX_MOD_SHIFT;
    bool ctrl = mods & SSHLX_MOD_CTRL;
    bool alt = mods & SSHLX_MOD_ALT;
    bool raw_mouse_mode =
        (s->send_raw_mouse &&
         !(shift && conf_get_bool(s->conf, CONF_mouse_override)));

    s->cumulative_vscroll += ydelta * SCROLL_INCREMENT_LINES;

    if (!raw_mouse_mode) {
        int scroll_lines = (int)s->cumulative_vscroll; /* rounds toward 0 */
        if (scroll_lines) {
            term_scroll(s->term, 0, scroll_lines);
            s->cumulative_vscroll -= scroll_lines;
        }
    } else {
        int scroll_events = (int)(s->cumulative_vscroll /
                                  SCROLL_INCREMENT_LINES);
        if (scroll_events) {
            int button;
            s->cumulative_vscroll -= scroll_events * SCROLL_INCREMENT_LINES;
            if (scroll_events > 0) {
                button = MBT_WHEEL_DOWN;
            } else {
                button = MBT_WHEEL_UP;
                scroll_events = -scroll_events;
            }
            while (scroll_events-- > 0)
                term_mouse(s->term, button, translate_button(button),
                           MA_CLICK, col, row, shift, ctrl, alt);
        }
        s->cumulative_hscroll += xdelta * SCROLL_INCREMENT_LINES;
        scroll_events = (int)(s->cumulative_hscroll /
                              SCROLL_INCREMENT_LINES);
        if (scroll_events) {
            int button;
            s->cumulative_hscroll -= scroll_events * SCROLL_INCREMENT_LINES;
            if (scroll_events > 0) {
                button = MBT_WHEEL_RIGHT;
            } else {
                button = MBT_WHEEL_LEFT;
                scroll_events = -scroll_events;
            }
            while (scroll_events-- > 0)
                term_mouse(s->term, button, translate_button(button),
                           MA_CLICK, col, row, shift, ctrl, alt);
        }
    }
    sshlx_after_entry();
}

/* ---- clipboard ------------------------------------------------------------ */
void sshlx_session_paste(sshlx_session *s, const char *utf8)
{
    if (s->dead || !utf8)
        return;
    size_t paste_len;
    wchar_t *paste = dup_mb_to_wc_c(CS_UTF8, utf8, strlen(utf8), &paste_len);
    term_do_paste(s->term, paste, paste_len);
    sfree(paste);
    sshlx_after_entry();
}

void sshlx_session_request_paste(sshlx_session *s, int clipboard)
{
    if (s->dead)
        return;
    term_request_paste(s->term, clipboard);
    sshlx_after_entry();
}

void sshlx_session_copy(sshlx_session *s, int clipboard)
{
    if (s->dead)
        return;
    term_request_copy(s->term, &clipboard, 1);
    sshlx_after_entry();
}

void sshlx_session_copy_all(sshlx_session *s)
{
    if (s->dead)
        return;
    static const int clips[] = { COPYALL_CLIPBOARDS };
    term_copyall(s->term, clips, lenof(clips));
    sshlx_after_entry();
}

void sshlx_session_clipboard_lost(sshlx_session *s, int clipboard)
{
    if (s->dead)
        return;
    term_lost_clipboard_ownership(s->term, clipboard);
    sshlx_after_entry();
}

void sshlx_session_set_mouse_ui(sshlx_session *s, bool copy_on_select,
                                bool ui_right_button)
{
    if (s->dead)
        return;
    s->copy_on_select = copy_on_select;
    s->ui_right_button = ui_right_button;
    sshlx_setup_clipboards(s);
}

bool sshlx_session_bracketed_paste(const sshlx_session *s)
{
    return !s->dead && s->term->bracketed_paste && !s->term->no_bracketed_paste;
}

char *sshlx_session_all_text(sshlx_session *s)
{
    if (s->dead)
        return NULL;
    static const int clips[] = { CLIP_CLIPBOARD };
    s->capture_clip = true;
    term_copyall(s->term, clips, lenof(clips));
    s->capture_clip = false;
    char *text = s->captured_clip;
    s->captured_clip = NULL;
    sshlx_after_entry();
    return text;
}

char *sshlx_session_selection_text(sshlx_session *s)
{
    if (s->dead)
        return NULL;
    static const int clips[] = { CLIP_CLIPBOARD };
    s->capture_clip = true;
    term_request_copy(s->term, clips, lenof(clips));
    s->capture_clip = false;
    char *text = s->captured_clip;
    s->captured_clip = NULL;
    sshlx_after_entry();
    return text;
}

void sshlx_session_clear_scrollback(sshlx_session *s)
{
    if (s->dead)
        return;
    term_clrsb(s->term);
    sshlx_after_entry();
}

void sshlx_session_clear_terminal(sshlx_session *s)
{
    if (s->dead)
        return;
    Terminal *term = s->term;
    /* A full-screen program owns the alternate screen and repaints it. */
    if (!term->alt_which) {
        /* Scroll the cursor's line to the top, then blank below it. */
        char seq[64];
        int len = 0;
        if (term->curs.y > 0)
            len += sprintf(seq + len, "\033[%dS", term->curs.y);
        const int col = term->curs.x + 1;
        len += sprintf(seq + len, "\033[2;1H\033[J\033[1;%dH", col);
        term_data(term, seq, len);
    }
    term_clrsb(term);
    term_invalidate(term);
    sshlx_after_entry();
}

void sshlx_session_select_all(sshlx_session *s)
{
    if (s->dead)
        return;
    Terminal *term = s->term;
    term->selstart.y = -count234(term->scrollback);
    term->selstart.x = 0;
    term->selend.y = term->curs.y;
    term->selend.x = term->cols;
    term->selanchor = term->selstart;
    term->seltype = LEXICOGRAPHIC;
    term->selstate = SELECTED;
    static const int clips[] = { COPYALL_CLIPBOARDS };
    term_copyall(term, clips, lenof(clips));
    term_invalidate(term);
    sshlx_after_entry();
}

/* window.c reset_terminal_menuitem() */
void sshlx_session_reset_terminal(sshlx_session *s)
{
    if (s->dead)
        return;
    term_pwron(s->term, true);
    if (s->ldisc)
        ldisc_echoedit_update(s->ldisc);
    sshlx_after_entry();
}

/* ---- specials ---------------------------------------------------------------- */
size_t sshlx_session_specials(sshlx_session *s, const sshlx_special **out)
{
    const SessionSpecial *specials =
        (s->backend && !s->dead) ? backend_get_specials(s->backend) : NULL;
    if (!specials) {
        s->n_specials = 0;
        *out = s->specials;
        return 0;
    }
    if (specials != s->specials_src || !s->specials) {
        /* As gtk_seat_update_specials_menu(): walk until the EXITMENU
         * that closes the top level. */
        size_t n = 0;
        int depth = 0;
        for (size_t i = 0;; i++) {
            n++;
            if (specials[i].code == SS_SUBMENU)
                depth++;
            else if (specials[i].code == SS_EXITMENU) {
                if (depth == 0)
                    break;
                depth--;
            }
        }
        sfree(s->specials);
        s->specials = snewn(n, sshlx_special);
        size_t j = 0;
        depth = 0;
        for (size_t i = 0;; i++) {
            sshlx_special *sp = &s->specials[j];
            sp->name = specials[i].name;
            sp->index = (int)i;
            if (specials[i].code == SS_SUBMENU) {
                sp->kind = SSHLX_SPECIAL_SUBMENU;
                depth++;
            } else if (specials[i].code == SS_EXITMENU) {
                if (depth == 0)
                    break;
                sp->kind = SSHLX_SPECIAL_END_SUBMENU;
                depth--;
            } else if (specials[i].code == SS_SEP) {
                sp->kind = SSHLX_SPECIAL_SEPARATOR;
            } else {
                sp->kind = SSHLX_SPECIAL_ITEM;
            }
            j++;
        }
        s->n_specials = j;
        s->specials_src = specials;
    }
    *out = s->specials;
    return s->n_specials;
}

void sshlx_session_send_special(sshlx_session *s, int index)
{
    if (s->dead || !s->backend || !s->specials_src || index < 0)
        return;
    const SessionSpecial *sp = &s->specials_src[index];
    backend_special(s->backend, sp->code, sp->arg);
    sshlx_after_entry();
}

bool sshlx_session_trusted(const sshlx_session *s)
{
    return s->trusted;
}
