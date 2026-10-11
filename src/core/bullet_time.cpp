#include "bullet_time.h"
#include "audio_player.h"
#include "path_utils.h"
#include "platform/platform.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <set>
#include <thread>

BulletTime::BulletTime(CanonController& controller, BulletTimeListener& listener)
    : controller(controller), listener(listener)
{
}

void BulletTime::PumpEvents(int totalMs, int stepMs)
{
    int elapsed = 0;

    while (elapsed < totalMs)
    {
        EdsGetEvent();
        Platform::ProcessEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
        elapsed += stepMs;
    }
}

BulletTime::ConnectResult BulletTime::ConnectCameras()
{
    // Quando uma câmera falha ao conectar (ex.: recém-reconectada, ainda não liberada
    // pelo sistema), as câmeras que já conectaram continuam conectadas e o programa
    // procura sozinho, por até AUTO_RETRY_SECONDS, a câmera que faltou.
    constexpr int AUTO_RETRY_SECONDS = 60;
    constexpr int RETRY_INTERVAL_MS = 3000;
    constexpr int ANIMATION_STEP_MS = 400;

    controller.SetVerbose(false);

    PumpEvents(500);

    if (!controller.DetectCameras())
    {
        controller.SetVerbose(true);
        return ConnectResult::NoCameras;
    }

    PumpEvents(300);
    controller.OpenSessions(&listener);

    const auto failed = controller.GetFailedCameraNames();
    if (!failed.empty())
    {
        std::set<std::string> ignoredPorts = controller.GetFailedCameraPorts();

        // Modelos que faltam (os nomes T7_1, T7_2... mudam ao renumerar; o modelo não)
        std::vector<std::string> missingModels;
        for (const auto& name : failed)
            missingModels.push_back(name.substr(0, name.rfind('_')));

        controller.DropFailedCameras();
        listener.OnCamerasFailed(failed);

        while (true)
        {
            listener.OnRetryStarted(AUTO_RETRY_SECONDS);
            const auto retryStart = std::chrono::steady_clock::now();

            while (!missingModels.empty())
            {
                const int waited = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - retryStart).count());
                if (waited >= AUTO_RETRY_SECONDS)
                    break;

                for (int elapsedMs = 0; elapsedMs < RETRY_INTERVAL_MS; elapsedMs += ANIMATION_STEP_MS)
                {
                    listener.OnWaitingForCameras(missingModels, waited + elapsedMs / 1000, elapsedMs / ANIMATION_STEP_MS);
                    PumpEvents(ANIMATION_STEP_MS, 100);
                }

                for (const auto& model : controller.ConnectNewCameras(ignoredPorts, &listener))
                {
                    auto it = std::find(missingModels.begin(), missingModels.end(), model);
                    if (it != missingModels.end())
                        missingModels.erase(it);
                }
            }

            if (missingModels.empty())
            {
                listener.OnAllCamerasConnected();
                break;
            }

            listener.OnRetryGaveUp();

            if (!listener.AskScanAgain())
                break;
        }
    }

    if (controller.GetCameras().empty())
    {
        controller.SetVerbose(true);
        return failed.empty() ? ConnectResult::NoCameras : ConnectResult::FailedAfterRetry;
    }

    controller.RegisterEventHandlers();

    PumpEvents(300);

    controller.SetSaveToHost();
    controller.SetCapacityForAll();

    PumpEvents(300);

    controller.SetVerbose(true);
    return ConnectResult::Connected;
}

bool BulletTime::TestCamera(const CameraContext& camera)
{
    if (!camera.sessionOpen || camera.camera == nullptr)
        return false;

    EdsSendCommand(camera.camera, kEdsCameraCommand_PressShutterButton, kEdsCameraCommand_ShutterButton_Halfway);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EdsSendCommand(camera.camera, kEdsCameraCommand_PressShutterButton, kEdsCameraCommand_ShutterButton_OFF);
    return true;
}

void BulletTime::RunCountdown(int seconds, bool playSounds)
{
    const auto countdownAudio = GetAssetAudioPath("countdown.wav");
    const auto bulletTimeAudio = GetAssetAudioPath("itsbullettime.wav");

    if (seconds > 0)
    {
        listener.OnCountdownStarted(seconds);

        for (int i = seconds; i >= 1; --i)
        {
            listener.OnCountdownTick(i);
            if (playSounds)
                AudioPlayer::PlayWavAsync(countdownAudio.string());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    listener.OnTrigger();
    if (playSounds)
        AudioPlayer::PlayWavAsync(bulletTimeAudio.string());
}

std::string BulletTime::GenerateShotId()
{
    static const char charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);

    std::string id;
    for (int i = 0; i < 4; ++i)
        id += charset[dist(gen)];

    return id;
}

bool BulletTime::RunSequence(const SequenceSettings& settings)
{
    bool sequenceAborted = false;
    std::string shotId;

    if (settings.sessionCount > 1)
        shotId = GenerateShotId();

    for (int sessionIndex = 1; sessionIndex <= settings.sessionCount; ++sessionIndex)
    {
        listener.OnSessionStarted(sessionIndex, settings.sessionCount);

        if (!controller.PrepareSessionFolder(sessionIndex, settings.sessionCount, shotId))
        {
            listener.OnSessionFolderFailed();
            sequenceAborted = true;
            break;
        }

        RunCountdown(settings.countdownSeconds, settings.playSounds);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        controller.ShootAll();

        for (const auto& cam : controller.GetCameras())
        {
            if (cam.sessionOpen && cam.lastShotError != 0)
                listener.OnShotFailed(cam, cam.lastShotError);
        }

        listener.OnWaitingForDownloads();

        const int maxWaitMs = settings.downloadWaitSeconds * 1000;
        int elapsed = 0;
        bool sessionFailed = false;

        while (true)
        {
            EdsGetEvent();
            Platform::ProcessEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            elapsed += 50;

            const int done = controller.GetCompletedDownloads();
            const int total = controller.GetExpectedDownloads();

            listener.OnDownloadProgress(done, total);

            if (done >= total && total > 0)
                break;

            if (elapsed >= maxWaitMs)
            {
                controller.RefreshCameraConnectionStatus();

                std::vector<const CameraContext*> lost;
                for (const auto& cam : controller.GetCameras())
                {
                    if (cam.connectionLost)
                        lost.push_back(&cam);
                }

                listener.OnDownloadTimeout(lost);
                sessionFailed = true;
                sequenceAborted = true;
                break;
            }
        }

        if (sessionFailed)
        {
            listener.OnSessionInterrupted();
            break;
        }

        listener.OnSessionCompleted();
    }

    listener.OnSequenceFinished(!sequenceAborted);
    return !sequenceAborted;
}
