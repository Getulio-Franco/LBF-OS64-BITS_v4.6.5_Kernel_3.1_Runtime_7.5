/* ============================================================================
   AGENDA LBF v1.3 - Cadastro sobre o BANCO NATIVO (Runtime_Banco)
   Conecta a banco/tabela JA CRIADOS no LBF STUDIO CREATE:
       codigo (I4 PK AI) | nome (C20) | texto (C20)
   v1.3: reescrito limpo - corrige OnBtnAltClick (faltava declarar "cod"),
   mantem san_txt/trim, verificacao de esquema, StringGrid e navegacao.
   ============================================================================ */
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "../system/sysutils.h"
#include "Runtime_sdk/components/TOS_IPC.h"
#include "Runtime_Banco/lib_banco/libbanco.h"

/* ==== CONEXAO: ajuste SO aqui se criar com outros nomes no STUDIO CREATE == */
#define AGENDA_BANCO      "CADASTRO"
#define AGENDA_TABELA     "CLIENTES"
#define AGENDA_CAMPO_COD  "codigo"
#define AGENDA_CAMPO_NOME "nome"
/* ========================================================================== */
#define MAX_REG 100

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

TGUIControl* EditCodigo = NULL;
TGUIControl* EditNome   = NULL;
TGUIControl* EditTexto  = NULL;
TGUIControl* BtnAdd     = NULL;
TGUIControl* BtnAlt     = NULL;
TGUIControl* BtnDel     = NULL;
TGUIControl* BtnPesq    = NULL;
TGUIControl* BtnLimpar  = NULL;
TGUIControl* BtnVolta   = NULL;
TGUIControl* BtnProx    = NULL;
TGUIControl* LabelNav   = NULL;
TGUIControl* GridReg    = NULL;
TGUIControl* MemoLog    = NULL;

/* cache da consulta atual (para navegacao) */
typedef struct { char cod[16]; char nome[24]; char texto[24]; } RegAgenda;
static RegAgenda g_regs[MAX_REG];
static int g_nregs = 0;
static int g_idx   = -1;
static bool g_solicitar_redesenho = false;

/* Remove espacos sobrando nas pontas: "3 " vira "3" */
static void trim_pontas(char* s) {
    if (!s) return;
    int i = 0;
    while (s[i] == ' ') i++;
    int n = (int)strlen(s);
    while (n > i && s[n - 1] == ' ') n--;
    int j = 0;
    while (i < n) s[j++] = s[i++];
    s[j] = 0;
}

/* Copia imprimiveis ASCII + bytes empacotados PT-BR (>= 0x80).
   Rejeita: controle (< 0x20), DEL (0x7F) e '|' (separador de campos do banco). */
static void san_txt(char* dst, const char* src, int max) {
    int i = 0;
    if (src) {
        while (i < max - 1) {
            unsigned char c = (unsigned char)src[i];
            if (c == 0) break;                                   /* fim da string */
            bool ok = ((c >= 0x20 && c <= 0x7E && c != '|') || c >= 0x80);
            if (!ok) break;                                      /* lixo invisivel: para */
            dst[i++] = (char)c;
        }
    }
    dst[i] = 0;
}

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
static int dentro(TGUIControl* c, int x, int y) {
    return c && x >= c->Left && x < (c->Left + c->Width) && y >= c->Top && y < (c->Top + c->Height);
}

/* ============================================================================
   VERIFICACAO DE ESQUEMA: le os campos do disco e avisa se campo de texto
   estiver como INT (causa do "nome=0, texto=0")
   ============================================================================ */
static void verificar_schema(void) {
    static char raw[512];
    log_line("--- ESQUEMA DETECTADO NO DISCO ---");
    if (sys_banco_list_fields(AGENDA_BANCO, AGENDA_TABELA, raw, sizeof(raw)) != BCO_OK) {
        log_line("ALERTA: tabela nao encontrada! Crie-a no STUDIO CREATE.");
        log_line("----------------------------------");
        return;
    }
    char* p = raw; int idx = 0;
    while (p && *p && idx < 3) {
        char* bar = strchr(p, '|');
        if (bar) *bar = 0;
        char* q = p; while (*q == ' ') q++;
        if (*q) {
            char line[96]; char num[8];
            itoa((uint64_t)(uint32_t)(idx + 1), num, 10);
            strcpy(line, "CAMPO "); strcat(line, num); strcat(line, ": ");
            strncat(line, q, sizeof(line) - strlen(line) - 1);
            log_line(line);
            char* cc = strchr(q, ':');
            char tipo = cc ? cc[1] : '?';
            if (idx >= 1 && tipo == 'I') {
                char nm[24]; int k = 0;
                while (q[k] && q[k] != ':' && k < 23) { nm[k] = q[k]; k++; }
                nm[k] = 0;
                char alert[160];
                strcpy(alert, "ALERTA: campo "); strcat(alert, nm);
                strcat(alert, " esta como INT! Texto vira 0. Recrie a tabela com TIPO=text.");
                log_line(alert);
            }
            idx++;
        }
        p = bar ? bar + 1 : 0;
    }
    log_line("----------------------------------");
}

/* ============================================================================
   GRADE: recarrega do disco e preenche o StringGrid
   ============================================================================ */
static void preencher_grid(void) {
    if (!GridReg) return;
    GUI_Grid_SetScroll(GridReg, 0);
    int total = g_nregs + 1;
    if (total < 2) total = 2;
    GridReg->RowCount = total;
    gui_set_prop((void*)GridReg->KernelHandle, PROP_STATE, (uint64_t)total);
    for (int r = 1; r < total; r++) {
        GUI_Grid_SetCells(GridReg, 0, r, "");
        GUI_Grid_SetCells(GridReg, 1, r, "");
        GUI_Grid_SetCells(GridReg, 2, r, "");
        GUI_Grid_SetCells(GridReg, 3, r, "");
    }
    for (int i = 0; i < g_nregs; i++) {
        char num[8];
        itoa((uint64_t)(uint32_t)(i + 1), num, 10);
        GUI_Grid_SetCells(GridReg, 0, i + 1, num);
        GUI_Grid_SetCells(GridReg, 1, i + 1, g_regs[i].cod);
        GUI_Grid_SetCells(GridReg, 2, i + 1, g_regs[i].nome);
        GUI_Grid_SetCells(GridReg, 3, i + 1, g_regs[i].texto);
    }
    g_solicitar_redesenho = true;
}
static int recarregar_regs(const char* campo, const char* valor) {
    g_nregs = 0; g_idx = -1;
    static char out[2048];
    int n = sys_banco_select(AGENDA_BANCO, AGENDA_TABELA, campo, valor, out, sizeof(out));
    if (n < 0) { preencher_grid(); return n; }
    char* p = out;
    while (p && *p && g_nregs < MAX_REG) {
        char* nl = strchr(p, '\n');
        if (nl) *nl = 0;
        RegAgenda* r = &g_regs[g_nregs];
        memset(r, 0, sizeof(*r));
        char* t = p; int idx = 0;
        while (t && *t && idx < 3) {
            char* bar = strchr(t, '|');
            if (bar) *bar = 0;
            char* eq = strchr(t, '=');
            const char* v = eq ? eq + 1 : t;
            while (*v == ' ') v++;
            char valbuf[64];
            strncpy(valbuf, v, 63); valbuf[63] = 0;
            trim_pontas(valbuf);
            {
               int k = 0;
               unsigned char uc;
               while ((uc = (unsigned char)valbuf[k]) != 0 && ((uc >= 0x20 && uc <= 0x7E) || uc >= 0x80)) k++;
               valbuf[k] = 0;
            }
            if (idx == 0) strncpy(r->cod,   valbuf, 15);
            else if (idx == 1) strncpy(r->nome,  valbuf, 23);
            else strncpy(r->texto, valbuf, 23);
            idx++;
            t = bar ? bar + 1 : 0;
        }
        g_nregs++;
        p = nl ? nl + 1 : 0;
    }
    preencher_grid();
    return g_nregs;
}

/* ============================================================================
   NAVEGACAO << VOLTA / PROXIMO >> : carrega o registro da vez nos campos
   ============================================================================ */
static void nav_label(void) {
    char b[64]; char n1[8], n2[8];
    itoa((uint64_t)(uint32_t)(g_idx + 1), n1, 10);
    itoa((uint64_t)(uint32_t)g_nregs, n2, 10);
    strcpy(b, "Registro "); strcat(b, n1); strcat(b, " de "); strcat(b, n2);
    if (LabelNav) GUI_Edit_SetText(LabelNav, b);
}
static void carregar_idx(int i) {
    if (i < 0 || i >= g_nregs) return;
    g_idx = i;
    GUI_Edit_SetText(EditCodigo, g_regs[i].cod);
    GUI_Edit_SetText(EditNome,   g_regs[i].nome);
    GUI_Edit_SetText(EditTexto,  g_regs[i].texto);
    nav_label();
    Flush_Grafico_Janela();
}
void OnBtnVoltaClick(void* s) {
    if (g_nregs == 0) { log_line("Nenhum registro para navegar."); return; }
    int i = (g_idx < 0) ? 0 : g_idx - 1;
    if (i < 0) i = 0;
    carregar_idx(i);
}
void OnBtnProxClick(void* s) {
    if (g_nregs == 0) { log_line("Nenhum registro para navegar."); return; }
    int i = (g_idx < 0) ? 0 : g_idx + 1;
    if (i > g_nregs - 1) i = g_nregs - 1;
    carregar_idx(i);
}

/* ============================================================================
   BOTOES DE MANIPULACAO
   ============================================================================ */
void OnBtnAddClick(void* s) {
    char nome_l[24], texto_l[24];
    san_txt(nome_l, GUI_Edit_GetText(EditNome),  sizeof(nome_l));
    san_txt(texto_l, GUI_Edit_GetText(EditTexto), sizeof(texto_l));
    if (!nome_l[0]) { log_line("Informe o NOME para adicionar."); return; }
    char valores[128];
    strcpy(valores, "0|");
    strcat(valores, nome_l);
    strcat(valores, "|");
    strcat(valores, texto_l);
    int rc = sys_banco_insert(AGENDA_BANCO, AGENDA_TABELA, valores);
    if (rc == BCO_OK) {
        log_line("Registro cadastrado com sucesso.");
        recarregar_regs(0, 0);
        GUI_Edit_SetText(EditCodigo, "");   /* limpa os TRES campos */
        GUI_Edit_SetText(EditNome,   "");
        GUI_Edit_SetText(EditTexto,  "");
        g_idx = -1;
        nav_label();
    } else if (rc == BCO_ERR_NOTFOUND) {
        log_line("Tabela nao encontrada - crie banco/tabela no STUDIO CREATE.");
    } else if (rc == BCO_ERR_FULL) {
        log_line("Tabela cheia (limite de 32KB).");
    } else {
        log_line("Erro ao cadastrar registro.");
    }
}
void OnBtnAltClick(void* s) {
    char* cod_src = GUI_Edit_GetText(EditCodigo);
    if (!cod_src || !cod_src[0]) { log_line("Informe o CODIGO (PK) para alterar."); return; }
    char cod[16];                                  /* <-- CORRIGIDO v1.3 */
    strncpy(cod, cod_src, 15); cod[15] = 0;        /* <-- CORRIGIDO v1.3 */
    trim_pontas(cod);                              /* <-- CORRIGIDO v1.3 */
    char nome_l[24], texto_l[24];
    san_txt(nome_l, GUI_Edit_GetText(EditNome),  sizeof(nome_l));
    san_txt(texto_l, GUI_Edit_GetText(EditTexto), sizeof(texto_l));
    char valores[128];
    strcpy(valores, "0|");
    strcat(valores, nome_l);
    strcat(valores, "|");
    strcat(valores, texto_l);
    int rc = sys_banco_update(AGENDA_BANCO, AGENDA_TABELA, cod, valores);
    if (rc == 1) {
        log_line("Registro alterado com sucesso.");
        int pos = g_idx;
        recarregar_regs(0, 0);
        if (pos >= g_nregs) pos = g_nregs - 1;
        if (pos >= 0) carregar_idx(pos);
    } else if (rc == BCO_ERR_NOTFOUND) {
        log_line("Codigo nao encontrado para alterar.");
    } else {
        log_line("Erro ao alterar registro.");
    }
}
void OnBtnDelClick(void* s) {
    char* cod_src = GUI_Edit_GetText(EditCodigo);
    if (!cod_src || !cod_src[0]) { log_line("Informe o CODIGO (PK) para deletar."); return; }
    char cod[16];
    strncpy(cod, cod_src, 15); cod[15] = 0;
    trim_pontas(cod);
    int rc = sys_banco_delete(AGENDA_BANCO, AGENDA_TABELA, cod);
    if (rc == 1) {
        log_line("Registro deletado com sucesso.");
        recarregar_regs(0, 0);
        GUI_Edit_SetText(EditCodigo, "");
        GUI_Edit_SetText(EditNome,   "");
        GUI_Edit_SetText(EditTexto,  "");
        g_idx = -1;
        nav_label();
    } else if (rc == BCO_ERR_NOTFOUND) {
        log_line("Codigo nao encontrado para deletar.");
    } else {
        log_line("Erro ao deletar registro.");
    }
}
void OnBtnPesqClick(void* s) {
    char* cod  = GUI_Edit_GetText(EditCodigo);
    char* nome = GUI_Edit_GetText(EditNome);
    char codt[16], nomet[24];
    strncpy(codt,  cod  ? cod  : "", 15); codt[15]  = 0; trim_pontas(codt);
    strncpy(nomet, nome ? nome : "", 23); nomet[23] = 0; trim_pontas(nomet);
    const char* fc = 0; const char* fv = 0;
    if (codt[0])       { fc = AGENDA_CAMPO_COD;  fv = codt;  }
    else if (nomet[0]) { fc = AGENDA_CAMPO_NOME; fv = nomet; }
    int n = recarregar_regs(fc, fv);
    if (n < 0)       log_line("Pesquisa falhou: tabela nao encontrada.");
    else if (n == 0) { log_line("Pesquisa concluida: nenhum registro encontrado."); nav_label(); }
    else {
        char b[64]; char num[8];
        itoa((uint64_t)(uint32_t)n, num, 10);
        strcpy(b, "Pesquisa concluida: "); strcat(b, num); strcat(b, " registro(s).");
        log_line(b);
        carregar_idx(0);
    }
}
void OnBtnLimparClick(void* s) {
    GUI_Edit_SetText(EditCodigo, "");
    GUI_Edit_SetText(EditNome,   "");
    GUI_Edit_SetText(EditTexto,  "");
    GUI_Memo_Clear(MemoLog);
    recarregar_regs(0, 0);
    nav_label();
    Flush_Grafico_Janela();
}

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
    my_app_slot = OS_IPC_RegisterApp("Agenda LBF", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Agenda LBF v1.3", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    GUI_CreateLabel(&MyApp, 10, 40, "BANCO: CADASTRO | TABELA: CLIENTES | CAMPOS: Codigo, Nome, Texto");

    GUI_CreateLabel(&MyApp, 10, 70, "CODIGO");
    GUI_CreateLabel(&MyApp, 90, 70, "NOME");
    GUI_CreateLabel(&MyApp, 330, 70, "TEXTO");
    EditCodigo = GUI_CreateEdit(&MyApp, 10,  86, 70,  25, "", NULL);
    EditNome   = GUI_CreateEdit(&MyApp, 90,  86, 230, 25, "", NULL);
    EditTexto  = GUI_CreateEdit(&MyApp, 330, 86, 230, 25, "", NULL);

    BtnAdd    = GUI_CreateButton(&MyApp, 10,  118, 110, 25, "ADICIONAR",  OnBtnAddClick);
    BtnAlt    = GUI_CreateButton(&MyApp, 130, 118, 100, 25, "ALTERAR",    OnBtnAltClick);
    BtnDel    = GUI_CreateButton(&MyApp, 240, 118, 100, 25, "DELETAR",    OnBtnDelClick);
    BtnPesq   = GUI_CreateButton(&MyApp, 350, 118, 110, 25, "PESQUISAR",  OnBtnPesqClick);
    BtnLimpar = GUI_CreateButton(&MyApp, 470, 118, 90,  25, "LIMPAR",     OnBtnLimparClick);

    BtnVolta = GUI_CreateButton(&MyApp, 10, 148, 100, 25, "<< VOLTA",    OnBtnVoltaClick);
    LabelNav = GUI_CreateLabel(&MyApp, 120, 152, "Registro 0 de 0");
    BtnProx  = GUI_CreateButton(&MyApp, 300, 148, 110, 25, "PROXIMO >>", OnBtnProxClick);

    GridReg = GUI_CreateStringGrid(&MyApp, 10, 180, 620, 180, 4, MAX_REG + 1);
    if (GridReg) {
        GUI_Grid_SetColWidth(GridReg, 0, 50);    /* [#]      */
        GUI_Grid_SetColWidth(GridReg, 1, 80);    /* [CODIGO] */
        GUI_Grid_SetColWidth(GridReg, 2, 240);   /* [NOME]   */
        GUI_Grid_SetColWidth(GridReg, 3, 240);   /* [TEXTO]  */
        GUI_Grid_SetCells(GridReg, 0, 0, "#");
        GUI_Grid_SetCells(GridReg, 1, 0, "CODIGO");
        GUI_Grid_SetCells(GridReg, 2, 0, "NOME");
        GUI_Grid_SetCells(GridReg, 3, 0, "TEXTO");
        GridReg->RowCount = 2;
        gui_set_prop((void*)GridReg->KernelHandle, PROP_STATE, (uint64_t)2);
        GUI_Grid_SetEnabled(GridReg, true);
        GUI_Grid_SetReadOnly(GridReg, true);
    }

    MemoLog = GUI_CreateMemo(&MyApp, 10, 368, 620, 142);
    gui_set_prop(MemoLog, PROP_COLOR, 0x000000);
    GUI_Memo_AddStr(MemoLog, "Agenda LBF v1.3 - conectada ao banco nativo.\n");
    GUI_Memo_AddStr(MemoLog, "ADICIONAR: nome+texto | ALTERAR/DELETAR: codigo\n");
    GUI_Memo_AddStr(MemoLog, "<< VOLTA / PROXIMO >>: navega e carrega nos campos\n\n");

    g_focused_control = (void*)EditNome;
    ultimo_controle_focado = (void*)EditNome;
    gui_set_prop(EditNome, PROP_SET_FOCUS, 1);
    verificar_schema();
    recarregar_regs(0, 0);
    nav_label();
    Flush_Grafico_Janela();

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) { Tratar_Fechamento_Software(); break; }
        bool euTenhoFoco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco;
        bool precisa_redesenhar = false;
        if (primeiro_desenho) { primeiro_desenho = false; precisa_redesenhar = true; }
        if (g_solicitar_redesenho) { g_solicitar_redesenho = false; precisa_redesenhar = true; }
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
                    if      (dentro(BtnAdd,    rel_x, rel_y)) gui_set_prop(BtnAdd,    PROP_STATE, 2);
                    else if (dentro(BtnAlt,    rel_x, rel_y)) gui_set_prop(BtnAlt,    PROP_STATE, 2);
                    else if (dentro(BtnDel,    rel_x, rel_y)) gui_set_prop(BtnDel,    PROP_STATE, 2);
                    else if (dentro(BtnPesq,   rel_x, rel_y)) gui_set_prop(BtnPesq,   PROP_STATE, 2);
                    else if (dentro(BtnLimpar, rel_x, rel_y)) gui_set_prop(BtnLimpar, PROP_STATE, 2);
                    else if (dentro(BtnVolta,  rel_x, rel_y)) gui_set_prop(BtnVolta,  PROP_STATE, 2);
                    else if (dentro(BtnProx,   rel_x, rel_y)) gui_set_prop(BtnProx,   PROP_STATE, 2);
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
                    if (BtnAdd)    gui_set_prop(BtnAdd,    PROP_STATE, 0);
                    if (BtnAlt)    gui_set_prop(BtnAlt,    PROP_STATE, 0);
                    if (BtnDel)    gui_set_prop(BtnDel,    PROP_STATE, 0);
                    if (BtnPesq)   gui_set_prop(BtnPesq,   PROP_STATE, 0);
                    if (BtnLimpar) gui_set_prop(BtnLimpar, PROP_STATE, 0);
                    if (BtnVolta)  gui_set_prop(BtnVolta,  PROP_STATE, 0);
                    if (BtnProx)   gui_set_prop(BtnProx,   PROP_STATE, 0);
                    events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                    precisa_redesenhar = true;
                }
            }
        }
        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(euTenhoFoco ? 16 : 32);
    }
    if (GridReg) { GUI_DestroyStringGrid(GridReg); GridReg = NULL; }
    sys_exit();
    return 0;
}
