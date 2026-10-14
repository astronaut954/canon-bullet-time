#include "path_utils.h"
#include "platform/platform.h"

fs::path GetAssetAudioPath(const std::string& filename)
{
    return Platform::ResourcesDirectory() / "assets" / "audio" / filename;
}
