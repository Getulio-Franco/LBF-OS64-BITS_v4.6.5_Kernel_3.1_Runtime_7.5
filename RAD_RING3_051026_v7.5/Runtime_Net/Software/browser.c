/*
HISTÓRICO DE COLABORAÇÃO
Arquivo: browser.c
Versão: 6.5 (Headers Flexíveis, SVG Filter, Net Content-Length)
Data: 03/10/2026
Mudanças v6.5 (sobre v6.4):
  - PARSER HTML: Adicionada flag in_svg para ocultar conteúdo vetorial (<svg>...</svg>).
  - REDE: net_fetch() agora faz parse do Content-Length e encerra a leitura 
    imediatamente sem aguardar timeout.
  - HTTP HEADERS: find_http_body() trata tanto \r\n\r\n quanto \n\n p/ isolar o body.
  - FOCO adaptativo do v5 mantido intocado.
*/
#include "Runtime_sdk/sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "../system/sysutils.h"
#include "Runtime_sdk/components/TOS_IPC.h"
/* Pilha de Rede Ring 3 */
#include "net_user/net_utils.h"
#include "net_user/net_interface.h"
#include "net_user/net_poll.h"
#include "net_user/arp.h"
#include "net_user/ip.h"
#include "net_user/dhcp.h"
#include "net_user/dns.h"
#include "net_user/socket.h"
/* Camada Syscall TLS 1.3 */
#include "Runtime_Net/sys_tls13/sys_tls13.h"

/* Protótipos RAD */
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

/* Protótipos web_html.c */
extern WebDoc* webdoc_create(void);
extern void    webdoc_destroy(WebDoc* doc);
extern void    webdoc_add_line(WebDoc* doc, const char* text, uint8_t style, int link_id, int img_slot);
extern int     webdoc_add_link(WebDoc* doc, const char* url);
extern int     webdoc_add_image(WebDoc* doc, const char* src);
extern void    webdoc_finalize(WebDoc* doc);
extern int     webimg_decode(const uint8_t* data, int len, uint32_t** out_px, int* out_w, int* out_h);

/* Protótipos locais */
void Flush_Grafico_Janela(void);
void gui_render_form(TForm* form); 
void Tratar_Fechamento_Software(void);
char Obter_Tecla_Entrada(void);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth  = 620;
const int winHeight = 620;
TGUIControl* EditURL    = NULL;
TGUIControl* BtnGo      = NULL;
TGUIControl* BtnBack    = NULL;
TGUIControl* BtnRefresh = NULL;
TGUIControl* BtnSave    = NULL;
TGUIControl* LblStatus  = NULL;
TGUIControl* WebPage    = NULL;
TGUIControl* LogMemo    = NULL;

static char g_rx[200 * 1024];
static char g_chunk[512]; 
static char* g_body = NULL;
static int   g_body_len = 0;
static char  g_host[128];
static WebDoc* g_doc = NULL;

#define MAX_HISTORY 10
static char g_history[MAX_HISTORY][192];
static int  g_history_top = 0;
static bool g_net_ready = false;
static uint32_t g_dns_server = 0x0302000A;

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

/* ============================================================================
HELPERS BÁSICOS
============================================================================ */
static size_t safe_strcpy(char* dest, const char* src, size_t max_size) {
    if (!dest || max_size == 0) return 0;
    size_t i = 0;
    if (src) { while (src[i] != '\0' && i < (max_size - 1)) { dest[i] = src[i]; i++; } }
    dest[i] = '\0';
    return i;
}

static size_t safe_strcat(char* dest, const char* src, size_t max_size, size_t current_len) {
    if (!dest || !src || current_len >= max_size) return current_len;
    while (*src && current_len < (max_size - 1)) dest[current_len++] = *src++;
    dest[current_len] = '\0';
    return current_len;
}

static const char* local_strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return haystack;
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*n && *h == *n) { h++; n++; }
        if (!*n) return haystack;
    }
    return 0;
}

static bool starts_with_ci(const char* s, const char* pre) {
    while (*pre) {
        char a = *s, b = *pre;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
        s++; pre++;
    }
    return true;
}

/* Novo Helper: Encontra início do Body lidando com diferentes quebras */
static const char* find_http_body(const char* buf) {
    if (!buf) return NULL;
    const char* p1 = local_strstr(buf, "\r\n\r\n");
    const char* p2 = local_strstr(buf, "\n\n");
    
    if (p1 && p2) return (p1 < p2) ? (p1 + 4) : (p2 + 2);
    if (p1) return p1 + 4;
    if (p2) return p2 + 2;
    return NULL;
}

static uint32_t parse_ip(const char* ip_str) {
    if (!ip_str) return 0;
    uint32_t bytes[4] = {0};
    int byte_idx = 0, current_val = 0;
    bool has_digit = false;
    while (*ip_str) {
        if (*ip_str >= '0' && *ip_str <= '9') {
            current_val = (current_val * 10) + (*ip_str - '0');
            if (current_val > 255) return 0;
            has_digit = true;
        } else if (*ip_str == '.') {
            if (!has_digit || byte_idx >= 3) return 0;
            bytes[byte_idx++] = current_val;
            current_val = 0; has_digit = false;
        } else return 0;
        ip_str++;
    }
    if (!has_digit || byte_idx != 3) return 0;
    bytes[3] = current_val;
    return (bytes[0]) | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
}

static bool parse_url(const char* url, char* host_out, size_t host_max,
                      char* path_out, size_t path_max, bool* is_tls_out) {
    if (!url || !host_out || !path_out || !is_tls_out) return false;
    const char* p = url;
    *is_tls_out = false;
    if (starts_with_ci(p, "https://")) {
        p += 8;
        *is_tls_out = true;
    } else if (starts_with_ci(p, "http://")) {
        p += 7;
        *is_tls_out = false;
    }
    size_t hi = 0;
    while (*p && *p != '/' && hi < host_max - 1) host_out[hi++] = *p++;
    host_out[hi] = '\0';
    size_t pi = 0;
    if (*p == '/') { while (*p && pi < path_max - 1) path_out[pi++] = *p++; }
    else path_out[pi++] = '/';
    path_out[pi] = '\0';
    return (hi > 0);
}

static int http_status_of(const char* buf) {
    if (!buf || !(buf[0]=='H' && buf[1]=='T' && buf[2]=='T' && buf[3]=='P')) return -1;
    const char* p = buf;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    int st = 0;
    while (*p >= '0' && *p <= '9') { st = st * 10 + (*p - '0'); p++; }
    return st;
}

static void resolve_url(const char* href, char* out, size_t max) {
    if (starts_with_ci(href, "http://") || starts_with_ci(href, "https://")) {
        safe_strcpy(out, href, max);
    } else if (href[0] == '/') {
        size_t l = safe_strcpy(out, g_host, max);
        safe_strcat(out, href, max, l);
    } else {
        size_t l = safe_strcpy(out, g_host, max);
        l = safe_strcat(out, "/", max, l);
        safe_strcat(out, href, max, l);
    }
}

static void Label_SetText(TGUIControl* lbl, const char* text) {
    if (!lbl || !lbl->KernelHandle || !text) return;
    gui_set_prop((void*)lbl->KernelHandle, PROP_CAPTION, (uintptr_t)text);
}

/* ============================================================================
DECHUNK & ENTIDADES HTML
============================================================================ */
static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int http_dechunk_inplace(char* body, int len) {
    if (!body || len <= 0) return 0;
    int r = 0, w = 0;
    while (r < len) {
        int chunk_size = 0;
        while (r < len) {
            char c = body[r++];
            if (c == '\r') { if (r < len && body[r] == '\n') r++; break; }
            if (c == '\n') break;
            if (c == ';') { while (r < len && body[r] != '\n') r++; if (r < len) r++; break; }
            int hv = hex_value(c);
            if (hv < 0) return w;
            chunk_size = (chunk_size << 4) | hv;
        }
        if (chunk_size == 0) break;
        if (r + chunk_size > len) chunk_size = len - r;
        for (int i = 0; i < chunk_size; i++) body[w++] = body[r++];
        if (r < len && body[r] == '\r') r++;
        if (r < len && body[r] == '\n') r++;
    }
    return w;
}

static uint8_t pack_cp(uint32_t cp) {
    switch (cp) {
        case 0xE7: return 0x87; case 0xC7: return 0x80;
        case 0xE1: return 0xA0; case 0xC1: return 0xC1;
        case 0xE9: return 0x82; case 0xC9: return 0x90;
        case 0xED: return 0xA1; case 0xCD: return 0xCD;
        case 0xF3: return 0xA2; case 0xD3: return 0xD3;
        case 0xFA: return 0xA3; case 0xDA: return 0xDA;
        case 0xE2: return 0x83; case 0xEA: return 0x88;
        case 0xEE: return 0x8C; case 0xF4: return 0x93;
        case 0xFB: return 0x96; case 0xE0: return 0x85;
        case 0xFC: return 0x81; case 0xDC: return 0x9A;
        case 0xE4: return 0x84; case 0xC4: return 0x8E;
        case 0xEB: return 0x89; case 0xEF: return 0x8B;
        case 0xF6: return 0x94; case 0xD6: return 0x99;
        case 0xE3: return 0xE3; case 0xC3: return 0xC3;
        case 0xF5: return 0xF5; case 0xD5: return 0xD5;
        case 0xE8: return 0x8A; case 0xEC: return 0x8D;
        case 0xF2: return 0x95; case 0xF9: return 0x97;
        case 0xE5: return 0x86; case 0xC5: return 0x8F;
        case 0xE6: return 0x91; case 0xC6: return 0x92;
        case 0xF1: return 0xA4; case 0xD1: return 0xA5;
        case 0xFF: return 0x98; case 0xB4: return 0xB4;
        case 0xA9: return 0xA9; case 0xAE: return 0xAE;
        case 0xB0: return 0xB0; case 0xB1: return 0xB1;
        case 0xB2: return 0xB2; case 0xB3: return 0xB3;
        case 0xB5: return 0xB5; case 0xBF: return 0xBF;
        case 0xB7: return 0xB7; case 0xA7: return 0xA7;
        case 0xAA: return 0xAA; case 0xBA: return 0xBA;
        case 0xAB: return 0xAB; case 0xBB: return 0xBB;
        case 0xAC: return 0xAC; case 0xA1: return 0xA6;
        case 0xA0: return ' ';
        default:
            if (cp < 0x80) return (uint8_t)cp;
            if (cp >= 0xC0 && cp <= 0xDE) return (uint8_t)(cp - 0x20);
            return '?';
    }
}

/* ============================================================================
BOOTSTRAP DE REDE
============================================================================ */
static bool net_bootstrap(void) {
    GUI_Memo_AddStr(LogMemo, "[NET] Bootstrap (ARP + DHCP)...\n");
    uint8_t mac[6] = {0};
    if (sys_net_get_mac(mac) != 0) return false;
    ip_set_config(MAKE_IP(10,0,2,15), MAKE_IP(255,255,255,0), MAKE_IP(10,0,2,2));
    arp_send_request(MAKE_IP(10,0,2,2));
    uint8_t gw_mac[6];
    for (int i = 0; i < 50; i++) {
        net_poll();
        if (arp_lookup(MAKE_IP(10,0,2,2), gw_mac)) break;
        sys_sleep(20);
    }
    dhcp_config_t cfg;
    if (dhcp_request_ip(mac, &cfg) == 0) {
        ip_set_config(cfg.ip, cfg.netmask, cfg.gateway);
        if (cfg.dns_server != 0) g_dns_server = cfg.dns_server;
        GUI_Memo_AddStr(LogMemo, "[NET] DHCP OK.\n");
    }
    return true;
}

/* ============================================================================
FETCH UNIFICADO COM CONTENT-LENGTH DINÂMICO
============================================================================ */
static int net_fetch(const char* host, const char* path, bool is_tls) {
    uint32_t target_ip = parse_ip(host);
    if (target_ip == 0) {
        target_ip = dns_resolve(host, g_dns_server);
        if (target_ip == 0) return -1;
    }
    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) return -2;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(is_tls ? 443 : 80);
    sa.sin_addr.s_addr = target_ip;
    if (connect(sockfd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        close(sockfd);
        return -3;
    }
    int tls_handle = 0;
    if (is_tls) {
        GUI_Memo_AddStr(LogMemo, "[TLS] Iniciando Handshake TLS 1.3...\n");
        Flush_Grafico_Janela();
        tls_handle = (int)tls13_syscall(SYS_TLS13_CONNECT, (uint64_t)sockfd, (uint64_t)host, 0, 0, 0, 0, 0);
        if (tls_handle <= 0) {
            GUI_Memo_AddStr(LogMemo, "[TLS] ERRO: Falha no Handshake TLS 1.3.\n");
            close(sockfd);
            return -4;
        }
        GUI_Memo_AddStr(LogMemo, "[TLS] Conexao TLS estabelecida!\n");
    }
    char req[512];
    size_t l = safe_strcpy(req, "GET ", sizeof(req));
    l = safe_strcat(req, path, sizeof(req), l);
    l = safe_strcat(req, " HTTP/1.1\r\nHost: ", sizeof(req), l);
    l = safe_strcat(req, host, sizeof(req), l);
    l = safe_strcat(req, "\r\nUser-Agent: LBF-Browser/6.5\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n", sizeof(req), l);
    
    if (is_tls) {
        tls13_syscall(SYS_TLS13_WRITE, (uint64_t)tls_handle, (uint64_t)req, l, 0, 0, 0, 0);
    } else {
        send(sockfd, req, l, 0);
    }

    int total = 0, timeout = 300;
    int expected_len = -1;
    bool headers_parsed = false;

    while (timeout > 0 && total < (int)sizeof(g_rx) - 1) {
        int n = 0;
        if (is_tls) {
            n = (int)tls13_syscall(SYS_TLS13_READ, (uint64_t)tls_handle, (uint64_t)g_chunk, sizeof(g_chunk) - 1, 0, 0, 0, 0);
        } else {
            n = recv(sockfd, g_chunk, sizeof(g_chunk) - 1, 0);
        }

        if (n > 0) {
            if (total + n > (int)sizeof(g_rx) - 1) n = sizeof(g_rx) - 1 - total;
            for (int i = 0; i < n; i++) g_rx[total + i] = g_chunk[i];
            total += n;
            g_rx[total] = '\0'; // Garante terminador provisório para análise

            if (!headers_parsed) {
                const char* body = find_http_body(g_rx);
                if (body) {
                    headers_parsed = true;
                    const char* cl = local_strstr(g_rx, "Content-Length:");
                    if (!cl) cl = local_strstr(g_rx, "content-length:");
                    
                    if (cl && cl < body) {
                        cl += 15; 
                        while (*cl == ' ') cl++;
                        expected_len = 0;
                        while (*cl >= '0' && *cl <= '9') {
                            expected_len = expected_len * 10 + (*cl - '0');
                            cl++;
                        }
                        // O expected total é tamanho do header + body (Content-Length)
                        expected_len += (int)(body - g_rx);
                    }
                }
            }

            // Encerra instantaneamente se já recebemos o prometido no Content-Length
            if (headers_parsed && expected_len > 0 && total >= expected_len) {
                break;
            }
            timeout = 150;
        } else {
            sys_sleep(10);
            timeout--;
        }
    }

    if (is_tls) {
        tls13_syscall(SYS_TLS13_CLOSE, (uint64_t)tls_handle, 0, 0, 0, 0, 0, 0);
    }
    close(sockfd);
    g_rx[total] = '\0';
    return total;
}

static int fetch_any(const char* url, char* out_host, size_t host_max, bool* out_tls, bool allow_tls_upgrade) {
    char cur[256];
    safe_strcpy(cur, url, sizeof(cur));
    for (int r = 0; r < 3; r++) {
        char host[128], path[128];
        bool is_tls = false;
        if (!parse_url(cur, host, sizeof(host), path, sizeof(path), &is_tls)) return -1;
        if (!allow_tls_upgrade && is_tls) is_tls = false;
        
        int total = net_fetch(host, path, is_tls);
        if (total <= 0) return total;
        
        int st = http_status_of(g_rx);
        if (st == 301 || st == 302 || st == 307) {
            const char* loc = local_strstr(g_rx, "Location:");
            if (!loc) loc = local_strstr(g_rx, "location:");
            if (loc) {
                loc += 9; while (*loc == ' ') loc++;
                char nu[192]; size_t k = 0;
                while (*loc && *loc != '\r' && *loc != '\n' && k < sizeof(nu) - 1) nu[k++] = *loc++;
                nu[k] = '\0';
                if (!allow_tls_upgrade && starts_with_ci(nu, "https://")) {
                    char nu_http[192];
                    safe_strcpy(nu_http, "http://", sizeof(nu_http));
                    safe_strcat(nu_http, nu + 8, sizeof(nu_http), 7);
                    safe_strcpy(nu, nu_http, sizeof(nu));
                }
                if (starts_with_ci(nu, "http://") || starts_with_ci(nu, "https://")) {
                    safe_strcpy(cur, nu, sizeof(cur));
                } else {
                    char tmp[256];
                    safe_strcpy(tmp, is_tls ? "https://" : "http://", sizeof(tmp));
                    size_t tl = safe_strcat(tmp, host, sizeof(tmp), strlen(tmp));
                    if (nu[0] != '/') tl = safe_strcat(tmp, "/", sizeof(tmp), tl);
                    safe_strcat(tmp, nu, sizeof(tmp), tl);
                    safe_strcpy(cur, tmp, sizeof(cur));
                }
                continue;
            }
        }
        if (out_host) safe_strcpy(out_host, host, host_max);
        if (out_tls) *out_tls = is_tls;
        return total;
    }
    return -1;
}

/* ============================================================================
PARSER HTML E CARREGAMENTO DE IMAGENS
============================================================================ */
static void flush_cur(WebDoc* doc, char* cur, int* len, uint8_t style, int link_id) {
    while (*len > 0 && cur[*len - 1] == ' ') (*len)--;
    if (*len > 0) {
        cur[*len] = '\0';
        webdoc_add_line(doc, cur, style, link_id, -1);
    }
    *len = 0;
    cur[0] = '\0';
}

static void html_to_webdoc(const char* html, WebDoc* doc) {
    static char cur[160];
    int len = 0;
    const char* p = html;
    
    /* Adicionada a flag in_svg para evitar leitura vetorial indesejada */
    bool in_script = false, in_style = false, in_title = false, in_svg = false;
    bool in_link = false, in_heading = false;
    int link_id = -1;
    char pend[4]; int npend = 0;
    
    while (*p || npend > 0) {
        if (npend > 0) {
            char cb = pend[0];
            for (int i = 0; i < npend - 1; i++) pend[i] = pend[i + 1];
            npend--;
            if (len < 140) cur[len++] = cb;
            else flush_cur(doc, cur, &len, in_link ? WEB_STYLE_LINK : (in_heading ? WEB_STYLE_TITLE : WEB_STYLE_TEXT), link_id);
            continue;
        }
        if (*p == '<') {
            if (p[1]=='!' && p[2]=='-' && p[3]=='-') {
                const char* e = local_strstr(p, "-->");
                p = e ? (e + 3) : (p + 4);
                continue;
            }
            if (starts_with_ci(p, "<script")) in_script = true;
            else if (starts_with_ci(p, "</script")) in_script = false;
            else if (starts_with_ci(p, "<style")) in_style = true;
            else if (starts_with_ci(p, "</style")) in_style = false;
            else if (starts_with_ci(p, "<title")) in_title = true;
            else if (starts_with_ci(p, "</title")) in_title = false;
            else if (starts_with_ci(p, "<svg")) in_svg = true;
            else if (starts_with_ci(p, "</svg")) in_svg = false;
            
            else if (!in_script && !in_style && !in_title && !in_svg) {
                uint8_t cur_style = in_link ? WEB_STYLE_LINK :
                                    (in_heading ? WEB_STYLE_TITLE : WEB_STYLE_TEXT);
                if (starts_with_ci(p, "<br") || starts_with_ci(p, "<p") ||
                    starts_with_ci(p, "</p") || starts_with_ci(p, "</div") ||
                    starts_with_ci(p, "<li") || starts_with_ci(p, "<tr")) {
                    flush_cur(doc, cur, &len, cur_style, link_id);
                }
                else if (starts_with_ci(p, "<h1") || starts_with_ci(p, "<h2") || starts_with_ci(p, "<h3")) {
                    flush_cur(doc, cur, &len, cur_style, link_id);
                    in_heading = true;
                }
                else if (starts_with_ci(p, "</h1") || starts_with_ci(p, "</h2") || starts_with_ci(p, "</h3")) {
                    flush_cur(doc, cur, &len, WEB_STYLE_TITLE, -1);
                    in_heading = false;
                }
                else if (starts_with_ci(p, "<a")) {
                    flush_cur(doc, cur, &len, cur_style, link_id);
                    const char* gt = p;
                    while (*gt && *gt != '>') gt++;
                    const char* h = local_strstr(p, "href=");
                    if (h && h < gt && doc->kc < 64) {
                        h += 5; char q = 0;
                        if (*h == '"' || *h == '\'') { q = *h; h++; }
                        char href[160]; int i = 0;
                        while (*h && *h != '>' && i < 159) {
                            if (q && *h == q) break;
                            if (!q && (*h == ' ' || *h == '\t')) break;
                            href[i++] = *h++;
                        }
                        href[i] = '\0';
                        if (i > 0) {
                            link_id = webdoc_add_link(doc, href);
                            in_link = (link_id >= 0);
                        }
                    }
                }
                else if (starts_with_ci(p, "</a")) {
                    flush_cur(doc, cur, &len, WEB_STYLE_LINK, link_id);
                    in_link = false; link_id = -1;
                }
                else if (starts_with_ci(p, "<img")) {
                    flush_cur(doc, cur, &len, cur_style, link_id);
                    const char* gt = p;
                    while (*gt && *gt != '>') gt++;
                    const char* s = local_strstr(p, "src=");
                    if (s && s < gt && doc->ic < 8) {
                        s += 4; char q = 0;
                        if (*s == '"' || *s == '\'') { q = *s; s++; }
                        char src[160]; int i = 0;
                        while (*s && *s != '>' && i < 159) {
                            if (q && *s == q) break;
                            src[i++] = *s++;
                        }
                        src[i] = '\0';
                        if (i > 0) {
                            int slot = webdoc_add_image(doc, src);
                            if (slot >= 0) {
                                char tmp[180];
                                safe_strcpy(tmp, "[IMG] ", sizeof(tmp));
                                safe_strcat(tmp, src, sizeof(tmp), strlen(tmp));
                                webdoc_add_line(doc, tmp, WEB_STYLE_IMG, -1, slot);
                            }
                        }
                    }
                }
            }
            while (*p && *p != '>') p++;
            if (*p == '>') p++;
            continue;
        }
        
        // Ignora toda e qualquer renderização se estivermos nos blocos de código/vetor
        if (in_script || in_style || in_title || in_svg) { p++; continue; }
        
        char c = 0;
        if (*p == '&') {
            if (starts_with_ci(p, "&amp;"))       { c = '&';  p += 5; }
            else if (starts_with_ci(p, "&lt;"))   { c = '<';  p += 4; }
            else if (starts_with_ci(p, "&gt;"))   { c = '>';  p += 4; }
            else if (starts_with_ci(p, "&quot;")) { c = '"';  p += 6; }
            else if (starts_with_ci(p, "&#39;"))  { c = '\''; p += 5; }
            else if (starts_with_ci(p, "&nbsp;")) { c = ' ';  p += 6; }
            else if (starts_with_ci(p, "&ccedil;")) { c = (char)0x87; p += 8; }
            else if (starts_with_ci(p, "&Ccedil;")) { c = (char)0x80; p += 8; }
            else if (starts_with_ci(p, "&aacute;")) { c = (char)0xA0; p += 8; }
            else if (starts_with_ci(p, "&eacute;")) { c = (char)0x82; p += 8; }
            else if (starts_with_ci(p, "&iacute;")) { c = (char)0xA1; p += 8; }
            else if (starts_with_ci(p, "&oacute;")) { c = (char)0xA2; p += 8; }
            else if (starts_with_ci(p, "&uacute;")) { c = (char)0xA3; p += 8; }
            else if (starts_with_ci(p, "&Aacute;")) { c = (char)0xC1; p += 8; }
            else if (starts_with_ci(p, "&Eacute;")) { c = (char)0x90; p += 8; }
            else if (starts_with_ci(p, "&Iacute;")) { c = (char)0xCD; p += 8; }
            else if (starts_with_ci(p, "&Oacute;")) { c = (char)0xD3; p += 8; }
            else if (starts_with_ci(p, "&Uacute;")) { c = (char)0xDA; p += 8; }
            else if (starts_with_ci(p, "&atilde;")) { c = (char)0xE3; p += 8; }
            else if (starts_with_ci(p, "&otilde;")) { c = (char)0xF5; p += 8; }
            else if (starts_with_ci(p, "&Atilde;")) { c = (char)0xC3; p += 8; }
            else if (starts_with_ci(p, "&Otilde;")) { c = (char)0xD5; p += 8; }
            else if (starts_with_ci(p, "&acirc;"))  { c = (char)0x83; p += 7; }
            else if (starts_with_ci(p, "&ecirc;"))  { c = (char)0x88; p += 7; }
            else if (starts_with_ci(p, "&ocirc;"))  { c = (char)0x93; p += 7; }
            else if (starts_with_ci(p, "&agrave;")) { c = (char)0x85; p += 8; }
            else if (starts_with_ci(p, "&uuml;"))   { c = (char)0x81; p += 6; }
            else if (starts_with_ci(p, "&Uuml;"))   { c = (char)0x9A; p += 6; }
            else if (starts_with_ci(p, "&ntilde;")) { c = (char)0xA4; p += 8; }
            else if (starts_with_ci(p, "&Ntilde;")) { c = (char)0xA5; p += 8; }
            else if (starts_with_ci(p, "&euml;"))   { c = (char)0x89; p += 6; }
            else if (starts_with_ci(p, "&iuml;"))   { c = (char)0x8B; p += 6; }
            else if (starts_with_ci(p, "&auml;"))   { c = (char)0x84; p += 6; }
            else if (starts_with_ci(p, "&ouml;"))   { c = (char)0x94; p += 6; }
            else if (starts_with_ci(p, "&copy;"))   { c = (char)0xA9; p += 6; }
            else if (starts_with_ci(p, "&reg;"))    { pend[0]='R'; pend[1]=')'; npend=2; c='('; p += 5; }
            else if (starts_with_ci(p, "&deg;"))    { c = 'o';  p += 5; }
            else if (starts_with_ci(p, "&plusmn;")) { pend[0]='-'; pend[1]='/'; npend=2; c='+'; p += 8; }
            else if (starts_with_ci(p, "&sup2;"))   { c = '2';  p += 6; }
            else if (starts_with_ci(p, "&sup3;"))   { c = '3';  p += 6; }
            else if (starts_with_ci(p, "&micro;"))  { c = 'u';  p += 7; }
            else if (starts_with_ci(p, "&mdash;"))  { pend[0]='-'; npend=1; c='-'; p += 7; }
            else if (starts_with_ci(p, "&ndash;"))  { c = '-';  p += 7; }
            else if (starts_with_ci(p, "&hellip;")) { pend[0]='.'; pend[1]='.'; npend=2; c='.'; p += 8; }
            else if (starts_with_ci(p, "&ldquo;"))  { c = '"';  p += 7; }
            else if (starts_with_ci(p, "&rdquo;"))  { c = '"';  p += 7; }
            else if (starts_with_ci(p, "&lsquo;"))  { c = '\''; p += 7; }
            else if (starts_with_ci(p, "&rsquo;"))  { c = '\''; p += 7; }
            else if (starts_with_ci(p, "&bull;"))   { c = '*';  p += 6; }
            else if (starts_with_ci(p, "&trade;"))  { c = 'T';  p += 7; }
            else if (starts_with_ci(p, "&euro;"))   { c = 'E';  p += 6; }
            else if (starts_with_ci(p, "&iexcl;"))  { c = '!';  p += 7; }
            else if (starts_with_ci(p, "&iquest;")) { c = '?';  p += 8; }
            else if (p[1] == '#') {
                int hex = (p[2] == 'x' || p[2] == 'X');
                const char* q = p + 2 + (hex ? 1 : 0);
                int v = 0;
                while (*q && *q != ';') {
                    int d;
                    if (*q >= '0' && *q <= '9') d = *q - '0';
                    else if (hex && *q >= 'a' && *q <= 'f') d = 10 + *q - 'a';
                    else if (hex && *q >= 'A' && *q <= 'F') d = 10 + *q - 'A';
                    else break;
                    v = v * (hex ? 16 : 10) + d;
                    q++;
                }
                if (*q == ';') q++;
                p = q;
                c = (char)pack_cp((uint32_t)v);
            }
            else { c = '&'; p++; }
        } else {
            unsigned char ub = (unsigned char)*p;
            if (ub < 0x80) {
                c = *p++;
            } else {
                uint32_t cp;
                if (ub >= 0xC2 && ub <= 0xDF && ((unsigned char)p[1] & 0xC0) == 0x80) {
                    cp = ((uint32_t)(ub & 0x1F) << 6) | (p[1] & 0x3F);
                    p += 2;
                } else if (ub >= 0xE0 && ub <= 0xEF &&
                           ((unsigned char)p[1] & 0xC0) == 0x80 &&
                           ((unsigned char)p[2] & 0xC0) == 0x80) {
                    cp = ((uint32_t)(ub & 0x0F) << 12) |
                         ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
                    p += 3;
                } else {
                    cp = ub;
                    p += 1;
                }
                c = (char)pack_cp(cp);
            }
        }
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ' && (len == 0 || cur[len - 1] == ' ')) continue;
        cur[len++] = c;
        if (len >= 140) {
            uint8_t st = in_link ? WEB_STYLE_LINK :
                         (in_heading ? WEB_STYLE_TITLE : WEB_STYLE_TEXT);
            flush_cur(doc, cur, &len, st, link_id);
        }
    }
    flush_cur(doc, cur, &len, in_link ? WEB_STYLE_LINK : (in_heading ? WEB_STYLE_TITLE : WEB_STYLE_TEXT), link_id);
    webdoc_finalize(doc);
}

static int try_load_images(WebDoc* doc) {
    int loaded = 0;
    for (int i = 0; i < doc->ic && loaded < 3; i++) {
        WebImgSlot* sl = &doc->imgs[i];
        char full[256];
        resolve_url(sl->url, full, sizeof(full));
        
        if (local_strstr(full, ".svg") || local_strstr(full, ".webp") ||
            local_strstr(full, ".ico") || local_strstr(full, ".jpg") ||
            local_strstr(full, ".jpeg")) continue;
            
        GUI_Memo_AddStr(LogMemo, "[IMG] ");
        GUI_Memo_AddStr(LogMemo, full);
        GUI_Memo_AddStr(LogMemo, "\n");
        Flush_Grafico_Janela();
        
        char ihost[128]; bool itls = false;
        int total = fetch_any(full, ihost, sizeof(ihost), &itls, true);
        if (total <= 0) continue;
        
        const char* hdr = find_http_body(g_rx);
        char* body = hdr ? (char*)hdr : g_rx;
        int blen = total - (int)(body - g_rx);
        if (blen < 0) blen = 0;
        
        int hlen = hdr ? (int)(hdr - g_rx) : total;
        char sv = g_rx[hlen]; 
        g_rx[hlen] = '\0';
        bool chk = (local_strstr(g_rx, "chunked") != NULL);
        g_rx[hlen] = sv;
        
        if (chk) blen = http_dechunk_inplace(body, blen);
        if (blen < 8) continue;
        
        uint32_t* px = NULL; int w = 0, h = 0;
        int rc = webimg_decode((uint8_t*)body, blen, &px, &w, &h);
        if (rc != 0 || !px || w <= 0 || h <= 0 || w * h > 256 * 256) {
            if (px) free(px);
            continue;
        }
        sl->px = px; sl->w = w; sl->h = h; sl->loaded = true;
        loaded++;
    }
    for (int i = 0; i < doc->lc; i++) {
        WebLine* ln = &doc->lines[i];
        if (ln->style == WEB_STYLE_IMG &&
            ln->img_slot >= 0 && ln->img_slot < doc->ic &&
            !doc->imgs[ln->img_slot].loaded) {
            ln->style = WEB_STYLE_TEXT;
        }
    }
    return loaded;
}

/* ============================================================================
CARREGAMENTO E NAVEGAÇÃO
============================================================================ */
static void history_push(const char* url) {
    if (g_history_top < MAX_HISTORY) {
        safe_strcpy(g_history[g_history_top], url, 192);
        g_history_top++;
    } else {
        for (int i = 0; i < MAX_HISTORY - 1; i++) safe_strcpy(g_history[i], g_history[i + 1], 192);
        safe_strcpy(g_history[MAX_HISTORY - 1], url, 192);
    }
}

static void free_doc(void) {
    if (!g_doc) return;
    for (int i = 0; i < g_doc->ic; i++) {
        if (g_doc->imgs[i].px) free(g_doc->imgs[i].px);
    }
    webdoc_destroy(g_doc);
    g_doc = NULL;
}

static void load_url(const char* url_in) {
    GUI_WebPage_Clear(WebPage);
    free_doc();
    char target_url[256];
    if (!starts_with_ci(url_in, "http://") && !starts_with_ci(url_in, "https://")) {
        safe_strcpy(target_url, "http://", sizeof(target_url));
        safe_strcat(target_url, url_in, sizeof(target_url), 7);
    } else {
        safe_strcpy(target_url, url_in, sizeof(target_url));
    }
    Label_SetText(LblStatus, "Conectando...");
    GUI_Memo_Clear(LogMemo);
    Flush_Grafico_Janela();
    if (!g_net_ready) {
        g_net_ready = net_bootstrap();
        if (!g_net_ready) { Label_SetText(LblStatus, "Erro de rede"); return; }
    }
    GUI_Memo_AddStr(LogMemo, "[GET] ");
    GUI_Memo_AddStr(LogMemo, target_url);
    GUI_Memo_AddStr(LogMemo, "\n");
    Flush_Grafico_Janela();
    char host[128];
    bool final_tls = false;
    int total = fetch_any(target_url, host, sizeof(host), &final_tls, true);
    if (total <= 0 && starts_with_ci(target_url, "https://")) {
        GUI_Memo_AddStr(LogMemo, "[TLS] Falha no TLS 1.3. Tentando Fallback para HTTP (Porta 80)...\n");
        Flush_Grafico_Janela();
        char fallback_url[256];
        safe_strcpy(fallback_url, "http://", sizeof(fallback_url));
        safe_strcat(fallback_url, target_url + 8, sizeof(fallback_url), 7);
        safe_strcpy(target_url, fallback_url, sizeof(target_url));
        GUI_Edit_SetText(EditURL, target_url);
        total = fetch_any(target_url, host, sizeof(host), &final_tls, false);
    }
    if (total <= 0) {
        Label_SetText(LblStatus, "Erro de conexao");
        GUI_Memo_AddStr(LogMemo, "[ERRO] Falha no download.\n");
        Flush_Grafico_Janela();
        return;
    }
    
    int status = http_status_of(g_rx);
    safe_strcpy(g_host, final_tls ? "https://" : "http://", sizeof(g_host));
    safe_strcat(g_host, host, sizeof(g_host), strlen(g_host));
    
    /* Parse dinâmico dos headers suportando tanto \n\n quanto \r\n\r\n */
    const char* hdr_end = find_http_body(g_rx);
    char* body = hdr_end ? (char*)hdr_end : g_rx;
    int body_len = total - (int)(body - g_rx);
    if (body_len < 0) body_len = 0;
    
    int hdr_len = hdr_end ? (int)(hdr_end - g_rx) : total;
    char saved = g_rx[hdr_len];
    g_rx[hdr_len] = '\0';
    bool is_chunked = (local_strstr(g_rx, "chunked") != NULL);
    g_rx[hdr_len] = saved;
    
    if (is_chunked) body_len = http_dechunk_inplace(body, body_len);
    
    static char* g_body_copy = NULL;
    if (g_body_copy) { free(g_body_copy); g_body_copy = NULL; }
    g_body_copy = (char*)malloc(body_len > 0 ? body_len : 1);
    
    if (g_body_copy) { for (int i = 0; i < body_len; i++) g_body_copy[i] = body[i]; g_body = g_body_copy; }
    else g_body = NULL;
    g_body_len = body_len;
    
    g_doc = webdoc_create();
    if (g_doc) {
        html_to_webdoc(body, g_doc);
        int imgs = try_load_images(g_doc);
        GUI_WebPage_SetDoc(WebPage, g_doc);
        char st[110]; char n1[8], n2[8];
        IntToStr(status, st);
        strcat(st, final_tls ? " OK (HTTPS/TLS) | " : " OK (HTTP) | ");
        IntToStr(g_doc->kc, n1); strcat(st, n1); strcat(st, " links | ");
        IntToStr(imgs, n2); strcat(st, n2); strcat(st, " imgs");
        Label_SetText(LblStatus, st);
        GUI_Memo_AddStr(LogMemo, "[OK] Pagina carregada com sucesso.\n");
    }
    Flush_Grafico_Janela();
}

void OnPageNavigate(void* sender, const char* url) {
    (void)sender;
    if (!url || url[0] == '\0') return;
    char full[256];
    resolve_url(url, full, sizeof(full));
    GUI_Edit_SetText(EditURL, full);
    history_push(full);
    load_url(full);
}

void OnBtnGoClick(void* sender) {
    (void)sender;
    char url[192];
    safe_strcpy(url, GUI_Edit_GetText(EditURL), sizeof(url));
    if (url[0] == '\0') safe_strcpy(url, "http://www.google.com/", sizeof(url));
    GUI_Edit_SetText(EditURL, url);
    history_push(url);
    load_url(url);
}

void OnBtnBackClick(void* sender) {
    (void)sender;
    if (g_history_top > 1) {
        g_history_top--;
        char prev[192];
        safe_strcpy(prev, g_history[g_history_top - 1], 192);
        GUI_Edit_SetText(EditURL, prev);
        load_url(prev);
    }
}

void OnBtnRefreshClick(void* sender) {
    (void)sender;
    char url[192];
    safe_strcpy(url, GUI_Edit_GetText(EditURL), sizeof(url));
    if (url[0] != '\0') load_url(url);
}

void OnBtnSaveClick(void* sender) {
    (void)sender;
    if (!g_body || g_body_len <= 0) return;
    sys_fat_write("0:/pagina.html", (void*)g_body, (uint32_t)g_body_len);
}

/* ============================================================================
FUNÇÕES DE JANELA
============================================================================ */
void Flush_Grafico_Janela(void) {
    if (my_app_slot < 0 || !MyApp.MainWindow) return;
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}

void Tratar_Fechamento_Software(void) {
    if (my_app_slot < 0) return;
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50);
}

char Obter_Tecla_Entrada(void) {
    if (my_app_slot < 0) return 0;
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0;
        return key;
    }
    return 0;
}

/* ============================================================================
MAIN
============================================================================ */
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    graphics_init_app(winWidth, winHeight);
    wm_init();
    my_app_slot = OS_IPC_RegisterApp("LBF Browser", winWidth, winHeight);
    if (my_app_slot == -1) return -1;
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "LBF Browser v6.6", winWidth, winHeight);
    EditURL    = GUI_CreateEdit(&MyApp, 10, 36, 440, 28, "http://www.google.com/", NULL);
    BtnGo      = GUI_CreateButton(&MyApp, 460, 36, 70, 28, "IR", OnBtnGoClick);
    BtnBack    = GUI_CreateButton(&MyApp, 10, 74, 90, 28, "VOLTAR", OnBtnBackClick);
    BtnRefresh = GUI_CreateButton(&MyApp, 110, 74, 110, 28, "ATUALIZAR", OnBtnRefreshClick);
    BtnSave    = GUI_CreateButton(&MyApp, 230, 74, 90, 28, "SALVAR", OnBtnSaveClick);
    LblStatus  = GUI_CreateLabel(&MyApp, 330, 78, "Pronto.");
    WebPage    = GUI_CreateWebPage(&MyApp, 10, 110, 600, 360, OnPageNavigate);
    LogMemo    = GUI_CreateMemo(&MyApp, 10, 478, 600, 130);
    GUI_Memo_AddStr(LogMemo, "LBF Browser v6.6: Pronto.\n");
    Flush_Grafico_Janela();

    g_focused_control = (void*)EditURL;
    void* ultimo_controle_focado = (void*)EditURL;
    gui_set_prop(EditURL, PROP_SET_FOCUS, 1);

    static int ultimo_x = 0, ultimo_y = 0, mouse_hold_timer = 0;
    static bool ultimo_estado_foco = false;

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }

        bool euTenhoFocoJanelaReal = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFocoJanelaReal != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFocoJanelaReal;
            if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFocoJanelaReal;
            Flush_Grafico_Janela();
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
            Flush_Grafico_Janela();
        }
        if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
            if (mouse_hold_timer == 0) {
                int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                ultimo_x = rel_x;
                ultimo_y = rel_y;
                mouse_hold_timer = 2;
                if (BtnGo && rel_x >= BtnGo->Left && rel_x < (BtnGo->Left + BtnGo->Width) &&
                    rel_y >= BtnGo->Top && rel_y < (BtnGo->Top + BtnGo->Height)) {
                    gui_set_prop(BtnGo, PROP_STATE, 2);
                }
                else if (BtnBack && rel_x >= BtnBack->Left && rel_x < (BtnBack->Left + BtnBack->Width) &&
                         rel_y >= BtnBack->Top && rel_y < (BtnBack->Top + BtnBack->Height)) {
                    gui_set_prop(BtnBack, PROP_STATE, 2);
                }
                else if (BtnRefresh && rel_x >= BtnRefresh->Left && rel_x < (BtnRefresh->Left + BtnRefresh->Width) &&
                         rel_y >= BtnRefresh->Top && rel_y < (BtnRefresh->Top + BtnRefresh->Height)) {
                    gui_set_prop(BtnRefresh, PROP_STATE, 2);
                }
                else if (BtnSave && rel_x >= BtnSave->Left && rel_x < (BtnSave->Left + BtnSave->Width) &&
                         rel_y >= BtnSave->Top && rel_y < (BtnSave->Top + BtnSave->Height)) {
                    gui_set_prop(BtnSave, PROP_STATE, 2);
                }
                events_process_mouse(rel_x, rel_y, 1, 0);
                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                    Flush_Grafico_Janela();
                    if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
                }
            }
            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
        }
        if (mouse_hold_timer > 0) {
            mouse_hold_timer--;
            if (mouse_hold_timer == 0) {
                if (BtnGo)      gui_set_prop(BtnGo, PROP_STATE, 0);
                if (BtnBack)    gui_set_prop(BtnBack, PROP_STATE, 0);
                if (BtnRefresh) gui_set_prop(BtnRefresh, PROP_STATE, 0);
                if (BtnSave)    gui_set_prop(BtnSave, PROP_STATE, 0);
                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                Flush_Grafico_Janela();
            }
        }
        sys_sleep(euTenhoFocoJanelaReal ? 16 : 32);
    }
    free_doc();
    sys_exit();
    return 0;
}
