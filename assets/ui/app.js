// Canon Bullet Time — comportamento da interface.
// Recebe os avisos do programa em window.onCoreEvent e chama o programa pelas
// funções que o C++ disponibiliza para a página: uiReady, shoot, identify,
// setPrefix, setOrder, takeOrderPhotos, getOrderPhoto, discardOrderPhotos,
// restartConnections, listSessions, getPhoto, saveSessions, deleteSession,
// deleteAllSessions e exitApp.
// As câmeras conectam sozinhas: a lista chega pelo aviso "cameras" sempre que muda.
//
// Textos da tela: frases curtas, sem emojis e sem cara de terminal.

(function () {
  const $ = (id) => document.getElementById(id);

  const ui = {
    status: $("status"), saveBtn: $("saveBtn"), sessionCount: $("sessionCount"),
    sessionList: $("sessionList"), noSessions: $("noSessions"),
    viewerEmpty: $("viewerEmpty"), sessionGallery: $("sessionGallery"),
    cameraList: $("cameraList"), noCameras: $("noCameras"), searching: $("searching"), orderBtn: $("orderBtn"),
    shootBtn: $("shootBtn"), shootNote: $("shootNote"), countdown: $("countdown"), sessions: $("sessions"), downloadWait: $("downloadWait"),
    progress: $("progress"),
    log: $("log"), logToggle: $("logToggle"), logLast: $("logLast"), logList: $("logList"), techToggle: $("techToggle"),
    restartBtn: $("restartBtn"),
    countdownOverlay: $("countdownOverlay"), countdownNumber: $("countdownNumber"), flash: $("flash"),
    orderOverlay: $("orderOverlay"), orderGallery: $("orderGallery"), orderHelp: $("orderHelp"),
    orderRetake: $("orderRetake"), orderCancel: $("orderCancel"), orderSave: $("orderSave"),
    dialogOverlay: $("dialogOverlay"), dialogTitle: $("dialogTitle"), dialogText: $("dialogText"), dialogActions: $("dialogActions"),
    toast: $("toast"),
  };

  const SEARCH_GRACE_MS = 4000;  // tempo procurando antes de mostrar "nenhuma câmera"
  const startedAt = Date.now();
  let busy = false;
  let busyAction = "";
  let cameras = [];              // última lista recebida (inclui as que estão conectando)
  let sessions = [];
  let currentSession = null;
  let playTimer = null;
  const photoCache = {};         // "loja/sessão/arquivo/tamanho" -> Promise<dataURL|null>

  // =================== Ícones ===================
  const ICONS = {
    up: '<path d="M3 9l4-4 4 4"/>',
    down: '<path d="M3 5l4 4 4-4"/>',
    left: '<path d="M9 3L5 7l4 4"/>',
    right: '<path d="M5 3l4 4-4 4"/>',
    play: '<path d="M4 2.5v9l7.5-4.5z" fill="currentColor" stroke="none"/>',
    pause: '<path d="M4 3v8M10 3v8"/>',
    folder: '<path d="M1.5 4.5v7h13v-8.5h-6.5l-1.5-1.5h-5v3z"/>',
    expand: '<path d="M2 5V2h3M9 2h3v3M12 9v3H9M5 12H2V9"/>',
    shrink: '<path d="M5 2v3H2M12 5H9V2M9 12V9h3M2 9h3v3"/>',
    trash: '<path d="M2.5 4h9M5.5 4V2.5h3V4M3.5 4l.6 7.5h5.8l.6-7.5"/>',
  };
  function icon(name, size) {
    const s = size || 14;
    const box = name === "folder" ? "0 0 16 14" : "0 0 14 14";
    return '<svg width="' + s + '" height="' + s + '" viewBox="' + box + '" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + ICONS[name] + "</svg>";
  }

  // =================== Utilitários ===================
  function setStatus(text, kind) {
    ui.status.textContent = text;
    ui.status.className = "status status-" + (kind || "idle");
  }

  function stillSearching() {
    return Date.now() - startedAt < SEARCH_GRACE_MS;
  }

  function plural(n, one, many) {
    return n + " " + (n === 1 ? one : many);
  }

  function pad(n) {
    return String(n).padStart(2, "0");
  }

  // Nome do modelo sem "Canon " (cabe melhor nas miniaturas)
  function shortModel(model) {
    return (model || "").replace(/^Canon\s+/i, "");
  }

  // Modelo da câmera pelo apelido (T7_1); se for o único daquele modelo, só o modelo
  function cameraLabel(name, model) {
    const cam = cameras.find((c) => c.name === name);
    const m = model || (cam && cam.model) || "";
    if (!m) return name;
    const sameModel = cameras.filter((c) => c.model === m).length;
    return sameModel > 1 ? m + " (" + name + ")" : m;
  }

  function readyStatus() {
    if (busy) return;
    const ready = cameras.filter((c) => c.connected).length;
    const connecting = cameras.filter((c) => c.state === "connecting").length;
    if (ready && connecting) setStatus(ready + " ready, " + connecting + " connecting", "ok");
    else if (ready) setStatus(plural(ready, "camera ready", "cameras ready"), "ok");
    else if (connecting) setStatus("Connecting", "busy");
    else if (stillSearching()) setStatus("Looking for cameras", "busy");
    else setStatus("No cameras", "idle");
  }

  // Mensagens do núcleo trazem emojis e espaços do terminal: só para os detalhes técnicos
  function plainText(text) {
    return text.replace(/[\u{1F000}-\u{1FAFF}\u{2190}-\u{21FF}\u{2300}-\u{23FF}\u{2600}-\u{27BF}\u{2B00}-\u{2BFF}\u{FE0F}\u{200D}]/gu, "")
      .replace(/\s+/g, " ").trim();
  }

  // Atividade: frases para a pessoa + detalhes técnicos (ocultos até marcar a opção)
  function log(text, kind, hint) {
    const li = document.createElement("li");
    if (kind) li.className = kind;
    const now = new Date();
    li.innerHTML = '<span class="log-time"></span><span class="log-mark"></span><span class="log-text"></span>';
    li.children[0].textContent = pad(now.getHours()) + ":" + pad(now.getMinutes()) + ":" + pad(now.getSeconds());
    li.children[2].textContent = text;
    if (hint) {
      const span = document.createElement("span");
      span.className = "log-hint";
      span.textContent = hint;
      li.children[2].appendChild(span);
    }
    const atBottom = ui.logList.scrollTop + ui.logList.clientHeight >= ui.logList.scrollHeight - 4;
    ui.logList.appendChild(li);
    if (atBottom) ui.logList.scrollTop = ui.logList.scrollHeight;
    if (kind !== "tech") ui.logLast.textContent = text + (hint ? " " + hint : "");
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
    // "2026-10-10_18-46-12_AB12_session_01" -> { date: "Oct 10, 18:46", detail: "Session 1" }
    const m = /^(\d{4})-(\d{2})-(\d{2})_(\d{2})-(\d{2})-(\d{2})(?:_[A-Z0-9]+_session_(\d+))?/.exec(name);
    if (!m) return { date: name, time: "", detail: "" };
    const date = new Date(+m[1], +m[2] - 1, +m[3]);
    return {
      date: date.toLocaleDateString(undefined, { month: "short", day: "numeric" }) + ", " + m[4] + ":" + m[5],
      time: m[4] + ":" + m[5] + ":" + m[6],
      detail: m[7] ? "Session " + (+m[7]) : "",
    };
  }

  function intValue(input, min, fallback) {
    const n = parseInt(input.value, 10);
    return Number.isFinite(n) && n >= min ? n : fallback;
  }

  function photo(loader, store, session, file, size) {
    const key = store + "/" + session + "/" + file + "/" + size;
    if (!photoCache[key]) photoCache[key] = Promise.resolve(loader(session, file, size));
    return photoCache[key];
  }
  const sessionPhoto = (s, f, size) => photo(window.getPhoto, "sessions", s, f, size);
  const orderPhoto = (s, f, size) => photo(window.getOrderPhoto, "order", s, f, size);

  // =================== Galeria: carrossel + foto grande ===================
  // Usada nas sessões e no "Reorder by photo". Com draggable, as miniaturas podem
  // ser arrastadas para mudar a ordem.
  function Gallery(root, opts) {
    root.innerHTML =
      '<div class="gallery-head"><div><div class="gallery-title"></div><div class="gallery-sub"></div></div><div class="gallery-actions"></div></div>' +
      '<div class="carousel-wrap"><ol class="carousel"></ol><div class="drop-marker hidden"></div></div>' +
      '<div class="stage"><img alt=""><div class="stage-caption hidden"></div><div class="stage-message hidden"></div></div>';
    let draggable = !!opts.draggable;
    root.classList.toggle("draggable", draggable);

    const el = {
      title: root.querySelector(".gallery-title"), sub: root.querySelector(".gallery-sub"),
      actions: root.querySelector(".gallery-actions"), carousel: root.querySelector(".carousel"),
      wrap: root.querySelector(".carousel-wrap"), marker: root.querySelector(".drop-marker"),
      img: root.querySelector(".stage img"), caption: root.querySelector(".stage-caption"),
      message: root.querySelector(".stage-message"),
    };
    let items = [];
    let index = 0;
    let drag = null;

    function badge(item, i) {
      return pad(draggable ? i + 1 : item.order || i + 1);
    }

    function render() {
      el.carousel.innerHTML = "";
      items.forEach((item, i) => el.carousel.appendChild(frame(item, i)));
      markSelected(false);
    }

    function frame(item, i) {
      const li = document.createElement("li");
      li.className = "frame";
      // Miniatura só com o número; o nome da câmera aparece na foto grande
      li.innerHTML = '<div class="frame-img"><span class="frame-badge"></span></div>';
      li.querySelector(".frame-badge").textContent = badge(item, i);
      li.title = item.title + (item.subtitle ? " · " + item.subtitle : "");
      const img = li.querySelector(".frame-img");
      if (item.file && item.previewable) {
        opts.load(item, 320).then((url) => { if (url) img.style.backgroundImage = "url(" + url + ")"; });
      } else {
        // Sem prévia: mostra o tipo do arquivo (ex.: "TIF"), ou nada se a câmera não mandou foto
        img.appendChild(document.createTextNode(item.file ? item.file.split(".").pop().toUpperCase() : ""));
      }

      if (draggable) {
        li.addEventListener("pointerdown", (e) => onPointerDown(e, li, i));
      } else {
        li.addEventListener("click", () => select(i));
      }
      return li;
    }

    function markSelected(scroll) {
      [...el.carousel.children].forEach((li, i) => li.classList.toggle("selected", i === index));
      const active = el.carousel.children[index];
      if (scroll && active) active.scrollIntoView({ block: "nearest", inline: "nearest" });
    }

    function message(text) {
      el.message.textContent = text;
      el.message.classList.toggle("hidden", !text);
    }

    function showLarge() {
      const item = items[index];
      if (!item) {
        el.img.removeAttribute("src");
        el.caption.classList.add("hidden");
        return;
      }
      el.caption.innerHTML = "<b></b><span></span>";
      el.caption.firstChild.textContent = badge(item, index);
      el.caption.lastChild.textContent = item.title + (item.subtitle ? " · " + item.subtitle : "");
      el.caption.classList.remove("hidden");

      if (!item.file) { el.img.removeAttribute("src"); return message("This camera did not send a photo."); }
      if (!item.previewable) { el.img.removeAttribute("src"); return message("No preview for this file type (" + item.file + ")."); }
      message(el.img.getAttribute("src") ? "" : "Loading");
      const wanted = item;
      opts.load(item, 1600).then((url) => {
        if (items[index] !== wanted) return;
        if (url) { el.img.src = url; message(""); }
        else { el.img.removeAttribute("src"); message("Could not open " + item.file + "."); }
      });
      // Pré-carrega as próximas para a reprodução ficar fluida
      for (let k = 1; k <= 2 && items.length > 1; k++) {
        const next = items[(index + k) % items.length];
        if (next.file && next.previewable) opts.load(next, 1600);
      }
    }

    function select(i) {
      if (!items.length) return;
      index = (i + items.length) % items.length;
      markSelected(true);
      showLarge();
    }

    // ----- Arrastar para reordenar -----
    function others() {
      return [...el.carousel.children].filter((li) => !li.classList.contains("dragging"));
    }

    function insertionIndex(x) {
      return others().filter((li) => {
        const r = li.getBoundingClientRect();
        return r.left + r.width / 2 < x;
      }).length;
    }

    function showMarker(target) {
      const list = others();
      const wrapRect = el.wrap.getBoundingClientRect();
      let x;
      if (!list.length) x = 0;
      else if (target < list.length) x = list[target].getBoundingClientRect().left - 5;
      else x = list[list.length - 1].getBoundingClientRect().right + 5;
      el.marker.style.left = (x - wrapRect.left) + "px";
      el.marker.classList.remove("hidden");
    }

    function onPointerDown(e, li, i) {
      if (e.button !== 0) return;
      drag = { li, i, x: e.clientX, y: e.clientY, scroll: el.carousel.scrollLeft, moved: false };
      li.setPointerCapture(e.pointerId);
      li.onpointermove = onPointerMove;
      li.onpointerup = onPointerUp;
      li.onpointercancel = () => endDrag(null);
    }

    function onPointerMove(e) {
      if (!drag) return;
      const dx = e.clientX - drag.x;
      if (!drag.moved && Math.abs(dx) < 6 && Math.abs(e.clientY - drag.y) < 6) return;
      drag.moved = true;
      drag.li.classList.add("dragging");

      // Rola o carrossel quando o arraste chega perto das bordas
      const r = el.carousel.getBoundingClientRect();
      if (e.clientX < r.left + 40) el.carousel.scrollLeft -= 12;
      if (e.clientX > r.right - 40) el.carousel.scrollLeft += 12;

      drag.li.style.transform = "translateX(" + (dx + el.carousel.scrollLeft - drag.scroll) + "px)";
      showMarker(insertionIndex(e.clientX));
    }

    function onPointerUp(e) {
      if (!drag) return;
      endDrag(drag.moved ? insertionIndex(e.clientX) : null);
    }

    function endDrag(target) {
      const d = drag;
      drag = null;
      if (!d) return;
      d.li.onpointermove = d.li.onpointerup = d.li.onpointercancel = null;
      d.li.classList.remove("dragging");
      d.li.style.transform = "";
      el.marker.classList.add("hidden");
      if (target === null) {
        if (!d.moved) select(d.i);
        return;
      }
      move(d.i, target);
    }

    function move(from, to) {
      if (to < 0 || to >= items.length || from === to) { select(from); return; }
      const [item] = items.splice(from, 1);
      items.splice(to, 0, item);
      index = to;
      render();
      showLarge();
      markSelected(true);
      if (opts.onReorder) opts.onReorder(items);
    }

    return {
      setDraggable(value) {
        draggable = !!value;
        root.classList.toggle("draggable", draggable);
        render();
      },
      setHead(title, sub) { el.title.textContent = title || ""; el.sub.textContent = sub || ""; },
      setActions(nodes) { el.actions.innerHTML = ""; nodes.forEach((n) => el.actions.appendChild(n)); },
      setItems(list, selectIndex) {
        items = list.slice();
        index = Math.min(selectIndex || 0, Math.max(0, items.length - 1));
        el.img.removeAttribute("src");
        render();
        showLarge();
        if (!items.length) message("");
      },
      message,
      select,
      next() { select(index + 1); },
      prev() { select(index - 1); },
      moveSelected(delta) { move(index, index + delta); },
      get items() { return items; },
      get index() { return index; },
    };
  }

  function button(label, onClick, opts) {
    const b = document.createElement("button");
    const o = opts || {};
    b.className = "btn btn-small " + (o.kind || "btn-secondary") + (o.icon && !label ? " btn-icon" : "");
    b.innerHTML = o.icon ? icon(o.icon) : "";
    if (label) b.appendChild(document.createTextNode(label));
    if (o.title) { b.title = o.title; b.setAttribute("aria-label", o.title); }
    b.addEventListener("click", onClick);
    return b;
  }

  // =================== Câmeras (tempo real) ===================
  function placedCameras() {
    return cameras.filter((c) => c.order > 0);
  }

  function renderCameras() {
    ui.cameraList.innerHTML = "";
    const placed = placedCameras();
    const empty = cameras.length === 0;
    ui.searching.classList.toggle("hidden", !empty || !stillSearching());
    ui.noCameras.classList.toggle("hidden", !empty || stillSearching());

    placed.forEach((cam, index) => ui.cameraList.appendChild(cameraRow(cam, index, placed.length)));
    cameras.filter((c) => c.order === 0).forEach((cam) => ui.cameraList.appendChild(cameraRow(cam, -1, placed.length)));
    updateButtons();
  }

  function cameraRow(cam, index, placedCount) {
    const state = cam.state || (cam.connected ? "connected" : "disconnected");

    const li = document.createElement("li");
    li.className = "camera is-" + state;

    const order = document.createElement("div");
    order.className = "camera-order";
    order.textContent = cam.order ? pad(cam.order) : "";

    const info = document.createElement("div");
    info.style.minWidth = "0";
    const model = document.createElement("div");
    model.className = "camera-model";
    model.innerHTML = '<span class="dot dot-' + state + '"></span>';
    model.appendChild(document.createTextNode(cam.model || cam.name));
    const meta = document.createElement("div");
    meta.className = "camera-meta";
    const stateText =
      state === "connected" ? "Ready"
      : state === "connecting" && cam.waiting ? "Waiting for another camera"
      : state === "connecting" ? "Connecting" + (cam.seconds >= 2 ? ", " + cam.seconds + " s" : "")
      : "Disconnected, turn it on to reconnect";
    meta.innerHTML = '<span></span> · <span class="state-' + state + '"></span>';
    meta.firstChild.textContent = cam.name;
    meta.lastChild.textContent = stateText;
    info.append(model, meta);

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
        smallButton(button("Identify", () => window.identify(cam.name), { title: "Half-press the shutter of this camera" }), state !== "connected", true),
        smallButton(button("", () => window.setPrefix(cam.name, cam.order - 1), { icon: "up", title: "Move up" }), index === 0),
        smallButton(button("", () => window.setPrefix(cam.name, cam.order + 1), { icon: "down", title: "Move down" }), index === placedCount - 1)
      );
    }

    li.append(order, info, actions);
    return li;
  }

  function smallButton(b, locked, usesCamera) {
    b.dataset.locked = locked ? "1" : "0";
    b.dataset.sdk = usesCamera ? "1" : "0";
    b.disabled = busy || !!locked;
    return b;
  }

  function updateButtons() {
    const connected = cameras.filter((c) => c.connected).length;
    // Enquanto uma câmera conecta, o SDK da Canon não aceita outros comandos
    const connectingNames = cameras.filter((c) => c.state === "connecting").map((c) => cameraLabel(c.name, c.model));
    const sdkBusy = connectingNames.length > 0;

    ui.restartBtn.disabled = busy;
    ui.shootBtn.disabled = busy || connected === 0 || sdkBusy;
    ui.orderBtn.disabled = busy || connected === 0 || sdkBusy;
    ui.shootNote.classList.toggle("hidden", !sdkBusy || connected === 0);
    ui.shootNote.textContent = "Available when " + connectingNames.join(" and ") + (connectingNames.length === 1 ? " finishes" : " finish") + " connecting.";
    document.querySelectorAll(".camera-actions button").forEach((b) =>
      (b.disabled = busy || b.dataset.locked === "1" || (sdkBusy && b.dataset.sdk === "1")));

    ui.saveBtn.disabled = sessions.length === 0;
    ui.saveBtn.textContent = sessions.length ? "Save sessions (" + sessions.length + ")" : "Save sessions";
    ui.sessionCount.textContent = sessions.length;
    ui.sessionCount.classList.toggle("hidden", sessions.length === 0);
  }

  // =================== Sessões ===================
  const sessionGallery = Gallery(ui.sessionGallery, {
    load: (item, size) => sessionPhoto(item.session, item.file, size),
  });
  const editing = { on: false };  // reordenando as fotos da sessão aberta
  const playBtn = button("", togglePlay, { icon: "play", title: "Play the sequence (space)" });
  const fullBtn = button("", toggleFullscreen, { icon: "expand", title: "Full screen (F)" });
  const viewActions = [
    button("", () => { stopPlay(); sessionGallery.prev(); }, { icon: "left", title: "Previous photo" }),
    playBtn,
    button("", () => { stopPlay(); sessionGallery.next(); }, { icon: "right", title: "Next photo" }),
    button("Reorder", startReorder, { title: "Change the order of the photos in this session" }),
    button("", deleteCurrentPhoto, { icon: "trash", title: "Delete this photo (Delete key)" }),
    button("Delete session", deleteCurrentSession, { kind: "btn-link", title: "Delete this session" }),
    fullBtn,
  ];
  const editActions = [
    button("", () => sessionGallery.moveSelected(-1), { icon: "left", title: "Move the selected photo earlier" }),
    button("", () => sessionGallery.moveSelected(1), { icon: "right", title: "Move the selected photo later" }),
    button("Cancel", () => stopReorder(false)),
    button("Save order", () => stopReorder(true), { kind: "btn-primary" }),
  ];
  sessionGallery.setActions(viewActions);

  function refreshSessions() {
    Promise.resolve(window.listSessions()).then(setSessions);
  }

  function filesOf(session) {
    return session ? session.photos.map((p) => p.file).join("|") : "";
  }

  // A sessão mais recente fica selecionada: ao abrir, quando chega uma sessão
  // nova e quando a selecionada é apagada ou salva
  function setSessions(list) {
    const known = sessions.map((x) => x.name);
    sessions = list || [];
    updateButtons();

    const newest = sessions[0] || null;
    const current = currentSession && sessions.find((x) => x.name === currentSession.name);
    const arrivedNewer = newest && !known.includes(newest.name);

    if (!newest) showSession(null);
    else if (!current || arrivedNewer) showSession(newest);
    else if (filesOf(current) !== filesOf(currentSession)) showSession(current, true);
    else { currentSession = current; renderSessionList(); }
  }

  function renderSessionList() {
    ui.sessionList.innerHTML = "";
    ui.noSessions.classList.toggle("hidden", sessions.length > 0);
    sessions.forEach((s) => {
      const label = sessionLabel(s.name);
      const count = sessionItems(s).length;
      const li = document.createElement("li");
      li.className = "folder" + (currentSession && currentSession.name === s.name ? " active" : "");
      li.innerHTML = icon("folder", 16) + '<div><div class="folder-date"></div><div class="folder-meta"></div></div>';
      li.querySelector(".folder-date").textContent = label.date;
      li.querySelector(".folder-meta").textContent = [plural(count, "photo", "photos"), label.detail].filter(Boolean).join(" · ");
      li.addEventListener("click", () => showSession(s));
      ui.sessionList.appendChild(li);
    });
  }

  // Uma foto por câmera: o RAW e o JPEG da mesma câmera ficam juntos (a prévia
  // usa o JPEG quando existe). Nome: o modelo da câmera, quando ela é conhecida.
  function sessionItems(session) {
    const items = [];
    const byGroup = {};
    session.photos.forEach((p) => {
      const group = p.camera ? pad(p.order) + "_" + p.camera : p.file;
      let item = byGroup[group];
      if (!item) {
        const cam = cameras.find((c) => c.name === p.camera);
        item = byGroup[group] = {
          group: group, movable: !!p.camera,
          session: session.name, order: p.order, files: [],
          title: cam ? shortModel(cam.model) : p.camera || p.file,
          subtitle: cam ? p.camera : "",
        };
        items.push(item);
      }
      item.files.push(p);
    });
    items.forEach((item) => {
      const pick = item.files.find((f) => /\.jpe?g$/i.test(f.file)) || item.files.find((f) => f.previewable) || item.files[0];
      item.file = pick.file;
      item.previewable = pick.previewable;
    });
    return items;
  }
  let sessionTitles = "";

  function sessionHead(session) {
    const label = sessionLabel(session.name);
    const count = sessionItems(session).length;
    sessionGallery.setHead(label.date + (label.time ? label.time.slice(5) : ""),
      editing.on ? "Drag the photos, or select one and use the arrows. Then save the order."
                 : [plural(count, "photo", "photos"), label.detail].filter(Boolean).join(" · "));
  }

  function showSession(session, keepIndex) {
    stopPlay();
    if (editing.on) setEditing(false);
    currentSession = session;
    renderSessionList();
    ui.viewerEmpty.classList.toggle("hidden", !!session);
    ui.sessionGallery.classList.toggle("hidden", !session);
    if (!session) {
      if (isFullscreen()) setFullscreen(false);
      return;
    }

    sessionHead(session);
    const items = sessionItems(session);
    sessionTitles = items.map((i) => i.title).join("|");
    sessionGallery.setItems(items, keepIndex ? sessionGallery.index : 0);
  }

  // As câmeras podem conectar depois que a sessão já está na tela: atualiza os nomes
  function refreshSessionTitles() {
    if (!currentSession || playTimer || editing.on) return;
    const titles = sessionItems(currentSession).map((i) => i.title).join("|");
    if (titles !== sessionTitles) showSession(currentSession, true);
  }

  // ----- Reordenar as fotos de uma sessão -----
  function setEditing(on) {
    editing.on = on;
    sessionGallery.setDraggable(on);
    sessionGallery.setActions(on ? editActions : viewActions);
    ui.sessionGallery.classList.toggle("editing", on);
    updateButtons();
  }

  function startReorder() {
    if (!currentSession) return;
    stopPlay();
    setEditing(true);
    sessionHead(currentSession);
  }

  function stopReorder(save) {
    const session = currentSession;
    const groups = sessionGallery.items.filter((i) => i.movable).map((i) => i.group);
    const changed = groups.join("|") !== sessionItems(session).filter((i) => i.movable).map((i) => i.group).join("|");
    // Números novos já na tela, enquanto os arquivos são renomeados
    if (save && changed) sessionGallery.items.forEach((item, i) => { item.order = i + 1; });
    setEditing(false);
    if (save && changed) {
      // As prévias guardadas usam os nomes antigos dos arquivos
      Object.keys(photoCache).filter((k) => k.startsWith("sessions/" + session.name + "/")).forEach((k) => delete photoCache[k]);
      window.reorderSession(session.name, ...groups);
      log("Photo order changed in the session from " + sessionLabel(session.name).date, "ok");
    } else {
      showSession(session, true);
    }
  }

  // ----- Tela cheia -----
  const sessionsPanel = document.querySelector(".sessions-panel");
  function setFullscreen(on) {
    sessionsPanel.classList.toggle("fullscreen", on);
    fullBtn.innerHTML = icon(on ? "shrink" : "expand");
    fullBtn.title = on ? "Exit full screen (Esc)" : "Full screen (F)";
    fullBtn.setAttribute("aria-label", fullBtn.title);
  }
  function isFullscreen() {
    return sessionsPanel.classList.contains("fullscreen");
  }
  function toggleFullscreen() {
    setFullscreen(!isFullscreen());
  }

  // ----- Apagar uma foto -----
  function deleteCurrentPhoto() {
    if (!currentSession || editing.on) return;
    const item = sessionGallery.items[sessionGallery.index];
    if (!item) return;
    stopPlay();
    const session = currentSession.name;
    const what = item.title + (item.subtitle ? " (" + item.subtitle + ")" : "");
    dialog("Delete photo " + pad(item.order || sessionGallery.index + 1) + "?",
      what + ". " + (item.files.length > 1 ? "Its RAW and JPEG files are" : "The file is") + " deleted permanently.", [
        { label: "Cancel" },
        { label: "Delete", primary: true, onClick: () => {
          window.deletePhotos(session, item.group);
          log("Photo " + pad(item.order || 0) + " deleted from the session of " + sessionLabel(session).date, "ok");
        } },
      ]);
  }

  function deleteCurrentSession() {
    if (!currentSession) return;
    const name = currentSession.name;
    const label = sessionLabel(name);
    dialog("Delete this session?", "The photos from " + label.date + " will be deleted permanently.", [
      { label: "Cancel" },
      { label: "Delete", primary: true, onClick: () => window.deleteSession(name) },
    ]);
  }

  function stopPlay() {
    clearInterval(playTimer);
    playTimer = null;
    playBtn.innerHTML = icon("play");
  }

  function togglePlay() {
    if (playTimer) return stopPlay();
    if (!currentSession || editing.on) return;
    playBtn.innerHTML = icon("pause");
    playTimer = setInterval(() => sessionGallery.next(), 220);
  }

  // =================== Reordenar por foto ===================
  const order = { open: false, capturing: false, session: null };
  const orderGallery = Gallery(ui.orderGallery, {
    draggable: true,
    load: (item, size) => orderPhoto(item.session, item.file, size),
  });
  orderGallery.setActions([
    button("Move earlier", () => orderGallery.moveSelected(-1), { icon: "left" }),
    button("Move later", () => orderGallery.moveSelected(1), { icon: "right" }),
  ]);

  function openOrder() {
    order.open = true;
    ui.orderOverlay.classList.remove("hidden");
    takeOrderPhotos();
  }

  function takeOrderPhotos() {
    order.capturing = true;
    order.session = null;
    orderGallery.setItems([]);
    orderGallery.setHead("Taking a photo with each camera", "");
    orderGallery.message("Taking a photo with each camera");
    updateOrderButtons();
    window.takeOrderPhotos(intValue(ui.downloadWait, 1, 10));
  }

  function showOrderPhotos(list) {
    order.capturing = false;
    const session = (list || [])[0];
    order.session = session ? session.name : null;

    // Uma foto por câmera conectada, na ordem atual (JPEG quando houver RAW + JPEG)
    const items = placedCameras().filter((c) => c.connected).map((cam) => {
      const photos = session ? session.photos.filter((p) => p.camera === cam.name) : [];
      const pick = photos.find((p) => /\.jpe?g$/i.test(p.file)) || photos.find((p) => p.previewable) || photos[0];
      return {
        name: cam.name, session: order.session,
        file: pick ? pick.file : "", previewable: pick ? pick.previewable : false,
        title: shortModel(cam.model), subtitle: cam.name,
      };
    });
    orderGallery.setItems(items, 0);
    const missing = items.filter((i) => !i.file).length;
    orderGallery.setHead(plural(items.length, "camera", "cameras"),
      missing ? plural(missing, "camera", "cameras") + " did not send a photo" : "Drag a photo, or select it and use Move earlier / Move later");
    updateOrderButtons();
  }

  function closeOrder(save) {
    if (save) {
      const names = orderGallery.items.map((i) => i.name);
      window.setOrder(...names);
      log("Camera order updated from photos", "ok");
    }
    order.open = false;
    order.capturing = false;
    ui.orderOverlay.classList.add("hidden");
    window.discardOrderPhotos();
    Object.keys(photoCache).filter((k) => k.startsWith("order/")).forEach((k) => delete photoCache[k]);
  }

  function updateOrderButtons() {
    ui.orderRetake.disabled = order.capturing;
    ui.orderSave.disabled = order.capturing || orderGallery.items.length < 1;
  }

  ui.orderBtn.addEventListener("click", openOrder);
  ui.orderRetake.addEventListener("click", takeOrderPhotos);
  ui.orderCancel.addEventListener("click", () => closeOrder(false));
  ui.orderSave.addEventListener("click", () => closeOrder(true));

  // =================== Teclado ===================
  document.addEventListener("keydown", (e) => {
    if (e.target.tagName === "INPUT" || !ui.dialogOverlay.classList.contains("hidden")) return;
    const target = order.open ? orderGallery : currentSession ? sessionGallery : null;
    if (!target) return;
    if (e.key === "ArrowLeft") { e.preventDefault(); stopPlay(); target.prev(); }
    if (e.key === "ArrowRight") { e.preventDefault(); stopPlay(); target.next(); }
    if (e.key === " " && !order.open) { e.preventDefault(); togglePlay(); }
    if (order.open) return;
    // Sessão: Esc sai da reordenação ou da tela cheia; F tela cheia; Delete apaga a foto
    if (e.key === "Escape") {
      if (editing.on) stopReorder(false);
      else if (isFullscreen()) setFullscreen(false);
    }
    if ((e.key === "f" || e.key === "F") && !editing.on) toggleFullscreen();
    if ((e.key === "Delete" || e.key === "Backspace") && !editing.on) { e.preventDefault(); deleteCurrentPhoto(); }
  });

  // =================== Salvar / sair ===================
  ui.saveBtn.addEventListener("click", () => window.saveSessions(false));

  function confirmExit(count) {
    dialog(
      "Save your sessions before closing?",
      plural(count, "session is", "sessions are") + " not saved yet. If you close without saving, the photos are deleted.",
      [
        { label: "Cancel" },
        { label: "Delete and close", onClick: () => window.exitApp(true) },
        { label: "Save and close", primary: true, onClick: () => window.saveSessions(true) },
      ]
    );
  }

  function offerRecovery(count) {
    dialog(
      "Sessions from last time",
      "The app closed before " + plural(count, "session was", "sessions were") + " saved. The photos are safe and shown in Sessions.",
      [
        { label: "Delete", link: true, onClick: () => dialog("Delete the kept sessions?", "Their photos will be deleted permanently.", [
          { label: "Cancel" },
          { label: "Delete", primary: true, onClick: () => window.deleteAllSessions() },
        ]) },
        { label: "Later" },
        { label: "Save", primary: true, onClick: () => window.saveSessions(false) },
      ]
    );
  }

  // =================== Avisos do programa ===================
  // Enquanto as fotos do "Reorder by photo" são tiradas, os avisos de disparo
  // vão para essa janela, não para a atividade nem para a contagem.
  const orderCapture = () => order.open && order.capturing;

  const handlers = {
    busy(e) {
      busy = e.busy;
      busyAction = e.busy ? e.action : "";
      if (busy) {
        const labels = { restart: "Restarting connections", shoot: "Shooting", identify: "Identifying", reorder: "Saving order", orderPhotos: "Taking photos" };
        setStatus(labels[e.action] || "Working", "busy");
        if (e.action === "restart") log("Restarting camera connections");
      } else {
        if (orderCapture()) {
          // O pedido terminou sem fotos (ex.: uma câmera ainda conectando)
          order.capturing = false;
          orderGallery.setHead("No photos", "");
          orderGallery.message("The photos could not be taken. Try again when all cameras are ready.");
          updateOrderButtons();
        }
        readyStatus();
      }
      updateButtons();
    },

    cameras(e) {
      e.cameras.forEach((cam) => {
        const before = cameras.find((c) => c.name === cam.name);
        if (before && before.connected && !cam.connected) log(cameraLabel(cam.name, cam.model) + " turned off or disconnected. Turn it on to reconnect.", "warn");
      });
      cameras = e.cameras;
      renderCameras();
      readyStatus();
      refreshSessionTitles();

      // Rodapé: quem está demorando para conectar
      const slow = cameras.filter((c) => c.state === "connecting" && !c.waiting && c.seconds >= 2);
      if (slow.length) {
        ui.logLast.textContent = "Connecting " + slow.map((c) => cameraLabel(c.name, c.model)).join(" and ") +
          ". Some cameras take up to a minute.";
      }
    },

    closing() {
      setStatus("Closing", "busy");
      busy = true;
      updateButtons();
    },
    waitConnecting() {
      toast("A camera is still connecting. Try again when it is ready.");
    },

    sdkFailed() {
      setStatus("Canon SDK error", "error");
      log("The Canon SDK could not start.", "error");
    },

    // Conexão
    connecting(e) { log("Connecting " + cameraLabel(e.camera, e.model)); },
    connected(e) {
      log(cameraLabel(e.camera, e.model) + " connected", "ok");
      if (!e.compatible) log(e.model + " is not on Canon's compatibility list.", "warn", "It may not work correctly.");
    },
    connectFailed(e) {
      log(cameraLabel(e.camera, e.model) + " did not connect. Trying again automatically.", "warn", e.hint);
    },

    // Sequência
    sessionStarted(e) {
      if (orderCapture()) return;
      if (e.total > 1) log("Session " + e.index + " of " + e.total);
      ui.progress.textContent = e.total > 1 ? "Session " + e.index + " of " + e.total : "Shooting";
    },
    sessionFolderFailed() { log("Could not create the folder for the session.", "error"); },
    countdownStarted() {
      if (orderCapture()) return;
      ui.countdownOverlay.classList.remove("hidden");
    },
    countdownTick(e) {
      if (orderCapture()) return;
      ui.countdownOverlay.classList.remove("hidden");
      ui.countdownNumber.textContent = e.seconds;
    },
    trigger() {
      ui.countdownOverlay.classList.add("hidden");
      if (orderCapture()) return;
      // Clarão rápido no lugar de texto
      ui.flash.classList.remove("hidden");
      ui.flash.style.animation = "none";
      void ui.flash.offsetWidth;
      ui.flash.style.animation = "";
      setTimeout(() => ui.flash.classList.add("hidden"), 500);
    },
    shotFailed(e) {
      // As explicações do núcleo vêm em minúsculas ("no memory card"): vira frase
      const hint = e.hint ? e.hint.charAt(0).toUpperCase() + e.hint.slice(1) + "." : "";
      log(cameraLabel(e.camera) + " did not take the photo.", "warn", hint);
    },
    waitingDownloads() {
      if (orderCapture()) return orderGallery.message("Receiving photos");
      ui.progress.textContent = "Receiving photos";
    },
    downloadProgress(e) {
      const text = "Receiving photos, " + e.done + " of " + e.total;
      if (orderCapture()) return orderGallery.message(text);
      ui.progress.textContent = text;
    },
    downloadTimeout(e) {
      log("Some photos did not arrive in time.", "error",
        e.lost.length ? "Lost connection: " + e.lost.map((n) => cameraLabel(n)).join(", ") + "." : "No camera was disconnected. Try a longer download wait.");
    },
    sessionInterrupted() { if (!orderCapture()) log("The session stopped before the end.", "error"); },
    sessionCompleted() {},
    sequenceFinished(e) {
      if (orderCapture()) return;
      if (e.completed) {
        log("Bullet time complete", "ok");
        ui.progress.textContent = "Done. The new session is shown above.";
      } else {
        log("Bullet time stopped before the end.", "error");
        ui.progress.textContent = "";
      }
    },

    deleteFailed(e) {
      toast(e.error, 8000);
      log("The photo could not be deleted.", "error", e.error);
    },
    reorderFailed(e) {
      toast(e.error, 8000);
      log("The photo order could not be changed.", "error", e.error);
    },

    // Reordenar por foto
    orderPhotos(e) { if (order.open) showOrderPhotos(e.sessions); },

    // Fotos guardadas
    sessions(e) { setSessions(e.sessions); },
    saving() { toast("Saving sessions", 60000); },
    saveCancelled() { toast("Save cancelled"); },
    saved(e) {
      if (e.error) {
        toast(e.error, 8000);
        log(e.error, "error");
      } else {
        const text = "Saved " + plural(e.count, "session", "sessions") + " to " + e.folder;
        toast(text);
        log(text, "ok");
      }
    },
    confirmExit(e) { confirmExit(e.unsavedSessions); },

    log(e) {
      const text = plainText(e.text);
      if (text) log(text, "tech");
    },
  };

  window.onCoreEvent = function (event) {
    const handler = handlers[event.type];
    if (handler) handler(event);
  };

  // =================== Ações ===================
  ui.restartBtn.addEventListener("click", () => {
    dialog("Restart camera connections?", "Use this when a camera is on and plugged in but does not connect. All cameras reconnect.", [
      { label: "Cancel" },
      { label: "Restart", primary: true, onClick: () => window.restartConnections() },
    ]);
  });
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
  renderCameras();
  readyStatus();
  // Ao fim da procura inicial, mostra o guia se nenhuma câmera apareceu
  setTimeout(() => { renderCameras(); readyStatus(); }, SEARCH_GRACE_MS + 100);
  Promise.resolve(window.uiReady()).then((info) => {
    refreshSessions();
    if (info && info.unsavedSessions > 0) offerRecovery(info.unsavedSessions);
  });
})();
