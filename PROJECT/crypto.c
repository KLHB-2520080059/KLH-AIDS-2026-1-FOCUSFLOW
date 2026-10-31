#include "crypto.h"
#include <string.h>
#include <stdio.h>

typedef struct { uint32_t h[8]; uint8_t buf[64]; uint64_t len; size_t bl; } Sha;
static const uint32_t K[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x,n) (((x)>>(n))|((x)<<(32-(n))))

static void comp(uint32_t *h, const uint8_t *p) {
    uint32_t w[64], a, b, c, d, e, f, g, hh, t1, t2;
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4*i] << 24) | ((uint32_t)p[4*i+1] << 16) | ((uint32_t)p[4*i+2] << 8) | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i-15],7) ^ ROR(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR(w[i-2],17) ^ ROR(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a=h[0]; b=h[1]; c=h[2]; d=h[3]; e=h[4]; f=h[5]; g=h[6]; hh=h[7];
    for (int i = 0; i < 64; i++) {
        t1 = hh + (ROR(e,6) ^ ROR(e,11) ^ ROR(e,25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        t2 = (ROR(a,2) ^ ROR(a,13) ^ ROR(a,22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}
static void s_init(Sha *s) {
    static const uint32_t i0[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(s->h, i0, sizeof i0); s->len = 0; s->bl = 0;
}
static void s_upd(Sha *s, const uint8_t *d, size_t n) {
    s->len += n;
    while (n) {
        size_t k = 64 - s->bl; if (k > n) k = n;
        memcpy(s->buf + s->bl, d, k); s->bl += k; d += k; n -= k;
        if (s->bl == 64) { comp(s->h, s->buf); s->bl = 0; }
    }
}
static void s_fin(Sha *s, uint8_t out[32]) {
    uint64_t bits = s->len * 8; uint8_t pad = 0x80, z = 0;
    s_upd(&*s, &pad, 1);
    while (s->bl != 56) s_upd(s, &z, 1);
    uint8_t L[8]; for (int i = 0; i < 8; i++) L[i] = (uint8_t)(bits >> (56 - 8*i));
    s_upd(s, L, 8);
    for (int i = 0; i < 8; i++) { out[4*i]=s->h[i]>>24; out[4*i+1]=s->h[i]>>16; out[4*i+2]=s->h[i]>>8; out[4*i+3]=s->h[i]; }
}
static void hmac(const uint8_t *key, size_t kl, const uint8_t *m1, size_t l1, uint8_t out[32]) {
    uint8_t k[64] = {0}, ip[64], op[64], in[32]; Sha s;
    if (kl > 64) { s_init(&s); s_upd(&s, key, kl); s_fin(&s, k); } else memcpy(k, key, kl);
    for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    s_init(&s); s_upd(&s, ip, 64); s_upd(&s, m1, l1); s_fin(&s, in);
    s_init(&s); s_upd(&s, op, 64); s_upd(&s, in, 32); s_fin(&s, out);
}
void pbkdf2_sha256(const char *pw, const uint8_t *salt, size_t sl, int iter, uint8_t out[32]) {
    uint8_t blk[80], u[32], t[32]; size_t kl = strlen(pw);
    if (sl > 64) sl = 64;
    memcpy(blk, salt, sl); blk[sl] = 0; blk[sl+1] = 0; blk[sl+2] = 0; blk[sl+3] = 1;
    hmac((const uint8_t *)pw, kl, blk, sl + 4, u); memcpy(t, u, 32);
    for (int i = 1; i < iter; i++) { hmac((const uint8_t *)pw, kl, u, 32, u); for (int j = 0; j < 32; j++) t[j] ^= u[j]; }
    memcpy(out, t, 32);
}
void to_hex(const uint8_t *in, size_t n, char *out) {
    for (size_t i = 0; i < n; i++) sprintf(out + 2*i, "%02x", in[i]);
    out[2*n] = 0;
}
int rand_bytes(uint8_t *b, size_t n) {
    FILE *f = fopen("/dev/urandom", "rb"); if (!f) return -1;
    size_t r = fread(b, 1, n, f); fclose(f); return r == n ? 0 : -1;
}
int ct_eq(const char *a, const char *b, size_t n) {
    unsigned char d = 0; for (size_t i = 0; i < n; i++) d |= (unsigned char)(a[i] ^ b[i]); return d == 0;
}
