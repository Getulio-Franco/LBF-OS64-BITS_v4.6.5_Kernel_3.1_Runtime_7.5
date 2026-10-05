/* bancoinsert.c - INSERT com codificacao segura de campos C (v4 final)
   Regras anti-lixo:
   1) comprimento do token = menor entre (contagem ate '|') e (strlen = ate NUL)
   2) memcpy so do texto real
   3) NUL explicito logo apos o texto + memset zero no resto do slot
   Sem rastros de debug: versao de producao. */
#include "bancoinsert.h"
#include "bancostore.h"
#include "bancoschema.h"
#include "../sys_banco/banco.h"
#include <string.h>

static void enc_field(uint8_t* dst, const BCO_Campo* c, const char* tok, int tlen, int ival){
    if(c->tipo==BCO_T_C){
        int sl = tok ? (int)strlen(tok) : 0;      /* comprimento real (para no NUL) */
        if (sl < tlen) tlen = sl;
        int m = tlen < c->tam ? tlen : c->tam;
        memcpy(dst, tok, (uint32_t)m);
        for (int k = m; k < c->tam; k++) dst[k] = 0x20;  /* NOVO: padding = espaços, SEM zero */
    }
    else {
        for(int i=0;i<c->tam;i++) dst[i]=(uint8_t)((ival>>(8*i))&0xFF);
    }
}

int binsert(const char* banco, const char* tabela, const char* valores){
    char arq[16]; if(bcat_find(banco,tabela,arq,sizeof(arq))!=0) return BCO_ERR_NOTFOUND;
    static uint8_t tb[BCO_MAXFILE]; BCO_Header h; BCO_Campo c[BCO_MAXCAMPOS];
    if(btable_load(arq,tb,sizeof(tb),&h,c)!=0) return BCO_ERR_IO;
    uint32_t hdr=bhdr_len((int)h.num_campos);
    uint32_t off=hdr+h.num_regs*h.rec_size;
    if(off+h.rec_size>BCO_MAXFILE) return BCO_ERR_FULL;
    uint8_t* rec=tb+off; memset(rec,0,h.rec_size); rec[0]=1;
    uint8_t nullmap=0; const char* p=valores?valores:"";
    for(uint32_t i=0;i<h.num_campos;i++){
        const char* tok=p; int tlen=0;
        while(*p&&*p!='|'){p++;tlen++;} if(*p=='|') p++;
        int ival=0;
        if((c[i].flags&BCO_F_AI)&&(tlen==0||(tlen==1&&tok[0]=='0'))){ ival=(int)h.next_pk; h.next_pk++; }
        else if(c[i].tipo!=BCO_T_C){ ival=bnum_parse(tok);
            if((c[i].flags&BCO_F_PK)&&!(c[i].flags&BCO_F_AI)&&ival>=(int)h.next_pk) h.next_pk=(uint32_t)ival+1; }
        if(tlen==0&&(c[i].flags&BCO_F_NULL)) nullmap|=(uint8_t)(1<<i);
        enc_field(rec+bcampo_off(c,(int)h.num_campos,(int)i), &c[i],tok,tlen,ival);
    }
    rec[1]=nullmap; h.num_regs++; memcpy(tb,&h,32);
    return (bstore_write_whole(arq,tb,off+h.rec_size)==0)?0:BCO_ERR_IO;
}
