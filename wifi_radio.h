#ifndef WIFI_RADIO_H
#define WIFI_RADIO_H

void conectarWifi();
// Carrega as credenciais persistidas e deixa o rádio Wi-Fi desligado.
void prepararWifiEmStandby();
bool iniciarReconexaoWifiSalvo();
void desligarWifi();
void supervisionarWifi();
bool wifiLigado();
bool wifiConectado();

#endif
