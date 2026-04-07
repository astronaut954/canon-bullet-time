#pragma once

#include "EDSDK.h"
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

class CanonController;

struct CameraContext
{
    EdsCameraRef camera = nullptr;
    CanonController* owner = nullptr;

    std::string modelName;
    std::string shortName;
    std::string baseShortName;

    int modelIndex = 0;
    int orderPrefix = 0;

    bool sessionOpen = false;
    bool connectionLost = false;
    int lastError = 0;

    fs::path currentShotDir;
};