#pragma once

#include "EDSDK.h"
#include "camera_context.h"
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

class CanonController
{
public:
    bool InitializeSdk();
    void TerminateSdk();

    bool DetectCameras();
    bool OpenSessions();
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
    bool PrepareSessionFolder(int sessionIndex);

private:
    int expectedDownloads = 0;
    int completedDownloads = 0;

    std::vector<CameraContext> cameras;
    fs::path baseOutputDir;
    fs::path currentShotDir;
    bool verbose = true;

    void LogMessage(const std::string& message) const;

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