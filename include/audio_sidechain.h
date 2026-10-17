#pragma once

// Ducking tipo "sidechain" para os sons sobrepostos (AudioPlayer::PlayWavMixed):
// o volume dos sons comuns (toques da contagem) acompanha, ao contrário, o nível do
// som dominante (som do grupo, "it's bullet time"): quanto mais alto o dominante
// naquele instante, mais baixo ficam os outros; quando ele decai, os outros voltam,
// com um release suave. Só cabeçalho, usado pelas versões Mac e Windows.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace AudioSidechain
{
    // Nível (RMS, 0..1 em relação ao pico do arquivo) a cada kStepSeconds
    struct Envelope
    {
        static constexpr double kStepSeconds = 0.04;
        std::vector<float> levels;
        double seconds = 0;

        float At(double t) const
        {
            if (levels.empty() || t < 0)
                return 0.0f;
            const size_t i = static_cast<size_t>(t / kStepSeconds);
            return i < levels.size() ? levels[i] : 0.0f;
        }
    };

    // Lê um WAV PCM 16 bits (RIFF "fmt " + "data") e calcula o envelope
    inline Envelope ComputeWavEnvelope(const std::string& bytes)
    {
        Envelope env;
        if (bytes.size() < 12 || bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 4, "WAVE") != 0)
            return env;
        uint16_t channels = 0, bits = 0;
        uint32_t rate = 0;
        const int16_t* samples = nullptr;
        size_t sampleCount = 0;
        size_t pos = 12;
        while (pos + 8 <= bytes.size())
        {
            const std::string id = bytes.substr(pos, 4);
            uint32_t size = 0;
            std::memcpy(&size, bytes.data() + pos + 4, 4);
            const size_t body = pos + 8;
            if (body + size > bytes.size())
                break;
            if (id == "fmt " && size >= 16)
            {
                std::memcpy(&channels, bytes.data() + body + 2, 2);
                std::memcpy(&rate, bytes.data() + body + 4, 4);
                std::memcpy(&bits, bytes.data() + body + 14, 2);
            }
            else if (id == "data")
            {
                samples = reinterpret_cast<const int16_t*>(bytes.data() + body);
                sampleCount = size / 2;
            }
            pos = body + size + (size & 1);
        }
        if (!samples || bits != 16 || channels == 0 || rate == 0)
            return env;
        const size_t frames = sampleCount / channels;
        const size_t step = static_cast<size_t>(rate * Envelope::kStepSeconds);
        float peak = 0.0f;
        for (size_t start = 0; start < frames; start += step)
        {
            const size_t end = std::min(frames, start + step);
            double sum = 0;
            for (size_t f = start; f < end; ++f)
                for (uint16_t c = 0; c < channels; ++c)
                {
                    const double v = samples[f * channels + c] / 32768.0;
                    sum += v * v;
                }
            const float rms = static_cast<float>(std::sqrt(sum / std::max<size_t>(1, (end - start) * channels)));
            env.levels.push_back(rms);
            peak = std::max(peak, rms);
        }
        if (peak > 0)
            for (float& l : env.levels)
                l /= peak;
        env.seconds = static_cast<double>(frames) / rate;
        return env;
    }

    // Ganho dos sons comuns a partir do nível do dominante: desce na hora (attack) e
    // sobe devagar (release). depth = quanto abaixa no pico (0,8 = fica em 20 %).
    class Compressor
    {
    public:
        float Update(float keyLevel, double dtSeconds)
        {
            const float target = 1.0f - kDepth * std::min(1.0f, keyLevel * kSensitivity);  // chega ao fundo antes do pico
            if (target < gain)
                gain = target;  // attack imediato
            else
                gain = std::min(target, gain + static_cast<float>(dtSeconds / kReleaseSeconds));
            return gain;
        }
        float Gain() const { return gain; }
        void Reset() { gain = 1.0f; }

    private:
        // Ajustados em 2026-10-17 (pedido: mais intenso): no pico os outros ficam em 20 %
        static constexpr float kDepth = 0.8f;
        static constexpr float kSensitivity = 2.0f;
        static constexpr double kReleaseSeconds = 0.5;
        float gain = 1.0f;
    };
}
