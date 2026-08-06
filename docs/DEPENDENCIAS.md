# Dependências validadas

Esta revisão foi compilada com:

| Componente | Versão |
| --- | --- |
| Arduino core ESP32 (`esp32:esp32`) | 3.3.10 |
| ESP32-audioI2S | 3.4.7 |
| ArduinoJson | 7.4.3 |
| WiFiManager | 2.0.17 |
| Adafruit GFX Library | 1.12.6 |
| Adafruit SSD1306 | 2.5.17 |
| Ai Esp32 Rotary Encoder | 1.7 |

Essas versões também estão registradas no perfil `esp32s3` de
`sketch.yaml`, incluindo a dependência transitiva Adafruit BusIO 1.17.4.
Ao compilar com `--profile esp32s3`, o Arduino CLI pode instalar os
componentes ausentes. Use `scripts/compilar.sh` para um build rotineiro sem
provisionamento automático.

Na ArduinoJson 7, use `JsonDocument`, que cresce dinamicamente. As classes
`DynamicJsonDocument` e `StaticJsonDocument` pertencem à camada de
compatibilidade com versões anteriores; o argumento de capacidade de
`DynamicJsonDocument` não reserva nem limita a memória nessa versão.

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
