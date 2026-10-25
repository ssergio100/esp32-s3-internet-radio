# Arquitetura do firmware

## Requisito prioritário: Rádio FM sem decoder/I2S ativo

O isolamento da recepção FM tem precedência sobre a ativação do TEF6686. A
transição para `RADIO_FM` só termina depois que `AudioService` confirma:

- stream encerrado e saída silenciada;
- instância `Audio` destruída;
- tarefa interna do decoder encerrada;
- canal I2S desabilitado e removido;
- `BCK`, `LRCK` e `DIN` configurados como saída em nível baixo.

Essa confirmação é síncrona e tem prazo limitado. Falha ou timeout bloqueiam a
ativação do TEF6686; o equipamento volta ao estado de relógio e publica o erro
no display. Confirmado o desligamento, o Wi-Fi é desligado e começa uma espera
não bloqueante de 1,1 segundo antes de ativar o receptor FM. O `loop()`, o OLED
e os encoders continuam ativos; passos de sintonia recebidos nesse intervalo
são acumulados e executados quando o TEF6686 acorda.

O PCM5102A detecta a ausência de clocks e entra em power-down automático. Nesse
estado ele desliga DAC, driver de linha, charge pump e circuitos internos de
polarização, conforme a seção *Power Down* do
[datasheet da Texas Instruments](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf).
Isso é o desligamento máximo possível somente por firmware na montagem atual:
a alimentação do módulo permanece presente porque não existe load switch,
regulador com `EN` nem GPIO ligado ao trilho de alimentação.

O MCP23017 não liga nem desliga o DAC. Ele roteia as entradas dos amplificadores
NS8002: `GPA7` controla o relé que seleciona DAC ou TEF6686, enquanto `GPA6` e
`GPA5` são enables ativos em LOW dos amplificadores direito e esquerdo. Os três bits são
atualizados juntos pelo registrador `OLATA`. Falhas nessa escrita geram
diagnóstico serial, mas não comandam nem bloqueiam a máquina de estados.

## Responsabilidades

### Serviço de áudio

`audio_radio.cpp` encapsula a instância de `Audio`. Chamadas como
`connecttohost()`, `connecttoFS()`, `stopSong()`, `setVolume()` e `loop()` acontecem somente
na tarefa `AudioService`. Display, controles e servidor não acessam a
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
Ao entrar em Relógio ou Rádio FM, um comando encerra o stream e destrói a
instância `Audio`; com isso, a própria biblioteca encerra sua tarefa interna e
remove o canal I2S. `AudioService` então aguarda uma notificação sem consumir
ciclos continuamente. O retorno a uma fonte digital recria decoder e I2S, e só
depois solicita a estação que já estava selecionada.

### Receptor FM

`src/tef6686/` contém o driver validado, o protocolo I2C, o patch do receptor,
RDS, medidas de qualidade, mute, standby e o scanner. Esse código não conhece
display, encoders, Wi-Fi, alarmes ou os estados gerais do produto.

`radio_fm.cpp` é a fronteira entre o driver e a aplicação. Ele converte o
volume comum de `0..21` para `-60..0 dB`, conserva a última frequência, publica
um `StatusRadioFm` e executa a busca cooperativa. Cada chamada de
`processarRadioFm()` faz no máximo uma etapa do scanner e as consultas
periódicas previstas; não existe uma tarefa concorrente lendo respostas do
TEF6686.

O receptor usa endereço `0x64`, clock interno de 9,216 MHz e começa em
100,10 MHz. Ele é inicializado uma vez no boot e mantido em standby até a fonte
Rádio FM ser escolhida. A ausência do módulo é publicada como indisponibilidade
da fonte FM e não impede Rádio Web, Relógio ou alarmes de iniciar.

### DFPlayer

`dfplayer.cpp` é a fronteira do DFPlayer com a aplicação. Ele é o único módulo
que conhece UART2, a biblioteca DFRobot e o pino `BUSY`. A interface inicializa
o cartão uma vez, compara a contagem de `/01` com a contagem total, toca uma
faixa pelo índice e publica estado explícito para alarme, chime ou prévia.

O cartão pertence ao DFPlayer; ele não é montado pelo ESP32. A pasta lógica de
sons deve ser gravada como `/01`, com nomes `001.mp3`, `002.mp3` etc. Como o
protocolo serial não fornece nomes reais, o catálogo público usa os identificadores
`Som 001`, `Som 002` etc. O coordenador inicializa e processa a interface em todos
os modos, mantendo o DAC do módulo e os NS8002 em mute até uma reprodução.

`GET /api/sons-dfplayer` publica o catálogo numérico e as duas contagens usadas
para diagnosticá-lo sem montar o cartão no ESP32. O volume público permanece na
faixa geral do equipamento; somente o driver converte para a faixa nativa 0..30.
`GET /api/chime` lê e `PUT /api/chime` grava ativação, som, volume e intervalo.
`POST /api/chime/preview` reproduz a seleção atual sem persistir. A escolha do
som é validada contra o catálogo antes de qualquer reprodução.

### Aplicação

`radio_web_1.ino` coordena a inicialização e a interface física. Ele envia
comandos ao serviço e reage às mudanças de estado, sem alimentar o decoder.
Uma operação HTTP lenta pode atrasar a interface, mas não interrompe a
execução do serviço de áudio.

As regras centrais de interação permanecem no arquivo principal: entrar na
seleção de estações, navegar, confirmar, cancelar por inatividade e ajustar o
volume recebido do encoder dedicado. As transições entre Rádio Web, Rádio FM e Relógio
também permanecem explícitas nesse arquivo. Cada transição usa uma função
nomeada para que o fluxo seja legível sem conhecer previamente os detalhes do
display ou do serviço de áudio.

### Chaveamento da saída de áudio

`chaveamento_audio.cpp` é a única camada que conhece os registradores do
MCP23017. O expansor usa o endereço `0x20` e compartilha o barramento I2C de
100 kHz. Somente `GPA7`, `GPA6` e `GPA5` são configurados como saída; os demais
pinos da porta A permanecem como entradas.

O latch é preparado com `GPA7=LOW` e `GPA6/GPA5=HIGH` antes da mudança de
direção, evitando habilitar os NS8002 durante a inicialização. A tabela aplicada é:

| Estado | GPA7: seleção | GPA6/GPA5: NS8002 |
| --- | --- | --- |
| Relógio/standby | LOW, lado FM | HIGH, mute |
| Rádio FM | LOW, TEF6686 | LOW, habilitados |
| Rádio Web/alarme digital | HIGH, DAC | LOW, habilitados |
| DFPlayer, ligação direta | Preservado | LOW, habilitados |
| Volume zero | Preserva a fonte | HIGH, mute |
| Busca FM | LOW, TEF6686 | HIGH, mute |

GPA6/GPA5 nunca são consultados para decidir o funcionamento do DAC ou das
fontes; são somente enables ativos em LOW. Como os pinos voltam ao modo de
entrada durante reset do MCP23017, o estado seguro anterior ao firmware requer
pull-down em GPA7 e pull-ups em GPA6/GPA5.

As saídas `DAC_L` e `DAC_R` do DFPlayer chegam diretamente aos NS8002 e não
passam pelo relé. Por isso, sua interface preserva o estado de GPA7 e modifica
somente GPA6/GPA5 para habilitar ou silenciar os amplificadores.

O `loop()` supervisiona a associação Wi-Fi. Se o ponto conectado estiver em
`BSSIDS_WIFI_BLOQUEADOS`, a associação é rejeitada. Durante a conexão inicial ou
enquanto a rede estiver desconectada, `wifi_radio.cpp` faz uma varredura
assíncrona a cada dez segundos, considera somente o SSID salvo e escolhe o ponto
permitido com melhor RSSI. A reconexão automática da pilha permanece desativada
para que ela não contorne esse filtro. O WiFiManager continua responsável pelas
credenciais e pelo portal, portanto outras redes permanecem utilizáveis.

A ativação normal de Rádio Web primeiro tenta associação direta com SSID e senha
persistidos. A varredura só começa se essa tentativa não conectar dentro do
intervalo já usado pela supervisão ou se o BSSID associado for bloqueado. Em
paralelo à associação, `AudioService` recria decoder e I2S; a abertura do stream
espera ambos ficarem prontos, mas não soma artificialmente seus tempos.
O serviço de áudio aguarda a rede e retoma suas próprias tentativas quando a
associação volta.
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
Isso também pode ocorrer durante uma seleção; após o intervalo, a barra retorna
ao conteúdo da tela atual. O nome da estação e o diagnóstico superior continuam
visíveis.

`DisplayService` executa na mesma prioridade e no mesmo núcleo do `loop()`, mas
com cadência própria. Assim, uma passagem demorada pelo servidor web não prende
a animação à próxima volta do `loop()`. Um mutex protege o estado e o buffer
gráfico porque mensagens, seleção e volume ainda podem solicitar desenhos pela
tarefa principal. Se o serviço não puder ser criado, a chamada mantida no
`loop()` assume automaticamente a atualização.

Cada quadro avança no máximo um pixel. Quando o decoder ou outra tarefa atrasa
uma atualização, o tempo perdido é descartado; ele não se transforma em um
salto de vários pixels no quadro seguinte. Velocidade da rolagem, cadência da
tarefa e intervalo de renovação ficam em `configuracao.h`.

No estado Relógio, o mesmo módulo desenha quatro cartões grandes para `HH:MM`,
com um corte horizontal que simula dígitos flip. A data completa ocupa uma
faixa rolante no rodapé. Essa tela consulta somente o relógio do sistema; o RTC
continua encapsulado em `relogio_rtc.cpp` e não é lido a cada quadro.

Na fonte Rádio FM, o display recebe apenas a fotografia já tratada por
`radio_fm.cpp`: frequência, PS do RDS, estado da busca e nível do sinal. O
display não inclui nem chama o driver TEF6686. Mudanças de frequência e estado
acordam a tarefa imediatamente; oscilações pequenas do nível são consolidadas
e sua apresentação é limitada a duas atualizações por segundo.

### Indicador LED

`indicador_led.cpp` é o único módulo que escreve diretamente no LED RGB.
O Wi-Fi solicita a animação azul durante a conexão inicial; depois disso, o
`loop()` solicita a apresentação do estado publicado pelo serviço de áudio.
As cores e a temporização ficam encapsuladas no indicador.

### Controles

`controles.cpp` configura duas instâncias independentes da biblioteca. O encoder
principal publica clique curto e deslocamento de navegação; o segundo publica
clique curto e deslocamento de volume. Cada botão tem sua própria interrupção e
seu próprio estado de confirmação após a soltura. Pressões que atingem
`TEMPO_MAXIMO_CLIQUE_CURTO_ENCODER_MS` são descartadas e não se transformam em
clique ao soltar.

Cada rotação usa diretamente o valor de `encoderChanged()` da instância
correspondente, sem manter contador paralelo, aplicar aceleração ou filtrar
mudanças de direção. Pinos e transições por detente ficam agrupados em
`ENCODER_NAVEGACAO` e `ENCODER_VOLUME`, em `configuracao.h`. O valor `4` do
principal foi confirmado no hardware; o mesmo valor inicial do segundo ainda
precisa ser validado no novo componente.

### Estados do equipamento

O firmware possui `RADIO_WEB`, `RADIO_FM` e `RELOGIO`. O boot termina sempre em
`RELOGIO`, estado de standby com Wi-Fi, servidor, decoder e I2S desligados e o
TEF6686 em repouso. O clique curto no encoder de volume abre a seleção de fonte;
o giro percorre circularmente `Relógio → Rádio FM → Rádio Web → Relógio` e outro
clique confirma. No Relógio, o primeiro giro já abre essa seleção e aplica todos
os passos recebidos. Durante um alarme, o clique continua ignorado.

Cada transição responde primeiro no OLED e depois administra os recursos da
fonte. Rádio Web mantém Wi-Fi, servidor e `AudioService`; Rádio FM destrói o
decoder/I2S, desliga a rede e só então acorda o TEF6686; Relógio mantém o Wi-Fi
e as duas fontes de áudio desligados. Ao sair de Rádio FM, o receptor é silenciado
e colocado em standby. Ao voltar a Rádio Web, a associação é assíncrona; depois
da rede, decoder e I2S são recriados antes da reprodução.

Fora da seleção de fonte, o giro do encoder de volume altera o valor comum às
fontes ativas. Em Rádio FM, a aplicação envia o novo nível diretamente ao
adaptador TEF. O segundo encoder mantém a lista
e a confirmação das estações da Rádio Web. Na fonte FM, seu giro inicia a busca
da próxima estação no sentido escolhido; deslocamentos acumulados são
enfileirados e a busca continua sem bloquear o `loop()`.

### Alarmes

`alarmes.cpp` carrega uma fotografia tipada de `/alarmes.json` em RAM e verifica
uma única vez cada minuto local. A presença de `dias` identifica uma regra
semanal; `data` identifica uma execução única. Não há campo de tipo, próxima
execução ou histórico persistido. Alarmes únicos vencidos são desativados.

O arquivo principal coordena a execução sem criar outro estado permanente. O
alarme interrompe Rádio Web, Rádio FM, Relógio ou outro alarme e usa a mesma fila e a mesma
instância de `Audio`. MP3 e WAV recomeçam depois do EOF; uma estação permanece
no ar. Todas as fontes encerram no clique curto do encoder principal ou no
limite configurado de 30 minutos. Um disparo posterior substitui o atual e
reinicia o limite; se vários
coincidirem no mesmo minuto, vence o maior `id`. Somente ao fim da cadeia o
estado anterior é restaurado e o servidor permanece suspenso durante a cadeia.
O encoder de volume parte do nível efetivamente aplicado ao disparo. Cada
ajuste altera a cópia em RAM e também `volumeAtual`, acompanha repetições e
fallback e permanece como volume geral depois da restauração, sem regravar o
cadastro. Se não houver movimento durante o alarme, `volumeAtual` não muda e o
nível geral anterior é restaurado. O encoder principal não altera volume nesse
estado; seu clique curto continua encerrando o alarme.

Uma fonte local espera qualquer stream publicar `PARADO`, desliga completamente
o Wi-Fi e só então abre o arquivo. Uma fonte Rádio Web mantém ou restabelece a
rede e executa sua supervisão, mas não reativa o servidor HTTP. As trocas entre
alarmes reaplicam essa política conforme a nova fonte. Ao final, Rádio Web
reconecta e reabre a estação anterior; Rádio FM volta a acordar o TEF6686;
Relógio deixa a rede desligada.
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
usado quando nenhuma fonte foi escolhida ou quando o `radioId` não existir mais.
O catálogo numérico do DFPlayer é preparado separadamente no boot e publicado
por `GET /api/sons-dfplayer`; nesta etapa ele ainda não é uma fonte de alarme.
A página também obtém a fotografia ativa das estações, inclusive a reserva, por
`GET /api/radios-alarmes`.

`chime.cpp` mantém a configuração global armazenada no objeto `chime` de
`/alarmes.json`. O coordenador verifica a hora local sem bloquear o `loop()` e
dispara uma única vez em cada janela de 1 a 60 minutos. A interface oferece os
intervalos 1, 2, 5, 10, 15, 30 e 60 para operação e testes. A conclusão é
detectada pelo evento do DFPlayer ou pela borda do pino `BUSY`.

Antes da reprodução, `dfplayer.cpp` fotografa diretamente o último estado
confirmado de GPA7/GPA6/GPA5. Ele habilita os NS8002 preservando o relé e, ao
terminar, restaura exatamente a fotografia, inclusive quando o estado anterior
era Relógio/mute. Isso torna chime e prévia independentes de Rádio Web, Rádio FM
ou Relógio. Durante a reprodução, o chaveamento dá prioridade temporária ao
DFPlayer: atualizações de mute, busca FM ou ativação de fonte não podem elevar
GPA6/GPA5. Alarmes e trocas manuais de fonte interrompem essas reproduções,
restauram a fotografia e então assumem o caminho de áudio.

O estado Relógio não é um modo global de baixo consumo, mas o subsistema de
áudio digital é encerrado como no Rádio FM. A API pública da ESP32-audioI2S não
expõe a remoção do canal isoladamente; por isso o firmware destrói a instância
`Audio`, cujo destrutor desabilita/remove I2S e encerra a tarefa interna. As três
linhas I2S são então mantidas em nível baixo para acionar o power-down automático
do PCM5102A. O relé de seleção e os enables dos amplificadores são controlados
pelo MCP23017; somente um corte do trilho de alimentação do DAC continuaria
dependendo de alteração de hardware.

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
data, a fonte opcional do alarme e a configuração global do chime.
`api_alarmes.cpp` implementa `GET`, `POST`, `PUT` e `DELETE` em `/api/alarmes` e
recarrega a fotografia em RAM após cada alteração; `api_chime.cpp` preserva a
mesma transação ao atualizar ativação, som e volume do chime.

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
