#ifndef CRYPTO_H
#define CRYPTO_H
#include <stdint.h>
#include <stddef.h>
void pbkdf2_sha256(const char *pw, const uint8_t *salt, size_t sl, int iter, uint8_t out[32]);
void to_hex(const uint8_t *in, size_t n, char *out);
int rand_bytes(uint8_t *b, size_t n);
int ct_eq(const char *a, const char *b, size_t n);
#endif
