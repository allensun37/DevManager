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
    dockerfile = require_file(ROOT / "Dockerfile")
    for required_fragment in (
        "debian:bookworm-slim@sha256:",
        "AS builder",
        "AS runtime",
        "cmake --build build",
        "ctest --test-dir build --output-on-failure",
        "--uid 10001",
        "COPY --from=builder /workspace/build/devmanager_http",
        "ENTRYPOINT [\"/opt/devmanager/devmanager_http\"]",
    ):
        if required_fragment not in dockerfile:
            raise AssertionError(f"Dockerfile must contain {required_fragment!r}")

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

    compose = require_file(ROOT / "deploy" / "docker" / "compose.yaml")
    for required_fragment in (
        "devmanager:",
        "127.0.0.1:${DEVMANAGER_PORT:-8080}:8080",
        "DEVMANAGER_API_KEY: ${DEVMANAGER_API_KEY:?Set DEVMANAGER_API_KEY in deploy/docker/.env}",
        "restart: unless-stopped",
        "init: true",
        "stop_grace_period: 10s",
        "read_only: true",
        "cap_drop: [ALL]",
        "no-new-privileges:true",
        "devmanager-data:/var/lib/devmanager",
        "devmanager-logs:/var/log/devmanager",
        "http://127.0.0.1:8080/ready",
    ):
        if required_fragment not in compose:
            raise AssertionError(f"compose.yaml must contain {required_fragment!r}")

    gitignore = require_file(ROOT / ".gitignore")
    if "/deploy/docker/.env" not in gitignore:
        raise AssertionError(".gitignore must exclude deploy/docker/.env")

    smoke = require_file(ROOT / "scripts" / "check_docker_compose.py")
    for required_fragment in (
        "--project-name",
        "--env-file",
        "up",
        "restart",
        "down",
        "--remove-orphans",
        "DEVMANAGER_API_KEY",
        "/health",
        "/ready",
        "/api/projects",
    ):
        if required_fragment not in smoke:
            raise AssertionError(f"Docker Compose smoke script must contain {required_fragment!r}")

    workflow = require_file(ROOT / ".github" / "workflows" / "ci.yml")
    for required_fragment in (
        "docker-compose:",
        "runs-on: ubuntu-latest",
        "docker compose -f deploy/docker/compose.yaml config",
        "python scripts/check_docker_compose.py",
        "os: [ubuntu-latest, windows-latest]",
        "ctest --test-dir build -C Debug --output-on-failure",
    ):
        if required_fragment not in workflow:
            raise AssertionError(f"CI workflow must contain {required_fragment!r}")

    print("Docker deployment contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
