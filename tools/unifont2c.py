#!/usr/bin/env python3
"""Converte um subconjunto do GNU Unifont (.hex) em uma tabela C.

Formato .hex: "CODEPOINT:BITMAP" onde o bitmap tem 32 dígitos (glifo 8x16)
ou 64 dígitos (glifo 16x16), linha a linha, bit mais significativo = coluna
mais à esquerda.

Para cada glifo também calculamos a primeira e a última coluna com pixels,
o que permite desenhar o texto da interface de forma proporcional.
"""
import sys


def main(src, dst):
    glyphs = []
    with open(src, encoding="ascii") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            cp_hex, bitmap = line.split(":")
            cp = int(cp_hex, 16)
            wide = len(bitmap) == 64
            step = 4 if wide else 2
            rows = []
            for y in range(16):
                v = int(bitmap[y * step:(y + 1) * step], 16)
                rows.append(v if wide else v << 8)  # sempre MSB = coluna 0
            used = 0
            for r in rows:
                used |= r
            width = 16 if wide else 8
            cols = [c for c in range(width) if used & (0x8000 >> c)]
            left, right = (cols[0], cols[-1]) if cols else (0, 0)
            glyphs.append((cp, wide, left, right, bool(cols), rows))

    glyphs.sort(key=lambda g: g[0])
    with open(dst, "w", encoding="utf-8") as out:
        out.write("/* Gerado por tools/unifont2c.py a partir do GNU Unifont. Não editar. */\n")
        out.write('#include "gui/font.h"\n\n')
        out.write("const glyph_t font_glyphs[] = {\n")
        for cp, wide, left, right, has, rows in glyphs:
            r = ",".join("0x%04x" % v for v in rows)
            out.write("    {0x%04x, %d, %d, %d, %d, {%s}},\n" % (cp, wide, left, right, has, r))
        out.write("};\n")
        out.write("const int font_glyph_count = %d;\n" % len(glyphs))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
