/*
 * testloop.h - a headless host loop (poll + one timer) and a character
 * grid "screen" for driving the engine in tests through sshlx_engine.h
 * only.
 */
#ifndef SSHLX_TESTLOOP_H
#define SSHLX_TESTLOOP_H

#include <stdbool.h>
#include "sshlx_engine.h"

#define TS_COLS 100
#define TS_ROWS 30

typedef struct test_screen {
    sshlx_session *s;
    const char *name;
    uint32_t cells[TS_ROWS][TS_COLS];
    char title[256];
    sshlx_state state;
    char fatal[1024];
    int fatal_errno;
    bool exited;
    int exit_code;
    /* last prompt */
    bool prompt_pending;
    sshlx_prompt_id prompt_id;
    sshlx_prompt_kind prompt_kind;
    int prompts_seen;
    int event_log_lines;
    int draws;
} test_screen;

void tl_init(void);
/* Run the loop until pred(ctx) returns true or timeout_ms elapses.
 * Returns true if pred became true. */
bool tl_run_until(bool (*pred)(void *ctx), void *ctx, int timeout_ms);
void tl_run_for(int ms);

void ts_init(test_screen *ts, const char *name);
sshlx_session_callbacks ts_callbacks(test_screen *ts);
/* Does the screen contain this ASCII text anywhere (on one row)? */
bool ts_contains(test_screen *ts, const char *text);
void ts_dump(test_screen *ts);

typedef struct { test_screen *ts; const char *text; } ts_wait_text;
bool ts_pred_text(void *ctx);

#define CHECK(cond) do {                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n",                \
                    __FILE__, __LINE__, #cond);                         \
            exit(1);                                                    \
        }                                                               \
    } while (0)

#endif
