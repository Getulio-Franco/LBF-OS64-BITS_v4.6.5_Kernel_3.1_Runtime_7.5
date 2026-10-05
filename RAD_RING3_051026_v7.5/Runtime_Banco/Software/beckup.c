/* ============================================================================
LBF BANCO BACKUP v2.4 - Snapshot/Restore do Runtime_Banco com QUARENTENA
v2.4 (correcao real do copy FALHOU):
  - LEITURA sempre INTEIRA via sys_fat_read (caminho comprovado: gfile/xcopy
    v3.3/cat/chk usam fat32_read_file). NUNCA sys_fat_read_at p/ conteudo.
  - ESCRITA em fatias: 1a fatia sys_fat_write("DIR/arq") (write_file resolve
    subpasta); fatias seguintes sys_fat_write_at com cwd no dir (nome puro).
  - copy_snap_to_root idem: read inteiro no snap, write/write_at na raiz.
  - Quarentena agora usa copy_root_to_dir (nao sys_fat_copy).
Mantem: cmp_snap v2.3 (read inteiro + diagnostico), JA IDENTICO no restore,
sondas R0/R1, MANIFEST via chdir, cwd="/" em todo handler.
============================================================================ */
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "../system/sysutils.h"
#include "Runtime_sdk/components/TOS_IPC.h"

void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth  = 640;
const int winHeight = 520;

TGUIControl* EditBanco    = NULL;
TGUIControl* BtnListar    = NULL;
TGUIControl* EditSnap     = NULL;
TGUIControl* BtnSnapshot  = NULL;
TGUIControl* BtnVerify    = NULL;
TGUIControl* BtnRestaurar = NULL;
TGUIControl* LabelStatus  = NULL;
TGUIControl* MemoLog      = NULL;

#define MAX_ARQ 24
static char g_arq[MAX_ARQ][16];

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
static void set_status(const char* m) { if (LabelStatus) GUI_Edit_SetText(LabelStatus, m); }
static int dentro(TGUIControl* c, int x, int y) {
    return c && x >= c->Left && x < (c->Left + c->Width) && y >= c->Top && y < (c->Top + c->Height);
}
/* ---------------- helpers ---------------- */
static void upper_copy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        dst[i] = c; i++;
    }
    dst[i] = 0;
}
static void seq_name(char* out, const char* prefix, int n) {
    strcpy(out, prefix);
    char d[5];
    d[0] = (char)('0' + (n / 1000) % 10);
    d[1] = (char)('0' + (n / 100) % 10);
    d[2] = (char)('0' + (n / 10) % 10);
    d[3] = (char)('0' + n % 10);
    d[4] = 0;
    strcat(out, d);
}
static int fs_contar_raiz(void) {
    sys_fat_chdir("/");
    char nome[64]; file_info_t fi;
    int idx = 0, n = 0;
    while (idx < 500 && sys_fat_readdir(idx, nome, &fi) == 1) {
        idx++;
        if (strcmp(nome, ".") == 0 || strcmp(nome, "..") == 0) continue;
        n++;
    }
    sys_fat_chdir("/");
    return n;
}
static int max_seq(const char* prefix) {
    sys_fat_chdir("/");
    char nome[64]; file_info_t fi;
    int idx = 0, maxv = 0;
    int plen = (int)strlen(prefix);
    while (idx < 500 && sys_fat_readdir(idx, nome, &fi) == 1) {
        idx++;
        if (!(fi.attributes & 0x10)) continue;
        if (strncmp(nome, prefix, plen) != 0) continue;
        int v = 0, digits = 0;
        for (const char* p = nome + plen; *p; p++) {
            if (*p < '0' || *p > '9') { digits = -1; break; }
            v = v * 10 + (*p - '0'); digits++;
        }
        if (digits > 0 && v > maxv) maxv = v;
    }
    sys_fat_chdir("/");
    return maxv;
}
static int descobrir_arquivos(const char* banco) {
    sys_fat_chdir("/");
    int n = 0;
    char cat[16];
    upper_copy(cat, banco, 8);
    strcat(cat, ".CAT");
    file_info_t fi;
    if (sys_fat_stat(cat, &fi) == 0) strcpy(g_arq[n++], cat);
    char base4[5];
    upper_copy(base4, banco, 4);
    char nome[64];
    int idx = 0;
    while (idx < 500 && sys_fat_readdir(idx, nome, &fi) == 1) {
        idx++;
        if (fi.attributes & 0x10) continue;
        if (strncmp(nome, base4, 4) != 0) continue;
        int L = (int)strlen(nome);
        if (L >= 4 && strcmp(nome + L - 4, ".TBL") == 0 && n < MAX_ARQ)
            strcpy(g_arq[n++], nome);
    }
    sys_fat_chdir("/");
    return n;
}
/* ---------------- comparador (v2.3: read inteiro + diagnostico) ---------------- */
static int cmp_snap(const char* snap, const char* name, uint32_t* sa, uint32_t* sb) {
    file_info_t fa, fb;
    if (sa) *sa = 0;
    if (sb) *sb = 0;
    char m[160]; char num[12];
    sys_fat_chdir("/");
    if (sys_fat_stat(name, &fa) != 0) {
        log_line("cmp: stat RAIZ falhou");
        return 0;
    }
    if (sys_fat_chdir(snap) != 0) {
        strcpy(m, "cmp: chdir snap falhou: "); strcat(m, snap); log_line(m);
        sys_fat_chdir("/");
        if (sa) *sa = fa.size;
        return 0;
    }
    if (sys_fat_stat(name, &fb) != 0) {
        log_line("cmp: stat SNAP falhou");
        sys_fat_chdir("/");
        if (sa) *sa = fa.size;
        return 0;
    }
    if (sa) *sa = fa.size;
    if (sb) *sb = fb.size;
    if (fa.size != fb.size) {
        strcpy(m, "cmp: tamanhos diferem raiz="); itoa(fa.size, num, 10); strcat(m, num);
        strcat(m, " snap="); itoa(fb.size, num, 10); strcat(m, num);
        log_line(m);
        sys_fat_chdir("/");
        return 0;
    }
    static uint8_t ba[4096], bb[4096];
    sys_fat_chdir(snap);
    int rb = sys_fat_read(name, bb, 4096);
    sys_fat_chdir("/");
    int ra = sys_fat_read(name, ba, 4096);
    if (rb != (int)fa.size) {
        strcpy(m, "cmp: leitura SNAP devolveu "); itoa((uint64_t)(uint32_t)rb, num, 10);
        strcat(m, num); strcat(m, " de "); itoa(fa.size, num, 10); strcat(m, num);
        log_line(m);
        return 0;
    }
    if (ra != (int)fa.size) {
        strcpy(m, "cmp: leitura RAIZ devolveu "); itoa((uint64_t)(uint32_t)ra, num, 10);
        strcat(m, num); strcat(m, " de "); itoa(fa.size, num, 10); strcat(m, num);
        log_line(m);
        return 0;
    }
    if (memcmp(ba, bb, (uint32_t)ra) != 0) {
        uint32_t d = 0;
        while (d < (uint32_t)ra && ba[d] == bb[d]) d++;
        strcpy(m, "cmp: CONTEUDO difere no offset "); itoa(d, num, 10); strcat(m, num);
        log_line(m);
        return 0;
    }
    sys_fat_chdir("/");
    return 1;
}
/* ---------------- COPIA raiz/name -> dir/name (v2.4: read INTEIRO) ---------------- */
static int copy_root_to_dir(const char* name, const char* dir) {
    sys_fat_chdir("/");
    file_info_t fs;
    if (sys_fat_stat(name, &fs) != 0) return 0;
    uint8_t* buf = (uint8_t*)malloc(fs.size ? fs.size : 1);
    if (!buf) return 0;
    int rd = sys_fat_read(name, buf, fs.size);          /* leitura inteira comprovada */
    if (rd != (int)fs.size) { free(buf); return 0; }
    char dest[32];
    strcpy(dest, dir); strcat(dest, "/"); strcat(dest, name);
    uint32_t off = 0; int first = 1; int ok = 1;
    while (off < fs.size || first) {
        uint32_t n = fs.size - off; if (n > 4096) n = 4096;
        if (first) {
            if (sys_fat_write(dest, buf, n) != 0) { ok = 0; break; }   /* write_file resolve subdir */
            first = 0;
        } else {
            sys_fat_chdir(dir);
            if (sys_fat_write_at(name, buf + off, n, off) != 0) { sys_fat_chdir("/"); ok = 0; break; }
            sys_fat_chdir("/");
        }
        off += n;
    }
    free(buf);
    sys_fat_chdir("/");
    return ok;
}
/* ---------------- COPIA snap/name -> raiz/name (v2.4: read INTEIRO) ---------------- */
static int copy_snap_to_root(const char* snap, const char* name) {
    sys_fat_chdir(snap);
    file_info_t fs;
    if (sys_fat_stat(name, &fs) != 0) { sys_fat_chdir("/"); return 0; }
    uint8_t* buf = (uint8_t*)malloc(fs.size ? fs.size : 1);
    if (!buf) { sys_fat_chdir("/"); return 0; }
    int rd = sys_fat_read(name, buf, fs.size);
    if (rd != (int)fs.size) { free(buf); sys_fat_chdir("/"); return 0; }
    uint32_t off = 0; int first = 1; int ok = 1;
    sys_fat_chdir("/");
    while (off < fs.size || first) {
        uint32_t n = fs.size - off; if (n > 4096) n = 4096;
        if (first) {
            if (sys_fat_write(name, buf, n) != 0) { ok = 0; break; }
            first = 0;
        } else {
            if (sys_fat_write_at(name, buf + off, n, off) != 0) { ok = 0; break; }
        }
        off += n;
    }
    free(buf);
    sys_fat_chdir("/");
    return ok;
}
static void snap_atual(char* out) {
    char* t = GUI_Edit_GetText(EditSnap);
    if (t && t[0]) { strcpy(out, t); return; }
    int m = max_seq("BAK");
    if (m == 0) { out[0] = 0; return; }
    seq_name(out, "BAK", m);
}
/* ---------------- eventos ---------------- */
void OnBtnListarClick(void* s) {
    (void)s;
    sys_fat_chdir("/");
    log_line("--- SNAPSHOTS / QUARENTENAS ---");
    char nome[64]; file_info_t fi;
    int idx = 0;
    while (idx < 500 && sys_fat_readdir(idx, nome, &fi) == 1) {
        idx++;
        if (!(fi.attributes & 0x10)) continue;
        if (strncmp(nome, "BAK", 3) == 0 || strncmp(nome, "QUAR", 4) == 0) {
            char m[96];
            strcpy(m, "  [DIR] "); strcat(m, nome);
            log_line(m);
        }
    }
    char snap[12]; snap_atual(snap);
    if (!snap[0]) { log_line("(nenhum snapshot ainda)"); set_status("sem snapshots"); sys_fat_chdir("/"); return; }
    char m[96]; strcpy(m, "--- conteudo de "); strcat(m, snap); strcat(m, " ---"); log_line(m);
    if (sys_fat_chdir(snap) == 0) {
        idx = 0;
        while (idx < 500 && sys_fat_readdir(idx, nome, &fi) == 1) {
            idx++;
            if (fi.attributes & 0x10) continue;
            char mm[96]; char sz[12];
            strcpy(mm, "  "); strcat(mm, nome); strcat(mm, "  ");
            itoa(fi.size, sz, 10); strcat(mm, sz); strcat(mm, " B");
            log_line(mm);
        }
    }
    sys_fat_chdir("/");
    set_status(snap);
}
void OnBtnSnapshotClick(void* s) {
    (void)s;
    sys_fat_chdir("/");
    char* banco = GUI_Edit_GetText(EditBanco);
    if (!banco || !banco[0]) { log_line("Nome do banco vazio!"); return; }
    int R0 = fs_contar_raiz();
    if (R0 == 0) { log_line("ALERTA: raiz sem entradas legiveis - abortando p/ nao piorar."); return; }
    int n = descobrir_arquivos(banco);
    if (n == 0) { log_line("Nenhum .CAT/.TBL encontrado para este banco."); return; }
    char num[8]; itoa((uint64_t)(uint32_t)n, num, 10);
    char m[128]; strcpy(m, "Arquivos do banco: "); strcat(m, num); log_line(m);
    int seq = max_seq("BAK") + 1;
    char snap[12]; seq_name(snap, "BAK", seq);
    if (sys_fat_mkdir(snap) != 0) { log_line("Falha ao criar o diretorio do snapshot."); sys_fat_chdir("/"); return; }
    log_line("--- SNAPSHOT ---");
    strcpy(m, "Criando "); strcat(m, snap); strcat(m, " ..."); log_line(m);
    int ok = 0;
    for (int i = 0; i < n; i++) {
        int rc = copy_root_to_dir(g_arq[i], snap);      /* v2.4: read inteiro + write fatiado */
        strcpy(m, "  copy "); strcat(m, g_arq[i]); strcat(m, rc ? " OK" : " FALHOU");
        log_line(m);
        if (rc) ok++;
    }
    /* MANIFEST via chdir + nome puro */
    static char man[1024];
    man[0] = 0;
    strcat(man, "BANCO: "); strcat(man, banco);
    strcat(man, "\nSNAP: "); strcat(man, snap); strcat(man, "\n");
    for (int i = 0; i < n; i++) {
        file_info_t fi;
        sys_fat_chdir("/");
        if (sys_fat_stat(g_arq[i], &fi) == 0) {
            strcat(man, g_arq[i]); strcat(man, "  ");
            char sz[12]; itoa(fi.size, sz, 10); strcat(man, sz); strcat(man, " B\n");
        }
    }
    sys_fat_chdir(snap);
    sys_fat_write("MANIFEST.TXT", man, (uint32_t)strlen(man));
    sys_fat_chdir("/");
    /* VERIFY */
    log_line("--- VERIFY ---");
    int vok = 0;
    for (int i = 0; i < n; i++) {
        uint32_t sa, sb;
        int ig = cmp_snap(snap, g_arq[i], &sa, &sb);
        strcpy(m, "  "); strcat(m, g_arq[i]);
        if (ig) strcat(m, " IDENTICO");
        else {
            char a[12], b[12];
            strcat(m, " DIVERGE (raiz="); itoa(sa, a, 10); strcat(m, a);
            strcat(m, " snap="); itoa(sb, b, 10); strcat(m, b); strcat(m, ")");
        }
        log_line(m);
        if (ig) vok++;
    }
    itoa((uint64_t)(uint32_t)vok, num, 10);
    strcpy(m, "Verificados: "); strcat(m, num); strcat(m, "/");
    itoa((uint64_t)(uint32_t)n, num, 10); strcat(m, num);
    log_line(m);
    int R1 = fs_contar_raiz();
    if (R1 < R0) {
        log_line("!!! ALERTA FAT: a raiz PERDEU entradas durante o snapshot !!!");
        log_line("Causa provavel: FAT[2] (cluster da raiz) estava livre antes.");
    }
    if (vok == n && ok == n && R1 >= R0) {
        strcpy(m, "SNAPSHOT "); strcat(m, snap); strcat(m, " COMPLETO E VERIFICADO.");
        log_line(m);
        GUI_Edit_SetText(EditSnap, snap);
        set_status(snap);
    } else {
        log_line("SNAPSHOT INCOMPLETO - NAO use para restore!");
        set_status("snapshot FALHOU");
    }
    sys_fat_chdir("/");
}
void OnBtnVerifyClick(void* s) {
    (void)s;
    sys_fat_chdir("/");
    char snap[12]; snap_atual(snap);
    if (!snap[0]) { log_line("Nenhum snapshot disponivel."); return; }
    char m[128]; strcpy(m, "--- VERIFY vs "); strcat(m, snap); strcat(m, " ---"); log_line(m);
    char* banco = GUI_Edit_GetText(EditBanco);
    int n = descobrir_arquivos(banco);
    int vok = 0;
    for (int i = 0; i < n; i++) {
        uint32_t sa, sb;
        int ig = cmp_snap(snap, g_arq[i], &sa, &sb);
        strcpy(m, "  "); strcat(m, g_arq[i]);
        if (ig) strcat(m, " IDENTICO");
        else {
            char a[12], b[12];
            strcat(m, " DIVERGE (raiz="); itoa(sa, a, 10); strcat(m, a);
            strcat(m, " snap="); itoa(sb, b, 10); strcat(m, b); strcat(m, ")");
        }
        log_line(m);
        if (ig) vok++;
    }
    char num[8];
    itoa((uint64_t)(uint32_t)vok, num, 10);
    strcpy(m, "Identicos: "); strcat(m, num); strcat(m, "/");
    itoa((uint64_t)(uint32_t)n, num, 10); strcat(m, num);
    log_line(m);
    set_status((vok == n) ? "verify OK" : "verify DIVERGE");
    sys_fat_chdir("/");
}
void OnBtnRestaurarClick(void* s) {
    (void)s;
    sys_fat_chdir("/");
    char snap[12]; snap_atual(snap);
    if (!snap[0]) { log_line("Nenhum snapshot disponivel."); return; }
    char* banco = GUI_Edit_GetText(EditBanco);
    /* 1) QUARENTENA obrigatoria (v2.4: copy_root_to_dir) */
    int n = descobrir_arquivos(banco);
    if (n > 0) {
        int qseq = max_seq("QUAR") + 1;
        char quar[12]; seq_name(quar, "QUAR", qseq);
        if (sys_fat_mkdir(quar) == 0) {
            char m[128];
            strcpy(m, "QUARENTENA: estado atual guardado em "); strcat(m, quar);
            log_line(m);
            for (int i = 0; i < n; i++) copy_root_to_dir(g_arq[i], quar);
        } else {
            log_line("AVISO: falha ao criar quarentena - restore ABORTADO por seguranca.");
            sys_fat_chdir("/");
            return;
        }
    }
    /* 2) nomes dentro do snap */
    if (sys_fat_chdir(snap) != 0) { log_line("Snapshot nao encontrado."); sys_fat_chdir("/"); return; }
    char nomes[32][16]; int qtd = 0;
    char nome[64]; file_info_t fi;
    int idx = 0;
    while (idx < 500 && sys_fat_readdir(idx, nome, &fi) == 1) {
        idx++;
        if (fi.attributes & 0x10) continue;
        if (strcmp(nome, "MANIFEST.TXT") == 0) continue;
        if (qtd < 32) strcpy(nomes[qtd++], nome);
    }
    sys_fat_chdir("/");
    /* 3) restore com atalho JA IDENTICO */
    log_line("--- RESTORE ---");
    char m[128];
    int rok = 0;
    for (int i = 0; i < qtd; i++) {
        uint32_t sa, sb;
        if (cmp_snap(snap, nomes[i], &sa, &sb)) {
            strcpy(m, "  restore "); strcat(m, nomes[i]);
            strcat(m, " JA IDENTICO (nada a gravar)");
            log_line(m);
            rok++;
            continue;
        }
        int rc = copy_snap_to_root(snap, nomes[i]);
        strcpy(m, "  restore "); strcat(m, nomes[i]); strcat(m, rc ? " OK" : " FALHOU");
        log_line(m);
        if (rc) rok++;
    }
    /* 4) verify pos-restore */
    log_line("--- VERIFY POS-RESTORE ---");
    int vok = 0;
    for (int i = 0; i < qtd; i++) {
        uint32_t sa, sb;
        int ig = cmp_snap(snap, nomes[i], &sa, &sb);
        strcpy(m, "  "); strcat(m, nomes[i]); strcat(m, ig ? " IDENTICO" : " DIVERGE!");
        log_line(m);
        if (ig) vok++;
    }
    log_line("RESTORE CONCLUIDO (estado anterior esta na quarentena).");
    set_status((vok == qtd && rok == qtd) ? "restore OK" : "restore DIVERGE");
    sys_fat_chdir("/");
}
/* ---------------- main ---------------- */
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;
    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("LBF Banco Backup", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "LBF Banco Backup v2.4", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0xC0C0C0);
    GUI_CreateLabel(&MyApp, 10, 42, "Banco:");
    EditBanco = GUI_CreateEdit(&MyApp, 60, 38, 140, 25, "CADASTRO", NULL);
    BtnListar = GUI_CreateButton(&MyApp, 210, 38, 135, 25, "LISTAR SNAPS", OnBtnListarClick);
    GUI_CreateLabel(&MyApp, 355, 42, "Snap:");
    EditSnap  = GUI_CreateEdit(&MyApp, 400, 38, 100, 25, "", NULL);
    BtnSnapshot  = GUI_CreateButton(&MyApp, 10,  68, 95,  25, "SNAPSHOT",  OnBtnSnapshotClick);
    BtnVerify    = GUI_CreateButton(&MyApp, 115, 68, 75,  25, "VERIFY",    OnBtnVerifyClick);
    BtnRestaurar = GUI_CreateButton(&MyApp, 200, 68, 105, 25, "RESTAURAR", OnBtnRestaurarClick);
    LabelStatus  = GUI_CreateLabel(&MyApp, 315, 72, "pronto");
    MemoLog = GUI_CreateMemo(&MyApp, 10, 98, 620, 412);
    gui_set_prop(MemoLog, PROP_COLOR, 0x000000);
    GUI_Memo_AddStr(MemoLog, "LBF Banco Backup v2.4 pronto.\n");
    GUI_Memo_AddStr(MemoLog, "Leitura INTEIRA (sys_fat_read) + escrita fatiada comprovada.\n\n");
    g_focused_control = (void*)EditBanco;
    ultimo_controle_focado = (void*)EditBanco;
    gui_set_prop(EditBanco, PROP_SET_FOCUS, 1);
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
                    if      (dentro(BtnListar,    rel_x, rel_y)) gui_set_prop(BtnListar,    PROP_STATE, 2);
                    else if (dentro(BtnSnapshot,  rel_x, rel_y)) gui_set_prop(BtnSnapshot,  PROP_STATE, 2);
                    else if (dentro(BtnVerify,    rel_x, rel_y)) gui_set_prop(BtnVerify,    PROP_STATE, 2);
                    else if (dentro(BtnRestaurar, rel_x, rel_y)) gui_set_prop(BtnRestaurar, PROP_STATE, 2);
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
                    if (BtnListar)    gui_set_prop(BtnListar,    PROP_STATE, 0);
                    if (BtnSnapshot)  gui_set_prop(BtnSnapshot,  PROP_STATE, 0);
                    if (BtnVerify)    gui_set_prop(BtnVerify,    PROP_STATE, 0);
                    if (BtnRestaurar) gui_set_prop(BtnRestaurar, PROP_STATE, 0);
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
