/* ============================================================================
STUDIO ALTER v1.2 - manutencao de tabelas existentes (sem perder dados)
v1.2: - botoes UNIFORMES (125x25) e alinhados (gap 10), terminando em x=540
      - combobox TIPO com 70px fechando em x=630 (borda do memo, sem atravessar)
      - faixa 540..630 LIVRE embaixo de TAM/TIPO (nada de botao sob o combo)
      - guarda de aba do combo (clique consumido, nao vaza p/ botões) + Esc
Mantem: ADD CAMPO / RENOMEAR / RESIZE / DROP CAMPO via sys_banco_alter_*
============================================================================ */
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "../system/sysutils.h"
#include "Runtime_sdk/components/TOS_IPC.h"
#include "Runtime_Banco/lib_banco/libbanco.h"

void gui_draw_form(TForm* form); void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* m, const char* s);
extern void GUI_Memo_Clear(TGUIControl* m);

int my_app_slot = -1; TGUIEnvironment MyApp;
const int winWidth = 640, winHeight = 560;

TGUIControl *EditBanco,*EditTabela,*BtnCarregar,*MemoCampos,
            *EditCampo,*EditNovo,*EditTam,*ComboTipo,
            *BtnAdd,*BtnRen,*BtnResize,*BtnDrop,*MemoLog;

/* ---- estado da aba do combo: chave do guarda anti-clique-vazado ---- */
static int  combo_popup_aberto = 0;
static bool click_consumido    = false;

typedef struct { uint8_t d[sizeof(IPC_WINDOW_LIST[0])]; volatile uint8_t a,b; } __attribute__((packed)) Ext;
char Obter_Tecla_Entrada(void){ Ext* e=(Ext*)&IPC_WINDOW_LIST[my_app_slot];
  if(e->b){char k=(char)e->a; e->b=0; return k;} return get_key(); }
void Flush_Grafico_Janela(void){ gui_draw_form((TForm*)MyApp.MainWindow); gui_render_form((TForm*)MyApp.MainWindow); OS_IPC_FlipBuffers(my_app_slot,winWidth,winHeight); }
void Tratar_Fechamento_Software(void){ if(MyApp.MainWindow) gui_set_prop(MyApp.MainWindow,PROP_VISIBLE,0);
  uint32_t* b0=(uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
  uint32_t* b1=(uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
  if(b0) memset(b0,0,winWidth*winHeight*4); if(b1) memset(b1,0,winWidth*winHeight*4);
  IPC_WINDOW_LIST[my_app_slot].is_active=0; sys_sleep(50); }
static void log_line(const char* m){ char b[300]; strcpy(b,m); strcat(b,"\n"); if(MemoLog) GUI_Memo_AddStr(MemoLog,b); sys_debug(m); Flush_Grafico_Janela(); }
static void log_num(const char* l,int v){ char b[96],n[16]; itoa((uint64_t)(uint32_t)v,n,10); strcpy(b,l); strcat(b,n); log_line(b); }
static int dentro(TGUIControl* c,int x,int y){ return c&&x>=c->Left&&x<(c->Left+c->Width)&&y>=c->Top&&y<(c->Top+c->Height); }
static void OnComboNoop(void* s){ (void)s; }
static int combo_sob_click(int x,int y){ return dentro(ComboTipo,x,y) ? 0 : -1; }

static void recarregar(void){
    char* b=GUI_Edit_GetText(EditBanco); char* t=GUI_Edit_GetText(EditTabela);
    GUI_Memo_Clear(MemoCampos);
    static char raw[1024];
    int rc=sys_banco_list_fields(b,t,raw,sizeof(raw));
    if(rc!=BCO_OK){ GUI_Memo_AddStr(MemoCampos,"(tabela nao encontrada)\n"); return; }
    char* p=raw;
    while(p){ char* bar=p; while(*bar&&*bar!='|') bar++;
        if(bar>p){ char f[96]; int l=(int)(bar-p); if(l>95)l=95; memcpy(f,p,l); f[l]=0;
            char ln[128]; strcpy(ln,"  "); strcat(ln,f); strcat(ln,"\n"); GUI_Memo_AddStr(MemoCampos,ln); }
        if(!*bar) break; p=bar+1; }
    Flush_Grafico_Janela();
}
static void tipo_map(const char* tipo,int tam,char* out,int cap){
    char c=tipo[0];
    if(c=='i'||c=='I'){ if(tam==1) strncpy(out,"I1",cap-1); else if(tam==2) strncpy(out,"I2",cap-1); else strncpy(out,"I4",cap-1); }
    else { if(tam<1) tam=15; char n[8]; itoa((uint64_t)(uint32_t)tam,n,10); strncpy(out,"C",cap-1); strncat(out,n,cap-strlen(out)-1); }
    out[cap-1]=0;
}
/* ---------------- eventos ---------------- */
void OnBtnCarregarClick(void* s){ (void)s; recarregar(); log_line("--- esquema carregado ---"); }
void OnBtnAddClick(void* s){ (void)s;
    char* b=GUI_Edit_GetText(EditBanco); char* t=GUI_Edit_GetText(EditTabela);
    char* nm=GUI_Edit_GetText(EditCampo); int tam=atoi(GUI_Edit_GetText(EditTam));
    const char* tp=GUI_ComboBoxEx_GetText(ComboTipo); if(!tp||!tp[0]) tp="int";
    char tm[8]; tipo_map(tp,tam,tm,sizeof(tm));
    char spec[64]; strcpy(spec,nm); strcat(spec,":"); strcat(spec,tm); strcat(spec,":NN");
    int rc=sys_banco_alter_add(b,t,spec);
    log_num("add rc=",rc); if(rc==BCO_OK){ log_line("campo adicionado (default 0/espacos)."); recarregar(); }
}
void OnBtnRenClick(void* s){ (void)s;
    char* b=GUI_Edit_GetText(EditBanco); char* t=GUI_Edit_GetText(EditTabela);
    char* oldn=GUI_Edit_GetText(EditCampo); char* newn=GUI_Edit_GetText(EditNovo);
    int rc=sys_banco_alter_rename(b,t,oldn,newn);
    log_num("rename rc=",rc); if(rc==BCO_OK){ log_line("campo renomeado (dados preservados)."); recarregar(); }
}
void OnBtnResizeClick(void* s){ (void)s;
    char* b=GUI_Edit_GetText(EditBanco); char* t=GUI_Edit_GetText(EditTabela);
    char* nm=GUI_Edit_GetText(EditCampo); int tam=atoi(GUI_Edit_GetText(EditTam));
    const char* tp=GUI_ComboBoxEx_GetText(ComboTipo); if(!tp||!tp[0]) tp="text";
    char tm[8]; tipo_map(tp,tam,tm,sizeof(tm));
    char spec[64]; strcpy(spec,nm); strcat(spec,":"); strcat(spec,tm);
    int rc=sys_banco_alter_resize(b,t,nm,spec);
    log_num("resize rc=",rc); if(rc==BCO_OK){ log_line("largura/tipo ajustado c/ migracao."); recarregar(); }
}
void OnBtnDropClick(void* s){ (void)s;
    char* b=GUI_Edit_GetText(EditBanco); char* t=GUI_Edit_GetText(EditTabela);
    char* nm=GUI_Edit_GetText(EditCampo);
    int rc=sys_banco_alter_drop(b,t,nm);
    log_num("drop rc=",rc); if(rc==BCO_OK){ log_line("campo removido (PK/AI protegidos)."); recarregar(); }
}
/* ---------------- main ---------------- */
int main(int argc,char** argv){ (void)argc;(void)argv;
    static int ux=0,uy=0,mh=0; static bool pd=true,uf=false; void* ufc=NULL;
    graphics_init_app(winWidth,winHeight); wm_init();
    my_app_slot=OS_IPC_RegisterApp("Studio Alter",winWidth,winHeight); if(my_app_slot==-1)return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp,my_app_slot,"Studio Alter v1.2",winWidth,winHeight);
    if(MyApp.MainWindow) gui_set_prop(MyApp.MainWindow,PROP_COLOR,0xC0C0C0);
    /* LINHA 1 (y=38): justificada 10..630 */
    GUI_CreateLabel(&MyApp, 10, 42, "Banco:");
    EditBanco  = GUI_CreateEdit(&MyApp, 60, 38, 200, 25, "CADASTRO", NULL);   /* 60..260  */
    GUI_CreateLabel(&MyApp, 270, 42, "Tabela:");
    EditTabela = GUI_CreateEdit(&MyApp, 325, 38, 200, 25, "CLIENTES", NULL);  /* 325..525 */
    BtnCarregar= GUI_CreateButton(&MyApp, 535, 38, 95, 25, "CARREGAR", OnBtnCarregarClick); /* 535..630 (8c=95) */
    /* memo do esquema (largura total) */
    MemoCampos = GUI_CreateMemo(&MyApp, 10, 70, 620, 110);
    gui_set_prop(MemoCampos, PROP_COLOR, 0x000000);
    /* LINHA 2: labels ACIMA + controles; combo TIPO 70px fecha em 630 */
    GUI_CreateLabel(&MyApp, 10,  192, "CAMPO ATUAL");
    GUI_CreateLabel(&MyApp, 260, 192, "NOVO NOME");
    GUI_CreateLabel(&MyApp, 500, 192, "TAM");
    GUI_CreateLabel(&MyApp, 560, 192, "TIPO");
    EditCampo = GUI_CreateEdit(&MyApp, 10,  208, 240, 25, "", NULL);   /* 10..250  */
    EditNovo  = GUI_CreateEdit(&MyApp, 260, 208, 230, 25, "", NULL);   /* 260..490 */
    EditTam   = GUI_CreateEdit(&MyApp, 500, 208, 50,  25, "20", NULL); /* 500..550 */
    ComboTipo = GUI_CreateComboBoxEx(&MyApp, 560, 208, 70, 24, OnComboNoop); /* 560..630 (nao atravessa) */
    GUI_ComboBoxEx_AddItem(ComboTipo, "text");
    GUI_ComboBoxEx_AddItem(ComboTipo, "int");
    /* LINHA 3 (y=243): 4 botoes IGUAIS (125x25), gap 10, terminam em 540;
       faixa 540..630 (sob TAM/TIPO) fica LIVRE */
    BtnAdd    = GUI_CreateButton(&MyApp, 10,  243, 125, 25, "ADD CAMPO",  OnBtnAddClick);    /* 10..145  */
    BtnRen    = GUI_CreateButton(&MyApp, 145, 243, 125, 25, "RENOMEAR",   OnBtnRenClick);    /* 145..270 */
    BtnResize = GUI_CreateButton(&MyApp, 280, 243, 125, 25, "RESIZE",     OnBtnResizeClick); /* 280..405 */
    BtnDrop   = GUI_CreateButton(&MyApp, 415, 243, 125, 25, "DROP CAMPO", OnBtnDropClick);   /* 415..540 */
    /* memo de log (largura total) */
    MemoLog = GUI_CreateMemo(&MyApp, 10, 278, 620, 272);
    gui_set_prop(MemoLog, PROP_COLOR, 0x000000);
    GUI_Memo_AddStr(MemoLog, "Studio Alter v1.2 - botoes uniformes, combo sem atravessar.\n");
    GUI_Memo_AddStr(MemoLog, "Toda alteracao grava <ARQ>.OLD antes; dados sao migrados.\n\n");
    g_focused_control=(void*)EditBanco; ufc=(void*)EditBanco; gui_set_prop(EditBanco,PROP_SET_FOCUS,1);
    Flush_Grafico_Janela();
    while(1){
        if(IPC_WINDOW_LIST[my_app_slot].is_active==0){Tratar_Fechamento_Software();break;}
        bool foco=(IPC_CONTROL->active_focus_slot==my_app_slot);
        if(MyApp.MainWindow)((TForm*)MyApp.MainWindow)->ActiveFocus=foco;
        bool rd=false; if(pd){pd=false;rd=true;}
        if(foco!=uf){uf=foco;rd=true;}
        if(g_focused_control)ufc=g_focused_control; else if(ufc){g_focused_control=ufc;gui_set_prop((TGUIControl*)ufc,PROP_SET_FOCUS,1);}
        if(foco){
            char k=Obter_Tecla_Entrada();
            if(k){ GUI_ProcessKeyboard(&MyApp,k); rd=true;
                   if(k==27) combo_popup_aberto=0; }   /* Esc fecha a aba sem mouse */
            if(IPC_WINDOW_LIST[my_app_slot].has_click_event==1){
                if(mh==0){
                    int rx=IPC_WINDOW_LIST[my_app_slot].local_click_x, ry=IPC_WINDOW_LIST[my_app_slot].local_click_y;
                    ux=rx; uy=ry; mh=2;
                    /* ===== guarda da aba: quem e dono deste clique? ===== */
                    int combo_alvo = combo_sob_click(rx, ry);
                    if (combo_popup_aberto) {          /* aba aberta: clique e dela */
                        click_consumido = true;
                        combo_popup_aberto = (combo_alvo >= 0);
                    } else {
                        click_consumido = false;
                        if (combo_alvo >= 0) combo_popup_aberto = 1;
                    }
                    if (!click_consumido) {
                        if (combo_alvo < 0) {
                            if      (dentro(BtnCarregar,rx,ry)) gui_set_prop(BtnCarregar,PROP_STATE,2);
                            else if (dentro(BtnAdd,     rx,ry)) gui_set_prop(BtnAdd,     PROP_STATE,2);
                            else if (dentro(BtnRen,     rx,ry)) gui_set_prop(BtnRen,     PROP_STATE,2);
                            else if (dentro(BtnResize,  rx,ry)) gui_set_prop(BtnResize,  PROP_STATE,2);
                            else if (dentro(BtnDrop,    rx,ry)) gui_set_prop(BtnDrop,    PROP_STATE,2);
                        }
                        events_process_mouse(rx, ry, 1, 0);
                    }
                    if(GUI_ProcessMouseClick(&MyApp,rx,ry)){ rd=true; if(g_focused_control) ufc=g_focused_control; }
                }
                IPC_WINDOW_LIST[my_app_slot].has_click_event=0; }
            if(mh>0){ mh--; if(mh==0){
                if(!click_consumido){
                    if(BtnCarregar)gui_set_prop(BtnCarregar,PROP_STATE,0);
                    if(BtnAdd)gui_set_prop(BtnAdd,PROP_STATE,0);
                    if(BtnRen)gui_set_prop(BtnRen,PROP_STATE,0);
                    if(BtnResize)gui_set_prop(BtnResize,PROP_STATE,0);
                    if(BtnDrop)gui_set_prop(BtnDrop,PROP_STATE,0);
                    events_process_mouse(ux,uy,0,0);
                }
                click_consumido=false;
                rd=true; } }
        }
        if(rd) Flush_Grafico_Janela();
        sys_sleep(foco?16:32);
    }
    sys_exit(); return 0;
}
