"""
Gera src/core/compatibility_list.inc a partir do PDF de compatibilidade da Canon
(docs/CDC_EDSDK_Compat_List.pdf), usando a coluna da versão do SDK indicada.

Uso:
    python3 scripts/update_compat_list.py "v13.20.0"

Requer: pip install pdfplumber
No PDF, o símbolo de "compatível" e "não compatível" é um caractere da fonte
Webdings (\\uf061 = ✓, \\uf072 = ✗).
"""

import sys
from pathlib import Path

import pdfplumber

REPO_ROOT = Path(__file__).resolve().parent.parent
PDF = REPO_ROOT / "docs" / "CDC_EDSDK_Compat_List.pdf"
OUT = REPO_ROOT / "src" / "core" / "compatibility_list.inc"
CHECK = ""


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    version = sys.argv[1]

    page = pdfplumber.open(PDF).pages[0]
    words = page.extract_words(keep_blank_chars=True, x_tolerance=1.5)

    headers = [w for w in words if version in w["text"]]
    if not headers:
        raise SystemExit(f"[ERRO] Coluna '{version}' não encontrada no PDF.")
    header = headers[0]

    marks = [
        c for c in page.chars
        if c["fontname"].endswith("Webdings")
        and header["x0"] - 12 <= (c["x0"] + c["x1"]) / 2 <= header["x1"] + 12
    ]
    names = [w for w in words if w["x0"] < 60 and w["top"] > header["bottom"]]

    supported = []
    for name in sorted(names, key=lambda w: w["top"]):
        center = (name["top"] + name["bottom"]) / 2
        row = [m for m in marks if abs((m["top"] + m["bottom"]) / 2 - center) < 2.5]
        if row and row[0]["text"] == CHECK:
            supported.append(name["text"].strip())

    lines = [
        f"// Gerado por scripts/update_compat_list.py a partir de {PDF.name}, coluna {version}.",
        "// Não editar à mão: rode o script de novo ao trocar de versão do SDK.",
        f'static const char* const kCompatibilitySdkVersion = "{version}";',
        "static const char* const kCompatibleModels[] = {",
    ]
    lines += [f'    "{name}",' for name in supported]
    lines.append("};")
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"[OK] {len(supported)} modelos compatíveis gravados em {OUT}")


if __name__ == "__main__":
    main()
