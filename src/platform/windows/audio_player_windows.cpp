#include "audio_player.h"

#include <windows.h>
#include <mmsystem.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <thread>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

namespace
{
    // ---- Sons sobrepostos: uma "voz" waveOut por som em curso; o Windows mistura ----
    struct Wav
    {
        WAVEFORMATEX format{};
        std::vector<char> samples;
    };
    struct Voice
    {
        HWAVEOUT handle = nullptr;
        WAVEHDR header{};
    };
    std::mutex g_mixMutex;
    std::map<std::string, std::shared_ptr<Wav>> g_mixWavs;
    std::vector<std::unique_ptr<Voice>> g_mixVoices;
    int g_duckDepth = 0;
    const float kDuckVolume = 0.25f;

    void SetVolume(HWAVEOUT handle, float volume)
    {
        const DWORD v = static_cast<DWORD>(volume * 0xFFFF);
        waveOutSetVolume(handle, (v << 16) | v);  // esquerdo e direito
    }
    void SetVolumes(float volume, HWAVEOUT except)
    {
        for (auto& voice : g_mixVoices)
            if (voice->handle != except)
                SetVolume(voice->handle, volume);
    }

    // Lê um WAV PCM simples (RIFF: "fmt " e "data")
    std::shared_ptr<Wav> LoadWav(const std::string& filePath)
    {
        auto it = g_mixWavs.find(filePath);
        if (it != g_mixWavs.end())
            return it->second;
        std::ifstream file(filePath, std::ios::binary);
        std::stringstream content;
        content << file.rdbuf();
        const std::string bytes = content.str();
        if (bytes.size() < 12 || bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 4, "WAVE") != 0)
            return nullptr;
        auto wav = std::make_shared<Wav>();
        bool haveFormat = false;
        size_t pos = 12;
        while (pos + 8 <= bytes.size())
        {
            const std::string id = bytes.substr(pos, 4);
            uint32_t size = 0;
            std::memcpy(&size, bytes.data() + pos + 4, 4);
            const size_t body = pos + 8;
            if (body + size > bytes.size())
                break;
            if (id == "fmt " && size >= 16)
            {
                std::memcpy(&wav->format, bytes.data() + body, (std::min)(static_cast<size_t>(size), sizeof(WAVEFORMATEX)));
                wav->format.cbSize = 0;
                haveFormat = true;
            }
            else if (id == "data")
                wav->samples.assign(bytes.begin() + body, bytes.begin() + body + size);
            pos = body + size + (size & 1);
        }
        if (!haveFormat || wav->samples.empty())
            return nullptr;
        g_mixWavs[filePath] = wav;
        return wav;
    }

    void ReapVoices()
    {
        for (auto it = g_mixVoices.begin(); it != g_mixVoices.end();)
        {
            Voice& v = **it;
            if (v.header.dwFlags & WHDR_DONE)
            {
                waveOutUnprepareHeader(v.handle, &v.header, sizeof(v.header));
                waveOutClose(v.handle);
                it = g_mixVoices.erase(it);
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
        ReapVoices();
        auto wav = LoadWav(filePath);
        if (!wav)
            return false;
        auto voice = std::make_unique<Voice>();
        if (waveOutOpen(&voice->handle, WAVE_MAPPER, &wav->format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            return false;
        voice->header.lpData = wav->samples.data();
        voice->header.dwBufferLength = static_cast<DWORD>(wav->samples.size());
        if (duckOthers)
            SetVolumes(kDuckVolume, voice->handle);
        SetVolume(voice->handle, !duckOthers && g_duckDepth > 0 ? kDuckVolume : 1.0f);
        if (waveOutPrepareHeader(voice->handle, &voice->header, sizeof(voice->header)) != MMSYSERR_NOERROR
            || waveOutWrite(voice->handle, &voice->header, sizeof(voice->header)) != MMSYSERR_NOERROR)
        {
            waveOutClose(voice->handle);
            return false;
        }
        g_mixVoices.push_back(std::move(voice));
        if (duckOthers)
        {
            ++g_duckDepth;
            const double seconds = wav->format.nAvgBytesPerSec ? static_cast<double>(wav->samples.size()) / wav->format.nAvgBytesPerSec : 1.0;
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
        return PlaySoundA(
            filePath.c_str(),
            nullptr,
            SND_FILENAME | SND_ASYNC | SND_NODEFAULT
        ) == TRUE;
    }

    bool PlayWavSync(const std::string& filePath)
    {
        return PlaySoundA(
            filePath.c_str(),
            nullptr,
            SND_FILENAME | SND_NODEFAULT
        ) == TRUE;
    }

    void Stop()
    {
        PlaySoundA(nullptr, nullptr, 0);
    }
}