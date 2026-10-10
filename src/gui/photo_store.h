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
};

class PhotoStore
{
public:
    explicit PhotoStore(std::filesystem::path root);

    const std::filesystem::path& Root() const { return root; }

    std::vector<StoredSession> ListSessions() const;
    int CountSessions() const;

    // JPEG reduzido (lado maior = maxSize), com cache em disco. "" se não der.
    std::string Preview(const std::string& session, const std::string& file, int maxSize) const;

    // Move todas as sessões para a pasta escolhida. Retorna quantas foram movidas;
    // em caso de erro, preenche errorMessage.
    int SaveAll(const std::filesystem::path& destination, std::string& errorMessage) const;

    bool DeleteSession(const std::string& session) const;
    void DeleteAll() const;

private:
    std::filesystem::path SessionPath(const std::string& session) const;

    std::filesystem::path root;
};

std::string Base64Encode(const std::string& data);
