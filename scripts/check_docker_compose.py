"""Run an isolated black-box Docker Compose smoke test for DevManager.

The script intentionally uses only the Python standard library. It creates a
unique project name, temporary environment file and loopback port; its finally
block removes only the Compose project and named volumes it created.
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPOSE_FILE = ROOT / "deploy" / "docker" / "compose.yaml"


def reserve_loopback_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def run(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if check and result.returncode != 0:
        raise RuntimeError(f"command failed with exit code {result.returncode}: {' '.join(command)}\n{result.stdout}")
    return result


def request_json(
    port: int,
    path: str,
    *,
    api_key: str | None = None,
    method: str = "GET",
    payload: dict[str, object] | None = None,
) -> tuple[int, object]:
    headers = {"X-Request-ID": "docker-compose-smoke"}
    data = None
    if api_key is not None:
        headers["Authorization"] = f"Bearer {api_key}"
    if payload is not None:
        headers["Content-Type"] = "application/json"
        data = json.dumps(payload).encode("utf-8")

    request = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}", data=data, headers=headers, method=method
    )
    try:
        with urllib.request.urlopen(request, timeout=1) as response:
            body = response.read().decode("utf-8")
            return response.status, json.loads(body) if body else None
    except urllib.error.HTTPError as error:
        body = error.read().decode("utf-8")
        return error.code, json.loads(body) if body else None


def wait_for_ready(port: int) -> None:
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        try:
            health_status, health = request_json(port, "/health")
            ready_status, ready = request_json(port, "/ready")
            if health_status == 200 and health == {"status": "ok"} and ready_status == 200 and ready == {"status": "ready"}:
                return
        except (OSError, urllib.error.URLError, TimeoutError, ValueError):
            pass
        time.sleep(0.2)
    raise RuntimeError("Docker Compose service did not become healthy and ready within 30 seconds")


def main() -> int:
    arguments = sys.argv[1:]
    if any(argument != "--skip-build" for argument in arguments) or arguments.count("--skip-build") > 1:
        raise RuntimeError("usage: check_docker_compose.py [--skip-build]")
    skip_build = "--skip-build" in arguments

    if not COMPOSE_FILE.is_file():
        raise RuntimeError(f"Compose file is missing: {COMPOSE_FILE}")

    project_name = f"devmanager-v09-{uuid.uuid4().hex[:12]}"
    port = reserve_loopback_port()
    api_key = f"docker-smoke-{uuid.uuid4().hex}"

    with tempfile.TemporaryDirectory(prefix="devmanager-v09-compose-") as temporary:
        environment_file = Path(temporary) / ".env"
        environment_file.write_text(
            f"DEVMANAGER_API_KEY={api_key}\nDEVMANAGER_PORT={port}\n", encoding="utf-8"
        )
        compose = [
            "docker",
            "compose",
            "--project-name",
            project_name,
            "--env-file",
            str(environment_file),
            "-f",
            str(COMPOSE_FILE),
        ]
        try:
            if not skip_build:
                run(compose + ["build"])
            run(compose + ["up", "--detach"])
            wait_for_ready(port)

            status, body = request_json(port, "/api/projects")
            if status != 401 or body != {"error": {"code": "unauthorized", "message": "authentication required"}}:
                raise RuntimeError("unauthenticated project request did not return the expected 401 error")

            status, body = request_json(port, "/api/projects", api_key=api_key)
            if status != 200 or body != []:
                raise RuntimeError("authenticated initial project list did not return an empty array")

            project_name_value = "Docker Compose Persistence"
            status, body = request_json(
                port,
                "/api/projects",
                api_key=api_key,
                method="POST",
                payload={
                    "name": project_name_value,
                    "techStack": ["C++", "Docker"],
                    "description": "Compose smoke-test project",
                    "status": "active",
                },
            )
            if status != 201 or not isinstance(body, dict) or body.get("name") != project_name_value:
                raise RuntimeError("authenticated project creation did not return the created project")

            run(compose + ["restart", "devmanager"])
            wait_for_ready(port)
            status, body = request_json(port, "/api/projects", api_key=api_key)
            if status != 200 or not isinstance(body, list) or not any(item.get("name") == project_name_value for item in body if isinstance(item, dict)):
                raise RuntimeError("SQLite project data did not survive Docker Compose restart")

            run(compose + ["stop", "devmanager"])
            logs = run(compose + ["logs", "--no-color", "devmanager"]).stdout
            if api_key in logs:
                raise RuntimeError("Docker Compose service log exposed the generated API key")
        finally:
            run(compose + ["down", "--volumes", "--remove-orphans"], check=False)

    print("Docker Compose smoke test passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"Docker Compose smoke test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
