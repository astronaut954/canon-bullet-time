#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

fs::path GetAssetAudioPath(const std::string& filename);
