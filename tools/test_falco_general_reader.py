#!/usr/bin/env python3
"""Test original Falco read-only HTML reader against real public HTTPS sites.
This is NOT a JS/CSS compatibility test. Chromium/WebKit are not involved.
Checks redirect/public DNS safety and real certificate-authenticated content.
"""
import json,os,subprocess,sys,time,urllib.error,urllib.request
from pathlib import Path
BASE="http://127.0.0.1:18446/falcon/native/"
RESULT={"status":"RUNNING","checked":[],"blocked_private":False}
p=subprocess.Popen([sys.executable,"-u","tools/falco_native_gateway.py",
                    "--bind","127.0.0.1"],stdout=subprocess.PIPE,
                   stderr=subprocess.STDOUT)
try:
    html_ok=False
    for host in ("example.org","www.python.org"):
        try:
            with urllib.request.urlopen(BASE+host+"/",timeout=24) as r:
                blob=r.read(3500)
                assert r.status==200
                assert r.headers["X-Falco-Native-Verified"]=="yes"
                assert r.headers["X-Falco-Source"]==host
                assert blob.startswith(("FALCO/1\nSOURCE|"+host+"\n").encode())
                assert b"T|" in blob
                RESULT["checked"].append({"site":host,"bytes":len(blob)})
                html_ok=True
                break
        except Exception as error:
            RESULT["checked"].append({"site":host,"error":repr(error)})
            time.sleep(.5)
    if not html_ok:raise AssertionError("No general real HTML site loaded through CA-validated reader")
    try:
        urllib.request.urlopen(BASE+"127.0.0.1/",timeout=7)
        raise AssertionError("Private loopback URL was unexpectedly fetched")
    except urllib.error.HTTPError as error:
        assert error.code in (400,502),error.code
        RESULT["blocked_private"]=True
    RESULT["status"]="PASS"
finally:
    p.terminate()
    try:p.wait(timeout=4)
    except subprocess.TimeoutExpired:p.kill()
    Path("build/falco-general-html-results.json").write_text(json.dumps(RESULT,indent=2))
print(RESULT)
