# Migração do áudio para PCM5102A estéreo

## Estado e limite do módulo

A configuração básica foi refletida no firmware. Compilação e validação
elétrica/sonora no hardware permanecem pendentes.

O componente fotografado é um `PCM5102A`: um DAC estéreo com entrada **I2S**,
não I2C. Ele converte o áudio digital em duas saídas analógicas de linha. Não é
um amplificador de potência e não deve receber alto-falantes passivos
diretamente. Para substituir fisicamente os dois MAX98357A, será necessário
ligar `LROUT`, `ROUT` e `AGND` à entrada de um amplificador estéreo analógico
adequado aos alto-falantes.

As fotos coincidem com o layout conhecido como GY-PCM5102, mas não mostram
fabricante nem revisão. O esquema desse breakout encontrado publicamente é de
engenharia reversa, não documentação do fabricante. Por isso, a alimentação e
os jumpers devem ser confirmados com multímetro antes de energizar.

Fontes técnicas usadas nesta decisão:

- datasheet PCM510xA Rev. C da Texas Instruments:
  <https://www.ti.com/lit/ds/symlink/pcm5102a.pdf>;
- documentação da ESP32-audioI2S, que aceita PCM5102A em I2S padrão com BCK,
  LRCK e dados: <https://github.com/schreibfaul1/ESP32-audioI2S/wiki>;
- esquema reverso do breakout visualmente correspondente:
  <https://macsbug.wordpress.com/wp-content/uploads/2021/02/pcm5102a_dac_schematic.pdf>.

## Ligação básica para o primeiro teste

Os três sinais I2S atuais podem ser mantidos; não há necessidade de reservar
novos GPIOs.

| ESP32-S3-DevKitC-1 | PCM5102A | Função |
| --- | --- | --- |
| `GPIO5` | `BCK` | clock de bits |
| `GPIO6` | `LCK` | seleção esquerda/direita (`LRCK`/`WS`) |
| `GPIO4` | `DIN` | dados enviados pelo ESP32 ao DAC |
| `GND` | `GND` | referência comum digital/alimentação |
| `5V` | `VIN` | alimentação pelo regulador do breakout correspondente |

Antes de aplicar os 5 V, confirmar que a placa é eletricamente igual ao
breakout documentado: `VIN` deve alimentar a entrada do regulador e `A3V3` deve
estar isolado de `VIN`. Depois de energizar, medir aproximadamente 3,3 V em
`A3V3`; se a tensão não estiver dentro de 3,0 V a 3,46 V, desligar e revisar a
placa. Não ligar `A3V3` ao pino de 5 V.

Para o clock de três fios, `SCK` precisa ficar em GND. A foto mostra o jumper
frontal próprio para isso, mas sua continuidade deve ser medida com a placa
desligada. Se houver continuidade `SCK`–`GND`, deixar o pino `SCK` sem fio. Se
não houver, fechar somente esse jumper ou ligar `SCK` a GND. Nunca dirigir SCK
por um GPIO enquanto ele estiver curto-circuitado a GND.

As saídas para o teste devem ir a uma entrada de linha/AUX ou a caixas ativas:

| PCM5102A | Destino analógico |
| --- | --- |
| `LROUT` ou contato `L` do jack | entrada esquerda |
| `ROUT` ou contato `R` do jack | entrada direita |
| `AGND` ou contato `G` do jack | terra da entrada |

O datasheet especifica saída de linha de 2,1 VRMS e carga mínima de 1 kΩ. Isso
exclui alto-falantes passivos e fones comuns de baixa impedância como carga de
validação.

## Configuração dos jumpers

Para o formato já produzido pela ESP32-audioI2S, os quatro controles precisam
ficar nestes estados:

| Jumper/pino | Estado básico | Motivo |
| --- | --- | --- |
| `H1` / `FLT` | `L` | filtro normal |
| `H2` / `DEMP` | `L` | de-emphasis de 44,1 kHz desligado |
| `H3` / `XSMT` | `H` | saída desmutada |
| `H4` / `FMT` | `L` | formato I2S padrão |

No jumper de três ilhas, unir o contato numerado somente ao lado marcado `H`
ou `L`; nunca fechar os dois lados. As fotos não bastam para comprovar quais
pontes estão eletricamente fechadas. Confirmar com multímetro entre cada pino
de controle e `A3V3`/`AGND` antes de ligar. `XSMT` em nível baixo deixa o DAC
mudo; `FMT` em nível alto seleciona left-justified e não corresponde à
configuração atual da biblioteca.

## Pinos reservados para melhorias futuras

### Prioridade alta: controle de `XSMT`

Em uma segunda etapa, remover a ponte fixa de `XSMT` para `H` e ligar `XSMT` a
um GPIO. `GPIO21` é o candidato atual: está exposto na revisão inicial da
ESP32-S3-DevKitC-1 e não aparece alocado pelo projeto. A revisão física da placa
e a ausência de outra ligação devem ser confirmadas antes da montagem.

O firmware poderá então manter o DAC mutado durante boot, troca de fonte e
desligamento, desmutando somente depois de iniciar o I2S. Isso permite aplicar a
rampa interna de soft mute e preparar uma sequência de desligamento com menos
estalos. A saída do ESP32 é 3,3 V; nunca aplicar 5 V a `XSMT`.

### Dependente do amplificador analógico

O amplificador estéreo ainda precisa ser escolhido. Se ele oferecer
`EN`, `SHDN`, `MUTE` ou sinal de falha, reservar um GPIO somente depois de
confirmar o datasheet, polaridade, estado durante reset e tensão lógica. O
controle deverá coordenar a ordem `mute → amplificador off → DAC` no
desligamento e a ordem inversa na partida.

### Baixa prioridade

- `FLT`: pode ser controlado para selecionar o filtro de baixa latência, mas o
  modo normal fixo atende ao rádio e ao Player.
- `DEMP`: só deve ser ativado para material de 44,1 kHz gravado com
  pre-emphasis; fica fixo desligado no uso atual.
- `SCK`: o PCM5102A já deriva o clock interno de BCK. Gerar MCLK no ESP32
  acrescentaria um fio e um GPIO sem necessidade demonstrada.
- `A3V3`: é o barramento analógico de 3,3 V do breakout, não um controle. Fica
  sem conexão externa na montagem básica.

## Roteiro de validação em bancada

1. Com tudo desligado, conferir curto entre `SCK` e `GND`, posição exclusiva
   dos quatro jumpers e ausência de curto entre alimentação e terra.
2. Alimentar somente o breakout e medir `A3V3`; desligar se sair da faixa
   registrada acima ou se algum componente aquecer.
3. Ligar GND e, em seguida, os três sinais I2S com fios curtos.
4. Conectar a saída a uma entrada de linha com o volume do destino inicialmente
   baixo; só então executar o exemplo mínimo ou o firmware principal.
5. Confirmar canal esquerdo, canal direito, ajuste de volume, silêncio ao parar,
   reinicialização e transições Rádio Web/Player/Relógio.
6. Validar o amplificador analógico e os alto-falantes em etapa separada, com
   alimentação dimensionada para eles.
