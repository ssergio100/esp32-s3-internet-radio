# Rádio Web para ESP32-S3

Firmware de rádio web com Player MP3, alarmes, saída I2S, display OLED,
encoder, LED RGB, persistência em FFat e interface HTTP para administração.

## Por onde começar

O arquivo `radio_web_1.ino` é o ponto de entrada e mostra a ordem geral de
inicialização e as atividades coordenadas pelo `loop()`. Os detalhes ficam
separados por responsabilidade:

| Arquivo | Responsabilidade |
| --- | --- |
| `configuracao.h` | Ligações do hardware e parâmetros ajustáveis |
| `alarmes.cpp` | Regras em RAM, agendamento e som padrão dos alarmes |
| `api_alarmes.cpp` | Cadastro, edição e exclusão dos alarmes pela rede |
| `api_radios.cpp` | Operações HTTP para listar, adicionar e excluir estações |
| `api_status.cpp` | Montagem do diagnóstico JSON solicitado pela rede |
| `audio_radio.cpp` | Serviço exclusivo para rádio web ou arquivo local |
| `wifi_radio.cpp` | Configuração e supervisão da conexão Wi-Fi |
| `display_radio.cpp` | Telas dos estados Rádio Web, Player e Relógio |
| `controles.cpp` | Leitura do encoder e do botão |
| `indicador_led.cpp` | Cores e animações do LED RGB |
| `player.cpp` | Montagem do microSD e catálogo MP3 de `/sons` |
| `radios.cpp` | Lista de estações em memória e reserva compilada |
| `persistencia_radios.cpp` | Validação e gravação segura dos arquivos de rádios |
| `persistencia_alarmes.cpp` | Validação e gravação segura dos arquivos de alarmes |
| `relogio.cpp` | Política entre RTC, sincronização NTP e hora do sistema |
| `relogio_rtc.cpp` | Acesso ao DS3231 por meio da RTClib |
| `servidor_web.cpp` | Montagem da FFat, inicialização do servidor e mapa das rotas HTTP |
| `telemetria.cpp` | Diagnóstico periódico publicado na porta serial |
| `upload_arquivos.cpp` | Recebimento e substituição segura de arquivos enviados |
| `web/index.html` | Página principal da administração |
| `web/alarmes.html` | Cadastro de alarmes semanais e de data única |
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
| `BSSIDS_WIFI_BLOQUEADOS` | Pontos de acesso que o rádio deve ignorar | `DC:33:3D:F9:C0:34` |
| `VOLUME_PADRAO` | Volume aplicado ao iniciar | 10 |
| `TEMPO_BARRA_VOLUME_MS` | Permanência do volume na barra inferior | 2000 ms |
| `TEMPO_INATIVIDADE_SELECAO_MS` | Tempo para cancelar a seleção inativa | 10000 ms |
| `TEMPO_CLIQUE_LONGO_ENCODER_MS` | Pressão necessária para levar uma fonte ao Relógio | 2000 ms |
| `INTERVALO_PASSO_ROLAGEM_NOME_MS` | Intervalo para o nome avançar um pixel | 40 ms |
| `INTERVALO_PASSO_ROLAGEM_PLAYER_MS` | Intervalo para o nome do arquivo avançar um pixel | 80 ms |
| `INTERVALO_PASSO_ROLAGEM_DIAGNOSTICO_MS` | Intervalo para o diagnóstico avançar um pixel | 13 ms |
| `INTERVALO_ATUALIZACAO_DIAGNOSTICO_DISPLAY_MS` | Renovação dos valores exibidos no diagnóstico | 1000 ms |
| `INTERVALO_PASSO_ROLAGEM_DATA_RELOGIO_MS` | Intervalo para a data do relógio avançar um pixel | 80 ms |
| `INTERVALO_PISCA_LED_CONEXAO_WIFI_MS` | Intervalo da piscada azul durante a conexão | 100 ms |
| `INTERVALO_TELEMETRIA_SERIAL_MS` | Intervalo entre diagnósticos na serial | 5000 ms |
| `TRANSICOES_ENCODER_POR_DETENTE` | Calibração do movimento físico do encoder | 4 |
| `FUSO_HORARIO_UTC_HORAS` | Fuso aplicado ao relógio | -3 horas |
| `INTERVALO_SINCRONIZACAO_NTP_MS` | Intervalo entre correções pela rede | 3600000 ms |
| `DESVIO_MINIMO_AJUSTE_RTC_SEGUNDOS` | Diferença mínima para regravar o DS3231 | 2 segundos |
| `FREQUENCIA_CARTAO_PLAYER_HZ` | Frequência SPI do microSD | 4000000 Hz |
| `REPRODUCAO_SEQUENCIAL_PLAYER` | Inicia automaticamente o próximo MP3 ao terminar | `true` |
| `DURACAO_MAXIMA_ALARME_MINUTOS` | Limite de repetição de um alarme | 30 minutos |

Nas rolagens do nome e do diagnóstico, um intervalo menor produz movimento
mais rápido e um intervalo maior produz movimento mais lento. A mesma relação
vale para o intervalo da piscada do LED. O brilho aceita valores de 0 a 255.
Os servidores NTP primário e secundário também ficam em `configuracao.h`.

O sketch independente
`examples/teste_ds3231sn/teste_ds3231sn.ino` valida o RTC DS3231SN pela porta
serial antes de sua integração ao firmware. Ele compartilha com o OLED o I2C
em `SDA=GPIO17` e `SCL=GPIO18`, identifica os dispositivos do barramento e lê
data, hora, temperatura e o indicador de perda de alimentação.

A organização física reserva oito pinos na parte inferior do mesmo lado da
placa para o futuro driver das Nixies: `GPIO8`, `GPIO3`, `GPIO9` e `GPIO10`
formarão o barramento BCD, enquanto `GPIO11` a `GPIO14` selecionarão os quatro
ânodos independentemente, com um GPIO dedicado para cada válvula. Os ânodos
não usam conversão binária de dois para quatro. O I2C fica logo acima em
`GPIO17/18`; o sinal `DIN` do I2S foi movido para o `GPIO4` e o botão do
encoder para o `GPIO7`. O pino de strapping
`GPIO46`, situado entre os grupos físicos, permanece sem conexão.
O microSD do Player usa `SCK=GPIO42`, `MISO=GPIO41`, `MOSI=GPIO40` e
`CS=GPIO39`.

Permanece como pendência implementar o driver das Nixies nesses oito pinos.
Até essa etapa, o firmware não configura nem aciona `GPIO8`, `GPIO3` e
`GPIO9` a `GPIO14` como parte do relógio. A implementação deverá primeiro ser
validada em um exemplo independente, incluindo a saída BCD de quatro bits, a
seleção direta dos quatro ânodos, o brilho e a multiplexação por timer do
ESP32-S3.

No firmware principal, o DS3231 conserva o horário em UTC e inicia o relógio
do sistema antes da conexão Wi-Fi. O SNTP continua sendo a referência de
precisão: após uma sincronização confirmada, o `loop()` corrige o RTC somente
se ele perdeu a referência ou se o desvio chegou ao limite configurado. Fuso e
horário de verão são aplicados apenas ao apresentar a hora local.

O Wi-Fi aceita normalmente redes novas configuradas pelo portal. Quando há
mais de um ponto para o SSID salvo, a reconexão escolhe o sinal mais forte cujo
BSSID não esteja em `BSSIDS_WIFI_BLOQUEADOS`. A busca é assíncrona e ocorre
somente durante a conexão ou enquanto o rádio está sem Wi-Fi.

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

O equipamento possui três estados operacionais: `Rádio Web`, `Player` e
`Relógio`. Em `Rádio Web`, os serviços de rede funcionam normalmente. Em
`Player`, Wi-Fi e servidor permanecem desligados e o mesmo decoder/I2S reproduz
progressivamente um MP3 de `/sons`. Em `Relógio`, a fonte e sua tarefa ficam
suspensos, o servidor é interrompido, o Wi-Fi é desligado e o LED permanece
apagado; somente relógio, encoder e OLED continuam sendo processados.

Os alarmes são uma sobreposição temporária aos três estados. Um alarme
interrompe a fonte atual, repete seu arquivo até o clique curto no encoder ou o
limite de 30 minutos e então restaura o estado anterior. Um novo disparo
substitui definitivamente o alarme em execução. Quando vários coincidem no
mesmo minuto, vence o cadastro de maior `id`.

O serviço de áudio publica estados explícitos (`conectando`,
`bufferizando`, `tocando`, `degradado`, `reconectando` e `erro`) e usa
reconexão progressiva de 1, 2, 5, 10 e 30 segundos. Se o Wi-Fi cair, ele
aguarda a rede voltar antes de abrir uma nova conexão.

Veja os detalhes em [docs/ARQUITETURA.md](docs/ARQUITETURA.md).

Na tela normal da rádio, a faixa superior percorre para a direita mostrando
data e hora locais, codec, bitrate, reserva de áudio em segundos, RSSI e BSSID.
O horário vem do relógio do sistema, iniciado pelo DS3231 e posteriormente
corrigido pelo NTP; a atualização visual não faz leituras I2C periódicas do RTC.
Os demais valores são uma fotografia passiva do serviço de áudio e do Wi-Fi: o
display não controla o decoder nem interfere na escolha do ponto de acesso.
Na faixa inferior invertida, a reserva permanece visível à esquerda e a posição
da estação na lista aparece à direita. Ao ajustar o volume, a reserva dá lugar
temporariamente à barra de nível e o valor atual, como `10/21`, substitui a
posição da estação.

A faixa central é a única área usada para o conteúdo principal: mostra o nome
rolante da rádio e, durante a abertura do stream, os estados `Conectando...` e
`Bufferizando...`, estáticos e em fonte pequena. Na seleção, o nome fica parado
e usa uma fonte menor quando não cabe no tamanho normal. Ao confirmar e iniciar
a reprodução, o nome retorna ao tamanho normal e à rolagem.

No estado `Relógio`, o OLED mostra `HH:MM` em quatro cartões grandes com uma
divisão horizontal inspirada em mostradores flip. O rodapé percorre a data
completa em português, por exemplo `sexta, 12 de agosto de 2026`.
Ao girar o encoder, essa interface é substituída integralmente por `PLAYER` ou
`RADIO WEB` em letras grandes. Um clique curto ativa a opção mostrada.

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

O firmware usa um encoder com `CLK`, `DT` e botão nos GPIOs 15, 16 e 7. Nos
estados de áudio, seu modo de repouso é o controle de volume:

- girar o encoder altera o volume e o mostra na barra inferior;
- após dois segundos, a barra volta a mostrar buffer e posição da estação;
- pressionar o encoder inicia a seleção na própria faixa central;
- girar escolhe a estação;
- pressionar novamente confirma a estação e retorna ao controle de volume;
- após dez segundos sem atividade, a seleção é cancelada e a estação
  anterior permanece ativa.
- manter o botão pressionado por dois segundos em Rádio Web ou Player leva ao
  `Relógio`;
- no Relógio, o giro substitui toda a tela pelas opções `PLAYER`, `RADIO WEB`
  e pela própria tela do relógio;
- um clique curto ativa a opção exibida; clique longo no Relógio não executa
  ação.

No Player, um clique curto abre a lista de até 100 arquivos MP3 diretamente em
`/sons`; o giro navega e outro clique começa a reprodução. Fora da lista, o
giro controla o volume. O Player não carrega o arquivo inteiro em memória e não
executa simultaneamente com a rádio web.
Com `REPRODUCAO_SEQUENCIAL_PLAYER = true`, o fim de uma faixa inicia a seguinte
na ordem alfabética e a última volta para a primeira. Uma escolha manual passa a
ser a faixa atual e, portanto, também o novo ponto da sequência.
O catálogo é criado uma vez durante o boot, antes do início do serviço de áudio,
e permanece em memória. Assim, a página de alarmes consegue listar os MP3 sem
varrer o cartão enquanto a rádio toca. Alterações físicas no conteúdo do cartão
exigem reinicialização nesta primeira versão.

## Alarmes

A página `/alarmes` cadastra alarmes semanais, com horário e dias da semana, ou
de execução única, com data e horário. O JSON não armazena um campo de tipo: a
presença de `dias` identifica a repetição semanal e a presença de `data`
identifica uma execução única. Os arquivos ativos, temporário e backup são
`/alarmes.json`, `/alarmes.tmp` e `/alarmes.bak`.

O arquivo escolhido deve ser um MP3 diretamente em `/sons`. Se não houver
arquivo configurado, cartão ou arquivo válido, o firmware usa
`/alarme_padrao.wav`, gerado automaticamente na FFat. Alarmes únicos são
desativados ao disparar; alarmes únicos já vencidos também são desativados pelo
agendador. Não há histórico nem cálculo de próxima execução na interface.

A rotação usa diretamente o deslocamento informado pela biblioteca do encoder,
sem aceleração ou filtro de direção. Se vários passos forem acumulados entre
duas passagens do `loop()`, todos são aplicados. O valor
`TRANSICOES_ENCODER_POR_DETENTE` está calibrado em `4` para o componente
instalado.

O estado `Relógio` encerra o stream e suspende a tarefa de serviço, mas a
biblioteca ESP32-audioI2S não expõe ao firmware a parada pública do I2S. Sem um
controle elétrico de `SD_MODE`, não se deve interpretar esse estado como
shutdown de baixo consumo dos amplificadores.

## Lista de rádios

O arquivo ativo é `/radios.json`, armazenado na partição FFat. Alterações
feitas pela API ou o upload específico desse arquivo usam escrita
transacional:

1. o novo JSON é gravado em arquivo temporário;
2. conteúdo, limites e URLs são validados;
3. a versão íntegra anterior vira `/radios.bak`;
4. o temporário é promovido para `/radios.json`.

`persistencia_radios.cpp` concentra os nomes desses arquivos, a validação do
JSON e essa transação. `api_radios.cpp` interpreta as requisições HTTP, enquanto
`radios.cpp` monta a lista em memória usada durante a execução.

Na inicialização, o firmware aplica a mesma validação usada pela API: tenta
`radios.json`, depois `radios.bak` e por último a lista de reserva compilada.
Um arquivo é rejeitado por inteiro se o JSON, a quantidade, um nome ou uma URL
forem inválidos. A serial informa o arquivo escolhido e quantas estações foram
carregadas.

## Diagnóstico

Com o dispositivo conectado à rede:

```text
GET http://IP_DO_ESP32/api/v1/status
```

A resposta JSON contém estado do áudio, rádio, título, codec, bitrate,
buffer estimado em milissegundos, eventos de stream lento, tentativas de
reconexão, estado dos alarmes, disponibilidade do som padrão, RSSI, heap, PSRAM
e uptime. A porta serial também imprime um resumo a cada cinco segundos.

O endpoint responde a consultas feitas por outro equipamento da rede. O
ESP32 não faz uma requisição para si mesmo: `api_status.cpp` apenas cria a
fotografia JSON quando `servidor_web.cpp` recebe um pedido externo.

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

A Arduino IDE 2 ainda não importa automaticamente desse arquivo a placa e as
opções dos menus. Na primeira abertura pela IDE, selecione `ESP32S3 Dev Module`
e reproduza as opções de [docs/DEPENDENCIAS.md](docs/DEPENDENCIAS.md); depois
disso, a IDE associa a seleção localmente ao caminho do sketch. Cada exemplo
que precisa de um perfil reproduzível possui seu próprio `sketch.yaml`.

## Interface web

Os arquivos-fonte das interfaces ficam em `web/index.html`, `web/alarmes.html`
e `web/upload.html`. Para o funcionamento completo, os três precisam existir
na raiz da partição FFat do dispositivo.

A rota `/upload` tenta servir `/upload.html`. Se o arquivo estiver ausente,
o firmware apresenta um formulário mínimo incorporado que permite restaurar
os arquivos da interface. Em um ESP32 novo, se a primeira montagem da FFat
falhar porque a partição ainda não está formatada, o firmware formata a
partição e tenta montá-la novamente. Se nem assim a montagem funcionar, o
servidor web não é iniciado.

O upload aceita nomes simples e faz substituição por arquivo temporário.
Esse fluxo fica em `upload_arquivos.cpp`, que trata separadamente o início, a
escrita das partes, a conclusão e o cancelamento do envio. `radios.json` e
`alarmes.json` recebem validação e backup próprios por meio de seus módulos de
persistência. Ao migrar
uma instalação existente, envie `upload.html` pela página incorporada atual
antes de gravar uma versão do firmware que passe a servi-lo da FFat.

O sketch mínimo usado para comparação foi isolado em
`examples/radio_web_exemplo_minimo/`. Ele não participa da compilação do
firmware principal e contém apenas placeholders de Wi-Fi.
