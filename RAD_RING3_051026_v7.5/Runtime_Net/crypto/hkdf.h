#ifndef HKDF_H
#define HKDF_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_SIZE 32

/**
 * HKDF-Extract (RFC 5869 §2.2)
 * Deriva uma Pseudorandom Key (PRK) a partir de um Salt e Input Keying Material (IKM).
 *
 * @param salt       Ponteiro para o salt (pode ser NULL se salt_len == 0)
 * @param salt_len   Tamanho do salt em bytes
 * @param ikm        Ponteiro para o IKM
 * @param ikm_len    Tamanho do IKM em bytes
 * @param prk_out    Buffer de saída para a PRK (mínimo de 32 bytes para SHA-256)
 * @return           0 em caso de sucesso, -1 em caso de erro
 */
int hkdf_extract(const uint8_t *salt, size_t salt_len,
                 const uint8_t *ikm, size_t ikm_len,
                 uint8_t *prk_out);

/**
 * HKDF-Expand (RFC 5869 §2.3)
 * Expande uma PRK para Output Keying Material (OKM) de comprimento desejado.
 *
 * @param prk        Ponteiro para a PRK gerada no Extract
 * @param prk_len    Tamanho da PRK (32 bytes para SHA-256)
 * @param info       Contexto/Informação opcional
 * @param info_len   Tamanho do buffer info em bytes
 * @param okm_out    Buffer de saída para a chave expandida
 * @param okm_len    Tamanho desejado da chave de saída (máx. 255 * 32 bytes)
 * @return           0 em caso de sucesso, -1 em caso de erro
 */
int hkdf_expand(const uint8_t *prk, size_t prk_len,
                const uint8_t *info, size_t info_len,
                uint8_t *okm_out, size_t okm_len);

/**
 * HKDF-Expand-Label (RFC 8446 §7.1 - TLS 1.3)
 * Formata a estrutura HkdfLabel ("tls13 " + label) e executa o HKDF-Expand.
 *
 * @param secret      Chave secreta de entrada
 * @param secret_len  Tamanho da chave secreta
 * @param label       String do label (ex: "derived", "c hs traffic")
 * @param context     Contexto/Hash do transcript do Handshake
 * @param context_len Tamanho do contexto em bytes (0 se nulo)
 * @param out         Buffer de saída da chave derivada
 * @param out_len     Tamanho desejado da chave derivada
 * @return            0 em caso de sucesso, -1 em caso de erro
 */
int hkdf_expand_label(const uint8_t *secret, size_t secret_len,
                      const char *label,
                      const uint8_t *context, size_t context_len,
                      uint8_t *out, uint16_t out_len);

#endif /* HKDF_H */
