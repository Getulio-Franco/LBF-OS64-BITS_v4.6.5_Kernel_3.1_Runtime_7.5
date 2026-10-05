/* ============================================================================
GERENCIADOR DE ARQUIVOS v0.1.1 - LBF-VESA (Nativo Ring 3 - Sem libc)
FUSÃO: segurança de strings + UX do v0.1.0  +  gate de foco do v0.0.9.
MELHORIAS v0.1.1:
 (1) FOCO: teclado só com janela focada; clique é consumido sempre (evita
     clique pendurado) mas só age focado. Obter_Tecla_Entrada tem get_key().
 (2) copy_edit(): null-terminação garantida após strncpy de texto de Edit.
 (3) edit_text(): getter seguro (nunca NULL).
 (4) EXEC: fallback sys_exec só se o 1º falhar; limpa campo só em sucesso.
 (5) Guarda anti auto-cópia / auto-renome (origem == destino).
 (6) Seleção limpa a cada reload (g_selected_file + PROP_ITEM_INDEX=-1).
Mantém v0.1.0: helpers bounded (build_msg_*, path_* c/ out_size), Delete por
seleção, prefixos cp/mv/copy/ren, destino c/ '/' mantém nome origem,
auto-fill ExecEdit p/ .ELF, filtro, status, Reset_All_Button_States.
============================================================================ */
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

/* Protótipos antecipados */
void Carregar_Diretorio(const char* path);
static const char* get_filename_from_path(const char* path);
static void path_canon(char* out, size_t out_size, const char* in);
static void path_join(char* out, size_t out_size, const char* base, const char* name);
static void path_parent(char* out, size_t out_size, const char* cur);
static void resolve_full_path(char* out, size_t out_size, const char* cur_dir, const char* input_path);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth  = 560;
const int winHeight = 560;

TGUIControl* PathEdit    = NULL;
TGUIControl* GoButton    = NULL;
TGUIControl* BtnRefresh  = NULL;
TGUIControl* FileList    = NULL;
TGUIControl* ActionEdit  = NULL;
TGUIControl* BtnRename   = NULL;
TGUIControl* BtnDel      = NULL;
TGUIControl* BtnCopy     = NULL;
TGUIControl* BtnNewDir   = NULL;
TGUIControl* ExecEdit    = NULL;
TGUIControl* BtnExecutar = NULL;
TGUIControl* LabelStatus = NULL;
TGUIControl* DebugMemo   = NULL;
TGUIControl* FilterEdit  = NULL;

static char g_selected_file[256] = {0};

/* ---------------- Helpers de formatação bounded (v0.1.0) ---------------- */
static void build_msg_str(char* out, size_t max_len, const char* prefix, const char* str_val) {
    if (!out || max_len == 0) return;
    out[0] = '\0';
    strncat(out, prefix ? prefix : "", max_len - 1);
    if (str_val) strncat(out, str_val, max_len - strlen(out) - 1);
}
static void build_msg_int(char* out, size_t max_len, const char* prefix, int num_val) {
    if (!out || max_len == 0) return;
    char num_buf[16];
    int_to_string(num_val, num_buf);
    out[0] = '\0';
    strncat(out, prefix ? prefix : "", max_len - 1);
    strncat(out, num_buf, max_len - strlen(out) - 1);
}
static void build_status_str(char* out, size_t max_len, int itens, const char* tamanho, const char* path) {
    if (!out || max_len == 0) return;
    char num_buf[16];
    int_to_string(itens, num_buf);
    out[0] = '\0';
    strncat(out, "itens: ", max_len - 1);
    strncat(out, num_buf, max_len - strlen(out) - 1);
    strncat(out, " | tamanho: ", max_len - strlen(out) - 1);
    strncat(out, tamanho ? tamanho : "0 B", max_len - strlen(out) - 1);
    strncat(out, " | caminho: ", max_len - strlen(out) - 1);
    strncat(out, path ? path : "", max_len - strlen(out) - 1);
}
static bool contains_substring_ci(const char* str, const char* sub) {
    if (!sub || sub[0] == '\0') return true;
    if (!str) return false;
    int len_str = (int)strlen(str);
    int len_sub = (int)strlen(sub);
    if (len_sub > len_str) return false;
    for (int i = 0; i <= len_str - len_sub; i++) {
        bool match = true;
        for (int j = 0; j < len_sub; j++) {
            char c1 = str[i + j];
            char c2 = sub[j];
            if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
            if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
            if (c1 != c2) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}
static void format_bytes(char* out, size_t max_len, uint32_t bytes) {
    if (!out || max_len == 0) return;
    char num[16];
    const char* unit = " B";
    if (bytes >= 1024 * 1024) { itoa(bytes / (1024 * 1024), num, 10); unit = " MB"; }
    else if (bytes >= 1024)   { itoa(bytes / 1024, num, 10);          unit = " KB"; }
    else                      { itoa(bytes, num, 10); }
    strncpy(out, num, max_len - 1);
    out[max_len - 1] = '\0';
    strncat(out, unit, max_len - strlen(out) - 1);
}
void Log_Debug(const char* msg) {
    if (DebugMemo && msg) {
        GUI_Memo_AddStr(DebugMemo, msg);
        GUI_Memo_AddStr(DebugMemo, "\n");
    }
}

/* ---------------- v0.1.1: getters seguros de Edit ---------------- */
static const char* edit_text(TGUIControl* e) {          /* nunca NULL */
    const char* t = e ? GUI_Edit_GetText(e) : NULL;
    return t ? t : "";
}
static void copy_edit(char* out, size_t n, TGUIControl* e) {  /* sempre null-terminated */
    if (!out || n == 0) return;
    strncpy(out, edit_text(e), n - 1);
    out[n - 1] = '\0';
}

/* ---------------- Helpers de string / caminho ---------------- */
static char* trim_str(char* str) {
    if (!str) return (char*)"";
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    if (*str == 0) return str;
    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) { *end = '\0'; end--; }
    return str;
}
static const char* strip_drive(const char* path) {
    if (!path) return "";
    if (path[0] && path[1] == ':') path += 2;
    while (*path == '/') path++;
    return path;
}
static const char* get_filename_from_path(const char* path) {
    if (!path) return "";
    const char* last_slash = NULL;
    for (const char* p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') last_slash = p;
    }
    return last_slash ? (last_slash + 1) : path;
}
static void path_join(char* out, size_t out_size, const char* base, const char* name) {
    if (!out || out_size == 0) return;
    strncpy(out, base ? base : "", out_size - 1);
    out[out_size - 1] = '\0';
    size_t L = strlen(out);
    if (L > 0 && out[L - 1] != '/' && L < out_size - 1) strcat(out, "/");
    if (name) strncat(out, name, out_size - strlen(out) - 1);
}
static void path_parent(char* out, size_t out_size, const char* cur) {
    if (!out || out_size == 0) return;
    strncpy(out, cur ? cur : "C:/", out_size - 1);
    out[out_size - 1] = '\0';
    int n = (int)strlen(out);
    while (n > 0 && out[n - 1] == '/') n--;
    while (n > 0 && out[n - 1] != '/') n--;
    if (n <= 2) strncpy(out, "C:/", out_size);
    else out[n] = '\0';
}
static void path_canon(char* out, size_t out_size, const char* in) {
    if (!out || out_size == 0) return;
    if (!in || !in[0]) { strncpy(out, "C:/", out_size); out[out_size-1]='\0'; return; }
    if (in[1] == ':') {
        strncpy(out, in, out_size - 1);
        out[out_size - 1] = '\0';
        out[0] = 'C';
    } else {
        strncpy(out, "C:/", out_size - 1);
        out[out_size - 1] = '\0';
        strncat(out, in, out_size - strlen(out) - 1);
    }
    size_t L = strlen(out);
    while (L > 3 && out[L - 1] == '/') { out[L - 1] = '\0'; L--; }
}
static void resolve_full_path(char* out, size_t out_size, const char* cur_dir, const char* input_path) {
    if (!out || out_size == 0) return;
    if (!input_path || !input_path[0]) { path_canon(out, out_size, cur_dir); return; }
    if ((input_path[0] && input_path[1] == ':') || input_path[0] == '/') { path_canon(out, out_size, input_path); return; }
    char tmp[256];
    path_join(tmp, sizeof(tmp), cur_dir, input_path);
    path_canon(out, out_size, tmp);
}

/* ---------------- Sistema / Gráficos ---------------- */
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
static void Reset_All_Button_States(void) {
    TGUIControl* btns[] = {GoButton, BtnRefresh, BtnRename, BtnDel, BtnCopy, BtnNewDir, BtnExecutar};
    for (size_t i = 0; i < sizeof(btns)/sizeof(btns[0]); i++) {
        if (btns[i]) gui_set_prop(btns[i], PROP_STATE, 0);
    }
}

/* ---------------- Carregar diretório ---------------- */
void Carregar_Diretorio(const char* path) {
    if (!FileList) return;
    GUI_ListView_Clear(FileList);
    g_selected_file[0] = '\0';                       /* v0.1.1: limpa seleção */
    gui_set_prop(FileList, PROP_ITEM_INDEX, -1);     /* v0.1.1: reseta índice */
    if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");
    const char* p = path;
    if (p[0] && p[1] == ':') p += 2;
    while (*p == '/') p++;
    while (*p) {
        char comp[64]; int i = 0;
        while (*p && *p != '/' && i < 63) comp[i++] = *p++;
        comp[i] = 0;
        while (*p == '/') p++;
        if (!comp[0] || strcmp(comp, ".") == 0) continue;
        if (sys_fat_chdir(comp) != 0) break;
    }
    if (strcmp(path, "C:/") != 0 && strcmp(path, "0:/") != 0 && strcmp(path, "/") != 0) {
        GUI_ListView_AddItem(FileList, "..", 0, 0x10);
    }
    char filter_buf[64] = {0};
    if (FilterEdit) {
        strncpy(filter_buf, edit_text(FilterEdit), 63);
        trim_str(filter_buf);
    }
    char nome[64]; file_info_t md;
    int idx = 0, itens = 0;
    uint32_t tamanho_total_pasta = 0;
    while (sys_fat_readdir(idx, nome, &md) == 1) {
        if (strcmp(nome, ".") != 0 && strcmp(nome, "..") != 0) {
            if (filter_buf[0] == '\0' || contains_substring_ci(nome, filter_buf)) {
                GUI_ListView_AddItem(FileList, nome, md.size, md.attributes);
                itens++;
                if (!(md.attributes & 0x10)) tamanho_total_pasta += md.size;
            }
        }
        idx++;
        if (idx > 500) break;
    }
    sys_fat_chdir("/");
    char str_tamanho[32];
    format_bytes(str_tamanho, sizeof(str_tamanho), tamanho_total_pasta);
    char st[256];
    build_status_str(st, sizeof(st), itens, str_tamanho, path);
    if (LabelStatus) GUI_Edit_SetText(LabelStatus, st);
}

/* ---------------- Callbacks ---------------- */
void OnBtnGoClick(void* sender) {
    (void)sender;
    char canon[256];
    path_canon(canon, sizeof(canon), edit_text(PathEdit));
    GUI_Edit_SetText(PathEdit, canon);
    Carregar_Diretorio(canon);
}
void OnBtnRefreshClick(void* sender) { (void)sender; Carregar_Diretorio(edit_text(PathEdit)); }

void OnBtnNewDirClick(void* sender) {
    (void)sender;
    char temp_buffer[256];
    copy_edit(temp_buffer, sizeof(temp_buffer), ActionEdit);
    char* clean_input = trim_str(temp_buffer);
    if (clean_input[0] == '\0') return;
    Log_Debug("--- NOVA PASTA ---");
    char cur_dir[256], full_path[256];
    copy_edit(cur_dir, sizeof(cur_dir), PathEdit);
    resolve_full_path(full_path, sizeof(full_path), cur_dir, clean_input);
    const char* clean_target = strip_drive(full_path);
    char msg_buf[256];
    build_msg_str(msg_buf, sizeof(msg_buf), "CRIAR PASTA: ", clean_target);
    Log_Debug(msg_buf);
    if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");
    int res = sys_fat_mkdir(clean_target);
    build_msg_int(msg_buf, sizeof(msg_buf), "sys_fat_mkdir retorno: ", res);
    Log_Debug(msg_buf);
    Carregar_Diretorio(cur_dir);
    GUI_Edit_SetText(ActionEdit, "");
}

void OnBtnDelClick(void* sender) {
    (void)sender;
    char target[256] = {0};
    const char* act = edit_text(ActionEdit);
    if (act[0] != '\0') strncpy(target, act, 255);
    else if (g_selected_file[0] != '\0') strncpy(target, g_selected_file, 255);
    else { Log_Debug("Erro: Selecione ou digite o arquivo a deletar!"); return; }
    target[255] = '\0';
    char* clean_input = trim_str(target);
    if (clean_input[0] == '\0') return;
    Log_Debug("--- DELETAR ARQUIVO ---");
    char cur_dir[256], full_path[256];
    copy_edit(cur_dir, sizeof(cur_dir), PathEdit);
    resolve_full_path(full_path, sizeof(full_path), cur_dir, clean_input);
    const char* clean_target = strip_drive(full_path);
    char msg_buf[256];
    build_msg_str(msg_buf, sizeof(msg_buf), "DELETAR: ", clean_target);
    Log_Debug(msg_buf);
    if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");
    int res = sys_fat_rm(clean_target);
    build_msg_int(msg_buf, sizeof(msg_buf), "sys_fat_rm retorno: ", res);
    Log_Debug(msg_buf);
    Carregar_Diretorio(cur_dir);
    GUI_Edit_SetText(ActionEdit, "");
    g_selected_file[0] = '\0';
}

void Executar_Binario_Seguro(void) {
    const char* caminho_elf = edit_text(ExecEdit);
    if (caminho_elf[0] == '\0') return;
    char full_exec[256];
    resolve_full_path(full_exec, sizeof(full_exec), edit_text(PathEdit), caminho_elf);
    const char* clean_exec = strip_drive(full_exec);
    Log_Debug("--- EXECUTAR BINARIO ---");
    char msg_buf[256];
    build_msg_str(msg_buf, sizeof(msg_buf), "EXEC: ", clean_exec);
    Log_Debug(msg_buf);
    if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");
    char dir_part[256];
    strncpy(dir_part, clean_exec, 255); dir_part[255] = '\0';
    char* last_slash = NULL;
    for (char* p = dir_part; *p != '\0'; p++) if (*p == '/' || *p == '\\') last_slash = p;
    const char* file_exec = get_filename_from_path(clean_exec);
    if (last_slash) {
        *last_slash = '\0';
        const char* p = dir_part;
        while (*p) {
            char comp[64]; int i = 0;
            while (*p && *p != '/' && *p != '\\' && i < 63) comp[i++] = *p++;
            comp[i] = 0;
            while (*p == '/' || *p == '\\') p++;
            if (!comp[0] || strcmp(comp, ".") == 0) continue;
            sys_fat_chdir(comp);
        }
    }
    int res = sys_exec(file_exec);
    if (res != 0) res = sys_exec(clean_exec);      /* v0.1.1: fallback só se falhou */
    build_msg_int(msg_buf, sizeof(msg_buf), "sys_exec retorno: ", res);
    Log_Debug(msg_buf);
    if (res == 0) GUI_Edit_SetText(ExecEdit, "");  /* v0.1.1: mantém p/ retry em falha */
}
void OnBtnExecutarClick(void* sender) { (void)sender; Executar_Binario_Seguro(); }

void Tratar_Modificacao_Arquivo(bool is_rename) {
    char temp_buffer[256];
    copy_edit(temp_buffer, sizeof(temp_buffer), ActionEdit);
    char* clean_input = trim_str(temp_buffer);
    if (clean_input[0] == '\0') { Log_Debug("Erro: Informe o destino ou os parâmetros!"); return; }
    Log_Debug(is_rename ? "--- INICIO RENOMEAR ---" : "--- INICIO COPIA ---");
    if (strncmp(clean_input, "cp ", 3) == 0) clean_input += 3;
    else if (strncmp(clean_input, "mv ", 3) == 0) clean_input += 3;
    else if (strncmp(clean_input, "copy ", 5) == 0) clean_input += 5;
    else if (strncmp(clean_input, "ren ", 4) == 0) clean_input += 4;
    clean_input = trim_str(clean_input);

    char arg1[256] = {0}, arg2[256] = {0};
    char* space_ptr = strchr(clean_input, ' ');
    if (space_ptr) {
        *space_ptr = '\0';
        strncpy(arg1, trim_str(clean_input), 255);
        strncpy(arg2, trim_str(space_ptr + 1), 255);
    } else {
        if (g_selected_file[0] != '\0') {
            strncpy(arg1, g_selected_file, 255);
            strncpy(arg2, clean_input, 255);
        } else {
            Log_Debug("Erro: Digite 'origem destino' ou selecione um arquivo!");
            return;
        }
    }
    if (arg1[0] == '\0' || arg2[0] == '\0') { Log_Debug("Erro: Origem ou destino invalidos!"); return; }

    char cur_dir[256], full_arg1[256], full_arg2[256];
    copy_edit(cur_dir, sizeof(cur_dir), PathEdit);
    resolve_full_path(full_arg1, sizeof(full_arg1), cur_dir, arg1);
    size_t arg2_len = strlen(arg2);
    if (arg2_len > 0 && (arg2[arg2_len - 1] == '/' || arg2[arg2_len - 1] == '\\')) {
        char base_dest[256];
        resolve_full_path(base_dest, sizeof(base_dest), cur_dir, arg2);
        path_join(full_arg2, sizeof(full_arg2), base_dest, get_filename_from_path(arg1));
    } else {
        resolve_full_path(full_arg2, sizeof(full_arg2), cur_dir, arg2);
    }
    const char* clean_orig = strip_drive(full_arg1);
    const char* clean_dest = strip_drive(full_arg2);

    /* v0.1.1: guarda anti auto-cópia / auto-renome */
    if (strcmp(clean_orig, clean_dest) == 0) {
        Log_Debug("Erro: origem e destino identicos!");
        GUI_Edit_SetText(ActionEdit, "");
        return;
    }

    char msg_buf[256];
    build_msg_str(msg_buf, sizeof(msg_buf), "ORIGEM: ", clean_orig);  Log_Debug(msg_buf);
    build_msg_str(msg_buf, sizeof(msg_buf), "DESTINO: ", clean_dest); Log_Debug(msg_buf);
    int res = 0;
    if (is_rename) {
        if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");
        char dir_part[256];
        strncpy(dir_part, clean_orig, 255); dir_part[255] = '\0';
        char* last_slash = NULL;
        for (char* p = dir_part; *p != '\0'; p++) if (*p == '/' || *p == '\\') last_slash = p;
        const char* file_orig = get_filename_from_path(clean_orig);
        const char* file_dest = get_filename_from_path(clean_dest);
        if (last_slash) {
            *last_slash = '\0';
            const char* p = dir_part;
            while (*p) {
                char comp[64]; int i = 0;
                while (*p && *p != '/' && *p != '\\' && i < 63) comp[i++] = *p++;
                comp[i] = 0;
                while (*p == '/' || *p == '\\') p++;
                if (!comp[0] || strcmp(comp, ".") == 0) continue;
                sys_fat_chdir(comp);
            }
        }
        res = sys_fat_rename(file_orig, file_dest);
        if (res != 0) {
            if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");
            res = sys_fat_rename(clean_orig, clean_dest);
        }
        build_msg_int(msg_buf, sizeof(msg_buf), "sys_fat_rename retorno: ", res);
    } else {
        if (sys_fat_chdir("/") != 0) sys_fat_chdir("0:/");
        res = sys_fat_copy(clean_orig, clean_dest);
        build_msg_int(msg_buf, sizeof(msg_buf), "sys_fat_copy retorno: ", res);
    }
    Log_Debug(msg_buf);
    Carregar_Diretorio(cur_dir);
    GUI_Edit_SetText(ActionEdit, "");
}
void OnBtnRenameClick(void* sender) { (void)sender; Tratar_Modificacao_Arquivo(true); }
void OnBtnCopyClick(void* sender)   { (void)sender; Tratar_Modificacao_Arquivo(false); }

void OnFileListChange(void* sender) {
    (void)sender;
    int idx = gui_get_prop(FileList, PROP_ITEM_INDEX);
    if (idx == -1) return;
    char nome_sel[64];
    uint32_t tam_arquivo = 0;
    uint8_t attributes = 0;
    GUI_ListView_GetItem(FileList, idx, nome_sel, &tam_arquivo, &attributes);
    if (attributes & 0x10) {
        char novo[256];
        if (strcmp(nome_sel, "..") == 0) path_parent(novo, sizeof(novo), edit_text(PathEdit));
        else                             path_join(novo, sizeof(novo), edit_text(PathEdit), nome_sel);
        GUI_Edit_SetText(PathEdit, novo);
        Carregar_Diretorio(novo);
    } else {
        strncpy(g_selected_file, nome_sel, 255);
        g_selected_file[255] = '\0';
        GUI_Edit_SetText(ActionEdit, nome_sel);
        if (contains_substring_ci(nome_sel, ".ELF")) GUI_Edit_SetText(ExecEdit, nome_sel);
    }
}

/* ---------------- Main ---------------- */
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("Gerenciador de Arquivos", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Gerenciador de Arquivos v0.1.1", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    GUI_CreateLabel(&MyApp, 10, 42, "Caminho:");
    PathEdit   = GUI_CreateEdit(&MyApp, 70, 38, 180, 25, "C:/", NULL);
    GUI_CreateLabel(&MyApp, 258, 42, "Filtro:");
    FilterEdit = GUI_CreateEdit(&MyApp, 308, 38, 80, 25, "", NULL);
    GoButton   = GUI_CreateButton(&MyApp, 396, 38, 76, 25, "Navegar", OnBtnGoClick);
    BtnRefresh = GUI_CreateButton(&MyApp, 478, 38, 72, 25, "Reler",   OnBtnRefreshClick);
    FileList = GUI_CreateListView(&MyApp, 10, 75, 540, 230, OnFileListChange);
    gui_set_prop(FileList, PROP_ITEM_INDEX, -1);
    GUI_CreateLabel(&MyApp, 10, 317, "Acao:");
    ActionEdit = GUI_CreateEdit(&MyApp, 60, 313, 490, 25, "", NULL);
    BtnRename  = GUI_CreateButton(&MyApp, 10,  345, 125, 25, "Renomear",   OnBtnRenameClick);
    BtnDel     = GUI_CreateButton(&MyApp, 145, 345, 125, 25, "Deletar",    OnBtnDelClick);
    BtnCopy    = GUI_CreateButton(&MyApp, 280, 345, 125, 25, "Copiar",     OnBtnCopyClick);
    BtnNewDir  = GUI_CreateButton(&MyApp, 415, 345, 135, 25, "Nova Pasta", OnBtnNewDirClick);
    GUI_CreateLabel(&MyApp, 10, 380, "Executar:");
    ExecEdit    = GUI_CreateEdit(&MyApp, 85, 376, 360, 25, "", NULL);
    BtnExecutar = GUI_CreateButton(&MyApp, 452, 376, 98, 25, "Executar", OnBtnExecutarClick);
    LabelStatus = GUI_CreateLabel(&MyApp, 10, 410, "itens: 0 | caminho: C:/");
    GUI_CreateLabel(&MyApp, 10, 430, "Debug Log:");
    DebugMemo = GUI_CreateMemo(&MyApp, 10, 448, 540, 100);
    gui_set_prop(DebugMemo, PROP_COLOR, 0x000000);
    Log_Debug("LOG DE DEBUG:");

    g_focused_control = (void*)PathEdit;
    ultimo_controle_focado = (void*)PathEdit;
    gui_set_prop(PathEdit, PROP_SET_FOCUS, 1);
    Carregar_Diretorio("C:/");
    Flush_Grafico_Janela();

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) { Tratar_Fechamento_Software(); break; }
        bool precisa_redesenhar = false;
        if (primeiro_desenho) { primeiro_desenho = false; precisa_redesenhar = true; }

        bool euTenhoFoco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFoco != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFoco;
            if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco;
            precisa_redesenhar = true;
        }
        if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
        else if (ultimo_controle_focado != NULL) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado, PROP_SET_FOCUS, 1);
        }

        /* v0.1.1 (fix #1): teclado SOMENTE focado */
        if (euTenhoFoco) {
            char key = Obter_Tecla_Entrada();
            if (key != 0) { GUI_ProcessKeyboard(&MyApp, key); precisa_redesenhar = true; }
        }

        /* v0.1.1 (fix #1): clique é CONSUMIDO sempre (não pendura), mas só age focado */
        if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
            if (euTenhoFoco && mouse_hold_timer == 0) {
                int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                ultimo_x = rel_x; ultimo_y = rel_y; mouse_hold_timer = 2;
                TGUIControl* btns[] = {GoButton, BtnRefresh, BtnRename, BtnDel, BtnCopy, BtnNewDir, BtnExecutar};
                for (size_t i = 0; i < sizeof(btns)/sizeof(btns[0]); i++) {
                    if (btns[i] && rel_x >= btns[i]->Left && rel_x < btns[i]->Left + btns[i]->Width &&
                        rel_y >= btns[i]->Top && rel_y < btns[i]->Top + btns[i]->Height) {
                        gui_set_prop(btns[i], PROP_STATE, 2);
                        break;
                    }
                }
                events_process_mouse(rel_x, rel_y, 1, 0);
                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                    precisa_redesenhar = true;
                    if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
                }
            }
        }
        if (mouse_hold_timer > 0) {
            mouse_hold_timer--;
            if (mouse_hold_timer == 0) {
                Reset_All_Button_States();
                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                precisa_redesenhar = true;
            }
        }

        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(euTenhoFoco ? 16 : 32);
    }
    sys_exit();
    return 0;
}
