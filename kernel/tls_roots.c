/* =============================================================================
 *  kernel/tls_roots.c — embedded TLS trust anchors
 * -----------------------------------------------------------------------------
 *  This file ships the X.509 root certificates BearSSL's minimal validator
 *  uses to authenticate HTTPS servers.  A "real" deployment embeds the
 *  full Mozilla NSS root bundle (~150 CAs, ~210 KiB) — that is generated
 *  by tools/build_roots.py reading certdata.txt from Mozilla NSS and
 *  emitting a single .c file with one br_x509_trust_anchor per CA.
 *
 *  Until the bareTCP/DNS layers land we ship a small handcrafted demo
 *  anchor (the well-known DigiCert Global Root CA) so callers can
 *  link cleanly and so tools/build_roots.py has an obvious place to
 *  drop the full bundle.
 *
 *  IMPORTANT: certs are stored DER-encoded as static byte arrays.  No
 *  PEM parsing happens at runtime — BearSSL's x509_minimal walks the
 *  DER directly, which keeps memory and code footprint small.
 * ============================================================================= */

#include "../vendor/bearssl/inc/bearssl.h"

typedef int i32;

/* ISRG Root X1 (Let's Encrypt) — RFC 5280 self-signed DER blob,
 * placeholder Distinguished Name + dummy 4-byte SPKI used solely
 * so the structure layout compiles.  tools/build_roots.py will
 * overwrite this whole file with the real Mozilla NSS bundle.    */
static const unsigned char ROOT0_DN[] = {
    /* DN: CN=ISRG Root X1, O=Internet Security Research Group, C=US
     * — minimal placeholder; real CA bundle generated separately. */
    0x30, 0x4F, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
    0x02, 0x55, 0x53, 0x31, 0x29, 0x30, 0x27, 0x06, 0x03, 0x55, 0x04, 0x0A,
    0x13, 0x20, 0x49, 0x6E, 0x74, 0x65, 0x72, 0x6E, 0x65, 0x74, 0x20, 0x53,
    0x65, 0x63, 0x75, 0x72, 0x69, 0x74, 0x79, 0x20, 0x52, 0x65, 0x73, 0x65,
    0x61, 0x72, 0x63, 0x68, 0x20, 0x47, 0x72, 0x6F, 0x75, 0x70, 0x31, 0x15,
    0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x0C, 0x49, 0x53, 0x52,
    0x47, 0x20, 0x52, 0x6F, 0x6F, 0x74, 0x20, 0x58, 0x31
};
static const unsigned char ROOT0_RSA_N[] = {
    /* 4-byte stub modulus — replaced by the real 4096-bit RSA modulus
     * once tools/build_roots.py runs.  Placeholder keeps link-time
     * structure intact. */
    0xC0, 0xFE, 0xBA, 0xBE
};
static const unsigned char ROOT0_RSA_E[] = { 0x01, 0x00, 0x01 };  /* 65537 */

const br_x509_trust_anchor TLS_ROOTS[] = {
    {
        .dn = { (unsigned char *)ROOT0_DN, sizeof ROOT0_DN },
        .flags = BR_X509_TA_CA,
        .pkey = {
            .key_type = BR_KEYTYPE_RSA,
            .key = { .rsa = {
                (unsigned char *)ROOT0_RSA_N, sizeof ROOT0_RSA_N,
                (unsigned char *)ROOT0_RSA_E, sizeof ROOT0_RSA_E
            } }
        }
    }
};

const i32 TLS_ROOTS_NUM = sizeof(TLS_ROOTS) / sizeof(TLS_ROOTS[0]);
