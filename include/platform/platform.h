#pragma once

// Funções que dependem do sistema operacional.
// Cada sistema tem sua própria implementação em src/platform/<sistema>/.
// O restante do programa usa apenas estas funções, nunca APIs do sistema direto.

#include <atomic>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace Platform
{
    // Prepara o terminal para exibir acentos e emojis (UTF-8).
    void InitConsole();

    // Pasta onde está o executável do programa.
    std::filesystem::path ExecutableDirectory();

    // Pasta com "assets" (sons e a página da interface): a do executável ou, no
    // aplicativo de Mac (.app), Contents/Resources.
    std::filesystem::path ResourcesDirectory();

    // Pasta "Downloads" do usuário (ou a pasta atual, se não for possível descobrir).
    std::filesystem::path DownloadsFolder();

    // Pasta de dados do app (fotos guardadas até a pessoa salvar):
    // Windows: %LOCALAPPDATA%\Stillpoint   Mac: ~/Library/Application Support/Stillpoint
    // (na primeira vez, a pasta antiga "Canon Bullet Time" é renomeada para a nova)
    std::filesystem::path AppDataFolder();

    // Converte um horário para a hora local do computador.
    std::tm LocalTime(std::time_t t);

    // Processa eventos do sistema por até `milliseconds` (0 = só os pendentes).
    // No Mac, o SDK da Canon entrega eventos das câmeras por aqui; no Windows não faz nada.
    void ProcessEvents(int milliseconds = 0);

    // Descarta o que foi digitado durante uma espera longa, para não virar
    // uma resposta acidental no próximo menu.
    void DiscardPendingInput();

    // Preparação/encerramento de uma thread auxiliar que chama o SDK da Canon.
    // No Windows o SDK exige COM inicializado em cada thread; no Mac não faz nada.
    void BeginSdkThread();
    void EndSdkThread();

    // Se a sessão de uma câmera pode ser aberta numa thread auxiliar (para a janela
    // continuar mostrando o andamento). Mac: sim (a T7 leva ~40 s). Windows: não;
    // dado de 2026-10-14: aberta em outra thread, os comandos seguintes falhavam.
    bool SdkSessionsInWorkerThreads();

    // Aparelho da Canon ligado no USB, segundo o próprio sistema (sem o SDK da Canon)
    struct UsbDevice
    {
        std::string name;       // ex.: "Canon Digital Camera" (o mesmo para todos os modelos)
        std::string location;   // endereço da entrada USB (fixo por entrada física)
        int productId = 0;      // código do produto: diferente para cada modelo de câmera
    };

    // Câmeras da Canon ligadas no USB (só aparelhos com interface de imagem estática,
    // a das câmeras: impressoras e scanners da Canon ficam de fora). Dado observado no
    // Mac: a T7 aparece no USB
    // ~40 s antes de aparecer no SDK. totalUsb recebe quantos aparelhos USB o sistema
    // listou, ou -1 se não deu para ler. Windows: SetupAPI; Mac: IOKit.
    std::vector<UsbDevice> CanonUsbDevices(int& totalUsb);

    // Reiniciar o app: abre uma nova cópia do programa, que espera esta fechar antes
    // de começar (WaitForPreviousInstance, no início do programa). false se não abriu.
    bool RelaunchAfterExit();
    void WaitForPreviousInstance();

    // --- Atualização do app (só o app de janela) ---

    // curl do próprio sistema (Windows 10/11: System32\curl.exe; Mac: /usr/bin/curl),
    // usado para falar com o GitHub e baixar a versão nova.
    std::filesystem::path CurlExecutable();

    // Roda um programa sem abrir janela e espera ele terminar. Devolve o código de
    // saída, ou -1 se não abriu, se passou de timeoutSeconds ou se cancel virou true
    // (nesses casos o programa é encerrado).
    int RunProcess(const std::vector<std::string>& args, int timeoutSeconds, const std::atomic<bool>* cancel = nullptr);

    // SHA-256 do arquivo em hexadecimal minúsculo ("" se não deu para ler).
    std::string Sha256OfFile(const std::filesystem::path& file);

    // Se este app pode se atualizar sozinho. Windows: só se foi instalado pelo
    // instalador (tem o desinstalador ao lado). Mac: só se roda de dentro de um .app
    // numa pasta em que dá para escrever (ex.: Aplicativos). reason explica o não.
    bool CanSelfUpdate(std::string& reason);

    // Começa a instalar a versão baixada e devolve logo; o app deve fechar em seguida.
    // Windows: roda o instalador em modo silencioso (ele reabre o app no fim).
    // Mac: um script espera o app fechar, troca o .app pelo do .dmg e abre de novo.
    bool StartUpdateInstall(const std::filesystem::path& installer, std::string& error);

    // Abre um endereço da internet no navegador padrão.
    void OpenUrl(const std::string& url);

    // Abre uma pasta no Finder (Mac) ou no Explorador de Arquivos (Windows).
    void OpenInFileManager(const std::filesystem::path& folder);
}
