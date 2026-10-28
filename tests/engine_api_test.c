/*
 * engine_api_test.c - unit tests for the non-session parts of the engine
 * API: Conf reflection, saved sessions (PuTTY storage format, in a
 * temporary $PUTTYDIR), per-key value transfer, protocols, and the
 * generic config-dialog bridge running PuTTY's own handlers.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "testloop.h"

static char tmpdir[256];

static void setup_puttydir(void)
{
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/sshlx-apitest-XXXXXX");
    const char *base = getenv("TMPDIR");
    if (base) {
        snprintf(tmpdir, sizeof(tmpdir), "%s/sshlx-apitest-XXXXXX", base);
    }
    CHECK(mkdtemp(tmpdir) != NULL);
    setenv("PUTTYDIR", tmpdir, 1);
}

static void test_conf_reflection(void)
{
    size_t n = sshlx_conf_key_count();
    CHECK(n > 200);
    int host = sshlx_conf_key_find("HostName");
    CHECK(host >= 0);
    CHECK(sshlx_conf_key_find("host") == host);
    const sshlx_conf_key_info *ki = sshlx_conf_key_get(host);
    CHECK(!strcmp(ki->id, "host"));
    CHECK(ki->value_type == SSHLX_T_STR);
    CHECK(ki->subkey_type == SSHLX_T_NONE);

    /* An enum-mapped key */
    int coe = sshlx_conf_key_find("CloseOnExit");
    CHECK(coe >= 0);
    CHECK(sshlx_conf_key_get(coe)->n_enum == 3);

    /* Keyed keys */
    int pf = sshlx_conf_key_find("portfwd");
    CHECK(pf >= 0 && sshlx_conf_key_get(pf)->subkey_type == SSHLX_T_STR);
    int colours = sshlx_conf_key_find("colours");
    CHECK(colours >= 0 && sshlx_conf_key_get(colours)->subkey_type == SSHLX_T_INT);

    int nbool = 0, nint = 0, nstr = 0, nfont = 0, nfile = 0;
    for (size_t i = 0; i < n; i++) {
        const sshlx_conf_key_info *k = sshlx_conf_key_get(i);
        switch (k->value_type) {
          case SSHLX_T_BOOL: nbool++; break;
          case SSHLX_T_INT: nint++; break;
          case SSHLX_T_STR: case SSHLX_T_STR_AMBI: nstr++; break;
          case SSHLX_T_FONT: nfont++; break;
          case SSHLX_T_FILENAME: nfile++; break;
          default: break;
        }
    }
    printf("conf keys: %zu (bool %d, int %d, str %d, font %d, file %d)\n",
           n, nbool, nint, nstr, nfont, nfile);
    CHECK(nfont == 4 && nfile == 6);
}

static void test_defaults_and_saved_sessions(void)
{
    sshlx_conf *c = sshlx_conf_new_default();
    int font = sshlx_conf_key_find("Font");
    int serline = sshlx_conf_key_find("SerialLine");
    int proto = sshlx_conf_key_protocol();
    int port = sshlx_conf_key_find("PortNumber");
    int host = sshlx_conf_key_find("HostName");
    int user = sshlx_conf_key_find("UserName");
    /* GTK PuTTY's platform defaults */
    CHECK(!strcmp(sshlx_conf_get_str(c, font), "client:Monospace 12"));
    CHECK(!strcmp(sshlx_conf_get_str(c, serline), "/dev/ttyS0"));
    CHECK(sshlx_conf_get_int(c, port) == 22);

    sshlx_conf_set_str(c, host, "example.org");
    sshlx_conf_set_str(c, user, "alice");
    sshlx_conf_set_int(c, port, 2222);
    CHECK(sshlx_conf_launchable(c));
    char *dest = sshlx_conf_dest(c);
    CHECK(!strcmp(dest, "example.org"));
    sshlx_free(dest);

    char *err = NULL;
    CHECK(sshlx_conf_save(c, "Test Session/one", &err) == 0);

    /* The file must be in PuTTY's format, readable by PuTTY. */
    char path[512];
    snprintf(path, sizeof(path), "%s/sessions/Test%%20Session%%2Fone", tmpdir);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    char line[512];
    bool saw_host = false, saw_port = false;
    while (fgets(line, sizeof(line), f)) {
        if (!strcmp(line, "HostName=example.org\n")) saw_host = true;
        if (!strcmp(line, "PortNumber=2222\n")) saw_port = true;
    }
    fclose(f);
    CHECK(saw_host && saw_port);

    sshlx_strlist *l = sshlx_saved_sessions_list();
    bool found = false;
    CHECK(l->n >= 2);
    CHECK(!strcmp(l->items[0], "Default Settings"));
    for (size_t i = 0; i < l->n; i++)
        if (!strcmp(l->items[i], "Test Session/one")) found = true;
    CHECK(found);
    sshlx_strlist_free(l);

    bool exists = false;
    sshlx_conf *c2 = sshlx_conf_load("Test Session/one", &exists);
    CHECK(exists);
    CHECK(!strcmp(sshlx_conf_get_str(c2, host), "example.org"));
    CHECK(sshlx_conf_get_int(c2, port) == 2222);
    CHECK(sshlx_conf_get_int(c2, proto) == sshlx_conf_get_int(c, proto));
    sshlx_conf_free(c2);

    sshlx_saved_session_delete("Test Session/one");
    sshlx_conf *c3 = sshlx_conf_load("Test Session/one", &exists);
    CHECK(!exists);
    sshlx_conf_free(c3);
    sshlx_conf_free(c);
}

static void test_key_transfer(void)
{
    sshlx_conf *a = sshlx_conf_new_default();
    sshlx_conf *b = sshlx_conf_new_default();
    int pf = sshlx_conf_key_find("portfwd");
    int user = sshlx_conf_key_find("UserName");
    int colours = sshlx_conf_key_find("colours");
    int font = sshlx_conf_key_find("Font");

    sshlx_conf_set_str_str(a, pf, "L8080", "localhost:80");
    sshlx_conf_set_str_str(a, pf, "R9000", "localhost:22");
    sshlx_conf_set_str_str(b, pf, "L1", "x:1");
    sshlx_conf_set_str(a, user, "bob");
    sshlx_conf_set_int_int(a, colours, 0, 1);
    sshlx_conf_set_str(a, font, "client:DejaVu Sans Mono 11");

    CHECK(!sshlx_conf_key_equal(a, b, pf));
    sshlx_conf_key_copy(b, a, pf);
    CHECK(sshlx_conf_key_equal(a, b, pf));
    sshlx_strlist *subs = sshlx_conf_str_subkeys(b, pf);
    CHECK(subs->n == 2);          /* L1 replaced, not merged */
    sshlx_strlist_free(subs);
    CHECK(!strcmp(sshlx_conf_get_str_str(b, pf, "L8080"), "localhost:80"));

    char *v = sshlx_conf_key_export(a, user);
    CHECK(sshlx_conf_key_import(b, user, v));
    CHECK(!strcmp(sshlx_conf_get_str(b, user), "bob"));
    sshlx_free(v);

    CHECK(!sshlx_conf_key_import(b, user, "vzz"));
    CHECK(!sshlx_conf_key_import(b, user, "garbage"));

    sshlx_conf_key_copy(b, a, colours);
    CHECK(sshlx_conf_get_int_int(b, colours, 0, -1) == 1);
    CHECK(sshlx_conf_get_int_int(b, colours, 100000, -7) == -7);

    sshlx_conf_key_copy(b, a, font);
    CHECK(!strcmp(sshlx_conf_get_str(b, font), "client:DejaVu Sans Mono 11"));

    sshlx_conf_free(a);
    sshlx_conf_free(b);
}

static void test_protocols(void)
{
    size_t n = sshlx_protocol_count();
    const char *want[] = { "ssh", "serial", "telnet", "rlogin", "supdup",
                           "raw" };
    for (size_t w = 0; w < sizeof(want) / sizeof(*want); w++) {
        bool found = false;
        for (size_t i = 0; i < n; i++)
            if (!strcmp(sshlx_protocol_get(i)->name, want[w]))
                found = true;
        CHECK(found);
    }
    printf("protocols: %zu\n", n);
}

/* ---- dialog bridge ------------------------------------------------------- */
static int dlg_end_value = -100;
static int dlg_changes;
static void dlg_changed(void *ctx, int id) { (void)ctx; (void)id; dlg_changes++; }
static void dlg_end_cb(void *ctx, int v) { (void)ctx; dlg_end_value = v; }

static int find_ctrl(sshlx_dlg *d, const char *path, sshlx_ctrl_type type,
                     const char *label_prefix)
{
    const sshlx_ctrl_set *sets;
    size_t n = sshlx_dlg_sets(d, &sets);
    for (size_t i = 0; i < n; i++) {
        if (path && strcmp(sets[i].path, path))
            continue;
        for (size_t j = 0; j < sets[i].n_ctrls; j++) {
            const sshlx_ctrl_desc *c = &sets[i].ctrls[j];
            if (c->type == type && c->label &&
                !strncmp(c->label, label_prefix, strlen(label_prefix)))
                return c->id;
        }
    }
    return -1;
}

static void test_dialog_bridge(void)
{
    sshlx_conf *c = sshlx_conf_new_default();
    sshlx_dlg_callbacks cb;
    memset(&cb, 0, sizeof(cb));
    cb.ctrl_changed = dlg_changed;
    cb.end = dlg_end_cb;
    sshlx_dlg *d = sshlx_dlg_new_config(c, false, 0, &cb);
    sshlx_dlg_refresh_all(d);
    CHECK(dlg_changes > 50);

    const sshlx_ctrl_set *sets;
    size_t nsets = sshlx_dlg_sets(d, &sets);
    size_t nctrls = 0, npanels = 0;
    const char *last = "";
    for (size_t i = 0; i < nsets; i++) {
        nctrls += sets[i].n_ctrls;
        if (*sets[i].path && strcmp(sets[i].path, last)) {
            npanels++;
            last = sets[i].path;
        }
    }
    printf("config box: %zu sets, %zu controls, %zu panel paths\n",
           nsets, nctrls, npanels);
    CHECK(nctrls > 200);

    /* Host name edit box: PuTTY's handler writes the Conf. */
    int hostctl = find_ctrl(d, "Session", SSHLX_C_EDITBOX, "Host Name");
    CHECK(hostctl >= 0);
    sshlx_dlg_set_text(d, hostctl, "dialog.example");
    CHECK(!strcmp(sshlx_conf_get_str(c, sshlx_conf_key_find("HostName")),
                  "dialog.example"));

    /* A checkbox deep in the tree. */
    int cb_id = find_ctrl(d, "Terminal", SSHLX_C_CHECKBOX, "Auto wrap mode");
    CHECK(cb_id >= 0);
    int wrapkey = sshlx_conf_key_find("wrap_mode");
    bool before = sshlx_conf_get_bool(c, wrapkey);
    sshlx_dlg_set_int(d, cb_id, !before);
    CHECK(sshlx_conf_get_bool(c, wrapkey) == !before);

    /* Protocol radio buttons / drop-down list change the port too. */
    int portctl = find_ctrl(d, "Session", SSHLX_C_EDITBOX, "Port");
    CHECK(portctl >= 0);
    CHECK(!strcmp(sshlx_dlg_get_text(d, portctl), "22"));

    /* The global "Cancel" button ends the dialog with 0. */
    int cancel = find_ctrl(d, "", SSHLX_C_BUTTON, "Cancel");
    CHECK(cancel >= 0);
    sshlx_dlg_activate(d, cancel);
    CHECK(dlg_end_value == 0);

    sshlx_dlg_free(d);

    /* Host CA box builds too. */
    sshlx_dlg *ca = sshlx_dlg_new_ca_config(&cb);
    sshlx_dlg_refresh_all(ca);
    CHECK(sshlx_dlg_sets(ca, &sets) > 0);
    sshlx_dlg_free(ca);
    sshlx_conf_free(c);
}

int main(void)
{
    setup_puttydir();
    tl_init();
    test_conf_reflection();
    test_defaults_and_saved_sessions();
    test_key_transfer();
    test_protocols();
    test_dialog_bridge();
    sshlx_engine_shutdown();
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpdir);
    if (system(cmd) != 0) { /* best effort */ }
    printf("engine_api_test: OK\n");
    return 0;
}
