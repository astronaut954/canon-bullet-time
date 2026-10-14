"""Gera o ícone do app (câmera de frente com o projétil dentro do vidro da lente).

Saída em resources/icon/:
    app-icon.svg        desenho original (1024 x 1024, grade de ícone do Mac)
    app-icon-1024.png   prévia
    app-icon.icns       ícone do app de Mac (Contents/Resources do .app)
    app-icon.ico        ícone do programa de Windows (embutido no .exe)

Os arquivos gerados ficam no Git; este script só precisa rodar de novo quando o
desenho mudar. Requer Python com cairosvg e Pillow:
    pip install cairosvg pillow
    python scripts/make_icon.py
"""
import io
import math
import random
from pathlib import Path

import cairosvg
from PIL import Image

OUT = Path(__file__).resolve().parent.parent / "resources" / "icon"

# Paleta da interface (assets/ui/themes/nocturne-neo.css, --neo-*)
G1, G2, G3, G4, G5 = "#00ff41", "#00cc33", "#009926", "#006619", "#003d0f"
BLACK = "#0a0a0a"

# Fundo arredondado: no Mac, 824 px de 1024 com cantos de 185 (grade da Apple).
# No Windows o ícone ocupa o quadro todo (sem a margem do Mac).
MAC_VIEWBOX = "0 0 1024 1024"
WIN_VIEWBOX = "96 96 832 832"


def svg(inner, viewbox):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="{viewbox}">
<defs>
  <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#021a06"/><stop offset="1" stop-color="{BLACK}"/>
  </linearGradient>
  <linearGradient id="body" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#0f4f1f"/><stop offset=".5" stop-color="#083016"/><stop offset="1" stop-color="#041a0b"/>
  </linearGradient>
  <linearGradient id="barrel" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#16201a"/><stop offset="1" stop-color="#050806"/>
  </linearGradient>
  <radialGradient id="glass" cx="0.42" cy="0.38" r="0.7">
    <stop offset="0" stop-color="#0a4a1c"/><stop offset=".55" stop-color="#021a08"/><stop offset="1" stop-color="#000"/>
  </radialGradient>
  <radialGradient id="glow" cx="0.5" cy="0.5" r="0.5">
    <stop offset="0" stop-color="{G1}" stop-opacity="0.30"/><stop offset="1" stop-color="{G1}" stop-opacity="0"/>
  </radialGradient>
  <clipPath id="clip"><rect x="100" y="100" width="824" height="824" rx="185"/></clipPath>
</defs>
<rect x="100" y="100" width="824" height="824" rx="185" fill="url(#bg)"/>
<g clip-path="url(#clip)">{inner}</g>
<rect x="101.5" y="101.5" width="821" height="821" rx="184" fill="none" stroke="{G4}" stroke-width="3"/>
</svg>'''


def bullet(cx, cy, s):
    """Projétil apontando para a direita, centrado em (cx, cy)."""
    p = (f"M {-130*s} {-58*s} L {35*s} {-58*s} C {105*s} {-58*s} {158*s} {-24*s} {178*s} 0 "
         f"C {158*s} {24*s} {105*s} {58*s} {35*s} {58*s} L {-130*s} {58*s} Z")
    out = f'<g transform="translate({cx} {cy})">'
    for w, o in ((60, .06), (36, .10), (18, .20)):
        out += f'<path d="{p}" fill="none" stroke="{G1}" stroke-width="{w*s}" stroke-opacity="{o}" stroke-linejoin="round"/>'
    out += f'<path d="{p}" fill="{G1}"/>'
    out += f'<rect x="{-80*s}" y="{-58*s}" width="{12*s}" height="{116*s}" fill="{G5}" opacity=".6"/>'
    out += f'<path d="M {-118*s} {-32*s} L {70*s} {-32*s}" stroke="#c8ffd6" stroke-width="{9*s}" stroke-linecap="round" opacity=".55"/>'
    return out + '</g>'


def waves(cx, cy, s, n=3, width=10):
    """Ondas de ar atrás do projétil (a cena do filme)."""
    out = f'<g transform="translate({cx} {cy})">'
    for i in range(n):
        x = -170 * s - i * 62 * s
        ry = (70 + i * 26) * s
        out += (f'<ellipse cx="{x:.1f}" cy="0" rx="{ry*0.26:.1f}" ry="{ry:.1f}" fill="none" '
                f'stroke="{G1}" stroke-width="{width*s:.1f}" opacity="{.85 - i*.25:.2f}"/>')
    return out + '</g>'


def code_rain(cx, cy, r, seed=5, opacity=.55):
    """Chuva de código, só dentro do vidro."""
    random.seed(seed)
    step, cw, ch = max(18, int(r * 0.16)), max(6, int(r * 0.06)), max(10, int(r * 0.10))
    out, x = '', cx - r
    while x < cx + r:
        n = random.randint(4, 9)
        y = random.randint(int(cy - r - n * (ch + 8) * 0.6), int(cy + r * 0.35))
        for k in range(n):
            c = G3 if k == n - 1 else G4
            out += (f'<rect x="{x:.0f}" y="{y + k*(ch+8):.0f}" width="{cw}" height="{ch}" '
                    f'fill="{c}" opacity="{opacity*(k+1)/n:.2f}"/>')
        x += step
    return out


def lens(cx, cy, r, bullet_s, detail):
    g = r * 0.70  # raio do vidro
    out = f'<circle cx="{cx}" cy="{cy+14}" r="{r+6}" fill="#000" opacity=".45"/>'
    out += f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="url(#barrel)" stroke="{G3}" stroke-width="{r*(0.025 if detail else 0.05):.1f}"/>'
    rf = r * 0.90
    out += f'<circle cx="{cx}" cy="{cy}" r="{rf:.1f}" fill="#0b120d" stroke="{G4}" stroke-width="{r*0.012:.1f}"/>'
    if detail:  # serrilhado do anel de foco
        for i in range(72):
            a = 2 * math.pi * i / 72
            x1, y1 = cx + (rf - r*0.07) * math.cos(a), cy + (rf - r*0.07) * math.sin(a)
            x2, y2 = cx + (rf - r*0.01) * math.cos(a), cy + (rf - r*0.01) * math.sin(a)
            out += f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{G5}" stroke-width="{r*0.018:.1f}"/>'
    out += f'<circle cx="{cx}" cy="{cy}" r="{r*0.78:.1f}" fill="#060a07" stroke="{G2}" stroke-width="{r*(0.022 if detail else 0.05):.1f}"/>'
    out += f'<circle cx="{cx}" cy="{cy}" r="{r*0.74:.1f}" fill="none" stroke="{G5}" stroke-width="{r*0.02:.1f}"/>'
    out += f'<clipPath id="glassClip"><circle cx="{cx}" cy="{cy}" r="{g:.1f}"/></clipPath>'
    out += f'<circle cx="{cx}" cy="{cy}" r="{g:.1f}" fill="url(#glass)"/>'
    out += '<g clip-path="url(#glassClip)">'
    if detail:
        out += code_rain(cx, cy, int(g))
    out += f'<circle cx="{cx}" cy="{cy}" r="{g*0.9:.1f}" fill="url(#glow)"/>'
    bx = cx + (24 if detail else 10)
    if detail:
        out += waves(bx, cy, bullet_s)
    out += bullet(bx, cy, bullet_s)
    if detail:  # reflexos do vidro
        out += (f'<path d="M {cx - g*0.78:.1f} {cy - g*0.18:.1f} A {g*0.8:.1f} {g*0.8:.1f} 0 0 1 {cx - g*0.18:.1f} {cy - g*0.78:.1f}" '
                f'fill="none" stroke="#d9ffe4" stroke-width="{g*0.07:.1f}" stroke-linecap="round" opacity=".28"/>')
        out += f'<circle cx="{cx + g*0.45:.1f}" cy="{cy + g*0.5:.1f}" r="{g*0.05:.1f}" fill="#d9ffe4" opacity=".25"/>'
    out += '</g>'
    out += f'<circle cx="{cx}" cy="{cy}" r="{g:.1f}" fill="none" stroke="#000" stroke-width="{r*0.03:.1f}" opacity=".7"/>'
    return out


def camera(detail=True):
    """detail=False: versão simplificada para 16-48 px (sem chuva, ondas e serrilhado)."""
    sw = 5 if detail else 12
    inner = (f'<path d="M 382 250 L 418 168 Q 424 152 440 152 L 584 152 Q 600 152 606 168 L 642 250 Z" '
             f'fill="url(#body)" stroke="{G3}" stroke-width="{sw}"/>')
    inner += f'<rect x="40" y="240" width="944" height="900" rx="80" fill="url(#body)" stroke="{G3}" stroke-width="{sw + 1}"/>'
    inner += f'<path d="M 120 244 L 904 244" stroke="{G3}" stroke-width="{sw}" opacity=".7"/>'
    inner += f'<rect x="170" y="200" width="110" height="44" rx="12" fill="#0b3a17" stroke="{G4}" stroke-width="{sw}"/>'
    inner += f'<rect x="196" y="180" width="58" height="26" rx="10" fill="{G2}"/>'
    inner += f'<circle cx="806" cy="312" r="{18 if detail else 26}" fill="{G1}"/>'
    if detail:
        inner += f'<circle cx="806" cy="312" r="34" fill="{G1}" opacity=".18"/>'
    inner += lens(512, 584, 306, 0.66 if detail else 0.80, detail)
    return inner


def render(svg_text, size):
    png = cairosvg.svg2png(bytestring=svg_text.encode(), output_width=size, output_height=size)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    mac_full = svg(camera(True), MAC_VIEWBOX)
    mac_small = svg(camera(False), MAC_VIEWBOX)
    win_full = svg(camera(True), WIN_VIEWBOX)
    win_small = svg(camera(False), WIN_VIEWBOX)

    (OUT / "app-icon.svg").write_text(mac_full, encoding="utf-8")
    render(mac_full, 1024).save(OUT / "app-icon-1024.png")

    # Mac: tamanhos de 16 a 1024; até 32 px usa a versão simplificada
    mac = {s: render(mac_full if s > 32 else mac_small, s) for s in (16, 32, 64, 128, 256, 512, 1024)}
    mac[1024].save(OUT / "app-icon.icns", append_images=[mac[s] for s in (16, 32, 64, 128, 256, 512)])

    # Windows: tamanhos de 16 a 256; até 48 px usa a versão simplificada
    win_sizes = (16, 20, 24, 32, 40, 48, 64, 128, 256)
    win = [render(win_full if s > 48 else win_small, s) for s in win_sizes]
    win[-1].save(OUT / "app-icon.ico", sizes=[(s, s) for s in win_sizes], append_images=win[:-1])

    for name in ("app-icon.svg", "app-icon-1024.png", "app-icon.icns", "app-icon.ico"):
        print(f"[WRITE] {OUT / name} ({(OUT / name).stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
