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
)


def require_pattern(contents: str, pattern: str, label: str) -> str | None:
    if re.search(pattern, contents, flags=re.DOTALL) is None:
        return label
    return None


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
        )
        if marker is not None
    )
    if missing:
        print("OpenAPI contract is incomplete; missing markers:", file=sys.stderr)
        for marker in missing:
            print(f"  - {marker}", file=sys.stderr)
        return 1

    print(f"OpenAPI contract markers verified: {document}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
