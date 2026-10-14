import os
import re
import stat
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

PROJECT_NAME = "stillpoint"

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
    BUILD_EXE = REPO_ROOT / "build" / "macos-arm64" / "Stillpoint Terminal App"
    BUILD_GUI = REPO_ROOT / "build" / "macos-arm64" / "Stillpoint"
    EDSDK_FRAMEWORK = REPO_ROOT / "third_party" / "edsdk" / "macos" / "EDSDK.framework"
else:
    PLATFORM = "windows-x64"
    BUILD_EXE = REPO_ROOT / "build" / "windows-x64" / "Release" / "Stillpoint Terminal App.exe"
    BUILD_GUI = REPO_ROOT / "build" / "windows-x64" / "Release" / "Stillpoint.exe"
    EDSDK_DLL = REPO_ROOT / "third_party" / "edsdk" / "x64" / "Dll" / "EDSDK.dll"
    EDSIMAGE_DLL = REPO_ROOT / "third_party" / "edsdk" / "x64" / "Dll" / "EdsImage.dll"

ASSETS_DIR = REPO_ROOT / "assets"
README_MD = REPO_ROOT / "README.md"
LICENSE_FILE = REPO_ROOT / "LICENSE"
THIRD_PARTY_NOTICES = REPO_ROOT / "THIRD_PARTY_NOTICES.md"
COMPAT_PDF = REPO_ROOT / "docs" / "CDC_EDSDK_Compat_List.pdf"
MAC_ICON = REPO_ROOT / "resources" / "icon" / "app-icon.icns"  # gerado por scripts/make_icon.py

DIST_DIR = REPO_ROOT / "dist" / PLATFORM
OUTPUT_DIR = REPO_ROOT / "release"
RELEASE_DIR = OUTPUT_DIR / f"{PROJECT_NAME}-{VERSION}"
ZIP_NAME = f"{PROJECT_NAME}-{VERSION}-{PLATFORM}.zip"

# Instaladores: .dmg no Mac, instalador .exe (Inno Setup) no Windows.
# Imagens e Read Me gerados por scripts/make_installer_art.py.
INSTALLER_DIR = REPO_ROOT / "resources" / "installer"
DMG_NAME = f"{PROJECT_NAME}-{VERSION}-{PLATFORM}.dmg"
SETUP_NAME = f"{PROJECT_NAME}-{VERSION}-{PLATFORM}-setup"   # o Inno Setup acrescenta .exe
DMGBUILD_VERSION = "1.6.7"
# Posições dos ícones na janela do .dmg (pontos); iguais às de scripts/make_installer_art.py
DMG_LAYOUT = {
    "window": (660, 500),
    "icon_size": 96,
    "positions": {"Stillpoint.app": (170, 190), "Applications": (490, 190), "Read Me.pdf": (330, 392)},
}


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


APP_NAME = "Stillpoint.app"
TERMINAL_DIR_NAME = "Terminal App"
TERMINAL_EXE_NAME = "Stillpoint Terminal App"
APP_BUNDLE_ID = "io.github.astronaut954.stillpoint"


def dmgbuild_python() -> str:
    """Python com o dmgbuild (cria o .dmg com o fundo e as posições dos ícones, sem usar o
    Finder). Na primeira vez, cria um ambiente só para isso em build/release-tools."""
    try:
        import dmgbuild  # noqa: F401
        return sys.executable
    except ImportError:
        pass
    venv = REPO_ROOT / "build" / "release-tools"
    py = venv / "bin" / "python3"
    if not py.exists():
        print(f"[SETUP] Creating {venv} (one time) to build the .dmg")
        subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
    check = subprocess.run([str(py), "-c", "import dmgbuild"], capture_output=True)
    if check.returncode != 0:
        print(f"[SETUP] Installing dmgbuild {DMGBUILD_VERSION} (internet required, one time)")
        subprocess.run([str(py), "-m", "pip", "install", "--quiet", f"dmgbuild=={DMGBUILD_VERSION}"], check=True)
    return str(py)


def make_dmg(app: Path, dmg_path: Path) -> None:
    """Cria o .dmg: Stillpoint.app, atalho para Aplicativos e Read Me.pdf, com o fundo aprovado."""
    import json
    import tempfile
    if dmg_path.exists():
        dmg_path.unlink()
    w, h = DMG_LAYOUT["window"]
    settings = {
        "format": "UDZO",
        "files": [str(app), str(INSTALLER_DIR / "Read Me.pdf")],
        "symlinks": {"Applications": "/Applications"},
        "icon": str(MAC_ICON),
        "background": str(INSTALLER_DIR / "dmg-background.png"),  # o @2x ao lado é usado em telas Retina
        "window_rect": [[200, 120], [w, h]],
        "icon_size": DMG_LAYOUT["icon_size"],
        "text_size": 12,
        "icon_locations": {k: list(v) for k, v in DMG_LAYOUT["positions"].items()},
    }
    script = (
        "import json, sys, dmgbuild\n"
        "s = json.load(open(sys.argv[1]))\n"
        "s['window_rect'] = tuple(tuple(x) for x in s['window_rect'])\n"
        "s['icon_locations'] = {k: tuple(v) for k, v in s['icon_locations'].items()}\n"
        "dmgbuild.build_dmg(sys.argv[2], 'Stillpoint', settings=s)\n"
    )
    with tempfile.TemporaryDirectory() as tmp:
        settings_file = Path(tmp) / "dmg.json"
        settings_file.write_text(json.dumps(settings), encoding="utf-8")
        subprocess.run([dmgbuild_python(), "-c", script, str(settings_file), str(dmg_path)], check=True)
    print(f"[DMG] {dmg_path}")


def find_iscc() -> str:
    """Compilador do Inno Setup (monta o instalador de Windows)."""
    found = shutil.which("ISCC") or shutil.which("iscc")
    if found:
        return found
    candidates = [
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Inno Setup 6" / "ISCC.exe",
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Inno Setup 6" / "ISCC.exe",
        Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "Inno Setup 6" / "ISCC.exe",
    ]
    for c in candidates:
        if c.is_file():
            return str(c)
    raise SystemExit(
        "\n[ERROR] Inno Setup not found (needed for the Windows installer).\n"
        "Install it once (version 6.5.2 or newer), then run this script again:\n"
        "  winget install JRSoftware.InnoSetup\n"
    )


def make_windows_setup(setup_name: str) -> Path:
    iscc = find_iscc()
    subprocess.run([
        iscc, "/Qp",
        f"/DAppVersion={VERSION.lstrip('v')}",
        f"/DDistDir={DIST_DIR}",
        f"/DOutputDir={OUTPUT_DIR}",
        f"/DOutputName={setup_name}",
        str(INSTALLER_DIR / "stillpoint.iss"),
    ], check=True)
    setup = OUTPUT_DIR / (setup_name + ".exe")
    print(f"[SETUP] {setup}")
    return setup


def make_mac_app(dest_dir: Path) -> None:
    """Monta o aplicativo de Mac (abre com dois cliques):
    Stillpoint.app/Contents/
        Info.plist
        MacOS/Stillpoint                 (o programa de janela)
        Frameworks/EDSDK.framework       (o SDK da Canon)
        Resources/assets                 (sons e a página da interface)
        Resources/AppIcon.icns           (ícone)
    O programa acha o SDK por @executable_path/../Frameworks e os arquivos por
    Contents/Resources (Platform::ResourcesDirectory)."""
    app = dest_dir / APP_NAME
    contents = app / "Contents"
    (contents / "MacOS").mkdir(parents=True)
    (contents / "Frameworks").mkdir()
    (contents / "Resources").mkdir()

    copy_file(BUILD_GUI, contents / "MacOS" / BUILD_GUI.name)
    subprocess.run(["ditto", str(EDSDK_FRAMEWORK), str(contents / "Frameworks" / "EDSDK.framework")], check=True)
    print(f"[COPYDIR] {EDSDK_FRAMEWORK} -> {contents / 'Frameworks' / 'EDSDK.framework'}")
    copy_tree(ASSETS_DIR, contents / "Resources" / "assets")
    copy_file(MAC_ICON, contents / "Resources" / "AppIcon.icns")

    short_version = VERSION.lstrip("v")
    plist = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Stillpoint</string>
    <key>CFBundleDisplayName</key><string>Stillpoint</string>
    <key>CFBundleIdentifier</key><string>{APP_BUNDLE_ID}</string>
    <key>CFBundleExecutable</key><string>{BUILD_GUI.name}</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>{short_version}</string>
    <key>CFBundleVersion</key><string>{short_version}</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>LSMinimumSystemVersion</key><string>13.3</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSCameraUsageDescription</key><string>Stillpoint controls your Canon cameras over USB for bullet time photography.</string>
</dict>
</plist>
"""
    (contents / "Info.plist").write_text(plist, encoding="utf-8")
    print(f"[WRITE] {contents / 'Info.plist'}")

    # Assinatura local (sem conta de desenvolvedor da Apple): liga o Info.plist ao
    # programa. Se falhar, o aplicativo ainda abre depois do comando xattr do README.
    result = subprocess.run(["codesign", "--force", "--sign", "-", str(app)])
    if result.returncode != 0:
        print("[WARN] codesign failed; the app is packaged without a local signature")


def main():
    ensure_file(BUILD_EXE, f"{BUILD_EXE.name} (Release build)")
    ensure_file(BUILD_GUI, f"{BUILD_GUI.name} (Release build of the window app)")
    if IS_MAC:
        ensure_dir(EDSDK_FRAMEWORK, "EDSDK.framework")
        ensure_file(MAC_ICON, "app-icon.icns")
        ensure_file(INSTALLER_DIR / "Read Me.pdf", "Read Me.pdf")
        ensure_file(INSTALLER_DIR / "dmg-background.png", "dmg-background.png")
    else:
        ensure_file(EDSDK_DLL, "EDSDK.dll")
        ensure_file(EDSIMAGE_DLL, "EdsImage.dll")
        ensure_file(INSTALLER_DIR / "stillpoint.iss", "stillpoint.iss")
        find_iscc()  # avisa antes de montar tudo se o Inno Setup não estiver instalado
    ensure_dir(ASSETS_DIR, "assets")
    ensure_file(README_MD, "README.md")
    ensure_file(LICENSE_FILE, "LICENSE")
    ensure_file(THIRD_PARTY_NOTICES, "THIRD_PARTY_NOTICES.md")

    OUTPUT_DIR.mkdir(exist_ok=True)

    clean_dir(DIST_DIR)
    clean_dir(RELEASE_DIR)

    DIST_DIR.mkdir(parents=True, exist_ok=True)

    # O programa principal é o de janela ("Stillpoint"). O de terminal vai numa
    # subpasta, com outro nome, para não confundir quem baixa.
    terminal_dir = DIST_DIR / TERMINAL_DIR_NAME
    terminal_dir.mkdir()
    if IS_MAC:
        make_mac_app(DIST_DIR)
        copy_file(BUILD_EXE, terminal_dir / TERMINAL_EXE_NAME)
        # ditto preserva links simbólicos e a assinatura do framework da Canon
        (terminal_dir / "Frameworks").mkdir()
        subprocess.run(["ditto", str(EDSDK_FRAMEWORK), str(terminal_dir / "Frameworks" / "EDSDK.framework")], check=True)
        print(f"[COPYDIR] {EDSDK_FRAMEWORK} -> {terminal_dir / 'Frameworks' / 'EDSDK.framework'}")
        copy_tree(ASSETS_DIR, terminal_dir / "assets")
    else:
        # Cada programa precisa das DLLs do SDK e da pasta assets ao lado dele
        dlls = [EDSDK_DLL, EDSIMAGE_DLL] + [
            d for d in BUILD_EXE.parent.glob("*.dll") if d.name not in ("EDSDK.dll", "EdsImage.dll")]
        copy_file(BUILD_GUI, DIST_DIR / BUILD_GUI.name)
        copy_file(BUILD_EXE, terminal_dir / (TERMINAL_EXE_NAME + ".exe"))
        for folder in (DIST_DIR, terminal_dir):
            for dll in dlls:
                copy_file(dll, folder / dll.name)
            copy_tree(ASSETS_DIR, folder / "assets")
    copy_file(THIRD_PARTY_NOTICES, DIST_DIR / "THIRD_PARTY_NOTICES.md")
    copy_file(README_MD, DIST_DIR / "README.md")
    copy_file(LICENSE_FILE, DIST_DIR / "LICENSE")

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

    if IS_MAC:
        installer = OUTPUT_DIR / DMG_NAME
        make_dmg(DIST_DIR / APP_NAME, installer)
    else:
        installer = make_windows_setup(SETUP_NAME)

    print(f"\n[OK] Release package ready: {zip_path}")
    print(f"[OK] Installer ready: {installer}")


if __name__ == "__main__":
    main()