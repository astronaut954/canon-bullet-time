// Interface gráfica do Stillpoint.
//
// A janela é uma página HTML/CSS/JS (assets/ui/) exibida pelo navegador do
// próprio sistema (WebView2 no Windows, WebKit no Mac) dentro de uma janela
// nativa. O trabalho com as câmeras roda numa thread própria ("thread das
// câmeras"), usando o mesmo núcleo do terminal (BulletTime / CanonController).
// A conexão é automática: sem pedidos, essa thread acompanha as câmeras a cada
// ~0,4 s (CanonController::MonitorTick), e cada câmera conecta em paralelo.
//
// Comunicação:
//   página -> C++ : funções JavaScript ligadas com webview::bind (shoot, identify...)
//   C++ -> página : window.onCoreEvent({ type: "...", ... }) via webview::dispatch
//
// As fotos ficam na pasta de dados do app (PhotoStore) até a pessoa escolher onde
// salvar. Ao fechar a janela com fotos não salvas, a página pergunta o que fazer;
// se o app fechar de forma inesperada, as fotos continuam lá e são oferecidas na
// próxima abertura.

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX  // windows.h (via SDK e webview) define min/max, que quebram std::min/std::max
#endif
#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS  // getenv: com /sdl, o aviso do Visual Studio vira erro
#endif
#include "audio_player.h"
#include "bullet_time.h"
#include "path_utils.h"
#include "canon_controller.h"
#include "logger.h"
#include "path_utils.h"
#include "platform/platform.h"
#include "sdk_diagnostics.h"

#include "native_window.h"
#include "photo_store.h"
#include "exporter.h"
#include "camera_settings.h"
#include "auto_match.h"
#include "match_debug.h"
#include "color_match.h"
#include "platform/media.h"

#include "webview/webview.h"

#include <algorithm>
#include <optional>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <map>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    // ---------------------------------------------------------------------
    // Versão e atualização (versão vem do arquivo VERSION, pelo CMake)
    // ---------------------------------------------------------------------
#ifndef STILLPOINT_VERSION
#define STILLPOINT_VERSION "0.0.0"
#endif
    const char* const kRepo = "astronaut954/stillpoint-bullet-time-photography";
    const std::string kReleaseDownloads = std::string("https://github.com/") + kRepo + "/releases/download/";
    const std::string kRepoPages = std::string("https://github.com/") + kRepo;

    // STILLPOINT_TEST_VERSION=2.1.0: o app se apresenta como uma versão antiga, para
    // testar a atualização com a versão publicada no GitHub
    std::string AppVersion()
    {
        const char* test = std::getenv("STILLPOINT_TEST_VERSION");
        return test && *test ? test : STILLPOINT_VERSION;
    }

    // Versão da última vez que o app abriu (para avisar "Updated to ..."); grava a atual
    std::string SwapLastVersion(const std::string& current)
    {
        const auto file = Platform::AppDataFolder() / "last-version.txt";
        std::string previous;
        {
            std::ifstream in(file);
            std::getline(in, previous);
        }
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        std::ofstream(file, std::ios::trunc) << current;
        return previous;
    }

    std::string ReadTextFile(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        std::stringstream content;
        content << in.rdbuf();
        return content.str();
    }

    struct UpdateState
    {
        std::mutex mutex;
        std::atomic<bool> downloading{ false };
        std::atomic<bool> cancel{ false };
        std::filesystem::path ready;  // instalador baixado e conferido
    };

    // ---------------------------------------------------------------------
    // JSON mínimo (só o necessário para conversar com a página)
    // ---------------------------------------------------------------------
    std::string JsonString(const std::string& text)
    {
        std::string out = "\"";
        for (unsigned char c : text)
        {
            switch (c)
            {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(c);
                }
            }
        }
        return out + "\"";
    }

    class Json
    {
    public:
        explicit Json(const std::string& type) : type(type) { Add("type", type); }
        const std::string& Type() const { return type; }

        Json& Add(const std::string& key, const std::string& value) { return Raw(key, JsonString(value)); }
        Json& Add(const std::string& key, const char* value) { return Raw(key, JsonString(value)); }
        Json& Add(const std::string& key, int value) { return Raw(key, std::to_string(value)); }
        Json& Add(const std::string& key, long value) { return Raw(key, std::to_string(value)); }
        Json& Add(const std::string& key, double value)
        {
            std::ostringstream text;
            text.precision(1);
            text << std::fixed << value;
            return Raw(key, text.str());
        }
        Json& Add(const std::string& key, bool value) { return Raw(key, value ? "true" : "false"); }
        Json& Add(const std::string& key, const std::vector<std::string>& values)
        {
            std::string list = "[";
            for (size_t i = 0; i < values.size(); ++i)
                list += (i ? "," : "") + JsonString(values[i]);
            return Raw(key, list + "]");
        }
        Json& Raw(const std::string& key, const std::string& rawJson)
        {
            body += (body.empty() ? "" : ",") + JsonString(key) + ":" + rawJson;
            return *this;
        }

        std::string Str() const { return "{" + body + "}"; }

    private:
        std::string type;
        std::string body;
    };

    // Argumentos chegam como um array JSON: ["T7_1"], [2, 1, 10], [true]
    std::vector<std::string> ParseStringArgs(const std::string& request)
    {
        std::vector<std::string> args;
        static const std::regex stringPattern(R"re("((?:[^"\\]|\\.)*)")re");
        for (std::sregex_iterator it(request.begin(), request.end(), stringPattern), end; it != end; ++it)
        {
            std::string value;
            const std::string raw = (*it)[1].str();
            for (size_t i = 0; i < raw.size(); ++i)
            {
                if (raw[i] == '\\' && i + 1 < raw.size())
                {
                    const char next = raw[++i];
                    value += next == 'n' ? '\n' : next == 't' ? '\t' : next;
                }
                else
                {
                    value += raw[i];
                }
            }
            args.push_back(value);
        }
        return args;
    }

    std::vector<int> ParseIntArgs(const std::string& request)
    {
        std::vector<int> args;
        static const std::regex numberPattern(R"(-?\d+)");
        for (std::sregex_iterator it(request.begin(), request.end(), numberPattern), end; it != end; ++it)
            args.push_back(std::stoi(it->str()));
        return args;
    }

    // ---------------------------------------------------------------------
    // Thread das câmeras: executa um pedido por vez, na ordem em que chegam
    // ---------------------------------------------------------------------
    class CameraThread
    {
    public:
        // onIdle: chamado a cada ~0,4 s sem pedidos (acompanhamento das câmeras)
        explicit CameraThread(std::function<void()> onIdle)
            : onIdle(std::move(onIdle)), worker([this] { Loop(); })
        {
        }

        ~CameraThread()
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                stopping = true;
            }
            ready.notify_one();
            worker.join();
        }

        void Post(std::function<void()> task)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                tasks.push_back(std::move(task));
            }
            ready.notify_one();
        }

    private:
        void Loop()
        {
            Platform::BeginSdkThread();
            while (true)
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    ready.wait_for(lock, std::chrono::milliseconds(400), [this] { return stopping || !tasks.empty(); });
                    if (!tasks.empty())
                    {
                        task = std::move(tasks.front());
                        tasks.pop_front();
                    }
                    else if (stopping)
                    {
                        break;
                    }
                }

                if (task)
                    task();
                else if (onIdle)
                    onIdle();
            }
            Platform::EndSdkThread();
        }

        std::function<void()> onIdle;
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<std::function<void()>> tasks;
        bool stopping = false;
        std::thread worker;
    };

    // Gera prévias de fotos fora da thread da janela (uma de cada vez)
    class PreviewThread
    {
    public:
        PreviewThread() : worker([this] { Loop(); }) {}
        ~PreviewThread()
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                stopping = true;
            }
            ready.notify_one();
            worker.join();
        }
        void Post(std::function<void()> task)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                tasks.push_back(std::move(task));
            }
            ready.notify_one();
        }

    private:
        void Loop()
        {
            while (true)
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    ready.wait(lock, [this] { return stopping || !tasks.empty(); });
                    if (stopping)
                        break;
                    task = std::move(tasks.front());
                    tasks.pop_front();
                }
                task();
            }
        }

        std::mutex mutex;
        std::condition_variable ready;
        std::deque<std::function<void()>> tasks;
        bool stopping = false;
        std::thread worker;
    };

    // ---------------------------------------------------------------------
    // Avisos do núcleo -> eventos para a página
    // ---------------------------------------------------------------------
    class GuiListener : public BulletTimeListener
    {
    public:
        explicit GuiListener(webview::webview& window) : window(window) {}

        // Cada aviso vai para a janela principal e para as janelas Live abertas
        void Emit(const Json& event)
        {
            if (closed)
                return;
            const std::string json = event.Str();
            if (event.Type() == "cameras")
            {
                std::lock_guard<std::mutex> lock(mutex);
                lastCameras = json;
            }
            const std::string script = "window.onCoreEvent && window.onCoreEvent(" + json + ")";
            window.dispatch([this, script] { window.eval(script); });
            std::lock_guard<std::mutex> lock(mutex);
            for (webview::webview* extra : extras)
                extra->dispatch([extra, script] { extra->eval(script); });
        }

        void AddWindow(webview::webview* extra)
        {
            std::lock_guard<std::mutex> lock(mutex);
            extras.push_back(extra);
        }
        // Última lista de câmeras mandada (para uma janela Live que acabou de abrir)
        std::string LastCameras()
        {
            std::lock_guard<std::mutex> lock(mutex);
            return lastCameras;
        }

        void Close() { closed = true; }

        // --- Conexão ---
        void OnConnecting(const CameraContext& camera) override
        {
            Emit(Json("connecting").Add("camera", camera.shortName).Add("model", camera.modelName));
        }
        void OnConnectingProgress(const CameraContext& camera, int seconds, int) override
        {
            Emit(Json("connectingProgress").Add("camera", camera.shortName).Add("seconds", seconds));
        }
        void OnConnected(const CameraContext& camera, double seconds) override
        {
            Emit(Json("connected").Add("camera", camera.shortName).Add("model", camera.modelName)
                .Add("seconds", seconds).Add("compatible", camera.onCompatibilityList));
        }
        void OnConnectFailed(const CameraContext& camera, long error, double seconds) override
        {
            Emit(Json("connectFailed").Add("camera", camera.shortName).Add("error", error)
                .Add("seconds", seconds).Add("hint", DescribeConnectionError(error)));
        }

        // --- Sequência ---
        void OnSessionStarted(int index, int total) override
        {
            Emit(Json("sessionStarted").Add("index", index).Add("total", total));
        }
        void OnSessionFolderFailed() override { Emit(Json("sessionFolderFailed")); }
        void OnCountdownStarted(int seconds) override { Emit(Json("countdownStarted").Add("seconds", seconds)); }
        void OnCountdownTick(int secondsLeft) override { Emit(Json("countdownTick").Add("seconds", secondsLeft)); }
        void OnTrigger() override { Emit(Json("trigger")); }
        void OnShotFired(const CameraContext& camera, int index, int total, bool ok) override
        {
            Emit(Json("shotFired").Add("camera", camera.shortName).Add("index", index).Add("total", total).Add("ok", ok));
        }
        void OnShotFailed(const CameraContext& camera, long error) override
        {
            Emit(Json("shotFailed").Add("camera", camera.shortName).Add("error", error)
                .Add("hint", DescribeShotError(error)));
        }
        void OnWaitingForDownloads() override { Emit(Json("waitingDownloads")); }
        void OnDownloadProgress(int done, int total) override
        {
            // Evita inundar a página: só manda quando muda
            if (done == lastDone && total == lastTotal)
                return;
            lastDone = done;
            lastTotal = total;
            Emit(Json("downloadProgress").Add("done", done).Add("total", total));
        }
        void OnDownloadTimeout(const std::vector<const CameraContext*>& lostCameras) override
        {
            std::vector<std::string> names;
            for (const auto* cam : lostCameras)
                names.push_back(cam->shortName);
            Emit(Json("downloadTimeout").Add("lost", names));
        }
        void OnSessionInterrupted() override { Emit(Json("sessionInterrupted")); }
        void OnSessionCompleted() override
        {
            lastDone = lastTotal = -1;
            Emit(Json("sessionCompleted"));
        }
        void OnSequenceCancelled() override { Emit(Json("sequenceCancelled")); }
        void OnSequenceFinished(bool completed) override
        {
            lastDone = lastTotal = -1;
            Emit(Json("sequenceFinished").Add("completed", completed));
        }

    private:
        webview::webview& window;
        std::atomic<bool> closed{ false };
        std::mutex mutex;
        std::vector<webview::webview*> extras;
        std::string lastCameras;

        int lastDone = -1;
        int lastTotal = -1;
    };

    // Estado atual das câmeras para a página redesenhar: conectadas, desconectadas
    // e as que estão conectando agora (com os segundos de espera)
    // Bullet time precisa de UMA foto por câmera: o modo de disparo ("drive") fica em
    // disparo único. Câmera em contínuo ou timer é posta em único pelo app; se ela não
    // aceitar, o aviso aparece no painel (a página lê "drive" nas configurações).
    // Disparo único silencioso também vale (uma foto).
    bool IsSingleShot(long long drive) { return drive == 0x00 || drive == 0x06 || drive == 0x13; }

    // Configurações de todas as câmeras conectadas, na ordem do app
    std::vector<CameraSettings> ReadAllSettings(CanonController& controller)
    {
        std::vector<const CameraContext*> ordered;
        for (const auto& cam : controller.GetCameras())
            if (cam.sessionOpen && (cam.camera != nullptr || cam.fake))
                ordered.push_back(&cam);
        std::sort(ordered.begin(), ordered.end(),
            [](const CameraContext* a, const CameraContext* b) { return a->orderPrefix < b->orderPrefix; });
        std::vector<CameraSettings> all;
        for (const CameraContext* cam : ordered)
            all.push_back(CameraSettingsCanon::Read(*cam));
        return all;
    }

    // Põe em disparo único as câmeras que não estão (all: leitura atual, atualizada
    // nas que mudaram). Avisa uma vez por câmera se não der.
    void EnsureSingleShot(CanonController& controller, std::vector<CameraSettings>& all, std::map<std::string, bool>& warned)
    {
        for (auto& s : all)
        {
            const auto drive = std::find_if(s.settings.begin(), s.settings.end(), [](const CameraSetting& c) { return c.key == "drive"; });
            if (drive == s.settings.end() || !drive->available || IsSingleShot(drive->value))
            {
                warned.erase(s.camera);
                continue;
            }
            const auto cam = std::find_if(controller.GetCameras().begin(), controller.GetCameras().end(),
                [&](const CameraContext& c) { return c.shortName == s.camera && c.sessionOpen; });
            if (cam == controller.GetCameras().end())
                continue;
            std::string error;
            if (CameraSettingsCanon::Write(*cam, "drive", 0x00, error))
            {
                Log(s.camera + " was set to single shooting (it was on " + drive->label + ")");
                warned.erase(s.camera);
                s = CameraSettingsCanon::Read(*cam);
            }
            else if (!warned[s.camera])
            {
                warned[s.camera] = true;
                Log(s.camera + " is on " + drive->label + " and could not be set to single shooting: " + error);
            }
        }
    }

    void EnsureSingleShot(CanonController& controller, std::map<std::string, bool>& warned)
    {
        auto all = ReadAllSettings(controller);
        EnsureSingleShot(controller, all, warned);
    }

    // Modo debug (Settings): pasta com as fotos e as medidas de cada Test shot
    std::filesystem::path DebugRoot() { return Platform::AppDataFolder() / "Debug"; }

    // Caminho como texto UTF-8 (para mandar à página)
    std::string PathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }

    // As fotos de uma sessão do Test shot, uma por câmera, com as configurações atuais
    // (as que a câmera usou nessa foto) e a medida, para o MatchDebug
    std::vector<MatchDebug::Photo> DebugPhotos(CanonController& controller, PhotoStore& store, const StoredSession& session,
                                               const std::map<std::string, ColorMatch::Stats>* measured)
    {
        std::map<std::string, const StoredPhoto*> picks;
        for (const auto& p : session.photos)
            if (!picks.count(p.camera) || IsJpegPhoto(p.file))
                picks[p.camera] = &p;
        std::vector<MatchDebug::Photo> photos;
        for (const auto& cam : controller.GetCameras())
        {
            if (!cam.sessionOpen || (cam.camera == nullptr && !cam.fake))
                continue;
            MatchDebug::Photo photo;
            photo.camera = cam.shortName;
            photo.settings = CameraSettingsCanon::Read(cam);
            const auto pick = picks.find(cam.shortName);
            if (pick != picks.end())
            {
                photo.file = store.Root() / session.name / pick->second->file;
                const auto m = measured ? measured->find(cam.shortName) : std::map<std::string, ColorMatch::Stats>::const_iterator{};
                if (measured && m != measured->end())
                    photo.measured = m->second;
                else if (pick->second->previewable)
                    photo.measured = ColorMatch::Measure(photo.file);
            }
            photos.push_back(photo);
        }
        return photos;
    }

    // Calibração do ajuste fino por modelo (feita pelo "Match automatically")
    std::filesystem::path CalibrationFile() { return Platform::AppDataFolder() / "camera-calibration.txt"; }
    std::filesystem::path ChartCalibrationFile() { return Platform::AppDataFolder() / "chart-calibration.txt"; }

    // Configurações para a tela Camera settings (JSON)
    std::string CameraSettingsJson(CanonController& controller, const std::vector<CameraSettings>& all)
    {
        const auto calibrations = AutoMatch::LoadCalibration(CalibrationFile());
        const auto charts = AutoMatch::LoadChartCalibration(ChartCalibrationFile());
        std::string list = "[";
        for (size_t i = 0; i < all.size(); ++i)
        {
            const CameraSettings& s = all[i];
            int order = 0;
            for (const auto& cam : controller.GetCameras())
                if (cam.shortName == s.camera)
                    order = cam.orderPrefix;
            std::string settings = "{";
            for (size_t k = 0; k < s.settings.size(); ++k)
            {
                const CameraSetting& c = s.settings[k];
                std::string choices = "[";
                for (size_t j = 0; j < c.choices.size(); ++j)
                    choices += (j ? "," : "") + std::string("[") + std::to_string(c.choices[j].value) + ","
                               + JsonString(c.choices[j].label) + "]";
                choices += "]";
                settings += (k ? "," : "") + JsonString(c.key) + ":"
                            + Json("setting").Add("available", c.available).Raw("value", std::to_string(c.value))
                                  .Add("label", c.label).Raw("choices", choices).Str();
            }
            settings += "}";
            // Modelo medido com o ajuste fino invertido: a página troca os rótulos (A/B, G/M)
            const auto cal = calibrations.find(s.model);
            const bool abFlip = cal != calibrations.end() && cal->second.abSign < 0;
            const bool gmFlip = cal != calibrations.end() && cal->second.gmSign < 0;
            // Diferença do modelo medida com a cartela (para o Picture Style atual), ou null
            std::string chart = "null";
            for (const auto& c : s.settings)
                if (c.key == "style")
                    if (const auto it = charts.find(AutoMatch::ChartKey(s.model, c.value)); it != charts.end())
                        chart = Json("chart").Add("contrast", it->second.contrast).Add("saturation", it->second.saturation)
                                    .Add("colorTone", it->second.colorTone).Str();
            list += (i ? "," : "") + Json("camera").Add("camera", s.camera).Add("model", s.model)
                .Add("abFlip", abFlip).Add("gmFlip", gmFlip)
                .Add("order", order).Add("adjustable", s.adjustable).Add("reason", s.reason)
                .Add("mode", s.mode).Add("manual", s.manualMode).Add("kelvin", s.kelvin)
                .Add("kelvinAvailable", s.kelvinAvailable).Add("shiftAvailable", s.shiftAvailable)
                .Add("shiftAB", s.shiftAB).Add("shiftGM", s.shiftGM).Add("focusManual", s.focusManual)
                .Add("styleDetail", s.styleDetailAvailable).Add("contrast", s.contrast)
                .Add("saturation", s.saturation).Add("colorTone", s.colorTone).Raw("chart", chart)
                .Raw("settings", settings).Str();
        }
        return list + "]";
    }

    // Lê tudo uma vez, confere o disparo único e manda para a página
    void EmitCameraSettings(CanonController& controller, GuiListener& listener, std::map<std::string, bool>& warned)
    {
        auto all = ReadAllSettings(controller);
        EnsureSingleShot(controller, all, warned);
        listener.Emit(Json("cameraSettings").Raw("cameras", CameraSettingsJson(controller, all)));
    }

    Json CamerasSnapshot(CanonController& controller)
    {
        const auto pending = controller.GetPendingConnections();

        std::vector<const CameraContext*> ordered;
        for (const auto& cam : controller.GetCameras())
            ordered.push_back(&cam);
        std::sort(ordered.begin(), ordered.end(),
            [](const CameraContext* a, const CameraContext* b) { return a->orderPrefix < b->orderPrefix; });

        std::string list = "[";
        bool first = true;
        auto addItem = [&](const Json& item)
        {
            list += (first ? "" : ",") + item.Str();
            first = false;
        };

        // Câmeras que já aparecem no USB mas que o SDK ainda não lista (a T7 leva ~40 s):
        // aparecem como "conectando", com o tempo desde que apareceram no USB. Com o
        // modelo conhecido, no lugar de uma câmera desse modelo que caiu; senão, como
        // câmera nova ("Canon camera" enquanto o modelo não é conhecido).
        auto starting = controller.GetStartingCameras();
        auto takeStarting = [&](const std::string& model) -> std::optional<StartingCameraInfo>
        {
            auto it = std::find_if(starting.begin(), starting.end(),
                [&](const StartingCameraInfo& s) { return !s.model.empty() && s.model == model; });
            if (it == starting.end())
                return std::nullopt;
            StartingCameraInfo info = *it;
            starting.erase(it);
            return info;
        };

        for (const auto* cam : ordered)
        {
            const bool connected = cam->sessionOpen && !cam->connectionLost;
            auto reconnect = std::find_if(pending.begin(), pending.end(),
                [&](const PendingConnectionInfo& p) { return p.reconnecting && !p.retrying && p.name == cam->shortName; });
            std::optional<StartingCameraInfo> startingInfo;
            if (!connected && reconnect == pending.end())
                startingInfo = takeStarting(cam->modelName);
            const bool startingHere = startingInfo.has_value();
            const bool connecting = !connected && (reconnect != pending.end() || startingHere);
            const bool waiting = connecting && !startingHere && reconnect->queued;

            Json item("camera");
            item.Add("order", cam->orderPrefix)
                .Add("name", cam->shortName)
                .Add("model", cam->modelName)
                .Add("connected", connected)
                .Add("state", connected ? "connected" : connecting ? "connecting" : "disconnected")
                .Add("seconds", startingHere ? startingInfo->seconds : connecting ? reconnect->seconds : 0)
                .Add("waiting", waiting)
                .Add("starting", startingHere)
                .Add("compatible", cam->onCompatibilityList);
            addItem(item);
        }

        // Câmeras novas que ainda estão conectando (sem posição na ordem). Novas
        // tentativas numa conexão que já falhou (ex.: entrada "fantasma" do SDK do
        // Mac) não aparecem, para a lista não piscar.
        for (const auto& p : pending)
        {
            if (p.reconnecting || p.retrying)
                continue;
            Json item("camera");
            item.Add("order", 0)
                .Add("name", p.name)
                .Add("model", p.model)
                .Add("connected", false)
                .Add("state", "connecting")
                .Add("seconds", p.seconds)
                .Add("waiting", p.queued)
                .Add("starting", false)
                .Add("compatible", IsOnCompatibilityList(p.model));
            addItem(item);
        }
        for (const auto& s : starting)
        {
            Json item("camera");
            item.Add("order", 0)
                .Add("name", "")
                .Add("model", s.model.empty() ? std::string("Canon camera") : s.model)
                .Add("connected", false)
                .Add("state", "connecting")
                .Add("seconds", s.seconds)
                .Add("waiting", false)
                .Add("starting", true)
                .Add("compatible", s.model.empty() || IsOnCompatibilityList(s.model));
            addItem(item);
        }
        list += "]";

        Json event("cameras");
        event.Raw("cameras", list);
        return event;
    }

    std::string SessionsJson(const PhotoStore& store)
    {
        std::string list = "[";
        bool firstSession = true;
        for (const auto& session : store.ListSessions())
        {
            std::string photos = "[";
            for (size_t i = 0; i < session.photos.size(); ++i)
            {
                const auto& photo = session.photos[i];
                Json item("photo");
                item.Add("file", photo.file).Add("order", photo.order)
                    .Add("camera", photo.camera).Add("previewable", photo.previewable);
                photos += (i ? "," : "") + item.Str();
            }
            photos += "]";

            Json item("session");
            item.Add("name", session.name).Add("saved", session.saved).Raw("photos", photos);
            list += (firstSession ? "" : ",") + item.Str();
            firstSession = false;
        }
        return list + "]";
    }

    // ---------------------------------------------------------------------
    // Settings: tema e idioma, em <dados do app>/settings.txt ("chave=valor" por
    // linha). Valem ao abrir o app de novo.
    // ---------------------------------------------------------------------

    // Temas: arquivos assets/ui/themes/<nome>.css que começam com
    //   /* stillpoint-theme: Nome do tema */
    // O padrão é o Nocturne com as cores do Neo Terminal. Arquivos sem essa linha (ex.:
    // temas de teste antigos que ficaram numa pasta de build) não aparecem nem carregam.
    const char* const kDefaultTheme = "nocturne-neo";
    // Idiomas da interface (código, nome). Por enquanto só inglês.
    const std::vector<std::pair<std::string, std::string>> kLanguages = { { "en", "English" } };

    std::filesystem::path SettingsFile() { return Platform::AppDataFolder() / "settings.txt"; }

    std::map<std::string, std::string> LoadSettings()
    {
        std::map<std::string, std::string> settings;
        std::ifstream in(SettingsFile());
        for (std::string line; std::getline(in, line);)
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto eq = line.find('=');
            if (eq != std::string::npos)
                settings[line.substr(0, eq)] = line.substr(eq + 1);
        }
        return settings;
    }

    bool SaveSettings(const std::map<std::string, std::string>& settings)
    {
        std::error_code ec;
        std::filesystem::create_directories(SettingsFile().parent_path(), ec);
        std::ofstream out(SettingsFile(), std::ios::trunc);
        for (const auto& [key, value] : settings)
            out << key << "=" << value << "\n";
        return static_cast<bool>(out);
    }

    // Nome declarado na primeira linha do tema ("" se o arquivo não for um tema do app)
    std::string DeclaredThemeName(const std::filesystem::path& file)
    {
        std::ifstream in(file);
        std::string first;
        std::getline(in, first);
        static const std::regex marker(R"(^\s*/\*\s*stillpoint-theme:\s*(.*?)\s*\*/)");
        std::smatch m;
        return std::regex_search(first, m, marker) ? m[1].str() : std::string();
    }

    // Temas instalados (o padrão primeiro, depois em ordem alfabética)
    std::vector<std::string> AvailableThemes()
    {
        std::vector<std::string> themes;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(
                 Platform::ResourcesDirectory() / "assets" / "ui" / "themes", ec))
            if (entry.path().extension() == ".css" && !DeclaredThemeName(entry.path()).empty())
                themes.push_back(entry.path().stem().string());
        std::sort(themes.begin(), themes.end(), [](const std::string& a, const std::string& b)
        {
            if ((a == kDefaultTheme) != (b == kDefaultTheme))
                return a == kDefaultTheme;
            return a < b;
        });
        if (themes.empty())
            themes.push_back(kDefaultTheme);
        return themes;
    }

    // Nome para mostrar: o declarado no arquivo; se faltar, "nocturne-neo" -> "Nocturne Neo"
    std::string ThemeName(const std::string& id)
    {
        const std::string declared =
            DeclaredThemeName(Platform::ResourcesDirectory() / "assets" / "ui" / "themes" / (id + ".css"));
        if (!declared.empty())
            return declared;
        std::string name;
        bool upper = true;
        for (char c : id)
        {
            if (c == '-' || c == '_') { name += ' '; upper = true; continue; }
            name += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            upper = false;
        }
        return name;
    }

    std::string ChosenTheme(const std::map<std::string, std::string>& settings)
    {
        const auto it = settings.find("theme");
        const auto themes = AvailableThemes();
        if (it != settings.end() && std::find(themes.begin(), themes.end(), it->second) != themes.end())
            return it->second;
        return kDefaultTheme;
    }

    std::string ChosenLanguage(const std::map<std::string, std::string>& settings)
    {
        const auto it = settings.find("language");
        for (const auto& [code, name] : kLanguages)
            if (it != settings.end() && it->second == code)
                return code;
        return kLanguages.front().first;
    }

    // Lista para a página: [{"id":"nocturne-neo","name":"Nocturne Neo"}, ...]
    std::string ChoicesJson(const std::vector<std::pair<std::string, std::string>>& choices)
    {
        std::string list = "[";
        for (size_t i = 0; i < choices.size(); ++i)
            list += (i ? "," : "") + std::string("{\"id\":") + JsonString(choices[i].first) + ",\"name\":"
                    + JsonString(choices[i].second) + "}";
        return list + "]";
    }

    // Lê assets/ui/index.html e embute o tema (CSS), as fontes e app.js (o WebKit
    // do Mac não carrega arquivos locais separados de uma página montada em memória).
    // Página da interface (index.html ou live.html) com o tema, as fontes, as imagens e os
    // scripts embutidos (uma string só para a janela)
    std::string LoadPage(const std::string& file, const std::string& theme)
    {
        const auto dir = Platform::ResourcesDirectory() / "assets" / "ui";
        auto read = [](const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            std::stringstream content;
            content << file.rdbuf();
            return content.str();
        };

        std::string html = read(dir / file);
        if (html.empty())
            return "<h1>Stillpoint</h1><p>UI files not found: " + dir.string() + "</p>";

        auto replace = [&html](const std::string& marker, const std::string& content)
        {
            const auto pos = html.find(marker);
            if (pos != std::string::npos)
                html.replace(pos, marker.size(), content);
        };

        // Fontes: url("__FONT:arquivo.ttf__") vira o próprio arquivo embutido
        auto embedFonts = [&](std::string css)
        {
            static const std::regex fontMarker(R"(__FONT:([A-Za-z0-9_.\-]+)__)");
            std::string out;
            auto begin = css.cbegin();
            for (std::sregex_iterator it(css.begin(), css.end(), fontMarker), end; it != end; ++it)
            {
                out.append(begin, css.cbegin() + it->position());
                const std::string font = read(dir / "fonts" / (*it)[1].str());
                out += font.empty() ? std::string() : "data:font/ttf;base64," + Base64Encode(font);
                begin = css.cbegin() + it->position() + it->length();
            }
            out.append(begin, css.cend());
            return out;
        };

        // Imagens: src="__IMAGE:arquivo.png__" vira o próprio arquivo embutido (assets/ui/images)
        auto embedImages = [&](std::string text)
        {
            static const std::regex imageMarker(R"(__IMAGE:([A-Za-z0-9_.\-]+)__)");
            std::string out;
            auto begin = text.cbegin();
            for (std::sregex_iterator it(text.begin(), text.end(), imageMarker), end; it != end; ++it)
            {
                out.append(begin, text.cbegin() + it->position());
                const std::string name = (*it)[1].str();
                const std::string data = read(dir / "images" / name);
                const bool svg = name.size() > 4 && name.compare(name.size() - 4, 4, ".svg") == 0;
                const bool jpg = name.size() > 4 && (name.compare(name.size() - 4, 4, ".jpg") == 0);
                const std::string mime = svg ? "image/svg+xml" : jpg ? "image/jpeg" : "image/png";
                out += data.empty() ? std::string() : "data:" + mime + ";base64," + Base64Encode(data);
                begin = text.cbegin() + it->position() + it->length();
            }
            out.append(begin, text.cend());
            return out;
        };
        html = embedImages(html);

        std::string css = read(dir / "themes" / (theme + ".css"));
        if (css.empty())
            css = read(dir / "themes" / (std::string(kDefaultTheme) + ".css"));
        replace("/*__THEME__*/", embedFonts(css));
        replace("/*__LIVE_CSS__*/", read(dir / "live.css"));
        replace("/*__SCRIPT__*/", read(dir / "app.js"));
        replace("/*__LIVE_SCRIPT__*/", read(dir / "live.js"));
        return html;
    }

    int RunApp()
    {
        Platform::InitConsole();
        Platform::WaitForPreviousInstance();   // aberto pelo "Restart app": espera a cópia anterior fechar

        webview::webview window(false, nullptr);
        window.set_title("Stillpoint");
        window.set_size(1200, 840, WEBVIEW_HINT_NONE);
        window.set_size(960, 680, WEBVIEW_HINT_MIN);

        GuiListener listener(window);
        CanonController controller;
        BulletTime bulletTime(controller, listener);
        PhotoStore store(Platform::AppDataFolder() / "Photos");
        // Sessões salvas e mantidas no app ficam só até fechar: se o app fechou de
        // repente, saem agora
        store.DeleteSaved();
        // Fotos de teste do "Reorder by photo": só servem para ordenar, são apagadas depois
        PhotoStore orderStore(Platform::AppDataFolder() / "Order photos");
        orderStore.DeleteAll();
        PreviewThread previews;

        bool sdkReady = false;
        std::atomic<bool> busy{ false };
        // STILLPOINT_FAKE_CAMERAS=9: câmeras simuladas (sem SDK) para testar a interface
        // sem câmeras. Disparos geram fotos de teste com o número da câmera.
        int fakeCount = 0;
        if (const char* fake = std::getenv("STILLPOINT_FAKE_CAMERAS"))
            try { fakeCount = std::clamp(std::stoi(fake), 1, 64); } catch (...) { fakeCount = 0; }
        std::atomic<bool> fakeCancel{ false };
        std::atomic<bool> allowClose{ false };
        std::atomic<bool> shuttingDown{ false };

        // Tempo real: sem pedidos pendentes, acompanha as câmeras a cada ~0,4 s.
        // Cada câmera conecta em paralelo; a página recebe a lista sempre que algo
        // muda e, enquanto alguma conecta, a cada volta (para mostrar os segundos).
        std::string settingsCameras;  // câmeras da última leitura das configurações
        std::map<std::string, bool> singleShotWarned;  // câmeras que não aceitaram disparo único (já avisadas)
        CameraThread cameras([&]
        {
            if (!sdkReady || busy || shuttingDown)
                return;
            if (!fakeCount)  // câmeras simuladas: sem SDK, só a leitura das configurações abaixo
            {
                if (!controller.HasPendingConnections())
                    EdsGetEvent();  // com uma câmera conectando, o SDK está ocupado
                // Redesenha quando algo mudou, e a cada volta enquanto há câmera conectando
                // ou ligando (para o tempo de espera andar)
                if (controller.MonitorTick(&listener) || controller.HasPendingConnections()
                    || !controller.GetStartingCameras().empty())
                    listener.Emit(CamerasSnapshot(controller));
            }

            // Configurações (avisos no painel e tela Camera settings): lidas de novo quando
            // as câmeras conectadas mudam (com a janela aberta, a página pede a cada 3 s).
            // Out/2026, Mac: com o aviso de "configuração mudada" do SDK ligado, o app fechou ao
            // desligar as câmeras (falha dentro do EdsGetEvent). Suspeita: esse aviso; saiu.
            if (controller.HasPendingConnections())
                return;
            std::string connected;
            for (const auto& cam : controller.GetCameras())
                if (cam.sessionOpen)
                    connected += cam.shortName + "@" + cam.portName + ";";
            if (connected != settingsCameras)
            {
                settingsCameras = connected;
                EmitCameraSettings(controller, listener, singleShotWarned);
            }
        });

        // Mensagens de texto do núcleo vão para o log da página
        SetLogSink([&listener](const std::string& message) { listener.Emit(Json("log").Add("text", message)); });

        // Enquanto uma câmera conecta, o SDK não aceita outros comandos: disparar
        // ou identificar teria de esperar a conexão terminar (até dezenas de segundos)
        auto cameraConnecting = [&]
        {
            if (!controller.HasPendingConnections())
                return false;
            listener.Emit(Json("waitConnecting"));
            return true;
        };

        auto emitSessions = [&]
        {
            listener.Emit(Json("sessions").Raw("sessions", SessionsJson(store)));
        };

        // Executa um pedido na thread das câmeras, avisando a página de "ocupado"
        auto run = [&](const std::string& action, std::function<void()> task)
        {
            if (busy.exchange(true))
                return;
            listener.Emit(Json("busy").Add("busy", true).Add("action", action));
            cameras.Post([&, task]
            {
                task();
                listener.Emit(CamerasSnapshot(controller));
                busy = false;
                listener.Emit(Json("busy").Add("busy", false));
            });
        };

        // Fecha o app de fato (depois que a página já resolveu as fotos).
        //
        // Dado observado no Mac: ao encerrar as câmeras, o SDK da Canon precisa que
        // a thread da janela continue funcionando. Se a janela fechar primeiro e
        // ficar esperando as câmeras, os dois esperam um pelo outro e o app trava.
        // Por isso: a janela continua aberta, a thread das câmeras encerra tudo e
        // só então manda a janela fechar. Se demorar demais, o programa sai mesmo assim.
        std::atomic<bool> camerasClosed{ false };
        auto closeNow = [&]
        {
            if (shuttingDown.exchange(true))
                return;
            store.DeleteSaved();  // as salvas mantidas no app saem ao fechar (a cópia salva fica)
            SetLogSink([](const std::string&) {});
            listener.Emit(Json("closing"));

            // Segurança: se as câmeras não encerrarem (o SDK da Canon travado), o
            // programa sai mesmo assim. As fotos já estão gravadas em disco.
            std::thread([] {
                std::this_thread::sleep_for(std::chrono::seconds(12));
                std::_Exit(0);
            }).detach();

            cameras.Post([&]
            {
                std::thread([] {
                    std::this_thread::sleep_for(std::chrono::seconds(8));
                    std::_Exit(0);  // o SDK não respondeu ao encerrar
                }).detach();

                // Uma câmera no meio da conexão segura o SDK: não dá para encerrar
                // direito. As fotos já estão gravadas em disco.
                if (controller.HasPendingConnections())
                    std::_Exit(0);
                controller.CloseSessions();
                // No Mac, encerrar o SDK aqui (fora da thread principal) às vezes
                // derruba o app: o ImageCaptureCore da Apple lança uma exceção ao
                // parar a busca de câmeras. As sessões já foram fechadas e o app vai
                // sair em seguida, então o sistema libera o resto.
#ifndef __APPLE__
                if (sdkReady)
                    controller.TerminateSdk();
#else
                (void)sdkReady;
#endif
                camerasClosed = true;
                window.dispatch([&]
                {
                    allowClose = true;
                    window.terminate();
                });
            });
        };

        // --- Câmeras e disparo ---

        auto startSdk = [&]
        {
            if (fakeCount)
            {
                // Câmeras simuladas: SL3 e T7 alternadas, todas "conectadas", sem SDK
                auto& list = controller.GetCameras();
                if (list.empty())
                    for (int i = 0; i < fakeCount; ++i)
                    {
                        CameraContext cam;
                        const bool sl3 = i % 2 == 0;
                        cam.modelName = sl3 ? "Canon EOS Rebel SL3" : "Canon EOS Rebel T7";
                        cam.shortName = (sl3 ? "SL3_" : "T7_") + std::to_string(i / 2 + 1);
                        cam.baseShortName = cam.shortName;
                        cam.portName = "fake" + std::to_string(i + 1);
                        cam.orderPrefix = i + 1;
                        cam.sessionOpen = true;
                        cam.fake = true;
                        list.push_back(cam);
                    }
                controller.SetOutputFolder(store.Root().string());
                sdkReady = true;
                Log("Fake cameras: " + std::to_string(fakeCount) + " (STILLPOINT_FAKE_CAMERAS)");
                return;
            }
            sdkReady = controller.InitializeSdk();
            if (!sdkReady)
            {
                listener.Emit(Json("sdkFailed"));
                return;
            }
            controller.SetOutputFolder(store.Root().string());
        };

        const std::string version = AppVersion();
        const std::string previousVersion = SwapLastVersion(STILLPOINT_VERSION);
        UpdateState update;

        // Tema e idioma escolhidos no Settings (valem a partir desta abertura)
        const auto settings = LoadSettings();
        const std::string theme = ChosenTheme(settings);
        const std::string language = ChosenLanguage(settings);
        // Modo debug: vale na hora e desliga sozinho ao fechar o app (não fica esquecido ligado)
        std::atomic<bool> debugMode{ false };
        std::mutex debugFolderMutex;
        std::filesystem::path lastDebugFolder;
        auto rememberDebugFolder = [&](const std::filesystem::path& folder)
        {
            std::lock_guard<std::mutex> lock(debugFolderMutex);
            lastDebugFolder = folder;
        };

        window.bind("uiReady", [&](const std::string&) -> std::string
        {
            // Só liga o SDK: a procura e a conexão das câmeras são automáticas
            cameras.Post([&]
            {
                if (!sdkReady)
                    startSdk();
                listener.Emit(CamerasSnapshot(controller));
            });
            // STILLPOINT_TEST_RESTART=1: mostra o botão "Restart app" o tempo todo, para testar
            // o reinício sem precisar de uma câmera presa conectando
            std::string selfUpdateReason;
            const bool canSelfUpdate = Platform::CanSelfUpdate(selfUpdateReason);
            return Json("startup").Add("unsavedSessions", store.CountUnsaved())
                .Add("testRestart", std::getenv("STILLPOINT_TEST_RESTART") != nullptr)
                .Add("version", version)
                .Add("previousVersion", previousVersion)
#ifdef _WIN32
                .Add("os", "windows")
#else
                .Add("os", "macos")
#endif
                .Add("canSelfUpdate", canSelfUpdate)
                .Add("selfUpdateReason", selfUpdateReason)
                .Add("checkUpdates", std::getenv("STILLPOINT_NO_UPDATE_CHECK") == nullptr)
                .Raw("themes", [&]
                {
                    std::vector<std::pair<std::string, std::string>> themes;
                    for (const auto& id : AvailableThemes())
                        themes.emplace_back(id, ThemeName(id));
                    return ChoicesJson(themes);
                }())
                .Add("theme", theme)
                .Raw("languages", ChoicesJson(kLanguages))
                .Add("language", language)
                .Add("debug", debugMode.load())
                .Add("timing", settings.count("timing") ? settings.at("timing") : std::string())
                .Add("live", settings.count("live") ? settings.at("live") : std::string())
                .Add("fakeCameras", fakeCount).Str();
        });

        // setDebugMode(true/false): liga ou desliga o modo debug (Settings). Não é gravado:
        // ao abrir o app, começa desligado.
        window.bind("setDebugMode", [&](const std::string& request) -> std::string
        {
            debugMode = request.find("true") != std::string::npos;
            return Json("settings").Add("ok", true).Add("debug", debugMode.load())
                .Add("bytes", static_cast<double>(MatchDebug::FolderSize(DebugRoot()))).Str();
        });

        // debugInfo(): espaço ocupado pelos dados de debug (Settings)
        window.bind("debugInfo", [&](const std::string&) -> std::string
        {
            return Json("debugInfo").Add("bytes", static_cast<double>(MatchDebug::FolderSize(DebugRoot()))).Str();
        });

        // deleteDebugData(): apaga todas as pastas de debug
        window.bind("deleteDebugData", [&](const std::string&) -> std::string
        {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(DebugRoot(), ec))
                std::filesystem::remove_all(entry.path(), ec);
            {
                std::lock_guard<std::mutex> lock(debugFolderMutex);
                lastDebugFolder.clear();
            }
            return Json("debugInfo").Add("bytes", static_cast<double>(MatchDebug::FolderSize(DebugRoot()))).Str();
        });

        // openDebugFolder(): abre a pasta do último Test shot com debug (ou a pasta Debug)
        window.bind("openDebugFolder", [&](const std::string&) -> std::string
        {
            std::filesystem::path folder;
            {
                std::lock_guard<std::mutex> lock(debugFolderMutex);
                folder = lastDebugFolder;
            }
            std::error_code ec;
            if (folder.empty() || !std::filesystem::exists(folder, ec))
            {
                folder = DebugRoot();
                std::filesystem::create_directories(folder, ec);
            }
            Platform::OpenInFileManager(folder);
            return "null";
        });

        // saveSettings(tema, idioma): grava no settings.txt. restart: true quando o que
        // foi escolhido é diferente do que está em uso (vale ao abrir de novo).
        // saveTiming(texto): como as câmeras disparam (Timing do painel Bullet time), guardado
        // como a página mandou (JSON numa linha)
        window.bind("saveTiming", [&](const std::string& request) -> std::string
        {
            const auto args = ParseStringArgs(request);
            if (args.empty() || args[0].find('\n') != std::string::npos)
                return Json("settings").Add("ok", false).Str();
            auto current = LoadSettings();
            current["timing"] = args[0];
            return Json("settings").Add("ok", SaveSettings(current)).Str();
        });

        window.bind("saveSettings", [&](const std::string& request) -> std::string
        {
            const auto args = ParseStringArgs(request);
            if (args.size() < 2)
                return Json("settings").Add("ok", false).Str();
            const auto themes = AvailableThemes();
            const bool themeOk = std::find(themes.begin(), themes.end(), args[0]) != themes.end();
            const bool languageOk = std::any_of(kLanguages.begin(), kLanguages.end(),
                                                [&](const auto& l) { return l.first == args[1]; });
            if (!themeOk || !languageOk)
                return Json("settings").Add("ok", false).Str();
            auto current = LoadSettings();
            current["theme"] = args[0];
            current["language"] = args[1];
            const bool ok = SaveSettings(current);
            return Json("settings").Add("ok", ok).Add("restart", ok && (args[0] != theme || args[1] != language)).Str();
        });

        // Solução de problemas: fecha a conexão com todas as câmeras e deixa a
        // procura automática conectar de novo (o SDK continua ligado: desligar e
        // religar o SDK no Mac fazia as câmeras não voltarem)
        window.bind("restartConnections", [&](const std::string&) -> std::string
        {
            run("restart", [&]
            {
                controller.WaitPendingConnections();
                controller.CloseSessions();
                controller.ResetConnectionHistory();
                if (!sdkReady)
                    startSdk();
            });
            return "null";
        });

        // Câmeras simuladas: faz o que RunSequence faz (mesmos avisos para a página),
        // mas em vez de disparar, grava uma foto de teste por item do plano
        auto fakeRun = [&](const SequenceSettings& settings)
        {
            fakeCancel = false;
            bool completed = true;
            for (int sessionIndex = 1; sessionIndex <= settings.sessionCount && completed; ++sessionIndex)
            {
                listener.OnSessionStarted(sessionIndex, settings.sessionCount);
                if (!controller.PrepareSessionFolder(sessionIndex, settings.sessionCount, "FAKE"))
                {
                    listener.OnSessionFolderFailed();
                    return;
                }
                const std::filesystem::path dir = controller.GetCameras().front().currentShotDir;
                std::error_code ec;
                listener.OnCountdownStarted(settings.countdownSeconds);
                for (int s = settings.countdownSeconds; s >= 1 && completed; --s)
                {
                    listener.OnCountdownTick(s);
                    if (settings.playSounds)
                        AudioPlayer::PlayWavAsync(GetAssetAudioPath("countdown.wav").string());
                    for (int step = 0; step < 10 && completed; ++step)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        if (fakeCancel)
                            completed = false;
                    }
                }
                if (!completed)
                {
                    std::filesystem::remove(dir, ec);
                    break;
                }
                listener.OnTrigger();
                if (settings.playSounds)
                    AudioPlayer::PlayWavAsync(GetAssetAudioPath("itsbullettime.wav").string());
                // Plano: vazio = todas ao mesmo tempo, uma foto por câmera
                std::vector<CanonController::ScheduledShot> plan = settings.schedule;
                if (plan.empty())
                    for (const auto& cam : controller.GetCameras())
                        plan.push_back({ cam.shortName, 0 });
                listener.OnWaitingForDownloads();
                const auto start = std::chrono::steady_clock::now();
                int done = 0;
                const bool groupSounds = settings.groupSounds && settings.playSounds;
                int tickedIndex = 0, tickedSecond = 0;
                for (size_t i = 0; i < plan.size(); ++i)
                {
                    const bool scheduled = !settings.schedule.empty();
                    while (true)
                    {
                        if (fakeCancel) { completed = false; break; }
                        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
                        if (elapsed >= plan[i].atMs)
                            break;
                        // Contagem com som a 3, 2 e 1 s de um grupo (intervalo maior que 1,5 s), como no núcleo
                        if (groupSounds && scheduled && i > 0 && plan[i].atMs - plan[i - 1].atMs > 1500)
                        {
                            const int second = static_cast<int>((plan[i].atMs - elapsed + 999) / 1000);
                            if (second >= 1 && second <= 3 && (static_cast<int>(i) != tickedIndex || second != tickedSecond))
                            {
                                tickedIndex = static_cast<int>(i);
                                tickedSecond = second;
                                AudioPlayer::PlayWavAsync(GetAssetAudioPath("countdown.wav").string());
                            }
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<long long>(20, plan[i].atMs - elapsed)));
                    }
                    if (!completed)
                        break;
                    int number = 0;
                    for (const auto& cam : controller.GetCameras())
                        if (cam.shortName == plan[i].shortName)
                        {
                            number = cam.orderPrefix;
                            if (scheduled)
                            {
                                if (settings.groupSounds && settings.playSounds && i > 0 && plan[i].atMs != plan[i - 1].atMs)
                                    AudioPlayer::PlayWavAsync(GetAssetAudioPath("group.wav").string());
                                listener.OnShotFired(cam, static_cast<int>(i) + 1, static_cast<int>(plan.size()), true);
                            }
                        }
                    char prefix[8];
                    std::snprintf(prefix, sizeof(prefix), "%02d", static_cast<int>(i) + 1);
                    WriteTestPhoto(dir / (std::string(prefix) + "_" + plan[i].shortName + "_IMG_" + std::to_string(1000 + i) + ".jpg"), number, static_cast<int>(i) + 1);
                    ++done;
                }
                // "Download": depois dos disparos, para não atrasar os tempos do plano
                for (int i = 0; i < done; ++i)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(120));
                    listener.OnDownloadProgress(i + 1, static_cast<int>(plan.size()));
                }
                if (completed)
                    listener.OnSessionCompleted();
                if (completed && sessionIndex < settings.sessionCount)
                    std::this_thread::sleep_for(std::chrono::milliseconds(600));
            }
            if (!completed)
                listener.OnSequenceCancelled();
            listener.OnSequenceFinished(completed);
        };

        // Plano do disparo com intervalos: "câmera:ms,câmera:ms,..." em ordem de tempo
        auto parseSchedule = [](const std::string& text)
        {
            std::vector<CanonController::ScheduledShot> plan;
            std::stringstream ss(text);
            std::string part;
            while (std::getline(ss, part, ','))
            {
                const auto colon = part.rfind(':');
                if (colon == std::string::npos || colon == 0)
                    continue;
                try { plan.push_back({ part.substr(0, colon), std::max(0, std::stoi(part.substr(colon + 1))) }); }
                catch (...) {}
            }
            std::stable_sort(plan.begin(), plan.end(), [](const auto& a, const auto& b) { return a.atMs < b.atMs; });
            return plan;
        };

        // shoot(contagem, sessões, espera, plano): plano vazio = todas ao mesmo tempo
        window.bind("shoot", [&](const std::string& request) -> std::string
        {
            // A página manda os quatro como texto: "3", "1", "10", "SL3_1:0,T7_1:500"
            const auto args = ParseStringArgs(request);
            SequenceSettings settings;
            auto number = [&](size_t i, int fallback) { try { return std::stoi(args.at(i)); } catch (...) { return fallback; } };
            if (args.size() >= 3)
            {
                settings.countdownSeconds = std::max(0, number(0, 0));
                settings.sessionCount = std::max(1, number(1, 1));
                settings.downloadWaitSeconds = std::max(1, number(2, 10));
            }
            if (args.size() > 3)
                settings.schedule = parseSchedule(args[3]);
            if (args.size() > 4)
            {
                // "som do disparo,som dos grupos" (1/0); só o segundo = compatível com "1"
                const std::string flags = args[4];
                const auto comma = flags.find(',');
                if (comma == std::string::npos)
                    settings.groupSounds = flags == "1";
                else
                {
                    settings.playSounds = flags.substr(0, comma) == "1";
                    settings.groupSounds = flags.substr(comma + 1) == "1";
                }
            }
            // A janela Live recebe o plano inteiro antes de começar: cada item com a
            // câmera (nome e número) e o instante em ms. Plano vazio = todas juntas no 0.
            {
                std::string items;
                auto addItem = [&](const std::string& shortName, int atMs)
                {
                    int number = 0;
                    for (const auto& cam : controller.GetCameras())
                        if (cam.shortName == shortName)
                            number = cam.orderPrefix;
                    items += std::string(items.empty() ? "" : ",") + "{\"cam\":" + JsonString(shortName)
                        + ",\"number\":" + std::to_string(number) + ",\"t\":" + std::to_string(atMs) + "}";
                };
                if (settings.schedule.empty())
                {
                    for (const auto& cam : controller.GetCameras())
                        if (cam.sessionOpen)
                            addItem(cam.shortName, 0);
                }
                else
                    for (const auto& item : settings.schedule)
                        addItem(item.shortName, item.atMs);
                listener.Emit(Json("livePlan").Add("countdown", settings.countdownSeconds).Add("sessions", settings.sessionCount)
                    .Add("all", settings.schedule.empty()).Raw("shots", "[" + items + "]"));
            }
            run("shoot", [&, settings]
            {
                if (fakeCount) { fakeRun(settings); emitSessions(); return; }
                if (cameraConnecting())
                    return;
                EnsureSingleShot(controller, singleShotWarned);  // uma foto por câmera
                bulletTime.RunSequence(settings);
                emitSessions();
            });
            return "null";
        });

        // cancelShoot(): para o bullet time em andamento (contagem, entre sessões ou
        // esperando as fotos). Só avisa o núcleo; ele para no próximo décimo de segundo.
        window.bind("cancelShoot", [&](const std::string&) -> std::string
        {
            fakeCancel = true;
            bulletTime.RequestCancel();
            return "null";
        });

        // readCameraSettings(): lê de novo as configurações de todas as câmeras
        window.bind("readCameraSettings", [&](const std::string&) -> std::string
        {
            cameras.Post([&]
            {
                if (busy || controller.HasPendingConnections())
                    return;
                EmitCameraSettings(controller, listener, singleShotWarned);
            });
            return "null";
        });

        // setCameraSetting(câmera ou "*", chave, valor): muda na hora; "*" = todas as
        // câmeras que dá para ajustar. Responde com cameraSettingResult e a leitura nova.
        window.bind("setCameraSetting", [&](const std::string& request) -> std::string
        {
            const auto args = ParseStringArgs(request);
            if (args.size() < 3)
                return "null";
            const std::string target = args[0], key = args[1];
            long long value = 0;
            try { value = std::stoll(args[2]); } catch (...) { return "null"; }
            cameras.Post([&, target, key, value]
            {
                auto result = Json("cameraSettingResult").Add("camera", target).Add("key", key);
                if (busy || controller.HasPendingConnections())
                {
                    listener.Emit(result.Add("ok", false).Add("error", busy ? "Stillpoint is busy. Try again when it finishes."
                                                                             : "A camera is connecting. Try again when it is ready."));
                    return;
                }
                std::vector<std::string> errors;
                for (const auto& cam : controller.GetCameras())
                {
                    if (!cam.sessionOpen || (target != "*" && cam.shortName != target))
                        continue;
                    if (target == "*" && !CameraSettingsCanon::Read(cam).adjustable)
                        continue;
                    std::string error;
                    if (!CameraSettingsCanon::Write(cam, key, value, error))
                        errors.push_back(cam.shortName + ": " + error);
                }
                std::string text;
                for (const auto& e : errors)
                    text += (text.empty() ? "" : " ") + e;
                listener.Emit(result.Add("ok", errors.empty()).Add("error", text));
                if (!fakeCount)
                    EdsGetEvent();
                EmitCameraSettings(controller, listener, singleShotWarned);
            });
            return "null";
        });

        window.bind("identify", [&](const std::string& request) -> std::string
        {
            const auto args = ParseStringArgs(request);
            if (args.empty())
                return "null";
            const std::string name = args[0];
            run("identify", [&, name]
            {
                if (cameraConnecting())
                    return;
                for (const auto& cam : controller.GetCameras())
                {
                    if (cam.shortName == name)
                        bulletTime.TestCamera(cam);
                }
            });
            return "null";
        });

        window.bind("setPrefix", [&](const std::string& request) -> std::string
        {
            const auto names = ParseStringArgs(request);
            const auto numbers = ParseIntArgs(request);
            if (names.empty() || numbers.empty())
                return "null";
            const std::string name = names[0];
            const int prefix = numbers.back();
            run("reorder", [&, name, prefix] { controller.SetCameraPrefix(name, prefix); });
            return "null";
        });

        // --- Sessões e fotos ---

        window.bind("listSessions", [&](const std::string&) -> std::string
        {
            return SessionsJson(store);
        });

        // getPhoto(sessão, arquivo, tamanho) -> "data:image/jpeg;base64,..." (ou null)
        auto bindPhotos = [&](webview::webview& target, const std::string& name, PhotoStore& source)
        {
            PhotoStore* from = &source;
            webview::webview* to = &target;
            target.bind(name, [&, from, to](const std::string& id, const std::string& request, void*)
            {
                const auto args = ParseStringArgs(request);
                const auto numbers = ParseIntArgs(request);
                const std::string session = args.size() > 0 ? args[0] : "";
                const std::string file = args.size() > 1 ? args[1] : "";
                const int size = numbers.empty() ? 1600 : std::clamp(numbers.back(), 64, 4000);

                // from e to por cópia: a biblioteca chama esta função numa cópia temporária
                // do fecho, então uma referência a eles ficaria pendurada quando a thread
                // das prévias rodasse (foi um travamento no Mac em 2026-10-16)
                previews.Post([&, from, to, id, session, file, size]
                {
                    const std::string jpeg = from->Preview(session, file, size);
                    to->resolve(id, 0, jpeg.empty() ? "null" : JsonString("data:image/jpeg;base64," + Base64Encode(jpeg)));
                });
            }, nullptr);
        };
        bindPhotos(window, "getPhoto", store);
        bindPhotos(window, "getOrderPhoto", orderStore);

        // ---------------------------------------------------------------
        // Janelas Live (assets/ui/live.html): palco do disparo numa janela separada,
        // tela cheia num monitor/TV ou flutuante. Até duas (ex.: Operator no monitor,
        // Audience na TV). Criadas na thread da janela e nunca destruídas enquanto o
        // app roda: fechar só esconde; Live view traz de volta. Recebem os mesmos
        // avisos do núcleo que a página principal (GuiListener::AddWindow).
        // Opções por janela em settings.txt (live1=, live2=); o som é do app (live=).
        // ---------------------------------------------------------------
        struct LiveWindow
        {
            int index = 0;
            std::unique_ptr<webview::webview> view;
        };
        std::vector<std::unique_ptr<LiveWindow>> lives;

        // Som do disparo: {"sound":…,"sounds":…} guardado em settings.txt (live=)
        auto soundSettings = [&]
        {
            const auto current = LoadSettings();
            const std::string text = current.count("live") ? current.at("live") : "";
            return std::make_pair(text.find("\"sound\":false") == std::string::npos,
                                  text.find("\"sounds\":false") == std::string::npos);
        };
        auto soundEvent = [&]
        {
            const auto [sound, sounds] = soundSettings();
            return Json("soundChanged").Add("sound", sound).Add("sounds", sounds);
        };
        // liveSetSound(som, grupos): "1"/"0"; avisa todas as janelas
        auto bindSetSound = [&](webview::webview& target)
        {
            target.bind("liveSetSound", [&](const std::string& request) -> std::string
            {
                const auto args = ParseStringArgs(request);
                if (args.size() < 2)
                    return "null";
                auto current = LoadSettings();
                current["live"] = std::string("{\"sound\":") + (args[0] == "1" ? "true" : "false")
                    + ",\"sounds\":" + (args[1] == "1" ? "true" : "false") + "}";
                SaveSettings(current);
                listener.Emit(soundEvent());
                return "null";
            });
        };
        bindSetSound(window);

        auto screensJson = []
        {
            std::string list = "[";
            bool first = true;
            for (const auto& screen : NativeWindow::Screens())
            {
                list += (first ? "" : ",") + Json("screen").Add("name", screen.name).Add("primary", screen.primary).Str();
                first = false;
            }
            return list + "]";
        };

        std::function<void(int)> openLive = [&](int index)
        {
            for (auto& live : lives)
                if (live->index == index)
                {
                    if (auto native = live->view->window(); native.ok())
                        NativeWindow::Show(native.value());
                    return;
                }
            auto live = std::make_unique<LiveWindow>();
            live->index = index;
            live->view = std::make_unique<webview::webview>(false, nullptr);
            webview::webview& v = *live->view;
            const std::string key = "live" + std::to_string(index);
            v.set_title(index == 1 ? "Stillpoint · Live" : "Stillpoint · Live " + std::to_string(index));
            v.set_size(960, 560, WEBVIEW_HINT_NONE);
            v.set_size(480, 300, WEBVIEW_HINT_MIN);

            // liveReady(): opções desta janela, som, monitores e a última lista de câmeras
            v.bind("liveReady", [&, index, key](const std::string&) -> std::string
            {
                const auto current = LoadSettings();
                const auto [sound, sounds] = soundSettings();
                const std::string cameras = listener.LastCameras();
                return Json("liveReady").Add("index", index)
                    .Add("opts", current.count(key) ? current.at(key) : std::string())
                    .Raw("sound", Json("sound").Add("sound", sound).Add("sounds", sounds).Str())
                    .Raw("screens", screensJson())
                    .Raw("cameraEvent", cameras.empty() ? "null" : cameras).Str();
            });
            // O disparo e o cancelamento passam pela página principal (o plano do Timing é dela)
            v.bind("liveShoot", [&](const std::string&) -> std::string
            {
                window.dispatch([&] { window.eval("window.liveShootRequest && window.liveShootRequest()"); });
                return "null";
            });
            v.bind("liveCancel", [&](const std::string&) -> std::string
            {
                window.dispatch([&] { window.eval("window.liveCancelRequest && window.liveCancelRequest()"); });
                return "null";
            });
            // saveLiveWindow(índice, JSON): modo, layout, display, monitor, sempre à frente
            v.bind("saveLiveWindow", [&, key](const std::string& request) -> std::string
            {
                const auto args = ParseStringArgs(request);
                if (args.size() < 2 || args[1].find('\n') != std::string::npos)
                    return "null";
                auto current = LoadSettings();
                current[key] = args[1];
                SaveSettings(current);
                return "null";
            });
            // liveDisplay(índice, "window"|"full", monitor, sempre à frente "1"/"0")
            webview::webview* self = &v;
            v.bind("liveDisplay", [self](const std::string& request) -> std::string
            {
                const auto args = ParseStringArgs(request);
                if (args.size() < 4)
                    return "null";
                auto native = self->window();
                if (!native.ok())
                    return "null";
                if (args[1] == "full")
                    NativeWindow::SetFullScreen(native.value(), args[2]);
                else
                    NativeWindow::SetWindowed(native.value(), args[3] == "1");
                return "null";
            });
            v.bind("liveClose", [self](const std::string&) -> std::string
            {
                if (auto native = self->window(); native.ok())
                    NativeWindow::Hide(native.value());
                return "null";
            });
            v.bind("liveAnother", [&](const std::string&) -> std::string
            {
                openLive(2);
                return "null";
            });
            bindSetSound(v);
            bindPhotos(v, "getPhoto", store);

            // O botão de fechar só esconde a janela (ela volta com Live view)
            if (auto native = v.window(); native.ok())
            {
                void* handle = native.value();
                NativeWindow::InstallCloseGuard(handle, [handle]
                {
                    NativeWindow::Hide(handle);
                    return false;
                });
            }
            v.set_html(LoadPage("live.html", theme));
            listener.AddWindow(&v);
            lives.push_back(std::move(live));
        };
        // openLive(índice): abre (ou traz de volta) a janela Live 1 ou 2
        window.bind("openLive", [&](const std::string& request) -> std::string
        {
            const auto numbers = ParseIntArgs(request);
            const auto args = ParseStringArgs(request);
            int index = !numbers.empty() ? numbers[0] : (!args.empty() ? std::atoi(args[0].c_str()) : 1);
            openLive(std::clamp(index, 1, 2));
            return "null";
        });

        // --- Reordenar por foto ---

        // takeOrderPhotos(espera de download): uma foto de cada câmera, sem som,
        // guardada à parte (não vira sessão)
        window.bind("takeOrderPhotos", [&](const std::string& request) -> std::string
        {
            const auto values = ParseIntArgs(request);
            SequenceSettings settings;
            settings.countdownSeconds = 0;
            settings.sessionCount = 1;
            settings.downloadWaitSeconds = values.empty() ? 10 : std::max(1, values[0]);
            settings.playSounds = false;
            run("orderPhotos", [&, settings]
            {
                if (cameraConnecting())
                    return;
                orderStore.DeleteAll();
                EnsureSingleShot(controller, singleShotWarned);  // uma foto por câmera
                const std::string previous = controller.GetOutputFolder();
                controller.SetOutputFolder(orderStore.Root().string());
                if (fakeCount)
                    fakeRun(settings);
                else
                    bulletTime.RunSequence(settings);
                controller.SetOutputFolder(previous);
                if (debugMode)
                {
                    const auto sessions = orderStore.ListSessions();
                    MatchDebug debug;
                    if (!sessions.empty() && debug.Begin(DebugRoot(), "test"))
                    {
                        debug.Shot(1, "Test shot", DebugPhotos(controller, orderStore, sessions.back(), nullptr));
                        rememberDebugFolder(debug.Folder());
                        listener.Emit(Json("debugSaved").Add("folder", PathToUtf8(debug.Folder())));
                    }
                }
                listener.Emit(Json("orderPhotos").Raw("sessions", SessionsJson(orderStore)));
            });
            return "null";
        });

        // cancelTest(): para o Test shot (com ou sem ajuste) entre uma foto e outra ou
        // esperando as fotos; com ajuste, as câmeras voltam como estavam.
        std::atomic<bool> testCancel{ false };
        // Cartão cinza: depois da primeira foto, o Match espera a pessoa conferir (ou
        // corrigir com um clique) onde está o cartão em cada foto
        struct CardPick
        {
            std::mutex mutex;
            std::condition_variable wake;
            bool confirmed = false;
            std::map<std::string, ColorMatch::Region> regions;      // por câmera
            std::map<std::string, std::filesystem::path> files;     // a foto 1 de cada câmera
            std::map<std::string, Chart::Quad> quads;               // cartela: cantos por câmera
        } cardPick;
        auto quadJson = [](const std::string& camera, const Chart::Quad& q)
        {
            std::string xs = "[", ys = "[";
            for (int k = 0; k < 4; ++k)
            {
                xs += (k ? "," : "") + std::to_string(q.x[k]);
                ys += (k ? "," : "") + std::to_string(q.y[k]);
            }
            return Json("quad").Add("camera", camera).Add("found", q.found).Raw("x", xs + "]").Raw("y", ys + "]").Str();
        };
        auto regionJson = [](const std::string& camera, const ColorMatch::Region& r)
        {
            return Json("region").Add("camera", camera).Add("found", r.found)
                .Add("x0", r.x0).Add("y0", r.y0).Add("x1", r.x1).Add("y1", r.y1).Str();
        };
        window.bind("cancelTest", [&](const std::string&) -> std::string
        {
            testCancel = true;
            bulletTime.RequestCancel();
            cardPick.wake.notify_all();
            return "null";
        });

        // autoMatch(espera): "Test shot" do Camera settings com "Match automatically".
        // Tira as fotos de teste (as mesmas temporárias do "Reorder by photo") e deixa as
        // câmeras o mais parecidas possível (auto_match.h). Avisa o andamento
        // (autoMatchProgress), mostra as últimas fotos (orderPhotos) e o resumo do que
        // mudou (autoMatchResult).
        // grayCardFind("câmera", "x", "y"): a pessoa clicou no cartão (x, y de 0 a 1000)
        window.bind("grayCardFind", [&](const std::string& request) -> std::string
        {
            const auto args = ParseStringArgs(request);
            if (args.size() < 3)
                return "null";
            std::filesystem::path file;
            {
                std::lock_guard<std::mutex> lock(cardPick.mutex);
                const auto it = cardPick.files.find(args[0]);
                if (it == cardPick.files.end())
                    return "null";
                file = it->second;
            }
            int x = 500, y = 500;
            try { x = std::stoi(args[1]); y = std::stoi(args[2]); } catch (...) {}
            const ColorMatch::Region region = ColorMatch::GrayCardAt(file, x / 1000.0, y / 1000.0);
            std::lock_guard<std::mutex> lock(cardPick.mutex);
            cardPick.regions[args[0]] = region;
            return regionJson(args[0], region);
        });

        // grayCardSet("câmera", "x0", "y0", "x1", "y1"): a pessoa desenhou a área do cartão
        // (de 0 a 1000)
        window.bind("grayCardSet", [&](const std::string& request) -> std::string
        {
            const auto args = ParseStringArgs(request);
            if (args.size() < 5)
                return "null";
            double v[4] = { 0, 0, 1000, 1000 };
            try { for (int i = 0; i < 4; ++i) v[i] = std::clamp(std::stod(args[i + 1]), 0.0, 1000.0) / 1000.0; }
            catch (...) { return "null"; }
            ColorMatch::Region region;
            region.found = v[2] > v[0] && v[3] > v[1];
            region.x0 = v[0]; region.y0 = v[1]; region.x1 = v[2]; region.y1 = v[3];
            std::lock_guard<std::mutex> lock(cardPick.mutex);
            if (!cardPick.files.count(args[0]) || !region.found)
                return "null";
            cardPick.regions[args[0]] = region;
            return regionJson(args[0], region);
        });

        // grayCardConfirm(): o cartão está certo em todas; o Match continua
        window.bind("grayCardConfirm", [&](const std::string&) -> std::string
        {
            {
                std::lock_guard<std::mutex> lock(cardPick.mutex);
                cardPick.confirmed = true;
            }
            cardPick.wake.notify_all();
            return "null";
        });

        window.bind("autoMatch", [&](const std::string& request) -> std::string
        {
            const auto values = ParseIntArgs(request);
            const bool grayCard = values.size() > 1 && values[1] != 0;
            SequenceSettings settings;
            settings.countdownSeconds = 0;
            settings.sessionCount = 1;
            settings.downloadWaitSeconds = values.empty() ? 10 : std::max(1, values[0]);
            settings.playSounds = false;
            run("orderPhotos", [&, settings, grayCard]
            {
                if (cameraConnecting())
                    return;
                testCancel = false;
                {
                    std::lock_guard<std::mutex> lock(cardPick.mutex);
                    cardPick.confirmed = false;
                    cardPick.regions.clear();
                    cardPick.files.clear();
                }
                MatchDebug debug;
                if (debugMode)
                    debug.Begin(DebugRoot(), grayCard ? "match-graycard" : "match");
                auto shoot = [&](int shot, int shots, const std::string& stage,
                                 std::map<std::string, ColorMatch::Stats>& measured) -> bool
                {
                    if (testCancel)
                        return false;
                    listener.Emit(Json("autoMatchProgress").Add("shot", shot).Add("shots", shots).Add("stage", stage));
                    orderStore.DeleteAll();
                    EnsureSingleShot(controller, singleShotWarned);
                    const std::string previous = controller.GetOutputFolder();
                    controller.SetOutputFolder(orderStore.Root().string());
                    bulletTime.RunSequence(settings);
                    controller.SetOutputFolder(previous);
                    const auto sessions = orderStore.ListSessions();
                    if (sessions.empty() || testCancel)
                        return false;
                    // Uma foto por câmera (JPEG quando houver RAW + JPEG)
                    const StoredSession& session = sessions.back();
                    std::map<std::string, const StoredPhoto*> picks;
                    for (const auto& p : session.photos)
                        if (!picks.count(p.camera) || IsJpegPhoto(p.file))
                            picks[p.camera] = &p;
                    if (grayCard && shot == 1)
                    {
                        // Acha o cartão em cada foto e espera a pessoa conferir
                        std::string regions = "[";
                        {
                            std::lock_guard<std::mutex> lock(cardPick.mutex);
                            for (const auto& [camera, photo] : picks)
                                if (photo->previewable)
                                {
                                    const auto file = orderStore.Root() / session.name / photo->file;
                                    cardPick.files[camera] = file;
                                    cardPick.regions[camera] = ColorMatch::FindGrayCard(file);
                                    regions += (regions.size() > 1 ? "," : "") + regionJson(camera, cardPick.regions[camera]);
                                }
                        }
                        listener.Emit(Json("grayCardPick").Raw("sessions", SessionsJson(orderStore)).Raw("regions", regions + "]"));
                        std::unique_lock<std::mutex> lock(cardPick.mutex);
                        while (!cardPick.confirmed && !testCancel)
                        {
                            cardPick.wake.wait_for(lock, std::chrono::milliseconds(200));
                            lock.unlock();
                            EdsGetEvent();  // as câmeras continuam respondendo enquanto espera
                            lock.lock();
                        }
                        if (testCancel)
                            return false;
                        for (const auto& [camera, r] : cardPick.regions)
                            debug.Line("Gray card " + camera + ": " + (r.found ? std::to_string(r.x0).substr(0, 5) + "," + std::to_string(r.y0).substr(0, 5)
                                                                        + " to " + std::to_string(r.x1).substr(0, 5) + "," + std::to_string(r.y1).substr(0, 5)
                                                                      : std::string("not marked")));
                    }
                    for (const auto& [camera, photo] : picks)
                        if (photo->previewable)
                        {
                            const auto file = orderStore.Root() / session.name / photo->file;
                            if (!grayCard)
                                measured[camera] = ColorMatch::Measure(file);
                            else
                            {
                                std::lock_guard<std::mutex> lock(cardPick.mutex);
                                const auto it = cardPick.regions.find(camera);
                                if (it != cardPick.regions.end())
                                    measured[camera] = ColorMatch::Measure(file, &it->second);
                            }
                        }
                    if (debug.Active())
                        debug.Shot(shot, stage, DebugPhotos(controller, orderStore, session, &measured));
                    return true;
                };
                auto trace = [&](const std::string& line)
                {
                    debug.Line(line);
                    Log("[match] " + line);
                };
                AutoMatch::Options options;
                options.grayCard = grayCard;
                options.chartFile = ChartCalibrationFile();
                const AutoMatch::Result result = AutoMatch::Run(controller, shoot, CalibrationFile(), [&] { return testCancel.load(); }, options,
                                                                debug.Active() ? std::function<void(const std::string&)>(trace) : nullptr);
                if (debug.Active())
                {
                    debug.Line(result.cancelled ? "Result: cancelled" : result.ok ? "Result: done" : "Result: " + result.error);
                    for (const auto& change : result.changes)
                        for (const auto& item : change.items)
                            debug.Line("  " + change.camera + ": " + item[0] + " " + item[1] + " -> " + item[2]);
                    for (const auto& note : result.notes)
                        debug.Line("  note: " + note);
                    rememberDebugFolder(debug.Folder());
                }

                std::string changes = "[";
                for (size_t i = 0; i < result.changes.size(); ++i)
                {
                    std::string items = "[";
                    for (size_t j = 0; j < result.changes[i].items.size(); ++j)
                    {
                        const auto& item = result.changes[i].items[j];
                        items += (j ? "," : "") + std::string("[") + JsonString(item[0]) + "," + JsonString(item[1]) + ","
                                 + JsonString(item[2]) + "]";
                    }
                    changes += (i ? "," : "") + Json("change").Add("camera", result.changes[i].camera).Raw("items", items + "]").Str();
                }
                changes += "]";
                std::string notes = "[";
                for (size_t i = 0; i < result.notes.size(); ++i)
                    notes += (i ? "," : "") + JsonString(result.notes[i]);
                notes += "]";

                listener.Emit(Json("orderPhotos").Raw("sessions", SessionsJson(orderStore)));
                listener.Emit(Json("autoMatchResult").Add("ok", result.ok).Add("matched", result.matched)
                                  .Add("cancelled", result.cancelled)
                                  .Add("debugFolder", debug.Active() ? PathToUtf8(debug.Folder()) : std::string())
                                  .Add("error", result.error).Raw("changes", changes).Raw("notes", notes));
                EmitCameraSettings(controller, listener, singleShotWarned);
            });
            return "null";
        });

        // openChart(): abre a cartela (PDF) no programa padrão, para imprimir
        window.bind("openChart", [&](const std::string&) -> std::string
        {
            const auto file = Platform::ResourcesDirectory() / "assets" / "chart" / "stillpoint-chart.pdf";
            std::error_code ec;
            const bool ok = std::filesystem::exists(file, ec);
            if (ok)
                Platform::OpenInFileManager(file);
            return Json("chart").Add("ok", ok).Str();
        });

        // chartSetQuad("câmera", x0, y0, x1, y1, x2, y2, x3, y3): a pessoa arrastou os cantos
        // (centros das marcas, de 0 a 1000, na ordem da cartela)
        window.bind("chartSetQuad", [&](const std::string& request) -> std::string
        {
            const auto args = ParseStringArgs(request);
            if (args.size() < 9)
                return "null";
            Chart::Quad q;
            try
            {
                for (int k = 0; k < 4; ++k)
                {
                    q.x[k] = std::clamp(std::stod(args[1 + 2 * k]), 0.0, 1000.0) / 1000.0;
                    q.y[k] = std::clamp(std::stod(args[2 + 2 * k]), 0.0, 1000.0) / 1000.0;
                }
            }
            catch (...) { return "null"; }
            q.found = true;
            std::lock_guard<std::mutex> lock(cardPick.mutex);
            if (!cardPick.files.count(args[0]))
                return "null";
            cardPick.quads[args[0]] = q;
            return quadJson(args[0], q);
        });

        // chartCalibrate(espera): calibra os modelos com a cartela (auto_match.h,
        // CalibrateChart). Depois da 1ª foto, a pessoa confere os cantos (chartPick).
        // Avisa o andamento (chartProgress) e o resultado (chartResult).
        window.bind("chartCalibrate", [&](const std::string& request) -> std::string
        {
            const auto values = ParseIntArgs(request);
            SequenceSettings settings;
            settings.countdownSeconds = 0;
            settings.sessionCount = 1;
            settings.downloadWaitSeconds = values.empty() ? 10 : std::max(1, values[0]);
            settings.playSounds = false;
            run("orderPhotos", [&, settings]
            {
                if (cameraConnecting())
                    return;
                testCancel = false;
                {
                    std::lock_guard<std::mutex> lock(cardPick.mutex);
                    cardPick.confirmed = false;
                    cardPick.quads.clear();
                    cardPick.files.clear();
                }
                MatchDebug debug;
                if (debugMode)
                    debug.Begin(DebugRoot(), "chart");
                auto shoot = [&](int shot, int shots, const std::string& stage,
                                 std::map<std::string, Chart::Reading>& measured) -> bool
                {
                    if (testCancel)
                        return false;
                    listener.Emit(Json("chartProgress").Add("shot", shot).Add("shots", shots).Add("stage", stage));
                    orderStore.DeleteAll();
                    EnsureSingleShot(controller, singleShotWarned);
                    const std::string previous = controller.GetOutputFolder();
                    controller.SetOutputFolder(orderStore.Root().string());
                    bulletTime.RunSequence(settings);
                    controller.SetOutputFolder(previous);
                    const auto sessions = orderStore.ListSessions();
                    if (sessions.empty() || testCancel)
                        return false;
                    const StoredSession& session = sessions.back();
                    std::map<std::string, const StoredPhoto*> picks;
                    for (const auto& p : session.photos)
                        if (!picks.count(p.camera) || IsJpegPhoto(p.file))
                            picks[p.camera] = &p;
                    if (shot == 1)
                    {
                        // Acha a cartela em cada foto e espera a pessoa conferir os cantos
                        std::string quads = "[";
                        {
                            std::lock_guard<std::mutex> lock(cardPick.mutex);
                            for (const auto& [camera, photo] : picks)
                                if (photo->previewable)
                                {
                                    const auto file = orderStore.Root() / session.name / photo->file;
                                    cardPick.files[camera] = file;
                                    cardPick.quads[camera] = Chart::Find(file);
                                    quads += (quads.size() > 1 ? "," : "") + quadJson(camera, cardPick.quads[camera]);
                                }
                        }
                        listener.Emit(Json("chartPick").Raw("sessions", SessionsJson(orderStore)).Raw("quads", quads + "]"));
                        std::unique_lock<std::mutex> lock(cardPick.mutex);
                        while (!cardPick.confirmed && !testCancel)
                        {
                            cardPick.wake.wait_for(lock, std::chrono::milliseconds(200));
                            lock.unlock();
                            EdsGetEvent();  // as câmeras continuam respondendo enquanto espera
                            lock.lock();
                        }
                        if (testCancel)
                            return false;
                    }
                    for (const auto& [camera, photo] : picks)
                        if (photo->previewable)
                        {
                            std::lock_guard<std::mutex> lock(cardPick.mutex);
                            const auto it = cardPick.quads.find(camera);
                            if (it == cardPick.quads.end() || !it->second.found)
                            {
                                Chart::Reading none;
                                none.problem = "the chart corners were not marked";
                                measured[camera] = none;
                            }
                            else
                                measured[camera] = Chart::Measure(orderStore.Root() / session.name / photo->file, it->second);
                        }
                    if (debug.Active())
                        debug.Shot(shot, stage, DebugPhotos(controller, orderStore, session, nullptr));
                    return true;
                };
                auto trace = [&](const std::string& line)
                {
                    debug.Line(line);
                    Log("[chart] " + line);
                };
                const AutoMatch::ChartResult result = AutoMatch::CalibrateChart(
                    controller, shoot, ChartCalibrationFile(), [&] { return testCancel.load(); },
                    debug.Active() ? std::function<void(const std::string&)>(trace) : nullptr);
                if (debug.Active())
                {
                    debug.Line(result.cancelled ? "Result: cancelled" : result.ok ? "Result: done" : "Result: " + result.error);
                    for (const auto& line : result.models)
                        debug.Line("  " + line);
                    for (const auto& note : result.notes)
                        debug.Line("  note: " + note);
                    rememberDebugFolder(debug.Folder());
                }
                auto list = [](const std::vector<std::string>& items)
                {
                    std::string out = "[";
                    for (size_t i = 0; i < items.size(); ++i)
                        out += (i ? "," : "") + JsonString(items[i]);
                    return out + "]";
                };
                listener.Emit(Json("orderPhotos").Raw("sessions", SessionsJson(orderStore)));
                listener.Emit(Json("chartResult").Add("ok", result.ok).Add("cancelled", result.cancelled)
                                  .Add("debugFolder", debug.Active() ? PathToUtf8(debug.Folder()) : std::string())
                                  .Add("error", result.error).Raw("models", list(result.models)).Raw("notes", list(result.notes)));
                EmitCameraSettings(controller, listener, singleShotWarned);
            });
            return "null";
        });

        // setOrder(["SL3_1", "T7_1", ...]): nova ordem completa
        window.bind("setOrder", [&](const std::string& request) -> std::string
        {
            const auto names = ParseStringArgs(request);
            run("reorder", [&, names] { controller.SetCameraOrder(names); });
            return "null";
        });

        window.bind("discardOrderPhotos", [&](const std::string&) -> std::string
        {
            previews.Post([&] { orderStore.DeleteAll(); });
            return "null";
        });

        // saveSessions(exitAfter): escolhe a pasta e move todas as sessões para lá
        // saveSessions("exit" ou "", "delete" ou "", sessão, sessão...): salva as sessões
        // escolhidas (nenhuma = todas). "exit" fecha o app depois; "delete" apaga as
        // sessões que não foram escolhidas, só se o salvamento deu certo.
        window.bind("saveSessions", [&](const std::string& request) -> std::string
        {
            auto args = ParseStringArgs(request);
            const bool exitAfter = !args.empty() && args[0] == "exit";
            const bool deleteRest = args.size() > 1 && args[1] == "delete";
            const std::vector<std::string> names(args.begin() + std::min<size_t>(args.size(), 2), args.end());
            const std::string folder = NativeWindow::ChooseFolder("Choose where to save your bullet time sessions");
            if (folder.empty())
            {
                listener.Emit(Json("saveCancelled"));
                return "null";
            }

            listener.Emit(Json("saving").Add("folder", folder));
            previews.Post([&, folder, exitAfter, deleteRest, names]
            {
                std::string error;
                std::vector<std::filesystem::path> savedTo;
                const int moved = store.SaveSessions(folder, names, error, &savedTo);
                // Mostra o resultado: uma sessão -> a pasta dela; várias -> a pasta escolhida
                if (moved > 0)
                    Platform::OpenInFileManager(moved == 1 && savedTo.size() == 1 ? savedTo.front()
                                                                                   : std::filesystem::path(folder));
                if (error.empty() && deleteRest && !names.empty())
                    for (const auto& session : store.ListSessions())
                        if (std::find(names.begin(), names.end(), session.name) == names.end())
                            store.DeleteSession(session.name);
                listener.Emit(Json("saved").Add("count", moved).Add("folder", folder).Add("error", error));
                emitSessions();
                if (exitAfter && error.empty())
                    window.dispatch([&] { closeNow(); });
            });
            return "null";
        });

        // --- Exportação: fotos convertidas ou sequência ---
        // exportFormats() -> formatos de foto que este computador não grava, com o motivo
        window.bind("exportFormats", [&](const std::string&) -> std::string
        {
            std::string reason;
            const bool heic = Export::FormatAvailable("heic", reason);
            return Json("formats").Add("heic", heic).Add("heicReason", reason).Str();
        });

        std::atomic<bool> exportCancel{ false };
        window.bind("cancelExport", [&](const std::string&) -> std::string
        {
            exportCancel = true;
            return "null";
        });

        // Uma sessão a salvar: nome final da pasta + o que salvar
        struct ExportJob
        {
            std::string session;              // pasta da sessão dentro do app
            std::string folderName;           // nome final (data, número ou nome dado)
            bool sequence = false;
            std::string photoFormat;          // "original", "jpg"... ("" = não salvar fotos)
            Export::Quality photoQuality = Export::Quality::High;
            Export::SequenceOptions options;
        };

        // Salvamento de fato (na fila de prévias). replace: apaga o que havia dentro da
        // pasta que já existe com o mesmo nome e grava só o desta exportação.
        std::function<void(bool)> pendingExport;
        auto startExport = [&](const std::string& folder, bool exitAfter, bool deleteRest, std::vector<ExportJob> jobs, bool replace)
        {
            exportCancel = false;
            listener.Emit(Json("saving").Add("folder", folder));
            previews.Post([&, folder, exitAfter, deleteRest, jobs, replace]
            {
                namespace fs = std::filesystem;
                std::string error;
                std::vector<fs::path> savedTo;
                std::vector<std::string> names, savedNames;
                const auto stored = store.ListSessions();
                int done = 0;
                static const std::regex shortName(R"(^(\d+_[^_]+_\d+)_.+$)");
                for (size_t index = 0; index < jobs.size() && error.empty(); ++index)
                {
                    const ExportJob& job = jobs[index];
                    names.push_back(job.session);
                    auto session = std::find_if(stored.begin(), stored.end(), [&](const StoredSession& s) { return s.name == job.session; });
                    if (session == stored.end())
                        continue;
                    auto report = [&](double fraction, const std::string& step)
                    {
                        listener.Emit(Json("exportProgress").Add("session", job.session).Add("index", static_cast<int>(index))
                                          .Add("total", static_cast<int>(jobs.size())).Add("step", step).Add("fraction", fraction));
                    };
                    const fs::path source = store.Root() / job.session;
                    std::error_code ec;
                    fs::path target = fs::path(folder) / job.folderName;
                    for (int n = 2; !replace && fs::exists(target, ec); ++n)
                        target = fs::path(folder) / (job.folderName + " (" + std::to_string(n) + ")");
                    if (replace && fs::exists(target, ec))
                    {
                        // Nunca apaga a pasta de dados do app (onde as fotos da sessão estão)
                        const fs::path root = fs::weakly_canonical(store.Root(), ec);
                        const fs::path old = fs::weakly_canonical(target, ec);
                        const auto inside = [](const fs::path& child, const fs::path& parent)
                        {
                            auto c = child.begin();
                            for (auto p = parent.begin(); p != parent.end(); ++p, ++c)
                                if (c == child.end() || *c != *p)
                                    return false;
                            return true;
                        };
                        if (inside(root, old) || inside(old, root))
                        {
                            error = "Could not replace " + target.filename().string() + ": it is the app's own photo folder.";
                            break;
                        }
                        if (!fs::is_directory(target, ec))
                            fs::remove(target, ec);
                        else
                            for (const auto& entry : fs::directory_iterator(target, ec))
                            {
                                std::error_code removeError;
                                fs::remove_all(entry.path(), removeError);
                                if (removeError && error.empty())
                                    error = "Could not delete " + entry.path().filename().string() + " in "
                                            + target.filename().string() + " (it may be open in another app).";
                            }
                        if (!error.empty())
                            break;
                    }
                    fs::create_directories(target, ec);

                    // Uma foto por câmera para converter e para a sequência (RAW + JPG: o JPG)
                    std::map<std::string, std::string> byCamera;
                    for (const auto& photo : session->photos)
                    {
                        if (!photo.previewable)
                            continue;
                        const std::string group = std::to_string(1000 + photo.order) + photo.camera;
                        if (!byCamera.count(group) || IsJpegPhoto(photo.file))
                            byCamera[group] = photo.file;
                    }
                    std::vector<std::string> perCamera;
                    for (const auto& [group, file] : byCamera)
                        perCamera.push_back(file);

                    // Subpastas só quando há fotos e sequência juntas: Photos/ e Sequence/
                    const bool savePhotos = !job.photoFormat.empty();
                    const bool split = savePhotos && job.sequence && !job.options.formats.empty();
                    const fs::path photosDir = split ? target / "Photos" : target;

                    if (job.sequence)
                    {
                        std::vector<fs::path> photos;
                        for (const auto& f : perCamera)
                            photos.push_back(source / f);
                        for (const auto& format : job.options.formats)
                        {
                            Export::SequenceOptions one = job.options;
                            one.formats = { format };
                            const fs::path dir = split ? target / "Sequence" : target;
                            fs::create_directories(dir, ec);
                            if (!Export::WriteSequence(photos, dir / job.folderName, one, report, &exportCancel, error))
                                break;
                        }
                        if (!error.empty())
                            break;
                    }

                    if (savePhotos)
                    {
                        fs::create_directories(photosDir, ec);
                        // Nome curto: "01_SL3_1_IMG_4821.JPG" -> "01_SL3_1.JPG" (repetido: "-2")
                        std::map<std::string, int> used;
                        auto shortStem = [&](const std::string& file, const std::string& ext)
                        {
                            std::smatch m;
                            const std::string stem = fs::path(file).stem().string();
                            std::string base = std::regex_match(file, m, shortName) ? m[1].str() : stem;
                            std::string key = base + ext;
                            for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            const int n = ++used[key];
                            return n == 1 ? base : base + "-" + std::to_string(n);
                        };
                        const bool original = job.photoFormat == "original";
                        std::vector<std::string> files;
                        if (original)
                        {
                            for (const auto& photo : session->photos)
                                files.push_back(photo.file);
                        }
                        else
                            files = perCamera;
                        for (size_t i = 0; i < files.size() && error.empty(); ++i)
                        {
                            if (exportCancel) { error = "cancelled"; break; }
                            report(static_cast<double>(i) / files.size(), original ? "photos-save" : job.photoFormat);
                            const fs::path from = source / files[i];
                            if (original)
                            {
                                const std::string ext = from.extension().string();
                                const fs::path to = photosDir / (shortStem(files[i], ext) + ext);
                                std::error_code moveError;
                                fs::remove(to, moveError);
                                moveError.clear();
                                fs::copy_file(from, to, fs::copy_options::overwrite_existing, moveError);
                                if (moveError)
                                    error = "Could not save " + to.filename().string() + ": " + moveError.message();
                            }
                            else
                                Export::ConvertPhoto(from, photosDir / shortStem(files[i], "." + job.photoFormat), job.photoFormat,
                                                     job.photoQuality, error);
                        }
                        if (!error.empty())
                            break;
                    }
                    // Tudo salvo. Fechando o app, a sessão sai do app; senão a pessoa escolhe
                    // depois se descarta ou mantém (pergunta na tela)
                    if (exitAfter)
                        store.DeleteSession(job.session);
                    else
                    {
                        store.MarkSaved(job.session);  // mantida: não conta como "não salva"
                        savedNames.push_back(job.session);
                    }
                    savedTo.push_back(target);
                    ++done;
                    report(1.0, "done");
                }
                if (error == "cancelled")
                    error = "Saving was cancelled. The sessions that were not saved are still in the app.";
                if (done > 0)
                    Platform::OpenInFileManager(done == 1 ? savedTo.front() : std::filesystem::path(folder));
                if (error.empty() && deleteRest)
                    for (const auto& session : store.ListSessions())
                        if (std::find(names.begin(), names.end(), session.name) == names.end())
                            store.DeleteSession(session.name);
                if (!error.empty())
                    Log("Save sessions: " + error);
                std::string saved = "[";
                for (const auto& name : savedNames)
                    saved += (saved.size() > 1 ? "," : "") + JsonString(name);
                listener.Emit(Json("saved").Add("count", done).Add("folder", folder).Add("error", error).Raw("sessions", saved + "]"));
                emitSessions();
                if (exitAfter && error.empty())
                    window.dispatch([&] { closeNow(); });
            });
        };

        // resolveExportConflict("both" | "replace" | "cancel")
        window.bind("resolveExportConflict", [&](const std::string& request) -> std::string
        {
            const auto args = ParseStringArgs(request);
            auto run = pendingExport;
            pendingExport = nullptr;
            if (!run || args.empty() || args[0] == "cancel")
                listener.Emit(Json("saveCancelled"));
            else
                run(args[0] == "replace");
            return "null";
        });

        // exportSessions("exit" ou "", "delete" ou "", especificação, ...). Cada sessão:
        //   "sessão|nome|texto|data 0/1|before ou after|photos|formato"
        //   "sessão|nome|texto|data 0/1|before ou after|sequence|mp4,gif|fps|loops|reverse 0/1|altura|fotos 0/1"
        // nome: "date" (data e hora), "number" (01, 02... continuando os da pasta) ou "custom"
        window.bind("exportSessions", [&](const std::string& request) -> std::string
        {
            namespace fs = std::filesystem;
            auto args = ParseStringArgs(request);
            const bool exitAfter = !args.empty() && args[0] == "exit";
            const bool deleteRest = args.size() > 1 && args[1] == "delete";
            struct Naming { std::string kind, text; bool date = true; bool before = false; };
            std::vector<ExportJob> jobs;
            std::vector<Naming> namings;
            for (size_t i = 2; i < args.size(); ++i)
            {
                std::vector<std::string> f;
                std::stringstream text(args[i]);
                for (std::string field; std::getline(text, field, '|');)
                    f.push_back(field);
                if (f.size() < 7)
                    continue;
                ExportJob job;
                job.session = f[0];
                Naming naming{ f[1], f[2], f[3] == "1", f[4] == "before" };
                job.sequence = f[5] == "sequence";
                if (job.sequence)
                {
                    std::stringstream formats(f[6]);
                    for (std::string x; std::getline(formats, x, ',');)
                        if (!x.empty()) job.options.formats.push_back(x);
                    try
                    {
                        job.options.fps = f.size() > 7 ? std::stod(f[7]) : 6;
                        job.options.loops = f.size() > 8 ? std::stoi(f[8]) : 3;
                        job.options.height = f.size() > 10 ? std::stoi(f[10]) : 1080;
                    }
                    catch (...) {}
                    job.options.reverse = f.size() <= 9 || f[9] == "1";
                    job.photoFormat = (f.size() > 11 && f[11] == "1") ? "original" : "";
                    job.options.quality = Export::ParseQuality(f.size() > 12 ? f[12] : "");
                }
                else
                {
                    job.photoFormat = f[6];
                    job.photoQuality = Export::ParseQuality(f.size() > 7 ? f[7] : "");
                }
                jobs.push_back(job);
                namings.push_back(naming);
            }
            if (jobs.empty())
                return "null";
            const std::string folder = NativeWindow::ChooseFolder("Choose where to save your bullet time sessions");
            if (folder.empty())
            {
                listener.Emit(Json("saveCancelled"));
                return "null";
            }

            // Nomes finais. Data: "2026-10-14 16.31.00". Sem caracteres que o sistema não aceita.
            auto clean = [](std::string s)
            {
                for (char& c : s)
                    if (std::string("/\\:*?\"<>|").find(c) != std::string::npos || static_cast<unsigned char>(c) < 32)
                        c = '-';
                while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
                while (!s.empty() && s.front() == ' ') s.erase(s.begin());
                return s;
            };
            auto dateOf = [](const std::string& session)
            {
                static const std::regex pattern(R"(^(\d{4}-\d{2}-\d{2})_(\d{2})-(\d{2})-(\d{2}))");
                std::smatch m;
                return std::regex_search(session, m, pattern) ? m[1].str() + " " + m[2].str() + "." + m[3].str() + "." + m[4].str() : session;
            };
            // Próximo número livre para "base NN" (ou só "NN") na pasta escolhida
            std::map<std::string, int> nextNumber;
            auto numberFor = [&](const std::string& base)
            {
                if (!nextNumber.count(base))
                {
                    int highest = 0;
                    const std::regex pattern("^" + std::regex_replace(base, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\$&)")
                                             + (base.empty() ? "" : " ") + R"((\d+)$)");
                    std::error_code ec;
                    for (const auto& entry : fs::directory_iterator(folder, ec))
                    {
                        std::smatch m;
                        const std::string existingName = entry.path().filename().string();
                        if (std::regex_match(existingName, m, pattern))
                            highest = std::max(highest, std::atoi(m[1].str().c_str()));
                    }
                    nextNumber[base] = highest + 1;
                }
                char digits[16];
                std::snprintf(digits, sizeof(digits), "%02d", nextNumber[base]++);
                return base.empty() ? std::string(digits) : base + " " + digits;
            };
            std::map<std::string, int> sameText;
            for (const auto& n : namings)
                if (n.kind == "custom" && !n.date)
                    ++sameText[clean(n.text)];
            for (size_t i = 0; i < jobs.size(); ++i)
            {
                const Naming& n = namings[i];
                const std::string text = clean(n.text);
                const std::string date = dateOf(jobs[i].session);
                if (n.kind == "number")
                    jobs[i].folderName = numberFor("");
                else if (n.kind == "custom" && !text.empty())
                {
                    if (n.date)
                        jobs[i].folderName = n.before ? date + " " + text : text + " " + date;
                    else
                        jobs[i].folderName = sameText[text] > 1 ? numberFor(text) : text;
                }
                else
                    jobs[i].folderName = date;
            }

            std::vector<std::string> existing;
            std::error_code existsError;
            for (const auto& job : jobs)
                if (fs::exists(fs::path(folder) / job.folderName, existsError))
                    existing.push_back(job.folderName);
            pendingExport = [&, folder, exitAfter, deleteRest, jobs](bool replace) { startExport(folder, exitAfter, deleteRest, jobs, replace); };
            if (!existing.empty())
            {
                // Já existe pasta com esse nome: a página pergunta o que fazer
                listener.Emit(Json("exportConflict").Add("folder", folder).Add("names", existing));
                return "null";
            }
            auto run = pendingExport;
            pendingExport = nullptr;
            run(false);
            return "null";
        });

        // reorderSession(sessão, "02_T7_1", "01_SL3_1", ...): muda a ordem das fotos
        // de uma sessão já feita (renomeia os arquivos)
        window.bind("reorderSession", [&](const std::string& request) -> std::string
        {
            auto args = ParseStringArgs(request);
            if (args.size() < 2)
                return "null";
            const std::string session = args.front();
            args.erase(args.begin());
            previews.Post([&, session, args]
            {
                std::string error;
                if (!store.ReorderSession(session, args, error))
                    listener.Emit(Json("reorderFailed").Add("error", error));
                emitSessions();
            });
            return "null";
        });

        // deletePhotos(sessão, "02_T7_1", ...): apaga fotos de uma sessão
        window.bind("deletePhotos", [&](const std::string& request) -> std::string
        {
            auto args = ParseStringArgs(request);
            if (args.size() < 2)
                return "null";
            const std::string session = args.front();
            args.erase(args.begin());
            previews.Post([&, session, args]
            {
                std::string error;
                if (!store.DeletePhotos(session, args, error))
                    listener.Emit(Json("deleteFailed").Add("error", error));
                emitSessions();
            });
            return "null";
        });

        window.bind("deleteSession", [&](const std::string& request) -> std::string
        {
            const auto args = ParseStringArgs(request);
            if (!args.empty())
                store.DeleteSession(args[0]);
            emitSessions();
            return "null";
        });

        window.bind("deleteAllSessions", [&](const std::string&) -> std::string
        {
            store.DeleteAll();
            emitSessions();
            return "null";
        });

        // --- Atualização (GitHub) ---

        // checkForUpdate() -> texto JSON da última versão publicada no GitHub, ou null.
        // A página compara as versões e escolhe o arquivo do sistema.
        window.bind("checkForUpdate", [&](const std::string& id, const std::string&, void*)
        {
            std::thread([&, id]
            {
                const auto file = std::filesystem::temp_directory_path() / "stillpoint-latest-release.json";
                std::error_code ec;
                std::filesystem::remove(file, ec);
                const int code = Platform::RunProcess({
                    Platform::CurlExecutable().string(), "-fsSL", "--max-time", "15",
                    "-H", "Accept: application/vnd.github+json",
                    "-H", "User-Agent: Stillpoint/" + version,
                    "-o", file.string(),
                    std::string("https://api.github.com/repos/") + kRepo + "/releases/latest" }, 20);
                const std::string text = code == 0 ? ReadTextFile(file) : std::string();
                std::filesystem::remove(file, ec);
                Log(code == 0 ? "Update check: release information received"
                              : "Update check failed (curl exit code " + std::to_string(code) + ")");
                window.dispatch([&, id, text] { window.resolve(id, 0, text.empty() ? "null" : JsonString(text)); });
            }).detach();
        }, nullptr);

        // downloadUpdate(endereço, "sha256:...", nome, "tamanho"): baixa o instalador para
        // a pasta temporária, com avisos de andamento, e confere tamanho e SHA-256
        window.bind("downloadUpdate", [&](const std::string& request) -> std::string
        {
            const auto args = ParseStringArgs(request);
            const std::string url = args.size() > 0 ? args[0] : "";
            const std::string digest = args.size() > 1 ? args[1] : "";
            const std::string name = args.size() > 2 ? args[2] : "";
            long long size = 0;
            try { size = args.size() > 3 ? std::stoll(args[3]) : 0; } catch (...) { size = 0; }
            // Só arquivos das versões publicadas deste projeto
            if (url.rfind(kReleaseDownloads, 0) != 0 || name.empty() || name.find_first_of("/\\") != std::string::npos)
                return Json("download").Add("ok", false).Str();
            if (update.downloading.exchange(true))
                return Json("download").Add("ok", false).Str();
            update.cancel = false;

            std::thread([&, url, digest, name, size]
            {
                const auto file = std::filesystem::temp_directory_path() / name;
                std::error_code ec;
                std::filesystem::remove(file, ec);

                std::atomic<bool> finished{ false };
                std::thread progress([&, file, size]
                {
                    while (!finished)
                    {
                        std::error_code sizeError;
                        const auto done = std::filesystem::file_size(file, sizeError);
                        listener.Emit(Json("updateProgress").Add("done", static_cast<double>(sizeError ? 0 : done))
                                                            .Add("total", static_cast<double>(size)));
                        std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    }
                });
                const int code = Platform::RunProcess({
                    Platform::CurlExecutable().string(), "-fsSL", "--retry", "2", "--max-time", "900",
                    "-H", "User-Agent: Stillpoint/" + version, "-o", file.string(), url }, 960, &update.cancel);
                finished = true;
                progress.join();

                std::string error;
                if (update.cancel)
                    error = "cancelled";
                else if (code != 0)
                    error = "The download failed. Check the internet connection and try again.";
                else if (size > 0 && static_cast<long long>(std::filesystem::file_size(file, ec)) != size)
                    error = "The downloaded file is incomplete. Try again.";
                else if (digest.rfind("sha256:", 0) == 0 && Platform::Sha256OfFile(file) != digest.substr(7))
                    error = "The downloaded file did not pass the safety check. Try again.";

                if (!error.empty())
                {
                    std::filesystem::remove(file, ec);
                    Log("Update download: " + error);
                    listener.Emit(Json("updateFailed").Add("error", error).Add("cancelled", error == "cancelled"));
                }
                else
                {
                    {
                        std::lock_guard<std::mutex> lock(update.mutex);
                        update.ready = file;
                    }
                    Log("Update downloaded and verified: " + file.string());
                    listener.Emit(Json("updateReady"));
                }
                update.downloading = false;
            }).detach();
            return Json("download").Add("ok", true).Str();
        });

        window.bind("cancelUpdate", [&](const std::string&) -> std::string
        {
            update.cancel = true;
            return "null";
        });

        // installUpdate(): começa a instalação da versão baixada e fecha o app (as
        // sessões não salvas ficam no disco e aparecem ao abrir de novo)
        window.bind("installUpdate", [&](const std::string&) -> std::string
        {
            std::filesystem::path installer;
            {
                std::lock_guard<std::mutex> lock(update.mutex);
                installer = update.ready;
            }
            std::string error = "The update was not downloaded.";
            if (installer.empty() || !Platform::StartUpdateInstall(installer, error))
                return Json("install").Add("ok", false).Add("error", error).Str();
            Log("Installing the update; closing the app");
            window.dispatch([&] { closeNow(); });
            return Json("install").Add("ok", true).Str();
        });

        // openUrl(endereço): só páginas deste projeto no GitHub
        window.bind("openUrl", [&](const std::string& request) -> std::string
        {
            const auto args = ParseStringArgs(request);
            if (!args.empty() && args[0].rfind(kRepoPages, 0) == 0)
                Platform::OpenUrl(args[0]);
            return "null";
        });

        // --- Saída ---

        // restartApp(): abre o app de novo e fecha este. Para quando uma câmera fica
        // presa conectando (o SDK não deixa cancelar). As fotos não salvas ficam no
        // disco e aparecem ao abrir de novo.
        window.bind("restartApp", [&](const std::string&) -> std::string
        {
            if (!Platform::RelaunchAfterExit())
                return Json("restart").Add("ok", false).Str();
            window.dispatch([&] { closeNow(); });
            return Json("restart").Add("ok", true).Str();
        });

        window.bind("exitApp", [&](const std::string& request) -> std::string
        {
            // exitApp(true) apaga as fotos não salvas antes de fechar (as salvas e
            // mantidas no app ficam)
            if (request.find("true") != std::string::npos)
                store.DeleteUnsaved();
            window.dispatch([&] { closeNow(); });
            return "null";
        });

        // Ao fechar a janela com fotos não salvas, a página pergunta o que fazer
        if (auto native = window.window(); native.ok())
        {
            NativeWindow::InstallCloseGuard(native.value(), [&]
            {
                if (allowClose)
                    return true;
                const int unsaved = store.CountUnsaved();
                if (unsaved == 0)
                    closeNow();  // encerra as câmeras antes de fechar a janela
                else
                    listener.Emit(Json("confirmExit").Add("unsavedSessions", unsaved));
                return false;
            });
            // Mac: menu na barra de cima; os itens do app viram eventos para a página
            NativeWindow::InstallAppMenu(native.value(), [&](const std::string& item)
            {
                listener.Emit(Json("menu").Add("item", item));
            });
        }

        window.set_html(LoadPage("index.html", theme));
        window.run();

        // Janela fechada. Se as câmeras não foram encerradas pelo caminho normal
        // (ex.: o sistema fechou o app), não esperar por elas: esperar pode travar.
        SetLogSink([](const std::string&) {});
        listener.Close();
        shuttingDown = true;
        if (!camerasClosed)
            std::_Exit(0);
        return 0;
    }
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
#else
int main()
#endif
{
    try
    {
        return RunApp();
    }
    catch (const webview::exception& e)
    {
        Log(std::string("Window error: ") + e.what());
        return 1;
    }
}
