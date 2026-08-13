"""Exercise the Linux SIGTERM shutdown contract of devmanager_http.

This script is intentionally dependency-free so it can run after the normal
CMake build in the Ubuntu CI matrix entry. It verifies the public health and
readiness probes, sends SIGTERM, then checks graceful termination, listener
closure, and log redaction.
"""

from __future__ import annotations

import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
API_KEY = "ci-v07-sigterm-test-key"


def find_server() -> Path:
    candidates = (ROOT / "build" / "devmanager_http", ROOT / "build" / "Debug" / "devmanager_http")
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RuntimeError("devmanager_http executable was not produced by the CI build")


def reserve_loopback_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def get_json(port: int, path: str) -> tuple[int, object]:
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}", headers={"X-Request-ID": "sigterm-probe"}
    )
    with urllib.request.urlopen(request, timeout=0.5) as response:
        return response.status, json.loads(response.read().decode("utf-8"))


def wait_for_ready(port: int) -> None:
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        try:
            health_status, health = get_json(port, "/health")
            ready_status, ready = get_json(port, "/ready")
            if health_status == 200 and health == {"status": "ok"} and ready_status == 200 and ready == {"status": "ready"}:
                return
        except Exception:
            pass
        time.sleep(0.05)
    raise RuntimeError("HTTP service did not become healthy and ready")


def require_listener_closed(port: int) -> None:
    try:
        get_json(port, "/health")
    except Exception:
        return
    raise RuntimeError("HTTP listener still accepted requests after SIGTERM shutdown")


def main() -> int:
    if os.name == "nt":
        print("SIGTERM shutdown check is only executed by the Ubuntu CI job")
        return 0

    port = reserve_loopback_port()
    with tempfile.TemporaryDirectory(prefix="devmanager-v07-sigterm-") as temporary:
        working_directory = Path(temporary)
        config_directory = working_directory / "config"
        log_path = working_directory / "logs" / "devmanager.log"
        config_directory.mkdir()
        (config_directory / "devmanager.json").write_text(
            json.dumps(
                {
                    "server": {"host": "127.0.0.1", "port": port},
                    "storage": {"type": "json", "path": "data/projects.json"},
                    "logging": {"level": "info", "path": "logs/devmanager.log"},
                }
            ),
            encoding="utf-8",
        )
        environment = os.environ.copy()
        environment["DEVMANAGER_API_KEY"] = API_KEY
        process = subprocess.Popen(
            [str(find_server())],
            cwd=working_directory,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            wait_for_ready(port)
            process.send_signal(signal.SIGTERM)
            exit_code = process.wait(timeout=10)
            if exit_code != 0:
                raise RuntimeError(f"SIGTERM shutdown exited with {exit_code}")
            require_listener_closed(port)
            log_contents = log_path.read_text(encoding="utf-8")
            if "shutdown_requested" not in log_contents:
                raise RuntimeError("shutdown_requested was not recorded")
            if API_KEY in log_contents:
                raise RuntimeError("API key was written to the service log")
        finally:
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=5)

    print("HTTP SIGTERM shutdown check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
