// Interface de terminal do Stillpoint.
// Só cuida de menus, leitura do teclado e mensagens na tela; o trabalho com as
// câmeras fica no núcleo (BulletTime / CanonController).

#include "bullet_time.h"
#include "canon_controller.h"
#include "platform/platform.h"
#include "sdk_diagnostics.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static const char* const kDotFrames[] = { "   ", ".  ", ".. ", "..." };

static std::string JoinNames(const std::vector<std::string>& names)
{
    std::string result;
    for (const auto& name : names)
        result += (result.empty() ? "" : ", ") + name;
    return result;
}

static std::string FormatSeconds(double seconds)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(1) << seconds << "s";
    return text.str();
}

// Mostra no terminal os avisos do núcleo.
class ConsoleListener : public BulletTimeListener
{
public:
    // --- Conexão com cada câmera ---
    void OnConnecting(const CameraContext& camera) override
    {
        connectingText = "  ⏳ Connecting to " + camera.shortName + " (" + camera.modelName + ")";
        showedTimer = false;
        std::cout << connectingText << "..." << std::flush;
    }

    void OnConnectingProgress(const CameraContext&, int seconds, int animationFrame) override
    {
        if (seconds < 2)
            return;

        showedTimer = true;
        std::cout << "\r" << connectingText << kDotFrames[animationFrame % 4] << " " << seconds
                  << "s (camera is starting up, please wait)" << std::flush;
    }

    void OnConnected(const CameraContext& camera, double seconds) override
    {
        ClearTimer();
        std::cout << " ✅ Connected (" << FormatSeconds(seconds) << ")\n";

        if (!camera.onCompatibilityList)
        {
            std::cout << "  ⚠️  " << camera.modelName << " is not on Canon's compatibility list for this app.\n";
            std::cout << "      It may not work correctly.\n";
        }
    }

    void OnConnectFailed(const CameraContext&, long error, double seconds) override
    {
        ClearTimer();
        std::cout << " ⚠️  Failed, error " << error << " (" << FormatSeconds(seconds) << ")\n";

        const std::string hint = DescribeConnectionError(error);
        if (!hint.empty())
            std::cout << "      " << hint << "\n";
    }

    void OnNewCamerasFound() override
    {
        std::cout << "\n";
    }

    // --- Espera por câmeras que falharam ---
    void OnCamerasFailed(const std::vector<std::string>& names) override
    {
        std::cout << "  ⚠️  Could not connect to: " << JoinNames(names) << "\n";
    }

    void OnRetryStarted(int maxSeconds) override
    {
        std::cout << "\n🔄 Retrying automatically for up to " << maxSeconds << "s...\n";
    }

    void OnWaitingForCameras(const std::vector<std::string>& models, int seconds, int animationFrame) override
    {
        std::cout << "\r  ⌛ Waiting for " << models.size()
                  << (models.size() == 1 ? " camera (" : " cameras (")
                  << JoinNames(models) << ")"
                  << kDotFrames[animationFrame % 4] << " "
                  << seconds << "s   " << std::flush;
    }

    void OnAllCamerasConnected() override
    {
        std::cout << "\n  ✅ All cameras connected.\n";
    }

    void OnRetryGaveUp() override
    {
        std::cout << "\n  ⚠️  Could not connect failed cameras.\n";
        Platform::DiscardPendingInput();
    }

    bool AskScanAgain() override
    {
        std::cout << "\nActions:\n";
        std::cout << "  [r]     → Scan again\n";
        std::cout << "  [Enter] → Continue with the connected cameras\n";
        std::cout << "Option: ";

        std::string answer;
        std::getline(std::cin, answer);
        return answer == "r" || answer == "R";
    }

    // --- Sequência de disparo ---
    void OnSessionStarted(int index, int total) override
    {
        std::cout << "\n🎞️ Session " << index << " of " << total << "\n";
    }

    void OnSessionFolderFailed() override
    {
        std::cout << "\n⚠️  Failed to prepare session folder.\n";
    }

    void OnCountdownStarted(int) override
    {
        std::cout << "⏳ Triggering in:\n";
    }

    void OnCountdownTick(int secondsLeft) override
    {
        std::cout << "  " << secondsLeft << "...\n";
    }

    void OnTrigger() override
    {
        std::cout << "\n📸 IT'S BULLET TIME!\n";
    }

    void OnShotFailed(const CameraContext& camera, long error) override
    {
        const std::string hint = DescribeShotError(error);
        std::cout << "  ⚠️  " << camera.shortName << " did not take the picture: "
                  << (hint.empty() ? "error " + std::to_string(error) : hint) << ".\n";
    }

    void OnWaitingForDownloads() override
    {
        std::cout << "\n📥 Waiting for downloads...\n";
    }

    void OnDownloadProgress(int done, int total) override
    {
        std::cout << "\r📥 [" << done << "/" << total << "]" << std::flush;
    }

    void OnDownloadTimeout(const std::vector<const CameraContext*>& lostCameras) override
    {
        std::cout << "\n⚠️  Timeout reached.\n";

        if (lostCameras.empty())
        {
            std::cout << "\nℹ️ No disconnected cameras detected.\n";
            return;
        }

        std::cout << "\n🔌 Lost connection on cameras:\n";
        for (const auto* cam : lostCameras)
            std::cout << "  - " << cam->shortName << " │ " << cam->modelName << "\n";
    }

    void OnSessionInterrupted() override
    {
        std::cout << "\n⚠️  Session interrupted.";
    }

    void OnSessionCompleted() override
    {
        std::cout << "\n✅ Session completed.\n";
    }

    void OnSequenceFinished(bool completed) override
    {
        if (completed)
            std::cout << "\n🏁 Sequence completed successfully.\n";
        else
            std::cout << "\n⚠️  Sequence interrupted.\n";
    }

private:
    void ClearTimer()
    {
        if (!showedTimer)
            return;

        // Apaga o contador e reescreve a linha antes do resultado
        std::cout << "\r" << std::string(connectingText.size() + 48, ' ') << "\r" << connectingText << "..." << std::flush;
        showedTimer = false;
    }

    std::string connectingText;
    bool showedTimer = false;
};

static void PrintMenu()
{
    std::cout << "\n😎 STILLPOINT\n";
    std::cout << "  1 - List Cameras\n";
    std::cout << "  2 - Detect Cameras\n";
    std::cout << "  3 - BULLET TIME\n";
    std::cout << "  4 - Output Folder\n";
    std::cout << "  5 - Reorder Cameras\n";
    std::cout << "  0 - Exit\n";
    std::cout << "Option: ";
}

// Orientação quando nenhuma câmera foi encontrada.
static void PrintNoCamerasGuide()
{
    std::cout << "\n⚠️  No cameras found.\n";
    std::cout << "    Check:\n";
    std::cout << "    • Turn the camera off and on again\n";
    std::cout << "    • Camera is on and not in movie mode\n";
    std::cout << "    • USB cable supports data (not charge-only)\n";
    std::cout << "    • No other app is using the camera (EOS Utility, Photos, Image Capture, Google Drive)\n";
    std::cout << "    • Your model is supported: see CDC_EDSDK_Compat_List.pdf\n";
}

// Conecta as câmeras e descarta o que foi digitado durante esperas longas,
// para não virar uma resposta acidental no próximo menu.
static BulletTime::ConnectResult ConnectCameras(BulletTime& bulletTime)
{
    const auto result = bulletTime.ConnectCameras();
    Platform::DiscardPendingInput();
    return result;
}

static void PrintCurrentCameras(CanonController& controller)
{
    controller.RefreshCameraConnectionStatus();
    auto& cams = controller.GetCameras();

    if (cams.empty())
    {
        std::cout << "\nNo cameras detected at the moment.\n";
        return;
    }

    std::vector<CameraContext*> ordered;
    ordered.reserve(cams.size());

    for (auto& cam : cams)
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

    std::cout << "\n📷  DETECTED CAMERAS\n";

    for (const auto* cam : ordered)
    {
        std::cout << "  "
            << std::setw(2) << std::setfill('0') << cam->orderPrefix
            << " │ " << cam->shortName
            << " │ " << cam->modelName;

        if (cam->connectionLost)
        {
            std::cout << " — ⚠️  Connection Lost!";
        }

        std::cout << "\n";
    }
}

static void PrintCameraOrderConsole(CanonController& controller)
{
    controller.RefreshCameraConnectionStatus();
    auto& cams = controller.GetCameras();

    if (cams.empty())
    {
        std::cout << "\nNo cameras detected at the moment.\n";
        return;
    }

    std::vector<CameraContext*> ordered;
    ordered.reserve(cams.size());

    for (auto& cam : cams)
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

    std::cout << "\nCURRENT ORDER\n";

    for (const auto* cam : ordered)
    {
        std::cout << "  "
            << std::setw(2) << std::setfill('0') << cam->orderPrefix
            << " │ " << cam->shortName;

        if (cam->connectionLost)
        {
            std::cout << " — ⚠️  Connection Lost!";
        }

        std::cout << "\n";
    }

    std::cout << "\n";
}

static bool RedetectAndPrepare(CanonController& controller, BulletTime& bulletTime, bool showSuccessMessage = true)
{
    std::cout << "\n🔄 Detecting cameras...\n";

    controller.CloseSessions();

    const auto result = ConnectCameras(bulletTime);
    if (result != BulletTime::ConnectResult::Connected)
    {
        PrintNoCamerasGuide();
        return false;
    }

    if (showSuccessMessage)
    {
        std::cout << "\n✅ Cameras successfully redetected.\n";
    }

    PrintCurrentCameras(controller);
    return true;
}

static void ReorderCamerasConsole(CanonController& controller, BulletTime& bulletTime)
{
    controller.RefreshCameraConnectionStatus();
    auto& cams = controller.GetCameras();

    if (cams.empty())
    {
        std::cout << "\nNo cameras detected at the moment.\n";
        return;
    }

    CameraContext* current = nullptr;

    for (auto& cam : cams)
    {
        if (current == nullptr || cam.orderPrefix < current->orderPrefix)
        {
            current = &cam;
        }
    }

    if (!current)
    {
        std::cout << "\nNo cameras available.\n";
        return;
    }

    while (true)
    {
        controller.RefreshCameraConnectionStatus();
        PrintCameraOrderConsole(controller);

        std::cout << "CURRENT CAMERA\n";
        std::cout << " 📷 " << current->shortName;

        if (current->connectionLost)
        {
            std::cout << " — ⚠️  Connection Lost!";
        }

        std::cout << "\n";
        std::cout << "  Prefix: "
            << std::setw(2) << std::setfill('0') << current->orderPrefix << "\n";
        std::cout << "\n";

        std::cout << "Actions:\n";
        std::cout << "  [number]    → Change prefix\n";
        std::cout << "  [test]      → Identify camera\n";
        std::cout << "  [shortName] → Switch camera\n";
        std::cout << "  [Enter]     → Exit\n";
        std::cout << "Command: ";

        std::string input;
        std::getline(std::cin, input);

        if (input.empty())
        {
            break;
        }

        if (input == "test")
        {
            if (current->connectionLost)
            {
                std::cout << "\n⚠️  Cannot test: connection lost.\n";
                continue;
            }

            if (!current->sessionOpen || current->camera == nullptr)
            {
                std::cout << "\nSession is not open.\n";
                continue;
            }

            std::cout << "\nTesting camera: " << current->shortName << "...\n";
            bulletTime.TestCamera(*current);
            std::cout << "Test completed.\n";
            continue;
        }

        CameraContext* found = nullptr;
        for (auto& cam : cams)
        {
            if (cam.shortName == input)
            {
                found = &cam;
                break;
            }
        }

        if (found)
        {
            current = found;
            continue;
        }

        int newPrefix = 0;
        try
        {
            newPrefix = std::stoi(input);
        }
        catch (...)
        {
            std::cout << "\n⚠️  Invalid input.\n";
            continue;
        }

        if (!controller.SetCameraPrefix(current->shortName, newPrefix))
        {
            std::cout << "\nFailed to change prefix.\n";
        }
        else
        {
            std::cout << "\n✅ Prefix updated successfully.\n";
        }
    }
}

static int ReadIntWithDefault(const std::string& prompt, int defaultValue, int minValue = 0)
{
    while (true)
    {
        std::string line;
        std::cout << prompt;
        std::getline(std::cin, line);

        if (line.empty())
        {
            return defaultValue;
        }

        try
        {
            int value = std::stoi(line);

            if (value < minValue)
            {
                std::cout << "\n⚠️  Invalid value. Use a number greater than or equal to " << minValue << ".\n";
                continue;
            }

            return value;
        }
        catch (...)
        {
            std::cout << "\n⚠️  Invalid input. Enter numbers only.\n";
        }
    }
}

int main()
{
    Platform::InitConsole();

    CanonController controller;
    ConsoleListener listener;
    BulletTime bulletTime(controller, listener);

    if (!controller.InitializeSdk())
    {
        std::cout << "\nFailed to initialize SDK.\n";
        return 1;
    }

    bool initialReady = ConnectCameras(bulletTime) == BulletTime::ConnectResult::Connected;

    if (initialReady)
        PrintCurrentCameras(controller);
    else
        PrintNoCamerasGuide();
    std::cout << "\n📁 Default output folder: " << controller.GetOutputFolder() << "\n";

    if (!initialReady)
    {
        std::cout << "\nNo cameras ready at the moment. Use option 2 to try again.\n";
    }

    int option = -1;
    SequenceSettings settings;

    while (true)
    {
        PrintMenu();

        std::string optionLine;
        std::getline(std::cin, optionLine);

        if (optionLine.empty())
        {
            continue;
        }

        try
        {
            option = std::stoi(optionLine);
        }
        catch (...)
        {
            std::cout << "\nInvalid option.\n";
            continue;
        }

        if (option == 0)
        {
            break;
        }
        else if (option == 1)
        {
            PrintCurrentCameras(controller);
        }
        else if (option == 2)
        {
            RedetectAndPrepare(controller, bulletTime);
        }
        else if (option == 3)
        {
            while (true)
            {
                auto& cams = controller.GetCameras();

                if (cams.empty())
                {
                    std::cout << "\n⚠️  No cameras detected. Use option 2 first.\n";
                    break;
                }

                std::cout << "\n🎬 BULLET TIME\n";
                std::cout << "  📁 Output Folder: " << controller.GetOutputFolder() << "\n";
                std::cout << "  ⏳ Timer: " << settings.countdownSeconds << "s\n";
                std::cout << "  🎞️ Sessions: " << settings.sessionCount << "\n";
                std::cout << "  📥 Download Wait: " << settings.downloadWaitSeconds << "s\n";

                std::cout << "\nActions:\n";
                std::cout << "  [Enter] → Trigger\n";
                std::cout << "  [c]     → Configure Timer and Sessions\n";
                std::cout << "  [t]     → Configure Download Timeout\n";
                std::cout << "  [x]     → Back\n";
                std::cout << "Command: ";

                std::string input;
                std::getline(std::cin, input);

                if (input == "c")
                {
                    settings.countdownSeconds = ReadIntWithDefault(
                        "\n⏳ Countdown before trigger (seconds): ",
                        settings.countdownSeconds,
                        0
                    );

                    settings.sessionCount = ReadIntWithDefault(
                        "\n🎞️ Number of sessions: ",
                        settings.sessionCount,
                        1
                    );

                    continue;
                }
                else if (input == "t")
                {
                    settings.downloadWaitSeconds = ReadIntWithDefault(
                        "\n📥 Maximum wait time (seconds): ",
                        settings.downloadWaitSeconds,
                        1
                    );

                    continue;
                }
                else if (input == "x")
                {
                    break;
                }
                else if (!input.empty())
                {
                    std::cout << "\n⚠️  Invalid command.\n";
                    continue;
                }

                bulletTime.RunSequence(settings);

                continue;
            }
        }
        else if (option == 4)
        {
            std::cout << "\n📁  OUTPUT FOLDER\n";
            std::cout << controller.GetOutputFolder() << "\n\n";

            std::string newPath;
            std::cout << "Enter a new path";
            std::cout << "\nor press Enter to keep current: ";
            std::getline(std::cin, newPath);

            if (newPath.empty())
            {
                std::cout << "\n↩️ No changes made.\n";
            }
            else
            {
                controller.SetOutputFolder(newPath);
                std::cout << "✅ Output folder updated.\n";
            }
        }
        else if (option == 5)
        {
            ReorderCamerasConsole(controller, bulletTime);
        }
        else
        {
            std::cout << "\nInvalid option.\n";
        }
    }

    controller.CloseSessions();
    controller.TerminateSdk();
    return 0;
}