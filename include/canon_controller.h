#pragma once

#include "EDSDK.h"
#include "camera_context.h"
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <thread>
#include <vector>
#include <set>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

// Avisos sobre a conexão com cada câmera, para a interface mostrar o andamento.
class ConnectionObserver
{
public:
    virtual ~ConnectionObserver() = default;

    virtual void OnConnecting(const CameraContext& /*camera*/) {}
    // Chamado a cada ~0,4 s enquanto a conexão não termina
    virtual void OnConnectingProgress(const CameraContext& /*camera*/, int /*seconds*/, int /*animationFrame*/) {}
    virtual void OnConnected(const CameraContext& /*camera*/, double /*seconds*/) {}
    virtual void OnConnectFailed(const CameraContext& /*camera*/, long /*error*/, double /*seconds*/) {}
    // Uma câmera nova apareceu durante a espera por câmeras que faltavam
    virtual void OnNewCamerasFound() {}
};

// Câmera que está conectando agora (para a interface mostrar)
struct PendingConnectionInfo
{
    std::string name;        // nome provisório (T7_1...) ou o da câmera que caiu
    std::string model;
    int order = 0;           // posição atual (0 = câmera nova, ainda sem posição)
    bool reconnecting = false;
    bool retrying = false;   // essa conexão USB já falhou antes (nova tentativa automática)
    bool queued = false;     // esperando outra câmera terminar de conectar
    int seconds = 0;
};

struct StartingCameraInfo
{
    std::string model;       // modelo, quando já se sabe pelo código do produto USB; senão ""
    int seconds = 0;         // há quanto tempo apareceu no USB
};

class CanonController
{
public:
    CanonController() = default;
    ~CanonController();
    CanonController(const CanonController&) = delete;
    CanonController& operator=(const CanonController&) = delete;

    bool InitializeSdk();
    void TerminateSdk();

    bool DetectCameras();
    bool OpenSessions(ConnectionObserver* observer = nullptr);
    std::vector<std::string> GetFailedCameraNames() const;
    std::set<std::string> GetFailedCameraPorts() const;
    std::vector<std::string> ConnectNewCameras(std::set<std::string>& ignoredPorts, ConnectionObserver* observer = nullptr);
    void DropFailedCameras();
    // Tempo real (interface gráfica). Chamar com frequência (ex.: a cada 0,4 s).
    // Não bloqueia: a câmera conecta numa thread própria, uma por vez (o SDK não
    // aceita outras chamadas enquanto uma sessão abre). Enquanto isso, esta função
    // não chama o SDK. Marca as que pararam de responder e conecta as que voltaram
    // ou chegaram; câmeras que voltam assumem o lugar (nome e ordem) da que tinha
    // caído. Retorna true se a lista mudou.
    bool MonitorTick(ConnectionObserver* observer);
    std::vector<PendingConnectionInfo> GetPendingConnections() const;
    bool HasPendingConnections() const;
    // Câmeras ligadas que o USB já mostra e o SDK ainda não (ver o .cpp)
    std::vector<StartingCameraInfo> GetStartingCameras() const;
    // Espera as conexões em andamento terminarem e descarta o resultado.
    void WaitPendingConnections();
    // Esquece falhas e tempos anteriores: todas as câmeras são tentadas de novo já.
    void ResetConnectionHistory();
    void RegisterEventHandlers();
    bool SetSaveToHost();
    bool SetCapacityForAll();
    bool ShootAll();
    void CloseSessions();

    void SetOutputFolder(const std::string& folderPath);
    std::vector<CameraContext>& GetCameras();
    std::string GetOutputFolder() const;

    void SetVerbose(bool enabled);

    void PrintCameraOrder() const;
    bool SetCameraPrefix(const std::string& shortName, int newPrefix);
    void ResetCameraPrefixesSequential();
    // Define a ordem inteira de uma vez (01, 02...) pelos nomes (T7_1, SL3_1...).
    // Câmeras fora da lista ficam depois, na ordem em que já estavam.
    void SetCameraOrder(const std::vector<std::string>& shortNames);

    int GetExpectedDownloads() const;
    int GetCompletedDownloads() const;

    void RefreshCameraConnectionStatus();

    // Alguma câmera avisou que uma configuração mudou (ISO, abertura... mexidos na
    // câmera ou pelo app). Devolve o aviso e o apaga.
    bool TakeSettingsChanged() { return settingsChanged.exchange(false); }
    bool PrepareSessionFolder(int sessionIndex, int totalSessions, const std::string& shotId);

private:
    // atômicos: no Mac o aviso de foto pronta pode chegar em outra thread
    std::atomic<int> expectedDownloads{ 0 };
    std::atomic<int> completedDownloads{ 0 };

    std::vector<CameraContext> cameras;

    // Conexões em andamento (MonitorTick)
    struct PendingConnection
    {
        CameraContext candidate;
        std::string slotName;  // câmera que caiu e será substituída ("" = câmera nova)
        std::chrono::steady_clock::time_point start;
        std::chrono::steady_clock::time_point finish;
        std::atomic<bool> done{ false };
        EdsError result = EDS_ERR_OK;
        std::thread worker;
    };
    struct PortRetry
    {
        std::chrono::steady_clock::time_point retryAt;
        int failures = 0;
    };
    std::vector<std::unique_ptr<PendingConnection>> pending;
    std::vector<EdsCameraRef> retiredCameras;  // ver RetireCamera()
    std::vector<PendingConnectionInfo> queued;  // achadas, esperando a vez de conectar
    std::map<std::string, double> connectSeconds;  // tempo da última conexão de cada modelo

    // Conexões USB de câmeras que caíram: o SDK do Mac mantém a entrada antiga
    // ("fantasma"), e abrir a sessão nela pode travar o SDK. Só são tentadas de novo
    // depois que o sistema avisa que uma câmera foi conectada (ver MonitorTick): pelo
    // aviso do SDK ou por uma câmera Canon que aparece de novo no USB. Também entram as
    // conexões que falharam 3 vezes seguidas. "Restart camera connections" limpa a lista.
    struct Suspect
    {
        int seen = 0;       // avisos de "câmera conectada" já contados
        bool lost = false;  // era uma câmera conectada que caiu (a entrada pode ser a antiga)
    };
    std::map<std::string, Suspect> suspectPorts;
    std::atomic<int> camerasAdded{ 0 };            // avisos de "câmera conectada" do SDK
    std::atomic<bool> settingsChanged{ false };    // aviso de configuração mudada (TakeSettingsChanged)
    static EdsError EDSCALLBACK HandlePropertyEvent(EdsPropertyEvent event, EdsPropertyID property,
                                                    EdsUInt32 param, EdsVoid* context);
    std::atomic<long long> lastAddedMs{ 0 };       // quando chegou o último (ms desde o início)
    bool addedHandlerSet = false;
    int loggedAdded = 0;            // diagnóstico (Activity, detalhes técnicos)
    std::string lastScanPorts;      // diagnóstico: última lista do SDK registrada
    std::string lastUsbDevices;     // diagnóstico: últimos aparelhos Canon vistos no USB
    std::chrono::steady_clock::time_point lastUsbCheck{};
    struct UsbSeen
    {
        std::string location;
        int productId = 0;
        std::chrono::steady_clock::time_point firstSeen;
    };
    bool usbAvailable = false;                  // a lista do USB pôde ser lida
    std::vector<UsbSeen> usbSeen;               // câmeras Canon no USB agora
    bool usbListed = false;                     // a lista do USB já foi lida uma vez
    bool sdkScanned = false;                    // já houve uma procura no SDK
    std::vector<std::string> lastSdkModels;     // modelos na última lista do SDK
    std::map<int, std::string> usbModels;       // código do produto USB -> modelo (aprendido)
    bool usbModelsLoaded = false;
    void LearnUsbModels();                      // aprende pelo que sobra ao comparar USB e SDK
    static EdsError EDSCALLBACK HandleCameraAdded(EdsVoid* context);
    std::map<std::string, PortRetry> portRetry;  // conexões USB que falharam
    std::chrono::steady_clock::time_point lastHealthCheck{};
    std::chrono::steady_clock::time_point lastScan{};

    fs::path baseOutputDir;
    fs::path currentShotDir;
    bool verbose = true;

    void LogMessage(const std::string& message) const;
    void RenumberCameras();
    void RetireCamera(EdsCameraRef camera);
    int FirstFreeModelIndex(const std::string& baseShortName, bool countPending) const;
    EdsError OpenOneSession(CameraContext& cam, ConnectionObserver* observer);
    bool BuildCameraContext(EdsCameraRef camera, CameraContext& ctx) const;

    static EdsError EDSCALLBACK HandleObjectEvent(
        EdsObjectEvent event,
        EdsBaseRef object,
        EdsVoid* context
    );

    static std::string MakeTimestamp(bool withMilliseconds = true);
    static fs::path GetDefaultDownloadsFolder();

    static std::string FormatOrderPrefix(int value);

    static fs::path BuildUniqueOutputPath(
        const fs::path& dir,
        int orderPrefix,
        const std::string& cameraPrefix,
        const std::string& originalName
    );
};