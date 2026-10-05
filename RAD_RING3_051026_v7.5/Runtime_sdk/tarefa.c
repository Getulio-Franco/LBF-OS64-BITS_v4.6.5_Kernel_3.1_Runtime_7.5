#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
// Componentes do Sistema encapsulados
#include "components/TOS_IPC.h"

// Protótipos de renderização gráfica
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);

// Funções externas de controle de hardware e foco
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;

// Protótipos de manipulação de Memo na SDK
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

// Variáveis de controle do ambiente da aplicação
#define MAX_PROCESSES 64
TProcessInfo lista_ps[MAX_PROCESSES];
int my_app_slot = -1;
TGUIEnvironment MyApp;

// Configurações de Dimensão
const int winWidth = 550;
const int winHeight = 520; // Aumentado para melhor espaçamento

// Ponteiros de Controle RAD
TGUIControl* ExeMemo      = NULL;
TGUIControl* EditPID      = NULL;
TGUIControl* EditPath     = NULL;
TGUIControl* BtnAtualizar = NULL;
TGUIControl* BtnKillUnico = NULL;
TGUIControl* BtnExecutar  = NULL;

/* ============================================================================
* ESTRUTURA AUXILIAR IPC (Mapeamento de Eventos Estendidos)
* ============================================================================ */
typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

/* ============================================================================
* FUNÇÕES AUXILIARES E DE AMBIENTE
* ============================================================================ */
char Obter_Tecla_Entrada(void) {
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0;
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
    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    }
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50);
}

/* ============================================================================
* CALLBACKS DE EVENTOS RAD
* ============================================================================ */
void OnBtnAtualizarClick(void* sender) {
    for(int z = 0; z < MAX_PROCESSES; z++) {
        lista_ps[z].pid = 0;
    }

    int qtd_processos = sys_get_ps_data(lista_ps, MAX_PROCESSES);
    
    GUI_Memo_Clear(ExeMemo);
    GUI_Memo_AddStr(ExeMemo, "=== GERENCIADOR DE TAREFAS LBF ===\n");
    
    char qtd_str[16];
    itoa((uint64_t)qtd_processos, qtd_str, 10);
    GUI_Memo_AddStr(ExeMemo, "Processos ativos: ");
    GUI_Memo_AddStr(ExeMemo, qtd_str);
    GUI_Memo_AddStr(ExeMemo, "\n------------------------------------\n");

    if (qtd_processos == 0) {
        GUI_Memo_AddStr(ExeMemo, "Nenhum processo encontrado.\n");
    } else {
        for(int k = 0; k < MAX_PROCESSES; k++) {
            if(lista_ps[k].pid != 0) {
                char pid_str[16];
                itoa(lista_ps[k].pid, pid_str, 10);
                
                GUI_Memo_AddStr(ExeMemo, "PID: ");
                GUI_Memo_AddStr(ExeMemo, pid_str);
                GUI_Memo_AddStr(ExeMemo, " | Nome: ");
                GUI_Memo_AddStr(ExeMemo, lista_ps[k].name);
                GUI_Memo_AddStr(ExeMemo, "\n");
            }
        }
    }
}

void OnBtnKillClick(void* sender) {
    char* texto_pid = GUI_Edit_GetText(EditPID);
    if (!texto_pid || texto_pid[0] == '\0') {
        GUI_Memo_AddStr(ExeMemo, "[ERRO] Digite um PID valido.\n");
        return;
    }

    int pid_alvo = atoi(texto_pid);
    
    if (pid_alvo < 5) {
        GUI_Memo_AddStr(ExeMemo, "[ERRO] PID invalido ou protegido.\n");
        return;
    }

    bool encontrado = false;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (lista_ps[i].pid == (uint64_t)pid_alvo) {
            encontrado = true;
            break;
        }
    }

    if (!encontrado) {
        GUI_Memo_AddStr(ExeMemo, "[AVISO] PID nao encontrado na lista. Tentando encerrar mesmo assim...\n");
    }

    for (int i = 0; i < (MAX_EXTERNAL_APPS - 5); i++) {
        if (IPC_WINDOW_LIST[i].pid == (uint64_t)pid_alvo && IPC_WINDOW_LIST[i].is_active == 1) {
            IPC_WINDOW_LIST[i].is_active = 0;
            break;
        }
    }
    
    sys_sleep(20);
    int kill_status = sys_kill((uint64_t)pid_alvo);
    
    char pid_str[16];
    itoa(pid_alvo, pid_str, 10);

    if (kill_status == 0) {
        GUI_Memo_AddStr(ExeMemo, "[OK] Processo ");
        GUI_Memo_AddStr(ExeMemo, pid_str);
        GUI_Memo_AddStr(ExeMemo, " finalizado com sucesso.\n");
    } else {
        GUI_Memo_AddStr(ExeMemo, "[ERRO] Falha ao finalizar o processo ");
        GUI_Memo_AddStr(ExeMemo, pid_str);
        GUI_Memo_AddStr(ExeMemo, ".\n");
    }

    GUI_Edit_SetText(EditPID, "");
}

void OnBtnExecutarClick(void* sender) {
    char* caminho_elf = GUI_Edit_GetText(EditPath);
    if (!caminho_elf || caminho_elf[0] == '\0') {
        GUI_Memo_AddStr(ExeMemo, "[ERRO] Caminho do executavel vazio.\n");
        return;
    }

    GUI_Memo_AddStr(ExeMemo, "[INFO] Solicitando execucao: ");
    GUI_Memo_AddStr(ExeMemo, caminho_elf);
    GUI_Memo_AddStr(ExeMemo, "\n");

    int exec_status = sys_exec(caminho_elf);
    
    if (exec_status == 0) {
        GUI_Memo_AddStr(ExeMemo, "[OK] Execucao iniciada com sucesso.\n");
        GUI_Edit_SetText(EditPath, "");
    } else if (exec_status == -1) {
        GUI_Memo_AddStr(ExeMemo, "[ERRO] Caminho invalido ou ponteiro nulo.\n");
    } else if (exec_status == -2) {
        GUI_Memo_AddStr(ExeMemo, "[AVISO] Sistema ocupado. Tente novamente em instantes.\n");
    } else {
        GUI_Memo_AddStr(ExeMemo, "[ERRO] Falha desconhecida. Codigo: ");
        char err_str[16];
        itoa((uint64_t)exec_status, err_str, 10);
        GUI_Memo_AddStr(ExeMemo, err_str);
        GUI_Memo_AddStr(ExeMemo, "\n");
    }
}

/* ============================================================================
* FUNÇÃO PRINCIPAL (MAIN)
* ============================================================================ */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0;
    static int ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("Gerenciador de Tarefas LBF", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Gerenciador de Tarefas LBF v0.1.0", winWidth, winHeight);
    
    // Fundo Cinza para a janela principal
    if (MyApp.MainWindow) {
        gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);
    }

    /* =========================================================================
    * DESIGN DO LAYOUT DE COMPONENTES (CORRIGIDO)
    * ========================================================================= */
    int margin = 10;
    int spacing = 10;
    int ctrlH = 30;
    
    // Larguras calculadas para preencher a janela (550 - 2*margin = 530)
    // Botão: 240px | Espaço: 10px | Edit: 270px | Margem direita: 10px = 530px ✓
    int btnW = 240;
    int editW = 270;

    // Linha 1: Botão Atualizar (largura menor, centralizado visualmente)
    BtnAtualizar = GUI_CreateButton(&MyApp, margin, 40, btnW, ctrlH, "ATUALIZAR LISTA", OnBtnAtualizarClick);
    
    // Memo: ocupa toda a largura disponível
    // Y=80, Height=270 → termina em Y=350
    ExeMemo = GUI_CreateMemo(&MyApp, margin, 80, winWidth - (2 * margin), 270);
    gui_set_prop(ExeMemo, PROP_COLOR, 0x202020);
    GUI_Memo_AddStr(ExeMemo, "Bem-vindo ao Gerenciador de Tarefas LBF.\n");
    GUI_Memo_AddStr(ExeMemo, "Clique em 'ATUALIZAR LISTA' para carregar os processos.\n");

    // Linha 2: Finalizar Processo (Y=370, folga de 20px após o memo)
    int y_row2 = 370;
    BtnKillUnico = GUI_CreateButton(&MyApp, margin, y_row2, btnW, ctrlH, "FINALIZAR (PID)", OnBtnKillClick);
    EditPID      = GUI_CreateEdit(&MyApp, margin + btnW + spacing, y_row2, editW, ctrlH, "", NULL);

    // Linha 3: Executar Processo (Y=410, folga de 10px após linha anterior)
    int y_row3 = 410;
    BtnExecutar  = GUI_CreateButton(&MyApp, margin, y_row3, btnW, ctrlH, "EXECUTAR PROCESSO", OnBtnExecutarClick);
    EditPath     = GUI_CreateEdit(&MyApp, margin + btnW + spacing, y_row3, editW, ctrlH, "", NULL);

    // Foco inicial no Edit de Execução
    g_focused_control = (void*)EditPath;
    ultimo_controle_focado = (void*)EditPath;
    gui_set_prop(EditPath, PROP_SET_FOCUS, 1);

    // Carrega a lista automaticamente na inicialização
    OnBtnAtualizarClick(NULL);

    Flush_Grafico_Janela();

    /* =========================================================================
    * LOOP DE EVENTOS CONTINUO
    * ========================================================================= */
    while(1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }

        bool precisa_redesenhar = false;

        if (primeiro_desenho) {
            primeiro_desenho = false;
            precisa_redesenhar = true;
        }

        bool euTenhoFocoJanelaReal = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFocoJanelaReal != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFocoJanelaReal;
            if (MyApp.MainWindow) {
                ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFocoJanelaReal;
            }
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

                if (BtnAtualizar && rel_x >= BtnAtualizar->Left && rel_x < (BtnAtualizar->Left + BtnAtualizar->Width) &&
                    rel_y >= BtnAtualizar->Top && rel_y < (BtnAtualizar->Top + BtnAtualizar->Height)) {
                    gui_set_prop(BtnAtualizar, PROP_STATE, 2);
                }
                else if (BtnKillUnico && rel_x >= BtnKillUnico->Left && rel_x < (BtnKillUnico->Left + BtnKillUnico->Width) &&
                    rel_y >= BtnKillUnico->Top && rel_y < (BtnKillUnico->Top + BtnKillUnico->Height)) {
                    gui_set_prop(BtnKillUnico, PROP_STATE, 2);
                }
                else if (BtnExecutar && rel_x >= BtnExecutar->Left && rel_x < (BtnExecutar->Left + BtnExecutar->Width) &&
                    rel_y >= BtnExecutar->Top && rel_y < (BtnExecutar->Top + BtnExecutar->Height)) {
                    gui_set_prop(BtnExecutar, PROP_STATE, 2);
                }

                events_process_mouse(rel_x, rel_y, 1, 0);
                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                    precisa_redesenhar = true;
                    if (g_focused_control != NULL) {
                        ultimo_controle_focado = g_focused_control;
                    }
                }
            }
            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
        }

        if (mouse_hold_timer > 0) {
            mouse_hold_timer--;
            if (mouse_hold_timer == 0) {
                if (BtnAtualizar) gui_set_prop(BtnAtualizar, PROP_STATE, 0);
                if (BtnKillUnico) gui_set_prop(BtnKillUnico, PROP_STATE, 0);
                if (BtnExecutar)  gui_set_prop(BtnExecutar, PROP_STATE, 0);
                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                precisa_redesenhar = true;
            }
        }

        if (precisa_redesenhar) {
            Flush_Grafico_Janela();
        }

        sys_sleep(euTenhoFocoJanelaReal ? 16 : 32);
    }

    sys_exit();
    return 0;
}
