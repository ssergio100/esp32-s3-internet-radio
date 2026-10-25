#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace tef6686 {

enum class Banda : uint8_t { FM, MW, SW, Quantidade };
enum class Direcao : int8_t { Abaixo = -1, Acima = 1 };
enum class EstadoOperacao : uint16_t {
    Boot = 0, Idle = 1, Standby = 2, FM = 3, AM = 4, Desconhecido = 0xFFFF
};
enum class Erro : uint8_t {
    Nenhum, NaoIniciado, ArgumentoInvalido, SemRespostaI2C, EscritaI2C,
    LeituraI2CCurta, Timeout, EstadoInvalido, IdentidadeIncompativel,
    Patch, EstacaoNaoEncontrada, NaoImplementado
};
// Retorno de processarBusca(): a busca avanca uma etapa por chamada.
enum class EstadoBusca : uint8_t {
    Nenhuma,          // Nenhuma busca ativa.
    EmAndamento,      // Uma etapa concluida; chame de novo no proximo loop.
    EstacaoEncontrada,// Candidato validado; audio ja liberado.
    NaoEncontrada,    // Volta completa sem candidato; frequencia restaurada.
    Falha             // Erro de I2C ou sintonia; frequencia restaurada.
};

// Estado interno da busca nao-bloqueante iniciada por iniciarBusca().
struct EstadoScanner {
    bool ativa = false;
    Direcao direcao = Direcao::Acima;
    Banda banda = Banda::FM;
    // Ponto de partida real do usuario: alvo de restauracao da volta completa.
    uint16_t frequenciaRestauracao = 0;
    bool muteInicial = true;
    uint16_t minimo = 0;
    uint16_t maximo = 0;
    uint16_t incremento = 0;
    bool gradeFm = false;
    // Ponteiro de sequencia: ultima frequencia testada, base do proximo passo.
    uint16_t atual = 0;
    uint16_t tentativas = 0;
    uint16_t maximoTentativas = 0;
    bool aguardandoQualidade = false;
    bool candidatoAprovado = false;
    uint32_t inicioJanelaQualidadeMs = 0;
};

struct FaixaSW {
    uint16_t metros = 0;
    uint16_t inicioKHz = 0;
    uint16_t fimKHz = 0;
};

// Limites e passos: FM usa unidades de 10 kHz; MW/SW usam kHz.
struct Configuracao {
    uint8_t enderecoI2C = 0x64;
    uint32_t clockHz = 9216000;
    bool clockExterno = false;
    uint16_t timeoutEstadoMs = 500;

    // Faixa brasileira estendida; os centros validos comecam em 76,1 MHz.
    uint16_t fmMin10kHz = 7600;
    uint16_t fmMax10kHz = 10800;
    uint16_t fmPasso10kHz = 10;

    // MW brasileira: centros em 530, 540, ..., 1700 kHz.
    uint16_t mwMinKHz = 530;
    uint16_t mwMaxKHz = 1700;
    uint16_t mwPassoKHz = 10;

    // SW continua. Faixas em metros sao atalhos; os intervalos continuam acessiveis.
    uint16_t swMinKHz = 1710;
    uint16_t swMaxKHz = 27000;
    uint16_t swPassoKHz = 5;

    // Criterios do scanner nas unidades nativas do TEF6686:
    // nivel = 0,1 dBuV; ruido/interferencia FM = 0,1%; offset = 0,1 kHz.
    uint16_t scannerTimeoutQualidadeMs = 120;
    // Grade brasileira de radiodifusao FM: 76,1; 76,3; ...; 107,9 MHz.
    // O passo manual de sintonia continua independente em fmPasso10kHz.
    uint16_t scannerFmPrimeiroCanal10kHz = 7610;
    uint16_t scannerFmPasso10kHz = 20;
    // A segunda leitura so e feita quando a primeira ja indicou uma estacao.
    uint8_t scannerFmConfirmacoes = 2;
    int16_t scannerFmNivelMinimo = 150;
    uint16_t scannerFmRuidoMax = 200;
    uint16_t scannerFmInterferenciaMax = 230;
    int16_t scannerFmOffsetMax = 80;
    int16_t scannerAmNivelMinimo = 200;
    uint16_t scannerAmRuidoMax = 1000;
    int16_t scannerAmOffsetMax = 50;
};

struct Identificacao {
    uint16_t dispositivo = 0;
    uint16_t hardware = 0;
    uint16_t software = 0;
    uint8_t tipo() const { return static_cast<uint8_t>(dispositivo >> 8); }
    uint8_t variante() const { return static_cast<uint8_t>(dispositivo); }
    bool correspondeAoTef6686() const { return tipo() == 9 && variante() == 14; }
};

struct Qualidade {
    uint16_t status = 0;
    int16_t nivelDecimosDbuv = 0;
    uint16_t ruido = 0;
    uint16_t interferencia = 0;
    int16_t offset = 0;
    uint16_t larguraBanda = 0;
    uint16_t modulacao = 0;
    uint16_t timestamp() const { return status & 0x03FF; }
};

struct Rds {
    uint16_t pi = 0;
    uint8_t pty = 0;
    // UTF-8: ate tres bytes por caractere do alfabeto RDS.
    char ps[25] = {};
    char texto[193] = {};
    bool ptyValido = false;
    bool psValido = false;
    bool textoValido = false;
    bool valido = false;
};

class RadioTef6686 {
public:
    Erro begin(TwoWire& wire, const Configuracao& configuracao = Configuracao());
    Erro probe();
    Erro identificar(Identificacao& identificacao);
    Erro lerEstado(EstadoOperacao& estado);

    Erro sintonizar(Banda banda, uint16_t frequencia);
    Erro passo(Direcao direcao);
    Erro proximaBanda();
    static const char* nomeBanda(Banda banda);

    // Subfaixas de radiodifusao dentro da banda SW.
    static const FaixaSW* faixasSW(size_t& quantidade);
    static bool obterFaixaSW(uint16_t metros, FaixaSW& faixa);
    static bool faixaSWDaFrequencia(uint16_t frequenciaKHz, FaixaSW& faixa);
    uint16_t metrosSWAtuais() const;
    Erro selecionarFaixaSW(uint16_t metros);
    Erro proximaFaixaSW(Direcao direcao);

    Erro definirVolumeDb(int16_t volumeDb);
    // Mute apenas de audio: continua sendo usado internamente pelo scanner.
    Erro definirMute(bool ativo);
    // Controle de repouso: true = mute + standby; false = sintonia + audio.
    // Confirma Standby ou FM/AM antes de retornar sucesso.
    Erro definirRepouso(bool ativo);

    Erro qualidadeDisponivel(bool& disponivel);
    Erro lerQualidade(Qualidade& qualidade);
    bool qualidadeCoerente(const Qualidade& qualidade) const;

    // FM: 0=automatico ou 56/64/72/84/97/114/133/151/168/184/
    // 200/217/236/254/287/311 kHz. MW/SW: 3/4/6/8 kHz.
    Erro definirLarguraBanda(uint16_t larguraKHz);
    static bool larguraBandaSuportada(Banda banda, uint16_t larguraKHz);
    static const uint16_t* largurasBandaSuportadas(Banda banda, size_t& quantidade);
    uint16_t larguraBandaConfiguradaKHz() const;

    // Stereo + StHiBlend do TEF6686. Nao usa FMSI do TEF6687/6689.
    Erro definirStereoAutomatico(bool automatico);
    Erro configurarRecepcaoRobusta();

    // Scanner nao-bloqueante: uma etapa por chamada de processarBusca().
    // Dentro de uma subfaixa SW, permanece nela; nos intervalos entre
    // subfaixas, percorre toda a SW continua.
    Erro iniciarBusca(Direcao direcao);
    EstadoBusca processarBusca();
    // Encerra a busca; restaura a frequencia de partida e o mute original.
    // restaurarFrequencia = false mantem a frequencia atual (para retomar
    // imediatamente em outra sintonia).
    void cancelarBusca(bool restaurarFrequencia);
    bool buscaAtiva() const { return scanner_.ativa; }

    // Versao bloqueante sobre o mesmo motor: roda processarBusca() ate o fim.
    Erro buscar(Direcao direcao);
    // Consome no maximo um grupo e devolve o ultimo conjunto estavel.
    Erro lerRds(Rds& rds);

    bool iniciado() const { return iniciado_; }
    Banda banda() const { return banda_; }
    uint16_t frequencia() const { return frequencia_; }
    int16_t volumeDb() const { return volumeDb_; }
    bool mute() const { return mute_; }
    const Configuracao& configuracao() const { return configuracao_; }
    Erro ultimoErro() const { return ultimoErro_; }
    static const char* textoErro(Erro erro);

private:
    static constexpr uint8_t MODULO_FM = 32;
    static constexpr uint8_t MODULO_AM = 33;
    static constexpr uint8_t MODULO_AUDIO = 48;
    static constexpr uint8_t MODULO_APLICACAO = 64;

    TwoWire* wire_ = nullptr;
    Configuracao configuracao_;
    bool iniciado_ = false;
    Banda banda_ = Banda::FM;
    uint16_t frequencia_ = 0;
    int16_t volumeDb_ = -30;
    bool mute_ = true;
    static constexpr size_t QUANTIDADE_BANDAS = static_cast<size_t>(Banda::Quantidade);
    uint16_t frequenciasSalvas_[QUANTIDADE_BANDAS] = {};
    static constexpr size_t QUANTIDADE_FAIXAS_SW = 15;
    uint16_t frequenciasFaixasSW_[QUANTIDADE_FAIXAS_SW] = {};
    uint16_t larguraFmKHz_ = 0;
    uint16_t larguraAmKHz_ = 4;
    Rds rds_;
    uint16_t piCandidato_ = 0;
    uint8_t repeticoesPi_ = 0;
    uint8_t leiturasSemSincronismo_ = 0;
    uint8_t ptyCandidato_ = 0;
    uint8_t repeticoesPty_ = 0;
    char psTrabalho_[9] = {};
    uint8_t mascaraPs_ = 0;
    char psCandidato_[9] = {};
    bool psCandidatoCompleto_ = false;
    char textoTrabalho_[65] = {};
    uint16_t mascaraTexto_ = 0;
    char textoCandidato_[65] = {};
    bool textoCandidatoCompleto_ = false;
    bool textoAb_ = false;
    bool textoAbConhecido_ = false;
    bool textoVersaoB_ = false;
    Erro ultimoErro_ = Erro::NaoIniciado;

    Erro inicializarAplicacao();
    Erro carregarPatchV102();
    Erro entrarEmIdle();
    Erro configurarClockEAtivar();
    Erro colocarEmStandby();
    Erro executarTabelaInicializacao();
    Erro configurarRds();
    Erro aguardarEstado(EstadoOperacao esperado);
    bool estacaoValidaParaBusca(const Qualidade& qualidade) const;
    uint16_t frequenciaMinima(Banda banda) const;
    uint16_t frequenciaMaxima(Banda banda) const;
    uint16_t passoBanda(Banda banda) const;
    uint16_t frequenciaPadrao(Banda banda) const;
    static int indiceFaixaSW(uint16_t metros);

    void reiniciarRds();
    void confirmarPiRds(uint16_t pi, uint8_t erro);
    void processarGrupoRds(uint16_t blocoA, uint16_t blocoB,
                           uint16_t blocoC, uint16_t blocoD,
                           uint16_t erros);
    void processarPsRds(uint16_t blocoB, uint16_t blocoD);
    void processarTextoRds(uint16_t blocoB, uint16_t blocoC,
                           uint16_t blocoD);

    EstadoScanner scanner_;
    // Encerra a busca ativa e devolve o estado final para o chamador.
    EstadoBusca finalizarBusca(EstadoBusca estado);
    // Calcula e avanca scanner_.atual para o proximo canal da grade.
    uint16_t proximaFrequenciaBusca();

    Erro escrever(const uint8_t* dados, size_t tamanho, uint16_t pausaMs = 2);
    Erro ler(uint8_t* dados, size_t tamanho);
    Erro enviarComando(uint8_t modulo, uint8_t comando,
                       const uint16_t* parametros, uint8_t quantidade);
    Erro obterResposta(uint8_t modulo, uint8_t comando,
                       uint8_t* resposta, size_t tamanho);
    Erro selecionarAreaPatch(uint16_t area);
    Erro enviarBlocoPatch(const unsigned char* dados, size_t tamanho);
    Erro falhar(Erro erro);
    Erro sucesso();
    static uint16_t lerBe16(const uint8_t* dados);
};

}  // namespace tef6686
