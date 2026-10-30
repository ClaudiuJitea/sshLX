/*
 * engine_conf.c - saved sessions (through PuTTY's own settings.c and
 * unix/storage.c, so the on-disk format is PuTTY's), Conf reflection for
 * the property grid (from PuTTY's conf_key_info[] table) and per-key
 * value transfer for folder inheritance.
 */
#include <stdlib.h>
#include <string.h>

#include "engine_internal.h"

/* ---- saved sessions ------------------------------------------------------ */
void sshlx_strlist_free(sshlx_strlist *l)
{
    if (!l)
        return;
    for (size_t i = 0; i < l->n; i++)
        sfree(l->items[i]);
    sfree(l->items);
    sfree(l);
}

sshlx_strlist *sshlx_saved_sessions_list(void)
{
    struct sesslist sl;
    get_sesslist(&sl, true);
    sshlx_strlist *l = snew(sshlx_strlist);
    l->n = sl.nsessions;
    l->items = snewn(l->n ? l->n : 1, char *);
    for (size_t i = 0; i < l->n; i++)
        l->items[i] = dupstr(sl.sessions[i]);
    get_sesslist(&sl, false);
    return l;
}

sshlx_conf *sshlx_conf_new_default(void)
{
    Conf *c = conf_new();
    do_defaults(NULL, c);
    return SCONF_OF(c);
}

sshlx_conf *sshlx_conf_load(const char *session_name, bool *exists_out)
{
    Conf *c = conf_new();
    bool exists = do_defaults(session_name, c);
    if (exists_out)
        *exists_out = exists;
    return SCONF_OF(c);
}

int sshlx_conf_save(const sshlx_conf *conf, const char *session_name,
                    char **errmsg_out)
{
    char *err = save_settings(session_name, CONF_OF(conf));
    if (err) {
        if (errmsg_out)
            *errmsg_out = err;
        else
            sfree(err);
        return -1;
    }
    if (errmsg_out)
        *errmsg_out = NULL;
    return 0;
}

void sshlx_saved_session_delete(const char *session_name)
{
    del_settings(session_name);
}

sshlx_conf *sshlx_conf_copy(const sshlx_conf *conf)
{
    return SCONF_OF(conf_copy(CONF_OF(conf)));
}

void sshlx_conf_copy_into(sshlx_conf *dst, const sshlx_conf *src)
{
    conf_copy_into(CONF_OF(dst), CONF_OF(src));
}

void sshlx_conf_free(sshlx_conf *conf)
{
    if (conf)
        conf_free(CONF_OF(conf));
}

bool sshlx_conf_launchable(const sshlx_conf *conf)
{
    /* The local shell needs no host name. */
    if (conf_get_int(CONF_OF(conf), CONF_protocol) == SSHLX_PROT_LOCALSHELL)
        return true;
    return conf_launchable(CONF_OF(conf));
}

char *sshlx_conf_dest(const sshlx_conf *conf)
{
    return dupstr(conf_dest(CONF_OF(conf)));
}

/* ---- reflection ------------------------------------------------------------ */
static const char *const conf_ids[] = {
    #define CONF_OPTION(keyword, ...) #keyword,
    #include "conf.h"
    #undef CONF_OPTION
};
typedef char conf_ids_check[lenof(conf_ids) == N_CONFIG_OPTIONS ? 1 : -1];

static sshlx_conf_key_info key_infos[N_CONFIG_OPTIONS];
static sshlx_enum_value *enum_storage[N_CONFIG_OPTIONS];
static bool key_infos_built;

static sshlx_type map_type(int t)
{
    switch (t) {
      case CONF_TYPE_BOOL: return SSHLX_T_BOOL;
      case CONF_TYPE_INT: return SSHLX_T_INT;
      case CONF_TYPE_STR: return SSHLX_T_STR;
      case CONF_TYPE_UTF8: return SSHLX_T_STR;
      case CONF_TYPE_STR_AMBI: return SSHLX_T_STR_AMBI;
      case CONF_TYPE_FILENAME: return SSHLX_T_FILENAME;
      case CONF_TYPE_FONT: return SSHLX_T_FONT;
      default: return SSHLX_T_NONE;
    }
}

static void build_key_infos(void)
{
    if (key_infos_built)
        return;
    for (int k = 0; k < N_CONFIG_OPTIONS; k++) {
        const ConfKeyInfo *ki = &conf_key_info[k];
        sshlx_conf_key_info *o = &key_infos[k];
        o->key = k;
        o->id = conf_ids[k];
        o->save_keyword = ki->save_keyword;
        o->value_type = map_type(ki->value_type);
        o->subkey_type = map_type(ki->subkey_type);
        o->not_saved = ki->not_saved;
        o->has_default = !ki->load_custom &&
            ki->subkey_type == CONF_TYPE_NONE;
        switch (ki->value_type) {
          case CONF_TYPE_BOOL: o->default_bool = ki->default_value.bval; break;
          case CONF_TYPE_INT: o->default_int = ki->default_value.ival; break;
          case CONF_TYPE_STR: case CONF_TYPE_UTF8: case CONF_TYPE_STR_AMBI:
          case CONF_TYPE_FILENAME: case CONF_TYPE_FONT:
            o->default_str = ki->default_value.sval;
            break;
        }
        if (ki->storage_enum) {
            size_t n = ki->storage_enum->nvalues;
            enum_storage[k] = snewn(n, sshlx_enum_value);
            for (size_t i = 0; i < n; i++) {
                enum_storage[k][i].value = ki->storage_enum->values[i].confval;
                enum_storage[k][i].storage_value =
                    ki->storage_enum->values[i].storageval;
                enum_storage[k][i].obsolete =
                    ki->storage_enum->values[i].obsolete;
            }
            o->n_enum = n;
            o->enum_values = enum_storage[k];
        }
    }
    key_infos_built = true;
}

size_t sshlx_conf_key_count(void)
{
    return N_CONFIG_OPTIONS;
}

const sshlx_conf_key_info *sshlx_conf_key_get(size_t key)
{
    if (key >= N_CONFIG_OPTIONS)
        return NULL;
    build_key_infos();
    return &key_infos[key];
}

int sshlx_conf_key_find(const char *name)
{
    if (!name)
        return -1;
    for (int k = 0; k < N_CONFIG_OPTIONS; k++) {
        if (!strcmp(conf_ids[k], name))
            return k;
        if (conf_key_info[k].save_keyword &&
            !strcmp(conf_key_info[k].save_keyword, name))
            return k;
    }
    return -1;
}

int sshlx_conf_key_protocol(void)
{
    return CONF_protocol;
}

static bool conf_has(Conf *conf, int key);

static bool scalar_of_type(int key, int type)
{
    return key >= 0 && key < N_CONFIG_OPTIONS &&
        conf_key_info[key].subkey_type == CONF_TYPE_NONE &&
        conf_key_info[key].value_type == type;
}

/* PuTTY's conf_get_*() assert that the key is present; some keys are only
 * populated in some configurations. Absent keys read as their default. */
bool sshlx_conf_get_bool(const sshlx_conf *c, int key)
{
    if (!scalar_of_type(key, CONF_TYPE_BOOL))
        return false;
    if (!conf_has(CONF_OF(c), key))
        return conf_key_info[key].default_value.bval;
    return conf_get_bool(CONF_OF(c), key);
}

int sshlx_conf_get_int(const sshlx_conf *c, int key)
{
    if (!scalar_of_type(key, CONF_TYPE_INT))
        return 0;
    if (!conf_has(CONF_OF(c), key))
        return conf_key_info[key].default_value.ival;
    return conf_get_int(CONF_OF(c), key);
}

bool sshlx_conf_has_key(const sshlx_conf *c, int key)
{
    return key >= 0 && key < N_CONFIG_OPTIONS && conf_has(CONF_OF(c), key);
}

const char *sshlx_conf_get_str(const sshlx_conf *c, int key)
{
    if (key < 0 || key >= N_CONFIG_OPTIONS ||
        conf_key_info[key].subkey_type != CONF_TYPE_NONE)
        return NULL;
    Conf *conf = CONF_OF(c);
    if (!conf_has(conf, key)) {
        const char *d = conf_key_info[key].default_value.sval;
        return (conf_key_info[key].value_type == CONF_TYPE_FILENAME ||
                conf_key_info[key].value_type == CONF_TYPE_FONT ||
                conf_key_info[key].load_custom || !d) ? "" : d;
    }
    switch (conf_key_info[key].value_type) {
      case CONF_TYPE_STR: return conf_get_str(conf, key);
      case CONF_TYPE_UTF8: return conf_get_utf8(conf, key);
      case CONF_TYPE_STR_AMBI: return conf_get_str_ambi(conf, key, NULL);
      case CONF_TYPE_FILENAME:
        return filename_to_str(conf_get_filename(conf, key));
      case CONF_TYPE_FONT: return conf_get_fontspec(conf, key)->name;
      default: return NULL;
    }
}

void sshlx_conf_set_bool(sshlx_conf *c, int key, bool v)
{
    if (scalar_of_type(key, CONF_TYPE_BOOL))
        conf_set_bool(CONF_OF(c), key, v);
}

void sshlx_conf_set_int(sshlx_conf *c, int key, int v)
{
    if (scalar_of_type(key, CONF_TYPE_INT))
        conf_set_int(CONF_OF(c), key, v);
}

void sshlx_conf_set_str(sshlx_conf *c, int key, const char *v)
{
    if (key < 0 || key >= N_CONFIG_OPTIONS || !v ||
        conf_key_info[key].subkey_type != CONF_TYPE_NONE)
        return;
    Conf *conf = CONF_OF(c);
    switch (conf_key_info[key].value_type) {
      case CONF_TYPE_STR: conf_set_str(conf, key, v); break;
      /* The UI deals in UTF-8, so ambiguous strings are set as UTF-8, as
       * PuTTY's config dialog does via dlg_editbox_get_utf8. */
      case CONF_TYPE_UTF8: case CONF_TYPE_STR_AMBI:
        conf_set_utf8(conf, key, v);
        break;
      case CONF_TYPE_FILENAME: {
        Filename *fn = filename_from_str(v);
        conf_set_filename(conf, key, fn);
        filename_free(fn);
        break;
      }
      case CONF_TYPE_FONT: {
        FontSpec *fs = fontspec_new(v);
        conf_set_fontspec(conf, key, fs);
        fontspec_free(fs);
        break;
      }
    }
}

static bool keyed_of(int key, int subtype, int valtype)
{
    return key >= 0 && key < N_CONFIG_OPTIONS &&
        conf_key_info[key].subkey_type == subtype &&
        conf_key_info[key].value_type == valtype;
}

const char *sshlx_conf_get_str_str(const sshlx_conf *c, int key,
                                   const char *sub)
{
    if (!keyed_of(key, CONF_TYPE_STR, CONF_TYPE_STR) || !sub)
        return NULL;
    return conf_get_str_str_opt(CONF_OF(c), key, sub);
}

void sshlx_conf_set_str_str(sshlx_conf *c, int key, const char *sub,
                            const char *v)
{
    if (keyed_of(key, CONF_TYPE_STR, CONF_TYPE_STR) && sub && v)
        conf_set_str_str(CONF_OF(c), key, sub, v);
}

void sshlx_conf_del_str_str(sshlx_conf *c, int key, const char *sub)
{
    if (keyed_of(key, CONF_TYPE_STR, CONF_TYPE_STR) && sub)
        conf_del_str_str(CONF_OF(c), key, sub);
}

void sshlx_conf_set_int_int(sshlx_conf *c, int key, int sub, int v)
{
    if (keyed_of(key, CONF_TYPE_INT, CONF_TYPE_INT))
        conf_set_int_int(CONF_OF(c), key, sub, v);
}

sshlx_strlist *sshlx_conf_str_subkeys(const sshlx_conf *c, int key)
{
    sshlx_strlist *l = snew(sshlx_strlist);
    l->n = 0;
    l->items = NULL;
    if (key < 0 || key >= N_CONFIG_OPTIONS ||
        conf_key_info[key].subkey_type != CONF_TYPE_STR)
        return l;
    size_t size = 0;
    char *sub;
    for (int i = 0; (sub = conf_get_str_nthstrkey(CONF_OF(c), key, i)); i++) {
        sgrowarray(l->items, size, l->n);
        l->items[l->n++] = dupstr(sub);
    }
    return l;
}

/* ---- per-key transfer via PuTTY's conf_serialise format ------------------- */

/* Skip (or copy) one serialised entry's subkey+value for key. Returns
 * false on a malformed source. */
static bool copy_entry(BinarySource *src, int key, BinarySink *out,
                       bool *int_sub_found, int want_sub, int *int_val)
{
    const ConfKeyInfo *ki = &conf_key_info[key];
    int sub = 0;
    switch (ki->subkey_type) {
      case CONF_TYPE_INT:
        sub = (int)get_uint32(src);
        if (out) put_uint32(out, (unsigned)sub);
        break;
      case CONF_TYPE_STR: {
        const char *s = get_asciz(src);
        if (out) put_asciz(out, s);
        break;
      }
    }
    switch (ki->value_type) {
      case CONF_TYPE_BOOL: {
        bool b = get_bool(src);
        if (out) put_bool(out, b);
        break;
      }
      case CONF_TYPE_INT: {
        int v = (int)get_uint32(src);
        if (out) put_uint32(out, (unsigned)v);
        if (int_sub_found && ki->subkey_type == CONF_TYPE_INT &&
            sub == want_sub) {
            *int_sub_found = true;
            *int_val = v;
        }
        break;
      }
      case CONF_TYPE_STR: case CONF_TYPE_UTF8: {
        const char *s = get_asciz(src);
        if (out) put_asciz(out, s);
        break;
      }
      case CONF_TYPE_STR_AMBI: {
        const char *s = get_asciz(src);
        bool u = get_bool(src);
        if (out) { put_asciz(out, s); put_bool(out, u); }
        break;
      }
      case CONF_TYPE_FILENAME: {
        Filename *fn = filename_deserialise(src);
        if (out) filename_serialise(out, fn);
        filename_free(fn);
        break;
      }
      case CONF_TYPE_FONT: {
        FontSpec *fs = fontspec_deserialise(src);
        if (out) fontspec_serialise(out, fs);
        fontspec_free(fs);
        break;
      }
    }
    return !get_err(src);
}

/* Extract the entries for one key, without their key index. */
static strbuf *extract_key(Conf *conf, int key, bool *found, int want_sub,
                           int *int_val)
{
    strbuf *all = strbuf_new();
    conf_serialise(BinarySink_UPCAST(all), conf);
    strbuf *out = strbuf_new();
    BinarySource src[1];
    BinarySource_BARE_INIT(src, all->u, all->len);
    while (true) {
        unsigned k = get_uint32(src);
        if (get_err(src) || k == 0xFFFFFFFFU || k >= N_CONFIG_OPTIONS)
            break;
        bool mine = ((int)k == key);
        if (!copy_entry(src, (int)k, mine ? BinarySink_UPCAST(out) : NULL,
                        mine ? found : NULL, want_sub, int_val))
            break;
    }
    strbuf_free(all);
    return out;
}

static bool conf_has(Conf *conf, int key)
{
    bool found = false;
    int dummy;
    strbuf *sb = extract_key(conf, key, NULL, 0, &dummy);
    found = sb->len > 0;
    strbuf_free(sb);
    return found;
}

int sshlx_conf_get_int_int(const sshlx_conf *c, int key, int sub, int dflt)
{
    if (!keyed_of(key, CONF_TYPE_INT, CONF_TYPE_INT))
        return dflt;
    bool found = false;
    int val = dflt;
    strbuf *sb = extract_key(CONF_OF(c), key, &found, sub, &val);
    strbuf_free(sb);
    return found ? val : dflt;
}

char *sshlx_conf_key_export(const sshlx_conf *c, int key)
{
    if (key < 0 || key >= N_CONFIG_OPTIONS)
        return NULL;
    strbuf *sb = extract_key(CONF_OF(c), key, NULL, 0, NULL);
    char *hex = snewn(sb->len * 2 + 2, char);
    hex[0] = 'v';   /* format version marker */
    for (size_t i = 0; i < sb->len; i++)
        sprintf(hex + 1 + 2 * i, "%02x", sb->u[i]);
    hex[1 + 2 * sb->len] = '\0';
    strbuf_free(sb);
    return hex;
}

bool sshlx_conf_key_import(sshlx_conf *c, int key, const char *value)
{
    if (key < 0 || key >= N_CONFIG_OPTIONS || !value || value[0] != 'v')
        return false;
    value++;
    size_t hexlen = strlen(value);
    if (hexlen % 2)
        return false;
    strbuf *raw = strbuf_new();
    for (size_t i = 0; i < hexlen; i += 2) {
        unsigned b;
        if (sscanf(value + i, "%2x", &b) != 1) {
            strbuf_free(raw);
            return false;
        }
        put_byte(raw, (unsigned char)b);
    }

    /* Re-insert the key index before each entry, validating as we go. */
    strbuf *blob = strbuf_new();
    BinarySource src[1];
    BinarySource_BARE_INIT(src, raw->u, raw->len);
    bool ok = true;
    while (get_avail(src) > 0) {
        put_uint32(blob, (unsigned)key);
        if (!copy_entry(src, key, BinarySink_UPCAST(blob), NULL, 0, NULL)) {
            ok = false;
            break;
        }
    }
    put_uint32(blob, 0xFFFFFFFFU);
    strbuf_free(raw);
    if (!ok) {
        strbuf_free(blob);
        return false;
    }

    Conf *conf = CONF_OF(c);
    Conf *tmp = conf_copy(conf);
    /* Keyed string maps (port forwards, environment, ...) are replaced
     * as a whole, so drop the existing entries first. */
    if (conf_key_info[key].subkey_type == CONF_TYPE_STR) {
        char *sub;
        while ((sub = conf_get_str_nthstrkey(tmp, key, 0)) != NULL) {
            char *copy = dupstr(sub);
            conf_del_str_str(tmp, key, copy);
            sfree(copy);
        }
    }
    BinarySource bsrc[1];
    BinarySource_BARE_INIT(bsrc, blob->u, blob->len);
    ok = conf_deserialise(tmp, bsrc);
    strbuf_free(blob);
    if (ok)
        conf_copy_into(conf, tmp);
    conf_free(tmp);
    return ok;
}

void sshlx_conf_key_copy(sshlx_conf *dst, const sshlx_conf *src, int key)
{
    char *v = sshlx_conf_key_export(src, key);
    if (v) {
        sshlx_conf_key_import(dst, key, v);
        sfree(v);
    }
}

bool sshlx_conf_key_equal(const sshlx_conf *a, const sshlx_conf *b, int key)
{
    char *va = sshlx_conf_key_export(a, key);
    char *vb = sshlx_conf_key_export(b, key);
    bool eq = va && vb && !strcmp(va, vb);
    sfree(va);
    sfree(vb);
    return eq;
}

/* ---- protocols ------------------------------------------------------------- */
static sshlx_protocol *protocols;
static size_t n_protocols;

static void build_protocols(void)
{
    if (protocols)
        return;
    size_t n = 0;
    while (backends[n])
        n++;
    protocols = snewn(n ? n : 1, sshlx_protocol);
    for (size_t i = 0; i < n; i++) {
        protocols[i].id = backends[i]->protocol;
        protocols[i].name = backends[i]->id;
        protocols[i].display_name = backends[i]->displayname_tc;
        protocols[i].default_port = backends[i]->default_port;
    }
    n_protocols = n;
}

size_t sshlx_protocol_count(void)
{
    build_protocols();
    return n_protocols;
}

const sshlx_protocol *sshlx_protocol_get(size_t i)
{
    build_protocols();
    return i < n_protocols ? &protocols[i] : NULL;
}
