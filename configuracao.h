#ifndef CONFIGURACAO_H
#define CONFIGURACAO_H

#include <Arduino.h>

// =====================================================
// Ligações do hardware
// =====================================================

// LED RGB integrado à placa
#define PIN_LED_RGB 48

// DAC estéreo I2S PCM5102A. O módulo usa o BCK como referência para a PLL
// interna; SCK deve permanecer em GND e não consome um GPIO do ESP32-S3.
// Os três sinais ficam no conector J3. GPIO45 é um pino de strap, mas no
// módulo N16R8 a tensão da memória é fixada por eFuse; a entrada BCK do DAC
// não deve impor nível ao GPIO45 durante o reset.
constexpr int PIN_PCM5102A_BCK  = 21;
constexpr int PIN_PCM5102A_LRCK = 45;  // Rotulado LCK no módulo.
constexpr int PIN_PCM5102A_DIN  = 47;

// Barramento I2C compartilhado pelo display OLED, RTC DS3231 e TEF6686.
// Estes pinos mantêm o barramento fora do grupo reservado às válvulas Nixie.
constexpr int PIN_BARRAMENTO_I2C_SDA = 16;
constexpr int PIN_BARRAMENTO_I2C_SCL = 17;
constexpr uint32_t FREQUENCIA_BARRAMENTO_I2C_HZ = 100000;

// Chaveamento da saída de áudio pelo MCP23017. A0, A1 e A2 do endereço do
// expansor devem estar ligados ao GND para selecionar 0x20. GPA7 seleciona,
// por relé, DAC ou FM nas entradas dos NS8002. GPA6/GPA5 são enables ativos
// em LOW: LOW habilita e HIGH silencia. O DFPlayer chega diretamente aos
// NS8002 e altera somente GPA6/GPA5, sem comandar GPA7. Não são GPIOs do ESP32.
constexpr uint8_t ENDERECO_I2C_MCP23017 = 0x20;
constexpr uint8_t PIN_MCP23017_RELE_8002_SELECIONA_DAC = 7;
constexpr uint8_t PIN_MCP23017_ENABLE_N_8002_DIREITO = 6;
constexpr uint8_t PIN_MCP23017_ENABLE_N_8002_ESQUERDO = 5;

constexpr int DISPLAY_LARGURA  = 128;
constexpr int DISPLAY_ALTURA   = 64;
constexpr int DISPLAY_ENDERECO = 0x3C;

// -----------------------------------------------------
// TFT ST7789 320x240 — PoC grafica recuperavel
// -----------------------------------------------------
// A flag mantem o caminho OLED anterior intacto. Em 0, o modulo TFT nem e
// compilado. O ST7789 foi identificado no ensaio funcional existente e usa
// SPI de hardware; a DisplayService e a unica dona deste barramento.
#define RADIO_UI_TFT_POC 1

#if RADIO_UI_TFT_POC
constexpr int PIN_TFT_SCK = 10;   // SCL no conector do modulo TFT.
constexpr int PIN_TFT_MOSI = 9;   // SDA no conector do modulo TFT.
constexpr int PIN_TFT_CS = 8;
constexpr int PIN_TFT_DC = 18;
constexpr int PIN_TFT_RST = 15;
constexpr int TFT_LARGURA = 320;
constexpr int TFT_ALTURA = 240;
constexpr uint32_t FREQUENCIA_SPI_TFT_HZ = 40000000;
#endif

// Rádio FM TEF6686. Estes parâmetros reproduzem o exemplo funcional e ficam
// fora do coordenador principal para permitir trocar apenas o firmware do rádio.
constexpr uint8_t ENDERECO_I2C_TEF6686 = 0x64;
constexpr uint32_t CLOCK_TEF6686_HZ = 9216000;
constexpr bool CLOCK_TEF6686_EXTERNO = false;
constexpr uint16_t FREQUENCIA_INICIAL_RADIO_FM_10KHZ = 10010;
constexpr unsigned long INTERVALO_LEITURA_QUALIDADE_RADIO_FM_MS = 100;
constexpr unsigned long INTERVALO_LEITURA_RDS_RADIO_FM_MS = 43;

struct ConfiguracaoEncoder {
    int pinoDt;
    int pinoClk;
    int pinoBotao;
    int pinoVcc;
    uint8_t transicoesPorDetente;
};

// O primeiro encoder ajusta o volume durante a reprodução. Seu botão abre a
// seleção de fonte; nessa tela, o giro percorre Relógio, Rádio FM e Rádio Web e
// o clique confirma. Quatro transições por detente foram confirmadas.
constexpr ConfiguracaoEncoder ENCODER_VOLUME  = {
    2,  // DT
    1,  // CLK
    42,   // SW
    -1,  // VCC ligado diretamente, sem GPIO de controle
    4
};

// O segundo encoder percorre a lista de rádios e seu botão confirma a estação.
// A calibração de quatro transições deve ser confirmada no hardware instalado.
constexpr ConfiguracaoEncoder ENCODER_NAVEGACAO = {
    41,   // DT
    40,   // CLK
    39,  // SW
    -1,  // VCC ligado diretamente, sem GPIO de controle
    4
};

// -----------------------------------------------------
// DFPlayer Mini — UART2, conector J3
// -----------------------------------------------------
// Os nomes RX/TX identificam os terminais do DFPlayer: o GPIO20 transmite do
// ESP32 para o RX do módulo; o GPIO19 recebe no ESP32 o TX do módulo. BUSY
// fica em LOW enquanto o módulo reproduz.
// GPIO19/GPIO20 deixam de atender ao USB nativo/JTAG. Programação e diagnóstico
// permanecem no USB-UART ligado à UART0 em GPIO43/GPIO44.
// UART_DFPLAYER seleciona um controlador serial interno do ESP32-S3: o valor 2
// significa UART2 e NÃO representa o GPIO2 nem um pino físico do DFPlayer.
constexpr int UART_DFPLAYER = 2;
constexpr uint32_t BAUD_DFPLAYER = 9600;
constexpr int PIN_DFPLAYER_RX = 20;  // GPIO ligado ao RX do DFPlayer (ESP TX).
constexpr int PIN_DFPLAYER_TX = 19;  // GPIO ligado ao TX do DFPlayer (ESP RX).
constexpr int PIN_DFPLAYER_BUSY = 38;

// O DFPlayer não endereça pastas por nomes livres. A "pasta sons" do cartão
// deve ser fisicamente /01, com arquivos /01/001.mp3, /01/002.mp3 etc.
// O comando playFolder suporta pastas 1..99 e até 255 arquivos por pasta.
constexpr uint8_t PASTA_SONS_DFPLAYER = 1;
constexpr uint8_t VOLUME_MAXIMO_DFPLAYER = 30;  // Faixa nativa: 0..30.
// Diagnóstico temporário do agendador, UART/BUSY e chaveamento do chime.
constexpr bool DEBUG_CHIME = true;
constexpr uint32_t TIMEOUT_DFPLAYER_MS = 800;

// -----------------------------------------------------
// Driver das quatro válvulas Nixie — reserva definitiva no conector J1
// -----------------------------------------------------
// As quatro linhas BCD são compartilhadas pelo driver. A, B, C e D possuem,
// respectivamente, os pesos binários 1, 2, 4 e 8.
constexpr int PIN_NIXIE_BCD_A = 4;  // peso 1
constexpr int PIN_NIXIE_BCD_B = 5;  // peso 2
constexpr int PIN_NIXIE_BCD_C = 6;  // peso 4
constexpr int PIN_NIXIE_BCD_D = 7;  // peso 8

// Uma linha independente seleciona o cátodo de cada válvula. A polaridade
// ativa será definida no driver quando o estágio elétrico estiver confirmado.
constexpr int PIN_NIXIE_CATODO_1 = 11;
constexpr int PIN_NIXIE_CATODO_2 = 12;
constexpr int PIN_NIXIE_CATODO_3 = 13;
constexpr int PIN_NIXIE_CATODO_4 = 14;

// =====================================================
// Comportamento ajustável pelo usuário
// =====================================================

// Wi-Fi
// O rádio nunca se associa aos pontos de acesso abaixo. A regra considera
// somente o BSSID: redes e pontos de acesso diferentes continuam permitidos.
constexpr const char* BSSIDS_WIFI_BLOQUEADOS[] = {
    "DC:33:3D:F9:C0:34"
};

// Áudio
constexpr int VOLUME_MINIMO = 0;
constexpr int VOLUME_MAXIMO = 21;
constexpr int VOLUME_PADRAO = 10;

// O PCM5102A entra em power-down após os clocks permanecerem parados por
// aproximadamente 1 segundo. A margem garante que o TEF6686 só seja acordado
// depois desse desligamento, sem bloquear o loop nem a leitura dos encoders.
constexpr uint32_t TEMPO_POWER_DOWN_PCM5102A_MS = 1100;

// Frequências maiores encurtam os bloqueios de leitura que disputam CPU com o
// OLED. Reduza somente se o cartão instalado ficar instável.
constexpr uint32_t FREQUENCIA_CARTAO_PLAYER_HZ = 4000000;

// Quando ativa, o fim de uma música inicia a próxima e, depois da última,
// retorna à primeira. A escolha manual de outro arquivo continua prevalecendo.
constexpr bool REPRODUCAO_SEQUENCIAL_PLAYER = true;

// O arquivo do alarme recomeça até o clique curto no encoder principal ou este
// limite.
// Um novo alarme sempre substitui o que estiver em execução.
constexpr uint32_t DURACAO_MAXIMA_ALARME_MINUTOS = 30;

// Uma fonte Rádio Web aguarda rede ou stream por este tempo antes do som padrão.
constexpr uint32_t TEMPO_LIMITE_CONEXAO_RADIO_ALARME_MS = 20000;

// Interface
// Intensidade de cada canal do LED, de 0 (apagado) a 255 (máximo).
#define BRILHO_LED_RGB 50

// Tempo durante o qual a barra inferior mostra o volume após um ajuste.
constexpr unsigned long TEMPO_BARRA_VOLUME_MS = 2000;
constexpr unsigned long TEMPO_INATIVIDADE_SELECAO_MS = 10000;

// Duração máxima aceita como clique curto nos dois encoders. Uma pressão mais
// longa é descartada. Reduza para exigir um toque mais rápido.
constexpr unsigned long TEMPO_MAXIMO_CLIQUE_CURTO_ENCODER_MS = 2000;

// O LED alterna entre azul e apagado a cada intervalo.
// Diminua o valor para piscar mais rápido; aumente para piscar mais devagar.
constexpr unsigned long INTERVALO_PISCA_LED_CONEXAO_WIFI_MS = 100;

// O nome avança um pixel a cada passo.
// Diminua o intervalo para acelerar a rolagem; aumente para desacelerar.
constexpr unsigned long INTERVALO_PASSO_ROLAGEM_NOME_MS = 40;

// O Player atualiza o OLED com menos frequência para reservar CPU à leitura
// local. Diminua para acelerar a rolagem; aumente para aliviar ainda mais o CPU.
constexpr unsigned long INTERVALO_PASSO_ROLAGEM_PLAYER_MS = 80;

// A faixa superior mostra codec, bitrate, buffer e dados passivos do Wi-Fi.
// O primeiro intervalo controla a velocidade da rolagem para a direita.
// O segundo controla a frequência de renovação dos valores exibidos.
constexpr unsigned long INTERVALO_PASSO_ROLAGEM_DIAGNOSTICO_MS = 40;
constexpr unsigned long INTERVALO_ATUALIZACAO_DIAGNOSTICO_DISPLAY_MS = 1000;

// A data completa no rodapé do relógio avança um pixel a cada passo.
constexpr unsigned long INTERVALO_PASSO_ROLAGEM_DATA_RELOGIO_MS = 80;

// A tarefa do display acorda quatro vezes dentro do menor intervalo de
// rolagem. Isso desacopla a animação do servidor web sem aumentar a prioridade
// sobre o loop da interface ou sobre o decodificador de áudio.
constexpr uint32_t INTERVALO_SERVICO_DISPLAY_MS =
    INTERVALO_PASSO_ROLAGEM_NOME_MS / 4;
static_assert(
    INTERVALO_SERVICO_DISPLAY_MS > 0,
    "O intervalo do servico do display deve ser positivo."
);

// Diagnóstico
constexpr unsigned long INTERVALO_TELEMETRIA_SERIAL_MS = 5000;

// Relógio
// Exemplo: -3 corresponde ao horário UTC-3 usado em Brasília.
constexpr int FUSO_HORARIO_UTC_HORAS = -3;
constexpr int AJUSTE_HORARIO_VERAO_HORAS = 0;
constexpr const char* SERVIDOR_NTP_PRIMARIO = "pool.ntp.org";
constexpr const char* SERVIDOR_NTP_SECUNDARIO = "time.nist.gov";

// O SNTP corrige o relógio do sistema neste intervalo. O DS3231 só é gravado
// quando a diferença alcançar o limiar abaixo ou quando perder a referência.
// Aumente o intervalo para reduzir consultas; diminua para corrigir a deriva
// do sistema com maior frequência enquanto houver rede.
constexpr uint32_t INTERVALO_SINCRONIZACAO_NTP_MS = 3600000;
constexpr uint32_t DESVIO_MINIMO_AJUSTE_RTC_SEGUNDOS = 2;

// =====================================================
// Ajustes internos
// =====================================================

// A tarefa interna do decoder/I2S fica no núcleo 1, acima do loop Arduino em
// prioridade. O serviço que abastece o buffer pela rede fica no núcleo 0,
// abaixo das tarefas internas de Wi-Fi e TCP/IP. Isso isola a entrega PCM da
// contenção de rede observada no núcleo 0.
constexpr int NUCLEO_DECODIFICADOR_AUDIO = 1;
constexpr int NUCLEO_SERVICO_AUDIO = 0;
constexpr uint32_t PILHA_SERVICO_AUDIO_BYTES = 12288;
constexpr UBaseType_t PRIORIDADE_SERVICO_AUDIO_RADIO = 3;

constexpr uint32_t INTERVALO_SERVICO_AUDIO_RADIO_MS = 1;
constexpr uint32_t INTERVALO_SERVICO_AUDIO_PLAYER_MS = 3;

// Tratamento dos controles
constexpr unsigned long TEMPO_VALIDACAO_CLIQUE_ENCODER_MS = 30;
constexpr unsigned long INTERVALO_MINIMO_CLIQUES_ENCODER_MS = 200;

// Atualização dos indicadores
constexpr unsigned long INTERVALO_VERIFICACAO_LED_AUDIO_MS = 250;

#endif
