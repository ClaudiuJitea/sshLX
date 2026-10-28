/*
 * serialdetect.c - see serialdetect.h.
 */
#define _GNU_SOURCE
#include "serialdetect.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ---- libudev, loaded at runtime ------------------------------------------
 * Only the handful of functions needed for a netlink monitor. Their ABI
 * has been stable since libudev.so.1. */
struct udev;
struct udev_monitor;
struct udev_device;

typedef struct udev_api {
    void *lib;
    struct udev *(*udev_new)(void);
    struct udev *(*udev_unref)(struct udev *);
    struct udev_monitor *(*monitor_new_from_netlink)(struct udev *, const char *);
    int (*monitor_filter_add_match_subsystem_devtype)(struct udev_monitor *,
                                                      const char *, const char *);
    int (*monitor_enable_receiving)(struct udev_monitor *);
    int (*monitor_get_fd)(struct udev_monitor *);
    struct udev_device *(*monitor_receive_device)(struct udev_monitor *);
    struct udev_monitor *(*monitor_unref)(struct udev_monitor *);
    struct udev_device *(*device_unref)(struct udev_device *);
} udev_api;

static bool udev_load(udev_api *u)
{
    memset(u, 0, sizeof(*u));
#ifdef SD_NO_UDEV
    return false;
#endif
    u->lib = dlopen("libudev.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!u->lib)
        return false;
#define SYM(field, name) \
    if (!(*(void **)&u->field = dlsym(u->lib, name))) goto fail;
    SYM(udev_new, "udev_new");
    SYM(udev_unref, "udev_unref");
    SYM(monitor_new_from_netlink, "udev_monitor_new_from_netlink");
    SYM(monitor_filter_add_match_subsystem_devtype,
        "udev_monitor_filter_add_match_subsystem_devtype");
    SYM(monitor_enable_receiving, "udev_monitor_enable_receiving");
    SYM(monitor_get_fd, "udev_monitor_get_fd");
    SYM(monitor_receive_device, "udev_monitor_receive_device");
    SYM(monitor_unref, "udev_monitor_unref");
    SYM(device_unref, "udev_device_unref");
#undef SYM
    return true;
  fail:
    dlclose(u->lib);
    u->lib = NULL;
    return false;
}

/* ---- sysfs helpers ---------------------------------------------------------- */
static bool read_attr(const char *dir, const char *attr, char *buf, size_t size)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, attr);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    bool ok = fgets(buf, (int)size, f) != NULL;
    fclose(f);
    if (!ok)
        return false;
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
        buf[--n] = '\0';
    return true;
}

static bool is_candidate_name(const char *n)
{
    return !strncmp(n, "ttyUSB", 6) || !strncmp(n, "ttyACM", 6) ||
        !strncmp(n, "rfcomm", 6) || !strncmp(n, "ttyS", 4);
}

/* Fill USB details by walking up from the tty's device directory to the
 * USB device (the ancestor that has idVendor). */
static void fill_usb(sd_device *d, const char *devdir)
{
    char path[PATH_MAX];
    if (!realpath(devdir, path))
        return;
    /* Interface number: the USB interface directory has bInterfaceNumber. */
    char cur[PATH_MAX];
    snprintf(cur, sizeof(cur), "%s", path);
    for (int depth = 0; depth < 8; depth++) {
        char tmp[64];
        if (!d->interface_num[0] &&
            read_attr(cur, "bInterfaceNumber", tmp, sizeof(tmp)))
            snprintf(d->interface_num, sizeof(d->interface_num), "%s", tmp);
        if (read_attr(cur, "idVendor", tmp, sizeof(tmp))) {
            d->usb = true;
            d->vid = (uint16_t)strtoul(tmp, NULL, 16);
            if (read_attr(cur, "idProduct", tmp, sizeof(tmp)))
                d->pid = (uint16_t)strtoul(tmp, NULL, 16);
            read_attr(cur, "manufacturer", d->manufacturer, sizeof(d->manufacturer));
            read_attr(cur, "product", d->product, sizeof(d->product));
            read_attr(cur, "serial", d->serial, sizeof(d->serial));
            return;
        }
        char *slash = strrchr(cur, '/');
        if (!slash || slash == cur)
            return;
        *slash = '\0';
    }
}

/* ttyS ports exist for every possible 8250 UART; only those with real
 * hardware report a non-zero UART type. */
static bool ttyS_is_real(const char *ttydir)
{
    char type[32];
    if (!read_attr(ttydir, "type", type, sizeof(type)))
        return false;
    return atoi(type) != 0;
}

static void find_by_id(sd_device *d, const char *dev_root)
{
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/serial/by-id", dev_root);
    DIR *dp = opendir(dir);
    if (!dp)
        return;
    struct dirent *e;
    while ((e = readdir(dp)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        char link[PATH_MAX], target[PATH_MAX];
        snprintf(link, sizeof(link), "%s/%s", dir, e->d_name);
        /* udev creates these as relative links to the node ("../../ttyUSB0");
         * compare that link's own target name rather than resolving it
         * fully, so a node that is itself a symlink still matches. */
        ssize_t n = readlink(link, target, sizeof(target) - 1);
        if (n <= 0)
            continue;
        target[n] = '\0';
        const char *base = strrchr(target, '/');
        base = base ? base + 1 : target;
        if (!strcmp(base, d->name)) {
            snprintf(d->by_id, sizeof(d->by_id), "%s", link);
            break;
        }
    }
    closedir(dp);
}

static int cmp_dev(const void *a, const void *b)
{
    return strcmp(((const sd_device *)a)->name, ((const sd_device *)b)->name);
}

size_t sd_scan(const char *sysfs_root, const char *dev_root, sd_device **out)
{
    if (!sysfs_root) sysfs_root = "/sys";
    if (!dev_root) dev_root = "/dev";
    char classdir[PATH_MAX];
    snprintf(classdir, sizeof(classdir), "%s/class/tty", sysfs_root);
    size_t n = 0, size = 16;
    sd_device *list = calloc(size, sizeof(sd_device));
    DIR *dp = opendir(classdir);
    if (!dp) {
        *out = list;
        return 0;
    }
    struct dirent *e;
    while ((e = readdir(dp)) != NULL) {
        if (!is_candidate_name(e->d_name))
            continue;
        char ttydir[PATH_MAX], devdir[PATH_MAX];
        snprintf(ttydir, sizeof(ttydir), "%s/%s", classdir, e->d_name);
        snprintf(devdir, sizeof(devdir), "%s/device", ttydir);
        struct stat st;
        /* Virtual ttys (consoles, ptys) have no "device" link. rfcomm
         * devices are the exception: they are virtual but real ports. */
        bool has_device = stat(devdir, &st) == 0;
        if (!has_device && strncmp(e->d_name, "rfcomm", 6))
            continue;
        if (!strncmp(e->d_name, "ttyS", 4) && !ttyS_is_real(ttydir))
            continue;
        if (n == size) {
            size *= 2;
            list = realloc(list, size * sizeof(sd_device));
        }
        sd_device *d = &list[n++];
        memset(d, 0, sizeof(*d));
        snprintf(d->name, sizeof(d->name), "%s", e->d_name);
        snprintf(d->devnode, sizeof(d->devnode), "%s/%s", dev_root, e->d_name);
        if (has_device) {
            char drv[PATH_MAX], target[PATH_MAX];
            snprintf(drv, sizeof(drv), "%s/driver", devdir);
            if (realpath(drv, target)) {
                const char *b = strrchr(target, '/');
                snprintf(d->driver, sizeof(d->driver), "%s", b ? b + 1 : target);
            }
            fill_usb(d, devdir);
        }
        find_by_id(d, dev_root);
    }
    closedir(dp);
    qsort(list, n, sizeof(sd_device), cmp_dev);
    *out = list;
    return n;
}

bool sd_device_accessible(const char *devnode, int *err)
{
    if (access(devnode, R_OK | W_OK) == 0)
        return true;
    if (err)
        *err = errno;
    return false;
}

/* ---- speed probe -------------------------------------------------------------- */
static speed_t baud_const(int speed)
{
    switch (speed) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default: return B0;
    }
}

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* Text a console sends at the right speed: printable ASCII, CR/LF, tab,
 * backspace, ESC (ANSI sequences), BEL. At a wrong speed the bytes are
 * framing garbage, mostly 0x00 and high-bit values. */
static bool looks_like_text(const unsigned char *buf, size_t n)
{
    size_t good = 0;
    bool newline = false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = buf[i];
        if (c == '\r' || c == '\n')
            newline = true;
        if ((c >= 0x20 && c < 0x7f) || c == '\r' || c == '\n' || c == '\t' ||
            c == '\b' || c == 0x1b || c == 0x07)
            good++;
    }
    /* A lone garbage byte can happen to be printable: want a line break or
     * a few characters. */
    return n > 0 && good * 10 >= n * 9 && (newline || n >= 4);
}

int sd_probe_speed(const char *devnode, const int *speeds, size_t n,
                   int timeout_ms, int *err)
{
    int fd = open(devnode, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (err)
            *err = errno;
        return 0;
    }
    struct termios t;
    if (tcgetattr(fd, &t) < 0) {
        if (err)
            *err = errno;
        close(fd);
        return 0;
    }
    int found = 0;
    for (size_t i = 0; i < n && !found; i++) {
        speed_t b = baud_const(speeds[i]);
        if (b == B0)
            continue;
        cfmakeraw(&t);
        t.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
        t.c_cflag |= CS8 | CLOCAL | CREAD;
        t.c_iflag &= ~(IXON | IXOFF | IXANY);
        cfsetispeed(&t, b);
        cfsetospeed(&t, b);
        if (tcsetattr(fd, TCSANOW, &t) < 0)
            continue;
        tcflush(fd, TCIOFLUSH);
        if (write(fd, "\r", 1) != 1)
            continue;
        unsigned char buf[256];
        size_t got = 0;
        const long deadline = now_ms() + timeout_ms;
        long end = deadline;
        for (;;) {
            long left = end - now_ms();
            if (left <= 0)
                break;
            struct pollfd p = {fd, POLLIN, 0};
            if (poll(&p, 1, (int)left) <= 0)
                break;
            ssize_t r = read(fd, buf + got, sizeof(buf) - got);
            if (r <= 0)
                break;
            got += (size_t)r;
            if (got == sizeof(buf))
                break;
            /* Once a reply has started, a short quiet gap ends it. */
            end = now_ms() + 80 < deadline ? now_ms() + 80 : deadline;
        }
        if (looks_like_text(buf, got))
            found = speeds[i];
    }
    close(fd);
    return found;
}

/* ---- monitor ------------------------------------------------------------------ */
struct sd_monitor {
    sd_callback cb;
    void *ctx;
    char *sysfs_root, *dev_root;
    sd_device *devs;
    size_t ndevs;
    udev_api udev;
    struct udev *u;
    struct udev_monitor *mon;
    int fd;
};

sd_monitor *sd_monitor_new(sd_callback cb, void *ctx, int flags,
                           const char *sysfs_root, const char *dev_root)
{
    sd_monitor *m = calloc(1, sizeof(*m));
    m->cb = cb;
    m->ctx = ctx;
    m->sysfs_root = strdup(sysfs_root ? sysfs_root : "/sys");
    m->dev_root = strdup(dev_root ? dev_root : "/dev");
    m->fd = -1;
    if (!(flags & SD_FLAG_NO_UDEV) && !sysfs_root && udev_load(&m->udev)) {
        m->u = m->udev.udev_new();
        if (m->u)
            m->mon = m->udev.monitor_new_from_netlink(m->u, "udev");
        if (m->mon &&
            m->udev.monitor_filter_add_match_subsystem_devtype(m->mon, "tty", NULL) >= 0 &&
            m->udev.monitor_enable_receiving(m->mon) >= 0) {
            m->fd = m->udev.monitor_get_fd(m->mon);
        }
        if (m->fd < 0) {
            if (m->mon) m->udev.monitor_unref(m->mon);
            if (m->u) m->udev.udev_unref(m->u);
            m->mon = NULL;
            m->u = NULL;
        }
    }
    m->ndevs = sd_scan(m->sysfs_root, m->dev_root, &m->devs);
    return m;
}

void sd_monitor_free(sd_monitor *m)
{
    if (!m)
        return;
    if (m->mon) m->udev.monitor_unref(m->mon);
    if (m->u) m->udev.udev_unref(m->u);
    if (m->udev.lib) dlclose(m->udev.lib);
    free(m->devs);
    free(m->sysfs_root);
    free(m->dev_root);
    free(m);
}

int sd_monitor_fd(const sd_monitor *m)
{
    return m->fd;
}

int sd_monitor_poll_interval_ms(const sd_monitor *m)
{
    return m->fd >= 0 ? 0 : 500;
}

const char *sd_monitor_backend(const sd_monitor *m)
{
    return m->fd >= 0 ? "udev" : "poll";
}

size_t sd_monitor_list(const sd_monitor *m, const sd_device **out)
{
    *out = m->devs;
    return m->ndevs;
}

static const sd_device *find_dev(const sd_device *list, size_t n, const char *name)
{
    for (size_t i = 0; i < n; i++)
        if (!strcmp(list[i].name, name))
            return &list[i];
    return NULL;
}

void sd_monitor_rescan(sd_monitor *m)
{
    sd_device *nd;
    size_t nn = sd_scan(m->sysfs_root, m->dev_root, &nd);
    sd_device *old = m->devs;
    size_t on = m->ndevs;
    m->devs = nd;
    m->ndevs = nn;
    for (size_t i = 0; i < on; i++) {
        const sd_device *now = find_dev(nd, nn, old[i].name);
        /* A device re-plugged under the same name with a different
         * identity counts as removed + added. */
        if (!now || strcmp(now->by_id, old[i].by_id) || now->vid != old[i].vid ||
            now->pid != old[i].pid || strcmp(now->serial, old[i].serial))
            m->cb(m->ctx, SD_REMOVED, &old[i]);
    }
    for (size_t i = 0; i < nn; i++) {
        const sd_device *was = find_dev(old, on, nd[i].name);
        if (!was || strcmp(was->by_id, nd[i].by_id) || was->vid != nd[i].vid ||
            was->pid != nd[i].pid || strcmp(was->serial, nd[i].serial))
            m->cb(m->ctx, SD_ADDED, &nd[i]);
    }
    free(old);
}

void sd_monitor_dispatch(sd_monitor *m)
{
    if (m->mon) {
        /* Drain the netlink queue; the events only tell us to rescan. */
        struct udev_device *d;
        while ((d = m->udev.monitor_receive_device(m->mon)) != NULL)
            m->udev.device_unref(d);
    }
    sd_monitor_rescan(m);
}
