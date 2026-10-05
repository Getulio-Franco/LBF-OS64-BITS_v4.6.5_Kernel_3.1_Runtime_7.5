#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"

#include "components/TOS_IPC.h"

void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);

extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;

extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

int my_app_slot = -1;
TGUIEnvironment MyApp;

const int winWidth  = 600;
const int winHeight = 450;

// ===== COMPONENTES SOB TESTE =====
TGUIControl* ComboTema    = NULL;   // ComboBoxEx #1 - Temas
TGUIControl* ComboIdioma  = NULL;   // ComboBoxEx #2 - Idiomas
TGUIControl* ComboResolu  = NULL;   // ComboBoxEx #3 - Resoluções
TGUIControl* BtnLimpar    = NULL;
TGUIControl* LogMemo      = NULL;

/* ============================================================================
 * IPC AUXILIAR
 * ============================================================================ */
typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

char Obter_Tecla_Entrada(void) {
    AppWindowInfoExtended* ext = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext->tem_evento_teclado == 1) {
        char key = (char)ext->fila_teclado_virtual;
        ext->tem_evento_teclado = 0;
        return key;
    }
    return 0;
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

void Log(const char* texto) {
    GUI_Memo_AddStr(LogMemo, texto);
    GUI_Memo_AddStr(LogMemo, "\n");
}

/* ============================================================================
 * CALLBACK ÚNICO — detecta qual combo disparou pelo nome/ponteiro
 * ============================================================================ */
void OnComboExChange(void* sender) {
    TGUIControl* c = (TGUIControl*)sender;
    if (!c || !LogMemo) return;

    const char* nome = "???";
    if (c == ComboTema)   nome = "TEMA";
    else if (c == ComboIdioma) nome = "IDIOMA";
    else if (c == ComboResolu) nome = "RESOLUCAO";

    char idx_str[4];
    itoa(GUI_ComboBoxEx_GetIndex(c), idx_str, 10);

    GUI_Memo_AddStr(LogMemo, "[");
    GUI_Memo_AddStr(LogMemo, nome);
    GUI_Memo_AddStr(LogMemo, "] selecionou: ");
    GUI_Memo_AddStr(LogMemo, GUI_ComboBoxEx_GetText(c));
    GUI_Memo_AddStr(LogMemo, " (idx=");
    GUI_Memo_AddStr(LogMemo, idx_str);
    GUI_Memo_AddStr(LogMemo, ")\n");
}

void OnBtnLimparClick(void* sender) {
    (void)sender;
    GUI_Memo_Clear(LogMemo);
    Log("--- LOG LIMPO ---");
    Log("Aguardando interacao...");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("Teste ComboBoxEx", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);

    GUI_InitApplication(&MyApp, my_app_slot, "Teste TComboBoxEx v1.0", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000);

    /* ================================================================
     * LAYOUT: 3 ComboBoxEx em linha, área livre embaixo para os popups
     * ================================================================ */

    // Combo #1 - TEMAS (5 itens)
    ComboTema = GUI_CreateComboBoxEx(&MyApp, 30, 50, 160, 22, OnComboExChange);
    GUI_ComboBoxEx_AddItem(ComboTema, "Tema Claro");
    GUI_ComboBoxEx_AddItem(ComboTema, "Tema Escuro");
    GUI_ComboBoxEx_AddItem(ComboTema, "Tema Azul");
    GUI_ComboBoxEx_AddItem(ComboTema, "Tema Roxo");
    GUI_ComboBoxEx_AddItem(ComboTema, "Tema Verde");

    // Combo #2 - IDIOMAS (3 itens)
    ComboIdioma = GUI_CreateComboBoxEx(&MyApp, 210, 50, 160, 22, OnComboExChange);
    GUI_ComboBoxEx_AddItem(ComboIdioma, "Portugues");
    GUI_ComboBoxEx_AddItem(ComboIdioma, "English");
    GUI_ComboBoxEx_AddItem(ComboIdioma, "Espanol");

    // Combo #3 - RESOLUCOES (4 itens)
    ComboResolu = GUI_CreateComboBoxEx(&MyApp, 390, 50, 160, 22, OnComboExChange);
    GUI_ComboBoxEx_AddItem(ComboResolu, "640x480");
    GUI_ComboBoxEx_AddItem(ComboResolu, "800x600");
    GUI_ComboBoxEx_AddItem(ComboResolu, "1024x768");
    GUI_ComboBoxEx_AddItem(ComboResolu, "1920x1080");

    BtnLimpar = GUI_CreateButton(&MyApp, 30, 90, 180, 28, "LIMPAR LOG", OnBtnLimparClick);

    LogMemo = GUI_CreateMemo(&MyApp, 30, 130, 540, 290);
    gui_set_prop(LogMemo, PROP_COLOR, 0x000000);

    Log("=== TESTE TComboBoxEx (popup/dropdown) ===");
    Log("");
    Log("COMPORTAMENTO ESPERADO:");
    Log("  * Clique no campo/seta -> abre popup abaixo");
    Log("  * Clique num item      -> seleciona e fecha");
    Log("  * Clique fora          -> fecha sem mudar");
    Log("  * Tecla W/S            -> navega entre itens");
    Log("  * Tecla Esc            -> fecha o popup");
    Log("  * 3 combos na tela     -> teste isolamento");
    Log("");
    Log("--- AGUARDANDO INTERACAO ---");

    g_focused_control = (void*)ComboTema;
    ultimo_controle_focado = (void*)ComboTema;
    gui_set_prop(ComboTema, PROP_SET_FOCUS, 1);

    Flush_Grafico_Janela();

    /* ================================================================
     * LOOP DE EVENTOS
     * ================================================================ */
    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }

        bool precisa_redesenhar = false;

        if (primeiro_desenho) {
            primeiro_desenho = false;
            precisa_redesenhar = true;
        }

        bool foco_real = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (foco_real != ultimo_estado_foco) {
            ultimo_estado_foco = foco_real;
            if (MyApp.MainWindow)
                ((TForm*)MyApp.MainWindow)->ActiveFocus = foco_real;
            precisa_redesenhar = true;
        }

        if (g_focused_control != NULL) {
            ultimo_controle_focado = g_focused_control;
        } else if (ultimo_controle_focado != NULL) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado, PROP_SET_FOCUS, 1);
        }

        char key = Obter_Tecla_Entrada();
        if (key != 0) {
            GUI_ProcessKeyboard(&MyApp, key);
            precisa_redesenhar = true;
        }

        if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
            if (mouse_hold_timer == 0) {
                int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                ultimo_x = rel_x;
                ultimo_y = rel_y;
                mouse_hold_timer = 2;

                if (BtnLimpar && rel_x >= BtnLimpar->Left &&
                    rel_x < (BtnLimpar->Left + BtnLimpar->Width) &&
                    rel_y >= BtnLimpar->Top &&
                    rel_y < (BtnLimpar->Top + BtnLimpar->Height)) {
                    gui_set_prop(BtnLimpar, PROP_STATE, 2);
                }

                events_process_mouse(rel_x, rel_y, 1, 0);

                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                    precisa_redesenhar = true;
                    if (g_focused_control != NULL)
                        ultimo_controle_focado = g_focused_control;
                }
            }
            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
        }

        if (mouse_hold_timer > 0) {
            mouse_hold_timer--;
            if (mouse_hold_timer == 0) {
                if (BtnLimpar) gui_set_prop(BtnLimpar, PROP_STATE, 0);
                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                precisa_redesenhar = true;
            }
        }

        if (precisa_redesenhar) Flush_Grafico_Janela();

        sys_sleep(foco_real ? 16 : 32);
    }

    sys_exit();
    return 0;
}
