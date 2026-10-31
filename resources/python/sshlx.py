"""Control sshLX from a Python script.

Scripts are started from sshLX (Tools > Run Script, or a button), which gives
this module the way back to it. See docs/SCRIPTING.md.

    import sshlx

    tab = sshlx.current()
    tab.send_line("show version")
    tab.wait_for("#")
"""
import json
import os
import socket

__all__ = ["SshlxError", "Tab", "Match", "tabs", "current", "connect", "local_shell",
           "connections", "status", "message", "confirm", "prompt"]


class SshlxError(Exception):
    """sshLX refused or could not do what was asked."""


class _Link:
    def __init__(self):
        path = os.environ.get("SSHLX_SCRIPT_SOCKET")
        if not path:
            raise SshlxError("this script must be started from sshLX")
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.connect(path)
        self.file = self.sock.makefile("rwb")
        self.next_id = 0
        self.call("hello", token=os.environ.get("SSHLX_SCRIPT_TOKEN", ""))

    def call(self, method, **params):
        self.next_id += 1
        self.file.write(json.dumps({"id": self.next_id, "method": method,
                                    "params": params}).encode() + b"\n")
        self.file.flush()
        line = self.file.readline()
        if not line:
            raise SshlxError("sshLX went away")
        reply = json.loads(line)
        if "error" in reply:
            raise SshlxError(reply["error"])
        return reply.get("result")


_link = None


def _call(method, **params):
    global _link
    if _link is None:
        _link = _Link()
    return _link.call(method, **params)


class Match:
    """What wait_for() found: the matched text and what came before it."""

    def __init__(self, data):
        self.text = data["text"]
        self.before = data["before"]

    def __repr__(self):
        return "Match(%r)" % self.text


class Tab:
    """One terminal tab."""

    def __init__(self, info):
        self.id = info["id"]
        self.name = info["name"]

    def __repr__(self):
        return "Tab(%r)" % self.name

    def _info(self):
        return _call("tab_info", tab=self.id)

    @property
    def title(self):
        return self._info()["title"]

    @property
    def state(self):
        """"idle", "connecting", "connected" or "disconnected"."""
        return self._info()["state"]

    @property
    def protocol(self):
        return self._info()["protocol"]

    def wait_connected(self, timeout=30):
        """Wait for the connection, False if it does not come up in time."""
        return _call("wait_connected", tab=self.id, timeout=timeout)

    def send(self, text):
        """Type text, exactly as given."""
        _call("send", tab=self.id, text=text)

    def send_line(self, text=""):
        """Type text followed by Enter."""
        _call("send", tab=self.id, text=text + "\r")

    def send_password(self, credential=None):
        """Answer a password prompt from the Credentials Manager.

        The password goes from the keyring to the session and never reaches
        the script. It is refused unless the last line looks like a password
        prompt. Without a name, uses the credential linked to this tab.
        """
        _call("send_password", tab=self.id, credential=credential)

    def wait_for(self, pattern, timeout=30, regex=False):
        """Wait for text in the output.

        Looks at what arrived since the last send, or for a script that has
        sent nothing yet, at the line the cursor was on and what came after
        it. Returns a Match, or None after timeout seconds. Output up to the
        match is used up.
        """
        found = _call("wait_for", tab=self.id, pattern=pattern, timeout=timeout, regex=regex)
        return Match(found) if found else None

    def run(self, command, prompt, timeout=30, regex=False):
        """Send a command, wait for the prompt, return the output between."""
        self.send_line(command)
        found = self.wait_for(prompt, timeout, regex)
        if found is None:
            raise SshlxError("%r did not appear after %r" % (prompt, command))
        lines = found.before.split("\n")
        return "\n".join(lines[1:]) if len(lines) > 1 else ""

    def screen(self):
        """All text of the terminal, scrollback included."""
        return _call("screen", tab=self.id)

    def activate(self):
        _call("activate", tab=self.id)

    def disconnect(self):
        _call("disconnect", tab=self.id)

    def reconnect(self):
        _call("reconnect", tab=self.id)

    def close(self):
        """Close the tab, without asking."""
        _call("close", tab=self.id)


def tabs():
    """Every terminal tab, in the order shown."""
    return [Tab(t) for t in _call("tabs")]


def current():
    """The tab in use, or None. In a logon script, the tab that logged in."""
    info = _call("current")
    return Tab(info) if info else None


def connect(name):
    """Open a connection from the tree, by name or Folder/Name, and return its tab."""
    return Tab(_call("open", name=name))


def local_shell():
    """Open a local shell and return its tab."""
    return Tab(_call("local_shell"))


def connections():
    """The connections in the tree: a list of dicts with name, path and id."""
    return _call("connections")


def status(text):
    """Show text in the status bar."""
    _call("status", text=text)


def message(text, title="Script"):
    """Show a message and wait for OK."""
    _call("message", text=text, title=title)


def confirm(text, title="Script"):
    """Ask a yes or no question."""
    return _call("confirm", text=text, title=title)


def prompt(text, default="", title="Script", password=False):
    """Ask for text; None if cancelled."""
    return _call("prompt", text=text, default=default, title=title, password=password)
