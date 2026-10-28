/*
 * engine_sessions_test.c - Phase 0 proof: run serial, raw, telnet and SSH
 * sessions concurrently in one process through sshlx_engine.h only, check
 * that one dying leaves the others working, and check serial EACCES
 * reporting.
 *
 * SSH runs against PuTTY's own test server 'uppity' (built from the
 * unchanged PuTTY tree), which accepts the password "weasel".
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "testloop.h"

static char tmpdir[256];

static int proto_id(const char *name)
{
    for (size_t i = 0; i < sshlx_protocol_count(); i++)
        if (!strcmp(sshlx_protocol_get(i)->name, name))
            return sshlx_protocol_get(i)->id;
    fprintf(stderr, "no protocol %s\n", name);
    exit(1);
}

static sshlx_conf *conf_for(const char *proto, const char *host, int port)
{
    sshlx_conf *c = sshlx_conf_new_default();
    sshlx_conf_set_int(c, sshlx_conf_key_protocol(), proto_id(proto));
    if (host)
        sshlx_conf_set_str(c, sshlx_conf_key_find("HostName"), host);
    if (port)
        sshlx_conf_set_int(c, sshlx_conf_key_find("PortNumber"), port);
    sshlx_conf_set_int(c, sshlx_conf_key_find("TermWidth"), TS_COLS);
    sshlx_conf_set_int(c, sshlx_conf_key_find("TermHeight"), TS_ROWS);
    return c;
}

static sshlx_session *open_session(test_screen *ts, sshlx_conf *c)
{
    sshlx_session_callbacks cb = ts_callbacks(ts);
    ts->s = sshlx_session_new(c, &cb, TS_COLS, TS_ROWS);
    CHECK(ts->s != NULL);
    return ts->s;
}

static bool wait_text(test_screen *ts, const char *text, int ms)
{
    ts_wait_text w = { ts, text };
    bool ok = tl_run_until(ts_pred_text, &w, ms);
    if (!ok) {
        fprintf(stderr, "timed out waiting for \"%s\" in %s\n", text, ts->name);
        ts_dump(ts);
    }
    return ok;
}

static void type_line(test_screen *ts, const char *text)
{
    sshlx_session_text(ts->s, text);
    sshlx_key_event ev = { .keysym = 0xff0d, .mods = 0, .text = "\r" };
    sshlx_session_key(ts->s, &ev);
}

/* ---- tiny TCP servers ------------------------------------------------------ */
static int listen_any(int *port_out)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    CHECK(fd >= 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0);
    CHECK(listen(fd, 4) == 0);
    socklen_t len = sizeof(a);
    getsockname(fd, (struct sockaddr *)&a, &len);
    *port_out = ntohs(a.sin_port);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}

typedef struct { int lfd; int cfd; } server;
static bool pred_accept(void *ctx)
{
    server *sv = ctx;
    if (sv->cfd < 0) {
        sv->cfd = accept4(sv->lfd, NULL, NULL, SOCK_CLOEXEC);
        if (sv->cfd >= 0)
            fcntl(sv->cfd, F_SETFL, O_NONBLOCK);
    }
    return sv->cfd >= 0;
}

typedef struct { int fd; char buf[4096]; size_t len; const char *want; } reader;
static bool pred_read(void *ctx)
{
    reader *r = ctx;
    ssize_t n = read(r->fd, r->buf + r->len, sizeof(r->buf) - 1 - r->len);
    if (n > 0) {
        r->len += n;
        r->buf[r->len] = '\0';
    }
    return memmem(r->buf, r->len, r->want, strlen(r->want)) != NULL;
}

/* ---- serial ------------------------------------------------------------------ */
static void test_serial(test_screen *ts, int *master_out)
{
    int master, slave;
    char name[256];
    CHECK(openpty(&master, &slave, name, NULL, NULL) == 0);
    fcntl(master, F_SETFD, FD_CLOEXEC);
    fcntl(slave, F_SETFD, FD_CLOEXEC);
    /* raw mode on the far end so bytes pass through unchanged */
    struct termios t;
    tcgetattr(master, &t);
    cfmakeraw(&t);
    tcsetattr(master, TCSANOW, &t);
    fcntl(master, F_SETFL, O_NONBLOCK);
    fprintf(stderr, "serial test on %s\n", name);

    sshlx_conf *c = conf_for("serial", NULL, 0);
    sshlx_conf_set_str(c, sshlx_conf_key_find("SerialLine"), name);
    sshlx_conf_set_int(c, sshlx_conf_key_find("SerialSpeed"), 115200);
    open_session(ts, c);
    sshlx_conf_free(c);
    CHECK(sshlx_session_connect(ts->s) == 0);
    CHECK(ts->state == SSHLX_STATE_CONNECTED);
    close(slave);   /* PuTTY has its own fd now */

    const char *msg = "hello-serial\r\n";
    CHECK(write(master, msg, strlen(msg)) == (ssize_t)strlen(msg));
    CHECK(wait_text(ts, "hello-serial", 5000));

    sshlx_session_text(ts->s, "typed-on-serial");
    reader r = { .fd = master, .want = "typed-on-serial" };
    CHECK(tl_run_until(pred_read, &r, 5000));
    *master_out = master;
}

/* ---- raw / telnet --------------------------------------------------------------- */
static void test_tcp(test_screen *ts, const char *proto, server *sv)
{
    int port;
    sv->lfd = listen_any(&port);
    sv->cfd = -1;
    sshlx_conf *c = conf_for(proto, "127.0.0.1", port);
    open_session(ts, c);
    sshlx_conf_free(c);
    CHECK(sshlx_session_connect(ts->s) == 0);
    CHECK(tl_run_until(pred_accept, sv, 5000));

    char msg[64];
    snprintf(msg, sizeof(msg), "hello-%s\r\n", proto);
    CHECK(write(sv->cfd, msg, strlen(msg)) == (ssize_t)strlen(msg));
    snprintf(msg, sizeof(msg), "hello-%s", proto);
    CHECK(wait_text(ts, msg, 5000));

    type_line(ts, "typed-on-tcp");
    reader r = { .fd = sv->cfd, .want = "typed-on-tcp" };
    CHECK(tl_run_until(pred_read, &r, 5000));
}

/* ---- ssh via uppity ----------------------------------------------------------------- */
static pid_t uppity_pid = -1;

static void kill_uppity(void)
{
    if (uppity_pid > 0) {
        kill(uppity_pid, SIGTERM);
        waitpid(uppity_pid, NULL, 0);
        uppity_pid = -1;
    }
}

static int start_uppity(void)
{
    char key[300], cmd[800];
    snprintf(key, sizeof(key), "%s/hostkey.ppk", tmpdir);
    snprintf(cmd, sizeof(cmd),
             "'%s' -t ed25519 -o '%s' --new-passphrase /dev/null "
             ">/dev/null 2>&1", PUTTYGEN_PATH, key);
    CHECK(system(cmd) == 0);

    int port;
    int probe = listen_any(&port);
    close(probe);

    uppity_pid = fork();
    CHECK(uppity_pid >= 0);
    if (uppity_pid == 0) {
        char portstr[16];
        snprintf(portstr, sizeof(portstr), "%d", port);
        int devnull = open("/dev/null", O_RDWR);
        dup2(devnull, 0);
        if (!getenv("SSHLX_TEST_VERBOSE")) {
            dup2(devnull, 1);
            dup2(devnull, 2);
        }
        setenv("SHELL", "/bin/sh", 1);
        execl(UPPITY_PATH, "uppity", "--listen", portstr, "--hostkey", key,
              (char *)NULL);
        _exit(127);
    }
    atexit(kill_uppity);   /* also on CHECK failure */
    /* wait until it listens */
    for (int i = 0; i < 100; i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int ok = connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0;
        close(fd);
        if (ok)
            return port;
        usleep(50000);
    }
    fprintf(stderr, "uppity did not start\n");
    exit(1);
}

static bool pred_prompt(void *ctx)
{
    return ((test_screen *)ctx)->prompt_pending;
}

static bool pred_connected(void *ctx)
{
    return ((test_screen *)ctx)->state == SSHLX_STATE_CONNECTED;
}

static void test_ssh(test_screen *ts, int port)
{
    sshlx_conf *c = conf_for("ssh", "127.0.0.1", port);
    const char *user = getenv("USER");
    sshlx_conf_set_str(c, sshlx_conf_key_find("UserName"),
                       user ? user : "sshlx");
    /* uppity offers keyboard-interactive test prompts first; use
     * PuTTY's own option to go straight to password auth. */
    sshlx_conf_set_bool(c, sshlx_conf_key_find("AuthKI"), false);
    open_session(ts, c);
    sshlx_conf_free(c);
    CHECK(sshlx_session_connect(ts->s) == 0);

    /* Host key prompt arrives asynchronously, non-modally. */
    CHECK(tl_run_until(pred_prompt, ts, 15000));
    CHECK(ts->prompt_kind == SSHLX_PROMPT_HOSTKEY);
    ts->prompt_pending = false;
    sshlx_session_answer_prompt(ts->s, ts->prompt_id, SSHLX_ANSWER_ONCE);

    /* PuTTY's own in-terminal password prompt. */
    CHECK(wait_text(ts, "password:", 15000));
    type_line(ts, "weasel");
    CHECK(tl_run_until(pred_connected, ts, 15000));
    tl_run_for(500);
    type_line(ts, "echo sshlx-$((6*7))");
    CHECK(wait_text(ts, "sshlx-42", 15000));
}

/* ---- EACCES -------------------------------------------------------------------------- */
static void test_serial_eacces(void)
{
    if (geteuid() == 0) {
        fprintf(stderr, "running as root: skipping EACCES test\n");
        return;
    }
    char path[300];
    snprintf(path, sizeof(path), "%s/ttyNOPE", tmpdir);
    int fd = open(path, O_CREAT | O_WRONLY, 0);
    CHECK(fd >= 0);
    close(fd);
    chmod(path, 0);

    test_screen ts;
    ts_init(&ts, "eacces");
    sshlx_conf *c = conf_for("serial", NULL, 0);
    sshlx_conf_set_str(c, sshlx_conf_key_find("SerialLine"), path);
    open_session(&ts, c);
    sshlx_conf_free(c);
    CHECK(sshlx_session_connect(ts.s) == -1);
    CHECK(ts.fatal_errno == EACCES);
    CHECK(strstr(ts.fatal, "Permission denied") != NULL);
    tl_run_for(100);
    CHECK(ts.state == SSHLX_STATE_DISCONNECTED);
    sshlx_session_free(ts.s);
    tl_run_for(50);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    snprintf(tmpdir, sizeof(tmpdir), "%s/sshlx-sesstest-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(tmpdir) != NULL);
    setenv("PUTTYDIR", tmpdir, 1);   /* never touch the user's host keys */

    tl_init();

    test_screen serial, raw, telnet, ssh;
    ts_init(&serial, "serial");
    ts_init(&raw, "raw");
    ts_init(&telnet, "telnet");
    ts_init(&ssh, "ssh");

    int master;
    server rawsv, telsv;
    test_serial(&serial, &master);
    fprintf(stderr, "== serial OK\n");

    /* Regression test for the unix/serial.c override: closing serial
     * sessions must not break the others' input (PuTTY's shared fd index
     * otherwise keeps freed, mis-keyed entries). */
    {
        enum { NS = 10 };
        test_screen *ss = calloc(NS, sizeof(test_screen));
        int masters[NS];
        char name[NS][16];
        for (int i = 0; i < NS; i++) {
            snprintf(name[i], sizeof(name[i]), "ser%d", i);
            ts_init(&ss[i], name[i]);
            test_serial(&ss[i], &masters[i]);
        }
        for (int i = 2; i < NS; i += 3) {     /* close 2, 5, 8 */
            sshlx_session_free(ss[i].s);
            close(masters[i]);
        }
        tl_run_for(100);
        for (int i = 0; i < NS; i++) {
            if (i % 3 == 2)
                continue;
            char msg[32];
            snprintf(msg, sizeof(msg), "still-%d\r\n", i);
            CHECK(write(masters[i], msg, strlen(msg)) > 0);
        }
        for (int i = 0; i < NS; i++) {
            if (i % 3 == 2)
                continue;
            char want[32];
            snprintf(want, sizeof(want), "still-%d", i);
            CHECK(wait_text(&ss[i], want, 5000));
            sshlx_session_free(ss[i].s);
            close(masters[i]);
        }
        const char *m = "after-close\r\n";
        CHECK(write(master, m, strlen(m)) > 0);
        CHECK(wait_text(&serial, "after-close", 5000));
        tl_run_for(50);
        free(ss);
        fprintf(stderr, "== serial close isolation OK\n");
    }

    /* Regression test for the unix/serial.c override: an I/O error on one
     * serial port (device gone, then a keystroke is sent before the hang-up
     * is noticed) must end that session only. PuTTY's original code calls
     * exit(1) here, which would take every tab with it. */
    {
        test_screen gone;
        ts_init(&gone, "gone");
        int m2;
        test_serial(&gone, &m2);
        close(m2);                               /* device disappears */
        sshlx_session_send_raw(gone.s, "x", 1);  /* write -> EIO */
        tl_run_for(200);
        CHECK(gone.state == SSHLX_STATE_DISCONNECTED);
        CHECK(strstr(gone.fatal, "serial port") != NULL);
        const char *m = "survived-eio\r\n";
        CHECK(write(master, m, strlen(m)) > 0);
        CHECK(wait_text(&serial, "survived-eio", 5000));
        sshlx_session_free(gone.s);
        tl_run_for(50);
        fprintf(stderr, "== serial I/O error isolation OK (%s)\n", gone.fatal);
    }
    test_tcp(&raw, "raw", &rawsv);
    fprintf(stderr, "== raw OK\n");
    test_tcp(&telnet, "telnet", &telsv);
    fprintf(stderr, "== telnet OK\n");
    int port = start_uppity();
    test_ssh(&ssh, port);
    fprintf(stderr, "== ssh OK\n");

    /* Isolation: the raw server goes away; that session must end, and
     * all the others must keep working. */
    close(rawsv.cfd);
    close(rawsv.lfd);
    tl_run_for(500);
    CHECK(raw.state == SSHLX_STATE_DISCONNECTED);

    /* Close (free) the telnet tab entirely. */
    sshlx_session_free(telnet.s);
    close(telsv.cfd);
    close(telsv.lfd);
    tl_run_for(200);

    type_line(&ssh, "echo still-$((40+2))");
    CHECK(wait_text(&ssh, "still-42", 10000));
    const char *msg = "serial-still-alive\r\n";
    CHECK(write(master, msg, strlen(msg)) > 0);
    CHECK(wait_text(&serial, "serial-still-alive", 5000));
    fprintf(stderr, "== isolation OK\n");

    /* Reconnect the dropped raw session (PuTTY "Restart Session"). */
    {
        int port2;
        server sv2 = { .lfd = listen_any(&port2), .cfd = -1 };
        sshlx_conf *c = sshlx_conf_copy(sshlx_session_conf(raw.s));
        sshlx_conf_set_int(c, sshlx_conf_key_find("PortNumber"), port2);
        sshlx_session_reconfigure(raw.s, c);
        sshlx_conf_free(c);
        CHECK(sshlx_session_reconnect(raw.s) == 0);
        CHECK(tl_run_until(pred_accept, &sv2, 5000));
        CHECK(write(sv2.cfd, "back-again\r\n", 12) == 12);
        CHECK(wait_text(&raw, "back-again", 5000));
        close(sv2.cfd);
        close(sv2.lfd);
        fprintf(stderr, "== reconnect OK\n");
    }

    /* Special commands are exposed for SSH. */
    const sshlx_special *sp;
    size_t nsp = sshlx_session_specials(ssh.s, &sp);
    CHECK(nsp > 0);
    const char *const *log;
    CHECK(sshlx_session_event_log(ssh.s, &log) > 5);

    /* Remote exit of the SSH session. */
    type_line(&ssh, "exit");
    tl_run_for(2000);
    CHECK(ssh.state == SSHLX_STATE_DISCONNECTED);
    fprintf(stderr, "== remote exit OK (exited=%d code=%d)\n", ssh.exited,
            ssh.exit_code);
    CHECK(wait_text(&serial, "serial-still-alive", 10));

    test_serial_eacces();
    fprintf(stderr, "== EACCES OK\n");

    sshlx_session_free(serial.s);
    sshlx_session_free(raw.s);
    sshlx_session_free(ssh.s);
    tl_run_for(100);
    sshlx_engine_shutdown();
    kill_uppity();
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpdir);
    if (system(cmd) != 0) { /* best effort */ }
    printf("engine_sessions_test: OK\n");
    return 0;
}
