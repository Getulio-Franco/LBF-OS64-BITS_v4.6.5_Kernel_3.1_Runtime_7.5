/**
============================================================================
PS/2 KEYBOARD DRIVER - V3.3 (LFB/VESA COMPATIBLE) - ABNT2 + DEAD KEYS FULL
============================================================================
Descrição: Gerencia a entrada de dados via IRQ1 (Teclado PS/2).
Localização: drivers/keyboard.c
Mudanças v3.3:
  - Dead keys GENERALIZADAS: ´ (agudo), ~ (til), ^ (circunflexo), ` (crase).
  - compose_accent(): combina acento+vogal nos slots do fonts.c
    (á é í ó ú Á É Í Ó Ú | ã õ Ã Õ ñ Ñ | â ê î ô û | à è ì ò ).
  - Duplo toque na tecla de acento => solta o símbolo isolado (´ ~ ^ `).
  - Caps Lock aplicado ANTES de compor (Caps + ´ + a = Á; Caps + ~ + o = Õ).
  - Sem composição válida => solta acento + tecla (ex.: ~ + m = "~m").
  - Mantém: ç/Ç diretos, toggle ç<->Ç com Caps, buffer circular, VFS stdin.
============================================================================
*/
#include "drivers/keyboard.h"
#include "drivers/vfs.h"
#include "drivers/proc.h"
#include "io.h"
#include <stdint.h>
#include <stddef.h>

#define KEYBOARD_BUFFER_SIZE 256
#define PS2_DATA_PORT 0x60
#define SCANCODE_RELEASE_MASK 0x80
#define SCANCODE_LSHIFT 0x2A
#define SCANCODE_RSHIFT 0x36
#define SCANCODE_CAPS_LOCK 0x3A

/* Códigos internos = slots da fonte fonts.c */
#define CHAR_ACUTE        0xB4  /* ´ símbolo agudo   */
#define CHAR_C_CEDILLA_L  0x87  /* ç */
#define CHAR_C_CEDILLA_U  0x80  /* Ç */
#define CHAR_a_ACUTE      0xA0  /* á */
#define CHAR_e_ACUTE      0x82  /* é */
#define CHAR_i_ACUTE      0xA1  /* í */
#define CHAR_o_ACUTE      0xA2  /* ó */
#define CHAR_u_ACUTE      0xA3  /* ú */
#define CHAR_A_ACUTE      0xC1  /* Á */
#define CHAR_E_ACUTE      0x90  /* É */
#define CHAR_I_ACUTE      0xCD  /* Í */
#define CHAR_O_ACUTE      0xD3  /* Ó */
#define CHAR_U_ACUTE      0xDA  /* Ú */

/* Tipos de acento morto */
#define ACC_NONE  0
#define ACC_ACUTE 1   /* ´ */
#define ACC_TILDE 2   /* ~ */
#define ACC_CIRC  3   /* ^ */
#define ACC_GRAVE 4   /* ` */

static volatile char circular_buffer[KEYBOARD_BUFFER_SIZE];
static volatile int buffer_head = 0;
static volatile int buffer_tail = 0;
static int shift_pressed = 0;
static int caps_lock_active = 0;
static int dead_key_pending = 0;   /* v3.3: qual acento está armado (0 = nenhum) */

/**
 * MAPA DE TECLAS ABNT2 — sem Shift
 */
static unsigned char abnt2_map[128] = {
    [0x00] = 0,    [0x01] = 27,   [0x02] = '1',  [0x03] = '2',  [0x04] = '3',
    [0x05] = '4',  [0x06] = '5',  [0x07] = '6',  [0x08] = '7',  [0x09] = '8',
    [0x0A] = '9',  [0x0B] = '0',  [0x0C] = '-',  [0x0D] = '=',  [0x0E] = '\b',
    [0x0F] = '\t', [0x10] = 'q',  [0x11] = 'w',  [0x12] = 'e',  [0x13] = 'r',
    [0x14] = 't',  [0x15] = 'y',  [0x16] = 'u',  [0x17] = 'i',  [0x18] = 'o',
    [0x19] = 'p',
    [0x1A] = CHAR_ACUTE,        /* dead key: acento agudo ´ */
    [0x1B] = '[',  [0x1C] = '\n',
    [0x1E] = 'a',  [0x1F] = 's',  [0x20] = 'd',  [0x21] = 'f',  [0x22] = 'g',
    [0x23] = 'h',  [0x24] = 'j',  [0x25] = 'k',  [0x26] = 'l',
    [0x27] = CHAR_C_CEDILLA_L,  /* ç */
    [0x28] = '~',               /* dead key: til */
    [0x29] = '\'',
    [0x2B] = ']',
    [0x2C] = 'z',  [0x2D] = 'x',  [0x2E] = 'c',  [0x2F] = 'v',  [0x30] = 'b',
    [0x31] = 'n',  [0x32] = 'm',  [0x33] = ',',  [0x34] = '.',  [0x35] = ';',
    [0x37] = '*',  [0x39] = ' ',
    [0x4A] = '-',
    [0x56] = '\\',
    [0x73] = '/'
};

/**
 * MAPA DE TECLAS ABNT2 — com Shift
 */
static unsigned char abnt2_shift_map[128] = {
    [0x00] = 0,    [0x01] = 27,   [0x02] = '!',  [0x03] = '@',  [0x04] = '#',
    [0x05] = '$',  [0x06] = '%',  [0x07] = '^',  [0x08] = '&',  [0x09] = '*',
    [0x0A] = '(',  [0x0B] = ')',  [0x0C] = '_',  [0x0D] = '+',  [0x0E] = '\b',
    [0x0F] = '\t', [0x10] = 'Q',  [0x11] = 'W',  [0x12] = 'E',  [0x13] = 'R',
    [0x14] = 'T',  [0x15] = 'Y',  [0x16] = 'U',  [0x17] = 'I',  [0x18] = 'O',
    [0x19] = 'P',
    [0x1A] = '`',               /* Shift+´ = dead key crase */
    [0x1B] = '{',  [0x1C] = '\n',
    [0x1E] = 'A',  [0x1F] = 'S',  [0x20] = 'D',  [0x21] = 'F',  [0x22] = 'G',
    [0x23] = 'H',  [0x24] = 'J',  [0x25] = 'K',  [0x26] = 'L',
    [0x27] = CHAR_C_CEDILLA_U,  /* Ç */
    [0x28] = '^',               /* Shift+~ = dead key circunflexo */
    [0x29] = '"',
    [0x2B] = '}',
    [0x2C] = 'Z',  [0x2D] = 'X',  [0x2E] = 'C',  [0x2F] = 'V',  [0x30] = 'B',
    [0x31] = 'N',  [0x32] = 'M',  [0x33] = '<',  [0x34] = '>',  [0x35] = ':',
    [0x37] = '*',  [0x39] = ' ',
    [0x4A] = '-',
    [0x56] = '|',
    [0x73] = '?'
};

/* --- VFS INTERFACE --- */
uint32_t keyboard_vfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (!buffer || size == 0) return 0;
    uint32_t bytes_read = 0;
    while (bytes_read < size) {
        char c = keyboard_pop_char();
        if (c != 0) {
            buffer[bytes_read++] = (uint8_t)c;
            if (c == '\n' || c == '\r') {
                return bytes_read;
            }
        } else {
            force_reschedule();
        }
    }
    return bytes_read;
}
static vfs_ops_t keyboard_ops = { .read = keyboard_vfs_read };
vfs_node_t keyboard_device_node = {
    .name = "stdin",
    .type = VFS_TYPE_CHAR_DEVICE,
    .ops  = &keyboard_ops
};

/* --- GERENCIAMENTO DE BUFFER --- */
void keyboard_init(void) {
    buffer_head = 0;
    buffer_tail = 0;
    shift_pressed = 0;
    caps_lock_active = 0;
    dead_key_pending = 0;
}
char keyboard_pop_char(void) {
    uint64_t flags = cli_save();
    if (buffer_head == buffer_tail) {
        sti_restore(flags);
        return 0;
    }
    char c = circular_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    sti_restore(flags);
    return c;
}
static void keyboard_push_char(char c) {
    int next_pos = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next_pos != buffer_tail) {
        circular_buffer[buffer_head] = c;
        buffer_head = next_pos;
    }
}

/* --- CAPS LOCK (letras ASCII + toggle ç/Ç) --- */
static unsigned char apply_caps_lock(unsigned char key_char) {
    if (key_char == CHAR_C_CEDILLA_L) return caps_lock_active ? CHAR_C_CEDILLA_U : CHAR_C_CEDILLA_L;
    if (key_char == CHAR_C_CEDILLA_U) return caps_lock_active ? CHAR_C_CEDILLA_L : CHAR_C_CEDILLA_U;
    if (caps_lock_active) {
        if (key_char >= 'a' && key_char <= 'z') return key_char - 32;
        if (key_char >= 'A' && key_char <= 'Z') return key_char + 32;
    }
    return key_char;
}

/* ============================================================================
v3.3: MOTOR DE DEAD KEYS (4 acentos)
============================================================================ */
static int accent_of(unsigned char c) {
    if (c == CHAR_ACUTE) return ACC_ACUTE;   /* ´ */
    if (c == '~')        return ACC_TILDE;   /* ~ */
    if (c == '^')        return ACC_CIRC;    /* ^ */
    if (c == '`')        return ACC_GRAVE;   /* ` */
    return ACC_NONE;
}
static unsigned char accent_symbol(int a) {
    switch (a) {
        case ACC_ACUTE: return CHAR_ACUTE;
        case ACC_TILDE: return '~';
        case ACC_CIRC:  return '^';
        case ACC_GRAVE: return '`';
        default:        return 0;
    }
}
/* Combina acento + base nos slots do fonts.c (0 = não compõe) */
static unsigned char compose_accent(int a, unsigned char base) {
    switch (a) {
    case ACC_ACUTE:
        switch (base) {
            case 'a': return CHAR_a_ACUTE;  case 'e': return CHAR_e_ACUTE;
            case 'i': return CHAR_i_ACUTE;  case 'o': return CHAR_o_ACUTE;
            case 'u': return CHAR_u_ACUTE;
            case 'A': return CHAR_A_ACUTE;  case 'E': return CHAR_E_ACUTE;
            case 'I': return CHAR_I_ACUTE;  case 'O': return CHAR_O_ACUTE;
            case 'U': return CHAR_U_ACUTE;
        }
        break;
    case ACC_TILDE:
        switch (base) {
            case 'a': return 0xE3;  case 'o': return 0xF5;   /* ã õ */
            case 'A': return 0xC3;  case 'O': return 0xD5;   /* Ã Õ */
            case 'n': return 0xA4;  case 'N': return 0xA5;   /* ñ Ñ */
        }
        break;
    case ACC_CIRC:
        switch (base) {
            case 'a': return 0x83;  case 'e': return 0x88;  case 'i': return 0x8C;
            case 'o': return 0x93;  case 'u': return 0x96;  /* â ê î ô û */
            case 'A': return 'A';   case 'E': return 'E';   case 'I': return 'I';
            case 'O': return 'O';   case 'U': return 'U';   /* sem slot: derruba */
        }
        break;
    case ACC_GRAVE:
        switch (base) {
            case 'a': return 0x85;  case 'e': return 0x8A;  case 'i': return 0x8D;
            case 'o': return 0x95;  case 'u': return 0x97;  /* à è ì ò ù */
            case 'A': return 'A';   case 'E': return 'E';   case 'I': return 'I';
            case 'O': return 'O';   case 'U': return 'U';
        }
        break;
    }
    return 0;
}

/* --- LÓGICA DE INTERRUPÇÃO DO TECLADO --- */
void keyboard_handler(void) {
    uint8_t scancode = inb(PS2_DATA_PORT);

    if (scancode & SCANCODE_RELEASE_MASK) {
        uint8_t released_key = scancode & 0x7F;
        if (released_key == SCANCODE_LSHIFT || released_key == SCANCODE_RSHIFT) {
            shift_pressed = 0;
        }
    } else {
        if (scancode == SCANCODE_LSHIFT || scancode == SCANCODE_RSHIFT) {
            shift_pressed = 1;
        } else if (scancode == SCANCODE_CAPS_LOCK) {
            caps_lock_active = !caps_lock_active;
        } else if (scancode < 128) {
            unsigned char key_char = shift_pressed ? abnt2_shift_map[scancode]
                                                   : abnt2_map[scancode];
            if (key_char == 0) { outb(0x20, 0x20); return; }

            /* 1. Tecla de acento: arma OU (duplo toque) solta o símbolo */
            int acc = accent_of(key_char);
            if (acc) {
                if (dead_key_pending == acc) {
                    dead_key_pending = 0;
                    if (foreground_process != NULL) keyboard_push_char((char)key_char);
                } else {
                    dead_key_pending = acc;
                }
                outb(0x20, 0x20);
                return;
            }

            /* 2. Havia acento armado: tenta compor */
            if (dead_key_pending) {
                int a = dead_key_pending;
                dead_key_pending = 0;
                unsigned char base = key_char;
                /* Caps ANTES de compor: Caps+´+a = Á ; Caps+~+o = Õ */
                if (caps_lock_active) {
                    if (base >= 'a' && base <= 'z') base -= 32;
                    else if (base == CHAR_C_CEDILLA_L) base = CHAR_C_CEDILLA_U;
                }
                unsigned char comp = compose_accent(a, base);
                if (foreground_process != NULL) {
                    if (comp) {
                        keyboard_push_char((char)comp);
                    } else {
                        keyboard_push_char((char)accent_symbol(a));  /* ~+m = "~m" */
                        keyboard_push_char((char)base);
                    }
                }
                outb(0x20, 0x20);
                return;
            }

            /* 3. Caminho normal */
            key_char = apply_caps_lock(key_char);
            if (foreground_process != NULL) keyboard_push_char((char)key_char);
        }
    }
    outb(0x20, 0x20); /* EOI */
}
