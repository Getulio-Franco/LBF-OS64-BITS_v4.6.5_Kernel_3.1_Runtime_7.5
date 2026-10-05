/* ============================================================================
   STUDIO LBF EX v1.0 - Testador de CONEXAO do BANCO NATIVO (Runtime_Banco)
   Papel: CONSUMIDOR. Nao cria banco nem tabela: le o esquema do DISCO
   (catalogo .CAT + header .TBL) criado por OUTRO app (STUDIO LBF) e prova
   que a ABI e compartilhada: PESQUISAR -> INSERT TESTE -> SELECT.
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
const int winHeight = 520;

TGUIControl* EditBanco   = NULL;
TGUIControl* EditTabela  = NULL;
TGUIControl* EditValores = NULL;
TGUIControl* BtnPesquisar= NULL;
TGUIControl* BtnInsert   = NULL;
TGUIControl* BtnSelect   = NULL;
TGUIControl* BtnLimpar   = NULL;
TGUIControl* MemoLog     = NULL;

static int g_teste_n = 0;   /* contador dos valores de teste gerados */

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

/* ============================================================================
   [PESQUISAR] = teste de conexao em 4 passos transparentes
   ============================================================================ */
void OnBtnPesquisarClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    if (!banco || !banco[0] || !tabela || !tabela[0]) { log_line("Preencha banco e tabela!"); return; }
    log_line("=== TESTE DE CONEXAO ===");

    /* [1] o banco existe? (le o catalogo .CAT) */
    char hdr[160];
    strcpy(hdr, "[1] Banco: "); strcat(hdr, banco);
    static char out[1024];
    int rc = sys_banco_list_tables(banco, out, sizeof(out));
    if (rc != BCO_OK) {
        strcat(hdr, " ... FALHOU (catalogo nao encontrado)");
        log_line(hdr);
        log_line("    -> crie este banco no STUDIO LBF primeiro.");
        return;
    }
    strcat(hdr, " ... OK"); log_line(hdr);
    log_line("    Tabelas do banco: ");
    log_line(out[0] ? out : "(nenhuma)");

    /* [2] a tabela esta no catalogo? (le o header .TBL) */
    char h2[160];
    strcpy(h2, "[2] Tabela: "); strcat(h2, tabela);
    static char raw[1024];
    rc = sys_banco_list_fields(banco, tabela, raw, sizeof(raw));
    if (rc != BCO_OK) {
        strcat(h2, " ... FALHOU (tabela nao esta no catalogo)");
        log_line(h2);
        return;
    }
    strcat(h2, " ... OK"); log_line(h2);

    /* [3] campos, um por linha (vindos do header no disco) */
    log_line("[3] Campos lidos do disco:");
    int n = 0;
    char* p = raw;
    while (*p) {
        char* bar = p;
        while (*bar && *bar != '|') bar++;
        if (bar > p) {
            char frag[96];
            int len = (int)(bar - p);
            if (len > 95) len = 95;
            memcpy(frag, p, len); frag[len] = 0;
            char* q = frag; while (*q == ' ') q++;
            if (*q) {
                char line[128]; char num[8];
                itoa((uint64_t)(uint32_t)(n + 1), num, 10);
                strcpy(line, "    CAMPO "); strcat(line, num); strcat(line, ": ");
                strncat(line, q, sizeof(line) - strlen(line) - 1);
                log_line(line);
                n++;
            }
        }
        if (*bar == 0) break;
        p = bar + 1;
    }

    /* [4] quantos registros vivos */
    log_num("[4] Registros vivos = ", sys_banco_count(banco, tabela));
    log_line("=== CONEXAO OK: INSERT/SELECT liberados ===");
}

/* ============================================================================
   [INSERT TESTE] = grava 1 registro usando o ESQUEMA LIDO DO DISCO
   (valores digitados no campo, ou auto: PK/AI=0, int=100+n, texto=TESTE-EXn)
   ============================================================================ */
void OnBtnInsertClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    if (!banco || !banco[0] || !tabela || !tabela[0]) { log_line("Preencha banco e tabela!"); return; }
    char* manual = GUI_Edit_GetText(EditValores);
    char valores[512];

    if (manual && manual[0]) {
        strncpy(valores, manual, sizeof(valores) - 1);
        valores[sizeof(valores) - 1] = 0;
        log_line("INSERT com valores digitados:");
    } else {
        static char raw[1024];
        if (sys_banco_list_fields(banco, tabela, raw, sizeof(raw)) != BCO_OK) {
            log_line("Tabela nao encontrada - clique PESQUISAR primeiro.");
            return;
        }
        g_teste_n++;
        valores[0] = 0;
        char* p = raw;
        while (*p) {
            char* bar = p;
            while (*bar && *bar != '|') bar++;
            if (bar > p) {
                char frag[96];
                int len = (int)(bar - p);
                if (len > 95) len = 95;
                memcpy(frag, p, len); frag[len] = 0;
                char* q = frag; while (*q == ' ') q++;
                if (*q) {
                    char val[40]; val[0] = 0;
                    char* pc = strchr(q, ':');
                    if (pc) {
                        int is_pk = (strstr(pc, ":PK") != 0);
                        int is_ai = (strstr(pc, ":AI") != 0);
                        if (is_pk && is_ai) {
                            strcpy(val, "0");                       /* auto-incremento */
                        } else if (pc[1] == 'I') {
                            char num[8]; itoa((uint64_t)(uint32_t)(100 + g_teste_n), num, 10);
                            strcpy(val, num);
                        } else {
                            char num[8]; itoa((uint64_t)(uint32_t)g_teste_n, num, 10);
                            strcpy(val, "TESTE-EX");
                            strncat(val, num, sizeof(val) - strlen(val) - 1);
                        }
                    }
                    if (valores[0]) strncat(valores, "|", sizeof(valores) - strlen(valores) - 1);
                    strncat(valores, val, sizeof(valores) - strlen(valores) - 1);
                }
            }
            if (*bar == 0) break;
            p = bar + 1;
        }
        log_line("INSERT auto (valores gerados pelo esquema do disco):");
    }
    log_line(valores);
    int rc = sys_banco_insert(banco, tabela, valores);
    log_num("insert rc = ", rc);
    if (rc == BCO_OK) log_line("INSERT TESTE ok - registro gravado no .TBL.");
    else if (rc == BCO_ERR_NOTFOUND) log_line("Tabela nao encontrada.");
    else if (rc == BCO_ERR_FULL) log_line("Tabela cheia (limite de 32KB).");
    else log_line("Erro no insert.");
}

/* ============================================================================
   [SELECT] = lista todas as linhas vivas da tabela
   ============================================================================ */
void OnBtnSelectClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    if (!banco || !banco[0] || !tabela || !tabela[0]) { log_line("Preencha banco e tabela!"); return; }
    static char out[2048];
    int n = sys_banco_select(banco, tabela, 0, 0, out, sizeof(out));
    log_num("select linhas = ", n);
    if (n < 0) { log_line("Erro no select (tabela nao encontrada?)."); return; }
    if (n == 0) { log_line("(tabela sem linhas vivas)"); return; }
    log_line(out);
}
void OnBtnLimparClick(void* s) { GUI_Memo_Clear(MemoLog); Flush_Grafico_Janela(); }

/* ============================================================================
   MAIN
   ============================================================================ */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("Studio LBF EX", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Studio LBF EX v1.0", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    /* linha 1: banco + tabela + PESQUISAR */
    GUI_CreateLabel(&MyApp, 10, 42, "Banco:");
    EditBanco   = GUI_CreateEdit(&MyApp, 60, 38, 140, 25, "MERCADO", NULL);
    GUI_CreateLabel(&MyApp, 210, 42, "Tabela:");
    EditTabela  = GUI_CreateEdit(&MyApp, 260, 38, 160, 25, "CLIENTES", NULL);
    BtnPesquisar= GUI_CreateButton(&MyApp, 430, 38, 120, 25, "PESQUISAR", OnBtnPesquisarClick);

    /* linha 2: valores opcionais + INSERT TESTE */
    GUI_CreateLabel(&MyApp, 10, 72, "Valores:");
    EditValores = GUI_CreateEdit(&MyApp, 70, 68, 270, 25, "", NULL);
    GUI_CreateLabel(&MyApp, 345, 72, "(vazio = auto)");
    BtnInsert   = GUI_CreateButton(&MyApp, 460, 68, 130, 25, "INSERT TESTE", OnBtnInsertClick);

    /* linha 3: SELECT + LIMPAR */
    BtnLimpar   = GUI_CreateButton(&MyApp, 10, 98, 110, 25, "LIMPAR", OnBtnLimparClick);
    BtnSelect   = GUI_CreateButton(&MyApp, 460, 98, 130, 25, "SELECT", OnBtnSelectClick);

    MemoLog = GUI_CreateMemo(&MyApp, 10, 128, 620, 382);
    gui_set_prop(MemoLog, PROP_COLOR, 0x000000);
    GUI_Memo_AddStr(MemoLog, "STUDIO LBF EX - testador de conexao do banco nativo.\n");
    GUI_Memo_AddStr(MemoLog, "1) Crie banco+tabela no STUDIO LBF (studio_create.elf)\n");
    GUI_Memo_AddStr(MemoLog, "2) Digite aqui os MESMOS nomes e clique PESQUISAR\n");
    GUI_Memo_AddStr(MemoLog, "3) INSERT TESTE grava usando o esquema lido do disco\n");
    GUI_Memo_AddStr(MemoLog, "4) SELECT lista as linhas vivas\n\n");

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
                    if      (dentro(BtnPesquisar, rel_x, rel_y)) gui_set_prop(BtnPesquisar, PROP_STATE, 2);
                    else if (dentro(BtnInsert,    rel_x, rel_y)) gui_set_prop(BtnInsert,    PROP_STATE, 2);
                    else if (dentro(BtnSelect,    rel_x, rel_y)) gui_set_prop(BtnSelect,    PROP_STATE, 2);
                    else if (dentro(BtnLimpar,    rel_x, rel_y)) gui_set_prop(BtnLimpar,    PROP_STATE, 2);
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
                    if (BtnPesquisar) gui_set_prop(BtnPesquisar, PROP_STATE, 0);
                    if (BtnInsert)    gui_set_prop(BtnInsert,    PROP_STATE, 0);
                    if (BtnSelect)    gui_set_prop(BtnSelect,    PROP_STATE, 0);
                    if (BtnLimpar)    gui_set_prop(BtnLimpar,    PROP_STATE, 0);
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
