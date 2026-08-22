# Código morto ou inócuo — revisão aplicada em 22/08/2026

## Alterações realizadas

1. `atualizarDisplayEstadoAudio()` agora confirma primeiro que o equipamento
   está em Rádio Web. No Player, não bloqueia mais o mutex nem copia
   desnecessariamente a fotografia `StatusAudio`, cujo tamanho esperado no
   ESP32 é de aproximadamente 440 bytes.

2. `supervisionarWifi()` guarda SSID e senha somente na transição para
   `WL_CONNECTED`. Foram preservadas as capturas necessárias durante a conexão
   inicial, antes de rejeitar um BSSID bloqueado e antes de desligar o Wi-Fi.
   As operações com `String` eram redundantes em cada passagem conectada, mas
   não havia evidência de duas novas alocações em todas as iterações.

3. Foi removida somente a atribuição de `alarmeObservouAudioAtivo` no ramo de
   Rádio Web. A variável permanece responsável por detectar o EOF e reiniciar
   MP3 ou WAV de alarme.

4. `calcularDuracaoBufferMs()` agora recebe a quantidade preenchida e o bitrate
   já lidos por `atualizarAmostraStatus()`. Isso mantém bytes e duração na mesma
   fotografia e elimina uma aquisição adicional do mutex interno realizada por
   `inBufferFilled()` a cada amostra de 250 ms. Na versão instalada da
   ESP32-audioI2S, `getBitRate()` apenas consulta campos e não adquire esse
   mutex.

5. `EstadoAudio::INICIALIZANDO` e seus ramos de apresentação foram removidos.
   No fluxo atual, o estado era substituído por `PARADO` ou `ERRO` ainda no
   `setup()` e não produzia comportamento específico na tarefa de áudio.

6. `mostrarConfiguracaoWifi()` agora retorna imediatamente quando o display não
   foi inicializado. Se `Adafruit_SSD1306::begin()` falhar ao alocar o buffer,
   ele permanece nulo; portanto, chamar `clearDisplay()` não era apenas uma
   divergência de padrão, mas um possível acesso inválido. A ausência física do
   OLED, por si só, não é confirmada pelo retorno de `begin()` dessa biblioteca.

## Falsos positivos descartados

- `/api/v1/status` e seus campos (consumidores externos)
- `estrelas` (usado por web/index.html)
- `titulo`, `bufferTotalBytes`
- Protótipos do `.ino`
- Funções estáticas internas dos .cpp
- Todas as constantes de `configuracao.h`
