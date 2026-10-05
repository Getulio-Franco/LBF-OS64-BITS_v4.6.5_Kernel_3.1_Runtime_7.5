#ifndef TLS13_CRYPTO_H
#define TLS13_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

void get_random_bytes(uint8_t *buf, size_t len);
void x25519_keygen(uint8_t public_key[32], uint8_t private_key[32]);
int  x25519_shared_secret(uint8_t shared_secret[32], const uint8_t private_key[32], const uint8_t peer_public_key[32]);

#endif /* TLS13_CRYPTO_H */
