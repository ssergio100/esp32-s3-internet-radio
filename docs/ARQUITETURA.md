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

Além dos streams, a fila aceita a reprodução de um arquivo local por
`tocarArquivoAudio()`. `arquivos_audio.cpp` monta o microSD pelo SPI, cria o
diretório previsível `/sons` e lista as faixas aceitas. O módulo apenas gerencia
o sistema de arquivos; a abertura pela ESP32-audioI2S continua acontecendo na
tarefa de áudio por meio de `connecttoFS()`. Ao receber o fim de um arquivo, o
serviço entra em `PARADO`; retomar uma rádio será uma decisão explícita do
futuro agendador, não uma reconexão acidental tratada como falha de stream.

Há duas tarefas relacionadas ao áudio:

- decodificador/I2S da ESP32-audioI2S, no núcleo 0;
- conexão, preenchimento e supervisão, no núcleo 1.

Os valores de núcleo, pilha e prioridade ficam em `configuracao.h`.

### Aplicação

`radio_web_1.ino` coordena a inicialização e a interface física. Ele envia
comandos ao serviço e reage às mudanças de estado, sem alimentar o decoder.
Uma operação HTTP lenta pode atrasar a interface, mas não interrompe a
execução do serviço de áudio.

As regras centrais de interação permanecem no arquivo principal: entrar na
seleção de estações, navegar, confirmar, cancelar por inatividade e ajustar
o volume. Cada transição usa uma função nomeada para que o fluxo seja legível
sem conhecer previamente os detalhes do display ou do serviço de áudio.

O `loop()` supervisiona a associação Wi-Fi. Se o ponto conectado estiver em
`BSSIDS_WIFI_BLOQUEADOS`, a associação é rejeitada. Durante a conexão inicial ou
enquanto a rede estiver desconectada, `wifi_radio.cpp` faz uma varredura
assíncrona a cada dez segundos, considera somente o SSID salvo e escolhe o ponto
permitido com melhor RSSI. A reconexão automática da pilha permanece desativada
para que ela não contorne esse filtro. O WiFiManager continua responsável pelas
credenciais e pelo portal, portanto outras redes permanecem utilizáveis.

A varredura não ocorre durante uma associação permitida. O serviço de áudio
aguarda a rede e retoma suas próprias tentativas quando a associação volta.
O modem sleep do Wi-Fi permanece habilitado para o experimento de consumo e
estabilidade do áudio.

### Display

`display_radio.cpp` mantém independentes as duas animações da tela operacional:
a faixa central e o diagnóstico na faixa superior. A faixa central mostra o
nome rolante da estação e apresenta os estados de conexão e bufferização. Na
seleção, o nome permanece estático e reduz a fonte quando não cabe no tamanho
normal. Os estados também ficam estáticos, em fonte pequena. Quando o áudio
começa a tocar, a faixa volta ao nome no tamanho normal e retoma a rolagem. O
diagnóstico percorre para a direita e mostra data e hora locais, codec,
bitrate, reserva de áudio, RSSI e BSSID. Seus valores são renovados uma vez por
segundo a partir do relógio do sistema, da fotografia pública do serviço de
áudio e de leituras passivas do Wi-Fi. A hora do sistema já foi iniciada pelo
DS3231, portanto o display não acrescenta consultas I2C periódicas ao RTC. Ele
também não acessa a instância de `Audio` nem influencia a escolha do ponto de
acesso.

A faixa inferior invertida mostra permanentemente a reserva de áudio à esquerda
e a posição da estação na lista à direita. A reserva também vem da fotografia
pública do serviço de áudio; não há sondagem adicional de rede para alimentar o
display.

Durante o ajuste de volume, a mesma faixa substitui temporariamente a reserva
por uma barra de nível e a posição da estação pelo valor atual sobre o máximo.
O nome da estação e o diagnóstico superior continuam visíveis.

Velocidade da rolagem e intervalo de renovação ficam em `configuracao.h`.

### Indicador LED

`indicador_led.cpp` é o único módulo que escreve diretamente no LED RGB.
O Wi-Fi solicita a animação azul durante a conexão inicial; depois disso, o
`loop()` solicita a apresentação do estado publicado pelo serviço de áudio.
As cores e a temporização ficam encapsuladas no indicador.

### Controles

`controles.cpp` configura o encoder e devolve uma `LeituraControles` contendo o
clique curto, o clique longo e o deslocamento assinado acumulado. Os cliques são
confirmados após a soltura, o que impede que o botão ainda pressionado provoque
um despertar imediato ao entrar em deep sleep. A rotação usa diretamente o
valor de `encoderChanged()`, sem manter um segundo contador, aplicar aceleração
ou filtrar mudanças de direção.

A calibração física fica em `TRANSICOES_ENCODER_POR_DETENTE`, em
`configuracao.h`. Os tempos com nomes de clique tratam somente o botão e não
interferem na rotação.

### Sono profundo

O arquivo principal mantém visível a transição de desligamento: reconhece o
clique longo, solicita a parada ao serviço de áudio, espera a confirmação por
um tempo limitado, apaga LED e OLED e então pede a entrada em deep sleep.
`sono_profundo.cpp` encapsula somente as APIs específicas do ESP32-S3: configura
o `GPIO7` ativo em nível baixo como fonte RTC de despertar, informa a causa da
inicialização e inicia o sono.

O despertar pelo botão reinicia normalmente o firmware. Como o controle físico
de `SD_MODE` foi adiado, a interrupção de `BCLK` durante o deep sleep deixa os
MAX98357A em standby automático, e não em shutdown completo.

### Relógio

`relogio.cpp` define a política entre as fontes de horário. No boot, ele tenta
iniciar o relógio do sistema com o instante UTC preservado pelo DS3231 e inicia
o SNTP sem bloquear. O callback de rede apenas publica que ocorreu uma
sincronização; o `loop()` chama `processarRelogio()`, que mantém todo acesso ao
I2C fora da tarefa de rede e corrige o RTC quando necessário.

`relogio_rtc.cpp` é o único proprietário da instância `RTC_DS3231`. Ele
encapsula RTClib e oferece inicialização, diagnóstico de perda de alimentação,
leitura e ajuste em UTC e temperatura. O firmware não armazena hora local no
RTC: fuso e horário de verão são aplicados pelo relógio do sistema somente na
apresentação. Alarmes e `INT/SQW` permanecem sem uso enquanto não houver uma
regra concreta de agendamento.

O SNTP é a referência de precisão e o RTC é a referência de continuidade. A
pilha consulta a rede no intervalo configurado, mas o DS3231 só é regravado
quando perdeu sua referência ou quando a diferença alcançou o limiar definido
em `configuracao.h`. A hora local aparece no diagnóstico superior da tela
normal.

### Telemetria

`telemetria.cpp` reúne o diagnóstico periódico de temperatura, memória,
Wi-Fi e áudio enviado à porta serial. O `loop()` apenas chama
`registrarTelemetriaPeriodica()`, que controla internamente o intervalo sem
bloquear as demais atividades. O intervalo ajustável fica em
`configuracao.h`.

### Diagnóstico HTTP

`api_status.cpp` cria a fotografia JSON retornada por `/api/v1/status`,
reunindo o estado publicado pelo áudio, a conexão Wi-Fi, a memória e o
uptime. Ele não inicia requisições e não altera o estado do rádio.
`servidor_web.cpp` continua responsável pela rota e pela resposta HTTP.

### API de rádios

`api_radios.cpp` implementa as respostas de `GET`, `POST` e `DELETE` da rota
`/api/radios`. Ele interpreta os parâmetros recebidos, aplica as regras de
cadastro e exclusão, escolhe o código HTTP e delega a leitura ou a gravação a
`persistencia_radios.cpp`.

Entre essas regras estão o limite de estações, a nota entre uma e cinco
estrelas, a geração do próximo identificador e a proibição de excluir a última
estação. `servidor_web.cpp` conserva apenas o registro visível dessas rotas.

### Persistência

`persistencia_radios.cpp` é o proprietário dos arquivos `/radios.json`,
`/radios.tmp` e `/radios.bak`. Ele concentra a validação do documento JSON e a
transação que preserva a versão anterior antes de promover uma nova lista.

`api_radios.cpp` interpreta as requisições HTTP e delega a leitura ou a gravação
da lista persistida. `radios.cpp` monta durante o boot a fotografia em memória
usada pelo restante do firmware e mantém a lista de reserva compilada. Por
projeto, uma lista nova entra em uso depois de reiniciar.

As interfaces web completas são arquivos físicos: `/index.html` e
`/upload.html`. A rota `/upload` usa um formulário mínimo incorporado apenas
quando `/upload.html` está ausente, permitindo restaurar arquivos enquanto a
FFat continuar montada.

`servidor_web.cpp` monta a FFat antes de registrar as rotas HTTP. Para permitir
o primeiro upload em um ESP32 novo, uma falha de montagem aciona a formatação
da partição e uma nova tentativa de montagem. Se essa tentativa também falhar,
o servidor não é iniciado e o arquivo principal apresenta o erro de
armazenamento.

`upload_arquivos.cpp` recebe os blocos enviados, valida o nome do arquivo e
substitui arquivos comuns usando `/.upload.tmp` e `/.upload.bak`. Quando o
destino é `/radios.json`, ele delega a validação e a promoção para
`persistencia_radios.cpp`. `servidor_web.cpp` apenas registra as rotas e entrega
o pedido ao módulo apropriado. Essas garantias permanecem no firmware; o
JavaScript não é considerado uma barreira de segurança.

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
promoção tenta restaurar o backup. Boot, API e edição usam a mesma seleção e a
mesma validação: `radios.json`, depois `radios.bak` e por último a lista de
reserva compilada. Um arquivo persistido somente é aceito quando contém um array
não vazio e todos os itens possuem nome e URL HTTP/HTTPS dentro dos limites
suportados.

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
