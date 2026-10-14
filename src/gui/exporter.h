#pragma once

// Exportação das sessões: fotos convertidas (JPG, PNG, WEBP, TIFF, HEIC) e
// sequências (MP4, MOV, GIF, WEBP, AVIF). Usado pelo "Save sessions".

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Export
{
    struct SequenceOptions
    {
        std::vector<std::string> formats;  // "mp4", "mov", "gif", "webp", "avif"
        double fps = 6.0;                  // fotos por segundo
        int loops = 3;                     // repetições dentro do MP4/MOV
        bool reverse = true;               // vai e volta: 1, 2, 3, 2, 1...
        int height = 1080;                 // 720, 1080 ou 2160 (4K: só MP4/MOV)
    };

    // Ordem dos quadros de UMA volta: 0..n-1 e, com reverse, volta até 1 (sem repetir as pontas)
    std::vector<int> CycleOrder(int photos, bool reverse);

    // Andamento: fração de 0 a 1 e o que está sendo feito ("GIF", "MP4"...)
    using Progress = std::function<void(double fraction, const std::string& step)>;

    // Cria os arquivos <base>.<formato> a partir das fotos (uma por câmera, em ordem).
    // GIF, WEBP e AVIF guardam uma volta e repetem sem parar; MP4 e MOV têm as voltas
    // dentro do vídeo. Devolve false e preenche error se algo falhar.
    bool WriteSequence(const std::vector<std::filesystem::path>& photos, const std::filesystem::path& base,
                       const SequenceOptions& options, const Progress& progress,
                       const std::atomic<bool>* cancel, std::string& error);

    // Converte uma foto para "jpg", "png", "webp", "tiff" ou "heic". dest sem extensão.
    bool ConvertPhoto(const std::filesystem::path& photo, const std::filesystem::path& destWithoutExtension,
                      const std::string& format, std::string& error);

    // Formatos que este computador consegue gerar (HEIC no Windows depende de extensões)
    bool FormatAvailable(const std::string& format, std::string& reason);
}
