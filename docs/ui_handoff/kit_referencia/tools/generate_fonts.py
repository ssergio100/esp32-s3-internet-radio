#!/usr/bin/env python3
"""Generate pre-rasterized LVGL v9 C fonts. Does not bundle any licensed font."""
from pathlib import Path
import subprocess
import argparse
import json

ROOT = Path(__file__).resolve().parents[1]
tokens = json.loads((ROOT / "config/ui_tokens.json").read_text(encoding="utf-8"))
parser = argparse.ArgumentParser(description="Converter fonte TTF/OTF local para LVGL, sem renderizacao TTF no ESP32")
parser.add_argument("font", type=Path, help="Caminho para um arquivo de fonte que voce possa utilizar")
parser.add_argument("--npx", default="npx", help="Executavel npx (precisa de node e lv_font_conv)")
args = parser.parse_args()
font = args.font.resolve()
if not font.is_file():
    parser.error(f"Fonte nao encontrada: {font}")
outdir = ROOT / "fonts/generated"
outdir.mkdir(exist_ok=True)
for name, size in tokens["fonts"].items():
    fontname = f"font_ui_{name}_{size}"
    cmd = [args.npx, "--yes", "lv_font_conv", "--font", str(font),
           "--size", str(size), "--bpp", "4", "--no-compress",
           "--format", "lvgl", "--lv-font-name", fontname]
    if name == "frequency":
        cmd += ["--symbols", "0123456789.,-+"]
    else:
        cmd += ["--range", "0x20-0x7E", "--range", "0xA0-0xFF", "--range", "0x2013-0x2014"]
    cmd += ["-o", str(outdir / (fontname + ".c"))]
    print("Gerando", fontname, flush=True)
    subprocess.run(cmd, check=True)
print("Pronto. Inclua os .c gerados na compilacao do firmware.")
