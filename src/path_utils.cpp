#include "path_utils.h"
#include "platform/platform.h"

fs::path GetAssetAudioPath(const std::string& filename)
{
    return Platform::ExecutableDirectory() / "assets" / "audio" / filename;
}
