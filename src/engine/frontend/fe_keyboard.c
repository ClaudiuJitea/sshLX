/*
 * fe_keyboard.c - keyboard translation, ported from key_event() in PuTTY
 * 0.85's unix/window.c. The structure, ordering and special cases are kept
 * as in the original so that every Conf keyboard option behaves exactly
 * as in GTK PuTTY; the only changes are the input types (X11 keysyms and
 * a modifier mask supplied by the UI instead of a GdkEventKey) and the
 * GTK input-method step, which is described where it happens.
 */
#include <string.h>

#include "engine_internal.h"

/* X11 keysym values (from <X11/keysymdef.h>; not included here so the
 * engine stays independent of X headers). */
enum {
    XK_BackSpace = 0xff08, XK_Tab = 0xff09, XK_Return = 0xff0d,
    XK_Break = 0xff6b, XK_Home = 0xff50, XK_Left = 0xff51, XK_Up = 0xff52,
    XK_Right = 0xff53, XK_Down = 0xff54, XK_Page_Up = 0xff55,
    XK_Page_Down = 0xff56, XK_End = 0xff57, XK_Begin = 0xff58,
    XK_Insert = 0xff63, XK_Delete = 0xffff, XK_Num_Lock = 0xff7f,
    XK_KP_Enter = 0xff8d, XK_KP_Home = 0xff95, XK_KP_Left = 0xff96,
    XK_KP_Up = 0xff97, XK_KP_Right = 0xff98, XK_KP_Down = 0xff99,
    XK_KP_Page_Up = 0xff9a, XK_KP_Page_Down = 0xff9b, XK_KP_End = 0xff9c,
    XK_KP_Begin = 0xff9d, XK_KP_Insert = 0xff9e, XK_KP_Delete = 0xff9f,
    XK_KP_Multiply = 0xffaa, XK_KP_Add = 0xffab, XK_KP_Subtract = 0xffad,
    XK_KP_Decimal = 0xffae, XK_KP_Divide = 0xffaf, XK_KP_0 = 0xffb0,
    XK_KP_1, XK_KP_2, XK_KP_3, XK_KP_4, XK_KP_5, XK_KP_6, XK_KP_7,
    XK_KP_8, XK_KP_9,
    XK_F1 = 0xffbe, XK_F20 = 0xffbe + 19,
    XK_ISO_Left_Tab = 0xfe20,
    XK_Meta_L = 0xffe7, XK_Meta_R = 0xffe8, XK_Alt_L = 0xffe9,
    XK_Alt_R = 0xffea
};

/* Subroutine used in key_event */
static int return_key(sshlx_session *s, char *output, bool *special)
{
    int end;
    if (s->term->cr_lf_return) {
        output[1] = '\015';
        output[2] = '\012';
        end = 3;
    } else {
        output[1] = '\015';
        end = 2;
        *special = true;
    }
    return end;
}

static void key_pressed(sshlx_session *s)
{
    /* GTK PuTTY closes an exited-but-open window on keypress; sshLX
     * keeps the tab (with its reconnect bar) instead, so nothing here. */
    (void)s;
}

/* Is this the kind of key press GTK's default input method would have
 * consumed and committed as text? (gtk_im_context_simple commits
 * printable, non-control characters when neither Ctrl nor Alt is held.) */
static bool im_would_commit(const sshlx_key_event *ev)
{
    if (ev->mods & (SSHLX_MOD_CTRL | SSHLX_MOD_ALT))
        return false;
    const unsigned char *t = (const unsigned char *)ev->text;
    if (!t || !*t)
        return false;
    if (*t < 0x20 || *t == 0x7f)
        return false;
    /* C1 controls encoded in UTF-8 */
    if (t[0] == 0xc2 && t[1] >= 0x80 && t[1] < 0xa0)
        return false;
    return true;
}

sshlx_key_result sshlx_keyboard_event(sshlx_session *s,
                                      const sshlx_key_event *ev)
{
    char output[256];
    wchar_t ucsoutput[2];
    int ucsval, start, end, output_charset;
    bool special, use_ucsoutput;
    bool force_format_numeric_keypad = false;
    bool generated_something = false;
    char num_keypad_key = '\0';
    const char *event_string = ev->text ? ev->text : "";
    const uint32_t keyval = ev->keysym;
    const bool shift = (ev->mods & SSHLX_MOD_SHIFT) != 0;
    const bool ctrl = (ev->mods & SSHLX_MOD_CTRL) != 0;
    const bool meta = (ev->mods & SSHLX_MOD_ALT) != 0;  /* meta_mod_mask */

    noise_ultralight(NOISE_SOURCE_KEY, keyval);

    end = start = 0;
    special = use_ucsoutput = false;
    output_charset = CS_ISO8859_1;

    if (ev->release) {
        /* Alt release after an Alt+numberpad sequence generates the
         * code that was typed (only if more than one digit). */
        if ((keyval == XK_Meta_L || keyval == XK_Meta_R ||
             keyval == XK_Alt_L || keyval == XK_Alt_R) &&
            s->alt_keycode >= 0 && s->alt_digits > 1) {
            output[0] = s->alt_keycode;
            end = 1;
            goto done;
        }
        return SSHLX_KEY_NOT_HANDLED;
    }

    /* ---- key press ---- */
    if (keyval == XK_Meta_L || keyval == XK_Meta_R ||
        keyval == XK_Alt_L || keyval == XK_Alt_R) {
        s->alt_keycode = -1;
        s->alt_digits = 0;
        goto done;                 /* this generates nothing else */
    }

    if (meta && s->alt_keycode != -2) {
        int digit = -1;
        switch (keyval) {
          case XK_KP_0: case XK_KP_Insert: digit = 0; break;
          case XK_KP_1: case XK_KP_End: digit = 1; break;
          case XK_KP_2: case XK_KP_Down: digit = 2; break;
          case XK_KP_3: case XK_KP_Page_Down: digit = 3; break;
          case XK_KP_4: case XK_KP_Left: digit = 4; break;
          case XK_KP_5: case XK_KP_Begin: digit = 5; break;
          case XK_KP_6: case XK_KP_Right: digit = 6; break;
          case XK_KP_7: case XK_KP_Home: digit = 7; break;
          case XK_KP_8: case XK_KP_Up: digit = 8; break;
          case XK_KP_9: case XK_KP_Page_Up: digit = 9; break;
        }
        if (digit < 0)
            s->alt_keycode = -2;   /* it's invalid */
        else {
            if (s->alt_keycode == -1)
                s->alt_keycode = digit;   /* one-digit code */
            else
                s->alt_keycode = s->alt_keycode * 10 + digit;
            s->alt_digits++;
            goto done;
        }
    }

    if (keyval == '>' && ctrl)
        return SSHLX_KEY_FONT_BIGGER;
    if (keyval == '<' && ctrl)
        return SSHLX_KEY_FONT_SMALLER;

    /* Shift-PgUp and Shift-PgDn don't even generate keystrokes at all. */
    if (keyval == XK_Page_Up && ctrl && shift) {
        term_scroll(s->term, 1, 0);
        return SSHLX_KEY_HANDLED;
    }
    if (keyval == XK_Page_Up && shift) {
        term_scroll(s->term, 0, -s->term->rows / 2);
        return SSHLX_KEY_HANDLED;
    }
    if (keyval == XK_Page_Up && ctrl) {
        term_scroll(s->term, 0, -1);
        return SSHLX_KEY_HANDLED;
    }
    if (keyval == XK_Page_Down && ctrl && shift) {
        term_scroll(s->term, -1, 0);
        return SSHLX_KEY_HANDLED;
    }
    if (keyval == XK_Page_Down && shift) {
        term_scroll(s->term, 0, +s->term->rows / 2);
        return SSHLX_KEY_HANDLED;
    }
    if (keyval == XK_Page_Down && ctrl) {
        term_scroll(s->term, 0, +1);
        return SSHLX_KEY_HANDLED;
    }

    /* Neither do Shift-Ins or Ctrl-Ins (if enabled). */
    if (keyval == XK_Insert && shift) {
        switch (conf_get_int(s->conf, CONF_ctrlshiftins)) {
          case CLIPUI_IMPLICIT:
            term_request_paste(s->term, CLIP_PRIMARY);
            return SSHLX_KEY_HANDLED;
          case CLIPUI_EXPLICIT:
            term_request_paste(s->term, CLIP_CLIPBOARD);
            return SSHLX_KEY_HANDLED;
          case CLIPUI_CUSTOM:
            term_request_paste(s->term, s->clipboard_ctrlshiftins);
            return SSHLX_KEY_HANDLED;
          default:
            break;
        }
    }
    if (keyval == XK_Insert && ctrl) {
        static const int clips_clipboard[] = { CLIP_CLIPBOARD };
        switch (conf_get_int(s->conf, CONF_ctrlshiftins)) {
          case CLIPUI_IMPLICIT:
            /* do nothing; re-copy to PRIMARY is not needed */
            return SSHLX_KEY_HANDLED;
          case CLIPUI_EXPLICIT:
            term_request_copy(s->term, clips_clipboard,
                              lenof(clips_clipboard));
            return SSHLX_KEY_HANDLED;
          case CLIPUI_CUSTOM:
            term_request_copy(s->term, &s->clipboard_ctrlshiftins, 1);
            return SSHLX_KEY_HANDLED;
          default:
            break;
        }
    }

    /* Another pair of copy-paste keys. */
    if (shift && ctrl &&
        (keyval == 'C' || keyval == 'c' || keyval == 'V' || keyval == 'v')) {
        bool paste = (keyval == 'V' || keyval == 'v');
        switch (conf_get_int(s->conf, CONF_ctrlshiftcv)) {
          case CLIPUI_IMPLICIT:
            if (paste)
                term_request_paste(s->term, CLIP_PRIMARY);
            return SSHLX_KEY_HANDLED;
          case CLIPUI_EXPLICIT:
            if (paste) {
                term_request_paste(s->term, CLIP_CLIPBOARD);
            } else {
                static const int clips[] = { CLIP_CLIPBOARD };
                term_request_copy(s->term, clips, lenof(clips));
            }
            return SSHLX_KEY_HANDLED;
          case CLIPUI_CUSTOM:
            if (paste)
                term_request_paste(s->term, s->clipboard_ctrlshiftcv);
            else
                term_request_copy(s->term, &s->clipboard_ctrlshiftcv, 1);
            return SSHLX_KEY_HANDLED;
        }
    }

    special = false;
    use_ucsoutput = false;

    /* ALT+things gives leading Escape. */
    output[0] = '\033';

    {
        bool numeric = false;
        bool nethack_mode = conf_get_bool(s->conf, CONF_nethack_keypad);
        bool app_keypad_mode = (s->term->app_keypad_keys &&
                                !conf_get_bool(s->conf, CONF_no_applic_k));

        switch (keyval) {
          case XK_Num_Lock: num_keypad_key = 'G'; break;
          case XK_KP_Divide: num_keypad_key = '/'; break;
          case XK_KP_Multiply: num_keypad_key = '*'; break;
          case XK_KP_Subtract: num_keypad_key = '-'; break;
          case XK_KP_Add: num_keypad_key = '+'; break;
          case XK_KP_Enter: num_keypad_key = '\r'; break;
          case XK_KP_0: num_keypad_key = '0'; numeric = true; break;
          case XK_KP_Insert: num_keypad_key = '0'; break;
          case XK_KP_1: num_keypad_key = '1'; numeric = true; break;
          case XK_KP_End: num_keypad_key = '1'; break;
          case XK_KP_2: num_keypad_key = '2'; numeric = true; break;
          case XK_KP_Down: num_keypad_key = '2'; break;
          case XK_KP_3: num_keypad_key = '3'; numeric = true; break;
          case XK_KP_Page_Down: num_keypad_key = '3'; break;
          case XK_KP_4: num_keypad_key = '4'; numeric = true; break;
          case XK_KP_Left: num_keypad_key = '4'; break;
          case XK_KP_5: num_keypad_key = '5'; numeric = true; break;
          case XK_KP_Begin: num_keypad_key = '5'; break;
          case XK_KP_6: num_keypad_key = '6'; numeric = true; break;
          case XK_KP_Right: num_keypad_key = '6'; break;
          case XK_KP_7: num_keypad_key = '7'; numeric = true; break;
          case XK_KP_Home: num_keypad_key = '7'; break;
          case XK_KP_8: num_keypad_key = '8'; numeric = true; break;
          case XK_KP_Up: num_keypad_key = '8'; break;
          case XK_KP_9: num_keypad_key = '9'; numeric = true; break;
          case XK_KP_Page_Up: num_keypad_key = '9'; break;
          case XK_KP_Decimal: num_keypad_key = '.'; numeric = true; break;
          case XK_KP_Delete: num_keypad_key = '.'; break;
        }
        if ((app_keypad_mode && num_keypad_key &&
             (numeric || s->term->funky_type != FUNKY_XTERM)) ||
            (nethack_mode && num_keypad_key >= '1' && num_keypad_key <= '9')) {
            /* In these modes, we override the keypad handling:
             * regardless of Num Lock, the keys are handled by
             * format_numeric_keypad_key below. */
            force_format_numeric_keypad = true;
        } else if (im_would_commit(ev)) {
            /* GTK passes the event to its input method here, which
             * commits printable text through input_method_commit_event()
             * (term_keyinput in UTF-8). Do exactly that. */
            term_keyinput(s->term, CS_UTF8, event_string,
                          (int)strlen(event_string));
            key_pressed(s);
            return SSHLX_KEY_HANDLED;
        }
    }

    /* The UI supplies the text in UTF-8 (as GTK 2+ does via the locale). */
    output_charset = CS_UTF8;
    {
        size_t n = strlen(event_string);
        if (n > lenof(output) - 2)
            n = lenof(output) - 2;
        memcpy(output + 1, event_string, n);
        output[1 + n] = '\0';
    }

    if (!output[1] &&
        (ucsval = keysym_to_unicode((int)keyval)) >= 0) {
        ucsoutput[0] = '\033';
        ucsoutput[1] = ucsval;
        use_ucsoutput = true;
        end = 2;
    } else {
        output[lenof(output) - 1] = '\0';
        end = strlen(output);
    }
    if (meta) {
        start = 0;
        if (end == 1) end = 0;
    } else
        start = 1;

    /* Control-` is the same as Control-\ (unless gtk has a better idea) */
    if (!output[1] && keyval == '`' && ctrl) {
        output[1] = '\x1C';
        use_ucsoutput = false;
        end = 2;
    }

    /* Some backends do not change the string in response to the Control
     * modifier. So we do it ourselves here, if it's not already happened.
     * The translations below are in line with X11 policy. */
    if (ctrl && end == 2) {
        int orig = use_ucsoutput ? ucsoutput[1] : output[1];
        int new = orig;

        if (new >= '3' && new <= '7') {
            /* ^3,...,^7 map to 0x1B,...,0x1F */
            new += '\x1B' - '3';
        } else if (new == '2' || new == ' ') {
            /* ^2 and ^Space are both ^@, i.e. \0 */
            new = '\0';
        } else if (new == '8') {
            /* ^8 is DEL */
            new = '\x7F';
        } else if (new == '/') {
            /* ^/ is the same as ^_ */
            new = '\x1F';
        } else if (new >= 0x40 && new < 0x7F) {
            /* Everything anywhere near the alphabetics just gets masked. */
            new &= 0x1F;
        }
        /* Anything else, e.g. '0', is unchanged. */
        if (orig != new) {
            output[1] = new;
            use_ucsoutput = false;
        }
    }

    /* Control-Break sends a Break special to the backend */
    if (keyval == XK_Break && ctrl) {
        if (s->backend)
            backend_special(s->backend, SS_BRK, 0);
        return SSHLX_KEY_HANDLED;
    }

    /* We handle Return ourselves, because it needs to be flagged as
     * special to ldisc. */
    if (keyval == XK_Return) {
        end = return_key(s, output, &special);
        use_ucsoutput = false;
    }

    /* Control-2, Control-Space and Control-@ are NUL */
    if (!output[1] &&
        (keyval == ' ' || keyval == '2' || keyval == '@') &&
        ctrl && !shift) {
        output[1] = '\0';
        use_ucsoutput = false;
        end = 2;
    }

    /* Control-Shift-Space is 160 (ISO8859 nonbreaking space) */
    if (!output[1] && keyval == ' ' && ctrl && shift) {
        output[1] = '\240';
        output_charset = CS_ISO8859_1;
        use_ucsoutput = false;
        end = 2;
    }

    /* We don't let the toolkit tell us what Backspace is! We know better. */
    if (keyval == XK_BackSpace && !shift) {
        output[1] = conf_get_bool(s->conf, CONF_bksp_is_delete) ?
            '\x7F' : '\x08';
        use_ucsoutput = false;
        end = 2;
        special = true;
    }
    /* For Shift Backspace, do opposite of what is configured. */
    if (keyval == XK_BackSpace && shift) {
        output[1] = conf_get_bool(s->conf, CONF_bksp_is_delete) ?
            '\x08' : '\x7F';
        use_ucsoutput = false;
        end = 2;
        special = true;
    }

    /* Shift-Tab is ESC [ Z */
    if (keyval == XK_ISO_Left_Tab || (keyval == XK_Tab && shift)) {
        end = 1 + sprintf(output + 1, "\033[Z");
        use_ucsoutput = false;
    }
    /* And normal Tab is Tab, if the keymap hasn't already told us. */
    if (keyval == XK_Tab && end <= 1) {
        output[1] = '\t';
        end = 2;
    }

    if (num_keypad_key && force_format_numeric_keypad) {
        end = 1 + format_numeric_keypad_key(output + 1, s->term,
                                            num_keypad_key, shift, ctrl);
        use_ucsoutput = false;
        goto done;
    }

    {
        int fkey_number;
        bool consumed_meta_key;
        SmallKeypadKey sk_key;
        int xkey;

        if (keyval >= XK_F1 && keyval <= XK_F20) {
            fkey_number = keyval - XK_F1 + 1;
            consumed_meta_key = false;
            end = 1 + format_function_key(output + 1, s->term, fkey_number,
                                          shift, ctrl, meta,
                                          &consumed_meta_key);
            if (consumed_meta_key)
                start = 1; /* supersedes the usual prefixing of Esc */
            use_ucsoutput = false;
            goto done;
        }

        switch (keyval) {
          case XK_Home: case XK_KP_Home:
            sk_key = SKK_HOME; goto small_keypad_key;
          case XK_Insert: case XK_KP_Insert:
            sk_key = SKK_INSERT; goto small_keypad_key;
          case XK_Delete: case XK_KP_Delete:
            sk_key = SKK_DELETE; goto small_keypad_key;
          case XK_End: case XK_KP_End:
            sk_key = SKK_END; goto small_keypad_key;
          case XK_Page_Up: case XK_KP_Page_Up:
            sk_key = SKK_PGUP; goto small_keypad_key;
          case XK_Page_Down: case XK_KP_Page_Down:
            sk_key = SKK_PGDN; goto small_keypad_key;
          small_keypad_key:
            /* These keys don't generate terminal input with Ctrl */
            if (ctrl)
                break;
            consumed_meta_key = false;
            end = 1 + format_small_keypad_key(
                output + 1, s->term, sk_key, shift, ctrl, meta,
                &consumed_meta_key);
            if (consumed_meta_key)
                start = 1; /* supersedes the usual prefixing of Esc */
            use_ucsoutput = false;
            goto done;

          case XK_Up: case XK_KP_Up:
            xkey = 'A'; goto arrow_key;
          case XK_Down: case XK_KP_Down:
            xkey = 'B'; goto arrow_key;
          case XK_Right: case XK_KP_Right:
            xkey = 'C'; goto arrow_key;
          case XK_Left: case XK_KP_Left:
            xkey = 'D'; goto arrow_key;
          case XK_Begin: case XK_KP_Begin:
            xkey = 'G'; goto arrow_key;
          arrow_key:
            consumed_meta_key = false;
            end = 1 + format_arrow_key(output + 1, s->term, xkey, shift,
                                       ctrl, meta, &consumed_meta_key);
            if (consumed_meta_key)
                start = 1; /* supersedes the usual prefixing of Esc */
            use_ucsoutput = false;
            goto done;
        }
    }

    if (num_keypad_key) {
        end = 1 + format_numeric_keypad_key(output + 1, s->term,
                                            num_keypad_key, shift, ctrl);
        if (end == 1 && num_keypad_key == '\r') {
            /* Keypad Enter, lacking any other translation, becomes the
             * same special Return code as normal Return. */
            end = return_key(s, output, &special);
            use_ucsoutput = false;
        }
        use_ucsoutput = false;
        goto done;
    }

  done:
    if (end - start > 0) {
        if (special) {
            /* For special control characters, the character set should
             * never matter. */
            output[end] = '\0';        /* NUL-terminate */
            generated_something = true;
            term_keyinput(s->term, -1, output + start, -2);
        } else if (!s->direct_to_font) {
            if (!use_ucsoutput) {
                generated_something = true;
                term_keyinput(s->term, output_charset,
                              output + start, end - start);
            } else {
                /* We generated our own Unicode key data from the
                 * keysym, so use that instead. */
                generated_something = true;
                term_keyinputw(s->term, ucsoutput + start, end - start);
            }
        } else {
            /* In direct-to-font mode, send the string exactly as
             * received. */
            generated_something = true;
            term_keyinput(s->term, -1, output + start, end - start);
        }
    }

    if (generated_something)
        key_pressed(s);
    /* As GTK's key_event, which returns true for every event it reaches
     * this point with. (Application shortcuts are resolved by the UI
     * before key events are delivered to the terminal.) */
    return SSHLX_KEY_HANDLED;
}
