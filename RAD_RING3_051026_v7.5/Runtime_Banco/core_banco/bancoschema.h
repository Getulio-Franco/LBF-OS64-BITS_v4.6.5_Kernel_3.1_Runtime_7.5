/* bancoschema.h */
#ifndef BANCOSCHEMA_H
#define BANCOSCHEMA_H
#include <stdint.h>
#define BCO_MAXCAMPOS 16
#define BCO_MAXFILE  (32*1024)
#define BCO_T_I1 1
#define BCO_T_I2 2
#define BCO_T_I4 3
#define BCO_T_C  4
#define BCO_F_PK   1
#define BCO_F_AI   2
#define BCO_F_NULL 4
typedef struct { char nome[16]; uint8_t tipo; uint8_t tam; uint8_t flags; uint8_t pad[13]; } BCO_Campo;
typedef struct { char magic[8]; uint32_t tbl_id; uint32_t num_campos; uint32_t num_regs;
                 uint32_t next_pk; uint32_t rec_size; uint32_t reserved; } BCO_Header;
void bnum_str(int v, char* buf);
int  bnum_parse(const char* s);
int  bspec_parse(const char* spec, BCO_Campo* campos, int max);
uint32_t bhdr_len(int ncampos);
uint32_t bcampo_off(const BCO_Campo* c, int n, int idx);
int  btable_load(const char* arq, uint8_t* buf, uint32_t cap, BCO_Header* h, BCO_Campo* campos);
void bcat_name(const char* banco, char* out);
int  bcat_count(const char* banco);
int  bcat_add(const char* banco, const char* tabela, const char* arquivo);
int  bcat_find(const char* banco, const char* tabela, char* out_arq, int cap);
int  bcat_list(const char* banco, char* out, int cap);
int  bcat_remove(const char* banco, const char* tabela);
int  bfields_list(const char* banco, const char* tabela, char* out, int cap);
#endif
