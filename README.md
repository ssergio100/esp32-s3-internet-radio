# Rádio Web para ESP32-S3

Firmware de rádio web com saída I2S, display OLED, encoder, LED RGB,
cadastro de estações em FFat e interface HTTP para administração.

## Por onde começar

O arquivo `radio_web_1.ino` é o ponto de entrada e mostra a ordem geral de
inicialização e as atividades coordenadas pelo `loop()`. Os detalhes ficam
separados por responsabilidade:

| Arquivo | Responsabilidade |
| --- | --- |
| `configuracao.h` | Ligações do hardware e parâmetros ajustáveis |
| `audio_radio.cpp` | Reprodução, estado e recuperação do stream |
| `wifi_radio.cpp` | Configuração e supervisão da conexão Wi-Fi |
| `display_radio.cpp` | Telas, relógio e animação do nome da estação |
| `controles.cpp` | Leitura do encoder e do botão |
| `indicador_led.cpp` | Cores e animações do LED RGB |
| `radios.cpp` | Carregamento e validação da lista de estações |
| `relogio.cpp` | Sincronização NTP e obtenção da hora local |
| `servidor_web.cpp` | API HTTP, arquivos da FFat e interface administrativa |
| `telemetria.cpp` | Diagnóstico periódico publicado na porta serial |
| `web/index.html` | Página principal da administração |
| `web/upload.html` | Página completa de manutenção dos arquivos |

Para uma primeira leitura, siga `radio_web_1.ino`, depois o arquivo do módulo
que deseja alterar. Consulte [docs/ARQUITETURA.md](docs/ARQUITETURA.md) antes
de mudar a comunicação entre os módulos.

## Personalização rápida

As opções que normalmente precisam ser adaptadas ficam em
`configuracao.h`. Cada tempo informa sua unidade no sufixo do nome.

| Opção | Efeito | Padrão |
| --- | --- | ---: |
| `BRILHO_LED_RGB` | Intensidade das cores do LED | 50 |
| `VOLUME_PADRAO` | Volume aplicado ao iniciar | 10 |
| `TEMPO_TELA_VOLUME_MS` | Permanência da tela de volume | 2000 ms |
| `TEMPO_INATIVIDADE_SELECAO_MS` | Tempo para cancelar a seleção inativa | 10000 ms |
| `INTERVALO_PASSO_ROLAGEM_NOME_MS` | Intervalo para o nome avançar um pixel | 50 ms |
| `INTERVALO_PISCA_LED_CONEXAO_WIFI_MS` | Intervalo da piscada azul durante a conexão | 100 ms |
| `INTERVALO_TELEMETRIA_SERIAL_MS` | Intervalo entre diagnósticos na serial | 5000 ms |
| `FUSO_HORARIO_UTC_HORAS` | Fuso aplicado ao relógio | -3 horas |

Na rolagem do nome, um intervalo menor produz movimento mais rápido e um
intervalo maior produz movimento mais lento. A mesma relação vale para o
intervalo da piscada do LED. O brilho aceita valores de 0 a 255.
Os servidores NTP primário e secundário também ficam em `configuracao.h`.

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
- pressionar novamente confirma a estação e retorna ao controle de volume;
- após dez segundos sem atividade, a seleção é cancelada e a estação
  anterior permanece ativa.

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

Os arquivos-fonte das interfaces ficam em `web/index.html` e
`web/upload.html`. Para o funcionamento completo, ambos precisam existir na
raiz da partição FFat do dispositivo.

A rota `/upload` tenta servir `/upload.html`. Se o arquivo estiver ausente,
o firmware apresenta um formulário mínimo incorporado que permite restaurar
os arquivos da interface. Esse fallback depende de a FFat ter sido montada;
ele não recupera uma falha de montagem do sistema de arquivos.

O upload aceita nomes simples e faz substituição por arquivo temporário.
`radios.json` recebe validação e backup próprios. Ao migrar uma instalação
existente, envie `upload.html` pela página incorporada atual antes de gravar
uma versão do firmware que passe a servi-lo da FFat.

O sketch mínimo usado para comparação foi isolado em
`examples/radio_web_exemplo_minimo/`. Ele não participa da compilação do
firmware principal e contém apenas placeholders de Wi-Fi.
