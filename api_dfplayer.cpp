#include "api_dfplayer.h"

#include <Arduino.h>
#include <WebServer.h>

#include "configuracao.h"
#include "dfplayer.h"

void responderCatalogoSonsDfPlayer(WebServer& servidor) {
    CatalogoSonsDfPlayer catalogo = obterCatalogoSonsDfPlayer();

    servidor.setContentLength(CONTENT_LENGTH_UNKNOWN);
    servidor.send(200, "application/json", "");

    char cabecalho[224];
    snprintf(
        cabecalho,
        sizeof(cabecalho),
        "{\"disponivel\":%s,\"pasta\":%u,\"quantidade\":%u,"
        "\"contagemPasta\":%d,\"contagemCartao\":%d,"
        "\"contagensDivergentes\":%s,\"sons\":[",
        catalogo.disponivel ? "true" : "false",
        static_cast<unsigned>(PASTA_SONS_DFPLAYER),
        static_cast<unsigned>(catalogo.quantidade),
        catalogo.quantidadeInformadaPasta,
        catalogo.quantidadeInformadaCartao,
        catalogo.contagensDivergentes ? "true" : "false"
    );
    servidor.sendContent(cabecalho);

    for (uint16_t numero = 1; numero <= catalogo.quantidade; numero++) {
        SomDfPlayer som;
        if (!obterSomDfPlayer(static_cast<uint8_t>(numero), som)) {
            continue;
        }

        char item[96];
        snprintf(
            item,
            sizeof(item),
            "%s{\"id\":%u,\"arquivo\":\"%s\",\"nome\":\"%s\"}",
            numero == 1 ? "" : ",",
            static_cast<unsigned>(som.numero),
            som.arquivo,
            som.nome
        );
        servidor.sendContent(item);
    }

    servidor.sendContent("]}");
}
