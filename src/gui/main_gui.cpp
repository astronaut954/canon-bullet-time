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
#include "bullet_time.h"
#include "canon_controller.h"
#include "logger.h"
#include "path_utils.h"
#include "platform/platform.h"
#include "sdk_diagnostics.h"

#include "native_window.h"
#include "photo_store.h"

#include "webview/webview.h"

#include <algorithm>
#include <optional>
#include <atomic>
#include <chrono>
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
        explicit Json(const std::string& type) { Add("type", type); }

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

        void Emit(const Json& event)
        {
            if (closed)
                return;
            const std::string script = "window.onCoreEvent && window.onCoreEvent(" + event.Str() + ")";
            window.dispatch([this, script] { window.eval(script); });
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
        void OnSequenceFinished(bool completed) override
        {
            lastDone = lastTotal = -1;
            Emit(Json("sequenceFinished").Add("completed", completed));
        }

    private:
        webview::webview& window;
        std::atomic<bool> closed{ false };

        int lastDone = -1;
        int lastTotal = -1;
    };

    // Estado atual das câmeras para a página redesenhar: conectadas, desconectadas
    // e as que estão conectando agora (com os segundos de espera)
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
            item.Add("name", session.name).Raw("photos", photos);
            list += (firstSession ? "" : ",") + item.Str();
            firstSession = false;
        }
        return list + "]";
    }

    // Visual da janela: assets/ui/themes/<nome>.css (estrutura do Nocturne com as
    // cores do Neo Terminal).
    const char* const kTheme = "nocturne-neo";

    // Lê assets/ui/index.html e embute o tema (CSS), as fontes e app.js (o WebKit
    // do Mac não carrega arquivos locais separados de uma página montada em memória).
    std::string LoadUi()
    {
        const auto dir = Platform::ResourcesDirectory() / "assets" / "ui";
        auto read = [](const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            std::stringstream content;
            content << file.rdbuf();
            return content.str();
        };

        std::string html = read(dir / "index.html");
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

        replace("/*__THEME__*/", embedFonts(read(dir / "themes" / (std::string(kTheme) + ".css"))));
        replace("/*__SCRIPT__*/", read(dir / "app.js"));
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
        // Fotos de teste do "Reorder by photo": só servem para ordenar, são apagadas depois
        PhotoStore orderStore(Platform::AppDataFolder() / "Order photos");
        orderStore.DeleteAll();
        PreviewThread previews;

        bool sdkReady = false;
        std::atomic<bool> busy{ false };
        std::atomic<bool> allowClose{ false };
        std::atomic<bool> shuttingDown{ false };

        // Tempo real: sem pedidos pendentes, acompanha as câmeras a cada ~0,4 s.
        // Cada câmera conecta em paralelo; a página recebe a lista sempre que algo
        // muda e, enquanto alguma conecta, a cada volta (para mostrar os segundos).
        CameraThread cameras([&]
        {
            if (!sdkReady || busy || shuttingDown)
                return;
            if (!controller.HasPendingConnections())
                EdsGetEvent();  // com uma câmera conectando, o SDK está ocupado
            // Redesenha quando algo mudou, e a cada volta enquanto há câmera conectando
            // ou ligando (para o tempo de espera andar)
            if (controller.MonitorTick(&listener) || controller.HasPendingConnections()
                || !controller.GetStartingCameras().empty())
                listener.Emit(CamerasSnapshot(controller));
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
            sdkReady = controller.InitializeSdk();
            if (!sdkReady)
            {
                listener.Emit(Json("sdkFailed"));
                return;
            }
            controller.SetOutputFolder(store.Root().string());
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
            return Json("startup").Add("unsavedSessions", store.CountSessions())
                .Add("testRestart", std::getenv("STILLPOINT_TEST_RESTART") != nullptr).Str();
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

        window.bind("shoot", [&](const std::string& request) -> std::string
        {
            const auto values = ParseIntArgs(request);
            SequenceSettings settings;
            if (values.size() >= 3)
            {
                settings.countdownSeconds = std::max(0, values[0]);
                settings.sessionCount = std::max(1, values[1]);
                settings.downloadWaitSeconds = std::max(1, values[2]);
            }
            run("shoot", [&, settings]
            {
                if (cameraConnecting())
                    return;
                bulletTime.RunSequence(settings);
                emitSessions();
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
        auto bindPhotos = [&](const std::string& name, PhotoStore& source)
        {
            PhotoStore* from = &source;
            window.bind(name, [&, from](const std::string& id, const std::string& request, void*)
            {
                const auto args = ParseStringArgs(request);
                const auto numbers = ParseIntArgs(request);
                const std::string session = args.size() > 0 ? args[0] : "";
                const std::string file = args.size() > 1 ? args[1] : "";
                const int size = numbers.empty() ? 1600 : std::clamp(numbers.back(), 64, 4000);

                previews.Post([&, from, id, session, file, size]
                {
                    const std::string jpeg = from->Preview(session, file, size);
                    window.resolve(id, 0, jpeg.empty() ? "null" : JsonString("data:image/jpeg;base64," + Base64Encode(jpeg)));
                });
            }, nullptr);
        };
        bindPhotos("getPhoto", store);
        bindPhotos("getOrderPhoto", orderStore);

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
                const std::string previous = controller.GetOutputFolder();
                controller.SetOutputFolder(orderStore.Root().string());
                bulletTime.RunSequence(settings);
                controller.SetOutputFolder(previous);
                listener.Emit(Json("orderPhotos").Raw("sessions", SessionsJson(orderStore)));
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
                const int moved = store.SaveSessions(folder, names, error);
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
            // exitApp(true) apaga as fotos não salvas antes de fechar
            if (request.find("true") != std::string::npos)
                store.DeleteAll();
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
                const int unsaved = store.CountSessions();
                if (unsaved == 0)
                    closeNow();  // encerra as câmeras antes de fechar a janela
                else
                    listener.Emit(Json("confirmExit").Add("unsavedSessions", unsaved));
                return false;
            });
        }

        window.set_html(LoadUi());
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
