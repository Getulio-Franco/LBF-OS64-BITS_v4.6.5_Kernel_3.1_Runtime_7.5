#include "libgui.h"
#include "../gui/wm.h"

// Globais do window manager (definidas em wm.c)
extern TForm* g_modal_form;
extern TForm* g_active;

/* ============================================================================
 * 1. CRIAÇÃO DE FORMULÁRIOS SECUNDÁRIOS (bsSingle / bsDialog)
 * ============================================================================ */
TGUIControl* GUI_CreateForm(TGUIEnvironment* app, TGUIControl* parent, const char* title,
                            int x, int y, int width, int height,
                            TBorderStyle style, bool modal) {
    if (!app) return NULL;

    TGUIControl* vform = (TGUIControl*)malloc(sizeof(TGUIControl));
    if (!vform) return NULL;
    memset(vform, 0, sizeof(TGUIControl));

    vform->Type = TYPE_FORM;
    vform->Left = x;   vform->Top = y;
    vform->Width = width; vform->Height = height;
    vform->Modal = modal;
    vform->BorderStyle = (int)style;
    vform->Parent = parent;
    vform->Owner  = (void*)app;

    const char* window_title = (title && title[0] != '\0') ? title : "Form";

    TForm* kernel_win = gui_create_form("SubForm", (char*)window_title, 1);
    if (!kernel_win) { free(vform); return NULL; }

    kernel_win->BorderStyle = style;
    kernel_win->IsModal     = modal;
    kernel_win->ModalResult = 0;
    kernel_win->ParentForm  = (parent && parent->Type == TYPE_FORM && parent->KernelHandle)
                              ? (TForm*)parent->KernelHandle : NULL;

    vform->KernelHandle = (uint64_t)kernel_win;

    // CRÍTICO: o form entra no app->Controls (filtro modal do Passo 5)
    GUI_RegisterControl(app, vform, "Form");

    gui_set_prop((void*)kernel_win, PROP_LEFT,   (uint64_t)vform->Left);
    gui_set_prop((void*)kernel_win, PROP_TOP,    (uint64_t)vform->Top);
    gui_set_prop((void*)kernel_win, PROP_WIDTH,  (uint64_t)vform->Width);
    gui_set_prop((void*)kernel_win, PROP_HEIGHT, (uint64_t)vform->Height);

    kernel_win->Win.Draggable = (style != bsDialog);

    wm_add_window(kernel_win);
    return vform;
}

/* ============================================================================
 * 2. REPARENT — o coração da técnica nova
 *    Move um controle já criado (fábrica normal) para DENTRO de um form:
 *    - Kernel: tira do MainWindow e põe na lista de filhos do form (desenho)
 *    - SDK:    Parent = form (coords absolutas + filtro modal)
 * ============================================================================ */
void GUI_ReparentToForm(TGUIControl* ctrl, TGUIControl* form) {
    if (!ctrl || !form || form->Type != TYPE_FORM) return;
    TControl* kc = (TControl*)ctrl->KernelHandle;
    TForm*  kf = (TForm*)form->KernelHandle;
    if (!kc || !kf) return;
    if (kc->Parent) gui_remove_from_parent(kc->Parent, kc);
    gui_add_to_parent((TWinControl*)kf, kc);
    ctrl->Parent = form;
}

/* ============================================================================
 * 3. OS 9 COMPONENTES ONFORM (4 linhas cada — zero código duplicado)
 * ============================================================================ */
TGUIControl* GUI_CreateButtonOnForm(TGUIEnvironment* app, TGUIControl* form,
                                    int x, int y, int w, int h,
                                    const char* caption, TEventClick onClick) {
    TGUIControl* c = GUI_CreateButton(app, x, y, w, h, caption, onClick);
    if (c) GUI_ReparentToForm(c, form);
    return c;
}

TGUIControl* GUI_CreateLabelOnForm(TGUIEnvironment* app, TGUIControl* form,
                                   int x, int y, const char* caption) {
    TGUIControl* c = GUI_CreateLabel(app, x, y, caption);
    if (c) GUI_ReparentToForm(c, form);
    return c;
}

TGUIControl* GUI_CreateEditOnForm(TGUIEnvironment* app, TGUIControl* form,
                                  int x, int y, int w, int h,
                                  const char* initialText, TEventChange onChange) {
    TGUIControl* c = GUI_CreateEdit(app, x, y, w, h, initialText, onChange);
    if (c) GUI_ReparentToForm(c, form);
    return c;
}

TGUIControl* GUI_CreateCheckBoxOnForm(TGUIEnvironment* app, TGUIControl* form,
                                      int x, int y, const char* caption, TEventChange onChange) {
    TGUIControl* c = GUI_CreateCheckBox(app, x, y, caption, onChange);
    if (c) GUI_ReparentToForm(c, form);
    return c;
}

TGUIControl* GUI_CreateRadioButtonOnForm(TGUIEnvironment* app, TGUIControl* form,
                                         int x, int y, const char* caption, TEventChange onChange) {
    TGUIControl* c = GUI_CreateRadioButton(app, x, y, caption, onChange);
    if (c) GUI_ReparentToForm(c, form);
    return c;
}

TGUIControl* GUI_CreateComboBoxExOnForm(TGUIEnvironment* app, TGUIControl* form,
                                        int x, int y, int w, int h, TEventChange onChange) {
    TGUIControl* c = GUI_CreateComboBoxEx(app, x, y, w, h, onChange);
    if (c) GUI_ReparentToForm(c, form);
    return c;
}

TGUIControl* GUI_CreateMemoOnForm(TGUIEnvironment* app, TGUIControl* form,
                                  int x, int y, int w, int h) {
    TGUIControl* c = GUI_CreateMemo(app, x, y, w, h);
    if (c) GUI_ReparentToForm(c, form);
    return c;
}

TGUIControl* GUI_CreateImageOnForm(TGUIEnvironment* app, TGUIControl* form,
                                   int x, int y, int w, int h, const char* path) {
    TGUIControl* c = GUI_CreateImage(app, x, y, w, h, path);
    if (c) GUI_ReparentToForm(c, form);
    return c;
}

TGUIControl* GUI_CreateStringGridOnForm(TGUIEnvironment* app, TGUIControl* form,
                                        int x, int y, int w, int h, int cols, int rows) {
    TGUIControl* c = GUI_CreateStringGrid(app, x, y, w, h, cols, rows);
    if (c) GUI_ReparentToForm(c, form);
    return c;
}

/* ============================================================================
 * 4. CICLO DE VIDA (com hotfix 6.2 = posição | hotfix 6.3 = estado limpo)
 * ============================================================================ */
void GUI_Form_Show(TGUIControl* form) {
    if (!form || form->Type != TYPE_FORM) return;
    TForm* kf = (TForm*)form->KernelHandle;
    if (!kf) return;
    kf->Win.Control.Visible = true;
    gui_set_prop((void*)kf, PROP_VISIBLE, 1);
}

/* ============================================================================
 * HOTFIX 6.5: RESET DE ESTADO INTERATIVO DOS CONTROLES DO FORM
 * CheckBox/RadioButton voltam desmarcados e Memo volta limpo —
 * exatamente como no momento da criação. (Mesma filosofia do State=0
 * dos botões: fechar esconde, abrir restaura o estado de fábrica.)
 * ============================================================================ */
void GUI_Form_ResetControls(TGUIControl* form) {
    if (!form || !form->Owner) return;
    TGUIEnvironment* app = (TGUIEnvironment*)form->Owner;

    for (int i = 0; i < app->ControlCount; i++) {
        TGUIControl* c = app->Controls[i];
        if (!c) continue;

        // Só reseta controles que pertencem a ESTE form (cadeia Parent)
        bool dentro = false;
        for (TGUIControl* p = c->Parent; p; p = p->Parent)
            if (p == form) { dentro = true; break; }
        if (!dentro) continue;

        switch (c->Type) {
            case TYPE_CHECKBOX:
                c->Checked = false;
                if (c->KernelHandle) ((TCheckBox*)c->KernelHandle)->Checked = false;
                break;

            case TYPE_RADIOBUTTON:
                c->Checked = false;
                if (c->KernelHandle) ((TRadioButton*)c->KernelHandle)->Checked = false;
                break;

            case TYPE_MEMO:
                GUI_Memo_Clear(c);   // limpa buffer SDK + Kernel + scroll
                break;

            case TYPE_EDIT:
                GUI_Edit_SetText(c, "");
                break;
          /*  case TYPE_COMBOBOX_EX:
                GUI_ComboBoxEx_SetIndex(c, 0);
                break;
            */
            default: break;
        }
    }
}

void GUI_Form_ShowModal(TGUIControl* form) {
    if (!form || form->Type != TYPE_FORM) return;
    TForm* kernel_form = (TForm*)form->KernelHandle;
    if (!kernel_form) return;

    // <<< HOTFIX 6.5: estado interativo limpo a cada abertura
    GUI_Form_ResetControls(form);

    // 6.3: abre sem botão "grudado" pressionado/hover
    for (int i = 0; i < kernel_form->ControlCount; i++) {
        TControl* c = kernel_form->Controls[i];
        if (c) c->State = 0;
    }

    g_modal_form = kernel_form;
    // 6.2: sempre abre na posição definida pela SDK
    gui_set_prop((void*)kernel_form, PROP_LEFT, (uint64_t)form->Left);
    gui_set_prop((void*)kernel_form, PROP_TOP,  (uint64_t)form->Top);
    kernel_form->Win.Control.Visible = true;
    kernel_form->ModalResult = 0;
    gui_set_prop((void*)kernel_form, PROP_VISIBLE, 1);
}

void GUI_Form_Close(TGUIControl* form, int modal_result) {
    if (!form || form->Type != TYPE_FORM) return;
    TForm* kernel_form = (TForm*)form->KernelHandle;
    if (!kernel_form) return;

    // 6.3: zera estado pressionado ANTES de esconder (release chega tarde)
    for (int i = 0; i < kernel_form->ControlCount; i++) {
        TControl* c = kernel_form->Controls[i];
        if (c) c->State = 0;
    }

    if (g_modal_form == kernel_form) g_modal_form = NULL;
    kernel_form->ModalResult = modal_result;
    kernel_form->Win.Control.Visible = false;
    gui_set_prop((void*)kernel_form, PROP_VISIBLE, 0);
    if (kernel_form->ParentForm) g_active = kernel_form->ParentForm;
}

int GUI_Form_GetModalResult(TGUIControl* form) {
    if (!form || form->Type != TYPE_FORM) return 0;
    TForm* kf = (TForm*)form->KernelHandle;
    if (!kf) return 0;
    return kf->ModalResult;
}

void GUI_DestroyForm(TGUIControl* form) {
    if (!form) return;
    TForm* kf = (TForm*)form->KernelHandle;
    if (kf && g_modal_form == kf) g_modal_form = NULL;
    if (kf) wm_remove_window(kf);
    free(form);
}
