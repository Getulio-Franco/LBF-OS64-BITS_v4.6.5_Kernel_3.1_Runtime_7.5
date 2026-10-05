/* ============================================================================
utf_8.c - Conversor UTF-8 -> Codepage da fonte 8x8 do LBF-VESA
Tabela de mapeamento idêntica à do browser.c (consistência entre apps).

Convenção de nomes:
  utf8_pack_cp()  - mapeia UM codepoint Unicode -> slot da fonte
  utf8_to_cp()    - converte STRING UTF-8 -> codepage (a que os apps usam)
============================================================================ */
#include "utf_8.h"
#include <stddef.h>

uint8_t utf8_pack_cp(uint32_t cp) {
    switch (cp) {
        /* cedilha */
        case 0xE7: return 0x87;  case 0xC7: return 0x80;
        /* agudos */
        case 0xE1: return 0xA0;  case 0xC1: return 0xC1;
        case 0xE9: return 0x82;  case 0xC9: return 0x90;
        case 0xED: return 0xA1;  case 0xCD: return 0xCD;
        case 0xF3: return 0xA2;  case 0xD3: return 0xD3;
        case 0xFA: return 0xA3;  case 0xDA: return 0xDA;
        /* circunflexos */
        case 0xE2: return 0x83;  case 0xEA: return 0x88;
        case 0xEE: return 0x8C;  case 0xF4: return 0x93;
        case 0xFB: return 0x96;
        /* crase */
        case 0xE0: return 0x85;
        /* trema */
        case 0xFC: return 0x81;  case 0xDC: return 0x9A;
        case 0xE4: return 0x84;  case 0xC4: return 0x8E;
        case 0xEB: return 0x89;  case 0xEF: return 0x8B;
        case 0xF6: return 0x94;  case 0xD6: return 0x99;
        /* til */
        case 0xE3: return 0xE3;  case 0xC3: return 0xC3;
        case 0xF5: return 0xF5;  case 0xD5: return 0xD5;
        /* graves / outros minúsc. */
        case 0xE8: return 0x8A;  case 0xEC: return 0x8D;
        case 0xF2: return 0x95;  case 0xF9: return 0x97;
        case 0xE5: return 0x86;  case 0xC5: return 0x8F;
        case 0xE6: return 0x91;  case 0xC6: return 0x92;
        case 0xF1: return 0xA4;  case 0xD1: return 0xA5;
        case 0xFF: return 0x98;
        /* acento agudo isolado */
        case 0xB4: return 0xB4;  case 0xA9: return 0xA9;
        /* símbolos com slot próprio */
        case 0xAE: return 0xAE;  case 0xB0: return 0xB0;
        case 0xB1: return 0xB1;  case 0xB2: return 0xB2;
        case 0xB3: return 0xB3;  case 0xB5: return 0xB5;
        case 0xBF: return 0xBF;  case 0xB7: return 0xB7;
        case 0xA7: return 0xA7;  case 0xAA: return 0xAA;
        case 0xBA: return 0xBA;  case 0xAB: return 0xAB;
        case 0xBB: return 0xBB;  case 0xAC: return 0xAC;
        case 0xA1: return 0xA6;  case 0xA0: return ' ';
        default:
            if (cp < 0x80) return (uint8_t)cp;
            if (cp >= 0xC0 && cp <= 0xDE) return (uint8_t)(cp - 0x20);
            return '?';
    }
}

void utf8_to_cp(const char* utf8, char* out, int max) {
    if (!out || max <= 0) return;
    int o = 0;
    if (!utf8) { out[0] = '\0'; return; }

    while (*utf8 && o < max - 1) {
        unsigned char ub = (unsigned char)*utf8;

        if (ub < 0x80) {
            /* ASCII puro: 1 byte */
            out[o++] = (char)ub;
            utf8++;
        }
        else if (ub >= 0xC2 && ub <= 0xDF &&
                 (utf8[1] & 0xC0) == 0x80) {
            /* Sequência de 2 bytes (U+0080..U+07FF) */
            uint32_t cp = ((uint32_t)(ub & 0x1F) << 6) |
                          ((unsigned char)utf8[1] & 0x3F);
            out[o++] = (char)utf8_pack_cp(cp);
            utf8 += 2;
        }
        else if (ub >= 0xE0 && ub <= 0xEF &&
                 (utf8[1] & 0xC0) == 0x80 &&
                 (utf8[2] & 0xC0) == 0x80) {
            /* Sequência de 3 bytes (U+0800..U+FFFF) */
            uint32_t cp = ((uint32_t)(ub & 0x0F) << 12) |
                          ((uint32_t)((unsigned char)utf8[1] & 0x3F) << 6) |
                          ((unsigned char)utf8[2] & 0x3F);
            out[o++] = (char)utf8_pack_cp(cp);
            utf8 += 3;
        }
        else if (ub >= 0xF0 && ub <= 0xF4 &&
                 (utf8[1] & 0xC0) == 0x80 &&
                 (utf8[2] & 0xC0) == 0x80 &&
                 (utf8[3] & 0xC0) == 0x80) {
            /* Sequência de 4 bytes (U+10000..U+10FFFF) - raramente usada */
            /* Mapeia para '?' pois a fonte 8x8 não tem esses glifos */
            out[o++] = '?';
            utf8 += 4;
        }
        else {
            /* Byte inválido / isolado: pula e emite '?' */
            out[o++] = '?';
            utf8++;
        }
    }
    out[o] = '\0';
}
