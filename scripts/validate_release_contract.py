"""Validate the checked-in v0.6 release contract without third-party packages.

This is intentionally a small, deterministic guard for local release checks. The
OpenAPI schema itself is validated separately with the pinned validator.
"""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
VERSION = "0.6.0"


def read(relative: str) -> str:
    path = ROOT / relative
    if not path.is_file():
        raise AssertionError(f"required file is missing: {relative}")
    return path.read_text(encoding="utf-8")


def require(contents: str, marker: str, source: str) -> None:
    if marker not in contents:
        raise AssertionError(f"{source} is missing required marker: {marker}")


def main() -> int:
    cmake = read("CMakeLists.txt")
    version_template = read("cmake/DevManagerVersion.h.in")
    openapi = read("docs/openapi.yaml")
    readme = read("README.md")
    example_config = read("config/devmanager.example.json")
    workflow = read(".github/workflows/ci.yml")

    if not re.search(rf"project\(\s*DevManager\s+VERSION\s+{re.escape(VERSION)}\b", cmake):
        raise AssertionError("CMake project version is not 0.6.0")
    require(version_template, "@PROJECT_VERSION@", "cmake/DevManagerVersion.h.in")
    require(openapi, f"version: {VERSION}", "docs/openapi.yaml")

    for marker in (
        f"v{VERSION}",
        "config/devmanager.json",
        "config/devmanager.example.json",
        '"storage": { "type": "json", "path": "data/projects.json" }',
        '"storage": { "type": "sqlite", "path": "data/devmanager.db" }',
        "JSON 和 SQLite 是互斥、独立的后端",
        "不会自动迁移数据，也不会删除原后端的数据",
        "migration 只对 SQLite 后端执行",
        "page` 和 `size` 是可选的分页参数",
        "不提供 `page`/`size` 时保持 v0.4 的数组契约",
        "/health",
        "/api/info",
        "/api/statistics",
        "X-Request-ID",
        "ctest --test-dir build-v06-final -C Debug --output-on-failure",
        "C++17",
        "DEVMANAGER_API_KEY",
        "Authorization: Bearer <API_KEY>",
        "`/health` does not require an API key",
        "HTTP startup fails",
        "401 unauthorized",
        "v0.5 pagination and error contracts remain unchanged",
    ):
        require(readme, marker, "README.md")

    for marker in (
        '"host": "127.0.0.1"',
        '"port": 8080',
        '"type": "sqlite"',
        '"path": "data/devmanager.db"',
        '"level": "info"',
        '"path": "logs/devmanager.log"',
    ):
        require(example_config, marker, "config/devmanager.example.json")

    for marker in (
        "ubuntu-latest",
        "windows-latest",
        "python-version: '3.12.4'",
        "python -m openapi_spec_validator docs/openapi.yaml",
        "ctest --test-dir build -C Debug --output-on-failure",
    ):
        require(workflow, marker, ".github/workflows/ci.yml")

    print(f"v{VERSION} release contract verified")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"release contract check failed: {error}", file=sys.stderr)
        raise SystemExit(1)
