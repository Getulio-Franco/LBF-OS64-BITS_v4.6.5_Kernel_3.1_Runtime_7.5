/* ============================================================================
 * TLS13.C v2.1 - Sessão, Transcript e Key Schedule TLS 1.3 (LBF-OS64 Ring 3)
 * v2.0: transcript com falha ALTA no estouro, Finished suportado,
 *       retornos de HKDF verificados em todas as derivações.
 * v2.1: OLHOS [KEY] via sys_debug — cada passo do key schedule loga
 *       rc (0=ok, 1=falhou), para apontar EXATAMENTE qual chamada HKDF
 *       retorna erro quando o handshake diz "[HS] ERR derive hs".
 * ============================================================================ */
#include "tls13.h"
#include "../system/liblib.h"          /* sys_debug (syscall serial) */

/* Primitivas criptográficas existentes no LBF OS */
extern void sha256(const uint8_t *data, size_t len, uint8_t *hash_out);
extern void x25519_keygen(uint8_t *pub_out, uint8_t *priv_out);
extern void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *msg, size_t msg_len,
                        uint8_t out[32]);

/* --- Utility Functions Freestanding --- */
static void *local_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}
static void *local_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
    return s;
}
static size_t local_strlen(const char *str) {
    size_t len = 0;
    while (str && str[len] != '\0') len++;
    return len;
}

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

/* ============================================================================
 * TRANSCRIPT HASH (histórico de mensagens do Handshake)
 * ============================================================================ */
void tls13_transcript_update(tls13_session_t *session, const uint8_t *data, size_t len) {
    if (!session || !data || len == 0) return;
    if (session->transcript_len + len > sizeof(session->transcript_buf)) {
        sys_debug("[KEY] ERR transcript overflow");      /* OLHOS */
        session->state = TLS13_STATE_ERROR;
        return;
    }
    local_memcpy(session->transcript_buf + session->transcript_len, data, len);
    session->transcript_len += len;
    dbg_num("[KEY] transcript len=", (uint32_t)session->transcript_len);  /* OLHOS */
}

void tls13_transcript_get_hash(tls13_session_t *session, uint8_t *hash_out) {
    if (!session || !hash_out) return;
    sha256(session->transcript_buf, session->transcript_len, hash_out);
}

/* ============================================================================
 * KEY SCHEDULE - CHAVES DE HANDSHAKE (RFC 8446 §7.1) + OLHOS [KEY] 1..7
 * ============================================================================ */
int tls13_derive_handshake_keys(tls13_session_t *session) {
    if (!session) return -1;
    uint8_t zeros[32];
    uint8_t derived_secret[32];
    uint8_t transcript_hash[32];
    uint8_t empty_hash[32];
    local_memset(zeros, 0, 32);
    sha256((const uint8_t *)"", 0, empty_hash);

    /* SANITY (OLHOS): checksum do shared_secret; 0 = X25519 vazio/suspeito */
    uint32_t ss_sum = 0;
    for (int i = 0; i < 32; i++) ss_sum += session->shared_secret[i];
    dbg_num("[KEY] ss sum=", ss_sum);

    int rc;
    /* 1. Early Secret = HKDF-Extract(Salt=0, IKM=0) */
    rc = hkdf_extract(zeros, 32, zeros, 32, session->secrets.early_secret);
    dbg_num("[KEY] 1 extract early=", (uint32_t)(rc == 0 ? 0 : 1));
    if (rc != 0) return -1;

    /* 2. Derived Secret = Expand-Label(early, "derived", Hash(""), 32) */
    rc = hkdf_expand_label(session->secrets.early_secret, 32, "derived",
                           empty_hash, 32, derived_secret, 32);
    dbg_num("[KEY] 2 expand derived=", (uint32_t)(rc == 0 ? 0 : 1));
    if (rc != 0) return -1;

    /* 3. Handshake Secret = Extract(Salt=derived, IKM=shared_secret ECDH) */
    rc = hkdf_extract(derived_secret, 32, session->shared_secret, 32,
                      session->secrets.handshake_secret);
    dbg_num("[KEY] 3 extract hs=", (uint32_t)(rc == 0 ? 0 : 1));
    if (rc != 0) return -1;

    /* Transcript Hash de ClientHello + ServerHello */
    tls13_transcript_get_hash(session, transcript_hash);

    /* 4/5. Traffic secrets de handshake (cliente & servidor) */
    rc = hkdf_expand_label(session->secrets.handshake_secret, 32, "c hs traffic",
                           transcript_hash, 32,
                           session->secrets.client_hs_traffic_secret, 32);
    dbg_num("[KEY] 4 c hs traffic=", (uint32_t)(rc == 0 ? 0 : 1));
    if (rc != 0) return -1;

    rc = hkdf_expand_label(session->secrets.handshake_secret, 32, "s hs traffic",
                           transcript_hash, 32,
                           session->secrets.server_hs_traffic_secret, 32);
    dbg_num("[KEY] 5 s hs traffic=", (uint32_t)(rc == 0 ? 0 : 1));
    if (rc != 0) return -1;

    /* 6. Chaves e IVs AEAD do handshake (contexto vazio, §7.1) */
    rc  = hkdf_expand_label(session->secrets.client_hs_traffic_secret, 32, "key",
                            NULL, 0, session->write_ctx.key, TLS13_KEY_LEN);
    rc |= hkdf_expand_label(session->secrets.client_hs_traffic_secret, 32, "iv",
                            NULL, 0, session->write_ctx.iv, TLS13_IV_LEN);
    dbg_num("[KEY] 6 c key/iv=", (uint32_t)(rc == 0 ? 0 : 1));
    if (rc != 0) return -1;

    /* 7. Chaves e IVs AEAD do lado do servidor */
    rc  = hkdf_expand_label(session->secrets.server_hs_traffic_secret, 32, "key",
                            NULL, 0, session->read_ctx.key, TLS13_KEY_LEN);
    rc |= hkdf_expand_label(session->secrets.server_hs_traffic_secret, 32, "iv",
                            NULL, 0, session->read_ctx.iv, TLS13_IV_LEN);
    dbg_num("[KEY] 7 s key/iv=", (uint32_t)(rc == 0 ? 0 : 1));
    if (rc != 0) return -1;

    /* Ativa os contextos de cifragem */
    session->write_ctx.seq_num = 0;
    session->write_ctx.active  = 1;
    session->read_ctx.seq_num  = 0;
    session->read_ctx.active   = 1;
    sys_debug("[KEY] derive hs OK");                     /* OLHOS */
    return 0;
}

/* ============================================================================
 * KEY SCHEDULE - CHAVES DE APLICAÇÃO (RFC 8446 §7.1) + OLHOS [KEY] A1..A6
 * ============================================================================ */
int tls13_derive_application_keys(tls13_session_t *session) {
    if (!session) return -1;
    uint8_t zeros[32];
    uint8_t derived_secret[32];
    uint8_t transcript_hash[32];
    uint8_t empty_hash[32];
    local_memset(zeros, 0, 32);
    sha256((const uint8_t *)"", 0, empty_hash);
    int rc;

    /* 1. Derived Secret a partir do Handshake Secret */
    rc = hkdf_expand_label(session->secrets.handshake_secret, 32, "derived",
                           empty_hash, 32, derived_secret, 32);
    dbg_num("[KEY] A1 expand derived=", (uint32_t)(rc == 0 ? 0 : 1));
    if (rc != 0) return -1;

    /* 2. Master Secret = Extract(Salt=derived, IKM=0) */
    rc = hkdf_extract(derived_secret, 32, zeros, 32,
                      session->secrets.master_secret);
    dbg_num("[KEY] A2 extract master=", (uint32_t)(rc == 0 ? 0 : 1));
    if (rc != 0) return -1;

    /* Transcript Hash completo do Handshake (CH .. server Finished) */
    tls13_transcript_get_hash(session, transcript_hash);

    /* 3/4. Traffic secrets de aplicação */
    rc = hkdf_expand_label(session->secrets.master_secret, 32, "c ap traffic",
                           transcript_hash, 32,
                           session->secrets.client_app_traffic_secret, 32);
    dbg_num("[KEY] A3 c ap traffic=", (uint32_t)(rc == 0 ? 0 : 1));
    if (rc != 0) return -1;

    rc = hkdf_expand_label(session->secrets.master_secret, 32, "s ap traffic",
                           transcript_hash, 32,
                           session->secrets.server_app_traffic_secret, 32);
    dbg_num("[KEY] A4 s ap traffic=", (uint32_t)(rc == 0 ? 0 : 1));
    if (rc != 0) return -1;

    /* 5/6. Chaves e IVs do tráfego de dados */
    rc  = hkdf_expand_label(session->secrets.client_app_traffic_secret, 32, "key",
                            NULL, 0, session->write_ctx.key, TLS13_KEY_LEN);
    rc |= hkdf_expand_label(session->secrets.client_app_traffic_secret, 32, "iv",
                            NULL, 0, session->write_ctx.iv, TLS13_IV_LEN);
    dbg_num("[KEY] A5 c key/iv=", (uint32_t)(rc == 0 ? 0 : 1));
    if (rc != 0) return -1;

    rc  = hkdf_expand_label(session->secrets.server_app_traffic_secret, 32, "key",
                            NULL, 0, session->read_ctx.key, TLS13_KEY_LEN);
    rc |= hkdf_expand_label(session->secrets.server_app_traffic_secret, 32, "iv",
                            NULL, 0, session->read_ctx.iv, TLS13_IV_LEN);
    dbg_num("[KEY] A6 s key/iv=", (uint32_t)(rc == 0 ? 0 : 1));
    if (rc != 0) return -1;

    /* Reinicia os números de sequência para o tráfego de dados */
    session->write_ctx.seq_num = 0;
    session->read_ctx.seq_num  = 0;
    sys_debug("[KEY] derive app OK");                    /* OLHOS */
    return 0;
}

/* ============================================================================
 * FINISHED (RFC 8446 §4.4.4) + OLHOS
 * ============================================================================ */
int tls13_derive_finished_key(const uint8_t traffic_secret[32], uint8_t out[32]) {
    if (!traffic_secret || !out) return -1;
    int rc = hkdf_expand_label(traffic_secret, 32, "finished", NULL, 0, out, 32);
    if (rc != 0) dbg_num("[KEY] ERR finished key=", 1);  /* OLHOS */
    return rc;
}

int tls13_finished_verify_data(const uint8_t traffic_secret[32],
                               const uint8_t transcript_hash[32],
                               uint8_t out[32]) {
    uint8_t finished_key[32];
    if (!traffic_secret || !transcript_hash || !out) return -1;
    if (tls13_derive_finished_key(traffic_secret, finished_key) != 0) return -1;
    hmac_sha256(finished_key, 32, transcript_hash, 32, out);
    for (int i = 0; i < 32; i++) finished_key[i] = 0;  /* zeroing seguro */
    return 0;
}

/* ============================================================================
 * INICIALIZAÇÃO DA SESSÃO + OLHOS
 * ============================================================================ */
int tls13_session_init(tls13_session_t *session, int socket_fd, const char *hostname) {
    if (!session || socket_fd < 0 || !hostname) return -1;
    local_memset(session, 0, sizeof(tls13_session_t));
    session->socket_fd = socket_fd;
    session->state     = TLS13_STATE_INIT;
    size_t host_len = local_strlen(hostname);
    if (host_len >= sizeof(session->hostname))
        host_len = sizeof(session->hostname) - 1;
    local_memcpy(session->hostname, hostname, host_len);
    session->hostname[host_len] = '\0';
    /* Gera o par de chaves efêmero X25519 do cliente */
    x25519_keygen(session->client_public_key, session->client_private_key);
    dbg_num("[KEY] init fd=", (uint32_t)socket_fd);      /* OLHOS */
    return 0;
}
