/*
 * fe_platform.c - process-global front-end hooks that PuTTY's GTK front
 * end (unix/window.c, unix/dialog.c, unix/putty.c, unix/gtk-common.c,
 * unix/utils/buildinfo_gtk_version.c) would otherwise provide. Values are
 * the same as GTK PuTTY's so that saved sessions behave identically.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "engine_internal.h"

/* ---- settings defaults (window.c) ------------------------------------- */
FontSpec *platform_default_fontspec(const char *name)
{
    if (!strcmp(name, "Font"))
        return fontspec_new(DEFAULT_GTK_FONT);
    else
        return fontspec_new_default();
}

Filename *platform_default_filename(const char *name)
{
    if (!strcmp(name, "LogFileName"))
        return filename_from_str("putty.log");
    else
        return filename_from_str("");
}

char *platform_default_s(const char *name)
{
    if (!strcmp(name, "SerialLine"))
        return dupstr("/dev/ttyS0");
    return NULL;
}

bool platform_default_b(const char *name, bool def)
{
    if (!strcmp(name, "WinNameAlways")) {
        /* X natively supports icon titles, so use 'em by default */
        return false;
    }
    return def;
}

int platform_default_i(const char *name, int def)
{
    if (!strcmp(name, "CloseOnExit"))
        return 2;  /* maps to FORCE_ON after painful rearrangement :-( */
    return def;
}

/* ---- X11 (unix/putty.c, unix/storage.c) -------------------------------- */
char *platform_get_x_display(void)
{
    /* GTK PuTTY asks GDK first; without GDK, it falls back to $DISPLAY,
     * which on Wayland points at XWayland. */
    const char *display = getenv("DISPLAY");
    return dupstr(display);
}

char *x_get_default(const char *key)
{
    (void)key;
    return NULL;      /* no X resource database */
}

/* ---- connection sharing (unix/putty.c) ---------------------------------- */
const bool share_can_be_downstream = true;
const bool share_can_be_upstream = true;

/* ---- build info -------------------------------------------------------- */
const bool buildinfo_gtk_relevant = false;
char *buildinfo_gtk_version(void)
{
    return NULL;
}

/* ---- messages ---------------------------------------------------------- */
void nonfatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *msg = dupvprintf(fmt, ap);
    va_end(ap);
    sshlx_global_event_fire(SSHLX_GLOBAL_NONFATAL, msg);
    sfree(msg);
}

void old_keyfile_warning(void)
{
    /* Same text as unix/dialog.c. */
    sshlx_global_event_fire(
        SSHLX_GLOBAL_OLD_KEYFILE,
        "You are loading an SSH-2 private key which has an old version of "
        "the file format. This means your key file is not fully "
        "tamperproof. Future versions of PuTTY may stop supporting this "
        "private key format. We recommend you convert your key to the new "
        "format.\n\n"
        "Once the key is loaded into PuTTYgen, you can perform this "
        "conversion simply by saving it again.");
}

static char last_fatal[1024] = "";

const char *sshlx_engine_last_fatal(void)
{
    return last_fatal;
}

static void (*fatal_handler)(const char *msg);

void sshlx_engine_set_fatal_handler(void (*fn)(const char *msg))
{
    fatal_handler = fn;
}

/*
 * PuTTY calls this for unrecoverable internal errors (out of memory,
 * impossible terminal states). It must not return. PuTTY's GTK front end
 * shows a message box and exits; we record the message, give the UI one
 * synchronous chance to write a crash breadcrumb, and exit. Continuing in
 * the same process would leave shared PuTTY state undefined (see
 * ARCHITECTURE-NOTES.md section 5).
 */
void modalfatalbox(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_fatal, sizeof(last_fatal), fmt, ap);
    va_end(ap);
    fprintf(stderr, "sshLX: fatal error: %s\n", last_fatal);
    if (fatal_handler)
        fatal_handler(last_fatal);
    _exit(1);
}
