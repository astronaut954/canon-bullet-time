#include "canon_controller.h"
#include "logger.h"
#include "platform/platform.h"
#include "sdk_diagnostics.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <unordered_map>
#include <thread>
#include <atomic>
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
        Log("Failed to initialize SDK.");
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
    LogMessage("SDK terminated.");
    EdsTerminateSDK();
    retiredCameras.clear();  // o SDK já liberou tudo
    suspectPorts.clear();    // SDK novo: sem entradas antigas
    addedHandlerSet = false;
}

void CanonController::RetireCamera(EdsCameraRef camera)
{
    // No Mac, o SDK avisa que uma câmera saiu na thread principal, no tempo dele,
    // e nesse aviso ainda usa o objeto da câmera. Se o app soltar (EdsRelease)
    // esse objeto antes do aviso chegar, o SDK lê memória já liberada e o app
    // fecha sozinho (visto com uma câmera reconectando e outra sendo desligada).
    // Por isso câmeras perdidas ou que falharam não são soltas durante o uso:
    // ficam guardadas aqui (poucos bytes cada) até o SDK ser desligado.
    if (camera != nullptr)
        retiredCameras.push_back(camera);
}

void CanonController::SetOutputFolder(const std::string& folderPath)
{
    baseOutputDir = folderPath;

    try
    {
        fs::create_directories(baseOutputDir);
        Log("\n📁 Output folder set to: " + baseOutputDir.string());
    }
    catch (...)
    {
        Log("⚠️  Failed to create output folder.");
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

    std::tm tmLocal = Platform::LocalTime(t);

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
    return Platform::DownloadsFolder();
}

fs::path CanonController::BuildUniqueOutputPath(
    const fs::path& dir,
    int orderPrefix,
    const std::string& cameraPrefix,
    const std::string& originalName
)
{
    std::string safeOriginal = originalName.empty() ? "file.bin" : originalName;

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
            Log("[" + ctx->shortName + "] EdsGetDirectoryItemInfo failed.");
            EdsRelease(object);
            return err;
        }

        fs::create_directories(ctx->currentShotDir);

        std::string originalName = dirInfo.szFileName ? dirInfo.szFileName : "file.bin";
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
            Log("[" + ctx->shortName + "] EdsCreateFileStream failed.");
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
            Log("[" + ctx->shortName + "] Downloaded: " + outPath.string());

            if (ctx->owner != nullptr)
            {
                ctx->owner->completedDownloads++;
            }
        }
        else
        {
            Log("[" + ctx->shortName + "] Download failed.");
        }
    }

    if (object)
        EdsRelease(object);

    return EDS_ERR_OK;
}

bool CanonController::DetectCameras()
{
    CloseSessions();

    EdsCameraListRef cameraList = nullptr;
    EdsUInt32 cameraCount = 0;

    // No Mac (macOS 13+), o SDK só enxerga as câmeras depois de processar eventos do sistema.
    Platform::ProcessEvents(1000);

    EdsError err = EdsGetCameraList(&cameraList);
    if (err != EDS_ERR_OK || cameraList == nullptr)
    {
        LogMessage("Failed to get camera list.");
        return false;
    }

    err = EdsGetChildCount(cameraList, &cameraCount);
    if (err != EDS_ERR_OK)
    {
        LogMessage("Failed to count cameras.");
        EdsRelease(cameraList);
        return false;
    }

    LogMessage("Cameras found: " + std::to_string(cameraCount));

    for (EdsUInt32 i = 0; i < cameraCount; i++)
    {
        EdsCameraRef camera = nullptr;
        err = EdsGetChildAtIndex(cameraList, i, &camera);
        if (err != EDS_ERR_OK || camera == nullptr)
        {
            LogMessage("Failed to get camera at index " + std::to_string(i));
            continue;
        }

        CameraContext ctx;
        if (!BuildCameraContext(camera, ctx))
        {
            LogMessage("Failed to get camera info for index " + std::to_string(i));
            EdsRelease(camera);
            continue;
        }

        cameras.push_back(ctx);
    }

    RenumberCameras();

    for (const auto& cam : cameras)
        LogMessage("Camera: " + cam.modelName + " [" + cam.shortName + "]");

    EdsRelease(cameraList);
    return !cameras.empty();
}

EdsError CanonController::OpenOneSession(CameraContext& cam, ConnectionObserver* observer)
{
    // Abre a sessão de uma câmera. Pode levar dezenas de segundos (no Mac, com câmera
    // recém-conectada), então roda numa thread auxiliar enquanto o observador recebe
    // avisos de andamento.
    if (observer)
        observer->OnConnecting(cam);

    const auto startTime = std::chrono::steady_clock::now();
    std::atomic<bool> done{ false };
    EdsError err = EDS_ERR_OK;
    EdsCameraRef cameraRef = cam.camera;

    std::thread worker([&]()
    {
        Platform::BeginSdkThread();
        err = EdsOpenSession(cameraRef);
        Platform::EndSdkThread();
        done = true;
    });

    int tick = 0;
    while (!done)
    {
        Platform::ProcessEvents(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ++tick;

        if (observer && tick % 4 == 0)
        {
            const int secondsSoFar = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime).count());
            observer->OnConnectingProgress(cam, secondsSoFar, tick / 4);
        }
    }
    worker.join();

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();

    if (err == EDS_ERR_OK)
    {
        cam.sessionOpen = true;
        cam.connectionLost = false;
        if (observer)
            observer->OnConnected(cam, seconds);
    }
    else
    {
        cam.connectionLost = true;
        cam.lastError = err;
        if (observer)
            observer->OnConnectFailed(cam, static_cast<long>(err), seconds);
    }

    LogMessage("OpenSession return [" + cam.shortName + "]: " + std::to_string(err));
    return err;
}

bool CanonController::OpenSessions(ConnectionObserver* observer)
{
    bool anySuccess = false;

    for (auto& cam : cameras)
    {
        if (!cam.sessionOpen)
            OpenOneSession(cam, observer);

        if (cam.sessionOpen)
            anySuccess = true;
    }

    return anySuccess;
}

CanonController::~CanonController()
{
    // Conexões ainda em andamento ao fechar: o SDK não permite interromper.
    // As threads são soltas (e os dados delas mantidos) porque o processo está terminando.
    for (auto& p : pending)
    {
        if (p->worker.joinable())
            p->worker.detach();
        (void)p.release();
    }
}

EdsError EDSCALLBACK CanonController::HandleCameraAdded(EdsVoid* context)
{
    // Chega pelo SDK (no Mac, na thread da janela): só anota
    auto* self = static_cast<CanonController*>(context);
    self->lastAddedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    ++self->camerasAdded;
    return EDS_ERR_OK;
}

namespace
{
    // Identifica uma entrada da lista do SDK pelo modelo e pela porta. Dado observado
    // no Mac: a "porta" não é única; depois de religada, a T7 apareceu com a mesma
    // porta da SL3 conectada e o app achou que ela já estava conectada.
    std::string EntryKey(const CameraContext& cam)
    {
        return cam.baseShortName + "@" + cam.portName;
    }
}

bool CanonController::MonitorTick(ConnectionObserver* observer)
{
    // Dado observado no Mac: enquanto uma câmera está abrindo a sessão
    // (EdsOpenSession), o SDK da Canon fica travado para qualquer outra chamada,
    // e a T7 pode levar dezenas de segundos para responder. Por isso:
    //   - só uma câmera conecta por vez (numa thread própria);
    //   - enquanto ela conecta, esta função não chama o SDK, para a janela
    //     continuar mostrando o andamento em vez de ficar parada esperando.
    using Clock = std::chrono::steady_clock;
    using std::chrono::seconds;
    bool listChanged = false;
    bool connectedChanged = false;

    // No Mac, o SDK só atualiza a lista e as conexões processando eventos do sistema
    Platform::ProcessEvents(50);

    if (!addedHandlerSet && pending.empty())
    {
        addedHandlerSet = true;
        EdsSetCameraAddedHandler(HandleCameraAdded, this);
    }

    // 1) Conexão que terminou
    for (auto it = pending.begin(); it != pending.end();)
    {
        PendingConnection& p = **it;
        if (!p.done)
        {
            ++it;
            continue;
        }
        p.worker.join();

        const double secs = std::chrono::duration<double>(p.finish - p.start).count();
        CameraContext cam = p.candidate;
        connectSeconds[cam.baseShortName] = secs;
        LogMessage("OpenSession return [" + cam.shortName + "]: " + std::to_string(p.result));

        if (p.result == EDS_ERR_OK)
        {
            portRetry.erase(EntryKey(cam));
            suspectPorts.erase(EntryKey(cam));
            cam.sessionOpen = true;
            cam.connectionLost = false;
            cam.lastError = 0;

            CameraContext* slot = nullptr;
            if (!p.slotName.empty())
                for (auto& c : cameras)
                    if (!c.sessionOpen && c.shortName == p.slotName) { slot = &c; break; }

            if (slot)
            {
                if (slot->camera != nullptr)
                    RetireCamera(slot->camera);
                *slot = cam;
                slot->owner = this;
                if (observer)
                    observer->OnConnected(*slot, secs);
            }
            else
            {
                // Câmera nova: menor número livre do modelo (T7_1, T7_2...) e último lugar na ordem
                int maxOrder = 0;
                for (const auto& c : cameras)
                    maxOrder = std::max(maxOrder, c.orderPrefix);
                cam.modelIndex = FirstFreeModelIndex(cam.baseShortName, false);
                cam.shortName = cam.baseShortName + "_" + std::to_string(cam.modelIndex);
                cam.orderPrefix = maxOrder + 1;
                cam.owner = this;
                cameras.push_back(cam);
                if (observer)
                    observer->OnConnected(cameras.back(), secs);
            }
            listChanged = connectedChanged = true;
        }
        else
        {
            if (cam.camera != nullptr)
                RetireCamera(cam.camera);

            // Conexão suspeita que falhou devagar (pode ser a entrada antiga, que trava):
            // só tenta de novo depois de outro aviso de conexão. Se falhou rápido, com
            // erro, segue as tentativas normais abaixo (dado observado: a T7 religada
            // falhou uma vez com 8195 e ficou de lado até reiniciar o app).
            auto suspect = suspectPorts.find(EntryKey(cam));
            if (suspect != suspectPorts.end())
            {
                if (secs >= 10.0)
                    suspect->second.seen = camerasAdded.load();
                else
                    suspectPorts.erase(suspect);
            }

            // Espera cada vez mais antes de tentar a mesma conexão (ex.: entrada
            // "fantasma" que o SDK do Mac mantém depois de a câmera sair)
            auto& retry = portRetry[EntryKey(cam)];
            ++retry.failures;
            static const int kWait[] = { 5, 10, 15, 30, 60 };
            const int waitSeconds = kWait[std::min(retry.failures, 5) - 1];
            retry.retryAt = Clock::now() + seconds(waitSeconds);

            // Três falhas seguidas: para de insistir até uma câmera ser conectada
            // (cada tentativa pode deixar o SDK ocupado por muito tempo)
            if (retry.failures >= 3 && suspectPorts.count(EntryKey(cam)) == 0)
            {
                suspectPorts[EntryKey(cam)] = Suspect{ camerasAdded.load(), false };
                retry.retryAt = Clock::now();
                LogMessage("Connection kept aside until a camera is plugged in: " + cam.portName);
            }

            cam.lastError = p.result;
            if (observer && retry.failures == 1)
                observer->OnConnectFailed(cam, static_cast<long>(p.result), secs);
            listChanged = true;
        }

        it = pending.erase(it);
        // A próxima da fila é escolhida de novo na próxima procura
        lastScan = {};
    }

    if (connectedChanged)
    {
        // Endereços na lista podem ter mudado: registra de novo os avisos de foto
        RegisterEventHandlers();
        SetSaveToHost();
        SetCapacityForAll();
    }

    // Uma câmera ainda conectando: o SDK está ocupado, não chamar mais nada agora
    if (!pending.empty())
        return listChanged;

    const auto now = Clock::now();

    // 2) Câmeras conectadas que pararam de responder (desligadas ou desconectadas)
    if (now - lastHealthCheck >= seconds(2))
    {
        lastHealthCheck = now;
        for (auto& cam : cameras)
        {
            // Marcada como perdida durante um disparo (fotos que não chegaram)
            if (!cam.sessionOpen && cam.camera != nullptr)
            {
                RetireCamera(cam.camera);
                cam.camera = nullptr;
                cam.connectionLost = true;
                suspectPorts[EntryKey(cam)] = Suspect{ camerasAdded.load(), true };
                listChanged = true;
                LogMessage("Camera lost during the shot: " + cam.shortName + " (" + cam.portName + ")");
                continue;
            }
            if (!cam.sessionOpen || cam.camera == nullptr)
                continue;

            if (EdsSendCommand(cam.camera, kEdsCameraCommand_ExtendShutDownTimer, 0) != EDS_ERR_OK)
            {
                RetireCamera(cam.camera);
                cam.camera = nullptr;
                cam.sessionOpen = false;
                cam.connectionLost = true;
                listChanged = true;
                // A entrada antiga pode continuar na lista do SDK: não abrir de novo
                // até uma câmera ser conectada (ver HandleCameraAdded)
                suspectPorts[EntryKey(cam)] = Suspect{ camerasAdded.load(), true };
                LogMessage("Camera stopped responding: " + cam.shortName + " (" + cam.portName + ")");
            }
        }
    }

    // Diagnóstico: aviso do sistema de que uma câmera foi conectada
    if (const int added = camerasAdded.load(); added != loggedAdded)
    {
        loggedAdded = added;
        LogMessage("System reported a camera plugged in (" + std::to_string(added) + ")");
    }

    // 3) Câmeras na lista do SDK que ainda não estão conectadas
    if (now - lastScan < seconds(1))
        return listChanged;
    lastScan = now;

    // Diagnóstico: aparelhos Canon que o sistema vê no USB (sem o SDK), quando muda.
    // Serve para saber se a T7 aparece no USB antes de aparecer no SDK.
    {
        std::string usb;
        for (const auto& d : Platform::CanonUsbDevices())
            usb += (usb.empty() ? "" : ", ") + d;
        if (usb.empty())
            usb = "none";
        if (usb != lastUsbDevices)
        {
            lastUsbDevices = usb;
            LogMessage("USB Canon devices: " + usb);
        }
    }
    const auto scanStart = Clock::now();
    std::string scanPorts;   // diagnóstico: o que o SDK listou nesta procura

    const size_t queuedBefore = queued.size();
    queued.clear();

    struct Found
    {
        CameraContext candidate;
        std::string slotName;
        bool firstAttempt = true;
        bool suspect = false;   // conexão de uma câmera que caiu (pode ser a entrada antiga)
    };
    std::vector<Found> found;

    const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const bool recentlyAdded = nowMs - lastAddedMs.load() < 3000;  // espera a lista assentar

    EdsCameraListRef cameraList = nullptr;
    if (EdsGetCameraList(&cameraList) != EDS_ERR_OK || cameraList == nullptr)
        return listChanged || queuedBefore > 0;

    EdsUInt32 count = 0;
    EdsGetChildCount(cameraList, &count);

    for (EdsUInt32 i = 0; i < count; i++)
    {
        EdsCameraRef ref = nullptr;
        if (EdsGetChildAtIndex(cameraList, i, &ref) != EDS_ERR_OK || ref == nullptr)
            continue;

        CameraContext info;
        const auto infoStart = Clock::now();
        const bool infoOk = BuildCameraContext(ref, info);
        const double infoSecs = std::chrono::duration<double>(Clock::now() - infoStart).count();
        if (infoSecs >= 1.0)
            LogMessage("Reading camera info took " + std::to_string(static_cast<int>(infoSecs)) + " s"
                + (infoOk ? " (" + info.modelName + ", " + info.portName + ")" : ""));
        if (!infoOk)
        {
            scanPorts += " [unreadable]";
            EdsRelease(ref);
            continue;
        }

        const bool alreadyConnected = std::any_of(cameras.begin(), cameras.end(),
            [&](const CameraContext& c) { return c.sessionOpen && EntryKey(c) == EntryKey(info); });
        const bool alreadyFound = std::any_of(found.begin(), found.end(),
            [&](const Found& f) { return EntryKey(f.candidate) == EntryKey(info); });
        auto retry = portRetry.find(EntryKey(info));
        const bool waitingRetry = retry != portRetry.end() && now < retry->second.retryAt;

        // Conexão de uma câmera que caiu: só depois de o sistema avisar que uma
        // câmera foi conectada (e a lista ter tido tempo de assentar)
        auto suspect = suspectPorts.find(EntryKey(info));
        const bool suspectWaiting = suspect != suspectPorts.end()
            && (camerasAdded.load() <= suspect->second.seen || recentlyAdded);

        scanPorts += " " + info.baseShortName + "@" + info.portName
            + (alreadyConnected ? ":connected" : waitingRetry ? ":retry-wait" : suspectWaiting ? ":kept-aside" : "");
        if (alreadyConnected || alreadyFound || waitingRetry || suspectWaiting)
        {
            EdsRelease(ref);
            continue;
        }

        // Lugar a ocupar: mesma conexão USB, ou a câmera do mesmo modelo que caiu
        auto taken = [&](const CameraContext& c)
        {
            return std::any_of(found.begin(), found.end(), [&](const Found& f) { return f.slotName == c.shortName; });
        };
        CameraContext* slot = nullptr;
        for (auto& c : cameras)
            if (!c.sessionOpen && c.portName == info.portName && c.baseShortName == info.baseShortName && !taken(c)) { slot = &c; break; }
        if (!slot)
            for (auto& c : cameras)
                if (!c.sessionOpen && c.baseShortName == info.baseShortName && !taken(c)) { slot = &c; break; }

        Found f;
        f.firstAttempt = retry == portRetry.end() || retry->second.failures == 0;
        f.suspect = suspect != suspectPorts.end() && suspect->second.lost;
        CameraContext& candidate = f.candidate;
        candidate = slot ? *slot : info;
        candidate.camera = ref;
        candidate.portName = info.portName;
        candidate.modelName = info.modelName;
        candidate.onCompatibilityList = info.onCompatibilityList;
        candidate.sessionOpen = false;
        candidate.owner = this;

        if (slot)
        {
            f.slotName = slot->shortName;
        }
        else
        {
            // Nome provisório: menor número livre, contando as que já estão na fila
            for (int index = 1;; ++index)
            {
                const bool used = std::any_of(cameras.begin(), cameras.end(),
                    [&](const CameraContext& c) { return c.baseShortName == candidate.baseShortName && c.modelIndex == index; })
                    || std::any_of(found.begin(), found.end(),
                    [&](const Found& o) { return o.slotName.empty() && o.candidate.baseShortName == candidate.baseShortName && o.candidate.modelIndex == index; });
                if (!used)
                {
                    candidate.modelIndex = index;
                    break;
                }
            }
            candidate.shortName = candidate.baseShortName + "_" + std::to_string(candidate.modelIndex);
            candidate.orderPrefix = 0;
        }
        found.push_back(f);
    }
    EdsRelease(cameraList);

    // Diagnóstico: registra a lista do SDK quando ela muda, e procuras demoradas
    const double scanSecs = std::chrono::duration<double>(Clock::now() - scanStart).count();
    if (scanPorts != lastScanPorts)
    {
        lastScanPorts = scanPorts;
        LogMessage("SDK camera list: " + std::to_string(count) + (scanPorts.empty() ? "" : " -" + scanPorts));
    }
    if (scanSecs >= 1.0)
        LogMessage("Camera search took " + std::to_string(static_cast<int>(scanSecs)) + " s");

    // Uma conexão suspeita só é tentada se for a única chance de recuperar uma câmera
    // que caiu: se apareceu outra entrada do mesmo modelo, a câmera voltou por ela
    // (a suspeita é a entrada antiga); se nenhuma câmera desse modelo está faltando,
    // não há o que recuperar. Nos dois casos, espera o próximo aviso de conexão.
    for (auto it = found.begin(); it != found.end();)
    {
        if (!it->suspect)
        {
            ++it;
            continue;
        }
        const std::string& model = it->candidate.baseShortName;
        const bool otherEntry = std::any_of(found.begin(), found.end(),
            [&](const Found& o) { return !o.suspect && o.candidate.baseShortName == model; });
        const bool missing = !it->slotName.empty();
        if (otherEntry || !missing)
        {
            suspectPorts[EntryKey(it->candidate)].seen = camerasAdded.load();
            EdsRelease(it->candidate.camera);
            it = found.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Sem as suspeitas, uma câmera que voltou por outra conexão ocupa o lugar livre
    for (auto& f : found)
    {
        if (!f.slotName.empty())
            continue;
        for (auto& c : cameras)
        {
            const bool taken = std::any_of(found.begin(), found.end(),
                [&](const Found& o) { return o.slotName == c.shortName; });
            if (c.sessionOpen || c.baseShortName != f.candidate.baseShortName || taken)
                continue;
            CameraContext slotted = c;
            slotted.camera = f.candidate.camera;
            slotted.portName = f.candidate.portName;
            slotted.modelName = f.candidate.modelName;
            slotted.onCompatibilityList = f.candidate.onCompatibilityList;
            slotted.sessionOpen = false;
            slotted.owner = this;
            f.candidate = slotted;
            f.slotName = c.shortName;
            break;
        }
    }

    if (found.empty())
        return listChanged || queuedBefore > 0;

    // Primeiro a que conectou mais rápido da última vez (modelo ainda não visto
    // conta como rápido): assim uma câmera lenta não segura as outras sempre
    auto expected = [&](const Found& f)
    {
        auto it = connectSeconds.find(f.candidate.baseShortName);
        return it == connectSeconds.end() ? 0.0 : it->second;
    };
    std::stable_sort(found.begin(), found.end(),
        [&](const Found& a, const Found& b) { return expected(a) < expected(b); });

    // As outras esperam a vez (a referência é solta agora, antes de o SDK ficar ocupado)
    for (size_t i = 1; i < found.size(); ++i)
    {
        PendingConnectionInfo info;
        info.name = found[i].candidate.shortName;
        info.model = found[i].candidate.modelName;
        info.order = found[i].slotName.empty() ? 0 : found[i].candidate.orderPrefix;
        info.reconnecting = !found[i].slotName.empty();
        info.retrying = !found[i].firstAttempt;
        info.queued = true;
        queued.push_back(info);
        EdsRelease(found[i].candidate.camera);
    }

    Found& next = found.front();
    auto job = std::make_unique<PendingConnection>();
    job->candidate = next.candidate;
    job->slotName = next.slotName;

    if (observer && next.firstAttempt)
        observer->OnConnecting(job->candidate);
    LogMessage("Connecting: " + job->candidate.shortName + " (" + job->candidate.portName + ")");

    job->start = Clock::now();
    PendingConnection* raw = job.get();
    job->worker = std::thread([raw]()
    {
        Platform::BeginSdkThread();
        raw->result = EdsOpenSession(raw->candidate.camera);
        raw->finish = std::chrono::steady_clock::now();
        Platform::EndSdkThread();
        raw->done = true;
    });
    pending.push_back(std::move(job));
    return true;
}

int CanonController::FirstFreeModelIndex(const std::string& baseShortName, bool countPending) const
{
    for (int index = 1;; ++index)
    {
        const bool used = std::any_of(cameras.begin(), cameras.end(),
            [&](const CameraContext& c) { return c.baseShortName == baseShortName && c.modelIndex == index; })
            || (countPending && std::any_of(pending.begin(), pending.end(),
            [&](const std::unique_ptr<PendingConnection>& p)
            { return p->slotName.empty() && p->candidate.baseShortName == baseShortName && p->candidate.modelIndex == index; }));
        if (!used)
            return index;
    }
}

std::vector<PendingConnectionInfo> CanonController::GetPendingConnections() const
{
    std::vector<PendingConnectionInfo> list;
    const auto now = std::chrono::steady_clock::now();
    for (const auto& p : pending)
    {
        PendingConnectionInfo info;
        info.name = p->candidate.shortName;
        info.model = p->candidate.modelName;
        info.order = p->slotName.empty() ? 0 : p->candidate.orderPrefix;
        info.reconnecting = !p->slotName.empty();
        info.seconds = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - p->start).count());
        auto retry = portRetry.find(EntryKey(p->candidate));
        info.retrying = retry != portRetry.end() && retry->second.failures > 0;
        list.push_back(info);
    }
    list.insert(list.end(), queued.begin(), queued.end());
    return list;
}

void CanonController::ResetConnectionHistory()
{
    lastScanPorts.clear();
    lastUsbDevices.clear();
    portRetry.clear();
    queued.clear();
    lastScan = {};
    lastHealthCheck = {};
}

bool CanonController::HasPendingConnections() const
{
    return !pending.empty();
}

void CanonController::WaitPendingConnections()
{
    for (auto& p : pending)
    {
        p->worker.join();
        if (p->result == EDS_ERR_OK)
            EdsCloseSession(p->candidate.camera);
        if (p->candidate.camera != nullptr)
            RetireCamera(p->candidate.camera);
    }
    pending.clear();
    queued.clear();
    portRetry.clear();
}

bool CanonController::BuildCameraContext(EdsCameraRef camera, CameraContext& ctx) const
{
    EdsDeviceInfo deviceInfo{};
    if (EdsGetDeviceInfo(camera, &deviceInfo) != EDS_ERR_OK)
        return false;

    ctx.camera = camera;
    ctx.owner = const_cast<CanonController*>(this);
    ctx.connectionLost = false;
    ctx.modelName = deviceInfo.szDeviceDescription;
    ctx.portName = deviceInfo.szPortName;
    ctx.onCompatibilityList = IsOnCompatibilityList(ctx.modelName);

    if (ctx.modelName.find("Rebel T7") != std::string::npos)
        ctx.baseShortName = "T7";
    else if (ctx.modelName.find("Rebel SL3") != std::string::npos)
        ctx.baseShortName = "SL3";
    else
        ctx.baseShortName = "CAM";

    return true;
}

void CanonController::RenumberCameras()
{
    // Nomes (T7_1, T7_2...) e ordem (01, 02...) sempre seguidos, sem buracos.
    std::unordered_map<std::string, int> modelCounters;
    for (size_t i = 0; i < cameras.size(); ++i)
    {
        auto& cam = cameras[i];
        cam.modelIndex = ++modelCounters[cam.baseShortName];
        cam.shortName = cam.baseShortName + "_" + std::to_string(cam.modelIndex);
        cam.orderPrefix = static_cast<int>(i) + 1;
    }
}

std::set<std::string> CanonController::GetFailedCameraPorts() const
{
    std::set<std::string> ports;
    for (const auto& cam : cameras)
    {
        if (!cam.sessionOpen)
            ports.insert(cam.portName);
    }
    return ports;
}

std::vector<std::string> CanonController::ConnectNewCameras(std::set<std::string>& ignoredPorts, ConnectionObserver* observer)
{
    // Procura câmeras novas SEM desconectar as que já estão funcionando.
    // Entradas já conhecidas como falhas (ex.: "fantasmas" que o SDK do Mac mantém
    // depois de uma câmera ser desconectada) são ignoradas pela conexão USB.
    Platform::ProcessEvents(1000);

    EdsCameraListRef cameraList = nullptr;
    if (EdsGetCameraList(&cameraList) != EDS_ERR_OK || cameraList == nullptr)
        return {};

    EdsUInt32 cameraCount = 0;
    EdsGetChildCount(cameraList, &cameraCount);

    size_t added = 0;
    for (EdsUInt32 i = 0; i < cameraCount; i++)
    {
        EdsCameraRef camera = nullptr;
        if (EdsGetChildAtIndex(cameraList, i, &camera) != EDS_ERR_OK || camera == nullptr)
            continue;

        CameraContext ctx;
        const bool known = !BuildCameraContext(camera, ctx)
            || ignoredPorts.count(ctx.portName) > 0
            || std::any_of(cameras.begin(), cameras.end(),
                   [&](const CameraContext& c) { return c.portName == ctx.portName; });

        if (known)
        {
            EdsRelease(camera);
            continue;
        }

        cameras.push_back(ctx);
        ++added;
    }
    EdsRelease(cameraList);

    if (added == 0)
        return {};

    RenumberCameras();

    std::set<std::string> portsBefore;
    for (const auto& c : cameras)
        if (c.sessionOpen)
            portsBefore.insert(c.portName);

    if (observer)
        observer->OnNewCamerasFound();
    OpenSessions(observer);

    for (const auto& port : GetFailedCameraPorts())
        ignoredPorts.insert(port);
    DropFailedCameras();

    // Devolve o modelo (T7, SL3...) de cada câmera que acabou de conectar
    std::vector<std::string> connectedModels;
    for (const auto& c : cameras)
        if (portsBefore.count(c.portName) == 0)
            connectedModels.push_back(c.baseShortName);
    return connectedModels;
}

std::vector<std::string> CanonController::GetFailedCameraNames() const
{
    std::vector<std::string> names;
    for (const auto& cam : cameras)
    {
        if (!cam.sessionOpen)
            names.push_back(cam.shortName);
    }
    return names;
}

void CanonController::DropFailedCameras()
{
    // Tira da lista as câmeras que não conectaram (ex.: no Mac, o SDK às vezes
    // mantém uma câmera já desconectada) e renumera as que sobraram:
    // T7_1 (falhou), T7_2, T7_3  ->  T7_1, T7_2
    for (auto& cam : cameras)
    {
        if (!cam.sessionOpen && cam.camera != nullptr)
        {
            EdsRelease(cam.camera);
            cam.camera = nullptr;
        }
    }

    cameras.erase(
        std::remove_if(cameras.begin(), cameras.end(),
            [](const CameraContext& cam) { return !cam.sessionOpen; }),
        cameras.end()
    );

    RenumberCameras();
}

void CanonController::RegisterEventHandlers()
{
    // Feito só com a lista final: o SDK guarda o endereço de cada câmera da lista.
    for (auto& cam : cameras)
    {
        if (!cam.sessionOpen)
            continue;

        LogMessage("Session opened: " + cam.shortName);

        EdsError err = EdsSetObjectEventHandler(
            cam.camera,
            kEdsObjectEvent_All,
            HandleObjectEvent,
            &cam
        );

        LogMessage("SetObjectEventHandler return [" + cam.shortName + "]: " + std::to_string(err));

        if (err == EDS_ERR_OK)
            LogMessage("Callback registered: " + cam.shortName);
        else
            LogMessage("Failed to register callback: " + cam.shortName);
    }
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

        LogMessage("Set SaveTo_Host return [" + cam.shortName + "]: " + std::to_string(err));

        if (err == EDS_ERR_OK)
        {
            anySuccess = true;
            LogMessage("SaveTo_Host configured: " + cam.shortName);
        }
        else
        {
            LogMessage("Failed to set SaveTo_Host: " + cam.shortName);
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
        LogMessage("SetCapacity return [" + cam.shortName + "]: " + std::to_string(err));

        if (err == EDS_ERR_OK)
        {
            anySuccess = true;
            LogMessage("Capacity configured: " + cam.shortName);
        }
        else
        {
            LogMessage("Failed to set capacity: " + cam.shortName);
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
    Log("\nRound folder: " + currentShotDir.string());

    for (auto& cam : cameras)
    {
        cam.currentShotDir = currentShotDir;

        if (cam.sessionOpen)
        {
            expectedDownloads++;
        }
    }

    Log("Waiting for downloads from " + std::to_string(expectedDownloads.load()) + " camera(s)");

    // =========================
    // PHASE 1: PRESS (all)
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

        cam.lastShotError = static_cast<long>(errDown);

        if (errDown == EDS_ERR_OK)
        {
            anySuccess = true;
        }
        else
        {
            LogMessage("Press DOWN failed: " + cam.shortName);
            // Câmera que não disparou não vai mandar foto: não esperar por ela
            if (expectedDownloads > 0)
                expectedDownloads--;
        }
    }

    // Small pause for better synchronization
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // =========================
    // PHASE 2: RELEASE (all)
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

        if (errOff == EDS_ERR_OK && cam.lastShotError == 0)
        {
            LogMessage("Shot triggered: " + cam.shortName);
        }
        else
        {
            LogMessage("Release OFF failed: " + cam.shortName);
        }
    }

    return anySuccess;
}

void CanonController::CloseSessions()
{
    for (auto& cam : cameras)
    {
        if (cam.camera != nullptr)
        {
            if (cam.sessionOpen)
            {
                EdsError err = EdsCloseSession(cam.camera);
                LogMessage("CloseSession return [" + cam.shortName + "]: " + std::to_string(err));
            }

            EdsError releaseErr = EdsRelease(cam.camera);
            LogMessage("Release camera return [" + cam.shortName + "]: " + std::to_string(releaseErr));
        }

        cam.sessionOpen = false;
        cam.camera = nullptr;
        cam.owner = nullptr;
        cam.connectionLost = false;
    }

    cameras.clear();
}

std::vector<CameraContext>& CanonController::GetCameras()
{
    return cameras;
}

void CanonController::PrintCameraOrder() const
{
    Log("CURRENT CAMERA ORDER");

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
        Log("Invalid prefix: " + std::to_string(newPrefix));
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
        Log("Camera not found: " + shortName);
        return false;
    }

    if (target->orderPrefix == newPrefix)
    {
        Log("Camera " + shortName + " is already using prefix " + FormatOrderPrefix(newPrefix));
        return true;
    }

    int oldPrefix = target->orderPrefix;

    if (existing != nullptr && existing != target)
    {
        existing->orderPrefix = oldPrefix;
    }

    target->orderPrefix = newPrefix;

    Log(
        "Prefix updated: " +
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

    Log("\nPrefixes reorganized sequentially.\n");
}

void CanonController::SetCameraOrder(const std::vector<std::string>& shortNames)
{
    std::vector<CameraContext*> ordered;
    for (const auto& name : shortNames)
    {
        for (auto& cam : cameras)
        {
            if (cam.shortName == name && std::find(ordered.begin(), ordered.end(), &cam) == ordered.end())
                ordered.push_back(&cam);
        }
    }

    std::vector<CameraContext*> rest;
    for (auto& cam : cameras)
    {
        if (std::find(ordered.begin(), ordered.end(), &cam) == ordered.end())
            rest.push_back(&cam);
    }
    std::sort(rest.begin(), rest.end(),
        [](const CameraContext* a, const CameraContext* b) { return a->orderPrefix < b->orderPrefix; });
    ordered.insert(ordered.end(), rest.begin(), rest.end());

    for (size_t i = 0; i < ordered.size(); ++i)
        ordered[i]->orderPrefix = static_cast<int>(i) + 1;

    LogMessage("Camera order updated.");
}

int CanonController::GetExpectedDownloads() const
{
    return expectedDownloads.load();
}

int CanonController::GetCompletedDownloads() const
{
    return completedDownloads.load();
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

bool CanonController::PrepareSessionFolder(int sessionIndex, int totalSessions, const std::string& shotId)
{
    try
    {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);

        std::tm localTime = Platform::LocalTime(t);

        std::ostringstream folderName;
        folderName << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S");

        if (totalSessions > 1)
        {
            folderName << "_"
                << shotId
                << "_session_"
                << std::setw(2) << std::setfill('0') << sessionIndex;
        }

        fs::path sessionDir = fs::path(GetOutputFolder()) / folderName.str();
        fs::create_directories(sessionDir);

        for (auto& cam : cameras)
        {
            cam.currentShotDir = sessionDir;
        }

        Log("\n📁 Session folder: " + sessionDir.string());
        return true;
    }
    catch (const std::exception& e)
    {
        Log(std::string("\n⚠️  Failed to create session folder: ") + e.what());
        return false;
    }
}