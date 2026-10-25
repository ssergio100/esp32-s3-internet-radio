# INSTRUCAO PARA O AGENTE — executar SOMENTE etapas 0 e 1

**Contexto:** `radio_web_1` e um projeto de radio hibrido ESP32-S3 N16R8 ja funcional. TFT 320x240 horizontal foi conectado; existia exibicao de dados em OLED. Objetivo desta tarefa: provar arquitetura grafica de fontes suaves, atualizacao parcial e desempenho no TFT, SEM iniciar refatoracao geral nem programar menus finais.

## Ordem obrigatoria
1. `git status`: verificar mudancas locais; nao sobrepor trabalho nem alterar branch sem proteger mudancas. Propor/criar uma branch de experimento `feat/tft-ui-poc` sem descartar alteracoes.
2. Inventariar o firmware REAL: estrutura e ferramenta de build, display/TFT e OLED, controladores e pins, bibliotecas e versoes, task(s), encoders, produtores dos dados de FM e Web, fontes de status/volume, limites de memoria. Consultar exemplos apenas se pertinente.
3. Ler `docs/ui_handoff/README.md`, `MAPA_DOS_INSUMOS.md`, `PLANO_DE_ACAO.md` e kit. O HTML e somente referencia visual.
4. Decidir backend grafico com fatos: preferir LVGL v9 se integravel sem romper o driver TFT ja funcional. Se nao for seguro, explicar e implementar uma PoC equivalente com biblioteca existente, protegendo o firmware. Nao atualizar bibliotecas fundamentais sem necessidade ou autorizacao.
5. Implementar o MENOR caminho possivel: produzir no TFT os MESMOS valores ja enviados ao OLED, sem duplicar queries/I2C nem obter telemetria nova. Exibir titulo/estado, FM (frequencia, sinal, RDS quando existente), Web (estacao, codec/bitrate/buffer/Wi-Fi quando existentes), volume. Campos indisponiveis = `-`.
6. Usar componente(s) de texto com fontes antialiasing preparadas offline, tema laranja e fundo preto. Nenhum blur em tempo real. Widgets persistentes, modificados apenas por mudanca de dados. Nao desenhar em callback de encoder, ISR ou task de streaming.
7. Isolar por opcao de compilacao recuperavel (exemplo `RADIO_UI_TFT_POC`) e preservar o caminho anterior. Evitar copias para `src/` de arquivos do kit sem verificar dependencias/API LVGL e adequacao ao projeto. Em ESP32-S3 N16R8, nao alocar GPIO26-37 como GPIO livre.
8. Compilar e, quando houver acesso real ao hardware, testar com streaming e FM em uso. Medir (nao presumir) responsividade, memoria e estabilidade; registrar observacoes e limitações.

## PROIBIDO NESTA ITERACAO
- Reescrever radios, rede, decodificacao, I2C, Nixies, DFPlayer, RTC, encoder, alarmes ou chimes.
- Construir todos os menus novos, trocar totalmente a UI OLED, adicionar animacoes ou efeitos visuais caros.
- Fazer consultas Web ao sintonizar/percorrer estacoes por giro sem confirmacao.
- Atribuir novos pinos ou APIs do driver por suposicao.
- Fazer migracao ampla de bibliotecas sem evidencias e aprovacoes tecnicas.

## Criterio de aceite
- Firmware anterior continua recuperavel e a funcionalidade de radio mantida.
- TFT mostra valores reais do firmware, com aparencia laranja e texto suavizado, sem depender de arquivos TTF em runtime.
- Atualizacoes parciais nao causam interrupcoes audiveis nem perda de encoder, conforme teste executado; anotar se teste fisico nao foi possivel.
- Relatar objetivamente os arquivos tocados, os resultados medidos e a proxima melhoria indispensavel.

**Pare apos cumprir esta tarefa e apresente o resultado. NAO avance para a interface final sem nova autorizacao.**
