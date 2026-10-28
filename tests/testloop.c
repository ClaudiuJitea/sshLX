#define _GNU_SOURCE
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "testloop.h"

/* ---- host loop ----------------------------------------------------------- */
typedef struct watch {
    sshlx_fd_t fd;
    int mask;
    bool live;
} watch;

#define MAX_WATCHES 256
static watch watches[MAX_WATCHES];
static long long timer_deadline = -1;
static bool wake_pending;

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void *tl_watch_fd(void *ctx, sshlx_fd_t fd, int mask)
{
    (void)ctx;
    for (int i = 0; i < MAX_WATCHES; i++) {
        if (!watches[i].live) {
            watches[i].fd = fd;
            watches[i].mask = mask;
            watches[i].live = true;
            return &watches[i];
        }
    }
    fprintf(stderr, "testloop: too many watches\n");
    exit(1);
}

static void tl_unwatch_fd(void *ctx, void *token)
{
    (void)ctx;
    ((watch *)token)->live = false;
}

static void tl_set_timer(void *ctx, long ms)
{
    (void)ctx;
    timer_deadline = ms < 0 ? -1 : now_ms() + ms;
}

static void tl_wake(void *ctx)
{
    (void)ctx;
    wake_pending = true;
}

static void tl_global(void *ctx, sshlx_global_event ev, const char *text)
{
    (void)ctx;
    fprintf(stderr, "[global event %d] %s\n", (int)ev, text);
}

void tl_init(void)
{
    sshlx_host_loop loop = {
        .ctx = NULL,
        .watch_fd = tl_watch_fd,
        .unwatch_fd = tl_unwatch_fd,
        .set_timer = tl_set_timer,
        .wake = tl_wake,
    };
    if (sshlx_engine_init(&loop, tl_global, NULL) != 0) {
        fprintf(stderr, "engine init failed\n");
        exit(1);
    }
}

static void tl_iterate(int max_wait_ms)
{
    if (wake_pending) {
        wake_pending = false;
        sshlx_engine_run_pending();
        return;
    }
    struct pollfd pfds[MAX_WATCHES];
    int idx[MAX_WATCHES];
    int n = 0;
    for (int i = 0; i < MAX_WATCHES; i++) {
        if (!watches[i].live)
            continue;
        pfds[n].fd = (int)watches[i].fd;
        pfds[n].events = 0;
        if (watches[i].mask & SSHLX_IO_READ) pfds[n].events |= POLLIN;
        if (watches[i].mask & SSHLX_IO_WRITE) pfds[n].events |= POLLOUT;
        if (watches[i].mask & SSHLX_IO_EXCEPT) pfds[n].events |= POLLPRI;
        pfds[n].revents = 0;
        idx[n] = i;
        n++;
    }
    int wait = max_wait_ms;
    if (timer_deadline >= 0) {
        long long d = timer_deadline - now_ms();
        if (d < 0) d = 0;
        if (d < wait) wait = (int)d;
    }
    int r = poll(pfds, n, wait);
    if (r > 0) {
        for (int k = 0; k < n; k++) {
            if (!pfds[k].revents)
                continue;
            int i = idx[k];
            if (!watches[i].live || watches[i].fd != pfds[k].fd)
                continue;
            int mask = 0;
            if (pfds[k].revents & (POLLIN | POLLHUP | POLLERR))
                mask |= SSHLX_IO_READ;
            if (pfds[k].revents & POLLOUT) mask |= SSHLX_IO_WRITE;
            if (pfds[k].revents & POLLPRI) mask |= SSHLX_IO_EXCEPT;
            mask &= watches[i].mask | SSHLX_IO_READ;
            if (mask)
                sshlx_engine_on_fd(pfds[k].fd, mask);
        }
    }
    if (timer_deadline >= 0 && now_ms() >= timer_deadline) {
        timer_deadline = -1;
        sshlx_engine_on_timer();
    }
}

bool tl_run_until(bool (*pred)(void *ctx), void *ctx, int timeout_ms)
{
    long long end = now_ms() + timeout_ms;
    while (now_ms() < end) {
        if (pred && pred(ctx))
            return true;
        tl_iterate(20);
    }
    return pred ? pred(ctx) : true;
}

void tl_run_for(int ms)
{
    tl_run_until(NULL, NULL, ms);
}

/* ---- screen ---------------------------------------------------------------- */
static bool ts_begin(void *ctx) { (void)ctx; return true; }
static void ts_end(void *ctx) { ((test_screen *)ctx)->draws++; }

static void ts_draw_text(void *ctx, int col, int row, int ncells,
                         const uint32_t *text, int len,
                         const sshlx_text_style *st)
{
    test_screen *ts = ctx;
    if (row < 0 || row >= TS_ROWS)
        return;
    int cellw = (st->flags & SSHLX_A_WIDE) ? 2 : 1;
    if (st->flags & SSHLX_A_COMBINING)
        len = 1;
    for (int i = 0; i < ncells && col + i < TS_COLS; i++)
        ts->cells[row][col + i] = ' ';
    for (int i = 0; i < len; i++) {
        int c = col + i * cellw;
        if (c >= 0 && c < TS_COLS)
            ts->cells[row][c] = text[i];
    }
}

static void ts_cursor(void *ctx, int col, int row, int ncells, int shape,
                      bool passive, bool right, sshlx_rgb colour, int la)
{
    (void)ctx; (void)col; (void)row; (void)ncells; (void)shape;
    (void)passive; (void)right; (void)colour; (void)la;
}

static void ts_set_title(void *ctx, const char *t)
{
    test_screen *ts = ctx;
    snprintf(ts->title, sizeof(ts->title), "%s", t);
}

static void ts_state(void *ctx, sshlx_state st)
{
    test_screen *ts = ctx;
    ts->state = st;
    fprintf(stderr, "[%s] state -> %d\n", ts->name, (int)st);
}

static void ts_fatal(void *ctx, const char *msg, int err)
{
    test_screen *ts = ctx;
    snprintf(ts->fatal, sizeof(ts->fatal), "%s", msg);
    ts->fatal_errno = err;
    fprintf(stderr, "[%s] fatal (errno %d): %s\n", ts->name, err, msg);
}

static void ts_event_log(void *ctx, const char *line)
{
    test_screen *ts = ctx;
    ts->event_log_lines++;
    if (getenv("SSHLX_TEST_VERBOSE"))
        fprintf(stderr, "[%s] log: %s\n", ts->name, line);
}

static void ts_exited(void *ctx, int code, bool close)
{
    test_screen *ts = ctx;
    ts->exited = true;
    ts->exit_code = code;
    fprintf(stderr, "[%s] exited %d (close=%d)\n", ts->name, code, close);
}

static void ts_prompt(void *ctx, const sshlx_prompt_request *req)
{
    test_screen *ts = ctx;
    ts->prompt_pending = true;
    ts->prompt_id = req->id;
    ts->prompt_kind = req->kind;
    ts->prompts_seen++;
    fprintf(stderr, "[%s] prompt kind %d (%zu text items)\n", ts->name,
            (int)req->kind, req->n_items);
}

static void ts_prompt_cancelled(void *ctx, sshlx_prompt_id id)
{
    test_screen *ts = ctx;
    if (ts->prompt_id == id)
        ts->prompt_pending = false;
}

static void ts_request_resize(void *ctx, int cols, int rows)
{
    test_screen *ts = ctx;
    (void)cols; (void)rows;
    sshlx_session_resize_done(ts->s);
}

static void ts_clip_request_paste(void *ctx, int clip)
{
    (void)ctx; (void)clip;
}

void ts_init(test_screen *ts, const char *name)
{
    memset(ts, 0, sizeof(*ts));
    ts->name = name;
    for (int r = 0; r < TS_ROWS; r++)
        for (int c = 0; c < TS_COLS; c++)
            ts->cells[r][c] = ' ';
}

sshlx_session_callbacks ts_callbacks(test_screen *ts)
{
    sshlx_session_callbacks cb;
    memset(&cb, 0, sizeof(cb));
    cb.ctx = ts;
    cb.begin_draw = ts_begin;
    cb.end_draw = ts_end;
    cb.draw_text = ts_draw_text;
    cb.draw_cursor_shape = ts_cursor;
    cb.set_title = ts_set_title;
    cb.state_changed = ts_state;
    cb.connection_fatal = ts_fatal;
    cb.event_log = ts_event_log;
    cb.exited = ts_exited;
    cb.prompt = ts_prompt;
    cb.prompt_cancelled = ts_prompt_cancelled;
    cb.request_resize = ts_request_resize;
    cb.clip_request_paste = ts_clip_request_paste;
    return cb;
}

bool ts_contains(test_screen *ts, const char *text)
{
    size_t n = strlen(text);
    for (int r = 0; r < TS_ROWS; r++) {
        char line[TS_COLS + 1];
        for (int c = 0; c < TS_COLS; c++) {
            uint32_t u = ts->cells[r][c];
            line[c] = (u >= 32 && u < 127) ? (char)u : '?';
        }
        line[TS_COLS] = '\0';
        if (n <= TS_COLS && strstr(line, text))
            return true;
    }
    return false;
}

void ts_dump(test_screen *ts)
{
    fprintf(stderr, "---- screen %s ----\n", ts->name);
    for (int r = 0; r < TS_ROWS; r++) {
        char line[TS_COLS + 1];
        int last = -1;
        for (int c = 0; c < TS_COLS; c++) {
            uint32_t u = ts->cells[r][c];
            line[c] = (u >= 32 && u < 127) ? (char)u : '?';
            if (line[c] != ' ') last = c;
        }
        line[last + 1] = '\0';
        if (last >= 0)
            fprintf(stderr, "%2d|%s\n", r, line);
    }
}

bool ts_pred_text(void *ctx)
{
    ts_wait_text *w = ctx;
    return ts_contains(w->ts, w->text);
}
