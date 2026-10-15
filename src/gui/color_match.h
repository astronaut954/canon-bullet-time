#pragma once

// "Auto match" do Camera settings: mede cor e brilho do centro da foto de cada câmera,
// para igualar todas à média pelo ajuste fino do balanço de branco e pelo ISO.
// Só contas; quem dispara e muda as câmeras é o main_gui.cpp.

#include <filesystem>
#include <vector>

namespace ColorMatch
{
    struct Stats
    {
        bool ok = false;
        double rb = 0;   // ln(R/B): mais alto = mais quente (âmbar), mais baixo = mais frio (azul)
        double g = 0;    // ln(G / √(R·B)): mais alto = mais verde, mais baixo = mais magenta
        double ev = 0;   // brilho em stops (log2 da luminância mediana)
    };

    // Centro da foto (metade da largura e da altura), em luz linear. Para a cor, ignora
    // o que está estourado ou quase preto. ok = false se não der para ler a foto.
    Stats Measure(const std::filesystem::path& photo);

    // Média de várias medidas (só as ok)
    Stats Average(const std::vector<Stats>& all);

    // Quanto cada passo do ajuste fino muda a medida (ponto de partida; o app corrige
    // pelo que mediu depois de mudar). Canon: 1 passo azul/âmbar ≈ 5 mireds.
    constexpr double kStartGainAB = 0.022;  // ln(R/B) por passo
    constexpr double kStartGainGM = 0.020;  // ln(G/√(R·B)) por passo

    // Diferenças pequenas demais para corrigir
    constexpr double kEvTolerance = 0.17;   // ~1/6 de stop
    constexpr double kStepTolerance = 0.6;  // menos de um passo
}
