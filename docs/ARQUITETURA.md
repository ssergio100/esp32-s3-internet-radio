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

As regras centrais de interação permanecem no arquivo principal: entrar na
seleção de estações, navegar, confirmar, cancelar por inatividade e ajustar
o volume. Cada transição usa uma função nomeada para que o fluxo seja legível
sem conhecer previamente os detalhes do display ou do serviço de áudio.

O Wi-Fi mantém a reconexão automática da pilha. Além disso, o `loop()`
supervisiona a associação e solicita `WiFi.reconnect()` a cada dez segundos
enquanto a rede estiver desconectada. A tentativa usa as credenciais
persistidas e não fixa nem filtra BSSID. O serviço de áudio aguarda a rede e
retoma suas próprias tentativas quando a associação volta.

### Indicador LED

`indicador_led.cpp` é o único módulo que escreve diretamente no LED RGB.
O Wi-Fi solicita a animação azul durante a conexão inicial; depois disso, o
`loop()` solicita a apresentação do estado publicado pelo serviço de áudio.
As cores e a temporização ficam encapsuladas no indicador.

### Relógio

`relogio.cpp` concentra a sincronização NTP, o fuso horário e a obtenção
da data e hora locais. O arquivo principal apenas inicia o relógio. O display
solicita a hora local e continua responsável por sua formatação e desenho.
Fuso, ajuste de horário de verão e servidores NTP ficam em `configuracao.h`.

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

### Persistência

`persistencia_radios.cpp` é o proprietário dos arquivos `/radios.json`,
`/radios.tmp` e `/radios.bak`. Ele concentra a validação do documento JSON e a
transação que preserva a versão anterior antes de promover uma nova lista.

`servidor_web.cpp` interpreta as requisições HTTP e delega a leitura ou a
gravação da lista persistida. `radios.cpp` monta durante o boot a fotografia em
memória usada pelo restante do firmware e mantém a lista de reserva compilada.
Por projeto, uma lista nova entra em uso depois de reiniciar.

As interfaces web completas são arquivos físicos: `/index.html` e
`/upload.html`. A rota `/upload` usa um formulário mínimo incorporado apenas
quando `/upload.html` está ausente, permitindo restaurar arquivos enquanto a
FFat continuar montada. Validação, escrita temporária e backup permanecem no
firmware; o JavaScript não é considerado uma barreira de segurança.

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
promoção tenta restaurar o backup. As alterações feitas pela API ou pelo upload
de `radios.json` aceitam somente um array não vazio cujos itens tenham nome e
URL HTTP/HTTPS dentro dos limites suportados.

No boot, `radios.cpp` tenta o backup quando o arquivo ativo não pode ser aberto,
interpretado ou não contém um array. Dentro de um array legível, itens inválidos
são ignorados. Se nenhum item válido for encontrado, a lista de reserva
compilada entra em uso.

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
