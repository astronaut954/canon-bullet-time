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
  let shootCancelling = false;   // pediu para cancelar, esperando o núcleo parar
  let shootCancelled = false;    // o bullet time atual foi cancelado
  let camSettings = [];          // configurações de cada câmera (evento cameraSettings)
  const csPending = new Set();   // "câmera|chave" enviados e ainda sem resposta ("*" = todas)
  const csTest = { capturing: false, has: false };  // teste rápido do Camera settings
  let csPoll = 0;                // releitura a cada 3 s com a janela aberta
  let csRenderLater = false;     // chegou leitura nova com uma lista aberta: redesenha ao sair dela
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
    menu: '<path d="M2.5 4h9M2.5 7h9M2.5 10h9"/>',
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

  // Campos do disparo: só números inteiros, até 999 (o Mac deixava digitar letras).
  // Vazio ou abaixo do mínimo volta ao padrão ao sair do campo; setas ↑ ↓ somam 1.
  [[ui.countdown, 0, 0], [ui.sessions, 1, 1], [ui.downloadWait, 1, 10]].forEach(([input, min, fallback]) => {
    input.addEventListener("input", () => {
      const digits = input.value.replace(/\D/g, "").slice(0, 3);
      if (digits !== input.value) input.value = digits;
    });
    input.addEventListener("blur", () => { input.value = String(intValue(input, min, fallback)); });
    input.addEventListener("keydown", (e) => {
      if (e.key !== "ArrowUp" && e.key !== "ArrowDown") return;
      e.preventDefault();
      const value = intValue(input, min, min) + (e.key === "ArrowUp" ? 1 : -1);
      input.value = String(Math.min(999, Math.max(min, value)));
    });
  });

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
        opts.load(item, 320).then((url) => {
          if (!url) return;
          img.style.backgroundImage = "url(" + url + ")";
          // Miniatura com a proporção da foto (foto inteira, em pé ou deitada)
          if (opts.fitThumbs) {
            const probe = new Image();
            probe.onload = () => img.style.setProperty("--ar", probe.naturalWidth + " / " + probe.naturalHeight);
            probe.src = url;
          }
        });
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
    $("cameraCount").textContent = cameras.length;
    $("cameraCount").classList.toggle("hidden", cameras.length === 0);
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
    // Configurações: diferente das outras, ou só dá para ajustar na câmera
    const tag = state === "connected" ? cameraSettingsTag(cam.name) : null;
    if (tag) {
      const t = document.createElement("div");
      t.innerHTML = '<span class="cs-tag ' + tag.cls + '"></span>';
      t.firstChild.textContent = tag.text;
      if (tag.title) t.firstChild.title = tag.title;
      info.appendChild(t);
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
    li.dataset.name = cam.name || "";
    // Arrastar para mudar a ordem (só as câmeras que já têm posição)
    if (index >= 0) {
      li.dataset.placed = "1";
      li.classList.toggle("can-drag", !busy);
      li.addEventListener("pointerdown", (e) => camPointerDown(e, li));
    }
    return li;
  }

  // ----- Arrastar câmeras para reordenar -----
  let camDrag = null;
  function placedRows() { return [...ui.cameraList.querySelectorAll("li.camera[data-placed='1']")]; }
  function camPointerDown(e, li) {
    if (e.button !== 0 || busy || e.target.closest("button")) return;
    camDrag = { li, y: e.clientY, moved: false, scroll: ui.cameraList.scrollTop };
    li.setPointerCapture(e.pointerId);
    li.onpointermove = camPointerMove;
    li.onpointerup = (ev) => camEndDrag(camDrag && camDrag.moved ? camInsertion(ev.clientY) : null);
    li.onpointercancel = () => camEndDrag(null);
  }
  function camInsertion(y) {
    return placedRows().filter((li) => {
      if (li === camDrag.li) return false;
      const r = li.getBoundingClientRect();
      return r.top + r.height / 2 < y;
    }).length;
  }
  function camPointerMove(e) {
    const d = camDrag;
    if (!d) return;
    const dy = e.clientY - d.y;
    if (!d.moved && Math.abs(dy) < 6) return;
    d.moved = true;
    d.li.classList.add("dragging");
    // Rola a lista quando o arraste chega perto de cima ou de baixo
    const list = ui.cameraList, r = list.getBoundingClientRect();
    if (e.clientY < r.top + 30) list.scrollTop -= 10;
    if (e.clientY > r.bottom - 30) list.scrollTop += 10;
    d.li.style.transform = "translateY(" + (dy + list.scrollTop - d.scroll) + "px)";
    const others = placedRows().filter((li) => li !== d.li);
    const target = camInsertion(e.clientY);
    let marker = list.querySelector(".camera-drop");
    if (!marker) { marker = document.createElement("div"); marker.className = "camera-drop"; list.appendChild(marker); }
    const top = !others.length ? 0 : target < others.length ? others[target].offsetTop - 5
      : others[others.length - 1].offsetTop + others[others.length - 1].offsetHeight + 3;
    marker.style.top = top + "px";
  }
  function camEndDrag(target) {
    const d = camDrag;
    camDrag = null;
    if (!d) return;
    d.li.onpointermove = d.li.onpointerup = d.li.onpointercancel = null;
    d.li.classList.remove("dragging");
    d.li.style.transform = "";
    const marker = ui.cameraList.querySelector(".camera-drop");
    if (marker) marker.remove();
    if (target === null || busy) return;
    const names = placedRows().filter((li) => li !== d.li).map((li) => li.dataset.name);
    names.splice(target, 0, d.li.dataset.name);
    const before = placedRows().map((li) => li.dataset.name);
    if (names.join("|") === before.join("|")) return;
    window.setOrder(...names);
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
    // Durante o bullet time, o botão de disparo vira "Cancel bullet time"
    const shooting = busy && busyAction === "shoot";
    ui.shootBtn.textContent = shooting ? (shootCancelling ? "Cancelling…" : "Cancel bullet time") : "It's bullet time";
    ui.shootBtn.classList.toggle("btn-primary", !shooting);
    ui.shootBtn.classList.toggle("btn-danger", shooting);
    ui.shootBtn.disabled = shooting ? shootCancelling : (busy || connected === 0 || sdkBusy);
    ui.orderBtn.disabled = busy || connected === 0 || sdkBusy;
    $("camSettingsBtn").disabled = busy || connected === 0;
    // Fotografando: o botão do Test shot vira "Cancel"
    $("csTest").textContent = csTest.capturing ? (csTest.cancelling ? "Cancelling…" : "Cancel") : "Test shot";
    $("csTest").classList.toggle("btn-danger", csTest.capturing);
    $("csTest").disabled = csTest.capturing ? !!csTest.cancelling : (busy || connected === 0);
    document.querySelectorAll("#cameraList li[data-placed='1']").forEach((li) => li.classList.toggle("can-drag", !busy));
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

  // Etiqueta de sessão já salva (lista de sessões e janela Save sessions)
  function savedTag() {
    const t = document.createElement("span");
    t.className = "saved-tag";
    t.textContent = "✓ Saved";
    t.title = "Already saved. It stays in the app until you delete it.";
    return t;
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
      if (s.saved) {
        li.querySelector(".folder-meta").after(savedTag());
        li.title = "Already saved. It stays in the app until you delete it.";
      }
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
  // Teste rápido do Camera settings (fotos temporárias, as mesmas do "Reorder by photo")
  const csGallery = Gallery($("csGallery"), {
    draggable: false,
    fitThumbs: true,
    load: (item, size) => orderPhoto(item.session, item.file, size),
  });
  // Só a fileira de miniaturas (maiores); clicar numa abre a foto grande por cima da
  // janela, com ← → e Esc. "Hide photos" recolhe a fileira.
  let csZoom = false, csCollapsed = false;
  const csHideBtn = button("Hide photos", () => csSetCollapsed(!csCollapsed), { kind: "btn-link" });
  csGallery.setActions([csHideBtn]);
  csGallery.setStageTools([
    button("", () => csGallery.prev(), { icon: "left", title: "Previous camera" }),
    button("", () => csGallery.next(), { icon: "right", title: "Next camera" }),
    button("Close", () => csSetZoom(false), { title: "Close (Esc)" }),
  ]);
  function csSetZoom(on) {
    csZoom = !!on && csTest.has;
    $("csTestBox").classList.toggle("zoom", csZoom);
  }
  function csSetCollapsed(on) {
    csCollapsed = !!on;
    $("csTestBox").classList.toggle("collapsed", csCollapsed);
    csHideBtn.textContent = csCollapsed ? "Show photos" : "Hide photos";
    if (csCollapsed) csSetZoom(false);
  }
  $("csGallery").querySelector(".carousel").addEventListener("click", (e) => { if (e.target.closest(".frame")) csSetZoom(true); });
  $("csGallery").querySelector(".stage").addEventListener("click", (e) => { if (e.target === e.currentTarget) csSetZoom(false); });
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
  const QUALITIES = [["standard", "Standard"], ["high", "High"], ["maximum", "Maximum"]];
  const LOSSY_PHOTOS = ["jpg", "webp", "heic"];  // PNG e TIFF são sempre sem perda
  function qualityLabel(q) { return QUALITIES.find((x) => x[0] === q)[1]; }
  const exp = { mode: "photos", exitAfter: false, selected: new Set(), active: "", settings: {}, heic: { ok: true, reason: "" },
                timer: null, frame: 0, photo: 0, sameForAll: true };

  // Configuração da sessão aberta igual nas outras (só a parte do modo atual e o nome)
  function settingsKey() { return exp.mode === "photos" ? "photos" : "seq"; }
  function sameAsActive(name) {
    const a = settingsFor(exp.active), o = settingsFor(name), k = settingsKey();
    return JSON.stringify(a[k]) === JSON.stringify(o[k]) && JSON.stringify(a.naming) === JSON.stringify(o.naming);
  }
  // "Same settings for all sessions" ligado: o que muda na sessão aberta vale para todas
  function syncAll() {
    if (!exp.sameForAll || !exp.active) return;
    const st = settingsFor(exp.active);
    sessions.forEach((o) => {
      if (o.name === exp.active) return;
      const t = settingsFor(o.name);
      t.photos = JSON.parse(JSON.stringify(st.photos));
      t.seq = JSON.parse(JSON.stringify(st.seq));
      t.naming = JSON.parse(JSON.stringify(st.naming));
    });
  }

  function defaultSettings() {
    return { naming: { kind: "date", text: "", date: true, position: "after" },
             photos: { format: "original", quality: "high" },
             seq: { formats: ["mp4"], fps: 6, loops: 3, reverse: true, height: 1080, alsoPhotos: true, quality: "high" } };
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
    if (exp.mode === "photos")
      return "→ " + PHOTO_FORMATS.find((f) => f[0] === st.photos.format)[1] +
        (LOSSY_PHOTOS.includes(st.photos.format) ? " · " + qualityLabel(st.photos.quality) : "");
    const s = st.seq;
    if (!s.formats.length) return "No format chosen";
    return s.formats.map((f) => f.toUpperCase()).join(" · ") + " · " + s.fps + " fps · " + plural(s.loops, "loop", "loops") +
      " · " + seconds(seqTotal(session, s).seconds) + " · " + qualityLabel(s.quality);
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
    // Salvando para fechar: só as que ainda não foram salvas (as já salvas ficam marcadas fora)
    const unsaved = sessions.filter((s) => !s.saved);
    exp.selected = new Set((exp.exitAfter && unsaved.length ? unsaved : sessions).map((s) => s.name));
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
        '<span class="export-summary"></span><span class="export-diff">Different</span></span>';
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
      if (s.saved) li.querySelector(".folder-meta").after(savedTag());
      li.querySelector(".export-summary").textContent = summaryText(s);
      li.classList.toggle("different", !exp.sameForAll && s.name !== exp.active && !sameAsActive(s.name));
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
      if (!sessions[i]) return;
      li.querySelector(".export-summary").textContent = summaryText(sessions[i]);
      li.classList.toggle("different", !exp.sameForAll && sessions[i].name !== exp.active && !sameAsActive(sessions[i].name));
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
    const rerender = () => { syncAll(); renderExportSettings(); refreshSummaries(); };

    // Mesma configuração para todas (padrão) ou uma por sessão
    if (sessions.length > 1) {
      const scope = checkOption("Same settings for all sessions", exp.sameForAll, (v) => { exp.sameForAll = v; rerender(); });
      scope.classList.add("export-scope");
      box.appendChild(scope);
    }

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
      if (LOSSY_PHOTOS.includes(s.format)) {
        const q = row("Quality");
        q.appendChild(segmented(QUALITIES, s.quality, (v) => { s.quality = v; rerender(); }));
        box.appendChild(q);
        const max = { jpg: "JPG at the highest quality, with full color detail.", webp: "Lossless: every pixel is kept.",
                      heic: "HEIC at the highest quality." };
        box.appendChild(el("div", "export-note", s.quality === "standard" ? "Smaller files, good for sharing online."
          : s.quality === "high" ? "Very close to the original photos, with smaller files. Good for most uses."
          : max[s.format] + " Largest files."));
      } else if (s.format === "png" || s.format === "tiff") {
        box.appendChild(el("div", "export-note", s.format.toUpperCase() + " keeps every pixel: no quality is lost in the conversion."));
      }
      box.appendChild(el("div", "export-note",
        "Original keeps each file exactly as the camera saved it (JPG and RAW): the highest quality. The other formats convert " +
        "every photo; RAW photos are converted from the full-size image the camera keeps inside them, and when a camera " +
        "saved RAW + JPG, the JPG is used."));
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

      const quality = row("Quality");
      quality.appendChild(segmented(QUALITIES, s.quality, (v) => { s.quality = v; rerender(); }));
      box.appendChild(quality);
      box.appendChild(el("div", "export-note", (s.quality === "standard" ? "Smaller files, good for sharing online."
        : s.quality === "high" ? "Very close to the photos, with smaller files. Good for most uses."
        : "As close to the photos as each format allows: WEBP keeps every pixel, MP4, MOV and AVIF get much more detail. " +
          "Large files, and AVIF takes longer.") + (s.formats.includes("gif") ? " GIF always has 256 colors, at any quality." : "")));

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
      box.appendChild(checkOption("Also save the photos (original format)", s.alsoPhotos, (v) => { s.alsoPhotos = v; syncAll(); refreshSummaries(); }));
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
      input.addEventListener("input", () => { n.text = input.value.replace(/\|/g, ""); syncAll(); folderLine.textContent = "Folder: " + folderName(session); refreshSummaries(); });
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

    if (!exp.sameForAll && sessions.length > 1) {
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
    } else if (sessions.length > 1) {
      box.appendChild(el("div", "export-note export-apply", "Changes apply to all sessions. Turn off \"Same settings for all sessions\" to set each one."));
    }
  }

  function exportSpec(name) {
    const st = settingsFor(name);
    const n = st.naming;
    const head = [name, n.kind, (n.text || "").replace(/\|/g, ""), n.date ? 1 : 0, n.position];
    if (exp.mode === "photos") return [...head, "photos", st.photos.format, st.photos.quality].join("|");
    const s = st.seq;
    return [...head, "sequence", s.formats.join(","), s.fps, s.loops, s.reverse ? 1 : 0, s.height, s.alsoPhotos ? 1 : 0, s.quality].join("|");
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
  const orderCapture = () => (order.open && order.capturing) || csTest.capturing;
  const captureGallery = () => (csTest.capturing ? csGallery : orderGallery);

  const handlers = {
    busy(e) {
      busy = e.busy;
      busyAction = e.busy ? e.action : "";
      if (e.busy && e.action === "shoot") { shootCancelling = false; shootCancelled = false; }
      activity.busyText = "";
      renderActivity();
      if (busy) {
        const labels = { restart: "Restarting connections", shoot: "Shooting", identify: "Identifying", reorder: "Saving order", orderPhotos: "Taking photos" };
        setStatus(labels[e.action] || "Working", "busy");
        if (e.action === "restart") log("Camera connections restarted");
      } else {
        if (csTest.capturing) {
          // Teste do Camera settings terminou sem fotos (ex.: uma câmera ainda conectando)
          csTest.capturing = false;
          const wasCancelled = csTest.cancelling;
          csTest.cancelling = false;
          $("camSettingsOverlay").querySelector(".cs-sheet").classList.remove("capturing");
          csGallery.setHead(wasCancelled ? "Test shot cancelled" : "No photos", "");
          csGallery.message(wasCancelled ? "Cancelled. Nothing was kept." : "The photos could not be taken. Try again when all cameras are ready.");
          if (wasCancelled) window.discardOrderPhotos();
        } else if (orderCapture()) {
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
      $("countdownCancel").disabled = shootCancelling;
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
      if (orderCapture()) return captureGallery().message("Receiving photos");
      ui.progress.textContent = "Receiving photos";
    },
    downloadProgress(e) {
      const text = "Receiving photos, " + e.done + " of " + e.total;
      activity.busyText = text;
      if (orderCapture()) return captureGallery().message(text);
      ui.progress.textContent = text;
    },
    downloadTimeout(e) {
      log("Some photos did not arrive in time.", "error",
        e.lost.length ? "Lost connection: " + e.lost.map((n) => cameraLabel(n)).join(", ") + "." : "No camera was disconnected. Try a longer download wait.");
    },
    sessionInterrupted() { if (!orderCapture()) log("The session stopped before the end.", "error"); },
    sessionCompleted() {},
    sequenceCancelled() {
      ui.countdownOverlay.classList.add("hidden");
      if (orderCapture()) return;
      shootCancelled = true;
      log("Bullet time cancelled", "warn", "Photos that already arrived stay in the session.");
      ui.progress.textContent = "Cancelled.";
    },
    sequenceFinished(e) {
      if (orderCapture()) return;
      if (shootCancelled) return;  // já avisado em sequenceCancelled
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
    // Modo debug: Test shot simples guardado
    debugSaved(e) {
      log("Debug data saved", "ok", e.folder);
      toast("Debug data saved");
    },
    // Test shot com "Match automatically"
    autoMatchProgress(e) {
      csGallery.setHead("Test shot · matching", "Photo " + e.shot + " of up to " + e.shots + " · " + e.stage);
      csGallery.message(e.stage);
    },
    // Cartão cinza: depois da primeira foto, conferir onde está o cartão
    grayCardPick(e) { csCardShow(e.sessions, e.regions || []); },
    // Cartela de cores
    chartPick(e) { csChartShow(e.sessions, e.quads || []); },
    chartProgress(e) {
      csGallery.setHead("Chart calibration", "Photo " + e.shot + " of " + e.shots + " · " + e.stage);
      csGallery.message(e.stage);
    },
    chartResult(e) {
      csCardHide();
      const notes = e.notes || [];
      if (e.cancelled) {
        toast("Chart calibration cancelled. The cameras are back to how they were.");
        log("Chart calibration cancelled; camera settings restored", "warn");
        return;
      }
      const actions = [{ label: "OK", primary: true }];
      if (e.debugFolder) actions.unshift({ label: "Open debug folder", link: true, onClick: () => window.openDebugFolder() });
      if (!e.ok) {
        dialog("Chart calibration did not finish", [e.error || "Try again."].concat(notes).join("\n\n"), actions);
        log("Chart calibration did not finish", "warn", e.error);
        return;
      }
      const parts = [(e.models || []).join("\n\n"),
        "Saved. From now on, Match automatically gives each model these Contrast, Saturation and Color tone differences. Differences under half a step are not changed. All camera settings are back to how they were."];
      if (notes.length) parts.push(notes.join("\n"));
      dialog("Cameras calibrated", parts.join("\n\n"), actions);
      log("Chart calibration saved", "ok");
    },
    autoMatchResult(e) {
      csCardHide();
      const notes = e.notes || [];
      if (e.cancelled) {
        toast("Test shot cancelled. The cameras are back to how they were.");
        log("Match automatically cancelled; camera settings restored", "warn");
        return;
      }
      if (!e.matched) {
        if (!e.ok) toast(e.error || "The photos could not be taken.", 8000);
        if (notes.length) dialog("Not matched automatically", notes.join("\n"), [{ label: "OK", primary: true }]);
        return;
      }
      const changes = e.changes || [];
      const blocks = changes.map((c) => c.camera + "\n" + c.items.map(([what, from, to]) => "   " + what + ":  " + from + "  →  " + to).join("\n"));
      const parts = [];
      if (!e.ok) parts.push(e.error || "Try again.");
      parts.push(blocks.length ? blocks.join("\n\n") : (e.ok ? "The cameras already look alike. Nothing was changed." : "Nothing was changed."));
      if (notes.length) parts.push(notes.join("\n"));
      if (e.ok) parts.push("The photos show the result. Use ← → to compare.");
      const title = !e.ok ? "Matching did not finish" : blocks.length ? "Cameras matched" : "Nothing to change";
      const actions = [{ label: "OK", primary: true }];
      if (e.debugFolder) actions.unshift({ label: "Open debug folder", link: true, onClick: () => window.openDebugFolder() });
      dialog(title, parts.join("\n\n"), actions);
      if (e.debugFolder) log("Debug data saved", "tech", e.debugFolder);
      log("Match automatically: " + (blocks.length ? plural(blocks.length, "camera", "cameras") + " changed" : "no change"), e.ok ? "ok" : "warn");
    },
    orderPhotos(e) {
      csCardHide();
      if (csTest.capturing && csTest.cancelling) return;  // cancelado: as fotos não são mostradas
      if (csTest.capturing) showCsTest(e.sessions);
      else if (order.open) showOrderPhotos(e.sessions);
    },

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
        "Keep both saves a new folder with (2) in the name. Replace deletes everything inside the existing " +
        (one ? "folder" : "folders") + " and keeps only this new save.", [
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
      const saved = (e.sessions || []).filter((name) => sessions.some((s) => s.name === name));
      if (exp.closeWhenDone) {
        // Fechando o app: as salvas saem do app sem perguntar (já estão na pasta escolhida)
        const how = exp.closeWhenDone;
        exp.closeWhenDone = "";
        saved.forEach((name) => window.deleteSession(name));
        const left = sessions.length - saved.length;
        if (how === "stop" || !e.error)
          setTimeout(() => { if (how === "stop" || !left) window.exitApp(false); else confirmExit(left); }, 500);
        saved.length = 0;
      }
      if (e.error) {
        toast(e.error, 8000);
        log(e.error, "error");
      } else {
        const text = "Saved " + plural(e.count, "session", "sessions") + " to " + e.folder;
        toast(text);
        log(text, "ok");
      }
      // Salvas (sem fechar o app): descartar do app ou manter
      if (saved.length) {
        const one = saved.length === 1;
        dialog(one ? "Discard the saved session?" : "Discard the " + saved.length + " saved sessions?",
          (one ? "The session was saved to " : "The sessions were saved to ") + e.folder + ". Discard " + (one ? "it" : "them") +
          " from Stillpoint to free up space, or keep " + (one ? "it" : "them") + " in the app. The saved copies are not touched.", [
          { label: "Keep in app" },
          { label: "Discard from app", primary: true, onClick: () => {
            saved.forEach((name) => window.deleteSession(name));
            log("Discarded " + plural(saved.length, "saved session", "saved sessions") + " from the app", "tech");
          } },
        ]);
      }
    },
    confirmExit(e) { confirmExit(e.unsavedSessions); },

    log(e) {
      const text = plainText(e.text);
      if (text) log(text, "tech");
    },
    // Item escolhido no menu do Mac (barra de cima)
    menu(e) { menuAction(e.item); },

    // Configurações das câmeras (lidas quando as câmeras mudam ou depois de uma mudança)
    cameraSettings(e) {
      const next = e.cameras || [];
      const same = JSON.stringify(next) === JSON.stringify(camSettings);
      camSettings = next;
      if (same) return;  // releitura sem mudança: nada a redesenhar
      renderCameras();
      if (!$("camSettingsOverlay").classList.contains("hidden")) csRender();
    },
    cameraSettingResult(e) {
      const keys = e.key === "kelvin" || e.key === "wb" ? ["wb", "kelvin"] : [e.key];
      [...csPending].forEach((p) => {
        const [cam, key] = p.split("|");
        if (keys.includes(key) && (e.camera === "*" || cam === e.camera || cam === "*")) csPending.delete(p);
      });
      if (!e.ok) {
        toast(e.error || "A camera did not accept the change.", 8000);
        log("A camera setting was not changed.", "warn", e.error);
      }
      if (!$("camSettingsOverlay").classList.contains("hidden")) renderCamSettings();
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

  // manual: pedido pelo menu ("Check for updates"); responde mesmo sem versão nova
  function checkForUpdate(manual) {
    if (!upd.info || (!manual && !upd.info.checkUpdates)) return;
    // Já achou uma versão nova: mostra de novo. Se ela ainda não tinha o arquivo deste
    // sistema (publicado num segundo momento), procura outra vez.
    if (manual && upd.release && upd.release.asset) { openUpdate(); return; }
    if (manual) toast("Checking for updates…");
    const answer = (title, text) => {
      if (!manual) return;
      ui.toast.classList.add("hidden");
      dialog(title, text, [{ label: "OK", primary: true }]);
    };
    const failed = () => answer("Could not check for updates", "Stillpoint could not reach GitHub. Check the internet connection and try again.");
    Promise.resolve(window.checkForUpdate()).then((text) => {
      if (!text) return failed();  // sem internet ou GitHub fora: na abertura, segue sem avisar
      let rel;
      try { rel = JSON.parse(text); } catch (e) { return failed(); }
      if (!rel || rel.draft || rel.prerelease || !rel.tag_name) return failed();
      const version = rel.tag_name.replace(/^v/i, "");
      if (!isNewer(version, upd.info.version))
        return answer("Stillpoint is up to date", "You have the latest version, " + upd.info.version + ".");
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
      if (manual) { ui.toast.classList.add("hidden"); openUpdate(); }
    });
  }
  updEl.chip.addEventListener("click", openUpdate);

  // =================== Menu do app, About e Settings ===================
  // Mac: os itens vêm do menu do sistema, na barra de cima (evento "menu").
  // Windows: botão no canto da barra do app, com os mesmos itens.
  const REPO_PAGE = "https://github.com/astronaut954/stillpoint-bullet-time-photography";
  const menuEl = { btn: $("menuBtn"), list: $("appMenu") };
  menuEl.btn.innerHTML = icon("menu");

  function setMenuOpen(open) {
    menuEl.list.classList.toggle("hidden", !open);
    menuEl.btn.setAttribute("aria-expanded", open ? "true" : "false");
  }

  function menuAction(item) {
    setMenuOpen(false);
    const version = (upd.info && upd.info.version) || "";
    if (item === "about") openAbout();
    else if (item === "updates") checkForUpdate(true);
    else if (item === "settings") openSettings();
    else if (item === "help") window.openUrl(REPO_PAGE + "#how-to-use");
    else if (item === "cameras") window.openUrl(REPO_PAGE + "#compatibility");
    else if (item === "whatsnew") window.openUrl(REPO_PAGE + (version ? "/releases/tag/v" + version : "/releases"));
    else if (item === "report") window.openUrl(REPO_PAGE + "/issues/new");
  }

  menuEl.btn.addEventListener("click", (e) => {
    e.stopPropagation();
    setMenuOpen(menuEl.list.classList.contains("hidden"));
  });
  menuEl.list.addEventListener("click", (e) => {
    const b = e.target.closest("button[data-item]");
    if (b) menuAction(b.dataset.item);
  });
  document.addEventListener("click", (e) => { if (!e.target.closest(".app-menu-wrap")) setMenuOpen(false); });

  // About
  function openAbout() {
    $("aboutVersion").textContent = upd.info && upd.info.version ? "Version " + upd.info.version : "";
    $("aboutOverlay").classList.remove("hidden");
  }
  $("aboutClose").addEventListener("click", () => $("aboutOverlay").classList.add("hidden"));
  $("aboutLicenses").addEventListener("click", () => window.openUrl(REPO_PAGE + "/blob/main/THIRD_PARTY_NOTICES.md"));
  $("aboutGithub").addEventListener("click", () => window.openUrl(REPO_PAGE));

  // Settings: tema e idioma. Gravados na hora; valem ao abrir o app de novo.
  const prefs = { theme: "", language: "", restart: false, debug: false };
  function openSettings() {
    if (!upd.info) return;
    if (!prefs.theme) { prefs.theme = upd.info.theme; prefs.language = upd.info.language; prefs.debug = !!upd.info.debug; }
    renderSettings();
    refreshDebugSize();
    $("settingsOverlay").classList.remove("hidden");
  }
  function renderSettings() {
    const info = upd.info;
    const themes = (info.themes && info.themes.length) ? info.themes : [{ id: info.theme, name: info.theme }];
    const languages = (info.languages && info.languages.length) ? info.languages : [{ id: info.language, name: info.language }];
    $("settingsThemes").innerHTML = "";
    $("settingsThemes").appendChild(segmented(themes.map((t) => [t.id, t.name]), prefs.theme, (v) => saveSettings(v, prefs.language)));
    $("settingsLanguages").innerHTML = "";
    $("settingsLanguages").appendChild(segmented(languages.map((l) => [l.id, l.name]), prefs.language, (v) => saveSettings(prefs.theme, v)));
    $("settingsRestart").classList.toggle("hidden", !prefs.restart);
    // Modo debug: vale na hora
    $("settingsDebug").innerHTML = "";
    $("settingsDebug").appendChild(segmented([["off", "Off"], ["on", "On"]], prefs.debug ? "on" : "off", (v) => {
      // App compilado antes do modo debug existir (página nova com o programa antigo)
      if (typeof window.setDebugMode !== "function") { toast("This build has no debug mode yet. Build the app again.", 8000); return; }
      Promise.resolve(window.setDebugMode(v === "on")).then((r) => {
        if (!r || !r.ok) { toast("Could not save the settings"); return; }
        prefs.debug = !!r.debug;
        log("Debug mode " + (prefs.debug ? "on" : "off"), "tech");
        renderSettings();
        showDebugSize(r);
      });
    }));
  }
  $("settingsDebugFolder").addEventListener("click", () => window.openDebugFolder());
  // Espaço ocupado pelos dados de debug
  function sizeText(bytes) {
    if (bytes >= 1024 * 1024 * 1024) return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GB";
    if (bytes >= 1024 * 1024) return Math.round(bytes / (1024 * 1024)) + " MB";
    if (bytes >= 1024) return Math.round(bytes / 1024) + " KB";
    return bytes + " bytes";
  }
  function showDebugSize(r) {
    const bytes = r && typeof r.bytes === "number" ? r.bytes : null;
    $("settingsDebugSize").textContent = bytes === null ? "" : bytes > 0 ? "Debug data: " + sizeText(bytes) : "No debug data saved.";
    $("settingsDebugDelete").classList.toggle("hidden", !bytes);
  }
  function refreshDebugSize() {
    if (typeof window.debugInfo !== "function") { showDebugSize(null); return; }
    Promise.resolve(window.debugInfo()).then(showDebugSize);
  }
  $("settingsDebugDelete").addEventListener("click", () => {
    dialog("Delete debug data?", "Deletes the photos and reports saved by debug mode. Your sessions are not touched.", [
      { label: "Cancel" },
      { label: "Delete", danger: true, onClick: () => {
        Promise.resolve(window.deleteDebugData()).then((r) => {
          showDebugSize(r);
          if (r && r.bytes > 0) toast("Some debug files could not be deleted");
          else { toast("Debug data deleted"); log("Debug data deleted", "tech"); }
        });
      } },
    ]);
  });
  function saveSettings(theme, language) {
    if (theme === prefs.theme && language === prefs.language) return;
    Promise.resolve(window.saveSettings(theme, language)).then((r) => {
      if (!r || !r.ok) { toast("Could not save the settings"); return; }
      prefs.theme = theme;
      prefs.language = language;
      prefs.restart = !!r.restart;
      renderSettings();
    });
  }
  $("settingsDone").addEventListener("click", () => $("settingsOverlay").classList.add("hidden"));
  $("settingsRestartBtn").addEventListener("click", () => {
    const reason = updateBlockedReason();
    if (reason) { toast(reason.replace("Update", "Restart")); return; }
    restartApp();
  });

  // =================== Camera settings ===================
  // ISO, velocidade, abertura... de cada câmera, lidos da própria câmera e mudados na
  // hora. "All cameras" muda em todas as que dá para ajustar. Cada lista só tem os
  // valores que aquela câmera aceita agora.
  const CS_COLS = [["iso", "ISO"], ["tv", "Shutter"], ["av", "Aperture"], ["expcomp", "Exp. comp."], ["wb", "White balance"],
    ["shift", "WB fine-tune"], ["style", "Picture Style"], ["quality", "Quality"], ["focus", "Focus"]];
  // Disparo único (o app põe sozinho; não é escolha). Único silencioso também vale.
  const isSingleShot = (v) => v === 0x00 || v === 0x06 || v === 0x13;
  const KELVINS = [];
  for (let k = 2800; k <= 10000; k += 100) KELVINS.push(k);
  const shiftLabel = (v, neg, pos) => (v === 0 ? "0" : (v < 0 ? neg : pos) + Math.abs(v));

  // Valor para comparar entre câmeras (null = a câmera não tem)
  function csLabel(c, key) {
    if (key === "shift") return c.shiftAvailable ? c.shiftAB + "/" + c.shiftGM : null;
    const st = c.settings && c.settings[key];
    if (!st || !st.available) return null;
    if (key === "wb" && st.value === 9) return "K" + c.kelvin;
    return st.label;
  }
  // Valor mais comum de cada configuração entre as câmeras ajustáveis (empate: a primeira na ordem)
  function csReference() {
    const ref = {};
    const cams = camSettings.filter((c) => c.adjustable);
    CS_COLS.forEach(([k]) => {
      if (k === "focus" || k === "shift") return;  // ajuste fino: correção por câmera, diferente de propósito
      const counts = new Map();
      cams.forEach((c) => { const l = csLabel(c, k); if (l !== null) counts.set(l, (counts.get(l) || 0) + 1); });
      let best = null, n = 0;
      counts.forEach((v, l) => { if (v > n) { n = v; best = l; } });
      ref[k] = counts.size > 1 ? best : null;  // null: todas iguais (nada a marcar)
    });
    return ref;
  }
  // Ajustes do Picture Style (contraste, saturação, tom): diferente do mais comum entre as câmeras
  // Cada opção do ± (Contrast, Saturation, Color tone): o valor mais comum, ou null se é
  // igual em todas as câmeras
  // Calibração com a cartela: quantos passos este modelo anda de propósito em relação ao
  // modelo mais comum (essa diferença não conta como "diferente")
  function csChartAdjust(c, f) {
    if (!c.chart) return 0;
    const cams = camSettings.filter((x) => x.adjustable && x.styleDetail);
    const count = {};
    cams.forEach((x) => { count[x.model] = (count[x.model] || 0) + 1; });
    let common = null;
    cams.forEach((x) => { if (x.chart && (!common || count[x.model] > count[common.model])) common = x; });
    if (!common || common.model === c.model) return 0;
    return Math.round(c.chart[f] - common.chart[f]);
  }
  const csStyleValue = (c, f) => c[f] - csChartAdjust(c, f);
  function csStyleDetailRefs() {
    const cams = camSettings.filter((c) => c.adjustable && c.styleDetail);
    const out = {};
    STYLE_DETAILS.forEach(([, , f]) => {
      const counts = new Map();
      cams.forEach((c) => counts.set(csStyleValue(c, f), (counts.get(csStyleValue(c, f)) || 0) + 1));
      let best = null, n = 0;
      counts.forEach((v, k) => { if (v > n) { n = v; best = k; } });
      out[f] = counts.size > 1 ? best : null;
    });
    return out;
  }
  // "" = igual às outras; "partial" = só algumas opções do ± diferentes; "diff" = todas
  function csStyleDetailLevel(c) {
    if (!c.styleDetail) return "";
    const refs = csStyleDetailRefs();
    const n = STYLE_DETAILS.filter(([, , f]) => refs[f] !== null && csStyleValue(c, f) !== refs[f]).length;
    return n === 0 ? "" : n === STYLE_DETAILS.length ? "diff" : "partial";
  }
  const csStyleDetailDiffers = (c) => csStyleDetailLevel(c) !== "";
  const csDiffers = (c, k, ref) => ref[k] !== null && ref[k] !== undefined && csLabel(c, k) !== null && csLabel(c, k) !== ref[k];

  // Aviso no painel Cameras
  function cameraSettingsTag(name) {
    const c = camSettings.find((x) => x.camera === name);
    if (!c) return null;
    const drive = c.settings && c.settings.drive;
    if (drive && drive.available && !isSingleShot(drive.value))
      return { cls: "diff", text: "Not on single shot", title: "The camera is on " + drive.label + " and did not accept single shooting. Set it to single shooting on the camera." };
    if (!c.adjustable) return { cls: "locked", text: "Settings on the camera only", title: c.reason };
    const ref = csReference();
    if (CS_COLS.some(([k]) => k !== "focus" && csDiffers(c, k, ref)) || csStyleDetailDiffers(c)) return { cls: "diff", text: "Settings differ", title: "Open Camera settings to match the cameras." };
    return null;
  }

  function csSelect(options, current, onPick, extraClass) {
    const sel = document.createElement("select");
    sel.className = "cs-sel" + (extraClass ? " " + extraClass : "");
    options.forEach(([value, label, disabled]) => {
      const o = document.createElement("option");
      o.value = value;
      o.textContent = label;
      if (disabled) o.disabled = true;
      sel.appendChild(o);
    });
    sel.value = current;
    sel.addEventListener("change", () => onPick(sel.value));
    return sel;
  }
  function csText(text, cls, title) {
    const v = el("span", "cs-val" + (cls ? " " + cls : ""), text);
    if (title) v.title = title;
    return v;
  }
  function csSet(camera, key, value) {
    csPending.add(camera + "|" + key);
    renderCamSettings();
    window.setCameraSetting(camera, key, String(value));
  }
  function csIsPending(camera, keys) {
    return keys.some((k) => csPending.has(camera + "|" + k) || csPending.has("*|" + k));
  }
  const COL_KEYS = { wb: ["wb", "kelvin"], shift: ["shiftab", "shiftgm"], style: ["style", "contrast", "saturation", "colortone"] };

  // ----- Ajustes do Picture Style (contraste, saturação, tom de cor), como o INFO da
  // câmera: um botão na célula abre uma caixa com os três -----
  const STYLE_DETAILS = [["contrast", "Contrast", "contrast"], ["saturation", "Saturation", "saturation"], ["colortone", "Color tone", "colorTone"]];
  let csStylePop = null;  // { camera } aberto ("*" = All cameras)
  const signed = (v) => (v > 0 ? "+" + v : String(v));
  // level: "" igual; "partial" algumas opções diferentes (âmbar tracejado); "diff" todas (âmbar)
  function csStyleButton(camera, level, values) {
    const differs = !!level;
    const b = document.createElement("button");
    b.className = "cs-more" + (level ? " " + level : "") + (csStylePop && csStylePop.camera === camera ? " open" : "");
    b.textContent = "±";
    const chartNote = values && values.chart && STYLE_DETAILS.some(([, , f]) => csChartAdjust(values, f))
      ? " · calibrated with the chart: " + STYLE_DETAILS.filter(([, , f]) => csChartAdjust(values, f)).map(([, label, f]) => label + " " + signed(csChartAdjust(values, f))).join(", ") + " for this model"
      : "";
    b.title = (values ? "Contrast " + signed(values.contrast) + " · Saturation " + signed(values.saturation) + " · Color tone " + signed(values.colorTone) + chartNote
                      : "Contrast, saturation and color tone") + (level === "partial" ? " (some are different from the other cameras)" : differs ? " (different from the other cameras)" : "");
    b.dataset.styleCam = camera;
    b.addEventListener("click", (e) => {
      e.stopPropagation();
      csStylePop = csStylePop && csStylePop.camera === camera ? null : { camera };
      renderStylePop();
    });
    return b;
  }
  function renderStylePop() {
    let pop = $("csStylePop");
    document.querySelectorAll(".cs-more.open").forEach((b) => b.classList.remove("open"));
    const anchor = csStylePop && document.querySelector('.cs-more[data-style-cam="' + csStylePop.camera + '"]');
    if (!csStylePop || !anchor || $("camSettingsOverlay").classList.contains("hidden")) {
      if (pop) pop.remove();
      csStylePop = anchor ? csStylePop : null;
      return;
    }
    anchor.classList.add("open");
    if (!pop) {
      pop = el("div", "cs-pop");
      pop.id = "csStylePop";
      pop.addEventListener("click", (e) => e.stopPropagation());
      pop.addEventListener("focusout", () => { if (csRenderLater) setTimeout(csRender, 0); });
      $("camSettingsOverlay").appendChild(pop);
    }
    const all = csStylePop.camera === "*";
    const cams = all ? camSettings.filter((c) => c.adjustable && c.styleDetail) : camSettings.filter((c) => c.camera === csStylePop.camera);
    pop.innerHTML = "";
    pop.appendChild(el("div", "cs-pop-title", (all ? "All cameras" : csStylePop.camera) + " · Picture Style"));
    const range = [];
    for (let v = -4; v <= 4; v++) range.push(v);
    STYLE_DETAILS.forEach(([key, label, field]) => {
      const values = new Set(cams.map((c) => c[field]));
      const current = values.size === 1 ? [...values][0] : null;
      const refs = all ? null : csStyleDetailRefs();
      const off = current === null || (refs && refs[field] !== null && current - csChartAdjust(cams[0], field) !== refs[field]);
      const options = range.map((v) => [String(v), signed(v)]);
      if (current === null) options.unshift(["", "—", true]);
      const row = el("label", "cs-pop-row" + (csIsPending(csStylePop.camera, [key]) ? " applying" : ""));
      row.appendChild(el("span", "", label));
      row.appendChild(csSelect(options, current === null ? "" : String(current), (v) => csSet(csStylePop.camera, key, +v), "small" + (off ? " diff" : "")));
      pop.appendChild(row);
    });
    pop.appendChild(el("div", "cs-pop-note", "Color tone: − redder, + more yellow (skin tones)."));
    const r = anchor.getBoundingClientRect();
    const w = 230;
    pop.style.left = Math.max(8, Math.min(window.innerWidth - w - 8, r.right - w)) + "px";
    pop.style.top = Math.min(window.innerHeight - 190, r.bottom + 6) + "px";
  }
  document.addEventListener("click", () => { if (csStylePop) { csStylePop = null; renderStylePop(); } });

  // ab ou gm null: câmeras com valores diferentes ("—" na lista; qualquer valor, até 0, aplica)
  // flip: modelo que o app mediu com o sentido invertido (troca os rótulos daquele eixo)
  function shiftPair(ab, gm, onAB, onGM, diff, flip) {
    const f = flip || {};
    const box = el("span", "cs-pair");
    const range = [];
    for (let v = -9; v <= 9; v++) range.push(v);
    const list = (current, neg, pos, onPick, differs) => {
      const options = range.map((v) => [String(v), shiftLabel(v, neg, pos)]);
      if (current === null) options.unshift(["", "—", true]);
      return csSelect(options, current === null ? "" : String(current), (v) => onPick(+v), "small" + (differs ? " diff" : ""));
    };
    // No SDK, + = âmbar e + = verde (conferido na T7 e na SL3)
    box.append(list(ab, f.ab ? "A" : "B", f.ab ? "B" : "A", onAB, diff || ab === null),
               list(gm, f.gm ? "G" : "M", f.gm ? "M" : "G", onGM, diff || gm === null));
    box.title = "Blue (B) ↔ amber (A) and green (G) ↔ magenta (M)";
    return box;
  }

  // Célula de uma câmera
  function csCell(c, k, ref) {
    const td = document.createElement("td");
    td.className = "cs-cell" + (csIsPending(c.camera, COL_KEYS[k] || [k]) ? " applying" : "");
    const diff = csDiffers(c, k, ref);
    if (k === "focus") {
      const f = c.settings.focus;
      if (!f || !f.available) td.appendChild(csText("—", "na", "Not available on this camera"));
      else if (c.focusManual) td.appendChild(csText("MF", "ok"));
      else td.appendChild(csText(f.label, "warn", "Autofocus is on. Turn the lens switch to MF so the camera fires without delay."));
      return td;
    }
    if (k === "shift") {
      if (!c.shiftAvailable) td.appendChild(csText("—", "na", "Not available on this camera"));
      else td.appendChild(shiftPair(c.shiftAB, c.shiftGM, (v) => csSet(c.camera, "shiftab", v), (v) => csSet(c.camera, "shiftgm", v), diff, { ab: c.abFlip, gm: c.gmFlip }));
      return td;
    }
    const st = c.settings[k];
    if (!st || !st.available) { td.appendChild(csText("—", "na", "Not available on this camera")); return td; }
    const current = k === "wb" && st.value === 9 ? "k" + c.kelvin : String(st.value);
    const currentLabel = k === "wb" && st.value === 9 ? c.kelvin + " K" : st.label;
    if (!st.choices.length) {
      // Tem o valor, mas não deixa mudar agora: valor dentro do tracejado
      td.appendChild(csText(currentLabel, "na" + (diff ? " diff" : ""), "The camera does not allow changing this now."));
      return td;
    }
    let options = st.choices.filter(([v]) => !(k === "wb" && v === 9)).map(([v, l]) => [String(v), l]);
    if (k === "wb" && st.choices.some(([v]) => v === 9)) {
      options = options.concat(KELVINS.map((kv) => ["k" + kv, kv + " K"]));
      if (st.value === 9 && !KELVINS.includes(c.kelvin)) options.push(["k" + c.kelvin, c.kelvin + " K"]);
    }
    if (!options.some(([v]) => v === current)) options.unshift([current, currentLabel]);
    td.appendChild(csSelect(options, current, (v) => {
      if (k === "wb" && v.startsWith("k")) csSet(c.camera, "kelvin", +v.slice(1));
      else csSet(c.camera, k, +v);
    }, diff ? "diff" : ""));
    if (k === "style" && c.styleDetail) td.appendChild(csStyleButton(c.camera, csStyleDetailLevel(c), c));
    return td;
  }

  // Célula de "All cameras": só os valores que todas as câmeras ajustáveis aceitam
  function csAllCell(k, cams) {
    const td = document.createElement("td");
    td.className = "cs-cell" + (csIsPending("*", COL_KEYS[k] || [k]) ? " applying" : "");
    const labels = new Set(cams.map((c) => csLabel(c, k)).filter((l) => l !== null));
    const same = labels.size === 1;
    if (k === "focus") {
      const af = camSettings.filter((c) => c.settings.focus && c.settings.focus.available && !c.focusManual).length;
      td.appendChild(af ? csText("AF on " + af, "warn", "Autofocus is on in " + plural(af, "camera", "cameras") + ". Turn the lens switch to MF.")
                        : csText("All MF", "ok"));
      return td;
    }
    if (!cams.length) { td.appendChild(csText("—", "na")); return td; }
    if (k === "shift") {
      if (!cams.every((c) => c.shiftAvailable)) { td.appendChild(csText("—", "na", "Not available on every camera")); return td; }
      const one = (key) => (new Set(cams.map((c) => c[key])).size === 1 ? cams[0][key] : null);
      td.appendChild(shiftPair(one("shiftAB"), one("shiftGM"), (v) => csSet("*", "shiftab", v), (v) => csSet("*", "shiftgm", v), false));
      return td;
    }
    if (k === "wb") {
      // Os predefinidos que todas aceitam e, se todas aceitarem, o atalho de Kelvin
      // (a SL3 e a T7, por exemplo, não têm Kelvin)
      const wbs = cams.map((c) => c.settings.wb).filter((w) => w && w.available);
      const wbText = (c) => (c.settings.wb.value === 9 ? c.kelvin + " K" : c.settings.wb.label);
      if (!wbs.length || wbs.some((w) => !w.choices.length)) {
        td.appendChild(csText("—", "na", "Not changeable on every camera"));
        return td;
      }
      const accepted = (v) => wbs.every((w) => w.choices.some(([x]) => x === v));
      let options = wbs[0].choices.filter(([v]) => v !== 9 && accepted(v)).map(([v, l]) => [String(v), l]);
      if (accepted(9)) options = options.concat(KELVINS.map((kv) => ["k" + kv, kv + " K"]));
      if (!options.length) { td.appendChild(csText("—", "na", "The cameras have no white balance in common")); return td; }
      let current = "";
      const first = cams.find((c) => c.settings.wb && c.settings.wb.available);
      if (same) {
        current = first.settings.wb.value === 9 ? "k" + first.kelvin : String(first.settings.wb.value);
        if (!options.some(([x]) => x === current)) current = "";
      }
      if (!current) options.unshift(["", same ? wbText(first) : "Different", true]);
      td.appendChild(csSelect(options, current, (v) => {
        if (v.startsWith("k")) csSet("*", "kelvin", +v.slice(1));
        else csSet("*", "wb", +v);
      }, same ? "" : "diff"));
      return td;
    }
    const lists = cams.filter((c) => c.settings[k] && c.settings[k].available).map((c) => c.settings[k].choices);
    // Alguma câmera não deixa mudar agora: "—" tracejado (não dá para aplicar em todas)
    if (!lists.length || lists.some((l) => !l.length)) {
      const fixed = cams.filter((c) => !(c.settings[k] && c.settings[k].available && c.settings[k].choices.length)).map((c) => c.camera);
      td.appendChild(csText("—", "na", "Not changeable on every camera" + (fixed.length ? " (" + fixed.join(", ") + " does not allow it now)" : "")));
      return td;
    }
    const common = lists[0].filter(([v]) => lists.every((l) => l.some(([x]) => x === v)));
    if (!common.length) { td.appendChild(csText("—", "na", "The cameras have no value in common")); return td; }
    const options = common.map(([v, l]) => [String(v), l]);
    let current = "";
    if (same) {
      const v = String(cams.find((c) => c.settings[k] && c.settings[k].available).settings[k].value);
      if (options.some(([x]) => x === v)) current = v;
    }
    if (!current) options.unshift(["", same ? [...labels][0] : "Different", true]);
    td.appendChild(csSelect(options, current, (v) => csSet("*", k, +v), same ? "" : "diff"));
    if (k === "style" && cams.some((c) => c.styleDetail)) {
      const refs = csStyleDetailRefs();
      const n = STYLE_DETAILS.filter(([, , f]) => refs[f] !== null).length;
      td.appendChild(csStyleButton("*", n === 0 ? "" : n === STYLE_DETAILS.length ? "diff" : "partial", null));
    }
    return td;
  }

  // Redesenha, mas não com uma lista aberta (a lista fecharia sozinha): espera sair dela
  function csRender() {
    const a = document.activeElement;
    const pop = $("csStylePop");
    if (a && a.tagName === "SELECT" && ($("csBody").contains(a) || (pop && pop.contains(a)))) { csRenderLater = true; return; }
    csRenderLater = false;
    renderCamSettings();
  }
  $("csBody").addEventListener("focusout", () => { if (csRenderLater) setTimeout(csRender, 0); });

  function renderCamSettings() {
    const body = $("csBody");
    const scroll = body.scrollTop;
    body.innerHTML = "";
    const copy = $("csCopy");
    copy.innerHTML = "";
    if (!camSettings.length) {
      body.appendChild(el("div", "cs-empty", "No camera connected."));
      copy.disabled = true;
      return;
    }
    const ref = csReference();
    const adjustable = camSettings.filter((c) => c.adjustable);
    const table = document.createElement("table");
    table.className = "cs";
    const head = document.createElement("tr");
    head.appendChild(document.createElement("th"));
    CS_COLS.forEach(([, label]) => head.appendChild(el("th", "", label)));
    table.appendChild(head);

    const all = document.createElement("tr");
    all.className = "all";
    const allName = el("td", "name", "All cameras");
    allName.appendChild(el("small", "", "applies to every camera"));
    all.appendChild(allName);
    CS_COLS.forEach(([k]) => all.appendChild(csAllCell(k, adjustable)));
    table.appendChild(all);

    camSettings.forEach((c) => {
      const tr = document.createElement("tr");
      const name = el("td", "name", pad(c.order) + " " + c.camera);
      name.appendChild(el("small", "", c.model + (c.mode ? " · " + c.mode : "")));
      tr.appendChild(name);
      if (!c.adjustable) {
        tr.className = "locked";
        const msg = el("td", "msg", "⚠  " + (c.reason || "Settings can only be changed on the camera."));
        msg.colSpan = CS_COLS.length;
        tr.appendChild(msg);
        table.appendChild(tr);
        return;
      }
      CS_COLS.forEach(([k]) => tr.appendChild(csCell(c, k, ref)));
      table.appendChild(tr);
      const drive = c.settings.drive;
      const driveNote = drive && drive.available && !isSingleShot(drive.value)
        ? "The camera is on " + drive.label + " and did not accept single shooting. Set it to single shooting on the camera." : "";
      const noteText = [c.reason, driveNote].filter(Boolean).join(" ");
      if (noteText) {
        tr.classList.add("has-note");
        const note = document.createElement("tr");
        note.className = "note";
        note.appendChild(document.createElement("td"));
        const td = el("td", "", "⚠  " + noteText);
        td.colSpan = CS_COLS.length;
        note.appendChild(td);
        table.appendChild(note);
      }
    });
    body.appendChild(table);
    body.scrollTop = scroll;
    renderStylePop();

    copy.disabled = adjustable.length < 2;
    copy.appendChild(new Option("Choose…", ""));
    adjustable.forEach((c) => copy.appendChild(new Option(pad(c.order) + " " + c.camera, c.camera)));
    copy.value = "";
  }

  // Deixa todas as câmeras iguais a uma escolhida
  function copyFromCamera(name) {
    const c = camSettings.find((x) => x.camera === name);
    if (!c) return;
    ["iso", "tv", "av", "expcomp", "style", "quality"].forEach((k) => {
      const st = c.settings[k];
      if (st && st.available && st.choices.length) csSet("*", k, st.value);
    });
    const wb = c.settings.wb;
    if (wb && wb.available && wb.choices.length) {
      if (wb.value === 9) csSet("*", "kelvin", c.kelvin);
      else csSet("*", "wb", wb.value);
    }
    if (c.shiftAvailable) { csSet("*", "shiftab", c.shiftAB); csSet("*", "shiftgm", c.shiftGM); }
    // Ajustes do Picture Style (Contrast, Saturation, Color tone): depois do estilo (cada
    // estilo guarda os seus). Com a calibração da cartela, cada modelo recebe o valor que
    // o deixa parecido com a câmera escolhida (o mesmo que o Match faria).
    if (c.styleDetail) {
      camSettings.filter((x) => x.adjustable && x.styleDetail && x.camera !== name).forEach((x) => {
        STYLE_DETAILS.forEach(([key, , f]) => {
          const v = Math.max(-4, Math.min(4, c[f] - csChartAdjust(c, f) + csChartAdjust(x, f)));
          const styleChanges = (c.settings.style && c.settings.style.value) !== (x.settings.style && x.settings.style.value);
          if (v !== x[f] || styleChanges) csSet(x.camera, key, v);
        });
      });
    }
    toast("Copying the settings of " + name + " to the other cameras");
  }

  // ----- Test shot: dispara todas as câmeras (fotos temporárias, como no "Reorder by
  // photo") para conferir lado a lado. Com "Match automatically", o app também muda as
  // câmeras para ficarem parecidas (até 5 fotos) e mostra as últimas. -----
  function csTakeTest(skipIsoCheck) {
    if (csTest.capturing) {  // o botão virou "Cancel"
      if (csTest.cancelling) return;
      csTest.cancelling = true;
      window.cancelTest();
      csGallery.message("Cancelling");
      updateButtons();
      return;
    }
    const connected = cameras.filter((c) => c.connected).length;
    if (busy || !connected) return;
    const match = $("csAuto").checked && connected >= 2;
    // ISO em Auto: o brilho não tem como ser igualado; avisa antes de começar
    const isoAuto = camSettings.filter((c) => c.adjustable && c.settings.iso && c.settings.iso.available && c.settings.iso.value === 0).map((c) => c.camera);
    if (match && isoAuto.length && skipIsoCheck !== true) {
      dialog("ISO is on Auto",
        (isoAuto.length === 1 ? isoAuto[0] + " has" : isoAuto.join(", ") + " have") + " ISO on Auto, so each camera picks its own brightness and the app cannot match it. " +
        "Set an ISO first (for example in All cameras), or continue to match only the color.",
        [{ label: "Cancel" }, { label: "Continue anyway", primary: true, onClick: () => csTakeTest(true) }]);
      return;
    }
    csTest.capturing = true;
    csTest.cancelling = false;
    csTest.has = false;
    csTest.mode = match ? "match" : "test";
    $("camSettingsOverlay").querySelector(".cs-sheet").classList.add("capturing");
    $("csTestBox").classList.remove("hidden", "has-photos");
    csSetZoom(false);
    csSetCollapsed(false);
    csGallery.setItems([]);
    csGallery.setHead(match ? "Test shot · matching" : "Test shot", "");
    csGallery.message("Taking a photo with each camera");
    if (match) window.autoMatch(intValue(ui.downloadWait, 1, 10), $("csCard").checked ? 1 : 0);
    else window.takeOrderPhotos(intValue(ui.downloadWait, 1, 10));
  }
  // ----- Cartão cinza: o app achou o cartão em cada foto; a pessoa confere ou clica nele -----
  const csCard = { regions: {}, mode: "card", quads: {}, moved: {} };
  function csCardHide() { $("csCardPick").classList.add("hidden"); }
  function csCardOpen() { return !$("csCardPick").classList.contains("hidden"); }
  function csCardBox(tile, r) {
    const box = tile.querySelector(".cs-card-box");
    const label = tile.querySelector(".cs-card-label");
    const found = r && r.found;
    box.style.display = found ? "" : "none";
    if (found) {
      box.style.left = (r.x0 * 100) + "%";
      box.style.top = (r.y0 * 100) + "%";
      box.style.width = ((r.x1 - r.x0) * 100) + "%";
      box.style.height = ((r.y1 - r.y0) * 100) + "%";
    }
    label.textContent = tile.dataset.title + (found ? "" : " · not found, click the card");
    label.classList.toggle("missing", !found);
  }
  function csCardStatus() {
    if (csCard.mode === "chart") return csChartStatus();
    const all = Object.keys(csCard.regions);
    const found = all.filter((k) => csCard.regions[k] && csCard.regions[k].found).length;
    $("csCardStatus").textContent = found + " of " + all.length + " cameras with the card marked";
    $("csCardContinue").disabled = found < 2;
  }
  function csCardShow(list, regions) {
    csCard.mode = "card";
    $("csCardTitle").textContent = "Gray card";
    $("csCardText").textContent = "Check that the box is on the gray card in every photo. If it is wrong or missing, click the card in that photo, or drag to draw a box over it.";
    const session = (list || [])[0];
    csCard.regions = {};
    regions.forEach((r) => { csCard.regions[r.camera] = r; });
    const grid = $("csCardGrid");
    grid.innerHTML = "";
    placedCameras().filter((c) => c.connected && csCard.regions[c.name]).forEach((cam) => {
      const photos = session ? session.photos.filter((p) => p.camera === cam.name) : [];
      const pick = photos.find((p) => /\.jpe?g$/i.test(p.file)) || photos.find((p) => p.previewable);
      const tile = document.createElement("div");
      tile.className = "cs-card-tile";
      tile.dataset.title = shortModel(cam.model) + " · " + cam.name;
      tile.innerHTML = '<div class="cs-card-photo"><img alt=""><div class="cs-card-box"></div></div><div class="cs-card-label"></div>';
      const img = tile.querySelector("img");
      if (session && pick) orderPhoto(session.name, pick.file, 900).then((url) => { if (url) img.src = url; });
      // Clicar: o app acha o cartão em volta do ponto. Arrastar: a pessoa desenha a área.
      img.draggable = false;
      const photoEl = tile.querySelector(".cs-card-photo");
      const at = (e) => {
        const rect = img.getBoundingClientRect();
        return { x: Math.min(1, Math.max(0, (e.clientX - rect.left) / rect.width)),
                 y: Math.min(1, Math.max(0, (e.clientY - rect.top) / rect.height)), ok: rect.width > 0 && rect.height > 0 };
      };
      const apply = (r) => {
        if (!r) return;
        csCard.regions[cam.name] = r;
        csCardBox(tile, r);
        csCardStatus();
      };
      let drag = null;
      photoEl.addEventListener("dragstart", (e) => e.preventDefault());
      photoEl.addEventListener("pointerdown", (e) => {
        if (e.button !== 0) return;
        e.preventDefault();
        const p = at(e);
        if (!p.ok) return;
        drag = { x0: p.x, y0: p.y, sx: e.clientX, sy: e.clientY, moved: false };
        photoEl.setPointerCapture(e.pointerId);
      });
      photoEl.addEventListener("pointermove", (e) => {
        if (!drag) return;
        if (!drag.moved && Math.hypot(e.clientX - drag.sx, e.clientY - drag.sy) < 6) return;
        drag.moved = true;
        const p = at(e);
        csCardBox(tile, { found: true, x0: Math.min(drag.x0, p.x), y0: Math.min(drag.y0, p.y), x1: Math.max(drag.x0, p.x), y1: Math.max(drag.y0, p.y) });
      });
      const finish = (e) => {
        if (!drag) return;
        const d = drag;
        drag = null;
        const p = at(e);
        const k = (v) => String(Math.round(v * 1000));
        if (!d.moved) {
          Promise.resolve(window.grayCardFind(cam.name, k(p.x), k(p.y))).then(apply);
          return;
        }
        const x0 = Math.min(d.x0, p.x), y0 = Math.min(d.y0, p.y), x1 = Math.max(d.x0, p.x), y1 = Math.max(d.y0, p.y);
        if (x1 - x0 < 0.005 || y1 - y0 < 0.005) { csCardBox(tile, csCard.regions[cam.name]); return; }
        Promise.resolve(window.grayCardSet(cam.name, k(x0), k(y0), k(x1), k(y1))).then((r) => apply(r || csCard.regions[cam.name]));
      };
      photoEl.addEventListener("pointerup", finish);
      photoEl.addEventListener("pointercancel", () => { drag = null; csCardBox(tile, csCard.regions[cam.name]); });
      grid.appendChild(tile);
      csCardBox(tile, csCard.regions[cam.name]);
    });
    csCardStatus();
    csSetZoom(false);
    $("csCardPick").classList.remove("hidden");
    csGallery.message("Waiting for you to check the gray card");
  }
  $("csCardContinue").addEventListener("click", () => {
    csCardHide();
    csGallery.message(csCard.mode === "chart" ? "Calibrating with the chart" : "Matching with the gray card");
    window.grayCardConfirm();
  });

  // ----- Cartela de cores: o app achou as 4 marcas; a pessoa confere ou arrasta os cantos -----
  // Cantos na ordem da cartela: 0 em cima à esquerda (o da bolinha), 1 em cima à direita,
  // 2 embaixo à direita, 3 embaixo à esquerda
  function csChartDraw(tile, q) {
    const svg = tile.querySelector("svg");
    const pts = [0, 1, 2, 3].map((k) => (q.x[k] * 100) + "," + (q.y[k] * 100)).join(" ");
    svg.querySelector("polygon").setAttribute("points", pts);
    tile.querySelectorAll(".cs-chart-handle").forEach((h, k) => {
      h.style.left = (q.x[k] * 100) + "%";
      h.style.top = (q.y[k] * 100) + "%";
    });
    const marked = q.found || csCard.moved[tile.dataset.camera];
    const label = tile.querySelector(".cs-card-label");
    label.textContent = tile.dataset.title + (marked ? "" : " · not found, drag the 4 dots onto the 4 corner marks");
    label.classList.toggle("missing", !marked);
    tile.classList.toggle("unmarked", !marked);
  }
  function csChartStatus() {
    const all = Object.keys(csCard.quads);
    const marked = all.filter((k) => csCard.quads[k].found || csCard.moved[k]).length;
    $("csCardStatus").textContent = marked + " of " + all.length + " cameras with the chart marked";
    $("csCardContinue").disabled = marked < 2;
  }
  function csChartShow(list, quads) {
    csCard.mode = "chart";
    csCard.quads = {};
    csCard.moved = {};
    quads.forEach((q) => { csCard.quads[q.camera] = q; });
    $("csCardTitle").textContent = "Chart";
    $("csCardText").textContent = "Check that the 4 dots are on the centers of the 4 corner marks in every photo. Dot 1 goes on the mark next to the small black dot. To fix one, drag it (or click where it should go).";
    const session = (list || [])[0];
    const grid = $("csCardGrid");
    grid.innerHTML = "";
    placedCameras().filter((c) => c.connected && csCard.quads[c.name]).forEach((cam) => {
      const photos = session ? session.photos.filter((p) => p.camera === cam.name) : [];
      const pick = photos.find((p) => /\.jpe?g$/i.test(p.file)) || photos.find((p) => p.previewable);
      const tile = document.createElement("div");
      tile.className = "cs-card-tile cs-chart-tile";
      tile.dataset.title = shortModel(cam.model) + " · " + cam.name;
      tile.dataset.camera = cam.name;
      tile.innerHTML = '<div class="cs-card-photo"><img alt="">' +
        '<svg class="cs-chart-lines" viewBox="0 0 100 100" preserveAspectRatio="none"><polygon></polygon></svg>' +
        [1, 2, 3, 4].map((n) => '<span class="cs-chart-handle' + (n === 1 ? " first" : "") + '">' + n + "</span>").join("") +
        '</div><div class="cs-card-label"></div>';
      const img = tile.querySelector("img");
      img.draggable = false;
      if (session && pick) orderPhoto(session.name, pick.file, 900).then((url) => { if (url) img.src = url; });
      const photoEl = tile.querySelector(".cs-card-photo");
      const at = (e) => {
        const rect = img.getBoundingClientRect();
        return { x: Math.min(1, Math.max(0, (e.clientX - rect.left) / rect.width)),
                 y: Math.min(1, Math.max(0, (e.clientY - rect.top) / rect.height)), ok: rect.width > 0 && rect.height > 0 };
      };
      let drag = -1;
      photoEl.addEventListener("dragstart", (e) => e.preventDefault());
      photoEl.addEventListener("pointerdown", (e) => {
        if (e.button !== 0) return;
        e.preventDefault();
        const p = at(e);
        if (!p.ok) return;
        const q = csCard.quads[cam.name];
        // O canto pego (ou o mais perto do clique) vai para onde está o ponteiro
        const handle = e.target.closest(".cs-chart-handle");
        drag = handle ? [...tile.querySelectorAll(".cs-chart-handle")].indexOf(handle)
                      : [0, 1, 2, 3].reduce((best, k) => (Math.hypot(q.x[k] - p.x, q.y[k] - p.y) < Math.hypot(q.x[best] - p.x, q.y[best] - p.y) ? k : best), 0);
        if (!handle) { q.x[drag] = p.x; q.y[drag] = p.y; csChartDraw(tile, q); }
        photoEl.setPointerCapture(e.pointerId);
      });
      photoEl.addEventListener("pointermove", (e) => {
        if (drag < 0) return;
        const p = at(e);
        const q = csCard.quads[cam.name];
        q.x[drag] = p.x;
        q.y[drag] = p.y;
        csChartDraw(tile, q);
      });
      const finish = () => {
        if (drag < 0) return;
        drag = -1;
        const q = csCard.quads[cam.name];
        csCard.moved[cam.name] = true;
        const args = [cam.name];
        for (let k = 0; k < 4; k++) args.push(String(Math.round(q.x[k] * 1000)), String(Math.round(q.y[k] * 1000)));
        Promise.resolve(window.chartSetQuad(...args)).then(() => { csChartDraw(tile, q); csChartStatus(); });
      };
      photoEl.addEventListener("pointerup", finish);
      photoEl.addEventListener("pointercancel", finish);
      grid.appendChild(tile);
      csChartDraw(tile, csCard.quads[cam.name]);
    });
    csChartStatus();
    csSetZoom(false);
    $("csCardPick").classList.remove("hidden");
    csGallery.message("Waiting for you to check the chart");
  }

  // Botão "Calibrate with chart": instruções, abrir a cartela e começar
  function csChartIntro() {
    const cams = camSettings.filter((c) => c.adjustable && c.styleDetail && cameras.some((x) => x.name === c.camera && x.connected));
    const models = new Set(cams.map((c) => c.model));
    if (models.size < 2) {
      dialog("Calibrate with chart",
        "This compares camera models, so it needs at least two different models connected whose Picture Style can be changed from the app (for example, an SL3 and a T7).\n\n" +
        "Cameras of the same model already look alike: Match automatically is enough for them.",
        [{ label: "Open chart", link: true, onClick: () => window.openChart() }, { label: "OK", primary: true }]);
      return;
    }
    dialog("Calibrate with chart",
      "Makes cameras of different models look alike in contrast, saturation and color tone. Do it once for each new camera model; Match automatically then uses the result.\n\n" +
      "1. Click Open chart and print it at 100% (actual size, no scaling), on matte paper.\n" +
      "2. Put it where every camera sees the whole chart, including the 4 corner marks: flat, under even light, without glare. It should fill about a quarter of each photo or more.\n" +
      "3. Set the exposure so the chart looks normal in every camera (tip: run Test shot with Match automatically first).\n" +
      "4. Click Start. The app takes 4 photos, changing contrast, saturation and color tone, and then puts every setting back as it was.\n\n" +
      "After the first photo, you check that the chart corners are marked right.",
      [{ label: "Cancel" },
       { label: "Open chart", link: true, onClick: () => {
         Promise.resolve(window.openChart()).then((r) => { if (!r || !r.ok) toast("The chart file was not found. Build the app again."); });
         csChartIntro();
       } },
       { label: "Start", primary: true, onClick: csChartStart }]);
  }
  function csChartStart() {
    const connected = cameras.filter((c) => c.connected).length;
    if (busy || !connected || csTest.capturing) return;
    csTest.capturing = true;
    csTest.cancelling = false;
    csTest.has = false;
    csTest.mode = "chart";
    $("camSettingsOverlay").querySelector(".cs-sheet").classList.add("capturing");
    $("csTestBox").classList.remove("hidden", "has-photos");
    csSetZoom(false);
    csSetCollapsed(false);
    csGallery.setItems([]);
    csGallery.setHead("Chart calibration", "");
    csGallery.message("Taking a photo with each camera");
    updateButtons();
    window.chartCalibrate(intValue(ui.downloadWait, 1, 10));
  }
  $("csChart").addEventListener("click", () => { if (!csTest.capturing) csChartIntro(); });
  $("csCardCancel").addEventListener("click", () => {
    csCardHide();
    if (csTest.capturing) csTakeTest();  // o mesmo que o botão Cancel do Test shot
  });
  // "Use gray card" só vale com "Match automatically"
  function csCardOption() {
    const on = $("csAuto").checked;
    $("csCard").disabled = !on;
    document.querySelector(".cs-card-option").classList.toggle("off", !on);
  }
  $("csAuto").addEventListener("change", csCardOption);
  csCardOption();

  function showCsTest(list) {
    csTest.capturing = false;
    csTest.cancelling = false;
    csTest.has = true;
    $("camSettingsOverlay").querySelector(".cs-sheet").classList.remove("capturing");
    const session = (list || [])[0];
    // Uma foto por câmera, na ordem atual (JPEG quando houver RAW + JPEG)
    const items = placedCameras().filter((c) => c.connected).map((cam) => {
      const photos = session ? session.photos.filter((p) => p.camera === cam.name) : [];
      const pick = photos.find((p) => /\.jpe?g$/i.test(p.file)) || photos.find((p) => p.previewable) || photos[0];
      return { name: cam.name, session: session ? session.name : "", order: cam.order,
               file: pick ? pick.file : "", previewable: pick ? pick.previewable : false,
               title: shortModel(cam.model), subtitle: cam.name };
    });
    csGallery.setItems(items, 0);
    $("csTestBox").classList.toggle("has-photos", items.length > 0);
    const missing = items.filter((i) => !i.file).length;
    csGallery.setHead(csTest.mode === "chart" ? "Chart calibration" : csTest.mode === "match" ? "Test shot · matched" : "Test shot",
      (missing ? plural(missing, "camera", "cameras") + " did not send a photo · " : "") + "Not saved · click a photo to enlarge · ← → to compare");
  }
  function csDiscardTest() {
    if (!csTest.has && !csTest.capturing) return;
    csTest.has = false;
    csSetZoom(false);
    $("csTestBox").classList.add("hidden");
    $("csTestBox").classList.remove("has-photos");
    csGallery.setItems([]);
    if (!csTest.capturing) window.discardOrderPhotos();
    Object.keys(photoCache).filter((k) => k.startsWith("order/")).forEach((k) => delete photoCache[k]);
  }
  function closeCamSettings() {
    // Fechar esperando o cartão cinza: cancela (senão o Match ficaria esperando)
    if (csCardOpen()) $("csCardCancel").click();
    $("camSettingsOverlay").classList.add("hidden");
    csStylePop = null;
    renderStylePop();
    clearInterval(csPoll);
    csPoll = 0;
    csDiscardTest();
  }
  // Com a janela aberta, lê as câmeras de novo a cada 3 s (mudanças feitas na própria
  // câmera aparecem sozinhas). Não lê durante fotos, mudanças ou com o app ocupado.
  function csPollTick() {
    if ($("camSettingsOverlay").classList.contains("hidden")) return;
    if (busy || csTest.capturing || csPending.size || !cameras.some((c) => c.connected)) return;
    window.readCameraSettings();
  }

  $("camSettingsBtn").addEventListener("click", () => {
    renderCamSettings();
    $("camSettingsOverlay").classList.remove("hidden");
    window.readCameraSettings();
    clearInterval(csPoll);
    csPoll = setInterval(csPollTick, 3000);
  });
  $("csTest").addEventListener("click", () => csTakeTest());
  $("csDone").addEventListener("click", closeCamSettings);
  $("csRead").addEventListener("click", () => window.readCameraSettings());
  $("csCopy").addEventListener("change", (e) => { if (e.target.value) copyFromCamera(e.target.value); });

  // Esc fecha o menu, o About e o Settings; com eles abertos, as teclas não
  // chegam à tela de trás. Windows: Ctrl+, abre o Settings.
  document.addEventListener("keydown", (e) => {
    if (!menuEl.list.classList.contains("hidden") && e.key === "Escape") {
      setMenuOpen(false);
      e.stopImmediatePropagation();
      return;
    }
    const open = ["aboutOverlay", "settingsOverlay", "camSettingsOverlay"].find((id) => !$(id).classList.contains("hidden"));
    if (open === "camSettingsOverlay" && csTest.has && e.target.tagName !== "SELECT" && (e.key === "ArrowLeft" || e.key === "ArrowRight")) {
      e.preventDefault();
      e.stopImmediatePropagation();
      if (e.key === "ArrowLeft") csGallery.prev(); else csGallery.next();
      return;
    }
    if (open) {
      if (e.key === "Escape") {
        if (open === "camSettingsOverlay" && csCardOpen()) $("csCardCancel").click();  // cartão cinza: Esc = Cancel
        else if (open === "camSettingsOverlay" && csStylePop) { csStylePop = null; renderStylePop(); }
        else if (open === "camSettingsOverlay" && csZoom) csSetZoom(false);  // primeiro fecha a foto grande
        else if (open === "camSettingsOverlay") closeCamSettings();
        else $(open).classList.add("hidden");
      }
      e.stopImmediatePropagation();
      return;
    }
    if (e.key === "," && e.ctrlKey && upd.info && upd.info.os === "windows") {
      e.preventDefault();
      openSettings();
    }
  }, true);

  // =================== Ações ===================
  ui.restartBtn.addEventListener("click", () => {
    dialog("Restart camera connections?", "Use this when a camera is on and plugged in but does not connect. All cameras reconnect.", [
      { label: "Cancel" },
      { label: "Restart", primary: true, onClick: () => window.restartConnections() },
    ]);
  });
  ui.shootBtn.addEventListener("click", () => {
    if (busy && busyAction === "shoot") return cancelShoot();
    window.shoot(intValue(ui.countdown, 0, 0), intValue(ui.sessions, 1, 1), intValue(ui.downloadWait, 1, 10));
  });

  // Cancelar o bullet time: botão na contagem, Esc na contagem ou o botão de disparo
  // (que vira "Cancel bullet time" enquanto roda). O que já foi disparado não volta.
  function cancelShoot() {
    if (!(busy && busyAction === "shoot") || shootCancelling) return;
    shootCancelling = true;
    $("countdownCancel").disabled = true;
    updateButtons();
    window.cancelShoot();
  }
  $("countdownCancel").addEventListener("click", cancelShoot);
  document.addEventListener("keydown", (e) => {
    if (e.key !== "Escape" || ui.countdownOverlay.classList.contains("hidden")) return;
    e.stopImmediatePropagation();
    cancelShoot();
  }, true);
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
    if (info && info.os === "windows") menuEl.btn.classList.remove("hidden");
    if (info && info.version) {
      $("brandVersion").textContent = "v" + info.version;
      $("brandVersion").title = "Stillpoint " + info.version;
    }
    if (info && info.previousVersion && isNewer(info.version, info.previousVersion))
      toast("Updated to Stillpoint " + info.version, 5000);
    checkForUpdate();
  });
})();
