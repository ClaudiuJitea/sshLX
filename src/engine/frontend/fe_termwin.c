/*
 * fe_termwin.c - sshLX's TermWin implementation.
 *
 * The colour, reverse-video, bold, dim, cursor and double-width/height
 * geometry rules are ported from PuTTY's unix/window.c
 * (do_text_internal, gtkwin_draw_text, gtkwin_draw_cursor) so that the
 * UI receives fully resolved draw operations and never needs PuTTY's
 * attribute bit layout. Glyph rendering itself happens in the UI.
 */
#include <stdlib.h>
#include <string.h>

#include "engine_internal.h"

#define SESSION_OF(tw) container_of(tw, sshlx_session, termwin)

static bool sshlx_tw_setup_draw_ctx(TermWin *tw)
{
    sshlx_session *s = SESSION_OF(tw);
    if (s->dead || !s->cb.begin_draw)
        return false;
    s->depth++;
    bool ok = s->cb.begin_draw(s->cb.ctx);
    s->depth--;
    s->draw_active = ok;
    return ok;
}

static void sshlx_tw_free_draw_ctx(TermWin *tw)
{
    sshlx_session *s = SESSION_OF(tw);
    if (s->draw_active)
        UI_CALL(s, end_draw);
    s->draw_active = false;
}

static sshlx_rgb rgb_from_palette(sshlx_session *s, int idx, bool dim)
{
    sshlx_rgb out;
    const rgb *c = &s->palette[idx];
    /* cairo_set_source_rgb_dim(): dimmed colours are 2/3 brightness */
    out.r = dim ? c->r * 2 / 3 : c->r;
    out.g = dim ? c->g * 2 / 3 : c->g;
    out.b = dim ? c->b * 2 / 3 : c->b;
    return out;
}

static sshlx_rgb rgb_from_optional(optionalrgb o, bool dim)
{
    sshlx_rgb out;
    out.r = dim ? o.r * 2 / 3 : o.r;
    out.g = dim ? o.g * 2 / 3 : o.g;
    out.b = dim ? o.b * 2 / 3 : o.b;
    return out;
}

/*
 * Geometry for a text run, following do_text_internal(): returns false if
 * nothing is visible. *col is updated for double-width lines and *ncells
 * receives the run width in cells.
 */
static bool run_geometry(sshlx_session *s, int *col, int *len,
                         unsigned long attr, int lattr, int *ncells)
{
    int widefactor = (attr & ATTR_WIDE) ? 2 : 1;
    int rlen;
    if ((lattr & LATTR_MODE) != LATTR_NORM) {
        *col *= 2;
        if (*col >= s->term->cols)
            return false;
        if (*col + *len * 2 * widefactor > s->term->cols) {
            *len = (s->term->cols - *col) / 2 / widefactor;
            if (*len == 0)
                return false;
        }
        rlen = *len * 2;
    } else {
        rlen = *len;
    }
    *ncells = rlen * widefactor;
    return true;
}

static int line_attr_of(int lattr)
{
    switch (lattr & LATTR_MODE) {
      case LATTR_WIDE: return SSHLX_LINE_WIDE;
      case LATTR_TOP: return SSHLX_LINE_TOP;
      case LATTR_BOT: return SSHLX_LINE_BOTTOM;
      default: return SSHLX_LINE_NORMAL;
    }
}

static void do_text_internal(sshlx_session *s, int x, int y,
                             wchar_t *text, int len, unsigned long attr,
                             int lattr, truecolour tc)
{
    int ncombining, nfg, nbg, t, ncells;

    if (attr & TATTR_COMBINING) {
        ncombining = len;
        len = 1;
    } else {
        ncombining = 1;
    }

    nfg = ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT);
    nbg = ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT);
    if (attr & ATTR_REVERSE) {
        optionalrgb trgb;
        t = nfg; nfg = nbg; nbg = t;
        trgb = tc.fg; tc.fg = tc.bg; tc.bg = trgb;
    }
    if ((s->bold_style & BOLD_STYLE_COLOUR) && (attr & ATTR_BOLD)) {
        if (nfg < 16) nfg |= 8;
        else if (nfg >= 256) nfg |= 1;
    }
    if ((s->bold_style & BOLD_STYLE_COLOUR) && (attr & ATTR_BLINK)) {
        if (nbg < 16) nbg |= 8;
        else if (nbg >= 256) nbg |= 1;
    }
    if (attr & ATTR_ACTCURS) {
        tc.fg = tc.bg = optionalrgb_none;
        nfg = 260;
        nbg = 261;
        attr &= ~ATTR_DIM;             /* don't dim the cursor */
    }

    int col = x;
    if (!run_geometry(s, &col, &len, attr, lattr, &ncells))
        return;

    bool dim = (attr & ATTR_DIM) != 0;
    sshlx_text_style st;
    memset(&st, 0, sizeof(st));
    st.bg = tc.bg.enabled ? rgb_from_optional(tc.bg, dim)
                          : rgb_from_palette(s, nbg, dim);
    st.fg = tc.fg.enabled ? rgb_from_optional(tc.fg, dim)
                          : rgb_from_palette(s, nfg, dim);
    if ((attr & ATTR_BOLD) && (s->bold_style & BOLD_STYLE_FONT)) {
        st.flags |= SSHLX_A_BOLD_FONT;
        if (s->shadowbold)
            st.flags |= SSHLX_A_SHADOW_BOLD;
    }
    if (attr & ATTR_UNDER) st.flags |= SSHLX_A_UNDER;
    if (attr & ATTR_STRIKE) st.flags |= SSHLX_A_STRIKE;
    if (attr & ATTR_WIDE) st.flags |= SSHLX_A_WIDE;
    if (ncombining > 1) st.flags |= SSHLX_A_COMBINING;
    if (dim) st.flags |= SSHLX_A_DIM;
    if (attr & ATTR_BLINK) st.flags |= SSHLX_A_BLINK;
    st.line_attr = line_attr_of(lattr);
    st.shadow_bold_offset = s->shadowboldoffset;

    int ntext = (ncombining > 1) ? ncombining : len;
    uint32_t stackbuf[256];
    uint32_t *u = ntext <= 256 ? stackbuf : snewn(ntext, uint32_t);
    for (int i = 0; i < ntext; i++)
        u[i] = (uint32_t)text[i];
    UI_CALL(s, draw_text, col, y, ncells, u, ntext, &st);
    if (u != stackbuf)
        sfree(u);
}

static void sshlx_tw_draw_text(TermWin *tw, int x, int y, wchar_t *text,
                               int len, unsigned long attrs, int lattrs,
                               truecolour tc)
{
    sshlx_session *s = SESSION_OF(tw);
    if (!s->draw_active)
        return;
    do_text_internal(s, x, y, text, len, attrs, lattrs, tc);
}

static void sshlx_tw_draw_cursor(TermWin *tw, int x, int y, wchar_t *text,
                                 int len, unsigned long attr, int lattr,
                                 truecolour tc)
{
    sshlx_session *s = SESSION_OF(tw);
    bool active, passive;
    if (!s->draw_active)
        return;

    /* As gtkwin_draw_cursor() */
    if (attr & ATTR_PASCURS) {
        attr &= ~ATTR_PASCURS;
        passive = true;
    } else
        passive = false;
    if ((attr & ATTR_ACTCURS) && s->cursor_type != CURSOR_BLOCK) {
        attr &= ~ATTR_ACTCURS;
        active = true;
    } else
        active = false;
    do_text_internal(s, x, y, text, len, attr, lattr, tc);

    if (attr & TATTR_COMBINING)
        len = 1;
    int col = x, ncells;
    if (!run_geometry(s, &col, &len, attr, lattr, &ncells))
        return;

    sshlx_rgb colour = rgb_from_palette(s, 261, false);
    bool right = (attr & ATTR_RIGHTCURS) != 0;
    if (s->cursor_type == CURSOR_BLOCK) {
        /* An active block cursor was drawn by do_text_internal. */
        if (passive)
            UI_CALL(s, draw_cursor_shape, col, y, ncells,
                    SSHLX_CURSOR_BOX_OUTLINE, true, right, colour,
                    line_attr_of(lattr));
    } else if (passive || active) {
        int shape = (s->cursor_type == CURSOR_UNDERLINE) ?
            SSHLX_CURSOR_UNDERLINE : SSHLX_CURSOR_VBAR;
        UI_CALL(s, draw_cursor_shape, col, y, ncells, shape, passive, right,
                colour, line_attr_of(lattr));
    }
}

static void sshlx_tw_draw_trust_sigil(TermWin *tw, int x, int y)
{
    sshlx_session *s = SESSION_OF(tw);
    if (s->draw_active)
        UI_CALL(s, draw_trust_sigil, x, y);
}

static int sshlx_tw_char_width(TermWin *tw, int uc)
{
    (void)tw;
    (void)uc;
    /* As GTK: double-width characters are drawn with a separate font. */
    return 1;
}

static void sshlx_tw_set_cursor_pos(TermWin *tw, int x, int y)
{
    sshlx_session *s = SESSION_OF(tw);
    UI_CALL(s, set_cursor_pos, x, y);
}

static void sshlx_tw_set_raw_mouse_mode(TermWin *tw, bool enable)
{
    sshlx_session *s = SESSION_OF(tw);
    s->send_raw_mouse = enable;
    UI_CALL(s, set_raw_mouse_mode, enable);
}

static void sshlx_tw_set_raw_mouse_mode_pointer(TermWin *tw, bool enable)
{
    sshlx_session *s = SESSION_OF(tw);
    s->pointer_raw = enable;
    UI_CALL(s, set_raw_mouse_pointer, enable);
}

static void sshlx_tw_set_scrollbar(TermWin *tw, int total, int start,
                                   int page)
{
    sshlx_session *s = SESSION_OF(tw);
    UI_CALL(s, set_scrollbar, total, start, page);
}

static void sshlx_tw_bell(TermWin *tw, int mode)
{
    sshlx_session *s = SESSION_OF(tw);
    UI_CALL(s, bell, mode);
}

static void sshlx_tw_clip_write(TermWin *tw, int clipboard, wchar_t *text,
                                int *attrs, truecolour *colours, int len,
                                bool must_deselect)
{
    sshlx_session *s = SESSION_OF(tw);
    (void)attrs;
    (void)colours;
    if (s->direct_to_font)
        return;   /* as GTK: no clipboard in direct-to-font mode */
    size_t outlen;
    char *utf8 = dup_wc_to_mb_c(CS_UTF8, text, len, "", &outlen);
    UI_CALL(s, clip_write, clipboard, utf8, must_deselect);
    sfree(utf8);
}

static void sshlx_tw_clip_request_paste(TermWin *tw, int clipboard)
{
    sshlx_session *s = SESSION_OF(tw);
    UI_CALL(s, clip_request_paste, clipboard);
}

static void sshlx_tw_refresh(TermWin *tw)
{
    sshlx_session *s = SESSION_OF(tw);
    term_invalidate(s->term);
    UI_CALL(s, refresh);
}

static void sshlx_tw_request_resize(TermWin *tw, int w, int h)
{
    sshlx_session *s = SESSION_OF(tw);
    if (s->cb.request_resize && !s->dead)
        UI_CALL(s, request_resize, w, h);
    else
        term_resize_request_completed(s->term);
}

static char *title_to_utf8(const char *title, int codepage)
{
    if (codepage != CP_UTF8) {
        wchar_t *title_w = dup_mb_to_wc(codepage, title);
        char *ret = encode_wide_string_as_utf8(title_w);
        sfree(title_w);
        return ret;
    }
    return dupstr(title);
}

static void sshlx_tw_set_title(TermWin *tw, const char *title, int codepage)
{
    sshlx_session *s = SESSION_OF(tw);
    char *t = title_to_utf8(title, codepage);
    UI_CALL(s, set_title, t);
    sfree(t);
}

static void sshlx_tw_set_icon_title(TermWin *tw, const char *title,
                                    int codepage)
{
    sshlx_session *s = SESSION_OF(tw);
    char *t = title_to_utf8(title, codepage);
    UI_CALL(s, set_icon_title, t);
    sfree(t);
}

static void sshlx_tw_set_minimised(TermWin *tw, bool minimised)
{
    sshlx_session *s = SESSION_OF(tw);
    UI_CALL(s, set_minimised, minimised);
}

static void sshlx_tw_set_maximised(TermWin *tw, bool maximised)
{
    sshlx_session *s = SESSION_OF(tw);
    UI_CALL(s, set_maximised, maximised);
}

static void sshlx_tw_move(TermWin *tw, int x, int y)
{
    sshlx_session *s = SESSION_OF(tw);
    UI_CALL(s, move_window, x, y);
}

static void sshlx_tw_set_zorder(TermWin *tw, bool top)
{
    sshlx_session *s = SESSION_OF(tw);
    UI_CALL(s, set_zorder, top);
}

static void sshlx_tw_palette_set(TermWin *tw, unsigned start,
                                 unsigned ncolours, const rgb *colours)
{
    sshlx_session *s = SESSION_OF(tw);
    assert(start <= OSC4_NCOLOURS);
    assert(ncolours <= OSC4_NCOLOURS - start);
    for (unsigned i = 0; i < ncolours; i++)
        s->palette[start + i] = colours[i];
    if (start <= OSC4_COLOUR_bg && OSC4_COLOUR_bg < start + ncolours) {
        sshlx_rgb bg = rgb_from_palette(s, OSC4_COLOUR_bg, false);
        UI_CALL(s, palette_changed, bg);
    }
}

static void sshlx_tw_palette_get_overrides(TermWin *tw, Terminal *term)
{
    (void)tw;
    (void)term;
    /* As GTK: no system-colour overrides. */
}

static void sshlx_tw_unthrottle(TermWin *tw, size_t bufsize)
{
    sshlx_session *s = SESSION_OF(tw);
    if (s->backend)
        backend_unthrottle(s->backend, bufsize);
}

const TermWinVtable sshlx_termwin_vt = {
    .setup_draw_ctx = sshlx_tw_setup_draw_ctx,
    .draw_text = sshlx_tw_draw_text,
    .draw_cursor = sshlx_tw_draw_cursor,
    .draw_trust_sigil = sshlx_tw_draw_trust_sigil,
    .char_width = sshlx_tw_char_width,
    .free_draw_ctx = sshlx_tw_free_draw_ctx,
    .set_cursor_pos = sshlx_tw_set_cursor_pos,
    .set_raw_mouse_mode = sshlx_tw_set_raw_mouse_mode,
    .set_raw_mouse_mode_pointer = sshlx_tw_set_raw_mouse_mode_pointer,
    .set_scrollbar = sshlx_tw_set_scrollbar,
    .bell = sshlx_tw_bell,
    .clip_write = sshlx_tw_clip_write,
    .clip_request_paste = sshlx_tw_clip_request_paste,
    .refresh = sshlx_tw_refresh,
    .request_resize = sshlx_tw_request_resize,
    .set_title = sshlx_tw_set_title,
    .set_icon_title = sshlx_tw_set_icon_title,
    .set_minimised = sshlx_tw_set_minimised,
    .set_maximised = sshlx_tw_set_maximised,
    .move = sshlx_tw_move,
    .set_zorder = sshlx_tw_set_zorder,
    .palette_set = sshlx_tw_palette_set,
    .palette_get_overrides = sshlx_tw_palette_get_overrides,
    .unthrottle = sshlx_tw_unthrottle,
};

/*
 * Clipboard routing from Conf, as window.c's setup_clipboards(). Custom
 * named X selections cannot be expressed through Qt, so the custom slots
 * are kept (PuTTY's numbering is unchanged) and the UI maps them to the
 * CLIPBOARD selection; see ARCHITECTURE-NOTES.md section 7.
 */
void sshlx_setup_clipboards(sshlx_session *s)
{
    Terminal *term = s->term;
    Conf *conf = s->conf;
    assert(term->mouse_select_clipboards[0] == CLIP_LOCAL);
    term->n_mouse_select_clipboards = 1;
    term->mouse_select_clipboards[
        term->n_mouse_select_clipboards++] = MOUSE_SELECT_CLIPBOARD;
    if (conf_get_bool(conf, CONF_mouseautocopy)) {
        term->mouse_select_clipboards[
            term->n_mouse_select_clipboards++] = CLIP_CLIPBOARD;
    }
    switch (conf_get_int(conf, CONF_mousepaste)) {
      case CLIPUI_IMPLICIT:
        term->mouse_paste_clipboard = MOUSE_PASTE_CLIPBOARD;
        break;
      case CLIPUI_EXPLICIT:
        term->mouse_paste_clipboard = CLIP_CLIPBOARD;
        break;
      case CLIPUI_CUSTOM:
        term->mouse_paste_clipboard = CLIP_CUSTOM_1;
        break;
      default:
        term->mouse_paste_clipboard = CLIP_NULL;
        break;
    }
    s->clipboard_ctrlshiftins = CLIP_CUSTOM_2;
    s->clipboard_ctrlshiftcv = CLIP_CUSTOM_3;
}
