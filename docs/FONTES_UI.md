# Fonte da PoC TFT

Os bitmaps 4-bpp antialiasing de `ui_tft_fontes_aa.cpp` foram gerados
offline a partir de Noto Sans Regular. O arquivo TTF nao e necessario no
firmware nem e distribuido neste repositorio.

- Fonte original: Noto Sans Regular
- Copyright: 2010, 2012-2020 Google Inc.; 2015-2020 Google LLC
- Licenca: SIL Open Font License 1.1
- Origem: pacote `fonts-noto-core` da distribuicao usada na geracao

Tamanhos embutidos: 12 px (pequena), 16 px (normal), 24 px
(destaque), 40 px (valor), 56 px (frequência) e 96 px (relógio). As fontes
de frequência e relógio contêm somente os caracteres numéricos usados pelo
firmware; a fonte de valor mantém o conjunto Latin-1 completo para os ajustes.

Para reproduzir o asset:

```text
python3 tools/ui_assets/generate_aa_fonts.py \
  /usr/share/fonts/truetype/noto/NotoSans-Regular.ttf \
  ui_tft_fontes_aa.cpp
```
