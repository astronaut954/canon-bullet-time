#pragma once

// "Match automatically" do Camera settings: mede cor, contraste e saturação do centro da
// foto de cada câmera e o brilho da foto inteira; ou, com o cartão cinza, só o cartão.
// Só a medida; as decisões ficam em auto_match.cpp.

#include <filesystem>
#include <vector>

namespace ColorMatch
{
    struct Stats
    {
        bool ok = false;
        double rb = 0;   // ln(R/B): mais alto = mais quente (âmbar), mais baixo = mais frio (azul)
        double g = 0;    // ln(G / √(R·B)): mais alto = mais verde, mais baixo = mais magenta
        double ev = 0;   // brilho em stops: claridade média da foto inteira (centro vale o dobro)
        double contrast = 0;   // stops entre os 10% mais escuros e os 10% mais claros
        double sat = 0;        // ln da saturação média (força das cores)
        double dark = 0;       // fração da foto quase preta (0 a 1)
        double clipped = 0;    // fração da foto estourada (algum canal no máximo)
        // Brilho e contraste confiáveis: a foto não está quase toda preta nem estourada.
        // (Nas sombras e nos estourados a câmera achata os tons, e a medida engana.)
        bool exposureOk = true;
    };

    // Parte da foto, de 0 a 1 (0,0 = canto superior esquerdo)
    struct Region
    {
        bool found = false;
        double x0 = 0, y0 = 0, x1 = 1, y1 = 1;
    };

    // Centro da foto (metade da largura e da altura), em luz linear. Para a cor, ignora
    // o que está estourado ou quase preto. ok = false se não der para ler a foto.
    // Com region: só aquela parte (o cartão cinza): cor e brilho médios dela.
    Stats Measure(const std::filesystem::path& photo, const Region* region = nullptr);

    // Cartão cinza: a maior área lisa (sem textura), de cor quase neutra e tom médio,
    // de preferência perto do centro. found = false se não achar.
    Region FindGrayCard(const std::filesystem::path& photo);
    // A área lisa em volta de um ponto clicado (x, y de 0 a 1). Se o ponto não estiver
    // numa área lisa, um quadradinho em volta dele.
    Region GrayCardAt(const std::filesystem::path& photo, double x, double y);

    // Média de várias medidas (só as ok; brilho e contraste só das com exposureOk, se houver)
    Stats Average(const std::vector<Stats>& all);

    // Diferenças pequenas demais para corrigir
    constexpr double kEvTolerance = 0.17;   // ~1/6 de stop
    constexpr double kStepTolerance = 0.6;  // menos de um passo
}
