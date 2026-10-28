/*
 * fe_dlg.c - sshLX's implementation of PuTTY's toolkit-neutral dialog API
 * (dialog.h: the dlg_* functions), replacing unix/dialog.c.
 *
 * PuTTY's config.c (plus unix/config-unix.c and unix/config-gtk.c, all
 * compiled unchanged) describes the complete configuration box as a
 * controlbox. We keep the state of every control in a C-side model,
 * PuTTY's handlers read and write it through dlg_*, and the Qt UI renders
 * the model generically and reports edits back. That way every PuTTY
 * option is reachable and PuTTY's own handlers perform every Conf change.
 */
#include <stdlib.h>
#include <string.h>

#include "engine_internal.h"

typedef struct uctrl {
    dlgcontrol *ctrl;
    int id;
    int set_index;
    char *text;               /* editbox / file / font / static text */
    int ival;                 /* radio index, checkbox state */
    char **items;             /* listbox, droplist, combo list */
    int *item_ids;
    bool *item_sel;
    size_t nitems, itemsize;
    /* colour selector */
    bool colour_ok;
    int colour_r, colour_g, colour_b;
} uctrl;

struct dlgparam {
    sshlx_dlg_callbacks cb;
    struct controlbox *box;
    void *data;               /* Conf for the config box; NULL for CA box */
    uctrl *uctrls;
    size_t nuctrls;
    sshlx_ctrl_set *sets;
    sshlx_ctrl_desc **set_descs;
    size_t nsets;
    dlgcontrol *currfocus, *lastfocus;
    char *visible_path;
    int retval;
    int handler_depth;
};

/* The public handle is the dlgparam itself. */
#define DP_OF(d)  ((struct dlgparam *)(d))
#define DLG_OF(dp) ((sshlx_dlg *)(dp))

static uctrl *find_byctrl(dlgparam *dp, dlgcontrol *ctrl)
{
    for (size_t i = 0; i < dp->nuctrls; i++)
        if (dp->uctrls[i].ctrl == ctrl)
            return &dp->uctrls[i];
    return NULL;
}

static uctrl *find_byid(dlgparam *dp, int id)
{
    if (id < 0 || (size_t)id >= dp->nuctrls)
        return NULL;
    return &dp->uctrls[id];
}

static void notify(dlgparam *dp, uctrl *uc)
{
    if (dp->cb.ctrl_changed && uc)
        dp->cb.ctrl_changed(dp->cb.ctx, uc->id);
}

static void run_handler(dlgparam *dp, uctrl *uc, int event)
{
    if (!uc || !uc->ctrl->handler)
        return;
    dp->handler_depth++;
    uc->ctrl->handler(uc->ctrl, dp, dp->data, event);
    dp->handler_depth--;
}

/* ---- dlg_* API implementations ------------------------------------------ */
dlgcontrol *dlg_last_focused(dlgcontrol *ctrl, dlgparam *dp)
{
    if (dp->currfocus != ctrl)
        return dp->currfocus;
    else
        return dp->lastfocus;
}

void dlg_radiobutton_set(dlgcontrol *ctrl, dlgparam *dp, int which)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && ctrl->type == CTRL_RADIO);
    uc->ival = which;
    notify(dp, uc);
}

int dlg_radiobutton_get(dlgcontrol *ctrl, dlgparam *dp)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && ctrl->type == CTRL_RADIO);
    return uc->ival;
}

void dlg_checkbox_set(dlgcontrol *ctrl, dlgparam *dp, bool checked)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && ctrl->type == CTRL_CHECKBOX);
    uc->ival = checked;
    notify(dp, uc);
}

bool dlg_checkbox_get(dlgcontrol *ctrl, dlgparam *dp)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && ctrl->type == CTRL_CHECKBOX);
    return uc->ival != 0;
}

void dlg_editbox_set_utf8(dlgcontrol *ctrl, dlgparam *dp, char const *text)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && ctrl->type == CTRL_EDITBOX);
    sfree(uc->text);
    uc->text = dupstr(text ? text : "");
    notify(dp, uc);
}

void dlg_editbox_set(dlgcontrol *ctrl, dlgparam *dp, char const *text)
{
    /* As GTK: edit boxes are always UTF-8. */
    dlg_editbox_set_utf8(ctrl, dp, text);
}

char *dlg_editbox_get_utf8(dlgcontrol *ctrl, dlgparam *dp)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && ctrl->type == CTRL_EDITBOX);
    return dupstr(uc->text ? uc->text : "");
}

char *dlg_editbox_get(dlgcontrol *ctrl, dlgparam *dp)
{
    return dlg_editbox_get_utf8(ctrl, dp);
}

void dlg_editbox_select_range(dlgcontrol *ctrl, dlgparam *dp,
                              size_t start, size_t len)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    if (uc && dp->cb.select_range)
        dp->cb.select_range(dp->cb.ctx, uc->id, start, len);
}

static bool has_list(dlgcontrol *ctrl)
{
    return ctrl->type == CTRL_LISTBOX ||
        (ctrl->type == CTRL_EDITBOX && ctrl->editbox.has_list);
}

void dlg_listbox_clear(dlgcontrol *ctrl, dlgparam *dp)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && has_list(ctrl));
    for (size_t i = 0; i < uc->nitems; i++)
        sfree(uc->items[i]);
    uc->nitems = 0;
    notify(dp, uc);
}

void dlg_listbox_del(dlgcontrol *ctrl, dlgparam *dp, int index)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && has_list(ctrl));
    if (index < 0 || (size_t)index >= uc->nitems)
        return;
    sfree(uc->items[index]);
    size_t tail = uc->nitems - index - 1;
    memmove(uc->items + index, uc->items + index + 1, tail * sizeof(char *));
    memmove(uc->item_ids + index, uc->item_ids + index + 1, tail * sizeof(int));
    memmove(uc->item_sel + index, uc->item_sel + index + 1, tail * sizeof(bool));
    uc->nitems--;
    notify(dp, uc);
}

void dlg_listbox_addwithid(dlgcontrol *ctrl, dlgparam *dp,
                           char const *text, int id)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && has_list(ctrl));
    if (uc->nitems >= uc->itemsize) {
        size_t old = uc->itemsize;
        sgrowarray(uc->items, uc->itemsize, uc->nitems);
        uc->item_ids = sresize(uc->item_ids, uc->itemsize, int);
        uc->item_sel = sresize(uc->item_sel, uc->itemsize, bool);
        (void)old;
    }
    uc->items[uc->nitems] = dupstr(text);
    uc->item_ids[uc->nitems] = id;
    uc->item_sel[uc->nitems] = false;
    uc->nitems++;
    notify(dp, uc);
}

void dlg_listbox_add(dlgcontrol *ctrl, dlgparam *dp, char const *text)
{
    dlg_listbox_addwithid(ctrl, dp, text, 0);
}

int dlg_listbox_getid(dlgcontrol *ctrl, dlgparam *dp, int index)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && has_list(ctrl));
    if (index < 0 || (size_t)index >= uc->nitems)
        return -1;
    return uc->item_ids[index];
}

/* Returns the single selected item, or -1 if none or several. */
int dlg_listbox_index(dlgcontrol *ctrl, dlgparam *dp)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && has_list(ctrl));
    int found = -1;
    for (size_t i = 0; i < uc->nitems; i++) {
        if (uc->item_sel[i]) {
            if (found >= 0)
                return -1;
            found = (int)i;
        }
    }
    return found;
}

bool dlg_listbox_issel(dlgcontrol *ctrl, dlgparam *dp, int index)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && has_list(ctrl));
    if (index < 0 || (size_t)index >= uc->nitems)
        return false;
    return uc->item_sel[index];
}

static bool is_multisel(dlgcontrol *ctrl)
{
    return ctrl->type == CTRL_LISTBOX && ctrl->listbox.multisel &&
        ctrl->listbox.height != 0;
}

void dlg_listbox_select(dlgcontrol *ctrl, dlgparam *dp, int index)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && has_list(ctrl));
    if (!is_multisel(ctrl))
        for (size_t i = 0; i < uc->nitems; i++)
            uc->item_sel[i] = false;
    if (index >= 0 && (size_t)index < uc->nitems)
        uc->item_sel[index] = true;
    notify(dp, uc);
}

void dlg_text_set(dlgcontrol *ctrl, dlgparam *dp, char const *text)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && ctrl->type == CTRL_TEXT);
    sfree(uc->text);
    uc->text = dupstr(text);
    notify(dp, uc);
}

void dlg_label_change(dlgcontrol *ctrl, dlgparam *dp, char const *text)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    if (uc && dp->cb.label_changed)
        dp->cb.label_changed(dp->cb.ctx, uc->id, text);
}

void dlg_filesel_set(dlgcontrol *ctrl, dlgparam *dp, Filename *fn)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && ctrl->type == CTRL_FILESELECT);
    sfree(uc->text);
    uc->text = dupstr(filename_to_str(fn));
    notify(dp, uc);
}

Filename *dlg_filesel_get(dlgcontrol *ctrl, dlgparam *dp)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && ctrl->type == CTRL_FILESELECT);
    return filename_from_str(uc->text ? uc->text : "");
}

void dlg_fontsel_set(dlgcontrol *ctrl, dlgparam *dp, FontSpec *fs)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && ctrl->type == CTRL_FONTSELECT);
    sfree(uc->text);
    uc->text = dupstr(fs->name);
    notify(dp, uc);
}

FontSpec *dlg_fontsel_get(dlgcontrol *ctrl, dlgparam *dp)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    assert(uc && ctrl->type == CTRL_FONTSELECT);
    return fontspec_new(uc->text ? uc->text : "");
}

void dlg_update_start(dlgcontrol *ctrl, dlgparam *dp)
{
    (void)ctrl;
    (void)dp;
}

void dlg_update_done(dlgcontrol *ctrl, dlgparam *dp)
{
    (void)ctrl;
    (void)dp;
}

void dlg_set_focus(dlgcontrol *ctrl, dlgparam *dp)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    if (uc && dp->cb.set_focus)
        dp->cb.set_focus(dp->cb.ctx, uc->id);
}

bool dlg_is_visible(dlgcontrol *ctrl, dlgparam *dp)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    if (!uc)
        return false;
    const char *path = dp->sets[uc->set_index].path;
    if (!*path)
        return true;   /* global buttons are always visible */
    return dp->visible_path && !strcmp(dp->visible_path, path);
}

void dlg_beep(dlgparam *dp)
{
    if (dp->cb.beep)
        dp->cb.beep(dp->cb.ctx);
}

void dlg_error_msg(dlgparam *dp, const char *msg)
{
    if (dp->cb.error)
        dp->cb.error(dp->cb.ctx, msg);
}

void dlg_end(dlgparam *dp, int value)
{
    dp->retval = value;
    if (dp->cb.end)
        dp->cb.end(dp->cb.ctx, value);
}

void dlg_refresh(dlgcontrol *ctrl, dlgparam *dp)
{
    if (ctrl) {
        if (ctrl->handler != NULL)
            ctrl->handler(ctrl, dp, dp->data, EVENT_REFRESH);
    } else {
        for (size_t i = 0; i < dp->nuctrls; i++) {
            uctrl *uc = &dp->uctrls[i];
            if (uc->ctrl->handler != NULL)
                uc->ctrl->handler(uc->ctrl, dp, dp->data, EVENT_REFRESH);
        }
    }
}

void dlg_coloursel_start(dlgcontrol *ctrl, dlgparam *dp, int r, int g, int b)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    if (uc && dp->cb.colour_select)
        dp->cb.colour_select(dp->cb.ctx, uc->id, r, g, b);
}

bool dlg_coloursel_results(dlgcontrol *ctrl, dlgparam *dp,
                           int *r, int *g, int *b)
{
    uctrl *uc = find_byctrl(dp, ctrl);
    if (uc && uc->colour_ok) {
        *r = uc->colour_r;
        *g = uc->colour_g;
        *b = uc->colour_b;
        return true;
    }
    return false;
}

/* "Manage host CAs" button in config.c */
void show_ca_config_box(dlgparam *dp)
{
    if (dp && dp->cb.open_ca_config)
        dp->cb.open_ca_config(dp->cb.ctx);
}

/* "About" button added by unix/config-gtk.c: its context is the 'win'
 * pointer we pass to gtk_setup_config_box(), i.e. our dlgparam. */
void about_box(void *window)
{
    dlgparam *dp = (dlgparam *)window;
    if (dp && dp->cb.about)
        dp->cb.about(dp->cb.ctx);
    else
        sshlx_global_event_fire(SSHLX_GLOBAL_ABOUT_REQUESTED, NULL);
}

/* ---- building the model ------------------------------------------------------ */
static sshlx_ctrl_type map_ctrl_type(int t)
{
    switch (t) {
      case CTRL_TEXT: return SSHLX_C_TEXT;
      case CTRL_EDITBOX: return SSHLX_C_EDITBOX;
      case CTRL_RADIO: return SSHLX_C_RADIO;
      case CTRL_CHECKBOX: return SSHLX_C_CHECKBOX;
      case CTRL_BUTTON: return SSHLX_C_BUTTON;
      case CTRL_LISTBOX: return SSHLX_C_LISTBOX;
      case CTRL_COLUMNS: return SSHLX_C_COLUMNS;
      case CTRL_FILESELECT: return SSHLX_C_FILESELECT;
      case CTRL_FONTSELECT: return SSHLX_C_FONTSELECT;
      default: return SSHLX_C_TEXT;
    }
}

static void build_model(dlgparam *dp)
{
    struct controlbox *b = dp->box;
    size_t total = 0;
    for (size_t i = 0; i < b->nctrlsets; i++)
        total += b->ctrlsets[i]->ncontrols;

    dp->uctrls = snewn(total ? total : 1, uctrl);
    memset(dp->uctrls, 0, (total ? total : 1) * sizeof(uctrl));
    dp->nsets = b->nctrlsets;
    dp->sets = snewn(dp->nsets ? dp->nsets : 1, sshlx_ctrl_set);
    dp->set_descs = snewn(dp->nsets ? dp->nsets : 1, sshlx_ctrl_desc *);

    /* First pass: assign ids so align_next_to can be resolved. */
    size_t n = 0;
    for (size_t i = 0; i < b->nctrlsets; i++) {
        struct controlset *s = b->ctrlsets[i];
        for (size_t j = 0; j < s->ncontrols; j++) {
            if (s->ctrls[j]->type == CTRL_TABDELAY)
                continue;
            uctrl *uc = &dp->uctrls[n];
            uc->ctrl = s->ctrls[j];
            uc->id = (int)n;
            uc->set_index = (int)i;
            if (uc->ctrl->type == CTRL_TEXT)
                uc->text = dupstr(uc->ctrl->label);
            n++;
        }
    }
    dp->nuctrls = n;

    for (size_t i = 0; i < b->nctrlsets; i++) {
        struct controlset *s = b->ctrlsets[i];
        sshlx_ctrl_set *set = &dp->sets[i];
        bool is_title = (s->boxname == NULL && s->ncolumns == 0 &&
                         s->ncontrols == 0);
        set->path = s->pathname;
        set->title = is_title ? NULL : s->boxtitle;
        set->panel_title = is_title ? s->boxtitle : NULL;
        sshlx_ctrl_desc *descs = snewn(s->ncontrols ? s->ncontrols : 1,
                                       sshlx_ctrl_desc);
        size_t nd = 0;
        for (size_t j = 0; j < s->ncontrols; j++) {
            dlgcontrol *c = s->ctrls[j];
            if (c->type == CTRL_TABDELAY)
                continue;
            uctrl *uc = find_byctrl(dp, c);
            sshlx_ctrl_desc *d = &descs[nd++];
            memset(d, 0, sizeof(*d));
            d->id = uc->id;
            d->type = map_ctrl_type(c->type);
            d->label = c->label;
            d->column = COLUMN_START(c->column);
            d->column_span = COLUMN_SPAN(c->column);
            d->align_next_to = -1;
            if (c->align_next_to) {
                uctrl *t = find_byctrl(dp, c->align_next_to);
                if (t)
                    d->align_next_to = t->id;
            }
            switch (c->type) {
              case CTRL_EDITBOX:
                d->shortcut = c->editbox.shortcut;
                d->password = c->editbox.password;
                d->has_list = c->editbox.has_list;
                d->percent_width = c->editbox.percentwidth;
                break;
              case CTRL_RADIO:
                d->shortcut = c->radio.shortcut;
                d->radio_ncolumns = c->radio.ncolumns;
                d->nbuttons = c->radio.nbuttons;
                d->buttons = (const char *const *)c->radio.buttons;
                break;
              case CTRL_CHECKBOX:
                d->shortcut = c->checkbox.shortcut;
                break;
              case CTRL_BUTTON:
                d->shortcut = c->button.shortcut;
                d->is_default = c->button.isdefault;
                d->is_cancel = c->button.iscancel;
                break;
              case CTRL_LISTBOX:
                d->shortcut = c->listbox.shortcut;
                d->list_height = c->listbox.height;
                d->draglist = c->listbox.draglist;
                d->multisel = c->listbox.multisel;
                d->percent_width = c->listbox.percentwidth;
                d->list_ncols = c->listbox.ncols;
                d->list_percents = c->listbox.percentages;
                break;
              case CTRL_COLUMNS:
                d->ncolumns = c->columns.ncols;
                d->column_percents = c->columns.percentages;
                break;
              case CTRL_FILESELECT:
                d->shortcut = c->fileselect.shortcut;
                d->for_writing = c->fileselect.for_writing;
                d->file_title = c->fileselect.title;
                d->file_filter = (int)c->fileselect.filter;
                break;
              case CTRL_FONTSELECT:
                d->shortcut = c->fontselect.shortcut;
                break;
              case CTRL_TEXT:
                d->wrap = c->text.wrap;
                break;
            }
        }
        set->n_ctrls = nd;
        set->ctrls = descs;
        dp->set_descs[i] = descs;
    }
}

static dlgparam *dlg_new(const sshlx_dlg_callbacks *cb)
{
    dlgparam *dp = snew(dlgparam);
    memset(dp, 0, sizeof(*dp));
    if (cb)
        dp->cb = *cb;
    dp->retval = -1;
    dp->box = ctrl_new_box();
    return dp;
}

sshlx_dlg *sshlx_dlg_new_config(sshlx_conf *conf, bool midsession,
                                int protcfginfo,
                                const sshlx_dlg_callbacks *cb)
{
    dlgparam *dp = dlg_new(cb);
    Conf *c = CONF_OF(conf);
    int protocol = conf_get_int(c, CONF_protocol);
    /* Exactly the sequence of unix/dialog.c create_config_box(). */
    setup_config_box(dp->box, midsession, protocol, protcfginfo);
    unix_setup_config_box(dp->box, midsession, protocol);
    gtk_setup_config_box(dp->box, midsession, dp);
    dp->data = c;
    build_model(dp);
    return DLG_OF(dp);
}

sshlx_dlg *sshlx_dlg_new_ca_config(const sshlx_dlg_callbacks *cb)
{
    dlgparam *dp = dlg_new(cb);
    setup_ca_config_box(dp->box);
    dp->data = NULL;   /* ca-config.c's handlers keep their own state */
    build_model(dp);
    return DLG_OF(dp);
}

void sshlx_dlg_refresh_all(sshlx_dlg *d)
{
    dlgparam *dp = DP_OF(d);
    dlg_refresh(NULL, dp);
    /* As create_config_box(): focus goes to the first focusable control
     * on a real panel. */
    for (size_t i = 0; i < dp->nuctrls; i++) {
        uctrl *uc = &dp->uctrls[i];
        int t = uc->ctrl->type;
        if (*dp->sets[uc->set_index].path &&
            t != CTRL_COLUMNS && t != CTRL_TEXT) {
            dp->lastfocus = uc->ctrl;
            break;
        }
    }
}

void sshlx_dlg_free(sshlx_dlg *d)
{
    dlgparam *dp = DP_OF(d);
    if (!dp)
        return;
    for (size_t i = 0; i < dp->nuctrls; i++) {
        uctrl *uc = &dp->uctrls[i];
        sfree(uc->text);
        for (size_t j = 0; j < uc->nitems; j++)
            sfree(uc->items[j]);
        sfree(uc->items);
        sfree(uc->item_ids);
        sfree(uc->item_sel);
    }
    sfree(dp->uctrls);
    for (size_t i = 0; i < dp->nsets; i++)
        sfree(dp->set_descs[i]);
    sfree(dp->set_descs);
    sfree(dp->sets);
    sfree(dp->visible_path);
    ctrl_free_box(dp->box);
    sfree(dp);
}

size_t sshlx_dlg_sets(const sshlx_dlg *d, const sshlx_ctrl_set **out)
{
    const dlgparam *dp = (const dlgparam *)d;
    *out = dp->sets;
    return dp->nsets;
}

const char *sshlx_dlg_get_text(const sshlx_dlg *d, int id)
{
    uctrl *uc = find_byid(DP_OF(d), id);
    return uc && uc->text ? uc->text : "";
}

int sshlx_dlg_get_int(const sshlx_dlg *d, int id)
{
    uctrl *uc = find_byid(DP_OF(d), id);
    return uc ? uc->ival : 0;
}

size_t sshlx_dlg_list_count(const sshlx_dlg *d, int id)
{
    uctrl *uc = find_byid(DP_OF(d), id);
    return uc ? uc->nitems : 0;
}

const char *sshlx_dlg_list_item(const sshlx_dlg *d, int id, size_t i)
{
    uctrl *uc = find_byid(DP_OF(d), id);
    return (uc && i < uc->nitems) ? uc->items[i] : "";
}

bool sshlx_dlg_list_selected(const sshlx_dlg *d, int id, size_t i)
{
    uctrl *uc = find_byid(DP_OF(d), id);
    return uc && i < uc->nitems && uc->item_sel[i];
}

/* ---- UI-originated events ------------------------------------------------------ */
void sshlx_dlg_set_text(sshlx_dlg *d, int id, const char *utf8)
{
    dlgparam *dp = DP_OF(d);
    uctrl *uc = find_byid(dp, id);
    if (!uc)
        return;
    int t = uc->ctrl->type;
    if (t != CTRL_EDITBOX && t != CTRL_FILESELECT && t != CTRL_FONTSELECT)
        return;
    if (uc->text && !strcmp(uc->text, utf8 ? utf8 : ""))
        return;
    sfree(uc->text);
    uc->text = dupstr(utf8 ? utf8 : "");
    run_handler(dp, uc, EVENT_VALCHANGE);
}

void sshlx_dlg_set_int(sshlx_dlg *d, int id, int value)
{
    dlgparam *dp = DP_OF(d);
    uctrl *uc = find_byid(dp, id);
    if (!uc)
        return;
    int t = uc->ctrl->type;
    if (t != CTRL_RADIO && t != CTRL_CHECKBOX)
        return;
    uc->ival = value;
    run_handler(dp, uc, EVENT_VALCHANGE);
}

void sshlx_dlg_list_select(sshlx_dlg *d, int id, size_t i, bool sel,
                           bool exclusive)
{
    dlgparam *dp = DP_OF(d);
    uctrl *uc = find_byid(dp, id);
    if (!uc || i >= uc->nitems)
        return;
    if (uc->ctrl->type == CTRL_EDITBOX) {
        /* Picking from a combo box's list changes the edit text, as a
         * GtkComboBoxEntry does. */
        sfree(uc->text);
        uc->text = dupstr(uc->items[i]);
        run_handler(dp, uc, EVENT_VALCHANGE);
        return;
    }
    if (exclusive || !is_multisel(uc->ctrl))
        for (size_t j = 0; j < uc->nitems; j++)
            uc->item_sel[j] = false;
    uc->item_sel[i] = sel;
    run_handler(dp, uc, EVENT_SELCHANGE);
}

void sshlx_dlg_activate(sshlx_dlg *d, int id)
{
    dlgparam *dp = DP_OF(d);
    uctrl *uc = find_byid(dp, id);
    if (!uc)
        return;
    if (uc->ctrl->type == CTRL_BUTTON || uc->ctrl->type == CTRL_LISTBOX)
        run_handler(dp, uc, EVENT_ACTION);
}

/* unix/dialog.c draglist_move() */
void sshlx_dlg_list_move(sshlx_dlg *d, int id, size_t i, int direction)
{
    dlgparam *dp = DP_OF(d);
    uctrl *uc = find_byid(dp, id);
    if (!uc || uc->ctrl->type != CTRL_LISTBOX)
        return;
    long j = (long)i + direction;
    if (j < 0 || (size_t)j >= uc->nitems || i >= uc->nitems)
        return;
    char *ts = uc->items[i]; uc->items[i] = uc->items[j]; uc->items[j] = ts;
    int ti = uc->item_ids[i]; uc->item_ids[i] = uc->item_ids[j];
    uc->item_ids[j] = ti;
    bool tb = uc->item_sel[i]; uc->item_sel[i] = uc->item_sel[j];
    uc->item_sel[j] = tb;
    notify(dp, uc);
    run_handler(dp, uc, EVENT_VALCHANGE);
}

void sshlx_dlg_colour_result(sshlx_dlg *d, int id, bool ok,
                             int r, int g, int b)
{
    dlgparam *dp = DP_OF(d);
    uctrl *uc = find_byid(dp, id);
    if (!uc)
        return;
    uc->colour_ok = ok;
    uc->colour_r = r;
    uc->colour_g = g;
    uc->colour_b = b;
    run_handler(dp, uc, EVENT_CALLBACK);
    uc->colour_ok = false;
}

void sshlx_dlg_focus_changed(sshlx_dlg *d, int id)
{
    dlgparam *dp = DP_OF(d);
    uctrl *uc = find_byid(dp, id);
    if (!uc)
        return;
    if (dp->currfocus != uc->ctrl) {
        dp->lastfocus = dp->currfocus;
        dp->currfocus = uc->ctrl;
    }
}

void sshlx_dlg_set_visible_path(sshlx_dlg *d, const char *path)
{
    dlgparam *dp = DP_OF(d);
    sfree(dp->visible_path);
    dp->visible_path = path ? dupstr(path) : NULL;
}
