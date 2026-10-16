#pragma once

#include "EDSDK.h"
#include <deque>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

class CanonController;

struct CameraContext
{
    EdsCameraRef camera = nullptr;
    CanonController* owner = nullptr;

    std::string modelName;
    std::string portName;   // identifica a conexão USB da câmera
    bool onCompatibilityList = true;  // modelo está na lista da Canon usada pelo app
    long lastShotError = 0;           // erro do último disparo (0 = ok)
    std::string shortName;
    std::string baseShortName;

    int modelIndex = 0;
    int orderPrefix = 0;

    bool sessionOpen = false;
    bool connectionLost = false;
    int lastError = 0;

    fs::path currentShotDir;
    // Disparo com intervalos (interface gráfica): número de cada foto desta câmera na
    // sessão, na ordem em que vão chegar. Vazio = usa orderPrefix (o normal).
    std::deque<int> pendingPrefixes;
};