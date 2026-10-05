/* ============================================================================
   LBF SCANDISK v2.0 - Exame logico do FAT32 (SEM syscalls novas)
   v2.0: integra o scanner do scandisk_v1.c como botao extra:
         - convencoes corrigidas: readdir==1 por entrada; read_at==0 em sucesso
         - botoes: INVENTARIO | TESTE LEITURA | TESTE BANCOS | TESTE ESCRITA
                   | ESCANEAR TUDO | SCAN V1 (RAIZ) | LIMPAR
         - ESCANEAR TUDO   = 4 fases no diretorio atual (escrita COM zeros)
         - SCAN V1 (RAIZ)  = exame do v1: chdir 0:/ + 4 fases + round-trip
                             zero-free (write/append/write_at/copy/rename/rm)
   v1.1 futuro: SYS_FATINFO/SYS_FATMAP p/ orfaos, cross-links e mapa.
   ============================================================================ */
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "../system/sysutils.h"
#include "Runtime_sdk/components/TOS_IPC.h"

void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth  = 640;
const int winHeight = 520;

TGUIControl* BtnInv   = NULL;
TGUIControl* BtnLeit  = NULL;
TGUIControl* BtnBanc  = NULL;
TGUIControl* BtnEscr  = NULL;
TGUIControl* BtnScan  = NULL;
TGUIControl* BtnV1    = NULL;
TGUIControl* BtnLimpa = NULL;
TGUIControl* MemoLog  = NULL;
TGUIControl* LabelMsg = NULL;

#define MAX_ITENS 128
static char     g_names[MAX_ITENS][32];
static uint32_t g_sizes[MAX_ITENS];
static uint8_t  g_attrs[MAX_ITENS];
static int      g_nitems = 0;
static int      g_alertas = 0, g_falhas = 0;
static int      g_rt_pass = 0, g_rt_tot = 0;
static uint8_t  g_b1[32768];
static uint8_t  g_b2[32768];
static uint8_t  g_c[64];

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;
char Obter_Tecla_Entrada(void) {
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0;
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
static void log_line(const char* m) {
    char buf[200];
    strcpy(buf, m); strcat(buf, "\n");
    if (MemoLog) GUI_Memo_AddStr(MemoLog, buf);
    sys_debug(m);
}
static void alerta(const char* m) { g_alertas++; log_line(m); }
static void falha (const char* m) { g_falhas++;  log_line(m); }
static void set_msg(const char* m) {
    if (LabelMsg) GUI_Edit_SetText(LabelMsg, m);
    Flush_Grafico_Janela();
}
static int dentro(TGUIControl* c, int x, int y) {
    return c && x >= c->Left && x < (c->Left + c->Width) && y >= c->Top && y < (c->Top + c->Height);
}
/* read_at: kernel retorna 0 em sucesso (convencao do bancostore) */
static int read_at_ok(const char* name, uint8_t* buf, uint32_t len, uint32_t off) {
    return (sys_fat_read_at(name, buf, len, off) == 0) ? 1 : 0;
}

/* ================= FASE 1: INVENTARIO (readdir == 1 por entrada) ========== */
static void fase_inventario(void) {
    g_nitems = 0;
    log_line("--- FASE 1: INVENTARIO DO DIRETORIO ATUAL ---");
    int idx = 0;
    while (idx < 400) {
        char nome[64]; file_info_t fi;
        memset(nome, 0, sizeof(nome)); memset(&fi, 0, sizeof(fi));
        int r = sys_fat_readdir(idx, nome, &fi);
        if (r != 1) break;                    /* kernel: 1 = entrada valida */
        if (nome[0] == 0 || strcmp(nome, ".") == 0 || strcmp(nome, "..") == 0) { idx++; continue; }
        for (int k = 0; k < g_nitems; k++) {
            if (strcmp(g_names[k], nome) == 0) {
                char m[96]; strcpy(m, "ALERTA: nome duplicado: "); strncat(m, nome, 60);
                alerta(m);
            }
        }
        if (g_nitems < MAX_ITENS) {
            strncpy(g_names[g_nitems], nome, 31);
            g_sizes[g_nitems] = fi.size;
            g_attrs[g_nitems] = fi.attributes;
            g_nitems++;
        }
        char line[96]; char num[12];
        if (fi.attributes & 0x10) {
            strcpy(line, "[DIR ] "); strncat(line, nome, 40);
        } else {
            strcpy(line, "[ARQ ] "); strncat(line, nome, 40);
            strcat(line, "  "); itoa(fi.size, num, 10); strcat(line, num); strcat(line, " B");
            if (fi.size == 0) strcat(line, " (vazio)");
        }
        if (!(fi.attributes & 0x10) && (fi.attributes & 0x0F)) {
            strcat(line, " attr="); itoa(fi.attributes, num, 10); strcat(line, num);
            g_alertas++;
        }
        log_line(line);
        idx++;
    }
    int nd = 0, nf = 0;
    for (int k = 0; k < g_nitems; k++) { if (g_attrs[k] & 0x10) nd++; else nf++; }
    char s[96]; char a[12], b[12];
    itoa(nf, a, 10); itoa(nd, b, 10);
    strcpy(s, "inventario: "); strcat(s, a); strcat(s, " arquivo(s), ");
    strcat(s, b); strcat(s, " diretorio(s)");
    log_line(s);
}

/* ================= FASE 2: LEITURA (2x + fatias, read_at==0 sucesso) ====== */
static void fase_leitura(void) {
    log_line("--- FASE 2: TESTE DE LEITURA (determinismo + fatias) ---");
    if (g_nitems == 0) fase_inventario();
    int testados = 0, pulados = 0;
    for (int k = 0; k < g_nitems && testados < 20; k++) {
        if (g_attrs[k] & 0x10) continue;
        uint32_t sz = g_sizes[k];
        if (sz == 0)    { pulados++; continue; }
        if (sz > 32768) {
            char m[96]; strcpy(m, "pulado (>32KB): "); strncat(m, g_names[k], 40);
            log_line(m); pulados++; continue;
        }
        int n1 = sys_fat_read(g_names[k], g_b1, sz);
        if (n1 != (int)sz) {
            char m[96]; strcpy(m, "FALHA: leitura curta em "); strncat(m, g_names[k], 40);
            falha(m); testados++; continue;
        }
        int n2 = sys_fat_read(g_names[k], g_b2, sz);
        if (n2 != (int)sz || memcmp(g_b1, g_b2, sz) != 0) {
            char m[96]; strcpy(m, "FALHA: leitura instavel (2 leituras diferem): ");
            strncat(m, g_names[k], 40);
            falha(m); testados++; continue;
        }
        int div = 0;
        uint32_t offs[3];
        offs[0] = 0; offs[1] = sz / 2; offs[2] = (sz > 16) ? sz - 16 : 0;
        for (int t = 0; t < 3 && !div; t++) {
            uint32_t len = sz - offs[t]; if (len > 16) len = 16;
            if (!read_at_ok(g_names[k], g_c, len, offs[t])) div = 1;
            else if (memcmp(g_c, g_b1 + offs[t], len) != 0) div = 1;
        }
        if (div) {
            char m[96]; strcpy(m, "FALHA: fatia read_at diverge: "); strncat(m, g_names[k], 40);
            falha(m);
        } else {
            char m[96]; strcpy(m, "ok leitura: "); strncat(m, g_names[k], 40);
            log_line(m);
        }
        testados++;
    }
    char s[96]; char a[12], b[12];
    itoa(testados, a, 10); itoa(pulados, b, 10);
    strcpy(s, "leituras testadas: "); strcat(s, a); strcat(s, " | puladas: "); strcat(s, b);
    log_line(s);
}

/* ================= FASE 3: MAGICOS DO BANCO (.CAT/.TBL) =================== */
static void fase_bancos(void) {
    log_line("--- FASE 3: MAGICOS DO BANCO (.CAT/.TBL) ---");
    if (g_nitems == 0) fase_inventario();
    int ach = 0;
    for (int i = 0; i < g_nitems; i++) {
        int Ln = (int)strlen(g_names[i]);
        if (Ln < 5) continue;
        int is_cat = (strcmp(g_names[i] + Ln - 4, ".CAT") == 0);
        int is_tbl = (strcmp(g_names[i] + Ln - 4, ".TBL") == 0);
        if (!is_cat && !is_tbl) continue;
        ach++;
        char l[96];
        strcpy(l, "  "); strcat(l, g_names[i]);
        int nr = sys_fat_read(g_names[i], g_b1, 32);
        if (nr < (is_tbl ? 32 : 12)) { strcat(l, "  falha de leitura!"); falha(l); continue; }
        int mok = is_tbl ? (memcmp(g_b1, "LBFTBL1", 7) == 0)
                         : (memcmp(g_b1, "LBFCAT1", 7) == 0);
        if (!mok) { strcat(l, "  MAGIC INVALIDO!"); falha(l); continue; }
        char n1[12];
        if (is_tbl) {
            uint32_t ncam, nreg, rsz;
            memcpy(&ncam, g_b1 + 12, 4); memcpy(&nreg, g_b1 + 16, 4); memcpy(&rsz, g_b1 + 24, 4);
            int sane = (ncam >= 1 && ncam <= 16 && rsz >= 2 && rsz <= 4096);
            itoa(nreg, n1, 10);
            if (sane) { strcat(l, "  ok ("); strcat(l, n1); strcat(l, " regs)"); log_line(l); }
            else      { strcat(l, "  HEADER SUSPEITO ("); strcat(l, n1); strcat(l, " regs)"); alerta(l); }
        } else {
            uint32_t ne; memcpy(&ne, g_b1 + 8, 4);
            itoa(ne, n1, 10);
            strcat(l, "  ok ("); strcat(l, n1); strcat(l, " entradas)"); log_line(l);
        }
    }
    if (!ach) log_line("  (nenhum .CAT/.TBL no diretorio)");
}

/* ================= FASE 4: ESCRITA com ZEROS (regressao do padding) ======= */
static void fase_escrita(void) {
    log_line("--- FASE 4: TESTE DE ESCRITA (payload COM zeros) ---");
    static uint8_t pat[512], pat2[128];
    for (int i = 0; i < 512; i++) pat[i]  = (uint8_t)((i * 7  + 3) & 0xFF); /* inclui zeros */
    for (int i = 0; i < 128; i++) pat2[i] = (uint8_t)((i * 13 + 1) & 0xFF);
    sys_fat_rm("SCANTEST.TMP"); sys_fat_rm("SCANTEST.BAK"); sys_fat_rm("SCANTEST.OLD");
    if (sys_fat_write("SCANTEST.TMP", pat, 512) != 0) { falha("FALHA: write do SCANTEST.TMP"); return; }
    int n = sys_fat_read("SCANTEST.TMP", g_b1, 512);
    if (n != 512 || memcmp(g_b1, pat, 512) != 0)
        falha("FALHA: write->read divergiu (payload com zeros!)");
    else log_line("ok write->read integra (512 B com zeros no payload)");
    if (!read_at_ok("SCANTEST.TMP", g_c, 64, 100) || memcmp(g_c, pat + 100, 64) != 0)
        falha("FALHA: read_at @100 diverge");
    else log_line("ok read_at @100 confere");
    if (sys_fat_append("SCANTEST.TMP", (const char*)pat2, 128) != 0) falha("FALHA: append");
    else {
        n = sys_fat_read("SCANTEST.TMP", g_b1, 640);
        if (n != 640 || memcmp(g_b1 + 512, pat2, 128) != 0) falha("FALHA: cauda do append diverge");
        else log_line("ok append 128 B confere");
    }
    if (sys_fat_copy("SCANTEST.TMP", "SCANTEST.BAK") != 0) falha("FALHA: copy");
    else {
        n = sys_fat_read("SCANTEST.BAK", g_b2, 640);
        if (n != 640 || memcmp(g_b1, g_b2, 640) != 0) falha("FALHA: copia diverge do original");
        else log_line("ok copy integra");
    }
    if (sys_fat_rename("SCANTEST.BAK", "SCANTEST.OLD") != 0) falha("FALHA: rename");
    else {
        file_info_t fi;
        if (sys_fat_stat("SCANTEST.OLD", &fi) != 0) falha("FALHA: stat pos-rename");
        else log_line("ok rename BAK->OLD");
    }
    sys_fat_rm("SCANTEST.TMP"); sys_fat_rm("SCANTEST.OLD");
    {
        file_info_t fi;
        if (sys_fat_stat("SCANTEST.TMP", &fi) == 0) falha("FALHA: rm nao removeu o TMP");
        else log_line("ok limpeza dos arquivos de teste");
    }
}

/* ========= FASE 4v1: ROUND-TRIP ZERO-FREE (o scanner do scandisk_v1) ====== */
static void fase_escrita_v1(void) {
    log_line("--- FASE 4v1: ROUND-TRIP ZERO-FREE (SCANDISK.*) ---");
    g_rt_pass = 0; g_rt_tot = 0;
    file_info_t fi;
    for (int i = 0; i < 512; i++) g_b1[i] = (uint8_t)(0x21 + (i % 94));   /* sem zeros */
    static uint8_t p64[64];
    for (int i = 0; i < 64; i++) p64[i] = (uint8_t)(0x41 + (i % 23));
    sys_fat_rm("SCANDISK.TMP"); sys_fat_rm("SCANDISK.CPY"); sys_fat_rm("SCANDISK.CP2");
    g_rt_tot++;
    if (sys_fat_write("SCANDISK.TMP", g_b1, 512) == 0 &&
        sys_fat_read("SCANDISK.TMP", g_b2, 512) == 512 &&
        memcmp(g_b1, g_b2, 512) == 0) { g_rt_pass++; log_line("  write+read inteiro ........ PASS"); }
    else log_line("  write+read inteiro ........ FALHOU");
    g_rt_tot++;
    if (sys_fat_append("SCANDISK.TMP", (const char*)p64, 64) == 0 &&
        sys_fat_stat("SCANDISK.TMP", &fi) == 0 && fi.size == 576 &&
        read_at_ok("SCANDISK.TMP", g_c, 64, 512) &&
        memcmp(g_c, p64, 64) == 0) { g_rt_pass++; log_line("  append+stat+read_at ....... PASS"); }
    else log_line("  append+stat+read_at ....... FALHOU");
    g_rt_tot++;
    if (sys_fat_write_at("SCANDISK.TMP", "LBFWR001", 8, 16) == 0 &&
        read_at_ok("SCANDISK.TMP", g_c, 8, 16) &&
        memcmp(g_c, "LBFWR001", 8) == 0) { g_rt_pass++; log_line("  write_at+read_at @16 ...... PASS"); }
    else log_line("  write_at+read_at @16 ...... FALHOU");
    g_rt_tot++;
    if (sys_fat_copy("SCANDISK.TMP", "SCANDISK.CPY") == 0 &&
        sys_fat_stat("SCANDISK.CPY", &fi) == 0 && fi.size == 576) { g_rt_pass++; log_line("  copy+stat ................. PASS"); }
    else log_line("  copy+stat ................. FALHOU");
    g_rt_tot++;
    if (sys_fat_rename("SCANDISK.CPY", "SCANDISK.CP2") == 0 &&
        sys_fat_stat("SCANDISK.CP2", &fi) == 0 &&
        sys_fat_rm("SCANDISK.CP2") == 0) { g_rt_pass++; log_line("  rename+stat+rm da copia ... PASS"); }
    else { log_line("  rename+stat+rm da copia ... FALHOU"); sys_fat_rm("SCANDISK.CPY"); }
    g_rt_tot++;
    if (sys_fat_rm("SCANDISK.TMP") == 0 &&
        sys_fat_stat("SCANDISK.TMP", &fi) != 0) { g_rt_pass++; log_line("  rm+stat negativo .......... PASS"); }
    else log_line("  rm+stat negativo .......... FALHOU");
    char s[64]; char a[12], b[12];
    itoa(g_rt_pass, a, 10); itoa(g_rt_tot, b, 10);
    strcpy(s, "  round-trip v1: "); strcat(s, a); strcat(s, "/"); strcat(s, b); strcat(s, " PASS");
    log_line(s);
    if (g_rt_pass != g_rt_tot) g_falhas++;
}

static void resumo(void) {
    char s[110]; char a[12], b[12];
    itoa(g_alertas, a, 10); itoa(g_falhas, b, 10);
    strcpy(s, "=== RESUMO: alertas="); strcat(s, a); strcat(s, " falhas="); strcat(s, b);
    if (g_falhas == 0 && g_alertas == 0) strcat(s, " - DISCO SAUDAVEL ===");
    else strcat(s, " - REVER LINHAS ACIMA ===");
    log_line(s);
}
static void reset_cont(void) { g_alertas = 0; g_falhas = 0; }

/* ================= eventos ================= */
void OnBtnInvClick(void* s)  { GUI_Memo_Clear(MemoLog); reset_cont(); fase_inventario(); resumo(); set_msg("inventario concluido."); }
void OnBtnLeitClick(void* s) { GUI_Memo_Clear(MemoLog); reset_cont(); fase_leitura();    resumo(); set_msg("teste de leitura concluido."); }
void OnBtnBancClick(void* s) { GUI_Memo_Clear(MemoLog); reset_cont(); fase_bancos();     resumo(); set_msg("teste de bancos concluido."); }
void OnBtnEscrClick(void* s) { GUI_Memo_Clear(MemoLog); reset_cont(); fase_escrita();    resumo(); set_msg("teste de escrita concluido."); }
void OnBtnScanClick(void* s) {
    GUI_Memo_Clear(MemoLog); reset_cont();
    log_line("=== SCANDISK v2.0 - exame do diretorio atual ===");
    fase_inventario(); fase_leitura(); fase_bancos(); fase_escrita(); resumo();
    set_msg(g_falhas ? "scan concluido COM FALHAS - rever laudo." : "scan concluido - disco saudavel.");
}
void OnBtnV1Click(void* s) {
    GUI_Memo_Clear(MemoLog); reset_cont();
    log_line("=== SCAN V1 (RAIZ) - exame estilo scandisk_v1 ===");
    sys_fat_chdir("0:/");
    fase_inventario(); fase_leitura(); fase_bancos(); fase_escrita_v1(); resumo();
    set_msg(g_falhas ? "scan v1 concluido COM FALHAS - rever laudo." : "scan v1 concluido - disco saudavel.");
}
void OnBtnLimpaClick(void* s) { GUI_Memo_Clear(MemoLog); set_msg("laudo limpo."); }

/* ================= main ================= */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("LBF ScanDisk", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "LBF ScanDisk v2.0", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    GUI_CreateLabel(&MyApp, 10, 40, "Exame logico FAT32: inventario + leitura + bancos + escrita (SCANTEST.*/SCANDISK.*)");

    /* linha 1: fases individuais - 4 colunas de 150px (caption completo) */
    BtnInv  = GUI_CreateButton(&MyApp, 10,  62, 150, 28, "INVENTARIO",    OnBtnInvClick);
    BtnLeit = GUI_CreateButton(&MyApp, 165, 62, 150, 28, "TESTE LEITURA", OnBtnLeitClick);
    BtnBanc = GUI_CreateButton(&MyApp, 320, 62, 150, 28, "TESTE BANCOS",  OnBtnBancClick);
    BtnEscr = GUI_CreateButton(&MyApp, 475, 62, 150, 28, "TESTE ESCRITA", OnBtnEscrClick);
    /* linha 2: exames completos + limpar - MESMAS colunas da linha 1 */
    BtnScan  = GUI_CreateButton(&MyApp, 10,  96, 150, 28, "ESCANEAR TUDO",  OnBtnScanClick);
    BtnV1    = GUI_CreateButton(&MyApp, 165, 96, 150, 28, "SCAN V1 (RAIZ)", OnBtnV1Click);
    BtnLimpa = GUI_CreateButton(&MyApp, 320, 96, 150, 28, "LIMPAR",         OnBtnLimpaClick);
    /* memo e mensagem descem p/ dar respiro as duas carreiras */
    MemoLog = GUI_CreateMemo(&MyApp, 10, 130, 620, 360);
    gui_set_prop(MemoLog, PROP_COLOR, 0x000000);
    GUI_Memo_AddStr(MemoLog, "LBF ScanDisk v2.1 pronto.\n");
    GUI_Memo_AddStr(MemoLog, "ESCANEAR TUDO  = 4 fases no diretorio atual (escrita COM zeros).\n");
    GUI_Memo_AddStr(MemoLog, "SCAN V1 (RAIZ) = exame do scandisk_v1: chdir 0:/ + 4 fases\n");
    GUI_Memo_AddStr(MemoLog, "                   + round-trip zero-free c/ write_at.\n\n");
    LabelMsg = GUI_CreateLabel(&MyApp, 10, 496, "Clique ESCANEAR TUDO ou SCAN V1 (RAIZ).");

    g_focused_control = (void*)BtnScan;
    ultimo_controle_focado = (void*)BtnScan;
    gui_set_prop(BtnScan, PROP_SET_FOCUS, 1);
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
                    if      (dentro(BtnInv,   rel_x, rel_y)) gui_set_prop(BtnInv,   PROP_STATE, 2);
                    else if (dentro(BtnLeit,  rel_x, rel_y)) gui_set_prop(BtnLeit,  PROP_STATE, 2);
                    else if (dentro(BtnBanc,  rel_x, rel_y)) gui_set_prop(BtnBanc,  PROP_STATE, 2);
                    else if (dentro(BtnEscr,  rel_x, rel_y)) gui_set_prop(BtnEscr,  PROP_STATE, 2);
                    else if (dentro(BtnScan,  rel_x, rel_y)) gui_set_prop(BtnScan,  PROP_STATE, 2);
                    else if (dentro(BtnV1,    rel_x, rel_y)) gui_set_prop(BtnV1,    PROP_STATE, 2);
                    else if (dentro(BtnLimpa, rel_x, rel_y)) gui_set_prop(BtnLimpa, PROP_STATE, 2);
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
                    if (BtnInv)   gui_set_prop(BtnInv,   PROP_STATE, 0);
                    if (BtnLeit)  gui_set_prop(BtnLeit,  PROP_STATE, 0);
                    if (BtnBanc)  gui_set_prop(BtnBanc,  PROP_STATE, 0);
                    if (BtnEscr)  gui_set_prop(BtnEscr,  PROP_STATE, 0);
                    if (BtnScan)  gui_set_prop(BtnScan,  PROP_STATE, 0);
                    if (BtnV1)    gui_set_prop(BtnV1,    PROP_STATE, 0);
                    if (BtnLimpa) gui_set_prop(BtnLimpa, PROP_STATE, 0);
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
