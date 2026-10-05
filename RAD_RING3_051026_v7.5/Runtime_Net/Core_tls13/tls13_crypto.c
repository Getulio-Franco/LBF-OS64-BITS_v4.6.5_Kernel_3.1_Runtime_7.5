/* ============================================================================
 * TLS13_CRYPTO.C v1.1 - Camada de Abstração Criptográfica Segura
 * Integração: x25519.c + rng.c (CSPRNG real + Montgomery Ladder)
 * Ambiente: LBF-OS64 Ring 3 (Freestanding)
 * v1.1: cheque RFC 7748 §6.1 (segredo all-zero = ponto de ordem baixa);
 *       removido include hkdf.h não utilizado.
 * ============================================================================ */
#include "tls13_crypto.h"
#include "rng.h"        /* CSPRNG seguro: RDRAND + Jitter + HMAC-DRBG */
#include "x25519.h"     /* Montgomery Ladder real em radix-51 */

/**
 * Preenche buffer com bytes aleatórios criptograficamente seguros.
 */
void get_random_bytes(uint8_t *buf, size_t len) {
    if (!buf || len == 0) return;
    if (!rng_ready()) {
        rng_init();
    }
    rng_bytes(buf, (int)len);
}

/**
 * Geração de par de chaves efêmeras X25519 (RFC 7748 §5).
 */
void x25519_keygen(uint8_t public_key[32], uint8_t private_key[32]) {
    if (!public_key || !private_key) return;
    get_random_bytes(private_key, 32);
    private_key[0]  &= 248;   /* clamping RFC 7748 */
    private_key[31] &= 127;
    private_key[31] |= 64;
    x25519_base(public_key, private_key);
}

/**
 * Segredo compartilhado Diffie-Hellman X25519.
 */
int x25519_shared_secret(uint8_t shared_secret[32],
                         const uint8_t private_key[32],
                         const uint8_t peer_public_key[32]) {
    if (!shared_secret || !private_key || !peer_public_key) {
        return -1;
    }
    x25519(shared_secret, private_key, peer_public_key);

    /* RFC 7748 §6.1: rejeitar saída all-zero (ponto de ordem baixa).
     * Cheque em tempo constante (acúmulo OR, sem branch por byte). */
    uint8_t acc = 0;
    for (int i = 0; i < 32; i++) acc |= shared_secret[i];
    if (acc == 0) {
        for (int i = 0; i < 32; i++) shared_secret[i] = 0;  /* zeroing */
        return -1;
    }
    return 0;
}
