#ifndef API_CHIME_H
#define API_CHIME_H

class WebServer;

void responderConfiguracaoChime(WebServer& servidor);
void atualizarConfiguracaoChime(WebServer& servidor);
void reproduzirPreviaChime(WebServer& servidor);

#endif
