#!/usr/bin/env python3
"""A pretend IOS device on stdin/stdout, run as the shell of a local session.
Logs each line it receives to $FAKE_IOS_LOG."""
import os
import sys
import termios
import time
import tty

log = open(os.environ["FAKE_IOS_LOG"], "a", buffering=1)
mode = "R1#"


def out(text):
    sys.stdout.write(text)
    sys.stdout.flush()


while True:
    out(mode)
    line = sys.stdin.readline()
    if not line:
        break
    line = line.rstrip("\r\n")
    log.write(line + "\n")
    if line == "configure terminal":
        out("Enter configuration commands, one per line.  End with CNTL/Z.\r\n")
        mode = "R1(config)#"
    elif line.startswith("interface "):
        mode = "R1(config-if)#"
    elif line == "end":
        mode = "R1#"
    elif line.startswith("interfce"):
        out("          ^\r\n% Invalid input detected at '^' marker.\r\n\r\n")
    elif line.startswith("banner motd"):
        out("Enter TEXT message.  End with the character '^'.\r\n")
        while "^" not in sys.stdin.readline():
            pass
    elif line == "write memory":
        out("Building configuration...\r\n")
        time.sleep(0.4)
        out("[OK]\r\n")
    elif line.startswith("flood"):
        for i in range(int(line.split()[1]) if " " in line else 300):
            out(f"needle-{i} filler\r\n")
    elif line == "sudo-test":
        out("[sudo: authenticate] Password: ")
        saved = termios.tcgetattr(0)
        hidden = termios.tcgetattr(0)
        hidden[3] &= ~termios.ECHO
        termios.tcsetattr(0, termios.TCSANOW, hidden)
        log.write("secret=" + sys.stdin.readline().rstrip("\r\n") + "\n")
        termios.tcsetattr(0, termios.TCSANOW, saved)
        out("\r\nauthenticated\r\n")
    elif line == "show version":
        out("Cisco IOS, see https://example.com/ios. for details\r\n")
        out("line 1\r\n --More-- ")
        saved = termios.tcgetattr(0)
        tty.setcbreak(0)        # a pager reads single keys, not lines
        os.read(0, 1)
        termios.tcsetattr(0, termios.TCSANOW, saved)
        out("\r          \rline 2\r\n")
