import shutil
import zipfile
import tempfile
from pathlib import Path

# ===== CONFIG =====
VERSION = "v1.0.0"
PROJECT_NAME = "canon-bullet-time"

DIST_DIR = Path("dist/windows-x64")
OUTPUT_DIR = Path("release")
ZIP_NAME = f"{PROJECT_NAME}-{VERSION}-windows-x64.zip"

FILES = [
    "Canon Bullet Time.exe",
    "EDSDK.dll",
    "EdsImage.dll",
    "README.md",
]

OPTIONAL_FILES = [
    "CDC_EDSDK_Compat_List.pdf",
]

def main():
    if not DIST_DIR.exists():
        raise Exception("Pasta dist/windows-x64 não encontrada")

    OUTPUT_DIR.mkdir(exist_ok=True)

    # cria pasta temporária do sistema (resolve problema de DLL travada)
    temp_dir = Path(tempfile.mkdtemp())

    print("Copiando arquivos...")

    # obrigatórios
    for file in FILES:
        src = DIST_DIR / file
        if not src.exists():
            raise Exception(f"Arquivo obrigatório não encontrado: {file}")
        shutil.copy(src, temp_dir / file)

    # opcionais
    for file in OPTIONAL_FILES:
        src = DIST_DIR / file
        if src.exists():
            shutil.copy(src, temp_dir / file)

    zip_path = OUTPUT_DIR / ZIP_NAME

    print("Criando ZIP...")

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for file in temp_dir.iterdir():
            z.write(file, file.name)

    print(f"Release gerado: {zip_path}")

    # limpeza (agora segura)
    try:
        shutil.rmtree(temp_dir)
    except Exception:
        print("Aviso: não conseguiu limpar temp (ok ignorar)")

if __name__ == "__main__":
    main()