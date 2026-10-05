#include "libgui.h"

/* ============================================================================
 * COMPONENTE TCOMBOBOXEX (v1 — dropdown/popup)
 * Coexiste com o TComboBox rotativo; o programador escolhe qual usar.
 * ============================================================================ */

TGUIControl* GUI_CreateComboBoxEx(TGUIEnvironment* app, int x, int y, int w, int h, TEventChange onChange) {
    if (!app) return NULL;
    TGUIControl* combo = (TGUIControl*)malloc(sizeof(TGUIControl));
    if (!combo) return NULL;
    memset(combo, 0, sizeof(TGUIControl));

    combo->Type = TYPE_COMBOBOX_EX;
    combo->Left = x;
    combo->Top = y;
    combo->Width = w;
    combo->Height = h;
    combo->ItemCount = 0;
    combo->ItemIndex = 0;
    combo->DroppedDown = false;
    combo->OnChange = onChange;

    GUI_RegisterControl(app, combo, "ComboBoxEx");
    combo->KernelHandle = (uint64_t)gui_create_combobox_ex((TWinControl*)app->MainWindow, combo->Name);
    if (combo->KernelHandle == 0) { free(combo); return NULL; }

    gui_set_prop((void*)combo->KernelHandle, PROP_LEFT,   (uint64_t)combo->Left);
    gui_set_prop((void*)combo->KernelHandle, PROP_TOP,    (uint64_t)combo->Top);
    gui_set_prop((void*)combo->KernelHandle, PROP_WIDTH,  (uint64_t)combo->Width);
    gui_set_prop((void*)combo->KernelHandle, PROP_HEIGHT, (uint64_t)combo->Height);
    return combo;
}

void GUI_ComboBoxEx_AddItem(TGUIControl* combo, const char* texto) {
    if (!combo || combo->Type != TYPE_COMBOBOX_EX || !texto || combo->ItemCount >= 8) return;
    strncpy(combo->Items[combo->ItemCount], texto, 15);
    combo->Items[combo->ItemCount][15] = '\0';
    combo->ItemCount++;
    if (combo->ItemCount == 1) combo->ItemIndex = 0;
    if (combo->KernelHandle)
        gui_set_prop((void*)combo->KernelHandle, PROP_ADD_ITEM, (uintptr_t)texto);
}

void GUI_ComboBoxEx_Toggle(TGUIControl* combo) {
    if (!combo || combo->Type != TYPE_COMBOBOX_EX || combo->ItemCount == 0) return;
    combo->DroppedDown = !combo->DroppedDown;
    if (combo->KernelHandle)
        gui_set_prop((void*)combo->KernelHandle, PROP_DROPDOWN, combo->DroppedDown ? 1 : 0);
}

void GUI_ComboBoxEx_Close(TGUIControl* combo) {
    if (!combo || combo->Type != TYPE_COMBOBOX_EX || !combo->DroppedDown) return;
    combo->DroppedDown = false;
    if (combo->KernelHandle)
        gui_set_prop((void*)combo->KernelHandle, PROP_DROPDOWN, 0);
}

void GUI_ComboBoxEx_SetIndex(TGUIControl* combo, int index) {
    if (!combo || combo->Type != TYPE_COMBOBOX_EX) return;
    if (index < 0 || index >= combo->ItemCount) return;
    bool mudou = (combo->ItemIndex != index);
    combo->ItemIndex = index;
    combo->DroppedDown = false;
    if (combo->KernelHandle) {
        gui_set_prop((void*)combo->KernelHandle, PROP_ITEM_INDEX, (uint64_t)index);
        gui_set_prop((void*)combo->KernelHandle, PROP_DROPDOWN, 0);
    }
    if (mudou && combo->OnChange) combo->OnChange(combo);
}

char* GUI_ComboBoxEx_GetText(TGUIControl* combo) {
    if (!combo || combo->ItemCount == 0 || combo->ItemIndex < 0) return "";
    return combo->Items[combo->ItemIndex];
}

int GUI_ComboBoxEx_GetIndex(TGUIControl* combo) {
    return combo ? combo->ItemIndex : -1;
}
