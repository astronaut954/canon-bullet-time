// Interface gráfica do Canon Bullet Time.
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
#include <atomic>
#include <chrono>
#include <condition_variable>
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

        for (const auto* cam : ordered)
        {
            const bool connected = cam->sessionOpen && !cam->connectionLost;
            auto reconnect = std::find_if(pending.begin(), pending.end(),
                [&](const PendingConnectionInfo& p) { return p.reconnecting && !p.retrying && p.name == cam->shortName; });
            const bool connecting = !connected && reconnect != pending.end();

            Json item("camera");
            item.Add("order", cam->orderPrefix)
                .Add("name", cam->shortName)
                .Add("model", cam->modelName)
                .Add("connected", connected)
                .Add("state", connected ? "connected" : connecting ? "connecting" : "disconnected")
                .Add("seconds", connecting ? reconnect->seconds : 0)
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
                .Add("compatible", IsOnCompatibilityList(p.model));
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

    // Lê assets/ui/index.html e embute style.css e app.js (o WebKit do Mac não
    // carrega arquivos locais separados de uma página montada em memória).
    std::string LoadUi()
    {
        const auto dir = Platform::ExecutableDirectory() / "assets" / "ui";
        auto read = [](const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            std::stringstream content;
            content << file.rdbuf();
            return content.str();
        };

        std::string html = read(dir / "index.html");
        if (html.empty())
            return "<h1>Canon Bullet Time</h1><p>UI files not found: " + dir.string() + "</p>";

        auto replace = [&html](const std::string& marker, const std::string& content)
        {
            const auto pos = html.find(marker);
            if (pos != std::string::npos)
                html.replace(pos, marker.size(), content);
        };
        replace("/*__STYLE__*/", read(dir / "style.css"));
        replace("/*__SCRIPT__*/", read(dir / "app.js"));
        return html;
    }

    int RunApp()
    {
        Platform::InitConsole();

        webview::webview window(false, nullptr);
        window.set_title("Canon Bullet Time");
        window.set_size(1120, 760, WEBVIEW_HINT_NONE);
        window.set_size(900, 600, WEBVIEW_HINT_MIN);

        GuiListener listener(window);
        CanonController controller;
        BulletTime bulletTime(controller, listener);
        PhotoStore store(Platform::AppDataFolder() / "Photos");
        PreviewThread previews;

        bool sdkReady = false;
        std::atomic<bool> busy{ false };
        std::atomic<bool> allowClose{ false };

        // Tempo real: sem pedidos pendentes, acompanha as câmeras a cada ~0,4 s.
        // Cada câmera conecta em paralelo; a página recebe a lista sempre que algo
        // muda e, enquanto alguma conecta, a cada volta (para mostrar os segundos).
        CameraThread cameras([&]
        {
            if (!sdkReady || busy)
                return;
            EdsGetEvent();
            if (controller.MonitorTick(&listener) || controller.HasPendingConnections())
                listener.Emit(CamerasSnapshot(controller));
        });

        // Mensagens de texto do núcleo vão para o log da página
        SetLogSink([&listener](const std::string& message) { listener.Emit(Json("log").Add("text", message)); });

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

        // Fecha o app de fato (depois que a página já resolveu as fotos)
        auto closeNow = [&]
        {
            allowClose = true;
            window.terminate();
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
            return Json("startup").Add("unsavedSessions", store.CountSessions()).Str();
        });

        // Solução de problemas: desliga e liga de novo a conexão com todas as câmeras
        window.bind("restartConnections", [&](const std::string&) -> std::string
        {
            run("restart", [&]
            {
                controller.WaitPendingConnections();
                controller.CloseSessions();
                if (sdkReady)
                    controller.TerminateSdk();
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
        window.bind("getPhoto", [&](const std::string& id, const std::string& request, void*)
        {
            const auto args = ParseStringArgs(request);
            const auto numbers = ParseIntArgs(request);
            const std::string session = args.size() > 0 ? args[0] : "";
            const std::string file = args.size() > 1 ? args[1] : "";
            const int size = numbers.empty() ? 1600 : std::clamp(numbers.back(), 64, 4000);

            previews.Post([&, id, session, file, size]
            {
                const std::string jpeg = store.Preview(session, file, size);
                window.resolve(id, 0, jpeg.empty() ? "null" : JsonString("data:image/jpeg;base64," + Base64Encode(jpeg)));
            });
        }, nullptr);

        // saveSessions(exitAfter): escolhe a pasta e move todas as sessões para lá
        window.bind("saveSessions", [&](const std::string& request) -> std::string
        {
            const bool exitAfter = request.find("true") != std::string::npos;
            const std::string folder = NativeWindow::ChooseFolder("Choose where to save your bullet time sessions");
            if (folder.empty())
            {
                listener.Emit(Json("saveCancelled"));
                return "null";
            }

            listener.Emit(Json("saving").Add("folder", folder));
            previews.Post([&, folder, exitAfter]
            {
                std::string error;
                const int moved = store.SaveAll(folder, error);
                listener.Emit(Json("saved").Add("count", moved).Add("folder", folder).Add("error", error));
                emitSessions();
                if (exitAfter && error.empty())
                    window.dispatch([&] { closeNow(); });
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
                    return true;
                listener.Emit(Json("confirmExit").Add("unsavedSessions", unsaved));
                return false;
            });
        }

        window.set_html(LoadUi());
        window.run();

        // Janela fechada: nada mais vai para a página; encerra as câmeras na
        // própria thread delas. Se alguma ainda estiver conectando, o SDK não pode
        // ser desligado com segurança: o sistema libera tudo ao fechar o programa.
        SetLogSink([](const std::string&) {});
        listener.Close();
        cameras.Post([&]
        {
            controller.CloseSessions();
            if (sdkReady && !controller.HasPendingConnections())
                controller.TerminateSdk();
        });
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
