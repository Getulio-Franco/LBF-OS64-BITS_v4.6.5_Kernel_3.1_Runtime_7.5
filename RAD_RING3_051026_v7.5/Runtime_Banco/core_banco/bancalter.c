/* ============================================================================
BANCALTER - ALTER TABLE nativo (Runtime_Banco)
Seguranca: 1) copia <ARQ>.OLD antes de qualquer rebuild
           2) registros migrados campo a campo (texto pad/trunca, int LE alarga)
           3) PK/AI nao removiveis; classe int<->texto nao conversivel
============================================================================ */
#include "bancalter.h"
#include "bancostore.h"
#include "bancoschema.h"
#include "bancocreate.h"
#include "../sys_banco/banco.h"
#include <string.h>

typedef struct {
    char       arq[16];
    BCO_Header hdr;
    BCO_Campo  cam[BCO_MAXCAMPOS];
    uint8_t*   body;          /* area de registros */
    uint32_t   body_len;
} BALTER_TBL;

static int balter_load(BALTER_TBL* t, const char* banco, const char* tabela) {
    if (!banco || !tabela) return BCO_ERR_NULL;
    if (bcat_find(banco, tabela, t->arq, sizeof(t->arq)) != 0) return BCO_ERR_NOTFOUND;
    static uint8_t buf[BCO_MAXFILE];
    uint32_t len = 0;
    if (bstore_read_whole(t->arq, buf, sizeof(buf), &len) != 0) return BCO_ERR_IO;
    if (len < 32) return BCO_ERR_IO;
    memcpy(&t->hdr, buf, 32);
    if (memcmp(t->hdr.magic, "LBFTBL1", 7) != 0) return BCO_ERR_IO;
    uint32_t n = t->hdr.num_campos;
    if (n == 0 || n > BCO_MAXCAMPOS) return BCO_ERR_IO;
    memcpy(t->cam, buf + 32, n * 32);
    uint32_t hdr_len = 32 + 32 * n;
    if (len < hdr_len) return BCO_ERR_IO;
    t->body     = buf + hdr_len;
    t->body_len = len - hdr_len;
    return BCO_OK;
}
static int cam_idx(BCO_Campo* cam, uint32_t n, const char* nome) {
    for (uint32_t i = 0; i < n; i++) if (strcmp(cam[i].nome, nome) == 0) return (int)i;
    return -1;
}
static void fill_default(const BCO_Campo* nc, uint8_t* nb, uint32_t noff) {
    for (uint32_t k = 0; k < nc->tam; k++) nb[noff + k] = (nc->tipo == 4) ? ' ' : 0;
}
static void migrate_field(const BCO_Campo* oc, const uint8_t* ob, uint32_t ooff,
                          const BCO_Campo* nc, uint8_t* nb, uint32_t noff) {
    if (nc->tipo == 4 && oc->tipo == 4) {                 /* texto: copia min, pad spaces */
        uint32_t m = (oc->tam < nc->tam) ? oc->tam : nc->tam;
        for (uint32_t k = 0; k < m; k++) nb[noff + k] = ob[ooff + k];
        for (uint32_t k = m; k < nc->tam; k++) nb[noff + k] = ' ';
    } else if (nc->tipo != 4 && oc->tipo != 4) {         /* int LE: alarga/estreia c/ sinal */
        int ow = (oc->tipo == 3) ? 4 : (oc->tipo == 2) ? 2 : 1;
        int32_t v = 0;
        for (int b = ow - 1; b >= 0; b--) v = (v << 8) | ob[ooff + b];
        if (ow < 4 && (ob[ooff + ow - 1] & 0x80)) v |= ~((1 << (ow * 8)) - 1);
        int nw = (nc->tipo == 3) ? 4 : (nc->tipo == 2) ? 2 : 1;
        for (int b = 0; b < nw; b++) nb[noff + b] = (uint8_t)((v >> (8 * b)) & 0xFF);
    } else {
        fill_default(nc, nb, noff);                      /* classe mudou: default */
    }
}
/* rebuild: novo layout + migracao; grava .OLD antes */
static int balter_commit(BALTER_TBL* t, BCO_Campo* nc, uint32_t newn, int* src) {
    uint32_t newrec = 2;
    for (uint32_t i = 0; i < newn; i++) newrec += nc[i].tam;
    uint32_t new_hdr = 32 + 32 * newn;
    uint32_t nreg    = t->hdr.num_regs;
    uint32_t newlen  = new_hdr + nreg * newrec;
    if (newlen > BCO_MAXFILE) return BCO_ERR_FULL;
    /* 1) copia de seguranca: ARQ.TBL -> ARQ.OLD */
    static uint8_t oldbuf[BCO_MAXFILE];
    uint32_t oldlen = 0;
    if (bstore_read_whole(t->arq, oldbuf, sizeof(oldbuf), &oldlen) != 0) return BCO_ERR_IO;
    char oldname[20];
    strcpy(oldname, t->arq);
    char* dot = strrchr(oldname, '.');
    if (dot) strcpy(dot + 1, "OLD"); else strcat(oldname, ".OLD");
    bstore_write_whole(oldname, oldbuf, oldlen);
    /* 2) header + descritores novos */
    static uint8_t out[BCO_MAXFILE];
    BCO_Header nh = t->hdr;
    nh.num_campos = newn;
    nh.rec_size   = newrec;
    memcpy(out, &nh, 32);
    memcpy(out + 32, nc, newn * 32);
    /* 3) migra registros */
    uint32_t ooff[BCO_MAXCAMPOS]; uint32_t o = 2;
    for (uint32_t i = 0; i < t->hdr.num_campos; i++) { ooff[i] = o; o += t->cam[i].tam; }
    for (uint32_t r = 0; r < nreg; r++) {
        uint32_t obase = r * t->hdr.rec_size;
        if (obase + t->hdr.rec_size > t->body_len) break;
        const uint8_t* ob = t->body + obase;
        uint8_t* nb = out + new_hdr + r * newrec;
        nb[0] = ob[0]; nb[1] = 0;
        uint32_t noff = 2;
        for (uint32_t j = 0; j < newn; j++) {
            int s = src[j];
            if (s >= 0) migrate_field(&t->cam[s], ob, ooff[s], &nc[j], nb, noff);
            else        fill_default(&nc[j], nb, noff);
            noff += nc[j].tam;
        }
    }
    if (bstore_write_whole(t->arq, out, newlen) != 0) return BCO_ERR_IO;
    return BCO_OK;
}
/* ---------------- operacoes publicas ---------------- */
int balter_rename_field(const char* banco, const char* tabela, const char* oldn, const char* newn) {
    BALTER_TBL t; int rc = balter_load(&t, banco, tabela); if (rc) return rc;
    uint32_t n = t.hdr.num_campos;
    if (!oldn || !newn || !newn[0] || strlen(newn) > 15) return BCO_ERR_PARSE;
    int i = cam_idx(t.cam, n, oldn);
    if (i < 0) return BCO_ERR_NOTFOUND;
    if (cam_idx(t.cam, n, newn) >= 0) return BCO_ERR_EXISTS;
    BCO_Campo nc[BCO_MAXCAMPOS]; int src[BCO_MAXCAMPOS];
    memcpy(nc, t.cam, n * 32);
    memset(nc[i].nome, 0, 16); strcpy(nc[i].nome, newn);
    for (uint32_t j = 0; j < n; j++) src[j] = (int)j;
    return balter_commit(&t, nc, n, src);
}
int balter_resize_field(const char* banco, const char* tabela, const char* nome, const char* spec_frag) {
    BALTER_TBL t; int rc = balter_load(&t, banco, tabela); if (rc) return rc;
    uint32_t n = t.hdr.num_campos;
    int i = cam_idx(t.cam, n, nome);
    if (i < 0) return BCO_ERR_NOTFOUND;
    BCO_Campo p; 
    if (bspec_parse(spec_frag, &p, 1) != 1) return BCO_ERR_PARSE;
    if ((p.tipo == 4) != (t.cam[i].tipo == 4)) return BCO_ERR_PARSE;  /* int<->texto recusado */
    if (p.tam < 1) return BCO_ERR_PARSE;
    BCO_Campo nc[BCO_MAXCAMPOS]; int src[BCO_MAXCAMPOS];
    memcpy(nc, t.cam, n * 32);
    nc[i].tipo = p.tipo; nc[i].tam = p.tam;
    for (uint32_t j = 0; j < n; j++) src[j] = (int)j;
    return balter_commit(&t, nc, n, src);
}
int balter_add_field(const char* banco, const char* tabela, const char* spec_frag) {
    BALTER_TBL t; int rc = balter_load(&t, banco, tabela); if (rc) return rc;
    uint32_t n = t.hdr.num_campos;
    if (n >= BCO_MAXCAMPOS) return BCO_ERR_FULL;
    BCO_Campo p;
    if (bspec_parse(spec_frag, &p, 1) != 1) return BCO_ERR_PARSE;
    if (cam_idx(t.cam, n, p.nome) >= 0) return BCO_ERR_EXISTS;
    BCO_Campo nc[BCO_MAXCAMPOS]; int src[BCO_MAXCAMPOS];
    memcpy(nc, t.cam, n * 32);
    nc[n] = p;
    for (uint32_t j = 0; j < n; j++) src[j] = (int)j;
    src[n] = -1;                                   /* campo novo = default */
    return balter_commit(&t, nc, n + 1, src);
}
int balter_drop_field(const char* banco, const char* tabela, const char* nome) {
    BALTER_TBL t; int rc = balter_load(&t, banco, tabela); if (rc) return rc;
    uint32_t n = t.hdr.num_campos;
    if (n <= 1) return BCO_ERR_PARSE;              /* manter >= 1 campo */
    int i = cam_idx(t.cam, n, nome);
    if (i < 0) return BCO_ERR_NOTFOUND;
    if (t.cam[i].flags & 1 || t.cam[i].flags & 2) return BCO_ERR_PARSE;  /* PK/AI protegidos */
    BCO_Campo nc[BCO_MAXCAMPOS]; int src[BCO_MAXCAMPOS];
    uint32_t j = 0;
    for (uint32_t k = 0; k < n; k++) {
        if ((int)k == i) continue;
        nc[j] = t.cam[k]; src[j] = (int)k; j++;
    }
    return balter_commit(&t, nc, j, src);
}
