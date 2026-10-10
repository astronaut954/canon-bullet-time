#pragma once

// Núcleo do Canon Bullet Time: conectar câmeras, disparar a sequência e
// baixar as fotos. Não lê teclado nem escreve no terminal; tudo o que a
// pessoa precisa ver ou decidir passa pelo BulletTimeListener. Assim o mesmo
// núcleo serve o terminal de hoje e uma interface gráfica no futuro.

#include "canon_controller.h"

#include <string>
#include <vector>

struct SequenceSettings
{
    int countdownSeconds = 0;     // contagem antes de cada disparo
    int sessionCount = 1;         // quantos disparos seguidos
    int downloadWaitSeconds = 10; // tempo máximo esperando as fotos
};

// Avisos do núcleo para a interface. Todos têm implementação vazia por padrão.
class BulletTimeListener : public ConnectionObserver
{
public:
    // --- Detecção / reconexão de câmeras ---
    virtual void OnCamerasFailed(const std::vector<std::string>& /*names*/) {}
    virtual void OnRetryStarted(int /*maxSeconds*/) {}
    virtual void OnWaitingForCameras(const std::vector<std::string>& /*models*/, int /*seconds*/, int /*animationFrame*/) {}
    virtual void OnAllCamerasConnected() {}
    virtual void OnRetryGaveUp() {}
    // true = procurar de novo; false = seguir com as câmeras conectadas
    virtual bool AskScanAgain() { return false; }

    // --- Sequência de disparo ---
    virtual void OnSessionStarted(int /*index*/, int /*total*/) {}
    virtual void OnSessionFolderFailed() {}
    virtual void OnCountdownStarted(int /*seconds*/) {}
    virtual void OnCountdownTick(int /*secondsLeft*/) {}
    virtual void OnTrigger() {}
    virtual void OnWaitingForDownloads() {}
    virtual void OnDownloadProgress(int /*done*/, int /*total*/) {}
    virtual void OnDownloadTimeout(const std::vector<const CameraContext*>& /*lostCameras*/) {}
    virtual void OnSessionInterrupted() {}
    virtual void OnSessionCompleted() {}
    virtual void OnSequenceFinished(bool /*completed*/) {}
};

class BulletTime
{
public:
    enum class ConnectResult
    {
        Connected,        // pelo menos uma câmera pronta
        NoCameras,        // nenhuma câmera encontrada
        FailedAfterRetry  // câmeras falharam e nenhuma ficou conectada (o listener já foi avisado)
    };

    BulletTime(CanonController& controller, BulletTimeListener& listener);

    // Detecta e conecta as câmeras, esperando automaticamente pelas que falharem.
    ConnectResult ConnectCameras();

    // Executa a sequência de disparos. Retorna true se todas as sessões terminaram.
    bool RunSequence(const SequenceSettings& settings);

    // Aperta o botão do obturador até a metade, para identificar a câmera.
    bool TestCamera(const CameraContext& camera);

    // Deixa o SDK e o sistema entregarem os avisos das câmeras.
    void PumpEvents(int totalMs, int stepMs = 50);

private:
    void RunCountdown(int seconds);
    static std::string GenerateShotId();

    CanonController& controller;
    BulletTimeListener& listener;
};
