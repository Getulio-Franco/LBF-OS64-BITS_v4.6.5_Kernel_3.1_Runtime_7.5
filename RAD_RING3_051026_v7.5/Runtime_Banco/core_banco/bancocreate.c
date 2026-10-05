/* bancocreate.c */
#include "bancocreate.h"
#include "bancostore.h"
#include "bancoschema.h"
#include "../sys_banco/banco.h" 
#include <string.h>
int bcreate_db(const char* banco){
    char cat[16]; bcat_name(banco,cat);
    if(bstore_exists(cat)) return BCO_ERR_EXISTS;
    uint8_t buf[12]; memcpy(buf,"LBFCAT1",7); buf[7]=0;
    uint32_t zero=0; memcpy(buf+8,&zero,4);
    return (bstore_write_whole(cat,buf,12)==0)?0:BCO_ERR_IO; }
int bcreate_table(const char* banco, const char* tabela, const char* spec){
    char arq[16];
    if(bcat_find(banco,tabela,arq,sizeof(arq))==0) return BCO_ERR_EXISTS;
    BCO_Campo c[BCO_MAXCAMPOS];
    int n=bspec_parse(spec,c,BCO_MAXCAMPOS); if(n<=0) return BCO_ERR_PARSE;
    uint32_t rec=2; for(int i=0;i<n;i++) rec+=c[i].tam;
    int id=bcat_count(banco); if(id<0) return BCO_ERR_NOTFOUND; id+=1;
    char base[5]; int k=0;
    for(;k<4&&banco[k];k++) base[k]=(banco[k]>='a'&&banco[k]<='z')?(char)(banco[k]-32):banco[k];
    for(;k<4;k++) base[k]='X'; base[4]=0;
    char num[8], seq[8]; bnum_str(id,num);
    int z=4-(int)strlen(num); for(int i=0;i<z;i++) seq[i]='0'; seq[z]=0; strcat(seq,num);
    strcpy(arq,base); strcat(arq,seq); strcat(arq,".TBL");
    static uint8_t tb[BCO_MAXFILE];
    BCO_Header h; memset(&h,0,sizeof(h));
    memcpy(h.magic,"LBFTBL1",7); h.tbl_id=(uint32_t)id; h.num_campos=(uint32_t)n;
    h.num_regs=0; h.next_pk=1; h.rec_size=rec;
    memcpy(tb,&h,32); memcpy(tb+32,c,(uint32_t)n*32);
    if (bstore_write_whole(arq, tb, bhdr_len(n)) != 0) return BCO_ERR_IO;        /* falhou gravar o .TBL */
    if (bcat_add(banco, tabela, arq) < 0)          return BCO_ERR_CATALOGO;     /* falhou atualizar o .CAT */
    return 0;}
