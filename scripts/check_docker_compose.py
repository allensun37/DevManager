"""Run an isolated black-box Docker Compose smoke test for DevManager.

The script intentionally uses only the Python standard library. It creates a
unique project name, temporary environment file and loopback port; its finally
block removes only the Compose project and named volumes it created.
"""

from __future__ import annotations

import json
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
LOOPBACK_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def run(
    command: list[str], *, check: bool = True, timeout_seconds: int = 120
) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            command,
            cwd=ROOT,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=timeout_seconds,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(
            f"command timed out after {timeout_seconds} seconds: {' '.join(command)}"
        ) from error
    if check and result.returncode != 0:
        raise RuntimeError(f"command failed with exit code {result.returncode}: {' '.join(command)}")
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
        with LOOPBACK_OPENER.open(request, timeout=1) as response:
            body = response.read().decode("utf-8")
            return response.status, json.loads(body) if body else None
    except urllib.error.HTTPError as error:
        body = error.read().decode("utf-8")
        return error.code, json.loads(body) if body else None


def published_loopback_port(compose: list[str]) -> int:
    endpoint = run(compose + ["port", "devmanager", "8080"], timeout_seconds=30).stdout.strip()
    host, separator, port_text = endpoint.rpartition(":")
    if separator != ":" or host != "127.0.0.1":
        raise RuntimeError("Docker Compose did not publish devmanager on a loopback IPv4 address")
    try:
        port = int(port_text)
    except ValueError as error:
        raise RuntimeError("Docker Compose did not report a numeric devmanager host port") from error
    if not 1 <= port <= 65535:
        raise RuntimeError("Docker Compose reported an invalid devmanager host port")
    return port


def running_container_id(compose: list[str]) -> str:
    container_id = run(compose + ["ps", "--quiet", "devmanager"], timeout_seconds=30).stdout.strip()
    if not container_id:
        raise RuntimeError("Docker Compose did not report a running devmanager container")
    return container_id


def health_status(compose: list[str]) -> str:
    container_id = running_container_id(compose)
    return run(
        [
            "docker",
            "inspect",
            "--format",
            "{{if .State.Health}}{{.State.Health.Status}}{{else}}missing{{end}}",
            container_id,
        ],
        timeout_seconds=30,
    ).stdout.strip()


def wait_for_ready(port: int, compose: list[str]) -> None:
    deadline = time.monotonic() + 30
    last_observation = f"no HTTP response from loopback port {port}"
    while time.monotonic() < deadline:
        try:
            http_health_status, health = request_json(port, "/health")
            ready_status, ready = request_json(port, "/ready")
            container_health = health_status(compose)
            last_observation = (
                f"health={http_health_status}/{health!r}, "
                f"ready={ready_status}/{ready!r}, docker_health={container_health!r}"
            )
            if (
                http_health_status == 200
                and health == {"status": "ok"}
                and ready_status == 200
                and ready == {"status": "ready"}
                and container_health == "healthy"
            ):
                return
        except (OSError, urllib.error.URLError, TimeoutError, ValueError) as error:
            last_observation = (
                f"HTTP request unavailable from loopback port {port}: "
                f"{type(error).__name__}: {error}"
            )
        time.sleep(0.2)
    raise RuntimeError(
        "Docker Compose service did not become healthy and ready within 30 seconds: "
        f"{last_observation}"
    )


def assert_key_is_absent_from_logs(compose: list[str], api_key: str) -> None:
    container_id = running_container_id(compose)
    file_log = run(
        ["docker", "exec", container_id, "cat", "/var/log/devmanager/devmanager.log"],
        timeout_seconds=30,
    ).stdout
    if api_key in file_log:
        raise RuntimeError("Docker Compose file log exposed the generated API key")

    compose_log = run(compose + ["logs", "--no-color", "devmanager"], timeout_seconds=30).stdout
    if api_key in compose_log:
        raise RuntimeError("Docker Compose service log exposed the generated API key")


def main() -> int:
    arguments = sys.argv[1:]
    if any(argument != "--skip-build" for argument in arguments) or arguments.count("--skip-build") > 1:
        raise RuntimeError("usage: check_docker_compose.py [--skip-build]")
    skip_build = "--skip-build" in arguments

    if not COMPOSE_FILE.is_file():
        raise RuntimeError(f"Compose file is missing: {COMPOSE_FILE}")

    project_name = f"devmanager-v09-{uuid.uuid4().hex[:12]}"
    api_key = f"docker-smoke-{uuid.uuid4().hex}"

    with tempfile.TemporaryDirectory(prefix="devmanager-v09-compose-") as temporary:
        environment_file = Path(temporary) / ".env"
        environment_file.write_text(
            f"DEVMANAGER_API_KEY={api_key}\nDEVMANAGER_PORT=\n", encoding="utf-8"
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
        cleanup_failure: Exception | None = None
        primary_failure: BaseException | None = None
        try:
            if not skip_build:
                run(compose + ["build"], timeout_seconds=900)
            run(compose + ["up", "--detach"], timeout_seconds=120)
            port = published_loopback_port(compose)
            wait_for_ready(port, compose)

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

            run(compose + ["restart", "devmanager"], timeout_seconds=60)
            port = published_loopback_port(compose)
            wait_for_ready(port, compose)
            status, body = request_json(port, "/api/projects", api_key=api_key)
            if status != 200 or not isinstance(body, list) or not any(item.get("name") == project_name_value for item in body if isinstance(item, dict)):
                raise RuntimeError("SQLite project data did not survive Docker Compose restart")

            assert_key_is_absent_from_logs(compose, api_key)
            run(compose + ["stop", "devmanager"], timeout_seconds=60)
        except BaseException as error:
            primary_failure = error
            raise
        finally:
            try:
                run(
                    compose + ["down", "--volumes", "--remove-orphans"],
                    timeout_seconds=60,
                )
            except Exception as error:
                cleanup_failure = error
                if primary_failure is None:
                    raise
                print(
                    f"Docker Compose smoke cleanup also failed: {cleanup_failure}",
                    file=sys.stderr,
                )

    print("Docker Compose smoke test passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"Docker Compose smoke test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
