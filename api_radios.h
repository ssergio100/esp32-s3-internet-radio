#ifndef API_RADIOS_H
#define API_RADIOS_H

class WebServer;

// Responde com a lista persistida de estações.
void responderListaRadios(WebServer& servidor);

// Valida a requisição e adiciona uma estação à lista persistida.
void adicionarRadioPelaApi(WebServer& servidor);

// Valida a requisição e exclui uma estação da lista persistida.
void excluirRadioPelaApi(WebServer& servidor);

#endif
