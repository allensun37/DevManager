"""Verify that the HTTP executable refuses to start without an API key.

This smoke check is intentionally dependency-free and works with both the
single-configure Unix layout and the multi-configure Windows layout produced
by the GitHub Actions build.
"""

from __future__ import annotations

import os
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
API_KEY_ENV = "DEVMANAGER_API_KEY"
TIMEOUT_SECONDS = 10


def find_http_executable() -> pathlib.Path | None:
    candidates = (
        ROOT / "build" / "devmanager_http",
        ROOT / "build" / "devmanager_http.exe",
        ROOT / "build" / "Debug" / "devmanager_http",
        ROOT / "build" / "Debug" / "devmanager_http.exe",
    )
    return next((candidate for candidate in candidates if candidate.is_file()), None)


def environment_without_api_key() -> dict[str, str]:
    environment = dict(os.environ)
    for name in list(environment):
        if name.upper() == API_KEY_ENV:
            del environment[name]
    return environment


def main() -> int:
    executable = find_http_executable()
    if executable is None:
        print("HTTP startup smoke failed: devmanager_http executable was not found", file=sys.stderr)
        return 1

    try:
        process = subprocess.Popen(
            [str(executable)],
            cwd=ROOT,
            env=environment_without_api_key(),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except OSError as error:
        print(f"HTTP startup smoke failed to launch executable: {error}", file=sys.stderr)
        return 1

    try:
        output, _ = process.communicate(timeout=TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        output, _ = process.communicate()
        print(
            "HTTP startup smoke failed: process did not reject missing API key "
            f"within {TIMEOUT_SECONDS} seconds\n{output}",
            file=sys.stderr,
        )
        return 1

    if process.returncode == 0:
        print(
            "HTTP startup smoke failed: process exited successfully without an API key\n"
            f"{output}",
            file=sys.stderr,
        )
        return 1

    if API_KEY_ENV not in output:
        print(
            "HTTP startup smoke failed: non-zero exit did not identify the missing API key\n"
            f"{output}",
            file=sys.stderr,
        )
        return 1

    print(
        "HTTP startup smoke passed: devmanager_http rejected missing "
        f"{API_KEY_ENV} with exit code {process.returncode}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
