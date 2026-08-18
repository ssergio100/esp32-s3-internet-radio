# Teste independente do DS3231SN

Este sketch testa o novo relógio antes de integrá-lo ao firmware principal.
Ele usa a biblioteca **RTClib**, da Adafruit, para acessar o DS3231SN.

Antes de compilar, instale `RTClib` pelo gerenciador de bibliotecas da Arduino
IDE. Essa dependência pertence somente ao exemplo e ainda não foi adicionada ao
firmware principal.

## Configuração da placa

O `sketch.yaml` desta pasta registra o mesmo ESP32-S3, as mesmas opções de
placa e a mesma versão do núcleo usadas pelo rádio web. Ele também fixa RTClib
2.1.4, Adafruit BusIO 1.17.4 e o monitor serial em 115200 baud.

Esse perfil torna a compilação reproduzível com o Arduino CLI. A Arduino IDE 2
ainda não carrega automaticamente do `sketch.yaml` a placa e as opções dos
menus. Ao abrir o exemplo pela primeira vez na IDE, selecione
**Ferramentas > Placa > esp32 > ESP32S3 Dev Module** — não use
`Arduino Nano ESP32` — e aplique as opções listadas em
`../../docs/DEPENDENCIAS.md`. A IDE deverá recordar essa seleção localmente
nas próximas vezes em que o mesmo caminho for aberto.

O sketch também verifica a definição `ARDUINO_ESP32S3_DEV` durante a
compilação. Se a IDE voltar a escolher `Arduino Nano ESP32` ou qualquer outra
placa, a compilação será interrompida com uma mensagem que informa a seleção
correta.

## Ligações

| DS3231SN | ESP32-S3 |
| --- | --- |
| `VCC` | `3V3` |
| `GND` | `GND` |
| `SDA` | `GPIO17` |
| `SCL` | `GPIO18` |

O RTC compartilha o I²C com o OLED: o DS3231SN responde em `0x68` e o OLED
em `0x3C`. O sketch detecta ambos, mas não escreve no display. Alimente o
módulo em 3,3 V para que os pull-ups de SDA e SCL não levem 5 V ao ESP32-S3.

Confira o tipo de bateria e o circuito do módulo antes de instalá-la. Módulos
com circuito de carga não devem carregar uma CR2032, que não é recarregável.

## Uso

1. Abra `teste_ds3231sn.ino` na Arduino IDE e confirme a placa conforme a
   seção anterior.
2. Abra o monitor serial em `115200` baud.
3. Confirme que o endereço `0x68` foi encontrado.
4. Se aparecer `OSF=1`, envie `a` para gravar no RTC a data e hora em que o
   sketch foi compilado.
5. Observe se os segundos avançam e se a temperatura é plausível.

Comandos disponíveis:

- `l`: faz uma leitura imediata;
- `a`: acerta o RTC com `__DATE__` e `__TIME__` da compilação e limpa `OSF`;
- `i`: repete a varredura I²C;
- `h`: mostra a ajuda.

O ajuste não acontece automaticamente em todo boot. Assim é possível desligar
a alimentação principal, aguardar alguns minutos com a bateria instalada e
religar o circuito para confirmar que a hora continuou avançando e que
`OSF=0`. O valor gravado pelo comando `a` pode ficar alguns segundos atrasado,
pois representa o momento da compilação e usa o relógio local do computador.

O firmware principal armazena UTC no DS3231. Portanto, o valor local gravado
por este teste serve somente à validação inicial; na primeira sincronização do
firmware completo, o NTP substituirá esse valor pela representação UTC correta.
