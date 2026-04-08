#include "audio_player.h"

#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

namespace AudioPlayer
{
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