# PuTTY overrides

sshLX compiles PuTTY 0.85 from `../putty-0.85` **unchanged**, with the
exceptions listed here (rule 5.3: last resort only). Each override is a
copy of the original file at `src/putty-overrides/<same relative path>`,
starting with a header comment that names the original, the PuTTY version
and the reason. `cmake/putty-sources.cmake` picks overrides up
automatically, and the configure log prints the list
(`sshLX: PuTTY overrides in use: ...`).

The PuTTY tree itself is never modified; `tools/verify-putty-untouched.sh`
checks this (also run as the `putty-untouched` test).

| File | PuTTY version | Change | Diff |
|---|---|---|---|
| `unix/serial.c` | 0.85 | (1) remove closed ports from the static fd index; (2) serial I/O errors end the session instead of calling `exit(1)`; (3) no `uxsel_set(-1)` after close | [`docs/overrides/unix-serial.c.diff`](docs/overrides/unix-serial.c.diff) |

## `unix/serial.c`

**Problem (blocks multiple sessions per process).** The serial back end
keeps a file-static `tree234 *serial_by_fd` that maps file descriptors to
open `Serial` back ends; `serial_select_result()` uses it to find which
back end an fd event belongs to. Entries are added in `serial_init()` but
never removed: `serial_close()` sets `serial->fd = -1` while the struct is
still in the tree (changing its sort key), and `serial_free()` then frees
the struct, leaving a dangling node in the shared tree.

In PuTTY this is harmless, because there is one session per process. In
sshLX, after one serial tab is closed or its device disappears, lookups for
the *other* serial sessions can fail. `serial_select_result()` then treats
their input as spurious, and those tabs stop receiving data while their fds
stay readable, so the process spins. This breaks hard requirement 3
("closing one tab never affects other tabs").

The tree is `static` inside `serial.c`, so the problem cannot be fixed from
sshLX's front-end code.

**Change 1 (stale index).** `serial_close()` now removes the back end
from `serial_by_fd` before invalidating its fd:

```c
static void serial_close(Serial *serial)
{
    if (serial->fd >= 0) {
        /* sshLX: keep the fd index consistent; see header comment. */
        del234(serial_by_fd, serial);
        uxsel_del(serial->fd);
        close(serial->fd);
        serial->fd = -1;
    }
}
```

`serial_free()` always calls `serial_close()`, so freed back ends are never
left in the tree.

**Problem 2 (process exit on I/O error).** On a read error other than
`EAGAIN`, and on any write error, `serial_select_result()` /
`serial_try_write()` call `perror()` then `exit(1)`. In PuTTY that closes
the single session. In sshLX it closes every tab. One easy trigger is
pulling a USB serial adapter and typing into its tab before the hang-up is
noticed: the write gets `EIO`.

**Change 2.** These two sites now call a new static helper,
`serial_io_error()`. It closes the port, marks the back end finished, and
reports `seat_connection_fatal(seat, "Reading/Writing serial port:
<strerror>")`. Only that tab ends, with PuTTY's usual fatal-error path, and
sshLX shows it in the tab's reconnect bar.

**Problem 3 / Change 3 (assertion after close).** After the port closes,
which already happens on a normal EOF, a late terminal unthrottle calls
`serial_uxsel_setup()` → `uxsel_set(fd = -1, ...)`, which asserts
`fd >= 0`, and PuTTY's assertions are fatal. `serial_uxsel_setup()` now
returns early when the port is closed.

There is no behaviour change for a single healthy session.

**Tests.** `tests/engine_sessions_test.c`:
- *serial close isolation*: opens 10 serial sessions on pseudo-terminals,
  closes 3, and checks the remaining ones and an unrelated earlier session
  still receive data. With PuTTY's original `serial.c` this fails
  (`timed out waiting for "after-close"`).
- *serial I/O error isolation*: closes a device's far end, immediately sends
  a keystroke (`EIO` on write), and checks that this session ends with
  "Writing serial port: Input/output error" while another session keeps
  working. The original code would `exit(1)` here.

**On upgrade.** Check whether a newer PuTTY removes the entry in
`serial_close()`/`serial_free()` and no longer exits on serial I/O errors.
Drop whichever changes become unnecessary, or the whole override.
