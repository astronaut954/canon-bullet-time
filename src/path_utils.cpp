#include "path_utils.h"

#include <windows.h>
#include <vector>

fs::path GetExecutableDirectory()
{
    std::vector<char> buffer(MAX_PATH);
    DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

    while (length == buffer.size())
    {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }

    if (length == 0)
    {
        return fs::current_path();
    }

    return fs::path(std::string(buffer.data(), length)).parent_path();
}

fs::path GetAssetAudioPath(const std::string& filename)
{
    return GetExecutableDirectory() / "assets" / "audio" / filename;
}