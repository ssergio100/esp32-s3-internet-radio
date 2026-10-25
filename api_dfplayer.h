#ifndef API_DFPLAYER_H
#define API_DFPLAYER_H

class WebServer;

// Publica o catálogo numérico de /01 sem montar o cartão no ESP32.
void responderCatalogoSonsDfPlayer(WebServer& servidor);

#endif
