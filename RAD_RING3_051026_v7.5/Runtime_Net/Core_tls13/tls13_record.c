/* ============================================================================
 * TLS13_RECORD.C v5.6 - Camada de Registro TLS 1.3 (LBF-OS64 Ring 3)
 * Base: v5.5 (recv acumulativo) — MANTIDA.
 * v5.6 = CURA DO FALSO PEER-CLOSE:
 *   - recv_full(): n==0 NÃO significa mais "peer fechou".
 *     Na stack Ring 3 o recv pode devolver 0 quando o buffer TCP está
 *     vazio, mesmo com a conexão viva. Agora 0 == "sem dados agora":
 *     bombeia net_poll_frame() + sys_sleep() e tenta de novo, gastando
 *     o orçamento. Só declara peer-close quando o orçamento ZERA e
 *     ainda temos have==0 (nada recebido) — senão devolve o parcial.
 *   - Olhos [REC] mostram o valor cru de recv() para diagnóstico.
 * ============================================================================ */
#include "tls13.h"
#include "../system/liblib.h"

extern int send(int sockfd, const void *buf, size_t len, int flags);
extern int recv(int sockfd, void *buf, size_t len, int flags);
extern void net_poll_frame(void);

extern void aes_gcm_encrypt(const uint8_t key[16], const uint8_t iv[12],
                            const uint8_t* aad, uint32_t aad_len,
                            const uint8_t* pt, uint32_t pt_len,
                            uint8_t* ct, uint8_t tag[16]);
extern int  aes_gcm_decrypt(const uint8_t key[16], const uint8_t iv[12],
                            const uint8_t* aad, uint32_t aad_len,
                            const uint8_t* ct, uint32_t ct_len,
                            uint8_t* pt, const uint8_t tag[16]);

static uint8_t g_rec_buf[TLS13_RECORD_MAX_PAYLOAD_LEN];
static uint8_t g_plain_buf[TLS13_RECORD_MAX_PLAIN_LEN + 1];
static uint8_t g_cipher_buf[TLS13_RECORD_MAX_PAYLOAD_LEN];

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
 * v5.6: RECV ACUMULATIVO TOLERANTE
 *   n > 0  -> acumula, rearma orçamento
 *   n == 0 -> NÃO é peer-close necessariamente: tenta de novo
 *   n < 0  -> sem dados agora: poll + sleep + orçamento
 * Só devolve -1 se o orçamento zerar E have==0 (peer realmente fechou).
 * ============================================================================ */
#define REC_RETRY_MAX 250   /* 250 x 20ms = 5s por fase */
static int recv_full(int fd, uint8_t *dst, size_t need) {
    size_t have = 0;
    int retries = REC_RETRY_MAX;
    while (have < need) {
        int n = recv(fd, dst + have, need - have, 0);
        dbg_num("[REC] recv n=", (uint32_t)(n < 0 ? 0xFFFFFFFFu : (uint32_t)n));
        if (n > 0) {
            have += (size_t)n;
            retries = REC_RETRY_MAX;
            continue;
        }
        if (n == 0) {
            /* EOF DEFINITIVO: peer fechou. Não espera, não retria. */
            dbg_num("[REC] eof at=", (uint32_t)have);
            return (have > 0) ? (int)have : -1;
        }
        /* n == 0 OU n < 0: tratar como "sem dados agora" */
        if (retries <= 0) {
            dbg_num("[REC] give up at=", (uint32_t)have);
            dbg_num("[REC] want=",     (uint32_t)need);
            return (have == 0) ? -1 : (int)have;
        }
        retries--;
        net_poll_frame();
        sys_sleep(20);
    }
    return (int)have;
}

static void build_nonce(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12]) {
    for (int i = 0; i < 12; i++) nonce[i] = iv[i];
    for (int i = 0; i < 8; i++) {
        nonce[11 - i] ^= (uint8_t)(seq >> (8 * i));
    }
}

int tls13_record_write(tls13_session_t *session, uint8_t content_type,
                       const uint8_t *payload, size_t len) {
    if (!session || (!payload && len > 0)) return -1;
    uint8_t *rec_buf = g_rec_buf;
    size_t pos = 0;

    if (!session->write_ctx.active) {
        rec_buf[0] = content_type;
        rec_buf[1] = 0x03;
        rec_buf[2] = 0x03;
        rec_buf[3] = (uint8_t)((len >> 8) & 0xFF);
        rec_buf[4] = (uint8_t)(len & 0xFF);
        pos = 5;
        for (size_t i = 0; i < len; i++) rec_buf[pos++] = payload[i];
        int sent = send(session->socket_fd, rec_buf, pos, 0);
        return (sent == (int)pos) ? 0 : -1;
    } else {
        uint8_t *plain = g_plain_buf;
        for (size_t i = 0; i < len; i++) plain[i] = payload[i];
        plain[len] = content_type;
        size_t plain_len = len + 1;

        uint8_t nonce[12];
        build_nonce(session->write_ctx.iv, session->write_ctx.seq_num, nonce);

        uint8_t hdr[5];
        hdr[0] = TLS13_CONTENT_APPLICATION_DATA;
        hdr[1] = 0x03;
        hdr[2] = 0x03;
        size_t enc_len = plain_len + 16;
        hdr[3] = (uint8_t)((enc_len >> 8) & 0xFF);
        hdr[4] = (uint8_t)(enc_len & 0xFF);

        uint8_t *ct = g_cipher_buf;
        uint8_t tag[16];
        aes_gcm_encrypt(session->write_ctx.key, nonce,
                        hdr, 5,
                        plain, (uint32_t)plain_len,
                        ct, tag);
        session->write_ctx.seq_num++;

        rec_buf[0] = hdr[0];
        rec_buf[1] = hdr[1];
        rec_buf[2] = hdr[2];
        rec_buf[3] = hdr[3];
        rec_buf[4] = hdr[4];
        pos = 5;
        for (size_t i = 0; i < plain_len; i++) rec_buf[pos++] = ct[i];
        for (int i = 0; i < 16; i++) rec_buf[pos++] = tag[i];
        int sent = send(session->socket_fd, rec_buf, pos, 0);
        return (sent == (int)pos) ? 0 : -1;
    }
}

int tls13_record_read(tls13_session_t *session, uint8_t *out_content_type,
                      uint8_t *out_payload, size_t *out_len) {
    if (!session || !out_content_type || !out_payload || !out_len) return -1;
    int skips = 0;

    for (;;) {
        uint8_t hdr[5];
        if (recv_full(session->socket_fd, hdr, 5) != 5) return -1;

        uint16_t rec_len = (uint16_t)((hdr[3] << 8) | hdr[4]);
        if (rec_len > TLS13_RECORD_MAX_PAYLOAD_LEN) return -1;

        dbg_num("[REC] type=", hdr[0]);
        dbg_num("[REC] len=",  rec_len);

        uint8_t *buf = g_rec_buf;
        if (recv_full(session->socket_fd, buf, rec_len) != (int)rec_len) return -1;

        if (hdr[0] == TLS13_CONTENT_ALERT && rec_len >= 2) {
            dbg_num("[REC] alert level=", buf[0]);
            dbg_num("[REC] alert desc=",  buf[1]);
        }

        if (hdr[0] == TLS13_CONTENT_CHANGE_CIPHER_SPEC) {
            if (++skips > 4) return -1;
            dbg_num("[REC] skip CCS=", (uint32_t)skips);
            continue;
        }

        if (!session->read_ctx.active) {
            if (rec_len > TLS13_RECORD_MAX_PLAIN_LEN) return -1;
            *out_content_type = hdr[0];
            for (size_t i = 0; i < rec_len; i++) out_payload[i] = buf[i];
            *out_len = rec_len;
            return 0;
        } else {
            if (rec_len < 17) return -1;
            size_t ct_len = rec_len - 16;
            if (ct_len > TLS13_RECORD_MAX_PLAIN_LEN + 1) return -1;

            uint8_t nonce[12];
            build_nonce(session->read_ctx.iv, session->read_ctx.seq_num, nonce);

            uint8_t *ct  = buf;
            uint8_t *tag = buf + ct_len;

            uint8_t *plain = g_plain_buf;
            int rc = aes_gcm_decrypt(session->read_ctx.key, nonce,
                                     hdr, 5,
                                     ct, (uint32_t)ct_len,
                                     plain, tag);
            dbg_num("[REC] gcm=", (rc == 0) ? 0 : 1);
            if (rc != 0) return -1;
            session->read_ctx.seq_num++;

            size_t plain_len = ct_len;
            if (plain_len == 0) return -1;
            *out_content_type = plain[plain_len - 1];
            dbg_num("[REC] inner=", *out_content_type);

            if (*out_content_type == TLS13_CONTENT_CHANGE_CIPHER_SPEC) {
                if (++skips > 4) return -1;
                continue;
            }

            size_t payload_len = plain_len - 1;
            for (size_t i = 0; i < payload_len; i++) out_payload[i] = plain[i];
            *out_len = payload_len;
            return 0;
        }
    }
}
