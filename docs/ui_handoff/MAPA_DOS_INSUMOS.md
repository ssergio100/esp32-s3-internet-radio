# Mapa de insumos: origem, destino e momento

**Raiz assumida:** `radio_web_1/`. Se o repositorio real usar outra estrutura, agente deve mapear caminhos existentes antes de qualquer copia para o build. Em primeira etapa, todos os arquivos permanecem sob `docs/ui_handoff/`.

| Arquivo deste pacote | Local inicial no projeto | Destino futuro *apos auditoria e PoC* | Observacao |
|---|---|---|---|
| `prototipo_radio_v5.html` | `docs/ui_handoff/prototipo_radio_v5.html` | nao entra no firmware | Referencia visual e navegação (versao v5). |
| `kit_referencia/config/ui_tokens.json` | `docs/ui_handoff/kit_referencia/config/ui_tokens.json` | `tools/ui_assets/config/ui_tokens.json` ou estrutura equivalente | Fonte dos tamanhos e cores. |
| `kit_referencia/tools/generate_theme.py` | `docs/ui_handoff/kit_referencia/tools/generate_theme.py` | `tools/ui_assets/generate_theme.py` | Conferir caminhos relativos ao mover, depois gerar novamente. |
| `kit_referencia/tools/generate_fonts.py` | `docs/ui_handoff/kit_referencia/tools/generate_fonts.py` | `tools/ui_assets/generate_fonts.py` | Requer fonte TTF/OTF local com licenca adequada, versao fixa do lv_font_conv, validacao com LVGL real. |
| `kit_referencia/src/ui_theme_generated.hpp` | `docs/ui_handoff/kit_referencia/src/ui_theme_generated.hpp` | `src/ui/ui_theme_generated.hpp` ou pasta de includes existente | Gerado; nao editar manualmente; nao importar antes da PoC. |
| `kit_referencia/src/ui_state.hpp` | `docs/ui_handoff/kit_referencia/src/ui_state.hpp` | `src/ui/ui_state.hpp` | Estrutura ilustrativa, deve ser adaptada ao modelo de dados real. |
| `kit_referencia/src/ui_text_field.hpp` | `docs/ui_handoff/kit_referencia/src/ui_text_field.hpp` | `src/ui/ui_text_field.hpp` | Depende de LVGL; codigo nao validado no firmware. |
| `kit_referencia/src/ui_web_presenter.hpp` | `docs/ui_handoff/kit_referencia/src/ui_web_presenter.hpp` | `src/ui/ui_web_presenter.hpp` | Apenas na fase Web; nunca chamar na task de rede. |
| `kit_referencia/web/ui_themes.css` | `docs/ui_handoff/kit_referencia/web/ui_themes.css` | opcional para prototipo web | Nao compilar no microcontrolador. |
| `kit_referencia/docs/ESPECIFICACAO.md` | `docs/ui_handoff/kit_referencia/docs/ESPECIFICACAO.md` | permanecer em docs | Orientacoes; nao confundir sugestoes com requisitos ja validados. |

**Arquivos que nao constam no pacote:** fonte TTF/OTF, fontes C convertidas, driver TFT, configuracao de pinos e DMA, task LVGL, implementacao real dos displays, hardware tests. Nao inventar esses detalhes.

**Referencia de funcionalidades:** no projeto `radio_web_1`, identificar os pontos reais de onde saem os valores hoje enviados ao OLED. Consultar `examples/RadioTef6686Mcp23017` apenas como referencia se existir; nao integrar/copiar firmware do exemplo. Checar status do projeto no Git antes de operar.
