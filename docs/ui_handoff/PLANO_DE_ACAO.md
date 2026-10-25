# Plano de acao e criterios de aceite

## Etapa 0 — auditoria e linha de base (sem mudancas funcionais)
- Nova branch: `feat/tft-ui-poc` a partir do estado funcionando; nao trabalhar direto na principal.
- Identificar biblioteca/driver TFT existente, controlador, barramento (SPI/paralelo), pinos, rotacao 320x240, frequencia, DMA/PSRAM, framework/versoes, mecanismo do OLED, encoders e task de audio.
- Registrar baseline: build OK, uso de heap/PSRAM, comportamento dos encoders, audio e logs de stream; identificar mecanismo atual de status FM/Web.
- Reaproveitar driver LCD ja testado. Nao substituir por suposta configuracao ideal sem evidencia.

## Etapa 1 — PoC TFT usando dados OLED existentes
- Um modulo grafico isolado, atras de flag compilavel (ex.: `RADIO_UI_TFT_POC`); via existente permanece recuperavel.
- Empregar backend/driver existente. Avaliar integracao LVGL v9 somente se compativel com driver e biblioteca no build atual; nao migrar toda a stack para encaixar LVGL.
- Mostrar no TFT inicialmente: modo ativo, freq. FM, sinal, estereo/RDS se disponivel, estacao Web, codec, bitrate, buffer e Wi-Fi se ja coletados, volume; dados nao existentes viram `-` — nao desenvolver novas sondas agora.
- Dados DEVEM vir dos produtores existentes, nao duplicar acesso ao tuner/stream.
- Fontes antialiasing pre-processadas e tema laranja em fundo preto; widgets criados uma vez, atualizados so quando mudam.
- Habilitar teste de alteracao rapida do valor ja existente (sem necessitar iniciar reconexao Web).
- Nenhuma alteracao no protocolo/temporizacao dos encoders, audio, DFPlayer, agendamento ou Nixies nesta fase.
- Fluxo de UI na sua propria task ou loop ja existente sem bloquear streaming; acesso grafico em unico contexto/thread.

## Etapa 2 — prova de responsividade no hardware
- Com audio Web ativo: girar encoder rapidamente, alteracao de volume e entrada de dados dinamicos sem cortes.
- Com FM ativo: mudar frequencia, verificar RDS, nivel de sinal e estabilidade de audio.
- Registar: memoria livre, tempo entre evento de encoder e resposta visivel (meta indicativa p95 <=50 ms), custo de flush, perda de passos, underruns de audio; diferenca em relacao ao baseline.
- Se instavel, ajustar tamanho de buffers, taxa de atualizacao e driver; nao prosseguir a interface nova antes de estabilizar.
- Para LVGL, preferir renderizacao parcial, buffers adequados e flush assincrono/DMA apenas se o driver suportar. Nao presume SPI DMA acessando qualquer regiao da PSRAM.

## Etapa 3 — congelar contrato das telas e navegacao
- Insumos visuais: `prototipo_radio_v5.html`. Definir modelo de estado e interface de eventos, sem acoplamento com rede/tuner.
- TFT apagado em Relogio; girar A nesse estado muda brilho dos Nixies 0..10 e exibe overlay temporario; A pressionado abre menu.
- A gira navega/ajusta, A pressionado seleciona/confirma; B gira volume quando aplicavel, B pressionado volta. Em edicao da hora, B gira o valor e B pressionado alterna horas/minutos.
- Radio Web: girar A altera apenas nome destacado; detalhes de pre-selecao = `-`; pressiona A para conectar; antes disso, emissora tocando permanece intacta.
- Temas laranja padrao, verde, azul, branco, vermelho; cores alimentadas por tokens gerados; sem glow. Configuracoes Tela contem cor e tempo de repouso; nao reintroduzir menus Audio e Sistema.

## Etapa 4 — migracao gradual
- Substituir PoC por componentes de tela: Relogio/overlay, FM, Web, menus, alarmes, chimes e configuracoes.
- Manter compatibilidade com sistema funcional e entrega por pequenos commits.
- So apos testes de regressao e aprovacao do comportamento no hardware considerar desativar/remover backend OLED.

## Paradas obrigatorias
- Se a interface quebrar streaming/audio, rollback da flag/branch.
- Se driver/versao LVGL for incompatível, documentar e propor alternativa antes de reestruturar o firmware.
- Se informacoes de pino ou periferico forem incertas, nao inventar ligacoes/configuracao.

## Entregavel da primeira iteracao
1. Inventario real de arquivos/versoes/pinos e caminho dos produtores de dados do OLED.
2. Diff minimo e isolado para o TFT apresentar dados existentes, usando a stack validada.
3. Procedimento de build e teste, relatorio objetivo com o que foi testado em hardware e o que falta.
4. Nenhuma nova tela complexa ou alteracao de arquiteturas de audio/radio.
