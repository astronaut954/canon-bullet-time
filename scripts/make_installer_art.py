"""Gera as imagens e o Read Me dos instaladores (visual "chuva de código", aprovado em 2026-10-14).

Saída em resources/installer/:
    dmg-background.png, dmg-background@2x.png   fundo da janela do .dmg (Mac)
    Read Me.pdf                                 instruções dentro do .dmg (fonte: readme-mac.html)
    wizard-image-*.png                          imagem lateral do instalador de Windows (boas-vindas e fim)
    wizard-small-*.png                          imagem pequena do instalador de Windows (demais telas)

Os arquivos gerados ficam no Git; este script só precisa rodar de novo quando o desenho mudar.
Requer Python com Pillow e Playwright (Chromium) e uma fonte com katakana (Noto Sans CJK no
Linux, Hiragino no Mac):
    pip install pillow playwright && python -m playwright install chromium
    python scripts/make_installer_art.py

As posições dos ícones no .dmg (APP_POS, APPS_POS, README_POS, ICON_SIZE) precisam bater com
scripts/build_release.py (DMG_LAYOUT).
"""
import random
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

REPO = Path(__file__).resolve().parent.parent
FONTS = REPO / "assets" / "ui" / "fonts"
ICON = REPO / "resources" / "icon" / "app-icon-1024.png"
OUT = REPO / "resources" / "installer"

CJK_CANDIDATES = [
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Medium.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/System/Library/Fonts/ヒラギノ角ゴシック W6.ttc",
    "/System/Library/Fonts/Hiragino Sans GB.ttc",
]

# Paleta da interface (assets/ui/themes/nocturne-neo.css, --neo-*)
BG = (10, 10, 10)
G1, G2, G3, G4, G5 = (0, 255, 65), (0, 204, 51), (0, 153, 38), (0, 102, 25), (0, 61, 15)
HEAD = (200, 255, 214)
GLYPHS = [chr(c) for c in range(0xFF66, 0xFF9E)] + list("0123456789") + list("Z:=*+-<>¦")

# Janela do .dmg, em pontos (o arquivo @2x tem o dobro). A altura inclui a barra de título
# (~32 pt) e, se a pessoa usa, a barra de caminho do Finder (~28 pt): com 560, o Read Me
# aparece inteiro nos dois casos (medido no macOS 26 em 2026-10-14).
DMG_W, DMG_H = 660, 560
APP_POS, APPS_POS, README_POS = (170, 190), (490, 190), (330, 392)
ICON_SIZE = 96

# Instalador de Windows (Inno Setup): proporção 164:314 e área quadrada; vários tamanhos,
# o instalador escolhe o melhor para a escala da tela (100% a 250%)
WIZARD_SIZES = [(202, 386), (303, 579), (404, 772), (534, 1022)]
WIZARD_SMALL_SIZES = [58, 87, 116, 159]


def cjk_font_path():
    for p in CJK_CANDIDATES:
        if Path(p).exists():
            return p
    raise SystemExit("[ERROR] No font with katakana found (install Noto Sans CJK).")


def font(name, size):
    return ImageFont.truetype(str(FONTS / name), size)


def rain_layer(w, h, cell, seed, density, dim=1.0, heads=True):
    """Uma camada de colunas de caracteres caindo (cabeça clara com brilho, rastro que apaga)."""
    rnd = random.Random(seed)
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d, gd = ImageDraw.Draw(layer), ImageDraw.Draw(glow)
    f = ImageFont.truetype(cjk_font_path(), max(6, int(cell * 0.9)), index=0)
    step = int(cell * 1.05)

    def mix(a, b, t):
        return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

    for c in range(int(w / cell) + 1):
        if rnd.random() > density:
            continue
        x = c * cell + rnd.randint(-1, 1)
        for _ in range(rnd.choice((1, 1, 2))):
            length = rnd.randint(10, 30)
            head_y = rnd.randint(int(h * 0.1), h + step * 8)
            for k in range(length):
                y = head_y - k * step
                if y < -step or y > h + step:
                    continue
                t = k / length
                if k == 0 and heads:
                    g = rnd.choice(GLYPHS)
                    gd.text((x, y), g, font=f, fill=G1 + (int(255 * dim),))
                    d.text((x, y), g, font=f, fill=HEAD + (int(255 * dim),))
                    continue
                col = mix(G1, G4, min(1, t * 1.2))
                d.text((x, y), rnd.choice(GLYPHS), font=f, fill=col + (int(250 * (1 - t) ** 1.4 * dim),))
    out = glow.filter(ImageFilter.GaussianBlur(cell * 0.35))
    out.alpha_composite(layer)
    return out


def matrix_background(w, h, seed=11, scale=1.0, calm=None):
    """Fundo: 3 camadas de chuva (longe desfocada, meio, perto) + vinheta.
    calm: (cx, cy, rx, ry) onde a chuva fica mais fraca (atrás de ícones e textos)."""
    img = Image.new("RGBA", (w, h), BG + (255,))
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((w * 0.1, h * 0.05, w * 0.9, h * 1.1), fill=(0, 60, 18, 90))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(w * 0.12)))
    far = rain_layer(w, h, int(13 * scale), seed, 0.9, dim=0.4, heads=False).filter(ImageFilter.GaussianBlur(1.0 * scale))
    mid = rain_layer(w, h, int(19 * scale), seed + 1, 0.6, dim=0.7)
    near = rain_layer(w, h, int(27 * scale), seed + 2, 0.25, dim=0.85).filter(ImageFilter.GaussianBlur(0.5 * scale))
    rain = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for layer in (far, mid, near):
        rain.alpha_composite(layer)
    if calm:
        mask = Image.new("L", (w, h), 255)
        md = ImageDraw.Draw(mask)
        for cx, cy, rx, ry in calm:
            md.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=95)
        mask = mask.filter(ImageFilter.GaussianBlur(40 * scale))
        rain.putalpha(Image.composite(rain.getchannel("A"), Image.new("L", (w, h), 0), mask))
    img.alpha_composite(rain)
    vig = Image.new("L", (w, h), 170)
    ImageDraw.Draw(vig).ellipse((-w * 0.15, -h * 0.25, w * 1.15, h * 1.25), fill=0)
    img.paste((0, 0, 0, 255), (0, 0), vig.filter(ImageFilter.GaussianBlur(w * 0.08)))
    return img


def glow_text(img, xy, text, f, fill, radius=6, anchor="mm"):
    """Texto com brilho verde e uma sombra escura macia atrás (legível sobre a chuva)."""
    box = ImageDraw.Draw(img).textbbox(xy, text, font=f, anchor=anchor)
    pad = (box[3] - box[1]) * 0.6
    shade = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(shade).rounded_rectangle((box[0] - pad, box[1] - pad * 0.6, box[2] + pad, box[3] + pad * 0.6),
                                            radius=pad, fill=(0, 0, 0, 185))
    img.alpha_composite(shade.filter(ImageFilter.GaussianBlur(pad * 0.7)))
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).text(xy, text, font=f, fill=G1 + (150,), anchor=anchor)
    img.alpha_composite(layer.filter(ImageFilter.GaussianBlur(radius)))
    ImageDraw.Draw(img).text(xy, text, font=f, fill=fill, anchor=anchor)


def dmg_background(s):
    w, h = DMG_W * s, DMG_H * s
    calm = [(p[0] * s, p[1] * s, 95 * s, 95 * s) for p in (APP_POS, APPS_POS, README_POS)]
    calm += [(330 * s, 58 * s, 300 * s, 40 * s), (330 * s, 312 * s, 280 * s, 26 * s)]
    img = matrix_background(w, h, seed=21, scale=s, calm=calm)
    d = ImageDraw.Draw(img)
    glow_text(img, (330 * s, 58 * s), "Drag Stillpoint to Applications",
              font("Rajdhani-SemiBold.ttf", 30 * s), (225, 255, 232), radius=10 * s)
    # Seta
    x0, x1, y = 250 * s, 410 * s, APP_POS[1] * s
    arrow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ad = ImageDraw.Draw(arrow)
    shaft_h, head_w, head_h = 16 * s, 40 * s, 48 * s
    ad.rounded_rectangle((x0, y - shaft_h // 2, x1 - head_w + 4 * s, y + shaft_h // 2), radius=6 * s, fill=G1 + (255,))
    ad.polygon([(x1 - head_w, y - head_h // 2), (x1, y), (x1 - head_w, y + head_h // 2)], fill=G1 + (255,))
    img.alpha_composite(arrow.filter(ImageFilter.GaussianBlur(9 * s)))
    img.alpha_composite(arrow)
    # Placas sob os nomes dos ícones: o Finder escreve os nomes em preto (Mac claro) ou
    # branco (Mac escuro); este verde médio deixa os dois legíveis
    for (cx, cy) in (APP_POS, APPS_POS, README_POS):
        ly = cy + ICON_SIZE // 2 + 14
        d.rounded_rectangle(((cx - 62) * s, (ly - 11) * s, (cx + 62) * s, (ly + 11) * s), radius=11 * s, fill=(46, 128, 70, 235))
    glow_text(img, (330 * s, 312 * s), "First time? Open “Read Me” to see how to open Stillpoint.",
              font("Rajdhani-SemiBold.ttf", 17 * s), (180, 255, 200), radius=6 * s)
    return img.convert("RGB")


def wizard_image(w, h):
    s = w / 164
    img = matrix_background(w, h, seed=33, scale=s * 0.8,
                            calm=[(w // 2, int(h * 0.36), int(w * 0.5), int(w * 0.5)),
                                  (w // 2, int(h * 0.62), int(w * 0.55), int(w * 0.18))])
    ic = int(w * 0.62)
    icon = Image.open(ICON).convert("RGBA").resize((ic, ic), Image.LANCZOS)
    img.alpha_composite(icon, ((w - ic) // 2, int(h * 0.36) - ic // 2))
    glow_text(img, (w // 2, int(h * 0.60)), "STILLPOINT", font("Rajdhani-Bold.ttf", int(30 * s)), (225, 255, 232), radius=max(1, int(7 * s)))
    glow_text(img, (w // 2, int(h * 0.655)), "BULLET TIME PHOTOGRAPHY", font("Rajdhani-SemiBold.ttf", max(6, int(10.5 * s))),
              G2, radius=max(1, int(3 * s)))
    return img.convert("RGB")


def wizard_small(size):
    img = matrix_background(size, size, seed=5, scale=size / 55 * 0.7)
    ic = int(size * 0.92)
    icon = Image.open(ICON).convert("RGBA").resize((ic, ic), Image.LANCZOS)
    img.alpha_composite(icon, ((size - ic) // 2, (size - ic) // 2))
    return img.convert("RGB")


def readme_pdf():
    from playwright.sync_api import sync_playwright
    with tempfile.TemporaryDirectory() as tmp:
        header = Path(tmp) / "header.png"
        matrix_background(1680, 440, seed=44, scale=2, calm=[(840, 220, 700, 150)]).convert("RGB").save(header)
        html = (OUT / "readme-mac.html").read_text(encoding="utf-8")
        html = html.replace("{{REPO}}", REPO.as_uri()).replace("{{HEADER}}", header.as_uri())
        page_file = Path(tmp) / "readme.html"
        page_file.write_text(html, encoding="utf-8")
        with sync_playwright() as pw:
            browser = pw.chromium.launch()
            page = browser.new_page()
            page.goto(page_file.as_uri())
            page.wait_for_timeout(500)
            page.pdf(path=str(OUT / "Read Me.pdf"), format="A4", print_background=True,
                     margin={"top": "0", "bottom": "0", "left": "0", "right": "0"})
            browser.close()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    dmg_background(1).save(OUT / "dmg-background.png")
    dmg_background(2).save(OUT / "dmg-background@2x.png")
    for w, h in WIZARD_SIZES:
        wizard_image(w, h).save(OUT / f"wizard-image-{w}x{h}.png")
    for s in WIZARD_SMALL_SIZES:
        wizard_small(s).save(OUT / f"wizard-small-{s}.png")
    readme_pdf()
    for p in sorted(OUT.iterdir()):
        print(f"[WRITE] {p.relative_to(REPO)} ({p.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
