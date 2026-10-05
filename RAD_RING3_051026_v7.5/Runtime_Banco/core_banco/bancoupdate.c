/* bancoupdate.c - UPDATE por PK (v4 final - paridade com bancoinsert v4)
   - SO a chave casada (pk) e preservada; demais campos atualizam
     (mesmo que a tabela tenha flags PK/AI sobrando nos outros campos)
   - AI com token vazio/"0" preserva o valor atual
   - PK em C decodificada com corte no primeiro NUL + apara espacos
   - campos C: clamp strlen/NUL + padding com ESPACOS (0x20), NUNCA zeros
     (os zeros do padding eram o gatilho do lixo na camada de baixo;
      o dec_field apara os espacos na leitura, entao o display fica limpo)
   Sem rastros de debug: versao de producao. */
#include "bancoupdate.h"
#include "bancostore.h"
#include "bancoschema.h"
#include "../sys_banco/banco.h"
#include <string.h>

int bupdate(const char* banco, const char* tabela, const char* pk_valor, const char* valores){
    char arq[16]; if(bcat_find(banco,tabela,arq,sizeof(arq))!=0) return BCO_ERR_NOTFOUND;
    static uint8_t tb[BCO_MAXFILE]; BCO_Header h; BCO_Campo c[BCO_MAXCAMPOS];
    if(btable_load(arq,tb,sizeof(tb),&h,c)!=0) return BCO_ERR_IO;
    int pk=-1; for(uint32_t i=0;i<h.num_campos;i++) if(c[i].flags&BCO_F_PK){pk=(int)i;break;}
    if(pk<0) return BCO_ERR_PARSE;
    uint32_t hdr=bhdr_len((int)h.num_campos);
    for(uint32_t r=0;r<h.num_regs;r++){
        uint8_t* rec=tb+hdr+r*h.rec_size;
        if(rec[0]!=1) continue;
        /* ---- LEITURA da PK desta linha (corte no NUL + apara espacos) ---- */
        char pv[64]; const BCO_Campo* cp=&c[pk];
        if(cp->tipo==BCO_T_C){
            int m=cp->tam<63?cp->tam:63;
            const uint8_t* s=rec+bcampo_off(c,(int)h.num_campos,pk);
            int L=0; while(L<m && s[L]!=0) L++;        /* para no primeiro NUL */
            while(L>0 && s[L-1]==' ') L--;             /* apara espacos do fim */
            memcpy(pv,s,(uint32_t)L); pv[L]=0;
        }
        else { int v=0; const uint8_t* s=rec+bcampo_off(c,(int)h.num_campos,pk);
            for(int i=0;i<cp->tam;i++) v|=((int)s[i])<<(8*i); bnum_str(v,pv); }
        if(strcmp(pv,pk_valor?pk_valor:"")!=0) continue;
        /* ---- ESCRITA dos valores NOVOS (codificacao identica ao insert) ---- */
        uint8_t nullmap=rec[1]; const char* p=valores?valores:"";
        for(uint32_t i=0;i<h.num_campos;i++){
            const char* tok=p; int tlen=0;
            while(*p&&*p!='|'){p++;tlen++;} if(*p=='|') p++;
            if((int)i==pk) continue;                                      /* so a chave casada */
            if((c[i].flags&BCO_F_AI)&&(tlen==0||(tlen==1&&tok[0]=='0'))) continue; /* AI vazio/0 preserva */
            if(tlen==0&&(c[i].flags&BCO_F_NULL)){ nullmap|=(uint8_t)(1<<i); continue; }
            nullmap&=(uint8_t)~(1<<i);
            uint8_t* dst=rec+bcampo_off(c,(int)h.num_campos,(int)i);
            if(c[i].tipo==BCO_T_C){
                int sl = tok ? (int)strlen(tok) : 0;                      /* para no NUL */
                if (sl < tlen) tlen = sl;
                int m = tlen < c[i].tam ? tlen : c[i].tam;
                memcpy(dst, tok, (uint32_t)m);
                for (int k = m; k < c[i].tam; k++) dst[k] = 0x20;         /* IGUAL ao insert: padding = espacos, SEM zero */
            }
            else { int v=bnum_parse(tok);
                for(int b=0;b<c[i].tam;b++) dst[b]=(uint8_t)((v>>(8*b))&0xFF); }
        }
        rec[1]=nullmap;
        return (bstore_write_whole(arq,tb,hdr+h.num_regs*h.rec_size)==0)?1:BCO_ERR_IO;
    }
    return BCO_ERR_NOTFOUND;
}
