"""Lightweight contract guard for the checked-in OpenAPI document.

The full schema validation runs in CI with a pinned validator.  This local
check intentionally uses only the Python standard library so contributors can
quickly detect an incomplete contract without installing dependencies.
"""

from __future__ import annotations

import pathlib
import re
import sys


REQUIRED_MARKERS = (
    "openapi: 3.0.3",
    "GET /api/projects",
    "POST /api/projects",
    "PUT /api/projects/{id}",
    "DELETE /api/projects/{id}",
    "GET /health",
    "GET /ready",
    "GET /api/info",
    "GET /api/statistics",
    "techStack",
    "description",
    "status",
    "X-Request-ID",
    "invalid_query",
    "project_not_found",
    "persistence_failure",
    "invalid_request",
    "invalid_json",
    "internal_error",
    "id_exhausted",
    "'201'",
    "'200'",
    "'204'",
    "'400'",
    "'404'",
    "'409'",
    "'500'",
    "name: page",
    "name: size",
    "X-Total-Count",
    "X-Page",
    "X-Page-Size",
    "At most one of name, technology, and status",
    "JSON/SQLite",
    "not an items/total envelope",
    "securitySchemes:",
    "bearerApiKey:",
    "scheme: bearer",
    "DEVMANAGER_API_KEY",
    "WWW-Authenticate",
    "unauthorized",
    "not_ready",
    "payload_too_large",
    "'413'",
    "'503'",
)

PROTECTED_OPERATIONS = (
    ("/api/projects", "get"),
    ("/api/projects", "post"),
    ("/api/projects/{id}", "put"),
    ("/api/projects/{id}", "delete"),
    ("/api/info", "get"),
    ("/api/statistics", "get"),
)


def require_pattern(contents: str, pattern: str, label: str) -> str | None:
    if re.search(pattern, contents, flags=re.DOTALL) is None:
        return label
    return None


def operation_block(contents: str, path: str, method: str) -> str | None:
    pattern = (
        rf"^\s{{2}}{re.escape(path)}:\s*\n"
        rf"(?:(?!^\s{{2}}/\S).)*?"
        rf"^\s{{4}}{re.escape(method)}:\s*\n"
        rf"(?P<operation>.*?)(?=^\s{{4}}(?:get|post|put|delete):\s|\Z)"
    )
    match = re.search(pattern, contents, flags=re.DOTALL | re.MULTILINE)
    return match.group("operation") if match is not None else None


def validate_protected_operations(contents: str) -> list[str]:
    missing: list[str] = []
    for path, method in PROTECTED_OPERATIONS:
        label = f"{method.upper()} {path}"
        operation = operation_block(contents, path, method)
        if operation is None:
            missing.append(f"{label} operation")
            continue
        if re.search(
            r"^\s{6}security:\s*\n\s*-\s+bearerApiKey:\s*\[\]",
            operation,
            flags=re.MULTILINE,
        ) is None:
            missing.append(f"{label} bearer security")
        if re.search(
            r"'401':\s*\n\s+\$ref:\s*'#/components/responses/Unauthorized'",
            operation,
        ) is None:
            missing.append(f"{label} 401 Unauthorized response")
    return missing


def main() -> int:
    document = pathlib.Path(__file__).resolve().parents[1] / "docs" / "openapi.yaml"
    if not document.is_file():
        print(f"OpenAPI document is missing: {document}", file=sys.stderr)
        return 1

    contents = document.read_text(encoding="utf-8")
    missing = [marker for marker in REQUIRED_MARKERS if marker not in contents]
    missing.extend(
        marker
        for marker in (
            require_pattern(
                contents,
                r"name:\s+page(?:(?!\n\s*- name:).){0,500}"
                r"type:\s+integer(?:(?!\n\s*- name:).){0,300}minimum:\s+1",
                "page integer minimum: 1",
            ),
            require_pattern(
                contents,
                r"name:\s+size(?:(?!\n\s*- name:).){0,500}"
                r"type:\s+integer(?:(?!\n\s*- name:).){0,300}"
                r"minimum:\s+1(?:(?!\n\s*- name:).){0,300}maximum:\s+100",
                "size integer minimum: 1 maximum: 100",
            ),
            require_pattern(
                contents,
                r"/api/projects:\s*\n\s+get:.*?"
                r"responses:\s*\n\s+'200':.*?"
                r"content:\s*\n\s+application/json:.*?"
                r"schema:\s*\n\s+type:\s+array",
                "GET /api/projects 200 array schema",
            ),
            require_pattern(
                contents,
                r"/api/projects:\s*\n\s+get:.*?"
                r"responses:\s*\n\s+'200':.*?"
                r"headers:\s*\n.*?X-Total-Count:.*?"
                r"X-Page:.*?X-Page-Size:",
                "GET /api/projects pagination response headers",
            ),
            require_pattern(
                contents,
                r"/api/projects:\s*\n\s+get:.*?security:\s*\n\s+- bearerApiKey:",
                "GET /api/projects bearer security",
            ),
            require_pattern(
                contents,
                r"/api/projects:\s*\n\s+get:.*?responses:.*?'401':\s*\n\s+\$ref: '#/components/responses/Unauthorized'",
                "GET /api/projects 401 unauthorized response",
            ),
            require_pattern(
                contents,
                r"/ready:\s*\n\s+get:.*?responses:\s*\n\s+'200':.*?"
                r"X-Request-ID:.*?enum:\s*\[ready\]",
                "GET /ready 200 ready response with request ID",
            ),
            require_pattern(
                contents,
                r"/ready:\s*\n\s+get:.*?responses:.*?'503':\s*\n\s+"
                r"\$ref:\s*'#/components/responses/NotReady'",
                "GET /ready 503 not ready response",
            ),
            require_pattern(
                contents,
                r"post:.*?responses:.*?'413':\s*\n\s+"
                r"\$ref:\s*'#/components/responses/PayloadTooLarge'",
                "POST /api/projects 413 payload too large response",
            ),
            require_pattern(
                contents,
                r"put:.*?responses:.*?'413':\s*\n\s+"
                r"\$ref:\s*'#/components/responses/PayloadTooLarge'",
                "PUT /api/projects/{id} 413 payload too large response",
            ),
        )
        if marker is not None
    )
    missing.extend(validate_protected_operations(contents))
    if missing:
        print("OpenAPI contract is incomplete; missing markers:", file=sys.stderr)
        for marker in missing:
            print(f"  - {marker}", file=sys.stderr)
        return 1

    print(f"OpenAPI contract markers verified: {document}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
