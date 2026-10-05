/* ============================================================================
 * SYS_TLS13.C - Dispatcher Syscall TLS 1.3 (LBF-OS64 Ring 3)
 * Elo entre libtls13.h (App/Browser) e Core_tls13 (Lógica/Handshake)
 * 
 * Dependências do Sistema:
 *   - ../system/sysutils.h  (sys_debug, sys_sleep)
 *   - ../system/liblib.h    (syscall serial infrastructure)
 *   - ../system/string.h    (memset, memcpy - fornecido por string.o)
 *   - net_poll_frame()      (extern - pilha de rede Ring 3)
 * ============================================================================ */

#include "sys_tls13.h"
#include "Runtime_Net/Core_tls13/tls13.h"

/* Headers do Sistema LBF-OS64 */
#include "../system/sysutils.h"  
#include "../system/liblib.h"    
#include "../system/string.h"    

/* Declaração externa da função de poll da pilha de rede */
extern void net_poll_frame(void);

#define MAX_TLS_SESSIONS 8

/* Pool de Sessões TLS (Estado Global do Dispatcher) */
static tls13_session_t g_tls_sessions[MAX_TLS_SESSIONS];
static int             g_tls_used[MAX_TLS_SESSIONS] = {0};

/* ============================================================================
 * FUNÇÕES UTILITÁRIAS LOCAIS (Freestanding - Sem Libc)
 * ============================================================================ */

static void local_strcpy(char *dest, const char *src) {
    if (!dest || !src) return;
    while (*src) *dest++ = *src++;
    *dest = '\0';
}

static void local_strcat(char *dest, const char *src) {
    if (!dest || !src) return;
    while (*dest) dest++;
    while (*src) *dest++ = *src++;
    *dest = '\0';
}

static size_t local_strlen(const char *str) {
    size_t len = 0;
    if (!str) return 0;
    while (str[len]) len++;
    return len;
}

/* itoa simples para debug (base 10) - evita dependência de libc */
static void local_itoa(uint64_t val, char *buf) {
    if (!buf) return;
    char tmp[24];
    int i = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (val > 0) {
        tmp[i++] = '0' + (char)(val % 10);
        val /= 10;
    }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* Busca slot de sessão pelo handle (1-based index) */
static tls13_session_t* get_session(int handle) {
    if (handle <= 0 || handle > MAX_TLS_SESSIONS) return NULL;
    if (!g_tls_used[handle - 1]) return NULL;
    return &g_tls_sessions[handle - 1];
}

/* ============================================================================
 * DISPATCHER PRINCIPAL
 * ============================================================================ */

uint64_t tls13_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    (void)a4; (void)a5; (void)a6; (void)a7;

    switch (num) {
    
    /* --- INICIALIZAÇÃO DE SESSÃO --- */
    case SYS_TLS13_INIT: {
        sys_debug("[TLS_SYS] INIT");
        int rc = (int)tls13_session_init((tls13_session_t *)a1, (int)a2, (const char *)a3);
        return (uint64_t)rc;
    }

    /* --- HANDSHAKE STEP (Modo Assíncrono / Manual) --- */
    case SYS_TLS13_HANDSHAKE_STEP: {
        tls13_session_t *s = (tls13_session_t *)a1;
        if (!s) return (uint64_t)TLS13_ERR_NULL;
        
        int rc = (int)tls13_handshake_step(s);
        
        /* Debug opcional via Serial */
        char buf[64];
        local_strcpy(buf, "[TLS_HS] State=");
        local_itoa((uint64_t)s->state, buf + local_strlen(buf));
        sys_debug(buf);
        
        return (uint64_t)rc;
    }

    /* --- CONEXÃO COMPLETA (Modo Bloqueante com Proteção) --- */
case SYS_TLS13_CONNECT: {
    int sockfd = (int)a1;
    const char *host = (const char *)a2;
    int slot = -1;
    sys_debug("[TLS_SYS] CONNECT Start");
    
    for (int i = 0; i < MAX_TLS_SESSIONS; i++) {
        if (!g_tls_used[i]) { slot = i; break; }
    }
    if (slot < 0) return 0;

    tls13_session_t *s = &g_tls_sessions[slot];
    if (tls13_session_init(s, sockfd, host) != 0) return 0;
    g_tls_used[slot] = 1;

    int steps = 0;
    int read_retries = 0; // Novo contador para retries de leitura

    while (s->state != TLS13_STATE_CONNECTED && s->state != TLS13_STATE_ERROR) {
        steps++;
        net_poll_frame(); 
        
        int rc = tls13_handshake_step(s);
        
        if (rc < 0) {
            // LÓGICA DE RECUPERAÇÃO:
            // Se estamos no estado CLIENT_HELLO_SENT (State=1) e falhamos,
            // provavelmente é porque o ServerHello ainda não chegou.
            // Vamos tentar novamente algumas vezes antes de declarar erro fatal.
            if (s->state == TLS13_STATE_CLIENT_HELLO_SENT && read_retries < 50) {
                read_retries++;
                sys_sleep(20); // Espera 20ms para a rede entregar o pacote
                continue;      // Tenta o mesmo step novamente
            }
            
            sys_debug("[TLS_HS] ERR: Fatal handshake failure");
            g_tls_used[slot] = 0;
            return 0;
        }
        
        // Se avançou de estado, reseta os retries
        if (s->state > TLS13_STATE_CLIENT_HELLO_SENT) {
            read_retries = 0;
        }

        if (steps > 500) {
            sys_debug("[TLS_HS] TIMEOUT");
            g_tls_used[slot] = 0;
            return 0;
        }
    }
    
    if (s->state == TLS13_STATE_CONNECTED) {
        sys_debug("[TLS_SYS] CONNECT OK");
        return (uint64_t)(slot + 1);
    } else {
        sys_debug("[TLS_SYS] CONNECT FAIL");
        g_tls_used[slot] = 0;
        return 0;
    }
}

    /* --- RECORD LAYER (Baixo Nível - Uso Avançado) --- */
    case SYS_TLS13_RECORD_WRITE:
        return (uint64_t)tls13_record_write(
            (tls13_session_t *)a1, (uint8_t)a2, 
            (const uint8_t *)a3, (size_t)a4);

    case SYS_TLS13_RECORD_READ:
        return (uint64_t)tls13_record_read(
            (tls13_session_t *)a1, (uint8_t *)a2, 
            (uint8_t *)a3, (size_t *)a4);

    /* --- API ALTO NÍVEL (Usada pelo browser.c) --- */
    case SYS_TLS13_WRITE: {
        tls13_session_t *s = get_session((int)a1);
        if (!s) {
            sys_debug("[TLS_SYS] WRITE: Invalid session handle");
            return (uint64_t)-1;
        }
        if (s->state != TLS13_STATE_CONNECTED) {
            sys_debug("[TLS_SYS] WRITE: Session not connected");
            return (uint64_t)-1;
        }
        return (uint64_t)tls13_record_write(
            s, TLS13_CONTENT_APPLICATION_DATA, 
            (const uint8_t *)a2, (size_t)a3);
    }

    case SYS_TLS13_READ: {
    tls13_session_t *s = get_session((int)a1);
    if (!s || s->state != TLS13_STATE_CONNECTED) return (uint64_t)-1;

    uint8_t type = 0;
    size_t out_len = 0;
    
    /* Poll antes da leitura */
    net_poll_frame(); 
    
    int ret = tls13_record_read(s, &type, (uint8_t *)a2, &out_len);
    
    if (ret < 0) {
        /* Se for um Alert de close_notify (tipo 21), é fim de conexão normal, não erro fatal */
        if (type == TLS13_CONTENT_ALERT) {
            sys_debug("[TLS_SYS] READ: Received Alert (connection closed by peer)");
            return 0; /* Retorna 0 bytes lidos, sinalizando EOF gracefully */
        }
        sys_debug("[TLS_SYS] READ: Record layer error");
        return (uint64_t)-1;
    }
    
    if (type != TLS13_CONTENT_APPLICATION_DATA) {
        /* Pode ser ChangeCipherSpec ou outro tipo válido pós-handshake */
        char dbg[64];
        local_strcpy(dbg, "[TLS_SYS] READ: Non-app data type=");
        local_itoa((uint64_t)type, dbg + local_strlen(dbg));
        sys_debug(dbg);
        return 0; /* Ignora e retorna 0 para o browser tentar novamente */
    }
    
    return (uint64_t)out_len;
}

    /* --- ENCERRAMENTO SEGURO --- */
    case SYS_TLS13_CLOSE: {
        int handle = (int)a1;
        if (handle > 0 && handle <= MAX_TLS_SESSIONS) {
            sys_debug("[TLS_SYS] CLOSE: Releasing session slot");
            g_tls_used[handle - 1] = 0;
            
            /* Zeroing seguro da sessão (previne vazamento de chaves privadas) */
            memset(&g_tls_sessions[handle - 1], 0, sizeof(tls13_session_t));
        }
        return 0;
    }

    default:
        sys_debug("[TLS_SYS] ERR: Unknown syscall number");
        return (uint64_t)-1;
    }
}
