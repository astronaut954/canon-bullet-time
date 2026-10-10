// Canon Bullet Time — comportamento da interface.
// Recebe os avisos do programa em window.onCoreEvent e chama o programa pelas
// funções detect(), shoot(), identify(), setPrefix(), setOutputFolder(),
// answerScanAgain() e uiReady(), que o C++ disponibiliza para a página.

(function () {
  const $ = (id) => document.getElementById(id);

  const ui = {
    status: $("status"),
    cameraList: $("cameraList"),
    noCameras: $("noCameras"),
    detectBtn: $("detectBtn"),
    shootBtn: $("shootBtn"),
    folderBtn: $("folderBtn"),
    countdown: $("countdown"),
    sessions: $("sessions"),
    downloadWait: $("downloadWait"),
    outputFolder: $("outputFolder"),
    progress: $("progress"),
    activity: $("activity"),
    details: $("details"),
    detailsBtn: $("detailsBtn"),
    countdownOverlay: $("countdownOverlay"),
    countdownNumber: $("countdownNumber"),
    askOverlay: $("askOverlay"),
    askText: $("askText"),
    scanAgainBtn: $("scanAgainBtn"),
    continueBtn: $("continueBtn"),
  };

  const DOTS = ["   ", ".  ", ".. ", "..."];
  let busy = false;
  let cameraCount = 0;
  let connectingLine = null; // linha "Connecting to..." sendo atualizada
  let waitingLine = null;    // linha "Waiting for..." sendo atualizada
  let tick = 0;

  // ---------- Utilitários ----------
  function setStatus(text, kind) {
    ui.status.textContent = text;
    ui.status.className = "status status-" + (kind || "idle");
  }

  function addLine(text, kind, hint) {
    const li = document.createElement("li");
    li.textContent = text;
    if (kind) li.className = kind;
    if (hint) {
      const span = document.createElement("span");
      span.className = "hint";
      span.textContent = hint;
      li.appendChild(span);
    }
    ui.activity.appendChild(li);
    ui.activity.scrollTop = ui.activity.scrollHeight;
    return li;
  }

  function updateButtons() {
    ui.detectBtn.disabled = busy;
    ui.folderBtn.disabled = busy;
    ui.shootBtn.disabled = busy || cameraCount === 0;
    document.querySelectorAll(".camera-actions button").forEach((b) => (b.disabled = busy));
  }

  function intValue(input, min, fallback) {
    const n = parseInt(input.value, 10);
    return Number.isFinite(n) && n >= min ? n : fallback;
  }

  // ---------- Lista de câmeras ----------
  function renderCameras(cameras) {
    cameraCount = cameras.filter((c) => c.connected).length;
    ui.cameraList.innerHTML = "";
    ui.noCameras.classList.toggle("hidden", cameras.length > 0);

    cameras.forEach((cam, index) => {
      const li = document.createElement("li");
      li.className = "camera";

      const order = document.createElement("div");
      order.className = "camera-order";
      order.textContent = String(cam.order).padStart(2, "0");

      const info = document.createElement("div");
      const name = document.createElement("div");
      name.className = "camera-name";
      name.innerHTML = '<span class="dot' + (cam.connected ? "" : " dot-lost") + '"></span>';
      name.appendChild(document.createTextNode(cam.name));
      const model = document.createElement("div");
      model.className = "camera-model";
      model.textContent = cam.model;
      info.append(name, model);

      if (!cam.connected) {
        const lost = document.createElement("div");
        lost.className = "camera-lost";
        lost.textContent = "Connection lost";
        info.appendChild(lost);
      }
      if (!cam.compatible) {
        const warn = document.createElement("div");
        warn.className = "camera-warning";
        warn.textContent = "Not on Canon's compatibility list. It may not work correctly.";
        info.appendChild(warn);
      }

      const actions = document.createElement("div");
      actions.className = "camera-actions";
      actions.append(
        button("Identify", () => window.identify(cam.name)),
        button("↑", () => window.setPrefix(cam.name, cam.order - 1), index === 0),
        button("↓", () => window.setPrefix(cam.name, cam.order + 1), index === cameras.length - 1)
      );

      li.append(order, info, actions);
      ui.cameraList.appendChild(li);
    });
    updateButtons();
  }

  function button(label, onClick, disabled) {
    const b = document.createElement("button");
    b.className = "btn btn-secondary btn-small";
    b.textContent = label;
    b.disabled = !!disabled;
    b.addEventListener("click", onClick);
    return b;
  }

  // ---------- Avisos do programa ----------
  const handlers = {
    busy(e) {
      busy = e.busy;
      if (busy) {
        const labels = { detect: "Detecting cameras…", shoot: "Shooting…", identify: "Identifying…", reorder: "Reordering…", folder: "Saving folder…" };
        setStatus(labels[e.action] || "Working…", "busy");
        if (e.action === "detect") addLine("🔄 Detecting cameras...", "muted");
      } else {
        setStatus(cameraCount ? cameraCount + (cameraCount === 1 ? " camera ready" : " cameras ready") : "No cameras", cameraCount ? "ok" : "idle");
      }
      updateButtons();
    },

    cameras(e) {
      renderCameras(e.cameras);
      if (document.activeElement !== ui.outputFolder) ui.outputFolder.value = e.outputFolder;
    },

    sdkFailed() {
      setStatus("Canon SDK error", "error");
      addLine("⚠️ Failed to initialize the Canon SDK.", "error");
    },

    // Conexão
    connecting(e) {
      waitingLine = null;
      connectingLine = addLine("⏳ Connecting to " + e.camera + " (" + e.model + ")...");
      connectingLine.dataset.base = connectingLine.textContent.replace(/\.\.\.$/, "");
    },
    connectingProgress(e) {
      if (!connectingLine || e.seconds < 2) return;
      tick++;
      connectingLine.textContent = connectingLine.dataset.base + DOTS[tick % 4] + " " + e.seconds + "s (camera is starting up, please wait)";
    },
    connected(e) {
      if (connectingLine) {
        connectingLine.textContent = connectingLine.dataset.base + "... ✅ Connected (" + e.seconds.toFixed(1) + "s)";
        connectingLine.className = "ok";
        if (!e.compatible) {
          addLine("⚠️ " + e.model + " is not on Canon's compatibility list for this app.", "warn", "It may not work correctly.");
        }
      }
      connectingLine = null;
    },
    connectFailed(e) {
      if (connectingLine) {
        connectingLine.textContent = connectingLine.dataset.base + "... ⚠️ Failed, error " + e.error + " (" + e.seconds.toFixed(1) + "s)";
        connectingLine.className = "warn";
        if (e.hint) {
          const span = document.createElement("span");
          span.className = "hint";
          span.textContent = e.hint;
          connectingLine.appendChild(span);
        }
      }
      connectingLine = null;
    },
    camerasFailed(e) {
      addLine("⚠️ Could not connect to: " + e.names.join(", "), "warn");
    },
    retryStarted(e) {
      addLine("🔄 Retrying automatically for up to " + e.maxSeconds + "s...", "muted");
      waitingLine = null;
    },
    waiting(e) {
      const text = "⌛ Waiting for " + e.models.length + (e.models.length === 1 ? " camera (" : " cameras (") + e.models.join(", ") + ")" + DOTS[tick++ % 4] + " " + e.seconds + "s";
      if (!waitingLine) waitingLine = addLine(text, "muted");
      else waitingLine.textContent = text;
    },
    allConnected() {
      waitingLine = null;
      addLine("✅ All cameras connected.", "ok");
    },
    retryGaveUp() {
      waitingLine = null;
      addLine("⚠️ Could not connect failed cameras.", "warn");
    },
    askScanAgain() {
      ui.askText.textContent = "Scan again, or continue with the cameras that are connected.";
      ui.askOverlay.classList.remove("hidden");
    },
    connectFinished(e) {
      if (e.result === "noCameras") addLine("⚠️ No cameras found.", "warn");
    },

    // Sequência
    sessionStarted(e) {
      addLine("🎞️ Session " + e.index + " of " + e.total);
      ui.progress.textContent = "Session " + e.index + " of " + e.total;
    },
    sessionFolderFailed() {
      addLine("⚠️ Failed to prepare session folder.", "error");
    },
    countdownStarted() {
      ui.countdownOverlay.classList.remove("hidden");
      ui.countdownNumber.classList.remove("fire");
    },
    countdownTick(e) {
      ui.countdownOverlay.classList.remove("hidden");
      ui.countdownNumber.textContent = e.seconds;
    },
    trigger() {
      ui.countdownOverlay.classList.remove("hidden");
      ui.countdownNumber.classList.add("fire");
      ui.countdownNumber.textContent = "IT'S BULLET TIME!";
      addLine("📸 IT'S BULLET TIME!");
      setTimeout(() => ui.countdownOverlay.classList.add("hidden"), 700);
    },
    shotFailed(e) {
      addLine("⚠️ " + e.camera + " did not take the picture: " + (e.hint || "error " + e.error) + ".", "warn");
    },
    waitingDownloads() {
      ui.progress.textContent = "📥 Waiting for downloads...";
    },
    downloadProgress(e) {
      ui.progress.textContent = "📥 " + e.done + "/" + e.total + " photos downloaded";
    },
    downloadTimeout(e) {
      addLine("⚠️ Timeout reached.", "error");
      if (e.lost.length) addLine("🔌 Lost connection on cameras: " + e.lost.join(", "), "error");
      else addLine("ℹ️ No disconnected cameras detected.", "muted");
    },
    sessionInterrupted() {
      addLine("⚠️ Session interrupted.", "error");
    },
    sessionCompleted() {
      addLine("✅ Session completed.", "ok");
    },
    sequenceFinished(e) {
      if (e.completed) {
        addLine("🏁 Sequence completed successfully.", "ok");
        ui.progress.textContent = "🏁 Done";
      } else {
        addLine("⚠️ Sequence interrupted.", "error");
        ui.progress.textContent = "";
      }
    },

    log(e) {
      ui.details.textContent += e.text.replace(/^\n+/, "") + "\n";
      ui.details.scrollTop = ui.details.scrollHeight;
    },
  };

  window.onCoreEvent = function (event) {
    const handler = handlers[event.type];
    if (handler) handler(event);
  };

  // ---------- Ações da pessoa ----------
  ui.detectBtn.addEventListener("click", () => window.detect());

  ui.shootBtn.addEventListener("click", () => {
    window.shoot(
      intValue(ui.countdown, 0, 0),
      intValue(ui.sessions, 1, 1),
      intValue(ui.downloadWait, 1, 10)
    );
  });

  ui.folderBtn.addEventListener("click", () => window.setOutputFolder(ui.outputFolder.value.trim()));

  ui.detailsBtn.addEventListener("click", () => {
    const hidden = ui.details.classList.toggle("hidden");
    ui.detailsBtn.textContent = hidden ? "Show details" : "Hide details";
  });

  function answer(scanAgain) {
    ui.askOverlay.classList.add("hidden");
    window.answerScanAgain(scanAgain);
  }
  ui.scanAgainBtn.addEventListener("click", () => answer(true));
  ui.continueBtn.addEventListener("click", () => answer(false));

  updateButtons();
  window.uiReady();
})();
