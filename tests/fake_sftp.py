#!/usr/bin/env python3
"""A small SFTP v3 server on stdin/stdout, rooted at argv[1]. Answers reads
short and lists hostile names, to keep the client honest."""
import os
import struct
import sys

root = sys.argv[1]
inp, out = sys.stdin.buffer, sys.stdout.buffer
handles = {}
next_handle = 0


def read_exact(n):
    b = b""
    while len(b) < n:
        c = inp.read(n - len(b))
        if not c:
            sys.exit(0)
        b += c
    return b


class R:
    def __init__(self, d):
        self.d, self.p = d, 0

    def u32(self):
        v = struct.unpack(">I", self.d[self.p:self.p + 4])[0]
        self.p += 4
        return v

    def u64(self):
        v = struct.unpack(">Q", self.d[self.p:self.p + 8])[0]
        self.p += 8
        return v

    def s(self):
        n = self.u32()
        v = self.d[self.p:self.p + n]
        self.p += n
        return v


def packet(t, body):
    out.write(struct.pack(">IB", len(body) + 1, t) + body)
    out.flush()


def st(s):
    return struct.pack(">I", len(s)) + s


def status(i, code, msg=b""):
    packet(101, struct.pack(">II", i, code) + st(msg) + st(b""))


def attrs(path):
    s = os.lstat(path)
    return struct.pack(">IQII", 1 | 4 | 8, s.st_size, 0, 0)[:12] + \
        struct.pack(">III", s.st_mode, int(s.st_mtime), int(s.st_mtime))


def local(p):
    return os.path.join(root, p.decode().lstrip("/"))


while True:
    n = struct.unpack(">I", read_exact(4))[0]
    r = R(read_exact(n))
    t = r.d[0]
    r.p = 1
    if t == 1:
        packet(2, struct.pack(">I", 3))
        continue
    i = r.u32()
    try:
        if t == 16:
            r.s()
            packet(104, struct.pack(">II", i, 1) + st(b"/") + st(b"/") + struct.pack(">I", 0))
        elif t == 11:
            p = local(r.s())
            names = os.listdir(p)
            handles[next_handle] = ["dir", p, names]
            packet(102, struct.pack(">I", i) + st(str(next_handle).encode()))
            next_handle += 1
        elif t == 12:
            h = handles[int(r.s())]
            if not h[2]:
                status(i, 1)
                continue
            names, h[2] = h[2], []
            body = b""
            for name in names:
                b = name.encode()
                body += st(b) + st(b) + attrs(os.path.join(h[1], name))
            evil = [b"..", b".", b"../evil", b"a/b"]
            for b in evil:
                body += st(b) + st(b) + attrs(h[1])
            packet(104, struct.pack(">II", i, len(names) + len(evil)) + body)
        elif t == 3:
            p = local(r.s())
            flags = r.u32()
            f = open(p, "r+b" if os.path.exists(p) and flags & 2 else "wb" if flags & 2 else "rb")
            if flags & 16:
                f.truncate()
            handles[next_handle] = ["file", f, None]
            packet(102, struct.pack(">I", i) + st(str(next_handle).encode()))
            next_handle += 1
        elif t == 5:
            f = handles[int(r.s())][1]
            off, want = r.u64(), r.u32()
            f.seek(off)
            data = f.read(min(want, want // 2 + 7))
            if not data:
                status(i, 1)
            else:
                packet(103, struct.pack(">I", i) + st(data))
        elif t == 6:
            f = handles[int(r.s())][1]
            off = r.u64()
            f.seek(off)
            f.write(r.s())
            status(i, 0)
        elif t == 4:
            h = handles.pop(int(r.s()))
            if h[0] == "file":
                h[1].close()
            status(i, 0)
        elif t == 17:
            p = local(r.s())
            packet(105, struct.pack(">I", i) + attrs(p))
        elif t == 14:
            os.mkdir(local(r.s()))
            status(i, 0)
        elif t == 15:
            os.rmdir(local(r.s()))
            status(i, 0)
        elif t == 13:
            os.remove(local(r.s()))
            status(i, 0)
        elif t == 18:
            os.rename(local(r.s()), local(r.s()))
            status(i, 0)
        else:
            status(i, 8)
    except FileNotFoundError:
        status(i, 2, b"No such file")
    except OSError as e:
        status(i, 4, str(e).encode())
