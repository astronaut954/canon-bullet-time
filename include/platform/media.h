#pragma once

// Recursos de mídia do sistema, usados só pelo app de janela na exportação:
// codificador de vídeo H.264 (Mac: VideoToolbox; Windows: Media Foundation)
// e gravação de fotos HEIC (Mac: ImageIO; Windows: WIC, se as extensões
// HEIF e HEVC da Microsoft estiverem instaladas).
// Implementação em src/platform/<sistema>/media_<sistema>.cpp.

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Platform
{
    class H264Encoder
    {
    public:
        virtual ~H264Encoder() = default;
        // Um quadro RGB (3 bytes por pixel, largura x altura do Create). Acrescenta
        // em "out" o que o codificador já produziu (formato Annex-B: 00 00 00 01 + NAL).
        virtual bool Encode(const unsigned char* rgb, std::vector<unsigned char>& out, std::string& error) = 0;
        // Fim: entrega os quadros que faltam
        virtual bool Finish(std::vector<unsigned char>& out, std::string& error) = 0;
    };

    // Arquivo MP4 inteiro gravado pelo próprio sistema (Windows: Media Foundation, que
    // monta o arquivo do jeito que os players do Windows esperam). Onde não houver,
    // devolve nullptr sem erro e a exportação usa o H264Encoder + minimp4.
    class VideoFileWriter
    {
    public:
        virtual ~VideoFileWriter() = default;
        virtual bool AddFrame(const unsigned char* rgb, std::string& error) = 0;
        virtual bool Finish(std::string& error) = 0;
    };
    std::unique_ptr<VideoFileWriter> CreateVideoFileWriter(const std::filesystem::path& file, int width, int height,
                                                           int fps, std::string& error);

    // width e height pares. nullptr (com error preenchido) se não houver codificador.
    std::unique_ptr<H264Encoder> CreateH264Encoder(int width, int height, int fps, std::string& error);

    bool HeicAvailable(std::string& reason);
    bool WriteHeic(const unsigned char* rgb, int width, int height, const std::filesystem::path& file,
                   std::string& error);
}
