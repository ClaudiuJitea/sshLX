#include "terminallook.h"

#include <QPainter>
#include <QPixmap>

namespace TerminalLook {

const QList<Scheme> &schemes()
{
    // ANSI: black, red, green, yellow, blue, magenta, cyan, white; then the
    // same eight bright.
    static const QList<Scheme> list = {
        {"PuTTY", 0xBBBBBB, 0x000000, 0x00FF00,
         {0x000000, 0xBB0000, 0x00BB00, 0xBBBB00, 0x0000BB, 0xBB00BB, 0x00BBBB, 0xBBBBBB,
          0x555555, 0xFF5555, 0x55FF55, 0xFFFF55, 0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF}},
        {"sshLX Dark", 0xD4D4D8, 0x1B1C1F, 0x60A5FA,
         {0x2A2C31, 0xF87171, 0x4ADE80, 0xFACC15, 0x60A5FA, 0xC084FC, 0x22D3EE, 0xD4D4D8,
          0x5C6370, 0xFCA5A5, 0x86EFAC, 0xFDE047, 0x93C5FD, 0xD8B4FE, 0x67E8F9, 0xFFFFFF}},
        {"Tango", 0xD3D7CF, 0x2E3436, 0xD3D7CF,
         {0x2E3436, 0xCC0000, 0x4E9A06, 0xC4A000, 0x3465A4, 0x75507B, 0x06989A, 0xD3D7CF,
          0x555753, 0xEF2929, 0x8AE234, 0xFCE94F, 0x729FCF, 0xAD7FA8, 0x34E2E2, 0xEEEEEC}},
        {"Ubuntu", 0xFFFFFF, 0x300A24, 0xFFFFFF,
         {0x2E3436, 0xCC0000, 0x4E9A06, 0xC4A000, 0x3465A4, 0x75507B, 0x06989A, 0xD3D7CF,
          0x555753, 0xEF2929, 0x8AE234, 0xFCE94F, 0x729FCF, 0xAD7FA8, 0x34E2E2, 0xEEEEEC}},
        {"Solarized Dark", 0x839496, 0x002B36, 0x93A1A1,
         {0x073642, 0xDC322F, 0x859900, 0xB58900, 0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5,
          0x002B36, 0xCB4B16, 0x586E75, 0x657B83, 0x839496, 0x6C71C4, 0x93A1A1, 0xFDF6E3}},
        {"Solarized Light", 0x657B83, 0xFDF6E3, 0x586E75,
         {0x073642, 0xDC322F, 0x859900, 0xB58900, 0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5,
          0x002B36, 0xCB4B16, 0x586E75, 0x657B83, 0x839496, 0x6C71C4, 0x93A1A1, 0xFDF6E3}},
        {"Dracula", 0xF8F8F2, 0x282A36, 0xF8F8F2,
         {0x21222C, 0xFF5555, 0x50FA7B, 0xF1FA8C, 0xBD93F9, 0xFF79C6, 0x8BE9FD, 0xF8F8F2,
          0x6272A4, 0xFF6E6E, 0x69FF94, 0xFFFFA5, 0xD6ACFF, 0xFF92DF, 0xA4FFFF, 0xFFFFFF}},
        {"Nord", 0xD8DEE9, 0x2E3440, 0xD8DEE9,
         {0x3B4252, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x88C0D0, 0xE5E9F0,
          0x4C566A, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x8FBCBB, 0xECEFF4}},
        {"One Dark", 0xABB2BF, 0x282C34, 0x528BFF,
         {0x282C34, 0xE06C75, 0x98C379, 0xE5C07B, 0x61AFEF, 0xC678DD, 0x56B6C2, 0xABB2BF,
          0x5C6370, 0xE06C75, 0x98C379, 0xE5C07B, 0x61AFEF, 0xC678DD, 0x56B6C2, 0xFFFFFF}},
        {"Gruvbox Dark", 0xEBDBB2, 0x282828, 0xEBDBB2,
         {0x282828, 0xCC241D, 0x98971A, 0xD79921, 0x458588, 0xB16286, 0x689D6A, 0xA89984,
          0x928374, 0xFB4934, 0xB8BB26, 0xFABD2F, 0x83A598, 0xD3869B, 0x8EC07C, 0xEBDBB2}},
        {"Monokai", 0xF8F8F2, 0x272822, 0xF8F8F0,
         {0x272822, 0xF92672, 0xA6E22E, 0xF4BF75, 0x66D9EF, 0xAE81FF, 0xA1EFE4, 0xF8F8F2,
          0x75715E, 0xF92672, 0xA6E22E, 0xF4BF75, 0x66D9EF, 0xAE81FF, 0xA1EFE4, 0xF9F8F5}},
        {"Tokyo Night", 0xC0CAF5, 0x1A1B26, 0xC0CAF5,
         {0x15161E, 0xF7768E, 0x9ECE6A, 0xE0AF68, 0x7AA2F7, 0xBB9AF7, 0x7DCFFF, 0xA9B1D6,
          0x414868, 0xF7768E, 0x9ECE6A, 0xE0AF68, 0x7AA2F7, 0xBB9AF7, 0x7DCFFF, 0xC0CAF5}},
        {"Catppuccin Mocha", 0xCDD6F4, 0x1E1E2E, 0xF5E0DC,
         {0x45475A, 0xF38BA8, 0xA6E3A1, 0xF9E2AF, 0x89B4FA, 0xF5C2E7, 0x94E2D5, 0xBAC2DE,
          0x585B70, 0xF38BA8, 0xA6E3A1, 0xF9E2AF, 0x89B4FA, 0xF5C2E7, 0x94E2D5, 0xA6ADC8}},
        {"GitHub Light", 0x24292F, 0xFFFFFF, 0x0969DA,
         {0x24292F, 0xCF222E, 0x116329, 0x4D2D00, 0x0969DA, 0x8250DF, 0x1B7C83, 0x6E7781,
          0x57606A, 0xA40E26, 0x1A7F37, 0x633C01, 0x218BFF, 0xA475F9, 0x3192AA, 0x8C959F}},
    };
    return list;
}

// PuTTY's 22 slots for a scheme.
static QList<QRgb> colourSlots(const Scheme &s)
{
    // The PuTTY scheme keeps PuTTY's own bold colours; the others draw bold
    // text in the normal colours (bold shows as a bold font by default).
    const bool putty = s.name == "PuTTY";
    QList<QRgb> c = {s.fg, putty ? 0xFFFFFF : s.fg, s.bg, putty ? 0x555555 : s.bg,
                     s.bg, s.cursor};
    for (int i = 0; i < 8; i++)
        c << s.ansi[i] << s.ansi[8 + i];
    return c;
}

void applyScheme(sshlx_conf *c, const Scheme &s)
{
    const int key = sshlx_conf_key_find("colours");
    const QList<QRgb> rgb = colourSlots(s);
    for (int i = 0; i < rgb.size(); i++) {
        sshlx_conf_set_int_int(c, key, i * 3, qRed(rgb[i]));
        sshlx_conf_set_int_int(c, key, i * 3 + 1, qGreen(rgb[i]));
        sshlx_conf_set_int_int(c, key, i * 3 + 2, qBlue(rgb[i]));
    }
}

int currentScheme(const sshlx_conf *c)
{
    const int key = sshlx_conf_key_find("colours");
    for (int n = 0; n < schemes().size(); n++) {
        const QList<QRgb> rgb = colourSlots(schemes()[n]);
        bool same = true;
        for (int i = 0; same && i < rgb.size(); i++)
            same = sshlx_conf_get_int_int(c, key, i * 3, -1) == qRed(rgb[i]) &&
                   sshlx_conf_get_int_int(c, key, i * 3 + 1, -1) == qGreen(rgb[i]) &&
                   sshlx_conf_get_int_int(c, key, i * 3 + 2, -1) == qBlue(rgb[i]);
        if (same)
            return n;
    }
    return -1;
}

QIcon swatch(const Scheme &s)
{
    // Square, to fill a menu's 16x16 icon box: the background, a line of
    // text, the cursor, and red/green/yellow/blue/magenta/cyan below.
    QIcon icon;
    for (int scale : {1, 2, 3}) {
        QPixmap pm(16 * scale, 16 * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(128, 128, 128, 140), 1));
        p.setBrush(QColor(s.bg));
        p.drawRoundedRect(QRectF(0.5, 0.5, 15, 15), 3, 3);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(s.fg));
        p.drawRect(QRectF(3, 3.5, 6.5, 2));
        p.setBrush(QColor(s.cursor));
        p.drawRect(QRectF(10.5, 3, 2.5, 3));
        for (int i = 0; i < 6; i++) {
            p.setBrush(QColor(s.ansi[1 + i]));
            p.drawEllipse(QRectF(3 + (i % 3) * 3.6, 7.6 + (i / 3) * 3.6, 2.8, 2.8));
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QStringList appearanceKeys()
{
    return {"colours", "font", "boldfont", "widefont", "wideboldfont",
            "cursor_type", "blink_cur"};
}

} // namespace TerminalLook
