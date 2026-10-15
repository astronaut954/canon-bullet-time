// Stillpoint — comportamento da interface.
// Recebe os avisos do programa em window.onCoreEvent e chama o programa pelas
// funções que o C++ disponibiliza para a página: uiReady, shoot, identify,
// setPrefix, setOrder, takeOrderPhotos, getOrderPhoto, discardOrderPhotos,
// restartConnections, listSessions, getPhoto, saveSessions (só as sessões escolhidas), deleteSession,
// deleteAllSessions e exitApp.
// As câmeras conectam sozinhas: a lista chega pelo aviso "cameras" sempre que muda.
//
// Textos da tela: frases curtas, sem emojis e sem cara de terminal.

(function () {
  const $ = (id) => document.getElementById(id);

  // =================== Abertura ===================
  // Chuva de código que vai aparecendo coluna por coluna; depois o logo surge e a
  // abertura some sozinha. Clique ou qualquer tecla pula. As câmeras já começam a
  // conectar por trás enquanto ela roda.
  (function splash() {
    const root = $("splash");
    const canvas = $("splashRain");
    if (!root || !canvas) return;
    const reduced = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    const LOGO_AT = reduced ? 0 : 2300;   // ms até o logo aparecer
    const LEAVE_AT = reduced ? 1200 : 4600; // ms até a abertura começar a sumir
    const ctx = canvas.getContext("2d");
    const glyphs = "ｱｲｳｴｵｶｷｸｹｺｻｼｽｾｿﾀﾁﾂﾃﾄﾅﾆﾇﾈﾉﾊﾋﾌﾍﾎﾏﾐﾑﾒﾓﾔﾕﾖﾗﾘﾙﾚﾛﾜﾝ0123456789Z:=*+-<>";
    const cell = 18;
    let width = 0, height = 0, columns = [], started = 0, last = 0, done = false;

    function resize() {
      const ratio = window.devicePixelRatio || 1;
      width = window.innerWidth; height = window.innerHeight;
      canvas.width = width * ratio; canvas.height = height * ratio;
      ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
      ctx.fillStyle = "#000"; ctx.fillRect(0, 0, width, height);
      const count = Math.ceil(width / cell);
      // Cada coluna começa num momento diferente: a chuva vai "chegando"
      columns = Array.from({ length: count }, () => ({
        y: -Math.random() * height * 0.6,
        speed: 0.6 + Math.random() * 0.8,
        startAt: Math.random() * 1800,
      }));
    }

    function frame() {
      if (done) return;
      const now = performance.now();
      if (!started) started = now;
      const t = now - started;
      if (now - last >= 50) {  // ~20 quadros por segundo, como no filme
        last = now;
        ctx.fillStyle = "rgba(0, 0, 0, 0.09)";
        ctx.fillRect(0, 0, width, height);
        ctx.font = "15px 'Hiragino Sans', 'Yu Gothic', 'Meiryo', 'MS Gothic', 'JetBrains Mono', monospace";
        ctx.textBaseline = "top";
        columns.forEach((c, i) => {
          if (t < c.startAt) return;
          const x = i * cell;
          const ch = glyphs[(Math.random() * glyphs.length) | 0];
          // rastro verde e a "cabeça" da coluna mais clara
          ctx.fillStyle = "#00cc33";
          ctx.fillText(glyphs[(Math.random() * glyphs.length) | 0], x, c.y - cell);
          ctx.fillStyle = "#c8ffd6";
          ctx.shadowColor = "#00ff41"; ctx.shadowBlur = 8;
          ctx.fillText(ch, x, c.y);
          ctx.shadowBlur = 0;
          c.y += cell * c.speed;
          if (c.y > height + cell && Math.random() > 0.96) c.y = -cell * (1 + Math.random() * 10);
        });
      }
      if (t >= LOGO_AT) root.classList.add("show-logo");
      if (t >= LEAVE_AT) return leave();
      requestAnimationFrame(frame);
    }

    function leave() {
      if (done) return;
      done = true;
      root.classList.add("show-logo", "leaving");
      setTimeout(() => root.remove(), 800);
      window.removeEventListener("resize", resize);
      document.removeEventListener("keydown", leave, true);
    }

    resize();
    window.addEventListener("resize", resize);
    root.addEventListener("click", leave);
    document.addEventListener("keydown", leave, true);
    requestAnimationFrame(frame);
  })();

  const ui = {
    status: $("status"), saveBtn: $("saveBtn"), sessionCount: $("sessionCount"),
    sessionList: $("sessionList"), noSessions: $("noSessions"),
    viewerEmpty: $("viewerEmpty"), sessionGallery: $("sessionGallery"),
    cameraList: $("cameraList"), noCameras: $("noCameras"), searching: $("searching"), orderBtn: $("orderBtn"),
    shootBtn: $("shootBtn"), shootNote: $("shootNote"), countdown: $("countdown"), sessions: $("sessions"), downloadWait: $("downloadWait"),
    progress: $("progress"),
    log: $("log"), logToggle: $("logToggle"), logLast: $("logLast"), logList: $("logList"), techToggle: $("techToggle"),
    restartBtn: $("restartBtn"), copyLogBtn: $("copyLogBtn"), restartAppBtn: $("restartAppBtn"),
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
    minus: '<path d="M3 7h8"/>',
    plus: '<path d="M3 7h8M7 3v8"/>',
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

  // Rodapé: enquanto algo está em andamento (conectando, disparando, salvando...),
  // mostra o que é com reticências animadas; senão, a última mensagem.
  const DOTS = ["", ".", "..", "..."];
  const activity = { last: "", busyText: "", saving: false, closing: false, step: 0 };
  const BUSY_TEXT = {
    restart: "Restarting camera connections", shoot: "Shooting", identify: "Identifying the camera",
    reorder: "Saving the camera order", orderPhotos: "Taking a photo with each camera",
  };

  function ongoingText() {
    if (activity.closing) return "Closing";
    if (activity.saving) return "Saving sessions";
    if (busy) return activity.busyText || BUSY_TEXT[busyAction] || "Working";
    const connecting = cameras.filter((c) => c.state === "connecting" && !c.waiting);
    if (connecting.length) return "Connecting " + connecting.map((c) => cameraLabel(c.name, c.model)).join(" and ");
    if (!cameras.length && stillSearching()) return "Looking for cameras";
    return "";
  }

  function renderActivity() {
    const text = ongoingText();
    ui.logLast.innerHTML = '<span></span><span class="dots"></span>';
    ui.logLast.firstChild.textContent = text || activity.last;
    ui.logLast.lastChild.textContent = text ? DOTS[activity.step % DOTS.length] : "";
  }

  setInterval(() => {
    activity.step++;
    if (ongoingText() || ui.logLast.lastChild && ui.logLast.lastChild.textContent) renderActivity();
  }, 400);

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
    if (kind !== "tech") {
      activity.last = text + (hint ? " " + hint : "");
      renderActivity();
    }
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
      b.className = "btn " + (a.danger ? "btn-danger" : a.primary ? "btn-primary" : a.link ? "btn-link" : "btn-secondary");
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
      '<div class="stage"><img alt=""><div class="stage-caption hidden"></div><div class="stage-message hidden"></div><div class="stage-tools"></div></div>';
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
      el.caption.innerHTML = "<b></b><span></span><span class=\"caption-file\"></span>";
      el.caption.children[0].textContent = badge(item, index);
      el.caption.children[1].textContent = item.title + (item.subtitle ? " · " + item.subtitle : "");
      // Nome do(s) arquivo(s) com a extensão (RAW e JPEG da mesma câmera aparecem juntos)
      const files = item.files ? item.files.map((f) => f.file) : item.file ? [item.file] : [];
      el.caption.children[2].textContent = files.join("  +  ");
      el.caption.children[2].classList.toggle("hidden", !files.length);
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
      // Botões sobre a foto grande (canto superior direito)
      setStageTools(nodes) {
        const tools = root.querySelector(".stage-tools");
        tools.innerHTML = "";
        nodes.forEach((n) => tools.appendChild(n));
      },
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
      : state === "stuck" ? "Not responding. Turn it off and on"
      : state === "connecting" && cam.waiting ? "Waiting for another camera"
      : state === "connecting" ? "Connecting" + (cam.seconds >= 2 ? ", " + cam.seconds + " s" : "")
      : "Disconnected, turn it on to reconnect";
    // Câmera que o USB já mostra e o SDK ainda não: ainda sem nome (T7_1...)
    meta.innerHTML = (cam.name ? '<span></span> · ' : "") + '<span class="state-' + state + '"></span>';
    if (cam.name) meta.firstChild.textContent = cam.name;
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

  // Conexão que passa de 1 minuto (a T7 leva ~40 s): o SDK da Canon não deixa
  // cancelar, e o disparo fica bloqueado até ela terminar. Oferece reiniciar o app.
  const SLOW_CONNECT_SECONDS = 60;
  let testRestart = false;   // STILLPOINT_TEST_RESTART=1: botão "Restart app" sempre visível (teste)
  const slowWarned = new Set();
  function checkSlowConnections() {
    const slow = cameras.filter((c) => c.state === "connecting" && !c.starting && !c.waiting && c.seconds >= SLOW_CONNECT_SECONDS);
    ui.restartAppBtn.classList.toggle("hidden", slow.length === 0 && !testRestart);
    if (!slow.length) return;
    const names = slow.map((c) => cameraLabel(c.name, c.model));
    ui.shootNote.classList.remove("hidden");
    ui.shootNote.textContent = names.join(" and ") + (names.length === 1 ? " is" : " are") +
      " taking too long to connect. Turn " + (names.length === 1 ? "that camera" : "those cameras") +
      " off to shoot with the others, or restart the app. Photos that are not saved are kept.";
    const fresh = slow.filter((c) => !slowWarned.has(c.name));
    if (!fresh.length) return;
    fresh.forEach((c) => slowWarned.add(c.name));
    log(names.join(" and ") + " has been connecting for over a minute", "warn");
    if (ui.dialogOverlay.classList.contains("hidden") && $("exportOverlay").classList.contains("hidden") && $("progressOverlay").classList.contains("hidden"))
      dialog("A camera is taking too long to connect",
        names.join(" and ") + " has been connecting for over a minute. Shooting stays blocked until it finishes. " +
        "Turn " + (names.length === 1 ? "that camera" : "those cameras") + " off to shoot with the others right away, " +
        "or restart the app. Photos that are not saved are kept.", [
        { label: "Keep waiting" },
        { label: "Restart app", primary: true, onClick: restartApp },
      ]);
  }
  // Quando a conexão termina, avisa de novo se ela demorar outra vez
  function forgetFinishedSlow() {
    [...slowWarned].forEach((name) => {
      if (!cameras.some((c) => c.name === name && c.state === "connecting")) slowWarned.delete(name);
    });
  }
  function restartApp() {
    log("Restarting the app", "warn");
    Promise.resolve(window.restartApp()).then((r) => {
      if (r && r.ok === false) toast("Could not restart the app. Close it and open it again.", 8000);
    });
  }

  function updateButtons() {
    forgetFinishedSlow();
    const connected = cameras.filter((c) => c.connected).length;
    // Enquanto uma câmera conecta, o SDK da Canon não aceita outros comandos. Uma câmera
    // que só o USB mostra (ainda ligando) não ocupa o SDK: dá para disparar com as outras.
    const connectingNames = cameras.filter((c) => c.state === "connecting" && !c.starting).map((c) => cameraLabel(c.name, c.model));
    const sdkBusy = connectingNames.length > 0;

    // Reiniciar as conexões espera a conexão em andamento terminar: não durante uma
    ui.restartBtn.disabled = busy || sdkBusy;
    ui.shootBtn.disabled = busy || connected === 0 || sdkBusy;
    ui.orderBtn.disabled = busy || connected === 0 || sdkBusy;
    ui.shootNote.classList.toggle("hidden", !sdkBusy || connected === 0);
    ui.shootNote.textContent = "Available when " + connectingNames.join(" and ") + (connectingNames.length === 1 ? " finishes" : " finish") + " connecting.";
    checkSlowConnections();
    document.querySelectorAll(".camera-actions button").forEach((b) =>
      (b.disabled = busy || b.dataset.locked === "1" || (sdkBusy && b.dataset.sdk === "1")));

    ui.saveBtn.disabled = sessions.length === 0;
    ui.saveBtn.textContent = sessions.length ? "Save sessions (" + sessions.length + ")" : "Save sessions";
    ui.sessionCount.textContent = sessions.length;
    ui.sessionCount.classList.toggle("hidden", sessions.length === 0);
    // A janela de atualização aberta acompanha o estado (ex.: libera "Update now")
    if (upd && upd.release && !updEl.overlay.classList.contains("hidden")) renderUpdate();
  }

  // =================== Sessões ===================
  const sessionGallery = Gallery(ui.sessionGallery, {
    load: (item, size) => sessionPhoto(item.session, item.file, size),
  });
  const editing = { on: false };  // reordenando as fotos da sessão aberta
  const playBtn = button("", togglePlay, { icon: "play", title: "Play the sequence (space)" });
  // Fora da tela cheia: só "Delete session" e o botão de tela cheia, ao lado das
  // miniaturas. Os outros controles (navegar, play, reordenar, apagar foto) ficam
  // na tela cheia, junto com a data da sessão.
  const fullBtn = button("", () => setFullscreen(true), { icon: "expand", title: "Full screen (F)" });
  const compactActions = [
    button("Delete session", deleteCurrentSession, { kind: "btn-link", title: "Delete this session" }),
    fullBtn,
  ];
  const exitFullBtn = button("", () => setFullscreen(false), { icon: "shrink", title: "Exit full screen (Esc)" });
  // "Reverse loop" (ligado por padrão): o play vai e volta (1, 2, 3, 2, 1...);
  // desligado, recomeça do início (1, 2, 3, 1, 2, 3...)
  const reverseLoopOption = document.createElement("label");
  reverseLoopOption.className = "log-option play-option";
  reverseLoopOption.title = "Play back and forth (1, 2, 3, 2, 1...)";
  reverseLoopOption.innerHTML = '<input type="checkbox" checked> Reverse loop';
  const reverseLoop = reverseLoopOption.querySelector("input");

  // Velocidade do play: de 0,5x a 2x (sem extremos); 1x = 220 ms por foto
  const PLAY_SPEEDS = [0.5, 0.75, 1, 1.25, 1.5, 2];
  let speedIndex = PLAY_SPEEDS.indexOf(1);
  const speedLabel = document.createElement("span");
  speedLabel.className = "play-speed";
  speedLabel.title = "Playback speed";
  const slowerBtn = button("", () => changeSpeed(-1), { icon: "minus", title: "Slower" });
  const fasterBtn = button("", () => changeSpeed(1), { icon: "plus", title: "Faster" });
  function playInterval() { return Math.round(220 / PLAY_SPEEDS[speedIndex]); }
  function renderSpeed() {
    const v = PLAY_SPEEDS[speedIndex];
    speedLabel.textContent = (v % 1 === 0 ? v.toFixed(1) : String(v)) + "x";  // 1.0x, 1.25x, 2.0x
    slowerBtn.disabled = speedIndex === 0;
    fasterBtn.disabled = speedIndex === PLAY_SPEEDS.length - 1;
  }
  function changeSpeed(delta) {
    speedIndex = Math.max(0, Math.min(PLAY_SPEEDS.length - 1, speedIndex + delta));
    renderSpeed();
    if (playTimer) {  // já tocando: continua, na velocidade nova
      clearInterval(playTimer);
      playTimer = setInterval(playStep, playInterval());
    }
  }
  renderSpeed();
  // Velocidade num grupo à parte (com separador), para não se misturar com ‹ ▶ ›
  const speedGroup = document.createElement("div");
  speedGroup.className = "play-group";
  const speedCaption = document.createElement("span");
  speedCaption.className = "play-group-caption";
  speedCaption.textContent = "Speed";
  speedGroup.append(speedCaption, slowerBtn, speedLabel, fasterBtn);

  const viewActions = [
    button("", () => { stopPlay(); sessionGallery.prev(); }, { icon: "left", title: "Previous photo" }),
    playBtn,
    button("", () => { stopPlay(); sessionGallery.next(); }, { icon: "right", title: "Next photo" }),
    speedGroup,
    reverseLoopOption,
    button("Reorder", startReorder, { title: "Change the order of the photos in this session" }),
    button("", deleteCurrentPhoto, { icon: "trash", title: "Delete this photo (Delete key)" }),
    button("Delete session", deleteCurrentSession, { kind: "btn-link", title: "Delete this session" }),
    exitFullBtn,
  ];
  const editActions = [
    button("", () => sessionGallery.moveSelected(-1), { icon: "left", title: "Move the selected photo earlier" }),
    button("", () => sessionGallery.moveSelected(1), { icon: "right", title: "Move the selected photo later" }),
    button("Cancel", () => stopReorder(false)),
    button("Save order", () => stopReorder(true), { kind: "btn-primary" }),
  ];
  function currentActions() {
    return editing.on ? editActions : isFullscreen() ? viewActions : compactActions;
  }
  sessionGallery.setActions(compactActions);

  function refreshSessions() {
    Promise.resolve(window.listSessions()).then(setSessions);
  }

  function filesOf(session) {
    return session ? session.photos.map((p) => p.file).join("|") : "";
  }

  // A sessão mais recente fica selecionada: ao abrir, quando chega uma sessão
  // nova e quando a selecionada é apagada ou salva
  function setSessions(list) {
    setTimeout(() => {
      // Janela de salvar aberta (ex.: salvamento com erro): mostra só o que ainda está no app
      if ($("exportOverlay").classList.contains("hidden") || !sessions.length) return;
      exp.selected = new Set([...exp.selected].filter((n) => sessions.some((s) => s.name === n)));
      if (!sessions.some((s) => s.name === exp.active)) exp.active = sessions[0].name;
      renderExport();
    }, 0);
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
    sessionGallery.setHead(label.date + (label.time ? label.time.slice(5) : ""),
      editing.on ? "Drag the photos, or select one and use the arrows. Then save the order."
                 : label.detail);
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
    sessionGallery.setActions(currentActions());
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
    if (on && !currentSession) return;
    if (!on) {
      stopPlay();
      if (editing.on) stopReorder(false);
    }
    sessionsPanel.classList.toggle("fullscreen", on);
    sessionGallery.setActions(currentActions());
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
        { label: "Delete", danger: true, onClick: () => {
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
      { label: "Delete", danger: true, onClick: () => window.deleteSession(name) },
    ]);
  }

  let playDirection = 1;
  function playStep() {
    const count = sessionGallery.items.length;
    if (count < 2 || !reverseLoop.checked) return sessionGallery.next();
    let i = sessionGallery.index + playDirection;
    if (i >= count) { playDirection = -1; i = count - 2; }
    else if (i < 0) { playDirection = 1; i = 1; }
    sessionGallery.select(i);
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
    playDirection = 1;
    playTimer = setInterval(playStep, playInterval());
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
    if (e.key === " " && !order.open && (isFullscreen() || playTimer)) { e.preventDefault(); togglePlay(); }
    if (order.open) return;
    // Sessão: Esc sai da reordenação ou da tela cheia; F tela cheia; Delete apaga a foto
    if (e.key === "Escape") {
      if (editing.on) stopReorder(false);
      else if (isFullscreen()) setFullscreen(false);
    }
    if ((e.key === "f" || e.key === "F") && !editing.on) toggleFullscreen();
    if ((e.key === "Delete" || e.key === "Backspace") && !editing.on && isFullscreen()) { e.preventDefault(); deleteCurrentPhoto(); }
  });

  // =================== Salvar / sair ===================
  // "Save sessions" pergunta Photos ou Sequence. Depois, uma janela com todas as
  // sessões: cada uma tem a sua configuração (formato, ou formatos, velocidade,
  // loops...), que pode ser copiada para todas. As sessões não marcadas podem ser
  // apagadas, só depois que as marcadas forem salvas.
  const FPS_STEPS = [2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 24, 30];
  const PHOTO_FORMATS = [["original", "Original"], ["jpg", "JPG"], ["png", "PNG"], ["webp", "WEBP"], ["tiff", "TIFF"], ["heic", "HEIC"]];
  const SEQ_FORMATS = ["mp4", "mov", "gif", "webp", "avif"];
  const SIZES = [[720, "720p"], [1080, "1080p"], [2160, "4K"]];
  const exp = { mode: "photos", exitAfter: false, selected: new Set(), active: "", settings: {}, heic: { ok: true, reason: "" },
                timer: null, frame: 0, photo: 0 };

  function defaultSettings() {
    return { naming: { kind: "date", text: "", date: true, position: "after" },
             photos: { format: "original" },
             seq: { formats: ["mp4"], fps: 6, loops: 3, reverse: true, height: 1080, alsoPhotos: true } };
  }
  function settingsFor(name) {
    if (!exp.settings[name]) exp.settings[name] = defaultSettings();
    return exp.settings[name];
  }
  // Mesma conta do app (Export::CycleOrder): 1..n e, com reverse, volta até 2
  function cycleOrder(n, reverse) {
    const order = [];
    for (let i = 0; i < n; i++) order.push(i);
    if (reverse) for (let i = n - 2; i >= 1; i--) order.push(i);
    return order;
  }
  // Data e hora da sessão como fica no nome da pasta: "2026-10-14 16.31.00"
  function dateName(session) {
    const m = /^(\d{4}-\d{2}-\d{2})_(\d{2})-(\d{2})-(\d{2})/.exec(session);
    return m ? m[1] + " " + m[2] + "." + m[3] + "." + m[4] : session;
  }
  function cleanName(text) { return text.replace(/[\/\\:*?"<>|\x00-\x1f]/g, "-").trim().replace(/[ .]+$/, ""); }
  // Nome previsto da pasta (os números finais dependem da pasta escolhida)
  function folderName(session) {
    const n = settingsFor(session.name).naming;
    const text = cleanName(n.text || "");
    if (n.kind === "number") return "01, 02…";
    if (n.kind === "custom" && text) {
      if (n.date) return n.position === "before" ? dateName(session.name) + " " + text : text + " " + dateName(session.name);
      const same = sessions.filter((o) => exp.selected.has(o.name) && settingsFor(o.name).naming.kind === "custom" &&
        !settingsFor(o.name).naming.date && cleanName(settingsFor(o.name).naming.text || "") === text).length;
      return same > 1 ? text + " 01, 02…" : text;
    }
    return dateName(session.name);
  }
  function seconds(v) { return (Math.round(v * 10) / 10).toFixed(1) + " s"; }
  function seqTotal(session, s) {
    const cycle = cycleOrder(sessionItems(session).length, s.reverse).length;
    return { cycle: cycle, frames: cycle * s.loops, seconds: cycle * s.loops / s.fps };
  }
  function summaryText(session) {
    return folderName(session) + " · " + settingsText(session);
  }
  function settingsText(session) {
    const st = settingsFor(session.name);
    if (exp.mode === "photos") return "→ " + PHOTO_FORMATS.find((f) => f[0] === st.photos.format)[1];
    const s = st.seq;
    if (!s.formats.length) return "No format chosen";
    return s.formats.map((f) => f.toUpperCase()).join(" · ") + " · " + s.fps + " fps · " + plural(s.loops, "loop", "loops") +
      " · " + seconds(seqTotal(session, s).seconds);
  }

  function startSave(exitAfter) {
    if (!sessions.length) return;
    exp.exitAfter = exitAfter;
    $("modeText").textContent = "How do you want to save " + (sessions.length === 1 ? "the session" : "the " + sessions.length + " sessions") + "?";
    $("modeOverlay").classList.remove("hidden");
  }

  function openExport(mode) {
    $("modeOverlay").classList.add("hidden");
    exp.mode = mode;
    exp.selected = new Set(sessions.map((s) => s.name));
    Object.keys(exp.settings).forEach((k) => { if (!sessions.some((s) => s.name === k)) delete exp.settings[k]; });
    if (!sessions.some((s) => s.name === exp.active)) exp.active = (currentSession && currentSession.name) || sessions[0].name;
    exp.photo = 0;
    Promise.resolve(window.exportFormats && window.exportFormats()).then((f) => {
      if (f) exp.heic = { ok: f.heic, reason: f.heicReason || "" };
      renderExport();
    });
    renderExport();
    $("exportOverlay").classList.remove("hidden");
  }

  function closeExport() {
    stopPreview();
    $("exportOverlay").classList.add("hidden");
  }

  function renderExport() {
    const seq = exp.mode === "sequence";
    $("exportTitle").textContent = seq ? "Save as sequence" : "Save as photos";
    $("exportSub").textContent = seq ? "Each session becomes a video or animated image. Click a session to see and change its settings."
                                     : "Click a session to see its photos and change its format.";
    $("exportModePhotos").classList.toggle("on", !seq);
    $("exportModeSequence").classList.toggle("on", seq);
    renderExportList();
    renderExportSettings();
    updateExportCount();
  }

  function renderExportList() {
    const list = $("exportList");
    list.innerHTML = "";
    sessions.forEach((s) => {
      const label = sessionLabel(s.name);
      const li = document.createElement("li");
      li.className = "export-item" + (s.name === exp.active ? " active" : "") + (exp.selected.has(s.name) ? "" : " off");
      li.innerHTML = '<label class="export-check"><input type="checkbox"><span class="save-check" aria-hidden="true"></span></label>' +
        '<span class="export-thumb"></span><span><span class="folder-date"></span><span class="folder-meta"></span>' +
        '<span class="export-summary"></span></span>';
      const box = li.querySelector("input");
      box.checked = exp.selected.has(s.name);
      box.addEventListener("change", () => {
        if (box.checked) exp.selected.add(s.name); else exp.selected.delete(s.name);
        li.classList.toggle("off", !box.checked);
        updateExportCount();
      });
      li.querySelector(".export-check").addEventListener("click", (e) => e.stopPropagation());
      li.querySelector(".folder-date").textContent = label.date;
      li.querySelector(".folder-meta").textContent = [plural(sessionItems(s).length, "photo", "photos"), label.detail].filter(Boolean).join(" · ");
      li.querySelector(".export-summary").textContent = summaryText(s);
      const first = sessionItems(s).find((i) => i.previewable);
      if (first) sessionPhoto(s.name, first.file, 320).then((url) => { if (url) li.querySelector(".export-thumb").style.backgroundImage = "url(" + url + ")"; });
      li.addEventListener("click", () => {
        exp.active = s.name;
        exp.photo = 0;
        renderExportList();
        renderExportSettings();
      });
      list.appendChild(li);
    });
  }

  function updateExportCount() {
    const chosen = exp.selected.size;
    const seqEmpty = exp.mode === "sequence" && sessions.some((s) => exp.selected.has(s.name) && !settingsFor(s.name).seq.formats.length);
    $("exportCount").textContent = seqEmpty ? "Choose at least one format for each selected session"
      : chosen + " of " + plural(sessions.length, "session", "sessions") + " selected";
    $("exportConfirm").disabled = chosen === 0 || seqEmpty;
    $("exportConfirm").textContent = chosen ? "Save " + plural(chosen, "session", "sessions") : "Save";
    $("exportSelectAll").disabled = chosen === sessions.length;
    $("exportUnselectAll").disabled = chosen === 0;
  }

  function refreshSummaries() {
    [...$("exportList").children].forEach((li, i) => {
      if (sessions[i]) li.querySelector(".export-summary").textContent = summaryText(sessions[i]);
    });
    updateExportCount();
  }

  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }
  function row(label) {
    const r = el("div", "export-row");
    if (label) r.appendChild(el("span", "export-label", label));
    return r;
  }
  function segmented(options, current, onPick, disabledReason) {
    const box = el("span", "segmented");
    options.forEach(([value, label]) => {
      const b = el("button", value === current ? "on" : "", label);
      const reason = disabledReason && disabledReason(value);
      if (reason) { b.disabled = true; b.title = reason; }
      b.addEventListener("click", () => onPick(value));
      box.appendChild(b);
    });
    return box;
  }
  function stepper(text, sub, onMinus, onPlus, minDisabled, maxDisabled) {
    const box = el("span", "stepper");
    const minus = button("", onMinus, { icon: "minus", title: "Less" });
    const plus = button("", onPlus, { icon: "plus", title: "More" });
    minus.disabled = minDisabled;
    plus.disabled = maxDisabled;
    const value = el("span", "value", text);
    if (sub) value.appendChild(el("small", "", sub));
    box.append(minus, value, plus);
    return box;
  }
  function checkOption(label, checked, onChange) {
    const l = el("label", "log-option");
    const c = document.createElement("input");
    c.type = "checkbox";
    c.checked = checked;
    c.addEventListener("change", () => onChange(c.checked));
    l.append(c, document.createTextNode(" " + label));
    return l;
  }

  function stopPreview() {
    clearInterval(exp.timer);
    exp.timer = null;
  }

  function renderExportSettings() {
    stopPreview();
    const box = $("exportSettings");
    box.innerHTML = "";
    const session = sessions.find((s) => s.name === exp.active);
    if (!session) return;
    const items = sessionItems(session).filter((i) => i.previewable);
    const st = settingsFor(session.name);
    const label = sessionLabel(session.name).date;
    const rerender = () => { renderExportSettings(); refreshSummaries(); };

    const preview = el("div", "export-preview");
    const img = document.createElement("img");
    img.alt = "";
    const caption = el("div", "stage-caption");
    preview.append(img, caption);
    box.appendChild(preview);
    const show = (i, text, file) => {
      const item = items[i];
      if (!item) return;
      sessionPhoto(session.name, item.file, 1600).then((url) => { if (url && exp.active === session.name) img.src = url; });
      caption.innerHTML = "";
      caption.append(el("b", "", text[0]), document.createTextNode(text[1]));
      if (file) caption.appendChild(el("span", "caption-file", file));
    };

    if (exp.mode === "photos") {
      const s = st.photos;
      const strip = el("ul", "export-strip");
      const showPhoto = () => {
        const item = items[exp.photo];
        if (!item) return;
        const stem = item.file.replace(/\.[^.]+$/, "");
        const target = s.format === "original" ? "" : " → " + stem + "." + s.format;
        show(exp.photo, [pad(item.order), item.title + (item.subtitle ? " · " + item.subtitle : "")], item.file + target);
        [...strip.children].forEach((li, k) => li.classList.toggle("selected", k === exp.photo));
      };
      items.forEach((item, k) => {
        const li = el("li", "frame");
        const thumb = el("div", "frame-img");
        thumb.appendChild(el("span", "frame-badge", pad(item.order)));
        li.appendChild(thumb);
        sessionPhoto(session.name, item.file, 320).then((url) => { if (url) thumb.style.backgroundImage = "url(" + url + ")"; });
        li.addEventListener("click", () => { exp.photo = k; showPhoto(); });
        strip.appendChild(li);
      });
      box.appendChild(strip);
      showPhoto();
      const r = row("Format");
      r.appendChild(segmented(PHOTO_FORMATS, s.format, (v) => { s.format = v; rerender(); },
        (v) => (v === "heic" && !exp.heic.ok ? exp.heic.reason || "Not available on this computer" : "")));
      box.appendChild(r);
      box.appendChild(el("div", "export-note",
        "Original keeps each file exactly as the camera saved it (JPG and RAW). The other formats convert every photo; " +
        "when a camera saved RAW + JPG, the JPG is used."));
    } else {
      const s = st.seq;
      const order = cycleOrder(items.length, s.reverse);
      const play = button("", () => { if (exp.timer) stopPreview(); else startPreview(); setPlayIcon(); }, { icon: "pause", title: "Play / pause", kind: "btn-overlay" });
      play.classList.add("preview-play");
      preview.appendChild(play);
      const setPlayIcon = () => { play.innerHTML = icon(exp.timer ? "pause" : "play"); };
      const step = () => {
        if (!order.length) return;
        exp.frame = (exp.frame + 1) % order.length;
        const i = order[exp.frame];
        show(i, ["Preview", " " + label + " · photo " + (i + 1) + " of " + items.length + " · " + s.fps + " fps"]);
      };
      const startPreview = () => { stopPreview(); exp.timer = setInterval(step, 1000 / s.fps); };
      exp.frame = -1;
      step();
      startPreview();
      setPlayIcon();
      // Carrega todas as prévias antes, para tocar sem falhas
      items.forEach((item) => sessionPhoto(session.name, item.file, 1600));

      const formats = row("Formats");
      const chips = el("span", "chips");
      SEQ_FORMATS.forEach((f) => {
        const c = el("button", "chip" + (s.formats.includes(f) ? " on" : ""), f.toUpperCase());
        c.addEventListener("click", () => {
          s.formats = s.formats.includes(f) ? s.formats.filter((x) => x !== f) : SEQ_FORMATS.filter((x) => x === f || s.formats.includes(x));
          rerender();
        });
        chips.appendChild(c);
      });
      formats.appendChild(chips);
      box.appendChild(formats);

      const k = FPS_STEPS.indexOf(s.fps);
      const speed = row("Speed");
      speed.appendChild(stepper(s.fps + " fps", (1 / s.fps).toFixed(2) + " s per photo",
        () => { s.fps = FPS_STEPS[Math.max(0, k - 1)]; rerender(); },
        () => { s.fps = FPS_STEPS[Math.min(FPS_STEPS.length - 1, k + 1)]; rerender(); },
        k <= 0, k >= FPS_STEPS.length - 1));
      const loopsLabel = el("span", "export-label", "Loops");
      loopsLabel.style.minWidth = "auto";
      speed.appendChild(loopsLabel);
      speed.appendChild(stepper(String(s.loops), "",
        () => { s.loops = Math.max(1, s.loops - 1); rerender(); },
        () => { s.loops = Math.min(20, s.loops + 1); rerender(); }, s.loops <= 1, s.loops >= 20));
      speed.appendChild(checkOption("Reverse loop", s.reverse, (v) => { s.reverse = v; rerender(); }));
      box.appendChild(speed);

      const size = row("Size");
      size.appendChild(segmented(SIZES, s.height, (v) => { s.height = v; rerender(); }));
      size.appendChild(el("span", "export-note", "4K only for MP4 and MOV. GIF, WEBP and AVIF go up to 1080p."));
      box.appendChild(size);

      const t = seqTotal(session, s);
      const total = el("div", "export-total");
      const path = order.length > 1 ? (s.reverse && items.length > 2 ? "1 → " + items.length + " → 2" : "1 → " + items.length) : "1";
      total.append(el("span", "", "1 loop: " + path + " · " + plural(t.cycle, "frame", "frames")),
                   el("span", "", "× " + plural(s.loops, "loop", "loops")), el("span", "spacer"));
      const sum = el("span", "", "Total ");
      sum.appendChild(el("b", "", seconds(t.seconds)));
      total.appendChild(sum);
      box.appendChild(total);
      box.appendChild(el("div", "export-note", "MP4 and MOV have the loops inside the video. GIF, WEBP and AVIF repeat without stopping."));
      box.appendChild(checkOption("Also save the photos (original format)", s.alsoPhotos, (v) => { s.alsoPhotos = v; }));
    }

    // Nome da pasta e dos arquivos
    const n = st.naming;
    const nameRow = row("Name");
    nameRow.appendChild(segmented([["date", "Date + time"], ["number", "Number"], ["custom", "Custom"]], n.kind,
      (v) => { n.kind = v; rerender(); }));
    box.appendChild(nameRow);
    if (n.kind === "custom") {
      const custom = row("");
      custom.style.paddingLeft = "84px";
      const input = document.createElement("input");
      input.className = "export-name";
      input.placeholder = "Name, e.g. Studio test";
      input.maxLength = 80;
      input.value = n.text;
      input.addEventListener("input", () => { n.text = input.value.replace(/\|/g, ""); folderLine.textContent = "Folder: " + folderName(session); refreshSummaries(); });
      custom.appendChild(input);
      custom.appendChild(checkOption("Add date + time", n.date, (v) => { n.date = v; rerender(); }));
      if (n.date) custom.appendChild(segmented([["before", "Before"], ["after", "After"]], n.position, (v) => { n.position = v; rerender(); }));
      box.appendChild(custom);
    }
    const folderLine = el("div", "export-note", "Folder: " + folderName(session));
    box.appendChild(folderLine);
    if (n.kind === "number" || (n.kind === "custom" && !n.date))
      box.appendChild(el("div", "export-note", n.kind === "number"
        ? "Numbers continue from the ones already in the folder you choose."
        : "Sessions with the same name get numbers (01, 02…), continuing from the folder you choose."));

    const apply = el("div", "export-apply");
    const key = exp.mode === "photos" ? "photos" : "seq";
    const same = sessions.length > 1 && sessions.every((o) => JSON.stringify(settingsFor(o.name)[key]) === JSON.stringify(st[key]) &&
      JSON.stringify(settingsFor(o.name).naming) === JSON.stringify(st.naming));
    const note = el("span", "export-note", sessions.length === 1 ? "" : same
      ? (exp.mode === "photos" ? "All sessions use this format." : "All sessions use these settings.")
      : (exp.mode === "photos" ? "This format is" : "These settings are") + " only for " + label + ".");
    apply.appendChild(note);
    apply.appendChild(button("Apply to all sessions", () => {
      const source = JSON.stringify(st[key]);
      const naming = JSON.stringify(st.naming);
      sessions.forEach((other) => {
        if (other.name === session.name) return;
        settingsFor(other.name)[key] = JSON.parse(source);
        settingsFor(other.name).naming = JSON.parse(naming);
      });
      rerender();
      toast("Applied to all " + plural(sessions.length, "session", "sessions"));
    }));
    box.appendChild(apply);
  }

  function exportSpec(name) {
    const st = settingsFor(name);
    const n = st.naming;
    const head = [name, n.kind, (n.text || "").replace(/\|/g, ""), n.date ? 1 : 0, n.position];
    if (exp.mode === "photos") return [...head, "photos", st.photos.format].join("|");
    const s = st.seq;
    return [...head, "sequence", s.formats.join(","), s.fps, s.loops, s.reverse ? 1 : 0, s.height, s.alsoPhotos ? 1 : 0].join("|");
  }

  function runExport(deleteRest) {
    const names = sessions.filter((s) => exp.selected.has(s.name)).map((s) => s.name);
    exp.waiting = true;  // a janela fica aberta até salvar de fato
    window.exportSessions(exp.exitAfter ? "exit" : "", deleteRest ? "delete" : "", ...names.map(exportSpec));
  }

  function confirmExport() {
    const chosen = exp.selected.size;
    if (!chosen) return;
    const left = sessions.length - chosen;
    if (!left) return runExport(false);
    dialog("Delete the sessions you did not select?",
      plural(left, "session was", "sessions were") + " not selected. You can delete " + (left === 1 ? "it" : "them") +
      " after the others are saved, or keep " + (left === 1 ? "it" : "them") + " in the app to save later.", [
      { label: "Back" },
      { label: "Keep " + (left === 1 ? "it" : "them"), onClick: () => runExport(false) },
      { label: "Delete " + (left === 1 ? "it" : "them"), danger: true, onClick: () => runExport(true) },
    ]);
  }

  ui.saveBtn.addEventListener("click", () => startSave(false));
  $("modePhotos").addEventListener("click", () => openExport("photos"));
  $("modeSequence").addEventListener("click", () => openExport("sequence"));
  $("modeCancel").addEventListener("click", () => $("modeOverlay").classList.add("hidden"));
  $("exportModePhotos").addEventListener("click", () => { exp.mode = "photos"; renderExport(); });
  $("exportModeSequence").addEventListener("click", () => { exp.mode = "sequence"; renderExport(); });
  $("exportSelectAll").addEventListener("click", () => { exp.selected = new Set(sessions.map((s) => s.name)); renderExportList(); updateExportCount(); });
  $("exportUnselectAll").addEventListener("click", () => { exp.selected.clear(); renderExportList(); updateExportCount(); });
  $("exportCancel").addEventListener("click", closeExport);
  $("exportConfirm").addEventListener("click", confirmExport);
  $("progressCancel").addEventListener("click", () => {
    $("progressCancel").disabled = true;
    $("progressStep").textContent = "Cancelling…";
    window.cancelExport();
  });

  function showProgress(e) {
    const overlay = $("progressOverlay");
    if (overlay.classList.contains("hidden")) {
      overlay.classList.remove("hidden");
      $("progressCancel").disabled = false;
    }
    const total = e.total || 1;
    $("progressTitle").textContent = "Saving " + plural(total, "session", "sessions");
    const stepNames = { photos: "reading the photos", "photos-save": "saving the photos", done: "done" };
    const step = stepNames[e.step] || (/^(MP4|MOV|GIF|WEBP|AVIF)$/.test(e.step) ? "making the " + e.step : "converting to " + String(e.step).toUpperCase());
    $("progressStep").textContent = sessionLabel(e.session).date + " · " + step;
    const pct = Math.round(((e.index + Math.min(1, e.fraction || 0)) / total) * 100);
    $("progressBar").style.width = pct + "%";
    $("progressSession").textContent = "Session " + (e.index + 1) + " of " + total;
    $("progressPercent").textContent = pct + "%";
  }

  function confirmExit(count) {
    // Salvando: continuar e fechar no fim, ou parar e fechar
    if (!$("progressOverlay").classList.contains("hidden")) {
      dialog("Saving in progress",
        "Stillpoint is saving your sessions. You can close when it finishes, or stop now: the sessions that were not saved yet stay in the app for next time.", [
        { label: "Stop and close", danger: true, onClick: () => { exp.closeWhenDone = "stop"; window.cancelExport(); } },
        { label: "Close when done", primary: true, onClick: () => { exp.closeWhenDone = "wait"; toast("Stillpoint will close when saving finishes"); } },
      ]);
      return;
    }
    // Janela de salvar aberta: voltar para ela ou apagar e fechar
    const exportOpen = !$("exportOverlay").classList.contains("hidden");
    const modeOpen = !$("modeOverlay").classList.contains("hidden");
    if (exportOpen || modeOpen) {
      $("exportOverlay").classList.add("hidden");
      $("modeOverlay").classList.add("hidden");
      stopPreview();
      const back = () => {
        if (exportOpen) { $("exportOverlay").classList.remove("hidden"); renderExportSettings(); }
        else $("modeOverlay").classList.remove("hidden");
      };
      dialog("Save your sessions before closing?",
        plural(count, "session is", "sessions are") + " not saved yet. If you close without saving, the photos are deleted.", [
        { label: "Delete and close", danger: true, onClick: () => window.exitApp(true) },
        { label: "Continue saving", primary: true, onClick: back },
      ]);
      return;
    }
    dialog(
      "Save your sessions before closing?",
      plural(count, "session is", "sessions are") + " not saved yet. If you close without saving, the photos are deleted.",
      [
        { label: "Cancel" },
        { label: "Delete and close", danger: true, onClick: () => window.exitApp(true) },
        { label: "Save and close", primary: true, onClick: () => startSave(true) },
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
          { label: "Delete", danger: true, onClick: () => window.deleteAllSessions() },
        ]) },
        { label: "Later" },
        { label: "Save", primary: true, onClick: () => startSave(false) },
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
      activity.busyText = "";
      renderActivity();
      if (busy) {
        const labels = { restart: "Restarting connections", shoot: "Shooting", identify: "Identifying", reorder: "Saving order", orderPhotos: "Taking photos" };
        setStatus(labels[e.action] || "Working", "busy");
        if (e.action === "restart") log("Camera connections restarted");
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
      // Câmera que só o USB mostra há mais de 1 minuto: o SDK não a encontrou. Não
      // bloqueia o disparo; deixa de aparecer como "conectando" e pede para religá-la.
      cameras.forEach((c) => {
        if (c.starting && c.seconds >= SLOW_CONNECT_SECONDS) c.state = "stuck";
      });
      renderCameras();
      readyStatus();
      refreshSessionTitles();

      renderActivity();
    },

    closing() {
      setStatus("Closing", "busy");
      activity.closing = true;
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
      activity.busyText = e.total > 1 ? "Shooting session " + e.index + " of " + e.total : "Shooting";
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
      activity.busyText = "Receiving photos";
      if (orderCapture()) return orderGallery.message("Receiving photos");
      ui.progress.textContent = "Receiving photos";
    },
    downloadProgress(e) {
      const text = "Receiving photos, " + e.done + " of " + e.total;
      activity.busyText = text;
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

    // Atualização
    updateProgress(e) {
      if (upd.state !== "downloading") return;
      const total = e.total || (upd.release.asset && upd.release.asset.size) || 0;
      const pct = total ? Math.min(100, Math.round((e.done * 100) / total)) : 0;
      updEl.bar.style.width = pct + "%";
      updEl.bytes.textContent = mb(e.done) + " of " + mb(total) + " MB";
      updEl.percent.textContent = pct + "%";
    },
    updateReady() {
      upd.state = "installing";
      renderUpdate();
      Promise.resolve(window.installUpdate()).then((res) => {
        if (res && res.ok) return;
        upd.state = "failed";
        upd.error = (res && res.error) || "The update could not be installed.";
        renderUpdate();
      });
    },
    updateFailed(e) {
      upd.state = e.cancelled ? restingState() : "failed";
      upd.error = e.cancelled ? "" : e.error;
      if (!e.cancelled) log("The update did not finish.", "error", e.error);
      renderUpdate();
    },

    // Fotos guardadas
    sessions(e) { setSessions(e.sessions); },
    saving() { activity.saving = true; renderActivity(); },
    exportProgress(e) { showProgress(e); },
    saveCancelled() {
      if (exp.waiting) exp.waiting = false;  // a janela continua aberta como estava
      else toast("Save cancelled");
    },
    exportConflict(e) {
      const labels = e.names.map((n) => sessionLabel(n).date);
      const one = e.names.length === 1;
      dialog(one ? "This session was already saved here" : "Some sessions were already saved here",
        "The folder " + e.folder + " already has " + (one ? "a folder" : "folders") + " for " + labels.join(", ") + ". " +
        "Keep both saves a new folder with (2) in the name. Replace saves inside the existing " + (one ? "folder" : "folders") +
        ", over the files with the same name.", [
        { label: "Cancel", onClick: () => window.resolveExportConflict("cancel") },
        { label: "Keep both", onClick: () => window.resolveExportConflict("both") },
        { label: "Replace", danger: true, onClick: () => window.resolveExportConflict("replace") },
      ]);
    },
    saved(e) {
      activity.saving = false;
      $("progressOverlay").classList.add("hidden");
      // Salvo: a janela de salvar fecha. Com erro, continua aberta com o que faltou
      exp.waiting = false;
      if (!e.error) closeExport();
      if (exp.closeWhenDone) {
        const how = exp.closeWhenDone;
        exp.closeWhenDone = "";
        if (how === "stop" || !e.error)
          setTimeout(() => { if (how === "stop" || !sessions.length) window.exitApp(false); else confirmExit(sessions.length); }, 500);
      }
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

  // Sem o menu do botão direito do navegador ("Reload" deixava a janela em branco);
  // nos campos de texto ele continua, para copiar e colar.
  document.addEventListener("contextmenu", (e) => {
    if (!e.target.closest("input, textarea")) e.preventDefault();
  });

  // =================== Atualização (versão nova no GitHub) ===================
  // Ao abrir, o app pergunta ao GitHub qual é a última versão. Se for mais nova,
  // aparece o botão "Update x.y.z" no topo; a janela mostra o que mudou e baixa,
  // confere e instala a versão nova (o app fecha e abre de novo sozinho).
  // var: updateButtons() pode rodar antes desta linha (fica undefined até aqui)
  var upd = {
    info: null,       // do uiReady: versão, sistema, se dá para atualizar pelo app
    release: null,    // { version, page, notes, asset }
    state: "offer",   // offer | manual | downloading | installing | failed
    error: "",
  };
  const updEl = {
    chip: $("updateChip"), chipText: $("updateChipText"), overlay: $("updateOverlay"), title: $("updateTitle"),
    versions: $("updateVersions"), notes: $("updateNotes"), progress: $("updateProgress"), bar: $("updateBar"),
    bytes: $("updateBytes"), percent: $("updatePercent"), hint: $("updateHint"), busyNote: $("updateBusy"),
    actions: $("updateActions"),
  };

  function versionParts(v) { return String(v || "").replace(/^v/i, "").split(".").map((n) => parseInt(n, 10) || 0); }
  function isNewer(a, b) {
    const x = versionParts(a), y = versionParts(b);
    for (let i = 0; i < Math.max(x.length, y.length); i++)
      if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) > (y[i] || 0);
    return false;
  }

  // Itens "- ..." logo abaixo do título "What's new" no texto da versão no GitHub
  function releaseNotes(body) {
    const items = [];
    let inside = false;
    for (const line of String(body || "").split(/\r?\n/)) {
      const t = line.trim();
      if (/^#+\s/.test(t) || /^\*\*[^*]+\*\*:?$/.test(t)) { inside = /what.?s new/i.test(t); continue; }
      if (inside && /^[-*]\s+/.test(t)) items.push(t.replace(/^[-*]\s+/, "").replace(/[*`]/g, "").trim());
      if (items.length >= 6) break;
    }
    return items.filter(Boolean);
  }

  const mb = (bytes) => (bytes / 1048576).toFixed(1);
  const restingState = () => (upd.info && upd.info.canSelfUpdate && upd.release && upd.release.asset ? "offer" : "manual");

  // Não atualiza no meio de um disparo nem enquanto uma câmera conecta
  function updateBlockedReason() {
    if (busy) return "Stillpoint is busy. Update when it finishes.";
    if (cameras.some((c) => c.state === "connecting" && !c.starting)) return "A camera is connecting. Update when it finishes.";
    return "";
  }

  function renderUpdate() {
    const r = upd.release;
    if (!r) return;
    const s = upd.state;
    updEl.title.textContent =
      s === "downloading" ? "Downloading the update"
      : s === "installing" ? "Installing the update"
      : s === "failed" ? "The update did not finish"
      : "Stillpoint " + r.version + " is available";

    const strong = (text) => { const b = document.createElement("b"); b.textContent = text; return b; };
    updEl.versions.innerHTML = "";
    if (s === "downloading" || s === "installing") updEl.versions.append("Stillpoint ", strong(r.version));
    else updEl.versions.append("You have ", strong(upd.info.version), "  →  new ", strong(r.version));

    updEl.notes.innerHTML = "";
    r.notes.forEach((n) => { const li = document.createElement("li"); li.textContent = n; updEl.notes.appendChild(li); });
    updEl.notes.classList.toggle("hidden", !((s === "offer" || s === "manual") && r.notes.length));
    updEl.progress.classList.toggle("hidden", s !== "downloading");

    updEl.hint.textContent =
      s === "offer" ? "Stillpoint closes, installs the update and opens again. Your sessions stay saved."
      : s === "manual" ? ((upd.info.selfUpdateReason ? upd.info.selfUpdateReason + " " : "") + "Download the new version from its release page.")
      : s === "installing" ? (upd.info.os === "windows"
          ? "Stillpoint closes now. Allow the installer if Windows asks; Stillpoint opens again when it finishes."
          : "Stillpoint closes now and opens again in a moment.")
      : s === "failed" ? upd.error
      : "";
    updEl.hint.classList.toggle("error", s === "failed");
    updEl.hint.classList.toggle("hidden", !updEl.hint.textContent);

    const blocked = s === "offer" || s === "failed" ? updateBlockedReason() : "";
    updEl.busyNote.textContent = blocked ? "● " + blocked : "";
    updEl.busyNote.classList.toggle("hidden", !blocked);

    updEl.actions.innerHTML = "";
    const add = (label, kind, onClick, disabled) => {
      const b = document.createElement("button");
      b.className = "btn " + kind;
      b.textContent = label;
      b.disabled = !!disabled;
      b.addEventListener("click", onClick);
      updEl.actions.appendChild(b);
    };
    const openPage = () => { window.openUrl(r.page); closeUpdate(); };
    if (s === "offer") {
      add("Later", "btn-secondary", closeUpdate);
      add("Update now", "btn-primary", startUpdate, !!blocked);
    } else if (s === "manual") {
      add("Later", "btn-secondary", closeUpdate);
      add("Open download page", "btn-primary", openPage);
    } else if (s === "downloading") {
      add("Cancel", "btn-secondary", () => window.cancelUpdate());
    } else if (s === "failed") {
      add("Open download page", "btn-secondary", openPage);
      if (r.asset && upd.info.canSelfUpdate) add("Try again", "btn-primary", startUpdate, !!blocked);
    }
  }

  function openUpdate() {
    if (!upd.release) return;
    renderUpdate();
    updEl.overlay.classList.remove("hidden");
  }
  function closeUpdate() {
    if (upd.state === "downloading" || upd.state === "installing") return;
    if (upd.state === "failed") upd.state = restingState();
    updEl.overlay.classList.add("hidden");
  }

  function startUpdate() {
    const a = upd.release.asset;
    if (!a || updateBlockedReason()) return renderUpdate();
    upd.state = "downloading";
    updEl.bar.style.width = "0%";
    updEl.bytes.textContent = "0.0 of " + mb(a.size) + " MB";
    updEl.percent.textContent = "0%";
    renderUpdate();
    log("Downloading Stillpoint " + upd.release.version);
    Promise.resolve(window.downloadUpdate(a.url, a.digest || "", a.name, String(a.size))).then((res) => {
      if (res && res.ok) return;
      upd.state = "failed";
      upd.error = "The download could not start. Try again.";
      renderUpdate();
    });
  }

  function checkForUpdate() {
    if (!upd.info || !upd.info.checkUpdates) return;
    Promise.resolve(window.checkForUpdate()).then((text) => {
      if (!text) return;  // sem internet ou GitHub fora: segue sem avisar
      let rel;
      try { rel = JSON.parse(text); } catch (e) { return; }
      if (!rel || rel.draft || rel.prerelease || !rel.tag_name) return;
      const version = rel.tag_name.replace(/^v/i, "");
      if (!isNewer(version, upd.info.version)) return;
      const suffix = upd.info.os === "windows" ? "-windows-x64-setup.exe" : "-macos-arm64.dmg";
      const asset = (rel.assets || []).find((x) => x.name && x.name.endsWith(suffix));
      upd.release = {
        version: version,
        page: rel.html_url,
        notes: releaseNotes(rel.body),
        asset: asset ? { url: asset.browser_download_url, size: asset.size, digest: asset.digest, name: asset.name } : null,
      };
      upd.state = restingState();
      updEl.chipText.textContent = "Update " + version;
      updEl.chip.classList.remove("hidden");
      log("Stillpoint " + version + " is available", "ok");
    });
  }
  updEl.chip.addEventListener("click", openUpdate);

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
  ui.restartAppBtn.addEventListener("click", restartApp);
  ui.techToggle.addEventListener("change", () => ui.logList.classList.toggle("show-tech", ui.techToggle.checked));

  // Copia a atividade que está na tela (com os detalhes técnicos, se ligados)
  function copyText(text) {
    const area = document.createElement("textarea");
    area.value = text;
    area.setAttribute("readonly", "");
    area.style.position = "fixed"; area.style.opacity = "0";
    document.body.appendChild(area);
    area.select();
    let ok = false;
    try { ok = document.execCommand("copy"); } catch (e) { ok = false; }
    area.remove();
    if (!ok && navigator.clipboard) return navigator.clipboard.writeText(text).then(() => true, () => false);
    return Promise.resolve(ok);
  }
  ui.copyLogBtn.addEventListener("click", () => {
    const lines = [...ui.logList.children]
      .filter((li) => li.offsetParent !== null)
      .map((li) => li.querySelector(".log-time").textContent + "  " + li.querySelector(".log-text").textContent);
    copyText(lines.join("\n")).then((ok) => toast(ok ? "Activity copied (" + plural(lines.length, "line", "lines") + ")" : "Could not copy the activity"));
  });

  updateButtons();
  renderCameras();
  readyStatus();
  // Ao fim da procura inicial, mostra o guia se nenhuma câmera apareceu
  setTimeout(() => { renderCameras(); readyStatus(); }, SEARCH_GRACE_MS + 100);
  Promise.resolve(window.uiReady()).then((info) => {
    refreshSessions();
    if (info && info.unsavedSessions > 0) offerRecovery(info.unsavedSessions);
    if (info && info.testRestart) { testRestart = true; ui.restartAppBtn.classList.remove("hidden"); }
    upd.info = info || null;
    if (info && info.previousVersion && isNewer(info.version, info.previousVersion))
      toast("Updated to Stillpoint " + info.version, 5000);
    checkForUpdate();
  });
})();
