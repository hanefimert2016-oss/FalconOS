"""Falco V2 browser projection: no fake pixels, no page-script execution."""
import io
import pathlib
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/"tools"))
from falco_html_v2 import project_safe_html,MAX_DOCUMENT_BYTES
import falcon_https_gateway as gateway

class FalcoV2SafeHTMLTests(unittest.TestCase):
    def test_preserves_real_page_headings_links_and_inline_css(self):
        html=(b"<html><head><title>Example</title>"
              b"<script>window.evil=1</script></head><body>"
              b"<main><h1 style='color:#1255aa;font-size:30px'>Real heading</h1>"
              b"<p>Paragraph <strong>and emphasis</strong></p>"
              b"<a href='/guide' onclick='alert(1)'>Read more</a>"
              b"</main></body></html>")
        out=project_safe_html(html).decode()
        self.assertIn("<h1 style=",out)
        self.assertIn("Real heading</h1>",out)
        self.assertIn("Paragraph and emphasis</p>",out)
        self.assertIn('href="/guide"',out)
        self.assertIn("Read more</a>",out)
        for forbidden in ("window.evil","onclick","<script","<iframe"):
            self.assertNotIn(forbidden,out)
    def test_active_payload_removed_and_unsafe_links_ignored(self):
        raw=b"""<html><body><main><h1>Docs</h1>
         <script>alert('s')</script><svg>SVG SECRET</svg>
         <p>Useful text</p>
         <a href="javascript:alert(1)">Do not execute</a>
         <a href="https://example.com/docs">Documentation</a>
         <a href="http://insecure.example/">http link</a>
         <form><input value="PASSWORD"></form>
         </main></body></html>"""
        out=project_safe_html(raw).decode()
        self.assertIn("Docs</h1>",out)
        self.assertIn('href="https://example.com/docs"',out)
        self.assertIn("Useful text",out)
        for forbidden in ("javascript:","http://","<script","SVG SECRET",
                          "<form","PASSWORD","onclick"):
            self.assertNotIn(forbidden,out)
    def test_main_content_preferred_over_navigation(self):
        html=b"<html><body><div>Menu menu menu</div><main><h1>Long headline</h1><p>"+(
            b"Important article body "*7)+b"</p></main></body></html>"
        out=project_safe_html(html).decode()
        self.assertIn("Important article body",out)
        self.assertNotIn("Menu menu",out)
    def test_budget_and_encoded_entities(self):
        html=("<html><body><main><h1>Real title</h1>"+
              "".join("<p>"+("G\"& < ü "*120)+"</p>" for _ in range(100))+
              "</main></body></html>").encode()
        out=project_safe_html(html)
        self.assertLessEqual(len(out),MAX_DOCUMENT_BYTES)
        self.assertIn(b"&amp;",out)
        self.assertIn(b"&lt;",out)
        self.assertEqual(out.decode("utf8").count("<html>"),1)
        self.assertTrue(out.endswith(b"</body></html>"))
    def test_empty_or_only_script_page_no_fake_page(self):
        self.assertEqual(project_safe_html(b"<html><script>evil()</script></html>"),b"")
        self.assertRaises(ValueError,project_safe_html,b"<h1>x</h1>",max_bytes=10000)
    def test_accessible_image_and_icon_links_without_fake_pixels(self):
        raw=(b"<html><body><main>"
             b"<h2>Illustrated story</h2>"
             b"<a aria-label='Read the full article' href='/article'><svg>"
             b"<path d='...'/></svg></a>"
             b"<a href='https://example.com/gallery'>"
             b"<img src='/image.webp' alt='Gallery photographs'/></a>"
             b"<img src='/photo.png' alt='Rocket launch platform'/>"
             b"</main></body></html>")
        out=project_safe_html(raw).decode()
        self.assertIn(">Read the full article</a>",out)
        self.assertIn(">Gallery photographs</a>",out)
        self.assertIn("[Image: Rocket launch platform]",out)
        self.assertNotIn("<img",out)
        self.assertNotIn("<svg",out)
        self.assertNotIn("src=",out)

    def test_whitelisted_real_stylesheet_selectors(self):
        raw=(b"<html><head><style>"
             b"h1 { color: #2266bb; font-size: 28px; }"
             b".hero{color:rgb(10,20,30);}"
             b"a:hover{color:red}"
             b"@import url('https://evil.test/style');"
             b"</style><title>Actual CSS page</title></head>"
             b"<body><main><h1 class='hero' id='welcome'>Headline</h1>"
             b"<a href='/guide'>Guide</a></main></body></html>")
        out=project_safe_html(raw).decode()
        self.assertIn("<title>Actual CSS page</title>",out)
        self.assertIn("h1{color:#2266bb;font-size:28px}",out)
        self.assertIn(".hero{color:rgb(10,20,30)}",out)
        self.assertIn('class="hero"',out)
        self.assertIn('id="welcome"',out)
        self.assertNotIn("hover",out)
        self.assertNotIn("@import",out)
        self.assertNotIn("evil.test",out)

    def test_unsafe_css_properties_and_id_injection_not_projected(self):
        raw=(b"<html><head><style>"
             b"#safe{background:url('http://bad.test/pixel');color:#123456;}"
             b"#x>script{color:red}</style></head>"
             b"<body><p id='safe' style='position:absolute;opacity:0;"
             b"background-image:url(javascript:evil);color:#456789'>Text</p>"
             b"<p id='x\" onclick=\"evil()'>Other</p></body></html>")
        out=project_safe_html(raw).decode()
        self.assertIn("color:#456789",out)
        for token in ("position:absolute","opacity:0","javascript:","onclick=",
                      "bad.test","<script"):
            self.assertNotIn(token,out)

    def test_guest_http_reply_contains_projected_html(self):
        handler=object.__new__(gateway.Handler)
        handler.path="/fetch/example.com/"
        handler.client_address=("127.0.0.1",45555)
        handler.wfile=io.BytesIO()
        headers={}
        handler.send_response=lambda status:headers.update(status=status)
        handler.send_header=lambda key,value:headers.update({key:value})
        handler.end_headers=lambda:None
        markup=b"<html><body><h1>Real Page</h1><p>Working HTML projection</p></body></html>"
        with patch.object(handler,"validated_https_request",return_value=markup) as fetch:
            handler.do_GET()
        self.assertEqual(headers["status"],200)
        self.assertEqual(headers["X-Falcon-Document-Mode"],"SafeHTML-V2")
        self.assertEqual(headers["X-Falcon-Host-HTTPS-Verified"],"yes")
        self.assertIn(b"<h1",handler.wfile.getvalue())
        self.assertIn(b"Real Page",handler.wfile.getvalue())
        fetch.assert_called_once()

if __name__=="__main__":unittest.main()
