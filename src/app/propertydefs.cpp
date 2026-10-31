#include "propertydefs.h"

#include "icons.h"

#include <QCoreApplication>
#include <QSet>

#include "sshlx_engine.h"

using K = PropertyDef::Kind;

static QString tr(const char *s)
{
    return QCoreApplication::translate("PropertyDefs", s);
}

static PropertyDef def(const char *cat, const char *name, const char *key, K kind,
                       const char *help, QList<QPair<int, QString>> e = {})
{
    PropertyDef d;
    d.category = tr(cat);
    d.name = tr(name);
    d.key = QString::fromLatin1(key);
    d.kind = kind;
    d.help = tr(help);
    d.enumValues = std::move(e);
    return d;
}

static QList<QPair<int, QString>> forceOnOffAuto()
{
    // PuTTY: FORCE_ON = 0, FORCE_OFF = 1, AUTO = 2
    return {{2, tr("Auto")}, {0, tr("Force on")}, {1, tr("Force off")}};
}

static QList<QPair<int, QString>> protocols()
{
    QList<QPair<int, QString>> r;
    for (size_t i = 0; i < sshlx_protocol_count(); i++) {
        const sshlx_protocol *p = sshlx_protocol_get(i);
        r.append({p->id, QString::fromUtf8(p->display_name)});
    }
    return r;
}

QStringList PropertyDefs::categoryOrder()
{
    return {tr("Display"), tr("Connection"), tr("Protocol"), tr("Terminal"),
            tr("Window"), tr("Appearance"), tr("Logging"), tr("Proxy"),
            tr("SSH"), tr("Tunnels"), tr("All Settings (advanced)")};
}

static QList<PropertyDef> buildCurated()
{
    QList<PropertyDef> l;
    // ---- Display (sshLX metadata) ----
    l << def("Display", "Name", "@name", K::Text,
             "Name of the connection. For connections this is also the name of "
             "the PuTTY saved session that stores its settings.");
    l << def("Display", "Description", "@description", K::Text,
             "Free-text description, shown as a tooltip in the tree.");
    QList<QPair<int, QString>> icons;
    for (int i = 0; i < Icons::nodeIcons().size(); i++)
        icons.append({i, Icons::nodeIcons()[i].label});
    l << def("Display", "Icon", "@icon", K::Enum,
             "Icon shown in the connection tree for this connection or folder. "
             "(protocol) uses the protocol's icon, or the folder icon for folders.", icons);
    l << def("Display", "Panel", "@panel", K::Text,
             "Name of the dock area this connection opens in. Leave empty to open "
             "next to the current tab.");
    l << def("Display", "Favourite", "@favourite", K::Bool,
             "Show this connection when the Favourites filter is on.");

    // ---- Connection ----
    l << def("Connection", "Hostname/IP", "host", K::Text,
             "Host name or IP address to connect to (PuTTY: Session > Host Name). "
             "May be given as user@host.");
    l << def("Connection", "Port", "port", K::Int,
             "TCP port to connect to (PuTTY: Session > Port).");
    l << def("Connection", "Username", "username", K::Text,
             "Auto-login user name (PuTTY: Connection > Data). Empty: ask.");
    l << def("Connection", "Credential", "@credential", K::Enum,
             "A shared login from Tools > Credentials Manager. Its user name, key and "
             "password replace this connection's, and changing it there changes every "
             "connection linked to it. A folder's credential applies to the connections "
             "inside it.");
    l << def("Connection", "Password (keyring)", "@password", K::Password,
             "Stored only in the system keyring (opt-in, Tools > Options > Passwords), "
             "never in a file, and offered once when the server asks. Leave empty "
             "to remove it. Not used while a credential is linked. PuTTY itself never "
             "stores passwords.");
    l << def("Connection", "SSH tunnel (jump host)", "@sshtunnel", K::Text,
             "Connect through this SSH server first ('user@jumphost[:port]'). "
             "Sets PuTTY's proxy type to 'SSH to proxy and use port forwarding'. "
             "Empty: no jump host.");
    l << def("Connection", "Address family", "addressfamily", K::Enum,
             "IPv4, IPv6 or automatic (PuTTY: Connection > Internet protocol version).",
             {{0, tr("Auto")}, {1, "IPv4"}, {2, "IPv6"}});
    l << def("Connection", "Keepalive interval (s)", "ping_interval", K::Int,
             "Seconds between keepalives; 0 disables them (PuTTY: Connection).");
    l << def("Connection", "Disable Nagle's algorithm", "tcp_nodelay", K::Bool,
             "Set TCP_NODELAY on the connection.");
    l << def("Connection", "TCP keepalives", "tcp_keepalives", K::Bool,
             "Enable the SO_KEEPALIVE socket option.");

    // ---- Protocol ----
    l << def("Protocol", "Protocol", "protocol", K::Enum,
             "Connection type: SSH, Serial, Telnet, Rlogin, SUPDUP, Raw or "
             "bare ssh-connection.", protocols());
    l << def("Protocol", "SSH version", "sshprot", K::Enum,
             "SSH protocol version. SSH-1 is insecure; only use it for legacy devices.",
             {{3, "2"}, {0, tr("1 (insecure)")}});
    l << def("Protocol", "Serial line", "serline", K::Text,
             "Serial device to open, e.g. /dev/ttyUSB0. A /dev/serial/by-id/... "
             "path survives renumbering.");
    l << def("Protocol", "Speed (baud)", "serspeed", K::Int, "Serial line speed.");
    l << def("Protocol", "Data bits", "serdatabits", K::Enum, "Data bits per character.",
             {{8, "8"}, {7, "7"}, {6, "6"}, {5, "5"}});
    l << def("Protocol", "Stop bits", "serstopbits", K::Enum, "Stop bits.",
             {{2, "1"}, {3, "1.5"}, {4, "2"}});   // stored in half-bits
    l << def("Protocol", "Parity", "serparity", K::Enum, "Parity checking.",
             {{0, tr("None")}, {1, tr("Odd")}, {2, tr("Even")}, {3, tr("Mark")}, {4, tr("Space")}});
    l << def("Protocol", "Flow control", "serflow", K::Enum, "Flow control.",
             {{0, tr("None")}, {1, "XON/XOFF"}, {2, "RTS/CTS"}, {3, "DSR/DTR"}});

    // ---- Terminal ----
    l << def("Terminal", "Terminal type", "termtype", K::Text,
             "Terminal type string sent to the server (PuTTY: Connection > Data).");
    l << def("Terminal", "Auto wrap mode", "wrap_mode", K::Bool,
             "Wrap text at the right edge of the terminal.");
    l << def("Terminal", "Implicit CR in every LF", "lfhascr", K::Bool,
             "Treat each line feed as CR+LF.");
    l << def("Terminal", "Implicit LF in every CR", "crhaslf", K::Bool,
             "Treat each carriage return as CR+LF.");
    l << def("Terminal", "Local echo", "localecho", K::Enum,
             "Echo typed characters locally.", forceOnOffAuto());
    l << def("Terminal", "Local line editing", "localedit", K::Enum,
             "Edit lines locally before sending.", forceOnOffAuto());
    l << def("Terminal", "Backspace key sends", "bksp_is_delete", K::Enum,
             "What the Backspace key sends.", {{1, "Control-? (127)"}, {0, "Control-H"}});
    l << def("Terminal", "Function keys", "funky_type", K::Enum,
             "Escape sequences sent by function keys.",
             {{0, "ESC[n~"}, {1, "Linux"}, {2, "Xterm R6"}, {3, "VT400"},
              {4, "VT100+"}, {5, "SCO"}, {6, "Xterm 216+"}});
    l << def("Terminal", "Bell", "beep", K::Enum, "Action on a terminal bell.",
             {{0, tr("None")}, {1, tr("System beep")}, {2, tr("Visual bell")}});
    l << def("Terminal", "Printer command", "printer", K::Text,
             "Host-based printing: a program, such as lpr, that gets what the remote "
             "side sends to the printer. Empty: printing is off.");
    l << def("Terminal", "Answerback to ^E", "answerback", K::Text,
             "String returned to the server on Ctrl-E.");

    // ---- Window ----
    l << def("Window", "Columns", "width", K::Int, "Initial terminal width.");
    l << def("Window", "Rows", "height", K::Int, "Initial terminal height.");
    l << def("Window", "Scrollback lines", "savelines", K::Int, "Lines of scrollback.");
    l << def("Window", "Show scrollbar", "scrollbar", K::Bool, "Display a scrollbar.");
    l << def("Window", "Reset scrollback on keypress", "scroll_on_key", K::Bool, "");
    l << def("Window", "Reset scrollback on display activity", "scroll_on_disp", K::Bool, "");
    l << def("Window", "Window title", "wintitle", K::Text,
             "Initial tab title. Empty: host name.");
    l << def("Window", "Close window on exit", "close_on_exit", K::Enum,
             "What PuTTY does when the session ends. sshLX keeps the tab with a "
             "reconnect bar unless 'Close tabs on exit' is enabled in Options.",
             {{2, tr("Only on clean exit")}, {0, tr("Always")}, {1, tr("Never")}});
    l << def("Window", "Warn before closing", "warn_on_close", K::Bool,
             "Ask before closing a connected tab.");
    l << def("Window", "Gap between text and window edge", "window_border", K::Int, "");

    // ---- Appearance ----
    l << def("Appearance", "Font", "font", K::Font,
             "Terminal font (PuTTY/GTK format, e.g. 'client:Monospace 12').");
    l << def("Appearance", "Bold font", "boldfont", K::Font, "Font for bold text; empty: derive.");
    l << def("Appearance", "Wide (CJK) font", "widefont", K::Font, "Font for double-width text.");
    l << def("Appearance", "Cursor", "cursor_type", K::Enum, "Cursor appearance.",
             {{0, tr("Block")}, {1, tr("Underline")}, {2, tr("Vertical line")}});
    l << def("Appearance", "Cursor blinks", "blink_cur", K::Bool, "");
    l << def("Appearance", "Bold text is", "bold_style", K::Enum,
             "How bold text is shown.",
             {{1, tr("A different font")}, {2, tr("A different colour")}, {3, tr("Both")}});
    {
        static const char *const names[] = {
            "Default Foreground", "Default Bold Foreground", "Default Background",
            "Default Bold Background", "Cursor Text", "Cursor Colour"};
        for (int i = 0; i < 6; i++) {
            PropertyDef d = def("Appearance", names[i], "colours", K::Colour,
                                "Terminal colour (PuTTY: Window > Colours). Inheritance "
                                "applies to the whole colour scheme.");
            d.colourIndex = i;
            l << d;
        }
    }
    l << def("Appearance", "ANSI colours", "ansi_colour", K::Bool, "Allow ANSI colours.");
    l << def("Appearance", "xterm 256-colour mode", "xterm_256_colour", K::Bool, "");
    l << def("Appearance", "24-bit true colour", "true_colour", K::Bool, "");
    l << def("Appearance", "Character set", "line_codepage", K::Text,
             "Remote character set, e.g. UTF-8 or ISO-8859-1.");

    // ---- Logging ----
    l << def("Logging", "Session logging", "logtype", K::Enum, "What to log.",
             {{0, tr("None")}, {1, tr("Printable output")}, {2, tr("All session output")},
              {3, tr("SSH packets")}, {4, tr("SSH packets and raw data")}});
    l << def("Logging", "Log file name", "logfilename", K::File,
             "&Y &M &D &T &H &P are expanded as in PuTTY.");
    l << def("Logging", "If log file exists", "logxfovr", K::Enum, "",
             {{1, tr("Always overwrite")}, {0, tr("Always append")}, {-1, tr("Ask")}});
    l << def("Logging", "Flush log frequently", "logflush", K::Bool, "");
    l << def("Logging", "Include header", "logheader", K::Bool, "");

    // ---- Proxy ----
    l << def("Proxy", "Proxy type", "proxy_type", K::Enum, "",
             {{0, tr("None")}, {1, "SOCKS 4"}, {2, "SOCKS 5"}, {3, "HTTP CONNECT"},
              {4, "Telnet"}, {5, tr("Local command")},
              {6, tr("SSH to proxy and use port forwarding")},
              {7, tr("SSH to proxy and execute a command")},
              {8, tr("SSH to proxy and invoke a subsystem")}});
    l << def("Proxy", "Proxy host", "proxy_host", K::Text, "");
    l << def("Proxy", "Proxy port", "proxy_port", K::Int, "");
    l << def("Proxy", "Proxy username", "proxy_username", K::Text, "");
    l << def("Proxy", "Proxy password", "proxy_password", K::Password,
             "Stored by PuTTY in the session file, as PuTTY does.");
    l << def("Proxy", "Telnet/local proxy command", "proxy_telnet_command", K::Text, "");
    l << def("Proxy", "Exclude hosts/IPs", "proxy_exclude_list", K::Text, "");
    l << def("Proxy", "DNS lookup at proxy end", "proxy_dns", K::Enum, "", forceOnOffAuto());

    // ---- SSH ----
    l << def("SSH", "Private key file", "keyfile", K::File,
             "Private key for public-key authentication (PuTTY .ppk).");
    l << def("SSH", "Certificate to use with the key", "detached_cert", K::File,
             "An OpenSSH certificate for the key, from an SSH certificate authority.");
    l << def("SSH", "Attempt authentication using Pageant", "tryagent", K::Bool,
             "Use keys from an agent, which is how smart cards work: see the README.");
    l << def("SSH", "Allow agent forwarding", "agentfwd", K::Bool, "");
    l << def("SSH", "Keyboard-interactive auth", "try_ki_auth", K::Bool, "");
    l << def("SSH", "GSSAPI auth", "try_gssapi_auth", K::Bool, "");
    l << def("SSH", "Enable compression", "compression", K::Bool, "");
    l << def("SSH", "Remote command", "remote_cmd", K::Text, "");
    l << def("SSH", "Don't start a shell", "ssh_no_shell", K::Bool, "");
    l << def("SSH", "Share SSH connections", "ssh_connection_sharing", K::Bool, "");
    l << def("SSH", "Enable X11 forwarding", "x11_forward", K::Bool, "");
    l << def("SSH", "X display location", "x11_display", K::Text, "");
    l << def("SSH", "Rekey after minutes", "ssh_rekey_time", K::Int, "");

    // ---- Tunnels ----
    l << def("Tunnels", "Port forwardings", "portfwd", K::KeyedMap,
             "Forwarded ports, as 'L8080=localhost:80; R2222=localhost:22; "
             "D1080=' (L local, R remote, D dynamic). Use Full settings for "
             "PuTTY's editor.");
    l << def("Tunnels", "Local ports accept other hosts", "lport_acceptall", K::Bool, "");
    l << def("Tunnels", "Remote ports do the same", "rport_acceptall", K::Bool, "");

    // Keep only keys that exist in this PuTTY build.
    QList<PropertyDef> ok;
    for (const PropertyDef &d : l)
        if (d.isSpecial() || sshlx_conf_key_find(d.key.toUtf8().constData()) >= 0)
            ok << d;
    return ok;
}

const QList<PropertyDef> &PropertyDefs::curated()
{
    static const QList<PropertyDef> l = buildCurated();
    return l;
}

const QList<PropertyDef> &PropertyDefs::all()
{
    static const QList<PropertyDef> l = [] {
        QList<PropertyDef> r = curated();
        QSet<QString> seen;
        for (const PropertyDef &d : r)
            seen.insert(d.inheritKey());
        const QString adv = tr("All Settings (advanced)");
        for (size_t k = 0; k < sshlx_conf_key_count(); k++) {
            const sshlx_conf_key_info *ki = sshlx_conf_key_get(k);
            QString id = QString::fromUtf8(ki->id);
            if (seen.contains(id) || ki->not_saved)
                continue;
            PropertyDef d;
            d.category = adv;
            d.name = ki->save_keyword ? QString::fromUtf8(ki->save_keyword) : id;
            d.key = id;
            d.help = tr("PuTTY setting '%1'. See PuTTY's documentation, or use Full "
                        "settings for PuTTY's own dialog.").arg(d.name);
            if (ki->subkey_type != SSHLX_T_NONE) {
                if (ki->subkey_type != SSHLX_T_STR || ki->value_type != SSHLX_T_STR)
                    continue;   // ordered lists (ciphers, kex...): Full settings
                d.kind = K::KeyedMap;
            } else {
                switch (ki->value_type) {
                case SSHLX_T_BOOL: d.kind = K::Bool; break;
                case SSHLX_T_INT: d.kind = K::Int; break;
                case SSHLX_T_FILENAME: d.kind = K::File; break;
                case SSHLX_T_FONT: d.kind = K::Font; break;
                default: d.kind = K::Text; break;
                }
            }
            r << d;
        }
        return r;
    }();
    return l;
}
