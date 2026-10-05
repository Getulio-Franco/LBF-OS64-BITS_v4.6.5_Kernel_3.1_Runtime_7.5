/* ============================================================================
BANCO STUDIO LBF v2.0 "CORINGA" - tudo-em-um do Runtime_Banco
Mantem 100% dos handlers v1.0 que ja funcionam (CRIAR DB/TAB, INSERT, SELECT,
UPDATE, DELETE, TABELAS, CAMPOS, COUNT, DROP, LIMPAR, SALVAR/LER LOG).
Adiciona: DUMP, EXPORTAR, IMPORTAR, TRUNCAR, CHECK, BACKUP, RESTAURAR, INFO.
============================================================================ */
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "../system/sysutils.h"
#include "Runtime_sdk/components/TOS_IPC.h"
#include "Runtime_Banco/lib_banco/libbanco.h"

void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth  = 640;
const int winHeight = 560;

TGUIControl* EditBanco   = NULL;
TGUIControl* BtnCriarDb  = NULL;
TGUIControl* EditTabela  = NULL;
TGUIControl* BtnCriarTab = NULL;
TGUIControl* EditSpec    = NULL;
TGUIControl* EditValores = NULL;
TGUIControl* EditPK      = NULL;
TGUIControl* EditCampo   = NULL;
TGUIControl* EditValor   = NULL;
TGUIControl* BtnInsert   = NULL;
TGUIControl* BtnSelect   = NULL;
TGUIControl* BtnUpdate   = NULL;
TGUIControl* BtnDelete   = NULL;
TGUIControl* BtnTabelas  = NULL;
TGUIControl* BtnCampos   = NULL;
TGUIControl* BtnCount    = NULL;
TGUIControl* BtnDrop     = NULL;
TGUIControl* BtnLimpar   = NULL;
TGUIControl* BtnSalvarL  = NULL;
TGUIControl* BtnLerL     = NULL;
TGUIControl* EditArqTxt  = NULL;
TGUIControl* BtnDump     = NULL;
TGUIControl* BtnExport   = NULL;
TGUIControl* BtnImport   = NULL;
TGUIControl* BtnTrunc    = NULL;
TGUIControl* BtnCheck    = NULL;
TGUIControl* EditSnap    = NULL;
TGUIControl* BtnBackup   = NULL;
TGUIControl* BtnRestore  = NULL;
TGUIControl* BtnInfo     = NULL;
TGUIControl* MemoLog     = NULL;

#define MAX_ARQ 24
static char g_arq[MAX_ARQ][16];

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;
/*char Obter_Tecla_Entrada(void) {
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0;
        return key;
    }
    return get_key();
}*/
/* Substituir a função Obter_Tecla_Entrada por uma leitura segura sem estourar o limite do slot IPC */
char Obter_Tecla_Entrada(void) {
    if (my_app_slot < 0) return get_key();
    
    /* Casting direto sobre a estrutura da janela sem adicionar bytes fora do limite */
    uint8_t* slot_bytes = (uint8_t*)&IPC_WINDOW_LIST[my_app_slot];
    
    /* Supondo que tem_evento_teclado e fila_teclado estejam localizados nos últimos bytes do slot IPC */
    volatile uint8_t* tem_evento = (volatile uint8_t*)(slot_bytes + sizeof(IPC_WINDOW_LIST[0]) - 2);
    volatile uint8_t* fila_tecla  = (volatile uint8_t*)(slot_bytes + sizeof(IPC_WINDOW_LIST[0]) - 1);

    if (*tem_evento == 1) {
        char key = (char)*fila_tecla;
        *tem_evento = 0;
        return key;
    }
    return get_key();
}
void Flush_Grafico_Janela(void) {
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}
void Tratar_Fechamento_Software(void) {
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50);
}
char* GUI_Memo_GetText(TGUIControl* memo) {
    if (!memo || !memo->Buffer) return NULL;
    return (char*)memo->Buffer;
}
static void log_line(const char* m) {
    char buf[300];
    strcpy(buf, m); strcat(buf, "\n");
    if (MemoLog) GUI_Memo_AddStr(MemoLog, buf);
    sys_debug(m);
    Flush_Grafico_Janela();
}
static void log_num(const char* label, int v) {
    char b[96]; char n[16];
    itoa((uint64_t)(uint32_t)v, n, 10);
    strcpy(b, label); strcat(b, n);
    log_line(b);
}
static int dentro(TGUIControl* c, int x, int y) {
    return c && x >= c->Left && x < (c->Left + c->Width) && y >= c->Top && y < (c->Top + c->Height);
}
/* ---------------- helpers coringa ---------------- */
static void upper_copy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        dst[i] = c; i++;
    }
    dst[i] = 0;
}
static void seq_name(char* out, const char* prefix, int n) {
    strcpy(out, prefix);
    char d[5];
    d[0] = (char)('0' + (n / 1000) % 10);
    d[1] = (char)('0' + (n / 100) % 10);
    d[2] = (char)('0' + (n / 10) % 10);
    d[3] = (char)('0' + n % 10);
    d[4] = 0;
    strcat(out, d);
}
static int max_seq(const char* prefix) {
    sys_fat_chdir("/");
    char nome[64]; file_info_t fi;
    int idx = 0, maxv = 0;
    int plen = (int)strlen(prefix);
    while (idx < 500 && sys_fat_readdir(idx, nome, &fi) == 1) {
        idx++;
        if (!(fi.attributes & 0x10)) continue;
        if (strncmp(nome, prefix, plen) != 0) continue;
        int v = 0, digits = 0;
        for (const char* p = nome + plen; *p; p++) {
            if (*p < '0' || *p > '9') { digits = -1; break; }
            v = v * 10 + (*p - '0'); digits++;
        }
        if (digits > 0 && v > maxv) maxv = v;
    }
    sys_fat_chdir("/");
    return maxv;
}
static int descobrir_arquivos(const char* banco) {
    sys_fat_chdir("/");
    int n = 0;
    char cat[16];
    upper_copy(cat, banco, 8);
    strcat(cat, ".CAT");
    file_info_t fi;
    if (sys_fat_stat(cat, &fi) == 0) strcpy(g_arq[n++], cat);
    char base4[5];
    upper_copy(base4, banco, 4);
    char nome[64];
    int idx = 0;
    while (idx < 500 && sys_fat_readdir(idx, nome, &fi) == 1) {
        idx++;
        if (fi.attributes & 0x10) continue;
        if (strncmp(nome, base4, 4) != 0) continue;
        int L = (int)strlen(nome);
        if (L >= 4 && strcmp(nome + L - 4, ".TBL") == 0 && n < MAX_ARQ)
            strcpy(g_arq[n++], nome);
    }
    sys_fat_chdir("/");
    return n;
}
/* ---------------- handlers v1.0 (INTACTOS) ---------------- */
void OnBtnCriarDbClick(void* s) {
    (void)s;
    char* banco = GUI_Edit_GetText(EditBanco);
    if (!banco || !banco[0]) { log_line("Nome do banco vazio!"); return; }
    int rc = sys_banco_create_db(banco);
    log_num("create_db rc = ", rc);
    if (rc == BCO_OK) log_line("BANCO criado no disco (.CAT).");
    else if (rc == BCO_ERR_EXISTS) log_line("Banco ja existe - pode prosseguir.");
    else log_line("Erro ao criar banco.");
}
void OnBtnCriarTabClick(void* s) {
    (void)s;
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    char* spec   = GUI_Edit_GetText(EditSpec);
    if (!banco || !banco[0] || !tabela || !tabela[0] || !spec || !spec[0]) {
        log_line("Preencha banco, tabela e spec!"); return;
    }
    int rc = sys_banco_create_table(banco, tabela, spec);
    log_num("create_table rc = ", rc);
    if (rc == BCO_OK) log_line("TABELA criada (.TBL + catalogo).");
    else if (rc == BCO_ERR_EXISTS) log_line("Tabela ja existe.");
    else if (rc == BCO_ERR_PARSE) log_line("Spec invalida! Ex: CODIGO:I4:PK:AI,NOME:C50:NN");
    else log_line("Erro ao criar tabela.");
}
void OnBtnInsertClick(void* s) {
    (void)s;
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    char* val    = GUI_Edit_GetText(EditValores);
    if (!banco || !banco[0] || !tabela || !tabela[0]) { log_line("Banco/tabela vazios!"); return; }
    int rc = sys_banco_insert(banco, tabela, val ? val : "");
    log_num("insert rc = ", rc);
    if (rc == BCO_OK) log_line("INSERT ok.");
    else if (rc == BCO_ERR_NOTFOUND) log_line("Tabela nao achada.");
    else if (rc == BCO_ERR_FULL) log_line("Tabela cheia.");
    else log_line("Erro no INSERT.");
}
void OnBtnSelectClick(void* s) {
    (void)s;
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    char* campo  = GUI_Edit_GetText(EditCampo);
    char* valor  = GUI_Edit_GetText(EditValor);
    static char out[2048];
    int n = sys_banco_select(banco, tabela,
                             (campo && campo[0]) ? campo : 0,
                             (campo && campo[0]) ? valor : 0,
                             out, sizeof(out));
    log_num("select linhas = ", n);
    if (n < 0) { log_line("Erro no SELECT."); return; }
    if (n == 0) { log_line("(sem linhas vivas)"); return; }
    log_line(out);
}
void OnBtnUpdateClick(void* s) {
    (void)s;
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    char* pk     = GUI_Edit_GetText(EditPK);
    char* val    = GUI_Edit_GetText(EditValores);
    int rc = sys_banco_update(banco, tabela, pk, val);
    log_num("update rc = ", rc);
    if (rc == 1) log_line("UPDATE aplicado.");
    else if (rc == BCO_ERR_NOTFOUND) log_line("PK nao encontrada.");
    else log_line("Erro no UPDATE.");
}
void OnBtnDeleteClick(void* s) {
    (void)s;
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    char* pk     = GUI_Edit_GetText(EditPK);
    int rc = sys_banco_delete(banco, tabela, pk);
    log_num("delete rc = ", rc);
    if (rc == 1) log_line("DELETE ok (marcado removido).");
    else if (rc == BCO_ERR_NOTFOUND) log_line("PK nao encontrada.");
    else log_line("Erro no DELETE.");
}
void OnBtnTabelasClick(void* s) {
    (void)s;
    char* banco = GUI_Edit_GetText(EditBanco);
    static char out[1024];
    int rc = sys_banco_list_tables(banco, out, sizeof(out));
    log_num("list_tables rc = ", rc);
    if (rc == BCO_OK) log_line(out[0] ? out : "(sem tabelas)");
    else log_line("Banco nao encontrado.");
}
void OnBtnCamposClick(void* s) {
    (void)s;
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    static char out[1024];
    int rc = sys_banco_list_fields(banco, tabela, out, sizeof(out));
    log_num("list_fields rc = ", rc);
    if (rc == BCO_OK) log_line(out[0] ? out : "(sem campos)");
    else log_line("Tabela nao encontrada.");
}
void OnBtnCountClick(void* s) {
    (void)s;
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    log_num("registros vivos = ", sys_banco_count(banco, tabela));
}
void OnBtnDropClick(void* s) {
    (void)s;
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    int rc = sys_banco_drop_table(banco, tabela);
    log_num("drop rc = ", rc);
    if (rc == BCO_OK) log_line("Tabela removida do disco e catalogo.");
    else log_line("Erro no DROP.");
}
void OnBtnLimparClick(void* s) { (void)s; GUI_Memo_Clear(MemoLog); Flush_Grafico_Janela(); }
void OnBtnSalvarLogClick(void* s) {
    (void)s;
    char* txt = GUI_Memo_GetText(MemoLog);
    if (!txt) txt = "";
    int st = sys_fat_write("0:/banco_log.txt", (void*)txt, (uint32_t)strlen(txt));
    log_num("salvar log = ", st);
}
void OnBtnLerLogClick(void* s) {
    (void)s;
    char buffer[4097]; memset(buffer, 0, sizeof(buffer));
    int n = sys_fat_read("0:/banco_log.txt", (void*)buffer, 4096);
    GUI_Memo_Clear(MemoLog);
    if (n > 0) GUI_Memo_AddStr(MemoLog, buffer);
    else GUI_Memo_AddStr(MemoLog, "(banco_log.txt vazio/inexistente)\n");
    Flush_Grafico_Janela();
}
/* ---------------- handlers NOVOS (coringa) ---------------- */
void OnBtnDumpClick(void* s) {
    (void)s;
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    static char out[4096];
    int n = sys_banco_select(banco, tabela, 0, 0, out, sizeof(out));
    log_num("dump linhas = ", n);
    if (n > 0) log_line(out); else log_line("(sem linhas vivas)");
}
void OnBtnExportClick(void* s) {
    (void)s;
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    char* campo  = GUI_Edit_GetText(EditCampo);
    char* valor  = GUI_Edit_GetText(EditValor);
    char* arq    = GUI_Edit_GetText(EditArqTxt);
    if (!arq || !arq[0]) arq = "EXPORT.TXT";
    static char out[4096];
    int n = sys_banco_select(banco, tabela,
                             (campo && campo[0]) ? campo : 0,
                             (campo && campo[0]) ? valor : 0,
                             out, sizeof(out));
    if (n < 0) { log_line("export: select falhou."); return; }
    int st = sys_fat_write(arq, out, (uint32_t)strlen(out));
    log_num("export linhas = ", n);
    log_num("export write rc = ", st);
    if (st == 0) { char m[96]; strcpy(m, "exportado p/ "); strcat(m, arq); log_line(m); }
}
void OnBtnImportClick(void* s) {
    (void)s;
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    char* arq    = GUI_Edit_GetText(EditArqTxt);
    if (!arq || !arq[0]) { log_line("import: digite o arquivo em Arq:."); return; }
    static char buf[4097]; memset(buf, 0, sizeof(buf));
    int rd = sys_fat_read(arq, buf, 4096);
    if (rd <= 0) { log_line("import: arquivo vazio/inexistente."); return; }
    buf[rd] = 0;
    int ins = 0;
    char* p = buf;
    while (p && *p) {
        char* nl = strchr(p, '\n');
        if (nl) *nl = 0;
        int l = (int)strlen(p);
        if (l && p[l - 1] == '\r') p[l - 1] = 0;
        if (l > 0 && strchr(p, '|')) {
            if (sys_banco_insert(banco, tabela, p) == BCO_OK) ins++;
        }
        p = nl ? nl + 1 : 0;
    }
    log_num("import inseridos = ", ins);
}
void OnBtnTruncClick(void* s) {
    (void)s;
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    static char raw[1024];
    int rc = sys_banco_list_fields(banco, tabela, raw, sizeof(raw));
    if (rc != BCO_OK) { log_line("truncar: tabela nao encontrada."); return; }
    for (char* q = raw; *q; q++) if (*q == '|') *q = ',';   /* fragments -> spec */
    int rd = sys_banco_drop_table(banco, tabela);
    log_num("drop rc = ", rd);
    int rc2 = sys_banco_create_table(banco, tabela, raw);
    log_num("recreate rc = ", rc2);
    if (rc2 == BCO_OK) log_line("TABELA truncada (estrutura mantida, registros zerados).");
}
/* helper: magic esperado pela extensao (mesmo criterio do RECOVER v3.1) */
static const char* ext_magic(const char* nome) {
    int L = (int)strlen(nome);
    if (L >= 4 && strcmp(nome + L - 4, ".TBL") == 0) return "LBFTBL1";
    if (L >= 4 && strcmp(nome + L - 4, ".CAT") == 0) return "LBFCAT1";
    return NULL;
}
void OnBtnCheckClick(void* s) {
    (void)s;
    char* banco = GUI_Edit_GetText(EditBanco);
    if (!banco || !banco[0]) { log_line("CHECK: nome do banco vazio!"); return; }
    static uint8_t hb[16];
    int n = descobrir_arquivos(banco);
    if (n == 0) { log_line("CHECK: nenhum arquivo do banco na raiz."); return; }
    log_line("--- CHECK DO BANCO (magic por extensao) ---");
    int okall = 1;
    for (int i = 0; i < n; i++) {
        const char* esp = ext_magic(g_arq[i]);       /* .CAT -> LBFCAT1 | .TBL -> LBFTBL1 */
        int r = sys_fat_read(g_arq[i], hb, 8);
        int ok = (esp != NULL && r >= 7 && memcmp(hb, esp, 7) == 0);
        char m[96];
        strcpy(m, "CHECK "); strcat(m, g_arq[i]);
        if (!esp)      strcat(m, ": extensao desconhecida!");
        else if (ok)   strcat(m, ": magic OK ("); 
        else           strcat(m, ": MAGIC CORROMPIDO! (esperado ");
        if (esp) { strcat(m, esp); strcat(m, ok ? ")" : ")"); }
        log_line(m);
        if (!ok) okall = 0;
    }
    log_num("check arquivos = ", n);
    log_line(okall ? "CHECK: banco INTEGRO." : "CHECK: ha arquivo(s) corrompido(s)! Use RECOVER/RESTAURAR.");
}
void OnBtnBackupClick(void* s) {
    (void)s;
    char* banco = GUI_Edit_GetText(EditBanco);
    int n = descobrir_arquivos(banco);
    if (n == 0) { log_line("backup: nada p/ copiar."); return; }
    int seq = max_seq("BAK") + 1;
    char snap[12]; seq_name(snap, "BAK", seq);
    if (sys_fat_mkdir(snap) != 0) { log_line("backup: falha ao criar snap."); return; }
    int ok = 0;
    for (int i = 0; i < n; i++) {
        char dest[32];
        strcpy(dest, snap); strcat(dest, "/"); strcat(dest, g_arq[i]);
        if (sys_fat_copy(g_arq[i], dest) == 0) ok++;
    }
    char m[96]; char num[8];
    strcpy(m, "BACKUP -> "); strcat(m, snap); strcat(m, " (");
    itoa((uint64_t)(uint32_t)ok, num, 10); strcat(m, num); strcat(m, "/");
    itoa((uint64_t)(uint32_t)n, num, 10); strcat(m, num); strcat(m, ")");
    log_line(m);
    GUI_Edit_SetText(EditSnap, snap);
}
void OnBtnRestoreClick(void* s) {
    (void)s;
    char* snap = GUI_Edit_GetText(EditSnap);
    if (!snap || !snap[0]) { log_line("restore: digite o snap (BAKnnnn)."); return; }
    if (sys_fat_chdir(snap) != 0) { log_line("restore: snap nao encontrado."); sys_fat_chdir("/"); return; }
    char nome[64]; file_info_t fi;
    int idx = 0, ok = 0;
    static uint8_t buf[4096];
    while (idx < 500 && sys_fat_readdir(idx, nome, &fi) == 1) {
        idx++;
        if (fi.attributes & 0x10) continue;
        int rd = sys_fat_read(nome, buf, 4096);
        if (rd > 0) {
            sys_fat_chdir("/");
            if (sys_fat_write(nome, buf, (uint32_t)rd) == 0) ok++;
            sys_fat_chdir(snap);
        }
    }
    sys_fat_chdir("/");
    log_num("restore arquivos = ", ok);
}
void OnBtnInfoClick(void* s) {
    (void)s;
    char* banco = GUI_Edit_GetText(EditBanco);
    int n = descobrir_arquivos(banco);
    uint32_t total = 0;
    for (int i = 0; i < n; i++) {
        file_info_t fi;
        if (sys_fat_stat(g_arq[i], &fi) == 0) {
            char m[96]; char sz[12];
            strcpy(m, "  "); strcat(m, g_arq[i]); strcat(m, "  ");
            itoa(fi.size, sz, 10); strcat(m, sz); strcat(m, " B");
            log_line(m);
            total += fi.size;
        }
    }
    char m[96]; char sz[16];
    strcpy(m, "INFO total = "); itoa(total, sz, 10); strcat(m, sz); strcat(m, " B");
    log_line(m);
}
/* ---------------- main ---------------- */
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;
    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("Banco Studio LBF", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Banco Studio v2.0 Coringa", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);
    /* linha 1: banco/tabela/criacoes (intacta) */
    GUI_CreateLabel(&MyApp, 10, 42, "Banco:");
    EditBanco  = GUI_CreateEdit(&MyApp, 60, 38, 120, 25, "MERCADO", NULL);
    BtnCriarDb = GUI_CreateButton(&MyApp, 185, 38, 125, 25, "CRIAR BANCO", OnBtnCriarDbClick);
    GUI_CreateLabel(&MyApp, 320, 42, "Tabela:");
    EditTabela  = GUI_CreateEdit(&MyApp, 370, 38, 120, 25, "CLIENTES", NULL);
    BtnCriarTab = GUI_CreateButton(&MyApp, 495, 38, 135, 25, "CRIAR TABELA", OnBtnCriarTabClick);
    /* linha 2: spec (intacta) */
    GUI_CreateLabel(&MyApp, 10, 72, "Spec:");
    EditSpec = GUI_CreateEdit(&MyApp, 60, 68, 530, 25, "CODIGO:I4:PK:AI,NOME:C50:NN,NUMERO:I4:NU", NULL);
    /* linha 3: valores + PK (intacta) */
    GUI_CreateLabel(&MyApp, 10, 102, "Valores:");
    EditValores = GUI_CreateEdit(&MyApp, 60, 98, 330, 25, "0|JOSE DA SILVA|123", NULL);
    GUI_CreateLabel(&MyApp, 400, 102, "PK:");
    EditPK = GUI_CreateEdit(&MyApp, 430, 98, 60, 25, "1", NULL);
    /* linha 4: filtro (intacta) */
    GUI_CreateLabel(&MyApp, 10, 132, "Filtro:");
    EditCampo = GUI_CreateEdit(&MyApp, 60, 128, 140, 25, "CODIGO", NULL);
    EditValor = GUI_CreateEdit(&MyApp, 210, 128, 140, 25, "1", NULL);
    GUI_CreateLabel(&MyApp, 360, 132, "(campo vazio = todas)");
    /* linha 5: DML (intacta) */
    BtnInsert  = GUI_CreateButton(&MyApp, 10,  158, 80, 25, "INSERT",  OnBtnInsertClick);
    BtnSelect  = GUI_CreateButton(&MyApp, 95,  158, 80, 25, "SELECT",  OnBtnSelectClick);
    BtnUpdate  = GUI_CreateButton(&MyApp, 180, 158, 80, 25, "UPDATE",  OnBtnUpdateClick);
    BtnDelete  = GUI_CreateButton(&MyApp, 265, 158, 80, 25, "DELETE",  OnBtnDeleteClick);
    BtnTabelas = GUI_CreateButton(&MyApp, 350, 158, 80, 25, "TABELAS", OnBtnTabelasClick);
    BtnCampos  = GUI_CreateButton(&MyApp, 435, 158, 80, 25, "CAMPOS",  OnBtnCamposClick);
    BtnCount   = GUI_CreateButton(&MyApp, 520, 158, 70, 25, "COUNT",   OnBtnCountClick);
    /* linha 6: admin (intacta) + Arq p/ export/import */
    BtnDrop    = GUI_CreateButton(&MyApp, 10,  188, 80, 25, "DROP TAB", OnBtnDropClick);
    BtnLimpar  = GUI_CreateButton(&MyApp, 95,  188, 80, 25, "LIMPAR",   OnBtnLimparClick);
    BtnSalvarL = GUI_CreateButton(&MyApp, 180, 188, 90, 25, "SALVAR LOG", OnBtnSalvarLogClick);
    BtnLerL    = GUI_CreateButton(&MyApp, 275, 188, 80, 25, "LER LOG",  OnBtnLerLogClick);
    GUI_CreateLabel(&MyApp, 365, 192, "Arq:");
    EditArqTxt = GUI_CreateEdit(&MyApp, 395, 188, 160, 25, "EXPORT.TXT", NULL);
    /* linha 7: coringa parte 1 */
    BtnDump   = GUI_CreateButton(&MyApp, 10,  218, 55, 25, "DUMP",     OnBtnDumpClick);
    BtnExport = GUI_CreateButton(&MyApp, 70,  218, 95, 25, "EXPORTAR", OnBtnExportClick);
    BtnImport = GUI_CreateButton(&MyApp, 170, 218, 95, 25, "IMPORTAR", OnBtnImportClick);
    BtnTrunc  = GUI_CreateButton(&MyApp, 270, 218, 85, 25, "TRUNCAR",  OnBtnTruncClick);
    BtnCheck  = GUI_CreateButton(&MyApp, 360, 218, 65, 25, "CHECK",    OnBtnCheckClick);
    /* linha 8: coringa parte 2 (snap/backup/restore/info) */
    GUI_CreateLabel(&MyApp, 10, 252, "Snap:");
    EditSnap   = GUI_CreateEdit(&MyApp, 48, 248, 92, 25, "", NULL);
    BtnBackup  = GUI_CreateButton(&MyApp, 145, 248, 75, 25, "BACKUP",   OnBtnBackupClick);
    BtnRestore = GUI_CreateButton(&MyApp, 225, 248, 105, 25, "RESTAURAR", OnBtnRestoreClick);
    BtnInfo    = GUI_CreateButton(&MyApp, 335, 248, 55, 25, "INFO",     OnBtnInfoClick);
    /* memo */
    MemoLog = GUI_CreateMemo(&MyApp, 10, 280, 620, 270);
    gui_set_prop(MemoLog, PROP_COLOR, 0x000000);
    GUI_Memo_AddStr(MemoLog, "Banco Studio v2.0 CORINGA pronto.\n");
    GUI_Memo_AddStr(MemoLog, "DDL+DML classici + DUMP/EXPORT/IMPORT/TRUNCAR/CHECK/BACKUP/RESTAURAR/INFO.\n\n");
    g_focused_control = (void*)EditBanco;
    ultimo_controle_focado = (void*)EditBanco;
    gui_set_prop(EditBanco, PROP_SET_FOCUS, 1);
    Flush_Grafico_Janela();
    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) { Tratar_Fechamento_Software(); break; }
        bool euTenhoFoco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco;
        bool precisa_redesenhar = false;
        if (primeiro_desenho) { primeiro_desenho = false; precisa_redesenhar = true; }
        if (euTenhoFoco != ultimo_estado_foco) { ultimo_estado_foco = euTenhoFoco; precisa_redesenhar = true; }
        if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
        else if (ultimo_controle_focado != NULL) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado, PROP_SET_FOCUS, 1);
        }
        if (euTenhoFoco) {
            char key = Obter_Tecla_Entrada();
            if (key != 0) { GUI_ProcessKeyboard(&MyApp, key); precisa_redesenhar = true; }
            if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
                if (mouse_hold_timer == 0) {
                    int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                    int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                    ultimo_x = rel_x; ultimo_y = rel_y; mouse_hold_timer = 2;
                    if      (dentro(BtnCriarDb,  rel_x, rel_y)) gui_set_prop(BtnCriarDb,  PROP_STATE, 2);
                    else if (dentro(BtnCriarTab, rel_x, rel_y)) gui_set_prop(BtnCriarTab, PROP_STATE, 2);
                    else if (dentro(BtnInsert,   rel_x, rel_y)) gui_set_prop(BtnInsert,   PROP_STATE, 2);
                    else if (dentro(BtnSelect,   rel_x, rel_y)) gui_set_prop(BtnSelect,   PROP_STATE, 2);
                    else if (dentro(BtnUpdate,   rel_x, rel_y)) gui_set_prop(BtnUpdate,   PROP_STATE, 2);
                    else if (dentro(BtnDelete,   rel_x, rel_y)) gui_set_prop(BtnDelete,   PROP_STATE, 2);
                    else if (dentro(BtnTabelas,  rel_x, rel_y)) gui_set_prop(BtnTabelas,  PROP_STATE, 2);
                    else if (dentro(BtnCampos,   rel_x, rel_y)) gui_set_prop(BtnCampos,   PROP_STATE, 2);
                    else if (dentro(BtnCount,    rel_x, rel_y)) gui_set_prop(BtnCount,    PROP_STATE, 2);
                    else if (dentro(BtnDrop,     rel_x, rel_y)) gui_set_prop(BtnDrop,     PROP_STATE, 2);
                    else if (dentro(BtnLimpar,   rel_x, rel_y)) gui_set_prop(BtnLimpar,   PROP_STATE, 2);
                    else if (dentro(BtnSalvarL,  rel_x, rel_y)) gui_set_prop(BtnSalvarL,  PROP_STATE, 2);
                    else if (dentro(BtnLerL,     rel_x, rel_y)) gui_set_prop(BtnLerL,     PROP_STATE, 2);
                    else if (dentro(BtnDump,     rel_x, rel_y)) gui_set_prop(BtnDump,     PROP_STATE, 2);
                    else if (dentro(BtnExport,   rel_x, rel_y)) gui_set_prop(BtnExport,   PROP_STATE, 2);
                    else if (dentro(BtnImport,   rel_x, rel_y)) gui_set_prop(BtnImport,   PROP_STATE, 2);
                    else if (dentro(BtnTrunc,    rel_x, rel_y)) gui_set_prop(BtnTrunc,    PROP_STATE, 2);
                    else if (dentro(BtnCheck,    rel_x, rel_y)) gui_set_prop(BtnCheck,    PROP_STATE, 2);
                    else if (dentro(BtnBackup,   rel_x, rel_y)) gui_set_prop(BtnBackup,   PROP_STATE, 2);
                    else if (dentro(BtnRestore,  rel_x, rel_y)) gui_set_prop(BtnRestore,  PROP_STATE, 2);
                    else if (dentro(BtnInfo,     rel_x, rel_y)) gui_set_prop(BtnInfo,     PROP_STATE, 2);
                    events_process_mouse(rel_x, rel_y, 1, 0);
                    if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                        precisa_redesenhar = true;
                        if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
                    }
                }
                IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
            }
            if (mouse_hold_timer > 0) {
                mouse_hold_timer--;
                if (mouse_hold_timer == 0) {
                    if (BtnCriarDb)  gui_set_prop(BtnCriarDb,  PROP_STATE, 0);
                    if (BtnCriarTab) gui_set_prop(BtnCriarTab, PROP_STATE, 0);
                    if (BtnInsert)   gui_set_prop(BtnInsert,   PROP_STATE, 0);
                    if (BtnSelect)   gui_set_prop(BtnSelect,   PROP_STATE, 0);
                    if (BtnUpdate)   gui_set_prop(BtnUpdate,   PROP_STATE, 0);
                    if (BtnDelete)   gui_set_prop(BtnDelete,   PROP_STATE, 0);
                    if (BtnTabelas)  gui_set_prop(BtnTabelas,  PROP_STATE, 0);
                    if (BtnCampos)   gui_set_prop(BtnCampos,   PROP_STATE, 0);
                    if (BtnCount)    gui_set_prop(BtnCount,    PROP_STATE, 0);
                    if (BtnDrop)     gui_set_prop(BtnDrop,     PROP_STATE, 0);
                    if (BtnLimpar)   gui_set_prop(BtnLimpar,   PROP_STATE, 0);
                    if (BtnSalvarL)  gui_set_prop(BtnSalvarL,  PROP_STATE, 0);
                    if (BtnLerL)     gui_set_prop(BtnLerL,     PROP_STATE, 0);
                    if (BtnDump)     gui_set_prop(BtnDump,     PROP_STATE, 0);
                    if (BtnExport)   gui_set_prop(BtnExport,   PROP_STATE, 0);
                    if (BtnImport)   gui_set_prop(BtnImport,   PROP_STATE, 0);
                    if (BtnTrunc)    gui_set_prop(BtnTrunc,    PROP_STATE, 0);
                    if (BtnCheck)    gui_set_prop(BtnCheck,    PROP_STATE, 0);
                    if (BtnBackup)   gui_set_prop(BtnBackup,   PROP_STATE, 0);
                    if (BtnRestore)  gui_set_prop(BtnRestore,  PROP_STATE, 0);
                    if (BtnInfo)     gui_set_prop(BtnInfo,     PROP_STATE, 0);
                    events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                    precisa_redesenhar = true;
                }
            }
        }
        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(euTenhoFoco ? 16 : 32);
    }
    sys_exit();
    return 0;
}
