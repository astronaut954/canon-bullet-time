#pragma once

// Fotos guardadas pelo app até a pessoa salvar (pasta de dados do app).
// Cada sessão de disparo é uma pasta; cada foto começa com a ordem e o nome
// da câmera, ex.: "02_T7_1_IMG_0001.JPG".

#include <filesystem>
#include <string>
#include <vector>

struct StoredPhoto
{
    std::string file;    // nome do arquivo
    int order = 0;       // posição da câmera na sequência (01, 02...)
    std::string camera;  // nome da câmera (T7_1...)
    bool previewable = true; // JPEG ou RAW (prévia embutida no arquivo)
};

struct StoredSession
{
    std::string name;    // nome da pasta (data/hora da sessão)
    std::vector<StoredPhoto> photos; // em ordem de câmera
    bool saved = false;  // já foi salva e a pessoa escolheu mantê-la no app
};

// Imagem RGB (3 bytes por pixel), já na orientação certa
struct RgbFrame
{
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
};

// Decodifica uma foto (JPEG, ou RAW pela prévia JPEG que a câmera embute nele).
// jpegBytes (opcional) recebe esse JPEG de origem, com os dados EXIF da câmera.
bool LoadPhotoRgb(const std::filesystem::path& file, RgbFrame& out, std::string* jpegBytes = nullptr);
bool IsRawPhoto(const std::string& file);
bool IsJpegPhoto(const std::string& file);

class PhotoStore
{
public:
    explicit PhotoStore(std::filesystem::path root);

    const std::filesystem::path& Root() const { return root; }

    std::vector<StoredSession> ListSessions() const;
    int CountSessions() const;
    // Sessões ainda não salvas (as salvas e mantidas no app não contam)
    int CountUnsaved() const;
    // Marca a sessão como salva (arquivo ".saved" dentro da pasta dela)
    void MarkSaved(const std::string& session) const;

    // JPEG reduzido (lado maior = maxSize), com cache em disco. "" se não der.
    std::string Preview(const std::string& session, const std::string& file, int maxSize) const;

    // Move as sessões para a pasta escolhida: só as de "names" ou, se vazio, todas.
    // Retorna quantas foram movidas; em caso de erro, preenche errorMessage.
    int SaveSessions(const std::filesystem::path& destination, const std::vector<std::string>& names,
                     std::string& errorMessage,
                     std::vector<std::filesystem::path>* savedTo = nullptr) const;  // onde cada uma ficou

    bool DeleteSession(const std::string& session) const;

    // Apaga fotos de uma sessão. Cada item é um grupo "ordem_câmera" (ex.: "02_T7_1",
    // apaga o RAW e o JPEG dessa câmera) ou o nome de um arquivo. Se a sessão ficar
    // vazia, a pasta é apagada.
    bool DeletePhotos(const std::string& session, const std::vector<std::string>& groups,
                      std::string& errorMessage) const;

    // Nova ordem das fotos de uma sessão: lista de "ordem_câmera" na ordem desejada
    // (ex.: {"02_T7_1", "01_SL3_1"}). Renomeia os arquivos (02_T7_1_IMG.JPG ->
    // 01_T7_1_IMG.JPG), levando juntos o RAW e o JPEG da mesma câmera.
    bool ReorderSession(const std::string& session, const std::vector<std::string>& groups,
                        std::string& errorMessage) const;
    void DeleteAll() const;
    // Apaga só as sessões ainda não salvas
    void DeleteUnsaved() const;
    // Apaga as sessões já salvas que ficaram no app (ao fechar e ao abrir)
    void DeleteSaved() const;

private:
    std::filesystem::path SessionPath(const std::string& session) const;

    std::filesystem::path root;
};

std::string Base64Encode(const std::string& data);
