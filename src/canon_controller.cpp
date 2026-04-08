#include "canon_controller.h"
#include "logger.h"

#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <unordered_map>
#include <thread>
#include <algorithm>

void CanonController::SetVerbose(bool enabled)
{
    verbose = enabled;
}

void CanonController::LogMessage(const std::string& message) const
{
    if (verbose)
    {
        Log(message);
    }
}

bool CanonController::InitializeSdk()
{
    EdsError err = EdsInitializeSDK();
    if (err != EDS_ERR_OK)
    {
        Log("Erro ao inicializar SDK.");
        return false;
    }

    if (baseOutputDir.empty())
    {
        baseOutputDir = GetDefaultDownloadsFolder() / "bullet-time";
    }

    fs::create_directories(baseOutputDir);

    return true;
}

void CanonController::TerminateSdk()
{
    LogMessage("SDK finalizada.");
    EdsTerminateSDK();
}

void CanonController::SetOutputFolder(const std::string& folderPath)
{
    baseOutputDir = folderPath;

    try
    {
        fs::create_directories(baseOutputDir);
        Log("\n📁 Pasta de saída definida: " + baseOutputDir.string());
    }
    catch (...)
    {
        Log("⚠️ Erro ao criar pasta de saída.");
    }
}

std::string CanonController::GetOutputFolder() const
{
    return baseOutputDir.string();
}

std::string CanonController::FormatOrderPrefix(int value)
{
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << value;
    return oss.str();
}

std::string CanonController::MakeTimestamp(bool withMilliseconds)
{
    using namespace std::chrono;

    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);

    std::tm tmLocal{};
    localtime_s(&tmLocal, &t);

    std::ostringstream oss;
    oss << std::put_time(&tmLocal, "%Y-%m-%d_%H-%M-%S");

    if (withMilliseconds)
    {
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        oss << "_" << std::setw(3) << std::setfill('0') << ms.count();
    }

    return oss.str();
}

fs::path CanonController::GetDefaultDownloadsFolder()
{
    char* userProfile = nullptr;
    size_t len = 0;

    if (_dupenv_s(&userProfile, &len, "USERPROFILE") == 0 && userProfile != nullptr)
    {
        fs::path result = fs::path(userProfile) / "Downloads";
        free(userProfile);
        return result;
    }

    return fs::current_path();
}

fs::path CanonController::BuildUniqueOutputPath(
    const fs::path& dir,
    int orderPrefix,
    const std::string& cameraPrefix,
    const std::string& originalName
)
{
    std::string safeOriginal = originalName.empty() ? "arquivo.bin" : originalName;

    for (char& c : safeOriginal)
    {
        if (c == '/' || c == '\\' || c == ':')
            c = '_';
    }

    std::string prefixText = FormatOrderPrefix(orderPrefix);
    std::string baseName = prefixText + "_" + cameraPrefix + "_" + safeOriginal;

    fs::path candidate = dir / baseName;

    if (!fs::exists(candidate))
        return candidate;

    fs::path originalPath(safeOriginal);
    std::string stem = originalPath.stem().string();
    std::string ext = originalPath.extension().string();

    int suffix = 2;
    while (true)
    {
        fs::path nextCandidate =
            dir / (prefixText + "_" + cameraPrefix + "_" + stem + "__" + std::to_string(suffix) + ext);

        if (!fs::exists(nextCandidate))
            return nextCandidate;

        suffix++;
    }
}

EdsError EDSCALLBACK CanonController::HandleObjectEvent(
    EdsObjectEvent event,
    EdsBaseRef object,
    EdsVoid* context
)
{
    CameraContext* ctx = static_cast<CameraContext*>(context);

    if (!ctx)
        return EDS_ERR_OK;

    if (event == kEdsObjectEvent_DirItemRequestTransfer && object != nullptr)
    {
        EdsDirectoryItemInfo dirInfo{};
        EdsError err = EdsGetDirectoryItemInfo((EdsDirectoryItemRef)object, &dirInfo);

        if (err != EDS_ERR_OK)
        {
            Log("[" + ctx->shortName + "] Erro em EdsGetDirectoryItemInfo.");
            EdsRelease(object);
            return err;
        }

        fs::create_directories(ctx->currentShotDir);

        std::string originalName = dirInfo.szFileName ? dirInfo.szFileName : "arquivo.bin";
        fs::path outPath = BuildUniqueOutputPath(
            ctx->currentShotDir,
            ctx->orderPrefix,
            ctx->shortName,
            originalName
        );

        EdsStreamRef stream = nullptr;
        err = EdsCreateFileStream(
            outPath.string().c_str(),
            kEdsFileCreateDisposition_CreateAlways,
            kEdsAccess_ReadWrite,
            &stream
        );

        if (err != EDS_ERR_OK)
        {
            Log("[" + ctx->shortName + "] Erro em EdsCreateFileStream.");
            EdsRelease(object);
            return err;
        }

        err = EdsDownload((EdsDirectoryItemRef)object, dirInfo.size, stream);
        if (err == EDS_ERR_OK)
            err = EdsDownloadComplete((EdsDirectoryItemRef)object);

        if (stream)
            EdsRelease(stream);

        if (err == EDS_ERR_OK)
        {
            Log("[" + ctx->shortName + "] Baixado: " + outPath.string());

            if (ctx->owner != nullptr)
            {
                ctx->owner->completedDownloads++;
            }
        }
        else
        {
            Log("[" + ctx->shortName + "] Erro no download.");
        }
    }

    if (object)
        EdsRelease(object);

    return EDS_ERR_OK;
}

bool CanonController::DetectCameras()
{
    cameras.clear();

    EdsCameraListRef cameraList = nullptr;
    EdsUInt32 cameraCount = 0;

    EdsError err = EdsGetCameraList(&cameraList);
    if (err != EDS_ERR_OK || cameraList == nullptr)
    {
        LogMessage("Erro ao obter lista de cameras.");
        return false;
    }

    err = EdsGetChildCount(cameraList, &cameraCount);
    if (err != EDS_ERR_OK)
    {
        LogMessage("Erro ao contar cameras.");
        EdsRelease(cameraList);
        return false;
    }

    LogMessage("Cameras encontradas: " + std::to_string(cameraCount));

    std::unordered_map<std::string, int> modelCounters;

    for (EdsUInt32 i = 0; i < cameraCount; i++)
    {
        EdsCameraRef camera = nullptr;
        err = EdsGetChildAtIndex(cameraList, i, &camera);
        if (err != EDS_ERR_OK || camera == nullptr)
        {
            LogMessage("Erro ao obter camera no indice " + std::to_string(i));
            continue;
        }

        EdsDeviceInfo deviceInfo{};
        err = EdsGetDeviceInfo(camera, &deviceInfo);
        if (err != EDS_ERR_OK)
        {
            LogMessage("Erro ao obter informacoes da camera " + std::to_string(i));
            EdsRelease(camera);
            continue;
        }

        CameraContext ctx;
        ctx.camera = camera;
        ctx.owner = this;
        ctx.connectionLost = false;
        ctx.modelName = deviceInfo.szDeviceDescription;

        if (ctx.modelName.find("Rebel T7") != std::string::npos)
            ctx.baseShortName = "T7";
        else if (ctx.modelName.find("Rebel SL3") != std::string::npos)
            ctx.baseShortName = "SL3";
        else
            ctx.baseShortName = "CAM";

        modelCounters[ctx.baseShortName]++;
        ctx.modelIndex = modelCounters[ctx.baseShortName];
        ctx.shortName = ctx.baseShortName + "_" + std::to_string(ctx.modelIndex);
        ctx.orderPrefix = static_cast<int>(cameras.size()) + 1;

        cameras.push_back(ctx);

        LogMessage(
            "Camera " + std::to_string(i) + ": " +
            ctx.modelName + " [" + ctx.shortName + "]"
        );
    }

    EdsRelease(cameraList);
    return !cameras.empty();
}

bool CanonController::OpenSessions()
{
    bool anySuccess = false;

    for (auto& cam : cameras)
    {
        if (cam.sessionOpen)
            continue;

        EdsError err = EdsOpenSession(cam.camera);
        LogMessage("OpenSession retorno [" + cam.shortName + "]: " + std::to_string(err));

        if (err == EDS_ERR_OK)
        {
            cam.sessionOpen = true;
            anySuccess = true;
            LogMessage("Sessão aberta: " + cam.shortName);

            err = EdsSetObjectEventHandler(
                cam.camera,
                kEdsObjectEvent_All,
                HandleObjectEvent,
                &cam
            );

            LogMessage("SetObjectEventHandler retorno [" + cam.shortName + "]: " + std::to_string(err));

            if (err == EDS_ERR_OK)
                LogMessage("Callback registrado: " + cam.shortName);
            else
                LogMessage("Erro ao registrar callback: " + cam.shortName);
        }
        else
        {
            LogMessage("Erro ao abrir sessão: " + cam.shortName);
        }
    }

    return anySuccess;
}

bool CanonController::SetSaveToHost()
{
    bool anySuccess = false;

    for (auto& cam : cameras)
    {
        if (!cam.sessionOpen)
            continue;

        EdsUInt32 saveTo = kEdsSaveTo_Host;
        EdsError err = EdsSetPropertyData(
            cam.camera,
            kEdsPropID_SaveTo,
            0,
            sizeof(saveTo),
            &saveTo
        );

        LogMessage("Set SaveTo_Host retorno [" + cam.shortName + "]: " + std::to_string(err));

        if (err == EDS_ERR_OK)
        {
            anySuccess = true;
            LogMessage("SaveTo_Host configurado: " + cam.shortName);
        }
        else
        {
            LogMessage("Erro em SaveTo_Host: " + cam.shortName);
        }
    }

    return anySuccess;
}

bool CanonController::SetCapacityForAll()
{
    bool anySuccess = false;

    for (auto& cam : cameras)
    {
        if (!cam.sessionOpen)
            continue;

        EdsCapacity capacity{};
        capacity.numberOfFreeClusters = 0x7FFFFFFF;
        capacity.bytesPerSector = 512;
        capacity.reset = 1;

        EdsError err = EdsSetCapacity(cam.camera, capacity);
        LogMessage("SetCapacity retorno [" + cam.shortName + "]: " + std::to_string(err));

        if (err == EDS_ERR_OK)
        {
            anySuccess = true;
            LogMessage("Capacity configurado: " + cam.shortName);
        }
        else
        {
            LogMessage("Erro em SetCapacity: " + cam.shortName);
        }
    }

    return anySuccess;
}

bool CanonController::ShootAll()
{
    expectedDownloads = 0;
    completedDownloads = 0;

    bool anySuccess = false;

    bool hasPreparedSessionDir = false;

    if (!cameras.empty())
    {
        hasPreparedSessionDir = !cameras.front().currentShotDir.empty();
    }

    if (hasPreparedSessionDir)
    {
        currentShotDir = cameras.front().currentShotDir;
    }
    else
    {
        currentShotDir = baseOutputDir / MakeTimestamp(false);
    }

    fs::create_directories(currentShotDir);
    Log("\nPasta da rodada: " + currentShotDir.string());

    for (auto& cam : cameras)
    {
        cam.currentShotDir = currentShotDir;

        if (cam.sessionOpen)
        {
            expectedDownloads++;
        }
    }

    Log("Esperando downloads de " + std::to_string(expectedDownloads) + " câmera(s)");

    // =========================
    // FASE 1: PRESS (todas)
    // =========================
    for (auto& cam : cameras)
    {
        if (!cam.sessionOpen)
            continue;

        EdsError errDown = EdsSendCommand(
            cam.camera,
            kEdsCameraCommand_PressShutterButton,
            kEdsCameraCommand_ShutterButton_Completely_NonAF
        );

        LogMessage("PressShutter DOWN [" + cam.shortName + "]: " + std::to_string(errDown));

        if (errDown == EDS_ERR_OK)
        {
            anySuccess = true;
        }
        else
        {
            LogMessage("Erro no DOWN: " + cam.shortName);
        }
    }

    // Pequena pausa para sincronizar melhor
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // =========================
    // FASE 2: RELEASE (todas)
    // =========================
    for (auto& cam : cameras)
    {
        if (!cam.sessionOpen)
            continue;

        EdsError errOff = EdsSendCommand(
            cam.camera,
            kEdsCameraCommand_PressShutterButton,
            kEdsCameraCommand_ShutterButton_OFF
        );

        LogMessage("PressShutter OFF [" + cam.shortName + "]: " + std::to_string(errOff));

        if (errOff == EDS_ERR_OK)
        {
            LogMessage("Disparo OK: " + cam.shortName);
        }
        else
        {
            LogMessage("Erro no OFF: " + cam.shortName);
        }
    }

    return anySuccess;
}

void CanonController::CloseSessions()
{
    for (auto& cam : cameras)
    {
        cam.sessionOpen = false;
        cam.camera = nullptr;
        cam.owner = nullptr;
        cam.connectionLost = true;
    }

    cameras.clear();
}

std::vector<CameraContext>& CanonController::GetCameras()
{
    return cameras;
}

void CanonController::PrintCameraOrder() const
{
    Log("ORDEM ATUAL DAS CÂMERAS");

    for (const auto& cam : cameras)
    {
        Log(
            FormatOrderPrefix(cam.orderPrefix) +
            " -> " +
            cam.shortName +
            " (" + cam.modelName + ")"
        );
    }
}

bool CanonController::SetCameraPrefix(const std::string& shortName, int newPrefix)
{
    if (newPrefix <= 0)
    {
        Log("Prefixo inválido: " + std::to_string(newPrefix));
        return false;
    }

    CameraContext* target = nullptr;
    CameraContext* existing = nullptr;

    for (auto& cam : cameras)
    {
        if (cam.shortName == shortName)
            target = &cam;

        if (cam.orderPrefix == newPrefix)
            existing = &cam;
    }

    if (target == nullptr)
    {
        Log("Câmera não encontrada: " + shortName);
        return false;
    }

    if (target->orderPrefix == newPrefix)
    {
        Log("Câmera " + shortName + " já está com o prefixo " + FormatOrderPrefix(newPrefix));
        return true;
    }

    int oldPrefix = target->orderPrefix;

    if (existing != nullptr && existing != target)
    {
        existing->orderPrefix = oldPrefix;
    }

    target->orderPrefix = newPrefix;

    Log(
        "Prefixo atualizado: " +
        shortName +
        " -> " +
        FormatOrderPrefix(newPrefix)
    );

    return true;
}

void CanonController::ResetCameraPrefixesSequential()
{
    std::vector<CameraContext*> ordered;
    ordered.reserve(cameras.size());

    for (auto& cam : cameras)
    {
        ordered.push_back(&cam);
    }

    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const CameraContext* a, const CameraContext* b)
        {
            return a->orderPrefix < b->orderPrefix;
        }
    );

    for (size_t i = 0; i < ordered.size(); i++)
    {
        ordered[i]->orderPrefix = static_cast<int>(i) + 1;
    }

    Log("\nPrefixos reorganizados sequencialmente.\n");
}

int CanonController::GetExpectedDownloads() const
{
    return expectedDownloads;
}

int CanonController::GetCompletedDownloads() const
{
    return completedDownloads;
}

void CanonController::RefreshCameraConnectionStatus()
{
    for (auto& cam : cameras)
    {
        if (cam.camera == nullptr)
        {
            cam.connectionLost = true;
            cam.sessionOpen = false;
            cam.lastError = -1;
            continue;
        }

        if (!cam.sessionOpen)
        {
            cam.connectionLost = true;
            cam.lastError = -2;
            continue;
        }

        EdsError err = EdsSendCommand(
            cam.camera,
            kEdsCameraCommand_ExtendShutDownTimer,
            0
        );

        if (err == EDS_ERR_OK)
        {
            cam.connectionLost = false;
            cam.lastError = 0;
        }
        else
        {
            cam.connectionLost = true;
            cam.sessionOpen = false;
            cam.lastError = err;

        }
    }
}

bool CanonController::PrepareSessionFolder(int sessionIndex)
{
    try
    {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);

        std::tm localTime{};
#ifdef _WIN32
        localtime_s(&localTime, &t);
#else
        localtime_r(&t, &localTime);
#endif

        std::ostringstream folderName;
        folderName << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S")
            << "_session_"
            << std::setw(2) << std::setfill('0') << sessionIndex;

        fs::path sessionDir = fs::path(GetOutputFolder()) / folderName.str();
        fs::create_directories(sessionDir);

        for (auto& cam : cameras)
        {
            cam.currentShotDir = sessionDir;
        }

        std::cout << "\n📁 Pasta da sessão: " << sessionDir.string() << "\n";
        return true;
    }
    catch (const std::exception& e)
    {
        std::cout << "\n⚠️ Erro ao criar pasta da sessão: " << e.what() << "\n";
        return false;
    }
}