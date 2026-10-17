#include "audio_player.h"

#include <csignal>
#include <spawn.h>
#include <sys/wait.h>

#include <objc/message.h>
#include <objc/runtime.h>

#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

extern char** environ;

// Toca os áudios com o "afplay", que vem em todo macOS.
// Assim como no Windows, um som novo interrompe o anterior.

namespace
{
    pid_t currentPid = 0;

    void ReapIfFinished()
    {
        if (currentPid > 0 && waitpid(currentPid, nullptr, WNOHANG) == currentPid)
        {
            currentPid = 0;
        }
    }

    bool Spawn(const std::string& filePath, pid_t& pid)
    {
        const char* argv[] = { "/usr/bin/afplay", filePath.c_str(), nullptr };
        return posix_spawn(&pid, argv[0], nullptr, nullptr,
                           const_cast<char* const*>(argv), environ) == 0;
    }
}

namespace
{
    // ---- Sons sobrepostos: AVAudioPlayer (AVFoundation), um tocador por som em curso ----
    template <typename Result, typename... Args>
    Result Send(id object, const char* selector, Args... args)
    {
        using Fn = Result (*)(id, SEL, Args...);
        return reinterpret_cast<Fn>(objc_msgSend)(object, sel_registerName(selector), args...);
    }

    std::mutex g_mixMutex;
    std::map<std::string, id> g_mixData;      // caminho -> NSData com o arquivo
    std::vector<id> g_mixPlayers;             // tocadores em curso (liberados quando terminam)
    int g_duckDepth = 0;                      // sons com ducking em curso
    const float kDuckVolume = 0.25f;

    void SetVolumes(float volume, id except)
    {
        for (id p : g_mixPlayers)
            if (p != except)
                Send<void>(p, "setVolume:fadeDuration:", volume, 0.08);
    }

    id LoadData(const std::string& filePath)
    {
        auto it = g_mixData.find(filePath);
        if (it != g_mixData.end())
            return it->second;
        std::ifstream file(filePath, std::ios::binary);
        std::stringstream content;
        content << file.rdbuf();
        const std::string bytes = content.str();
        if (bytes.empty())
            return nullptr;
        id cls = reinterpret_cast<id>(objc_getClass("NSData"));
        id data = Send<id>(cls, "dataWithBytes:length:", static_cast<const void*>(bytes.data()),
                           static_cast<unsigned long>(bytes.size()));
        Send<id>(data, "retain");
        g_mixData[filePath] = data;
        return data;
    }

    void ReapMixPlayers()
    {
        for (auto it = g_mixPlayers.begin(); it != g_mixPlayers.end();)
        {
            if (!Send<BOOL>(*it, "isPlaying"))
            {
                Send<void>(*it, "release");
                it = g_mixPlayers.erase(it);
            }
            else
                ++it;
        }
    }
}

namespace AudioPlayer
{
    bool PlayWavMixed(const std::string& filePath, bool duckOthers)
    {
        std::lock_guard<std::mutex> lock(g_mixMutex);
        ReapMixPlayers();
        id data = LoadData(filePath);
        if (!data)
            return false;
        id cls = reinterpret_cast<id>(objc_getClass("AVAudioPlayer"));
        if (!cls)
            return false;
        id player = Send<id>(Send<id>(cls, "alloc"), "initWithData:error:", data, static_cast<id*>(nullptr));
        if (!player)
            return false;
        Send<BOOL>(player, "prepareToPlay");
        if (duckOthers)
            SetVolumes(kDuckVolume, player);
        else if (g_duckDepth > 0)
            Send<void>(player, "setVolume:", kDuckVolume);  // entra já baixo, por baixo do toque
        if (!Send<BOOL>(player, "play"))
        {
            Send<void>(player, "release");
            return false;
        }
        g_mixPlayers.push_back(player);
        if (duckOthers)
        {
            // Volta o volume dos outros quando este som terminar
            ++g_duckDepth;
            const double seconds = Send<double>(player, "duration");
            std::thread([seconds]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(seconds * 1000)));
                std::lock_guard<std::mutex> lock(g_mixMutex);
                if (--g_duckDepth <= 0)
                {
                    g_duckDepth = 0;
                    SetVolumes(1.0f, nullptr);
                }
            }).detach();
        }
        return true;
    }

    bool PlayWavAsync(const std::string& filePath)
    {
        Stop();

        pid_t pid = 0;
        if (!Spawn(filePath, pid))
        {
            return false;
        }

        currentPid = pid;
        return true;
    }

    bool PlayWavSync(const std::string& filePath)
    {
        Stop();

        pid_t pid = 0;
        if (!Spawn(filePath, pid))
        {
            return false;
        }

        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    void Stop()
    {
        ReapIfFinished();

        if (currentPid > 0)
        {
            kill(currentPid, SIGTERM);
            waitpid(currentPid, nullptr, 0);
            currentPid = 0;
        }
    }
}
