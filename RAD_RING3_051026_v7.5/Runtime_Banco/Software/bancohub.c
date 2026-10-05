/* ============================================================================
BANCO HUB v1.1 - Launcher das ferramentas do Runtime_Banco
v1.1 (disciplina do launcher simples que NAO trava):
  - callback de clique NUNCA redesenha/flipa/imprime no serial:
    log_line/set_status so AGENDAM (g_pending_flush); o loop flusha 1x
  - exec_tool: chdir("/") c/ fallback "0:/", agenda log, sys_exec, agenda status
  - mantém: grade 4x2, memo-guia, feedback PROP_STATE, foco restaurado
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

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth  = 640;
const int winHeight = 420;

TGUIControl* BtnStudioEx = NULL;
TGUIControl* BtnStudio   = NULL;
TGUIControl* BtnStudioDb = NULL;
TGUIControl* BtnBancoAlt = NULL;
TGUIControl* BtnAgenda   = NULL;
TGUIControl* BtnBeckup   = NULL;
TGUIControl* BtnRecover  = NULL;
TGUIControl* BtnHexview  = NULL;
TGUIControl* MemoLog     = NULL;
TGUIControl* LabelStatus = NULL;

static volatile int g_pending_flush = 0;   /* unica chave: flush so no loop */

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
/* log SEM flush e SEM serial: so memo + agenda de redraw */
static void log_line(const char* m) {
    char b[200];
    strcpy(b, m); strcat(b, "\n");
    if (MemoLog) GUI_Memo_AddStr(MemoLog, b);
    g_pending_flush = 1;
}
static void set_status(const char* m) {
    if (LabelStatus) GUI_Edit_SetText(LabelStatus, m);
    g_pending_flush = 1;
}
static int dentro(TGUIControl* c, int x, int y) {
    return c && x >= c->Left && x < (c->Left + c->Width) && y >= c->Top && y < (c->Top + c->Height);
}
/* ---- nucleo de exec: leve como o launcher simples ---- */
static void exec_tool(const char* elf) {
    if (!elf || !elf[0]) return;
    if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");
    char m[128];
    strcpy(m, "EXEC: "); strcat(m, elf);
    log_line(m);            /* agenda, nao flusha */
    sys_exec(elf);          /* assincrono: task_d cria o filho */
    set_status(elf);        /* agenda, nao flusha */
}
void OnBtnStudioExClick(void* s){ (void)s; exec_tool("studioex.elf"); }
void OnBtnStudioClick(void* s)  { (void)s; exec_tool("studio.elf");   }
void OnBtnStudioDbClick(void* s){ (void)s; exec_tool("studiodb.elf"); }
void OnBtnBancoAltClick(void* s){ (void)s; exec_tool("bancoalt.elf"); }
void OnBtnAgendaClick(void* s)  { (void)s; exec_tool("agenda.elf");   }
void OnBtnBeckupClick(void* s)  { (void)s; exec_tool("beckup.elf");   }
void OnBtnRecoverClick(void* s) { (void)s; exec_tool("recover.elf");  }
void OnBtnHexviewClick(void* s) { (void)s; exec_tool("hexview.elf");  }

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;
    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("Banco Hub", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "BANCO HUB v1.1", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);
    /* grade 4x2 (y=45 e y=90), botoes 145x35, gap 10 */
    BtnStudioEx = GUI_CreateButton(&MyApp, 10,  45, 145, 35, "STUDIO EX", OnBtnStudioExClick);
    BtnStudio   = GUI_CreateButton(&MyApp, 165, 45, 145, 35, "STUDIO",    OnBtnStudioClick);
    BtnStudioDb = GUI_CreateButton(&MyApp, 320, 45, 145, 35, "STUDIO DB", OnBtnStudioDbClick);
    BtnBancoAlt = GUI_CreateButton(&MyApp, 475, 45, 145, 35, "BANCO ALT", OnBtnBancoAltClick);
    BtnAgenda   = GUI_CreateButton(&MyApp, 10,  90, 145, 35, "AGENDA",    OnBtnAgendaClick);
    BtnBeckup   = GUI_CreateButton(&MyApp, 165, 90, 145, 35, "BECKUP",    OnBtnBeckupClick);
    BtnRecover  = GUI_CreateButton(&MyApp, 320, 90, 145, 35, "RECOVER",   OnBtnRecoverClick);
    BtnHexview  = GUI_CreateButton(&MyApp, 475, 90, 145, 35, "HEXVIEW",   OnBtnHexviewClick);
    MemoLog = GUI_CreateMemo(&MyApp, 10, 135, 620, 250);
    gui_set_prop(MemoLog, PROP_COLOR, 0x000000);
    GUI_Memo_AddStr(MemoLog, "BANCO HUB v1.1 - flush/serial NUNCA dentro do callback.\n");
    GUI_Memo_AddStr(MemoLog, "STUDIO EX / STUDIO / STUDIO DB / BANCO ALT /\n");
    GUI_Memo_AddStr(MemoLog, "AGENDA / BECKUP / RECOVER / HEXVIEW.\n\n");
    LabelStatus = GUI_CreateLabel(&MyApp, 10, 390, "pronto");
    g_focused_control = (void*)BtnStudioEx;
    ultimo_controle_focado = (void*)BtnStudioEx;
    gui_set_prop(BtnStudioEx, PROP_SET_FOCUS, 1);
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
                    if      (dentro(BtnStudioEx, rel_x, rel_y)) gui_set_prop(BtnStudioEx, PROP_STATE, 2);
                    else if (dentro(BtnStudio,   rel_x, rel_y)) gui_set_prop(BtnStudio,   PROP_STATE, 2);
                    else if (dentro(BtnStudioDb, rel_x, rel_y)) gui_set_prop(BtnStudioDb, PROP_STATE, 2);
                    else if (dentro(BtnBancoAlt, rel_x, rel_y)) gui_set_prop(BtnBancoAlt, PROP_STATE, 2);
                    else if (dentro(BtnAgenda,   rel_x, rel_y)) gui_set_prop(BtnAgenda,   PROP_STATE, 2);
                    else if (dentro(BtnBeckup,   rel_x, rel_y)) gui_set_prop(BtnBeckup,   PROP_STATE, 2);
                    else if (dentro(BtnRecover,  rel_x, rel_y)) gui_set_prop(BtnRecover,  PROP_STATE, 2);
                    else if (dentro(BtnHexview,  rel_x, rel_y)) gui_set_prop(BtnHexview,  PROP_STATE, 2);
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
                    if (BtnStudioEx) gui_set_prop(BtnStudioEx, PROP_STATE, 0);
                    if (BtnStudio)   gui_set_prop(BtnStudio,   PROP_STATE, 0);
                    if (BtnStudioDb) gui_set_prop(BtnStudioDb, PROP_STATE, 0);
                    if (BtnBancoAlt) gui_set_prop(BtnBancoAlt, PROP_STATE, 0);
                    if (BtnAgenda)   gui_set_prop(BtnAgenda,   PROP_STATE, 0);
                    if (BtnBeckup)   gui_set_prop(BtnBeckup,   PROP_STATE, 0);
                    if (BtnRecover)  gui_set_prop(BtnRecover,  PROP_STATE, 0);
                    if (BtnHexview)  gui_set_prop(BtnHexview,  PROP_STATE, 0);
                    events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                    precisa_redesenhar = true;
                }
            }
        }
        /* flush UNICO e diferido: nunca dentro do dispatch de clique */
        if (precisa_redesenhar || g_pending_flush) {
            g_pending_flush = 0;
            Flush_Grafico_Janela();
        }
        sys_sleep(euTenhoFoco ? 16 : 32);
    }
    sys_exit();
    return 0;
}
