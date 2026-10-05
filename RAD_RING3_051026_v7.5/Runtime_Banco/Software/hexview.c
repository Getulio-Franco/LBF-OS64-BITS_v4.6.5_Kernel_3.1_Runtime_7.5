/* ============================================================================
HEXVIEW LBF v1.3 - Inspetor/Editor de Disco do LBF-VESA
v1.3: strings UTF-8 convertidas via system/utf_8.h (utf8_to_cp)
v1.2: janela 800x600 (memo 780x400), 16 bytes s/linha (formato clássico),
linhas de controle separadas (nada encavalado), magic byte-a-byte.
Mantém: busca hex (h:..), GRAVAR BYTE, DECODE REGS, VIEW HEX.
============================================================================ */
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "../system/sysutils.h"
#include "../system/utf_8.h"
#include "Runtime_sdk/components/TOS_IPC.h"

void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth  = 800;
const int winHeight = 600;
#define HV_MAX     32768u
#define PAGE_BYTES 256u      /* 16 linhas de 16 bytes */

TGUIControl* EditArq   = NULL;
TGUIControl* BtnAbrir  = NULL;
TGUIControl* BtnReler  = NULL;
TGUIControl* EditOff   = NULL;
TGUIControl* BtnIr     = NULL;
TGUIControl* BtnVolta  = NULL;
TGUIControl* BtnAvanca = NULL;
TGUIControl* EditBusca = NULL;
TGUIControl* BtnBusca  = NULL;
TGUIControl* EditByte  = NULL;
TGUIControl* BtnGravar = NULL;
TGUIControl* BtnDecode = NULL;
TGUIControl* BtnHex    = NULL;
TGUIControl* LabelInfo = NULL;
TGUIControl* MemoHex   = NULL;
TGUIControl* LabelMsg  = NULL;

static uint8_t  g_buf[HV_MAX];
static uint32_t g_size = 0;
static uint32_t g_page = 0;
static char     g_fname[64];
static bool     g_decode = false;

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

char Obter_Tecla_Entrada(void) {
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*) &IPC_WINDOW_LIST[my_app_slot];
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

/* Helper: buffer estático para conversão de strings literais (labels/botões).
   NÃO usar duas chamadas na mesma expressão (ex: f(UTF8("a"), UTF8("b"))). */
static char _utf8_buf[256];
static const char* UTF8(const char* s) {
    utf8_to_cp(s, _utf8_buf, sizeof(_utf8_buf));
    return _utf8_buf;
}

static void set_msg(const char* m) {
    char buf[160];
    utf8_to_cp(m, buf, sizeof(buf));
    if (LabelMsg) GUI_Edit_SetText(LabelMsg, buf);
    sys_debug(m);
    Flush_Grafico_Janela();
}

static void add_line(const char* l) {
    char b[160];
    strcpy(b, l); strcat(b, "\n");
    GUI_Memo_AddStr(MemoHex, b);
}

static int dentro(TGUIControl* c, int x, int y) {
    return c && x >= c->Left && x < (c->Left + c->Width) && y >= c->Top && y < (c->Top + c->Height);
}

static void hex2(uint8_t v, char* out) {
    static const char hx[] = "0123456789ABCDEF";
    out[0] = hx[v >> 4]; out[1] = hx[v & 0xF]; out[2] = 0;
}

static void hex6(uint32_t v, char* out) {
    static const char hx[] = "0123456789ABCDEF";
    for (int i = 5; i >= 0; i--) { out[i] = hx[v & 0xF]; v >>= 4; }
    out[6] = 0;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static uint32_t parse_off(const char* s) {
    uint32_t v = 0;
    if (!s || !s[0]) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (const char* p = s + 2; *p; p++) { int d = hexval(*p); if (d < 0) break; v = v * 16 + (uint32_t)d; }
    } else v = (uint32_t)atoi(s);
    return v;
}

/* comparação de magic byte-a-byte (à prova de paste corrompido) */
static int magic_is(const uint8_t* b, const char* m) {
    for (int i = 0; i < 7; i++) if (b[i] != (uint8_t)m[i]) return 0;
    return 1;
}

/* ---------------- render hex (16 bytes/linha, formato clássico) ---------------- */
static void render_hex(void) {
    GUI_Memo_Clear(MemoHex);
    if (g_size == 0) { set_msg("Nenhum arquivo carregado."); return; }
    if (g_page >= g_size) g_page = 0;
    char line[96];
    for (uint32_t off = g_page; off < g_page + PAGE_BYTES && off < g_size; off += 16) {
        hex6(off, line);
        strcat(line, "   ");
        char h2[3];
        for (int i = 0; i < 16; i++) {
            if (off + i < g_size) { hex2(g_buf[off + i], h2); strcat(line, h2); strcat(line, "  "); }
            else strcat(line, "    ");
        }
        strcat(line, " | ");
        for (int i = 0; i < 16; i++) {
            uint8_t c = (off + i < g_size) ? g_buf[off + i] : 0;
            char ch[2]; ch[0] = (c >= 0x20 && c <= 0x7E) ? (char)c : '.'; ch[1] = 0;
            strcat(line, ch);
        }
        strcat(line, "| ");
        add_line(line);
    }
    char st[96]; char a[12], b[12];
    itoa(g_page, a, 10); itoa(g_size, b, 10);
    strcpy(st, "offset "); strcat(st, a); strcat(st, " .. de "); strcat(st, b); strcat(st, " B");
    set_msg(st);
}

/* ---------------- render decode (registros campo a campo) ---------------- */
static void render_decode(void) {
    GUI_Memo_Clear(MemoHex);
    if (g_size < 32 || !magic_is(g_buf, "LBFTBL1")) {
        set_msg("DECODE só para .TBL (magic LBFTBL1). Use VIEW HEX.");
        return;
    }
    uint32_t ncam, nreg, rsz;
    memcpy(&ncam, g_buf + 12, 4); memcpy(&nreg, g_buf + 16, 4); memcpy(&rsz, g_buf + 24, 4);
    uint32_t hdr = 32 + 32 * ncam;
    char line[128]; char num[12];
    strcpy(line, "=== DECODE: campos= "); itoa(ncam, num, 10); strcat(line, num);
    strcat(line, " regs= "); itoa(nreg, num, 10); strcat(line, num);
    strcat(line, " rec_size= "); itoa(rsz, num, 10); strcat(line, num);
    add_line(line);
    int shown = 0;
    for (uint32_t r = 0; r < nreg; r++) {
        uint32_t base = hdr + r * rsz;
        if (base + rsz > g_size) break;
        if (g_buf[base] != 1) {
            strcpy(line, "--- REG # "); itoa(r, num, 10); strcat(line, num);
            strcat(line, ": DELETADO (viva=2) ---"); add_line(line); continue;
        }
        strcpy(line, "--- REG # "); itoa(r, num, 10); strcat(line, num);
        strcat(line, " @ off "); itoa(base, num, 10); strcat(line, num); strcat(line, " ---");
        add_line(line);
        uint32_t foff = 2;
        for (uint32_t i = 0; i < ncam; i++) {
            const uint8_t* d = g_buf + 32 + i * 32;
            char nome[17]; memcpy(nome, d, 16); nome[16] = 0;
            int L = 0; while (L < 16 && nome[L] != 0) L++; nome[L] = 0;
            uint8_t tipo = d[16], tam = d[17], flags = d[18];
            char val[64];
            if (tipo == 4) {                                   /* C */
                int k = 0; while (k < tam && g_buf[base + foff + k] != 0) k++;
                while (k > 0 && g_buf[base + foff + k - 1] == ' ') k--;
                if (k > 63) k = 63;
                memcpy(val, g_buf + base + foff, (uint32_t)k); val[k] = 0;
            } else {                                           /* I1/I2/I4 */
                int tb = (tipo == 3) ? 4 : (tipo == 2) ? 2 : 1;
                int v = 0;
                for (int b = 0; b < tb; b++) v |= ((int)g_buf[base + foff + b]) << (8 * b);
                itoa(v, val, 10);
            }
            strcpy(line, "   "); strcat(line, nome); strcat(line, " = "); strcat(line, val);
            if (flags & 1) strcat(line, " [PK");
            if (flags & 2) strcat(line, " AI");
            if (flags & 4) strcat(line, " NU");
            if (flags) strcat(line, "]");
            add_line(line);
            foff += tam;
        }
        shown++;
    }
    if (!shown) add_line("(nenhum registro vivo)");
    char st[64]; char n2[12];
    strcpy(st, "decode: "); itoa(shown, n2, 10); strcat(st, n2); strcat(st, " registro(s) vivo(s)");
    set_msg(st);
}

static void render_page(void) { if (g_decode) render_decode(); else render_hex(); }

/* ---------------- abrir / reler ---------------- */
static void hv_open(const char* name) {
    if (!name || !name[0]) { set_msg("Digite o nome do arquivo."); return; }
    file_info_t st;
    if (sys_fat_stat(name, &st) != 0) { g_size = 0; GUI_Memo_Clear(MemoHex); set_msg("Arquivo não encontrado."); return; }
    uint32_t want = (st.size > HV_MAX) ? HV_MAX : st.size;
    int n = sys_fat_read(name, g_buf, want);
    if (n < 0) { g_size = 0; set_msg("Falha na leitura do arquivo."); return; }
    g_size = (uint32_t)n;
    strncpy(g_fname, name, sizeof(g_fname) - 1);
    g_page = 0;
    char info[160];
    if (g_size >= 32 && magic_is(g_buf, "LBFTBL1")) {
        uint32_t ncam, nreg, npk, rsz;
        memcpy(&ncam, g_buf + 12, 4); memcpy(&nreg, g_buf + 16, 4);
        memcpy(&npk,  g_buf + 20, 4); memcpy(&rsz,  g_buf + 24, 4);
        char a[12], b[12], c[12], d[12];
        itoa(ncam, a, 10); itoa(nreg, b, 10); itoa(rsz, c, 10); itoa(npk, d, 10);
        strcpy(info, "TABELA .TBL | campos= "); strcat(info, a);
        strcat(info, " regs= ");  strcat(info, b);
        strcat(info, " rec_size= "); strcat(info, c);
        strcat(info, " next_pk= ");  strcat(info, d);
    } else if (g_size >= 12 && magic_is(g_buf, "LBFCAT1")) {
        uint32_t ne; memcpy(&ne, g_buf + 8, 4);
        char a[12]; itoa(ne, a, 10);
        strcpy(info, "CATÁLOGO .CAT | entradas= "); strcat(info, a);
    } else strcpy(info, "Arquivo binário genérico");
    char sz[16]; itoa(g_size, sz, 10);
    strcat(info, " | lidos= "); strcat(info, sz); strcat(info, " B");
    if (st.size > HV_MAX) strcat(info, " (primeiros 32KB)");
    GUI_Edit_SetText(LabelInfo, info);
    render_page();
}

/* ---------------- eventos ---------------- */
void OnBtnAbrirClick(void* s)  { hv_open(GUI_Edit_GetText(EditArq)); }
void OnBtnRelerClick(void* s)  {
    if (!g_fname[0]) { set_msg("Nada para reler - abra um arquivo antes."); return; }
    uint32_t pg = g_page;
    hv_open(g_fname);
    g_page = pg; render_page();
}
void OnBtnIrClick(void* s) {
    if (!g_size) { set_msg("Abra um arquivo antes."); return; }
    uint32_t o = parse_off(GUI_Edit_GetText(EditOff));
    if (o >= g_size) o = 0;
    g_page = o & ~15u;
    g_decode = false;
    render_page();
}
void OnBtnVoltaClick(void* s) {
    if (!g_size) return;
    g_decode = false;
    g_page = (g_page >= PAGE_BYTES) ? (g_page - PAGE_BYTES) : 0;
    render_page();
}
void OnBtnAvancaClick(void* s) {
    if (!g_size) return;
    g_decode = false;
    if (g_page + PAGE_BYTES < g_size) { g_page += PAGE_BYTES; render_page(); }
    else set_msg("Fim do arquivo.");
}
void OnBtnBuscaClick(void* s) {
    if (!g_size) { set_msg("Abra um arquivo antes."); return; }
    const char* q = GUI_Edit_GetText(EditBusca);
    if (!q || !q[0]) { set_msg("Digite texto ou h:4C4246 (hex)."); return; }
    uint8_t nd[64]; int nlen = 0;
    if ((q[0] == 'h' || q[0] == 'H') && q[1] == ':') {
        const char* p = q + 2;
        while (p[0] && p[1] && nlen < 64) {
            int a = hexval(p[0]), b = hexval(p[1]);
            if (a < 0 || b < 0) break;
            nd[nlen++] = (uint8_t)((a << 4) | b);
            p += 2;
        }
        if (!nlen) { set_msg("hex inválido - use pares: h:4C4246"); return; }
    } else {
        while (q[nlen] && nlen < 64) { nd[nlen] = (uint8_t)q[nlen]; nlen++; }
    }
    for (uint32_t i = g_page; i + (uint32_t)nlen <= g_size; i++) {
        if (memcmp(g_buf + i, nd, (uint32_t)nlen) == 0) {
            g_decode = false;
            g_page = i & ~15u;
            render_page();
            char m[64]; char h[8];
            hex6(i, h);
            strcpy(m, "achado em 0x"); strcat(m, h);
            set_msg(m);
            return;
        }
    }
    set_msg("não encontrado deste offset em diante.");
}
void OnBtnGravarClick(void* s) {
    if (!g_size || !g_fname[0]) { set_msg("Abra um arquivo antes."); return; }
    uint32_t off = parse_off(GUI_Edit_GetText(EditOff));
    if (off >= g_size) { set_msg("Offset fora do arquivo."); return; }
    const char* vb = GUI_Edit_GetText(EditByte);
    int a = hexval(vb[0]), b = hexval(vb[1] ? vb[1] : vb[0]);
    if (a < 0 || b < 0) { set_msg("Valor hex inválido (ex: 20 ou 0x20)."); return; }
    uint8_t byte = (uint8_t)((vb[1] ? (a << 4) | b : a) & 0xFF);
    if (sys_fat_write_at(g_fname, &byte, 1, off) != 0) { set_msg("Falha na gravação do byte."); return; }
    uint32_t pg = g_page;
    hv_open(g_fname);
    g_page = pg; g_decode = false; render_page();
    char m[64]; char h[8], h2[3];
    hex6(off, h); hex2(byte, h2);
    strcpy(m, "byte 0x"); strcat(m, h2); strcat(m, " gravado em 0x"); strcat(m, h);
    set_msg(m);
}
void OnBtnDecodeClick(void* s) { g_decode = true;  render_page(); }
void OnBtnHexClick(void* s)   { g_decode = false; render_page(); }

/* ---------------- main ---------------- */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("HexView LBF", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "HexView LBF v1.3", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);

    /* linha 1: arquivo */
    GUI_CreateLabel(&MyApp, 10, 42, UTF8("Arquivo:"));
    EditArq  = GUI_CreateEdit(&MyApp, 70, 38, 260, 25, "CADA0001.TBL", NULL);
    BtnAbrir = GUI_CreateButton(&MyApp, 340, 38, 100, 25, UTF8("ABRIR"),  OnBtnAbrirClick);
    BtnReler = GUI_CreateButton(&MyApp, 450, 38, 100, 25, UTF8("RELER"),  OnBtnRelerClick);

    /* linha 2: offset + navegação + busca */
    GUI_CreateLabel(&MyApp, 10, 72, UTF8("Offset:"));
    EditOff  = GUI_CreateEdit(&MyApp, 70, 68, 90, 25, "0", NULL);
    BtnIr    = GUI_CreateButton(&MyApp, 170, 68, 70,  25, UTF8("IR"),     OnBtnIrClick);
    BtnVolta = GUI_CreateButton(&MyApp, 250, 68, 100, 25, UTF8("<< VOLTAR"),  OnBtnVoltaClick);
    BtnAvanca= GUI_CreateButton(&MyApp, 360, 68, 110, 25, UTF8("AVANÇAR >>"), OnBtnAvancaClick);
    EditBusca= GUI_CreateEdit(&MyApp, 530, 68, 150, 25, "", NULL);
    BtnBusca = GUI_CreateButton(&MyApp, 690, 68, 90, 25, UTF8("ACHAR"), OnBtnBuscaClick);

    /* linha 3: info do arquivo (linha própria, nada encavalado) */
    LabelInfo = GUI_CreateLabel(&MyApp, 10, 100, UTF8("(nenhum arquivo aberto)"));

    /* linha 4: edição de byte + modos */
    GUI_CreateLabel(&MyApp, 10, 124, UTF8("Valor(hex):"));
    EditByte = GUI_CreateEdit(&MyApp, 90, 120, 60, 25, "20", NULL);
    BtnGravar= GUI_CreateButton(&MyApp, 160, 120, 120, 25, UTF8("GRAVAR BYTE"), OnBtnGravarClick);
    BtnDecode= GUI_CreateButton(&MyApp, 290, 120, 120, 25, UTF8("DECODE REGS"), OnBtnDecodeClick);
    BtnHex   = GUI_CreateButton(&MyApp, 420, 120, 100, 25, UTF8("VIEW HEX"),   OnBtnHexClick);

    /* área hex: grande, 16 bytes/linha cabem com folga */
    MemoHex = GUI_CreateMemo(&MyApp, 10, 150, 780, 400);
    gui_set_prop(MemoHex, PROP_COLOR, 0x000000);

    /* mensagem */
    LabelMsg = GUI_CreateLabel(&MyApp, 10, 556, UTF8("Digite o nome do arquivo e clique ABRIR."));

    g_focused_control = (void*)EditArq;
    ultimo_controle_focado = (void*)EditArq;
    gui_set_prop(EditArq, PROP_SET_FOCUS, 1);
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
            if (key != 0) { GUI_ProcessKeyboard(&MyApp, key); precisa_redesenhar = true; }
            if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
                if (mouse_hold_timer == 0) {
                    int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                    int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                    ultimo_x = rel_x; ultimo_y = rel_y; mouse_hold_timer = 2;
                    if      (dentro(BtnAbrir,  rel_x, rel_y)) gui_set_prop(BtnAbrir,  PROP_STATE, 2);
                    else if (dentro(BtnReler,  rel_x, rel_y)) gui_set_prop(BtnReler,  PROP_STATE, 2);
                    else if (dentro(BtnIr,     rel_x, rel_y)) gui_set_prop(BtnIr,     PROP_STATE, 2);
                    else if (dentro(BtnVolta,  rel_x, rel_y)) gui_set_prop(BtnVolta,  PROP_STATE, 2);
                    else if (dentro(BtnAvanca, rel_x, rel_y)) gui_set_prop(BtnAvanca, PROP_STATE, 2);
                    else if (dentro(BtnBusca,  rel_x, rel_y)) gui_set_prop(BtnBusca,  PROP_STATE, 2);
                    else if (dentro(BtnGravar, rel_x, rel_y)) gui_set_prop(BtnGravar, PROP_STATE, 2);
                    else if (dentro(BtnDecode, rel_x, rel_y)) gui_set_prop(BtnDecode, PROP_STATE, 2);
                    else if (dentro(BtnHex,    rel_x, rel_y)) gui_set_prop(BtnHex,    PROP_STATE, 2);
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
                    if (BtnAbrir)  gui_set_prop(BtnAbrir,  PROP_STATE, 0);
                    if (BtnReler)  gui_set_prop(BtnReler,  PROP_STATE, 0);
                    if (BtnIr)     gui_set_prop(BtnIr,     PROP_STATE, 0);
                    if (BtnVolta)  gui_set_prop(BtnVolta,  PROP_STATE, 0);
                    if (BtnAvanca) gui_set_prop(BtnAvanca, PROP_STATE, 0);
                    if (BtnBusca)  gui_set_prop(BtnBusca,  PROP_STATE, 0);
                    if (BtnGravar) gui_set_prop(BtnGravar, PROP_STATE, 0);
                    if (BtnDecode) gui_set_prop(BtnDecode, PROP_STATE, 0);
                    if (BtnHex)    gui_set_prop(BtnHex,    PROP_STATE, 0);
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
