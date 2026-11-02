#ifndef NIXIE_H
#define NIXIE_H

#include <Arduino.h>

// Inicializa as quatro linhas BCD e os quatro chaveamentos ativos em HIGH.
// As válvulas permanecem apagadas até existir uma hora local válida.
bool iniciarNixies();

// Atualiza o quadro HH:MM a partir do relógio do sistema. A multiplexação
// continua inteiramente no temporizador e não depende da velocidade do loop.
void processarNixies();

// Interface mínima reutilizável para futuras telas, testes e efeitos.
// A ordem acompanha a multiplexação física: GPIO14, 13, 12 e 11.
// O valor -1 apaga individualmente a posição correspondente.
void mostrarDigitosNixie(
    int8_t unidadeMinuto,
    int8_t dezenaMinuto,
    int8_t unidadeHora,
    int8_t dezenaHora
);

// Ajusta o brilho em quinze posições lógicas. O nível zero apaga as válvulas;
// os níveis 1 a 4 subdividem a faixa inferior e os níveis 5 a 15 equivalem
// aos onze níveis físicos estáveis de 227 a 2497 us.
void ajustarBrilhoNixie(long deslocamento);
uint8_t obterNivelBrilhoNixie();

// Ajustes experimentais aplicados em tempo real e restaurados aos valores
// padrão de configuracao.h após cada reinicialização.
void ajustarTempoAcesoNixie(long deslocamento);
uint32_t obterTempoAcesoNixieUs();
void ajustarTempoApagadoNixie(long deslocamento);
uint32_t obterTempoApagadoNixieUs();
void ajustarIntervaloBcdNixie(long deslocamento);
uint32_t obterIntervaloBcdNixieUs();

void apagarNixies();

#endif
