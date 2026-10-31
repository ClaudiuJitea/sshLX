# Scripting sshLX

sshLX runs Python scripts with the Python 3 on your machine. Start one from
**Tools → Run Script**, from a macro of the *Python script* kind, or put it in
`~/.config/sshLX/scripts/` to list it under **Tools → Scripts**. What it prints
appears in the Script Console, which also has a Stop button.

The `sshlx` module is provided by sshLX, so a script only needs `import sshlx`.
It works only in scripts that sshLX started.

```python
import sshlx

for tab in sshlx.tabs():
    if tab.state != "connected":
        continue
    tab.send_line("terminal length 0")
    tab.wait_for("#")
    version = tab.run("show version", "#")
    with open(tab.name + ".txt", "w") as f:
        f.write(version)

sshlx.status("Done")
```

A login that needs a saved password:

```python
import sshlx

tab = sshlx.connect("Lab/Switch 1")
tab.wait_connected()
tab.wait_for(">")
tab.send_line("enable")
tab.wait_for("Password:")
tab.send_password("Enable")     # a name from the Credentials Manager
tab.wait_for("#")
```

## Module

| Call | Result |
|---|---|
| `tabs()` | every terminal tab |
| `current()` | the tab in use, or `None` |
| `connect(name)` | opens a connection by name or `Folder/Name` |
| `local_shell()` | opens a local shell |
| `connections()` | the tree's connections: `name`, `path`, `id` |
| `status(text)` | shows text in the status bar |
| `message(text)` | shows a message and waits |
| `confirm(text)` | asks yes or no |
| `prompt(text, default="", password=False)` | asks for text, `None` if cancelled |

## Tab

| Member | Meaning |
|---|---|
| `name`, `title`, `state`, `protocol` | `state` is `idle`, `connecting`, `connected` or `disconnected` |
| `send(text)` | types text as given |
| `send_line(text="")` | types text and Enter |
| `send_password(credential=None)` | answers a password prompt |
| `wait_for(pattern, timeout=30, regex=False)` | a `Match` with `text` and `before`, or `None` on timeout |
| `run(command, prompt, timeout=30, regex=False)` | sends a command and returns the output up to the prompt |
| `wait_connected(timeout=30)` | `True` once connected |
| `screen()` | all the terminal's text |
| `activate()`, `disconnect()`, `reconnect()`, `close()` | as in the tab's menu; `close()` does not ask |

Errors raise `sshlx.SshlxError`.

`wait_for` looks at what arrived since the last send, so a command's reply
can't slip past between `send_line` and `wait_for`, and an old prompt can't be
mistaken for the new one. Before the first send it starts from the line the
cursor is on, so a prompt already showing counts. Output up to a match is used
up. Patterns are PCRE, as in Qt.

## Recording

**Tools → Record Script**, or Record Script in the terminal menu, writes down
what you type as a script: each line waits for the prompt that was showing,
then sends the line. Choose the same entry again to stop and save.

A line typed at a password prompt is never written down. It becomes
`tab.send_password()`, which uses the tab's credential when the script runs;
add a credential's name if it needs a different one. This goes by what the
prompt says, so a prompt that doesn't mention a password or passphrase is not
recognised: check a recorded script before sharing it. Arrow keys are noted in
a comment rather than recorded.

## Logon scripts

Right-click a connection or folder → **Logon Actions** runs a script after the
logon steps, on each connect. There, `sshlx.current()` is the tab that logged
in, whichever tab is in front. If a step fails, the script doesn't run.
Importing a connection tree drops logon scripts, so that a file from elsewhere
can't run programs on your machine.

## Security

A script runs as you, with your files and network, so run only scripts you
trust. It cannot read a saved password: `send_password` passes it straight from
the keyring to the session, and refuses unless the cursor's line looks like a
password prompt. The screen can still show whatever the device echoes.

The connection to sshLX is a socket in a private temporary folder, with a
one-time token.
