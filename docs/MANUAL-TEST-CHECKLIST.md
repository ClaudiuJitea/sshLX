# sshLX manual test checklist

This list covers what needs a real desktop, real servers and real
hardware. Run it on both **X11** and **Wayland** sessions
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

## 1b. Terminal mouse and Paste Configuration

Automated against a fake device by `ctest` (`tests/session_e2e_test.cpp`); these need a real desktop and device.

| # | Step | Expected |
|---|---|---|
| 1b.1 | Select text, paste into another application with Ctrl+V | The selection arrives |
| 1b.2 | Right click in the terminal; Ctrl + right click; Menu key | Pastes; menu; menu |
| 1b.3 | Right click inside `vim` with `set mouse=a`, then Shift + right click | vim gets the first; the second pastes |
| 1b.4 | Copy two lines, right click at a shell without bracketed paste | Asks first |
| 1b.5 | Paste Configuration on a real switch with one misspelt line | Stops on it; Send Amended continues from there; Skip Line goes on to the next |
| 1b.6 | The same over a 9600 serial console, with a `banner motd` block and `write memory` | Nothing lost; no false stops |
| 1b.7 | Pull the cable during a push | Bar says Stopped, with what was applied |

## 1c. Keyword highlighting

| # | Step | Expected |
|---|---|---|
| 1c.1 | Add `up` in green and `down` in red, then run `show ip interface brief` | Each word shows in its own colour |
| 1c.2 | Select a coloured word | The selection shows normally |
| 1c.3 | Scroll back; resize; change font | Colours stay on the right text |
| 1c.4 | Ctrl+Shift+H | All tabs lose and regain colours at once |

## 1d. Terminal menu

| # | Step | Expected |
|---|---|---|
| 1d.0 | Ctrl+Shift+F, search a word in a long scrollback; Previous and Next; Esc | Matches light up in the terminal, it jumps to each, Esc returns to the bottom |
| 1d.1 | Print Terminal Text to a real printer | Prints the whole scrollback |
| 1d.2 | Clear Terminal inside `vim` | vim's screen stays; scrollback is emptied |
| 1d.3 | Change Terminal Title, then run a program that sets the title | The title you chose stays; an empty title follows the server again |

## 1e. Credentials Manager

| # | Step | Expected |
|---|---|---|
| 1e.1 | Create a credential with a password, link a folder, log in to two devices inside it | Both log in without asking; the secret is visible in Seahorse/KWallet as `credential:<id>`, and in no file under `~/.config/sshLX` |
| 1e.2 | Change the password, then reconnect an open tab | The new one is used |
| 1e.3 | A credential with a passphrase-protected key and a password | Each goes to its own prompt |
| 1e.4 | A wrong stored password | Offered once, then PuTTY asks in the terminal |
| 1e.5 | Lock the keyring or stop its service, then Save | Error shown; nothing is saved and the dialog stays open |

## 1f. File transfer

- [ ] Right-click an SSH connection → File Transfer: login shows in the tab, then the two panes.
- [ ] A linked credential logs in with no prompt.
- [ ] Drag a file each way, then a folder with subfolders. Compare checksums.
- [ ] Replacing an existing file asks first. Skip leaves it alone.
- [ ] Cancel a large download over an existing file: the old file is unchanged.
- [ ] Cancel a large upload: no partial file is left on the server.
- [ ] Rename, New Folder and Delete on the server side. Closing mid-transfer warns.
- [ ] Click the left pane: the same toolbar buttons now act on this computer, and Delete moves to the Trash.
- [ ] F5, F2 and Del work in the panel, and still reach the device in a terminal tab.
- [ ] Filter and Hidden Files change both sides, and Hidden Files is remembered.
- [ ] Unplug the network mid-transfer: the job fails, Reconnect brings the panes back.
- [ ] A switch with `ip scp server enable` and no SFTP: the tab says SCP only; send and fetch a file on `flash:`.
- [ ] Test SSH and SFTP without a listening port, using PuTTY's uppity on stdio:
      `SSHLX_E2E_SFTP_COMMAND="cd DIR && exec uppity --hostkey KEY --allow-none-auth --sessiondir DIR" SSHLX_E2E_SFTP_DIR=DIR session-e2e-test transferTabOverRealSsh`

## 1g. Command Window, button bar, logon, workspaces, transfers, scripts

- [ ] View → Command Window: send to All tabs on two real SSH sessions, then Chosen tabs.
- [ ] Two lines to two devices: an error on one pauses only that tab.
- [ ] Button bar: run a macro, a folder's macro, and right-click → Run on Command Window Tabs.
- [ ] Drag the button bar to the bottom edge, restart: it stays there.
- [ ] Logon actions on a real switch: `enable`, then its password from a credential.
- [ ] A step whose text never appears: the bar reports it after 30 seconds, and the terminal stays usable.
- [ ] Save a workspace with split tabs, close them, reopen: Replace restores the split, Add joins.
- [ ] Workspace with a Quick Connect tab: the save reports it was left out.
- [ ] ROMMON or a boot loader: Xmodem an image over a real serial line at 9600.
- [ ] SSH to a Linux host with lrzsz: `sz file` offers a transfer; Cancel leaves the shell usable.
- [ ] `rz` offers a file dialog; Send from the terminal menu types `rz` itself.
- [ ] TFTP: a real switch copies a file from the server on port 69 (see the dialog's hint).
- [ ] TFTP: uploads refused by default; allowed with the box ticked; an existing file needs the second box.
- [ ] TFTP status shows in the status bar, and stops when sshLX closes.
- [ ] Run a script from each place: Tools → Run Script, Tools → Scripts, a macro.
- [ ] In the AppImage too: a script finds the system's Python.

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
