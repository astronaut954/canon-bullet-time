import shutil
import zipfile
import tempfile
from pathlib import Path

VERSION = "v1.0.0"
PROJECT_NAME = "canon-bullet-time"

DIST_DIR = Path("dist/windows-x64")
OUTPUT_DIR = Path("release")
ZIP_NAME = f"{PROJECT_NAME}-{VERSION}-windows-x64.zip"

FILES = [
    "Canon Bullet Time.exe",
    "EDSDK.dll",
    "EdsImage.dll",
]

OPTIONAL_FILES = [
    "CDC_EDSDK_Compat_List.pdf",
]

README_CONTENT = """Canon Bullet Time

Como usar:

1. Conecte suas câmeras Canon via USB
2. Execute: Canon Bullet Time.exe
3. Siga as instruções no terminal

Requisitos:
- Windows 10 ou 11 (64-bit)
- Câmeras Canon compatíveis

Observações:
- Não é necessário instalar nada
- As imagens serão salvas automaticamente
"""

def main():
    if not DIST_DIR.exists():
        raise Exception("dist/windows-x64 não encontrada")

    OUTPUT_DIR.mkdir(exist_ok=True)

    zip_path = OUTPUT_DIR / ZIP_NAME

    print("Criando ZIP...")

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:

        # arquivos principais
        for file in FILES:
            src = DIST_DIR / file
            if not src.exists():
                raise Exception(f"Arquivo não encontrado: {file}")
            z.write(src, file)

        # opcionais
        for file in OPTIONAL_FILES:
            src = DIST_DIR / file
            if src.exists():
                z.write(src, file)

        # README.txt amigável
        z.writestr("README.txt", README_CONTENT)

    print(f"Release pronto: {zip_path}")


if __name__ == "__main__":
    main()