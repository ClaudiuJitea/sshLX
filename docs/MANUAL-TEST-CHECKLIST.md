# sshLX manual test checklist

The automated suite (`ctest`) covers the engine and much of the UI on Qt's
offscreen platform. This list covers what needs a real desktop, real
servers and real hardware. Run it on both **X11** and **Wayland** sessions
(section 7).

Before you start:
- Build, then run `build/sshLX`.
- Keep a PuTTY 0.85 for side-by-side comparison where noted.

## 1. Every protocol

| # | Step | Expected |
|---|---|---|
| 1.1 | SSH-2 to a real server (password auth) | Host-key question inside the tab (Accept / Connect Once / Cancel, More info shows fingerprints). After Accept, PuTTY's `sshhostkeys` contains the key; reconnect asks nothing. |
| 1.2 | SSH-2 with a `.ppk` key and with Pageant running | Logs in via key / agent; "Authenticating with public key" in Event Log |
| 1.3 | SSH with agent forwarding, X11 forwarding (`xclock`), local and dynamic port forwarding | All work; listed in Event Log |
| 1.4 | SSH through a jump host (Config ▸ Connection ▸ SSH tunnel) | Connects via proxy; Event Log shows the proxy connection |
| 1.5 | SSH-1 to a legacy device (if available) | Works after the SSH-1 warning |
| 1.6 | Telnet (`telnet` server or a switch) | Negotiation works; Special Commands menu offers Break, AYT, … |
| 1.7 | Rlogin | Logs in with the configured user name |
| 1.8 | SUPDUP | Connects to a SUPDUP server (e.g. an ITS emulator) |
| 1.9 | Raw (`nc -l 9000`) | Text both ways; closing nc disconnects the tab |
| 1.10 | Serial (USB adapter, 115200 8N1) | Both directions; Special Commands ▸ Break works |
| 1.11 | Session logging (Config ▸ Logging = All session output) to an existing file | Log-file question in the tab (Overwrite / Append / Disable); the file is written |
| 1.12 | Change Settings mid-session (tab context menu): font, colours, scrollback | Applied immediately, as in PuTTY |
| 1.13 | Weak cipher allowed below the warn line | Weak-crypto question in the tab |

## 2. Settings compatibility

| # | Step | Expected |
|---|---|---|
| 2.1 | Start sshLX with existing PuTTY sessions | All appear in the tree with no import step |
| 2.2 | Edit a session in sshLX's Config panel, then open it in PuTTY | PuTTY shows the change |
| 2.3 | Save a session in PuTTY while sshLX runs, then File ▸ Import PuTTY Sessions | It appears in the tree |
| 2.4 | Rename and delete in the tree | The PuTTY session file is renamed / deleted |
| 2.5 | Folder sets User name + Proxy; a connection inherits them | Values show in italics in the grid; the connection uses them; editing the folder changes them |
| 2.6 | Full settings… on a folder, change Font | Every connection inheriting `font` changes |
| 2.7 | Export tree, then import on another machine/user | Folders and connections recreated, sessions included |

## 3. Many tabs, isolation

| # | Step | Expected |
|---|---|---|
| 3.1 | Open 10+ tabs mixing SSH, Telnet and serial | All responsive; typing in one never shows in another |
| 3.2 | Kill one SSH server (or `ip link set down` its route); pull one serial adapter; close one tab | Only those tabs change state (reconnect bar); every other session keeps working |
| 3.3 | Type into a tab right after unplugging its adapter | That tab shows "Writing serial port: Input/output error"; nothing else is affected |
| 3.4 | Reconnect from the bar | Same tab, scrollback kept, "Session restarted" in Event Log |
| 3.5 | Quit with connected tabs | Confirmation; with Options ▸ Restore sessions on startup, tabs and their dock layout return on the next start |
| 3.6 | `kill -SEGV` the process with tabs open, restart | sshLX offers to reopen the tabs |

## 4. USB serial hot-plug

| # | Step | Expected |
|---|---|---|
| 4.1 | Plug in an FTDI / CP210x / CH340 / CDC-ACM adapter | Within ~1 s it appears under Serial Ports (tooltip: node, by-id path, VID:PID, manufacturer, product, serial number); banner: "USB serial device detected: … on /dev/ttyUSB0 – Connect?" |
| 4.2 | Click Connect | Serial tab at 115200 8N1 using the `/dev/serial/by-id/...` path |
| 4.3 | Unplug | Tab disconnected (kept) |
| 4.4 | Plug back in, even into another port (ttyUSB1) | Tab reconnects automatically |
| 4.5 | Settings… (or Device Rule…) → 9600 7E1, Always connect; replug | New tab opens automatically with 9600 7E1 |
| 4.6 | Never ask; replug | Listed in the tree, no banner, no tab |
| 4.7 | Remove your user from `dialout`, log in again, plug in | Banner and tab explain the dialout/uucp group fix |
| 4.8 | Options ▸ Serial ▸ desktop notification | Desktop notification as well as the banner |
| 4.9 | Bluetooth `rfcomm0` (`rfcomm bind`) | Appears and connects |

## 5. Docking and tabs

| # | Step | Expected |
|---|---|---|
| 5.1 | Drag a tab to the right edge of the session area | Two areas side by side (e.g. two sessions left, one SSH right) |
| 5.2 | Drag a tab out of the window | Floating window; terminal keeps working; shortcuts still work in it |
| 5.3 | Middle-click a tab; tab drop-down list | Closes (with confirmation if connected); list shows all tabs of the area |
| 5.4 | Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+PgDn, Ctrl+PgUp | Cycle through tabs across areas |
| 5.5 | Server sets the title (`printf '\e]0;hello\a'`) | Tab title becomes "hello" |
| 5.6 | Connection with Config ▸ Display ▸ Panel = "Right" | Opens in its own area; other "Right" connections join it |
| 5.7 | View ▸ Reset Layout | Default layout; tabs kept |

## 6. Terminal fidelity (compare with PuTTY 0.85)

| # | Step | Expected |
|---|---|---|
| 6.1 | `vim`, `htop`, `mc`, `tmux` | Same rendering and key behaviour as PuTTY |
| 6.2 | Keyboard settings: Backspace ^H/^?, Home/End modes, function keys (ESC[n~, Linux, Xterm R6, VT400, VT100+, SCO), application keypad, NetHack keypad, Alt+numpad codes | Byte-for-byte the same as PuTTY (`showkey -a` / `cat -v`) |
| 6.3 | Mouse selection, word/line double/triple click, rectangular Alt-drag, xterm mouse reporting (in `mc`), Shift overrides it | As in PuTTY |
| 6.4 | Copy/paste: select = PRIMARY, middle-click paste, Ctrl+Shift+C/V per Config ▸ Selection | As in PuTTY |
| 6.5 | Wide (CJK), combining characters, emoji, 256-colour and true-colour tests, DEC line drawing with "VT100 line drawing in UTF-8 mode" on | Correct cells, no misalignment |
| 6.6 | Ctrl+> / Ctrl+< | Font size changes |
| 6.7 | HiDPI (scale 2 and fractional 1.25) | Sharp text, correct cell grid |
| 6.8 | IME (ibus / fcitx) composing | Preedit shown at the cursor; commit sent |
| 6.9 | Server resize (`printf '\e[8;30;100t'`) | Ignored in a docked tab (terminal keeps the tab size); see notes §7 |

## 7. X11 and Wayland

Run sections 1.1, 3.1, 4.1–4.4, 5 and 6.3–6.8 twice:
- on **Wayland**: `QT_QPA_PLATFORM=wayland build/sshLX`;
- on **X11**: an X11 session, or `QT_QPA_PLATFORM=xcb build/sshLX` under XWayland.

| # | Check | Expected |
|---|---|---|
| 7.1 | PRIMARY selection (middle-click paste into another app) | Works on X11; on Wayland where the compositor supports primary selection |
| 7.2 | Keyboard: keysyms reach the engine (Qt passes native keysyms on both) | Same bytes as 6.2 |
| 7.3 | X11 forwarding on Wayland | Uses XWayland's `$DISPLAY` |
| 7.4 | Floating tabs, drag & drop docking | Work on both |
