#pragma once

// "Test shot" do Camera settings com "Match automatically": deixa as câmeras que dá
// para ajustar o mais parecidas possível, mudando as próprias câmeras.
//  1. Sem foto: mesma velocidade, abertura, ISO e compensação (o valor mais perto da
//     média que todas aceitam), o mesmo Picture Style (o mais comum, nunca Auto) e os
//     mesmos Contrast, Saturation e Color tone (os mais comuns).
//  2. Foto 1, como estão: a média da cor e do brilho de todas vira o alvo.
//  3. Balanço de branco em Auto ou diferente entre elas: todas em Daylight sem ajuste
//     fino, foto 2, e daí um balanço comum (Kelvin, se todas aceitarem; senão o
//     predefinido mais perto) que deixa a cor perto do alvo.
//  3b. Modelo ainda não calibrado neste computador: mais 2 fotos, com o ajuste fino no
//     extremo de cada eixo, para medir o sentido e a força dele naquela câmera.
//  4. Cor pelo ajuste fino do balanço de branco e brilho pelo ISO (em M) ou pela
//     compensação (outros modos); foto de novo para conferir, até ficarem perto do alvo.
// Com o cartão cinza (Options::grayCard), quem chama mede só o cartão; o alvo da cor passa
// a ser o cinza neutro (balanço de branco certo) e o balanço comum é procurado sempre.
// Quem dispara e mede é quem chama (função Shoot); aqui ficam as decisões.

#include "chart.h"
#include "color_match.h"

#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

class CanonController;

namespace AutoMatch
{
    // Tira uma foto com todas as câmeras e devolve a medida de cada uma (pelo nome).
    // shot/shots: foto atual e o máximo; stage: o que está sendo feito, para a tela.
    // false = as fotos não vieram.
    using Shoot = std::function<bool(int shot, int shots, const std::string& stage,
                                     std::map<std::string, ColorMatch::Stats>& measured)>;

    struct Change
    {
        std::string camera;
        std::vector<std::array<std::string, 3>> items;  // configuração, antes, depois
    };

    struct Result
    {
        bool ok = false;        // terminou (as fotos vieram e deu para medir)
        bool matched = false;   // o ajuste rodou (false: menos de duas câmeras ajustáveis)
        bool cancelled = false; // a pessoa cancelou: as câmeras voltaram como estavam
        std::string error;
        std::vector<Change> changes;
        std::vector<std::string> notes;
    };

    // Como o ajuste fino de um modelo mexe na foto, medido pelo app (2 fotos de teste nos
    // extremos) e guardado no computador: sentido de cada eixo (1 = + âmbar / + verde,
    // como o app mostra; -1 = invertido) e quanto 1 passo muda a cor.
    struct Calibration
    {
        int abSign = 1, gmSign = 1;
        double rbPerStep = 0, gPerStep = 0;
    };
    // Arquivo de texto, uma linha por modelo. Sem arquivo: lista vazia.
    std::map<std::string, Calibration> LoadCalibration(const std::filesystem::path& file);

    struct Options
    {
        bool grayCard = false;  // as medidas são do cartão cinza: cor certa = cinza neutro
        // Calibração com a cartela (LoadChartCalibration): com modelos misturados, cada modelo
        // recebe o Contrast/Saturation/Color tone que o deixa parecido com os outros
        std::filesystem::path chartFile;
    };

    // ---- Cartela de cores ----
    // Quanto cada modelo precisa mudar (em passos do Picture Style) para ficar parecido com
    // os outros, medido com a cartela. Só a diferença entre modelos importa.
    struct ChartOffsets
    {
        double contrast = 0, saturation = 0, colorTone = 0;
    };
    // Chave: "<modelo>|<código do Picture Style>"
    std::map<std::string, ChartOffsets> LoadChartCalibration(const std::filesystem::path& file);
    std::string ChartKey(const std::string& model, long long style);

    // Tira uma foto com todas as câmeras e devolve a medida da cartela de cada uma
    using ChartShoot = std::function<bool(int shot, int shots, const std::string& stage,
                                          std::map<std::string, Chart::Reading>& measured)>;
    struct ChartResult
    {
        bool ok = false;
        bool cancelled = false;
        std::string error;
        std::vector<std::string> models;  // uma linha por modelo, para a tela
        std::vector<std::string> notes;
    };
    // 4 fotos (como estão; Contrast +2; Saturation +2; Color tone +2), sempre com o mesmo
    // Picture Style e o mesmo balanço de branco em todas; depois tudo volta como estava.
    // Precisa de pelo menos 2 modelos diferentes. Guarda o resultado em chartFile.
    ChartResult CalibrateChart(CanonController& controller, const ChartShoot& shoot, const std::filesystem::path& chartFile,
                               const std::function<bool()>& cancelled,
                               const std::function<void(const std::string&)>& trace = nullptr);

    // Chamar na thread das câmeras, sem conexão em andamento. calibrationFile: onde ficam
    // as calibrações por modelo (lidas e atualizadas).
    // cancelled: a pessoa pediu para parar (conferido entre as fotos; Shoot também deve
    // devolver false). Ao parar, as câmeras voltam às configurações de antes.
    // trace: recebe, linha a linha, o que foi medido e decidido (modo debug; pode ser vazio).
    Result Run(CanonController& controller, const Shoot& shoot, const std::filesystem::path& calibrationFile,
               const std::function<bool()>& cancelled, const Options& options = {},
               const std::function<void(const std::string&)>& trace = nullptr);
}
