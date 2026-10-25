#!/usr/bin/env python3
"""Gera fontes 4-bpp embutidas para a PoC ST7789.

O arquivo TTF e usado apenas nesta etapa offline. O firmware recebe bitmaps de
cobertura e nao depende de fonte ou sistema de arquivos em tempo de execucao.
"""

from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont


SIZES = {
    "pequena": (12, range(32, 256)),
    "normal": (16, range(32, 256)),
    "destaque": (24, range(32, 256)),
    "frequencia": (56, [32, 45, 46, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58]),
}


def pack_nibbles(pixels):
    packed = []
    for index in range(0, len(pixels), 2):
        high = (pixels[index] + 8) // 17
        low = (pixels[index + 1] + 8) // 17 if index + 1 < len(pixels) else 0
        packed.append((high << 4) | low)
    return packed


def glyph(font, codepoint):
    char = chr(codepoint)
    left, top, right, bottom = font.getbbox(char, anchor="ls")
    width = max(0, right - left)
    height = max(0, bottom - top)
    advance = int(round(font.getlength(char)))
    pixels = []
    if width and height:
        image = Image.new("L", (width, height), 0)
        draw = ImageDraw.Draw(image)
        draw.text((-left, -top), char, font=font, fill=255, anchor="ls")
        pixels = list(image.getdata())
    return left, top, width, height, advance, pack_nibbles(pixels)


def emit_array(output, ctype, name, values, per_line=16):
    output.append(f"const {ctype} {name}[] PROGMEM = {{")
    for index in range(0, len(values), per_line):
        chunk = values[index:index + per_line]
        output.append("    " + ", ".join(str(value) for value in chunk) + ",")
    output.append("};")
    output.append("")


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit("uso: generate_aa_fonts.py fonte.ttf [arquivo_saida.cpp]")

    font_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2]) if len(sys.argv) == 3 else Path("ui_tft_fontes_aa.cpp")
    output = [
        '// Gerado por tools/ui_assets/generate_aa_fonts.py; nao editar.',
        '#include "ui_tft_fontes_aa.h"',
        '',
    ]

    for name, (size, codepoints) in SIZES.items():
        font = ImageFont.truetype(str(font_path), size)
        ascent, descent = font.getmetrics()
        bitmap = []
        glyphs = []
        for codepoint in codepoints:
            left, top, width, height, advance, packed = glyph(font, codepoint)
            offset = len(bitmap)
            bitmap.extend(packed)
            glyphs.append((codepoint, offset, width, height, advance, left, top))

        emit_array(output, "uint8_t", f"bitmap_{name}", bitmap)
        output.append(f"const GlifoFonteAa glifos_{name}[] PROGMEM = {{")
        for values in glyphs:
            output.append("    {" + ", ".join(str(value) for value in values) + "},")
        output.extend([
            "};",
            "",
            f"const FonteAa FONTE_AA_{name.upper()} = {{",
            f"    bitmap_{name}, glifos_{name}, {len(glyphs)}, {ascent}, {descent}",
            "};",
            "",
        ])

    output_path.write_text("\n".join(output), encoding="utf-8")


if __name__ == "__main__":
    main()
