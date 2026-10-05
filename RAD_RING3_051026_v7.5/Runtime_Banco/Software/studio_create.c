/* ============================================================================
   LBF STUDIO CREATE v1.4 - Designer DDL do BANCO NATIVO (Runtime_Banco)
   v1.4: CORRECAO REAL do ComboBoxEx: com a aba aberta, o clique (dentro ou
   fora da aba) e CONSUMIDO pelo loop - nada e repassado ao que estiver
   embaixo. Fora disso, layout original v1.3 (intuitivo, sem encavalar).
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

TGUIControl* EditBanco    = NULL;
TGUIControl* BtnCriarDb   = NULL;
TGUIControl* EditTabela   = NULL;
TGUIControl* BtnCriarTab  = NULL;
TGUIControl* EditCampo    = NULL;
TGUIControl* EditTam      = NULL;
TGUIControl* ComboTipo    = NULL;
TGUIControl* ComboPK      = NULL;
TGUIControl* ComboAI      = NULL;
TGUIControl* ComboNU      = NULL;
TGUIControl* BtnAddCampo  = NULL;
TGUIControl* BtnLimpaC    = NULL;
TGUIControl* BtnPesqBanco = NULL;
TGUIControl* BtnPesqTab   = NULL;
TGUIControl* BtnPesqCampos= NULL;
TGUIControl* MemoCampos   = NULL;
TGUIControl* MemoLog      = NULL;

static char g_spec[512];

/* ---- estado da aba (popup) dos combos: a chave da correcao v1.4 ---- */
static int  combo_popup_aberto = -1;   /* qual combo esta com a aba aberta */
static bool click_consumido    = false;/* clique atual pertence a aba      */

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
static void OnComboNoop(void* sender) { (void)sender; }
static int combo1(TGUIControl* c) {
    const char* t = GUI_ComboBoxEx_GetText(c);
    return (t && t[0] == '1') ? 1 : 0;
}
/* Qual combo (0..3) esta sob o ponto clicado; -1 = nenhum */
static int combo_sob_click(int x, int y) {
    if (dentro(ComboTipo, x, y)) return 0;
    if (dentro(ComboPK,   x, y)) return 1;
    if (dentro(ComboAI,   x, y)) return 2;
    if (dentro(ComboNU,   x, y)) return 3;
    return -1;
}
static void tipo_map(const char* tipo, int tam, char* out, int cap) {
    char c = tipo[0];
    if (c == 'i' || c == 'I') {
        if (tam == 1) strncpy(out, "I1", cap - 1);
        else if (tam == 2) strncpy(out, "I2", cap - 1);
        else strncpy(out, "I4", cap - 1);
    } else {
        if (tam < 1) tam = 15;
        char n[8]; itoa((uint64_t)(uint32_t)tam, n, 10);
        strncpy(out, "C", cap - 1); strncat(out, n, cap - strlen(out) - 1);
    }
    out[cap - 1] = 0;
}

/* ---------------- eventos ---------------- */
void OnBtnCriarDbClick(void* s) {
    char* banco = GUI_Edit_GetText(EditBanco);
    if (!banco || !banco[0]) { log_line("Nome do banco vazio!"); return; }
    int rc = sys_banco_create_db(banco);
    log_num("create_db rc =", rc);
    if (rc == BCO_OK) log_line("BANCO criado no disco (.CAT).");
    else if (rc == BCO_ERR_EXISTS) log_line("Banco ja existe - pode prosseguir.");
    else log_line("Erro ao criar banco.");
}
void OnBtnAddCampoClick(void* s) {
    char* campo = GUI_Edit_GetText(EditCampo);
    char* stam  = GUI_Edit_GetText(EditTam);
    const char* tipo = GUI_ComboBoxEx_GetText(ComboTipo);
    if (!campo || !campo[0]) { log_line("Nome do campo vazio!"); return; }
    if (!tipo || !tipo[0]) tipo = "int";
    int tam = atoi(stam ? stam : "0");
    if (tam < 0) tam = 0;
    int pk = combo1(ComboPK), ai = combo1(ComboAI), nu = combo1(ComboNU);
    char tmap[8];
    tipo_map(tipo, tam, tmap, sizeof(tmap));
    if (g_spec[0]) strncat(g_spec, ",", sizeof(g_spec) - strlen(g_spec) - 1);
    strncat(g_spec, campo, sizeof(g_spec) - strlen(g_spec) - 1);
    strncat(g_spec, ":",   sizeof(g_spec) - strlen(g_spec) - 1);
    strncat(g_spec, tmap,  sizeof(g_spec) - strlen(g_spec) - 1);
    if (pk) strncat(g_spec, ":PK", sizeof(g_spec) - strlen(g_spec) - 1);
    if (ai) strncat(g_spec, ":AI", sizeof(g_spec) - strlen(g_spec) - 1);
    strncat(g_spec, nu ? ":NU" : ":NN", sizeof(g_spec) - strlen(g_spec) - 1);
    char linha[128];
    strcpy(linha, campo); strcat(linha, "  :  "); strcat(linha, tmap);
    if (pk) strcat(linha, "  PK");
    if (ai) strcat(linha, "  AUTO");
    strcat(linha, nu ? "  NULL" : "  NOT-NULL");
    strcat(linha, "\n");
    GUI_Memo_AddStr(MemoCampos, linha);
    Flush_Grafico_Janela();
}
void OnBtnLimpaCClick(void* s) {
    g_spec[0] = 0;
    GUI_Memo_Clear(MemoCampos);
    Flush_Grafico_Janela();
}
void OnBtnCriarTabClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    if (!banco || !banco[0] || !tabela || !tabela[0]) { log_line("Banco/tabela vazios!"); return; }
    if (!g_spec[0]) { log_line("Adicione campos antes (+ CAMPO)!"); return; }
    int rc = sys_banco_create_table(banco, tabela, g_spec);
    log_num("create_table rc =", rc);
    if (rc == BCO_OK) {
        log_line("TABELA criada com os campos do designer.");
        log_line("spec gravada: ");
        log_line(g_spec);
        OnBtnLimpaCClick(0);
    } else if (rc == BCO_ERR_EXISTS) log_line("Tabela ja existe - nada foi alterado.");
    else if (rc == BCO_ERR_PARSE) log_line("Spec invalida - revise os campos.");
    else log_line("Erro ao criar tabela.");
}
void OnBtnPesqBancoClick(void* s) {
    char* banco = GUI_Edit_GetText(EditBanco);
    if (!banco || !banco[0]) { log_line("Nome do banco vazio!"); return; }
    static char out[1024];
    int rc = sys_banco_list_tables(banco, out, sizeof(out));
    log_num("list_tables rc =", rc);
    if (rc == BCO_OK) { log_line("Tabelas do banco:"); log_line(out[0] ? out : "(nenhuma tabela)"); }
    else log_line("Banco nao encontrado.");
}
void OnBtnPesqTabClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    if (!banco || !banco[0] || !tabela || !tabela[0]) { log_line("Banco/tabela vazios!"); return; }
    static char out[1024];
    int rc = sys_banco_list_fields(banco, tabela, out, sizeof(out));
    log_num("list_fields rc =", rc);
    if (rc == BCO_OK) { log_line("Campos da tabela:"); log_line(out[0] ? out : "(sem campos)"); }
    else log_line("Tabela nao encontrada.");
}
void OnBtnPesqCamposClick(void* s) {
    char* banco  = GUI_Edit_GetText(EditBanco);
    char* tabela = GUI_Edit_GetText(EditTabela);
    if (!banco || !banco[0] || !tabela || !tabela[0]) { log_line("Banco/tabela vazios!"); return; }
    char hdr[160];
    strcpy(hdr, "BANCO: "); strcat(hdr, banco);
    strcat(hdr, " | TABELA: "); strcat(hdr, tabela);
    log_line("--- CAMPOS DA TABELA ---");
    log_line(hdr);
    static char raw[1024];
    int rc = sys_banco_list_fields(banco, tabela, raw, sizeof(raw));
    if (rc != BCO_OK) { log_line("Tabela nao encontrada neste banco."); return; }
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
            if (*q != 0) {
                char line[128]; char num[8];
                itoa((uint64_t)(uint32_t)(n + 1), num, 10);
                strcpy(line, "CAMPO "); strcat(line, num); strcat(line, ": ");
                strncat(line, q, sizeof(line) - strlen(line) - 1);
                log_line(line);
                n++;
            }
        }
        if (*bar == 0) break;
        p = bar + 1;
    }
    char cnt[64]; char num2[8];
    itoa((uint64_t)(uint32_t)n, num2, 10);
    strcpy(cnt, "total de campos: "); strcat(cnt, num2);
    log_line(cnt);
    log_num("registros vivos =", sys_banco_count(banco, tabela));
}

/* ---------------- main ---------------- */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;
    g_spec[0] = 0;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("LBF Studio Create", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "LBF Studio Create v1.4", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    /* linha 1: banco (1x) + tabela (Nx) - LAYOUT ORIGINAL */
    GUI_CreateLabel(&MyApp, 10, 42, "Banco:");
    EditBanco  = GUI_CreateEdit(&MyApp, 60, 38, 120, 25, "MERCADO", NULL);
    BtnCriarDb = GUI_CreateButton(&MyApp, 185, 38, 125, 25, "CRIAR BANCO", OnBtnCriarDbClick);
    GUI_CreateLabel(&MyApp, 320, 42, "Tabela:");
    EditTabela  = GUI_CreateEdit(&MyApp, 370, 38, 120, 25, "CLIENTES", NULL);
    BtnCriarTab = GUI_CreateButton(&MyApp, 495, 38, 135, 25, "CRIAR TABELA", OnBtnCriarTabClick);

    /* linha 2: designer de campo com combos - LAYOUT ORIGINAL */
    GUI_CreateLabel(&MyApp, 10, 72, "CAMPO");
    GUI_CreateLabel(&MyApp, 135, 72, "TAM");
    GUI_CreateLabel(&MyApp, 185, 72, "TIPO");
    GUI_CreateLabel(&MyApp, 260, 72, "PK");
    GUI_CreateLabel(&MyApp, 315, 72, "AI");
    GUI_CreateLabel(&MyApp, 370, 72, "NULO");
    EditCampo = GUI_CreateEdit(&MyApp, 10,  88, 120, 25, "codigo", NULL);
    EditTam   = GUI_CreateEdit(&MyApp, 135, 88, 45,  25, "4",      NULL);
    ComboTipo = GUI_CreateComboBoxEx(&MyApp, 185, 88, 70, 24, OnComboNoop);
    GUI_ComboBoxEx_AddItem(ComboTipo, "int");
    GUI_ComboBoxEx_AddItem(ComboTipo, "text");
    ComboPK = GUI_CreateComboBoxEx(&MyApp, 260, 88, 50, 24, OnComboNoop);
    GUI_ComboBoxEx_AddItem(ComboPK, "0");
    GUI_ComboBoxEx_AddItem(ComboPK, "1");
    ComboAI = GUI_CreateComboBoxEx(&MyApp, 315, 88, 50, 24, OnComboNoop);
    GUI_ComboBoxEx_AddItem(ComboAI, "0");
    GUI_ComboBoxEx_AddItem(ComboAI, "1");
    ComboNU = GUI_CreateComboBoxEx(&MyApp, 370, 88, 50, 24, OnComboNoop);
    GUI_ComboBoxEx_AddItem(ComboNU, "0");
    GUI_ComboBoxEx_AddItem(ComboNU, "1");
    BtnAddCampo = GUI_CreateButton(&MyApp, 430, 88, 110, 25, "+ CAMPO", OnBtnAddCampoClick);

    /* linha 3: limpar + as TRES pesquisas - LAYOUT ORIGINAL */
    BtnLimpaC      = GUI_CreateButton(&MyApp, 10,  118, 145, 25, "LIMPAR CAMPOS", OnBtnLimpaCClick);
    BtnPesqBanco   = GUI_CreateButton(&MyApp, 165, 118, 125, 25, "PESQ. BANCO",   OnBtnPesqBancoClick);
    BtnPesqTab     = GUI_CreateButton(&MyApp, 300, 118, 145, 25, "PESQ. TABELAS", OnBtnPesqTabClick);
    BtnPesqCampos  = GUI_CreateButton(&MyApp, 455, 118, 135, 25, "PESQ. CAMPOS",  OnBtnPesqCamposClick);

    /* memos */
    MemoCampos = GUI_CreateMemo(&MyApp, 10, 148, 620, 110);
    gui_set_prop(MemoCampos, PROP_COLOR, 0x000000);
    MemoLog = GUI_CreateMemo(&MyApp, 10, 264, 620, 246);
    gui_set_prop(MemoLog, PROP_COLOR, 0x000000);
    GUI_Memo_AddStr(MemoLog, "LBF Studio Create v1.4 pronto.\n");
    GUI_Memo_AddStr(MemoLog, "1) CRIAR BANCO  2) desenhe campos (+ CAMPO)  3) CRIAR TAB\n");
    GUI_Memo_AddStr(MemoLog, "Combos: clique abre a aba; clique fora so fecha (nao vaza).\n\n");

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
            if (key != 0) {
                GUI_ProcessKeyboard(&MyApp, key);
                precisa_redesenhar = true;
                if (key == 27) combo_popup_aberto = -1;   /* Esc fecha a aba sem mouse */
            }
            if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
                if (mouse_hold_timer == 0) {
                    int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                    int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                    ultimo_x = rel_x; ultimo_y = rel_y; mouse_hold_timer = 2;

                    /* ===== CORRECAO v1.4: quem dono deste clique? ===== */
                    int combo_alvo = combo_sob_click(rel_x, rel_y);
                    if (combo_popup_aberto >= 0) {
                        /* Aba aberta: clique dentro = seleciona item; clique fora =
                           so fecha. Nos DOIS casos o clique e da aba: NADA vai para
                           o que estiver embaixo (fim do clique duplo/vazado). */
                        click_consumido = true;
                        combo_popup_aberto = (combo_alvo >= 0) ? combo_alvo : -1;
                    } else {
                        click_consumido = false;
                        if (combo_alvo >= 0) combo_popup_aberto = combo_alvo; /* abriu a aba */
                    }
                    if (!click_consumido) {
                        if (combo_alvo < 0) {
                            if      (dentro(BtnCriarDb,    rel_x, rel_y)) gui_set_prop(BtnCriarDb,    PROP_STATE, 2);
                            else if (dentro(BtnCriarTab,   rel_x, rel_y)) gui_set_prop(BtnCriarTab,   PROP_STATE, 2);
                            else if (dentro(BtnAddCampo,   rel_x, rel_y)) gui_set_prop(BtnAddCampo,   PROP_STATE, 2);
                            else if (dentro(BtnLimpaC,     rel_x, rel_y)) gui_set_prop(BtnLimpaC,     PROP_STATE, 2);
                            else if (dentro(BtnPesqBanco,  rel_x, rel_y)) gui_set_prop(BtnPesqBanco,  PROP_STATE, 2);
                            else if (dentro(BtnPesqTab,    rel_x, rel_y)) gui_set_prop(BtnPesqTab,    PROP_STATE, 2);
                            else if (dentro(BtnPesqCampos, rel_x, rel_y)) gui_set_prop(BtnPesqCampos, PROP_STATE, 2);
                        }
                        events_process_mouse(rel_x, rel_y, 1, 0);
                    }
                    /* O roteador RAD sempre ve o clique (aba/combos/edits/memos): */
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
                    if (!click_consumido) {
                        if (BtnCriarDb)    gui_set_prop(BtnCriarDb,    PROP_STATE, 0);
                        if (BtnCriarTab)   gui_set_prop(BtnCriarTab,   PROP_STATE, 0);
                        if (BtnAddCampo)   gui_set_prop(BtnAddCampo,   PROP_STATE, 0);
                        if (BtnLimpaC)     gui_set_prop(BtnLimpaC,     PROP_STATE, 0);
                        if (BtnPesqBanco)  gui_set_prop(BtnPesqBanco,  PROP_STATE, 0);
                        if (BtnPesqTab)    gui_set_prop(BtnPesqTab,    PROP_STATE, 0);
                        if (BtnPesqCampos) gui_set_prop(BtnPesqCampos, PROP_STATE, 0);
                        events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                    }
                    click_consumido = false;
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
