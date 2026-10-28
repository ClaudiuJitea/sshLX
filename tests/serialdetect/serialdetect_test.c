/*
 * serialdetect_test.c - serial detection against a mocked sysfs + /dev
 * tree (no udev, no real hardware), including hot-plug add/remove and
 * identity change on re-plug.
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "serialdetect.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
    __FILE__, __LINE__, #c); exit(1); } } while (0)

static char root[256], sys_root[300], dev_root[300];

static void sh(const char *fmt, ...)
{
    char cmd[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    CHECK(system(cmd) == 0);
}

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    CHECK(f);
    fputs(content, f);
    fclose(f);
}

/* Fake a USB serial adapter: a USB device dir with ids/strings, an
 * interface dir, the tty class entry linking to it, a /dev node and a
 * /dev/serial/by-id symlink. */
static void plug_usb(const char *tty, const char *vid, const char *pid,
                     const char *mfr, const char *prod, const char *serial,
                     const char *driver)
{
    char usbdev[600], intf[700], path[900];
    snprintf(usbdev, sizeof(usbdev), "%s/devices/usb1/1-%s", sys_root, tty);
    snprintf(intf, sizeof(intf), "%s/1-%s:1.0", usbdev, tty);
    sh("mkdir -p '%s/%s' '%s/drivers/%s'", intf, tty, sys_root, driver);
    snprintf(path, sizeof(path), "%s/idVendor", usbdev); write_file(path, vid);
    snprintf(path, sizeof(path), "%s/idProduct", usbdev); write_file(path, pid);
    snprintf(path, sizeof(path), "%s/manufacturer", usbdev); write_file(path, mfr);
    snprintf(path, sizeof(path), "%s/product", usbdev); write_file(path, prod);
    snprintf(path, sizeof(path), "%s/serial", usbdev); write_file(path, serial);
    snprintf(path, sizeof(path), "%s/bInterfaceNumber", intf); write_file(path, "00\n");
    sh("ln -sfn '%s/drivers/%s' '%s/driver'", sys_root, driver, intf);
    sh("mkdir -p '%s/class/tty/%s'", sys_root, tty);
    sh("ln -sfn '%s' '%s/class/tty/%s/device'", intf, sys_root, tty);
    sh("touch '%s/%s'", dev_root, tty);
    sh("mkdir -p '%s/serial/by-id'", dev_root);
    sh("ln -sfn ../../%s '%s/serial/by-id/usb-%s_%s_%s-if00-port0'",
       tty, dev_root, mfr, prod, serial);
}

static void unplug(const char *tty)
{
    sh("rm -rf '%s/class/tty/%s' '%s/%s'", sys_root, tty, dev_root, tty);
    sh("find '%s/serial/by-id' -xtype l -delete", dev_root);
}

static int added, removed;
static sd_device last;
static void cb(void *ctx, sd_event ev, const sd_device *d)
{
    (void)ctx;
    if (ev == SD_ADDED) added++; else removed++;
    last = *d;
    printf("%s %s %s %04x:%04x '%s' '%s' '%s'\n", ev == SD_ADDED ? "ADD" : "DEL",
           d->name, d->by_id, d->vid, d->pid, d->manufacturer, d->product, d->serial);
}

int main(void)
{
    snprintf(root, sizeof(root), "%s/sshlx-sdtest-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(root));
    snprintf(sys_root, sizeof(sys_root), "%s/sys", root);
    snprintf(dev_root, sizeof(dev_root), "%s/dev", root);
    sh("mkdir -p '%s/class/tty' '%s'", sys_root, dev_root);

    /* Non-serial ttys that must be ignored: a console (no device link)
     * and a ttyS without a UART (type 0); plus a real ttyS0 (type 4). */
    sh("mkdir -p '%s/class/tty/tty1'", sys_root);
    sh("mkdir -p '%s/devices/platform/serial8250/tty/ttyS1' '%s/devices/pnp0/00:01/tty/ttyS0'",
       sys_root, sys_root);
    sh("mkdir -p '%s/class/tty/ttyS1' '%s/class/tty/ttyS0'", sys_root, sys_root);
    sh("ln -sfn '%s/devices/platform/serial8250' '%s/class/tty/ttyS1/device'", sys_root, sys_root);
    sh("ln -sfn '%s/devices/pnp0/00:01' '%s/class/tty/ttyS0/device'", sys_root, sys_root);
    char p[600];
    snprintf(p, sizeof(p), "%s/class/tty/ttyS1/type", sys_root); write_file(p, "0\n");
    snprintf(p, sizeof(p), "%s/class/tty/ttyS0/type", sys_root); write_file(p, "4\n");

    plug_usb("ttyACM0", "2341", "0043", "Arduino", "Uno", "8573531", "cdc_acm");

    sd_monitor *m = sd_monitor_new(cb, NULL, SD_FLAG_NO_UDEV, sys_root, dev_root);
    CHECK(!strcmp(sd_monitor_backend(m), "poll"));
    CHECK(sd_monitor_poll_interval_ms(m) > 0 && sd_monitor_poll_interval_ms(m) <= 1000);
    const sd_device *list;
    size_t n = sd_monitor_list(m, &list);
    CHECK(n == 2);   /* ttyACM0 + ttyS0; not tty1, not ttyS1 */
    CHECK(!strcmp(list[0].name, "ttyACM0"));
    CHECK(list[0].usb && list[0].vid == 0x2341 && list[0].pid == 0x0043);
    CHECK(!strcmp(list[0].product, "Uno"));
    CHECK(!strcmp(list[0].driver, "cdc_acm"));
    CHECK(strstr(list[0].by_id, "/serial/by-id/usb-Arduino_Uno_8573531-if00-port0"));
    CHECK(!strcmp(list[1].name, "ttyS0") && !list[1].usb);

    /* Hot-plug an FTDI adapter. */
    plug_usb("ttyUSB0", "0403", "6001", "FTDI", "FT232R_USB_UART", "A10KZ8QX", "ftdi_sio");
    sd_monitor_dispatch(m);
    CHECK(added == 1 && removed == 0);
    CHECK(!strcmp(last.name, "ttyUSB0") && last.vid == 0x0403 && last.pid == 0x6001);
    CHECK(!strcmp(last.serial, "A10KZ8QX"));
    CHECK(!strcmp(last.manufacturer, "FTDI"));
    CHECK(!strcmp(last.devnode + strlen(dev_root), "/ttyUSB0"));
    CHECK(!strcmp(last.interface_num, "00"));

    /* No change: no events. */
    sd_monitor_dispatch(m);
    CHECK(added == 1 && removed == 0);

    /* Unplug. */
    unplug("ttyUSB0");
    sd_monitor_dispatch(m);
    CHECK(removed == 1 && !strcmp(last.name, "ttyUSB0"));

    /* A different adapter appears under the same name: remove + add are
     * not confused with the old one. */
    plug_usb("ttyUSB0", "0403", "6001", "FTDI", "FT232R_USB_UART", "A10KZ8QX", "ftdi_sio");
    sd_monitor_dispatch(m);
    CHECK(added == 2);
    unplug("ttyUSB0");
    plug_usb("ttyUSB0", "10c4", "ea60", "Silicon_Labs", "CP2102", "0001", "cp210x");
    sd_monitor_dispatch(m);
    CHECK(added == 3 && removed == 2);
    CHECK(last.vid == 0x10c4);

    sd_monitor_free(m);

    /* The real system: whatever is there, the scan must not crash, and
     * the udev backend should load if libudev.so.1 exists. */
    sd_monitor *real = sd_monitor_new(cb, NULL, 0, NULL, NULL);
    printf("real system backend: %s, %zu device(s)\n", sd_monitor_backend(real),
           sd_monitor_list(real, &list));
    sd_monitor_free(real);

    sh("rm -rf '%s'", root);
    printf("serialdetect_test: OK\n");
    return 0;
}
