import re
import subprocess
import sys
from pathlib import Path

PROJECT_NAME = "stillpoint"
REPO_ROOT = Path(__file__).resolve().parent.parent
RELEASE_DIR = REPO_ROOT / "release"


def run(cmd, cwd=REPO_ROOT):
    print(f"[RUN] {' '.join(cmd)}")
    subprocess.run(cmd, cwd=cwd, check=True)


def get_output(cmd, cwd=REPO_ROOT):
    result = subprocess.run(cmd, cwd=cwd, check=True, capture_output=True, text=True)
    return result.stdout.strip()


def ensure_clean_git_tree():
    status = get_output(["git", "status", "--porcelain"])
    if status:
        raise SystemExit(
            "\n[ERROR] Working tree is not clean.\n"
            "Commit or stash your changes before publishing a release."
        )


def ensure_version(version: str):
    if not re.fullmatch(r"v\d+\.\d+\.\d+", version):
        raise SystemExit(
            f"\n[ERROR] Invalid version format: '{version}'.\n"
            "Expected format example:\n"
            "  v1.0.0\n"
            "  v1.1.0\n"
            "  v2.0.3\n"
        )


def ensure_tag_does_not_exist(version: str):
    tags = get_output(["git", "tag"])
    if version in tags.splitlines():
        raise SystemExit(f"\n[ERROR] Git tag already exists: {version}")


def ensure_gh_available():
    try:
        run(["gh", "--version"])
    except Exception:
        raise SystemExit(
            "\n[ERROR] GitHub CLI (gh) not found.\n"
            "Install it and authenticate first.\n"
        )


def build_release(version: str):
    run([sys.executable, "scripts/build_release.py", version])


def find_assets(version: str) -> list:
    """Arquivos deste sistema para a página da versão: o zip e o instalador
    (.dmg no Mac, -setup.exe no Windows)."""
    platform = "macos-arm64" if sys.platform == "darwin" else "windows-x64"
    base = f"{PROJECT_NAME}-{version}-{platform}"
    installer = f"{base}.dmg" if sys.platform == "darwin" else f"{base}-setup.exe"
    assets = []
    for name in (installer, f"{base}.zip"):
        path = RELEASE_DIR / name
        if not path.exists():
            raise SystemExit(f"\n[ERROR] Release file not found: {path}")
        assets.append(path)
    return assets


def create_tag(version: str):
    run(["git", "tag", version])


def push_branch():
    current_branch = get_output(["git", "branch", "--show-current"])
    if not current_branch:
        raise SystemExit("\n[ERROR] Could not determine current branch.")
    run(["git", "push", "origin", current_branch])


def push_tag(version: str):
    run(["git", "push", "origin", version])


def create_github_release(version: str, assets: list):
    notes = (
        f"Stillpoint {version}\n\n"
        "**Download**\n\n"
        f"- **macOS** (Apple Silicon): `{PROJECT_NAME}-{version}-macos-arm64.dmg`. Open it and drag "
        "Stillpoint to Applications. The first time, follow the steps in *Read Me* (inside the .dmg) "
        "to allow the app in System Settings.\n"
        f"- **Windows** 10/11 (64-bit): `{PROJECT_NAME}-{version}-windows-x64-setup.exe`. If Windows shows "
        "\"Windows protected your PC\", click **More info** and then **Run anyway**.\n"
        "- The `.zip` files have the same apps without an installer (on Mac, also the terminal app).\n"
    )

    run([
        "gh", "release", "create", version,
        *[str(a) for a in assets],
        "--title", version,
        "--notes", notes
    ])


def main():
    if len(sys.argv) < 2:
        raise SystemExit(
            "\n[ERROR] Missing release version.\n"
            "Usage:\n"
            "  python scripts/publish_release.py v1.1.0\n"
        )

    version = sys.argv[1]

    ensure_version(version)
    ensure_clean_git_tree()
    ensure_gh_available()

    run(["git", "fetch", "--tags", "origin"])
    tag_exists = version in get_output(["git", "tag"]).splitlines()

    build_release(version)
    assets = find_assets(version)

    if tag_exists:
        # A versão já foi publicada pelo outro sistema: só anexa os arquivos deste sistema
        run(["gh", "release", "upload", version, *[str(a) for a in assets], "--clobber"])
        for a in assets:
            print(f"[OK] Asset added to existing release {version}: {a}")
        return

    push_branch()
    create_tag(version)
    push_tag(version)
    create_github_release(version, assets)

    print(f"\n[OK] GitHub release published successfully: {version}")
    for a in assets:
        print(f"[OK] Asset attached: {a}")


if __name__ == "__main__":
    main()