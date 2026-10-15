"""Falco V2: preserve real HTTPS page structure as bounded, inert HTML.

This is a *reader projection*, not source-authentic CSS/layout. All scripts,
event attributes, forms and active embeds are removed on the host. The FalconOS
native MiniWeb renderer draws the resulting real text and HTTPS links.
"""
from html import escape
from html.parser import HTMLParser
from urllib.parse import urlsplit

MAX_DOCUMENT_BYTES = 2850
CONTENT_NODES = {"h1","h2","h3","p","div","section","article","li",
                 "blockquote","label","a","button"}
SKIP_TAGS = {"script","style","svg","iframe","canvas","template","object",
             "embed","form","input","select","textarea","nav","footer",
             "noscript"}
SAFE_STYLE = ("color", "background-color", "font-size", "font-weight",
              "text-align")
BASIC_STYLE = (
    "body{background-color:#f6f9fe}"
    "h1{color:#174d91;font-size:34px}"
    "h2{color:#2661a7;font-size:27px}"
    "h3{color:#315781;font-size:23px}"
    "a{color:#125eb1;background-color:#e9f2ff;font-size:18px}"
    "p{color:#263449;font-size:16px}"
)
def _safe_href(raw: str) -> str:
    if not raw or len(raw)>155 or any(ord(c)<33 for c in raw):
        return ""
    if raw.startswith("https://"):
        parts=urlsplit(raw)
        host=parts.hostname or ""
        if not host or parts.username or parts.password or parts.port not in (None,443):
            return ""
        if any(not (x.isascii() and (x.isalnum() or x in ".-")) for x in host):
            return ""
        return raw
    if raw.startswith(("/", "./")) and not raw.startswith("//"):
        if ".." not in raw and ":" not in raw and "\\" not in raw:return raw
    if not raw.startswith(("#", "?", "/")) and ":" not in raw and ".." not in raw:
        return raw
    return ""

def _safe_style(value: str) -> str:
    rules=[]
    for decl in value[:350].split(";"):
        if ":" not in decl:continue
        key,val=[part.strip().lower() for part in decl.split(":",1)]
        if key not in SAFE_STYLE:continue
        if len(val)>28 or any(c not in "#(),.% -abcdefghijklmnopqrstuvwxyz0123456789"
                              for c in val):continue
        if "url(" in val or "expression" in val or "var(" in val:continue
        rules.append(key+":"+val)
        if len(rules)==5:break
    return ";".join(rules)

class _Collector(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.entries=[]
        self.open_tags=[]
        self.skip=0
        self.head=0
        self.title=""
        self.title_parts=[]
        self.main=0
        self.seq=0
    def handle_starttag(self,tag,attrs):
        tag=tag.lower()
        if tag=="head":
            self.head+=1
        if tag=="title":
            self.title_parts=[]
        if tag in SKIP_TAGS:self.skip+=1
        if tag=="main":self.main+=1
        if self.skip or self.head:return
        if tag in CONTENT_NODES:
            attrs=dict(attrs)
            href=_safe_href(attrs.get("href") or "") if tag=="a" else ""
            # Untrusted website scripts can never add real button actions.
            if tag=="button":tag="p"
            item={"tag":tag,"text":[],"href":href,
                  "style":_safe_style(attrs.get("style") or ""),
                  "main":self.main>0,"seq":self.seq}
            self.seq+=1
            self.entries.append(item)
            self.open_tags.append(item)
    def handle_startendtag(self,tag,attrs):
        self.handle_starttag(tag,attrs);self.handle_endtag(tag)
    def handle_endtag(self,tag):
        tag=tag.lower()
        if tag=="title":self.title="".join(self.title_parts).strip()[:85]
        if tag=="head":self.head=max(0,self.head-1)
        if tag in SKIP_TAGS:self.skip=max(0,self.skip-1)
        if tag=="main":self.main=max(0,self.main-1)
        if tag in CONTENT_NODES and self.open_tags:
            search="p" if tag=="button" else tag
            for index in range(len(self.open_tags)-1,-1,-1):
                if self.open_tags[index]["tag"]==search:
                    self.open_tags=self.open_tags[:index]
                    break
    def handle_data(self,data):
        if self.head and not self.skip and self.open_tags==[]:
            # Only explicit <title> text should populate title; this path
            # receives harmless head text which is omitted from the page.
            return
        if self.skip or self.head or not self.open_tags:return
        item=self.open_tags[-1]
        if len(item["text"])<40:item["text"].append(data[:350])

def project_safe_html(raw: bytes, *, max_bytes: int=MAX_DOCUMENT_BYTES) -> bytes:
    """Return a validated small HTML document that Falco draws itself.

    Large script-heavy sites are parsed first and only useful page content
    is emitted (up to the exact guest 4 KiB network reply budget).
    """
    if not 400<=max_bytes<=MAX_DOCUMENT_BYTES:
        raise ValueError("Invalid Falco document budget")
    parser=_Collector()
    parser.feed(raw[:60000].decode("utf-8","replace"))
    parser.close()
    entries=[]
    for item in parser.entries:
        cleaned=" ".join("".join(item["text"]).split()).strip()
        if not cleaned:continue
        # Cap each visible block to fit the software-native renderer.
        item["text"]=" ".join(cleaned.split())[:185]
        if len(item["text"])<3 and not item["href"]:continue
        entries.append(item)
    main=[item for item in entries if item["main"]]
    if sum(len(item["text"]) for item in main)>85:entries=main
    prefix=("<html><head><title>Falco V2 Reader</title><style>"+BASIC_STYLE+
            "</style></head><body>")
    suffix="</body></html>"
    output=prefix.encode("utf8")
    reserve=len(suffix.encode("utf8"))
    selected=0
    for item in entries[:150]:
        tag=item["tag"]
        parts=["<",tag]
        if item["href"]:parts.extend([' href="',escape(item["href"],quote=True),'"'])
        if item["style"]:parts.extend([' style="',escape(item["style"],quote=True),'"'])
        parts.extend([">",escape(item["text"]),"</",tag,">"])
        encoded="".join(parts).encode("utf8")
        if len(output)+len(encoded)+reserve>max_bytes:
            if selected:break
            # A long link can be skipped if a shorter useful block follows.
            continue
        output+=encoded;selected+=1
        if selected>=56:break
    if selected==0:
        return b""
    return output+suffix.encode("utf8")
