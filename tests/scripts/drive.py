import os

import sshlx
tab = sshlx.current()
res = {}
res["tabs"] = len(sshlx.tabs())
res["state"] = tab.state
tab.send_line("show ip route")
found = tab.wait_for("R1#", timeout=5)
res["found"] = found.text
res["missing"] = tab.wait_for("will never appear", timeout=0.2) is None
res["run"] = tab.run("show clock", "R1#", timeout=5)
tab.send_line("show re")
res["regex"] = tab.wait_for(r"R\d#", timeout=5, regex=True).text
try:
    tab.send_password("nothing")
except sshlx.SshlxError as e:
    res["refused"] = str(e)
try:
    tab.send_password()
except sshlx.SshlxError as e:
    res["refused2"] = str(e)
res["screen"] = "show ip route" in tab.screen()
sshlx.status("script ok")
print("hello from the script")
import json
open(os.environ["SSHLX_TEST_OUT"], "w").write(json.dumps(res))
