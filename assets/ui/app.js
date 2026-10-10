// Canon Bullet Time — comportamento da interface.
// Recebe os avisos do programa em window.onCoreEvent e chama o programa pelas
// funções que o C++ disponibiliza para a página: uiReady, detect, shoot,
// identify, setPrefix, answerScanAgain, listSessions, getPhoto, saveSessions,
// deleteSession, deleteAllSessions e exitApp.

(function () {
  const $ = (id) => document.getElementById(id);

  const ui = {
    status: $("status"), saveBtn: $("saveBtn"), sessionCount: $("sessionCount"),
    tabs: document.querySelectorAll(".tab"),
    viewCapture: $("view-capture"), viewSessions: $("view-sessions"),
    cameraList: $("cameraList"), noCameras: $("noCameras"), detectBtn: $("detectBtn"),
    shootBtn: $("shootBtn"), countdown: $("countdown"), sessions: $("sessions"), downloadWait: $("downloadWait"),
    progress: $("progress"),
    sessionList: $("sessionList"), noSessions: $("noSessions"),
    viewerEmpty: $("viewerEmpty"), viewerContent: $("viewerContent"),
    viewerTitle: $("viewerTitle"), viewerCamera: $("viewerCamera"),
    prevBtn: $("prevBtn"), playBtn: $("playBtn"), nextBtn: $("nextBtn"), deleteSessionBtn: $("deleteSessionBtn"),
    stageImage: $("stageImage"), stageMessage: $("stageMessage"), scrubber: $("scrubber"), filmstrip: $("filmstrip"),
    log: $("log"), logToggle: $("logToggle"), logLast: $("logLast"), logList: $("logList"), techToggle: $("techToggle"),
    countdownOverlay: $("countdownOverlay"), countdownNumber: $("countdownNumber"),
    dialogOverlay: $("dialogOverlay"), dialogTitle: $("dialogTitle"), dialogText: $("dialogText"), dialogActions: $("dialogActions"),
    toast: $("toast"),
  };

  const DOTS = ["   ", ".  ", ".. ", "..."];
  let busy = false;
  let cameras = [];          // última lista recebida do programa
  let connecting = {};       // câmera -> segundos (conexão em andamento)
  let sessions = [];
  let currentSession = null;
  let currentIndex = 0;
  let playTimer = null;
  const photoCache = {};     // "sessão/arquivo/tamanho" -> Promise<dataURL|null>
  let tick = 0;

  // =================== Utilitários ===================
  function setStatus(text, kind) {
    ui.status.textContent = text;
    ui.status.className = "status status-" + (kind || "idle");
  }

  function readyStatus() {
    const n = cameras.filter((c) => c.connected).length;
    setStatus(n ? n + (n === 1 ? " camera ready" : " cameras ready") : "No cameras", n ? "ok" : "idle");
  }

  // Log: linhas amigáveis + detalhes técnicos (ocultos até marcar a opção)
  function log(text, kind, hint) {
    const li = document.createElement("li");
    li.textContent = text;
    if (kind) li.className = kind;
    if (hint) {
      const span = document.createElement("span");
      span.className = "hint";
      span.textContent = hint;
      li.appendChild(span);
    }
    ui.logList.appendChild(li);
    ui.logList.scrollTop = ui.logList.scrollHeight;
    if (kind !== "tech") ui.logLast.textContent = text + (hint ? " — " + hint : "");
    return li;
  }

  function toast(text, ms) {
    ui.toast.textContent = text;
    ui.toast.classList.remove("hidden");
    clearTimeout(toast.timer);
    toast.timer = setTimeout(() => ui.toast.classList.add("hidden"), ms || 3500);
  }

  function dialog(title, text, actions) {
    ui.dialogTitle.textContent = title;
    ui.dialogText.textContent = text;
    ui.dialogActions.innerHTML = "";
    actions.forEach((a) => {
      const b = document.createElement("button");
      b.className = "btn " + (a.primary ? "btn-primary" : a.link ? "btn-link" : "btn-secondary");
      b.textContent = a.label;
      b.addEventListener("click", () => { ui.dialogOverlay.classList.add("hidden"); a.onClick && a.onClick(); });
      ui.dialogActions.appendChild(b);
    });
    ui.dialogOverlay.classList.remove("hidden");
  }

  function sessionLabel(name) {
    // "2026-10-10_18-46-12_AB12_session_01" -> "Oct 10, 18:46:12 · session 1"
    const m = /^(\d{4})-(\d{2})-(\d{2})_(\d{2})-(\d{2})-(\d{2})(?:_[A-Z0-9]+_session_(\d+))?/.exec(name);
    if (!m) return name;
    const date = new Date(+m[1], +m[2] - 1, +m[3]);
    const day = date.toLocaleDateString(undefined, { month: "short", day: "numeric" });
    return day + ", " + m[4] + ":" + m[5] + ":" + m[6] + (m[7] ? " · session " + (+m[7]) : "");
  }

  function updateButtons() {
    const connected = cameras.filter((c) => c.connected).length;
    ui.detectBtn.disabled = busy;
    ui.shootBtn.disabled = busy || connected === 0;
    document.querySelectorAll(".camera-actions button").forEach((b) => (b.disabled = busy || b.dataset.locked === "1"));
    ui.saveBtn.disabled = sessions.length === 0;
    ui.saveBtn.textContent = sessions.length ? "Save sessions (" + sessions.length + ")" : "Save sessions";
    ui.sessionCount.textContent = sessions.length;
    ui.sessionCount.classList.toggle("hidden", sessions.length === 0);
  }

  function intValue(input, min, fallback) {
    const n = parseInt(input.value, 10);
    return Number.isFinite(n) && n >= min ? n : fallback;
  }

  // =================== Abas ===================
  function showView(view) {
    ui.tabs.forEach((t) => t.classList.toggle("active", t.dataset.view === view));
    ui.viewCapture.classList.toggle("hidden", view !== "capture");
    ui.viewSessions.classList.toggle("hidden", view !== "sessions");
    if (view === "sessions") refreshSessions();
    else stopPlay();
  }
  ui.tabs.forEach((t) => t.addEventListener("click", () => showView(t.dataset.view)));

  // =================== Câmeras (tempo real) ===================
  function renderCameras() {
    ui.cameraList.innerHTML = "";
    const pending = Object.keys(connecting).filter((n) => !cameras.some((c) => c.name === n));
    ui.noCameras.classList.toggle("hidden", cameras.length > 0 || pending.length > 0);

    cameras.forEach((cam, index) => ui.cameraList.appendChild(cameraRow(cam, index)));
    pending.forEach((name) => ui.cameraList.appendChild(cameraRow({ name: name, model: connecting[name].model, order: 0, connected: false, compatible: true }, -1)));
    updateButtons();
  }

  function cameraRow(cam, index) {
    const isConnecting = connecting[cam.name] !== undefined;
    const state = isConnecting ? "connecting" : cam.connected ? "connected" : "disconnected";

    const li = document.createElement("li");
    li.className = "camera is-" + state;

    const order = document.createElement("div");
    order.className = "camera-order";
    order.textContent = cam.order ? String(cam.order).padStart(2, "0") : "··";

    const info = document.createElement("div");
    const name = document.createElement("div");
    name.className = "camera-name";
    name.innerHTML = '<span class="dot dot-' + state + '"></span>';
    name.appendChild(document.createTextNode(cam.name));
    const model = document.createElement("div");
    model.className = "camera-model";
    model.textContent = cam.model;
    const stateLine = document.createElement("div");
    stateLine.className = "camera-state state-" + state;
    stateLine.textContent =
      state === "connected" ? "Connected"
      : state === "connecting" ? "Connecting" + (connecting[cam.name].seconds >= 2 ? "… " + connecting[cam.name].seconds + "s (camera is starting up)" : "…")
      : "Disconnected — turn it on to reconnect";
    info.append(name, model, stateLine);

    if (!cam.compatible) {
      const warn = document.createElement("div");
      warn.className = "camera-warning";
      warn.textContent = "Not on Canon's compatibility list. It may not work correctly.";
      info.appendChild(warn);
    }

    const actions = document.createElement("div");
    actions.className = "camera-actions";
    if (index >= 0) {
      actions.append(
        smallButton("Identify", () => window.identify(cam.name), state !== "connected"),
        smallButton("↑", () => window.setPrefix(cam.name, cam.order - 1), index === 0),
        smallButton("↓", () => window.setPrefix(cam.name, cam.order + 1), index === cameras.length - 1)
      );
    }

    li.append(order, info, actions);
    return li;
  }

  function smallButton(label, onClick, locked) {
    const b = document.createElement("button");
    b.className = "btn btn-secondary btn-small";
    b.textContent = label;
    b.dataset.locked = locked ? "1" : "0";
    b.disabled = busy || !!locked;
    b.addEventListener("click", onClick);
    return b;
  }

  // =================== Sessões e fotos ===================
  function refreshSessions() {
    const list = window.listSessions();
    (list && list.then ? list : Promise.resolve(list)).then(setSessions);
  }

  function setSessions(list) {
    sessions = list || [];
    updateButtons();
    renderSessionList();
    if (currentSession && !sessions.some((s) => s.name === currentSession.name)) {
      currentSession = null;
      showSession(null);
    }
  }

  function renderSessionList() {
    ui.sessionList.innerHTML = "";
    ui.noSessions.classList.toggle("hidden", sessions.length > 0);
    sessions.forEach((s) => {
      const li = document.createElement("li");
      li.className = "session-item" + (currentSession && currentSession.name === s.name ? " active" : "");
      li.innerHTML = '<div class="session-date"></div><div class="session-meta"></div>';
      li.firstChild.textContent = sessionLabel(s.name);
      li.lastChild.textContent = s.photos.length + (s.photos.length === 1 ? " photo" : " photos");
      li.addEventListener("click", () => showSession(s));
      ui.sessionList.appendChild(li);
    });
  }

  function photo(session, file, size) {
    const key = session + "/" + file + "/" + size;
    if (!photoCache[key]) photoCache[key] = window.getPhoto(session, file, size);
    return photoCache[key];
  }

  function showSession(session) {
    stopPlay();
    currentSession = session;
    renderSessionList();
    ui.viewerEmpty.classList.toggle("hidden", !!session);
    ui.viewerContent.classList.toggle("hidden", !session);
    if (!session) return;

    ui.viewerTitle.textContent = sessionLabel(session.name);
    ui.scrubber.max = Math.max(0, session.photos.length - 1);
    ui.filmstrip.innerHTML = "";
    session.photos.forEach((p, i) => {
      const li = document.createElement("li");
      li.className = "thumb";
      li.innerHTML = '<div class="thumb-image"></div><div class="thumb-label"></div>';
      li.lastChild.textContent = String(p.order).padStart(2, "0") + " · " + (p.camera || p.file);
      li.addEventListener("click", () => showPhoto(i));
      ui.filmstrip.appendChild(li);
      if (p.previewable) {
        photo(session.name, p.file, 320).then((url) => { if (url) li.firstChild.style.backgroundImage = "url(" + url + ")"; });
      }
    });
    showPhoto(0);
  }

  function showPhoto(index) {
    const s = currentSession;
    if (!s || !s.photos.length) return;
    currentIndex = (index + s.photos.length) % s.photos.length;
    const p = s.photos[currentIndex];

    ui.scrubber.value = currentIndex;
    ui.viewerCamera.textContent = String(p.order).padStart(2, "0") + " · " + (p.camera || "?") + " — " + (currentIndex + 1) + " of " + s.photos.length;
    ui.filmstrip.querySelectorAll(".thumb").forEach((t, i) => t.classList.toggle("active", i === currentIndex));
    const active = ui.filmstrip.children[currentIndex];
    if (active) active.scrollIntoView({ block: "nearest", inline: "nearest" });

    if (!p.previewable) {
      ui.stageImage.removeAttribute("src");
      ui.stageMessage.textContent = "No preview for this file type (" + p.file + ").";
      ui.stageMessage.classList.remove("hidden");
      return;
    }
    ui.stageMessage.textContent = "Loading…";
    ui.stageMessage.classList.remove("hidden");
    const wanted = currentIndex;
    photo(s.name, p.file, 1600).then((url) => {
      if (currentSession !== s || currentIndex !== wanted) return;
      if (url) { ui.stageImage.src = url; ui.stageMessage.classList.add("hidden"); }
      else { ui.stageImage.removeAttribute("src"); ui.stageMessage.textContent = "Could not open " + p.file + "."; }
    });
    // Pré-carrega as próximas para a reprodução ficar fluida
    for (let k = 1; k <= 2; k++) {
      const next = s.photos[(currentIndex + k) % s.photos.length];
      if (next.previewable) photo(s.name, next.file, 1600);
    }
  }

  function stopPlay() {
    clearInterval(playTimer);
    playTimer = null;
    ui.playBtn.textContent = "Play";
  }

  function togglePlay() {
    if (playTimer) return stopPlay();
    if (!currentSession) return;
    ui.playBtn.textContent = "Pause";
    playTimer = setInterval(() => showPhoto(currentIndex + 1), 220);
  }

  ui.prevBtn.addEventListener("click", () => { stopPlay(); showPhoto(currentIndex - 1); });
  ui.nextBtn.addEventListener("click", () => { stopPlay(); showPhoto(currentIndex + 1); });
  ui.playBtn.addEventListener("click", togglePlay);
  ui.scrubber.addEventListener("input", () => { stopPlay(); showPhoto(+ui.scrubber.value); });
  ui.deleteSessionBtn.addEventListener("click", () => {
    if (!currentSession) return;
    const name = currentSession.name;
    dialog("Delete this session?", sessionLabel(name) + " — its photos will be deleted permanently.", [
      { label: "Cancel" },
      { label: "Delete", primary: true, onClick: () => window.deleteSession(name) },
    ]);
  });
  document.addEventListener("keydown", (e) => {
    if (ui.viewSessions.classList.contains("hidden") || e.target.tagName === "INPUT") return;
    if (e.key === "ArrowLeft") { stopPlay(); showPhoto(currentIndex - 1); }
    if (e.key === "ArrowRight") { stopPlay(); showPhoto(currentIndex + 1); }
    if (e.key === " ") { e.preventDefault(); togglePlay(); }
  });

  // =================== Salvar / sair ===================
  ui.saveBtn.addEventListener("click", () => window.saveSessions(false));

  function confirmExit(count) {
    dialog(
      count === 1 ? "You have 1 unsaved session." : "You have " + count + " unsaved sessions.",
      "Save them before closing, or exit and delete the photos.",
      [
        { label: "Cancel" },
        { label: "Exit and delete photos", onClick: () => window.exitApp(true) },
        { label: "Save and exit", primary: true, onClick: () => window.saveSessions(true) },
      ]
    );
  }

  function offerRecovery(count) {
    dialog(
      count === 1 ? "1 session from last time was kept." : count + " sessions from last time were kept.",
      "The app closed before they were saved. They are safe in the app's folder until you save or delete them.",
      [
        { label: "Delete them", link: true, onClick: () => dialog("Delete the kept sessions?", "Their photos will be deleted permanently.", [
          { label: "Cancel" },
          { label: "Delete", primary: true, onClick: () => window.deleteAllSessions() },
        ]) },
        { label: "Later" },
        { label: "Review", onClick: () => showView("sessions") },
        { label: "Save…", primary: true, onClick: () => window.saveSessions(false) },
      ]
    );
  }

  // =================== Avisos do programa ===================
  const handlers = {
    busy(e) {
      busy = e.busy;
      if (busy) {
        const labels = { detect: "Detecting cameras…", shoot: "Shooting…", identify: "Identifying…", reorder: "Reordering…" };
        setStatus(labels[e.action] || "Working…", "busy");
        if (e.action === "detect") log("🔄 Detecting cameras...", "muted");
      } else {
        readyStatus();
      }
      updateButtons();
    },

    cameras(e) {
      // Tempo real: avisa no log quando uma câmera cai
      e.cameras.forEach((cam) => {
        const before = cameras.find((c) => c.name === cam.name);
        if (before && before.connected && !cam.connected) log("🔌 " + cam.name + " disconnected — turn it on to reconnect.", "warn");
      });
      cameras = e.cameras;
      renderCameras();
      if (!busy) readyStatus();
    },

    sdkFailed() {
      setStatus("Canon SDK error", "error");
      log("⚠️ Failed to initialize the Canon SDK.", "error");
    },

    // Conexão
    connecting(e) {
      connecting[e.camera] = { seconds: 0, model: e.model };
      log("⏳ Connecting to " + e.camera + " (" + e.model + ")...", "muted");
      renderCameras();
    },
    connectingProgress(e) {
      if (!connecting[e.camera]) return;
      connecting[e.camera].seconds = e.seconds;
      if (e.seconds >= 2) ui.logLast.textContent = "⏳ Connecting to " + e.camera + DOTS[tick++ % 4] + " " + e.seconds + "s (camera is starting up, please wait)";
      renderCameras();
    },
    connected(e) {
      delete connecting[e.camera];
      log("✅ " + e.camera + " connected (" + e.seconds.toFixed(1) + "s)", "ok");
      if (!e.compatible) log("⚠️ " + e.model + " is not on Canon's compatibility list for this app.", "warn", "It may not work correctly.");
      renderCameras();
    },
    connectFailed(e) {
      delete connecting[e.camera];
      log("⚠️ " + e.camera + " failed to connect, error " + e.error + " (" + e.seconds.toFixed(1) + "s)", "warn", e.hint);
      renderCameras();
    },
    camerasFailed(e) { log("⚠️ Could not connect to: " + e.names.join(", "), "warn"); },
    retryStarted(e) { log("🔄 Retrying automatically for up to " + e.maxSeconds + "s...", "muted"); },
    waiting(e) {
      ui.logLast.textContent = "⌛ Waiting for " + e.models.length + (e.models.length === 1 ? " camera (" : " cameras (") + e.models.join(", ") + ")" + DOTS[tick++ % 4] + " " + e.seconds + "s";
    },
    allConnected() { log("✅ All cameras connected.", "ok"); },
    retryGaveUp() { log("⚠️ Could not connect failed cameras.", "warn"); },
    askScanAgain() {
      dialog("Could not connect failed cameras.", "Scan again, or continue with the cameras that are connected.", [
        { label: "Scan again", onClick: () => window.answerScanAgain(true) },
        { label: "Continue with the connected cameras", primary: true, onClick: () => window.answerScanAgain(false) },
      ]);
    },
    connectFinished(e) { if (e.result === "noCameras") log("⚠️ No cameras found.", "warn"); },

    // Sequência
    sessionStarted(e) {
      log("🎞️ Session " + e.index + " of " + e.total);
      ui.progress.textContent = "Session " + e.index + " of " + e.total;
    },
    sessionFolderFailed() { log("⚠️ Failed to prepare session folder.", "error"); },
    countdownStarted() {
      ui.countdownNumber.classList.remove("fire");
      ui.countdownOverlay.classList.remove("hidden");
    },
    countdownTick(e) {
      ui.countdownOverlay.classList.remove("hidden");
      ui.countdownNumber.textContent = e.seconds;
    },
    trigger() {
      ui.countdownOverlay.classList.remove("hidden");
      ui.countdownNumber.classList.add("fire");
      ui.countdownNumber.textContent = "IT'S BULLET TIME!";
      log("📸 IT'S BULLET TIME!");
      setTimeout(() => ui.countdownOverlay.classList.add("hidden"), 700);
    },
    shotFailed(e) { log("⚠️ " + e.camera + " did not take the picture: " + (e.hint || "error " + e.error) + ".", "warn"); },
    waitingDownloads() { ui.progress.textContent = "📥 Waiting for downloads..."; },
    downloadProgress(e) { ui.progress.textContent = "📥 " + e.done + "/" + e.total + " photos downloaded"; },
    downloadTimeout(e) {
      log("⚠️ Timeout reached.", "error");
      if (e.lost.length) log("🔌 Lost connection on cameras: " + e.lost.join(", "), "error");
      else log("ℹ️ No disconnected cameras detected.", "muted");
    },
    sessionInterrupted() { log("⚠️ Session interrupted.", "error"); },
    sessionCompleted() { log("✅ Session completed.", "ok"); },
    sequenceFinished(e) {
      if (e.completed) {
        log("🏁 Sequence completed successfully.", "ok");
        ui.progress.textContent = "🏁 Done — see it in Sessions";
      } else {
        log("⚠️ Sequence interrupted.", "error");
        ui.progress.textContent = "";
      }
    },

    // Fotos guardadas
    sessions(e) { setSessions(e.sessions); },
    saving() { toast("Saving sessions…", 60000); },
    saveCancelled() { toast("Save cancelled."); },
    saved(e) {
      if (e.error) {
        toast("⚠️ " + e.error, 8000);
        log("⚠️ " + e.error, "error");
      } else {
        toast("Saved " + e.count + (e.count === 1 ? " session" : " sessions") + " to " + e.folder);
        log("💾 Saved " + e.count + (e.count === 1 ? " session" : " sessions") + " to " + e.folder, "ok");
      }
    },
    confirmExit(e) { confirmExit(e.unsavedSessions); },

    log(e) { log(e.text.replace(/^\n+/, ""), "tech"); },
  };

  window.onCoreEvent = function (event) {
    const handler = handlers[event.type];
    if (handler) handler(event);
  };

  // =================== Ações ===================
  ui.detectBtn.addEventListener("click", () => window.detect());
  ui.shootBtn.addEventListener("click", () => {
    window.shoot(intValue(ui.countdown, 0, 0), intValue(ui.sessions, 1, 1), intValue(ui.downloadWait, 1, 10));
  });
  ui.logToggle.addEventListener("click", () => {
    ui.log.classList.toggle("expanded");
    ui.log.classList.toggle("collapsed");
    ui.logList.scrollTop = ui.logList.scrollHeight;
  });
  ui.techToggle.addEventListener("change", () => ui.logList.classList.toggle("show-tech", ui.techToggle.checked));

  updateButtons();
  window.uiReady().then((info) => {
    refreshSessions();
    if (info && info.unsavedSessions > 0) offerRecovery(info.unsavedSessions);
  });
})();
