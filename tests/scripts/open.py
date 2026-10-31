import sshlx
t = sshlx.connect("script-dev")
assert t.wait_connected(5)
assert t.wait_for("R1#", 5)
assert "script-dev" in [c["name"] for c in sshlx.connections()]
t.close()
try:
    t.send("x")
except sshlx.SshlxError:
    pass
else:
    raise SystemExit(4)
try:
    sshlx.connect("no such thing")
except sshlx.SshlxError:
    pass
else:
    raise SystemExit(5)
