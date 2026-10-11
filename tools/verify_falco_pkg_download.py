#!/usr/bin/env python3
"""Check live GitHub release bytes through Falco's non-Chromium HTTPS-only
app.pkg download companion (no fabricated download, no execution).
"""
import hashlib,subprocess,sys,time,urllib.request,urllib.error,json
from pathlib import Path
B=Path("build");B.mkdir(exist_ok=True)
LOG=B/"falco-pkg-https-host.log";REPORT=B/"falco-pkg-https-results.json"
url=("http://127.0.0.1:18444/download/github.com/"
     "hanefimert2016-oss/FalconOS-Marketplace/releases/download/"
     "app-hello-world-v1.0.0/hello-world-v1.0.0.app.pkg")
result={"result":"RUNNING","source":url,"checks":[]}
with LOG.open("wb") as out:
    proc=subprocess.Popen([sys.executable,"-u","tools/falcon_https_gateway.py",
                           "--bind","127.0.0.1"],stdout=out,stderr=subprocess.STDOUT)
    try:
        for n in range(55):
            try:
                with urllib.request.urlopen(url,timeout=24) as reply:
                    data=reply.read()
                    assert reply.status==200
                    assert reply.headers["X-Falcon-Pkg-TLS"]=="host-ca-verified"
                    assert data.startswith(b"FAPP/1\nid=hello-world\n")
                    assert len(data)<4096
                    checksum=hashlib.sha256(data).hexdigest()
                    result["sha256"]=checksum
                    result["byte_length"]=len(data)
                    assert checksum=="eb045bbfa6feed47ff21adbca4af4af256fcc744c7799f1e21f56461dbe743ad",checksum
                    result["checks"].append("Real public GitHub .app.pkg via validated HTTPS redirected asset")
                    result["checks"].append("Actual publisher SHA256 matches known release")
                    result["result"]="PASS"
                    break
            except (urllib.error.URLError,ConnectionError) as e:
                if n>0:raise
                time.sleep(.3)
        if result["result"]!="PASS":raise RuntimeError("HTTPS package request failed")
    finally:
        proc.terminate()
        try:proc.wait(timeout=4)
        except subprocess.TimeoutExpired:proc.kill()
        REPORT.write_text(json.dumps(result,indent=2))
print(result)
