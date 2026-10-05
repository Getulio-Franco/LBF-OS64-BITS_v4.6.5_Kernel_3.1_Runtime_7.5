/* bancodelete.c */
#include "bancodelete.h"
#include "bancostore.h"
#include "bancoschema.h"
#include "../sys_banco/banco.h" 
#include <string.h>
int bdelete(const char* banco, const char* tabela, const char* pk_valor){
    char arq[16]; if(bcat_find(banco,tabela,arq,sizeof(arq))!=0) return BCO_ERR_NOTFOUND;
    static uint8_t tb[BCO_MAXFILE]; BCO_Header h; BCO_Campo c[BCO_MAXCAMPOS];
    if(btable_load(arq,tb,sizeof(tb),&h,c)!=0) return BCO_ERR_IO;
    int pk=-1; for(uint32_t i=0;i<h.num_campos;i++) if(c[i].flags&BCO_F_PK){pk=(int)i;break;}
    if(pk<0) return BCO_ERR_PARSE;
    uint32_t hdr=bhdr_len((int)h.num_campos);
    for(uint32_t r=0;r<h.num_regs;r++){
        uint8_t* rec=tb+hdr+r*h.rec_size;
        if(rec[0]!=1) continue;
        char pv[64]; const BCO_Campo* cp=&c[pk];
        if(cp->tipo==BCO_T_C){ int m=cp->tam<63?cp->tam:63;
            memcpy(pv,rec+bcampo_off(c,(int)h.num_campos,pk),(uint32_t)m); pv[m]=0;
            while(m>0&&(pv[m-1]==0||pv[m-1]==' ')) pv[--m]=0; }
        else { int v=0; const uint8_t* s=rec+bcampo_off(c,(int)h.num_campos,pk);
            for(int i=0;i<cp->tam;i++) v|=((int)s[i])<<(8*i); bnum_str(v,pv); }
        if(strcmp(pv,pk_valor?pk_valor:"")!=0) continue;
        rec[0]=2; /* marca como deletado */
        return (bstore_write_whole(arq,tb,hdr+h.num_regs*h.rec_size)==0)?1:BCO_ERR_IO; }
    return BCO_ERR_NOTFOUND; }
int bdrop(const char* banco, const char* tabela){
    char arq[16]; if(bcat_find(banco,tabela,arq,sizeof(arq))!=0) return BCO_ERR_NOTFOUND;
    if(bstore_delete(arq)!=0) return BCO_ERR_IO;
    return bcat_remove(banco,tabela); }
