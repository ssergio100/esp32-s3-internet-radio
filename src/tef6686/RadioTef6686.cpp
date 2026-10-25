#include "RadioTef6686.h"
#include "Tuner_Patch_Lithio_V102_p224.h"
#include <string.h>
namespace tef6686 {
namespace {
constexpr uint8_t CMD_TUNE_TO = 1;
constexpr uint8_t CMD_SET_BANDWIDTH = 10;
constexpr uint8_t CMD_SET_MPH_SUPPRESSION = 20;
constexpr uint8_t CMD_SET_CHANNEL_EQUALIZER = 22;
constexpr uint8_t CMD_SET_NOISE_BLANKER = 23;
constexpr uint8_t CMD_SET_SOFTMUTE_TIME = 40;
constexpr uint8_t CMD_SET_SOFTMUTE_LEVEL = 42;
constexpr uint8_t CMD_SET_SOFTMUTE_NOISE = 43;
constexpr uint8_t CMD_SET_SOFTMUTE_MPH = 44;
constexpr uint8_t CMD_SET_SOFTMUTE_MAX = 45;
constexpr uint8_t CMD_SET_HIGHCUT_TIME = 50;
constexpr uint8_t CMD_SET_HIGHCUT_LEVEL = 52;
constexpr uint8_t CMD_SET_HIGHCUT_NOISE = 53;
constexpr uint8_t CMD_SET_HIGHCUT_MPH = 54;
constexpr uint8_t CMD_SET_HIGHCUT_MAX = 55;
constexpr uint8_t CMD_SET_HIGHCUT_MIN = 56;
constexpr uint8_t CMD_SET_STEREO_TIME = 60;
constexpr uint8_t CMD_SET_STEREO_LEVEL = 62;
constexpr uint8_t CMD_SET_STEREO_NOISE = 63;
constexpr uint8_t CMD_SET_STEREO_MPH = 64;
constexpr uint8_t CMD_SET_STEREO_MAX = 65;
constexpr uint8_t CMD_SET_STEREO_MIN = 66;
constexpr uint8_t CMD_SET_STHIBLEND_TIME = 70;
constexpr uint8_t CMD_SET_STHIBLEND_LEVEL = 72;
constexpr uint8_t CMD_SET_STHIBLEND_NOISE = 73;
constexpr uint8_t CMD_SET_STHIBLEND_MPH = 74;
constexpr uint8_t CMD_SET_STHIBLEND_MAX = 75;
constexpr uint8_t CMD_SET_STHIBLEND_MIN = 76;
constexpr uint8_t CMD_SET_RDS = 81;
constexpr uint8_t CMD_SET_VOLUME = 10;
constexpr uint8_t CMD_SET_MUTE = 11;
constexpr uint8_t CMD_SET_INPUT = 12;
constexpr uint8_t CMD_GET_QUALITY_STATUS = 128;
constexpr uint8_t CMD_GET_QUALITY_DATA = 129;
constexpr uint8_t CMD_GET_RDS_DATA = 131;
constexpr uint8_t CMD_SET_OPERATION_MODE = 1;
constexpr uint8_t CMD_GET_OPERATION_STATUS = 128;
constexpr uint8_t CMD_GET_IDENTIFICATION = 130;
const uint8_t TABELA_INICIALIZACAO[] PROGMEM = {
    17, 0x20,0x26,0x01, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
        0xFF,0xFC,0xFF,0xF8, 0x00,0x00,
    9,  0x20,0x36,0x01, 0x00,0x00, 0x01,0x68, 0x01,0x2C,
    7,  0x20,0x37,0x01, 0x00,0x00, 0x0F,0xA0,
    7,  0x20,0x39,0x01, 0x00,0x01, 0x00,0x01,
    7,  0x20,0x3A,0x01, 0x00,0x01, 0x00,0x01,
    11, 0x20,0x3C,0x01, 0x00,0x3C, 0x00,0x78, 0x00,0x64, 0x00,0xC8,
    11, 0x20,0x46,0x01, 0x01,0xF4, 0x07,0xD0, 0x00,0xC8, 0x00,0xC8,
    9,  0x20,0x48,0x01, 0x00,0x00, 0x02,0x58, 0x00,0xF0,
    9,  0x20,0x49,0x01, 0x00,0x00, 0x00,0xA0, 0x00,0x8C,
    9,  0x20,0x4A,0x01, 0x00,0x00, 0x00,0xA0, 0x00,0x8C,
    7,  0x20,0x4B,0x01, 0x00,0x00, 0x0F,0xA0,
    7,  0x30,0x15,0x01, 0x00,0x80, 0x00,0x01,
    13, 0x30,0x16,0x01, 0x00,0x21, 0x00,0x02, 0x00,0x10,
        0x01,0x00, 0x12,0xC0,
    7,  0x30,0x0D,0x01, 0x00,0x80, 0x00,0xE0,
    0
};
bool clockAceito(uint32_t clockHz) {
    return clockHz == 4000000 || clockHz == 9216000 ||
           clockHz == 12000000 || clockHz == 55000000;
}

constexpr uint16_t LARGURAS_FM[] = {
    56, 64, 72, 84, 97, 114, 133, 151,
    168, 184, 200, 217, 236, 254, 287, 311
};
constexpr uint16_t LARGURAS_AM[] = {3, 4, 6, 8};
constexpr FaixaSW FAIXAS_SW[] = {
    {160, 1800, 2000},
    {120, 2300, 2495},
    { 90, 3200, 3400},
    { 75, 3900, 4000},
    { 60, 4750, 4995},
    { 49, 5900, 6200},
    { 41, 7200, 7450},
    { 31, 9400, 9900},
    { 25, 11600, 12100},
    { 22, 13570, 13870},
    { 19, 15100, 15800},
    { 16, 17480, 17900},
    { 15, 18900, 19020},
    { 13, 21450, 21850},
    { 11, 25670, 26100},
};
constexpr size_t NUM_FAIXAS_SW = sizeof(FAIXAS_SW) / sizeof(FAIXAS_SW[0]);
static_assert(NUM_FAIXAS_SW == 15, "Tabela SW e armazenamento devem permanecer sincronizados");
bool contemLargura(const uint16_t* valores, size_t quantidade, uint16_t larguraKHz) {
    for (size_t i = 0; i < quantidade; ++i) {
        if (valores[i] == larguraKHz) return true;
    }
    return false;
}

void preencherEspacos(char* texto, size_t quantidade) {
    memset(texto, ' ', quantidade);
    texto[quantidade] = '\0';
}

// Copia ate o terminador RDS (0x0D) e remove os espacos de preenchimento.
void finalizarTextoRds(const char* origem, size_t tamanho, char* destino) {
    size_t fim = 0;
    while (fim < tamanho && static_cast<uint8_t>(origem[fim]) != 0x0D) ++fim;
    while (fim > 0 && (origem[fim - 1] == ' ' || origem[fim - 1] == '\0' ||
                       static_cast<uint8_t>(origem[fim - 1]) == 0xFF)) {
        --fim;
    }
    memcpy(destino, origem, fim);
    destino[fim] = '\0';
}

uint16_t mascaraAteSegmento(uint8_t segmento) {
    return segmento == 15
        ? 0xFFFF
        : static_cast<uint16_t>((1UL << (segmento + 1)) - 1);
}

char normalizarCaractereRds(uint8_t caractere) {
    if (caractere == 0x0D) return static_cast<char>(caractere);
    return caractere < 0x20 || caractere == 0x7F
        ? ' ' : static_cast<char>(caractere);
}

const uint16_t CARACTERES_RDS_80_FF[] PROGMEM = {
    0x00E1, 0x00E0, 0x00E9, 0x00E8, 0x00ED, 0x00EC, 0x00F3, 0x00F2,
    0x00FA, 0x00F9, 0x00D1, 0x00C7, 0x015E, 0x03B2, 0x00A1, 0x0132,
    0x00E2, 0x00E4, 0x00EA, 0x00EB, 0x00EE, 0x00EF, 0x00F4, 0x00F6,
    0x00FB, 0x00FC, 0x00F1, 0x00E7, 0x015F, 0x01E7, 0x0131, 0x0133,
    0x00AA, 0x03B1, 0x00A9, 0x2030, 0x01E6, 0x011B, 0x0148, 0x0151,
    0x03C0, 0x20AC, 0x00A3, 0x0024, 0x2190, 0x2191, 0x2192, 0x2193,
    0x00BA, 0x00B9, 0x00B2, 0x00B3, 0x00B1, 0x0130, 0x0144, 0x0171,
    0x00B5, 0x00BF, 0x00F7, 0x00B0, 0x00BC, 0x00BD, 0x00BE, 0x00A7,
    0x00C1, 0x00C0, 0x00C9, 0x00C8, 0x00CD, 0x00CC, 0x00D3, 0x00D2,
    0x00DA, 0x00D9, 0x0158, 0x010C, 0x0160, 0x017D, 0x00D0, 0x013F,
    0x00C2, 0x00C4, 0x00CA, 0x00CB, 0x00CE, 0x00CF, 0x00D4, 0x00D6,
    0x00DB, 0x00DC, 0x0159, 0x010D, 0x0161, 0x017E, 0x0111, 0x0140,
    0x00C3, 0x00C5, 0x00C6, 0x0152, 0x0177, 0x00DD, 0x00D5, 0x00D8,
    0x00DE, 0x014A, 0x0154, 0x0106, 0x015A, 0x0179, 0x0166, 0x00F0,
    0x00E3, 0x00E5, 0x00E6, 0x0153, 0x0175, 0x00FD, 0x00F5, 0x00F8,
    0x00FE, 0x014B, 0x0155, 0x0107, 0x015B, 0x017A, 0x0167, 0x0020,
};

uint16_t unicodeDoCaractereRds(uint8_t caractere) {
    if (caractere == 0x5E) return 0x2015;
    if (caractere == 0x7E) return 0x00AF;
    if (caractere >= 0x80) {
        return pgm_read_word(CARACTERES_RDS_80_FF + caractere - 0x80);
    }
    return caractere;
}

size_t escreverUtf8(uint16_t codigo, char* destino, size_t capacidade) {
    if (codigo < 0x80) {
        if (capacidade < 1) return 0;
        destino[0] = static_cast<char>(codigo);
        return 1;
    }
    if (codigo < 0x800) {
        if (capacidade < 2) return 0;
        destino[0] = static_cast<char>(0xC0 | (codigo >> 6));
        destino[1] = static_cast<char>(0x80 | (codigo & 0x3F));
        return 2;
    }
    if (capacidade < 3) return 0;
    destino[0] = static_cast<char>(0xE0 | (codigo >> 12));
    destino[1] = static_cast<char>(0x80 | ((codigo >> 6) & 0x3F));
    destino[2] = static_cast<char>(0x80 | (codigo & 0x3F));
    return 3;
}

void converterTextoRdsParaUtf8(
    const char* origem,
    char* destino,
    size_t capacidade
) {
    if (capacidade == 0) return;
    size_t saida = 0;
    for (size_t i = 0; origem[i] != '\0'; ++i) {
        const uint16_t codigo = unicodeDoCaractereRds(
            static_cast<uint8_t>(origem[i])
        );
        const size_t escritos = escreverUtf8(
            codigo,
            destino + saida,
            capacidade - saida - 1
        );
        if (escritos == 0) break;
        saida += escritos;
    }
    destino[saida] = '\0';
}

}  // namespace
Erro RadioTef6686::begin(TwoWire& wire, const Configuracao& configuracao) {
    wire_ = &wire;
    configuracao_ = configuracao;
    iniciado_ = false;
    frequencia_ = 0;
    mute_ = true;
    volumeDb_ = -30;
    larguraFmKHz_ = 0;
    larguraAmKHz_ = 4;
    reiniciarRds();
    for (size_t i = 0; i < QUANTIDADE_BANDAS; ++i) frequenciasSalvas_[i] = 0;
    for (size_t i = 0; i < NUM_FAIXAS_SW; ++i) frequenciasFaixasSW_[i] = 0;
    if (!clockAceito(configuracao_.clockHz) ||
        configuracao_.fmMin10kHz >= configuracao_.fmMax10kHz ||
        configuracao_.fmPasso10kHz == 0 ||
        configuracao_.fmPasso10kHz >
            configuracao_.fmMax10kHz - configuracao_.fmMin10kHz ||
        configuracao_.mwMinKHz >= configuracao_.mwMaxKHz ||
        configuracao_.mwPassoKHz == 0 ||
        configuracao_.mwPassoKHz >
            configuracao_.mwMaxKHz - configuracao_.mwMinKHz ||
        configuracao_.swMinKHz >= configuracao_.swMaxKHz ||
        configuracao_.swPassoKHz == 0 ||
        configuracao_.swPassoKHz > configuracao_.swMaxKHz - configuracao_.swMinKHz ||
        configuracao_.timeoutEstadoMs == 0 ||
        configuracao_.scannerTimeoutQualidadeMs == 0 ||
        configuracao_.scannerFmPrimeiroCanal10kHz < configuracao_.fmMin10kHz ||
        configuracao_.scannerFmPrimeiroCanal10kHz > configuracao_.fmMax10kHz ||
        configuracao_.scannerFmPasso10kHz == 0 ||
        configuracao_.scannerFmPasso10kHz >
            configuracao_.fmMax10kHz - configuracao_.fmMin10kHz ||
        configuracao_.scannerFmConfirmacoes == 0) {
        return falhar(Erro::ArgumentoInvalido);
    }
    Erro erro = probe();
    if (erro != Erro::Nenhum) return erro;
    erro = inicializarAplicacao();
    if (erro != Erro::Nenhum) return erro;
    Identificacao id;
    erro = identificar(id);
    if (erro != Erro::Nenhum) return erro;
    if (!id.correspondeAoTef6686()) {
        return falhar(Erro::IdentidadeIncompativel);
    }
    const uint16_t entradaRadio = 0;
    erro = enviarComando(MODULO_AUDIO, CMD_SET_INPUT, &entradaRadio, 1);
    if (erro != Erro::Nenhum) return erro;
    iniciado_ = true;
    erro = definirVolumeDb(volumeDb_);
    if (erro != Erro::Nenhum) {
        iniciado_ = false;
        return erro;
    }
    erro = definirMute(true);
    if (erro != Erro::Nenhum) {
        iniciado_ = false;
        return erro;
    }
    // O primeiro modelo ja nasce com o processamento de recepcao real ativado.
    // Isso sobrescreve entradas da tabela de inicializacao que deixam alguns
    // controles adaptativos desabilitados para avaliacao.
    erro = configurarRecepcaoRobusta();
    if (erro != Erro::Nenhum) {
        iniciado_ = false;
        return erro;
    }
    erro = configurarRds();
    if (erro != Erro::Nenhum) {
        iniciado_ = false;
        return erro;
    }
    return sucesso();
}

Erro RadioTef6686::probe() {
    if (wire_ == nullptr) return falhar(Erro::NaoIniciado);
    wire_->beginTransmission(configuracao_.enderecoI2C);
    if (wire_->endTransmission(true) != 0) {
        return falhar(Erro::SemRespostaI2C);
    }
    return sucesso();
}

Erro RadioTef6686::identificar(Identificacao& identificacao) {
    if (wire_ == nullptr) return falhar(Erro::NaoIniciado);
    uint8_t resposta[6];
    Erro erro = obterResposta(
        MODULO_APLICACAO,
        CMD_GET_IDENTIFICATION,
        resposta,
        sizeof(resposta)
    );
    if (erro != Erro::Nenhum) return erro;
    identificacao.dispositivo = lerBe16(resposta);
    identificacao.hardware = lerBe16(resposta + 2);
    identificacao.software = lerBe16(resposta + 4);
    return sucesso();
}

Erro RadioTef6686::lerEstado(EstadoOperacao& estado) {
    if (wire_ == nullptr) return falhar(Erro::NaoIniciado);
    uint8_t resposta[2];
    Erro erro = obterResposta(
        MODULO_APLICACAO,
        CMD_GET_OPERATION_STATUS,
        resposta,
        sizeof(resposta)
    );
    if (erro != Erro::Nenhum) return erro;
    const uint16_t bruto = lerBe16(resposta);
    if (bruto > static_cast<uint16_t>(EstadoOperacao::AM)) {
        estado = EstadoOperacao::Desconhecido;
        return falhar(Erro::EstadoInvalido);
    }
    estado = static_cast<EstadoOperacao>(bruto);
    return sucesso();
}

const char* RadioTef6686::nomeBanda(Banda banda) {
    switch (banda) {
        case Banda::FM: return "FM";
        case Banda::MW: return "MW";
        case Banda::SW: return "SW";
        case Banda::Quantidade: break;
    }
    return "?";
}

const FaixaSW* RadioTef6686::faixasSW(size_t& quantidade) {
    quantidade = NUM_FAIXAS_SW;
    return FAIXAS_SW;
}

int RadioTef6686::indiceFaixaSW(uint16_t metros) {
    for (size_t i = 0; i < NUM_FAIXAS_SW; ++i) {
        if (FAIXAS_SW[i].metros == metros) return static_cast<int>(i);
    }
    return -1;
}

bool RadioTef6686::obterFaixaSW(uint16_t metros, FaixaSW& faixa) {
    const int indice = indiceFaixaSW(metros);
    if (indice < 0) return false;
    faixa = FAIXAS_SW[indice];
    return true;
}

bool RadioTef6686::faixaSWDaFrequencia(uint16_t frequenciaKHz, FaixaSW& faixa) {
    for (size_t i = 0; i < NUM_FAIXAS_SW; ++i) {
        if (frequenciaKHz >= FAIXAS_SW[i].inicioKHz &&
            frequenciaKHz <= FAIXAS_SW[i].fimKHz) {
            faixa = FAIXAS_SW[i];
            return true;
        }
    }
    return false;
}

uint16_t RadioTef6686::metrosSWAtuais() const {
    if (banda_ != Banda::SW) return 0;
    FaixaSW faixa;
    return faixaSWDaFrequencia(frequencia_, faixa) ? faixa.metros : 0;
}

Erro RadioTef6686::selecionarFaixaSW(uint16_t metros) {
    if (!iniciado_) return falhar(Erro::NaoIniciado);
    const int indice = indiceFaixaSW(metros);
    if (indice < 0) return falhar(Erro::ArgumentoInvalido);
    uint16_t frequencia = frequenciasFaixasSW_[indice];
    if (frequencia < FAIXAS_SW[indice].inicioKHz || frequencia > FAIXAS_SW[indice].fimKHz) {
        frequencia = FAIXAS_SW[indice].inicioKHz;
    }
    return sintonizar(Banda::SW, frequencia);
}

Erro RadioTef6686::proximaFaixaSW(Direcao direcao) {
    if (!iniciado_) return falhar(Erro::NaoIniciado);
    FaixaSW atual;
    int indice = faixaSWDaFrequencia(frequencia_, atual) ? indiceFaixaSW(atual.metros) : -1;
    if (direcao == Direcao::Acima) {
        indice = (indice < 0 || indice + 1 >= static_cast<int>(NUM_FAIXAS_SW)) ? 0 : indice + 1;
    } else {
        indice = (indice <= 0) ? static_cast<int>(NUM_FAIXAS_SW) - 1 : indice - 1;
    }
    return selecionarFaixaSW(FAIXAS_SW[indice].metros);
}

uint16_t RadioTef6686::frequenciaMinima(Banda banda) const {
    if (banda == Banda::FM) return configuracao_.fmMin10kHz;
    if (banda == Banda::MW) return configuracao_.mwMinKHz;
    if (banda == Banda::SW) return configuracao_.swMinKHz;
    return 0;
}

uint16_t RadioTef6686::frequenciaMaxima(Banda banda) const {
    if (banda == Banda::FM) return configuracao_.fmMax10kHz;
    if (banda == Banda::MW) return configuracao_.mwMaxKHz;
    if (banda == Banda::SW) return configuracao_.swMaxKHz;
    return 0;
}

uint16_t RadioTef6686::passoBanda(Banda banda) const {
    if (banda == Banda::FM) return configuracao_.fmPasso10kHz;
    if (banda == Banda::MW) return configuracao_.mwPassoKHz;
    if (banda == Banda::SW) return configuracao_.swPassoKHz;
    return 0;
}

uint16_t RadioTef6686::frequenciaPadrao(Banda banda) const {
    if (banda == Banda::FM) {
        const uint16_t preferida = 10000;
        return preferida >= configuracao_.fmMin10kHz &&
               preferida <= configuracao_.fmMax10kHz
            ? preferida : configuracao_.fmMin10kHz;
    }
    if (banda == Banda::MW) {
        const uint16_t preferida = 1000;
        return preferida >= configuracao_.mwMinKHz &&
               preferida <= configuracao_.mwMaxKHz
            ? preferida : configuracao_.mwMinKHz;
    }
    if (banda == Banda::SW) {
        // 49 m e um ponto inicial pratico para radiodifusao em SW.
        const uint16_t preferida = 6000;
        return preferida >= configuracao_.swMinKHz &&
               preferida <= configuracao_.swMaxKHz
            ? preferida : configuracao_.swMinKHz;
    }
    return 0;
}

Erro RadioTef6686::sintonizar(Banda banda, uint16_t frequencia) {
    if (!iniciado_) return falhar(Erro::NaoIniciado);
    if (banda == Banda::Quantidade) return falhar(Erro::ArgumentoInvalido);
    const uint16_t minimo = frequenciaMinima(banda);
    const uint16_t maximo = frequenciaMaxima(banda);
    if (minimo == 0 || frequencia < minimo || frequencia > maximo) {
        return falhar(Erro::ArgumentoInvalido);
    }
    const bool fm = banda == Banda::FM;
    const uint16_t parametros[] = {1, frequencia};  // Tune mode Preset.
    Erro erro = enviarComando(
        fm ? MODULO_FM : MODULO_AM,
        CMD_TUNE_TO,
        parametros,
        2
    );
    if (erro != Erro::Nenhum) return erro;
    erro = aguardarEstado(fm ? EstadoOperacao::FM : EstadoOperacao::AM);
    if (erro != Erro::Nenhum) return erro;
    banda_ = banda;
    frequencia_ = frequencia;
    frequenciasSalvas_[static_cast<size_t>(banda)] = frequencia;
    if (banda == Banda::SW) {
        FaixaSW faixa;
        if (faixaSWDaFrequencia(frequencia, faixa)) {
            const int indice = indiceFaixaSW(faixa.metros);
            if (indice >= 0) frequenciasFaixasSW_[indice] = frequencia;
        }
    }
    reiniciarRds();
    return sucesso();
}

Erro RadioTef6686::passo(Direcao direcao) {
    if (!iniciado_ || frequencia_ == 0) return falhar(Erro::NaoIniciado);
    const uint16_t minimo = frequenciaMinima(banda_);
    const uint16_t maximo = frequenciaMaxima(banda_);
    const uint16_t incremento = passoBanda(banda_);
    if (minimo == 0 || incremento == 0) return falhar(Erro::ArgumentoInvalido);
    uint16_t nova = frequencia_;
    if (direcao == Direcao::Acima) {
        nova = (frequencia_ > maximo - incremento)
            ? minimo
            : static_cast<uint16_t>(frequencia_ + incremento);
    } else {
        nova = (frequencia_ < minimo + incremento)
            ? maximo
            : static_cast<uint16_t>(frequencia_ - incremento);
    }
    return sintonizar(banda_, nova);
}

Erro RadioTef6686::proximaBanda() {
    if (!iniciado_) return falhar(Erro::NaoIniciado);
    size_t proxima = static_cast<size_t>(banda_) + 1;
    if (proxima >= QUANTIDADE_BANDAS) proxima = 0;
    const Banda nova = static_cast<Banda>(proxima);
    uint16_t frequencia = frequenciasSalvas_[proxima];
    if (frequencia == 0) frequencia = frequenciaPadrao(nova);
    return sintonizar(nova, frequencia);
}

Erro RadioTef6686::definirVolumeDb(int16_t volumeDb) {
    if (!iniciado_) return falhar(Erro::NaoIniciado);
    if (volumeDb < -60 || volumeDb > 0) {
        return falhar(Erro::ArgumentoInvalido);
    }
    const int16_t decimosDb = static_cast<int16_t>(volumeDb * 10);
    const uint16_t parametro = static_cast<uint16_t>(decimosDb);
    Erro erro = enviarComando(MODULO_AUDIO, CMD_SET_VOLUME, &parametro, 1);
    if (erro != Erro::Nenhum) return erro;
    volumeDb_ = volumeDb;
    return sucesso();
}

Erro RadioTef6686::definirMute(bool ativo) {
    if (!iniciado_) return falhar(Erro::NaoIniciado);
    const uint16_t parametro = ativo ? 1 : 0;
    Erro erro = enviarComando(MODULO_AUDIO, CMD_SET_MUTE, &parametro, 1);
    if (erro != Erro::Nenhum) return erro;
    mute_ = ativo;
    return sucesso();
}

// API de repouso: aproveita o mute, a sintonia e o standby ja existentes.
// O scanner continua usando definirMute(), que silencia somente o audio.
Erro RadioTef6686::definirRepouso(bool ativo) {
    if (!iniciado_) return falhar(Erro::NaoIniciado);

    // Nas duas direcoes, comeca com a saida de audio silenciada.
    Erro erro = definirMute(true);
    if (erro != Erro::Nenhum) return erro;

    if (ativo) return colocarEmStandby();

    // Tune_To acorda o receptor. sintonizar() ja espera FM/AM ativo.
    const uint16_t frequenciaRetomada = frequencia_ == 0
        ? frequenciaPadrao(banda_) : frequencia_;
    erro = sintonizar(banda_, frequenciaRetomada);
    if (erro != Erro::Nenhum) {
        // Se falhar a retomada, mantem mute e tenta voltar a standby.
        // Preserva o erro da retomada, mesmo se o standby tambem falhar.
        colocarEmStandby();
        return falhar(erro);
    }

    return definirMute(false);
}


Erro RadioTef6686::qualidadeDisponivel(bool& disponivel) {
    if (!iniciado_) return falhar(Erro::NaoIniciado);
    uint8_t resposta[2];
    Erro erro = obterResposta(
        banda_ == Banda::FM ? MODULO_FM : MODULO_AM,
        CMD_GET_QUALITY_STATUS,
        resposta,
        sizeof(resposta)
    );
    if (erro != Erro::Nenhum) return erro;
    const uint16_t status = lerBe16(resposta);
    const bool reservadosZerados = (status & 0x7C00) == 0;
    disponivel = reservadosZerados && (status & 0x03FF) == 1000;
    return sucesso();
}

Erro RadioTef6686::lerQualidade(Qualidade& qualidade) {
    if (!iniciado_) return falhar(Erro::NaoIniciado);
    uint8_t resposta[14];
    Erro erro = obterResposta(
        banda_ == Banda::FM ? MODULO_FM : MODULO_AM,
        CMD_GET_QUALITY_DATA,
        resposta,
        sizeof(resposta)
    );
    if (erro != Erro::Nenhum) return erro;
    qualidade.status = lerBe16(resposta);
    qualidade.nivelDecimosDbuv = static_cast<int16_t>(lerBe16(resposta + 2));
    qualidade.ruido = lerBe16(resposta + 4);
    qualidade.interferencia = lerBe16(resposta + 6);
    qualidade.offset = static_cast<int16_t>(lerBe16(resposta + 8));
    qualidade.larguraBanda = lerBe16(resposta + 10);
    qualidade.modulacao = lerBe16(resposta + 12);
    return sucesso();
}

bool RadioTef6686::qualidadeCoerente(const Qualidade& q) const {
    const bool statusValido = (q.status & 0x7C00) == 0 &&
                              q.timestamp() >= 1 && q.timestamp() <= 1000;
    const bool comuns = statusValido &&
                        q.nivelDecimosDbuv >= -200 && q.nivelDecimosDbuv <= 1200 &&
                        q.offset >= -1200 && q.offset <= 1200 &&
                        q.modulacao <= 2000;
    if (!comuns) return false;
    if (banda_ == Banda::FM) {
        return q.ruido <= 1000 && q.interferencia <= 1000 &&
               q.larguraBanda >= 560 && q.larguraBanda <= 3110;
    }
    return q.ruido <= 50000 && q.interferencia <= 1 &&
           q.larguraBanda >= 30 && q.larguraBanda <= 80;
}

bool RadioTef6686::larguraBandaSuportada(Banda banda, uint16_t larguraKHz) {
    if (banda == Banda::FM) {
        if (larguraKHz == 0) return true;
        return contemLargura(
            LARGURAS_FM,
            sizeof(LARGURAS_FM) / sizeof(LARGURAS_FM[0]),
            larguraKHz
        );
    }
    return contemLargura(
        LARGURAS_AM,
        sizeof(LARGURAS_AM) / sizeof(LARGURAS_AM[0]),
        larguraKHz
    );
}

const uint16_t* RadioTef6686::largurasBandaSuportadas(Banda banda, size_t& quantidade) {
    if (banda == Banda::FM) {
        quantidade = sizeof(LARGURAS_FM) / sizeof(LARGURAS_FM[0]);
        return LARGURAS_FM;
    }
    quantidade = sizeof(LARGURAS_AM) / sizeof(LARGURAS_AM[0]);
    return LARGURAS_AM;
}

uint16_t RadioTef6686::larguraBandaConfiguradaKHz() const {
    return banda_ == Banda::FM ? larguraFmKHz_ : larguraAmKHz_;
}

Erro RadioTef6686::definirLarguraBanda(uint16_t larguraKHz) {
    if (!iniciado_) return falhar(Erro::NaoIniciado);
    if (!larguraBandaSuportada(banda_, larguraKHz)) {
        return falhar(Erro::ArgumentoInvalido);
    }
    Erro erro;
    if (banda_ == Banda::FM) {
        if (larguraKHz == 0) {
            const uint16_t parametros[] = {1, 2360, 1000, 1000};
            erro = enviarComando(MODULO_FM, CMD_SET_BANDWIDTH, parametros, 4);
        } else {
            const uint16_t parametros[] = {
                0, static_cast<uint16_t>(larguraKHz * 10), 1000, 1000
            };
            erro = enviarComando(MODULO_FM, CMD_SET_BANDWIDTH, parametros, 4);
        }
        if (erro != Erro::Nenhum) return erro;
        larguraFmKHz_ = larguraKHz;
        return sucesso();
    }
    const uint16_t parametros[] = {0, static_cast<uint16_t>(larguraKHz * 10)};
    erro = enviarComando(MODULO_AM, CMD_SET_BANDWIDTH, parametros, 2);
    if (erro != Erro::Nenhum) return erro;
    larguraAmKHz_ = larguraKHz;
    return sucesso();
}

Erro RadioTef6686::definirStereoAutomatico(bool automatico) {
    if (!iniciado_) return falhar(Erro::NaoIniciado);
    Erro erro;
    if (!automatico) {
        // Desabilita apenas o tratamento dinamico de stereo. Nao forca mono.
        const uint16_t off3[] = {0, 0, 100};
        erro = enviarComando(MODULO_FM, CMD_SET_STEREO_LEVEL, off3, 3);
        if (erro != Erro::Nenhum) return erro;
        erro = enviarComando(MODULO_FM, CMD_SET_STEREO_NOISE, off3, 3);
        if (erro != Erro::Nenhum) return erro;
        erro = enviarComando(MODULO_FM, CMD_SET_STEREO_MPH, off3, 3);
        if (erro != Erro::Nenhum) return erro;
        const uint16_t stereoMaxOff[] = {0};
        erro = enviarComando(MODULO_FM, CMD_SET_STEREO_MAX, stereoMaxOff, 1);
        if (erro != Erro::Nenhum) return erro;
        erro = enviarComando(MODULO_FM, CMD_SET_STHIBLEND_LEVEL, off3, 3);
        if (erro != Erro::Nenhum) return erro;
        erro = enviarComando(MODULO_FM, CMD_SET_STHIBLEND_NOISE, off3, 3);
        if (erro != Erro::Nenhum) return erro;
        erro = enviarComando(MODULO_FM, CMD_SET_STHIBLEND_MPH, off3, 3);
        if (erro != Erro::Nenhum) return erro;
        const uint16_t hiBlendMaxOff[] = {0, 4000};
        erro = enviarComando(MODULO_FM, CMD_SET_STHIBLEND_MAX, hiBlendMaxOff, 2);
        if (erro != Erro::Nenhum) return erro;
        return sucesso();
    }
    // TEF6686: tratamento padrao Stereo + Stereo High Blend.
    // Nao usar CMD 32/FMSI: esse recurso pertence ao TEF6687/6689.
    const uint16_t stereoTime[] = {200, 4000, 20, 80};
    erro = enviarComando(MODULO_FM, CMD_SET_STEREO_TIME, stereoTime, 4);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t stereoLevel[] = {3, 460, 240};
    erro = enviarComando(MODULO_FM, CMD_SET_STEREO_LEVEL, stereoLevel, 3);
    if (erro != Erro::Nenhum) return erro;
    // Sensibilidades recomendadas no manual para melhor desempenho em campo.
    const uint16_t stereoNoise[] = {3, 120, 160};
    erro = enviarComando(MODULO_FM, CMD_SET_STEREO_NOISE, stereoNoise, 3);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t stereoMph[] = {3, 100, 150};
    erro = enviarComando(MODULO_FM, CMD_SET_STEREO_MPH, stereoMph, 3);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t stereoMax[] = {1};
    erro = enviarComando(MODULO_FM, CMD_SET_STEREO_MAX, stereoMax, 1);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t stereoMin[] = {0, 400};
    erro = enviarComando(MODULO_FM, CMD_SET_STEREO_MIN, stereoMin, 2);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t hiBlendTime[] = {500, 2000, 20, 20};
    erro = enviarComando(MODULO_FM, CMD_SET_STHIBLEND_TIME, hiBlendTime, 4);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t hiBlendLevel[] = {3, 600, 240};
    erro = enviarComando(MODULO_FM, CMD_SET_STHIBLEND_LEVEL, hiBlendLevel, 3);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t hiBlendNoise[] = {3, 80, 140};
    erro = enviarComando(MODULO_FM, CMD_SET_STHIBLEND_NOISE, hiBlendNoise, 3);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t hiBlendMph[] = {3, 80, 140};
    erro = enviarComando(MODULO_FM, CMD_SET_STHIBLEND_MPH, hiBlendMph, 3);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t hiBlendMax[] = {1, 4000};
    erro = enviarComando(MODULO_FM, CMD_SET_STHIBLEND_MAX, hiBlendMax, 2);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t hiBlendMin[] = {0, 7000};
    erro = enviarComando(MODULO_FM, CMD_SET_STHIBLEND_MIN, hiBlendMin, 2);
    if (erro != Erro::Nenhum) return erro;
    return sucesso();
}

Erro RadioTef6686::configurarRecepcaoRobusta() {
    if (!iniciado_) return falhar(Erro::NaoIniciado);
    Erro erro;
    // Recursos que o manual recomenda ligados para melhor desempenho em campo.
    const uint16_t ligado[] = {1};
    erro = enviarComando(MODULO_FM, CMD_SET_MPH_SUPPRESSION, ligado, 1);
    if (erro != Erro::Nenhum) return erro;
    erro = enviarComando(MODULO_FM, CMD_SET_CHANNEL_EQUALIZER, ligado, 1);
    if (erro != Erro::Nenhum) return erro;
    // Noise blanker FM/AM nos parametros padrao documentados.
    const uint16_t noiseBlanker[] = {1, 1000};
    erro = enviarComando(MODULO_FM, CMD_SET_NOISE_BLANKER, noiseBlanker, 2);
    if (erro != Erro::Nenhum) return erro;
    erro = enviarComando(MODULO_AM, CMD_SET_NOISE_BLANKER, noiseBlanker, 2);
    if (erro != Erro::Nenhum) return erro;
    // Soft-mute FM: reduz ruido de forma progressiva em sinal fraco/ruidoso.
    const uint16_t softmuteTime[] = {120, 500, 10, 20};
    erro = enviarComando(MODULO_FM, CMD_SET_SOFTMUTE_TIME, softmuteTime, 4);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t softmuteLevel[] = {2, 150, 220};
    erro = enviarComando(MODULO_FM, CMD_SET_SOFTMUTE_LEVEL, softmuteLevel, 3);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t softmuteNoise[] = {3, 200, 1000};
    erro = enviarComando(MODULO_FM, CMD_SET_SOFTMUTE_NOISE, softmuteNoise, 3);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t softmuteMph[] = {3, 200, 1000};
    erro = enviarComando(MODULO_FM, CMD_SET_SOFTMUTE_MPH, softmuteMph, 3);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t softmuteMaxFM[] = {1, 200};
    erro = enviarComando(MODULO_FM, CMD_SET_SOFTMUTE_MAX, softmuteMaxFM, 2);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t softmuteMaxAM[] = {1, 250};
    erro = enviarComando(MODULO_AM, CMD_SET_SOFTMUTE_MAX, softmuteMaxAM, 2);
    if (erro != Erro::Nenhum) return erro;
    // High-cut adaptativo FM: reduz agudos onde o chiado e o multipath incomodam.
    const uint16_t highcutTime[] = {200, 2000, 10, 80};
    erro = enviarComando(MODULO_FM, CMD_SET_HIGHCUT_TIME, highcutTime, 4);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t highcutLevel[] = {3, 360, 300};
    erro = enviarComando(MODULO_FM, CMD_SET_HIGHCUT_LEVEL, highcutLevel, 3);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t highcutNoise[] = {3, 150, 200};
    erro = enviarComando(MODULO_FM, CMD_SET_HIGHCUT_NOISE, highcutNoise, 3);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t highcutMph[] = {3, 120, 160};
    erro = enviarComando(MODULO_FM, CMD_SET_HIGHCUT_MPH, highcutMph, 3);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t highcutMax[] = {1, 2400};
    erro = enviarComando(MODULO_FM, CMD_SET_HIGHCUT_MAX, highcutMax, 2);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t highcutMin[] = {0, 10000};
    erro = enviarComando(MODULO_FM, CMD_SET_HIGHCUT_MIN, highcutMin, 2);
    if (erro != Erro::Nenhum) return erro;
    erro = definirStereoAutomatico(true);
    if (erro != Erro::Nenhum) return erro;
    // FM em largura automatica por padrao; AM conserva 4 kHz.
    const uint16_t larguraFMAuto[] = {1, 2360, 1000, 1000};
    erro = enviarComando(MODULO_FM, CMD_SET_BANDWIDTH, larguraFMAuto, 4);
    if (erro != Erro::Nenhum) return erro;
    const uint16_t larguraAM[] = {0, 40};
    erro = enviarComando(MODULO_AM, CMD_SET_BANDWIDTH, larguraAM, 2);
    if (erro != Erro::Nenhum) return erro;
    larguraFmKHz_ = 0;
    larguraAmKHz_ = 4;
    return sucesso();
}

Erro RadioTef6686::configurarRds() {
    if (!iniciado_) return falhar(Erro::NaoIniciado);
    // Decoder de grupos, reinicio automatico apos sintonia e sem pino DAVN.
    const uint16_t parametros[] = {1, 2, 0};
    reiniciarRds();
    return enviarComando(MODULO_FM, CMD_SET_RDS, parametros, 3);
}

bool RadioTef6686::estacaoValidaParaBusca(const Qualidade& q) const {
    if (!qualidadeCoerente(q)) return false;
    const int16_t offsetAbs = q.offset < 0 ? -q.offset : q.offset;
    if (banda_ == Banda::FM) {
        return q.nivelDecimosDbuv >= configuracao_.scannerFmNivelMinimo &&
               q.ruido <= configuracao_.scannerFmRuidoMax &&
               q.interferencia <= configuracao_.scannerFmInterferenciaMax &&
               offsetAbs <= configuracao_.scannerFmOffsetMax;
    }
    return q.nivelDecimosDbuv >= configuracao_.scannerAmNivelMinimo &&
           q.ruido <= configuracao_.scannerAmRuidoMax &&
           q.interferencia == 0 &&
           offsetAbs <= configuracao_.scannerAmOffsetMax;
}

// ----------------------------------------------------------------------------
// Busca de estacao nao-bloqueante.
//
// O motor fica aqui para reaproveitar os helpers privados (sintonizar,
// estacaoValidaParaBusca, definirMute). Uma chamada de processarBusca()
// executa no maximo uma sintonia ou uma consulta de medida, de modo que o
// loop do sketch continua lendo encoders e redesenhando o display.
// ----------------------------------------------------------------------------

uint16_t RadioTef6686::proximaFrequenciaBusca() {
    const uint16_t minimo = scanner_.minimo;
    const uint16_t maximo = scanner_.maximo;
    const uint16_t incremento = scanner_.incremento;
    uint16_t& atual = scanner_.atual;

    if (scanner_.gradeFm) {
        if (scanner_.direcao == Direcao::Acima) {
            if (atual < minimo) {
                atual = minimo;
            } else if (atual >= maximo) {
                atual = minimo;
            } else {
                const uint16_t canaisDesdeMinimo =
                    static_cast<uint16_t>((atual - minimo) / incremento + 1);
                atual = static_cast<uint16_t>(
                    minimo + canaisDesdeMinimo * incremento
                );
            }
        } else {
            if (atual > maximo) {
                atual = maximo;
            } else if (atual <= minimo) {
                atual = maximo;
            } else {
                const uint16_t canaisDesdeMinimo = static_cast<uint16_t>(
                    (atual - minimo - 1) / incremento
                );
                atual = static_cast<uint16_t>(
                    minimo + canaisDesdeMinimo * incremento
                );
            }
        }
        return atual;
    }

    if (scanner_.direcao == Direcao::Acima) {
        atual = (atual > maximo - incremento)
            ? minimo : static_cast<uint16_t>(atual + incremento);
    } else {
        atual = (atual < minimo + incremento)
            ? maximo : static_cast<uint16_t>(atual - incremento);
    }
    return atual;
}

Erro RadioTef6686::iniciarBusca(Direcao direcao) {
    if (!iniciado_ || frequencia_ == 0) return falhar(Erro::NaoIniciado);

    // Reconhece um giro novo chegado no meio de uma busca: os alvos de
    // restauracao e de mute continuam sendo os da partida original do usuario.
    const bool retomando = scanner_.ativa;
    const Banda banda = banda_;

    uint16_t minimo = frequenciaMinima(banda);
    uint16_t maximo = frequenciaMaxima(banda);
    uint16_t incremento = passoBanda(banda);
    const bool gradeFm = banda == Banda::FM;
    // O passo manual pode ser fino para diagnostico, mas o scanner FM deve
    // percorrer somente os centros de canal de radiodifusao. Isso tambem
    // realinha a busca quando ela parte de uma frequencia como 97,6 MHz.
    if (gradeFm) {
        minimo = configuracao_.scannerFmPrimeiroCanal10kHz;
        incremento = configuracao_.scannerFmPasso10kHz;
        maximo = static_cast<uint16_t>(
            minimo + ((maximo - minimo) / incremento) * incremento
        );
    }
    // Em SW, uma frequencia dentro de uma faixa de radiodifusao em metros
    // faz a busca permanecer nela. Nos gaps, percorre toda a SW continua.
    if (banda == Banda::SW) {
        const uint16_t referencia = retomando
            ? scanner_.frequenciaRestauracao : frequencia_;
        FaixaSW faixa;
        if (faixaSWDaFrequencia(referencia, faixa)) {
            minimo = faixa.inicioKHz;
            maximo = faixa.fimKHz;
        }
    }
    if (minimo == 0 || incremento == 0 || maximo < minimo) {
        return falhar(Erro::ArgumentoInvalido);
    }

    EstadoScanner novo;
    novo.ativa = true;
    novo.direcao = direcao;
    novo.banda = banda;
    novo.frequenciaRestauracao = retomando
        ? scanner_.frequenciaRestauracao : frequencia_;
    novo.muteInicial = retomando ? scanner_.muteInicial : mute_;
    novo.minimo = minimo;
    novo.maximo = maximo;
    novo.incremento = incremento;
    novo.gradeFm = gradeFm;
    // A sequencia parte da frequencia atual, inclusive no meio da busca.
    novo.atual = frequencia_;

    const uint16_t totalCanais = static_cast<uint16_t>(
        (maximo - minimo) / incremento + 1
    );
    const bool inicioNaGrade =
        frequencia_ >= minimo && frequencia_ <= maximo &&
        (frequencia_ - minimo) % incremento == 0;
    // Se a sintonia atual estiver fora da grade, nenhum canal deve ser
    // excluido da volta. Se estiver na grade, evita testar o canal de partida.
    novo.maximoTentativas = static_cast<uint16_t>(
        totalCanais - (gradeFm && !inicioNaGrade ? 0 : 1)
    );

    // Silencia o audio uma unica vez, na partida da busca original.
    if (!novo.muteInicial && !retomando) {
        const Erro erro = definirMute(true);
        if (erro != Erro::Nenhum) return erro;
    }

    scanner_ = novo;
    return sucesso();
}

EstadoBusca RadioTef6686::processarBusca() {
    if (!scanner_.ativa) return EstadoBusca::Nenhuma;
    if (!iniciado_ || frequencia_ == 0) {
        scanner_.ativa = false;
        ultimoErro_ = Erro::NaoIniciado;
        return EstadoBusca::Falha;
    }

    if (!scanner_.aguardandoQualidade) {
        if (scanner_.tentativas >= scanner_.maximoTentativas) {
            return finalizarBusca(EstadoBusca::NaoEncontrada);
        }
        ++scanner_.tentativas;
        const uint16_t candidata = proximaFrequenciaBusca();
        const Erro erro = sintonizar(scanner_.banda, candidata);
        if (erro != Erro::Nenhum) return finalizarBusca(EstadoBusca::Falha);
        scanner_.aguardandoQualidade = true;
        scanner_.candidatoAprovado = false;
        scanner_.inicioJanelaQualidadeMs = millis();
        return EstadoBusca::EmAndamento;
    }

    // O chip so entrega medida nova apos alguns periodos de medicao; aqui se
    // faz uma consulta por chamada, sem delay(), para o loop nao parar.
    bool disponivel = false;
    Erro erro = qualidadeDisponivel(disponivel);
    if (erro != Erro::Nenhum) return finalizarBusca(EstadoBusca::Falha);
    if (disponivel) {
        Qualidade qualidade;
        erro = lerQualidade(qualidade);
        if (erro != Erro::Nenhum) return finalizarBusca(EstadoBusca::Falha);
        if (estacaoValidaParaBusca(qualidade)) {
            const uint8_t confirmacoes = scanner_.banda == Banda::FM
                ? configuracao_.scannerFmConfirmacoes : 1;
            // Uma leitura isolada pode coincidir com queda momentanea de WAM.
            // A confirmacao tem janela propria; o resto do loop segue rodando.
            if (confirmacoes > 1 && !scanner_.candidatoAprovado) {
                scanner_.candidatoAprovado = true;
                scanner_.inicioJanelaQualidadeMs = millis();
                return EstadoBusca::EmAndamento;
            }
            return finalizarBusca(EstadoBusca::EstacaoEncontrada);
        }
        // Candidato reprovado: o proximo canal e sintonizado na chamada seguinte.
        scanner_.aguardandoQualidade = false;
        scanner_.candidatoAprovado = false;
        return EstadoBusca::EmAndamento;
    }

    if (millis() - scanner_.inicioJanelaQualidadeMs >=
        configuracao_.scannerTimeoutQualidadeMs) {
        // Sem medida no prazo: canal vazio ou confirmacao perdida.
        scanner_.aguardandoQualidade = false;
        scanner_.candidatoAprovado = false;
    }
    return EstadoBusca::EmAndamento;
}

EstadoBusca RadioTef6686::finalizarBusca(EstadoBusca estado) {
    const Erro erroOriginal = ultimoErro_;
    const bool encontrada = estado == EstadoBusca::EstacaoEncontrada;
    const uint16_t alvo = scanner_.frequenciaRestauracao;
    const bool restaurar = !encontrada && alvo != 0 &&
        (frequencia_ != alvo || banda_ != scanner_.banda);
    const bool desmutar = !scanner_.muteInicial;
    // Desarma antes dos comandos de saida: uma falha de I2C nao deixa a busca
    // marcada como ativa para sempre.
    scanner_.ativa = false;

    Erro erro = Erro::Nenhum;
    if (restaurar) erro = sintonizar(scanner_.banda, alvo);
    if (erro == Erro::Nenhum && desmutar) erro = definirMute(false);
    if (erro != Erro::Nenhum) {
        falhar(erro);
        return EstadoBusca::Falha;
    }
    if (estado == EstadoBusca::Falha) {
        // A restauracao deu certo e apagou o erro; a causa real volta para o
        // chamador, que imprime radio.ultimoErro().
        ultimoErro_ = erroOriginal;
    }
    return estado;
}

void RadioTef6686::cancelarBusca(bool restaurarFrequencia) {
    if (!scanner_.ativa) return;
    const uint16_t alvo = scanner_.frequenciaRestauracao;
    const bool restaurar = restaurarFrequencia && alvo != 0 &&
        (frequencia_ != alvo || banda_ != scanner_.banda);
    const bool desmutar = !scanner_.muteInicial;
    scanner_.ativa = false;
    if (!iniciado_) return;
    if (restaurar) {
        const Erro erro = sintonizar(scanner_.banda, alvo);
        if (erro != Erro::Nenhum) return;
    }
    if (desmutar) definirMute(false);
}

Erro RadioTef6686::buscar(Direcao direcao) {
    const Erro erro = iniciarBusca(direcao);
    if (erro != Erro::Nenhum) return erro;
    EstadoBusca estado = EstadoBusca::EmAndamento;
    while (estado == EstadoBusca::EmAndamento) {
        estado = processarBusca();
        if (estado == EstadoBusca::EmAndamento) delay(2);
    }
    if (estado == EstadoBusca::EstacaoEncontrada) return sucesso();
    if (estado == EstadoBusca::Falha) return ultimoErro_;
    return falhar(Erro::EstacaoNaoEncontrada);
}

Erro RadioTef6686::lerRds(Rds& rds) {
    if (!iniciado_) return falhar(Erro::NaoIniciado);
    if (banda_ != Banda::FM) return falhar(Erro::ArgumentoInvalido);

    uint8_t resposta[12];
    Erro erro = obterResposta(
        MODULO_FM,
        CMD_GET_RDS_DATA,
        resposta,
        sizeof(resposta)
    );
    if (erro != Erro::Nenhum) return erro;

    const uint16_t status = lerBe16(resposta);
    const bool dadosDisponiveis = (status & 0x8000) != 0;
    const bool primeiroPi = (status & 0x2000) != 0;
    const bool sincronizado = (status & 0x0200) != 0;
    if (sincronizado) {
        leiturasSemSincronismo_ = 0;
    } else if (leiturasSemSincronismo_ < 2) {
        ++leiturasSemSincronismo_;
        if (leiturasSemSincronismo_ == 2) reiniciarRds();
    }
    if (dadosDisponiveis && sincronizado) {
        const uint16_t blocoA = lerBe16(resposta + 2);
        const uint16_t erros = lerBe16(resposta + 10);
        const uint8_t erroA = static_cast<uint8_t>((erros >> 14) & 0x03);
        confirmarPiRds(blocoA, erroA);

        // No evento de primeiro PI, o manual deixa B/C/D indefinidos.
        if (!primeiroPi && erroA <= 1 && rds_.valido && blocoA == rds_.pi) {
            processarGrupoRds(
                blocoA,
                lerBe16(resposta + 4),
                lerBe16(resposta + 6),
                lerBe16(resposta + 8),
                erros
            );
        }
    }

    rds = rds_;
    return sucesso();
}

void RadioTef6686::reiniciarRds() {
    rds_ = {};
    piCandidato_ = 0;
    repeticoesPi_ = 0;
    leiturasSemSincronismo_ = 0;
    ptyCandidato_ = 0;
    repeticoesPty_ = 0;
    preencherEspacos(psTrabalho_, 8);
    mascaraPs_ = 0;
    psCandidato_[0] = '\0';
    psCandidatoCompleto_ = false;
    preencherEspacos(textoTrabalho_, 64);
    mascaraTexto_ = 0;
    textoCandidato_[0] = '\0';
    textoCandidatoCompleto_ = false;
    textoAb_ = false;
    textoAbConhecido_ = false;
    textoVersaoB_ = false;
}

void RadioTef6686::confirmarPiRds(uint16_t pi, uint8_t erro) {
    // 0 = sem erro; 1 = erro pequeno corrigido; 2 e 3 nao sao publicados.
    if (erro > 1 || pi == 0) return;
    if (rds_.valido && pi == rds_.pi) {
        piCandidato_ = pi;
        repeticoesPi_ = 2;
        return;
    }
    if (pi == piCandidato_) {
        if (repeticoesPi_ < 2) ++repeticoesPi_;
    } else {
        piCandidato_ = pi;
        repeticoesPi_ = 1;
    }
    if (repeticoesPi_ < 2) return;

    // Um PI novo confirmado invalida todos os textos da estacao anterior.
    reiniciarRds();
    rds_.pi = pi;
    rds_.valido = true;
    piCandidato_ = pi;
    repeticoesPi_ = 2;
}

void RadioTef6686::processarGrupoRds(
    uint16_t blocoA,
    uint16_t blocoB,
    uint16_t blocoC,
    uint16_t blocoD,
    uint16_t erros
) {
    (void)blocoA;
    const uint8_t erroB = static_cast<uint8_t>((erros >> 12) & 0x03);
    const uint8_t erroC = static_cast<uint8_t>((erros >> 10) & 0x03);
    const uint8_t erroD = static_cast<uint8_t>((erros >> 8) & 0x03);
    if (erroB > 1) return;

    const uint8_t pty = static_cast<uint8_t>((blocoB >> 5) & 0x1F);
    if (pty == ptyCandidato_) {
        if (repeticoesPty_ < 2) ++repeticoesPty_;
    } else {
        ptyCandidato_ = pty;
        repeticoesPty_ = 1;
    }
    if (repeticoesPty_ >= 2) {
        rds_.pty = pty;
        rds_.ptyValido = true;
    }

    const uint8_t tipoGrupo = static_cast<uint8_t>(blocoB >> 12);
    const bool versaoB = (blocoB & 0x0800) != 0;
    if (tipoGrupo == 0 && erroD <= 1) {
        processarPsRds(blocoB, blocoD);
    } else if (tipoGrupo == 2 && erroD <= 1 && (versaoB || erroC <= 1)) {
        processarTextoRds(blocoB, blocoC, blocoD);
    }
}

void RadioTef6686::processarPsRds(uint16_t blocoB, uint16_t blocoD) {
    const uint8_t segmento = static_cast<uint8_t>(blocoB & 0x03);
    const uint8_t posicao = static_cast<uint8_t>(segmento * 2);
    psTrabalho_[posicao] = normalizarCaractereRds(static_cast<uint8_t>(blocoD >> 8));
    psTrabalho_[posicao + 1] = normalizarCaractereRds(static_cast<uint8_t>(blocoD));
    mascaraPs_ |= static_cast<uint8_t>(1U << segmento);
    if (mascaraPs_ != 0x0F) return;

    char completo[9];
    finalizarTextoRds(psTrabalho_, 8, completo);
    if (psCandidatoCompleto_ && strcmp(completo, psCandidato_) == 0) {
        converterTextoRdsParaUtf8(completo, rds_.ps, sizeof(rds_.ps));
        rds_.psValido = true;
    } else {
        strcpy(psCandidato_, completo);
        psCandidatoCompleto_ = true;
    }
    preencherEspacos(psTrabalho_, 8);
    mascaraPs_ = 0;
}

void RadioTef6686::processarTextoRds(
    uint16_t blocoB,
    uint16_t blocoC,
    uint16_t blocoD
) {
    const bool versaoB = (blocoB & 0x0800) != 0;
    const bool ab = (blocoB & 0x0010) != 0;
    if (!textoAbConhecido_ || ab != textoAb_ || versaoB != textoVersaoB_) {
        preencherEspacos(textoTrabalho_, 64);
        mascaraTexto_ = 0;
        textoCandidato_[0] = '\0';
        textoCandidatoCompleto_ = false;
        rds_.texto[0] = '\0';
        rds_.textoValido = false;
        textoAb_ = ab;
        textoAbConhecido_ = true;
        textoVersaoB_ = versaoB;
    }

    const uint8_t segmento = static_cast<uint8_t>(blocoB & 0x0F);
    const uint8_t caracteresPorSegmento = versaoB ? 2 : 4;
    const uint8_t posicao = static_cast<uint8_t>(segmento * caracteresPorSegmento);
    bool encontrouFim = false;
    if (!versaoB) {
        const char caracteres[] = {
            normalizarCaractereRds(static_cast<uint8_t>(blocoC >> 8)),
            normalizarCaractereRds(static_cast<uint8_t>(blocoC)),
            normalizarCaractereRds(static_cast<uint8_t>(blocoD >> 8)),
            normalizarCaractereRds(static_cast<uint8_t>(blocoD))
        };
        for (uint8_t i = 0; i < 4; ++i) {
            textoTrabalho_[posicao + i] = caracteres[i];
            if (static_cast<uint8_t>(caracteres[i]) == 0x0D) encontrouFim = true;
        }
    } else {
        const char caracteres[] = {
            normalizarCaractereRds(static_cast<uint8_t>(blocoD >> 8)),
            normalizarCaractereRds(static_cast<uint8_t>(blocoD))
        };
        for (uint8_t i = 0; i < 2; ++i) {
            textoTrabalho_[posicao + i] = caracteres[i];
            if (static_cast<uint8_t>(caracteres[i]) == 0x0D) encontrouFim = true;
        }
    }
    mascaraTexto_ |= static_cast<uint16_t>(1U << segmento);

    const bool segmentosContiguos =
        (mascaraTexto_ & mascaraAteSegmento(segmento)) == mascaraAteSegmento(segmento);
    if (mascaraTexto_ != 0xFFFF && !(encontrouFim && segmentosContiguos)) return;

    char completo[65];
    finalizarTextoRds(textoTrabalho_, versaoB ? 32 : 64, completo);
    if (textoCandidatoCompleto_ && strcmp(completo, textoCandidato_) == 0) {
        converterTextoRdsParaUtf8(completo, rds_.texto, sizeof(rds_.texto));
        rds_.textoValido = true;
    } else {
        strcpy(textoCandidato_, completo);
        textoCandidatoCompleto_ = true;
    }
    preencherEspacos(textoTrabalho_, 64);
    mascaraTexto_ = 0;
}

const char* RadioTef6686::textoErro(Erro erro) {
    switch (erro) {
        case Erro::Nenhum: return "nenhum";
        case Erro::NaoIniciado: return "nao iniciado";
        case Erro::ArgumentoInvalido: return "argumento invalido";
        case Erro::SemRespostaI2C: return "sem resposta I2C";
        case Erro::EscritaI2C: return "falha de escrita I2C";
        case Erro::LeituraI2CCurta: return "leitura I2C curta";
        case Erro::Timeout: return "timeout";
        case Erro::EstadoInvalido: return "estado invalido";
        case Erro::IdentidadeIncompativel: return "identidade incompativel";
        case Erro::Patch: return "falha ao carregar patch";
        case Erro::EstacaoNaoEncontrada: return "nenhuma estacao encontrada";
        case Erro::NaoImplementado: return "nao implementado";
    }
    return "erro desconhecido";
}

Erro RadioTef6686::inicializarAplicacao() {
    EstadoOperacao estado = EstadoOperacao::Desconhecido;
    Erro erro = lerEstado(estado);
    if (erro != Erro::Nenhum) return erro;
    if (estado == EstadoOperacao::Boot) {
        erro = carregarPatchV102();
        if (erro != Erro::Nenhum) return falhar(Erro::Patch);
        erro = entrarEmIdle();
        if (erro != Erro::Nenhum) return erro;
        estado = EstadoOperacao::Idle;
    }
    if (estado == EstadoOperacao::Idle) {
        erro = configurarClockEAtivar();
        if (erro != Erro::Nenhum) return erro;
    }
    erro = colocarEmStandby();
    if (erro != Erro::Nenhum) return erro;
    return executarTabelaInicializacao();
}

Erro RadioTef6686::carregarPatchV102() {
    const uint8_t reset[] = {0x1E, 0x5A, 0x01, 0x5A, 0x5A};
    Erro erro = escrever(reset, sizeof(reset), 0);
    if (erro != Erro::Nenhum) return erro;
    delay(100);
    erro = selecionarAreaPatch(0x0000);
    if (erro != Erro::Nenhum) return erro;
    delay(100);
    erro = selecionarAreaPatch(0x0074);
    if (erro != Erro::Nenhum) return erro;
    erro = enviarBlocoPatch(pPatchBytes102, PatchSize102);
    if (erro != Erro::Nenhum) return erro;
    erro = selecionarAreaPatch(0x0000);
    if (erro != Erro::Nenhum) return erro;
    delay(100);
    erro = selecionarAreaPatch(0x0075);
    if (erro != Erro::Nenhum) return erro;
    erro = enviarBlocoPatch(pLutBytes102, LutSize102);
    if (erro != Erro::Nenhum) return erro;
    erro = selecionarAreaPatch(0x0000);
    if (erro != Erro::Nenhum) return erro;
    delay(100);
    return sucesso();
}

Erro RadioTef6686::entrarEmIdle() {
    const uint8_t iniciar[] = {0x14, 0x00, 0x01};
    Erro erro = escrever(iniciar, sizeof(iniciar), 0);
    if (erro != Erro::Nenhum) return erro;
    return aguardarEstado(EstadoOperacao::Idle);
}

Erro RadioTef6686::configurarClockEAtivar() {
    const uint16_t modo = configuracao_.clockExterno ? 1 : 0;
    const uint32_t clock = configuracao_.clockHz;
    const uint8_t comandoClock[] = {
        MODULO_APLICACAO, 0x04, 0x01,
        static_cast<uint8_t>(clock >> 24),
        static_cast<uint8_t>(clock >> 16),
        static_cast<uint8_t>(clock >> 8),
        static_cast<uint8_t>(clock),
        static_cast<uint8_t>(modo >> 8),
        static_cast<uint8_t>(modo)
    };
    Erro erro = escrever(comandoClock, sizeof(comandoClock));
    if (erro != Erro::Nenhum) return erro;
    const uint8_t habilitar[] = {MODULO_APLICACAO, 0x05, 0x01, 0x00, 0x01};
    erro = escrever(habilitar, sizeof(habilitar), 0);
    if (erro != Erro::Nenhum) return erro;
    return aguardarEstado(EstadoOperacao::Standby);
}

Erro RadioTef6686::colocarEmStandby() {
    const uint16_t modo = 1;
    Erro erro = enviarComando(
        MODULO_APLICACAO,
        CMD_SET_OPERATION_MODE,
        &modo,
        1
    );
    if (erro != Erro::Nenhum) return erro;
    return aguardarEstado(EstadoOperacao::Standby);
}

Erro RadioTef6686::executarTabelaInicializacao() {
    size_t posicao = 0;
    while (true) {
        const uint8_t tamanho = pgm_read_byte(TABELA_INICIALIZACAO + posicao++);
        if (tamanho == 0) return sucesso();
        if (tamanho > 24) return falhar(Erro::Patch);
        uint8_t quadro[24];
        for (uint8_t i = 0; i < tamanho; ++i) {
            quadro[i] = pgm_read_byte(TABELA_INICIALIZACAO + posicao++);
        }
        Erro erro = escrever(quadro, tamanho);
        if (erro != Erro::Nenhum) return erro;
    }
}

Erro RadioTef6686::aguardarEstado(EstadoOperacao esperado) {
    const uint32_t inicio = millis();
    do {
        EstadoOperacao estado = EstadoOperacao::Desconhecido;
        Erro erro = lerEstado(estado);
        if (erro != Erro::Nenhum) return erro;
        if (estado == esperado) return sucesso();
        delay(5);
    } while (millis() - inicio < configuracao_.timeoutEstadoMs);
    return falhar(Erro::Timeout);
}

Erro RadioTef6686::escrever(
    const uint8_t* dados,
    size_t tamanho,
    uint16_t pausaMs
) {
    if (wire_ == nullptr) return falhar(Erro::NaoIniciado);
    wire_->beginTransmission(configuracao_.enderecoI2C);
    const size_t enviados = wire_->write(dados, tamanho);
    const uint8_t codigo = wire_->endTransmission(true);
    if (enviados != tamanho || codigo != 0) {
        return falhar(Erro::EscritaI2C);
    }
    if (pausaMs > 0) delay(pausaMs);
    return sucesso();
}

Erro RadioTef6686::ler(uint8_t* dados, size_t tamanho) {
    if (wire_ == nullptr) return falhar(Erro::NaoIniciado);
    const size_t recebidos = wire_->requestFrom(
        configuracao_.enderecoI2C,
        static_cast<uint8_t>(tamanho),
        static_cast<uint8_t>(true)
    );
    if (recebidos != tamanho) {
        while (wire_->available()) wire_->read();
        return falhar(Erro::LeituraI2CCurta);
    }
    for (size_t i = 0; i < tamanho; ++i) {
        dados[i] = static_cast<uint8_t>(wire_->read());
    }
    return sucesso();
}

Erro RadioTef6686::enviarComando(
    uint8_t modulo,
    uint8_t comando,
    const uint16_t* parametros,
    uint8_t quantidade
) {
    if (quantidade > 14 || (quantidade > 0 && parametros == nullptr)) {
        return falhar(Erro::ArgumentoInvalido);
    }
    uint8_t quadro[31] = {modulo, comando, 1};
    size_t posicao = 3;
    for (uint8_t i = 0; i < quantidade; ++i) {
        quadro[posicao++] = static_cast<uint8_t>(parametros[i] >> 8);
        quadro[posicao++] = static_cast<uint8_t>(parametros[i]);
    }
    return escrever(quadro, posicao);
}

Erro RadioTef6686::obterResposta(
    uint8_t modulo,
    uint8_t comando,
    uint8_t* resposta,
    size_t tamanho
) {
    const uint8_t pedido[] = {modulo, comando, 1};
    Erro erro = escrever(pedido, sizeof(pedido));
    if (erro != Erro::Nenhum) return erro;
    return ler(resposta, tamanho);
}

Erro RadioTef6686::selecionarAreaPatch(uint16_t area) {
    const uint8_t comando[] = {
        0x1C,
        static_cast<uint8_t>(area >> 8),
        static_cast<uint8_t>(area)
    };
    return escrever(comando, sizeof(comando), 0);
}

Erro RadioTef6686::enviarBlocoPatch(
    const unsigned char* dados,
    size_t tamanho
) {
    uint8_t quadro[25];
    quadro[0] = 0x1B;
    while (tamanho > 0) {
        const uint8_t bloco = tamanho > 24 ? 24 : static_cast<uint8_t>(tamanho);
        for (uint8_t i = 0; i < bloco; ++i) {
            quadro[i + 1] = pgm_read_byte(dados + i);
        }
        Erro erro = escrever(quadro, bloco + 1, 0);
        if (erro != Erro::Nenhum) return erro;
        dados += bloco;
        tamanho -= bloco;
    }
    return sucesso();
}

Erro RadioTef6686::falhar(Erro erro) {
    ultimoErro_ = erro;
    return erro;
}

Erro RadioTef6686::sucesso() {
    ultimoErro_ = Erro::Nenhum;
    return Erro::Nenhum;
}

uint16_t RadioTef6686::lerBe16(const uint8_t* dados) {
    return (static_cast<uint16_t>(dados[0]) << 8) | dados[1];
}

}  // namespace tef6686
