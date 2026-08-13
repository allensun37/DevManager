"""Verify the static v0.9 Docker deployment contract without Docker."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def require_file(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required deployment file is missing: {path.relative_to(ROOT)}")
    return path.read_text(encoding="utf-8")


def main() -> int:
    environment_example = require_file(ROOT / "deploy" / "docker" / ".env.example")
    if "DEVMANAGER_API_KEY=" not in environment_example:
        raise AssertionError(".env.example must document DEVMANAGER_API_KEY")

    dockerignore = require_file(ROOT / ".dockerignore")
    for ignored_path in (".git/", ".worktrees/", "build*/", "logs/", "data/", ".env"):
        if ignored_path not in dockerignore:
            raise AssertionError(f".dockerignore must exclude {ignored_path}")

    config = json.loads(require_file(ROOT / "deploy" / "docker" / "config" / "devmanager.json"))
    if config != {
        "server": {"host": "0.0.0.0", "port": 8080},
        "storage": {"type": "sqlite", "path": "/var/lib/devmanager/devmanager.db"},
        "logging": {"level": "info", "path": "/var/log/devmanager/devmanager.log"},
    }:
        raise AssertionError("container config must use the approved server, SQLite, and log paths")

    print("Docker deployment contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
