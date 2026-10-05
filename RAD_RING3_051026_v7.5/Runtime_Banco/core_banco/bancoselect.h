/* bancoselect.h */
#ifndef BANCOSELECT_H
#define BANCOSELECT_H
int bselect(const char* banco, const char* tabela, const char* campo, const char* valor, char* out, int cap);
int bcount(const char* banco, const char* tabela);
#endif
