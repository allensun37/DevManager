"""Lightweight contract guard for the checked-in OpenAPI document.

The full schema validation runs in CI with a pinned validator.  This local
check intentionally uses only the Python standard library so contributors can
quickly detect an incomplete contract without installing dependencies.
"""

from __future__ import annotations

import pathlib
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
)


def main() -> int:
    document = pathlib.Path(__file__).resolve().parents[1] / "docs" / "openapi.yaml"
    if not document.is_file():
        print(f"OpenAPI document is missing: {document}", file=sys.stderr)
        return 1

    contents = document.read_text(encoding="utf-8")
    missing = [marker for marker in REQUIRED_MARKERS if marker not in contents]
    if missing:
        print("OpenAPI contract is incomplete; missing markers:", file=sys.stderr)
        for marker in missing:
            print(f"  - {marker}", file=sys.stderr)
        return 1

    print(f"OpenAPI contract markers verified: {document}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
