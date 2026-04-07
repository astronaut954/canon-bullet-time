#include "canon_controller.h"
#include "logger.h"
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

static void TestCameraConsole(CanonController& controller);

static void PrintMenu()
{
    std::cout << "\n😎 CANON BULLET TIME\n";
    std::cout << "  1 - Listar câmeras\n";
    std::cout << "  2 - Detectar novamente\n";
    std::cout << "  3 - BULLET TIME\n";
    std::cout << "  4 - Pasta de saída\n";
    std::cout << "  5 - Reordenar câmeras\n";
    std::cout << "  0 - Sair\n";
    std::cout << "Opção: ";
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
        std::cout << "\nNenhuma câmera detectada no momento.\n";
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

    std::cout << "\n📷  CÂMERAS DETECTADAS\n";

    for (const auto* cam : ordered)
    {
        std::cout << "  "
            << std::setw(2) << std::setfill('0') << cam->orderPrefix
            << " │ " << cam->shortName
            << " │ " << cam->modelName;

        if (cam->connectionLost)
        {
            std::cout << " — ⚠️ Conexão Perdida!";
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
        std::cout << "\nNenhuma câmera detectada no momento.\n";
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

    std::cout << "\nORDEM ATUAL\n";

    for (const auto* cam : ordered)
    {
        std::cout << "  "
            << std::setw(2) << std::setfill('0') << cam->orderPrefix
            << " │ " << cam->shortName;

        if (cam->connectionLost)
        {
            std::cout << " — ⚠️ Conexão Perdida!";
        }

        std::cout << "\n";
    }

    std::cout << "\n";
}

static bool RedetectAndPrepare(CanonController& controller, bool showSuccessMessage = true)
{
    std::cout << "\n🔄 Redetectando câmeras...\n";
    std::cout << "\n";

    if (!PrepareSessions(controller))
    {
        std::cout << "⚠️ Nenhuma câmera encontrada ou falha ao abrir sessões.\n";
        return false;
    }

    if (showSuccessMessage)
    {
        std::cout << "✅ Câmeras redetectadas com sucesso.\n";
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
        std::cout << "\nNenhuma câmera detectada no momento.\n";
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
        std::cout << "\nNenhuma câmera disponível.\n";
        return;
    }

    while (true)
    {
        controller.RefreshCameraConnectionStatus();
        PrintCameraOrderConsole(controller);

        std::cout << "CÂMERA ATUAL\n";
        std::cout << " 📷 " << current->shortName;

        if (current->connectionLost)
        {
            std::cout << " — ⚠️ Conexão Perdida!";
        }

        std::cout << "\n";
        std::cout << "  Prefixo: "
            << std::setw(2) << std::setfill('0') << current->orderPrefix << "\n";
        std::cout << "\n";

        std::cout << "Ações:\n";
        std::cout << "  [numero]     → Alterar prefixo\n";
        std::cout << "  [test]       → Identificar câmera\n";
        std::cout << "  [shortName]  → Trocar câmera\n";
        std::cout << "  [Enter]      → Sair\n";
        std::cout << "Comando: ";

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
                std::cout << "\n⚠️ Não é possível testar: conexão perdida.\n";
                continue;
            }

            if (!current->sessionOpen || current->camera == nullptr)
            {
                std::cout << "\nSessão não está aberta.\n";
                continue;
            }

            std::cout << "\nTestando câmera: " << current->shortName << "...\n";

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

            std::cout << "Teste concluído.\n";
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
            std::cout << "\n⚠️ Entrada inválida.\n";
            continue;
        }

        if (!controller.SetCameraPrefix(current->shortName, newPrefix))
        {
            std::cout << "\nErro ao alterar prefixo.\n";
        }
        else
        {
            std::cout << "\n✅ Prefixo atualizado com sucesso.\n";
        }
    }
}

static void TestCameraConsole(CanonController& controller)
{
    auto& cams = controller.GetCameras();

    if (cams.empty())
    {
        std::cout << "\n⚠️ Nenhuma câmera detectada.\n";
        return;
    }

    std::string shortName;
    std::cout << "\nDigite o shortName da camera para testar (ex: T7_1): ";
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
        std::cout << "\nCâmera não encontrada.\n";
        return;
    }

    if (!target->sessionOpen)
    {
        std::cout << "\nSessão não está aberta para essa câmera.\n";
        return;
    }

    std::cout << "\nTestando câmera: " << shortName << "...\n";

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

    std::cout << "\nTeste concluído.\n";
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
                std::cout << "\n⚠️ Valor inválido. Use um número maior ou igual a " << minValue << ".\n";
                continue;
            }

            return value;
        }
        catch (...)
        {
            std::cout << "\n⚠️ Entrada inválida. Digite apenas números.\n";
        }
    }
}

static void RunCountdown(int seconds)
{
    if (seconds <= 0)
    {
        return;
    }

    std::cout << "⏳ Disparo em:\n";

    for (int i = seconds; i >= 1; --i)
    {
        std::cout << "  " << i << "...\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n📸 IT'S BULLET TIME!\n \n";
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF8");

    CanonController controller;

    if (!controller.InitializeSdk())
    {
        std::cout << "\nFalha ao iniciar SDK.\n";
        return 1;
    }

    bool initialReady = PrepareSessions(controller);

    PrintCurrentCameras(controller);
    std::cout << "\n📁 Pasta padrão de saída: " << controller.GetOutputFolder() << "\n";

    if (!initialReady)
    {
        std::cout << "\nNenhuma câmera pronta no momento. Use a opção 2 para tentar novamente.\n";
    }

    int option = -1;
    int bulletCountdownSeconds = 0;
    int bulletWaitSeconds = 10;

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
            std::cout << "\nOpção inválida.\n";
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
                    std::cout << "\n⚠️ Nenhuma câmera detectada. Use a opção 2 primeiro.\n";
                    break;
                }

                std::cout << "\n🎬 BULLET TIME\n";
                std::cout << "  📁 Pasta de saída: " << controller.GetOutputFolder() << "\n";
                std::cout << "  ⏳ Timer: " << bulletCountdownSeconds << "s\n";
                std::cout << "  📥 Espera downloads: " << bulletWaitSeconds << "s\n";

                std::cout << "\nAções:\n";
                std::cout << "  [Enter] → Disparar\n";
                std::cout << "  [c]     → Configurar timer\n";
                std::cout << "  [t]     → Configurar tempo de download\n";
                std::cout << "  [x]     → Voltar\n";
                std::cout << "Comando: ";

                std::string input;
                std::getline(std::cin, input);

                if (input == "c")
                {
                    bulletCountdownSeconds = ReadIntWithDefault(
                        "\n⏳ Timer antes do disparo (segundos): ",
                        bulletCountdownSeconds,
                        0
                    );
                    continue;
                }
                else if (input == "t")
                {
                    bulletWaitSeconds = ReadIntWithDefault(
                        "\n📥 Tempo máximo de espera (segundos): ",
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
                    std::cout << "\n⚠️ Comando inválido.\n";
                    continue;
                }

                RunCountdown(bulletCountdownSeconds);

                controller.ShootAll();

                std::cout << "\n📥 Aguardando downloads...\n";

                int maxWaitMs = bulletWaitSeconds * 1000;
                int elapsed = 0;

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
                        std::cout << "\n⚠️ Timeout atingido.\n";

                        controller.RefreshCameraConnectionStatus();

                        auto& cams = controller.GetCameras();

                        bool anyLost = false;

                        for (const auto& cam : cams)
                        {
                            if (cam.connectionLost)
                            {
                                if (!anyLost)
                                {
                                    std::cout << "\n🔌 Conexão perdida nas câmeras:\n";
                                    anyLost = true;
                                }

                                std::cout << "  - " << cam.shortName
                                    << " │" << cam.modelName << "\n";
                            }
                        }

                        if (!anyLost)
                        {
                            std::cout << "\nℹ️ Nenhuma câmera desconectada.\n";
                        }

                        break;
                    }
                }

                std::cout << "\n✅ Sequência finalizada.\n";
                continue;
            }
        }
        else if (option == 4)
        {
            std::cout << "\n📁  PASTA DE SAÍDA\n";
            std::cout << controller.GetOutputFolder() << "\n";
            std::cout << "\n";

            std::string newPath;
            std::cout << "Digite um novo caminho";
            std::cout << "\nou pressione Enter para manter: ";
            std::getline(std::cin, newPath);

            if (newPath.empty())
            {
                std::cout << "\n↩️ Nenhuma alteração feita.\n";
            }
            else
            {
                controller.SetOutputFolder(newPath);
                std::cout << "✅ Pasta atualizada.\n";
            }
        }
        else if (option == 5)
        {
            ReorderCamerasConsole(controller);
        }
        else
        {
            std::cout << "\nOpção inválida.\n";
        }
    }

    controller.CloseSessions();
    controller.TerminateSdk();
    return 0;
}