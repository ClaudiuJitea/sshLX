/*
 * fe_hostloop.c - sshLX's replacement for the event-loop glue that PuTTY's
 * unix/gtk-common.c provides on GTK: uxsel_input_add/remove, the timer
 * (timer_change_notify) and toplevel-callback notifications. Everything
 * is forwarded to the UI's sshlx_host_loop, so the engine never depends on
 * which event loop the toolkit uses.
 */
#include <locale.h>
#include <signal.h>
#include <stdlib.h>

#include "engine_internal.h"
#include "ssh.h"   /* enable_dit() */

sshlx_host_loop sshlx_loop;
sshlx_global_cb sshlx_global_callback;
void *sshlx_global_ctx;

static bool engine_initialised;
static bool wake_scheduled;

/* ---- uxsel ---------------------------------------------------------- */
struct uxsel_id {
    void *token;
};

uxsel_id *uxsel_input_add(int fd, int rwx)
{
    uxsel_id *id = snew(uxsel_id);
    int mask = 0;
    if (rwx & SELECT_R) mask |= SSHLX_IO_READ;
    if (rwx & SELECT_W) mask |= SSHLX_IO_WRITE;
    if (rwx & SELECT_X) mask |= SSHLX_IO_EXCEPT;
    id->token = sshlx_loop.watch_fd(sshlx_loop.ctx, fd, mask);
    return id;
}

void uxsel_input_remove(uxsel_id *id)
{
    sshlx_loop.unwatch_fd(sshlx_loop.ctx, id->token);
    sfree(id);
}

void sshlx_engine_on_fd(sshlx_fd_t fd, int io_mask)
{
    /* Same order as gtk-common.c: exceptional data first, so we don't go
     * straight past an urgent marker. */
    if (io_mask & SSHLX_IO_EXCEPT)
        select_result((int)fd, SELECT_X);
    if (io_mask & SSHLX_IO_READ)
        select_result((int)fd, SELECT_R);
    if (io_mask & SSHLX_IO_WRITE)
        select_result((int)fd, SELECT_W);
    sshlx_after_entry();
}

/* ---- timers --------------------------------------------------------- */
static unsigned long timer_next;
static bool timer_armed;

void timer_change_notify(unsigned long next)
{
    long ticks = (long)(next - GETTICKCOUNT());
    if (ticks <= 0)
        ticks = 1;
    timer_next = next;
    timer_armed = true;
    sshlx_loop.set_timer(sshlx_loop.ctx, ticks);
}

void sshlx_engine_on_timer(void)
{
    /* As gtk-common.c's timer_trigger(): run with the scheduled time and
     * re-arm ourselves if run_timers() didn't do it via
     * timer_change_notify(). */
    unsigned long now = timer_next, next;
    timer_armed = false;
    if (run_timers(now, &next) && !timer_armed) {
        unsigned long then = now;
        long ticks;
        now = GETTICKCOUNT();
        if (now - then > next - then)
            ticks = 0;
        else
            ticks = (long)(next - now);
        timer_next = next;
        timer_armed = true;
        sshlx_loop.set_timer(sshlx_loop.ctx, ticks);
    }
    sshlx_after_entry();
}

/* ---- toplevel callbacks --------------------------------------------- */
static void notify_toplevel_callback(void *vctx)
{
    (void)vctx;
    if (!wake_scheduled) {
        wake_scheduled = true;
        sshlx_loop.wake(sshlx_loop.ctx);
    }
}

void sshlx_after_entry(void)
{
    if (toplevel_callback_pending())
        notify_toplevel_callback(NULL);
}

void sshlx_engine_run_pending(void)
{
    wake_scheduled = false;
    /* Run a bounded batch so UI events can interleave with a busy
     * session, as GTK's idle function does. */
    for (int i = 0; i < 64 && run_toplevel_callbacks(); i++)
        ;
    sshlx_after_entry();
}

/* ---- global events ----------------------------------------------------- */
void sshlx_global_event_fire(sshlx_global_event ev, const char *text)
{
    if (sshlx_global_callback)
        sshlx_global_callback(sshlx_global_ctx, ev, text ? text : "");
}

/* ---- init / shutdown --------------------------------------------------- */
int sshlx_engine_init(const sshlx_host_loop *loop,
                      sshlx_global_cb cb, void *cb_ctx)
{
    if (engine_initialised)
        return 0;
    if (!loop || !loop->watch_fd || !loop->unwatch_fd ||
        !loop->set_timer || !loop->wake)
        return -1;
    sshlx_loop = *loop;
    sshlx_global_callback = cb;
    sshlx_global_ctx = cb_ctx;

    /* What main-gtk-simple.c's main() and unix/putty.c's setup() do. */
    setlocale(LC_CTYPE, "");
    sk_init();
    enable_dit();
    settings_set_default_protocol(be_default_protocol);
    {
        const struct BackendVtable *vt =
            backend_vt_from_proto(be_default_protocol);
        settings_set_default_port(0);
        if (vt)
            settings_set_default_port(vt->default_port);
    }
    uxsel_init();
    request_callback_notifications(notify_toplevel_callback, NULL);
    block_signal(SIGPIPE, true);

    engine_initialised = true;
    return 0;
}

void sshlx_engine_shutdown(void)
{
    if (!engine_initialised)
        return;
    /* Free every remaining session, then do what cleanup_exit() does. */
    while (sshlx_live_sessions) {
        sshlx_session *s = sshlx_live_sessions;
        sshlx_session_free(s);
        /* sshlx_session_free defers; force the deferred work now. */
        while (run_toplevel_callbacks())
            ;
        if (sshlx_live_sessions == s)
            break;   /* should not happen; avoid looping forever */
    }
    sk_cleanup();
    random_save_seed();
    engine_initialised = false;
}

const char *sshlx_engine_putty_version(void)
{
    return ver;
}

char *sshlx_engine_buildinfo(void)
{
    return buildinfo("\n");
}

void sshlx_free(void *p)
{
    sfree(p);
}
