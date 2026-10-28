# PoC da interface TFT 320x240

## Escopo desta etapa

A interface apresenta no ST7789 os dados do modelo compartilhado:
Radio Web, Radio FM, volume, relogio, mensagens, configuracao Wi-Fi e alarme.
Ela nao cria menus, nao consulta o TEF6686, o RTC ou o decoder e nao altera a
selecao das fontes de audio.

## Arquitetura

```text
produtores existentes (FM / Web / volume / RTC)
                     |
                     v
          modelo atual de display_radio
                     |
          snapshot fixo EstadoUiTftPoc
                     |
                     v
          DisplayService (unico escritor)
                     |
                     v
              backend TFT ST7789
                     |
           campos persistentes/diff
                     |
            SPI somente se mudou
```

O snapshot e montado sob o mutex ja existente. O desenho TFT acontece depois
que o mutex e liberado. Encoder, ISR e tarefa de
streaming nunca desenham no TFT. O backend compara com o ultimo snapshot e
limpa/redesenha apenas o retangulo cujo valor mudou.

## Hardware e bibliotecas identificados

- Controlador: ST7789, inicializado como 240x320 e rotacionado para 320x240.
- Driver: Adafruit ST7735 and ST7789 Library 1.11.0.
- SPI de hardware a 40 MHz: SCK GPIO10, MOSI GPIO9, CS GPIO8, DC GPIO18,
  RST GPIO15.
- Fontes: Noto Sans convertida offline para bitmaps 4-bpp antialiasing.
- Tema: tokens laranja do handoff, sobre fundo preto.

O firmware anterior e recuperado definindo `RADIO_UI_TFT_POC` como `0` em
`configuracao.h`.

## Validacao obrigatoria no equipamento

1. Iniciar em Relogio e confirmar hora/data, sem cintilacao ou caracteres
   residuais.
2. Alternar entre Relogio, Radio FM e Radio Web pelos encoders; confirmar que
   nenhum passo se perde e que o TFT continua coerente.
3. Em FM, variar sintonia e conferir frequencia, RDS e sinal. Campos que o
   driver nao fornece devem permanecer `-`.
4. Em Web, reproduzir continuamente por pelo menos 30 minutos, girar volume e
   alternar estacoes; observar cortes, buffer, reconexoes e eventos de fluxo
   lento na telemetria.
5. Registrar a linha `TFT PoC` do console. Ela informa duracao
   ultimo/medio/maximo do desenho parcial, latencia entre modelo e tela e delta
   de heap/PSRAM da inicializacao.
6. Testar alarme e chime durante Relogio, FM e Web para confirmar que o TFT nao
   modificou o chaveamento de audio.

Compilacao e metricas do linker nao substituem esses testes fisicos.
