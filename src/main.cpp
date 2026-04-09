#include "canon_controller.h"
#include "logger.h"
#include "audio_player.h"
#include "path_utils.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <limits>
#include <vector>
#include <algorithm>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <locale.h>
#include <random>

static void TestCameraConsole(CanonController& controller);

static void PrintMenu()
{
    std::cout << "\n😎 CANON BULLET TIME\n";
    std::cout << "  1 - List Cameras\n";
    std::cout << "  2 - Detect Cameras\n";
    std::cout << "  3 - BULLET TIME\n";
    std::cout << "  4 - Output Folder\n";
    std::cout << "  5 - Reorder Cameras\n";
    std::cout << "  0 - Exit\n";
    std::cout << "Option: ";
}

static void PumpSdkEvents(int totalMs, int stepMs = 50)
{
    int elapsed = 0;

    while (elapsed < totalMs)
    {
        EdsGetEvent();
        std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
        elapsed += stepMs;
    }
}

static bool PrepareSessions(CanonController& controller)
{
    controller.SetVerbose(false);

    PumpSdkEvents(500);

    if (!controller.DetectCameras())
    {
        controller.SetVerbose(true);
        return false;
    }

    PumpSdkEvents(300);

    if (!controller.OpenSessions())
    {
        controller.SetVerbose(true);
        return false;
    }

    PumpSdkEvents(300);

    controller.SetSaveToHost();
    controller.SetCapacityForAll();

    PumpSdkEvents(300);

    controller.SetVerbose(true);
    return true;
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
            std::cout << " — ⚠️ Connection Lost!";
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
            std::cout << " — ⚠️ Connection Lost!";
        }

        std::cout << "\n";
    }

    std::cout << "\n";
}

static bool RedetectAndPrepare(CanonController& controller, bool showSuccessMessage = true)
{
    std::cout << "\n🔄 Detecting cameras...\n";

    controller.CloseSessions();

    if (!PrepareSessions(controller))
    {
        std::cout << "\n⚠️ No cameras found or failed to open sessions.\n";
        return false;
    }

    if (showSuccessMessage)
    {
        std::cout << "\n✅ Cameras successfully redetected.\n";
    }

    PrintCurrentCameras(controller);
    return true;
}

static void ReorderCamerasConsole(CanonController& controller)
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
            std::cout << " — ⚠️ Connection Lost!";
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
                std::cout << "\n⚠️ Cannot test: connection lost.\n";
                continue;
            }

            if (!current->sessionOpen || current->camera == nullptr)
            {
                std::cout << "\nSession is not open.\n";
                continue;
            }

            std::cout << "\nTesting camera: " << current->shortName << "...\n";

            EdsSendCommand(
                current->camera,
                kEdsCameraCommand_PressShutterButton,
                kEdsCameraCommand_ShutterButton_Halfway
            );

            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            EdsSendCommand(
                current->camera,
                kEdsCameraCommand_PressShutterButton,
                kEdsCameraCommand_ShutterButton_OFF
            );

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
            std::cout << "\n⚠️ Invalid input.\n";
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

static void TestCameraConsole(CanonController& controller)
{
    auto& cams = controller.GetCameras();

    if (cams.empty())
    {
        std::cout << "\n⚠️ No cameras detected.\n";
        return;
    }

    std::string shortName;
    std::cout << "\nEnter camera shortName to test (e.g. T7_1): ";
    std::getline(std::cin >> std::ws, shortName);

    CameraContext* target = nullptr;

    for (auto& cam : cams)
    {
        if (cam.shortName == shortName)
        {
            target = &cam;
            break;
        }
    }

    if (!target)
    {
        std::cout << "\nCamera not found.\n";
        return;
    }

    if (!target->sessionOpen)
    {
        std::cout << "\nSession is not open for this camera.\n";
        return;
    }

    std::cout << "\nTesting camera: " << shortName << "...\n";

    EdsSendCommand(
        target->camera,
        kEdsCameraCommand_PressShutterButton,
        kEdsCameraCommand_ShutterButton_Halfway
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EdsSendCommand(
        target->camera,
        kEdsCameraCommand_PressShutterButton,
        kEdsCameraCommand_ShutterButton_OFF
    );

    std::cout << "\nTest completed.\n";
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
                std::cout << "\n⚠️ Invalid value. Use a number greater than or equal to " << minValue << ".\n";
                continue;
            }

            return value;
        }
        catch (...)
        {
            std::cout << "\n⚠️ Invalid input. Enter numbers only.\n";
        }
    }
}

static void RunCountdown(int seconds)
{
    const auto countdownAudio = GetAssetAudioPath("countdown.wav");
    const auto bulletTimeAudio = GetAssetAudioPath("itsbullettime.wav");

    if (seconds > 0)
    {
        std::cout << "⏳ Triggering in:\n";

        for (int i = seconds; i >= 1; --i)
        {
            std::cout << "  " << i << "...\n";
            AudioPlayer::PlayWavAsync(countdownAudio.string());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::cout << "\n📸 IT'S BULLET TIME!\n";
    AudioPlayer::PlayWavAsync(bulletTimeAudio.string());
}

static std::string GenerateShotId()
{
    static const char charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);

    std::string id;

    for (int i = 0; i < 4; ++i)
    {
        id += charset[dist(gen)];
    }

    return id;
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF8");

    CanonController controller;

    if (!controller.InitializeSdk())
    {
        std::cout << "\nFailed to initialize SDK.\n";
        return 1;
    }

    bool initialReady = PrepareSessions(controller);

    PrintCurrentCameras(controller);
    std::cout << "\n📁 Default output folder: " << controller.GetOutputFolder() << "\n";

    if (!initialReady)
    {
        std::cout << "\nNo cameras ready at the moment. Use option 2 to try again.\n";
    }

    int option = -1;
    int bulletCountdownSeconds = 0;
    int bulletWaitSeconds = 10;
    int bulletSessionCount = 1;

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
            RedetectAndPrepare(controller);
        }
        else if (option == 3)
        {
            while (true)
            {
                auto& cams = controller.GetCameras();

                if (cams.empty())
                {
                    std::cout << "\n⚠️ No cameras detected. Use option 2 first.\n";
                    break;
                }

                std::cout << "\n🎬 BULLET TIME\n";
                std::cout << "  📁 Output Folder: " << controller.GetOutputFolder() << "\n";
                std::cout << "  ⏳ Timer: " << bulletCountdownSeconds << "s\n";
                std::cout << "  🎞️ Sessions: " << bulletSessionCount << "\n";
                std::cout << "  📥 Download Wait: " << bulletWaitSeconds << "s\n";

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
                    bulletCountdownSeconds = ReadIntWithDefault(
                        "\n⏳ Countdown before trigger (seconds): ",
                        bulletCountdownSeconds,
                        0
                    );

                    bulletSessionCount = ReadIntWithDefault(
                        "\n🎞️ Number of sessions: ",
                        bulletSessionCount,
                        1
                    );

                    continue;
                }
                else if (input == "t")
                {
                    bulletWaitSeconds = ReadIntWithDefault(
                        "\n📥 Maximum wait time (seconds): ",
                        bulletWaitSeconds,
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
                    std::cout << "\n⚠️ Invalid command.\n";
                    continue;
                }

                bool sequenceAborted = false;
                std::string shotId;

                if (bulletSessionCount > 1)
                {
                    shotId = GenerateShotId();
                }

                for (int sessionIndex = 1; sessionIndex <= bulletSessionCount; ++sessionIndex)
                {
                    std::cout << "\n🎞️ Session " << sessionIndex << " of " << bulletSessionCount << "\n";

                    if (!controller.PrepareSessionFolder(sessionIndex, bulletSessionCount, shotId))
                    {
                        std::cout << "\n⚠️ Failed to prepare session folder.\n";
                        sequenceAborted = true;
                        break;
                    }

                    RunCountdown(bulletCountdownSeconds);
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));

                    controller.ShootAll();

                    std::cout << "\n📥 Waiting for downloads...\n";

                    int maxWaitMs = bulletWaitSeconds * 1000;
                    int elapsed = 0;
                    bool sessionFailed = false;

                    while (true)
                    {
                        EdsGetEvent();
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        elapsed += 50;

                        int done = controller.GetCompletedDownloads();
                        int total = controller.GetExpectedDownloads();

                        std::cout << "\r📥 [" << done << "/" << total << "]" << std::flush;

                        if (done >= total && total > 0)
                        {
                            break;
                        }

                        if (elapsed >= maxWaitMs)
                        {
                            std::cout << "\n⚠️ Timeout reached.\n";

                            controller.RefreshCameraConnectionStatus();

                            auto& camsLost = controller.GetCameras();

                            bool anyLost = false;

                            for (const auto& cam : camsLost)
                            {
                                if (cam.connectionLost)
                                {
                                    if (!anyLost)
                                    {
                                        std::cout << "\n🔌 Lost connection on cameras:\n";
                                        anyLost = true;
                                    }

                                    std::cout << "  - " << cam.shortName
                                        << " │ " << cam.modelName << "\n";
                                }
                            }

                            if (!anyLost)
                            {
                                std::cout << "\nℹ️ No disconnected cameras detected.\n";
                            }

                            sessionFailed = true;
                            sequenceAborted = true;
                            break;
                        }
                    }

                    if (sessionFailed)
                    {
                        std::cout << "\n⚠️ Session interrupted.";
                        break;
                    }

                    std::cout << "\n✅ Session completed.\n";
                }

                if (sequenceAborted)
                {
                    std::cout << "\n⚠️ Sequence interrupted.\n";
                }
                else
                {
                    std::cout << "\n🏁 Sequence completed successfully.\n";
                }

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
            ReorderCamerasConsole(controller);
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