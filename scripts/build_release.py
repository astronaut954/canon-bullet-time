import os
import stat
import shutil
import zipfile
from pathlib import Path

VERSION = "v1.0.0"
PROJECT_NAME = "canon-bullet-time"

REPO_ROOT = Path(__file__).resolve().parent.parent

BUILD_EXE = REPO_ROOT / "x64" / "Release" / "Canon Bullet Time.exe"
EDSDK_DLL = REPO_ROOT / "third_party" / "edsdk" / "x64" / "Dll" / "EDSDK.dll"
EDSIMAGE_DLL = REPO_ROOT / "third_party" / "edsdk" / "x64" / "Dll" / "EdsImage.dll"

ASSETS_DIR = REPO_ROOT / "assets"
README_MD = REPO_ROOT / "README.md"
LICENSE_FILE = REPO_ROOT / "LICENSE"
COMPAT_PDF = REPO_ROOT / "docs" / "CDC_EDSDK_Compat_List.pdf"

DIST_DIR = REPO_ROOT / "dist" / "windows-x64"
OUTPUT_DIR = REPO_ROOT / "release"
RELEASE_DIR = OUTPUT_DIR / f"{PROJECT_NAME}-{VERSION}"
ZIP_NAME = f"{PROJECT_NAME}-{VERSION}-windows-x64.zip"


def ensure_file(path: Path, label: str) -> None:
    if not path.exists() or not path.is_file():
        raise FileNotFoundError(f"{label} não encontrado: {path}")


def ensure_dir(path: Path, label: str) -> None:
    if not path.exists() or not path.is_dir():
        raise FileNotFoundError(f"{label} não encontrada: {path}")


def _on_rm_error(func, path, exc_info):
    try:
        os.chmod(path, stat.S_IWRITE)
        func(path)
    except Exception:
        raise


def clean_dir(path: Path) -> None:
    if not path.exists():
        return

    try:
        shutil.rmtree(path, onerror=_on_rm_error)
        print(f"[CLEAN] {path}")
    except PermissionError:
        raise PermissionError(
            f"\nNão foi possível limpar '{path}'.\n"
            "Verifique se algum programa ainda está usando arquivos dessa pasta "
            "ou se algum arquivo está protegido somente para leitura."
        )


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    print(f"[COPY] {src} -> {dst}")


def copy_tree(src: Path, dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst, onerror=_on_rm_error)
    shutil.copytree(src, dst)
    print(f"[COPYDIR] {src} -> {dst}")


def zip_dir(src_dir: Path, zip_path: Path) -> None:
    if zip_path.exists():
        zip_path.unlink()
        print(f"[REMOVE] {zip_path}")

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for path in src_dir.rglob("*"):
            if path.is_file():
                arcname = src_dir.name / path.relative_to(src_dir)
                z.write(path, arcname)
                print(f"[ZIP] {path} -> {arcname}")


def main():
    ensure_file(BUILD_EXE, "Canon Bullet Time.exe (build Release)")
    ensure_file(EDSDK_DLL, "EDSDK.dll")
    ensure_file(EDSIMAGE_DLL, "EdsImage.dll")
    ensure_dir(ASSETS_DIR, "assets")
    ensure_file(README_MD, "README.md")
    ensure_file(LICENSE_FILE, "LICENSE")

    OUTPUT_DIR.mkdir(exist_ok=True)

    clean_dir(DIST_DIR)
    clean_dir(RELEASE_DIR)

    DIST_DIR.mkdir(parents=True, exist_ok=True)

    copy_file(BUILD_EXE, DIST_DIR / "Canon Bullet Time.exe")
    copy_file(EDSDK_DLL, DIST_DIR / "EDSDK.dll")
    copy_file(EDSIMAGE_DLL, DIST_DIR / "EdsImage.dll")
    copy_file(README_MD, DIST_DIR / "README.md")
    copy_file(LICENSE_FILE, DIST_DIR / "LICENSE")
    copy_tree(ASSETS_DIR, DIST_DIR / "assets")

    if COMPAT_PDF.exists():
        copy_file(COMPAT_PDF, DIST_DIR / "CDC_EDSDK_Compat_List.pdf")

    shutil.copytree(DIST_DIR, RELEASE_DIR)
    print(f"[COPYDIR] {DIST_DIR} -> {RELEASE_DIR}")

    zip_path = OUTPUT_DIR / ZIP_NAME
    zip_dir(RELEASE_DIR, zip_path)

    print(f"\n[OK] Release pronta: {zip_path}")


if __name__ == "__main__":
    main()