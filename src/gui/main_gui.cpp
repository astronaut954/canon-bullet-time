// Interface gráfica do Canon Bullet Time.
//
// A janela é uma página HTML/CSS/JS (assets/ui/) exibida pelo navegador do
// próprio sistema (WebView2 no Windows, WebKit no Mac) dentro de uma janela
// nativa. O trabalho com as câmeras roda numa thread própria ("thread das
// câmeras"), usando o mesmo núcleo do terminal (BulletTime / CanonController).
//
// Comunicação:
//   página -> C++ : funções JavaScript ligadas com webview::bind (detect, shoot...)
//   C++ -> página : window.onCoreEvent({ type: "...", ... }) via webview::dispatch

#include "bullet_time.h"
#include "canon_controller.h"
#include "logger.h"
#include "path_utils.h"
#include "platform/platform.h"
#include "sdk_diagnostics.h"

#include "webview/webview.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
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
        CameraThread()
            : worker([this] { Loop(); })
        {
        }

        ~CameraThread()
        {
            Post(nullptr);
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
                    ready.wait(lock, [this] { return !tasks.empty(); });
                    task = std::move(tasks.front());
                    tasks.pop_front();
                }
                if (!task)
                    break;
                task();
            }
            Platform::EndSdkThread();
        }

        std::mutex mutex;
        std::condition_variable ready;
        std::deque<std::function<void()>> tasks;
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

        // Resposta da página para a pergunta "procurar de novo?"
        void AnswerScanAgain(bool scanAgain)
        {
            {
                std::lock_guard<std::mutex> lock(answerMutex);
                answer = scanAgain;
                answered = true;
            }
            answerReady.notify_one();
        }

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

        // --- Espera por câmeras ---
        void OnCamerasFailed(const std::vector<std::string>& names) override
        {
            Emit(Json("camerasFailed").Add("names", names));
        }
        void OnRetryStarted(int maxSeconds) override
        {
            Emit(Json("retryStarted").Add("maxSeconds", maxSeconds));
        }
        void OnWaitingForCameras(const std::vector<std::string>& models, int seconds, int) override
        {
            Emit(Json("waiting").Add("models", models).Add("seconds", seconds));
        }
        void OnAllCamerasConnected() override { Emit(Json("allConnected")); }
        void OnRetryGaveUp() override { Emit(Json("retryGaveUp")); }

        bool AskScanAgain() override
        {
            {
                std::lock_guard<std::mutex> lock(answerMutex);
                answered = false;
            }
            Emit(Json("askScanAgain"));

            std::unique_lock<std::mutex> lock(answerMutex);
            answerReady.wait(lock, [this] { return answered; });
            return answer;
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

        std::mutex answerMutex;
        std::condition_variable answerReady;
        bool answered = false;
        bool answer = false;

        int lastDone = -1;
        int lastTotal = -1;
    };

    // Estado atual (lista de câmeras e pasta) para a página redesenhar
    Json CamerasSnapshot(CanonController& controller)
    {
        controller.RefreshCameraConnectionStatus();

        std::vector<const CameraContext*> ordered;
        for (const auto& cam : controller.GetCameras())
            ordered.push_back(&cam);
        std::sort(ordered.begin(), ordered.end(),
            [](const CameraContext* a, const CameraContext* b) { return a->orderPrefix < b->orderPrefix; });

        std::string list = "[";
        for (size_t i = 0; i < ordered.size(); ++i)
        {
            const auto* cam = ordered[i];
            Json item("camera");
            item.Add("order", cam->orderPrefix)
                .Add("name", cam->shortName)
                .Add("model", cam->modelName)
                .Add("connected", cam->sessionOpen && !cam->connectionLost)
                .Add("compatible", cam->onCompatibilityList);
            list += (i ? "," : "") + item.Str();
        }
        list += "]";

        Json event("cameras");
        event.Raw("cameras", list).Add("outputFolder", controller.GetOutputFolder());
        return event;
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
        window.set_size(1040, 720, WEBVIEW_HINT_NONE);
        window.set_size(820, 560, WEBVIEW_HINT_MIN);

        GuiListener listener(window);
        CanonController controller;
        BulletTime bulletTime(controller, listener);
        CameraThread cameras;
        bool sdkReady = false;
        std::atomic<bool> busy{ false };

        // Mensagens de texto do núcleo vão para o painel "Details" da página
        SetLogSink([&listener](const std::string& message) { listener.Emit(Json("log").Add("text", message)); });

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

        auto connect = [&]
        {
            const auto result = bulletTime.ConnectCameras();
            listener.Emit(Json("connectFinished")
                .Add("result", result == BulletTime::ConnectResult::Connected ? "connected"
                             : result == BulletTime::ConnectResult::NoCameras ? "noCameras"
                                                                               : "failedAfterRetry"));
        };

        window.bind("uiReady", [&](const std::string&) -> std::string
        {
            run("detect", [&]
            {
                if (!sdkReady)
                {
                    sdkReady = controller.InitializeSdk();
                    if (!sdkReady)
                    {
                        listener.Emit(Json("sdkFailed"));
                        return;
                    }
                }
                // Mostra logo a pasta de saída, antes da detecção terminar
                listener.Emit(CamerasSnapshot(controller));
                connect();
            });
            return "null";
        });

        window.bind("detect", [&](const std::string&) -> std::string
        {
            run("detect", [&]
            {
                controller.CloseSessions();
                connect();
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
            run("shoot", [&, settings] { bulletTime.RunSequence(settings); });
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

        window.bind("setOutputFolder", [&](const std::string& request) -> std::string
        {
            const auto args = ParseStringArgs(request);
            if (args.empty() || args[0].empty())
                return "null";
            const std::string path = args[0];
            run("folder", [&, path] { controller.SetOutputFolder(path); });
            return "null";
        });

        window.bind("answerScanAgain", [&](const std::string& request) -> std::string
        {
            listener.AnswerScanAgain(request.find("true") != std::string::npos);
            return "null";
        });

        window.set_html(LoadUi());
        window.run();

        // Janela fechada: nada mais vai para a página; libera uma pergunta pendente
        // e encerra as câmeras na própria thread delas
        SetLogSink([](const std::string&) {});
        listener.Close();
        listener.AnswerScanAgain(false);
        cameras.Post([&]
        {
            controller.CloseSessions();
            if (sdkReady)
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
