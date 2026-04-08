#pragma once

#include <filesystem>

namespace fs = std::filesystem;

fs::path GetExecutableDirectory();
fs::path GetAssetAudioPath(const std::string& filename);