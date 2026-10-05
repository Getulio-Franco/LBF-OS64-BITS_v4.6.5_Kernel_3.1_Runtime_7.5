/* bancoselect.c */
#include "bancoselect.h"
#include "bancostore.h"
#include "bancoschema.h"
#include "../sys_banco/banco.h"
#include <string.h>
static void dec_field(const uint8_t* src, const BCO_Campo* c, char* out, int cap){
    if(c->tipo==BCO_T_C){ int m=c->tam<cap-1?c->tam:cap-1; memcpy(out,src,(uint32_t)m); out[m]=0;
        while(m>0&&(out[m-1]==0||out[m-1]==' ')) out[--m]=0; }
    else { int v=0; for(int i=0;i<c->tam;i++) v|=((int)src[i])<<(8*i); bnum_str(v,out); } }
int bselect(const char* banco, const char* tabela, const char* campo, const char* valor, char* out, int cap){
    char arq[16]; if(bcat_find(banco,tabela,arq,sizeof(arq))!=0) return BCO_ERR_NOTFOUND;
    static uint8_t tb[BCO_MAXFILE]; BCO_Header h; BCO_Campo c[BCO_MAXCAMPOS];
    if(btable_load(arq,tb,sizeof(tb),&h,c)!=0) return BCO_ERR_IO;
    out[0]=0; int rows=0, fidx=-1;
    if(campo&&campo[0]){ for(uint32_t i=0;i<h.num_campos;i++)
        if(strcmp(c[i].nome,campo)==0){fidx=(int)i;break;} if(fidx<0) return BCO_ERR_NOTFOUND; }
    uint32_t hdr=bhdr_len((int)h.num_campos);
    for(uint32_t r=0;r<h.num_regs;r++){
        const uint8_t* rec=tb+hdr+r*h.rec_size;
        if(rec[0]!=1) continue;
        if(fidx>=0){ char fv[64]; dec_field(rec+bcampo_off(c,(int)h.num_campos,fidx),&c[fidx],fv,sizeof(fv));
            if(strcmp(fv,valor?valor:"")!=0) continue; }
        char line[256]; line[0]=0;
        for(uint32_t i=0;i<h.num_campos;i++){ char fv[64];
            dec_field(rec+bcampo_off(c,(int)h.num_campos,(int)i),&c[i],fv,sizeof(fv));
            strcat(line,c[i].nome); strcat(line,"=");
            strncat(line,fv,sizeof(line)-strlen(line)-1); strcat(line," | "); }
        strcat(line,"\n");
        strncat(out,line,cap-strlen(out)-1); rows++; }
    return rows; }
int bcount(const char* banco, const char* tabela){
    char arq[16]; if(bcat_find(banco,tabela,arq,sizeof(arq))!=0) return BCO_ERR_NOTFOUND;
    static uint8_t tb[BCO_MAXFILE]; BCO_Header h; BCO_Campo c[BCO_MAXCAMPOS];
    if(btable_load(arq,tb,sizeof(tb),&h,c)!=0) return BCO_ERR_IO;
    uint32_t hdr=bhdr_len((int)h.num_campos); int n=0;
    for(uint32_t r=0;r<h.num_regs;r++) if(tb[hdr+r*h.rec_size]==1) n++;
    return n; }
