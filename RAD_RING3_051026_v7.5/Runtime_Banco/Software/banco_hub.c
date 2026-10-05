/* ============================================================================
 CENTRAL DE FERRAMENTAS DE BANCO DE DADOS v1.0 - LBF OS
 ============================================================================ */
#include <string.h>
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "Runtime_sdk/components/TOS_IPC.h"

/* Protótipos de funções externas para evitar declaração implícita */
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth  = 420;
const int winHeight = 270;

TGUIControl* LabelStatus = NULL;

/* Helper para execução segura de binários no diretório raiz */
static void Executar_Ferramenta(const char* elf_name) {
    if (!elf_name || elf_name[0] == '\0') return;

    if (sys_fat_chdir("/") != 0) {
        sys_fat_chdir("0:/");
    }

    char msg[128];
    strcpy(msg, "Executando: ");
    strcat(msg, elf_name);
    if (LabelStatus) GUI_Edit_SetText(LabelStatus, msg);

    sys_exec(elf_name);
}

/* ---------------- Callbacks dos Botoes ---------------- */
void OnBtnStudioExClick(void* sender) { Executar_Ferramenta("studioex.elf"); }
void OnBtnRecoverClick(void* sender)  { Executar_Ferramenta("recover.elf");  }
void OnBtnHexViewClick(void* sender)  { Executar_Ferramenta("hexview.elf");  }
void OnBtnStudioClick(void* sender)   { Executar_Ferramenta("studio.elf");   }
void OnBtnBackupClick(void* sender)   { Executar_Ferramenta("beckup.elf");   }
void OnBtnStudioDBClick(void* sender) { Executar_Ferramenta("studiodb.elf"); }
void OnBtnBancoAltClick(void* sender) { Executar_Ferramenta("bancoalt.elf"); }
void OnBtnAgendaClick(void* sender)   { Executar_Ferramenta("agenda.elf");   }

/* ---------------- Eventos de Janela / IPC ---------------- */
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

/* ---------------- Main ---------------- */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("Central Banco de Dados", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Banco de Dados - Launcher", winWidth, winHeight);

    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);
    }

    /* Layout em Colunas (Coluna 1: X=20, Coluna 2: X=220 | Largura: 180, Altura: 32) */
    
    // Coluna 1
    GUI_CreateButton(&MyApp, 20, 40,  180, 32, "Studio EX",  OnBtnStudioExClick);
    GUI_CreateButton(&MyApp, 20, 82,  180, 32, "Recover",    OnBtnRecoverClick);
    GUI_CreateButton(&MyApp, 20, 124, 180, 32, "Hex View",   OnBtnHexViewClick);
    GUI_CreateButton(&MyApp, 20, 166, 180, 32, "Studio",     OnBtnStudioClick);

    // Coluna 2
    GUI_CreateButton(&MyApp, 220, 40,  180, 32, "Backup",    OnBtnBackupClick);
    GUI_CreateButton(&MyApp, 220, 82,  180, 32, "Studio DB", OnBtnStudioDBClick);
    GUI_CreateButton(&MyApp, 220, 124, 180, 32, "Banco Alt", OnBtnBancoAltClick);
    GUI_CreateButton(&MyApp, 220, 166, 180, 32, "Agenda",    OnBtnAgendaClick);

    // Barra de Status na parte inferior
    LabelStatus = GUI_CreateLabel(&MyApp, 20, 215, "Status: Selecione uma ferramenta...");

    Flush_Grafico_Janela();

    /* Loop Principal de Eventos */
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

        bool euTenhoFoco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFoco != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFoco;
            if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco;
            precisa_redesenhar = true;
        }

        if (euTenhoFoco) {
            if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
                if (mouse_hold_timer == 0) {
                    int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                    int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                    ultimo_x = rel_x; 
                    ultimo_y = rel_y; 
                    mouse_hold_timer = 2;

                    events_process_mouse(rel_x, rel_y, 1, 0);
                    if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                        precisa_redesenhar = true;
                    }
                }
                IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
            }

            if (mouse_hold_timer > 0) {
                mouse_hold_timer--;
                if (mouse_hold_timer == 0) {
                    events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                    precisa_redesenhar = true;
                }
            }
        }

        if (precisa_redesenhar) {
            Flush_Grafico_Janela();
        }

        sys_sleep(euTenhoFoco ? 16 : 32);
    }

    sys_exit();
    return 0;
}
