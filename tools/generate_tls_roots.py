#!/usr/bin/env python3
"""Build-time BearSSL X.509 trust anchor compiler.
Reads an explicit PEM CA bundle (never a network URI); emits C source for
immutable root names and RSA/EC public keys. Cryptography is a BUILD tool,
not part of the FalconOS kernel.
"""
import argparse
import pathlib
import re
from cryptography import x509
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.hazmat.primitives import serialization

def c_array(name,blob):
    return "static unsigned char %s[]={%s};\n"%(name,",".join("0x%02x"%b for b in blob))
def num_bytes(n):
    return n.to_bytes(max(1,(n.bit_length()+7)//8),"big")
def compile_anchors(pem, output):
    matches=re.findall(rb"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",pem,re.S)
    generated=['/* Generated trust anchors. Do not edit; input must be audited CA bundle. */',
               '#include "bearssl.h"']
    records=[]
    for pem_cert in matches:
        cert=x509.load_pem_x509_certificate(pem_cert)
        try:
            bc=cert.extensions.get_extension_for_class(x509.BasicConstraints).value
            if not bc.ca:continue
        except x509.ExtensionNotFound:
            continue
        key=cert.public_key()
        index=len(records)
        prefix=f"falcon_ta_{index}"
        dn=cert.subject.public_bytes()
        generated.append(c_array(prefix+"_dn",dn))
        key_field=None
        if isinstance(key,rsa.RSAPublicKey):
            parts=key.public_numbers()
            n=num_bytes(parts.n);e=num_bytes(parts.e)
            generated.append(c_array(prefix+"_n",n))
            generated.append(c_array(prefix+"_e",e))
            key_field=f"{{BR_KEYTYPE_RSA,{{.rsa={{ {prefix}_n,sizeof({prefix}_n),{prefix}_e,sizeof({prefix}_e) }} }} }}"
        elif isinstance(key,ec.EllipticCurvePublicKey):
            curve={"secp256r1":23,"secp384r1":24,"secp521r1":25}.get(key.curve.name)
            if curve is None: continue
            point=key.public_bytes(serialization.Encoding.X962,
                                   serialization.PublicFormat.UncompressedPoint)
            generated.append(c_array(prefix+"_q",point))
            key_field=f"{{BR_KEYTYPE_EC,{{.ec={{ {curve},{prefix}_q,sizeof({prefix}_q) }} }} }}"
        else:
            continue
        records.append(f"{{ {{ {prefix}_dn,sizeof({prefix}_dn) }},BR_X509_TA_CA,{key_field} }}")
    if not records:
        raise RuntimeError("CA bundle has no compatible RSA/EC trust anchor")
    generated.append("const br_x509_trust_anchor falcon_tls_anchors[]={\n"+",\n".join(records)+"\n};")
    generated.append(f"const size_t falcon_tls_anchor_count={len(records)}u;")
    output.write_text("\n".join(generated)+"\n",encoding="utf-8")
    print(f"Wrote {len(records)} X.509 trust anchors to {output}")
def main():
    p=argparse.ArgumentParser()
    p.add_argument("--bundle",required=True,type=pathlib.Path)
    p.add_argument("--output",required=True,type=pathlib.Path)
    args=p.parse_args()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    compile_anchors(args.bundle.read_bytes(),args.output)
if __name__=="__main__":
    main()
