/*
 * sshlx_engine.h - the only interface between the sshLX UI and the PuTTY
 * core. Plain C99, includes no PuTTY headers, safe to include from C++.
 *
 * Threading: PuTTY is single-threaded. Every function here must be called
 * on the thread that called sshlx_engine_init(), and every callback is
 * invoked on that thread.
 *
 * Strings: UTF-8, NUL-terminated. Strings and arrays passed *to* callbacks
 * are borrowed for the duration of the callback. Strings returned by
 * functions are borrowed until the next engine call on the same object,
 * unless documented as "caller frees with sshlx_free()".
 */
#ifndef SSHLX_ENGINE_H
#define SSHLX_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSHLX_ENGINE_API_VERSION 1

typedef struct sshlx_session sshlx_session;  /* one terminal session (tab) */
typedef struct sshlx_conf    sshlx_conf;     /* a PuTTY Conf */
typedef struct sshlx_dlg     sshlx_dlg;      /* one PuTTY config dialog */
typedef intptr_t sshlx_fd_t;
typedef uint32_t sshlx_prompt_id;

void sshlx_free(void *p);

/* ======================================================================
 * Host loop: implemented by the UI (Qt: QSocketNotifier, QTimer, queued
 * invocation). The engine never blocks.
 * ==================================================================== */
enum { SSHLX_IO_READ = 1, SSHLX_IO_WRITE = 2, SSHLX_IO_EXCEPT = 4 };

typedef struct sshlx_host_loop {
    void *ctx;
    /* Start watching fd for the given SSHLX_IO_* mask. Returns a token. */
    void *(*watch_fd)(void *ctx, sshlx_fd_t fd, int io_mask);
    void  (*unwatch_fd)(void *ctx, void *token);
    /* Arm the single engine timer to fire in ms milliseconds (replacing
     * any earlier setting); ms < 0 cancels. On expiry call
     * sshlx_engine_on_timer(). */
    void  (*set_timer)(void *ctx, long ms);
    /* Ask the UI to call sshlx_engine_run_pending() soon, from the event
     * loop (never synchronously from inside this call). */
    void  (*wake)(void *ctx);
} sshlx_host_loop;

typedef enum {
    SSHLX_GLOBAL_NONFATAL,        /* text: a non-session-specific warning */
    SSHLX_GLOBAL_OLD_KEYFILE,     /* text: old-format key file warning */
    SSHLX_GLOBAL_ABOUT_REQUESTED  /* "About" pressed in a config dialog */
} sshlx_global_event;
typedef void (*sshlx_global_cb)(void *ctx, sshlx_global_event ev,
                                const char *text);

/* Returns 0 on success. Must be called once before anything else. */
int  sshlx_engine_init(const sshlx_host_loop *loop,
                       sshlx_global_cb cb, void *cb_ctx);
/* Saves the random seed and closes network subsystems. */
void sshlx_engine_shutdown(void);
void sshlx_engine_on_fd(sshlx_fd_t fd, int io_mask);
void sshlx_engine_on_timer(void);
void sshlx_engine_run_pending(void);
const char *sshlx_engine_putty_version(void);   /* e.g. "Release 0.85" */
char *sshlx_engine_buildinfo(void);             /* caller frees */
/* Last fatal-message text produced by PuTTY's modalfatalbox() handler,
 * for a crash breadcrumb. Never returns NULL. */
const char *sshlx_engine_last_fatal(void);
/* Called synchronously (then the process exits) when PuTTY hits an
 * unrecoverable internal error. Must be async-signal-tolerant: write a
 * breadcrumb, don't run the event loop. */
void sshlx_engine_set_fatal_handler(void (*fn)(const char *msg));

/* ======================================================================
 * Saved sessions: PuTTY's own storage code and file format
 * (~/.config/putty/sessions or ~/.putty/sessions, or $PUTTYDIR).
 * ==================================================================== */
typedef struct sshlx_strlist { size_t n; char **items; } sshlx_strlist;
void sshlx_strlist_free(sshlx_strlist *l);

/* Includes "Default Settings" first, exactly as PuTTY lists them. */
sshlx_strlist *sshlx_saved_sessions_list(void);
sshlx_conf *sshlx_conf_new_default(void);            /* "Default Settings" */
/* Loads a saved session (or the defaults if it does not exist; check
 * *exists_out). */
sshlx_conf *sshlx_conf_load(const char *session_name, bool *exists_out);
/* Returns 0 on success; on failure *errmsg_out (caller frees). */
int  sshlx_conf_save(const sshlx_conf *conf, const char *session_name,
                     char **errmsg_out);
void sshlx_saved_session_delete(const char *session_name);
sshlx_conf *sshlx_conf_copy(const sshlx_conf *conf);
void sshlx_conf_copy_into(sshlx_conf *dst, const sshlx_conf *src);
void sshlx_conf_free(sshlx_conf *conf);
bool sshlx_conf_launchable(const sshlx_conf *conf);
char *sshlx_conf_dest(const sshlx_conf *conf);       /* caller frees */
/* Per-key value transfer, used for folder inheritance. The value of one
 * Conf key (including all its subkeys, e.g. every port forwarding) is
 * exported as an opaque printable string built from PuTTY's own
 * conf_serialise() format, without the build-specific key index. Caller
 * frees. Import replaces the key's value(s); returns false if the string
 * is malformed (conf is then unchanged). */
char *sshlx_conf_key_export(const sshlx_conf *conf, int key);
bool  sshlx_conf_key_import(sshlx_conf *conf, int key, const char *value);
/* Copy one key's value(s) from src to dst. */
void  sshlx_conf_key_copy(sshlx_conf *dst, const sshlx_conf *src, int key);
bool  sshlx_conf_key_equal(const sshlx_conf *a, const sshlx_conf *b, int key);

/* ======================================================================
 * Conf reflection (property grid). Metadata comes from PuTTY's own
 * conf_key_info[] table.
 * ==================================================================== */
typedef enum {
    SSHLX_T_NONE, SSHLX_T_BOOL, SSHLX_T_INT, SSHLX_T_STR,
    SSHLX_T_STR_AMBI, SSHLX_T_FILENAME, SSHLX_T_FONT
} sshlx_type;

typedef struct sshlx_enum_value {
    int value;          /* the Conf value */
    int storage_value;  /* the integer stored in the session file */
    bool obsolete;
} sshlx_enum_value;

typedef struct sshlx_conf_key_info {
    int key;                    /* index, valid for this build */
    const char *id;             /* PuTTY's CONF_ name without prefix, e.g. "host" */
    const char *save_keyword;   /* e.g. "HostName"; NULL if saved by custom code */
    sshlx_type value_type;
    sshlx_type subkey_type;     /* SSHLX_T_NONE for a scalar key */
    bool not_saved;             /* not persisted at all */
    bool has_default;
    bool default_bool;
    int  default_int;
    const char *default_str;
    size_t n_enum;
    const sshlx_enum_value *enum_values;
} sshlx_conf_key_info;

size_t sshlx_conf_key_count(void);
const sshlx_conf_key_info *sshlx_conf_key_get(size_t key);
int  sshlx_conf_key_find(const char *id_or_save_keyword);  /* -1 if none */

/* Getters never abort: a key absent from this Conf reads as its default. */
bool sshlx_conf_has_key(const sshlx_conf *c, int key);
bool sshlx_conf_get_bool(const sshlx_conf *c, int key);
int  sshlx_conf_get_int (const sshlx_conf *c, int key);
/* STR, STR_AMBI, FILENAME (path) and FONT (PuTTY font name) values. */
const char *sshlx_conf_get_str(const sshlx_conf *c, int key);
void sshlx_conf_set_bool(sshlx_conf *c, int key, bool v);
void sshlx_conf_set_int (sshlx_conf *c, int key, int v);
void sshlx_conf_set_str (sshlx_conf *c, int key, const char *v);
/* Keyed values (PuTTY has int->int and string->string maps). get_str_str
 * returns NULL when absent. */
const char *sshlx_conf_get_str_str(const sshlx_conf *c, int key, const char *sub);
int  sshlx_conf_get_int_int(const sshlx_conf *c, int key, int sub, int dflt);
void sshlx_conf_set_str_str(sshlx_conf *c, int key, const char *sub, const char *v);
void sshlx_conf_set_int_int(sshlx_conf *c, int key, int sub, int v);
void sshlx_conf_del_str_str(sshlx_conf *c, int key, const char *sub);
sshlx_strlist *sshlx_conf_str_subkeys(const sshlx_conf *c, int key);

typedef struct sshlx_protocol {
    int id; const char *name; const char *display_name; int default_port;
} sshlx_protocol;
size_t sshlx_protocol_count(void);
const sshlx_protocol *sshlx_protocol_get(size_t i);
/* A copy of an SSH conf for a file transfer connection: runs command (a
 * subsystem if subsystem is true) with no pty, forwarding or session log.
 * Caller frees with sshlx_conf_free. */
sshlx_conf *sshlx_conf_for_transfer(const sshlx_conf *conf, const char *command,
                                    bool subsystem);

int sshlx_conf_key_protocol(void);   /* convenience: key of CONF_protocol */

/* ======================================================================
 * Sessions
 * ==================================================================== */
typedef enum {
    SSHLX_STATE_IDLE,          /* created, never connected */
    SSHLX_STATE_CONNECTING,
    SSHLX_STATE_CONNECTED,     /* backend reports connected */
    SSHLX_STATE_DISCONNECTED   /* backend gone; terminal kept */
} sshlx_state;

enum {   /* sshlx_text_style.flags */
    SSHLX_A_BOLD_FONT  = 1 << 0,   /* draw with a bold font */
    SSHLX_A_UNDER      = 1 << 1,
    SSHLX_A_STRIKE     = 1 << 2,
    SSHLX_A_WIDE       = 1 << 3,   /* each char occupies 2 cells */
    SSHLX_A_COMBINING  = 1 << 4,   /* text = one cell: base + combiners */
    SSHLX_A_DIM        = 1 << 5,   /* informational; colours already dimmed */
    SSHLX_A_BLINK      = 1 << 6,
    SSHLX_A_SHADOW_BOLD = 1 << 7   /* bold via overstrike (Conf shadowbold) */
};
enum {   /* line attributes */
    SSHLX_LINE_NORMAL = 0, SSHLX_LINE_WIDE = 1,
    SSHLX_LINE_TOP = 2, SSHLX_LINE_BOTTOM = 3
};
enum {   /* cursor shapes for draw_cursor_shape */
    SSHLX_CURSOR_BOX_OUTLINE = 0,  /* passive block */
    SSHLX_CURSOR_UNDERLINE = 1,
    SSHLX_CURSOR_VBAR = 2
};

typedef struct sshlx_rgb { uint8_t r, g, b; } sshlx_rgb;

typedef struct sshlx_text_style {
    sshlx_rgb fg, bg;    /* final colours: palette, reverse, bold-as-colour
                            and dim already applied */
    uint32_t flags;      /* SSHLX_A_* */
    int line_attr;       /* SSHLX_LINE_* */
    int shadow_bold_offset;
} sshlx_text_style;

/* Clipboards, as PuTTY numbers them on Unix. */
enum {
    SSHLX_CLIP_NULL = 0, SSHLX_CLIP_LOCAL = 1,
    SSHLX_CLIP_PRIMARY = 2, SSHLX_CLIP_CLIPBOARD = 3,
    SSHLX_CLIP_CUSTOM_1 = 4, SSHLX_CLIP_CUSTOM_2 = 5, SSHLX_CLIP_CUSTOM_3 = 6
};

typedef enum {
    SSHLX_PROMPT_HOSTKEY,       /* answers: ACCEPT (store) / ONCE / CANCEL */
    SSHLX_PROMPT_WEAK_CRYPTO,   /* ACCEPT / CANCEL */
    SSHLX_PROMPT_WEAK_HOSTKEY,  /* ACCEPT / CANCEL */
    SSHLX_PROMPT_LOG_APPEND,    /* OVERWRITE / APPEND / CANCEL (no logging) */
    SSHLX_PROMPT_PASSWORD       /* only with sshlx_session_set_password_hook */
} sshlx_prompt_kind;

/* Mirrors PuTTY's SeatDialogTextType. */
typedef enum {
    SSHLX_DT_PARA, SSHLX_DT_DISPLAY, SSHLX_DT_SCARY_HEADING,
    SSHLX_DT_TITLE, SSHLX_DT_PROMPT, SSHLX_DT_BATCH_ABORT,
    SSHLX_DT_MORE_INFO_KEY, SSHLX_DT_MORE_INFO_VALUE_SHORT,
    SSHLX_DT_MORE_INFO_VALUE_BLOB
} sshlx_dt_kind;

typedef struct sshlx_dialog_text_item {
    sshlx_dt_kind kind; const char *text;
} sshlx_dialog_text_item;

typedef struct sshlx_prompt_request {
    sshlx_prompt_id id;
    sshlx_prompt_kind kind;
    size_t n_items;
    const sshlx_dialog_text_item *items;
    /* button labels in PuTTY's words (accept/once/cancel or yes/no) */
    const char *accept_label, *once_label, *cancel_label;
    /* HOSTKEY: */
    const char *host; int port; const char *keytype;
    /* LOG_APPEND: */
    const char *filename;
    /* PASSWORD: the prompt text and remote user/host for keyring lookup */
    const char *prompt_text;
    /* PASSWORD: this asks for a private key's passphrase, not a login password */
    bool passphrase;
} sshlx_prompt_request;

typedef enum {
    SSHLX_ANSWER_ACCEPT, SSHLX_ANSWER_ONCE, SSHLX_ANSWER_CANCEL,
    SSHLX_ANSWER_OVERWRITE, SSHLX_ANSWER_APPEND
} sshlx_answer;

typedef struct sshlx_session_callbacks {
    void *ctx;

    /* Drawing. Coordinates are in character cells. begin_draw returns
     * false if the UI cannot draw right now (draws are then skipped and
     * the engine repaints later on sshlx_session_paint()). */
    bool (*begin_draw)(void *ctx);
    /* Fill cells [col, col+ncells) of row with bg, then draw text.
     * For double-width/height lines (line_attr != NORMAL) col and ncells
     * are already in doubled units and text must be stretched 2x. */
    void (*draw_text)(void *ctx, int col, int row, int ncells,
                      const uint32_t *text, int len,
                      const sshlx_text_style *st);
    /* Non-block cursor or passive cursor outline, drawn after the cell. */
    void (*draw_cursor_shape)(void *ctx, int col, int row, int ncells,
                              int shape, bool passive, bool right_edge,
                              sshlx_rgb colour, int line_attr);
    void (*draw_trust_sigil)(void *ctx, int col, int row);
    void (*end_draw)(void *ctx);

    void (*set_cursor_pos)(void *ctx, int col, int row);   /* IME anchor */
    void (*set_scrollbar)(void *ctx, int total, int start, int page);
    void (*set_raw_mouse_mode)(void *ctx, bool raw);
    void (*set_raw_mouse_pointer)(void *ctx, bool raw);
    void (*bell)(void *ctx, int mode);                /* PuTTY BELL_* mode */
    void (*set_title)(void *ctx, const char *title);
    void (*set_icon_title)(void *ctx, const char *title);
    /* Server asked for a new terminal size; the UI must eventually call
     * sshlx_session_resize_done() whether or not it honoured it. */
    void (*request_resize)(void *ctx, int cols, int rows);
    void (*set_minimised)(void *ctx, bool minimised);
    void (*set_maximised)(void *ctx, bool maximised);
    void (*move_window)(void *ctx, int x, int y);
    void (*set_zorder)(void *ctx, bool top);
    void (*refresh)(void *ctx);
    void (*palette_changed)(void *ctx, sshlx_rgb default_bg);
    /* Bytes received from the remote side, after the terminal has had them
     * (may be NULL). Do not send from inside this. */
    void (*output_data)(void *ctx, const char *data, size_t len);
    /* Raw mode only, may be NULL: the remote's stderr, instead of the terminal. */
    void (*stderr_data)(void *ctx, const char *data, size_t len);

    /* Clipboard. utf8 is the selection; must_deselect as in PuTTY. */
    void (*clip_write)(void *ctx, int clipboard, const char *utf8,
                       bool must_deselect);
    /* Fetch the clipboard asynchronously and call sshlx_session_paste(). */
    void (*clip_request_paste)(void *ctx, int clipboard);

    /* Session state */
    void (*state_changed)(void *ctx, sshlx_state state);
    /* Fatal connection error. sys_errno is the errno behind it when known
     * (e.g. EACCES opening a serial port), else 0. */
    void (*connection_fatal)(void *ctx, const char *message, int sys_errno);
    void (*nonfatal)(void *ctx, const char *message);
    void (*event_log)(void *ctx, const char *line);
    void (*logging_error)(void *ctx, const char *message);
    void (*specials_changed)(void *ctx);
    void (*busy)(void *ctx, int busy_status);
    /* The remote side exited; close_requested reflects CONF_close_on_exit
     * as PuTTY would apply it. */
    void (*exited)(void *ctx, int exit_code, bool close_requested);
    /* Async prompts: show inside the tab, answer later. */
    void (*prompt)(void *ctx, const sshlx_prompt_request *req);
    void (*prompt_cancelled)(void *ctx, sshlx_prompt_id id);

    /* Platform queries (may be NULL). */
    bool (*window_pixel_size)(void *ctx, int *w, int *h);
    bool (*window_id)(void *ctx, long *id);        /* X11 window id */
} sshlx_session_callbacks;

/* conf is copied. The callbacks struct is copied. */
sshlx_session *sshlx_session_new(const sshlx_conf *conf,
                                 const sshlx_session_callbacks *cb,
                                 int cols, int rows);
/* Destroys the session. Safe to call from inside one of its callbacks:
 * the actual teardown is then deferred. No callbacks are made after this
 * returns. */
void sshlx_session_free(sshlx_session *s);

/* Returns 0 if the backend started (state CONNECTING/CONNECTED), -1 if it
 * failed immediately (connection_fatal has been called). */
int  sshlx_session_connect(sshlx_session *s);
void sshlx_session_disconnect(sshlx_session *s);
/* PuTTY's "Restart Session": only valid when disconnected. */
int  sshlx_session_reconnect(sshlx_session *s);
sshlx_state sshlx_session_state(const sshlx_session *s);
int  sshlx_session_exit_code(const sshlx_session *s);       /* -1 if none */
const sshlx_conf *sshlx_session_conf(const sshlx_session *s);
/* A copy of the session's Conf prepared for a mid-session settings dialog
 * (PuTTY's term_pre_reconfig). Caller frees with sshlx_conf_free. */
sshlx_conf *sshlx_session_reconfig_conf(sshlx_session *s);
/* Apply new settings mid-session (PuTTY "Change Settings"). Returns a
 * bitmask: 1 = fonts/codepage changed (UI should reload fonts and call
 * sshlx_session_resize), 2 = window size settings changed. */
int  sshlx_session_reconfigure(sshlx_session *s, const sshlx_conf *newconf);
/* Text for a "really close?" warning, or NULL. Caller frees. */
char *sshlx_session_close_warning(sshlx_session *s);
/* Backend protocol cfg_info for mid-session config dialogs. */
int  sshlx_session_cfg_info(sshlx_session *s);

/* Size and painting */
void sshlx_session_resize(sshlx_session *s, int cols, int rows);
void sshlx_session_resize_done(sshlx_session *s);
void sshlx_session_paint(sshlx_session *s, int col0, int row0,
                         int col1, int row1);   /* inclusive cells */
void sshlx_session_invalidate(sshlx_session *s);
void sshlx_session_focus(sshlx_session *s, bool has_focus);
/* As PuTTY's term_scroll(): whence +1 = absolute position from the top of
 * the scrollback (scrollbar), 0 = relative, -1 = relative to the bottom. */
/* Scroll so that line y of the whole buffer (0 = oldest) is at the top and
 * draw it now, rather than on the next timer. */
void sshlx_session_show_line(sshlx_session *s, int y);
void sshlx_session_scroll(sshlx_session *s, int whence, int lines);
void sshlx_session_get_size(const sshlx_session *s, int *cols, int *rows);
sshlx_rgb sshlx_session_default_bg(const sshlx_session *s);
/* Cursor blink is driven by the engine's timers; nothing to do in UI. */

/* Keyboard. keysym is an X11 keysym (on X11/Wayland Qt's
 * nativeVirtualKey(); otherwise mapped from Qt::Key by the UI). */
enum {
    SSHLX_MOD_SHIFT = 1, SSHLX_MOD_CTRL = 2, SSHLX_MOD_ALT = 4,
    SSHLX_MOD_SUPER = 8, SSHLX_MOD_KEYPAD = 16
};
typedef struct sshlx_key_event {
    uint32_t keysym;
    uint32_t mods;          /* SSHLX_MOD_* */
    const char *text;       /* what the keyboard layout produced, UTF-8 */
    bool release;           /* key release (needed for Alt+numpad codes) */
} sshlx_key_event;
typedef enum {
    SSHLX_KEY_NOT_HANDLED = 0,  /* let the UI handle it (e.g. IME) */
    SSHLX_KEY_HANDLED = 1,
    SSHLX_KEY_FONT_BIGGER = 2,  /* Ctrl+> : UI should enlarge the font */
    SSHLX_KEY_FONT_SMALLER = 3  /* Ctrl+< */
} sshlx_key_result;
/* Applies the session's PuTTY keyboard configuration (port of GTK PuTTY's
 * key_event). */
sshlx_key_result sshlx_session_key(sshlx_session *s, const sshlx_key_event *ev);
/* IME commit / plain text typed. */
void sshlx_session_text(sshlx_session *s, const char *utf8);

/* Raw mode, for file transfer: once the session has started, the remote
 * stdout goes only to the output_data callback, never to the terminal. The
 * login and stderr still appear there. The callback must not disconnect
 * the session directly: it runs inside the SSH code. send_bytes bypasses the line discipline, so binary
 * data arrives unchanged. */
void sshlx_session_set_raw(sshlx_session *s, bool raw);
void sshlx_session_send_bytes(sshlx_session *s, const char *data, size_t len);
size_t sshlx_session_send_backlog(sshlx_session *s);
/* True once the remote command or subsystem has really started. CONNECTED
 * comes earlier, and bytes sent in between reach the server too soon. */
bool sshlx_session_send_ready(sshlx_session *s);
/* Closes the session log, or opens it again, appending to the same file. The
 * session's Conf is left alone. */
void sshlx_session_log_set_paused(sshlx_session *s, bool paused);
void sshlx_session_preedit(sshlx_session *s, const char *utf8_or_null);
/* Send raw bytes to the backend as if typed (e.g. "send command"). */
void sshlx_session_send_raw(sshlx_session *s, const char *data, size_t len);

/* Mouse. Buttons/actions as in PuTTY. col/row may be outside the
 * terminal while dragging. */
typedef enum {
    SSHLX_MB_NOTHING = 0, SSHLX_MB_LEFT, SSHLX_MB_MIDDLE, SSHLX_MB_RIGHT,
    SSHLX_MB_WHEEL_UP, SSHLX_MB_WHEEL_DOWN,
    SSHLX_MB_WHEEL_LEFT, SSHLX_MB_WHEEL_RIGHT
} sshlx_mbutton;
typedef enum {
    SSHLX_MA_CLICK, SSHLX_MA_2CLK, SSHLX_MA_3CLK,
    SSHLX_MA_DRAG, SSHLX_MA_RELEASE, SSHLX_MA_MOVE
} sshlx_maction;
/* Returns true if the event was consumed by the terminal. Ctrl+right
 * click returns false so the UI can show its context menu, as PuTTY does. */
bool sshlx_session_mouse(sshlx_session *s, sshlx_mbutton b, sshlx_maction a,
                         int col, int row, uint32_t mods);
/* Wheel with fractional deltas in "notches" (positive = down/right). */
void sshlx_session_wheel(sshlx_session *s, double dx, double dy,
                         int col, int row, uint32_t mods);

void sshlx_session_paste(sshlx_session *s, const char *utf8_or_null);
void sshlx_session_request_paste(sshlx_session *s, int clipboard);
void sshlx_session_copy(sshlx_session *s, int clipboard);
void sshlx_session_copy_all(sshlx_session *s);
void sshlx_session_clipboard_lost(sshlx_session *s, int clipboard);
/* copy_on_select: a mouse selection also fills CLIPBOARD, whatever the Conf
 * says. ui_right_button: plain right clicks stop reaching the selection
 * code (sshlx_session_mouse returns false and the UI acts on them); a
 * program that asked for the mouse still gets them. Neither is stored in
 * the Conf, which is shared with PuTTY. */
void sshlx_session_set_mouse_ui(sshlx_session *s, bool copy_on_select,
                                bool ui_right_button);
/* True while the running program has bracketed paste on, i.e. a paste
 * cannot run commands by itself. */
bool sshlx_session_bracketed_paste(const sshlx_session *s);
/* All scrollback and screen text as UTF-8, or NULL. sshlx_free() it. */
char *sshlx_session_all_text(sshlx_session *s);
/* The latest mouse selection as UTF-8, or NULL. sshlx_free() it. */
char *sshlx_session_selection_text(sshlx_session *s);
void sshlx_session_clear_scrollback(sshlx_session *s);
/* Clear Terminal: the scrollback and the screen, except the cursor's line,
 * which moves to the top so a prompt and anything typed at it survive. */
void sshlx_session_clear_terminal(sshlx_session *s);
/* Selects everything, scrollback included, and copies it. */
void sshlx_session_select_all(sshlx_session *s);
void sshlx_session_reset_terminal(sshlx_session *s);

/* Special commands (the backend's specials menu). */
typedef enum {
    SSHLX_SPECIAL_ITEM, SSHLX_SPECIAL_SEPARATOR,
    SSHLX_SPECIAL_SUBMENU, SSHLX_SPECIAL_END_SUBMENU
} sshlx_special_kind;
typedef struct sshlx_special {
    sshlx_special_kind kind; const char *name; int index;
} sshlx_special;
/* Returns the number of entries; *out valid until the next call. */
size_t sshlx_session_specials(sshlx_session *s, const sshlx_special **out);
void sshlx_session_send_special(sshlx_session *s, int index);

/* Event log (PuTTY's Event Log window contents). */
size_t sshlx_session_event_log(const sshlx_session *s,
                               const char *const **lines_out);

/* Prompts */
void sshlx_session_answer_prompt(sshlx_session *s, sshlx_prompt_id id,
                                 sshlx_answer answer);
/* Opt-in keyring support: when enabled, a single hidden password prompt
 * is first offered to the UI as SSHLX_PROMPT_PASSWORD. The UI answers
 * with sshlx_session_answer_password(); answering NULL falls back to
 * PuTTY's normal in-terminal prompt. */
void sshlx_session_set_password_hook(sshlx_session *s, bool enabled);
void sshlx_session_answer_password(sshlx_session *s, sshlx_prompt_id id,
                                   const char *password_or_null);

/* Trust status (anti-spoofing): true while output is from PuTTY itself. */
bool sshlx_session_trusted(const sshlx_session *s);

/* ======================================================================
 * Generic PuTTY configuration dialog bridge. The engine builds PuTTY's
 * own controlbox (config.c + unix additions) and implements the dlg_*
 * API over a C-side model; the UI renders the model and reports edits.
 * ==================================================================== */
typedef enum {
    SSHLX_C_TEXT, SSHLX_C_EDITBOX, SSHLX_C_RADIO, SSHLX_C_CHECKBOX,
    SSHLX_C_BUTTON, SSHLX_C_LISTBOX, SSHLX_C_COLUMNS,
    SSHLX_C_FILESELECT, SSHLX_C_FONTSELECT
} sshlx_ctrl_type;

typedef struct sshlx_ctrl_desc {
    int id;                     /* unique within the dialog */
    sshlx_ctrl_type type;
    const char *label;          /* may contain no shortcut marker */
    char shortcut;              /* '\0' if none */
    int column, column_span;    /* position in current COLUMNS layout */
    /* COLUMNS: new layout for following controls */
    int ncolumns; const int *column_percents;
    /* RADIO */
    int radio_ncolumns; int nbuttons; const char *const *buttons;
    /* EDITBOX */
    bool password; bool has_list; int percent_width;
    /* LISTBOX: height 0 = drop-down list */
    int list_height; bool draglist; int multisel;
    int list_ncols; const int *list_percents;
    /* BUTTON */
    bool is_default, is_cancel;
    /* FILESELECT */
    bool for_writing; const char *file_title; int file_filter;
    /* TEXT */
    bool wrap;
    /* Help/context: the PuTTY help topic (empty on Unix builds). */
    int align_next_to;          /* id of control this aligns with, or -1 */
} sshlx_ctrl_desc;

typedef struct sshlx_ctrl_set {
    const char *path;           /* "Connection/SSH/Kex"; "" = action area */
    const char *title;          /* box title, may be NULL */
    const char *panel_title;    /* title of the panel (ctrl_settitle) or NULL */
    size_t n_ctrls;
    const sshlx_ctrl_desc *ctrls;
} sshlx_ctrl_set;

typedef struct sshlx_dlg_callbacks {
    void *ctx;
    void (*ctrl_changed)(void *ctx, int ctrl_id);   /* re-read its state */
    void (*label_changed)(void *ctx, int ctrl_id, const char *label);
    void (*set_focus)(void *ctx, int ctrl_id);
    void (*select_range)(void *ctx, int ctrl_id, size_t start, size_t len);
    void (*beep)(void *ctx);
    void (*error)(void *ctx, const char *msg);
    void (*end)(void *ctx, int value);              /* close the dialog */
    void (*colour_select)(void *ctx, int ctrl_id, int r, int g, int b);
    void (*open_ca_config)(void *ctx);
    void (*about)(void *ctx);
} sshlx_dlg_callbacks;

/* conf is edited in place by PuTTY's handlers; the caller owns it and
 * must keep it alive until sshlx_dlg_free. */
sshlx_dlg *sshlx_dlg_new_config(sshlx_conf *conf, bool midsession,
                                int protcfginfo,
                                const sshlx_dlg_callbacks *cb);
sshlx_dlg *sshlx_dlg_new_ca_config(const sshlx_dlg_callbacks *cb);
void sshlx_dlg_free(sshlx_dlg *d);
/* Fill all controls from the Conf (PuTTY's EVENT_REFRESH). Call once
 * after building the UI. */
void sshlx_dlg_refresh_all(sshlx_dlg *d);
size_t sshlx_dlg_sets(const sshlx_dlg *d, const sshlx_ctrl_set **out);

const char *sshlx_dlg_get_text(const sshlx_dlg *d, int ctrl_id);
int  sshlx_dlg_get_int(const sshlx_dlg *d, int ctrl_id);   /* radio/checkbox */
size_t sshlx_dlg_list_count(const sshlx_dlg *d, int ctrl_id);
const char *sshlx_dlg_list_item(const sshlx_dlg *d, int ctrl_id, size_t i);
bool sshlx_dlg_list_selected(const sshlx_dlg *d, int ctrl_id, size_t i);

/* UI-originated changes; each runs the PuTTY handler. */
void sshlx_dlg_set_text(sshlx_dlg *d, int ctrl_id, const char *utf8);
void sshlx_dlg_set_int(sshlx_dlg *d, int ctrl_id, int value);
void sshlx_dlg_list_select(sshlx_dlg *d, int ctrl_id, size_t i, bool sel,
                           bool exclusive);
void sshlx_dlg_activate(sshlx_dlg *d, int ctrl_id);   /* button / dbl-click */
void sshlx_dlg_list_move(sshlx_dlg *d, int ctrl_id, size_t i, int direction);
void sshlx_dlg_colour_result(sshlx_dlg *d, int ctrl_id, bool ok,
                             int r, int g, int b);
void sshlx_dlg_focus_changed(sshlx_dlg *d, int ctrl_id);
/* True if PuTTY's handlers said the dialog should be visible on that path
 * (dlg_is_visible support): the UI reports the currently shown panel. */
void sshlx_dlg_set_visible_path(sshlx_dlg *d, const char *path);

#ifdef __cplusplus
}
#endif
#endif /* SSHLX_ENGINE_H */
