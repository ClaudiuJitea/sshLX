/*
 * fe_logpolicy.c - sshLX's LogPolicy, modelled on gtk_logpolicy_vt in
 * PuTTY's unix/window.c. Event-log lines are kept per session (PuTTY's
 * Event Log window contents) and forwarded to the UI.
 */
#include <string.h>
#include <time.h>

#include "engine_internal.h"

#define SESSION_OF(lp) container_of(lp, sshlx_session, logpolicy)

int sshlx_ask_append(sshlx_session *s, Filename *filename,
                     void (*callback)(void *ctx, int result), void *ctx);

/* Retention as in unix/dialog.c logevent_dlg(): the first
 * LOGEVENT_INITIAL_MAX lines are kept for ever, then a circular buffer of
 * the latest LOGEVENT_CIRCULAR_MAX, whose oldest slot turns into ".."
 * once lines have been dropped. */
#define LOGEVENT_INITIAL_MAX 128
#define LOGEVENT_CIRCULAR_MAX 128

void sshlx_eventlog_add(sshlx_session *s, const char *string)
{
    /* Timestamp format as in unix/dialog.c logevent_dlg(). */
    char timebuf[40];
    struct tm tm = ltime();
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S\t", &tm);
    char *line = dupcat(timebuf, string);

    if (s->n_eventlog == LOGEVENT_INITIAL_MAX + LOGEVENT_CIRCULAR_MAX) {
        size_t first = LOGEVENT_INITIAL_MAX;
        sfree(s->eventlog[first]);
        memmove(s->eventlog + first, s->eventlog + first + 1,
                (s->n_eventlog - first - 1) * sizeof(char *));
        s->n_eventlog--;
        sfree(s->eventlog[first]);
        s->eventlog[first] = dupstr("..");
    }
    sgrowarray(s->eventlog, s->eventlog_size, s->n_eventlog);
    s->eventlog[s->n_eventlog++] = line;
    UI_CALL(s, event_log, line);
}

size_t sshlx_session_event_log(const sshlx_session *s,
                               const char *const **lines_out)
{
    *lines_out = (const char *const *)s->eventlog;
    return s->n_eventlog;
}

static void sshlx_eventlog(LogPolicy *lp, const char *string)
{
    sshlx_eventlog_add(SESSION_OF(lp), string);
}

static int sshlx_askappend(LogPolicy *lp, Filename *filename,
                           void (*callback)(void *ctx, int result),
                           void *ctx)
{
    return sshlx_ask_append(SESSION_OF(lp), filename, callback, ctx);
}

static void sshlx_logging_error(LogPolicy *lp, const char *event)
{
    sshlx_session *s = SESSION_OF(lp);
    /* As GTK: 'can't open log file' errors go to the terminal window. */
    seat_stderr_pl(&s->seat, ptrlen_from_asciz(event));
    seat_stderr_pl(&s->seat, PTRLEN_LITERAL("\r\n"));
    UI_CALL(s, logging_error, event);
}

const LogPolicyVtable sshlx_logpolicy_vt = {
    .eventlog = sshlx_eventlog,
    .askappend = sshlx_askappend,
    .logging_error = sshlx_logging_error,
    .verbose = null_lp_verbose_yes,
};
