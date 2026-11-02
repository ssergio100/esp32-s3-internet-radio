#ifndef WIFI_RADIO_H
#define WIFI_RADIO_H

void conectarWifi();
// Carrega as credenciais persistidas, deixa o rádio Wi-Fi desligado e
// informa se existe uma rede previamente cadastrada.
bool prepararWifiEmStandby();
bool iniciarReconexaoWifiSalvo();
void desligarWifi();
void supervisionarWifi();
bool wifiLigado();
bool wifiConectado();

#endif
