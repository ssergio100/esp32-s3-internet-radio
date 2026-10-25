# Fonte da PoC TFT

Os bitmaps 4-bpp antialiasing de `ui_tft_fontes_aa.cpp` foram gerados
offline a partir de Noto Sans Regular. O arquivo TTF nao e necessario no
firmware nem e distribuido neste repositorio.

- Fonte original: Noto Sans Regular
- Copyright: 2010, 2012-2020 Google Inc.; 2015-2020 Google LLC
- Licenca: SIL Open Font License 1.1
- Origem: pacote `fonts-noto-core` da distribuicao usada na geracao

Para reproduzir o asset:

```text
python3 tools/ui_assets/generate_aa_fonts.py \
  /usr/share/fonts/truetype/noto/NotoSans-Regular.ttf \
  ui_tft_fontes_aa.cpp
```
