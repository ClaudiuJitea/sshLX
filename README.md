<p align="center">
  <img src="resources/appicon/sshlx-128.png" width="96" alt="sshLX icon">
</p>

<h1 align="center">sshLX</h1>

<p align="center">
  A tabbed, session-managed SSH, Telnet and serial client for Linux,<br>
  built on the <b>PuTTY 0.85</b> core with a modern <b>Qt 6</b> interface.
</p>

<p align="center">
  <img src="docs/images/screenshot.png" alt="sshLX with a connection tree, split SSH tabs and a serial console">
</p>

## Features

- **Connection tree:** folders, favourites, search and drag & drop. Your existing PuTTY sessions appear automatically.
- **Group icons:** 22 line icons that follow the dark and light themes, for folders and connections (servers, databases, cloud, network gear, containers, lab, sites and more).
- **Property grid** to edit connections and folders, with **folder inheritance** (set a username or proxy once per folder).
- **Dockable, splittable tabs:** side by side, stacked or floating (detach a tab, re-attach it from its window or the View menu). The layout and open tabs are restored on the next start.
- **Update check:** Help → Check for Updates asks GitHub for the latest release.
- **Every PuTTY option** stays available, through PuTTY's complete settings dialog.
- **PuTTY compatible:** sessions are stored in PuTTY's own format, so `putty` keeps working with them.
- **Serial hot-plug:**
  - plugging in a device offers to connect;
  - the line speed is detected automatically (9600 for most network consoles, 115200 for most boards);
  - per-device rules (*Always connect / Never ask*) survive renumbering;
  - tabs reconnect when their device comes back;
  - a permission error explains the `dialout` group fix.
- **In-tab prompts** for host keys, weak crypto and log files, with a **Reconnect** bar when a session ends.
- **Opt-in password storage** in the system keyring (GNOME Keyring / KWallet), never in a file.
- **Dark and light themes,** configurable shortcuts and an interface font size setting.

| Options | PuTTY settings, restyled |
|---|---|
| ![Options](docs/images/options.png) | ![Session settings](docs/images/session-settings.png) |

<p align="center"><img src="docs/images/icons.png" alt="Group icons" width="560"></p>

## Install

Download the packages from the [latest release](https://github.com/ClaudiuJitea/sshLX/releases/latest).

```sh
sudo apt install ./sshlx_0.6.0_amd64.deb      # Debian / Ubuntu
# or, on any distribution:
chmod +x sshLX-0.6.0-x86_64.AppImage && ./sshLX-0.6.0-x86_64.AppImage
```

Serial ports need membership of the `dialout` group (`uucp` on Arch): `sudo usermod -aG dialout $USER`.

## Build from source

You need CMake ≥ 3.21, a C/C++17 compiler, Perl, Ninja and Qt ≥ 6.2 (Widgets, DBus). `libsecret-1-dev` is optional and enables GNOME Keyring support.

```sh
git clone https://github.com/ClaudiuJitea/sshLX.git
cd sshLX
tools/fetch-putty.sh              # PuTTY 0.85 source into ../putty-0.85, checksum-verified
cmake -S . -B build -G Ninja
cmake --build build
./build/sshLX
```

`tools/package.sh` builds the `.deb` and the AppImage into `dist/`.

The PuTTY tree is only ever read. Its sha256 manifest is checked by `tools/verify-putty-untouched.sh` (also `cmake --build build --target verify-putty`).

## How it works

sshLX doesn't launch `putty`. PuTTY's own code is compiled in, unchanged:
- the SSH, Telnet, Rlogin, SUPDUP, Raw and serial back ends;
- the terminal emulator and cryptography;
- configuration and session storage.

sshLX replaces only the GTK front end, with a small C engine API and a Qt UI. It adds one back end of its own: **Local shell** (File → New Local Shell, Ctrl+Shift+T), your login shell in a pseudo-terminal on this machine. One PuTTY file has a documented override that fixes three serial bugs. See [ARCHITECTURE-NOTES.md](ARCHITECTURE-NOTES.md) and [PUTTY-OVERRIDES.md](PUTTY-OVERRIDES.md).

| Data | Location |
|---|---|
| Sessions | PuTTY's `~/.putty/sessions` (or `~/.config/putty`) |
| Tree, folders, inheritance | `~/.config/sshLX/connections.json` |
| Layout, shortcuts, options | `~/.config/sshLX/sshLX.conf` |
| Passwords (opt-in) | system keyring only |

## Licence and credits

sshLX is released under the [MIT licence](LICENSE).

- **PuTTY** is copyright 1997-2025 Simon Tatham and the PuTTY authors, and is used under the MIT licence ([LICENCE](third_party/putty/LICENCE)). sshLX is not PuTTY and isn't endorsed by its authors.
- **Libraries,** all linked dynamically:
  - Qt 6 (LGPL-3.0);
  - [Qt Advanced Docking System](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System) 4.4.1 (LGPL-2.1). Small fixes are applied in `cmake/patches/`.
  - [QtKeychain](https://github.com/frankosterfeld/qtkeychain) 0.15.0 (BSD-3-Clause);
  - libudev (LGPL-2.1), loaded at runtime.
