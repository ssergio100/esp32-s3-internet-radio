# Handoff — TFT 320x240 do radio hibrido (ESP32-S3 N16R8)

**Destino:** copie a pasta `docs/ui_handoff/` inteira para `radio_web_1/docs/ui_handoff/` na **branch experimental**, sem substituir arquivos existentes do firmware.

**O que este pacote e:** referencia para o agente (plano, contrato, prototipo web e insumos de UI). **Nao e biblioteca plug-and-play nem firmware pronto.**

## Leia nesta ordem
1. `AGENTE_ETAPA_1.md` — tarefa delimitada que pode ser repassada ao agente hoje.
2. `PLANO_DE_ACAO.md` — fases, criterios de aceite e checkpoints de migracao.
3. `MAPA_DOS_INSUMOS.md` — qual arquivo vai para onde, incluindo os futuros destinos de build.
4. `prototipo_radio_v5.html` — **referencia visual e de interacao**, nao fonte de codigo para Arduino.
5. `kit_referencia/README.md` e `kit_referencia/docs/ESPECIFICACAO.md` — componentes auxiliares ainda nao integrados.

## Regra de preservacao
- Nao substituir a UI OLED existente por enquanto. Se o OLED tiver sido removido fisicamente, preservar o codigo e os dados atuais e criar o TFT como backend alternativo.
- Nao substituir tasks, bibliotecas de audio, acesso TEF6686, Wi-Fi, decodificador, DFPlayer, RTC, encoders e Nixies.
- Nao declarar a migracao pronta ate compilar e testar no hardware real, com audio em reproducao.
- Antes de usar o kit `src/`, verificar API/versao do LVGL instalado; **nao copiar cegamente**.

## Primeira execucao
Passe ao agente o conteudo de `AGENTE_ETAPA_1.md` ou simplesmente solicite: `Execute apenas o que esta em radio_web_1/docs/ui_handoff/AGENTE_ETAPA_1.md. Nao implemente outras fases.`
