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

const int winWidth  = 550;
const int winHeight = 410;

// --- Form principal ---
TGUIControl* BtnAbrir = NULL;
TGUIControl* BtnFundo = NULL;
TGUIControl* LogMemo  = NULL;

// --- Dialog modal VITRINE (9 componentes) ---
TGUIControl* DialogVitrine = NULL;
TGUIControl* EditVitrine   = NULL;   // 1) TEdit
TGUIControl* CheckVitrine  = NULL;   // 2) TCheckBox
TGUIControl* RadioAVitrine = NULL;   // 3) TRadioButton (A)
TGUIControl* RadioBVitrine = NULL;   // 3) TRadioButton (B)
TGUIControl* ComboVitrine  = NULL;   // 4) TComboBoxEx
TGUIControl* MemoVitrine   = NULL;   // 5) TMemo
TGUIControl* GridVitrine   = NULL;   // 6) TStringGrid
TGUIControl* ImageVitrine  = NULL;   // 7) TImage
                                     // 8) TLabel  / 9) TButton = abaixo

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
    // 1) Form principal
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    // 2) Dialog modal POR CIMA (TForm separado na pilha Z)
    if (DialogVitrine && DialogVitrine->KernelHandle) {
        gui_render_form((TForm*)DialogVitrine->KernelHandle);
    }
    // 3) Envia o buffer para a tela
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

void Log(const char* s) {
    GUI_Memo_AddStr(LogMemo, s);
    GUI_Memo_AddStr(LogMemo, "\n");
}

void LogVitrine(const char* s) {
    if (!MemoVitrine) return;
    GUI_Memo_AddStr(MemoVitrine, s);
    GUI_Memo_AddStr(MemoVitrine, "\n");
}

/* ============================================================================
 * CALLBACKS DOS COMPONENTES DA VITRINE (log no memo DENTRO do dialog)
 * ============================================================================ */
void OnComboVitrineChange(void* sender) {
    TGUIControl* c = (TGUIControl*)sender;
    if (!c) return;
    LogVitrine("ComboEx -> ");
    LogVitrine(GUI_ComboBoxEx_GetText(c));
}

void OnCheckVitrineChange(void* sender) {
    (void)sender;
    LogVitrine("CheckBox alternou");
}

void OnRadioVitrineChange(void* sender) {
    if (sender == (void*)RadioAVitrine) LogVitrine("Radio A selecionado");
    else                                LogVitrine("Radio B selecionado");
}

void OnGridVitrineClick(void* sender, int row) {
    (void)sender;
    char buf[8];
    itoa(row, buf, 10);
    LogVitrine("Grid linha ");
    LogVitrine(buf);
}

/* ============================================================================
 * CALLBACKS DO DIALOG (OK / CANCEL)
 * ============================================================================ */
void OnBtnOKClick(void* sender) {
    (void)sender;
    GUI_Form_Close(DialogVitrine, 1);
    Log(">> Vitrine fechou com OK (result=1)");
    Log(">> Modal liberado: form principal volta a agir.");
}

void OnBtnCancelClick(void* sender) {
    (void)sender;
    GUI_Form_Close(DialogVitrine, 2);
    Log(">> Vitrine fechou com CANCEL (result=2)");
    Log(">> Modal liberado: nada foi executado.");
}

/* ============================================================================
 * CALLBACKS DO FORM PRINCIPAL
 * ============================================================================ */
void OnBtnAbrirClick(void* sender) {
    (void)sender;
    Log("-- Abrindo VITRINE MODAL (9 componentes)...");
    GUI_Form_ShowModal(DialogVitrine);
    Log("-- MODAL ATIVO: clique fora nao deve agir!");
}

void OnBtnFundoClick(void* sender) {
    (void)sender;
    Log("!! BOTAO DE FUNDO CLICADO !!");
    Log("!! (Se o modal estava aberto, o bloqueio FALHOU!)");
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

    my_app_slot = OS_IPC_RegisterApp("Vitrine Forms Modais", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);

    GUI_InitApplication(&MyApp, my_app_slot, "Vitrine TForm Modal v1.0", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000);

    /* =========================================================================
     * FORM PRINCIPAL
     * ========================================================================= */
    BtnAbrir = GUI_CreateButton(&MyApp, 30, 40, 220, 30, "ABRIR VITRINE", OnBtnAbrirClick);
    BtnFundo = GUI_CreateButton(&MyApp, 30, 80, 220, 30, "BOTAO DE FUNDO", OnBtnFundoClick);

    LogMemo = GUI_CreateMemo(&MyApp, 30, 120, 490, 260);
    gui_set_prop(LogMemo, PROP_COLOR, 0x000000);

    /* =========================================================================
     * DIALOG MODAL VITRINE — os 9 componentes da SDK
     * Layout em 2 colunas (440x350 em 60,40)
     * ========================================================================= */
    DialogVitrine = GUI_CreateForm(&MyApp, NULL, "Vitrine Modal", 60, 40, 440, 350, bsDialog, true);
    if (DialogVitrine) {
        // --- 8) TLabel (titulo interno) ---
        GUI_CreateLabelOnForm(&MyApp, DialogVitrine, 20, 28, "Vitrine: 9 componentes da SDK");

        // --- 1) TEdit ---
        EditVitrine = GUI_CreateEditOnForm(&MyApp, DialogVitrine, 20, 52, 200, 24, "digite aqui...", NULL);

        // --- 2) TCheckBox ---
        CheckVitrine = GUI_CreateCheckBoxOnForm(&MyApp, DialogVitrine, 20, 84, "Lembrar escolha", OnCheckVitrineChange);

        // --- 3) TRadioButton (par de grupo) ---
        RadioAVitrine = GUI_CreateRadioButtonOnForm(&MyApp, DialogVitrine, 20, 108, "Opcao A", OnRadioVitrineChange);
        RadioBVitrine = GUI_CreateRadioButtonOnForm(&MyApp, DialogVitrine, 20, 130, "Opcao B", OnRadioVitrineChange);

        // --- 6) TStringGrid ---
        GridVitrine = GUI_CreateStringGridOnForm(&MyApp, DialogVitrine, 20, 156, 200, 140, 3, 5);
        if (GridVitrine) {
            GUI_Grid_SetCells(GridVitrine, 0, 0, "Nome");
            GUI_Grid_SetCells(GridVitrine, 1, 0, "Qtd");
            GUI_Grid_SetCells(GridVitrine, 2, 0, "Status");
            GUI_Grid_SetCells(GridVitrine, 0, 1, "Item A");
            GUI_Grid_SetCells(GridVitrine, 1, 1, "10");
            GUI_Grid_SetCells(GridVitrine, 2, 1, "OK");
            GUI_Grid_SetCells(GridVitrine, 0, 2, "Item B");
            GUI_Grid_SetCells(GridVitrine, 1, 2, "25");
            GUI_Grid_SetCells(GridVitrine, 2, 2, "OK");
            GUI_Grid_SetCells(GridVitrine, 0, 3, "Item C");
            GUI_Grid_SetCells(GridVitrine, 1, 3, "7");
            GUI_Grid_SetCells(GridVitrine, 2, 3, "ALERTA");
            GridVitrine->OnItemClick = OnGridVitrineClick;
        }

        // --- 4) TComboBoxEx (popup dentro do modal!) ---
        ComboVitrine = GUI_CreateComboBoxExOnForm(&MyApp, DialogVitrine, 240, 52, 180, 22, OnComboVitrineChange);
        if (ComboVitrine) {
            GUI_ComboBoxEx_AddItem(ComboVitrine, "Tema Claro");
            GUI_ComboBoxEx_AddItem(ComboVitrine, "Tema Escuro");
            GUI_ComboBoxEx_AddItem(ComboVitrine, "Tema Azul");
        }

        // --- 5) TMemo (log interno da vitrine) ---
        MemoVitrine = GUI_CreateMemoOnForm(&MyApp, DialogVitrine, 240, 84, 180, 110);

        // --- 7) TImage (box vazio; passe um .bmp real se quiser) ---
        ImageVitrine = GUI_CreateImageOnForm(&MyApp, DialogVitrine, 240, 204, 180, 70, NULL);

        // --- 9) TButton (decisao) ---
        GUI_CreateButtonOnForm(&MyApp, DialogVitrine, 70, 306, 90, 28, "OK",     OnBtnOKClick);
        GUI_CreateButtonOnForm(&MyApp, DialogVitrine, 200, 306, 90, 28, "CANCEL", OnBtnCancelClick);

        GUI_Form_Close(DialogVitrine, 0);   // esconde no boot (sem modal)
    }

    Log("=== VITRINE TFORM MODAL (9 componentes) ===");
    Log("");
    Log("Dentro do dialog: Label, Edit, CheckBox,");
    Log("RadioButton x2, ComboBoxEx, Memo, StringGrid,");
    Log("Image e Buttons OK/CANCEL.");
    Log("");
    Log("COMPORTAMENTO ESPERADO:");
    Log("  * Tudo funciona DENTRO do modal (clique/teclado)");
    Log("  * Popup do ComboBoxEx flutua sobre o proprio dialog");
    Log("  * Fora do modal: nada responde");
    Log("  * OK=1 / CANCEL=2 fecham e liberam o pai");
    Log("");
    Log("--- AGUARDANDO INTERACAO ---");

    g_focused_control = (void*)BtnAbrir;
    ultimo_controle_focado = (void*)BtnAbrir;
    gui_set_prop(BtnAbrir, PROP_SET_FOCUS, 1);

    Flush_Grafico_Janela();

    /* =========================================================================
     * LOOP DE EVENTOS
     * ========================================================================= */
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

                if (BtnAbrir && rel_x >= BtnAbrir->Left && rel_x < (BtnAbrir->Left + BtnAbrir->Width) &&
                    rel_y >= BtnAbrir->Top && rel_y < (BtnAbrir->Top + BtnAbrir->Height)) {
                    gui_set_prop(BtnAbrir, PROP_STATE, 2);
                }
                else if (BtnFundo && rel_x >= BtnFundo->Left && rel_x < (BtnFundo->Left + BtnFundo->Width) &&
                         rel_y >= BtnFundo->Top && rel_y < (BtnFundo->Top + BtnFundo->Height)) {
                    gui_set_prop(BtnFundo, PROP_STATE, 2);
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
                if (BtnAbrir) gui_set_prop(BtnAbrir, PROP_STATE, 0);
                if (BtnFundo) gui_set_prop(BtnFundo, PROP_STATE, 0);
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
