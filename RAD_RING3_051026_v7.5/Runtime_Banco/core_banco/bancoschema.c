/* ============================================================================
   BANCOSCHEMA.C - Runtime_Banco / core_banco
   ----------------------------------------------------------------------------
   Responsabilidades deste core (na ordem):
     0) str_ieq        -> compara nomes ignorando maiusculas/minusculas
     1) bnum_*         -> conversoes numericas
     2) bspec_parse    -> spec "CODIGO:I4:PK:AI,NOME:C50:NN,..."
     3) header .TBL    -> bhdr_len / bcampo_off / btable_load
     4) catalogo .CAT  -> bcat_name/count/add/find/list/remove
     5) bfields_list   -> lista os campos p/ o Studio
   Correcoes desta versao:
     - bcat_find e bcat_remove usam str_ieq (acha "PRODUTO" digitando "produto")
     - bfields_list usa separador " | " SO ENTRE campos (sem sobra no fim,
       que fazia o Studio contar um campo fantasma)
   ============================================================================ */
#include "bancoschema.h"
#include "bancostore.h"
#include "../sys_banco/banco.h"
#include <string.h>

/* ============================================================================
   0) COMPARACAO SEM DISTINGUIR MAIUSCULAS ("PRODUTO" == "produto")
   ============================================================================ */
static int str_ieq(const char* a, const char* b) {
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 32) : *b;
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == *b;   /* iguais se acabaram juntos; diferentes se sobrou algo */
}

/* ============================================================================
   1) CONVERSORES NUMERICOS
   ============================================================================ */
void bnum_str(int v, char* buf) {
    int i = 0, t = 0, neg = 0;
    char tmp[12];
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) tmp[t++] = '0';
    while (v > 0 && t < 11) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    if (neg) buf[i++] = '-';
    for (int k = t - 1; k >= 0; k--) buf[i++] = tmp[k];
    buf[i] = 0;
}

int bnum_parse(const char* s) {
    int neg = 0, v = 0;
    if (!s || !*s) return 0;
    if (*s == '-')      { neg = 1; s++; }
    else if (*s == '+') { s++; }
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
    return neg ? -v : v;
}

/* ============================================================================
   2) PARSE DA SPEC: "CAMPO:TIPO[:PK][:AI][:NU|:NN],..."
      Retorna o numero de campos, ou -1 se a spec for invalida.
   ============================================================================ */
int bspec_parse(const char* spec, BCO_Campo* campos, int max) {
    int n = 0;
    const char* p = spec;
    while (*p && n < max) {
        BCO_Campo* c = &campos[n];
        memset(c, 0, sizeof(*c));
        /* nome do campo: ate ':' ou ',' */
        int i = 0;
        while (*p && *p != ':' && *p != ',' && i < 15) c->nome[i++] = *p++;
        c->nome[i] = 0;
        if (*p == ':') p++;
        /* tipo: I1 | I2 | I4 | C<n> */
        if      (p[0]=='I' && p[1]=='1') { c->tipo = BCO_T_I1; c->tam = 1; p += 2; }
        else if (p[0]=='I' && p[1]=='2') { c->tipo = BCO_T_I2; c->tam = 2; p += 2; }
        else if (p[0]=='I' && p[1]=='4') { c->tipo = BCO_T_I4; c->tam = 4; p += 2; }
        else if (p[0]=='C') {
            p++;
            int t = 0;
            while (*p >= '0' && *p <= '9') { t = t * 10 + (*p - '0'); p++; }
            if (t < 1 || t > 250) return -1;
            c->tipo = BCO_T_C; c->tam = (uint8_t)t;
        }
        else return -1;
        /* flags opcionais: :PK :AI :NU :NN */
        while (*p == ':') {
            p++;
            if      (p[0]=='P' && p[1]=='K') { c->flags |= BCO_F_PK;   p += 2; }
            else if (p[0]=='A' && p[1]=='I') { c->flags |= BCO_F_AI;   p += 2; }
            else if (p[0]=='N' && p[1]=='U') { c->flags |= BCO_F_NULL; p += 2; }
            else if (p[0]=='N' && p[1]=='N') { p += 2; }
            else return -1;
        }
        n++;
        if (*p == ',') p++;
    }
    return n;
}

/* ============================================================================
   3) HEADER DO .TBL
      [magic 8][tbl_id 4][num_campos 4][num_regs 4][next_pk 4][rec_size 4]
      [reserved 4]  = 32 bytes, + 32 bytes por campo, + registros
   ============================================================================ */
uint32_t bhdr_len(int ncampos) { return 32 + 32 * (uint32_t)ncampos; }

uint32_t bcampo_off(const BCO_Campo* c, int n, int idx) {
    uint32_t off = 2;                      /* byte viva + byte nullmap */
    for (int i = 0; i < idx && i < n; i++) off += c[i].tam;
    return off;
}

int btable_load(const char* arq, uint8_t* buf, uint32_t cap,
                BCO_Header* h, BCO_Campo* campos) {
    uint32_t len = 0;
    if (bstore_read_whole(arq, buf, cap, &len) != 0) return -1;
    if (len < 32) return -1;
    memcpy(h, buf, 32);
    if (h->magic[0]!='L' || h->magic[1]!='B' || h->magic[2]!='F') return -1;
    if (h->num_campos > BCO_MAXCAMPOS) return -1;
    if (campos) memcpy(campos, buf + 32, h->num_campos * 32);
    return 0;
}

/* ============================================================================
   4) CATALOGO DO BANCO (.CAT)
      [magic 8][num_entradas 4] + entradas de 48 bytes:
      [nome_tabela 24][nome_arquivo 13][tbl_id 4][pad 7]
   ============================================================================ */
#define CAT_ENTRY 48

static int cat_load(const char* banco, uint8_t* buf, uint32_t cap, uint32_t* n) {
    char cat[16];
    bcat_name(banco, cat);
    uint32_t len = 0;
    if (bstore_read_whole(cat, buf, cap, &len) != 0) return -1;
    if (len < 12) return -1;
    memcpy(n, buf + 8, 4);
    return 0;
}

void bcat_name(const char* banco, char* out) {
    strcpy(out, banco);
    strcat(out, ".CAT");
}

int bcat_count(const char* banco) {
    uint8_t b[2048]; uint32_t n = 0;
    if (cat_load(banco, b, sizeof(b), &n) != 0) return -1;
    return (int)n;
}

int bcat_add(const char* banco, const char* tabela, const char* arquivo) {
    uint8_t b[2048]; uint32_t n = 0;
    if (cat_load(banco, b, sizeof(b), &n) != 0) return BCO_ERR_NOTFOUND;
    if (n >= 40) return BCO_ERR_FULL;
    uint32_t id = n + 1;
    uint8_t* e = b + 12 + n * CAT_ENTRY;
    memset(e, 0, CAT_ENTRY);
    strncpy((char*)e,      tabela,  23);
    strncpy((char*)e + 24, arquivo, 12);
    memcpy(e + 40, &id, 4);
    n++;
    memcpy(b + 8, &n, 4);
    char cat[16];
    bcat_name(banco, cat);
    if (bstore_write_whole(cat, b, 12 + n * CAT_ENTRY) != 0) return BCO_ERR_IO;
    return (int)id;
}

/* Procura a tabela pelo nome (IGNORANDO maiusculas) e devolve o arquivo .TBL */
int bcat_find(const char* banco, const char* tabela, char* out_arq, int cap) {
    uint8_t b[2048]; uint32_t n = 0;
    if (cat_load(banco, b, sizeof(b), &n) != 0) return BCO_ERR_NOTFOUND;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t* e = b + 12 + i * CAT_ENTRY;
        if (str_ieq((const char*)e, tabela)) {              /* <- NOVO */
            strncpy(out_arq, (const char*)e + 24, cap - 1);
            out_arq[cap - 1] = 0;
            return 0;
        }
    }
    return BCO_ERR_NOTFOUND;
}

int bcat_list(const char* banco, char* out, int cap) {
    uint8_t b[2048]; uint32_t n = 0;
    out[0] = 0;
    if (cat_load(banco, b, sizeof(b), &n) != 0) return BCO_ERR_NOTFOUND;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t* e = b + 12 + i * CAT_ENTRY;
        if (i) strncat(out, ", ", cap - strlen(out) - 1);
        strncat(out, (const char*)e, cap - strlen(out) - 1);
    }
    return 0;
}

/* Remove a tabela do catalogo (usada pelo DROP TABLE) - tambem case-insensitive */
int bcat_remove(const char* banco, const char* tabela) {
    uint8_t b[2048]; uint32_t n = 0;
    if (cat_load(banco, b, sizeof(b), &n) != 0) return BCO_ERR_NOTFOUND;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t* e = b + 12 + i * CAT_ENTRY;
        if (str_ieq((const char*)e, tabela)) {              /* <- NOVO */
            memmove(e, e + CAT_ENTRY, (size_t)(n - i - 1) * CAT_ENTRY);
            n--;
            memcpy(b + 8, &n, 4);
            char cat[16];
            bcat_name(banco, cat);
            return (bstore_write_whole(cat, b, 12 + n * CAT_ENTRY) == 0) ? 0 : BCO_ERR_IO;
        }
    }
    return BCO_ERR_NOTFOUND;
}

/* ============================================================================
   5) LISTA DE CAMPOS (o que o PESQ. CAMPOS do Studio mostra)
      Formato: "nome:TIPO[:PK][:AI][:NU]" separados por " | "
      O separador entra SO ENTRE campos - nunca sobra no fim.
   ============================================================================ */
int bfields_list(const char* banco, const char* tabela, char* out, int cap) {
    char arq[16];
    if (bcat_find(banco, tabela, arq, sizeof(arq)) != 0) return BCO_ERR_NOTFOUND;
    static uint8_t tb[BCO_MAXFILE];
    BCO_Header h;
    BCO_Campo c[BCO_MAXCAMPOS];
    if (btable_load(arq, tb, sizeof(tb), &h, c) != 0) return BCO_ERR_IO;
    out[0] = 0;
    for (uint32_t i = 0; i < h.num_campos; i++) {
        char line[64];
        line[0] = 0;
        strcat(line, c[i].nome);
        strcat(line, ":");
        if (c[i].tipo == BCO_T_C) {
            char t[8];
            strcat(line, "C");
            bnum_str(c[i].tam, t);
            strcat(line, t);
        } else {
            strcat(line, (c[i].tipo == BCO_T_I4) ? "I4" :
                         (c[i].tipo == BCO_T_I2) ? "I2" : "I1");
        }
        if (c[i].flags & BCO_F_PK)   strcat(line, ":PK");
        if (c[i].flags & BCO_F_AI)   strcat(line, ":AI");
        if (c[i].flags & BCO_F_NULL) strcat(line, ":NU");
        if (i) strncat(out, " | ", cap - strlen(out) - 1);  /* so ENTRE campos */
        strncat(out, line, cap - strlen(out) - 1);
    }
    return 0;
}
