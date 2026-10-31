/*
 * engine_internal.h - private to the sshLX engine (C only). This is the
 * one place where PuTTY's headers meet sshLX's public API.
 */
#ifndef SSHLX_ENGINE_INTERNAL_H
#define SSHLX_ENGINE_INTERNAL_H

#include <stddef.h>
#include <stdbool.h>

#include "putty.h"
#include "terminal.h"
#include "storage.h"
#include "dialog.h"

#include "sshlx_engine.h"

/* sshlx_conf is a PuTTY Conf in disguise. */
#define CONF_OF(c)  ((Conf *)(c))
#define SCONF_OF(c) ((sshlx_conf *)(c))

/* ---- local shell back end (engine_shell.c) --------------------------- */
/* Outside PuTTY's PROT_* range, so it can never clash with a PuTTY one. */
#define SSHLX_PROT_LOCALSHELL 64
extern const BackendVtable sshlx_localshell_backend;

/* ---- host loop / process-global state (fe_hostloop.c) ---------------- */
extern sshlx_host_loop sshlx_loop;
extern sshlx_global_cb sshlx_global_callback;
extern void *sshlx_global_ctx;
void sshlx_global_event_fire(sshlx_global_event ev, const char *text);
/* Run after every engine entry point that might have queued work. */
void sshlx_after_entry(void);

/* ---- sessions -------------------------------------------------------- */
typedef struct pending_prompt pending_prompt;
struct pending_prompt {
    pending_prompt *next;
    sshlx_prompt_id id;
    sshlx_prompt_kind kind;
    /* host key: */
    char *host, *keytype, *keystr;
    int port;
    /* PuTTY continuation */
    void (*spr_callback)(void *ctx, SeatPromptResult result);
    void (*int_callback)(void *ctx, int result);
    void *callback_ctx;
    /* password hook: identity of the prompts_t and its continuation */
    prompts_t *prompts;
    toplevel_callback_fn_t userpass_callback;
    bool passphrase;
};

struct sshlx_session {
    /* PuTTY-facing objects; container_of() gets back to us */
    Seat seat;
    TermWin termwin;
    LogPolicy logpolicy;

    sshlx_session_callbacks cb;

    Conf *conf;
    Terminal *term;
    LogContext *logctx;
    Backend *backend;
    Ldisc *ldisc;
    struct unicode_data ucsdata;
    bool direct_to_font;

    sshlx_state state;
    bool exited;              /* GTK PuTTY's inst->exited */
    int exit_code;
    bool dead;                /* freed by the UI; teardown deferred */
    int depth;                /* re-entrancy depth of engine->UI calls */
    bool draw_active;

    /* cached Conf values (cache_conf_values in window.c) */
    int bold_style;
    int cursor_type;
    bool shadowbold;
    int shadowboldoffset;

    rgb palette[OSC4_NCOLOURS];

    /* keyboard state */
    int alt_keycode, alt_digits;
    int clipboard_ctrlshiftins, clipboard_ctrlshiftcv;

    /* mouse */
    bool send_raw_mouse, pointer_raw;
    bool copy_on_select, ui_right_button;
    double cumulative_vscroll, cumulative_hscroll;

    /* sshlx_session_all_text() catches term_copyall()'s clipboard write */
    bool capture_clip;
    char *captured_clip;

    /* event log */
    char **eventlog;
    size_t n_eventlog, eventlog_size;

    /* prompts */
    pending_prompt *prompts;
    sshlx_prompt_id next_prompt_id;
    bool password_hook;
    bool password_hook_declined;
    bool passphrase_hook_declined;
    char *hook_password;      /* answer waiting to be consumed */

    /* specials cache */
    sshlx_special *specials;
    const SessionSpecial *specials_src;
    size_t n_specials;

    int last_errno;
    bool trusted;

    sshlx_session *next_live;
};

extern sshlx_session *sshlx_live_sessions;

/* fe_seat.c */
extern const SeatVtable sshlx_seat_vt;
void sshlx_prompts_cancel_all(sshlx_session *s);
/* fe_termwin.c */
extern const TermWinVtable sshlx_termwin_vt;
void sshlx_setup_clipboards(sshlx_session *s);
/* fe_logpolicy.c */
extern const LogPolicyVtable sshlx_logpolicy_vt;
void sshlx_eventlog_add(sshlx_session *s, const char *line);
/* engine_session.c */
void sshlx_session_set_state(sshlx_session *s, sshlx_state st);
void sshlx_session_cache_conf(sshlx_session *s);
void sshlx_session_destroy_connection(sshlx_session *s);
bool sshlx_session_usable(const sshlx_session *s);
/* fe_keyboard.c */
sshlx_key_result sshlx_keyboard_event(sshlx_session *s,
                                      const sshlx_key_event *ev);

/* Wrap every call from engine into the UI so that sshlx_session_free()
 * from inside a callback is deferred safely. */
#define UI_CALL(s, fn, ...) do {                        \
        if ((s)->cb.fn && !(s)->dead) {                 \
            (s)->depth++;                               \
            (s)->cb.fn((s)->cb.ctx, ##__VA_ARGS__);     \
            (s)->depth--;                               \
        }                                               \
    } while (0)

#endif
