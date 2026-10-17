// Janela Live: o que está acontecendo no disparo, em tamanho de palco (segundo monitor
// ou janela flutuante). Recebe os mesmos avisos do programa que a página principal
// (window.onCoreEvent -> Live.handle) e desenha: contagem inicial, as câmeras na ordem
// do rig (disparadas, disparando agora, próximas), a contagem até o próximo grupo, as
// fotos chegando e a sequência tocando em loop no fim.
//
// Dois modos: Operator (tudo: câmeras, plano, fotos recebidas, erros) e Audience (só a
// contagem, "hold still", a contagem entre grupos e a sequência no fim).
// Layout das câmeras: arc (padrão), circle ou line.
//
// A página principal chama Live.init({...}), Live.setCameras(lista) e, quando a sessão
// nova chega, Live.playback(fotos, carregar). Live.handle(evento) recebe os avisos.
window.Live = (function () {
  "use strict";
  const $ = (id) => document.getElementById(id);
  const DEFAULTS = { mode: "operator", layout: "arc", sound: true, sounds: true };
  const NUMERIC_GAP_MS = 1500;   // contagem em números só para intervalos maiores que isto
  const FLASH_MS = 320;          // quanto tempo a câmera fica branca depois de disparar
  const PLAY_FPS = 8;

  const S = {
    open: false,
    opts: Object.assign({}, DEFAULTS),
    cams: [],                  // [{name, number, model, connected}] na ordem do rig
    plan: null,                // {all, countdown, sessions, shots:[{cam, number, t}], groups:[{t, idx:[...]}]}
    session: { index: 1, total: 1 },
    phase: "idle",             // idle | countdown | firing | receiving | done | cancelled | failed
    countdown: 0,
    t0: 0,                     // instante (performance.now) do primeiro disparo
    fired: new Set(),          // índices (1..n) do plano já disparados
    failed: new Set(),         // nomes de câmeras com erro no disparo
    lost: new Set(),           // câmeras cuja foto não chegou
    received: { done: 0, total: 0 },
    elapsedMs: 0,              // duração do disparo (do primeiro ao último)
    play: { urls: [], i: 0, timer: 0, waiting: false },
    raf: 0,
    hideTimer: 0,
    rigVisible: false,
  };
  let cb = {};
  let el = null;

  // ---------------------------------------------------------------- utilidades
  function fmtS(ms) { return (Math.max(0, ms) / 1000).toFixed(1) + " s"; }
  function camNumber(name) {
    const c = S.cams.find((x) => x.name === name);
    return c ? c.number : 0;
  }
  function camList(numbers) {
    const list = numbers.slice().sort((a, b) => a - b);
    if (list.length <= 6) return list.join(", ");
    return list.slice(0, 5).join(", ") + " … " + list[list.length - 1];
  }
  function groupsOf(shots) {
    const groups = [];
    shots.forEach((s, i) => {
      const last = groups[groups.length - 1];
      if (last && last.t === s.t) last.idx.push(i + 1);
      else groups.push({ t: s.t, idx: [i + 1] });
    });
    return groups;
  }
  function planSummary() {
    const p = S.plan;
    if (!p) return S.cams.length ? S.cams.length + " cameras" : "";
    const n = p.shots.length;
    if (p.all) return n + " cameras · all at once";
    const g = p.groups.length;
    const total = p.shots[n - 1].t;
    if (g === n) return n + " shots · one after another · " + fmtS(total);
    return n + " shots · " + g + " groups · " + fmtS(total);
  }

  // ---------------------------------------------------------------- montagem
  function build() {
    el = {
      root: $("live"),
      top: $("liveTop"), topInfo: $("liveTopInfo"), topRight: $("liveTopRight"),
      big: $("liveBig"), sub: $("liveSub"),
      st: $("liveStatus"), stN: $("liveStatusN"), stT: $("liveStatusT"),
      rig: $("liveRig"), next: $("liveNext"), legend: $("liveLegend"),
      bar: $("liveBar"), fill: $("liveBarFill"), ticks: $("liveBarTicks"),
      film: $("liveFilm"), stage: $("liveStage"), stageImg: $("liveStageImg"),
      hint: $("liveHint"), tools: $("liveTools"),
      modeBtns: Array.from(document.querySelectorAll("#liveTools [data-mode]")),
      layoutBtns: Array.from(document.querySelectorAll("#liveTools [data-layout]")),
      sounds: $("liveSounds"), close: $("liveClose"), sound: $("liveSound"),
    };
    el.sound.addEventListener("click", () => setOpts({ sound: !S.opts.sound }));
    el.modeBtns.forEach((b) => b.addEventListener("click", () => setOpts({ mode: b.dataset.mode })));
    el.layoutBtns.forEach((b) => b.addEventListener("click", () => setOpts({ layout: b.dataset.layout })));
    el.sounds.addEventListener("change", () => setOpts({ sounds: el.sounds.checked }));
    el.close.addEventListener("click", close);
    el.root.addEventListener("mousemove", showTools);
    // Um botão clicado não fica com o foco: Space continua sendo "disparar", não o botão
    el.root.addEventListener("click", (e) => { const b = e.target.closest("button"); if (b) b.blur(); });
    window.addEventListener("resize", () => { if (S.open) layoutRig(); });
    document.addEventListener("keydown", onKey, true);
  }

  function showTools() {
    if (!el) return;
    el.root.classList.add("tools-visible");
    clearTimeout(S.hideTimer);
    S.hideTimer = setTimeout(() => el.root.classList.remove("tools-visible"), 2500);
  }

  function onKey(e) {
    if (!S.open) return;
    if (e.key === "Escape") {
      e.preventDefault();
      e.stopImmediatePropagation();
      if (S.phase === "countdown" || S.phase === "firing" || S.phase === "receiving") { if (cb.cancel) cb.cancel(); }
      else close();
    } else if (e.key === " ") {
      if (e.target.tagName === "INPUT") return;
      e.preventDefault();
      e.stopImmediatePropagation();
      if ((S.phase === "idle" || S.phase === "done" || S.phase === "cancelled" || S.phase === "failed") && cb.shoot) cb.shoot();
    }
  }

  function setOpts(change, silent) {
    Object.assign(S.opts, change);
    if (!["operator", "audience"].includes(S.opts.mode)) S.opts.mode = "operator";
    if (!["arc", "circle", "line"].includes(S.opts.layout)) S.opts.layout = "arc";
    S.opts.sounds = !!S.opts.sounds;
    S.opts.sound = !!S.opts.sound;
    if (!silent && cb.save) cb.save(Object.assign({}, S.opts));
    if (cb.changed) cb.changed(Object.assign({}, S.opts));
    renderOpts();
    if (el && S.open) layoutRig();
    render();
  }
  function renderOpts() {
    if (!el) return;
    el.root.classList.toggle("audience", S.opts.mode === "audience");
    el.modeBtns.forEach((b) => b.classList.toggle("on", b.dataset.mode === S.opts.mode));
    el.layoutBtns.forEach((b) => b.classList.toggle("on", b.dataset.layout === S.opts.layout));
    el.sounds.checked = S.opts.sounds;
    el.sound.classList.toggle("off", !S.opts.sound);
    el.sound.querySelector("span").textContent = S.opts.sound ? "Sound on" : "Sound off";
  }

  // ---------------------------------------------------------------- câmeras (rig)
  function rigNodes() {
    return Array.from(el.rig.children);
  }
  function buildRig() {
    el.rig.innerHTML = "";
    S.cams.forEach((c) => {
      const d = document.createElement("div");
      d.className = "live-cam";
      d.dataset.name = c.name;
      d.innerHTML = "<span></span>";
      d.firstChild.textContent = c.number || "";
      d.title = c.model ? c.model + " (" + c.name + ")" : c.name;
      el.rig.appendChild(d);
    });
    layoutRig();
  }
  // Posições em % do palco: arco (200° em volta do assunto, embaixo), círculo ou linha
  function layoutRig() {
    const nodes = rigNodes();
    const n = nodes.length;
    if (!n) return;
    const w = el.rig.clientWidth || 1, h = el.rig.clientHeight || 1;
    const size = Math.max(30, Math.min(64, Math.min(w / (n * 1.25), h / 6)));
    el.rig.style.setProperty("--cam-size", size + "px");
    const layout = S.opts.layout;
    nodes.forEach((node, i) => {
      let x, y;
      if (layout === "line") {
        x = n === 1 ? 50 : 8 + (84 * i) / (n - 1);
        y = 50;
      } else if (layout === "circle") {
        const a = -Math.PI / 2 + (2 * Math.PI * i) / n;
        x = 50 + 38 * Math.cos(a) * (h / w);
        y = 56 + 38 * Math.sin(a);
      } else {
        // arco: do canto inferior esquerdo, por cima, ao canto inferior direito
        const start = Math.PI * 1.06, end = -Math.PI * 0.06;
        const a = n === 1 ? Math.PI / 2 : start + ((end - start) * i) / (n - 1);
        x = 50 + 42 * Math.cos(a) * Math.min(1, (h / w) * 1.6);
        y = 62 - 48 * Math.sin(a);
      }
      node.style.left = x + "%";
      node.style.top = y + "%";
    });
    el.rig.classList.toggle("line", layout === "line");
  }

  // ---------------------------------------------------------------- estado do disparo
  function currentGroup() {
    // Último grupo com algum item disparado; -1 antes do primeiro
    const g = S.plan ? S.plan.groups : [];
    let cur = -1;
    g.forEach((grp, i) => { if (grp.idx.some((k) => S.fired.has(k))) cur = i; });
    return cur;
  }
  function nextGroupIndex() {
    const g = S.plan ? S.plan.groups : [];
    for (let i = 0; i < g.length; i++)
      if (!g[i].idx.every((k) => S.fired.has(k))) return i;
    return -1;
  }

  function renderRig() {
    const p = S.plan;
    const firedCams = new Set(), nextCams = new Set();
    if (p && !p.all) {
      p.shots.forEach((s, i) => { if (S.fired.has(i + 1)) firedCams.add(s.cam); });
      const ni = nextGroupIndex();
      if (ni >= 0 && (S.phase === "firing")) p.groups[ni].idx.forEach((k) => nextCams.add(p.shots[k - 1].cam));
    }
    rigNodes().forEach((node) => {
      const name = node.dataset.name;
      const allFired = p && p.all && (S.phase === "receiving" || S.phase === "done");
      node.classList.toggle("on", (firedCams.has(name) || allFired) && !S.failed.has(name) && !S.lost.has(name));
      node.classList.toggle("next", nextCams.has(name) && !firedCams.has(name));
      node.classList.toggle("bad", S.failed.has(name) || S.lost.has(name));
      node.classList.toggle("idle", !p || S.phase === "idle" || S.phase === "countdown");
    });
  }
  function flashCam(name) {
    rigNodes().forEach((node) => {
      if (node.dataset.name !== name) return;
      node.classList.add("cur");
      setTimeout(() => node.classList.remove("cur"), FLASH_MS);
    });
  }

  function renderBar() {
    const p = S.plan;
    if (!p || p.all || !p.shots.length) { el.bar.classList.add("hidden"); return; }
    el.bar.classList.remove("hidden");
    const total = Math.max(1, p.shots[p.shots.length - 1].t);
    if (el.ticks.childElementCount !== p.groups.length) {
      el.ticks.innerHTML = "";
      p.groups.forEach((g) => {
        const i = document.createElement("i");
        i.style.left = (100 * g.t) / total + "%";
        el.ticks.appendChild(i);
      });
    }
    let pct = 0;
    if (S.phase === "firing") pct = Math.min(100, (100 * (performance.now() - S.t0)) / total);
    else if (S.phase === "receiving" || S.phase === "done") pct = 100;
    el.fill.style.width = pct + "%";
  }

  // Texto grande no meio (contagem, "k / n", segundos até o próximo grupo)
  function renderCenter() {
    const p = S.plan;
    const audience = S.opts.mode === "audience";
    const big = (n, sub) => { el.big.textContent = n; el.sub.textContent = sub; el.big.classList.remove("hidden"); el.sub.classList.remove("hidden"); el.st.classList.add("hidden"); };
    const status = (n, t, large) => { el.stN.textContent = n; el.stT.textContent = t; el.st.classList.toggle("large", !!large); el.root.classList.toggle("large-status", !!large); el.st.classList.remove("hidden"); el.big.classList.add("hidden"); el.sub.classList.add("hidden"); };
    const nothing = () => { el.big.classList.add("hidden"); el.sub.classList.add("hidden"); el.st.classList.add("hidden"); };
    el.root.classList.remove("large-status");
    el.next.textContent = "";
    el.next.classList.add("hidden");

    switch (S.phase) {
      case "idle":
        if (audience) nothing();
        else status("", S.cams.length ? "Ready · " + planSummary() : "Waiting for cameras");
        break;
      case "countdown":
        big(String(S.countdown), "Get ready");
        break;
      case "firing": {
        const n = p.shots.length;
        const ni = nextGroupIndex();
        const now = performance.now() - S.t0;
        const nextIn = ni >= 0 ? p.groups[ni].t - now : 0;
        const prev = currentGroup();
        const gap = ni >= 0 ? p.groups[ni].t - (prev >= 0 ? p.groups[prev].t : 0) : 0;
        const nextCams = ni >= 0 ? p.groups[ni].idx.map((k) => p.shots[k - 1].number) : [];
        if (ni >= 0 && gap > NUMERIC_GAP_MS && nextIn > 0) {
          // Intervalo longo: contagem em números (no modo Operator, sem cobrir as câmeras)
          const label = nextCams.length > 1 ? "Next group: cameras " + camList(nextCams) : "Next: camera " + nextCams[0];
          if (audience) big(fmtS(nextIn), "Hold still");
          else status(fmtS(nextIn), label, true);
        } else if (audience) {
          big("", "Hold still");
        } else {
          const k = S.fired.size;
          status(k + " / " + n, k ? "Firing · camera " + camNumber(p.shots[Math.max(...S.fired) - 1].cam) : "Firing");
        }
        if (!audience && ni >= 0 && nextIn > 0 && !(gap > NUMERIC_GAP_MS)) {
          el.next.textContent = (nextCams.length > 1 ? "Next: cameras " + camList(nextCams) : "Next: camera " + nextCams[0]) + " in " + fmtS(nextIn);
          el.next.classList.remove("hidden");
        }
        break;
      }
      case "receiving":
        if (audience) big("", S.play.urls.length ? "" : "Got it!");
        else status(S.received.done + " / " + S.received.total, "Receiving photos");
        break;
      case "done":
        if (audience) nothing();
        else {
          const n = S.received.total || (p ? p.shots.length : 0);
          status(S.received.done + " / " + n + " received", S.play.urls.length ? "Playing the sequence" : (S.lost.size ? "Some photos did not arrive" : "Done"));
        }
        break;
      case "cancelled":
        status("", "Cancelled");
        break;
      case "failed":
        status("", "The session stopped before the end");
        break;
    }
  }

  function renderTop() {
    const p = S.plan;
    const audience = S.opts.mode === "audience";
    let info = "";
    if (!audience) {
      if (S.phase === "done" && S.elapsedMs > 0) info = "Done · " + S.received.done + " photos · " + fmtS(S.elapsedMs);
      else info = planSummary();
    }
    el.topInfo.textContent = info;
    const sessions = p ? p.sessions : S.session.total;
    el.topRight.textContent = !audience && sessions > 1 ? "Session " + S.session.index + " of " + sessions : "";
    el.hint.textContent =
      S.phase === "countdown" || S.phase === "firing" || S.phase === "receiving" ? "Esc · cancel"
      : "Space · " + (S.phase === "done" ? "shoot again" : "bullet time") + "    Esc · close";
    el.legend.classList.toggle("hidden", audience || !p || p.all || S.phase === "idle" || S.phase === "countdown");
  }

  function render() {
    if (!el || !S.open) return;
    renderTop();
    renderCenter();
    renderRig();
    renderBar();
    renderPlayback();
    const showRig = S.opts.mode === "operator" && S.phase !== "countdown" && !S.play.urls.length;
    el.root.classList.toggle("has-rig", showRig);
    el.root.classList.toggle("playing", !!S.play.urls.length);
    // As posições dependem do tamanho da área, que só existe com ela visível
    if (showRig && !S.rigVisible) layoutRig();
    S.rigVisible = showRig;
  }

  function tick() {
    S.raf = 0;
    if (!S.open) return;
    if (S.phase === "firing") {
      renderCenter();
      renderBar();
      S.raf = requestAnimationFrame(tick);
    }
  }
  function startTicking() { if (!S.raf) S.raf = requestAnimationFrame(tick); }

  // ---------------------------------------------------------------- sequência tocando em loop
  function stopPlayback() {
    clearInterval(S.play.timer);
    S.play = { urls: [], i: 0, timer: 0, waiting: false };
    if (el) { el.film.innerHTML = ""; el.stageImg.removeAttribute("src"); }
  }
  function renderPlayback() {
    const urls = S.play.urls;
    el.film.classList.toggle("hidden", !urls.length);
    el.stage.classList.toggle("hidden", !urls.length);
    if (!urls.length) return;
    if (el.film.childElementCount !== urls.length) {
      el.film.innerHTML = "";
      urls.forEach((u, i) => {
        const d = document.createElement("div");
        d.style.backgroundImage = "url(" + u + ")";
        d.title = String(i + 1);
        el.film.appendChild(d);
      });
    }
    Array.from(el.film.children).forEach((d, i) => d.classList.toggle("cur", i === S.play.i));
    const url = urls[S.play.i];
    if (el.stageImg.getAttribute("src") !== url) el.stageImg.src = url;
  }
  // fotos: [{file, order}] da sessão nova; load(file) -> Promise<url>
  function playback(photos, load) {
    if (!S.open || !S.play.waiting) return;
    S.play.waiting = false;
    const sorted = photos.slice().sort((a, b) => a.order - b.order);
    Promise.all(sorted.map((ph) => Promise.resolve(load(ph.file)).catch(() => null))).then((urls) => {
      if (!S.open || S.phase !== "done") return;
      S.play.urls = urls.filter(Boolean);
      S.play.i = 0;
      clearInterval(S.play.timer);
      if (S.play.urls.length > 1)
        S.play.timer = setInterval(() => { S.play.i = (S.play.i + 1) % S.play.urls.length; renderPlayback(); }, 1000 / PLAY_FPS);
      render();
    });
  }

  // ---------------------------------------------------------------- avisos do programa
  function resetShot() {
    S.fired = new Set();
    S.failed = new Set();
    S.lost = new Set();
    S.received = { done: 0, total: 0 };
    S.elapsedMs = 0;
    stopPlayback();
  }
  const handlers = {
    livePlan(e) {
      S.plan = { all: !!e.all, countdown: e.countdown, sessions: e.sessions, shots: e.shots || [], groups: groupsOf(e.shots || []) };
      S.session = { index: 1, total: e.sessions };
      S.phase = "idle";
      resetShot();
      if (el) { el.ticks.innerHTML = ""; }
    },
    sessionStarted(e) {
      S.session = { index: e.index, total: e.total };
      S.phase = "idle";
      resetShot();
      if (el) el.ticks.innerHTML = "";
    },
    countdownStarted(e) { S.phase = "countdown"; S.countdown = e.seconds; },
    countdownTick(e) { S.phase = "countdown"; S.countdown = e.seconds; },
    trigger() {
      S.t0 = performance.now();
      if (S.plan && S.plan.all) {
        S.phase = "receiving";
        S.received.total = S.plan.shots.length;
        if (el) rigNodes().forEach((n) => flashCam(n.dataset.name));
      } else {
        S.phase = "firing";
        startTicking();
      }
    },
    shotFired(e) {
      if (!S.plan || S.plan.all) return;
      S.fired.add(e.index);
      // Acerta o relógio pelo disparo que chegou (o plano diz quando ele devia sair)
      const shot = S.plan.shots[e.index - 1];
      if (shot) S.t0 = performance.now() - shot.t;
      if (e.ok) flashCam(e.camera); else S.failed.add(e.camera);
      if (e.index >= e.total) {
        S.phase = "receiving";
        S.elapsedMs = shot ? shot.t : 0;
        S.received.total = e.total - S.failed.size;
      }
    },
    shotFailed(e) { S.failed.add(e.camera); },
    waitingDownloads() { if (S.phase === "firing" && S.plan && S.plan.all) S.phase = "receiving"; },
    downloadProgress(e) {
      S.received = { done: e.done, total: e.total };
      if (S.phase === "firing" && S.plan && S.plan.all) S.phase = "receiving";
    },
    downloadTimeout(e) { (e.lost || []).forEach((n) => S.lost.add(n)); },
    sessionCompleted() {
      S.phase = "done";
      S.play.waiting = true;
    },
    sessionInterrupted() { S.phase = "failed"; },
    sequenceCancelled() { S.phase = "cancelled"; stopPlayback(); },
    sequenceFinished(e) {
      if (S.phase === "cancelled") return;
      if (!e.completed && S.phase !== "done") S.phase = "failed";
    },
  };

  function handle(event) {
    const h = handlers[event.type];
    if (!h) return;
    h(event);
    render();
  }

  // ---------------------------------------------------------------- API
  function init(callbacks, saved) {
    cb = callbacks || {};
    build();
    let opts = {};
    try { opts = saved ? JSON.parse(saved) : {}; } catch (err) { opts = {}; }
    setOpts(Object.assign({}, DEFAULTS, opts || {}), true);
  }
  function setCameras(list) {
    S.cams = (list || []).filter((c) => c.connected).map((c) => ({ name: c.name, number: c.order, model: c.model, connected: true }))
      .sort((a, b) => a.number - b.number);
    if (el && S.open) { buildRig(); render(); }
  }
  function open() {
    if (!el) return;
    S.open = true;
    el.root.classList.remove("hidden");
    renderOpts();
    buildRig();
    render();
    showTools();
    if (S.phase === "firing") startTicking();
    if (cb.opened) cb.opened();
  }
  function close() {
    if (!el) return;
    S.open = false;
    el.root.classList.add("hidden");
    if (cb.closed) cb.closed();
  }
  function isOpen() { return S.open; }
  function options() { return Object.assign({}, S.opts); }

  return { init, setCameras, open, close, isOpen, handle, playback, options, setOptions: setOpts };
})();
