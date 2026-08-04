# Rádio Web para ESP32-S3

Firmware de rádio web com saída I2S, display OLED, encoder, LED RGB,
cadastro de estações em FFat e interface HTTP para administração.

## Arquitetura

O fluxo de áudio não é executado pelo `loop()` principal. O módulo
`audio_radio.cpp` mantém uma tarefa dedicada, recebe comandos por uma fila
FreeRTOS e é o único proprietário das operações da biblioteca de áudio.
A tarefa interna de decodificação da ESP32-audioI2S usa o núcleo 0; o serviço
que alimenta e supervisiona o stream usa o núcleo 1.

O `loop()` principal fica responsável por:

- display e encoder;
- atendimento do servidor HTTP;
- apresentação do estado publicado pelo serviço de áudio;
- telemetria periódica.

O serviço de áudio publica estados explícitos (`conectando`,
`bufferizando`, `tocando`, `degradado`, `reconectando` e `erro`) e usa
reconexão progressiva de 1, 2, 5, 10 e 30 segundos. Se o Wi-Fi cair, ele
aguarda a rede voltar antes de abrir uma nova conexão.

Veja os detalhes em [docs/ARQUITETURA.md](docs/ARQUITETURA.md).

## Indicação do LED

| Cor | Estado |
| --- | --- |
| Azul | Inicializando, conectando ou formando buffer |
| Verde | Tocando normalmente |
| Amarelo | Stream degradado |
| Vermelho | Reconectando ou em erro |
| Apagado | Áudio parado |

As cores indicam o estado operacional, não apenas uma leitura instantânea
do percentual do buffer.

## Encoder

O modo de repouso é sempre o controle de volume:

- girar o encoder mostra e altera o volume;
- após dois segundos, o display volta ao nome da rádio;
- pressionar o encoder abre a seleção de estações;
- girar escolhe a estação;
- após um segundo sem movimento, a escolha é confirmada e o encoder retorna
  automaticamente ao volume;
- pressionar novamente antes da confirmação cancela a seleção.

## Lista de rádios

O arquivo ativo é `/radios.json`, armazenado na partição FFat. Alterações
feitas pela API ou o upload específico desse arquivo usam escrita
transacional:

1. o novo JSON é gravado em arquivo temporário;
2. conteúdo, limites e URLs são validados;
3. a versão íntegra anterior vira `/radios.bak`;
4. o temporário é promovido para `/radios.json`.

Na inicialização, o firmware tenta o arquivo ativo, depois o backup e por
último a lista de reserva compilada no firmware.

## Diagnóstico

Com o dispositivo conectado à rede:

```text
GET http://IP_DO_ESP32/api/v1/status
```

A resposta JSON contém estado do áudio, rádio, título, codec, bitrate,
buffer estimado em milissegundos, eventos de stream lento, tentativas de
reconexão, RSSI, heap, PSRAM e uptime. A porta serial também imprime um
resumo a cada cinco segundos.

## Compilação

As versões validadas e a configuração da placa estão em
[docs/DEPENDENCIAS.md](docs/DEPENDENCIAS.md). Com o Arduino CLI e as
dependências instaladas:

```sh
./scripts/compilar.sh
```

Se o executável não estiver no `PATH`:

```sh
ARDUINO_CLI_BIN=/caminho/para/arduino-cli ./scripts/compilar.sh
```

Os artefatos são gerados em `build/firmware`. A gravação no hardware não é
feita automaticamente pelo script.

O perfil `esp32s3` de `sketch.yaml` registra a configuração completa e pode
provisionar uma máquina nova. Esse modo permite que o Arduino CLI instale as
versões fixadas; o script rotineiro apenas usa o ambiente já instalado.

## Interface web

O arquivo-fonte da interface principal fica em `web/index.html`; a página
de upload de manutenção está incorporada ao firmware. Os arquivos web
precisam existir na partição FFat do dispositivo. O upload aceita nomes
simples e faz substituição por arquivo temporário. `radios.json` recebe
validação e backup próprios.

O sketch mínimo usado para comparação foi isolado em
`examples/radio_web_exemplo_minimo/`. Ele não participa da compilação do
firmware principal e contém apenas placeholders de Wi-Fi.
