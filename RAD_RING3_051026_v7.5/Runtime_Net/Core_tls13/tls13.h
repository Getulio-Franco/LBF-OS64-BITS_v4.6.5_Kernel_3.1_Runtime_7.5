#ifndef TLS13_H
#define TLS13_H
/* ============================================================================
 * TLS13.H - Header público do Core TLS 1.3 (LBF-OS64 Ring 3)
 * Fonte única de tipos, constantes e protótipos do protocolo.
 * ============================================================================ */
#include <stddef.h>
#include <stdint.h>
#include "hkdf.h"

/* --- Versões do Protocolo --- */
#define TLS13_VERSION                  0x0304
#define TLS12_LEGACY_VERSION           0x0303

/* --- Tipos de Registro (Record Content Types) --- */
#define TLS13_CONTENT_CHANGE_CIPHER_SPEC 20
#define TLS13_CONTENT_ALERT              21
#define TLS13_CONTENT_HANDSHAKE          22
#define TLS13_CONTENT_APPLICATION_DATA   23

/* --- Mensagens de Handshake TLS 1.3 --- */
#define TLS13_HANDSHAKE_CLIENT_HELLO          1
#define TLS13_HANDSHAKE_SERVER_HELLO          2
#define TLS13_HANDSHAKE_NEW_SESSION_TICKET    4
#define TLS13_HANDSHAKE_ENCRYPTED_EXTENSIONS  8
#define TLS13_HANDSHAKE_CERTIFICATE           11
#define TLS13_HANDSHAKE_CERTIFICATE_VERIFY    15
#define TLS13_HANDSHAKE_FINISHED              20

/* --- Suítes de Cifragem (RFC 8446) --- */
#define TLS13_AES_128_GCM_SHA256             0x1301

/* --- Grupos de Troca de Chaves --- */
#define TLS13_GROUP_SECP256R1                0x0017
#define TLS13_GROUP_X25519                   0x001D

/* --- Tamanhos Máximos de Buffers --- */
#define TLS13_RECORD_MAX_PLAIN_LEN           16384
#define TLS13_RECORD_MAX_PAYLOAD_LEN         (TLS13_RECORD_MAX_PLAIN_LEN + 256)
#define TLS13_KEY_LEN                        16   /* AES-128 */
#define TLS13_IV_LEN                         12   /* GCM Nonce */
#define TLS13_HASH_LEN                       32   /* SHA-256 */
/* Transcript: CH+SH+EE+Certificado(cadeia)+CV+Finished ultrapassa 4KB em
 * cadeias reais (Google ~5KB). 8KB cobre com folga; estouro agora falha
 * VISIVELMENTE (state=ERROR) em vez de truncar calado e derivar chave errada. */
#define TLS13_TRANSCRIPT_MAX                 8192

/* --- Máquina de Estados da Conexão TLS 1.3 --- */
typedef enum {
    TLS13_STATE_INIT = 0,
    TLS13_STATE_CLIENT_HELLO_SENT,
    TLS13_STATE_WAIT_SERVER_HELLO,
    TLS13_STATE_WAIT_ENCRYPTED_EXTENSIONS,
    TLS13_STATE_WAIT_CERTIFICATE,
    TLS13_STATE_WAIT_CERTIFICATE_VERIFY,
    TLS13_STATE_WAIT_FINISHED,
    TLS13_STATE_CONNECTED,
    TLS13_STATE_ERROR
} tls13_state_t;

/* --- Controle dos Segredos do Key Schedule --- */
typedef struct {
    uint8_t early_secret[TLS13_HASH_LEN];
    uint8_t handshake_secret[TLS13_HASH_LEN];
    uint8_t master_secret[TLS13_HASH_LEN];
    uint8_t client_hs_traffic_secret[TLS13_HASH_LEN];
    uint8_t server_hs_traffic_secret[TLS13_HASH_LEN];
    uint8_t client_app_traffic_secret[TLS13_HASH_LEN];
    uint8_t server_app_traffic_secret[TLS13_HASH_LEN];
} tls13_secrets_t;

/* --- Estado de Cifragem/Decifragem (AEAD - AES-GCM) --- */
typedef struct {
    uint8_t key[TLS13_KEY_LEN];
    uint8_t iv[TLS13_IV_LEN];
    uint64_t seq_num; /* Contador de sequência para montagem do Nonce */
    int active;       /* 1 = Cifragem ativada para este nível */
} tls13_cipher_ctx_t;

/* --- Contexto da Sessão TLS 1.3 --- */
typedef struct {
    int socket_fd;
    tls13_state_t state;
    uint16_t cipher_suite;
    char hostname[256];
    /* Troca de Chaves Efêmera (X25519) */
    uint8_t client_private_key[32];
    uint8_t client_public_key[32];
    uint8_t shared_secret[32];
    /* Histórico para Transcript Hash (SHA-256) */
    uint8_t transcript_buf[TLS13_TRANSCRIPT_MAX];
    size_t transcript_len;
    /* Segredos do Handshake */
    tls13_secrets_t secrets;
    /* Contextos de Cifragem (Tx = Envio, Rx = Recebimento) */
    tls13_cipher_ctx_t write_ctx;
    tls13_cipher_ctx_t read_ctx;
} tls13_session_t;

/* ============================================================================
 * PROTÓTIPOS - Sessão, Transcript e Key Schedule (tls13.c)
 * ============================================================================ */
int  tls13_session_init(tls13_session_t *session, int socket_fd, const char *hostname);
void tls13_transcript_update(tls13_session_t *session, const uint8_t *data, size_t len);
void tls13_transcript_get_hash(tls13_session_t *session, uint8_t *hash_out);
int  tls13_derive_handshake_keys(tls13_session_t *session);
int  tls13_derive_application_keys(tls13_session_t *session);
/* Finished (RFC 8446 §4.4.4): finished_key e verify_data */
int  tls13_derive_finished_key(const uint8_t traffic_secret[32], uint8_t out[32]);
int  tls13_finished_verify_data(const uint8_t traffic_secret[32],
                                const uint8_t transcript_hash[32],
                                uint8_t out[32]);

/* ============================================================================
 * PROTÓTIPOS - Handshake (tls13_handshake.c)
 * ============================================================================ */
int  tls13_send_client_hello(tls13_session_t *session);
int  tls13_process_server_hello(tls13_session_t *session, const uint8_t *data, size_t len);
int  tls13_handshake_step(tls13_session_t *session);

/* ============================================================================
 * PROTÓTIPOS - Camada de Registro (tls13_record.c)
 * ============================================================================ */
int  tls13_record_write(tls13_session_t *session, uint8_t content_type,
                        const uint8_t *payload, size_t len);
int  tls13_record_read(tls13_session_t *session, uint8_t *out_content_type,
                       uint8_t *out_payload, size_t *out_len);

#endif /* TLS13_H */
