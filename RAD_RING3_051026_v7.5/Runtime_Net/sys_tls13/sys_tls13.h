#ifndef SYS_TLS13_H
#define SYS_TLS13_H

#include <stdint.h>
#include <stddef.h>

/* IDs de Syscall TLS 1.3 (Faixa 300-307) */
#define SYS_TLS13_INIT            300
#define SYS_TLS13_HANDSHAKE_STEP  301
#define SYS_TLS13_CONNECT         302
#define SYS_TLS13_RECORD_WRITE    303
#define SYS_TLS13_RECORD_READ     304
#define SYS_TLS13_WRITE           305
#define SYS_TLS13_READ            306
#define SYS_TLS13_CLOSE           307

/* Códigos de Retorno Padronizados */
#define TLS13_OK                   0
#define TLS13_ERR_NULL            -1
#define TLS13_ERR_STATE           -2
#define TLS13_ERR_RECORD          -3
#define TLS13_ERR_HANDSHAKE       -4
#define TLS13_ERR_IO              -5
#define TLS13_ERR_TIMEOUT         -6

/* Protótipo do Dispatcher (Elo entre Lib e Core) */
uint64_t tls13_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);

#endif /* SYS_TLS13_H */
