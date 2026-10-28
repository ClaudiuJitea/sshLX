/*
 * serialdetect.h - serial device discovery and hot-plug monitoring (Linux).
 *
 * Plain C, independent of the PuTTY engine and of Qt. Reports tty devices
 * that are real serial ports: ttyUSB*, ttyACM*, rfcomm*, and ttyS* that
 * have a UART behind them.
 *
 * Change notification uses libudev's netlink monitor when available
 * (loaded at runtime with dlopen, so no build-time dependency); otherwise
 * the caller polls. Device details always come from sysfs and
 * /dev/serial/by-id, so both paths report identical data.
 */
#ifndef SSHLX_SERIALDETECT_H
#define SSHLX_SERIALDETECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sd_device {
    char name[64];            /* kernel name, e.g. "ttyUSB0" */
    char devnode[256];        /* e.g. "/dev/ttyUSB0" */
    char by_id[512];          /* stable /dev/serial/by-id/... path or "" */
    bool usb;
    uint16_t vid, pid;        /* USB ids (0 if not USB) */
    char manufacturer[128];
    char product[128];
    char serial[128];         /* USB serial number string */
    char driver[64];          /* kernel driver, e.g. "ftdi_sio" */
    char interface_num[8];    /* USB interface number, e.g. "00" */
} sd_device;

typedef enum { SD_ADDED, SD_REMOVED } sd_event;
typedef void (*sd_callback)(void *ctx, sd_event ev, const sd_device *dev);

typedef struct sd_monitor sd_monitor;

enum {
    SD_FLAG_NO_UDEV = 1       /* force the polling fallback */
};

/* Create a monitor. sysfs_root/dev_root default to "/sys" and "/dev"
 * (non-NULL values are for tests). The initial device set is scanned
 * immediately but no events are reported for it; use sd_monitor_list(). */
sd_monitor *sd_monitor_new(sd_callback cb, void *ctx, int flags,
                           const char *sysfs_root, const char *dev_root);
void sd_monitor_free(sd_monitor *m);

/* If >= 0, watch this fd for readability and call sd_monitor_dispatch(). */
int sd_monitor_fd(const sd_monitor *m);
/* If > 0, also call sd_monitor_dispatch() at this interval (polling
 * fallback, or settling udev events). */
int sd_monitor_poll_interval_ms(const sd_monitor *m);
/* Process pending changes; reports SD_ADDED/SD_REMOVED via the callback. */
void sd_monitor_dispatch(sd_monitor *m);
/* Force a rescan (e.g. from a timer after a udev event). */
void sd_monitor_rescan(sd_monitor *m);

/* Current devices (borrowed until the next dispatch/rescan). */
size_t sd_monitor_list(const sd_monitor *m, const sd_device **out);
/* "udev" or "poll" */
const char *sd_monitor_backend(const sd_monitor *m);

/* One-shot scan without a monitor. Caller frees *out with free(). */
size_t sd_scan(const char *sysfs_root, const char *dev_root, sd_device **out);

/* Is this device node accessible for read/write by this process? Fills
 * *err with errno on failure. */
bool sd_device_accessible(const char *devnode, int *err);

/* Guess a console's line speed: at each of speeds[0..n) (8N1, no flow
 * control) send a carriage return and listen up to timeout_ms for a reply.
 * The first speed answered with readable text (a prompt or a blank line)
 * wins, so list the likeliest first. Returns 0 if none answered or the
 * port could not be opened (*err gets errno then). Blocks; call it off the
 * GUI thread. */
int sd_probe_speed(const char *devnode, const int *speeds, size_t n,
                   int timeout_ms, int *err);

#ifdef __cplusplus
}
#endif
#endif
