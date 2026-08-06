#ifndef SONO_PROFUNDO_H
#define SONO_PROFUNDO_H

// Registra na serial quando a inicialização atual ocorreu pelo botão.
void informarMotivoDespertar();

// Prepara o botão do encoder, ativo em nível baixo, como fonte de despertar.
// Retorna false se o pino não puder ser usado pelo domínio RTC.
bool configurarDespertarPeloBotaoEncoder();

// Interrompe a execução e somente retorna por uma nova inicialização.
void entrarSonoProfundo();

#endif
