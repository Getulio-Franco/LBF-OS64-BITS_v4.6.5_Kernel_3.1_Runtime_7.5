#ifndef BANCALTER_H
#define BANCALTER_H
#include <stdint.h>
/* ALTER TABLE seguro: rebuild com migracao de registros + copia .OLD antes */
int balter_rename_field(const char* banco, const char* tabela, const char* oldn, const char* newn);
int balter_resize_field(const char* banco, const char* tabela, const char* nome, const char* spec_frag);
int balter_add_field   (const char* banco, const char* tabela, const char* spec_frag);
int balter_drop_field  (const char* banco, const char* tabela, const char* nome);
#endif /* BANCALTER_H */
