#pragma once

// Cartela de cores do Stillpoint (assets/chart/stillpoint-chart.pdf, feita por
// scripts/make_chart.py): acha a cartela numa foto pelas 4 marcas dos cantos e mede como
// a câmera desenhou os tons de cinza (contraste), as cores (saturação) e os tons de pele
// (tom de cor). As medidas da cartela (mm) são as mesmas do script.

#include <filesystem>
#include <string>

namespace Chart
{
    // Onde estão os centros das 4 marcas na foto (0 a 1), na ordem da cartela:
    // 0 = em cima à esquerda (a da bolinha), 1 = em cima à direita, 2 = embaixo à direita,
    // 3 = embaixo à esquerda
    struct Quad
    {
        bool found = false;
        double x[4] = { 0.3, 0.7, 0.7, 0.3 };
        double y[4] = { 0.3, 0.3, 0.7, 0.7 };
    };

    struct Reading
    {
        bool ok = false;
        std::string problem;      // por que não deu para medir (em inglês, para a tela)
        double contrast = 0;      // L* do tom claro menos o do tom escuro (mais = mais contraste)
        double sat = 0;           // ln do croma médio das 16 cores (mais = mais saturado)
        double tone = 0;          // ângulo de cor dos tons de pele, em graus (mais = mais amarelo)
        double wedge[11] = {};    // L* de cada tom de cinza, do branco ao preto
        bool clipped = false;     // 3º tom estourado ou 9º sem detalhe: exposição errada
    };

    // Acha a cartela (found = false se não achar as 4 marcas)
    Quad Find(const std::filesystem::path& photo);
    // Mede a cartela na foto, com os cantos achados (ou corrigidos pela pessoa)
    Reading Measure(const std::filesystem::path& photo, const Quad& quad);
}
