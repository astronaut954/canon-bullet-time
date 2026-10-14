"""Gera as imagens do README do GitHub: cabeçalho e faixas de título das seções
(mesmo visual do instalador e do app; aprovado em 2026-10-14).

Saída em docs/images/. Requer o mesmo que scripts/make_installer_art.py (Pillow e uma
fonte com katakana).
    python scripts/make_readme_art.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

import make_installer_art as art

OUT = art.REPO / "docs" / "images"

# Seções do README (## ...), na ordem em que aparecem
SECTIONS = ["Download", "Requirements", "SDK Version", "How to Use", "Troubleshooting",
            "Compatibility", "Notes", "For Developers", "License"]


def rounded(img, radius):
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, img.width, img.height), radius=radius, fill=255)
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.paste(img, (0, 0), mask)
    return out


def banner(w=1600, h=520):
    img = art.matrix_background(w, h, seed=44, scale=2, calm=[(w // 2, h // 2, int(w * 0.42), int(h * 0.42))])
    ic = 300
    icon = Image.open(art.ICON).convert("RGBA").resize((ic, ic), Image.LANCZOS)
    x0 = 330
    img.alpha_composite(icon, (x0, (h - ic) // 2))
    tx = x0 + ic + 50
    art.glow_text(img, (tx, 205), "STILLPOINT", art.font("Rajdhani-Bold.ttf", 124), (225, 255, 232), radius=14, anchor="lm")
    art.glow_text(img, (tx + 4, 290), "BULLET TIME PHOTOGRAPHY", art.font("Rajdhani-SemiBold.ttf", 42), art.G1, radius=6, anchor="lm")
    art.glow_text(img, (tx + 4, 350), "Fire every camera at the same instant.", art.font("Rajdhani-SemiBold.ttf", 34),
                  (150, 220, 170), radius=4, anchor="lm")
    return rounded(img, 28)


def section(num, title, w=1600, h=150):
    img = art.matrix_background(w, h, seed=100 + num, scale=1.4, calm=[(w * 0.3, h // 2, w * 0.35, h)])
    img.alpha_composite(Image.new("RGBA", (w, h), (0, 0, 0, 120)))
    d = ImageDraw.Draw(img)
    g = art.G1 + (255,)
    # Trilho verde com marcas em cima e embaixo, como os painéis do app
    d.rectangle((0, 0, 6, h), fill=g)
    d.rectangle((0, 0, 32, 6), fill=g)
    d.rectangle((0, h - 6, 32, h), fill=g)
    mono = ImageFont.truetype(str(art.FONTS / "JetBrainsMono-Variable.ttf"), 30)
    box = (60, h // 2 - 26, 132, h // 2 + 26)
    d.rectangle(box, fill=(0, 102, 25, 255))
    d.text(((box[0] + box[2]) // 2, h // 2), f"{num:02d}", font=mono, fill=(225, 255, 232), anchor="mm")
    art.glow_text(img, (164, h // 2 + 2), title.upper(), art.font("Rajdhani-Bold.ttf", 62), (225, 255, 232), radius=8, anchor="lm")
    d.rectangle((0, h - 2, w, h), fill=(0, 102, 25, 255))
    return rounded(img, 14)


def slug(title):
    return title.lower().replace(" ", "-")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    banner().save(OUT / "readme-banner.png", optimize=True)
    for i, title in enumerate(SECTIONS, 1):
        section(i, title).save(OUT / f"section-{slug(title)}.png", optimize=True)
    for p in sorted(OUT.iterdir()):
        print(f"[WRITE] {p.relative_to(art.REPO)} ({p.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
