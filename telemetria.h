#ifndef TELEMETRIA_H
#define TELEMETRIA_H

/*
 * Publica periodicamente na porta serial um resumo do estado do sistema.
 * A função é não bloqueante e deve ser chamada continuamente no loop().
 */
void registrarTelemetriaPeriodica();

#endif
