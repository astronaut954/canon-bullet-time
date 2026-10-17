"""Gera o GIF de demonstração do README (docs/images/stillpoint-demo.gif).

Usa a interface real do app (assets/ui) num navegador, com câmeras e disparo simulados.
As fotos da sessão saem de um vídeo bullet time: cada vista diferente do vídeo vira a
foto de uma câmera, na ordem em que aparecem. Aprovado em 2026-10-14; refeito em
2026-10-17 com o visual da 2.10.0: janela de 1300 px, Timing (Sweep) e a janela Live
(assets/ui/live.html) mostrando a varredura e a sequência no fim.

Requer Python com Pillow e Playwright (Chromium) e o ffmpeg:
    pip install pillow playwright && python -m playwright install chromium
    python scripts/make_demo_gif.py caminho/do/video.mp4 [--no-opening]
"""
import base64
import io
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from PIL import Image, ImageChops, ImageStat
from playwright.sync_api import sync_playwright

REPO = Path(__file__).resolve().parent.parent
UI = REPO / "assets" / "ui"
OUT_GIF = REPO / "docs" / "images" / "stillpoint-demo.gif"
MODELS = [("SL3", "Canon EOS Rebel SL3"), ("T7", "Canon EOS Rebel T7")]


def distinct_views(video, work, max_views=12):
    """Vistas diferentes do vídeo, na ordem em que aparecem (o vídeo pode ir e voltar)."""
    small, full = work / "small", work / "full"
    small.mkdir(); full.mkdir()
    subprocess.run(["ffmpeg", "-v", "error", "-i", str(video), "-vf", "scale=160:160", str(small / "f%04d.png")], check=True)
    subprocess.run(["ffmpeg", "-v", "error", "-i", str(video), "-q:v", "3", str(full / "f%04d.jpg")], check=True)
    chosen = []
    for f in sorted(small.iterdir()):
        im = Image.open(f).convert("L")
        if all(ImageStat.Stat(ImageChops.difference(im, c)).mean[0] > 3 for _, c in chosen):
            chosen.append((f, im))
            if len(chosen) == max_views:
                break
    return [full / (f.stem + ".jpg") for f, _ in chosen]


def data_url(path, size):
    im = Image.open(path).convert("RGB")
    im.thumbnail((size, size))
    buf = io.BytesIO()
    im.save(buf, "JPEG", quality=85)
    return "data:image/jpeg;base64," + base64.b64encode(buf.getvalue()).decode()


def main():
    if len(sys.argv) < 2:
        raise SystemExit("Usage: python scripts/make_demo_gif.py path/to/bullet-time-video.mp4")
    work = Path(tempfile.mkdtemp())
    try:
        build(Path(sys.argv[1]), work)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def build(video, work):
    views = distinct_views(video, work)
    n = len(views)
    CAMS = []
    for i in range(n):
        short, model = MODELS[i % 2]
        CAMS.append((f"{short}_{i // 2 + 1}", model))
    FILES = [f"{i + 1:02d}_{CAMS[i][0]}_IMG_{4821 + i}.JPG" for i in range(n)]
    photos_js = "{" + ",".join(
        f'"{FILES[i]}": {{small: "{data_url(views[i], 320)}", big: "{data_url(views[i], 1000)}"}}' for i in range(n)) + "}"

    css = (UI / "themes" / "nocturne-neo.css").read_text(encoding="utf-8")
    css = re.sub(r"__FONT:([A-Za-z0-9_.\-]+)__",
                 lambda m: "data:font/ttf;base64," + base64.b64encode((UI / "fonts" / m.group(1)).read_bytes()).decode(), css)
    stub = """
window.__photos = %s;
// o relógio do teste controla o Date: a abertura segue o mesmo relógio
performance.now = () => Date.now();
window.uiReady = () => Promise.resolve({});
window.listSessions = () => Promise.resolve(window.__sessions || []);
window.getPhoto = (session, file, size) => Promise.resolve(size > 400 ? window.__photos[file].big : window.__photos[file].small);
for (const n of ['shoot','identify','setPrefix','restartConnections','restartApp','saveSessions','exitApp','takeOrderPhotos','setOrder','discardOrderPhotos','reorderSession','deletePhotos','deleteSession','deleteAllSessions','getOrderPhoto','saveTiming','saveSettings','liveSetSound','openLive','readCameraSettings','checkForUpdate']) window[n] = () => Promise.resolve({});
""" % photos_js
    html = (UI / "index.html").read_text(encoding="utf-8").replace("/*__THEME__*/", css)
    html = re.sub(r"__IMAGE:([A-Za-z0-9_.\-]+)__",
                  lambda m: "data:image/png;base64," + base64.b64encode((UI / "images" / m.group(1)).read_bytes()).decode(), html)
    # --no-opening: o GIF começa direto no app, sem a abertura
    if "--no-opening" in sys.argv:
        html = re.sub(r'<div id="splash".*?</div>\s*</div>\s*</div>', "", html, count=1, flags=re.S)
    html = html.replace("<script>/*__SCRIPT__*/</script>",
                        "<script>" + stub + "</script><script>" + (UI / "app.js").read_text(encoding="utf-8") + "</script>")
    page_file = work / "demo.html"
    page_file.write_text(html, encoding="utf-8")

    # Janela Live (assets/ui/live.html) com as mesmas fotos e funções simuladas
    live_stub = """
performance.now = () => Date.now();
window.__photos = %s;
window.liveReady = () => Promise.resolve({index: 1, opts: '{"mode":"operator","layout":"arc"}', sound: {sound: true, sounds: true},
  screens: [{name: 'Display', primary: true}], cameraEvent: {type: 'cameras', cameras: %s}});
for (const n of ['liveShoot','liveCancel','saveLiveWindow','liveDisplay','liveSetSound','liveClose','liveAnother']) window[n] = () => Promise.resolve(null);
window.getPhoto = (session, file, size) => Promise.resolve(size > 400 ? window.__photos[file].big : window.__photos[file].small);
""" % (photos_js, "[" + ",".join(f'{{name:"{CAMS[i][0]}",model:"{CAMS[i][1]}",connected:true,state:"connected",order:{i + 1},seconds:0,compatible:true}}' for i in range(n)) + "]")
    live_html = (UI / "live.html").read_text(encoding="utf-8").replace("/*__THEME__*/", css).replace("/*__LIVE_CSS__*/", (UI / "live.css").read_text(encoding="utf-8"))
    live_html = live_html.replace("<script>/*__LIVE_SCRIPT__*/</script>", "<script>" + live_stub + "</script><script>" + (UI / "live.js").read_text(encoding="utf-8") + "</script>")
    live_file = work / "live.html"
    live_file.write_text(live_html, encoding="utf-8")
    FR = work / "frames"
    FR.mkdir()

    CURSOR = """
    const c = document.createElement('div'); c.id='demoCursor';
    c.innerHTML = '<svg width="26" height="26" viewBox="0 0 24 24"><path d="M4 2 L4 20 L9 15.5 L12.5 22.5 L15.5 21 L12 14 L19 14 Z" fill="white" stroke="black" stroke-width="1.4" stroke-linejoin="round"/></svg>';
    c.style.cssText='position:fixed;left:900px;top:700px;z-index:99999;pointer-events:none;filter:drop-shadow(0 1px 2px rgba(0,0,0,.6))';
    document.body.appendChild(c);
    """

    
    frames = []  # (path, seconds)
    W, H = 1300, 840  # a largura com que o app abre

    with sync_playwright() as pw:
        b = pw.chromium.launch()
        pg = b.new_page(viewport={"width": W, "height": H})
        # Relógio parado: o tempo da página só anda quando o script manda (run_for)
        pg.clock.install()
        pg.clock.pause_at(int(time.time() * 1000) + 2000)
        pg.goto(page_file.as_uri())

        def shot(sec):
            p = FR / f"{len(frames):04d}.png"
            pg.screenshot(path=str(p))
            frames.append((p, sec))

        # 0. Abertura (chuva e logo), com as câmeras já começando a conectar por trás
        if pg.locator("#splash").count():
            t = 0
            while t < 4600:
                pg.clock.run_for(120)
                t += 120
                if t >= 2300:
                    pg.wait_for_timeout(60)  # o fade do logo anda em tempo real
                if t == 1080:
                    pg.evaluate("c => window.onCoreEvent({type:'cameras', cameras: c})",
                                [{"name": CAMS[0][0], "model": CAMS[0][1], "connected": False, "state": "connecting",
                                  "order": 1, "seconds": 3, "compatible": True}])
                shot(0.12)
            shot(0.5)
            pg.clock.run_for(120)  # começa a sumir
            for _ in range(5):
                pg.wait_for_timeout(140)
                shot(0.12)
            pg.clock.run_for(900)
        pg.evaluate(CURSOR)
        pg.clock.run_for(200)

        def run(ms):
            pg.clock.run_for(ms)

        def cameras(states):
            cams = []
            for i, (name, model) in enumerate(CAMS):
                st, sec = states[i]
                if st is None:
                    continue
                cams.append({"name": name, "model": model, "connected": st == "connected", "state": st,
                             "order": i + 1, "seconds": sec, "compatible": True})
            pg.evaluate("c => window.onCoreEvent({type:'cameras', cameras: c})", cams)

        def move_to(selector, steps=6, total=0.45):
            box = pg.locator(selector).first.bounding_box()
            tx, ty = box["x"] + box["width"] * 0.55, box["y"] + box["height"] * 0.6
            sx, sy = pg.evaluate("() => { const c=document.getElementById('demoCursor'); return [parseFloat(c.style.left), parseFloat(c.style.top)]; }")
            for k in range(1, steps + 1):
                t = k / steps
                t = 1 - (1 - t) ** 3
                pg.evaluate("([x,y]) => { const c=document.getElementById('demoCursor'); c.style.left=x+'px'; c.style.top=y+'px'; }",
                            [sx + (tx - sx) * t, sy + (ty - sy) * t])
                shot(total / steps)

        def press(selector):
            pg.evaluate("s => document.querySelector(s).classList.add('demo-press')", selector)
            pg.add_style_tag(content=".demo-press{filter:brightness(1.35);transform:scale(.98)}")
            shot(0.15)
            pg.evaluate("s => document.querySelector(s).classList.remove('demo-press')", selector)

        # 1. Abrindo
        shot(0.6)
        # 2. Câmeras conectando uma a uma
        states = [(None, 0)] * n
        for i in range(n):
            states = [s if j != i else ("connecting", 3) for j, s in enumerate(states)]
            cameras(states); run(50); shot(0.35)
            states = [s if j != i else ("connected", 0) for j, s in enumerate(states)]
            cameras(states); run(50); shot(0.35)
        shot(0.6)

        # 3. Timing: Advanced → Sweep (a varredura)
        move_to("#timingToggle")
        press("#timingToggle"); pg.click("#timingToggle"); run(50); shot(0.6)
        sweep = "#timingModes button:nth-child(2)"
        move_to(sweep); press(sweep); pg.click(sweep); run(50); shot(0.9)
        move_to("#timingToggle", steps=4, total=0.3); press("#timingToggle"); pg.click("#timingToggle"); run(50); shot(0.6)

        # 4. Contagem de 3 s e disparo (janela principal)
        move_to("#countdown")
        pg.fill("#countdown", "3"); shot(0.5)
        move_to("#shootBtn")
        press("#shootBtn")
        pg.evaluate("window.onCoreEvent({type:'busy', busy:true, action:'shoot'})")
        pg.evaluate("window.onCoreEvent({type:'sessionStarted', index:1, total:1})")
        pg.evaluate("window.onCoreEvent({type:'countdownStarted'})")
        for sec in (3, 2, 1):
            pg.evaluate(f"window.onCoreEvent({{type:'countdownTick', seconds:{sec}}})")
            run(50); shot(0.75)
        pg.evaluate("window.onCoreEvent({type:'trigger'})")
        pg.wait_for_timeout(60); shot(0.12)
        pg.wait_for_timeout(150); shot(0.12)
        run(600); pg.wait_for_timeout(400)
        pg.evaluate("window.onCoreEvent({type:'waitingDownloads'})"); run(50); shot(0.4)
        for d in range(1, n + 1):
            pg.evaluate(f"window.onCoreEvent({{type:'downloadProgress', done:{d}, total:{n}}})"); run(50); shot(0.2)

        # 5. Sessão nova aparece
        session = {"name": "2026-10-14_14-30-05",
                   "photos": [{"file": FILES[i - 1], "order": i, "camera": CAMS[i - 1][0], "previewable": True} for i in range(1, n + 1)]}
        pg.evaluate("s => { window.__sessions=[s]; }", session)
        pg.evaluate("window.onCoreEvent({type:'busy', busy:false, action:''})")
        pg.evaluate("window.onCoreEvent({type:'sequenceFinished', completed:true})")
        pg.evaluate("s => window.onCoreEvent({type:'sessions', sessions:[s]})", session)
        run(100); pg.wait_for_timeout(500); run(100); shot(1.2)

        # 6. Live view: a janela separada com a varredura e a sequência no fim
        move_to("#liveBtn"); press("#liveBtn"); shot(0.3)
        lv = b.new_page(viewport={"width": W, "height": H})
        lv.goto(live_file.as_uri()); lv.wait_for_timeout(500)
        lv.add_style_tag(content=".live-tools, .live-play { transition: none !important; }")
        def lshot(sec):
            # as opções (que aparecem ao abrir e com o mouse) ficam fora do GIF
            lv.evaluate("document.getElementById('live').classList.remove('tools-visible')")
            p = FR / f"{len(frames):04d}.png"
            lv.screenshot(path=str(p))
            frames.append((p, sec))
        def ev(t, **d):
            lv.evaluate("e => window.onCoreEvent(e)", dict(type=t, **d))
        lshot(0.8)
        shots = [{"cam": CAMS[i][0], "number": i + 1, "t": i * 200} for i in range(n)]
        ev("livePlan", countdown=3, sessions=1, all=False, shots=shots)
        ev("sessionStarted", index=1, total=1); ev("countdownStarted", seconds=3)
        for sec in (3, 2, 1):
            ev("countdownTick", seconds=sec); lv.wait_for_timeout(50); lshot(0.75)
        ev("trigger")
        for i in range(n):
            ev("shotFired", camera=CAMS[i][0], index=i + 1, total=n, ok=True)
            lv.wait_for_timeout(40); lshot(0.1)
            lv.wait_for_timeout(120); lshot(0.1)
        for d in range(1, n + 1):
            ev("downloadProgress", done=d, total=n); lv.wait_for_timeout(30); lshot(0.12)
        ev("sessionCompleted")
        lv.evaluate("s => window.onCoreEvent({type:'sessions', sessions:[s]})", session)
        lv.wait_for_timeout(900)
        for k in range(n * 3):
            lv.wait_for_timeout(220); lshot(0.22)
        b.close()

    # GIF: ffmpeg concat com durações, paleta própria
    lst = work / "frames.txt"
    with open(lst, "w") as f:
        for p, sec in frames:
            f.write(f"file '{p}'\nduration {sec:.3f}\n")
        f.write(f"file '{frames[-1][0]}'\n")
    out = OUT_GIF
    out.parent.mkdir(parents=True, exist_ok=True)
    vf = "fps=15,scale=960:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "concat", "-safe", "0", "-i", str(lst), "-vf", vf, "-loop", "0", str(out)], check=True)
    print(out, out.stat().st_size // 1024, "KB", len(frames), "frames", round(sum(s for _, s in frames), 1), "s")


if __name__ == "__main__":
    main()
