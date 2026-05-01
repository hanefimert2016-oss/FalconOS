/* =============================================================================
 *  FalconOS — authentication primitives  (v5+)
 * -----------------------------------------------------------------------------
 *  Self-contained SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256 and a tiny
 *  TSC-seeded random source.  Built so user passwords NEVER touch disk in
 *  plain text:
 *
 *      stored = PBKDF2-HMAC-SHA256(password, salt, 10 000 iters, 32 bytes)
 *
 *  Per-user salt = 16 bytes from rng_bytes(); regenerated every time the
 *  password is changed.  All maths is byte-for-byte from FIPS 180-4 (SHA-256)
 *  and RFC 2898 §5.2 (PBKDF2).
 * ============================================================================= */
#include "falcon.h"

/* --------------------------------------------------------------------------- */
/* SHA-256 — FIPS 180-4 §6.2                                                   */

static const u32 K256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static u32 ror32(u32 x, u32 n) { return (x >> n) | (x << (32 - n)); }

typedef struct {
    u32  h[8];
    u8   buf[64];
    u32  buf_len;
    u64  bit_len;
} sha256_ctx;

static void sha256_init(sha256_ctx *c)
{
    c->h[0] = 0x6a09e667u; c->h[1] = 0xbb67ae85u;
    c->h[2] = 0x3c6ef372u; c->h[3] = 0xa54ff53au;
    c->h[4] = 0x510e527fu; c->h[5] = 0x9b05688cu;
    c->h[6] = 0x1f83d9abu; c->h[7] = 0x5be0cd19u;
    c->buf_len = 0;
    c->bit_len = 0;
}

static void sha256_compress(sha256_ctx *c, const u8 *block)
{
    u32 W[64];
    for (i32 i = 0; i < 16; i++) {
        W[i] = ((u32)block[i*4] << 24) | ((u32)block[i*4+1] << 16)
             | ((u32)block[i*4+2] <<  8) |  (u32)block[i*4+3];
    }
    for (i32 i = 16; i < 64; i++) {
        u32 s0 = ror32(W[i-15], 7) ^ ror32(W[i-15], 18) ^ (W[i-15] >> 3);
        u32 s1 = ror32(W[i-2], 17) ^ ror32(W[i-2],  19) ^ (W[i-2]  >> 10);
        W[i] = W[i-16] + s0 + W[i-7] + s1;
    }
    u32 a=c->h[0],b=c->h[1],cc=c->h[2],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7];
    for (i32 i = 0; i < 64; i++) {
        u32 S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        u32 ch = (e & f) ^ ((~e) & g);
        u32 t1 = h + S1 + ch + K256[i] + W[i];
        u32 S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        u32 mj = (a & b) ^ (a & cc) ^ (b & cc);
        u32 t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d;
    c->h[4]+=e; c->h[5]+=f; c->h[6]+=g;  c->h[7]+=h;
}

static void sha256_update(sha256_ctx *c, const u8 *data, u32 len)
{
    c->bit_len += (u64)len * 8;
    while (len) {
        u32 take = 64 - c->buf_len;
        if (take > len) take = len;
        k_memcpy(c->buf + c->buf_len, data, take);
        c->buf_len += take;
        data += take; len -= take;
        if (c->buf_len == 64) {
            sha256_compress(c, c->buf);
            c->buf_len = 0;
        }
    }
}

static void sha256_final(sha256_ctx *c, u8 out[32])
{
    /* pad: 0x80 then zero, leave 8 bytes for length */
    c->buf[c->buf_len++] = 0x80;
    if (c->buf_len > 56) {
        while (c->buf_len < 64) c->buf[c->buf_len++] = 0;
        sha256_compress(c, c->buf);
        c->buf_len = 0;
    }
    while (c->buf_len < 56) c->buf[c->buf_len++] = 0;
    u64 bl = c->bit_len;
    for (i32 i = 7; i >= 0; i--) c->buf[c->buf_len++] = (u8)(bl >> (i * 8));
    sha256_compress(c, c->buf);
    for (i32 i = 0; i < 8; i++) {
        out[i*4]   = (u8)(c->h[i] >> 24);
        out[i*4+1] = (u8)(c->h[i] >> 16);
        out[i*4+2] = (u8)(c->h[i] >>  8);
        out[i*4+3] = (u8)(c->h[i]      );
    }
}

void sha256_hash(const u8 *data, u32 len, u8 out[32])
{
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, data, len);
    sha256_final(&c, out);
}

/* --------------------------------------------------------------------------- */
/* HMAC-SHA256 — FIPS 198-1                                                    */

void hmac_sha256(const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 out[32])
{
    u8 k[64]; k_memset(k, 0, 64);
    if (klen > 64) {
        u8 tmp[32];
        sha256_hash(key, klen, tmp);
        k_memcpy(k, tmp, 32);
    } else {
        k_memcpy(k, key, klen);
    }
    u8 ipad[64], opad[64];
    for (i32 i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5C;
    }
    sha256_ctx c; u8 inner[32];
    sha256_init(&c);
    sha256_update(&c, ipad, 64);
    sha256_update(&c, msg, mlen);
    sha256_final(&c, inner);
    sha256_init(&c);
    sha256_update(&c, opad, 64);
    sha256_update(&c, inner, 32);
    sha256_final(&c, out);
}

/* --------------------------------------------------------------------------- */
/* PBKDF2-HMAC-SHA256 — RFC 2898 §5.2                                          */

void pbkdf2_sha256(const u8 *pwd, u32 plen,
                   const u8 *salt, u32 slen,
                   u32 iterations, u8 *out, u32 outlen)
{
    u32 hLen = 32;
    u32 blocks = (outlen + hLen - 1) / hLen;
    u8 saltblk[FALCON_SALT_BYTES + 64];   /* salt + 4 */
    u8 U[32], T[32];

    for (u32 b = 1; b <= blocks; b++) {
        if (slen > sizeof(saltblk) - 4) slen = sizeof(saltblk) - 4;
        k_memcpy(saltblk, salt, slen);
        saltblk[slen]   = (u8)(b >> 24);
        saltblk[slen+1] = (u8)(b >> 16);
        saltblk[slen+2] = (u8)(b >>  8);
        saltblk[slen+3] = (u8) b;

        hmac_sha256(pwd, plen, saltblk, slen + 4, U);
        k_memcpy(T, U, hLen);

        for (u32 i = 1; i < iterations; i++) {
            hmac_sha256(pwd, plen, U, hLen, U);
            for (u32 j = 0; j < hLen; j++) T[j] ^= U[j];
        }

        u32 take = hLen;
        if (b == blocks) {
            u32 leftover = outlen - (blocks - 1) * hLen;
            if (leftover) take = leftover;
        }
        k_memcpy(out + (b - 1) * hLen, T, take);
    }
}

/* --------------------------------------------------------------------------- */
/* RNG — TSC + g_ticks + last-output mixed through SHA-256                     */
/*  Not cryptographically unpredictable on a fresh boot, but salt randomness
 *  here only needs to vary across users on a single machine.  Each call
 *  re-mixes the previous output with TSC, so successive draws diverge.       */

static u8 g_rng_state[32];
static bool g_rng_seeded = false;

void rng_bytes(u8 *out, u32 n)
{
    if (!g_rng_seeded) {
        u64 t = rdtsc();
        u8 seed[16];
        for (i32 i = 0; i < 8; i++) seed[i]   = (u8)(t >> (i * 8));
        u32 ticks = g_ticks;
        for (i32 i = 0; i < 4; i++) seed[8+i] = (u8)(ticks >> (i * 8));
        seed[12] = 0xFA; seed[13] = 0x1C; seed[14] = 0x0E; seed[15] = 0xC5;
        sha256_hash(seed, 16, g_rng_state);
        g_rng_seeded = true;
    }
    while (n) {
        /* Mix in another 8 bytes of TSC each round */
        u8 mix[40];
        k_memcpy(mix, g_rng_state, 32);
        u64 t = rdtsc();
        for (i32 i = 0; i < 8; i++) mix[32 + i] = (u8)(t >> (i * 8));
        sha256_hash(mix, 40, g_rng_state);
        u32 take = (n < 32) ? n : 32;
        k_memcpy(out, g_rng_state, take);
        out += take; n -= take;
    }
}

void hex_encode(const u8 *in, u32 n, char *out)
{
    static const char H[] = "0123456789abcdef";
    for (u32 i = 0; i < n; i++) {
        out[i*2]   = H[(in[i] >> 4) & 0xF];
        out[i*2+1] = H[ in[i]       & 0xF];
    }
    out[n*2] = 0;
}
