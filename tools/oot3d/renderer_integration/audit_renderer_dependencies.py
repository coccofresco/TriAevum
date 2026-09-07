#!/usr/bin/env python3
"""Audit the dependency boundary of the native OoT3D renderer."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inl"}

SCOPES = {
    "renderer_core": (
        "runtime/three_ds_recomp/include/fast/oot3d/**/*",
        "runtime/three_ds_recomp/src/fast/oot3d/**/*",
    ),
    "renderer_vulkan_backend": (
        "runtime/three_ds_recomp/include/fast/backends/gfx_vulkan*",
        "runtime/three_ds_recomp/include/fast/backends/oot3d_vulkan*",
        "runtime/three_ds_recomp/src/fast/backends/gfx_vulkan*",
        "runtime/three_ds_recomp/src/fast/backends/oot3d_vulkan*",
    ),
    "native_renderer_adapter": (
        "runtime/three_ds_recomp/include/three_ds_recomp/oot3d/**/*",
        "runtime/three_ds_recomp/src/oot3d/**/*",
    ),
    "native_game_frontend": (
        "tools/oot3d/native_demo_host/**/*",
        "tools/oot3d/native_game_runtime/**/*",
        "tools/oot3d/native_pica_frontend/**/*",
        "tools/oot3d/ui_topscreen/**/*",
        "tools/oot3d/ui_n64/**/*",
    ),
}

DEPENDENCY_PATTERNS = {
    "legacy_runtime_api": re.compile(
        r'#\s*include\s*[<"](?:ship|runtime/three_ds_recomp)/'
        r"|\bShip::|\bnamespace\s+Ship\b"
    ),
    "n64": re.compile(
        r'#\s*include\s*[<"](?:soh|ultra64|libultra)/'
        r'|#\s*include\s*[<"]fast/(?:lus_gbi|f3d|f3dex|f3dex2)\.h'
        r"|\b(?:G_TX_|G_IM_|G_CULL_|G_ZBUFFER|SHADER_TEXEL[01A]*)"
    ),
    "fast3d_adapter": re.compile(
        r'#\s*include\s*[<"]fast/(?:backends/gfx_rendering_api'
        r"|interpreter|lus_gbi|f3d|f3dex|f3dex2)\.h"
        r"|\b(?:Fast::)?GfxRenderingAPI\b"
    ),
}

TARGET_LINK_PATTERN = re.compile(
    r"target_link_libraries\s*\(\s*([A-Za-z0-9_:+.-]+)\s+(.*?)\)",
    re.DOTALL | re.IGNORECASE,
)
TARGET_ALIAS_PATTERN = re.compile(
    r"add_(?:library|executable)\s*\(\s*([A-Za-z0-9_:+.-]+)"
    r"\s+ALIAS\s+([A-Za-z0-9_:+.-]+)\s*\)",
    re.IGNORECASE,
)
TARGET_TOKEN_PATTERN = re.compile(r"\$<[^>]+>|[A-Za-z0-9_:+.-]+")
LINK_QUALIFIERS = {
    "PUBLIC",
    "PRIVATE",
    "INTERFACE",
    "LINK_PRIVATE",
    "LINK_PUBLIC",
    "debug",
    "optimized",
    "general",
}


def _source_files(repo_root: Path, patterns: Iterable[str]) -> list[Path]:
    files: set[Path] = set()
    for pattern in patterns:
        for candidate in repo_root.glob(pattern):
            if candidate.is_file() and candidate.suffix.lower() in SOURCE_SUFFIXES:
                files.add(candidate)
    return sorted(files)


def _relative(repo_root: Path, path: Path) -> str:
    return path.relative_to(repo_root).as_posix()


def _scan_scope(repo_root: Path, patterns: Iterable[str]) -> dict:
    files = _source_files(repo_root, patterns)
    dependency_files: dict[str, list[str]] = {
        category: [] for category in DEPENDENCY_PATTERNS
    }
    evidence: dict[str, list[dict]] = {
        category: [] for category in DEPENDENCY_PATTERNS
    }
    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        lines = text.splitlines()
        for category, pattern in DEPENDENCY_PATTERNS.items():
            matches = [
                (index + 1, line.strip())
                for index, line in enumerate(lines)
                if pattern.search(line)
            ]
            if not matches:
                continue
            relative = _relative(repo_root, path)
            dependency_files[category].append(relative)
            evidence[category].append(
                {
                    "path": relative,
                    "first_line": matches[0][0],
                    "first_match": matches[0][1],
                    "match_count": len(matches),
                }
            )
    return {
        "file_count": len(files),
        "dependency_file_counts": {
            category: len(paths)
            for category, paths in dependency_files.items()
        },
        "dependency_files": dependency_files,
        "evidence": evidence,
    }


def _cmake_link_graph(repo_root: Path) -> dict[str, set[str]]:
    cmake_path = repo_root / "CMakeLists.txt"
    text = cmake_path.read_text(encoding="utf-8", errors="replace")
    graph: dict[str, set[str]] = {}
    for match in TARGET_ALIAS_PATTERN.finditer(text):
        graph.setdefault(match.group(1), set()).add(match.group(2))
    for match in TARGET_LINK_PATTERN.finditer(text):
        target = match.group(1)
        dependencies = graph.setdefault(target, set())
        for token in TARGET_TOKEN_PATTERN.findall(match.group(2)):
            if token in LINK_QUALIFIERS or token.startswith("$<"):
                continue
            dependencies.add(token)
    return graph


def _reachable_link_report(graph: dict[str, set[str]], root_target: str) -> dict:
    visited: set[str] = set()
    stack = [root_target]
    runtime_edges: list[dict[str, str]] = []
    while stack:
        target = stack.pop()
        if target in visited:
            continue
        visited.add(target)
        for dependency in sorted(graph.get(target, ())):
            if dependency == "three_ds_recomp_runtime":
                runtime_edges.append(
                    {"from": target, "to": dependency}
                )
            if dependency in graph and dependency not in visited:
                stack.append(dependency)
    return {
        "root_target": root_target,
        "reachable_target_count": len(visited),
        "reachable_targets": sorted(visited),
        "runtime_edge_count": len(runtime_edges),
        "runtime_edges": runtime_edges,
    }


def _load_policy(path: Path | None) -> dict | None:
    if path is None:
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def _evaluate_policy(report: dict, policy: dict | None) -> dict:
    if policy is None:
        return {
            "ratchet_status": "not_evaluated",
            "goal_complete": False,
            "violations": [],
        }

    violations: list[dict] = []
    all_dependency_counts: list[int] = []
    for scope_name, limits in policy.get("scope_file_limits", {}).items():
        actual_scope = report["scopes"].get(scope_name)
        if actual_scope is None:
            violations.append(
                {
                    "kind": "missing_scope",
                    "scope": scope_name,
                }
            )
            continue
        for category, maximum in limits.items():
            actual = actual_scope["dependency_file_counts"].get(category, 0)
            all_dependency_counts.append(actual)
            if actual > maximum:
                violations.append(
                    {
                        "kind": "scope_file_limit",
                        "scope": scope_name,
                        "category": category,
                        "actual": actual,
                        "maximum": maximum,
                    }
                )

    cmake_maximum = policy.get("reachable_runtime_edge_maximum")
    cmake_actual = report["cmake"]["runtime_edge_count"]
    all_dependency_counts.append(cmake_actual)
    if cmake_maximum is not None and cmake_actual > cmake_maximum:
        violations.append(
            {
                "kind": "reachable_runtime_edge_limit",
                "actual": cmake_actual,
                "maximum": cmake_maximum,
            }
        )

    reachable_targets = set(report["cmake"]["reachable_targets"])
    for target in policy.get("forbidden_reachable_targets", []):
        if target in reachable_targets:
            violations.append(
                {
                    "kind": "forbidden_reachable_target",
                    "target": target,
                }
            )

    return {
        "ratchet_status": "passed" if not violations else "failed",
        "goal_complete": bool(all_dependency_counts)
        and all(value == 0 for value in all_dependency_counts),
        "violations": violations,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[3],
    )
    parser.add_argument("--policy", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--fail-on-goal-incomplete",
        action="store_true",
        help="Return non-zero until every tracked dependency reaches zero.",
    )
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    report = {
        "format": "oot3d_renderer_dependency_audit_v1",
        "repo_root": str(repo_root),
        "scopes": {
            name: _scan_scope(repo_root, patterns)
            for name, patterns in SCOPES.items()
        },
        "cmake": _reachable_link_report(
            _cmake_link_graph(repo_root), "oot3d_native_game"
        ),
    }
    policy = _load_policy(args.policy.resolve() if args.policy else None)
    report["policy"] = _evaluate_policy(report, policy)

    encoded = json.dumps(report, indent=2) + "\n"
    if args.output:
        output = args.output.resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")

    if report["policy"]["ratchet_status"] == "failed":
        return 1
    if args.fail_on_goal_incomplete and not report["policy"]["goal_complete"]:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
