#ifndef API_STATUS_H
#define API_STATUS_H

#include <Arduino.h>

// Cria uma fotografia do estado atual do rádio para diagnóstico remoto.
// O resultado está pronto para ser enviado como application/json.
String criarStatusSistemaJson();

#endif
