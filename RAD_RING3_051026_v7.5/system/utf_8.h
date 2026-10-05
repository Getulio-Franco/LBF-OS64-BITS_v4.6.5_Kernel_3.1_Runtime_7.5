/* ============================================================================
utf_8.h - Conversor UTF-8 -> Codepage da fonte 8x8 do LBF-VESA
Módulo compartilhado: linkar utf_8.o em todos os software.elf.

Convenção de nomes:
  utf8_pack_cp()  - mapeia UM codepoint Unicode para o slot da fonte
  utf8_to_cp()    - converte STRING UTF-8 para codepage (a que os apps usam)
  cp_to_utf8()    - (futuro) inverso: codepage -> UTF-8
============================================================================ */
#ifndef UTF_8_H
#define UTF_8_H

#include <stdint.h>

/*
 * utf8_pack_cp: mapeia um codepoint Unicode (U+0000..U+FFFF) para o slot
 * correspondente na fonte bitmap 8x8 do LBF-VESA.
 * Retorna '?' (0x3F) se o codepoint não tiver glifo mapeado.
 *
 * Uso direto: raramente — normalmente use utf8_to_cp() para strings.
 */
uint8_t utf8_pack_cp(uint32_t cp);

/*
 * utf8_to_cp: converte uma string UTF-8 (literal no código-fonte ou lida de
 * arquivo) para a codepage da fonte 8x8, escrevendo no buffer 'out'.
 *
 * A string de saída sempre é NUL-terminada (se max > 0).
 *
 * Exemplo de uso:
 *   char buf[64];
 *   utf8_to_cp("AVANÇAR >>", buf, sizeof(buf));
 *   BtnAvanca = GUI_CreateButton(..., buf, ...);
 */
void utf8_to_cp(const char* utf8, char* out, int max);

#endif /* UTF_8_H */
