#ifndef UPLOAD_ARQUIVOS_H
#define UPLOAD_ARQUIVOS_H

class WebServer;

// Recebe cada parte do arquivo enviado pelo cliente HTTP.
void processarDadosUpload(WebServer& servidor);

// Responde ao cliente depois que todas as partes foram processadas.
void responderResultadoUpload(WebServer& servidor);

#endif
