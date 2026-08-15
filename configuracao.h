#ifndef CONFIGURACAO_H
#define CONFIGURACAO_H

#include <Arduino.h>

// =====================================================
// Ligações do hardware
// =====================================================

// LED RGB integrado à placa
#define PIN_LED_RGB 48

// Amplificador de áudio I2S MAX98357A
constexpr int PIN_MAX98357A_BCLK = 5;
constexpr int PIN_MAX98357A_LRC  = 6;
constexpr int PIN_MAX98357A_DIN  = 7;

// Display OLED SSD1306
constexpr int PIN_DISPLAY_SDA = 9;
constexpr int PIN_DISPLAY_SCL = 10;

constexpr int DISPLAY_LARGURA  = 128;
constexpr int DISPLAY_ALTURA   = 64;
constexpr int DISPLAY_ENDERECO = 0x3C;

// Encoder rotativo principal
constexpr int PIN_ENCODER_CLK = 16;
constexpr int PIN_ENCODER_DT  = 15;
constexpr int PIN_ENCODER_SW  = 17;
constexpr int PIN_ENCODER_VCC = -1;

// Leitor microSD no barramento SPI
constexpr int PIN_CARTAO_MICRO_SD_SCK  = 11;
constexpr int PIN_CARTAO_MICRO_SD_MISO = 12;
constexpr int PIN_CARTAO_MICRO_SD_MOSI = 13;
constexpr int PIN_CARTAO_MICRO_SD_CS   = 14;

// Frequência conservadora para módulos com conversores de nível e fios longos.
// Aumente somente depois de confirmar a leitura estável do cartão.
constexpr uint32_t FREQUENCIA_CARTAO_MICRO_SD_HZ = 1000000;

// Valor calibrado para o encoder instalado: cada detente produz quatro
// transições válidas reconhecidas pela biblioteca.
constexpr int TRANSICOES_ENCODER_POR_DETENTE = 4;

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

// Interface
// Intensidade de cada canal do LED, de 0 (apagado) a 255 (máximo).
#define BRILHO_LED_RGB 50

// Tempo durante o qual a barra inferior mostra o volume após um ajuste.
constexpr unsigned long TEMPO_BARRA_VOLUME_MS = 2000;
constexpr unsigned long TEMPO_INATIVIDADE_SELECAO_MS = 10000;

// Tempo que o botão do encoder deve permanecer pressionado para desligar.
// Aumente para reduzir acionamentos acidentais; diminua para desligar mais rápido.
constexpr unsigned long TEMPO_CLIQUE_LONGO_ENCODER_MS = 2000;

// O LED alterna entre azul e apagado a cada intervalo.
// Diminua o valor para piscar mais rápido; aumente para piscar mais devagar.
constexpr unsigned long INTERVALO_PISCA_LED_CONEXAO_WIFI_MS = 100;

// O nome avança um pixel a cada passo.
// Diminua o intervalo para acelerar a rolagem; aumente para desacelerar.
constexpr unsigned long INTERVALO_PASSO_ROLAGEM_NOME_MS = 40;

// A faixa superior mostra codec, bitrate, buffer e dados passivos do Wi-Fi.
// O primeiro intervalo controla a velocidade da rolagem para a direita.
// O segundo controla a frequência de renovação dos valores exibidos.
constexpr unsigned long INTERVALO_PASSO_ROLAGEM_DIAGNOSTICO_MS = 13;
constexpr unsigned long INTERVALO_ATUALIZACAO_DIAGNOSTICO_DISPLAY_MS = 1000;

// Diagnóstico
constexpr unsigned long INTERVALO_TELEMETRIA_SERIAL_MS = 5000;

// Relógio
// Exemplo: -3 corresponde ao horário UTC-3 usado em Brasília.
constexpr int FUSO_HORARIO_UTC_HORAS = -3;
constexpr int AJUSTE_HORARIO_VERAO_HORAS = 0;
constexpr const char* SERVIDOR_NTP_PRIMARIO = "pool.ntp.org";
constexpr const char* SERVIDOR_NTP_SECUNDARIO = "time.nist.gov";

// =====================================================
// Ajustes internos
// =====================================================

// Tarefas de áudio
constexpr int NUCLEO_DECODIFICADOR_AUDIO = 0;
constexpr int NUCLEO_SERVICO_AUDIO = 1;
constexpr uint32_t PILHA_SERVICO_AUDIO_BYTES = 12288;
constexpr UBaseType_t PRIORIDADE_SERVICO_AUDIO = 3;

// Tratamento dos controles
constexpr unsigned long TEMPO_VALIDACAO_CLIQUE_ENCODER_MS = 30;
constexpr unsigned long INTERVALO_MINIMO_CLIQUES_ENCODER_MS = 200;

// Evita impedir o sono indefinidamente caso o serviço de áudio não confirme
// a parada. No deep sleep, o próprio ESP32 interrompe os periféricos.
constexpr unsigned long TEMPO_MAXIMO_PARADA_AUDIO_ANTES_SONO_MS = 1000;

// Atualização dos indicadores
constexpr unsigned long INTERVALO_VERIFICACAO_LED_AUDIO_MS = 250;

#endif
