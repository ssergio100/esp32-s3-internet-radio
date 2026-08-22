# Arquitetura do firmware

## Responsabilidades

### Serviço de áudio

`audio_radio.cpp` encapsula a instância de `Audio`. Chamadas como
`connecttohost()`, `connecttoFS()`, `stopSong()`, `setVolume()` e `loop()` acontecem somente
na tarefa `AudioService`. Display, encoder e servidor não acessam a
biblioteca diretamente.

Comandos externos são estruturas POD copiadas para uma fila FreeRTOS. O
estado publicado também é POD e é copiado sob mutex. Assim, nenhuma
referência para `String` ou memória temporária cruza tarefas.

Há duas tarefas relacionadas ao áudio:

- decodificador/I2S da ESP32-audioI2S, no núcleo 1;
- conexão, preenchimento e supervisão, no núcleo 0.

Os valores de núcleo, pilha e prioridade ficam em `configuracao.h`. No Rádio
Web, `AudioService` conserva prioridade 3 e intervalo de 1 ms para alimentar a
rede continuamente. No Player, ela usa prioridade 1 e aguarda 3 ms entre ciclos
para espaçar as leituras do microSD. O decoder interno permanece no núcleo 1 em
prioridade 2 e, portanto, pode preemptar o `loop()` da interface quando precisar
alimentar o I2S. O SPI do cartão opera por padrão a 4 MHz e a rolagem do nome
redesenha o OLED a cada 80 ms.
Ao entrar no estado Relógio, um comando encerra o stream, silencia a saída e
faz `AudioService` aguardar uma notificação sem consumir ciclos continuamente.
O retorno ao estado Rádio Web acorda a mesma tarefa antes de solicitar a
estação que já estava selecionada.

### Aplicação

`radio_web_1.ino` coordena a inicialização e a interface física. Ele envia
comandos ao serviço e reage às mudanças de estado, sem alimentar o decoder.
Uma operação HTTP lenta pode atrasar a interface, mas não interrompe a
execução do serviço de áudio.

As regras centrais de interação permanecem no arquivo principal: entrar na
seleção de estações, navegar, confirmar, cancelar por inatividade e ajustar
o volume. As transições entre Rádio Web e Relógio também permanecem explícitas
nesse arquivo. Cada transição usa uma função nomeada para que o fluxo seja legível
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

No estado Relógio, o mesmo módulo desenha quatro cartões grandes para `HH:MM`,
com um corte horizontal que simula dígitos flip. A data completa ocupa uma
faixa rolante no rodapé. Essa tela consulta somente o relógio do sistema; o RTC
continua encapsulado em `relogio_rtc.cpp` e não é lido a cada quadro.

### Indicador LED

`indicador_led.cpp` é o único módulo que escreve diretamente no LED RGB.
O Wi-Fi solicita a animação azul durante a conexão inicial; depois disso, o
`loop()` solicita a apresentação do estado publicado pelo serviço de áudio.
As cores e a temporização ficam encapsuladas no indicador.

### Controles

`controles.cpp` configura o encoder e devolve uma `LeituraControles` contendo o
clique curto, o clique longo e o deslocamento assinado acumulado. Os cliques são
confirmados após a soltura. A rotação usa diretamente o
valor de `encoderChanged()`, sem manter um segundo contador, aplicar aceleração
ou filtrar mudanças de direção.

A calibração física fica em `TRANSICOES_ENCODER_POR_DETENTE`, em
`configuracao.h`. Os tempos com nomes de clique tratam somente o botão e não
interferem na rotação.

### Estados do equipamento

O firmware possui `RADIO_WEB`, `PLAYER` e `RELOGIO`. Um clique longo confirmado
leva qualquer fonte ativa ao Relógio. Ao entrar nele, o OLED muda imediatamente; depois o
arquivo principal apaga o LED, interrompe o servidor, solicita a suspensão do
áudio e desliga o rádio Wi-Fi. O `loop()` continua atendendo somente relógio,
display e encoder. O clique longo não executa ação nesse estado.

No Relógio, o giro percorre um catálogo central de estados. Cada opção substitui
toda a tela; um clique curto confirma a opção exibida. Escolher Rádio Web
conecta o Wi-Fi, reabre o servidor, acorda a tarefa e retoma a estação. Escolher
Player mantém a rede desligada, reutiliza o microSD e o catálogo MP3 de `/sons`
preparados durante o boot e acorda a mesma tarefa de áudio.

`player.cpp` limita o catálogo a 100 MP3 diretamente em `/sons`, sem busca
recursiva. A listagem ocorre antes do serviço de áudio iniciar e nunca durante a
reprodução. O arquivo é decodificado progressivamente por `connecttoFS()`; não
há arquivo completo em RAM, mixer, anel PCM ou segundo decoder. A FFat, o
catálogo e as seleções permanecem preservados entre transições.

Quando `REPRODUCAO_SEQUENCIAL_PLAYER` está ativa, o arquivo principal observa o
fim confirmado da faixa e solicita a seguinte na ordem alfabética; depois da
última, retorna à primeira. A observação só é armada depois que o áudio realmente
entrou em bufferização ou reprodução, para o estado parado existente antes do
comando não provocar um salto prematuro. Enquanto o usuário navega pela lista, a
sequência aguarda sua escolha ou o cancelamento da seleção por inatividade.

### Alarmes

`alarmes.cpp` carrega uma fotografia tipada de `/alarmes.json` em RAM e verifica
uma única vez cada minuto local. A presença de `dias` identifica uma regra
semanal; `data` identifica uma execução única. Não há campo de tipo, próxima
execução ou histórico persistido. Alarmes únicos vencidos são desativados.

O arquivo principal coordena a execução sem criar outro estado permanente. O
alarme interrompe Rádio Web, Player ou outro alarme e usa a mesma fila e a mesma
instância de `Audio`. MP3 e WAV recomeçam depois do EOF; uma estação permanece
no ar. Todas as fontes encerram no clique curto ou no limite configurado de 30
minutos. Um disparo posterior substitui o atual e reinicia o limite; se vários
coincidirem no mesmo minuto, vence o maior `id`. Somente ao fim da cadeia o
estado anterior é restaurado e o servidor permanece suspenso durante a cadeia.
O giro do encoder altera diretamente o volume da cópia em RAM do disparo e
envia o comando existente ao serviço de áudio. Esse valor temporário acompanha
repetições e fallback, mas não é gravado no cadastro nem altera o volume normal
restaurado ao final.

Uma fonte local espera qualquer stream publicar `PARADO`, desliga completamente
o Wi-Fi e só então abre o arquivo. Uma fonte Rádio Web mantém ou restabelece a
rede e executa sua supervisão, mas não reativa o servidor HTTP. As trocas entre
alarmes reaplicam essa política conforme a nova fonte. Ao final, Rádio Web
reconecta e reabre a estação anterior; Player e Relógio deixam a rede desligada.
Se a rede ou o stream não ficar disponível no limite configurado, a fonte muda
para o som padrão sem bloquear o `loop()` durante a espera.

Ao partir do Relógio, uma fonte de alarme Rádio Web retoma o serviço no perfil
de rádio e restabelece a rede de forma assíncrona. A abertura normal e a abertura
para alarme usam o mesmo caminho interno; o alarme acrescenta somente seu volume
e seu estado de telemetria.

Para isolar a entrega PCM da pilha de rede, a tarefa interna do
decoder/I2S roda no núcleo 1 e o serviço que abastece o buffer roda no núcleo 0.
No núcleo Arduino 3.3.10 instalado, o TCP/IP também está fixado no núcleo 0; o
decoder passa a preemptar o `loop()` da interface, enquanto o serviço de rede
permanece abaixo das tarefas internas de TCP/IP em prioridade. Essa distribuição
eliminou em hardware a lentidão e a distorção na transição Relógio para Rádio
Web de alarme.

`/alarme_padrao.wav` é um WAV PCM curto gerado automaticamente na FFat. Ele é
usado quando nenhuma fonte foi escolhida, quando cartão/arquivo não puder ser
aberto ou quando o `radioId` não existir mais. O catálogo do microSD é preparado
no boot, antes do áudio, para que `GET /api/arquivos-player` responda
exclusivamente a partir da RAM. A página também obtém a fotografia ativa das
estações, inclusive a reserva, por `GET /api/radios-alarmes`.

O estado Relógio não é um modo de baixo consumo. A versão instalada da
ESP32-audioI2S encerra o stream, mas não oferece uma chamada pública para parar
o I2S; sem controlar fisicamente `SD_MODE`, o firmware não garante shutdown dos
amplificadores.

### Relógio

`relogio.cpp` define a política entre as fontes de horário. No boot, ele tenta
iniciar o relógio do sistema com o instante UTC preservado pelo DS3231 e inicia
o SNTP sem bloquear. O callback de rede apenas publica que ocorreu uma
sincronização; o `loop()` chama `processarRelogio()`, que mantém todo acesso ao
I2C fora da tarefa de rede e corrige o RTC quando necessário.

`relogio_rtc.cpp` é o único proprietário da instância `RTC_DS3231`. Ele
encapsula RTClib e oferece inicialização, diagnóstico de perda de alimentação,
leitura e ajuste em UTC. O firmware não armazena hora local no
RTC: fuso e horário de verão são aplicados pelo relógio do sistema somente na
apresentação. O agendador usa essa hora local; os pinos e registradores
`INT/SQW` do DS3231 permanecem sem uso.

O SNTP é a referência de precisão e o RTC é a referência de continuidade. A
pilha consulta a rede no intervalo configurado, mas o DS3231 só é regravado
quando perdeu sua referência ou quando a diferença alcançou o limiar definido
em `configuracao.h`. A hora local aparece no diagnóstico superior da tela
normal.

### Telemetria

`telemetria.cpp` reúne o diagnóstico periódico de temperatura, memória,
Wi-Fi, áudio e presença do RTC enviado à porta serial. O diagnóstico do DS3231
confirma uma resposta atual no endereço I2C `0x68`, sem interpretar ou alterar
seu horário. A ausência de resposta não interrompe o funcionamento. O `loop()`
apenas chama
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

`persistencia_alarmes.cpp` aplica a mesma transação a `/alarmes.json`,
`/alarmes.tmp` e `/alarmes.bak`, validando IDs, nomes, volume, horário, dias ou
data e uma única fonte opcional: caminho MP3 ou `radioId`. `api_alarmes.cpp`
implementa `GET`, `POST`, `PUT` e `DELETE` em `/api/alarmes` e recarrega a
fotografia em RAM após cada alteração.

`api_radios.cpp` interpreta as requisições HTTP e delega a leitura ou a gravação
da lista persistida. `radios.cpp` monta durante o boot a fotografia em memória
usada pelo restante do firmware e mantém a lista de reserva compilada. Alterações
confirmadas pela API ou pelo upload recarregam essa fotografia imediatamente,
inclusive para resolver o `radioId` usado pelos alarmes.
O estado principal conserva também o ID da estação em reprodução. Ao voltar
do Relógio ou abrir a seleção, ele resolve novamente o índice pela fotografia
atual; se a estação foi removida, seleciona a primeira disponível.

As interfaces web completas são arquivos físicos: `/index.html`,
`/alarmes.html` e `/upload.html`. A rota `/upload` usa um formulário mínimo incorporado apenas
quando `/upload.html` está ausente, permitindo restaurar arquivos enquanto a
FFat continuar montada.

`servidor_web.cpp` monta a FFat antes de registrar as rotas HTTP. Para permitir
o primeiro upload em um ESP32 novo, uma falha de montagem aciona a formatação
da partição e uma nova tentativa de montagem. Se essa tentativa também falhar,
o servidor não é iniciado e o arquivo principal apresenta o erro de
armazenamento.

`upload_arquivos.cpp` recebe os blocos enviados, valida o nome do arquivo e
substitui arquivos comuns usando `/.upload.tmp` e `/.upload.bak`. Quando o
destino é `/radios.json` ou `/alarmes.json`, ele delega a validação e a promoção
para o módulo de persistência correspondente. `servidor_web.cpp` apenas registra as rotas e entrega
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
