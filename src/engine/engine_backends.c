/*
 * engine_backends.c - sshLX's list of back ends, in place of PuTTY's
 * per-application be_list.c (same contents as be_list.c built with SSH,
 * SERIAL and OTHERBACKENDS, plus sshLX's local shell at the end).
 *
 * PuTTY's config box shows backends[] entries below PROTOCOL_LIMIT only,
 * so the local shell, last, is chosen in sshLX's own UI rather than there.
 */
#include "engine_internal.h"

const char *const appname = "sshLX";

const int be_default_protocol = PROT_SSH;

const struct BackendVtable *const backends[] = {
    &ssh_backend,
    &serial_backend,
    &telnet_backend,
    &rlogin_backend,
    &supdup_backend,
    &raw_backend,
    &sshconn_backend,
    &sshlx_localshell_backend,
    NULL
};

/* SSH and Serial get radio buttons in PuTTY's config box. */
const size_t n_ui_backends = 2;
