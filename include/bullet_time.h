#pragma once

// Núcleo do Stillpoint: conectar câmeras, disparar a sequência e
// baixar as fotos. Não lê teclado nem escreve no terminal; tudo o que a
// pessoa precisa ver ou decidir passa pelo BulletTimeListener. Assim o mesmo
// núcleo serve o terminal de hoje e uma interface gráfica no futuro.

#include "canon_controller.h"

#include <atomic>
#include <string>
#include <vector>

struct SequenceSettings
{
    int countdownSeconds = 0;     // contagem antes de cada disparo
    int sessionCount = 1;         // quantos disparos seguidos
    int downloadWaitSeconds = 10; // tempo máximo esperando as fotos
    bool playSounds = true;       // áudio da contagem e do disparo (desligado nas fotos de teste)
    // Disparo com intervalos (interface gráfica): cada item é uma foto de uma câmera
    // num instante (ms depois do primeiro disparo), em ordem de tempo. Vazio = todas
    // ao mesmo tempo, como sempre (a versão de terminal só usa isso).
    std::vector<CanonController::ScheduledShot> schedule;
    // Som curto a cada grupo do plano depois do primeiro (janela Live); nada no terminal
    bool groupSounds = false;
    // Sons sobrepostos (AudioPlayer::PlayWavMixed) em vez de um por vez; a interface
    // gráfica liga; o terminal fica como sempre
    bool mixedAudio = false;
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
    // Disparo com plano: uma câmera acabou de disparar (índice 1..total no plano);
    // ok = false quando o comando falhou. Não é chamado no disparo de todas juntas.
    virtual void OnShotFired(const CameraContext& /*camera*/, int /*index*/, int /*total*/, bool /*ok*/) {}
    virtual void OnShotFailed(const CameraContext& /*camera*/, long /*error*/) {}
    virtual void OnWaitingForDownloads() {}
    virtual void OnDownloadProgress(int /*done*/, int /*total*/) {}
    virtual void OnDownloadTimeout(const std::vector<const CameraContext*>& /*lostCameras*/) {}
    virtual void OnSessionInterrupted() {}
    virtual void OnSessionCompleted() {}
    // A pessoa cancelou (RequestCancel); depois vem OnSequenceFinished(false)
    virtual void OnSequenceCancelled() {}
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

    // Pede para parar a sequência em andamento: na contagem (nada é disparado), entre
    // sessões (a próxima não começa) ou esperando as fotos (as que chegaram ficam).
    // Um disparo já feito não volta. Pode ser chamado de outra thread.
    void RequestCancel() { cancelRequested = true; }

    // Aperta o botão do obturador até a metade, para identificar a câmera.
    bool TestCamera(const CameraContext& camera);

    // Deixa o SDK e o sistema entregarem os avisos das câmeras.
    void PumpEvents(int totalMs, int stepMs = 50);

private:
    // false se a contagem foi cancelada (aí não dispara)
    bool RunCountdown(int seconds, bool playSounds, bool mixed = false);
    static std::string GenerateShotId();

    CanonController& controller;
    BulletTimeListener& listener;
    std::atomic<bool> cancelRequested{ false };
};
