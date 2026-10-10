#pragma once

#include "EDSDK.h"
#include "camera_context.h"
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

class CanonController
{
public:
    bool InitializeSdk();
    void TerminateSdk();

    bool DetectCameras();
    bool OpenSessions(ConnectionObserver* observer = nullptr);
    std::vector<std::string> GetFailedCameraNames() const;
    std::set<std::string> GetFailedCameraPorts() const;
    std::vector<std::string> ConnectNewCameras(std::set<std::string>& ignoredPorts, ConnectionObserver* observer = nullptr);
    void DropFailedCameras();
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
    int expectedDownloads = 0;
    int completedDownloads = 0;

    std::vector<CameraContext> cameras;
    fs::path baseOutputDir;
    fs::path currentShotDir;
    bool verbose = true;

    void LogMessage(const std::string& message) const;
    void RenumberCameras();
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