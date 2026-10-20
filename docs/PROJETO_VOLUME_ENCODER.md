# Projeto: encoders e resposta da interface

Este documento registra o contrato dos dois encoders e o caminho de baixa
latência entre a entrada física, o áudio e o OLED.

## Estado

Implementado e compilado para ESP32-S3. A validação final no equipamento
físico permanece obrigatória, pois o ambiente de compilação não possui uma
porta serial conectada.

## Contrato da interface

1. O encoder 1 controla o volume pelo giro.
2. Fora de alarmes, um clique curto no encoder 1 alterna diretamente entre
   `RADIO_WEB` e `RELOGIO`. A volta ao rádio inicia a reconexão Wi-Fi de forma
   assíncrona. Os encoders continuam sendo lidos durante a espera, mas o
   encoder 2 permanece inativo enquanto a Rádio Web ainda não foi ativada.
3. O encoder 2 percorre somente a lista de rádios. Girar a partir do repouso
   abre a seleção e aplica o deslocamento no mesmo ciclo.
4. Um clique curto no encoder 2 abre a seleção quando ela está fechada e
   confirma a estação quando ela está aberta.
5. No Relógio, o encoder 2 não executa ação. Durante um alarme, seu clique
   continua encerrando o alarme.
6. Pressões longas são reconhecidas e descartadas; não viram clique ao soltar.

## Ligações em uso

### Encoder 1: volume e fonte

| Sinal | GPIO |
| --- | ---: |
| DT | 2 |
| CLK | 1 |
| SW | 42 |
| VCC controlado | não usado (`-1`) |

### Encoder 2: rádios

| Sinal | GPIO |
| --- | ---: |
| DT | 41 |
| CLK | 40 |
| SW | 39 |
| VCC controlado | não usado (`-1`) |

Os dois componentes usam quatro transições por detente. Essa calibração
deve ser confirmada no hardware instalado; se um componente for diferente, a
alteração fica restrita ao respectivo `ConfiguracaoEncoder`.

## Captura dos passos

Cada encoder possui uma instância de `AiEsp32RotaryEncoder` 1.7 e uma ISR
própria. A ISR registra as transições mesmo quando outra tarefa está ocupada;
`encoderChanged()` entrega o deslocamento assinado acumulado desde a leitura
anterior. A aceleração está desativada para que um detente corresponda a um
passo da aplicação.

Não é aplicado limite artificial de `-10000..10000`: permanecem os limites
nativos amplos da biblioteca, evitando saturação depois de uso prolongado.

## Caminho de baixa latência

- O `loop()` lê os controles antes dos serviços cooperativos.
- Ajustes consecutivos de volume não ocupam a fila geral de comandos. Um valor
  atômico conserva sempre o ajuste mais recente e a tarefa de áudio o aplica no
  próximo ciclo.
- No caminho normal, com `DisplayService` ativa, as APIs do display não
  transferem o framebuffer I2C no contexto do `loop()`. Elas atualizam o
  modelo, marcam a tela como pendente e acordam a tarefa. Se a criação da
  tarefa falhar, o caminho de contingência faz a apresentação no chamador.
- Notificações repetidas do OLED são consolidadas. Se o usuário girar rápido,
  a próxima imagem apresenta o estado mais novo em vez de transmitir todos os
  quadros intermediários.
- Com a tarefa ativa, a transferência I2C ocorre fora do mutex do modelo.
  Assim, uma nova leitura de encoder não espera o envio integral do framebuffer
  do SSD1306.

## Volume no Relógio

O giro do encoder 1 no Relógio altera o volume reservado para a próxima
ativação da rádio. Na próxima apresentação, o rodapé do OLED mostra a barra
e o valor; depois de `TEMPO_BARRA_VOLUME_MS`, a data volta automaticamente.

## Validação física obrigatória

1. Girar cada encoder lentamente nos dois sentidos e confirmar exatamente um
   passo por detente.
2. Girar cada encoder rapidamente por pelo menos dez voltas, nos dois sentidos,
   e comparar a posição final com a quantidade de detentes aplicada.
3. Ajustar volume enquanto nome e diagnóstico estão rolando; o som deve reagir
   sem aguardar o OLED, e a tela deve convergir diretamente ao valor mais novo.
4. Navegar por toda a lista, dar a volta nas duas extremidades e confirmar uma
   rádio diferente.
5. Alternar repetidamente Rádio/Relógio pelo encoder 1, inclusive com Wi-Fi
   demorando para reassociar; o encoder deve continuar respondendo.
6. Validar os dois cliques curtos, o descarte de pressão longa e os controles
   durante um alarme.
7. Repetir os testes após reset e ciclo completo de energia.

Compilação confirma integração e tipos; somente estes testes no equipamento
confirmam contato elétrico, quantidade real de transições e resposta percebida.
