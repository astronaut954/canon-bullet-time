"""Gera a cartela de cores do Stillpoint (assets/chart/stillpoint-chart.pdf).

PDF vetorial, A4 deitado. As medidas (em mm, a partir do canto de cima à esquerda) são
as mesmas de src/gui/chart.cpp: mudando aqui, mudar lá também.

    python3 scripts/make_chart.py
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "assets" / "chart" / "stillpoint-chart.pdf"
PAGE_W, PAGE_H = 297.0, 210.0
PT = 72 / 25.4

# Marcas dos cantos (quadrado preto, anel branco, centro preto) e a bolinha de orientação
MARK = 22.0
MARKS = [(20.0, 20.0), (255.0, 20.0), (20.0, 156.0), (255.0, 156.0)]  # canto de cima à esquerda de cada marca
DOT = (48.0, 23.0, 3.0)  # centro x, y e raio
# Amostras
GX0, GX1, GAP = 56.0, 241.0, 4.0
WEDGE_Y0, WEDGE_Y1 = 24.0, 80.0
WEDGE_L = [96, 88, 80, 71, 62, 53, 44, 35, 27, 19, 12]  # claridade (L*) de cada tom
GRID_Y0, GRID_Y1 = 90.0, 174.0
MID_L = 50
COLORS = [  # 16 cores (sRGB), 8 de cada lado do cinza médio; as duas primeiras são tons de pele
    (115, 82, 68), (194, 150, 130), (98, 122, 157), (87, 108, 67),
    (214, 126, 44), (80, 91, 166), (193, 90, 99), (94, 60, 108),
    (157, 188, 64), (224, 163, 46), (56, 61, 150), (70, 148, 73),
    (175, 54, 60), (231, 199, 31), (187, 86, 149), (8, 133, 161),
]


def srgb_from_lstar(L):
    f = (L + 16) / 116
    y = f ** 3 if f > 0.206893 else (f - 16 / 116) / 7.787
    c = 12.92 * y if y <= 0.0031308 else 1.055 * y ** (1 / 2.4) - 0.055
    return round(max(0.0, min(1.0, c)) * 255)


def build():
    ops = []

    def rect(x, y, w, h, rgb):
        r, g, b = (v / 255 for v in rgb)
        ops.append(f"{r:.4f} {g:.4f} {b:.4f} rg {x * PT:.2f} {(PAGE_H - y - h) * PT:.2f} {w * PT:.2f} {h * PT:.2f} re f")

    def circle(cx, cy, rad, rgb):
        r, g, b = (v / 255 for v in rgb)
        k = 0.5523 * rad
        X, Y = cx * PT, (PAGE_H - cy) * PT
        R, K = rad * PT, k * PT
        ops.append(f"{r:.4f} {g:.4f} {b:.4f} rg {X + R:.2f} {Y:.2f} m "
                   f"{X + R:.2f} {Y + K:.2f} {X + K:.2f} {Y + R:.2f} {X:.2f} {Y + R:.2f} c "
                   f"{X - K:.2f} {Y + R:.2f} {X - R:.2f} {Y + K:.2f} {X - R:.2f} {Y:.2f} c "
                   f"{X - R:.2f} {Y - K:.2f} {X - K:.2f} {Y - R:.2f} {X:.2f} {Y - R:.2f} c "
                   f"{X + K:.2f} {Y - R:.2f} {X + R:.2f} {Y - K:.2f} {X + R:.2f} {Y:.2f} c f")

    def text(x, y, size, s, font="F1", gray=0):
        s = s.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")
        ops.append(f"BT /{font} {size:.1f} Tf {gray:.2f} g {x * PT:.2f} {(PAGE_H - y) * PT:.2f} Td ({s}) Tj ET")

    black, white = (0, 0, 0), (255, 255, 255)
    for x, y in MARKS:
        rect(x, y, MARK, MARK, black)
        rect(x + MARK / 6, y + MARK / 6, MARK * 2 / 3, MARK * 2 / 3, white)
        rect(x + MARK / 3, y + MARK / 3, MARK / 3, MARK / 3, black)
    circle(*DOT, black)

    n = len(WEDGE_L)
    pw = (GX1 - GX0 - GAP * (n - 1)) / n
    for i, L in enumerate(WEDGE_L):
        v = srgb_from_lstar(L)
        rect(GX0 + i * (pw + GAP), WEDGE_Y0, pw, WEDGE_Y1 - WEDGE_Y0, (v, v, v))

    cw = (GX1 - GX0 - GAP * 9) / 10
    ch = (GRID_Y1 - GRID_Y0 - GAP) / 2
    for k, rgb in enumerate(COLORS):
        col = (k % 4) + (0 if k < 8 else 6)
        row = (k % 8) // 4
        rect(GX0 + col * (cw + GAP), GRID_Y0 + row * (ch + GAP), cw, ch, rgb)
    g = srgb_from_lstar(MID_L)
    rect(GX0 + 4 * (cw + GAP), GRID_Y0, 2 * cw + GAP, GRID_Y1 - GRID_Y0, (g, g, g))

    text(20, 188, 13, "STILLPOINT  -  CAMERA CHART v1", "F2")
    text(20, 195, 8.5, "Print at 100% (actual size, no scaling) on matte paper. Every camera must see the whole chart,", gray=0.25)
    text(20, 200, 8.5, "including the four corner marks, under even light. In Stillpoint: Camera settings > Calibrate with chart.", gray=0.25)

    content = "\n".join(ops).encode("latin-1")
    objs = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        (f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {PAGE_W * PT:.2f} {PAGE_H * PT:.2f}] "
         f"/Resources << /Font << /F1 4 0 R /F2 5 0 R >> >> /Contents 6 0 R >>").encode(),
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>",
        b"<< /Length " + str(len(content)).encode() + b" >>\nstream\n" + content + b"\nendstream",
    ]
    out = bytearray(b"%PDF-1.4\n")
    offsets = []
    for i, body in enumerate(objs, 1):
        offsets.append(len(out))
        out += f"{i} 0 obj\n".encode() + body + b"\nendobj\n"
    xref = len(out)
    out += f"xref\n0 {len(objs) + 1}\n0000000000 65535 f \n".encode()
    for o in offsets:
        out += f"{o:010d} 00000 n \n".encode()
    out += f"trailer\n<< /Size {len(objs) + 1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(bytes(out))
    print(f"[OK] {OUT}")


if __name__ == "__main__":
    build()
