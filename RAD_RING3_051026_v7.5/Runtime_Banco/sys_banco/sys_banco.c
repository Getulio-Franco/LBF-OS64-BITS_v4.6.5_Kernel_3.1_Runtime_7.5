#include "banco.h"
#include "../core_banco/bancostore.h"
#include "../core_banco/bancoschema.h"
#include "../core_banco/bancocreate.h"
#include "../core_banco/bancoinsert.h"
#include "../core_banco/bancoselect.h"
#include "../core_banco/bancoupdate.h"
#include "../core_banco/bancodelete.h"
#include "../core_banco/bancalter.h"
uint64_t banco_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
    (void)a5; (void)a6; (void)a7;
    switch (num) {
    case SYS_BANCO_CREATE_DB:    return (uint64_t)bcreate_db((const char*)a1);
    case SYS_BANCO_CREATE_TABLE: return (uint64_t)bcreate_table((const char*)a1,(const char*)a2,(const char*)a3);
    case SYS_BANCO_INSERT:       return (uint64_t)binsert((const char*)a1,(const char*)a2,(const char*)a3);
    case SYS_BANCO_SELECT:       return (uint64_t)bselect((const char*)a1,(const char*)a2,(const char*)a3,(const char*)a4,(char*)a5,(int)a6);
    case SYS_BANCO_UPDATE:       return (uint64_t)bupdate((const char*)a1,(const char*)a2,(const char*)a3,(const char*)a4);
    case SYS_BANCO_DELETE:       return (uint64_t)bdelete((const char*)a1,(const char*)a2,(const char*)a3);
    case SYS_BANCO_LIST_TABLES:  return (uint64_t)bcat_list((const char*)a1,(char*)a2,(int)a3);
    case SYS_BANCO_LIST_FIELDS:  return (uint64_t)bfields_list((const char*)a1,(const char*)a2,(char*)a3,(int)a4);
    case SYS_BANCO_COUNT:        return (uint64_t)bcount((const char*)a1,(const char*)a2);
    case SYS_BANCO_DROP_TABLE:   return (uint64_t)bdrop((const char*)a1,(const char*)a2);
    case SYS_BANCO_ALTER_RENAME: return (uint64_t)balter_rename_field((const char*)a1,(const char*)a2,(const char*)a3,(const char*)a4);
    case SYS_BANCO_ALTER_RESIZE: return (uint64_t)balter_resize_field((const char*)a1,(const char*)a2,(const char*)a3,(const char*)a4);
    case SYS_BANCO_ALTER_ADD:    return (uint64_t)balter_add_field((const char*)a1,(const char*)a2,(const char*)a3);
    case SYS_BANCO_ALTER_DROP:   return (uint64_t)balter_drop_field((const char*)a1,(const char*)a2,(const char*)a3);  /* ← CORRIGIDO: 3 args */
    default: return (uint64_t)-1;
    }
}
