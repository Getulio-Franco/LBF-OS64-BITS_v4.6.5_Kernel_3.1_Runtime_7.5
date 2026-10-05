/* ============================================================================
 * HKDF.C v1.2 - HKDF + HKDF-Expand-Label (RFC 5869 / RFC 8446 §7.1)
 * LBF-OS64 Ring 3 | Runtime_Net/crypto
 * v1.2: OLHOS [HKDF] via sys_debug para diagnosticar por que
 *       tls13_derive_handshake_keys vê hkdf_extract() != 0:
 *       - canary de entrada em cada função (prova QUAL código executa)
 *       - flags de ponteiro nulo (ikm / out / salt)
 *       - códigos de guarda distintos no expand / expand_label
 *       Mantidas as guardas anti-overflow da v1.1 (comportamento
 *       IDÊNTICO para entradas legítimas do TLS 1.3).
 * ============================================================================ */
#include "hkdf.h"
#include "../system/liblib.h"          /* sys_debug (syscall serial) */
#include "Browser/crypto/hmac.h"       /* Módulo HMAC-SHA256 do Ring 3 do LBF OS */

/* --- OLHOS: log numérico mínimo p/ Kernel Live Debug (sem libc) --- */
static void dbg_num(const char* tag, uint32_t v) {
    char t[16]; int i = 0;
    if (v == 0) t[i++] = '0';
    while (v) { t[i++] = (char)('0' + v % 10); v /= 10; }
    char o[16]; int j = 0;
    while (i > 0) o[j++] = t[--i];
    o[j] = '\0';
    sys_debug(tag);
    sys_debug(o);
}

/* Funções utilitárias auxiliares para ambiente freestanding (sem libc) */
static void *local_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}
static void *local_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }
    return s;
}
static size_t local_strlen(const char *str) {
    size_t len = 0;
    while (str && str[len] != '\0') {
        len++;
    }
    return len;
}

/* ============================================================================
 * HKDF-Extract (RFC 5869 §2.2) — PRK = HMAC-Hash(Salt, IKM)
 * ============================================================================ */
int hkdf_extract(const uint8_t *salt, size_t salt_len,
                 const uint8_t *ikm, size_t ikm_len,
                 uint8_t *prk_out)
{
    uint8_t null_salt[SHA256_DIGEST_SIZE];

    /* CANARY: se esta linha NÃO aparecer no KLD, o código executado
     * NÃO é este arquivo (símbolo duplicado/stale no link). */
    sys_debug("[HKDF] enter extract");
    dbg_num("[HKDF] ikm null=",  ikm ? 0u : 1u);
    dbg_num("[HKDF] out null=",  prk_out ? 0u : 1u);

    if (!ikm || !prk_out) {
        sys_debug("[HKDF] ERR extract null");
        return -1;
    }
    /* Se o salt não for fornecido, utiliza um bloco de 32 bytes a zero */
    if (!salt || salt_len == 0) {
        local_memset(null_salt, 0, SHA256_DIGEST_SIZE);
        salt = null_salt;
        salt_len = SHA256_DIGEST_SIZE;
        sys_debug("[HKDF] extract null-salt");
    }
    hmac_sha256(salt, salt_len, ikm, ikm_len, prk_out);
    sys_debug("[HKDF] extract ok");
    return 0;
}

/* ============================================================================
 * HKDF-Expand (RFC 5869 §2.3) — OKM = T(1) || T(2) || ... truncado
 * ============================================================================ */
int hkdf_expand(const uint8_t *prk, size_t prk_len,
                const uint8_t *info, size_t info_len,
                uint8_t *okm_out, size_t okm_len)
{
    sys_debug("[HKDF] enter expand");           /* CANARY */
    if (!prk || !okm_out || okm_len == 0) {
        sys_debug("[HKDF] ERR expand ptr");
        return -1;
    }
    /* Limite máximo do HKDF conforme especificação RFC 5869 */
    size_t n = (okm_len + SHA256_DIGEST_SIZE - 1) / SHA256_DIGEST_SIZE;
    dbg_num("[HKDF] expand n=", (uint32_t)n);
    if (n > 255) {
        sys_debug("[HKDF] ERR expand n255");
        return -1;
    }
    /* GUARDA v1.1: tmp_buf = T(32) + info(<=256) + counter(1) */
    if (info && info_len > 256) {
        dbg_num("[HKDF] ERR expand info=", (uint32_t)info_len);
        return -1;
    }

    uint8_t t[SHA256_DIGEST_SIZE];
    size_t t_len = 0;
    size_t okm_pos = 0;
    uint8_t counter = 1;
    uint8_t tmp_buf[SHA256_DIGEST_SIZE + 256 + 1];

    for (size_t i = 0; i < n; i++) {
        size_t tmp_len = 0;
        /* Copia T(i-1) se não for o primeiro bloco */
        if (t_len > 0) {
            local_memcpy(tmp_buf + tmp_len, t, t_len);
            tmp_len += t_len;
        }
        /* Adiciona o parâmetro info */
        if (info && info_len > 0) {
            local_memcpy(tmp_buf + tmp_len, info, info_len);
            tmp_len += info_len;
        }
        /* Adiciona o contador de bloco */
        tmp_buf[tmp_len++] = counter;
        /* T(i) = HMAC-Hash(PRK, T(i-1) || info || counter) */
        hmac_sha256(prk, prk_len, tmp_buf, tmp_len, t);
        t_len = SHA256_DIGEST_SIZE;
        /* Copia o resultado para o buffer final OKM */
        size_t to_copy = (okm_len - okm_pos < SHA256_DIGEST_SIZE) ?
                         (okm_len - okm_pos) : SHA256_DIGEST_SIZE;
        local_memcpy(okm_out + okm_pos, t, to_copy);
        okm_pos += to_copy;
        counter++;
    }
    sys_debug("[HKDF] expand ok");
    return 0;
}

/* ============================================================================
 * HKDF-Expand-Label (RFC 8446 §7.1) — HkdfLabel = len || "tls13 "+label || ctx
 * ============================================================================ */
int hkdf_expand_label(const uint8_t *secret, size_t secret_len,
                      const char *label,
                      const uint8_t *context, size_t context_len,
                      uint8_t *out, uint16_t out_len)
{
    uint8_t hkdf_label[512];

    sys_debug("[HKDF] enter label");            /* CANARY */
    size_t label_str_len = local_strlen(label);
    size_t prefix_len = 6; /* "tls13 " */
    size_t full_label_len = prefix_len + label_str_len;
    dbg_num("[HKDF] label len=", (uint32_t)full_label_len);
    dbg_num("[HKDF] ctx len=",   (uint32_t)context_len);

    if (full_label_len > 255 || context_len > 255) {
        sys_debug("[HKDF] ERR label 255");
        return -1;
    }
    /* GUARDA v1.1: pior caso teórico 2+1+255+1+255 = 514 > 512 */
    if (2 + 1 + full_label_len + 1 + context_len > sizeof(hkdf_label)) {
        sys_debug("[HKDF] ERR label size");
        return -1;
    }

    size_t pos = 0;
    /* 1. uint16 length (Big-Endian) */
    hkdf_label[pos++] = (uint8_t)((out_len >> 8) & 0xFF);
    hkdf_label[pos++] = (uint8_t)(out_len & 0xFF);
    /* 2. opaque label<7..255> = "tls13 " + label */
    hkdf_label[pos++] = (uint8_t)full_label_len;
    local_memcpy(&hkdf_label[pos], "tls13 ", prefix_len);
    pos += prefix_len;
    local_memcpy(&hkdf_label[pos], label, label_str_len);
    pos += label_str_len;
    /* 3. opaque context<0..255> */
    hkdf_label[pos++] = (uint8_t)context_len;
    if (context && context_len > 0) {
        local_memcpy(&hkdf_label[pos], context, context_len);
        pos += context_len;
    }
    /* Executa o HKDF-Expand passando a estrutura HkdfLabel construída */
    int rc = hkdf_expand(secret, secret_len, hkdf_label, pos, out, out_len);
    dbg_num("[HKDF] label rc=", (uint32_t)(rc == 0 ? 0 : 1));
    return rc;
}
