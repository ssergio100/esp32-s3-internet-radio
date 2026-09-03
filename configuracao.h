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
constexpr int PIN_PCM5102A_BCK  = 5;
constexpr int PIN_PCM5102A_LRCK = 6;  // Rotulado LCK no módulo.
constexpr int PIN_PCM5102A_DIN  = 4;

// Barramento I2C compartilhado pelo display OLED e pelo RTC DS3231.
// Estes pinos mantêm o barramento fora do grupo reservado às válvulas Nixie.
constexpr int PIN_BARRAMENTO_I2C_SDA = 17;
constexpr int PIN_BARRAMENTO_I2C_SCL = 18;

constexpr int DISPLAY_LARGURA  = 128;
constexpr int DISPLAY_ALTURA   = 64;
constexpr int DISPLAY_ENDERECO = 0x3C;

struct ConfiguracaoEncoder {
    int pinoDt;
    int pinoClk;
    int pinoBotao;
    int pinoVcc;
    uint8_t transicoesPorDetente;
};

// O encoder principal navega e confirma opções. O valor de quatro transições
// por detente foi confirmado no componente instalado.
constexpr ConfiguracaoEncoder ENCODER_NAVEGACAO = {
    16,  // DT
    15,  // CLK
    7,   // SW
    -1,  // VCC ligado diretamente, sem GPIO de controle
    4
};

// O segundo encoder controla o volume e seu botão leva uma fonte ativa ao
// Relógio. A calibração começa em quatro e ainda deve ser confirmada no novo
// componente em hardware.
constexpr ConfiguracaoEncoder ENCODER_VOLUME = {
    2,   // DT
    1,   // CLK
    42,  // SW
    -1,  // VCC ligado diretamente, sem GPIO de controle
    4
};

// Cartão microSD do Player, em barramento SPI com pinos explícitos.
constexpr int PIN_CARTAO_PLAYER_SCK  = 38;
constexpr int PIN_CARTAO_PLAYER_MISO = 41;
constexpr int PIN_CARTAO_PLAYER_MOSI = 40;
constexpr int PIN_CARTAO_PLAYER_CS   = 39;

// Reservados para o futuro driver das quatro válvulas Nixie:
// BCD compartilhado: GPIO8, GPIO3, GPIO9 e GPIO10.
// Ânodos independentes: GPIO11, GPIO12, GPIO13 e GPIO14, um por válvula.
// O GPIO46, situado entre esses grupos na placa, permanece sem conexão.

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
constexpr UBaseType_t PRIORIDADE_SERVICO_AUDIO_PLAYER = 1;
constexpr uint32_t INTERVALO_SERVICO_AUDIO_RADIO_MS = 1;
constexpr uint32_t INTERVALO_SERVICO_AUDIO_PLAYER_MS = 3;

// Tratamento dos controles
constexpr unsigned long TEMPO_VALIDACAO_CLIQUE_ENCODER_MS = 30;
constexpr unsigned long INTERVALO_MINIMO_CLIQUES_ENCODER_MS = 200;

// Atualização dos indicadores
constexpr unsigned long INTERVALO_VERIFICACAO_LED_AUDIO_MS = 250;

#endif
