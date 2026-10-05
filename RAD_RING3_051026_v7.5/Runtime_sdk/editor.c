/* ============================================================================
BLOCO DE NOTAS LBF v1.3
v1.3: - removidos os botoes/teclas de atalho (NOVO, Ctrl+S/O/N)
      + adicionado botao CANCELAR (descarta alteracoes recarregando do disco)
      * botoes redimensionados pela REGRA DE OURO:
        LARGURA = caracteres_do_caption * 10 + 15   (8 chars -> 95 px)
============================================================================ */
#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "components/TOS_IPC.h"
#include "../system/sysutils.h"

void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 600;
const int winHeight = 500;

TGUIControl* MemoTexto   = NULL;
TGUIControl* EditArquivo = NULL;
TGUIControl* BtnCarregar = NULL;
TGUIControl* BtnSalvar   = NULL;
TGUIControl* BtnCancelar = NULL;
TGUIControl* LabelLnCol  = NULL;
TGUIControl* LabelStatus = NULL;

static int linha_cursor = 1;
static int coluna_cursor = 1;
static int doc_modificado = 0;
static char status_msg[80] = "Pronto";

#define MEMO_X 10
#define MEMO_Y 75
#define MEMO_W 580
#define MEMO_H 355
#define STATUS_Y 440
#define CHAR_W 8
#define CHAR_H 16
#define MAX_DOC 4096

/* REGRA DE OURO (memorizada): largura_botao = strlen(caption)*10 + 15 */
#define BTN_W(caption) ((int)(sizeof(caption) - 1) * 10 + 15)

char* GUI_Memo_GetText(TGUIControl* memo) {
    if (!memo || !memo->Buffer) return NULL;
    return (char*)memo->Buffer;
}

/* ---------------- status e cursor ---------------- */
static void Editor_Atualizar_Status(void) {
    char buf[110];
    strcpy(buf, status_msg);
    if (doc_modificado) strcat(buf, "  [MOD]");
    if (LabelStatus) GUI_Edit_SetText(LabelStatus, buf);
}
static void Editor_Set_Status(const char* msg) {
    strncpy(status_msg, msg, sizeof(status_msg) - 1);
    status_msg[sizeof(status_msg) - 1] = 0;
    Editor_Atualizar_Status();
}
static void Editor_Marcar_Mod(void) {
    doc_modificado = 1;
    Editor_Atualizar_Status();
}
static void Editor_Atualizar_LnCol(void) {
    char buf[32];
    char num_ln[12], num_col[12];
    IntToStr(linha_cursor, num_ln);
    IntToStr(coluna_cursor, num_col);
    strcpy(buf, "Ln ");
    strcat(buf, num_ln);
    strcat(buf, ", Col ");
    strcat(buf, num_col);
    if (LabelLnCol) GUI_Edit_SetText(LabelLnCol, buf);
}
static int Editor_Compr_Linha(int linha) {
    char* texto = GUI_Memo_GetText(MemoTexto);
    int ln = 1, comp = 0;
    if (!texto) return 0;
    for (char* p = texto; *p; p++) {
        if (*p == '\n') {
            if (ln == linha) return comp;
            ln++;
            comp = 0;
        } else if (ln == linha) {
            comp++;
        }
    }
    return (ln == linha) ? comp : 0;
}
static void Editor_Recalcular_LnCol_Fim(void) {
    char* texto = GUI_Memo_GetText(MemoTexto);
    int ln = 1, col = 1;
    if (texto) {
        for (char* p = texto; *p; p++) {
            if (*p == '\n') { ln++; col = 1; }
            else col++;
        }
    }
    linha_cursor = ln;
    coluna_cursor = col;
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

/* ---------------- acoes ---------------- */
void OnBtnCarregarClick(void* sender) {
    (void)sender;
    char* caminho = GUI_Edit_GetText(EditArquivo);
    if (!caminho || caminho[0] == '\0') {
        Editor_Set_Status("Caminho invalido!");
        return;
    }
    file_info_t fi;
    uint32_t tam_real = 0;
    if (sys_fat_stat(caminho, &fi) == 0) tam_real = fi.size;
    GUI_Memo_Clear(MemoTexto);
    char buffer[MAX_DOC + 1];
    memset(buffer, 0, sizeof(buffer));
    int bytes_lidos = sys_fat_read(caminho, (void*)buffer, MAX_DOC);
    if (bytes_lidos > 0) {
        buffer[bytes_lidos] = '\0';
        GUI_Memo_AddStr(MemoTexto, buffer);
        char msg[96]; char num[16];
        if (tam_real > MAX_DOC) {
            itoa(tam_real, num, 10);
            strcpy(msg, "Carregado TRUNCADO (4096 de "); strcat(msg, num); strcat(msg, " B)");
        } else {
            itoa(bytes_lidos, num, 10);
            strcpy(msg, "Carregado: "); strcat(msg, num); strcat(msg, " bytes.");
        }
        Editor_Set_Status(msg);
        doc_modificado = 0;
        Editor_Atualizar_Status();
    } else {
        Editor_Set_Status("Arquivo vazio ou nao encontrado.");
    }
    Editor_Recalcular_LnCol_Fim();
    Editor_Atualizar_LnCol();
}
void OnBtnSalvarClick(void* sender) {
    (void)sender;
    char* caminho = GUI_Edit_GetText(EditArquivo);
    char* conteudo = GUI_Memo_GetText(MemoTexto);
    if (!caminho || caminho[0] == '\0') {
        Editor_Set_Status("Caminho invalido!");
        return;
    }
    if (!conteudo) conteudo = "";
    uint32_t tam = (uint32_t)strlen(conteudo);
    int status = sys_fat_write(caminho, (void*)conteudo, tam);
    if (status == 0) {
        char msg[96]; char num[16];
        itoa(tam, num, 10);
        strcpy(msg, "Salvo: "); strcat(msg, num); strcat(msg, " bytes.");
        Editor_Set_Status(msg);
        doc_modificado = 0;
        Editor_Atualizar_Status();
    } else {
        Editor_Set_Status("[Erro] Falha ao gravar no disco.");
    }
}
/* CANCELAR: descarta as alteracoes recarregando o arquivo do disco
   (ou limpa o memo se o arquivo nao existir) */
void OnBtnCancelarClick(void* sender) {
    (void)sender;
    char* caminho = GUI_Edit_GetText(EditArquivo);
    GUI_Memo_Clear(MemoTexto);
    if (caminho && caminho[0]) {
        char buffer[MAX_DOC + 1];
        memset(buffer, 0, sizeof(buffer));
        int n = sys_fat_read(caminho, (void*)buffer, MAX_DOC);
        if (n > 0) {
            buffer[n] = '\0';
            GUI_Memo_AddStr(MemoTexto, buffer);
        }
    }
    doc_modificado = 0;
    Editor_Set_Status("Alteracoes descartadas.");
    Editor_Recalcular_LnCol_Fim();
    Editor_Atualizar_LnCol();
}

/* ---------------- main ---------------- */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("Bloco de Notas LBF", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Bloco de Notas v1.3", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    /* topo: caminho + 3 botoes na REGRA DE OURO (chars*10+15) */
    GUI_CreateLabel(&MyApp, 10, 42, "Arquivo:");
    EditArquivo  = GUI_CreateEdit(&MyApp, 70, 38, 245, 25, "0:/nota.txt", NULL);
    BtnCarregar  = GUI_CreateButton(&MyApp, 320, 38, BTN_W("CARREGAR"), 25, "CARREGAR", OnBtnCarregarClick); /* 8 -> 95 */
    BtnSalvar    = GUI_CreateButton(&MyApp, 420, 38, BTN_W("SALVAR"),   25, "SALVAR",   OnBtnSalvarClick);   /* 6 -> 75 */
    BtnCancelar  = GUI_CreateButton(&MyApp, 500, 38, BTN_W("CANCELAR"), 25, "CANCELAR", OnBtnCancelarClick); /* 8 -> 95 */

    MemoTexto = GUI_CreateMemo(&MyApp, MEMO_X, MEMO_Y, MEMO_W, MEMO_H);
    gui_set_prop(MemoTexto, PROP_COLOR, 0x000000);

    LabelLnCol  = GUI_CreateLabel(&MyApp, 10, STATUS_Y, "Ln 1, Col 1");
    LabelStatus = GUI_CreateLabel(&MyApp, 150, STATUS_Y, "Pronto");

    g_focused_control = (void*)MemoTexto;
    ultimo_controle_focado = (void*)MemoTexto;
    gui_set_prop(MemoTexto, PROP_SET_FOCUS, 1);
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
                unsigned char k = (unsigned char)key;
                /* sem atalhos de teclado: apenas edicao normal + flag [MOD] */
                if (k >= 32 && k <= 126) {
                    coluna_cursor++;
                    Editor_Marcar_Mod();
                } else if (k == 13 || k == 10) {
                    linha_cursor++;
                    coluna_cursor = 1;
                    Editor_Marcar_Mod();
                } else if (k == 8) {
                    if (coluna_cursor > 1) coluna_cursor--;
                    else if (linha_cursor > 1) {
                        linha_cursor--;
                        coluna_cursor = Editor_Compr_Linha(linha_cursor) + 1;
                    }
                    Editor_Marcar_Mod();
                }
                GUI_ProcessKeyboard(&MyApp, key);
                Editor_Atualizar_LnCol();
                precisa_redesenhar = true;
            }
            if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
                if (mouse_hold_timer == 0) {
                    int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                    int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                    ultimo_x = rel_x; ultimo_y = rel_y; mouse_hold_timer = 2;
                    /* feedback visual dos 3 botoes */
                    if      (BtnCarregar && rel_x >= BtnCarregar->Left && rel_x < BtnCarregar->Left + BtnCarregar->Width && rel_y >= BtnCarregar->Top && rel_y < BtnCarregar->Top + BtnCarregar->Height) gui_set_prop(BtnCarregar, PROP_STATE, 2);
                    else if (BtnSalvar   && rel_x >= BtnSalvar->Left   && rel_x < BtnSalvar->Left   + BtnSalvar->Width   && rel_y >= BtnSalvar->Top   && rel_y < BtnSalvar->Top   + BtnSalvar->Height)   gui_set_prop(BtnSalvar,   PROP_STATE, 2);
                    else if (BtnCancelar && rel_x >= BtnCancelar->Left && rel_x < BtnCancelar->Left + BtnCancelar->Width && rel_y >= BtnCancelar->Top && rel_y < BtnCancelar->Top + BtnCancelar->Height) gui_set_prop(BtnCancelar, PROP_STATE, 2);
                    events_process_mouse(rel_x, rel_y, 1, 0);
                    /* clique no texto reposiciona o cursor */
                    if (rel_x >= MEMO_X && rel_x < (MEMO_X + MEMO_W) &&
                        rel_y >= MEMO_Y && rel_y < (MEMO_Y + MEMO_H)) {
                        linha_cursor = (rel_y - MEMO_Y) / CHAR_H + 1;
                        coluna_cursor = (rel_x - MEMO_X) / CHAR_W + 1;
                        int total_linhas = 1;
                        char* texto = GUI_Memo_GetText(MemoTexto);
                        if (texto) {
                            for (char* p = texto; *p; p++) if (*p == '\n') total_linhas++;
                        }
                        if (linha_cursor > total_linhas) linha_cursor = total_linhas;
                        if (linha_cursor < 1) linha_cursor = 1;
                        int comp = Editor_Compr_Linha(linha_cursor);
                        if (coluna_cursor > comp + 1) coluna_cursor = comp + 1;
                        if (coluna_cursor < 1) coluna_cursor = 1;
                        Editor_Atualizar_LnCol();
                    }
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
                    if (BtnCarregar) gui_set_prop(BtnCarregar, PROP_STATE, 0);
                    if (BtnSalvar)   gui_set_prop(BtnSalvar,   PROP_STATE, 0);
                    if (BtnCancelar) gui_set_prop(BtnCancelar, PROP_STATE, 0);
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
