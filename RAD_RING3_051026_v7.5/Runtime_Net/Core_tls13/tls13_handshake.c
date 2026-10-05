/* ============================================================================
 * TLS13_HANDSHAKE.C v3.0 - Máquina de Estados do Handshake TLS 1.3
 * LBF-OS64 Ring 3 | Runtime_Net/Core_tls13
 *
 * CORREÇÕES v3.0 (sobre v2.8):
 * 1. Envia o Finished do CLIENTE após o Finished do servidor (RFC 8446 §4.4.4).
 *    Antes o cliente marcava CONNECTED sem responder — o servidor dava timeout.
 * 2. Trata Certificate (11) e CertificateVerify (15) explicitamente:
 *    aceita a mensagem, avança o estado. Verificação criptográfica fica
 *    como TODO (requer RSA-PSS/ECDSA + trust store).
 * 3. Trata NewSessionTicket (4) sem quebrar o parser.
 * 4. Ordem canônica das extensões no ClientHello (Google/Cloudflare estritos).
 * 5. Transcript do ServerHello limitado a 4+msg_len (não engole vizinhos).
 * ============================================================================ */
#include "tls13.h"
#include "../system/liblib.h"

extern int  x25519_shared_secret(uint8_t shared_secret[32],
                                 const uint8_t private_key[32],
                                 const uint8_t peer_public_key[32]);
extern void get_random_bytes(uint8_t *buf, size_t len);
extern void tls13_transcript_update(tls13_session_t *session, const uint8_t *data, size_t len);
extern int  tls13_derive_handshake_keys(tls13_session_t *session);
extern int  tls13_derive_application_keys(tls13_session_t *session);
extern int  tls13_finished_verify_data(const uint8_t traffic_secret[32],
                                       const uint8_t transcript_hash[32],
                                       uint8_t out[32]);

static uint8_t g_hs_payload[TLS13_RECORD_MAX_PLAIN_LEN];
static uint8_t g_ch_msg[1024];

static void *local_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}
static size_t local_strlen(const char *str) {
    size_t len = 0;
    while (str && str[len] != '\0') len++;
    return len;
}
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
 * CLIENT HELLO (v3.0 — ordem canônica de extensões)
 * ============================================================================ */
int tls13_send_client_hello(tls13_session_t *session) {
    if (!session) return -1;
    uint8_t *msg = g_ch_msg;
    size_t pos = 0;

    msg[pos++] = TLS13_HANDSHAKE_CLIENT_HELLO;
    size_t length_pos = pos;
    pos += 3;

    /* legacy_version = 0x0303 */
    msg[pos++] = 0x03;
    msg[pos++] = 0x03;
    /* random[32] */
    get_random_bytes(&msg[pos], 32);
    pos += 32;
    /* legacy_session_id: 32 bytes (middlebox compat) */
    msg[pos++] = 32;
    get_random_bytes(&msg[pos], 32);
    pos += 32;
    /* cipher_suites: TLS_AES_128_GCM_SHA256 */
    msg[pos++] = 0x00;
    msg[pos++] = 0x02;
    msg[pos++] = (uint8_t)(TLS13_AES_128_GCM_SHA256 >> 8);
    msg[pos++] = (uint8_t)(TLS13_AES_128_GCM_SHA256 & 0xFF);
    /* compression_methods: null */
    msg[pos++] = 0x01;
    msg[pos++] = 0x00;
    /* extensions length (preenchido no fim) */
    size_t ext_len_pos = pos;
    pos += 2;

    /* --- 1. SNI (0x0000) --- */
    size_t host_len = local_strlen(session->hostname);
    if (host_len > 0) {
        msg[pos++] = 0x00; msg[pos++] = 0x00;
        uint16_t sni_ext_len = (uint16_t)(host_len + 5);
        msg[pos++] = (uint8_t)(sni_ext_len >> 8);
        msg[pos++] = (uint8_t)(sni_ext_len & 0xFF);
        uint16_t list_len = (uint16_t)(host_len + 3);
        msg[pos++] = (uint8_t)(list_len >> 8);
        msg[pos++] = (uint8_t)(list_len & 0xFF);
        msg[pos++] = 0x00;
        msg[pos++] = (uint8_t)(host_len >> 8);
        msg[pos++] = (uint8_t)(host_len & 0xFF);
        local_memcpy(&msg[pos], session->hostname, host_len);
        pos += host_len;
    }

    /* --- 2. supported_groups (0x000A) --- */
    msg[pos++] = 0x00; msg[pos++] = 0x0A;
    msg[pos++] = 0x00; msg[pos++] = 0x04;
    msg[pos++] = 0x00; msg[pos++] = 0x02;
    msg[pos++] = (uint8_t)(TLS13_GROUP_X25519 >> 8);
    msg[pos++] = (uint8_t)(TLS13_GROUP_X25519 & 0xFF);

    /* --- 3. signature_algorithms (0x000D) --- */
    msg[pos++] = 0x00; msg[pos++] = 0x0D;
    msg[pos++] = 0x00; msg[pos++] = 0x08;
    msg[pos++] = 0x00; msg[pos++] = 0x06;
    msg[pos++] = 0x08; msg[pos++] = 0x04;   /* rsa_pss_rsae_sha256 */
    msg[pos++] = 0x04; msg[pos++] = 0x03;   /* ecdsa_secp256r1_sha256 */
    msg[pos++] = 0x04; msg[pos++] = 0x01;   /* rsa_pkcs1_sha256 */

    /* --- 4. ALPN (0x0010) — http/1.1 --- */
    msg[pos++] = 0x00; msg[pos++] = 0x10;
    msg[pos++] = 0x00; msg[pos++] = 0x0B;
    msg[pos++] = 0x00; msg[pos++] = 0x09;
    msg[pos++] = 0x08;
    local_memcpy(&msg[pos], "http/1.1", 8);
    pos += 8;

    /* --- 5. supported_versions (0x002B) --- */
    msg[pos++] = 0x00; msg[pos++] = 0x2B;
    msg[pos++] = 0x00; msg[pos++] = 0x03;
    msg[pos++] = 0x02;
    msg[pos++] = (uint8_t)(TLS13_VERSION >> 8);
    msg[pos++] = (uint8_t)(TLS13_VERSION & 0xFF);

    /* --- 6. key_share (0x0033) — x25519 --- */
    msg[pos++] = 0x00; msg[pos++] = 0x33;
    msg[pos++] = 0x00; msg[pos++] = 0x26;
    msg[pos++] = 0x00; msg[pos++] = 0x24;
    msg[pos++] = (uint8_t)(TLS13_GROUP_X25519 >> 8);
    msg[pos++] = (uint8_t)(TLS13_GROUP_X25519 & 0xFF);
    msg[pos++] = 0x00; msg[pos++] = 0x20;
    local_memcpy(&msg[pos], session->client_public_key, 32);
    pos += 32;

    /* Preenche length da mensagem e das extensões */
    uint32_t payload_len = (uint32_t)(pos - 4);
    msg[length_pos]     = (uint8_t)((payload_len >> 16) & 0xFF);
    msg[length_pos + 1] = (uint8_t)((payload_len >> 8) & 0xFF);
    msg[length_pos + 2] = (uint8_t)(payload_len & 0xFF);
    uint16_t total_ext_len = (uint16_t)(pos - (ext_len_pos + 2));
    msg[ext_len_pos]     = (uint8_t)(total_ext_len >> 8);
    msg[ext_len_pos + 1] = (uint8_t)(total_ext_len & 0xFF);

    tls13_transcript_update(session, msg, pos);
    dbg_num("[HS] CH len=", (uint32_t)pos);

    if (tls13_record_write(session, TLS13_CONTENT_HANDSHAKE, msg, pos) < 0) {
        sys_debug("[HS] ERR ch write");
        session->state = TLS13_STATE_ERROR;
        return -1;
    }
    session->state = TLS13_STATE_CLIENT_HELLO_SENT;
    sys_debug("[HS] CH sent -> state=1");
    return 0;
}

/* ============================================================================
 * SERVER HELLO
 * ============================================================================ */
int tls13_process_server_hello(tls13_session_t *session, const uint8_t *data, size_t len) {
    if (!session || !data || len < 38) {
        sys_debug("[HS] ERR sh short");
        session->state = TLS13_STATE_ERROR;
        return -1;
    }
    size_t pos = 0;
    if (data[pos++] != TLS13_HANDSHAKE_SERVER_HELLO) {
        sys_debug("[HS] ERR sh type");
        session->state = TLS13_STATE_ERROR;
        return -1;
    }
    uint32_t msg_len = ((uint32_t)data[pos] << 16) | ((uint32_t)data[pos + 1] << 8) | data[pos + 2];
    pos += 3;
    if (msg_len + 4 > len) {
        sys_debug("[HS] ERR sh len");
        session->state = TLS13_STATE_ERROR;
        return -1;
    }
    pos += 34;  /* legacy_version(2) + random(32) */
    uint8_t sess_id_len = data[pos++];
    pos += sess_id_len;

    uint16_t cipher_suite = (uint16_t)((data[pos] << 8) | data[pos + 1]);
    pos += 2;
    dbg_num("[HS] SH cipher=", cipher_suite);

    if (cipher_suite != TLS13_AES_128_GCM_SHA256) {
        sys_debug("[HS] ERR sh cipher");
        session->state = TLS13_STATE_ERROR;
        return -1;
    }
    session->cipher_suite = cipher_suite;

    pos++;  /* legacy_compression_method */
    uint16_t ext_len = (uint16_t)((data[pos] << 8) | data[pos + 1]);
    pos += 2;

    uint8_t server_pubkey[32];
    int found_key_share = 0;
    size_t ext_end = pos + ext_len;
    while (pos + 4 <= ext_end) {
        uint16_t ext_type = (uint16_t)((data[pos] << 8) | data[pos + 1]);
        uint16_t ext_data_len = (uint16_t)((data[pos + 2] << 8) | data[pos + 3]);
        pos += 4;
        if (ext_type == 0x0033 && ext_data_len >= 4) {
            uint16_t group = (uint16_t)((data[pos] << 8) | data[pos + 1]);
            uint16_t key_len = (uint16_t)((data[pos + 2] << 8) | data[pos + 3]);
            if (group == TLS13_GROUP_X25519 && key_len == 32) {
                local_memcpy(server_pubkey, data + pos + 4, 32);
                found_key_share = 1;
            }
        }
        pos += ext_data_len;
    }
    if (!found_key_share) {
        sys_debug("[HS] ERR sh keyshare");
        session->state = TLS13_STATE_ERROR;
        return -1;
    }
    sys_debug("[HS] SH keyshare ok");

    /* Transcript: APENAS a mensagem SH (4 + msg_len) */
    tls13_transcript_update(session, data, 4 + msg_len);

    int xrc = x25519_shared_secret(session->shared_secret,
                                   session->client_private_key,
                                   server_pubkey);
    dbg_num("[HS] x25519 rc=", (uint32_t)(xrc == 0 ? 0 : 1));
    if (xrc != 0) {
        session->state = TLS13_STATE_ERROR;
        return -1;
    }

    if (tls13_derive_handshake_keys(session) != 0) {
        sys_debug("[HS] ERR derive hs");
        session->state = TLS13_STATE_ERROR;
        return -1;
    }

    session->state = TLS13_STATE_WAIT_ENCRYPTED_EXTENSIONS;
    sys_debug("[HS] keys ok -> state=3");
    return 0;
}

/* ============================================================================
 * v3.0: ENVIA O FINISHED DO CLIENTE (RFC 8446 §4.4.4)
 * ============================================================================ */
static int tls13_send_finished(tls13_session_t *session) {
    uint8_t transcript_hash[32];
    uint8_t verify_data[32];
    uint8_t finished_msg[4 + 32];

    /* 1. Hash do transcript ATÉ AGORA (inclui Finished do servidor) */
    tls13_transcript_get_hash(session, transcript_hash);

    /* 2. verify_data = HMAC(finished_key, transcript_hash) */
    if (tls13_finished_verify_data(session->secrets.client_hs_traffic_secret,
                                   transcript_hash,
                                   verify_data) != 0) {
        sys_debug("[HS] ERR finished hmac");
        return -1;
    }

    /* 3. Monta mensagem: [20][00 00 20][verify_data] */
    finished_msg[0] = TLS13_HANDSHAKE_FINISHED;
    finished_msg[1] = 0x00;
    finished_msg[2] = 0x00;
    finished_msg[3] = 0x20;
    local_memcpy(&finished_msg[4], verify_data, 32);

    /* 4. Envia CIFRADO com client_hs_traffic_secret (write_ctx já ativo) */
    if (tls13_record_write(session, TLS13_CONTENT_HANDSHAKE,
                           finished_msg, sizeof(finished_msg)) < 0) {
        sys_debug("[HS] ERR finished write");
        return -1;
    }
    sys_debug("[HS] client Finished sent");
    return 0;
}

/* ============================================================================
 * STEP
 * ============================================================================ */
int tls13_handshake_step(tls13_session_t *session) {
    if (!session) return -1;
    uint8_t content_type;
    uint8_t *payload = g_hs_payload;
    size_t payload_len = 0;

    dbg_num("[HS] step state=", (uint32_t)session->state);

    switch (session->state) {
    case TLS13_STATE_INIT:
        return tls13_send_client_hello(session);

    case TLS13_STATE_CLIENT_HELLO_SENT:
        if (tls13_record_read(session, &content_type, payload, &payload_len) < 0) {
            sys_debug("[HS] ERR read sh");
            session->state = TLS13_STATE_ERROR;
            return -1;
        }
        dbg_num("[HS] read ct=", content_type);
        dbg_num("[HS] read len=", (uint32_t)payload_len);
        if (content_type != TLS13_CONTENT_HANDSHAKE) {
            sys_debug("[HS] ERR ct sh");
            session->state = TLS13_STATE_ERROR;
            return -1;
        }
        if (tls13_process_server_hello(session, payload, payload_len) < 0) {
            session->state = TLS13_STATE_ERROR;
            return -1;
        }
        return 0;

    case TLS13_STATE_WAIT_ENCRYPTED_EXTENSIONS:
    case TLS13_STATE_WAIT_CERTIFICATE:
    case TLS13_STATE_WAIT_CERTIFICATE_VERIFY:
    case TLS13_STATE_WAIT_FINISHED: {
        size_t p = 0;

        if (tls13_record_read(session, &content_type, payload, &payload_len) < 0) {
            dbg_num("[HS] ERR read st=", (uint32_t)session->state);
            session->state = TLS13_STATE_ERROR;
            return -1;
        }
        dbg_num("[HS] read ct=", content_type);
        dbg_num("[HS] read len=", (uint32_t)payload_len);

        if (content_type == 20) {
            sys_debug("[HS] Ignore CCS -> continue");
            return 0;
        }
        if (content_type != TLS13_CONTENT_HANDSHAKE) {
            sys_debug("[HS] ERR ct enc");
            session->state = TLS13_STATE_ERROR;
            return -1;
        }

        while (p < payload_len) {
            if (p + 4 > payload_len) {
                sys_debug("[HS] ERR msg short");
                session->state = TLS13_STATE_ERROR;
                return -1;
            }
            uint8_t msg_type = payload[p];
            uint32_t msg_len = ((uint32_t)payload[p+1] << 16) |
                               ((uint32_t)payload[p+2] << 8) | payload[p+3];
            size_t total_msg_len = 4 + msg_len;

            if (p + total_msg_len > payload_len) {
                sys_debug("[HS] ERR msg oob");
                session->state = TLS13_STATE_ERROR;
                return -1;
            }
            dbg_num("[HS] msg type=", msg_type);
            dbg_num("[HS] msg len=",  msg_len);

            /* Transcript: TODAS as mensagens de handshake entram — EXCETO
             * NewSessionTicket (4), que é pós-handshake, e o nosso próprio
             * Finished (que ainda não foi enviado neste ponto). */
            if (msg_type != 4) {
                tls13_transcript_update(session, &payload[p], total_msg_len);
            }

            switch (msg_type) {
            case 8:  /* EncryptedExtensions */
                session->state = TLS13_STATE_WAIT_CERTIFICATE;
                break;

            case 11: /* Certificate */
                /* TODO(segurança): validar cadeia contra trust store */
                session->state = TLS13_STATE_WAIT_CERTIFICATE_VERIFY;
                break;

            case 15: /* CertificateVerify */
                /* TODO(segurança): verificar assinatura RSA-PSS/ECDSA
                 * Conteúdo assinado = 0x20*64 || "TLS 1.3, server CertificateVerify"
                 *                     || 0x00 || transcript_hash_até_cert */
                session->state = TLS13_STATE_WAIT_FINISHED;
                break;

            case 20: /* Finished do servidor */
                /* 1. Enviar Finished do CLIENTE (crucial!) */
                if (tls13_send_finished(session) != 0) {
                    session->state = TLS13_STATE_ERROR;
                    return -1;
                }
                /* 2. Derivar application keys */
                if (tls13_derive_application_keys(session) != 0) {
                    sys_debug("[HS] ERR derive app");
                    session->state = TLS13_STATE_ERROR;
                    return -1;
                }
                session->state = TLS13_STATE_CONNECTED;
                sys_debug("[HS] CONNECTED state=7");
                break;

            case 4:  /* NewSessionTicket — pós-handshake, ignorar */
                sys_debug("[HS] ignore NewSessionTicket");
                break;

            default:
                dbg_num("[HS] unknown msg=", msg_type);
                break;
            }

            p += total_msg_len;
        }
        return 0;
    }

    case TLS13_STATE_CONNECTED:
        return 0;

    default:
        sys_debug("[HS] ERR bad state");
        return -1;
    }
}
