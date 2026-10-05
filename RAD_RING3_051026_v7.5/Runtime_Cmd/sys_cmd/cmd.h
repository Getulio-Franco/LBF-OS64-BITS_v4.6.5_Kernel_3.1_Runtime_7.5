#ifndef CMD_H
#define CMD_H
#include <stdint.h>

/* ---- codigos de retorno de TODOS os cores ---- */
#define CMD_OK           0
#define CMD_ERR_ARGS    -1
#define CMD_ERR_NOTFOUND -2
#define CMD_ERR_EXISTS  -3
#define CMD_ERR_IO      -4
#define CMD_ERR_VERIFY  -5
#define CMD_ERR_MEM     -6
#define CMD_ERR_FULL    -7

/* ---- IDs de operacao (faixas reservadas por core) ---- */
#define CMD_OP_CHDIR    1                      /* navegacao (syscall direta)   */
/* core_create: 10-19 */
#define CMD_OP_MKDIR   10
#define CMD_OP_TOUCH   11
#define CMD_OP_WRITE   12
/* core_copy: 20-29 */
#define CMD_OP_COPY    20
/* core_delete: 30-39 */
#define CMD_OP_DELETE  30
/* reservas futuras: core_read 40-49 | core_rename 50-59 | core_search 60-69 */
#endif /* CMD_H */
