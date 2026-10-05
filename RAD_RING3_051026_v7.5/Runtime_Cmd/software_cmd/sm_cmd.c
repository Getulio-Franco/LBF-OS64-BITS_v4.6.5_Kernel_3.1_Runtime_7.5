/* ============================================================================
LBF SM_CMD v0.1 - BANCADA DE TESTES DO RUNTIME_CMD
Base: gfile v0.0.9 (mesmo esqueleto RAD), mas TODA mutacao passa pelos
cores via lib_cmd.h (sys_cmd_*) e cada rc eh logado no DebugMemo.
Botao SUITE auto-testa todos os cores (caminhos feliz e de erro).
Navegacao/listagem: sys_cmd_chdir (core) + sys_fat_readdir (so p/ exibir).
============================================================================ */
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "Runtime_sdk/components/TOS_IPC.h"
#include "Runtime_Cmd/lib_cmd/lib_cmd.h"   /* sys_cmd_* + CMD_OK/CMD_ERR_* */

void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth  = 560;
const int winHeight = 560;

TGUIControl* PathEdit   = NULL;
TGUIControl* GoButton   = NULL;
TGUIControl* BtnRefresh = NULL;
TGUIControl* BtnSuite   = NULL;
TGUIControl* FileList   = NULL;
TGUIControl* ActionEdit = NULL;
TGUIControl* BtnNewDir  = NULL;
TGUIControl* BtnTouch   = NULL;
TGUIControl* BtnWrite   = NULL;
TGUIControl* BtnCopy    = NULL;
TGUIControl* BtnDel     = NULL;
TGUIControl* LabelStatus= NULL;
TGUIControl* DebugMemo  = NULL;

static int suite_falhas = 0;
static const char* SM_PAYLOAD = "SM_CMD v0.1 - teste de escrita do core_write.\n";

/* ---------------- helpers ---------------- */
void Log_Debug(const char* msg) {
    if (DebugMemo) { GUI_Memo_AddStr(DebugMemo, msg); GUI_Memo_AddStr(DebugMemo, "\n"); }
}
static void log_rc(const char* op, int rc) {
    char b[128]; char n[12];
    strcpy(b, op); strcat(b, " rc=");
    itoa((uint64_t)(uint32_t)rc, n, 10); strcat(b, n);
    strcat(b, (rc == CMD_OK) ? "  OK" : "  << ERRO");
    Log_Debug(b);
}
static void suite_line(const char* nome, int rc, int esperado) {
    char b[160]; char n[12];
    strcpy(b, nome); strcat(b, " rc=");
    itoa((uint64_t)(uint32_t)rc, n, 10); strcat(b, n);
    strcat(b, (rc == esperado) ? "  PASS" : "  FAIL");
    Log_Debug(b);
    if (rc != esperado) suite_falhas++;
}
static int dentro(TGUIControl* c, int x, int y) {
    return c && x >= c->Left && x < (c->Left + c->Width) && y >= c->Top && y < (c->Top + c->Height);
}
static int split2(char* rest, char* a, int ca, char* b, int cb) {
    int i = 0; while (*rest && *rest != ' ' && i < ca - 1) a[i++] = *rest++;
    a[i] = 0;
    while (*rest == ' ') rest++;
    i = 0; while (*rest && *rest != ' ' && i < cb - 1) b[i++] = *rest++;
    b[i] = 0;
    return (a[0] && b[0]);
}
static void path_join(char* out, const char* base, const char* name) {
    strcpy(out, base);
    int L = (int)strlen(out);
    if (L > 0 && out[L - 1] != '/') strcat(out, "/");
    strcat(out, name);
}
static void path_parent(char* out, const char* cur) {
    strcpy(out, cur);
    int n = (int)strlen(out);
    while (n > 0 && out[n - 1] == '/') n--;
    while (n > 0 && out[n - 1] != '/') n--;
    if (n <= 2) strcpy(out, "C:/");
    else out[n] = 0;
}

/* ---------------- IPC / janela ---------------- */
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

/* ---------------- listagem (core chdir + readdir so p/ exibir) ---------------- */
void Carregar_Diretorio(const char* path) {
    if (!FileList) return;
    GUI_ListView_Clear(FileList);
    sys_cmd_chdir("/");
    const char* p = path;
    if (p[0] && p[1] == ':') p += 2;
    while (*p == '/') p++;
    while (*p) {
        char comp[64]; int i = 0;
        while (*p && *p != '/' && i < 63) comp[i++] = *p++;
        comp[i] = 0;
        while (*p == '/') p++;
        if (!comp[0] || strcmp(comp, ".") == 0) continue;
        if (sys_cmd_chdir(comp) != CMD_OK) break;
    }
    if (strcmp(path, "C:/") != 0 && strcmp(path, "0:/") != 0 && strcmp(path, "/") != 0)
        GUI_ListView_AddItem(FileList, "..", 0, 0x10);
    char nome[64]; file_info_t md;
    int idx = 0, itens = 0;
    while (sys_fat_readdir(idx, nome, &md) == 1) {
        if (strcmp(nome, ".") != 0 && strcmp(nome, "..") != 0) {
            GUI_ListView_AddItem(FileList, nome, md.size, md.attributes);
            itens++;
        }
        idx++;
        if (idx > 500) break;
    }
    char st[256]; char n[12];
    itoa(itens, n, 10);
    strcpy(st, "itens: "); strcat(st, n); strcat(st, " | cwd: "); strcat(st, path);
    if (LabelStatus) GUI_Edit_SetText(LabelStatus, st);
}

/* ---------------- callbacks dos cores ---------------- */
void OnBtnGoClick(void* s)      { (void)s; Carregar_Diretorio(GUI_Edit_GetText(PathEdit)); }
void OnBtnRefreshClick(void* s) { (void)s; Carregar_Diretorio(GUI_Edit_GetText(PathEdit)); }
void OnBtnNewDirClick(void* s) {
    (void)s;
    char* a = GUI_Edit_GetText(ActionEdit);
    if (!a || !a[0]) return;
    log_rc("core mkdir ", sys_cmd_mkdir(a));
    GUI_Edit_SetText(ActionEdit, "");
    Carregar_Diretorio(GUI_Edit_GetText(PathEdit));
}
void OnBtnTouchClick(void* s) {
    (void)s;
    char* a = GUI_Edit_GetText(ActionEdit);
    if (!a || !a[0]) return;
    log_rc("core touch ", sys_cmd_touch(a));
    GUI_Edit_SetText(ActionEdit, "");
    Carregar_Diretorio(GUI_Edit_GetText(PathEdit));
}
void OnBtnWriteClick(void* s) {
    (void)s;
    char* a = GUI_Edit_GetText(ActionEdit);
    if (!a || !a[0]) return;
    log_rc("core write ", sys_cmd_write(a, SM_PAYLOAD, (uint32_t)strlen(SM_PAYLOAD)));
    GUI_Edit_SetText(ActionEdit, "");
    Carregar_Diretorio(GUI_Edit_GetText(PathEdit));
}
void OnBtnCopyClick(void* s) {
    (void)s;
    char* a = GUI_Edit_GetText(ActionEdit);
    char x[64], y[64];
    if (!a || !split2(a, x, sizeof(x), y, sizeof(y))) { Log_Debug("copy: use 'origem destino'"); return; }
    log_rc("core copy  ", sys_cmd_copy(x, y));
    GUI_Edit_SetText(ActionEdit, "");
    Carregar_Diretorio(GUI_Edit_GetText(PathEdit));
}
void OnBtnDelClick(void* s) {
    (void)s;
    char* a = GUI_Edit_GetText(ActionEdit);
    if (!a || !a[0]) return;
    log_rc("core delete", sys_cmd_delete(a));
    GUI_Edit_SetText(ActionEdit, "");
    Carregar_Diretorio(GUI_Edit_GetText(PathEdit));
}
/* ---------------- SUITE: auto-teste de todos os cores ---------------- */
void OnBtnSuiteClick(void* s) {
    (void)s;
    suite_falhas = 0;
    Log_Debug("=== SUITE RUNTIME_CMD (cwd atual) ===");
    suite_line("touch SMTEST.TXT            ", sys_cmd_touch("SMTEST.TXT"), CMD_OK);
    suite_line("write SMTEST.TXT            ", sys_cmd_write("SMTEST.TXT", SM_PAYLOAD, (uint32_t)strlen(SM_PAYLOAD)), CMD_OK);
    suite_line("copy  SMTEST.TXT->SMTEST.BAK", sys_cmd_copy("SMTEST.TXT", "SMTEST.BAK"), CMD_OK);
    file_info_t f1, f2;
    int ver = (sys_fat_stat("SMTEST.TXT", &f1) == 0 && sys_fat_stat("SMTEST.BAK", &f2) == 0 &&
               f1.size == f2.size && f2.size == (uint32_t)strlen(SM_PAYLOAD));
    suite_line("verify tamanhos iguais      ", ver ? CMD_OK : CMD_ERR_VERIFY, CMD_OK);
    suite_line("delete SMTEST.BAK           ", sys_cmd_delete("SMTEST.BAK"), CMD_OK);
    suite_line("delete SMTEST.TXT           ", sys_cmd_delete("SMTEST.TXT"), CMD_OK);
    suite_line("delete repetido (NOTFOUND)  ", sys_cmd_delete("SMTEST.TXT"), CMD_ERR_NOTFOUND);
    suite_line("mkdir SMTEST_DIR            ", sys_cmd_mkdir("SMTEST_DIR"), CMD_OK);
    suite_line("mkdir repetido (EXISTS)     ", sys_cmd_mkdir("SMTEST_DIR"), CMD_ERR_EXISTS);
    suite_line("delete de dir (ARGS)        ", sys_cmd_delete("SMTEST_DIR"), CMD_ERR_ARGS);
    Log_Debug(suite_falhas ? "VEREDITO: SUITE FALHOU (veja FAIL acima)"
                           : "VEREDITO: SUITE 100% OK");
    Log_Debug("(SMTEST_DIR fica no disco: core_rmdir ainda nao existe)");
    Carregar_Diretorio(GUI_Edit_GetText(PathEdit));
}
void OnFileListChange(void* s) {
    (void)s;
    int idx = gui_get_prop(FileList, PROP_ITEM_INDEX);
    if (idx == -1) return;
    char nome_sel[64]; uint32_t tam = 0; uint8_t attr = 0;
    GUI_ListView_GetItem(FileList, idx, nome_sel, &tam, &attr);
    if (attr & 0x10) {
        char novo[256];
        if (strcmp(nome_sel, "..") == 0) path_parent(novo, GUI_Edit_GetText(PathEdit));
        else                             path_join(novo, GUI_Edit_GetText(PathEdit), nome_sel);
        GUI_Edit_SetText(PathEdit, novo);
        Carregar_Diretorio(novo);
    } else {
        GUI_Edit_SetText(ActionEdit, nome_sel);
    }
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
    my_app_slot = OS_IPC_RegisterApp("SM_CMD Runtime_Cmd", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "LBF SM_CMD v0.1", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    /* linha do caminho + suite */
    GUI_CreateLabel(&MyApp, 10, 42, "Caminho:");
    PathEdit   = GUI_CreateEdit(&MyApp, 70, 38, 250, 25, "C:/", NULL);
    GoButton   = GUI_CreateButton(&MyApp, 325, 38, 85, 25, "Navegar", OnBtnGoClick);
    BtnRefresh = GUI_CreateButton(&MyApp, 415, 38, 65, 25, "Reler",   OnBtnRefreshClick);
    BtnSuite   = GUI_CreateButton(&MyApp, 485, 38, 65, 25, "SUITE",   OnBtnSuiteClick);
    FileList   = GUI_CreateListView(&MyApp, 10, 75, 540, 230, OnFileListChange);
    gui_set_prop(FileList, PROP_ITEM_INDEX, -1);
    /* linha de acao (nomes no cwd atual) */
    GUI_CreateLabel(&MyApp, 10, 317, "Acao:");
    ActionEdit = GUI_CreateEdit(&MyApp, 60, 313, 490, 25, "", NULL);
    BtnNewDir = GUI_CreateButton(&MyApp, 10,  345, 115, 25, "Nova Pasta", OnBtnNewDirClick);
    BtnTouch  = GUI_CreateButton(&MyApp, 135, 345, 95,  25, "Novo Arq",   OnBtnTouchClick);
    BtnWrite  = GUI_CreateButton(&MyApp, 240, 345, 75,  25, "Gravar",     OnBtnWriteClick);
    BtnCopy   = GUI_CreateButton(&MyApp, 325, 345, 75,  25, "Copiar",     OnBtnCopyClick);
    BtnDel    = GUI_CreateButton(&MyApp, 410, 345, 85,  25, "Deletar",    OnBtnDelClick);
    /* status + debug */
    LabelStatus = GUI_CreateLabel(&MyApp, 10, 380, "itens: 0 | cwd: C:/");
    GUI_CreateLabel(&MyApp, 10, 400, "Debug dos cores:");
    DebugMemo = GUI_CreateMemo(&MyApp, 10, 418, 540, 132);
    gui_set_prop(DebugMemo, PROP_COLOR, 0x000000);
    Log_Debug("SM_CMD v0.1 pronto.");
    Log_Debug("Botoes testam os cores; SUITE valida todos c/ PASS/FAIL.");

    g_focused_control = (void*)PathEdit;
    ultimo_controle_focado = (void*)PathEdit;
    gui_set_prop(PathEdit, PROP_SET_FOCUS, 1);
    Carregar_Diretorio("C:/");
    Flush_Grafico_Janela();

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) { Tratar_Fechamento_Software(); break; }
        bool precisa_redesenhar = false;
        if (primeiro_desenho) { primeiro_desenho = false; precisa_redesenhar = true; }
        bool euTenhoFoco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFoco != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFoco;
            if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco;
            precisa_redesenhar = true;
        }
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
                    if      (dentro(GoButton,   rel_x, rel_y)) gui_set_prop(GoButton,   PROP_STATE, 2);
                    else if (dentro(BtnRefresh, rel_x, rel_y)) gui_set_prop(BtnRefresh, PROP_STATE, 2);
                    else if (dentro(BtnSuite,   rel_x, rel_y)) gui_set_prop(BtnSuite,   PROP_STATE, 2);
                    else if (dentro(BtnNewDir,  rel_x, rel_y)) gui_set_prop(BtnNewDir,  PROP_STATE, 2);
                    else if (dentro(BtnTouch,   rel_x, rel_y)) gui_set_prop(BtnTouch,   PROP_STATE, 2);
                    else if (dentro(BtnWrite,   rel_x, rel_y)) gui_set_prop(BtnWrite,   PROP_STATE, 2);
                    else if (dentro(BtnCopy,    rel_x, rel_y)) gui_set_prop(BtnCopy,    PROP_STATE, 2);
                    else if (dentro(BtnDel,     rel_x, rel_y)) gui_set_prop(BtnDel,     PROP_STATE, 2);
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
                    if (GoButton)   gui_set_prop(GoButton,   PROP_STATE, 0);
                    if (BtnRefresh) gui_set_prop(BtnRefresh, PROP_STATE, 0);
                    if (BtnSuite)   gui_set_prop(BtnSuite,   PROP_STATE, 0);
                    if (BtnNewDir)  gui_set_prop(BtnNewDir,  PROP_STATE, 0);
                    if (BtnTouch)   gui_set_prop(BtnTouch,   PROP_STATE, 0);
                    if (BtnWrite)   gui_set_prop(BtnWrite,   PROP_STATE, 0);
                    if (BtnCopy)    gui_set_prop(BtnCopy,    PROP_STATE, 0);
                    if (BtnDel)     gui_set_prop(BtnDel,     PROP_STATE, 0);
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
