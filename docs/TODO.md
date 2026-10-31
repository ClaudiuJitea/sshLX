# sshLX TODO

What is left after sshLX 0.7.3, from the audit against SecureCRT 9.7.3 (1 July 2026).
Items are in the order I'd build them.

## Check on real equipment first

0.7.3 was tested against real lrzsz, tftp-hpa and OpenSSH, but not on network hardware
or with a real mouse and desktop. These come before new features. The steps are in
`docs/MANUAL-TEST-CHECKLIST.md`, sections 1f and 1g.

- [ ] Xmodem from ROMMON or a boot loader over a real serial line at 9600.
- [ ] A switch copying a file from the TFTP server on port 69.
- [ ] Logon actions on a real switch: `enable` and its password.
- [ ] Command Window on several real devices, including an error on one of them.
- [ ] File transfer on a Cisco device, including `flash:` paths in SCP-only mode.
- [ ] Real mouse drags on GNOME Wayland in the file transfer tab.
- [ ] A Python script started from the AppImage, using the system's Python.
- [ ] Moving the button bar to the bottom edge, then restarting.

## Check the newest features

- [ ] Run section 1h of the checklist. The list above covers 0.7.3; these eleven are newer.
- [ ] Run sections 1h and 1i on real equipment.

## Improvements to the newest features

- [ ] Live typing: mirror paste as well as keys.
- [ ] Recorder: arrow keys, and prompts that don't say password or passphrase.
- [ ] Log rotation per connection. It is one setting for all of them.
- [ ] Session locking with a password. Anyone at the keyboard can unlock a tab.
- [ ] Keep tab groups when tabs are closed. A group exists only while it has a tab.
- [ ] Bulk edit: fonts, colours and port forwarding, which need their own editors.

## Improvements to the medium-priority features

- [ ] Check them: section 1i of the checklist.
- [ ] Kermit: long packets and sliding windows. It is slow on fast links.
- [ ] Command line: password prompts for keyboard-interactive logins.
- [ ] Command line: `get` and `put` through SCP for devices without SFTP.
- [ ] Hex view: data sent as well as received, and data during file transfers.
- [ ] Session status: a details window with host key and MAC.

## Not planned

Reviewed for 0.7.7 and left out on purpose:

- **TN3270, Wyse 50/60:** they need a new terminal emulator beside PuTTY's, for very few users.
- **FTP, FTPS, WebDAV, S3:** a file manager's job. SFTP and SCP cover SSH devices, and TFTP and the modem protocols cover the rest.
- **Window transparency:** Qt can't set a window's opacity on Wayland, which is where sshLX mostly runs.
- **FIPS 140-2:** needs a validated crypto module. PuTTY's own crypto is not one, and no code change makes it one.
- **TAPI:** a Windows interface.
- **Mobile version:** a separate product.

Section 508 got a first pass in 0.7.7, with accessible names on the newer controls. A review with a real screen reader is still open.

## Improvements to 0.7.3 features

- [ ] Command Window: warn before a multi-line send to many tabs.
- [ ] Logon actions: a per-step timeout. It is fixed at 30 seconds.
- [ ] Logon actions: a Config panel row, as they are only in the right-click menu.
- [ ] Macros: warn when text looks like a password, as macros are stored as plain text.
- [ ] Workspaces: rename, and include file transfer tabs. Quick Connect tabs are left out.
- [ ] TFTP: netascii line-ending conversion. Such requests are currently served as binary.
- [ ] TFTP: IPv6.
- [ ] TFTP: a one-click way to use port 69, which needs a system setting today.
- [ ] Xmodem: trim the last block's padding. Xmodem can't record a file's real length, so
      received files can end with extra `0x1A` bytes.
- [ ] Scripts: arguments in the UI, and a tab-scoped way to run a script on Command
      Window targets.

## Housekeeping

- [ ] Install `libsecret-1-dev`. Release builds rely on headers unpacked in `build/`,
      which are lost if that folder is deleted.
- [ ] Install the current `.deb`: `/usr/bin/sshLX` on this machine is older than 0.7.
- [ ] The tests are kept out of the published source: back up `tests/`, as a fresh clone has none.
- [ ] Add the OpenSSH (`sshd -i`), lrzsz and tftp test runs to CI. They are skipped
      without their environment variables.
- [ ] Consider dropping `uppity` from the SSH tests: it can break off an SCP download
      partway, which real OpenSSH did not do in 10 runs.

## Not applicable

SecureCRT limits these to Windows, so they don't count as gaps for a Linux app:
RDP tabs, Telnet/SSL, ActiveX scripting (VBScript, JScript, PerlScript), the
customisable menu and toolbar, and MSI installers.

## Sources

- [SecureCRT features](https://www.vandyke.com/products/securecrt/features.html)
- [Key features](https://www.vandyke.com/products/securecrt/key_features.html)
- [New in 9.7](https://www.vandyke.com/products/securecrt/new-in.html)
- [Version history](https://www.vandyke.com/products/securecrt/history.txt)
