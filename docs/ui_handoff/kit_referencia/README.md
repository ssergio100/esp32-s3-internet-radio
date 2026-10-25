# Kit de insumos para interface do Radio Hibrido — v0.1

Este kit **nao e um firmware completo**. E a base reutilizavel para portabilidade do wireframe HTML ao ESP32-S3, usando LVGL 9. Evita desenhar fonte com TTF em tempo de execucao e torna cores e componentes padronizados.

## Arquivos
- `config/ui_tokens.json`: fonte unica de cores, resolucao, dimensoes e tamanhos de fontes.
- `tools/generate_theme.py`: gera `src/ui_theme_generated.hpp` (RGB565) e `web/ui_themes.css` da mesma configuracao.
- `tools/generate_fonts.py`: gera quatro fontes C 4-bpp LVGL (precisa de fonte TTF/OTF local e `npx lv_font_conv`). **Nenhum arquivo de fonte e incluido.**
- `src/ui_state.hpp`: structs de estados; separam dados de radio da interface.
- `src/ui_text_field.hpp`: label reutilizavel que so atualiza se o texto mudar.
- `src/ui_web_presenter.hpp`: exemplo de apresentacao da radio Web com pre-selecao sem transferencia de dados.
- `docs/ESPECIFICACAO.md`: regras, cadencia de atualizacao, fluxo e metas mensuraveis.

## Preparacao
```bash
python3 tools/generate_theme.py
python3 tools/generate_fonts.py /caminho/para/sua-fonte.ttf
```
Para as fontes: requer Node.js e acesso ao pacote `lv_font_conv`, que deve ter sua versao fixada nas dependencias do firmware final, visando builds reproduziveis.

## Exemplo de uso na task LVGL
```cpp
// Ponteiros para labels ja criados ao abrir a tela Web
radio_ui::WebPresenter web(lblStation, lblTitle, lblCodec, lblBitrate, lblWifi, lblBuffer);

// Quando chega um snapshot novo a TASK DA UI:
web.apply(snapshot.web); // so atualiza widgets cujos textos mudaram
```
Nao chamar do callback do encoder, ISR ou task de streaming.

## Fora do escopo
Inicializacao do barramento LCD, alocacao dos buffers DMA, `lv_display_set_flush_cb()`, fontes em disco, implementacao de task/queue e regras finais da maquina de estados. Dependem do driver LCD e da stack (Arduino/LovyanGFX/ESP-IDF) ainda nao especificados.
