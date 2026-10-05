#ifndef CMD_H
#define CMD_H
#include <stdint.h>

/* ---- códigos de retorno (espelho de BCO_*) ---- */
#define CMD_OK            0
#define CMD_ERR_NOTFOUND -1   /* arquivo/dir nao existe          */
#define CMD_ERR_IO       -2   /* falha de leitura/gravação       */
#define CMD_ERR_EXISTS   -3   /* já existe (mkdir/touch/copy)    */
#define CMD_ERR_MEM      -4   /* heap sem memoria                */
#define CMD_ERR_ARGS     -5   /* argumentos inválidos            */
#define CMD_ERR_VERIFY   -6   /* pós-verificação falhou          */
#define CMD_ERR_FULL     -7   /* disco cheio                     */
#define CMD_ERR_RANGE    -8   /* offset/size fora do arquivo     */

/* ---- IDs de operação do dispatcher ---- */
#define CMD_OP_WRITE      1   /* cria/sobrescreve inteiro        */
#define CMD_OP_READ       2   /* lê inteiro p/ buffer + out_len  */
#define CMD_OP_APPEND     3   /* acrescenta no fim               */
#define CMD_OP_UPDATE_AT  4   /* grava em offset (random write)  */
#define CMD_OP_READ_AT    5   /* lê em offset                    */
#define CMD_OP_DELETE     6   /* remove arquivo                  */
#define CMD_OP_RENAME     7   /* renomeia                        */
#define CMD_OP_COPY       8   /* copia + verifica tamanho        */
#define CMD_OP_MOVE       9   /* copy+verify+delete              */
#define CMD_OP_MKDIR     10   /* cria pasta                      */
#define CMD_OP_TOUCH     11   /* cria vazio (falha se existe)    */
#define CMD_OP_STAT      12   /* size + attr                     */
#define CMD_OP_EXISTS    13   /* 1 existe / 0 não                */
#define CMD_OP_READDIR   14   /* entrada idx → nome/size/attr    */
#define CMD_OP_FIND      15   /* busca substring → offset        */
#define CMD_OP_HEXTEXT   16   /* dump hex formatado p/ GUI       */
#define CMD_OP_COMPARE   17   /* compara 2 arquivos byte a byte  */
#define CMD_OP_BACKUP    18   /* copy + compare + relatório      */
#define CMD_OP_CONCAT    19   /* anexa conteúdo de B em A        */
#define CMD_OP_WC        20   /* conta linhas e bytes            */
#endif /* CMD_H */
