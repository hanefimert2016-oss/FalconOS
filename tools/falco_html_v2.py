"""Falco V2: preserve real HTTPS page structure as bounded, inert HTML.

This is a *reader projection*, not source-authentic CSS/layout. All scripts,
event attributes, forms and active embeds are removed on the host. The FalconOS
native MiniWeb renderer draws the resulting real text and HTTPS links.
"""
import re
from html import escape
from html.parser import HTMLParser
from urllib.parse import urlsplit

MAX_DOCUMENT_BYTES = 3200
CONTENT_NODES = {"h1","h2","h3","h4","h5","h6","p","div",
                 "section","article","li","blockquote","label","a",
                 "button","summary","dt","dd"}
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
        try:
            parts=urlsplit(raw)
            port=parts.port
        except ValueError:
            return ""
        host=parts.hostname or ""
        if not host or parts.username or parts.password or port not in (None,443):
            return ""
        if any(not (x.isascii() and (x.isalnum() or x in ".-")) for x in host):
            return ""
        return raw
    if raw.startswith(("/", "./")) and not raw.startswith("//"):
        if ".." not in raw and ":" not in raw and "\\" not in raw:return raw
    if not raw.startswith(("#", "?", "/")) and ":" not in raw and ".." not in raw:
        return raw
    return ""

def _safe_ident(value: str) -> str:
    """Only CSS identifiers supported by guest MiniWeb, no selectors/code."""
    v=(value or "").strip()
    return v if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_-]{0,23}",v) else ""

def _sample_site_css(styles: list[str], entries: list[dict]) -> str:
    """Extract only flat site CSS rules actually usable by native MiniWeb.

    CSS @rules/combinators, url(), expressions and executable material are
    intentionally not supported. All outputs are selected by an allowlist.
    """
    useful={"body","h1","h2","h3","p","a","li","div","blockquote"}
    for item in entries:
        if item.get("id"):useful.add("#"+item["id"])
        if item.get("klass"):useful.add("."+item["klass"])
    output=[]
    seen=set()
    raw="".join(styles)[:12000]
    raw=re.sub(r"/\*.*?\*/","",raw,flags=re.S)
    for match in re.finditer(r"([^{}]{1,90})\{([^{}]{1,450})\}",raw):
        selectors=[s.strip() for s in match.group(1).split(",")]
        valid=[s for s in selectors if s in useful and s not in seen]
        if not valid:continue
        style=_safe_style(match.group(2))
        if not style:continue
        for sel in valid[:2]:
            result=sel+"{"+style+"}"
            if sum(map(len,output))+len(result)>390:return "".join(output)
            output.append(result)
            seen.add(sel)
            if len(output)>=7:return "".join(output)
    return "".join(output)

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
        self.in_title=False
        self.main=0
        self.seq=0
        self.images=0
        self.styles=[]
        self.style_depth=0
    def handle_starttag(self,tag,attrs):
        tag=tag.lower()
        if tag=="head":
            self.head+=1
        if tag=="style":
            self.style_depth+=1
            if len(self.styles)<10:self.styles.append("")
        if tag=="title":
            self.title_parts=[]
            self.in_title=True
        # HTML void tags have no matching end tag; never let <input> or
        # <embed> permanently hide following legitimate body content.
        if tag in SKIP_TAGS and tag not in {"input","embed"}:
            self.skip+=1
        if tag=="main":self.main+=1
        if self.skip or self.head:return
        attrs=dict(attrs)
        if tag=="img":
            # Images are NOT decoded in native FalconOS. Surface only
            # meaningful *actual alt text*, explicitly marked as a caption.
            alt=" ".join((attrs.get("alt") or "").split())[:90]
            if self.open_tags and self.open_tags[-1]["tag"]=="a":
                if alt and not self.open_tags[-1]["text"]:
                    self.open_tags[-1]["text"].append(alt)
            elif alt and len(alt)>=6 and self.images<5:
                self.images+=1
                self.entries.append({"tag":"p","text":["[Image: "+alt+"]"],
                     "href":"","style":"color:#5c687b",
                     "main":self.main>0,"seq":self.seq})
                self.seq+=1
            return
        if tag in CONTENT_NODES:
            href=_safe_href(attrs.get("href") or "") if tag=="a" else ""
            # No untrusted site may create native interactive JS buttons.
            rendered="h3" if tag in {"h4","h5","h6"} else (
                "p" if tag in {"button","summary","dt","dd"} else tag)
            label=" ".join((attrs.get("aria-label") or "").split())[:90]
            klass=next((_safe_ident(k) for k in
                (attrs.get("class") or "").split()
                if _safe_ident(k)),"")
            item={"tag":rendered,"source_tag":tag,
                  "text":[],"label":label,"href":href,
                  "id":_safe_ident(attrs.get("id") or ""),"klass":klass,
                  "style":_safe_style(attrs.get("style") or ""),
                  "main":self.main>0,"seq":self.seq}
            self.seq+=1
            self.entries.append(item)
            self.open_tags.append(item)
    def handle_startendtag(self,tag,attrs):
        self.handle_starttag(tag,attrs);self.handle_endtag(tag)
    def handle_endtag(self,tag):
        tag=tag.lower()
        if tag=="title":
            self.title="".join(self.title_parts).strip()[:85]
            self.in_title=False
        if tag=="head":self.head=max(0,self.head-1)
        if tag=="style":self.style_depth=max(0,self.style_depth-1)
        if tag in SKIP_TAGS and tag not in {"input","embed"}:
            self.skip=max(0,self.skip-1)
        if tag=="main":self.main=max(0,self.main-1)
        if tag in CONTENT_NODES and self.open_tags:
            for index in range(len(self.open_tags)-1,-1,-1):
                if self.open_tags[index]["source_tag"]==tag:
                    self.open_tags=self.open_tags[:index]
                    break
    def handle_data(self,data):
        if self.style_depth and self.styles:
            if len(self.styles[-1])<2000:
                self.styles[-1]+=data[:2000-len(self.styles[-1])]
            return
        if self.in_title and not self.skip:
            self.title_parts.append(data[:120])
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
        if not cleaned:
            cleaned=item.get("label","") if item["tag"]=="a" else ""
        if not cleaned:continue
        # Cap each visible block to fit the software-native renderer.
        item["text"]=" ".join(cleaned.split())[:185]
        if len(item["text"])<3 and not item["href"]:continue
        entries.append(item)
    main=[item for item in entries if item["main"]]
    if sum(len(item["text"]) for item in main)>85:entries=main
    title=escape(parser.title or "Falco V2 Reader")[:90]
    site_css=_sample_site_css(parser.styles,entries)
    prefix=("<html><head><title>"+title+"</title><style>"+BASIC_STYLE+
            site_css+"</style></head><body>")
    suffix="</body></html>"
    output=prefix.encode("utf8")
    reserve=len(suffix.encode("utf8"))
    selected=0
    for item in entries[:150]:
        tag=item["tag"]
        parts=["<",tag]
        if item["href"]:parts.extend([' href="',escape(item["href"],quote=True),'"'])
        if item.get("id"):parts.extend([' id="',item["id"],'"'])
        if item.get("klass"):parts.extend([' class="',item["klass"],'"'])
        if item["style"]:parts.extend([' style="',escape(item["style"],quote=True),'"'])
        parts.extend([">",escape(item["text"],quote=False),"</",tag,">"])
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
