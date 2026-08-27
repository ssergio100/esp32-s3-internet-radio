# Projeto: segundo encoder dedicado ao volume

Documento de avaliação e registro da separação física entre navegação e volume.
O encoder principal está dedicado à navegação e um segundo encoder cuida
exclusivamente do volume. Qualquer mudança desse comportamento deve manter este
documento e o código coerentes.

## Estado

Implementado no código. Compilação, gravação e validação em hardware permanecem
pendentes.

## Decisões registradas

1. O encoder principal é usado somente para navegação e confirmação.
2. O encoder 2 é o único controle físico de volume. Seu giro ajusta o
   volume independentemente do modo de navegação apresentado pelo encoder
   principal.
3. Um toque curto no botão do encoder 2, ligado ao GPIO42, leva Rádio Web ou
   Player ao estado `Relógio`. No Relógio e durante alarmes, ele não executa
   ação.
4. Não haverá potenciômetro, leitura ADC, filtro analógico, média móvel ou banda
   morta de tensão.
5. O campo `volume` dos alarmes permanece em JSON, validação, persistência e
   página web. Ele determina o volume no início do alarme.

## Ligações aprovadas

### Encoder principal: navegação

| Sinal | GPIO |
| --- | ---: |
| DT | 16 |
| CLK | 15 |
| SW | 7 |
| VCC controlado | não usado (`-1`) |

### Encoder 2: volume

| Sinal | GPIO |
| --- | ---: |
| DT | 2 |
| CLK | 1 |
| SW | 42 |
| VCC controlado | não usado (`-1`) |

GPIO1, GPIO2 e GPIO42 são consecutivos no mesmo conector da revisão inicial da
ESP32-S3-DevKitC-1. O botão no GPIO42 é o comando de desligamento da fonte
ativa.

### Cartão microSD

Para liberar o GPIO42, o clock do microSD foi transferido para o GPIO38:

| Sinal | GPIO atual | GPIO aprovado |
| --- | ---: | ---: |
| SCK | 42 | 38 |
| MISO | 41 | 41 |
| MOSI | 40 | 40 |
| CS | 39 | 39 |

O Player já fornece os quatro pinos explicitamente a `SPI.begin()`, portanto a
troca não exige mudar sua arquitetura. O GPIO38 não integra o grupo GPIO33–37
ocupado pela PSRAM OPI. Na revisão inicial da DevKitC-1, identificada pelo LED
RGB no GPIO48, ele permanece disponível. Essa identificação deve ser confirmada
no hardware antes de refazer a ligação.

## Representação da configuração

As ligações ficam centralizadas em `configuracao.h`, agrupadas por componente
para evitar listas de constantes soltas. A implementação é equivalente a:

```cpp
struct ConfiguracaoEncoder {
    int pinoDt;
    int pinoClk;
    int pinoBotao;
    int pinoVcc;
    uint8_t transicoesPorDetente;
};

constexpr ConfiguracaoEncoder ENCODER_NAVEGACAO = {
    16, 15, 7, -1, 4
};

constexpr ConfiguracaoEncoder ENCODER_VOLUME = {
    2, 1, 42, -1, 4
};
```

O número de transições por detente do encoder 2 permanece separado. Ele começa
em `4`, única calibração disponível no projeto, mas ainda precisa ser confirmado
no novo componente em hardware. Um resultado diferente deve alterar somente
`ENCODER_VOLUME`.

`controles.cpp` constrói explicitamente as duas instâncias da biblioteca. Uma
função auxiliar reúne apenas a sequência comum de inicialização; não existe uma
fábrica que devolva objetos de encoder, pois cada instância
precisa manter identidade estável para sua própria ISR.

## Eventos publicados pelos controles

`LeituraControles` distingue as duas origens:

```cpp
struct LeituraControles {
    bool cliqueNavegacaoDetectado = false;
    bool cliqueVolumeDetectado = false;
    long deslocamentoEncoderNavegacao = 0;
    long deslocamentoEncoderVolume = 0;
};
```

Cada encoder tem uma ISR de rotação que chama `readEncoder_ISR()` na instância
correta. A leitura usa diretamente o deslocamento assinado de
`encoderChanged()`, preservando todos os passos acumulados. A aceleração fica
desabilitada. Contador paralelo, debounce de rotação e filtro de direção só
devem ser acrescentados se um comportamento observado no hardware os justificar.

Cada botão possui sua própria interrupção e sua própria máquina de confirmação.
O firmware publica somente cliques curtos: uma pressão que alcance
`TEMPO_MAXIMO_CLIQUE_CURTO_ENCODER_MS` é descartada ao soltar. Assim, o antigo
clique longo do encoder principal deixa de levar o equipamento ao Relógio e uma
pressão longa no encoder de volume também não aciona o desligamento.

## Comportamento da interface

- `ModoInterface::VOLUME` foi substituído por `ModoInterface::REPOUSO`.
- No repouso, o giro do encoder principal abre a seleção da fonte atual e
  aplica o deslocamento recebido. O clique curto também continua abrindo a
  seleção.
- Durante uma seleção, o encoder principal navega, o clique curto confirma e a
  inatividade continua cancelando a seleção.
- No Relógio, o encoder principal continua percorrendo e confirmando os
  estados do equipamento.
- O giro do encoder 2 nunca navega por estações, arquivos ou estados. Ele ajusta
  somente o volume.
- Um toque curto no encoder 2 leva Rádio Web ou Player ao Relógio. Esse evento é
  ignorado no próprio Relógio e durante alarmes.

## Boot, alarmes e display

- Como o encoder é relativo, `volumeAtual` começa em
  `VOLUME_PADRAO`, salvo se uma futura decisão introduzir persistência do último
  volume. Não existe posição física absoluta para ler no boot.
- O alarme começa no volume cadastrado. Se o encoder 2 for girado durante sua
  execução, o ajuste parte do volume efetivamente ouvido e passa a ser também o
  novo `volumeAtual` geral. Sem movimento, o volume geral anterior permanece
  disponível para restauração ao final.
- `mostrarVolume()` exibe temporariamente a barra inferior após
  cada alteração feita pelo encoder 2.
- A tela de alarme mostra o volume efetivo corrente.

## Balanço da implementação

| Área | Efeito implementado |
| --- | --- |
| `configuracao.h` | agrupamento das duas configurações e SCK do SD no GPIO38 |
| `controles.cpp/.h` | segunda instância, segunda ISR e dois deslocamentos publicados |
| `radio_web_1.ino` | volume dedicado e simplificação dos modos do encoder principal |
| Novo módulo de volume | desnecessário |
| APIs de áudio, display, persistência e web | interfaces preservadas |

O diff atual dos quatro arquivos de firmware envolvidos apresenta crescimento
líquido de 136 linhas. Esse total inclui nomes mais explícitos, comentários e a
substituição do antigo modo de volume pelo repouso da navegação; não representa
apenas a segunda instância da biblioteca.

## Plano de validação

1. Confirmar visualmente a revisão da placa e que o LED RGB instalado usa o
   GPIO48, deixando o GPIO38 livre.
2. Mover somente o SCK para GPIO38 e validar montagem, catálogo, leitura e
   reprodução prolongada do microSD a 4 MHz.
3. Validar independentemente o sentido e as transições por detente do encoder
   2 antes de fixar sua calibração.
4. Confirmar que movimentos rápidos preservam todos os passos e não interferem
   no encoder principal.
5. Confirmar que o toque curto no botão do encoder 2 leva Rádio Web e Player ao
   Relógio, mas não interfere durante alarmes nem no próprio Relógio.
6. Confirmar que uma pressão longa nos dois botões não produz clique curto.
7. Validar volume em Rádio Web, Player, Relógio e durante alarmes.
8. Repetir as regressões críticas de alarmes: arquivo local com Wi-Fi desligado
   e Relógio para alarme Rádio Web no perfil Rádio.

`README.md`, `AGENTS.md` e `docs/ARQUITETURA.md` descrevem a implementação. O
projeto só poderá ser considerado validado depois dos testes físicos listados
acima.
