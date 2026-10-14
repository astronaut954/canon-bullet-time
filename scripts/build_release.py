import os
import re
import stat
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

PROJECT_NAME = "canon-bullet-time"

if len(sys.argv) < 2:
    raise SystemExit(
        "\n[ERROR] Missing release version tag.\n"
        "Usage:\n"
        "  python scripts/build_release.py v1.1.0\n"
    )

VERSION = sys.argv[1]

if not re.fullmatch(r"v\d+\.\d+\.\d+", VERSION):
    raise SystemExit(
        f"\n[ERROR] Invalid version format: '{VERSION}'.\n"
        "Expected semantic version tag format, for example:\n"
        "  python scripts/build_release.py v1.0.0\n"
        "  python scripts/build_release.py v1.1.0\n"
        "  python scripts/build_release.py v2.0.3\n"
    )

REPO_ROOT = Path(__file__).resolve().parent.parent

IS_MAC = sys.platform == "darwin"

if IS_MAC:
    PLATFORM = "macos-arm64"
    BUILD_EXE = REPO_ROOT / "build" / "macos-arm64" / "Canon Bullet Time"
    BUILD_GUI = REPO_ROOT / "build" / "macos-arm64" / "Canon Bullet Time GUI"
    EDSDK_FRAMEWORK = REPO_ROOT / "third_party" / "edsdk" / "macos" / "EDSDK.framework"
else:
    PLATFORM = "windows-x64"
    BUILD_EXE = REPO_ROOT / "build" / "windows-x64" / "Release" / "Canon Bullet Time.exe"
    BUILD_GUI = REPO_ROOT / "build" / "windows-x64" / "Release" / "Canon Bullet Time GUI.exe"
    EDSDK_DLL = REPO_ROOT / "third_party" / "edsdk" / "x64" / "Dll" / "EDSDK.dll"
    EDSIMAGE_DLL = REPO_ROOT / "third_party" / "edsdk" / "x64" / "Dll" / "EdsImage.dll"

ASSETS_DIR = REPO_ROOT / "assets"
README_MD = REPO_ROOT / "README.md"
LICENSE_FILE = REPO_ROOT / "LICENSE"
THIRD_PARTY_NOTICES = REPO_ROOT / "THIRD_PARTY_NOTICES.md"
COMPAT_PDF = REPO_ROOT / "docs" / "CDC_EDSDK_Compat_List.pdf"

DIST_DIR = REPO_ROOT / "dist" / PLATFORM
OUTPUT_DIR = REPO_ROOT / "release"
RELEASE_DIR = OUTPUT_DIR / f"{PROJECT_NAME}-{VERSION}"
ZIP_NAME = f"{PROJECT_NAME}-{VERSION}-{PLATFORM}.zip"


def ensure_file(path: Path, label: str) -> None:
    if not path.exists() or not path.is_file():
        raise FileNotFoundError(f"{label} not found: {path}")


def ensure_dir(path: Path, label: str) -> None:
    if not path.exists() or not path.is_dir():
        raise FileNotFoundError(f"{label} not found: {path}")


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
            f"\nCould not clean '{path}'.\n"
            "Make sure no program is still using files from this folder "
            "and that no file is protected as read-only."
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
    ensure_file(BUILD_EXE, f"{BUILD_EXE.name} (Release build)")
    ensure_file(BUILD_GUI, f"{BUILD_GUI.name} (Release build of the window app)")
    if IS_MAC:
        ensure_dir(EDSDK_FRAMEWORK, "EDSDK.framework")
    else:
        ensure_file(EDSDK_DLL, "EDSDK.dll")
        ensure_file(EDSIMAGE_DLL, "EdsImage.dll")
    ensure_dir(ASSETS_DIR, "assets")
    ensure_file(README_MD, "README.md")
    ensure_file(LICENSE_FILE, "LICENSE")
    ensure_file(THIRD_PARTY_NOTICES, "THIRD_PARTY_NOTICES.md")

    OUTPUT_DIR.mkdir(exist_ok=True)

    clean_dir(DIST_DIR)
    clean_dir(RELEASE_DIR)

    DIST_DIR.mkdir(parents=True, exist_ok=True)

    copy_file(BUILD_EXE, DIST_DIR / BUILD_EXE.name)
    copy_file(BUILD_GUI, DIST_DIR / BUILD_GUI.name)
    if IS_MAC:
        # ditto preserva links simbólicos e a assinatura do framework da Canon
        (DIST_DIR / "Frameworks").mkdir()
        subprocess.run(["ditto", str(EDSDK_FRAMEWORK), str(DIST_DIR / "Frameworks" / "EDSDK.framework")], check=True)
        print(f"[COPYDIR] {EDSDK_FRAMEWORK} -> {DIST_DIR / 'Frameworks' / 'EDSDK.framework'}")
    else:
        copy_file(EDSDK_DLL, DIST_DIR / "EDSDK.dll")
        copy_file(EDSIMAGE_DLL, DIST_DIR / "EdsImage.dll")
        # Outras bibliotecas que a compilação colocou ao lado dos programas (ex.: o
        # carregador do WebView2, se a versão do webview usar um)
        for dll in BUILD_EXE.parent.glob("*.dll"):
            if dll.name not in ("EDSDK.dll", "EdsImage.dll"):
                copy_file(dll, DIST_DIR / dll.name)
    copy_file(THIRD_PARTY_NOTICES, DIST_DIR / "THIRD_PARTY_NOTICES.md")
    copy_file(README_MD, DIST_DIR / "README.md")
    copy_file(LICENSE_FILE, DIST_DIR / "LICENSE")
    copy_tree(ASSETS_DIR, DIST_DIR / "assets")

    if COMPAT_PDF.exists():
        copy_file(COMPAT_PDF, DIST_DIR / "CDC_EDSDK_Compat_List.pdf")

    zip_path = OUTPUT_DIR / ZIP_NAME
    if IS_MAC:
        subprocess.run(["ditto", str(DIST_DIR), str(RELEASE_DIR)], check=True)
        print(f"[COPYDIR] {DIST_DIR} -> {RELEASE_DIR}")
        if zip_path.exists():
            zip_path.unlink()
        # zip com ditto: o zipfile do Python não preserva links simbólicos do framework
        subprocess.run(["ditto", "-c", "-k", "--keepParent", str(RELEASE_DIR), str(zip_path)], check=True)
    else:
        shutil.copytree(DIST_DIR, RELEASE_DIR)
        print(f"[COPYDIR] {DIST_DIR} -> {RELEASE_DIR}")
        zip_dir(RELEASE_DIR, zip_path)

    print(f"\n[OK] Release package ready: {zip_path}")


if __name__ == "__main__":
    main()