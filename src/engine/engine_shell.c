/*
 * engine_shell.c - the "Local shell" back end: the user's shell on this
 * machine, in a pseudo-terminal.
 *
 * PuTTY's own pty back end (unix/pty.c, pterm's) is not used: it reaps
 * children with a process-wide SIGCHLD handler calling waitpid(-1), which
 * would also reap the processes Qt starts. This one waits only for its own
 * child, through a pidfd watched by uxsel.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "engine_internal.h"

#define SHELL_MAX_BACKLOG 32768

typedef struct Shell Shell;
struct Shell {
    Seat *seat;
    LogContext *logctx;
    Conf *conf;
    int master;             /* pty master, -1 once closed */
    int pidfd;              /* -1 without pidfd_open (pre-5.3 kernels) */
    pid_t pid;
    bool exited;
    int status;             /* waitpid() status once exited */
    size_t inbufsize;
    bufchain output_data;
    Shell *next;
    Backend backend;
};

/* uxsel callbacks only get the fd. */
static Shell *shells;

static Shell *shell_by_fd(int fd)
{
    for (Shell *sh = shells; sh; sh = sh->next)
        if (sh->master == fd || sh->pidfd == fd)
            return sh;
    return NULL;
}

static void shell_select_result(int fd, int event);

static void shell_uxsel_setup(Shell *sh)
{
    if (sh->master < 0)
        return;
    int rwx = 0;
    if (sh->inbufsize <= SHELL_MAX_BACKLOG)
        rwx |= SELECT_R;
    if (bufchain_size(&sh->output_data))
        rwx |= SELECT_W;
    uxsel_set(sh->master, rwx, shell_select_result);
}

static void shell_close_master(Shell *sh)
{
    if (sh->master >= 0) {
        uxsel_del(sh->master);
        close(sh->master);
        sh->master = -1;
    }
}

/* Everything the shell wrote before exiting. */
static void shell_drain(Shell *sh)
{
    char buf[4096];
    ssize_t n;
    while (sh->master >= 0 && (n = read(sh->master, buf, sizeof(buf))) > 0)
        seat_stdout(sh->seat, buf, n);
}

static void shell_reaped(Shell *sh, int status)
{
    sh->exited = true;
    sh->status = status;
    if (sh->pidfd >= 0) {
        uxsel_del(sh->pidfd);
        close(sh->pidfd);
        sh->pidfd = -1;
    }
    shell_drain(sh);
    shell_close_master(sh);
    if (WIFSIGNALED(status))
        logeventf(sh->logctx, "Shell terminated on signal %d (%s)",
                  WTERMSIG(status), strsignal(WTERMSIG(status)));
    else
        logeventf(sh->logctx, "Shell exited with code %d", WEXITSTATUS(status));
    seat_eof(sh->seat);
    seat_notify_remote_exit(sh->seat);
}

static void shell_try_write(Shell *sh)
{
    while (sh->master >= 0 && bufchain_size(&sh->output_data)) {
        ptrlen data = bufchain_prefix(&sh->output_data);
        ssize_t n = write(sh->master, data.ptr, data.len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR)
                break;
            bufchain_clear(&sh->output_data);
            break;
        }
        bufchain_consume(&sh->output_data, n);
    }
    shell_uxsel_setup(sh);
}

static void shell_select_result(int fd, int event)
{
    Shell *sh = shell_by_fd(fd);
    if (!sh)
        return;

    if (fd == sh->pidfd) {
        int status;
        if (waitpid(sh->pid, &status, WNOHANG) == sh->pid)
            shell_reaped(sh, status);
        return;
    }

    if (event == SELECT_R) {
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            sh->inbufsize = seat_stdout(sh->seat, buf, n);
            shell_uxsel_setup(sh);
        } else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            return;
        } else {
            /* EIO: every slave fd is closed, normally because the shell
             * exited. Its exit status comes through the pidfd; without
             * one, collect it here. */
            shell_close_master(sh);
            if (sh->pidfd < 0) {
                int status = 0;
                if (waitpid(sh->pid, &status, 0) == sh->pid)
                    shell_reaped(sh, status);
            }
        }
    } else if (event == SELECT_W) {
        shell_try_write(sh);
    }
}

static const char *login_shell_path(void)
{
    const char *shell = getenv("SHELL");
    if (shell && *shell)
        return shell;
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_shell && *pw->pw_shell)
        return pw->pw_shell;
    return "/bin/sh";
}

/* In the child, between fork() and exec(): async-signal-safe calls only,
 * plus setenv(), as pty.c does. */
static void shell_child(Conf *conf, const char *slave_name, const char *shell,
                        const char *argv0, const char *cmd, bool utf8)
{
    setsid();
    int slave = open(slave_name, O_RDWR);
    if (slave < 0)
        _exit(127);
    ioctl(slave, TIOCSCTTY, 0);

    struct termios t;
    if (tcgetattr(slave, &t) == 0) {
        t.c_cc[VERASE] = conf_get_bool(conf, CONF_bksp_is_delete) ? 0x7F : 0x08;
#ifdef IUTF8
        if (utf8)
            t.c_iflag |= IUTF8;
        else
            t.c_iflag &= ~IUTF8;
#endif
        tcsetattr(slave, TCSANOW, &t);
    }

    dup2(slave, 0);
    dup2(slave, 1);
    dup2(slave, 2);
    if (slave > 2)
        close(slave);
    for (int fd = 3, max = (int)sysconf(_SC_OPEN_MAX); fd < max && fd < 65536; fd++)
        close(fd);

    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    for (int sig = 1; sig < NSIG; sig++)
        signal(sig, SIG_DFL);

    setenv("TERM", conf_get_str(conf, CONF_termtype), 1);
    setenv("COLORTERM", "truecolor", 1);
    unsetenv("LINES");
    unsetenv("COLUMNS");
    char *key, *val;
    for (val = conf_get_str_strs(conf, CONF_environmt, NULL, &key); val;
         val = conf_get_str_strs(conf, CONF_environmt, key, &key))
        setenv(key, val, 1);

    const char *home = getenv("HOME");
    if (!home || chdir(home) < 0)
        if (chdir("/") < 0)
            _exit(127);

    if (cmd)
        execl(shell, shell, "-c", cmd, (char *)NULL);
    else
        execl(shell, argv0, (char *)NULL);
    _exit(127);
}

static char *shell_init(const BackendVtable *vt, Seat *seat,
                        Backend **backend_handle, LogContext *logctx,
                        Conf *conf, const char *host, int port,
                        char **realhost, bool nodelay, bool keepalive)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master < 0)
        return dupprintf("Cannot open a pseudo-terminal: %s", strerror(errno));
    char slave_name[256];
    if (grantpt(master) < 0 || unlockpt(master) < 0 ||
        ptsname_r(master, slave_name, sizeof(slave_name)) != 0) {
        char *err = dupprintf("Cannot set up a pseudo-terminal: %s", strerror(errno));
        close(master);
        return err;
    }
    struct winsize ws = {
        .ws_row = conf_get_int(conf, CONF_height),
        .ws_col = conf_get_int(conf, CONF_width),
    };
    ioctl(master, TIOCSWINSZ, &ws);

    /* Everything the child needs, prepared before fork(). */
    const char *shell = login_shell_path();
    const char *base = strrchr(shell, '/');
    base = base ? base + 1 : shell;
    char *argv0 = conf_get_bool(conf, CONF_login_shell) ? dupprintf("-%s", base)
                                                        : dupstr(base);
    const char *cmd = conf_get_str_ambi(conf, CONF_remote_cmd, NULL);
    if (!*cmd)
        cmd = NULL;
    bool utf8 = seat_is_utf8(seat);

    pid_t pid = fork();
    if (pid < 0) {
        char *err = dupprintf("Cannot start %s: %s", shell, strerror(errno));
        close(master);
        sfree(argv0);
        return err;
    }
    if (pid == 0)
        shell_child(conf, slave_name, shell, argv0, cmd, utf8);
    sfree(argv0);

    Shell *sh = snew(Shell);
    memset(sh, 0, sizeof(*sh));
    sh->backend.vt = vt;
    sh->seat = seat;
    sh->logctx = logctx;
    sh->conf = conf_copy(conf);
    sh->master = master;
    sh->pid = pid;
    bufchain_init(&sh->output_data);
#ifdef SYS_pidfd_open
    sh->pidfd = (int)syscall(SYS_pidfd_open, pid, 0);
#else
    sh->pidfd = -1;
#endif
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    sh->next = shells;
    shells = sh;
    *backend_handle = &sh->backend;

    if (sh->pidfd >= 0) {
        fcntl(sh->pidfd, F_SETFD, FD_CLOEXEC);
        uxsel_set(sh->pidfd, SELECT_R, shell_select_result);
    }
    shell_uxsel_setup(sh);

    logeventf(logctx, "Started %s%s%s (pid %d) on %s", shell,
              cmd ? " -c " : "", cmd ? cmd : "", (int)pid, slave_name);
    *realhost = dupstr("localhost");
    seat_set_trust_status(seat, false);
    return NULL;
}

/* Collects a shell that is still running when its tab goes away, so it
 * does not linger as a zombie. */
static void *reap_thread(void *arg)
{
    waitpid((pid_t)(intptr_t)arg, NULL, 0);
    return NULL;
}

static void shell_free(Backend *be)
{
    Shell *sh = container_of(be, Shell, backend);
    for (Shell **p = &shells; *p; p = &(*p)->next)
        if (*p == sh) {
            *p = sh->next;
            break;
        }
    shell_close_master(sh);   /* the kernel hangs up the shell's session */
    if (!sh->exited) {
        kill(-sh->pid, SIGHUP);
        kill(sh->pid, SIGHUP);
        if (waitpid(sh->pid, NULL, WNOHANG) != sh->pid) {
            pthread_t t;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_create(&t, &attr, reap_thread, (void *)(intptr_t)sh->pid);
            pthread_attr_destroy(&attr);
        }
    }
    if (sh->pidfd >= 0) {
        uxsel_del(sh->pidfd);
        close(sh->pidfd);
    }
    bufchain_clear(&sh->output_data);
    conf_free(sh->conf);
    sfree(sh);
}

static void shell_reconfig(Backend *be, Conf *conf)
{
    Shell *sh = container_of(be, Shell, backend);
    conf_copy_into(sh->conf, conf);
}

static void shell_send(Backend *be, const char *buf, size_t len)
{
    Shell *sh = container_of(be, Shell, backend);
    if (sh->master < 0)
        return;
    bufchain_add(&sh->output_data, buf, len);
    shell_try_write(sh);
}

static size_t shell_sendbuffer(Backend *be)
{
    Shell *sh = container_of(be, Shell, backend);
    return bufchain_size(&sh->output_data);
}

static void shell_size(Backend *be, int width, int height)
{
    Shell *sh = container_of(be, Shell, backend);
    if (sh->master < 0)
        return;
    struct winsize ws = { .ws_row = height, .ws_col = width };
    ioctl(sh->master, TIOCSWINSZ, &ws);   /* the kernel sends SIGWINCH */
}

static void shell_special(Backend *be, SessionSpecialCode code, int arg)
{
}

static const SessionSpecial *shell_get_specials(Backend *be)
{
    return NULL;
}

static bool shell_connected(Backend *be)
{
    Shell *sh = container_of(be, Shell, backend);
    return !sh->exited;
}

static int shell_exitcode(Backend *be)
{
    Shell *sh = container_of(be, Shell, backend);
    if (!sh->exited)
        return -1;
    if (WIFSIGNALED(sh->status))
        return 128 + WTERMSIG(sh->status);
    return WEXITSTATUS(sh->status);
}

static bool shell_sendok(Backend *be)
{
    return true;
}

/* The pty does its own echo and line editing. */
static bool shell_ldisc(Backend *be, int option)
{
    return false;
}

static void shell_provide_ldisc(Backend *be, Ldisc *ldisc)
{
}

static void shell_unthrottle(Backend *be, size_t backlog)
{
    Shell *sh = container_of(be, Shell, backend);
    sh->inbufsize = backlog;
    shell_uxsel_setup(sh);
}

static int shell_cfg_info(Backend *be)
{
    return 0;
}

const BackendVtable sshlx_localshell_backend = {
    .init = shell_init,
    .free = shell_free,
    .reconfig = shell_reconfig,
    .send = shell_send,
    .sendbuffer = shell_sendbuffer,
    .size = shell_size,
    .special = shell_special,
    .get_specials = shell_get_specials,
    .connected = shell_connected,
    .exitcode = shell_exitcode,
    .sendok = shell_sendok,
    .ldisc_option_state = shell_ldisc,
    .provide_ldisc = shell_provide_ldisc,
    .unthrottle = shell_unthrottle,
    .cfg_info = shell_cfg_info,
    .id = "localshell",
    .displayname_tc = "Local shell",
    .displayname_lc = "local shell",
    .protocol = SSHLX_PROT_LOCALSHELL,
    .flags = BACKEND_NEEDS_TERMINAL,
};
