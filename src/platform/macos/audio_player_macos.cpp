#include "audio_player.h"
#include "audio_sidechain.h"

#include <csignal>
#include <spawn.h>
#include <sys/wait.h>

#include <objc/message.h>
#include <objc/runtime.h>

#include <algorithm>
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

    struct MixFile
    {
        id data = nullptr;                   // NSData com o arquivo
        AudioSidechain::Envelope envelope;   // nível ao longo do tempo (para o ducking)
    };
    struct MixVoice
    {
        id player = nullptr;
        const MixFile* file = nullptr;
        bool dominant = false;               // abaixa os outros enquanto toca
        std::chrono::steady_clock::time_point start;
    };
    std::mutex g_mixMutex;
    std::map<std::string, MixFile> g_mixFiles;
    std::vector<MixVoice> g_mixVoices;
    bool g_duckerRunning = false;
    AudioSidechain::Compressor g_compressor;

    const MixFile* LoadFile(const std::string& filePath)
    {
        auto it = g_mixFiles.find(filePath);
        if (it != g_mixFiles.end())
            return &it->second;
        std::ifstream file(filePath, std::ios::binary);
        std::stringstream content;
        content << file.rdbuf();
        const std::string bytes = content.str();
        if (bytes.empty())
            return nullptr;
        MixFile mf;
        id cls = reinterpret_cast<id>(objc_getClass("NSData"));
        mf.data = Send<id>(cls, "dataWithBytes:length:", static_cast<const void*>(bytes.data()),
                           static_cast<unsigned long>(bytes.size()));
        Send<id>(mf.data, "retain");
        mf.envelope = AudioSidechain::ComputeWavEnvelope(bytes);
        return &(g_mixFiles[filePath] = mf);
    }

    void ReapMixVoices()
    {
        for (auto it = g_mixVoices.begin(); it != g_mixVoices.end();)
        {
            if (!Send<BOOL>(it->player, "isPlaying"))
            {
                Send<void>(it->player, "release");
                it = g_mixVoices.erase(it);
            }
            else
                ++it;
        }
    }

    // Sidechain: a cada 40 ms, o nível do som dominante naquele instante define o volume
    // dos outros. Roda enquanto houver som dominante tocando.
    void DuckerLoop()
    {
        using namespace std::chrono;
        auto last = steady_clock::now();
        while (true)
        {
            std::this_thread::sleep_for(milliseconds(40));
            std::lock_guard<std::mutex> lock(g_mixMutex);
            ReapMixVoices();
            const auto now = steady_clock::now();
            const double dt = duration<double>(now - last).count();
            last = now;
            float key = 0.0f;
            bool anyDominant = false;
            for (const auto& v : g_mixVoices)
                if (v.dominant)
                {
                    anyDominant = true;
                    key = std::max(key, v.file->envelope.At(duration<double>(now - v.start).count()));
                }
            const float gain = g_compressor.Update(key, dt);
            for (const auto& v : g_mixVoices)
                if (!v.dominant)
                    Send<void>(v.player, "setVolume:", gain);
            if (!anyDominant && gain >= 0.999f)
            {
                g_compressor.Reset();
                g_duckerRunning = false;
                return;
            }
        }
    }
}

namespace AudioPlayer
{
    bool PlayWavMixed(const std::string& filePath, bool duckOthers)
    {
        std::lock_guard<std::mutex> lock(g_mixMutex);
        ReapMixVoices();
        const MixFile* file = LoadFile(filePath);
        if (!file)
            return false;
        id cls = reinterpret_cast<id>(objc_getClass("AVAudioPlayer"));
        if (!cls)
            return false;
        id player = Send<id>(Send<id>(cls, "alloc"), "initWithData:error:", file->data, static_cast<id*>(nullptr));
        if (!player)
            return false;
        Send<BOOL>(player, "prepareToPlay");
        if (!duckOthers)
            Send<void>(player, "setVolume:", g_compressor.Gain());  // entra no volume atual do ducking
        if (!Send<BOOL>(player, "play"))
        {
            Send<void>(player, "release");
            return false;
        }
        g_mixVoices.push_back({ player, file, duckOthers, std::chrono::steady_clock::now() });
        if (duckOthers && !g_duckerRunning)
        {
            g_duckerRunning = true;
            std::thread(DuckerLoop).detach();
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
