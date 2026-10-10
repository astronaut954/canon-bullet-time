"""
Instala o Canon EDSDK em third_party/edsdk/ a partir do zip baixado da Canon.

Uso:
    Windows:  python scripts/setup_edsdk.py "C:/caminho/EDSDK_v13.20.10_Windows.zip"
    macOS:    python3 scripts/setup_edsdk.py ~/Downloads/ESDK/EDSDK_v13.20.10_Macintosh.zip

Resultado (Windows):
    third_party/edsdk/Header/EDSDK.h, EDSDKErrors.h, EDSDKTypes.h
    third_party/edsdk/x64/Library/EDSDK.lib
    third_party/edsdk/x64/Dll/EDSDK.dll, EdsImage.dll
    third_party/edsdk/VERSION.txt

Resultado (macOS):
    third_party/edsdk/Header/EDSDK.h, EDSDKErrors.h, EDSDKTypes.h
    third_party/edsdk/macos/EDSDK.framework
    third_party/edsdk/VERSION.txt

O SDK da Canon não pode ser publicado no repositório (ver README).
A pasta third_party/edsdk/ é ignorada pelo Git.
"""

import os
import plistlib
import subprocess
import shutil
import stat
import struct
import sys
import tempfile
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEST = REPO_ROOT / "third_party" / "edsdk"

HEADERS = ["EDSDK.h", "EDSDKErrors.h", "EDSDKTypes.h"]
DLLS = ["EDSDK.dll", "EdsImage.dll"]
PE_X64 = 0x8664


def _force_remove(func, path, _exc_info):
    """Remove arquivos marcados como somente leitura (comum em DLLs vindas de zip)."""
    os.chmod(path, stat.S_IWRITE)
    func(path)


def extract_all(zip_path: Path, target: Path) -> None:
    """Extrai o zip e quaisquer zips aninhados."""
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(target)
    for inner in list(target.rglob("*.zip")):
        inner_dir = inner.with_name(inner.name + "_unzipped")
        inner_dir.mkdir(exist_ok=True)
        extract_all(inner, inner_dir)


def is_pe_x64(path: Path) -> bool:
    data = path.read_bytes()[:4096]
    if data[:2] != b"MZ":
        return False
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset + 6 > len(data):
        data = path.read_bytes()[: pe_offset + 6]
    return struct.unpack_from("<H", data, pe_offset + 4)[0] == PE_X64


def find_one(root: Path, name: str, predicate=None) -> Path:
    candidates = [
        p for p in root.rglob(name)
        if "__MACOSX" not in p.parts and (predicate is None or predicate(p))
    ]
    if not candidates:
        raise SystemExit(f"[ERRO] '{name}' não encontrado dentro do zip.")
    if len(candidates) > 1:
        candidates.sort(key=lambda p: len(p.parts))
    return candidates[0]


def lib_is_x64(path: Path) -> bool:
    return "64" in str(path.parent).lower()


def read_version(dll: Path) -> str:
    data = dll.read_bytes()
    key = "FileVersion".encode("utf-16le")
    i = data.find(key)
    if i < 0:
        return "desconhecida"
    raw = data[i + len(key): i + len(key) + 64].decode("utf-16le", "ignore")
    return raw.strip("\x00 ").split("\x00")[0] or "desconhecida"


def backup_existing() -> None:
    if DEST.exists():
        backup = DEST.with_name("edsdk_backup")
        if backup.exists():
            shutil.rmtree(backup, onerror=_force_remove)
        DEST.rename(backup)
        print(f"[BACKUP] SDK anterior movido para {backup}")


def write_version(version: str, zip_path: Path) -> None:
    (DEST / "VERSION.txt").write_text(
        f"EDSDK {version}\nOrigem: {zip_path.name}\n", encoding="utf-8"
    )


def install_windows(tmp_dir: Path, zip_path: Path) -> str:
    headers = {h: find_one(tmp_dir, h) for h in HEADERS}
    lib = find_one(tmp_dir, "EDSDK.lib", lib_is_x64)
    dlls = {d: find_one(tmp_dir, d, is_pe_x64) for d in DLLS}

    backup_existing()

    (DEST / "Header").mkdir(parents=True)
    (DEST / "x64" / "Library").mkdir(parents=True)
    (DEST / "x64" / "Dll").mkdir(parents=True)

    for name, src in headers.items():
        shutil.copy2(src, DEST / "Header" / name)
    shutil.copy2(lib, DEST / "x64" / "Library" / "EDSDK.lib")
    for name, src in dlls.items():
        shutil.copy2(src, DEST / "x64" / "Dll" / name)

    version = read_version(DEST / "x64" / "Dll" / "EDSDK.dll")
    write_version(version, zip_path)
    return version


def install_macos(tmp_dir: Path, zip_path: Path) -> str:
    dmgs = [p for p in tmp_dir.rglob("*.dmg") if p.is_file() and "__MACOSX" not in p.parts]
    if not dmgs:
        raise SystemExit("[ERRO] Nenhum .dmg encontrado dentro do zip (é o pacote de Mac?).")

    mount = tmp_dir / "_mnt"
    mount.mkdir()
    subprocess.run(
        ["hdiutil", "attach", str(dmgs[0]), "-nobrowse", "-readonly", "-mountpoint", str(mount)],
        check=True, stdout=subprocess.DEVNULL,
    )
    try:
        headers = {h: find_one(mount, h) for h in HEADERS}
        framework = find_one(mount, "EDSDK.framework")

        backup_existing()

        (DEST / "Header").mkdir(parents=True)
        (DEST / "macos").mkdir(parents=True)

        for name, src in headers.items():
            shutil.copy2(src, DEST / "Header" / name)
        # ditto preserva links simbólicos e a assinatura do framework
        subprocess.run(["ditto", str(framework), str(DEST / "macos" / "EDSDK.framework")], check=True)
    finally:
        subprocess.run(["hdiutil", "detach", str(mount), "-quiet"], check=False)

    # Arquivos baixados da internet ficam "em quarentena" e o macOS pode bloquear o carregamento
    subprocess.run(["xattr", "-dr", "com.apple.quarantine", str(DEST)], check=False)

    info = DEST / "macos" / "EDSDK.framework" / "Resources" / "Info.plist"
    version = "desconhecida"
    if info.exists():
        with open(info, "rb") as f:
            version = plistlib.load(f).get("CFBundleShortVersionString", version)

    write_version(version, zip_path)
    return version


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)

    zip_path = Path(sys.argv[1]).expanduser().resolve()
    if not zip_path.is_file():
        raise SystemExit(f"[ERRO] Arquivo não encontrado: {zip_path}")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        print(f"[EXTRAIR] {zip_path.name}")
        extract_all(zip_path, tmp_dir)

        if sys.platform == "darwin":
            version = install_macos(tmp_dir, zip_path)
        else:
            version = install_windows(tmp_dir, zip_path)

    print(f"[OK] EDSDK {version} instalado em {DEST}")


if __name__ == "__main__":
    main()
