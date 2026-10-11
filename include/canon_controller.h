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

    int GetExpectedDownloads() const;
    int GetCompletedDownloads() const;

    void RefreshCameraConnectionStatus();
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