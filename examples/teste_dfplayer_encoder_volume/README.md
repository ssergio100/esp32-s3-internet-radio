# Teste mínimo do DFPlayer com o encoder de volume

Este sketch valida isoladamente o DFPlayer, o cartão microSD e o primeiro
encoder. Ele não inicia Wi-Fi, display, RTC, rádio FM nem o DAC I2S.

## Preparação do cartão

O DFPlayer não aceita um nome livre de pasta no comando usado pelo firmware.
Por isso, a pasta lógica **sons** deve ser gravada fisicamente como `01`:

```text
cartão/
└── 01/
    ├── 001.mp3
    ├── 002.mp3
    ├── 003.mp3
    └── ...
```

Use FAT16 ou FAT32 e um cartão de até 32 GB. A pasta aceita no máximo 255
arquivos nesse modo. Evite remover o cartão com o DFPlayer alimentado.

## Ligações

Os nomes RX e TX abaixo identificam os terminais do DFPlayer.

| DFPlayer Mini | ESP32-S3 |
| --- | --- |
| `VCC` | `5V` |
| `GND` | `GND` comum |
| `RX` | `GPIO20` (`TX` do ESP32), preferencialmente por resistor de 1 kΩ |
| `TX` | `GPIO19` (`RX` do ESP32) |
| `BUSY` | `GPIO38` |

Use as saídas analógicas `DAC_L` e `DAC_R` do DFPlayer no caminho de entrada
direto dos NS8002. Não use `SPK1/SPK2` neste aparelho. O terra analógico do
DFPlayer deve compartilhar a referência do restante do circuito de áudio.

O sketch também inicializa o MCP23017 no mesmo barramento I2C do firmware
principal (`SDA=GPIO16`, `SCL=GPIO17`, endereço `0x20`). Antes de tocar:

- o DAC interno do DFPlayer é habilitado;
- `GPA6=LOW` habilita um canal NS8002;
- `GPA7=LOW` habilita o outro canal NS8002.

No standby ou em caso de erro, o DAC interno é desabilitado e GPA7/GPA6 voltam
a HIGH para silenciar os amplificadores. `GPA5` não é configurado nem alterado
pelo teste, pois o DFPlayer não passa pelo relé.

O encoder de volume continua na pinagem do aparelho:

| Encoder | ESP32-S3 |
| --- | --- |
| `DT` | `GPIO2` |
| `CLK` | `GPIO1` |
| `SW` | `GPIO42` |
| `VCC` | alimentação direta |
| `GND` | `GND` comum |

## Comportamento

- Cada detente no sentido horário toca o próximo arquivo.
- Cada detente no sentido anti-horário toca o arquivo anterior.
- Um clique curto também avança para o próximo arquivo.
- Depois do último arquivo, volta para `001.mp3`.
- A serial em 115200 mostra a quantidade encontrada e o arquivo solicitado.

O teste usa UART2 a 9600 baud e a biblioteca oficial
`DFRobotDFPlayerMini` 1.0.6. A alimentação deve permanecer estável; a DFRobot
recomenda 5 V para evitar falhas de reprodução.
