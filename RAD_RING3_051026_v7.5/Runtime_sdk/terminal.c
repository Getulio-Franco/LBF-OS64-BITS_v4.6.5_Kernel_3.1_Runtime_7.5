/* ============================================================================
TERMINAL LBF v4.4 (Parte 1) - Dispatch Table & Tokenizer com Aspas
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

#define CMD_BUFFER_SIZE 256
#define HIST_MAX        8
#define REDIR_CAP       8192
#define MAX_ARGS        16

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth  = 720;
const int winHeight = 480;

TGUIControl* TerminalMemo = NULL;
char cmd_buffer[CMD_BUFFER_SIZE];
int cmd_idx = 0;

/* ---- historico ---- */
static char g_hist[HIST_MAX][128];
static int  g_hist_count = 0;
/* ---- redirecao ---- */
static int  g_redir = 0;
static char g_redir_file[64];
static char g_redir_buf[REDIR_CAP];
static int  g_redir_len = 0;
/* ---- unidade ativa + caminho desde a raiz ---- */
static char g_drive = 'C';
static char g_path[128] = "/";
static uint8_t fbuf[8192 + 1];

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
    sys_fat_chdir("/");
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50);
}

/* ---------------- saida (memo OU buffer de redirecao) ---------------- */
static void t_line(const char* s) {
    if (g_redir) {
        int l = (int)strlen(s);
        if (g_redir_len + l + 1 < REDIR_CAP) {
            memcpy(g_redir_buf + g_redir_len, s, (uint32_t)l);
            g_redir_len += l;
            g_redir_buf[g_redir_len++] = '\n';
        }
        return;
    }
    GUI_Memo_AddStr(TerminalMemo, s);
    GUI_Memo_AddStr(TerminalMemo, "\n");
}

static void t_num(const char* label, uint64_t v) {
    char b[80]; char n[24];
    itoa(v, n, 10);
    strcpy(b, label); strcat(b, n);
    t_line(b);
}

static void memo_raw(const char* s) { GUI_Memo_AddStr(TerminalMemo, s); }

/* ---------------- modelo de caminho C:/ ---------------- */
static void prompt_path(char* out) {
    out[0] = g_drive; out[1] = ':'; out[2] = 0;
    strcat(out, g_path);
}

static void path_pop(void) {
    int n = (int)strlen(g_path);
    if (n <= 1) return;
    while (n > 0 && g_path[n - 1] == '/') n--;
    while (n > 0 && g_path[n - 1] != '/') n--;
    if (n == 0) strcpy(g_path, "/"); else g_path[n] = 0;
}

static void path_append(const char* rel) {
    const char* p = rel;
    while (*p) {
        char comp[64]; int i = 0;
        while (*p && *p != '/' && i < 63) comp[i++] = *p++;
        comp[i] = 0;
        while (*p == '/') p++;
        if (!comp[0] || strcmp(comp, ".") == 0) continue;
        if (strcmp(comp, "..") == 0) { path_pop(); continue; }
        if (strcmp(g_path, "/") != 0) strcat(g_path, "/");
        strcat(g_path, comp);
    }
}

static int chdir_root(void) {
    if (sys_fat_chdir("/") == 0)   return 1;
    if (sys_fat_chdir("0:/") == 0) return 1;
    if (sys_fat_chdir(".") == 0)   return 1;
    return 0;
}

static int walk_from_root(const char* rel) {
    if (!chdir_root()) return 0;
    const char* p = rel;
    while (*p) {
        char comp[64]; int i = 0;
        while (*p && *p != '/' && i < 63) comp[i++] = *p++;
        comp[i] = 0;
        while (*p == '/') p++;
        if (!comp[0] || strcmp(comp, ".") == 0) continue;
        if (sys_fat_chdir(comp) != 0) return 0;
    }
    return 1;
}

static const char* nome_no_drive(const char* s) {
    if (s && s[0] && s[1] == ':') {
        char L = s[0];
        if (L != 'c' && L != 'C' && L != '0') {
            t_line("unidade indisponivel nesta versao (so C:; D:/E: voltam com o EHCI)");
            return NULL;
        }
        s += 2;
        while (*s == '/') s++;
    }
    return s;
}

/* ---------------- TOKENIZER (Suporte a Aspas e Espacos) ---------------- */
static int tokenize_line(char* line, char* argv[]) {
    int argc = 0;
    char* p = line;
    while (*p && argc < MAX_ARGS - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') {
                *p = '\0';
                p++;
            }
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) {
                *p = '\0';
                p++;
            }
        }
    }
    argv[argc] = NULL;
    return argc;
}

static void hex_line(uint32_t off, const uint8_t* b, uint32_t n) {
    static const char hx[] = "0123456789ABCDEF";
    char l[80]; char o[8]; char h[3];
    for (int j = 5; j >= 0; j--) { o[j] = hx[off & 0xF]; off >>= 4; }
    o[6] = 0;
    strcpy(l, o); strcat(l, "  ");
    for (uint32_t i = 0; i < 16; i++) {
        if (i < n) { h[0] = hx[b[i] >> 4]; h[1] = hx[b[i] & 0xF]; h[2] = 0; strcat(l, h); strcat(l, " "); }
        else strcat(l, "   ");
    }
    strcat(l, " |");
    for (uint32_t i = 0; i < 16; i++) {
        char c = (i < n) ? (char)b[i] : 0;
        char ch[2]; ch[0] = (c >= 0x20 && c <= 0x7E) ? c : '.'; ch[1] = 0;
        strcat(l, ch);
    }
    strcat(l, "|");
    t_line(l);
}

/* ---------------- historico ---------------- */
static void hist_record(const char* cmd) {
    if (g_hist_count < HIST_MAX) {
        strncpy(g_hist[g_hist_count], cmd, 127);
        g_hist[g_hist_count][127] = 0;
        g_hist_count++;
    } else {
        for (int i = 0; i < HIST_MAX - 1; i++) strcpy(g_hist[i], g_hist[i + 1]);
        strncpy(g_hist[HIST_MAX - 1], cmd, 127);
        g_hist[HIST_MAX - 1][127] = 0;
    }
}

/* ---------------- IMPLEMENTACAO DOS COMANDOS ---------------- */
typedef void (*cmd_handler_t)(int argc, char** argv);

static void cmd_help_h(int argc, char** argv) {
    if (argc < 2) {
        t_line("=== TERMINAL LBF v4.4 - COMANDOS ===");
        t_line("ARQUIVOS: ls cd pwd cat hex stat rm cp mv mkdir touch write append grep");
        t_line("DISCO: chk | PROC: ps kill run pid | SYS: clear ver ticks klog dbg");
        t_line("HARDWARE: pci vesa mouse net ser beep | HIST: history !N !!");
        t_line("UNIDADE: C: ativa (D:/E: voltam na fase EHCI)");
        t_line("REDIRECAO: <cmd> > ARQ.TXT | <cmd> >> ARQ.TXT | BOOT: AUTOEXEC.TXT");
        t_line("digite help <cmd> para a descricao de um comando.");
        return;
    }
    static const struct { const char* nome; const char* desc; } g_help[] = {
        {"ls",      "lista arquivos/pastas de C: + diretorio atual (alias: dir)"},
        {"cd",      "cd <dir> | cd .. | cd / | cd /pasta/sub | cd sozinho mostra o caminho"},
        {"pwd",     "mostra unidade + caminho atual (C:/...)"},
        {"cat",     "cat <arq> - imprime conteudo (nao-imprimiveis viram '.')"},
        {"hex",     "hex <arq> [n] - dump hex+ascii dos primeiros n bytes (padrao 128)"},
        {"stat",    "stat <arq> - tamanho e atributos"},
        {"rm",      "rm <arq> - deleta"},
        {"cp",      "cp <orig> <dest> - copia"},
        {"mv",      "mv <orig> <dest> - renomeia/move"},
        {"mkdir",   "mkdir <dir> - cria pasta"},
        {"touch",   "touch <arq> - cria arquivo vazio"},
        {"write",   "write <arq> <texto> - grava texto (sobrescreve)"},
        {"append",  "append <arq> <texto> - acrescenta texto no fim"},
        {"grep",    "grep <texto> - procura nos arquivos <=8KB do dir atual"},
        {"chk",     "consistencia de leitura dos arquivos do dir atual (2x + fatias)"},
        {"c:",      "mostra/confirma a unidade ativa (C: nesta versao)"},
        {"ps",      "lista processos (pid/status/nome)"},
        {"kill",    "kill <pid> - termina processo"},
        {"run",     "run <app.elf> - executa um programa"},
        {"pid",     "mostra o pid deste terminal"},
        {"ticks",   "ticks do sistema desde o boot"},
        {"klog",    "mostra logs novos do kernel"},
        {"dbg",     "dbg <msg> - envia msg ao debug do kernel"},
        {"pci",     "varre o barramento PCI com classes decodificadas"},
        {"vesa",    "informacoes de video (resolucao/bpp)"},
        {"mouse",   "posicao e botoes do mouse"},
        {"net",     "MAC da placa de rede"},
        {"ser",     "ser <texto> - envia p/ COM1 9600"},
        {"beep",    "tom 440Hz de 0,1s no audio"},
        {"history", "lista os ultimos comandos (!N repete, !! = ultimo)"},
        {"clear",   "limpa a tela (alias: cls)"},
        {"ver",     "versao do terminal"},
        {NULL, NULL}
    };
    for (int i = 0; g_help[i].nome; i++) {
        if (strcmp(g_help[i].nome, argv[1]) == 0) {
            char l[160];
            strcpy(l, g_help[i].nome); strcat(l, ": "); strcat(l, g_help[i].desc);
            t_line(l);
            return;
        }
    }
    char m[80]; strcpy(m, "sem ajuda para: "); strcat(m, argv[1]); t_line(m);
}

static void cmd_clear_h(int argc, char** argv) { (void)argc; (void)argv; if (!g_redir) GUI_Memo_Clear(TerminalMemo); }
static void cmd_ver_h(int argc, char** argv) {
    (void)argc; (void)argv;
    t_line("Terminal LBF v4.4 | LBF-VESA | versao C:/ funcional (sem EHCI)");
    t_line("dispatch table + parser com aspas + historico + redirecao + AUTOEXEC");
}
static void cmd_ticks_h(int argc, char** argv) { (void)argc; (void)argv; t_num("ticks do sistema: ", get_system_ticks()); }
static void cmd_pid_h(int argc, char** argv) { (void)argc; (void)argv; t_num("pid deste terminal: ", sys_get_pid()); }

static void cmd_history_h(int argc, char** argv) {
    (void)argc; (void)argv;
    if (g_hist_count == 0) { t_line("history vazio."); return; }
    for (int i = 0; i < g_hist_count; i++) {
        char l[160]; char n[8];
        itoa((uint64_t)(i + 1), n, 10);
        strcpy(l, " "); strcat(l, n); strcat(l, ": "); strcat(l, g_hist[i]);
        t_line(l);
    }
    t_line("(!N repete o N-esimo | !! repete o ultimo)");
}

static void cmd_drive_show(void) {
    t_line("Unidade ativa: C:\\  (HD SATA)");
    char p[160]; prompt_path(p);
    char m[192]; strcpy(m, "caminho: "); strcat(m, p);
    t_line(m);
    t_line("D:/E: (USB) voltam na proxima fase, com o EHCI refeito.");
}

static void cmd_ls_h(int argc, char** argv) {
    (void)argc; (void)argv;
    char nome[64]; file_info_t fi;
    int idx = 0, n = 0;
    char hdr[160]; char p[160];
    strcpy(hdr, "Diretorio: "); prompt_path(p); strcat(hdr, p);
    t_line(hdr);
    while (idx < 500 && sys_fat_readdir(idx, nome, &fi) == 1) {
        if (strcmp(nome, ".") != 0 && strcmp(nome, "..") != 0) {
            char l[96]; char sz[16];
            if (fi.attributes & 0x10) { strcpy(l, " [DIR]  "); strcat(l, nome); }
            else {
                itoa(fi.size, sz, 10);
                strcpy(l, " [ARQ]  "); strcat(l, nome);
                strcat(l, "  "); strcat(l, sz); strcat(l, " B");
            }
            t_line(l); n++;
        }
        idx++;
    }
    t_num("itens: ", (uint64_t)(uint32_t)n);
}

static void cmd_cd_h(int argc, char** argv) {
    if (argc < 2) { char p[160]; prompt_path(p); t_line(p); return; }
    const char* rp = argv[1];
    if (rp[0] && rp[1] == ':') {
        if (rp[0] != 'c' && rp[0] != 'C' && rp[0] != '0') {
            t_line("cd: nesta versao so C: esta ativa (D:/E: voltam com o EHCI)");
            return;
        }
        rp += 2;
    }
    if (rp[0] == 0) { char p[160]; prompt_path(p); t_line(p); return; }
    if (strcmp(rp, "/") == 0) {
        if (chdir_root()) { strcpy(g_path, "/"); char p[160]; prompt_path(p); t_line(p); }
        else t_line("cd: falha ao ir para a raiz");
        return;
    }
    if (rp[0] == '/') {
        char saved[128]; strcpy(saved, g_path);
        if (walk_from_root(rp + 1)) {
            strcpy(g_path, "/"); path_append(rp + 1);
            char p[160]; prompt_path(p); t_line(p);
        } else {
            t_line("cd: caminho invalido");
            walk_from_root(saved + 1);
            strcpy(g_path, saved);
        }
        return;
    }
    char saved[128]; strcpy(saved, g_path);
    const char* p = rp; int ok = 1;
    while (*p && ok) {
        char comp[64]; int i = 0;
        while (*p && *p != '/' && i < 63) comp[i++] = *p++;
        comp[i] = 0;
        while (*p == '/') p++;
        if (!comp[0] || strcmp(comp, ".") == 0) continue;
        if (sys_fat_chdir(comp) != 0) ok = 0;
        else path_append(comp);
    }
    if (ok) { char p2[160]; prompt_path(p2); t_line(p2); }
    else {
        t_line("cd: caminho invalido");
        walk_from_root(saved + 1);
        strcpy(g_path, saved);
    }
}

static void cmd_pwd_h(int argc, char** argv) { (void)argc; (void)argv; char p[160]; prompt_path(p); t_line(p); }

static void cmd_cat_h(int argc, char** argv) {
    if (argc < 2) { t_line("uso: cat <arquivo>"); return; }
    const char* nm = nome_no_drive(argv[1]);
    if (!nm) return;
    file_info_t fi;
    if (sys_fat_stat(nm, &fi) != 0) { t_line("cat: arquivo nao encontrado."); return; }
    uint32_t want = (fi.size > 8192) ? 8192 : fi.size;
    int n = sys_fat_read(nm, fbuf, want);
    if (n <= 0) { t_line("cat: falha de leitura."); return; }
    for (int i = 0; i < n; i++)
        if (fbuf[i] != '\n' && fbuf[i] != '\t' && (fbuf[i] < 0x20 || fbuf[i] > 0x7E)) fbuf[i] = '.';
    fbuf[n] = 0;
    t_line((char*)fbuf);
    if (fi.size > 8192) t_line("(truncado em 8192 B)");
}

static void cmd_hex_h(int argc, char** argv) {
    if (argc < 2) { t_line("uso: hex <arquivo> [bytes]"); return; }
    const char* nm = nome_no_drive(argv[1]);
    if (!nm) return;
    uint32_t want = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 128;
    if (want == 0 || want > 8192) want = 128;
    file_info_t fi;
    if (sys_fat_stat(nm, &fi) != 0) { t_line("hex: arquivo nao encontrado."); return; }
    if (want > fi.size) want = fi.size;
    int n = sys_fat_read(nm, fbuf, want);
    if (n <= 0) { t_line("hex: falha de leitura."); return; }
    for (int off = 0; off < n; off += 16)
        hex_line((uint32_t)off, fbuf + off, (uint32_t)((n - off > 16) ? 16 : n - off));
}

static void cmd_stat_h(int argc, char** argv) {
    if (argc < 2) { t_line("uso: stat <arquivo>"); return; }
    const char* nm = nome_no_drive(argv[1]);
    if (!nm) return;
    file_info_t fi;
    if (sys_fat_stat(nm, &fi) != 0) { t_line("stat: arquivo nao encontrado."); return; }
    char l[96]; char sz[16];
    strcpy(l, nm); strcat(l, ": ");
    itoa(fi.size, sz, 10); strcat(l, sz); strcat(l, " B, attr=0x");
    char a[8]; itoa(fi.attributes, a, 16); strcat(l, a);
    if (fi.attributes & 0x10) strcat(l, " (DIR)");
    t_line(l);
}

static void cmd_rm_h(int argc, char** argv) {
    if (argc < 2) { t_line("uso: rm <arquivo>"); return; }
    const char* nm = nome_no_drive(argv[1]);
    if (!nm) return;
    t_line((sys_fat_rm(nm) == 0) ? "rm: ok." : "rm: falha.");
}

static void cmd_cp_h(int argc, char** argv) {
    if (argc < 3) { t_line("uso: cp <origem> <destino>"); return; }
    const char* na = nome_no_drive(argv[1]);
    const char* nb = nome_no_drive(argv[2]);
    if (!na || !nb) return;
    t_line((sys_fat_copy(na, nb) >= 0) ? "cp: ok." : "cp: falha.");
}

static void cmd_mv_h(int argc, char** argv) {
    if (argc < 3) { t_line("uso: mv <origem> <destino>"); return; }
    const char* na = nome_no_drive(argv[1]);
    const char* nb = nome_no_drive(argv[2]);
    if (!na || !nb) return;
    t_line((sys_fat_rename(na, nb) == 0) ? "mv: ok." : "mv: falha.");
}

static void cmd_mkdir_h(int argc, char** argv) {
    if (argc < 2) { t_line("uso: mkdir <dir>"); return; }
    const char* nm = nome_no_drive(argv[1]);
    if (!nm) return;
    t_line((sys_fat_mkdir(nm) == 0) ? "mkdir: ok." : "mkdir: falha.");
}

static void cmd_touch_h(int argc, char** argv) {
    if (argc < 2) { t_line("uso: touch <arquivo>"); return; }
    const char* nm = nome_no_drive(argv[1]);
    if (!nm) return;
    t_line((sys_fat_write(nm, fbuf, 0) == 0) ? "touch: ok." : "touch: falha.");
}

static void cmd_write_h(int argc, char** argv) {
    if (argc < 3) { t_line("uso: write <arquivo> <texto>"); return; }
    const char* nm = nome_no_drive(argv[1]);
    if (!nm) return;
    t_line((sys_fat_write(nm, argv[2], (uint32_t)strlen(argv[2])) == 0) ? "write: ok." : "write: falha.");
}

static void cmd_append_h(int argc, char** argv) {
    if (argc < 3) { t_line("uso: append <arquivo> <texto>"); return; }
    const char* nm = nome_no_drive(argv[1]);
    if (!nm) return;
    t_line((sys_fat_append(nm, argv[2], (uint32_t)strlen(argv[2])) == 0) ? "append: ok." : "append: falha.");
}

static void cmd_grep_h(int argc, char** argv) {
    if (argc < 2) { t_line("uso: grep <texto>"); return; }
    char nome[64]; file_info_t fi;
    int idx = 0, ach = 0;
    while (idx < 500 && sys_fat_readdir(idx, nome, &fi) == 1) {
        idx++;
        if ((fi.attributes & 0x10) || fi.size == 0 || fi.size > 8192) continue;
        int n = sys_fat_read(nome, fbuf, fi.size);
        if (n <= 0) continue;
        fbuf[n] = 0;
        char* hit = strstr((char*)fbuf, argv[1]);
        if (hit) {
            char l[96]; char off[16];
            itoa((uint64_t)(uint32_t)(hit - (char*)fbuf), off, 10);
            strcpy(l, "achado em "); strcat(l, nome); strcat(l, " @"); strcat(l, off);
            t_line(l); ach++;
        }
    }
    t_num("arquivos com ocorrencia: ", (uint64_t)(uint32_t)ach);
}

static void cmd_chk_h(int argc, char** argv) {
    (void)argc; (void)argv;
    char nome[64]; file_info_t fi;
    int idx = 0, ok = 0, bad = 0, skip = 0;
    static uint8_t a[4096], b[4096], sl[64];
    t_line("--- chk: consistencia de leitura (2x + fatias) ---");
    while (idx < 500 && sys_fat_readdir(idx, nome, &fi) == 1) {
        idx++;
        if ((fi.attributes & 0x10) || fi.size == 0 || fi.size > 4096) { skip++; continue; }
        int n1 = sys_fat_read(nome, a, fi.size);
        int n2 = sys_fat_read(nome, b, fi.size);
        int bom = (n1 == (int)fi.size) && (n2 == n1) && (memcmp(a, b, (uint32_t)n1) == 0);
        if (bom && n1 > 64) {
            uint32_t offs[3];
            offs[0] = 0; offs[1] = (uint32_t)n1 / 2; offs[2] = (uint32_t)n1 - 64;
            for (int k = 0; k < 3 && bom; k++) {
                if (sys_fat_read_at(nome, sl, 64, offs[k]) == 0) {
                    if (memcmp(sl, a + offs[k], 64) != 0) bom = 0;
                } else bom = 0;
            }
        }
        char l[96];
        strcpy(l, " "); strcat(l, nome); strcat(l, bom ? "  OK" : "  DIVERGENTE!");
        t_line(l);
        if (bom) ok++; else bad++;
    }
    char s[96]; char x[12], y[12], z[12];
    itoa(ok, x, 10); itoa(bad, y, 10); itoa(skip, z, 10);
    strcpy(s, "chk: "); strcat(s, x); strcat(s, " ok, "); strcat(s, y);
    strcat(s, " divergentes, "); strcat(s, z); strcat(s, " pulados");
    t_line(s);
}

static void cmd_klog_h(int argc, char** argv) {
    (void)argc; (void)argv;
    static char kb[2048];
    int n = sys_read_kernel_log(kb, sizeof(kb) - 1);
    if (n <= 0) { t_line("(sem logs novos do kernel)"); return; }
    kb[n] = 0;
    if (g_redir) { t_line(kb); return; }
    GUI_Memo_AddStr(TerminalMemo, kb);
    GUI_Memo_AddStr(TerminalMemo, "\n");
}

static void cmd_dbg_h(int argc, char** argv) {
    if (argc < 2) { t_line("uso: dbg <mensagem>"); return; }
    sys_debug(argv[1]);
    t_line("dbg: enviado ao debug do kernel.");
}

static void cmd_ps_h(int argc, char** argv) {
    (void)argc; (void)argv;
    TProcessInfo p_info[32];
    int total = sys_get_ps_data(p_info, 32);
    if (total <= 0) { t_line("Nenhum processo retornado."); return; }
    t_line("PID     STATUS      NOME");
    for (int i = 0; i < total; i++) {
        char pid_str[16]; char l[128];
        itoa((uint64_t)p_info[i].pid, pid_str, 10);
        strcpy(l, pid_str);
        while (strlen(l) < 8) strcat(l, " ");
        strcat(l, p_info[i].state == 1 ? "RUNNING     " : "READY       ");
        strcat(l, p_info[i].name);
        t_line(l);
    }
}

static void cmd_kill_h(int argc, char** argv) {
    if (argc < 2) { t_line("uso: kill <pid>"); return; }
    t_num("kill rc=", (uint64_t)(uint32_t)sys_kill((uint64_t)atoi(argv[1])));
}

static void cmd_run_h(int argc, char** argv) {
    if (argc < 2) { t_line("uso: run <arquivo.elf>"); return; }
    char l[96]; strcpy(l, "exec: "); strcat(l, argv[1]); t_line(l);
    const char* nm = nome_no_drive(argv[1]);
    if (nm) sys_exec(nm);
}

static const char* classe_pci(uint8_t c) {
    switch (c) {
        case 0x00: return "Pre-Classificado";
        case 0x01: return "Armazenamento (SATA/IDE)";
        case 0x02: return "Rede (Ethernet)";
        case 0x03: return "Video (VGA/VESA)";
        case 0x04: return "Multimedia (Audio)";
        case 0x05: return "Controladora de Memoria";
        case 0x06: return "Ponte de Barramento";
        case 0x07: return "Comunicacao Simples (Serial)";
        case 0x08: return "Periferico de Sistema (PIC/DMA)";
        case 0x09: return "Dispositivo de Entrada";
        case 0x0C: return "Serial Bus (USB)";
        default:   return "Desconhecido";
    }
}

static void cmd_pci_h(int argc, char** argv) {
    (void)argc; (void)argv;
    typedef struct {
        uint8_t bus, slot, func;
        uint16_t vendor_id, device_id;
        uint8_t class_id, subclass_id, prog_if, encontrado;
    } __attribute__((packed)) pci_device_t;
    pci_device_t dev;
    int index = 0, n = 0;
    while (1) {
        if (sys_get_pci_device(index, (uintptr_t)&dev) == -1) break;
        char l[128]; char h[12];
        strcpy(l, "B:S:F "); itoa(dev.bus, h, 10); strcat(l, h); strcat(l, ":");
        itoa(dev.slot, h, 10); strcat(l, h); strcat(l, ":");
        itoa(dev.func, h, 10); strcat(l, h);
        strcat(l, "  vendor=0x"); itoa(dev.vendor_id, h, 16); strcat(l, h);
        strcat(l, " dev=0x"); itoa(dev.device_id, h, 16); strcat(l, h);
        t_line(l);
        char l2[96];
        strcpy(l2, "   -> "); strcat(l2, classe_pci(dev.class_id));
        t_line(l2);
        index++; n++;
    }
    t_num("dispositivos PCI: ", (uint64_t)(uint32_t)n);
}

static void cmd_vesa_h(int argc, char** argv) {
    (void)argc; (void)argv;
    vesa_info_t v;
    if (get_video_info(&v) != 0) { t_line("vesa: falha."); return; }
    char l[96]; char n[12];
    strcpy(l, "video: "); itoa(v.width, n, 10); strcat(l, n); strcat(l, "x");
    itoa(v.height, n, 10); strcat(l, n); strcat(l, " bpp=");
    itoa(v.bpp, n, 10); strcat(l, n);
    t_line(l);
}

static void cmd_mouse_h(int argc, char** argv) {
    (void)argc; (void)argv;
    mouse_t m;
    get_mouse(&m);
    char l[96]; char n[12];
    strcpy(l, "mouse: x="); itoa(m.x, n, 10); strcat(l, n); strcat(l, " y=");
    itoa(m.y, n, 10); strcat(l, n); strcat(l, " botoes=");
    itoa(m.buttons, n, 10); strcat(l, n);
    t_line(l);
}

static void cmd_net_h(int argc, char** argv) {
    (void)argc; (void)argv;
    uint8_t mac[6];
    if (sys_net_get_mac(mac) != 0) { t_line("net: falha ao ler MAC."); return; }
    static const char hx[] = "0123456789ABCDEF";
    char l[64] = "MAC: ";
    for (int i = 0; i < 6; i++) {
        char h[3]; h[0] = hx[mac[i] >> 4]; h[1] = hx[mac[i] & 0xF]; h[2] = 0;
        strcat(l, h); if (i < 5) strcat(l, ":");
    }
    t_line(l);
}

static void cmd_ser_h(int argc, char** argv) {
    if (argc < 2) { t_line("uso: ser <texto>"); return; }
    static int ser_id = -1;
    if (ser_id < 0) ser_id = sys_serial_open(1, 9600);
    if (ser_id < 0) { t_line("ser: falha ao abrir COM1."); return; }
    sys_serial_write(ser_id, argv[1], (uint32_t)strlen(argv[1]));
    t_line("ser: enviado p/ COM1 (9600).");
}

static void cmd_beep_h(int argc, char** argv) {
    (void)argc; (void)argv;
    static int16_t bb[9600 * 2];
    int frames = 4800, period = 54;
    for (int i = 0; i < frames; i++) {
        int16_t v = ((i % period) < period / 2) ? 8000 : -8000;
        bb[i * 2] = v; bb[i * 2 + 1] = v;
    }
    t_num("beep rc=", (uint64_t)(uint32_t)sys_audio_play(bb, (uint32_t)(frames * 4), false));
}

static void cmd_drive_h(int argc, char** argv) {
    if (argc >= 1 && argv[0][2] != 0) {
        cmd_cd_h(argc, argv);
    } else {
        cmd_drive_show();
    }
}

/* ---------------- DISPATCH TABLE ---------------- */
typedef struct {
    const char* name;
    cmd_handler_t handler;
} CommandMap;

static const CommandMap g_cmd_table[] = {
    {"help",    cmd_help_h},
    {"?",       cmd_help_h},
    {"clear",   cmd_clear_h},
    {"cls",     cmd_clear_h},
    {"ver",     cmd_ver_h},
    {"ticks",   cmd_ticks_h},
    {"pid",     cmd_pid_h},
    {"klog",    cmd_klog_h},
    {"dbg",     cmd_dbg_h},
    {"history", cmd_history_h},
    {"c:",      cmd_drive_h},
    {"C:",      cmd_drive_h},
    {"ls",      cmd_ls_h},
    {"dir",     cmd_ls_h},
    {"cd",      cmd_cd_h},
    {"pwd",     cmd_pwd_h},
    {"cat",     cmd_cat_h},
    {"hex",     cmd_hex_h},
    {"stat",    cmd_stat_h},
    {"rm",      cmd_rm_h},
    {"cp",      cmd_cp_h},
    {"mv",      cmd_mv_h},
    {"mkdir",   cmd_mkdir_h},
    {"touch",   cmd_touch_h},
    {"write",   cmd_write_h},
    {"append",  cmd_append_h},
    {"grep",    cmd_grep_h},
    {"chk",     cmd_chk_h},
    {"ps",      cmd_ps_h},
    {"kill",    cmd_kill_h},
    {"run",     cmd_run_h},
    {"pci",     cmd_pci_h},
    {"vesa",    cmd_vesa_h},
    {"mouse",   cmd_mouse_h},
    {"net",     cmd_net_h},
    {"ser",     cmd_ser_h},
    {"beep",    cmd_beep_h},
    {NULL,      NULL}
};

void Processar_Comando_Terminal(char* input) {
    if (strncmp(input, "cd", 2) == 0 && input[2] != ' ' && input[2] != 0) {
        int len = (int)strlen(input);
        if (len + 1 < CMD_BUFFER_SIZE) {
            for (int k = len + 1; k > 2; k--) input[k] = input[k - 1];
            input[2] = ' ';
        }
    }

    char* argv[MAX_ARGS];
    int argc = tokenize_line(input, argv);
    if (argc == 0) return;

    for (int i = 0; g_cmd_table[i].name != NULL; i++) {
        if (strcmp(argv[0], g_cmd_table[i].name) == 0) {
            g_cmd_table[i].handler(argc, argv);
            return;
        }
    }

    char m[64];
    strcpy(m, "comando desconhecido: "); strcat(m, argv[0]); strcat(m, "  (digite help)");
    t_line(m);
}

/* ---------------- redirecao + execucao ---------------- */
static void parse_and_run(char* line) {
    g_redir = 0; g_redir_len = 0;
    char* p = strstr(line, ">>");
    int mode = 2;
    if (!p) { p = strchr(line, '>'); mode = 1; }
    if (p) {
        *p = 0;
        char* f = p + (mode == 2 ? 2 : 1);
        while (*f == ' ') f++;
        int lt = (int)strlen(line);
        while (lt > 0 && line[lt - 1] == ' ') line[--lt] = 0;
        if (f[0] && line[0]) {
            g_redir = mode;
            strncpy(g_redir_file, f, 63); g_redir_file[63] = 0;
        }
    }
    if (!line[0]) { g_redir = 0; return; }
    Processar_Comando_Terminal(line);
    if (g_redir) {
        int rc;
        if (g_redir == 1) rc = sys_fat_write(g_redir_file, g_redir_buf, (uint32_t)g_redir_len);
        else              rc = sys_fat_append(g_redir_file, g_redir_buf, (uint32_t)g_redir_len);
        g_redir = 0;
        char m[128]; char nn[12];
        itoa((uint64_t)(uint32_t)g_redir_len, nn, 10);
        strcpy(m, (rc == 0) ? "redirect ok: " : "redirect FALHOU: ");
        strcat(m, nn); strcat(m, " bytes -> "); strcat(m, g_redir_file);
        t_line(m);
    }
}

/* ---------------- AUTOEXEC.TXT ---------------- */
static void rodar_autoexec(void) {
    file_info_t fi;
    if (sys_fat_stat("AUTOEXEC.TXT", &fi) != 0 || fi.size == 0) return;
    static char buf[4096];
    uint32_t want = (fi.size > 4094) ? 4094 : fi.size;
    int n = sys_fat_read("AUTOEXEC.TXT", buf, want);
    if (n <= 0) return;
    buf[n] = 0;
    t_line("--- AUTOEXEC.TXT encontrado: executando ---");
    char* p = buf;
    while (p && *p) {
        char* nl = strchr(p, '\n');
        if (nl) *nl = 0;
        int l = (int)strlen(p);
        if (l && p[l - 1] == '\r') p[l - 1] = 0;
        char* q = p; while (*q == ' ' || *q == '\t') q++;
        if (*q && *q != '#' && *q != ';') {
            t_line(q);
            parse_and_run(q);
        }
        p = nl ? nl + 1 : 0;
    }
    t_line("--- fim do AUTOEXEC ---");
}

/* ---------------- main ---------------- */
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;

    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("Terminal LBF", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Terminal LBF v4.4", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000);

    GUI_CreateLabel(&MyApp, 10, 42, "LBF-VESA Console v4.4 - C:/ | help | history | > ARQ | AUTOEXEC");
    TerminalMemo = GUI_CreateMemo(&MyApp, 10, 68, 700, 390);
    gui_set_prop(TerminalMemo, PROP_COLOR, 0x000000);
    t_line("Terminal LBF v4.4 [console nativo - Dispatch Table + Parser]");
    t_line("digite help para a lista; help <cmd> para detalhe.");

    rodar_autoexec();

    char pr[160]; prompt_path(pr); strcat(pr, "> ");
    memo_raw(pr);

    g_focused_control = (void*)TerminalMemo;
    gui_set_prop(TerminalMemo, PROP_SET_FOCUS, 1);
    memset(cmd_buffer, 0, CMD_BUFFER_SIZE);
    Flush_Grafico_Janela();

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) { Tratar_Fechamento_Software(); break; }
        bool euTenhoFoco = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFoco;
        bool precisa_redesenhar = false;
        if (primeiro_desenho) { primeiro_desenho = false; precisa_redesenhar = true; }
        if (euTenhoFoco != ultimo_estado_foco) { ultimo_estado_foco = euTenhoFoco; precisa_redesenhar = true; }
        if (euTenhoFoco) {
            char key = Obter_Tecla_Entrada();
            if (key != 0) {
                precisa_redesenhar = true;
                if (key == '\n' || key == '\r') {
                    cmd_buffer[cmd_idx] = '\0';
                    char* line = cmd_buffer;
                    while (*line == ' ') line++;
                    int ll = (int)strlen(line);
                    while (ll > 0 && line[ll - 1] == ' ') line[--ll] = 0;
                    memo_raw("\n");
                    if (ll > 0) {
                        char expanded[CMD_BUFFER_SIZE];
                        expanded[0] = 0;
                        if (strcmp(line, "!!") == 0) {
                            if (g_hist_count > 0) strcpy(expanded, g_hist[g_hist_count - 1]);
                            else t_line("history vazio.");
                        } else if (line[0] == '!' && line[1] >= '1' && line[1] <= '9') {
                            int idxh = line[1] - '1';
                            if (idxh < g_hist_count) strcpy(expanded, g_hist[idxh]);
                            else t_line("indice de history invalido.");
                        } else strcpy(expanded, line);
                        if (expanded[0]) {
                            if (strcmp(expanded, line) != 0) { memo_raw(expanded); memo_raw("\n"); }
                            hist_record(expanded);
                            parse_and_run(expanded);
                        }
                    }
                    char pr[160]; prompt_path(pr); strcat(pr, "> ");
                    memo_raw(pr);
                    cmd_idx = 0;
                    memset(cmd_buffer, 0, CMD_BUFFER_SIZE);
                }
                else if ((key == '\b' || key == 8) && cmd_idx > 0) {
                    cmd_idx--;
                    cmd_buffer[cmd_idx] = '\0';
                    char bs[2] = { '\b', '\0' };
                    memo_raw(bs);
                }
                else if (cmd_idx < CMD_BUFFER_SIZE - 1 && key >= 32 && key <= 126) {
                    cmd_buffer[cmd_idx++] = key;
                    char t[2] = { key, '\0' };
                    memo_raw(t);
                }
            }
            if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
                if (mouse_hold_timer == 0) {
                    int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                    int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                    ultimo_x = rel_x; ultimo_y = rel_y; mouse_hold_timer = 2;
                    events_process_mouse(rel_x, rel_y, 1, 0);
                    if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) precisa_redesenhar = true;
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
        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(euTenhoFoco ? 16 : 32);
    }
    sys_exit();
    return 0;
}
