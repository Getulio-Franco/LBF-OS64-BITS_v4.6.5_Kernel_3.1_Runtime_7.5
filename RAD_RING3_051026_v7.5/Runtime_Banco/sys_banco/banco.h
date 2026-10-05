#ifndef BANCO_H
#define BANCO_H
#include <stdint.h>
#include <stddef.h>
/* IDs de chamada (faixa 200-219, não colide com kernel 0-80 nem IA 100-119) */
#define SYS_BANCO_CREATE_DB     200
#define SYS_BANCO_CREATE_TABLE  201
#define SYS_BANCO_INSERT        202
#define SYS_BANCO_SELECT        203
#define SYS_BANCO_UPDATE        204
#define SYS_BANCO_DELETE        205
#define SYS_BANCO_LIST_TABLES   206
#define SYS_BANCO_LIST_FIELDS   207
#define SYS_BANCO_COUNT         208
#define SYS_BANCO_DROP_TABLE    209
#define SYS_BANCO_ALTER_RENAME  210
#define SYS_BANCO_ALTER_RESIZE  211
#define SYS_BANCO_ALTER_ADD     212
#define SYS_BANCO_ALTER_DROP    213
/* Erros padronizados */
#define BCO_OK            0
#define BCO_ERR_NULL     -1
#define BCO_ERR_NOTFOUND -2
#define BCO_ERR_FULL     -3
#define BCO_ERR_IO       -4
#define BCO_ERR_PARSE    -5
#define BCO_ERR_EXISTS   -6
#define BCO_ERR_CATALOGO -7   /* falhou ao atualizar o .CAT (dicionario) */
/* Dispatcher 100% Ring 3 (mesmo contrato do neural_syscall) */
uint64_t banco_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);
#endif /* BANCO_H */
