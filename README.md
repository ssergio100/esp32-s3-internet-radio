# Rádio Web para ESP32-S3

Firmware de rádio web e relógio com alarmes, DAC estéreo I2S PCM5102A,
saída analógica de linha, display OLED, dois encoders, LED RGB, persistência
em FFat e interface HTTP para administração.

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
| `audio_radio.cpp` | Serviço dedicado de rádio web e som padrão dos alarmes |
| `wifi_radio.cpp` | Configuração e supervisão da conexão Wi-Fi |
| `display_radio.cpp` | Modelo e tarefa das telas Rádio Web e Relógio |
| `controles.cpp` | Leitura dos dois encoders e de seus botões |
| `indicador_led.cpp` | Cores e animações do LED RGB |
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
| `TEMPO_MAXIMO_CLIQUE_CURTO_ENCODER_MS` | Duração máxima aceita como clique curto | 2000 ms |
| `INTERVALO_PASSO_ROLAGEM_NOME_MS` | Intervalo para o nome avançar um pixel | 40 ms |
| `INTERVALO_PASSO_ROLAGEM_DIAGNOSTICO_MS` | Intervalo para o diagnóstico avançar um pixel | 40 ms |
| `INTERVALO_ATUALIZACAO_DIAGNOSTICO_DISPLAY_MS` | Renovação dos valores exibidos no diagnóstico | 1000 ms |
| `INTERVALO_PASSO_ROLAGEM_DATA_RELOGIO_MS` | Intervalo para a data do relógio avançar um pixel | 80 ms |
| `INTERVALO_SERVICO_DISPLAY_MS` | Cadência da tarefa do OLED, derivada da rolagem do nome | 10 ms |
| `INTERVALO_PISCA_LED_CONEXAO_WIFI_MS` | Intervalo da piscada azul durante a conexão | 100 ms |
| `INTERVALO_TELEMETRIA_SERIAL_MS` | Intervalo entre diagnósticos na serial | 5000 ms |
| `ENCODER_VOLUME` | Encoder 1: volume e alternância Rádio/Relógio | DT=2, CLK=1, SW=42, 4 transições |
| `ENCODER_NAVEGACAO` | Encoder 2: navegação e confirmação de rádios | DT=41, CLK=40, SW=39, 4 transições |
| `FUSO_HORARIO_UTC_HORAS` | Fuso aplicado ao relógio | -3 horas |
| `INTERVALO_SINCRONIZACAO_NTP_MS` | Intervalo entre correções pela rede | 3600000 ms |
| `DESVIO_MINIMO_AJUSTE_RTC_SEGUNDOS` | Diferença mínima para regravar o DS3231 | 2 segundos |
| `DURACAO_MAXIMA_ALARME_MINUTOS` | Limite de repetição de um alarme | 30 minutos |
| `TEMPO_LIMITE_CONEXAO_RADIO_ALARME_MS` | Espera por rede/stream antes do som padrão | 20 segundos |

Nas rolagens do nome e do diagnóstico, um intervalo menor produz movimento
mais rápido e um intervalo maior produz movimento mais lento. A mesma relação
vale para o intervalo da piscada do LED. O brilho aceita valores de 0 a 255.
Os servidores NTP primário e secundário também ficam em `configuracao.h`.

A animação do OLED roda em uma tarefa própria. As APIs chamadas pelo `loop()`
alteram somente o modelo e acordam essa tarefa; a transferência I2C do
framebuffer ocorre fora do mutex do modelo. Atualizações repetidas são
consolidadas no estado mais recente. Cada quadro de rolagem avança no máximo um
pixel, sem compensar atrasos com saltos. Se a tarefa não puder ser criada, a
chamada de contingência no `loop()` assume a atualização.

O sketch independente
`examples/teste_ds3231sn/teste_ds3231sn.ino` valida o RTC DS3231SN pela porta
serial antes de sua integração ao firmware. O exemplo conserva sua pinagem
independente `SDA=GPIO17` e `SCL=GPIO18`; o firmware principal usa
`SDA=GPIO16` e `SCL=GPIO17`. Ele identifica os dispositivos do barramento e lê
data, hora, temperatura e o indicador de perda de alimentação.

A ligação e a preparação do novo módulo de áudio estão registradas em
[`docs/MIGRACAO_AUDIO_PCM5102A.md`](docs/MIGRACAO_AUDIO_PCM5102A.md). O
PCM5102A usa I2S, não I2C, e entrega dois canais em nível de linha. Portanto,
alto-falantes passivos continuam exigindo um amplificador estéreo analógico
depois do DAC.

As ligações atuais são `SDA=GPIO16` e `SCL=GPIO17` para I2C;
`BCK=GPIO12`, `LRCK=GPIO10` e `DIN=GPIO11` para I2S; `GPIO2/1/42` para o
encoder 1; e `GPIO41/40/39` para o encoder 2. O Player e o barramento microSD
foram removidos deste branch. A futura pinagem das Nixies deve ser reavaliada
contra essas ligações antes de qualquer implementação em hardware.

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
`audio_radio.cpp` mantém uma tarefa dedicada e é o único proprietário das
operações da biblioteca de áudio. Comandos de fonte usam uma fila FreeRTOS;
volume usa um valor atômico separado, consolidando movimentos rápidos no ajuste
mais recente sem criar atraso na fila.
A tarefa interna de decodificação da ESP32-audioI2S usa o núcleo 1; o serviço
que alimenta e supervisiona o stream usa o núcleo 0. Essa distribuição isolou
a entrega de PCM da contenção das tarefas de rede e corrigiu em hardware a
retomada de Rádio Web por um alarme iniciado no Relógio.

O `loop()` principal lê os encoders antes dos demais serviços cooperativos e
fica responsável por:

- os dois encoders e o modelo apresentado pelo display;
- atendimento do servidor HTTP;
- apresentação do estado publicado pelo serviço de áudio;
- telemetria periódica.

O equipamento possui dois estados operacionais: `Rádio Web` e `Relógio`. Em
`Rádio Web`, os serviços de rede funcionam normalmente. Em `Relógio`, o stream
fica suspenso, o servidor é interrompido, o Wi-Fi é desligado e o LED permanece
apagado; relógio, encoders e OLED continuam sendo processados. A volta ao rádio
usa reconexão assíncrona para não bloquear os controles.

Os alarmes são uma sobreposição temporária aos dois estados. Um alarme usa uma
estação ou o som padrão até o clique curto no encoder 2 ou o
limite de 30 minutos e então restaura o estado anterior. Um novo disparo substitui
definitivamente o alarme em execução. Quando vários coincidem no mesmo minuto,
vence o cadastro de maior `id`. O som padrão desliga o Wi-Fi; alarmes Rádio Web
mantêm a rede e sua supervisão, sempre com o servidor HTTP suspenso.
Durante a execução, o encoder de volume parte do nível efetivamente aplicado
ao alarme. O ajuste não altera o cadastro, mas passa a ser também o novo volume
geral do equipamento e permanece depois da restauração. Sem movimento nesse
encoder, o volume geral anterior é restaurado. A tela do alarme apresenta o
valor efetivo de `0/21` a `21/21`.

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
O encoder 1 ajusta o nível reservado à próxima ativação e substitui
temporariamente a data por uma barra de volume. Seu clique curto volta
diretamente à Rádio Web. O encoder 2 não executa ação no Relógio.

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

## Encoders

O encoder 1 usa `DT=GPIO2`, `CLK=GPIO1` e `SW=GPIO42`:

- o giro controla somente o volume, inclusive durante a seleção de rádios;
- o clique curto alterna diretamente entre Rádio Web e Relógio;
- no Relógio, o giro altera o nível reservado à próxima ativação;
- durante alarmes, o giro altera o volume efetivo e o clique é ignorado.

O encoder 2 usa `DT=GPIO41`, `CLK=GPIO40` e `SW=GPIO39`:

- no repouso da Rádio Web, girar abre a lista e aplica o deslocamento no mesmo
  ciclo; um clique curto também abre a lista;
- durante a seleção, o giro escolhe a estação e o clique confirma;
- depois de dez segundos sem atividade, a seleção é cancelada;
- no Relógio, giro e clique não executam ação;
- durante um alarme, o clique curto encerra a reprodução.

Cada encoder tem sua própria ISR. `encoderChanged()` entrega o deslocamento
assinado acumulado, a aceleração fica desativada e não há o antigo limite
artificial de 10 mil passos. Pressões de dois segundos ou mais são descartadas.
O contrato detalhado e o roteiro de validação física estão em
[`docs/PROJETO_VOLUME_ENCODER.md`](docs/PROJETO_VOLUME_ENCODER.md).

## Alarmes

A página `/alarmes` cadastra alarmes semanais, com horário e dias da semana, ou
de execução única, com data e horário. O JSON não armazena um campo de tipo: a
presença de `dias` identifica a repetição semanal e a presença de `data`
identifica uma execução única. Os arquivos ativos, temporário e backup são
`/alarmes.json`, `/alarmes.tmp` e `/alarmes.bak`.

A fonte pode ser uma estação da lista de Rádio Web ou o som padrão. O JSON
guarda `radioId` quando uma estação foi escolhida; sua ausência seleciona
`/alarme_padrao.wav`, gerado automaticamente na FFat. Uma estação removida ou
indisponível também recorre ao som padrão. Alarmes únicos são desativados ao
disparar; alarmes únicos já vencidos também são desativados pelo agendador. Não
há histórico nem cálculo de próxima execução na interface.

Enquanto um alarme está em execução, o encoder 1 ajusta o volume efetivo entre
0 e 21 e o clique curto do encoder 2 encerra a reprodução. O ajuste vale também
para repetições e fallback, não modifica o cadastro do alarme e passa a ser o
novo `volumeAtual`. Se não houver ajuste, o volume geral anterior é restaurado
ao final.

A rotação dos dois encoders usa diretamente o deslocamento assinado acumulado
informado pela biblioteca, sem aceleração ou filtro de direção. Se vários passos
forem acumulados entre duas passagens do `loop()`, todos são aplicados. Cada
encoder possui configuração independente; as quatro transições por detente
devem ser confirmadas nos dois componentes instalados.

O estado `Relógio` encerra o stream e suspende a tarefa de serviço, mas a
biblioteca ESP32-audioI2S não expõe ao firmware a parada pública do I2S. Na
montagem básica, `XSMT` fica fixo em nível alto; portanto, o firmware ainda não
dispõe de mute/standby físico explícito do PCM5102A. O controle de `XSMT` por
GPIO está registrado como melhoria futura.

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
e uptime. A porta serial também imprime um resumo a cada cinco segundos,
incluindo se o RTC está presente e respondendo no barramento I2C.

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
