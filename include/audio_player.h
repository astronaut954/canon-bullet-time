#pragma once

#include <string>

namespace AudioPlayer
{
    bool PlayWavAsync(const std::string& filePath);
    bool PlayWavSync(const std::string& filePath);
    void Stop();
}