# Dependências fixadas

O perfil de compilação fixa estas versões:

| Componente | Versão |
| --- | --- |
| Arduino core ESP32 (`esp32:esp32`) | 3.3.10 |
| ESP32-audioI2S | 3.4.7 |
| ArduinoJson | 7.4.3 |
| WiFiManager | 2.0.17 |
| Adafruit GFX Library | 1.12.6 |
| Adafruit ST7735 and ST7789 Library | 1.11.0 |
| Ai Esp32 Rotary Encoder | 1.7 |
| DFRobotDFPlayerMini | 1.0.6 |

Essas versões também estão registradas no perfil `esp32s3` de
`sketch.yaml`, incluindo a dependência transitiva Adafruit BusIO 1.17.4.
Ao compilar com `--profile esp32s3`, o Arduino CLI pode instalar os
componentes ausentes. Use `scripts/compilar.sh` para um build rotineiro sem
provisionamento automático.

A PoC do TFT usa o controlador ST7789 com a biblioteca Adafruit 1.11.0. Essa
foi a mesma familia de controlador e driver encontrada no ensaio funcional
local `teste_menu_display`; a integracao ao projeto principal fica isolada por
`RADIO_UI_TFT_POC`.

A DFRobotDFPlayerMini foi acrescentada nesta etapa e ainda precisa da primeira
compilação e validação no módulo físico. As demais versões já pertenciam à
combinação usada pelo firmware.

O DFPlayer usa seu próprio cartão microSD e é controlado pela biblioteca
DFRobotDFPlayerMini. O ESP32 não monta nem lê diretamente esse cartão.
O som padrão dos alarmes é um WAV PCM gerado pelo próprio firmware na FFat e
decodificado pela mesma ESP32-audioI2S; não acrescenta biblioteca ou asset
externo.

O TEF6686 também não acrescenta uma biblioteca externa. O driver validado e o
patch Lithio V102 p224 ficam versionados em `src/tef6686/`; a aplicação acessa
somente a camada `radio_fm.cpp`. O MCP23017 faz parte do firmware principal
para selecionar DAC/FM nas entradas dos NS8002 (`GPA5`), controlar os enables
ativos em LOW dos dois amplificadores (`GPA7/GPA6`) e habilitar a alimentação
do TEF6686 em HIGH (`GPA4`), usando acesso I2C local sem biblioteca
adicional. Os dois encoders continuam ligados diretamente ao ESP32-S3.

Na ArduinoJson 7, use `JsonDocument`, que cresce dinamicamente. As classes
`DynamicJsonDocument` e `StaticJsonDocument` pertencem à camada de
compatibilidade com versões anteriores; o argumento de capacidade de
`DynamicJsonDocument` não reserva nem limita a memória nessa versão.

O teste independente do DS3231 foi validado em hardware com RTClib 2.1.4. A
integração dessa mesma versão ao firmware principal está registrada em
`sketch.yaml`, mas ainda aguarda compilação e validação no rádio completo. O
RTC compartilha com MCP23017 e TEF6686 o `Wire` fornecido pelo Arduino core;
Adafruit BusIO 1.17.4 atende à dependência da RTClib.

Não atualize uma biblioteca isoladamente em uma versão destinada ao
hardware. Crie uma nova combinação, compile e execute o teste de
estabilidade antes de promovê-la.

## Placa

FQBN base:

```text
esp32:esp32:esp32s3
```

Opções validadas:

```text
UploadSpeed=460800
USBMode=hwcdc
CDCOnBoot=default
MSCOnBoot=default
DFUOnBoot=default
UploadMode=default
CPUFreq=240
FlashMode=qio
FlashSize=16M
PartitionScheme=app3M_fat9M_16MB
DebugLevel=none
PSRAM=opi
LoopCore=1
EventsCore=1
EraseFlash=none
JTAGAdapter=default
ZigbeeMode=default
```

A PSRAM OPI é requisito prático da configuração atual da
ESP32-audioI2S. Se a placa física não tiver PSRAM compatível ou o modo
estiver incorreto, a inicialização do áudio falhará explicitamente.
