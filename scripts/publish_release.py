import re
import subprocess
import sys
from pathlib import Path

PROJECT_NAME = "canon-bullet-time"
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
    run(["python", "scripts/build_release.py", version])


def find_zip(version: str) -> Path:
    zip_path = RELEASE_DIR / f"{PROJECT_NAME}-{version}-windows-x64.zip"
    if not zip_path.exists():
        raise SystemExit(f"\n[ERROR] Release ZIP not found: {zip_path}")
    return zip_path


def create_tag(version: str):
    run(["git", "tag", version])


def push_branch():
    current_branch = get_output(["git", "branch", "--show-current"])
    if not current_branch:
        raise SystemExit("\n[ERROR] Could not determine current branch.")
    run(["git", "push", "origin", current_branch])


def push_tag(version: str):
    run(["git", "push", "origin", version])


def create_github_release(version: str, zip_path: Path):
    notes = (
        f"Release {version}\n\n"
        "- Updated release package\n"
        "- Attached Windows x64 build\n"
    )

    run([
        "gh", "release", "create", version,
        str(zip_path),
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
    ensure_tag_does_not_exist(version)
    ensure_gh_available()

    build_release(version)
    zip_path = find_zip(version)

    push_branch()
    create_tag(version)
    push_tag(version)
    create_github_release(version, zip_path)

    print(f"\n[OK] GitHub release published successfully: {version}")
    print(f"[OK] Asset attached: {zip_path}")


if __name__ == "__main__":
    main()