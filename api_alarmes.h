#ifndef API_ALARMES_H
#define API_ALARMES_H

class WebServer;

void responderListaAlarmes(WebServer& servidor);
void adicionarAlarmePelaApi(WebServer& servidor);
void atualizarAlarmePelaApi(WebServer& servidor);
void excluirAlarmePelaApi(WebServer& servidor);
void responderArquivosPlayer(WebServer& servidor);
void responderRadiosParaAlarmes(WebServer& servidor);

#endif
