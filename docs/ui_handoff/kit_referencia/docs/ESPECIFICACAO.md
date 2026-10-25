# Interface do radio hibrido (320 x 240, horizontal)

## Objetivo
Reusar assets, estilo, navegacao e renderizacao no ESP32-S3 N16R8; manter encoder e radios responsivos. O wireframe HTML v5 define aparencia, nao o codigo do firmware.

## Premissas
- ESP32-S3 N16R8: 16MB Flash, 8MB PSRAM OPI.
- LCD 320 x 240, controlador e barramento ainda nao definidos.
- 4 Nixies fisicos mostram hora. TFT apaga no modo relogio.
- LVGL v9 para desenho e fontes 4 bpp convertidas offline.
- Fontes C geradas pelo projeto, temas RGB565 gerados do mesmo arquivo JSON do CSS web.

## Regras dos encoders
- Encoder A: girar navega, pressionar confirma; no relogio com TFT apagado, girar altera brilho 0..10 dos Nixies e apresenta overlay temporario.
- Encoder B: girar ajusta volume, pressionar volta; na edicao manual da hora, girar altera valor e pressionar alterna entre hora e minuto.
- Encoder B longo: voltar ao relogio e apagar TFT (enquanto nao estiver em fluxo que necessite confirmacao).
- Menu principal contem FM, Web, Alarmes, Chimes, Configuracoes.

## Estado WEB (regra de desempenho funcional)
- Girar encoder A: muda nome de emissora destacada, sem iniciar streaming; metadados, codec, bitrate, RSSI e buffer da PRE-SELECAO ficam '-'.
- Radio atual continua tocando. Pressionar A: confirma, inicia streaming assincrono; somente depois alimenta metadados de rede.
- Pressionar B: cancela pre-selecao.
- Importante: ao exibir dados da PRE-SELECAO, nunca misturar telemetria da emissora que ainda toca.

## Paletas e fontes
- Uma fonte de verdade: config/ui_tokens.json; rodar tools/generate_theme.py.
- Gerar 4 tamanhos de fontes 4bpp offline sem compressao (na primeira versao), fontes opcionais nao sao incluidas.
- Todos os caracteres textuais sao desenhados como glifos alpha (sem bitmap RGB precolorido): recoloracao dos temas e barata.

## Atualizacao eficiente
- Criar widgets uma unica vez ao entrar em cada tela; reutiliza-los ate sair.
- Alterar label so se novo conteudo for diferente do exibido (exemplo src/ui_text_field.hpp).
- Usar LVGL renderizacao PARCIAL + dois draw buffers RGB565 para transferir via DMA, se driver permitir.
- Comecar com 320 x 24 linhas x 2 bytes = 15.360 B por draw buffer (30.720 B para dois).
- Escolher buffers no heap apropriado ao DMA do controlador; nao pressupor PSRAM diretamente acessivel pelo SPI DMA.
- Desabilitar animacoes/scrolling no inicio; ajustar depois de medir.
- Tela em repouso: nenhum refresh periodico desnecessario, somente evento de overlay de brilho.

## Prioridades e cadencia inicial de atualizacao (ajustar apos medir)
- Encoder: captura imediata por PCNT ou ISR de GPIO; tarefa dedicada consome eventos (nao bloquear com SPI/Wi-Fi).
- Loop da UI: manter LVGL handler responsivo; nao impor uma taxa fixa de full-frame.
- Barra de buffer WEB: 4 a 5 atualizacoes por segundo (ou so quando mudar em granulos uteis).
- Intensidade FM: 4 a 5 atualizacoes por segundo.
- RDS e musica: so quando texto mudar.
- Volume: atualizar na mudanca, sem animacao custosa.
- Nixies: atualizar digitos quando necessario (nao depende do frame rate do TFT).

## Task ownership
- Task de audio/web/tuner: atualiza snapshot/posta evento em fila; nunca chama lv_... diretamente.
- Task UI: consome os eventos; transforma dados em viewmodel e atualiza widgets LVGL; executa o lv_timer_handler().
- Encoder ISR/PCNT: escreve contagens/eventos simples; jamais desenha.
- Se usar LVGL de multiplas tasks, mutex obrigatorio; melhor um unico dono da interface.
- Streaming e decodificacao de audio devem seguir independentes do flush de tela.

## Meta inicial de desempenho (medir no hardware, nao promessa)
- 95% dos eventos de encoder refletidos visualmente em ate 50 ms, inclusive sob streaming.
- Nenhum evento perdido em sequencias rapidas (valido mediante medicao fisica e log).
- Sem interrupcao de audio ou underruns por navegacao ou RDS.
- SPI 40 MHz: full-screen RGB565 teorico ~30,7ms so em transferencia, antes do overhead. Evitar full-screen continuo.

## Pendencias antes do driver real
- Controlador do LCD (ILI9341/ST7789/...), interface SPI ou paralela, frequencia estavel, pinos, ordem RGB/BGR.
- LVGL 9.x concreto e versao da biblioteca de display/painel.
- Selecionar familia tipografica licenciada pelo projeto e gerar fontes com script.
