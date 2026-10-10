"""Offline security tests for FalconOS host certificate-validated site gateway."""
import email.message
import pathlib
import socket
import sys
import unittest
import urllib.error
from unittest.mock import patch

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/"tools"))
import falcon_https_gateway as gateway

GLOBAL_RECORD=[(socket.AF_INET,socket.SOCK_STREAM,6,"",("1.1.1.1",443))]
PRIVATE_RECORD=[(socket.AF_INET,socket.SOCK_STREAM,6,"",("127.0.0.1",443))]


class Response:
    status=200
    def __init__(self,body=b"public FalconOS page"):
        self.body=body
    def read(self,cap):
        return self.body[:cap]
    def __enter__(self):
        return self
    def __exit__(self,*_):
        return False


class FakeOpener:
    def __init__(self,routes):
        self.routes=routes
        self.queries=[]
    def open(self,request,timeout=0):
        self.queries.append(request.full_url)
        response=self.routes[request.full_url]
        if isinstance(response,Exception):
            raise response
        return Response(response)


def redirect(url,location,code=301):
    headers=email.message.Message()
    headers["Location"]=location
    return urllib.error.HTTPError(url,code,"Moved",headers,None)


class GatewaySecurityTests(unittest.TestCase):
    def fetch(self,start,routes,resolver=None):
        instance=object.__new__(gateway.Handler)
        opener=FakeOpener(routes)
        with patch.object(gateway.socket,"getaddrinfo",
                          side_effect=resolver if resolver is not None else
                          lambda host,port,**_: GLOBAL_RECORD), \
             patch.object(gateway.urllib.request,"build_opener",return_value=opener):
            outcome=instance.validated_https_request(start,200)
        return outcome,opener.queries

    def test_successful_https(self):
        site=b"<h1>Real page</h1>"
        body,seen=self.fetch("https://falconos.tech/",{"https://falconos.tech/":site})
        self.assertEqual(body,site)
        self.assertEqual(seen,["https://falconos.tech/"])

    def test_same_site_https_redirect(self):
        start="https://falconos.tech/"
        end="https://www.falconos.tech/home"
        body,seen=self.fetch(start,{start:redirect(start,end),end:b"OK"})
        self.assertEqual(body,b"OK")
        self.assertEqual(seen,[start,end])

    def test_relative_redirect(self):
        start="https://falconos.tech/"
        end="https://falconos.tech/tr/"
        self.assertEqual(self.fetch(start,{start:redirect(start,"/tr/"),end:b"TR"})[0],b"TR")

    def test_http_downgrade_blocked(self):
        start="https://falconos.tech/"
        with self.assertRaisesRegex(ValueError,"unsafe"):
            self.fetch(start,{start:redirect(start,"http://falconos.tech/")})

    def test_private_redirect_blocked(self):
        start="https://falconos.tech/"
        target="https://metadata.internal/"
        def resolver(host,port,**_):
            return PRIVATE_RECORD if host=="metadata.internal" else GLOBAL_RECORD
        with self.assertRaisesRegex(ValueError,"non-public"):
            self.fetch(start,{start:redirect(start,target),target:b"SECRET"},resolver)

    def test_redirect_credentials_blocked(self):
        start="https://falconos.tech/"
        target="https://user:password@site.example/"
        with self.assertRaisesRegex(ValueError,"unsafe"):
            self.fetch(start,{start:redirect(start,target),target:b"secret"})

    def test_redirect_loop_blocked(self):
        a="https://falconos.tech/a"
        b="https://falconos.tech/b"
        with self.assertRaisesRegex(ValueError,"loop"):
            self.fetch(a,{a:redirect(a,b),b:redirect(b,a)})

    def test_http_403_is_failure_not_a_fake_success(self):
        url="https://falconos.tech/"
        with self.assertRaisesRegex(ValueError,"Upstream HTTP 403"):
            self.fetch(url,{url:redirect(url,"/denied",code=403)})


if __name__=="__main__":
    unittest.main()
