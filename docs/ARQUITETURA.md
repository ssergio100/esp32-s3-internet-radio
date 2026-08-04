# Arquitetura do firmware

## Responsabilidades

### Serviço de áudio

`audio_radio.cpp` encapsula a instância de `Audio`. Chamadas como
`connecttohost()`, `stopSong()`, `setVolume()` e `loop()` acontecem somente
na tarefa `AudioService`. Display, encoder e servidor não acessam a
biblioteca diretamente.

Comandos externos são estruturas POD copiadas para uma fila FreeRTOS. O
estado publicado também é POD e é copiado sob mutex. Assim, nenhuma
referência para `String` ou memória temporária cruza tarefas.

Há duas tarefas relacionadas ao áudio:

- decodificador/I2S da ESP32-audioI2S, no núcleo 0;
- conexão, preenchimento e supervisão, no núcleo 1.

Os valores de núcleo, pilha e prioridade ficam em `configuracao.h`.

### Aplicação

`radio_web_1.ino` coordena a inicialização e a interface física. Ele envia
comandos ao serviço e reage às mudanças de estado, sem alimentar o decoder.
Uma operação HTTP lenta pode atrasar a interface, mas não interrompe a
execução do serviço de áudio.

### Persistência

`servidor_web.cpp` administra FFat e as mutações de `radios.json`.
`radios.cpp` carrega uma fotografia da lista durante o boot. Por projeto, a
lista nova entra em uso depois de reiniciar.

## Máquina de estados

```text
parado
  |
  v
conectando -> bufferizando -> tocando
     |             |            |
     +-------------+------------+
                   |
                   v
              reconectando
                   |
                   +----> conectando

tocando -> degradado -> tocando
```

Falha imediata, fim de stream, conexão encerrada ou timeout de preparação
levam a `reconectando`. O intervalo cresce até 30 segundos. Um evento de
stream lento leva a `degradado`. O estado só volta a `tocando` quando o
buffer recupera pelo menos cinco segundos; se isso não acontecer em 30
segundos, a conexão é reiniciada.

O buffer é exposto em bytes e também estimado em tempo:

```text
buffer_ms = bytes_recebidos * 8000 / bitrate_em_bits_por_segundo
```

Tempo de áudio é uma medida mais útil que percentual, pois o mesmo número
de bytes representa durações diferentes em streams de bitrates distintos.

## Recuperação de dados

Para alterações na lista:

```text
JSON novo
   |
   v
radios.tmp -- validação --> radios.json
                               |
                               +--> radios.bak (versão anterior)
```

Uma falha antes da promoção preserva o arquivo ativo. Uma falha durante a
promoção tenta restaurar o backup. O boot aceita somente um array cujos
itens tenham nome e URL HTTP/HTTPS dentro dos limites suportados.

## Limites atuais

- máximo de 50 rádios;
- mínimo de uma rádio na lista persistida;
- nome com até 63 bytes;
- URL com até 511 bytes;
- timeout de 15 segundos para preparar um stream;
- fila de oito comandos de áudio;
- autenticação HTTP ainda não implementada.

Para uso fora de uma rede confiável, autenticação, proteção contra CSRF e
um modo de administração temporário devem ser tratados antes de expor o
servidor.

## Validação em hardware

Após gravar o firmware, o teste de estabilidade recomendado é:

1. tocar continuamente por pelo menos duas horas;
2. alternar entre estações HTTP e HTTPS;
3. acompanhar `/api/v1/status` e a serial;
4. desligar e religar o ponto de acesso para validar recuperação;
5. reiniciar durante uma atualização de `radios.json` e verificar o
   fallback;
6. observar `menor RAM livre`, `maior bloco RAM` e o watermark da pilha.
