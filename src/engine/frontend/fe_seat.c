/*
 * fe_seat.c - sshLX's Seat implementation, modelled on gtk_seat_vt in
 * PuTTY's unix/window.c and the prompt dialogs in unix/dialog.c.
 *
 * Differences from GTK PuTTY are confined to presentation: host-key,
 * weak-crypto and log-append questions are raised as asynchronous prompt
 * events which the UI shows inside the tab, instead of modal windows. The
 * text, the choices and their effects are PuTTY's.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "engine_internal.h"

#define SESSION_OF(seat) container_of(seat, sshlx_session, seat)

/* ---- prompt bookkeeping --------------------------------------------- */
static pending_prompt *prompt_new(sshlx_session *s, sshlx_prompt_kind kind)
{
    pending_prompt *pp = snew(pending_prompt);
    memset(pp, 0, sizeof(*pp));
    pp->id = ++s->next_prompt_id;
    pp->kind = kind;
    pp->next = s->prompts;
    s->prompts = pp;
    return pp;
}

static void prompt_unlink_free(sshlx_session *s, pending_prompt *pp)
{
    for (pending_prompt **link = &s->prompts; *link; link = &(*link)->next) {
        if (*link == pp) {
            *link = pp->next;
            break;
        }
    }
    sfree(pp->host);
    sfree(pp->keytype);
    if (pp->keystr) {
        smemclr(pp->keystr, strlen(pp->keystr));
        sfree(pp->keystr);
    }
    sfree(pp);
}

static pending_prompt *prompt_find(sshlx_session *s, sshlx_prompt_id id)
{
    for (pending_prompt *pp = s->prompts; pp; pp = pp->next)
        if (pp->id == id)
            return pp;
    return NULL;
}

/* Cancel prompts belonging to the backend (everything except log-file
 * questions, which belong to the LogContext and survive reconnects). */
static void prompts_cancel(sshlx_session *s, bool include_log)
{
    pending_prompt **link = &s->prompts;
    while (*link) {
        pending_prompt *pp = *link;
        if (pp->kind == SSHLX_PROMPT_LOG_APPEND && !include_log) {
            link = &pp->next;
            continue;
        }
        sshlx_prompt_id id = pp->id;
        prompt_unlink_free(s, pp);
        UI_CALL(s, prompt_cancelled, id);
        link = &s->prompts;
    }
}

void sshlx_prompts_cancel_all(sshlx_session *s)
{
    prompts_cancel(s, true);
}

void sshlx_prompts_cancel_backend(sshlx_session *s)
{
    prompts_cancel(s, false);
}

static void emit_prompt(sshlx_session *s, pending_prompt *pp,
                        SeatDialogText *text, const char *filename,
                        const char *prompt_text)
{
    static const sshlx_dialog_text_item none[1];
    sshlx_dialog_text_item *items = NULL;
    size_t n = 0;
    if (text && text->nitems) {
        items = snewn(text->nitems, sshlx_dialog_text_item);
        for (size_t i = 0; i < text->nitems; i++) {
            items[i].kind = (sshlx_dt_kind)text->items[i].type;
            items[i].text = text->items[i].text;
        }
        n = text->nitems;
    }

    sshlx_prompt_request req;
    memset(&req, 0, sizeof(req));
    req.id = pp->id;
    req.kind = pp->kind;
    req.n_items = n;
    req.items = items ? items : none;
    req.host = pp->host;
    req.port = pp->port;
    req.keytype = pp->keytype;
    req.filename = filename;
    req.prompt_text = prompt_text;
    switch (pp->kind) {
      case SSHLX_PROMPT_HOSTKEY:
        /* Button labels as in unix/dialog.c */
        req.accept_label = "Accept";
        req.once_label = "Connect Once";
        req.cancel_label = "Cancel";
        break;
      case SSHLX_PROMPT_WEAK_CRYPTO:
      case SSHLX_PROMPT_WEAK_HOSTKEY:
        req.accept_label = "Yes";
        req.cancel_label = "No";
        break;
      case SSHLX_PROMPT_LOG_APPEND:
        req.accept_label = "Overwrite";
        req.once_label = "Append";
        req.cancel_label = "Disable";
        break;
      case SSHLX_PROMPT_PASSWORD:
        req.accept_label = "OK";
        req.cancel_label = "Type it in the terminal";
        break;
    }
    UI_CALL(s, prompt, &req);
    sfree(items);
}

void sshlx_session_answer_prompt(sshlx_session *s, sshlx_prompt_id id,
                                 sshlx_answer answer)
{
    pending_prompt *pp = prompt_find(s, id);
    if (!pp || s->dead)
        return;

    /* Detach first: the PuTTY callback may cause further prompts. */
    pending_prompt copy = *pp;
    pp->host = pp->keytype = pp->keystr = NULL;
    prompt_unlink_free(s, pp);

    switch (copy.kind) {
      case SSHLX_PROMPT_HOSTKEY: {
        SeatPromptResult res;
        if (answer == SSHLX_ANSWER_ACCEPT) {
            /* As confirm_ssh_host_key_result_callback(): Accept stores
             * the key, Connect Once doesn't, Cancel aborts. */
            store_host_key(&s->seat, copy.host, copy.port,
                           copy.keytype, copy.keystr);
            res = SPR_OK;
        } else if (answer == SSHLX_ANSWER_ONCE) {
            res = SPR_OK;
        } else {
            res = SPR_USER_ABORT;
        }
        copy.spr_callback(copy.callback_ctx, res);
        break;
      }
      case SSHLX_PROMPT_WEAK_CRYPTO:
      case SSHLX_PROMPT_WEAK_HOSTKEY:
        copy.spr_callback(copy.callback_ctx,
                          answer == SSHLX_ANSWER_ACCEPT ?
                          SPR_OK : SPR_USER_ABORT);
        break;
      case SSHLX_PROMPT_LOG_APPEND:
        /* logging.c: 2 = overwrite, 1 = append, 0 = disable logging */
        copy.int_callback(copy.callback_ctx,
                          answer == SSHLX_ANSWER_OVERWRITE ? 2 :
                          answer == SSHLX_ANSWER_APPEND ? 1 : 0);
        break;
      case SSHLX_PROMPT_PASSWORD:
        break;   /* handled by sshlx_session_answer_password */
    }

    sfree(copy.host);
    sfree(copy.keytype);
    if (copy.keystr) {
        smemclr(copy.keystr, strlen(copy.keystr));
        sfree(copy.keystr);
    }
    sshlx_after_entry();
}

/* ---- Seat methods -------------------------------------------------------- */
static size_t sshlx_seat_output(Seat *seat, SeatOutputType type,
                                const void *data, size_t len)
{
    sshlx_session *s = SESSION_OF(seat);
    (void)type;
    return term_data(s->term, data, len);
}

static bool sshlx_seat_eof(Seat *seat)
{
    (void)seat;
    return true;   /* do respond to incoming EOF with outgoing */
}

static SeatPromptResult sshlx_seat_get_userpass_input(Seat *seat,
                                                      prompts_t *p)
{
    sshlx_session *s = SESSION_OF(seat);

    /* Opt-in keyring hook: offer a single hidden prompt to the UI first.
     * PuTTY's own behaviour (prompt in the terminal) is the fallback and
     * the default. */
    if (s->password_hook && !s->password_hook_declined &&
        p->n_prompts == 1 && !p->prompts[0]->echo && p->callback) {
        if (s->hook_password) {
            prompt_set_result(p->prompts[0], s->hook_password);
            smemclr(s->hook_password, strlen(s->hook_password));
            sfree(s->hook_password);
            s->hook_password = NULL;
            /* Only offer the stored password once per connection, so a
             * wrong one falls back to PuTTY's prompt. */
            s->password_hook_declined = true;
            return SPR_OK;
        }
        for (pending_prompt *pp = s->prompts; pp; pp = pp->next)
            if (pp->kind == SSHLX_PROMPT_PASSWORD && pp->prompts == p)
                return SPR_INCOMPLETE;
        pending_prompt *pp = prompt_new(s, SSHLX_PROMPT_PASSWORD);
        pp->prompts = p;
        pp->callback_ctx = p->callback_ctx;
        pp->host = dupstr(conf_get_str(s->conf, CONF_host));
        pp->port = conf_get_int(s->conf, CONF_port);
        /* Stash the continuation so we never touch p after returning. */
        pp->userpass_callback = p->callback;
        emit_prompt(s, pp, NULL, NULL, p->prompts[0]->prompt);
        return SPR_INCOMPLETE;
    }
    return term_get_userpass_input(s->term, p);
}

void sshlx_session_set_password_hook(sshlx_session *s, bool enabled)
{
    s->password_hook = enabled;
}

void sshlx_session_answer_password(sshlx_session *s, sshlx_prompt_id id,
                                   const char *password)
{
    pending_prompt *pp = prompt_find(s, id);
    if (!pp || pp->kind != SSHLX_PROMPT_PASSWORD || s->dead)
        return;
    toplevel_callback_fn_t fn = pp->userpass_callback;
    void *ctx = pp->callback_ctx;
    prompt_unlink_free(s, pp);
    if (password) {
        s->hook_password = dupstr(password);
    } else {
        s->password_hook_declined = true;
    }
    /* Ask the SSH layer to call get_userpass_input again, exactly as
     * term_get_userpass_input() does when the user presses Return. */
    queue_toplevel_callback(fn, ctx);
    sshlx_after_entry();
}

static void sshlx_seat_notify_session_started(Seat *seat)
{
    sshlx_session *s = SESSION_OF(seat);
    sshlx_session_set_state(s, SSHLX_STATE_CONNECTED);
}

static void exit_callback(void *vctx)
{
    sshlx_session *s = (sshlx_session *)vctx;
    int exitcode;

    /* As window.c's exit_callback(). */
    if (!s->exited && s->backend &&
        (exitcode = backend_exitcode(s->backend)) >= 0) {
        int close_on_exit = conf_get_int(s->conf, CONF_close_on_exit);
        sshlx_session_destroy_connection(s);
        s->exit_code = exitcode;
        bool close_requested = (close_on_exit == FORCE_ON ||
                                (close_on_exit == AUTO && exitcode == 0));
        UI_CALL(s, exited, exitcode, close_requested);
    }
}

static void sshlx_seat_notify_remote_exit(Seat *seat)
{
    sshlx_session *s = SESSION_OF(seat);
    queue_toplevel_callback(exit_callback, s);
}

static void connection_fatal_callback(void *vctx)
{
    sshlx_session *s = (sshlx_session *)vctx;
    sshlx_session_destroy_connection(s);
}

static void sshlx_seat_connection_fatal(Seat *seat, const char *msg)
{
    sshlx_session *s = SESSION_OF(seat);
    int err = s->last_errno;
    s->last_errno = 0;
    sshlx_eventlog_add(s, msg);
    UI_CALL(s, connection_fatal, msg, err);
    s->exited = true;   /* suppress normal exit handling */
    queue_toplevel_callback(connection_fatal_callback, s);
}

static void sshlx_seat_nonfatal(Seat *seat, const char *msg)
{
    sshlx_session *s = SESSION_OF(seat);
    UI_CALL(s, nonfatal, msg);
}

static void sshlx_seat_update_specials_menu(Seat *seat)
{
    sshlx_session *s = SESSION_OF(seat);
    s->specials_src = NULL;    /* rebuild lazily */
    UI_CALL(s, specials_changed);
}

static char *sshlx_seat_get_ttymode(Seat *seat, const char *mode)
{
    sshlx_session *s = SESSION_OF(seat);
    return term_get_ttymode(s->term, mode);
}

static void sshlx_seat_set_busy_status(Seat *seat, BusyStatus status)
{
    sshlx_session *s = SESSION_OF(seat);
    UI_CALL(s, busy, (int)status);
}

static SeatPromptResult sshlx_seat_confirm_ssh_host_key(
    Seat *seat, const char *host, int port, const char *keytype,
    char *keystr, SeatDialogText *text, HelpCtx helpctx,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    sshlx_session *s = SESSION_OF(seat);
    (void)helpctx;
    pending_prompt *pp = prompt_new(s, SSHLX_PROMPT_HOSTKEY);
    pp->host = dupstr(host);
    pp->port = port;
    pp->keytype = dupstr(keytype);
    pp->keystr = dupstr(keystr);
    pp->spr_callback = callback;
    pp->callback_ctx = ctx;
    emit_prompt(s, pp, text, NULL, NULL);
    return SPR_INCOMPLETE;
}

static SeatPromptResult confirm_weak(
    Seat *seat, sshlx_prompt_kind kind, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    sshlx_session *s = SESSION_OF(seat);
    pending_prompt *pp = prompt_new(s, kind);
    pp->spr_callback = callback;
    pp->callback_ctx = ctx;
    emit_prompt(s, pp, text, NULL, NULL);
    return SPR_INCOMPLETE;
}

static SeatPromptResult sshlx_seat_confirm_weak_crypto_primitive(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    return confirm_weak(seat, SSHLX_PROMPT_WEAK_CRYPTO, text, callback, ctx);
}

static SeatPromptResult sshlx_seat_confirm_weak_cached_hostkey(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx)
{
    return confirm_weak(seat, SSHLX_PROMPT_WEAK_HOSTKEY, text, callback, ctx);
}

/* Log-file question, raised via the LogPolicy (fe_logpolicy.c). */
int sshlx_ask_append(sshlx_session *s, Filename *filename,
                     void (*callback)(void *ctx, int result), void *ctx)
{
    pending_prompt *pp = prompt_new(s, SSHLX_PROMPT_LOG_APPEND);
    pp->int_callback = callback;
    pp->callback_ctx = ctx;
    /* Text as in unix/dialog.c gtkdlg_askappend(). */
    SeatDialogText *text = seat_dialog_text_new();
    seat_dialog_text_append(text, SDT_TITLE, "%s Log to File", appname);
    seat_dialog_text_append(
        text, SDT_PARA,
        "The session log file \"%s\" already exists. You can overwrite it "
        "with a new session log, append your session log to the end of it, "
        "or disable session logging for this session.",
        filename_to_str(filename));
    emit_prompt(s, pp, text, filename_to_str(filename), NULL);
    seat_dialog_text_free(text);
    return -1;
}

static const SeatDialogPromptDescriptions *sshlx_seat_prompt_descriptions(
    Seat *seat)
{
    (void)seat;
    /* The in-tab prompt uses the same button names as GTK PuTTY, so the
     * same instructions apply. */
    static const SeatDialogPromptDescriptions descs = {
        .hk_accept_action = "press \"Accept\"",
        .hk_connect_once_action = "press \"Connect Once\"",
        .hk_cancel_action = "press \"Cancel\"",
        .hk_cancel_action_Participle = "Pressing \"Cancel\"",
        .weak_accept_action = "press \"Yes\"",
        .weak_cancel_action = "press \"No\"",
    };
    return &descs;
}

static bool sshlx_seat_is_utf8(Seat *seat)
{
    sshlx_session *s = SESSION_OF(seat);
    return s->ucsdata.line_codepage == CS_UTF8;
}

static const char *sshlx_seat_get_display(Seat *seat, SeatDisplayType dtype)
{
    (void)seat;
    (void)dtype;
    /* GTK PuTTY returns the GDK display name; we use $DISPLAY (on
     * Wayland, XWayland's) which is what GDK reports in practice. */
    return getenv("DISPLAY");
}

static bool sshlx_seat_get_windowid(Seat *seat, long *id)
{
    sshlx_session *s = SESSION_OF(seat);
    if (s->cb.window_id && !s->dead)
        return s->cb.window_id(s->cb.ctx, id);
    return false;
}

static bool sshlx_seat_get_window_pixel_size(Seat *seat, int *w, int *h)
{
    sshlx_session *s = SESSION_OF(seat);
    if (s->cb.window_pixel_size && !s->dead)
        return s->cb.window_pixel_size(s->cb.ctx, w, h);
    return false;
}

static StripCtrlChars *sshlx_seat_stripctrl_new(
    Seat *seat, BinarySink *bs_out, SeatInteractionContext sic)
{
    sshlx_session *s = SESSION_OF(seat);
    (void)sic;
    return stripctrl_new_term(bs_out, false, 0, s->term);
}

static void sshlx_seat_set_trust_status(Seat *seat, bool trusted)
{
    sshlx_session *s = SESSION_OF(seat);
    s->trusted = trusted;
    term_set_trust_status(s->term, trusted);
}

static bool sshlx_seat_can_set_trust_status(Seat *seat)
{
    (void)seat;
    return true;
}

static bool sshlx_seat_get_cursor_position(Seat *seat, int *x, int *y)
{
    sshlx_session *s = SESSION_OF(seat);
    if (s->term) {
        term_get_cursor_position(s->term, x, y);
        return true;
    }
    return false;
}

const SeatVtable sshlx_seat_vt = {
    .output = sshlx_seat_output,
    .eof = sshlx_seat_eof,
    .sent = nullseat_sent,
    .banner = nullseat_banner_to_stderr,
    .get_userpass_input = sshlx_seat_get_userpass_input,
    .notify_session_started = sshlx_seat_notify_session_started,
    .notify_remote_exit = sshlx_seat_notify_remote_exit,
    .notify_remote_disconnect = nullseat_notify_remote_disconnect,
    .connection_fatal = sshlx_seat_connection_fatal,
    .nonfatal = sshlx_seat_nonfatal,
    .update_specials_menu = sshlx_seat_update_specials_menu,
    .get_ttymode = sshlx_seat_get_ttymode,
    .set_busy_status = sshlx_seat_set_busy_status,
    .confirm_ssh_host_key = sshlx_seat_confirm_ssh_host_key,
    .confirm_weak_crypto_primitive = sshlx_seat_confirm_weak_crypto_primitive,
    .confirm_weak_cached_hostkey = sshlx_seat_confirm_weak_cached_hostkey,
    .prompt_descriptions = sshlx_seat_prompt_descriptions,
    .is_utf8 = sshlx_seat_is_utf8,
    .echoedit_update = nullseat_echoedit_update,
    .get_display = sshlx_seat_get_display,
    .get_windowid = sshlx_seat_get_windowid,
    .get_window_pixel_size = sshlx_seat_get_window_pixel_size,
    .stripctrl_new = sshlx_seat_stripctrl_new,
    .set_trust_status = sshlx_seat_set_trust_status,
    .can_set_trust_status = sshlx_seat_can_set_trust_status,
    .has_mixed_input_stream = nullseat_has_mixed_input_stream_yes,
    .verbose = nullseat_verbose_yes,
    .interactive = nullseat_interactive_yes,
    .get_cursor_position = sshlx_seat_get_cursor_position,
};
