#include "graphics.h"
#include "malloc.h"
#include "liblib.h"
#include "fonts.h"
#include "string.h"

// ============================================================================
// Variáveis Globais (Ring 3)
// ============================================================================
uint8_t* ram_buffer = NULL;
int screen_w = 0;
int screen_h = 0;
int screen_pitch = 0;
int bpp_bytes = 0;
static int my_slot = -1;

// ============================================================================
// Forward declarations internos
// ============================================================================
void sys_draw_char(int x, int y, unsigned char c, uint32_t color, int scale);
void sys_draw_rect(int x, int y, int w, int h, uint32_t color);

// ============================================================================
// Gerenciamento de Superfície
// ============================================================================
surface_t* graphics_create_surface(int w, int h) {
    surface_t* sur = (surface_t*)malloc(sizeof(surface_t));
    if (!sur) return NULL;

    vesa_info_t vesa;
    get_video_info(&vesa);

    sur->width  = w;
    sur->height = h;
    sur->pitch  = vesa.pitch;
    sur->bpp    = vesa.bpp;

    sur->buffer = (uint8_t*)malloc(sur->pitch * h);
    if (!sur->buffer) {
        free(sur);
        return NULL;
    }
    return sur;
}

void graphics_destroy_surface(surface_t* sur) {
    if (sur) {
        if (sur->buffer) free(sur->buffer);
        free(sur);
    }
}

void graphics_commit(void) {
    if (!ram_buffer) return;
    video_flip(ram_buffer);
}

// ============================================================================
// Desenho Primitivo em surface_t
// ============================================================================
void draw_pixel(surface_t* sur, int x, int y, uint32_t color) {
    if (!sur || !sur->buffer) return;
    if (x < 0 || x >= sur->width || y < 0 || y >= sur->height) return;

    if (sur->bpp == 32) {
        uint32_t* ptr = (uint32_t*)sur->buffer;
        ptr[y * (sur->pitch / 4) + x] = color;
    } else {
        int offset = (y * sur->pitch) + (x * 3);
        sur->buffer[offset]     = color & 0xFF;
        sur->buffer[offset + 1] = (color >> 8) & 0xFF;
        sur->buffer[offset + 2] = (color >> 16) & 0xFF;
    }
}

void draw_clear(surface_t* sur, uint32_t color) {
    if (!sur || !sur->buffer) return;

    if (sur->bpp == 32) {
        size_t total_pixels = (sur->pitch / 4) * sur->height;
        uint32_t* ptr = (uint32_t*)sur->buffer;
        while (total_pixels--) {
            *ptr++ = color;
        }
    } else if (sur->bpp == 24) {
        uint8_t b = color & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t r = (color >> 16) & 0xFF;

        for (int y = 0; y < sur->height; y++) {
            int row_offset = y * sur->pitch;
            for (int x = 0; x < sur->width; x++) {
                int offset = row_offset + (x * 3);
                sur->buffer[offset]     = b;
                sur->buffer[offset + 1] = g;
                sur->buffer[offset + 2] = r;
            }
        }
    }
}

void draw_rect(surface_t* sur, int x, int y, int w, int h, uint32_t color) {
    for (int iy = 0; iy < h; iy++) {
        for (int ix = 0; ix < w; ix++) {
            draw_pixel(sur, x + ix, y + iy, color);
        }
    }
}

// ============================================================================
// Texto em surface_t
// ============================================================================
void draw_char(surface_t* sur, int x, int y, unsigned char c, uint32_t color, int scale) {
    if (!sur || !sur->buffer) return;
    if (scale < 1) scale = 1;

    uint8_t index = (uint8_t)c;

    for (int row = 0; row < 8; row++) {
        uint8_t row_data = font_8x8[index][row];
        for (int col = 0; col < 8; col++) {
            if ((row_data >> (7 - col)) & 1) {
                if (scale <= 1) {
                    draw_pixel(sur, x + col, y + row, color);
                } else {
                    draw_rect(sur, x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

void draw_string(surface_t* sur, int x, int y, const char* str, uint32_t color, int scale) {
    if (!str) return;
    if (scale < 1) scale = 1;

    int cur_x = x;
    int cur_y = y;
    int line_start_x = x;

    while (*str) {
        unsigned char c = (unsigned char)*str;

        if (c == '\n') {
            cur_x = line_start_x;
            cur_y += 8 * scale;
        } else if (c == '\r') {
            cur_x = line_start_x;
        } else if (c == '\t') {
            int tab_chars = 4;
            int col_atual = (cur_x - line_start_x) / (8 * scale);
            int prox = ((col_atual / tab_chars) + 1) * tab_chars;
            cur_x = line_start_x + prox * 8 * scale;
        } else if (c >= 32) {
            draw_char(sur, cur_x, cur_y, c, color, scale);
            cur_x += 8 * scale;
        }
        str++;
    }
}

void draw_bytes(surface_t* sur, int x, int y, const unsigned char* data, int len,
                uint32_t color, int scale) {
    if (!sur || !data || len <= 0) return;
    if (scale < 1) scale = 1;

    int cur_x = x;
    int cur_y = y;
    int line_start_x = x;

    for (int i = 0; i < len; i++) {
        unsigned char c = data[i];

        if (c == '\n') {
            cur_x = line_start_x;
            cur_y += 8 * scale;
        } else if (c == '\r') {
            cur_x = line_start_x;
        } else if (c == '\t') {
            int tab_chars = 4;
            int col_atual = (cur_x - line_start_x) / (8 * scale);
            int prox = ((col_atual / tab_chars) + 1) * tab_chars;
            cur_x = line_start_x + prox * 8 * scale;
        } else if (c >= 32) {
            draw_char(sur, cur_x, cur_y, c, color, scale);
            cur_x += 8 * scale;
        }
    }
}

// ============================================================================
// Numéricos e Bitmaps
// ============================================================================
void draw_hex(surface_t* sur, int x, int y, uint64_t val, uint32_t color) {
    char hex_chars[] = "0123456789ABCDEF";
    draw_char(sur, x, y, '0', color, 1);
    draw_char(sur, x + 8, y, 'x', color, 1);

    int current_x = x + 16;
    for (int i = 60; i >= 0; i -= 4) {
        int nibble = (val >> i) & 0xF;
        draw_char(sur, current_x, y, hex_chars[nibble], color, 1);
        current_x += 8;
    }
}

void draw_dec(surface_t* sur, int x, int y, uint32_t val, uint32_t color) {
    if (val == 0) {
        draw_char(sur, x, y, '0', color, 1);
        return;
    }
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    uint32_t temp = val;
    while (temp > 0) {
        buf[--i] = (temp % 10) + '0';
        temp /= 10;
    }
    draw_string(sur, x, y, &buf[i], color, 1);
}

void draw_bitmap(surface_t* sur, int x, int y, int w, int h, uint32_t* data) {
    if (!sur || !data) return;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            uint32_t color = data[j * w + i];
            if (color != 0xFF00FF) {
                draw_pixel(sur, x + i, y + j, color);
            }
        }
    }
}

// ============================================================================
// Framebuffer Global (Ring 3)
// ============================================================================
void sys_draw_put_pixel(int x, int y, uint32_t color) {
    if (!ram_buffer) return;
    if (x < 0 || x >= screen_w || y < 0 || y >= screen_h) return;

    uint8_t* pixel_ptr = ram_buffer + (y * screen_pitch) + (x * bpp_bytes);

    if (bpp_bytes == 4) {
        *((uint32_t*)pixel_ptr) = color;
    } else if (bpp_bytes == 3) {
        pixel_ptr[0] = color & 0xFF;
        pixel_ptr[1] = (color >> 8) & 0xFF;
        pixel_ptr[2] = (color >> 16) & 0xFF;
    }
}

uint32_t sys_draw_get_pixel(int x, int y) {
    if (!ram_buffer) return 0;
    if (x < 0 || x >= screen_w || y < 0 || y >= screen_h) return 0;

    uint8_t* pixel_ptr = ram_buffer + (y * screen_pitch) + (x * bpp_bytes);

    if (bpp_bytes == 4) {
        return *((uint32_t*)pixel_ptr);
    } else if (bpp_bytes == 3) {
        return pixel_ptr[0] | (pixel_ptr[1] << 8) | (pixel_ptr[2] << 16);
    }
    return 0;
}

void sys_draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (!ram_buffer) {
        graphics_init();
        if (!ram_buffer) return;
    }
    for (int iy = 0; iy < h; iy++) {
        for (int ix = 0; ix < w; ix++) {
            sys_draw_put_pixel(x + ix, y + iy, color);
        }
    }
}

void sys_draw_char(int x, int y, unsigned char c, uint32_t color, int scale) {
    if (!ram_buffer) {
        graphics_init();
        if (!ram_buffer) return;
    }
    if (scale < 1) scale = 1;

    uint8_t index = (uint8_t)c;

    for (int row = 0; row < 8; row++) {
        uint8_t row_data = font_8x8[index][row];
        for (int col = 0; col < 8; col++) {
            if ((row_data >> (7 - col)) & 1) {
                if (scale <= 1) {
                    sys_draw_put_pixel(x + col, y + row, color);
                } else {
                    sys_draw_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

void sys_draw_string(int x, int y, const char* str, uint32_t color, int scale) {
    if (!str) return;
    if (scale < 1) scale = 1;
    if (!ram_buffer) {
        graphics_init();
        if (!ram_buffer) return;
    }

    int cur_x = x;
    int cur_y = y;
    int line_start_x = x;

    while (*str) {
        unsigned char c = (unsigned char)*str;

        if (c == '\n') {
            cur_x = line_start_x;
            cur_y += 8 * scale;
        } else if (c == '\r') {
            cur_x = line_start_x;
        } else if (c == '\t') {
            int tab_chars = 4;
            int col_atual = (cur_x - line_start_x) / (8 * scale);
            int prox = ((col_atual / tab_chars) + 1) * tab_chars;
            cur_x = line_start_x + prox * 8 * scale;
        } else if (c >= 32) {
            sys_draw_char(cur_x, cur_y, c, color, scale);
            cur_x += 8 * scale;
        }
        str++;
    }
}

void sys_draw_bytes(int x, int y, const unsigned char* data, int len,
                    uint32_t color, int scale) {
    if (!data || len <= 0) return;
    if (scale < 1) scale = 1;
    if (!ram_buffer) {
        graphics_init();
        if (!ram_buffer) return;
    }

    int cur_x = x;
    int cur_y = y;
    int line_start_x = x;

    for (int i = 0; i < len; i++) {
        unsigned char c = data[i];

        if (c == '\n') {
            cur_x = line_start_x;
            cur_y += 8 * scale;
        } else if (c == '\r') {
            cur_x = line_start_x;
        } else if (c == '\t') {
            int tab_chars = 4;
            int col_atual = (cur_x - line_start_x) / (8 * scale);
            int prox = ((col_atual / tab_chars) + 1) * tab_chars;
            cur_x = line_start_x + prox * 8 * scale;
        } else if (c >= 32) {
            sys_draw_char(cur_x, cur_y, c, color, scale);
            cur_x += 8 * scale;
        }
    }
}

// ============================================================================
// Texto em modo Grade (coluna, linha)
// ============================================================================
void sys_draw_char_at(int col, int row, unsigned char c, uint32_t color, int scale) {
    if (scale < 1) scale = 1;
    sys_draw_char(col * 8 * scale, row * 8 * scale, c, color, scale);
}

void sys_draw_string_at(int col, int row, const char* str, uint32_t color, int scale) {
    if (!str) return;
    if (scale < 1) scale = 1;

    int cur_col = col;
    int cur_row = row;

    while (*str) {
        unsigned char c = (unsigned char)*str;

        if (c == '\n') {
            cur_col = col;
            cur_row++;
        } else if (c == '\r') {
            cur_col = col;
        } else if (c == '\t') {
            cur_col = ((cur_col / 4) + 1) * 4;
        } else if (c >= 32) {
            sys_draw_char(cur_col * 8 * scale, cur_row * 8 * scale, c, color, scale);
            cur_col++;
        }
        str++;
    }
}

// ============================================================================
// Medição de texto
// ============================================================================
int sys_draw_string_width(const char* str, int scale) {
    if (!str) return 0;
    if (scale < 1) scale = 1;

    int max_col = 0;
    int cur_col = 0;

    while (*str) {
        unsigned char c = (unsigned char)*str;
        if (c == '\n' || c == '\r') {
            if (cur_col > max_col) max_col = cur_col;
            cur_col = 0;
        } else if (c == '\t') {
            cur_col = ((cur_col / 4) + 1) * 4;
        } else if (c >= 32) {
            cur_col++;
        }
        str++;
    }
    if (cur_col > max_col) max_col = cur_col;

    return max_col * 8 * scale;
}

int sys_draw_string_height(const char* str, int scale) {
    if (!str) return 0;
    if (scale < 1) scale = 1;

    int lines = 1;
    while (*str) {
        if (*str == '\n') lines++;
        str++;
    }
    return lines * 8 * scale;
}

// ============================================================================
// Modo Célula (terminal/console)
// ============================================================================
void sys_draw_cell(int col, int row, unsigned char c,
                   uint32_t fg, uint32_t bg, int scale) {
    if (scale < 1) scale = 1;
    int px = col * 8 * scale;
    int py = row * 8 * scale;
    int size = 8 * scale;

    // Preenche o fundo da célula
    sys_draw_rect(px, py, size, size, bg);

    // Desenha o caractere por cima (se for imprimível)
    if (c >= 32) {
        sys_draw_char(px, py, c, fg, scale);
    }
}

void sys_draw_line(int col, int row, const char* str, int max_cols,
                   uint32_t fg, uint32_t bg, int scale) {
    if (!str || max_cols <= 0) return;
    if (scale < 1) scale = 1;

    int cur_col = col;

    for (int i = 0; i < max_cols; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '\0') c = ' ';

        sys_draw_cell(cur_col, row, c, fg, bg, scale);
        cur_col++;
    }
}

// ============================================================================
// Inicialização
// ============================================================================
void graphics_init(void) {
    if (ram_buffer != NULL) return;

    vesa_info_t info;
    get_video_info(&info);

    screen_w     = info.width;
    screen_h     = info.height;
    bpp_bytes    = info.bpp / 8;
    if (bpp_bytes == 0) bpp_bytes = 4;
    screen_pitch = info.pitch;
    if (screen_pitch == 0) screen_pitch = screen_w * bpp_bytes;

    size_t buffer_size = (size_t)screen_pitch * screen_h;
    ram_buffer = (uint8_t*)malloc(buffer_size);

    // Se o malloc falhar por falta de RAM na resolução atual, o processo
    // encerra de forma limpa — o Ring 3 não altera o hardware diretamente.
    if (!ram_buffer) {
        sys_exit();
    }

    memset(ram_buffer, 0, buffer_size);
}

void graphics_init_app(int width, int height) {
    if (ram_buffer != NULL) return;

    screen_w     = width;
    screen_h     = height;
    bpp_bytes    = 4;
    screen_pitch = screen_w * bpp_bytes;

    size_t buffer_size = (size_t)screen_pitch * screen_h;
    ram_buffer = (uint8_t*)malloc(buffer_size);

    if (!ram_buffer) {
        sys_exit();
    }

    graphics_clear(0xCCCCCC);
}

// ============================================================================
// IPC e Slots
// ============================================================================
int graphics_get_slot(void) {
    return my_slot;
}

void graphics_set_slot(int slot) {
    my_slot = slot;
}

uint8_t* graphics_get_buffer(void) {
    return ram_buffer;
}

// ============================================================================
// Limpeza e Atualização
// ============================================================================
void graphics_clear(uint32_t color) {
    if (!ram_buffer) return;

    for (int y = 0; y < screen_h; y++) {
        uint8_t* row_ptr = ram_buffer + (y * screen_pitch);

        if (bpp_bytes == 4) {
            uint32_t* row32 = (uint32_t*)row_ptr;
            for (int x = 0; x < screen_w; x++) {
                row32[x] = color;
            }
        } else if (bpp_bytes == 3) {
            for (int x = 0; x < screen_w; x++) {
                int offset = x * 3;
                row_ptr[offset]     = color & 0xFF;
                row_ptr[offset + 1] = (color >> 8) & 0xFF;
                row_ptr[offset + 2] = (color >> 16) & 0xFF;
            }
        }
    }
}

void graphics_update(void) {
    if (ram_buffer) {
        video_flip(ram_buffer);
    }
}

// ============================================================================
// Desenho Direto (atalhos que usam o framebuffer global)
// ============================================================================
void graphics_draw_rect(int x, int y, int w, int h, uint32_t color) {
    sys_draw_rect(x, y, w, h, color);
}

void graphics_fill_rect(int x, int y, int w, int h, uint32_t color) {
    sys_draw_rect(x, y, w, h, color);
}
