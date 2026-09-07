#!/usr/bin/env python3
"""Build revision-pinned OOT3D source-owner closure packages.

The decompilation checkout is an immutable input. Tracked files are read from a
Git commit, while generated callgraph evidence is read once and identified by
content hash. No file is written below the decompilation root.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import re
import shutil
import subprocess
import tarfile
from collections import defaultdict, deque
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


FORMAT = "oot3d_source_owner_closure_v1"
GENERATOR_VERSION = "1.7.0"
REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_CONFIG = Path(__file__).with_name("owner_roots.json")
CLOSURE_SOURCE_STATUSES = frozenset(
    {"reviewed_semantic", "consumer_reviewed_semantic", "service_boundary"}
)
RUNTIME_IMPORT_ACCEPTANCE_EVIDENCE = (
    "target-proven callback field and ABI layout",
    "typed native entry lookup before fallback",
    "generic A32 fallback for unmigrated callback targets",
    "callback return and guest-memory differential tests",
    "savestate and Native30 owner-level differential tests",
    "Enhanced60 timing classification remains outside dispatch",
)

COMMENT_OR_LITERAL = re.compile(
    r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
    re.DOTALL,
)
FUNCTION_TAIL = re.compile(
    r"([A-Za-z_]\w*)\s*\([^;{}]*\)\s*"
    r"(?:__attribute__\s*\(\([^{}]*\)\)\s*)?$",
    re.DOTALL,
)
CONTROL_KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof"}
CONSTANT_PATTERN = re.compile(
    r"\b(?P<name>k[A-Za-z0-9_]+)\s*=\s*"
    r"(?P<value>0x[0-9A-Fa-f]+)U?\s*;"
)


@dataclass(frozen=True)
class CommitSnapshot:
    revision: str
    files: dict[str, bytes]
    ignored_worktree_changes: int


def canonical_sha256(value: object) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def payload_sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def normalize_entry(value: object) -> str:
    if isinstance(value, int) and not isinstance(value, bool):
        number = value
    else:
        text = str(value or "").strip()
        if text.lower().startswith("0x"):
            text = text[2:]
        try:
            number = int(text, 16)
        except ValueError as exc:
            raise ValueError(
                f"invalid OOT3D entry address: {value!r}"
            ) from exc
    if not 0 < number <= 0xFFFFFFFF:
        raise ValueError(f"OOT3D entry is outside the 32-bit range: {value!r}")
    return f"{number:08x}"


def display_entry(entry: str) -> str:
    return f"0x{int(entry, 16):08X}"


def build_owner_closure_coverage(
    owners: Iterable[dict[str, Any]],
) -> dict[str, Any]:
    owner_list = list(owners)
    membership_total = 0
    membership_resolved = 0
    unique_status: dict[str, bool] = {}
    for owner in owner_list:
        for member in owner.get("members", []):
            membership_total += 1
            resolved = member["source_status"] in CLOSURE_SOURCE_STATUSES
            membership_resolved += int(resolved)
            entry = normalize_entry(member["entry"])
            prior = unique_status.setdefault(entry, resolved)
            if prior != resolved:
                raise ValueError(
                    "owner closure contains inconsistent source status for "
                    f"{display_entry(entry)}"
                )

    unique_resolved = sum(unique_status.values())

    def coverage(resolved: int, total: int) -> dict[str, Any]:
        return {
            "resolved": resolved,
            "total": total,
            "open": total - resolved,
            "percent": (
                round(100.0 * resolved / total, 4) if total else 100.0
            ),
        }

    return {
        "owner_count": len(owner_list),
        "source_closed_owner_count": sum(
            bool(owner["source_graph_closed"]) for owner in owner_list
        ),
        "runtime_import_ready_owner_count": sum(
            bool(owner["runtime_import_ready"]) for owner in owner_list
        ),
        "owner_memberships": coverage(
            membership_resolved, membership_total
        ),
        "unique_entries": coverage(unique_resolved, len(unique_status)),
    }


def run_git(
    repository: Path, *arguments: str, text: bool = True
) -> subprocess.CompletedProcess[str] | subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(
            ["git", "-C", str(repository), *arguments],
            check=True,
            capture_output=True,
            text=text,
        )
    except FileNotFoundError as exc:
        raise ValueError("git is required for decomp source synchronization") from exc
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr
        if isinstance(detail, bytes):
            detail = detail.decode("utf-8", errors="replace")
        raise ValueError(
            f"git {' '.join(arguments)} failed in {repository}: "
            f"{str(detail or '').strip()}"
        ) from exc


def git_file(repository: Path, revision: str, path: str) -> bytes:
    result = run_git(repository, "show", f"{revision}:{path}", text=False).stdout
    if not isinstance(result, bytes):
        raise ValueError(f"git returned text for binary request: {path}")
    return result


def archive_commit_files(
    repository: Path, revision: str, paths: Iterable[str]
) -> CommitSnapshot:
    repository = repository.resolve()
    resolved = str(
        run_git(repository, "rev-parse", f"{revision}^{{commit}}").stdout
    ).strip()
    status = str(
        run_git(repository, "status", "--porcelain=v1", "-uno").stdout
    )
    ignored_changes = len([line for line in status.splitlines() if line])

    normalized_paths = sorted(
        {PurePosixPath(path).as_posix() for path in paths if path}
    )
    # Windows has a relatively small command-line limit.  A current decomp
    # snapshot can contain hundreds of maintained source/manifest paths, so
    # collapse the path list to the smallest tracked top-level directories
    # when the expanded argv would exceed that limit.  The archive is only an
    # immutable read of the pinned commit; the existing missing-path check
    # below still validates every requested path after extraction.
    archive_paths = normalized_paths
    if sum(len(path) + 1 for path in archive_paths) > 24000:
        archive_paths = sorted({path.split("/", 1)[0] for path in archive_paths})
    archive = run_git(
        repository,
        "archive",
        "--format=tar",
        resolved,
        *archive_paths,
        text=False,
    ).stdout
    if not isinstance(archive, bytes):
        raise ValueError("git archive unexpectedly returned text")

    files: dict[str, bytes] = {}
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as source:
        for member in source.getmembers():
            if not member.isfile():
                continue
            extracted = source.extractfile(member)
            if extracted is None:
                raise ValueError(f"cannot extract committed file: {member.name}")
            files[PurePosixPath(member.name).as_posix()] = extracted.read()
    missing = [
        path
        for path in normalized_paths
        if path not in files
        and not any(candidate.startswith(path.rstrip("/") + "/") for candidate in files)
    ]
    if missing:
        raise ValueError(f"commit {resolved} is missing required path: {missing[0]}")
    return CommitSnapshot(resolved, files, ignored_changes)


def read_json_bytes(payload: bytes, source: str) -> Any:
    try:
        return json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid JSON in {source}: {exc}") from exc


def read_csv_bytes(payload: bytes, source: str) -> list[dict[str, str]]:
    try:
        text = payload.decode("utf-8-sig")
    except UnicodeDecodeError as exc:
        raise ValueError(f"invalid UTF-8 CSV in {source}") from exc
    reader = csv.DictReader(io.StringIO(text))
    if reader.fieldnames is None:
        raise ValueError(f"CSV has no header: {source}")
    return [dict(row) for row in reader]


def strip_comments_and_literals(source: str) -> str:
    def replace(match: re.Match[str]) -> str:
        value = match.group(0)
        return "\n" * value.count("\n") if "\n" in value else " "

    return COMMENT_OR_LITERAL.sub(replace, source)


def function_definitions(payload: bytes) -> set[str]:
    raw = payload.decode("utf-8", errors="replace")
    macro_names = set(
        re.findall(r"(?m)^\s*#\s*define\s+([A-Za-z_]\w*)", raw)
    )
    source = strip_comments_and_literals(raw)
    result: set[str] = set()
    depth = 0
    for offset, character in enumerate(source):
        if character == "{":
            if depth == 0:
                prefix = source[max(0, offset - 1000) : offset].rstrip()
                match = FUNCTION_TAIL.search(prefix.split(";")[-1])
                if match and match.group(1) not in CONTROL_KEYWORDS:
                    result.add(match.group(1))
            depth += 1
        elif character == "}":
            depth = max(0, depth - 1)
    return result - macro_names


def recovered_manifest_paths(lane: dict[str, Any]) -> list[str]:
    result = [str(path) for path in lane.get("manifests", [])]
    legacy = lane.get("manifest")
    if legacy and str(legacy) not in result:
        result.append(str(legacy))
    return result


def load_commit_snapshot(repository: Path, revision: str) -> tuple[
    CommitSnapshot, dict[str, Any], dict[str, Any]
]:
    resolved = str(
        run_git(repository, "rev-parse", f"{revision}^{{commit}}").stdout
    ).strip()
    source_lanes = read_json_bytes(
        git_file(repository, resolved, "config/source_lanes.json"),
        "config/source_lanes.json",
    )
    reconstruction = read_json_bytes(
        git_file(repository, resolved, "config/reconstruction.json"),
        "config/reconstruction.json",
    )
    lanes = source_lanes["lanes"]
    semantic = lanes["portable_semantic"]
    certified = lanes.get("semantic_certified_bulk_c", {})
    bulk = lanes.get("bulk_recovered_c", {})
    paths = {
        "config/source_lanes.json",
        "config/reconstruction.json",
        "symbols/manual_symbols.csv",
        "metadata/exact_functions.csv",
        *semantic.get("source_files", []),
        *semantic.get("entry_manifests", []),
        *recovered_manifest_paths(certified),
        *bulk.get("source_files", []),
        *recovered_manifest_paths(bulk),
    }
    return (
        archive_commit_files(repository, resolved, paths),
        source_lanes,
        reconstruction,
    )


def classify_sources(
    snapshot: CommitSnapshot,
    source_lanes: dict[str, Any],
    reconstruction: dict[str, Any],
) -> dict[str, Any]:
    lanes = source_lanes["lanes"]
    semantic = lanes["portable_semantic"]
    certified = lanes.get("semantic_certified_bulk_c", {})
    bulk = lanes.get("bulk_recovered_c", {})
    manual_rows = read_csv_bytes(
        snapshot.files["symbols/manual_symbols.csv"],
        "symbols/manual_symbols.csv",
    )
    exact_rows = read_csv_bytes(
        snapshot.files["metadata/exact_functions.csv"],
        "metadata/exact_functions.csv",
    )

    names_to_entries: dict[str, set[str]] = defaultdict(set)
    manual_by_entry: dict[str, dict[str, str]] = {}
    for row in manual_rows:
        entry = normalize_entry(row["entry"])
        manual_by_entry[entry] = row
        for key in ("old_name", "new_name"):
            if row.get(key):
                names_to_entries[row[key]].add(entry)
    exact_entries: set[str] = set()
    for row in exact_rows:
        entry = normalize_entry(row["entry"])
        exact_entries.add(entry)
        if row.get("name"):
            names_to_entries[row["name"]].add(entry)

    reviewed_sources: dict[str, set[str]] = defaultdict(set)
    reviewed_names: set[str] = set()
    for path in semantic.get("source_files", []):
        definitions = function_definitions(snapshot.files[path])
        reviewed_names.update(definitions)
        for name in definitions:
            for entry in names_to_entries.get(name, set()):
                reviewed_sources[entry].add(path)

    for path in semantic.get("entry_manifests", []):
        for row in read_csv_bytes(snapshot.files[path], path):
            if not row.get("entry") or not row.get("name"):
                continue
            entry = normalize_entry(row["entry"])
            reviewed_names.add(row["name"])
            names_to_entries[row["name"]].add(entry)
            reviewed_sources[entry].add(row.get("source_file") or path)

    bulk_sources: dict[str, set[str]] = defaultdict(set)
    bulk_definitions: set[str] = set()
    for path in bulk.get("source_files", []):
        bulk_definitions.update(function_definitions(snapshot.files[path]))
    for path in recovered_manifest_paths(bulk):
        for row in read_csv_bytes(snapshot.files[path], path):
            if not row.get("entry") or not row.get("name"):
                continue
            if row["name"] not in bulk_definitions:
                continue
            bulk_sources[normalize_entry(row["entry"])].add(
                row.get("cohort_source") or path
            )

    checkpoint = reconstruction["checkpoint"]
    certified_by_entry: dict[str, dict[str, str]] = {}
    certified_manifest_entries: set[str] = set()
    for path in recovered_manifest_paths(certified):
        for row in read_csv_bytes(snapshot.files[path], path):
            if not row.get("entry") or not row.get("name"):
                continue
            entry = normalize_entry(row["entry"])
            if entry in certified_manifest_entries:
                raise ValueError(
                    "duplicate semantic-certified entry: "
                    f"{display_entry(entry)}"
                )
            certified_manifest_entries.add(entry)
            # Certification manifests are historical evidence. A later
            # reviewed/typed source promotion is authoritative and no longer
            # requires the old bulk body to remain in the active source lane.
            if entry in reviewed_sources:
                continue
            if entry not in bulk_sources:
                raise ValueError(
                    "semantic-certified entry lacks a bulk source body: "
                    f"{display_entry(entry)}"
                )
            certified_by_entry[entry] = row
    expected_certified = int(
        checkpoint.get(
            "semantic_certified_manifest_entries",
            checkpoint.get(
                "semantically_certified_bulk_c_bodies",
                len(certified_manifest_entries),
            ),
        )
    )
    if len(certified_manifest_entries) != expected_certified:
        raise ValueError(
            "semantic-certified manifest has "
            f"{len(certified_manifest_entries)} entries, expected "
            f"{expected_certified}"
        )
    all_target_linked = (
        int(checkpoint["target_linked_c_bodies"])
        == int(checkpoint["automated_function_coverage"])
    )
    return {
        "reviewed_sources": reviewed_sources,
        "certified_by_entry": certified_by_entry,
        "bulk_sources": bulk_sources,
        "exact_entries": exact_entries,
        "manual_by_entry": manual_by_entry,
        "reviewed_names": reviewed_names,
        "all_target_linked": all_target_linked,
    }


def runtime_file_payload(
    runtime_root: Path, relative_path: str
) -> tuple[bytes, Path]:
    relative = PurePosixPath(str(relative_path))
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(
            f"runtime consumer path escapes runtime root: {relative}"
        )
    candidate = runtime_root.joinpath(*relative.parts)
    if not candidate.is_file() or candidate.is_symlink():
        raise ValueError(
            f"runtime consumer file is missing or unsafe: {candidate}"
        )
    resolved_root = runtime_root.resolve()
    resolved_candidate = candidate.resolve()
    try:
        resolved_candidate.relative_to(resolved_root)
    except ValueError as exc:
        raise ValueError(
            f"runtime consumer path escapes runtime root: {relative}"
        ) from exc
    return candidate.read_bytes(), candidate


def load_consumer_semantic_overlays(
    runtime_root: Path, config: dict[str, Any]
) -> tuple[dict[str, dict[str, Any]], dict[str, Any] | None]:
    manifest_relative = config.get("consumer_semantic_overlay_manifest")
    if not manifest_relative:
        return {}, None
    manifest_payload, _ = runtime_file_payload(
        runtime_root, str(manifest_relative)
    )
    document = read_json_bytes(
        manifest_payload, str(manifest_relative)
    )
    if document.get("format") != "oot3d_consumer_semantic_overlays_v1":
        raise ValueError(
            "unsupported consumer semantic overlay manifest format"
        )

    by_entry: dict[str, dict[str, Any]] = {}
    entries: list[dict[str, Any]] = []
    for source in document.get("entries", []):
        entry = normalize_entry(source.get("entry"))
        if entry in by_entry:
            raise ValueError(
                "duplicate consumer semantic overlay entry: "
                f"{display_entry(entry)}"
            )
        if source.get("status") != "reviewed_semantic":
            raise ValueError(
                "consumer semantic overlay must be reviewed_semantic: "
                f"{display_entry(entry)}"
            )
        if not str(source.get("id", "")).strip() or not str(
            source.get("name", "")
        ).strip():
            raise ValueError(
                "consumer semantic overlay requires id and name: "
                f"{display_entry(entry)}"
            )
        implementation_files = [
            str(path) for path in source.get("implementation_files", [])
        ]
        verification_files = [
            str(path) for path in source.get("verification_files", [])
        ]
        if not implementation_files or not verification_files:
            raise ValueError(
                "consumer semantic overlay requires implementation and "
                f"verification files: {display_entry(entry)}"
            )
        if (
            not source.get("verification_cases")
            or not source.get("target_evidence")
            or not source.get("abi")
            or not source.get("layout")
            or not source.get("decomp_handoff")
        ):
            raise ValueError(
                "consumer semantic overlay lacks acceptance evidence: "
                f"{display_entry(entry)}"
            )

        file_hashes: dict[str, str] = {}
        for relative in dict.fromkeys(
            implementation_files + verification_files
        ):
            payload, _ = runtime_file_payload(runtime_root, relative)
            file_hashes[relative] = payload_sha256(payload)
        normalized = {
            **source,
            "entry": display_entry(entry),
            "implementation_files": implementation_files,
            "verification_files": verification_files,
            "file_sha256": file_hashes,
        }
        normalized["overlay_sha256"] = canonical_sha256(normalized)
        by_entry[entry] = normalized
        entries.append(normalized)

    entries.sort(key=lambda item: int(normalize_entry(item["entry"]), 16))
    summary = {
        "format": document["format"],
        "path": str(manifest_relative),
        "sha256": payload_sha256(manifest_payload),
        "entry_count": len(entries),
        "entries": entries,
    }
    return by_entry, summary


def load_owner_import_acceptance(
    runtime_root: Path,
    config: dict[str, Any],
    consumer_overlays: dict[str, dict[str, Any]],
    dynamic_contracts_by_owner: dict[str, list[dict[str, Any]]],
) -> tuple[dict[str, dict[str, Any]], dict[str, Any]]:
    owners = {
        str(owner["id"]): normalize_entry(owner["entry"])
        for owner in config.get("owners", [])
    }
    approved_list = [
        str(owner).strip() for owner in config.get("approved_owners", [])
    ]
    if any(not owner for owner in approved_list) or len(
        approved_list
    ) != len(set(approved_list)):
        raise ValueError("approved_owners contains an empty or duplicate ID")
    unknown_approved = sorted(set(approved_list) - set(owners))
    if unknown_approved:
        raise ValueError(
            f"approved owner is not configured: {unknown_approved[0]}"
        )

    overlay_by_id = {
        str(overlay["id"]): overlay for overlay in consumer_overlays.values()
    }
    required_text = (
        "abi_layout",
        "native30_differential",
        "savestate_contract",
        "callback_coverage",
        "enhanced60_timing",
        "performance_scope",
    )
    acceptance_by_owner: dict[str, dict[str, Any]] = {}
    for source in config.get("owner_import_acceptance", []):
        record = dict(source)
        owner_id = str(record.get("owner", "")).strip()
        if owner_id not in owners:
            raise ValueError(
                f"owner import acceptance has unknown owner: {owner_id}"
            )
        if owner_id in acceptance_by_owner:
            raise ValueError(
                f"duplicate owner import acceptance: {owner_id}"
            )
        if record.get("status") != "approved":
            raise ValueError(
                f"owner import acceptance must be approved: {owner_id}"
            )
        missing_text = [
            field
            for field in required_text
            if not str(record.get(field, "")).strip()
        ]
        if missing_text:
            raise ValueError(
                f"owner import acceptance {owner_id} lacks "
                f"{missing_text[0]}"
            )
        implementation_files = [
            str(path) for path in record.get("implementation_files", [])
        ]
        verification_files = [
            str(path) for path in record.get("verification_files", [])
        ]
        verification_cases = [
            str(case).strip()
            for case in record.get("verification_cases", [])
        ]
        if (
            not implementation_files
            or not verification_files
            or not verification_cases
            or any(not case for case in verification_cases)
            or not record.get("target_evidence")
        ):
            raise ValueError(
                f"owner import acceptance {owner_id} lacks executable "
                "verification evidence"
            )

        overlay_id = str(record.get("consumer_overlay", "")).strip()
        if overlay_id:
            overlay = overlay_by_id.get(overlay_id)
            if overlay is None:
                raise ValueError(
                    f"owner import acceptance {owner_id} references unknown "
                    f"consumer overlay: {overlay_id}"
                )
            if normalize_entry(overlay["entry"]) != owners[owner_id]:
                raise ValueError(
                    f"owner import acceptance {owner_id} references overlay "
                    "for a different entry"
                )
            unknown_cases = sorted(
                set(verification_cases)
                - set(overlay.get("verification_cases", []))
            )
            if unknown_cases:
                raise ValueError(
                    f"owner import acceptance {owner_id} references "
                    f"unverified case: {unknown_cases[0]}"
                )

        contracts = {
            str(contract["id"]): contract
            for contract in dynamic_contracts_by_owner.get(owner_id, [])
        }
        contract_ids = [
            str(contract).strip()
            for contract in record.get("dynamic_dispatch_contracts", [])
        ]
        for contract_id in contract_ids:
            contract = contracts.get(contract_id)
            if contract is None or contract.get("status") != "implemented":
                raise ValueError(
                    f"owner import acceptance {owner_id} requires an "
                    f"implemented dynamic contract: {contract_id}"
                )

        file_hashes: dict[str, str] = {}
        for relative in dict.fromkeys(
            implementation_files + verification_files
        ):
            payload, _ = runtime_file_payload(runtime_root, relative)
            file_hashes[relative] = payload_sha256(payload)
        normalized = {
            **record,
            "owner": owner_id,
            "entry": display_entry(owners[owner_id]),
            "implementation_files": implementation_files,
            "verification_files": verification_files,
            "verification_cases": verification_cases,
            "dynamic_dispatch_contracts": contract_ids,
            "file_sha256": file_hashes,
        }
        normalized["acceptance_sha256"] = canonical_sha256(normalized)
        acceptance_by_owner[owner_id] = normalized

    accepted = set(acceptance_by_owner)
    approved = set(approved_list)
    if approved != accepted:
        missing = sorted(approved - accepted)
        extra = sorted(accepted - approved)
        if missing:
            raise ValueError(
                f"approved owner lacks import acceptance: {missing[0]}"
            )
        raise ValueError(
            "owner import acceptance is not listed in approved_owners: "
            f"{extra[0]}"
        )

    entries = sorted(
        acceptance_by_owner.values(), key=lambda item: item["owner"]
    )
    return acceptance_by_owner, {
        "format": "oot3d_owner_import_acceptance_v1",
        "entry_count": len(entries),
        "entries": entries,
    }


def load_sidecar(path: Path) -> tuple[bytes, str]:
    if not path.is_file() or path.is_symlink():
        raise ValueError(f"required generated evidence is missing or unsafe: {path}")
    payload = path.read_bytes()
    return payload, payload_sha256(payload)


def load_callgraph(payload: bytes) -> tuple[
    dict[str, dict[str, Any]], dict[str, set[str]]
]:
    graph = read_json_bytes(payload, "analysis/callgraph.json")
    nodes: dict[str, dict[str, Any]] = {}
    for raw in graph.get("nodes", []):
        entry = normalize_entry(raw["entry"])
        nodes[entry] = dict(raw)
    adjacency: dict[str, set[str]] = defaultdict(set)
    for edge in graph.get("edges", []):
        caller = normalize_entry(edge["caller"])
        callee = normalize_entry(edge["callee"])
        if caller in nodes and callee in nodes:
            adjacency[caller].add(callee)
    return nodes, adjacency


def load_function_pointer_refs(
    payload: bytes | None,
) -> dict[str, list[dict[str, str]]]:
    if payload is None:
        return {}
    by_source: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in read_csv_bytes(payload, "analysis/function_pointer_refs.csv"):
        if not row.get("source_entry") or not row.get("target_entry"):
            continue
        normalized = dict(row)
        normalized["source_entry"] = normalize_entry(row["source_entry"])
        normalized["target_entry"] = normalize_entry(row["target_entry"])
        by_source[normalized["source_entry"]].append(normalized)
    return by_source


def parse_runtime_entries(
    runtime_root: Path, registrations: list[dict[str, Any]]
) -> tuple[set[str], list[dict[str, Any]]]:
    entries: set[str] = set()
    summaries: list[dict[str, Any]] = []
    for registration in registrations:
        header_names = [
            str(path) for path in registration.get("headers", [])
        ]
        legacy_header = registration.get("header")
        if legacy_header and str(legacy_header) not in header_names:
            header_names.append(str(legacy_header))
        if not header_names:
            raise ValueError(
                f"runtime registry {registration['id']} has no headers"
            )
        header_payloads = [
            runtime_file_payload(runtime_root, path)[0]
            for path in header_names
        ]
        source_payload, _ = runtime_file_payload(
            runtime_root, registration["source"]
        )
        text = "\n".join(
            payload.decode("utf-8")
            for payload in [*header_payloads, source_payload]
        )
        constants = {
            match.group("name"): normalize_entry(match.group("value"))
            for match in CONSTANT_PATTERN.finditer(text)
        }
        array_name = re.escape(registration["array"])
        array_match = re.search(
            rf"constexpr\s+std::array\s+{array_name}\s*\{{(?P<body>.*?)\}};",
            text,
            re.DOTALL,
        )
        if array_match is None:
            raise ValueError(
                f"runtime entry array {registration['array']} was not found"
            )
        names = re.findall(r"\b(k[A-Za-z0-9_]+)\b", array_match.group("body"))
        unresolved = sorted({name for name in names if name not in constants})
        if unresolved:
            raise ValueError(
                f"runtime registry {registration['id']} has unresolved constant "
                f"{unresolved[0]}"
            )
        registry_entries = {constants[name] for name in names}
        entries.update(registry_entries)
        summaries.append(
            {
                "id": registration["id"],
                "entry_count": len(registry_entries),
                "headers": header_names,
                "source": registration["source"],
                "array": registration["array"],
                "sha256": payload_sha256(
                    b"\0".join([*header_payloads, source_payload])
                ),
            }
        )
    return entries, summaries


def load_profiles(
    runtime_root: Path, profiles: list[dict[str, Any]]
) -> tuple[dict[str, dict[str, float]], list[dict[str, Any]]]:
    heat: dict[str, dict[str, float]] = defaultdict(dict)
    summaries: list[dict[str, Any]] = []
    for profile_config in profiles:
        path = runtime_root / profile_config["path"]
        payload = path.read_bytes()
        document = read_json_bytes(payload, str(path))
        if document.get("format") != "oot3d_hot_source_coverage_v1":
            raise ValueError(f"unsupported hot-source profile format: {path}")
        profile_id = profile_config["id"]
        weight = float(profile_config.get("weight", 1.0))
        for function in document.get("functions", []):
            entry = normalize_entry(function["entry"])
            heat[entry][profile_id] = float(
                function.get("total_profile_coverage", 0.0)
            )
        summaries.append(
            {
                "id": profile_id,
                "path": profile_config["path"],
                "weight": weight,
                "function_count": len(document.get("functions", [])),
                "sha256": payload_sha256(payload),
            }
        )
    return heat, summaries


def producer_source_status(
    entry: str, classification: dict[str, Any]
) -> tuple[str, list[str]]:
    if entry in classification["reviewed_sources"]:
        return (
            "reviewed_semantic",
            sorted(classification["reviewed_sources"][entry]),
        )
    if entry in classification["certified_by_entry"]:
        return (
            "semantic_certified_bulk",
            sorted(classification["bulk_sources"][entry]),
        )
    if entry in classification["exact_entries"]:
        return "exact_anchor_only", []
    if entry in classification["bulk_sources"]:
        return "bulk_only", sorted(classification["bulk_sources"][entry])
    if classification["all_target_linked"]:
        return "bulk_only", []
    return "unreviewed", []


def source_status(
    entry: str, classification: dict[str, Any]
) -> tuple[str, list[str]]:
    producer_status, producer_paths = producer_source_status(
        entry, classification
    )
    if producer_status == "reviewed_semantic":
        return producer_status, producer_paths
    overlay = classification["consumer_overlays"].get(entry)
    if overlay is not None:
        return (
            "consumer_reviewed_semantic",
            sorted(overlay["implementation_files"]),
        )
    return producer_status, producer_paths


def compile_boundaries(
    config: dict[str, Any],
) -> list[tuple[str, list[re.Pattern[str]], set[str], str]]:
    result = []
    for group in config.get("service_boundaries", []):
        result.append(
            (
                group["id"],
                [re.compile(pattern) for pattern in group.get("name_patterns", [])],
                {
                    normalize_entry(entry)
                    for entry in group.get("entries", [])
                },
                group.get("rationale", ""),
            )
        )
    return result


def boundary_for(
    entry: str,
    name: str,
    boundaries: list[tuple[str, list[re.Pattern[str]], set[str], str]],
) -> str | None:
    for boundary_id, patterns, entries, _ in boundaries:
        if entry in entries or any(pattern.search(name) for pattern in patterns):
            return boundary_id
    return None


def compile_dynamic_dispatch_contracts(
    config: dict[str, Any],
) -> dict[str, list[dict[str, Any]]]:
    owner_ids = {str(owner["id"]) for owner in config.get("owners", [])}
    result: dict[str, list[dict[str, Any]]] = defaultdict(list)
    seen_ids: set[str] = set()
    for source_contract in config.get(
        "dynamic_dispatch_contracts", []
    ):
        contract = dict(source_contract)
        contract_id = str(contract.get("id", "")).strip()
        owner_id = str(contract.get("owner", "")).strip()
        status = str(contract.get("status", "")).strip()
        if not contract_id:
            raise ValueError("dynamic dispatch contract is missing an id")
        if contract_id in seen_ids:
            raise ValueError(
                f"duplicate dynamic dispatch contract: {contract_id}"
            )
        if owner_id not in owner_ids:
            raise ValueError(
                f"dynamic dispatch contract {contract_id} has unknown "
                f"owner: {owner_id}"
            )
        if status not in {"open", "implemented"}:
            raise ValueError(
                f"dynamic dispatch contract {contract_id} has unsupported "
                f"status: {status}"
            )
        fields = contract.get("callback_fields", [])
        if not isinstance(fields, list) or not fields:
            raise ValueError(
                f"dynamic dispatch contract {contract_id} has no "
                "callback fields"
            )
        field_names = [str(field.get("name", "")).strip() for field in fields]
        if any(not name for name in field_names):
            raise ValueError(
                f"dynamic dispatch contract {contract_id} has an unnamed "
                "callback field"
            )
        if len(set(field_names)) != len(field_names):
            raise ValueError(
                f"dynamic dispatch contract {contract_id} has duplicate "
                "callback fields"
            )
        evidence = contract.get("evidence", [])
        if not isinstance(evidence, list) or not evidence:
            raise ValueError(
                f"dynamic dispatch contract {contract_id} has no target "
                "evidence"
            )
        if status == "implemented" and (
            not contract.get("implementation")
            or not contract.get("verification")
        ):
            raise ValueError(
                f"implemented dynamic dispatch contract {contract_id} "
                "requires implementation and verification evidence"
            )
        contract["source"] = normalize_entry(contract.get("source"))
        seen_ids.add(contract_id)
        result[owner_id].append(contract)
    for contracts in result.values():
        contracts.sort(key=lambda item: str(item["id"]))
    return result


def traverse_owner(
    root: str,
    nodes: dict[str, dict[str, Any]],
    adjacency: dict[str, set[str]],
    boundaries: list[tuple[str, list[re.Pattern[str]], set[str], str]],
) -> tuple[dict[str, int], dict[str, str]]:
    if root not in nodes:
        raise ValueError(f"owner root is absent from callgraph: {display_entry(root)}")
    distance = {root: 0}
    boundary_hits: dict[str, str] = {}
    queue: deque[str] = deque([root])
    while queue:
        current = queue.popleft()
        if current != root:
            boundary = boundary_for(
                current, str(nodes[current].get("name", "")), boundaries
            )
            if boundary is not None:
                boundary_hits[current] = boundary
                continue
        for callee in sorted(adjacency.get(current, set())):
            if callee not in distance:
                distance[callee] = distance[current] + 1
                queue.append(callee)
    return distance, boundary_hits


def weighted_heat(
    entry: str,
    heat: dict[str, dict[str, float]],
    profile_weights: dict[str, float],
) -> float:
    return sum(
        value * profile_weights.get(profile_id, 1.0)
        for profile_id, value in heat.get(entry, {}).items()
    )


def build_snapshot(
    decomp_root: Path,
    runtime_root: Path,
    config_path: Path,
    revision: str = "HEAD",
) -> dict[str, Any]:
    config_payload = config_path.read_bytes()
    config = read_json_bytes(config_payload, str(config_path))
    if config.get("format") != "oot3d_source_owner_roots_v1":
        raise ValueError("unsupported source-owner configuration format")

    sidecar_checkout_revision = str(
        run_git(decomp_root, "rev-parse", "HEAD").stdout
    ).strip()
    commit, source_lanes, reconstruction = load_commit_snapshot(
        decomp_root, revision
    )
    classification = classify_sources(commit, source_lanes, reconstruction)
    consumer_overlays, consumer_overlay_summary = (
        load_consumer_semantic_overlays(runtime_root, config)
    )
    classification["consumer_overlays"] = consumer_overlays

    callgraph_payload, callgraph_hash = load_sidecar(
        decomp_root / "analysis" / "callgraph.json"
    )
    nodes, adjacency = load_callgraph(callgraph_payload)
    unknown_consumer_overlays = sorted(set(consumer_overlays) - set(nodes))
    if unknown_consumer_overlays:
        raise ValueError(
            "consumer semantic overlay is outside the target callgraph: "
            f"{display_entry(unknown_consumer_overlays[0])}"
        )
    for entry, overlay in consumer_overlays.items():
        target_name = str(nodes[entry].get("name", ""))
        if target_name != overlay["name"]:
            raise ValueError(
                "consumer semantic overlay name does not match target "
                f"callgraph at {display_entry(entry)}: "
                f"{overlay['name']} != {target_name}"
            )
    expected_nodes = int(
        reconstruction["checkpoint"]["automated_function_coverage"]
    )
    if len(nodes) != expected_nodes:
        raise ValueError(
            f"callgraph has {len(nodes)} nodes, expected {expected_nodes} "
            f"for commit {commit.revision}"
        )

    pointer_path = decomp_root / "analysis" / "function_pointer_refs.csv"
    pointer_payload: bytes | None = None
    pointer_hash: str | None = None
    if pointer_path.is_file() and not pointer_path.is_symlink():
        pointer_payload, pointer_hash = load_sidecar(pointer_path)
    pointer_refs = load_function_pointer_refs(pointer_payload)
    final_checkout_revision = str(
        run_git(decomp_root, "rev-parse", "HEAD").stdout
    ).strip()
    final_callgraph_hash = payload_sha256(
        (decomp_root / "analysis" / "callgraph.json").read_bytes()
    )
    final_pointer_hash = (
        payload_sha256(pointer_path.read_bytes())
        if pointer_payload is not None
        else None
    )
    if (
        final_checkout_revision != sidecar_checkout_revision
        or final_callgraph_hash != callgraph_hash
        or final_pointer_hash != pointer_hash
    ):
        raise ValueError(
            "decomp checkout or generated sidecars changed during analysis; "
            "rerun the synchronization"
        )

    runtime_entries, runtime_registries = parse_runtime_entries(
        runtime_root, config.get("runtime_entry_registries", [])
    )
    heat, profile_summaries = load_profiles(
        runtime_root, config.get("profiles", [])
    )
    profile_weights = {
        item["id"]: float(item.get("weight", 1.0))
        for item in config.get("profiles", [])
    }
    boundaries = compile_boundaries(config)
    dynamic_contracts_by_owner = compile_dynamic_dispatch_contracts(
        config
    )
    acceptance_by_owner, acceptance_summary = (
        load_owner_import_acceptance(
            runtime_root,
            config,
            consumer_overlays,
            dynamic_contracts_by_owner,
        )
    )
    approved = set(acceptance_by_owner)
    indirect_contracts = {
        (
            normalize_entry(item["source"]),
            normalize_entry(item["target"]),
        )
        for item in config.get("indirect_contracts", [])
    }
    blockers_by_owner: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for blocker in config.get("manual_blockers", []):
        blockers_by_owner[blocker["owner"]].append(dict(blocker))
    source_overrides: dict[str, dict[str, Any]] = {}
    for override in config.get("source_status_overrides", []):
        entry = normalize_entry(override["entry"])
        status = str(override["status"])
        if status in CLOSURE_SOURCE_STATUSES:
            raise ValueError(
                f"source override cannot grant importable status: "
                f"{display_entry(entry)}"
            )
        if entry in source_overrides:
            raise ValueError(
                f"duplicate source status override: {display_entry(entry)}"
            )
        source_overrides[entry] = dict(override)
    unknown_overrides = sorted(set(source_overrides) - set(nodes))
    if unknown_overrides:
        raise ValueError(
            "source status override is outside the target callgraph: "
            f"{display_entry(unknown_overrides[0])}"
        )

    registry: list[dict[str, Any]] = []
    registry_by_entry: dict[str, dict[str, Any]] = {}
    for entry, node in sorted(nodes.items()):
        producer_status, producer_paths = producer_source_status(
            entry, classification
        )
        original_status, source_paths = source_status(
            entry, classification
        )
        override = source_overrides.get(entry)
        status = (
            str(override["status"])
            if override is not None
            else original_status
        )
        row = {
            "entry": display_entry(entry),
            "name": str(node.get("name", "")),
            "signature": str(node.get("signature", "")),
            "source_status": status,
            "original_source_status": original_status,
            "source_status_override": override,
            "source_paths": source_paths,
            "producer_source_status": producer_status,
            "producer_source_paths": producer_paths,
            "consumer_source_overlay": consumer_overlays.get(entry),
            "source_certification": classification[
                "certified_by_entry"
            ].get(entry),
            "runtime_typed_entry": entry in runtime_entries,
            "caller_count": int(node.get("caller_count", 0)),
            "call_count": len(adjacency.get(entry, set())),
            "profile_heat": heat.get(entry, {}),
            "weighted_profile_heat": weighted_heat(
                entry, heat, profile_weights
            ),
        }
        registry.append(row)
        registry_by_entry[entry] = row

    owners: list[dict[str, Any]] = []
    work_items: dict[str, dict[str, Any]] = {}
    for owner_config in config["owners"]:
        owner_id = owner_config["id"]
        root = normalize_entry(owner_config["entry"])
        distance, boundary_hits = traverse_owner(
            root, nodes, adjacency, boundaries
        )
        dynamic_dispatch_contracts = []
        for contract in dynamic_contracts_by_owner.get(owner_id, []):
            source_entry = normalize_entry(contract["source"])
            if source_entry not in distance:
                raise ValueError(
                    f"dynamic dispatch contract {contract['id']} source "
                    f"{display_entry(source_entry)} is outside owner "
                    f"{owner_id}"
                )
            record = {
                **contract,
                "source_entry": display_entry(source_entry),
                "source_depth": distance[source_entry],
                "resolved": contract["status"] == "implemented",
            }
            record.pop("source", None)
            dynamic_dispatch_contracts.append(record)
        unresolved_dynamic_dispatch_contracts = [
            contract
            for contract in dynamic_dispatch_contracts
            if not contract["resolved"]
        ]
        members: list[dict[str, Any]] = []
        status_counts: dict[str, int] = defaultdict(int)
        direct_status_counts: dict[str, int] = defaultdict(int)
        candidate_producer_sources: set[str] = set()
        candidate_consumer_sources: set[str] = set()
        for entry, depth in sorted(
            distance.items(), key=lambda item: (item[1], int(item[0], 16))
        ):
            registry_row = registry_by_entry[entry]
            status = (
                "service_boundary"
                if entry in boundary_hits
                else registry_row["source_status"]
            )
            status_counts[status] += 1
            if depth == 1:
                direct_status_counts[status] += 1
            if status == "reviewed_semantic":
                candidate_producer_sources.update(
                    registry_row["source_paths"]
                )
            elif status == "consumer_reviewed_semantic":
                overlay = registry_row["consumer_source_overlay"]
                candidate_consumer_sources.update(
                    overlay["implementation_files"]
                )
                candidate_consumer_sources.update(
                    overlay["verification_files"]
                )
            members.append(
                {
                    "entry": registry_row["entry"],
                    "name": registry_row["name"],
                    "depth": depth,
                    "source_status": status,
                    "original_source_status": registry_row[
                        "original_source_status"
                    ],
                    "source_status_override": registry_row[
                        "source_status_override"
                    ],
                    "source_paths": registry_row["source_paths"],
                    "producer_source_status": registry_row[
                        "producer_source_status"
                    ],
                    "consumer_source_overlay": registry_row[
                        "consumer_source_overlay"
                    ],
                    "source_certification": registry_row[
                        "source_certification"
                    ],
                    "runtime_typed_entry": registry_row["runtime_typed_entry"],
                    "service_boundary": boundary_hits.get(entry),
                    "weighted_profile_heat": registry_row[
                        "weighted_profile_heat"
                    ],
                }
            )
            if status not in CLOSURE_SOURCE_STATUSES:
                item = work_items.setdefault(
                    entry,
                    {
                        "entry": display_entry(entry),
                        "name": registry_row["name"],
                        "source_status": status,
                        "source_certification": registry_row[
                            "source_certification"
                        ],
                        "owners": {},
                        "relations": set(),
                        "root_owner_count": 0,
                        "direct_owner_count": 0,
                        "runtime_typed_entry": registry_row[
                            "runtime_typed_entry"
                        ],
                        "caller_count": registry_row["caller_count"],
                        "weighted_profile_heat": registry_row[
                            "weighted_profile_heat"
                        ],
                    },
                )
                item["owners"][owner_id] = min(
                    depth, item["owners"].get(owner_id, depth)
                )
                item["relations"].add("direct_callgraph")
                if depth == 0:
                    item["root_owner_count"] += 1
                if depth == 1:
                    item["direct_owner_count"] += 1

        indirect: dict[tuple[str, str], dict[str, Any]] = {}
        unresolved_indirect: list[dict[str, Any]] = []
        for source_entry, source_depth in distance.items():
            if source_entry in boundary_hits:
                continue
            for ref in pointer_refs.get(source_entry, []):
                target = ref["target_entry"]
                key = (source_entry, target)
                record = indirect.setdefault(
                    key,
                    {
                        "source_entry": display_entry(source_entry),
                        "source_name": nodes[source_entry].get("name", ""),
                        "target_entry": display_entry(target),
                        "target_name": ref.get("target_name", ""),
                        "source_depth": source_depth,
                        "reference_count": 0,
                        "contracted": key in indirect_contracts,
                        "contexts": [],
                    },
                )
                record["reference_count"] += 1
                if len(record["contexts"]) < 3:
                    record["contexts"].append(ref.get("context", ""))
        for record in indirect.values():
            target = normalize_entry(record["target_entry"])
            target_status = registry_by_entry.get(target, {}).get(
                "source_status", "unknown"
            )
            record["target_source_status"] = target_status
            resolved = (
                record["contracted"]
                or target_status in CLOSURE_SOURCE_STATUSES
                or target in boundary_hits
            )
            record["resolved"] = resolved
            if not resolved:
                unresolved_indirect.append(record)
                if target in registry_by_entry:
                    registry_row = registry_by_entry[target]
                    item = work_items.setdefault(
                        target,
                        {
                            "entry": display_entry(target),
                            "name": registry_row["name"],
                            "source_status": target_status,
                            "source_certification": registry_row[
                                "source_certification"
                            ],
                            "owners": {},
                            "relations": set(),
                            "root_owner_count": 0,
                            "direct_owner_count": 0,
                            "runtime_typed_entry": registry_row[
                                "runtime_typed_entry"
                            ],
                            "caller_count": registry_row["caller_count"],
                            "weighted_profile_heat": registry_row[
                                "weighted_profile_heat"
                            ],
                        },
                    )
                    depth = int(record["source_depth"]) + 1
                    item["owners"][owner_id] = min(
                        depth, item["owners"].get(owner_id, depth)
                    )
                    item["relations"].add("indirect_target")

        manual_blockers = blockers_by_owner.get(owner_id, [])
        open_manual_blockers = [
            blocker
            for blocker in manual_blockers
            if blocker.get("status") != "resolved"
        ]
        source_graph_closed = (
            all(
                status in CLOSURE_SOURCE_STATUSES
                for status in status_counts
            )
            and not unresolved_indirect
            and not unresolved_dynamic_dispatch_contracts
            and not open_manual_blockers
        )
        expected_name = owner_config.get("expected_name", "")
        actual_name = str(nodes[root].get("name", ""))
        callgraph_identity_matches_expected = actual_name == expected_name
        identity_resolution = owner_config.get("identity_resolution")
        identity_resolved = False
        if identity_resolution is not None:
            identity_resolution = dict(identity_resolution)
            if identity_resolution.get("status") == "resolved":
                resolved_name = str(
                    identity_resolution.get("resolved_name", "")
                )
                if resolved_name != expected_name:
                    raise ValueError(
                        f"owner {owner_id} resolved identity "
                        f"{resolved_name!r} does not match expected name "
                        f"{expected_name!r}"
                    )
                identity_resolved = True
        identity_matches_expected = (
            callgraph_identity_matches_expected or identity_resolved
        )
        canonical_name = (
            expected_name if identity_matches_expected else actual_name
        )
        owners.append(
            {
                "id": owner_id,
                "entry": display_entry(root),
                "actual_name": actual_name,
                "canonical_name": canonical_name,
                "expected_name": expected_name,
                "callgraph_identity_matches_expected": (
                    callgraph_identity_matches_expected
                ),
                "identity_matches_expected": identity_matches_expected,
                "identity_resolution": identity_resolution,
                "identity_requires_target_verification": bool(
                    owner_config.get("identity_requires_target_verification")
                ),
                "domain": owner_config["domain"],
                "priority": int(owner_config["priority"]),
                "closure_function_count": len(distance),
                "max_direct_depth": max(distance.values()),
                "direct_call_count": len(adjacency.get(root, set())),
                "status_counts": dict(sorted(status_counts.items())),
                "direct_status_counts": dict(
                    sorted(direct_status_counts.items())
                ),
                "indirect_reference_edges": len(indirect),
                "unresolved_indirect_edges": len(unresolved_indirect),
                "dynamic_dispatch_contracts": dynamic_dispatch_contracts,
                "unresolved_dynamic_dispatch_contracts": len(
                    unresolved_dynamic_dispatch_contracts
                ),
                "manual_blockers": manual_blockers,
                "source_graph_closed": source_graph_closed,
                "approved_for_runtime_import": owner_id in approved,
                "runtime_import_acceptance": acceptance_by_owner.get(
                    owner_id
                ),
                "runtime_import_ready": (
                    source_graph_closed and owner_id in approved
                ),
                "runtime_import_validation": owner_config.get(
                    "runtime_import_validation", {}
                ),
                "candidate_source_files": sorted(
                    candidate_producer_sources
                    | candidate_consumer_sources
                ),
                "candidate_producer_source_files": sorted(
                    candidate_producer_sources
                ),
                "candidate_consumer_source_files": sorted(
                    candidate_consumer_sources
                ),
                "members": members,
                "indirect_dependencies": sorted(
                    indirect.values(),
                    key=lambda item: (
                        int(normalize_entry(item["source_entry"]), 16),
                        int(normalize_entry(item["target_entry"]), 16),
                    ),
                ),
            }
        )

    owner_priority = {
        item["id"]: int(item["priority"]) for item in config["owners"]
    }
    worklist: list[dict[str, Any]] = []
    for item in work_items.values():
        owner_contribution = sum(
            owner_priority[owner_id] * 1000.0 / (depth + 1)
            for owner_id, depth in item["owners"].items()
        )
        score = (
            item["root_owner_count"] * 1_000_000
            + item["direct_owner_count"] * 200_000
            + owner_contribution
            + item["weighted_profile_heat"] * 1_000_000
            + min(item["caller_count"], 1000) * 10
        )
        if item["runtime_typed_entry"]:
            score *= 0.9
        worklist.append(
            {
                **item,
                "owners": [
                    {"id": owner_id, "depth": depth}
                    for owner_id, depth in sorted(
                        item["owners"].items(),
                        key=lambda value: (
                            value[1],
                            -owner_priority[value[0]],
                            value[0],
                        ),
                    )
                ],
                "relations": sorted(item["relations"]),
                "priority_score": round(score, 3),
            }
        )
    worklist.sort(
        key=lambda item: (
            -float(item["priority_score"]),
            int(normalize_entry(item["entry"]), 16),
        )
    )
    owner_closure_coverage = build_owner_closure_coverage(owners)

    source = {
        "repository": str(decomp_root.resolve()),
        "revision": commit.revision,
        "sidecar_checkout_revision": sidecar_checkout_revision,
        "pinned_revision_matches_sidecar_checkout": (
            commit.revision == sidecar_checkout_revision
        ),
        "tracked_worktree_changes_ignored": commit.ignored_worktree_changes,
        "callgraph": {
            "mode": "generated_sidecar",
            "path": "analysis/callgraph.json",
            "sha256": callgraph_hash,
            "node_count": len(nodes),
            "edge_count": sum(len(values) for values in adjacency.values()),
        },
        "function_pointer_refs": {
            "mode": "generated_sidecar" if pointer_payload else "absent",
            "path": "analysis/function_pointer_refs.csv",
            "sha256": pointer_hash,
            "source_function_count": len(pointer_refs),
            "reference_count": sum(len(values) for values in pointer_refs.values()),
        },
    }
    configuration_hash = payload_sha256(config_payload)
    consumer_inputs = {
        "generator_version": GENERATOR_VERSION,
        "generator_sha256": payload_sha256(Path(__file__).read_bytes()),
        "configuration_sha256": configuration_hash,
        "callgraph_sha256": callgraph_hash,
        "function_pointer_refs_sha256": pointer_hash,
        "profiles": [
            {
                "id": item["id"],
                "sha256": item["sha256"],
                "weight": item["weight"],
            }
            for item in profile_summaries
        ],
        "runtime_entry_registries": [
            {"id": item["id"], "sha256": item["sha256"]}
            for item in runtime_registries
        ],
        "consumer_semantic_overlays": (
            {
                "path": consumer_overlay_summary["path"],
                "sha256": consumer_overlay_summary["sha256"],
                "entries": [
                    {
                        "entry": item["entry"],
                        "overlay_sha256": item["overlay_sha256"],
                    }
                    for item in consumer_overlay_summary["entries"]
                ],
            }
            if consumer_overlay_summary is not None
            else None
        ),
        "owner_import_acceptance": {
            "format": acceptance_summary["format"],
            "entries": [
                {
                    "owner": item["owner"],
                    "acceptance_sha256": item["acceptance_sha256"],
                    "file_sha256": item["file_sha256"],
                }
                for item in acceptance_summary["entries"]
            ],
        },
    }
    consumer_input_hash = canonical_sha256(consumer_inputs)
    snapshot_id = f"{commit.revision[:12]}-{consumer_input_hash[:12]}"
    return {
        "format": FORMAT,
        "generator_version": GENERATOR_VERSION,
        "snapshot_id": snapshot_id,
        "source": source,
        "configuration_sha256": configuration_hash,
        "consumer_inputs": consumer_inputs,
        "consumer_input_sha256": consumer_input_hash,
        "decomp_checkpoint": reconstruction["checkpoint"],
        "profiles": profile_summaries,
        "runtime_entry_registries": runtime_registries,
        "runtime_typed_entry_count": len(runtime_entries),
        "semantic_certified_bulk_count": len(
            classification["certified_by_entry"]
        ),
        "consumer_semantic_overlay_count": len(consumer_overlays),
        "consumer_semantic_overlays": consumer_overlay_summary,
        "owner_import_acceptance": acceptance_summary,
        "owner_closure_coverage": owner_closure_coverage,
        "runtime_source_root": str(runtime_root.resolve()),
        "source_status_overrides": list(source_overrides.values()),
        "service_boundaries": config.get("service_boundaries", []),
        "owners": owners,
        "worklist": worklist,
        "registry": registry,
        "bundle_support_paths": config.get("bundle_support_paths", []),
    }


def render_report(snapshot: dict[str, Any], previous: dict[str, Any] | None) -> str:
    checkpoint = snapshot["decomp_checkpoint"]
    lines = [
        "# OOT3D Recomp Source Owner Closure",
        "",
        "This package consumes a pinned decompilation commit. Generated Ghidra",
        "evidence is content-addressed and the decompilation checkout is never written.",
        "",
        f"- Snapshot: `{snapshot['snapshot_id']}`",
        f"- Decomp commit: `{snapshot['source']['revision']}`",
        f"- Generated-sidecar checkout: "
        f"`{snapshot['source']['sidecar_checkout_revision']}`",
        f"- Sidecars match pinned commit: "
        f"{'yes' if snapshot['source']['pinned_revision_matches_sidecar_checkout'] else 'no'}",
        f"- Ignored tracked worktree changes: "
        f"{snapshot['source']['tracked_worktree_changes_ignored']}",
        f"- Reviewed target-linked bodies: "
        f"{checkpoint['reviewed_target_linked_c_bodies']}/"
        f"{checkpoint['automated_function_coverage']}",
        f"- Semantic-certified bulk bodies awaiting cleanup: "
        f"{snapshot['semantic_certified_bulk_count']}",
        f"- Consumer-reviewed semantic overlays: "
        f"{snapshot['consumer_semantic_overlay_count']}",
        f"- Runtime-import acceptances: "
        f"{snapshot['owner_import_acceptance']['entry_count']}",
        f"- Runtime typed entries already present: "
        f"{snapshot['runtime_typed_entry_count']}",
        f"- Owner-membership semantic coverage: "
        f"{snapshot['owner_closure_coverage']['owner_memberships']['resolved']}/"
        f"{snapshot['owner_closure_coverage']['owner_memberships']['total']} "
        f"({snapshot['owner_closure_coverage']['owner_memberships']['percent']:.2f}%)",
        f"- Unique owner-closure semantic coverage: "
        f"{snapshot['owner_closure_coverage']['unique_entries']['resolved']}/"
        f"{snapshot['owner_closure_coverage']['unique_entries']['total']} "
        f"({snapshot['owner_closure_coverage']['unique_entries']['percent']:.2f}%)",
        f"- Source-closed owners: "
        f"{snapshot['owner_closure_coverage']['source_closed_owner_count']}/"
        f"{snapshot['owner_closure_coverage']['owner_count']}; "
        f"runtime-import-ready: "
        f"{snapshot['owner_closure_coverage']['runtime_import_ready_owner_count']}/"
        f"{snapshot['owner_closure_coverage']['owner_count']}",
        "",
        "## Owners",
        "",
        "| Owner | Root | Direct | Closure | Producer reviewed | Consumer reviewed | Missing | Indirect open | Dynamic open | Manual open | Import |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for owner in snapshot["owners"]:
        counts = owner["status_counts"]
        manual_open = sum(
            blocker.get("status") != "resolved"
            for blocker in owner["manual_blockers"]
        )
        root_name = f"`{owner['canonical_name']}`"
        if owner["canonical_name"] != owner["actual_name"]:
            root_name += f" (callgraph `{owner['actual_name']}`)"
        lines.append(
            f"| `{owner['id']}` | `{owner['entry']}` "
            f"{root_name} | {owner['direct_call_count']} | "
            f"{owner['closure_function_count']} | "
            f"{counts.get('reviewed_semantic', 0)} | "
            f"{counts.get('consumer_reviewed_semantic', 0)} | "
            f"{sum(count for status, count in counts.items() if status not in CLOSURE_SOURCE_STATUSES)} | "
            f"{owner['unresolved_indirect_edges']} | "
            f"{owner.get('unresolved_dynamic_dispatch_contracts', 0)} | "
            f"{manual_open} | "
            f"{'ready' if owner['runtime_import_ready'] else 'blocked'} |"
        )

    lines.extend(
        [
            "",
            "## Explicit Blockers",
            "",
        ]
    )
    manual_blockers = [
        blocker
        for owner in snapshot["owners"]
        for blocker in owner["manual_blockers"]
        if blocker.get("status") != "resolved"
    ]
    dynamic_blockers = [
        contract
        for owner in snapshot["owners"]
        for contract in owner.get("dynamic_dispatch_contracts", [])
        if not contract.get("resolved")
    ]
    if manual_blockers or dynamic_blockers:
        for blocker in manual_blockers:
            lines.append(
                f"- `{blocker['owner']}` / `{blocker['kind']}`: "
                f"{blocker['description']}"
            )
        for contract in dynamic_blockers:
            lines.append(
                f"- `{contract['owner']}` / `dynamic_dispatch` / "
                f"`{contract['id']}`: {contract['description']}"
            )
    else:
        lines.append("- None.")

    resolved_findings = [
        blocker
        for owner in snapshot["owners"]
        for blocker in owner["manual_blockers"]
        if blocker.get("status") == "resolved"
    ]
    lines.extend(["", "## Resolved Structural Findings", ""])
    if resolved_findings:
        for finding in resolved_findings:
            lines.append(
                f"- `{finding['owner']}` / `{finding['kind']}`: "
                f"{finding.get('resolution', finding['description'])}"
            )
    else:
        lines.append("- None.")

    lines.extend(["", "## Consumer-Reviewed Semantic Overlays", ""])
    overlay_summary = snapshot.get("consumer_semantic_overlays")
    if overlay_summary is not None and overlay_summary["entries"]:
        for overlay in overlay_summary["entries"]:
            producer = next(
                row
                for row in snapshot["registry"]
                if normalize_entry(row["entry"])
                == normalize_entry(overlay["entry"])
            )
            lines.append(
                f"- `{overlay['entry']}` `{overlay['name']}`: producer "
                f"status `{producer['producer_source_status']}`; handoff "
                "is recorded in `decomp_handoff.md` and "
                "`decomp_handoff.json`."
            )
    else:
        lines.append("- None.")

    quarantined = [
        row
        for row in snapshot["registry"]
        if row["source_status_override"] is not None
    ]
    lines.extend(["", "## Quarantined Source Promotions", ""])
    if quarantined:
        for row in quarantined:
            override = row["source_status_override"]
            lines.append(
                f"- `{row['entry']}` `{row['name']}` / "
                f"`{override['kind']}`: {override['description']} "
                f"Evidence: `{override['evidence']}`."
            )
    else:
        lines.append("- None.")

    certified_work = [
        item
        for item in snapshot["worklist"]
        if item["source_status"] == "semantic_certified_bulk"
    ]
    lines.extend(
        [
            "",
            "## Semantic-Certified Cleanup Frontier",
            "",
            f"- Target-wide certified bodies: "
            f"{snapshot['semantic_certified_bulk_count']}.",
            f"- Bodies reachable from configured owners: "
            f"{len(certified_work)}.",
            "",
        ]
    )
    if certified_work:
        for item in certified_work[:20]:
            certification = item["source_certification"] or {}
            lines.append(
                f"- `{item['entry']}` `{item['name']}`: "
                f"{certification.get('remaining_work', 'semantic cleanup')} "
                f"(parity {certification.get('parity', 'unknown')})."
            )
    else:
        lines.append("- None intersect the configured owner closures.")

    lines.extend(
        [
            "",
            "## Highest-Leverage Missing Functions",
            "",
            "| Rank | Score | Entry | Function | Status | Owners | Relations | Hot weight | Runtime leaf |",
            "| ---: | ---: | --- | --- | --- | --- | --- | ---: | --- |",
        ]
    )
    for rank, item in enumerate(snapshot["worklist"][:40], 1):
        owners = ", ".join(
            f"{owner['id']}@{owner['depth']}" for owner in item["owners"]
        )
        lines.append(
            f"| {rank} | {item['priority_score']:.1f} | "
            f"`{item['entry']}` | `{item['name']}` | "
            f"{item['source_status']} | {owners} | "
            f"{', '.join(item['relations'])} | "
            f"{item['weighted_profile_heat']:.6f} | "
            f"{'yes' if item['runtime_typed_entry'] else 'no'} |"
        )

    if previous is not None:
        delta = build_delta(snapshot, previous)
        previous_owners = {item["id"]: item for item in previous.get("owners", [])}
        lines.extend(
            [
                "",
                "## Delta From Previous Snapshot",
                "",
                f"- Baseline: `{previous['snapshot_id']}` "
                f"at `{previous['source']['revision']}`",
                f"- Complete consumer inputs match baseline: "
                f"{'yes' if delta['consumer_inputs_match'] else 'no'}",
                f"- Stable consumer contract matches baseline: "
                f"{'yes' if delta['consumer_contract_match'] else 'no'}",
                f"- Reviewed semantic promotions: "
                f"{len(delta['semantic_promotions'])}",
                f"- Promotions intersecting configured owners: "
                f"{len(delta['owner_relevant_semantic_promotions'])}",
                f"- Promotions outside configured owners: "
                f"{len(delta['owner_external_semantic_promotions'])}",
                f"- New semantic-certified bulk bodies: "
                f"{len(delta['certification_promotions'])}",
                f"- New consumer-reviewed semantic overlays: "
                f"{len(delta['consumer_semantic_promotions'])}",
                f"- Certified bodies graduated to reviewed source: "
                f"{len(delta['certification_graduations'])}",
                "",
                "| Owner | Producer-reviewed delta | Consumer-reviewed delta | Missing delta | Indirect-open delta | Dynamic-open delta |",
                "| --- | ---: | ---: | ---: | ---: | ---: |",
            ]
        )
        for owner in snapshot["owners"]:
            old = previous_owners.get(owner["id"])
            if old is None:
                continue
            new_counts = owner["status_counts"]
            old_counts = old["status_counts"]
            new_bulk = sum(
                count
                for status, count in new_counts.items()
                if status not in CLOSURE_SOURCE_STATUSES
            )
            old_bulk = sum(
                count
                for status, count in old_counts.items()
                if status not in CLOSURE_SOURCE_STATUSES
            )
            lines.append(
                f"| `{owner['id']}` | "
                f"{new_counts.get('reviewed_semantic', 0) - old_counts.get('reviewed_semantic', 0):+d} | "
                f"{new_counts.get('consumer_reviewed_semantic', 0) - old_counts.get('consumer_reviewed_semantic', 0):+d} | "
                f"{new_bulk - old_bulk:+d} | "
                f"{owner['unresolved_indirect_edges'] - old['unresolved_indirect_edges']:+d} | "
                f"{owner.get('unresolved_dynamic_dispatch_contracts', 0) - old.get('unresolved_dynamic_dispatch_contracts', 0):+d} |"
            )
        if delta["semantic_promotions"]:
            lines.extend(["", "### Promoted Entries", ""])
            for item in delta["semantic_promotions"]:
                lines.append(
                    f"- `{item['entry']}` `{item['name']}`: "
                    f"{item['previous_status']} -> "
                    f"{item['current_status']} "
                    f"({', '.join(item['owners']) if item['owners'] else 'outside configured owner closures'})"
                )

    lines.extend(
        [
            "",
            "## Import Rule",
            "",
            "A source candidate is materialized only when every reachable direct",
            "dependency is reviewed semantic C, every indirect edge has a reviewed",
            "target or an explicit contract, every dynamic dispatch boundary is",
            "implemented and verified, and all manual blockers are resolved.",
            "Runtime import additionally requires the owner ID in `approved_owners`",
            "after ABI, Native30 differential and timing tests pass.",
            "",
        ]
    )
    return "\n".join(lines)


def csv_text(rows: list[dict[str, Any]], fields: list[str]) -> str:
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, fieldnames=fields, lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow({field: row.get(field, "") for field in fields})
    return output.getvalue()


def build_decomp_handoff(snapshot: dict[str, Any]) -> dict[str, Any]:
    registry = {
        normalize_entry(row["entry"]): row for row in snapshot["registry"]
    }
    owners_by_entry = {
        normalize_entry(owner["entry"]): owner
        for owner in snapshot["owners"]
    }
    overlay_summary = snapshot.get("consumer_semantic_overlays")
    entries = []
    for overlay in (
        overlay_summary.get("entries", [])
        if overlay_summary is not None
        else []
    ):
        entry = normalize_entry(overlay["entry"])
        producer = registry.get(entry, {})
        producer_status = producer.get(
            "producer_source_status", "unknown"
        )
        owner = owners_by_entry.get(entry)
        entries.append(
            {
                "id": overlay["id"],
                "entry": overlay["entry"],
                "name": overlay["name"],
                "consumer_status": "consumer_reviewed_semantic",
                "producer_status": producer_status,
                "handoff_status": (
                    "producer_already_reviewed"
                    if producer_status == "reviewed_semantic"
                    else "ready_for_producer_review"
                ),
                "implementation_files": overlay[
                    "implementation_files"
                ],
                "verification_files": overlay["verification_files"],
                "verification_cases": overlay["verification_cases"],
                "file_sha256": overlay["file_sha256"],
                "overlay_sha256": overlay["overlay_sha256"],
                "abi": overlay["abi"],
                "layout": overlay["layout"],
                "target_code": overlay.get("target_code"),
                "target_evidence": overlay["target_evidence"],
                "decomp_handoff": overlay["decomp_handoff"],
                "runtime_import_acceptance": (
                    owner.get("runtime_import_acceptance")
                    if owner is not None
                    else None
                ),
            }
        )
    return {
        "format": "oot3d_consumer_to_decomp_handoff_v1",
        "snapshot_id": snapshot["snapshot_id"],
        "decomp_base_revision": snapshot["source"]["revision"],
        "consumer_input_sha256": snapshot["consumer_input_sha256"],
        "policy": {
            "decomp_checkout_was_read_only": True,
            "consumer_source_is_not_producer_source": True,
            "target_evidence_remains_authoritative": True,
            "producer_must_review_and_retest_before_promotion": True,
            "producer_may_supersede_consumer_implementation": True,
        },
        "entries": entries,
    }


def render_decomp_handoff(handoff: dict[str, Any]) -> str:
    lines = [
        "# OOT3D Consumer-to-Decomp Handoff",
        "",
        f"- Snapshot: `{handoff['snapshot_id']}`",
        f"- Decomp base: `{handoff['decomp_base_revision']}`",
        "",
        "Consumer-reviewed entries are independently reconstructed runtime",
        "implementations. They are evidence for producer review, not edits",
        "already made to the decomp repository.",
        "",
    ]
    for entry in handoff["entries"]:
        transfer = entry["decomp_handoff"]
        lines.extend(
            [
                f"## {entry['entry']} {entry['name']}",
                "",
                f"- Status: `{entry['handoff_status']}`",
                f"- Suggested source: `{transfer['suggested_source_path']}`",
                f"- Suggested header: `{transfer['suggested_header_path']}`",
                f"- Target code SHA-256: "
                f"`{(entry.get('target_code') or {}).get('sha256', 'unrecorded')}`",
                f"- Consumer overlay SHA-256: `{entry['overlay_sha256']}`",
                "",
                "Target evidence:",
                "",
                *[
                    f"- `{evidence}`"
                    for evidence in entry["target_evidence"]
                ],
                "",
                "```c",
                *transfer["semantic_body"],
                "```",
                "",
                "Required assertions:",
                "",
                *[
                    f"- `{assertion}`"
                    for assertion in transfer["required_assertions"]
                ],
                "",
                "Verification:",
                "",
                *[
                    f"- `{case}`"
                    for case in entry["verification_cases"]
                ],
                "",
            ]
        )
        acceptance = entry.get("runtime_import_acceptance")
        if acceptance is not None:
            lines.extend(
                [
                    "Runtime acceptance:",
                    "",
                    f"- Status: `{acceptance['status']}`",
                    f"- ABI/layout: {acceptance['abi_layout']}",
                    f"- Native30: {acceptance['native30_differential']}",
                    f"- Savestate: {acceptance['savestate_contract']}",
                    f"- Enhanced60: {acceptance['enhanced60_timing']}",
                    f"- A32 scope: {acceptance['performance_scope']}",
                    "",
                ]
            )
    if not handoff["entries"]:
        lines.extend(["No consumer-reviewed entries are pending.", ""])
    return "\n".join(lines)


def build_decomp_work_request(snapshot: dict[str, Any]) -> dict[str, Any]:
    manual_blockers = []
    resolved_findings = []
    for owner in snapshot["owners"]:
        for blocker in owner["manual_blockers"]:
            if blocker.get("status") == "resolved":
                resolved_findings.append(
                    {
                        **blocker,
                        "owner": owner["id"],
                        "owner_entry": owner["entry"],
                    }
                )
                continue
            manual_blockers.append(
                {
                    **blocker,
                    "owner": owner["id"],
                    "owner_entry": owner["entry"],
                }
            )
    work_by_entry = {
        normalize_entry(item["entry"]): (rank, item)
        for rank, item in enumerate(snapshot["worklist"], 1)
    }
    certified_cleanup = []
    for row in snapshot["registry"]:
        certification = row["source_certification"]
        if certification is None:
            continue
        work_rank, work = work_by_entry.get(
            normalize_entry(row["entry"]),
            (None, None),
        )
        certified_cleanup.append(
            {
                "entry": row["entry"],
                "name": row["name"],
                "owner_worklist_rank": work_rank,
                "owners": work["owners"] if work is not None else [],
                "runtime_priority": (
                    certification.get("runtime_priority", "").lower()
                    == "true"
                ),
                "weighted_profile_heat": row["weighted_profile_heat"],
                "parity": float(certification.get("parity", 0.0)),
                "target_lines": int(
                    certification.get("target_lines", 0)
                ),
                "cohort_source": certification.get("cohort_source", ""),
                "recovered_definition": certification.get(
                    "recovered_definition", ""
                ),
                "remaining_work": certification.get(
                    "remaining_work", ""
                ),
            }
        )
    certified_cleanup.sort(
        key=lambda item: (
            item["owner_worklist_rank"] is None,
            item["owner_worklist_rank"] or 0,
            not item["runtime_priority"],
            -item["weighted_profile_heat"],
            -item["parity"],
            item["target_lines"],
            int(normalize_entry(item["entry"]), 16),
        )
    )
    return {
        "format": "oot3d_decomp_consumer_work_request_v1",
        "snapshot_id": snapshot["snapshot_id"],
        "decomp_revision": snapshot["source"]["revision"],
        "consumer_input_sha256": snapshot["consumer_input_sha256"],
        "policy": {
            "decomp_checkout_is_read_only": True,
            "tracked_input_must_be_committed": True,
            "generated_sidecars_must_be_regenerated_for_the_commit": True,
            "bulk_recovered_c_is_evidence_not_importable_source": True,
            "semantic_certified_bulk_requires_cleanup_before_import": True,
            "reviewed_semantic_source_required_for_closure": True,
        },
        "consumer_semantic_handoff": build_decomp_handoff(snapshot)[
            "entries"
        ],
        "manual_blockers": manual_blockers,
        "resolved_structural_findings": resolved_findings,
        "target_mismatches": [
            {
                "entry": row["entry"],
                "name": row["name"],
                "original_source_status": row[
                    "original_source_status"
                ],
                **row["source_status_override"],
            }
            for row in snapshot["registry"]
            if row["source_status_override"] is not None
        ],
        "semantic_certified_cleanup": certified_cleanup,
        "priority_entries": [
            {
                "rank": rank,
                "entry": item["entry"],
                "name": item["name"],
                "source_status": item["source_status"],
                "owners": item["owners"],
                "relations": item["relations"],
                "priority_score": item["priority_score"],
                "weighted_profile_heat": item["weighted_profile_heat"],
            }
            for rank, item in enumerate(snapshot["worklist"][:100], 1)
        ],
        "acceptance_evidence": [
            "reviewed target-linked semantic C/C++ body",
            "verified entry address, signature and calling convention",
            "explicit contracts for every reachable indirect target",
            "regenerated callgraph and function-pointer references",
            "decomp test suite passing at the reported commit",
        ],
    }


def build_consumer_work_request(
    snapshot: dict[str, Any],
) -> dict[str, Any]:
    dynamic_dispatch_contracts = []
    owner_frontier = []
    for owner in snapshot["owners"]:
        for contract in owner.get("dynamic_dispatch_contracts", []):
            dynamic_dispatch_contracts.append(
                {
                    **contract,
                    "owner_entry": owner["entry"],
                    "owner_root": owner["canonical_name"],
                }
            )
        missing_entries = sum(
            count
            for status, count in owner["status_counts"].items()
            if status not in CLOSURE_SOURCE_STATUSES
        )
        owner_frontier.append(
            {
                "owner": owner["id"],
                "entry": owner["entry"],
                "name": owner["canonical_name"],
                "callgraph_name": owner["actual_name"],
                "identity_resolution": owner.get("identity_resolution"),
                "missing_entries": missing_entries,
                "unresolved_indirect_edges": owner[
                    "unresolved_indirect_edges"
                ],
                "unresolved_dynamic_dispatch_contracts": owner.get(
                    "unresolved_dynamic_dispatch_contracts", 0
                ),
                "open_manual_blockers": sum(
                    blocker.get("status") != "resolved"
                    for blocker in owner["manual_blockers"]
                ),
                "source_graph_closed": owner["source_graph_closed"],
                "approved_for_runtime_import": owner[
                    "approved_for_runtime_import"
                ],
                "runtime_import_acceptance": owner[
                    "runtime_import_acceptance"
                ],
                "runtime_import_ready": owner["runtime_import_ready"],
                "runtime_import_validation": owner.get(
                    "runtime_import_validation", {}
                ),
            }
        )
    dynamic_dispatch_contracts.sort(
        key=lambda item: (
            item.get("resolved", False),
            -next(
                owner["priority"]
                for owner in snapshot["owners"]
                if owner["id"] == item["owner"]
            ),
            item["id"],
        )
    )
    owner_frontier.sort(
        key=lambda item: (
            item["runtime_import_ready"],
            item["source_graph_closed"],
            item["missing_entries"],
            item["owner"],
        )
    )
    return {
        "format": "oot3d_source_consumer_work_request_v1",
        "snapshot_id": snapshot["snapshot_id"],
        "decomp_revision": snapshot["source"]["revision"],
        "consumer_input_sha256": snapshot["consumer_input_sha256"],
        "policy": {
            "dynamic_callbacks_are_typed_boundaries": True,
            "unknown_callback_targets_fall_back_to_native_a32": True,
            "implemented_contracts_require_verification_evidence": True,
            "owner_import_requires_closed_source_graph": True,
            "owner_import_remains_explicit": True,
            "owner_import_requires_hashed_acceptance_evidence": True,
        },
        "dynamic_dispatch_contracts": dynamic_dispatch_contracts,
        "owner_frontier": owner_frontier,
        "owner_import_acceptance": snapshot[
            "owner_import_acceptance"
        ],
        "owner_closure_coverage": snapshot["owner_closure_coverage"],
        "acceptance_evidence": list(RUNTIME_IMPORT_ACCEPTANCE_EVIDENCE),
    }


def build_delta(
    snapshot: dict[str, Any], previous: dict[str, Any] | None
) -> dict[str, Any]:
    current_reviewed = int(
        snapshot["decomp_checkpoint"]["reviewed_target_linked_c_bodies"]
    )
    current_owner_membership: dict[str, list[str]] = defaultdict(list)
    for owner in snapshot["owners"]:
        for member in owner.get("members", []):
            current_owner_membership[
                normalize_entry(member["entry"])
            ].append(owner["id"])
    for owners in current_owner_membership.values():
        owners.sort()
    result: dict[str, Any] = {
        "format": "oot3d_source_owner_closure_delta_v1",
        "snapshot_id": snapshot["snapshot_id"],
        "previous_snapshot_id": (
            previous.get("snapshot_id") if previous is not None else None
        ),
        "consumer_inputs_match": (
            previous.get("consumer_input_sha256")
            == snapshot["consumer_input_sha256"]
            if previous is not None
            else None
        ),
        "consumer_contract_match": (
            consumer_contract_sha256(previous)
            == consumer_contract_sha256(snapshot)
            if previous is not None
            else None
        ),
        "reviewed_target_linked_bodies": {
            "current": current_reviewed,
            "previous": None,
            "delta": None,
        },
        "source_status_changes": [],
        "producer_source_status_changes": [],
        "semantic_promotions": [],
        "consumer_semantic_promotions": [],
        "consumer_semantic_superseded": [],
        "owner_relevant_semantic_promotions": [],
        "owner_external_semantic_promotions": [],
        "semantic_regressions": [],
        "certification_promotions": [],
        "certification_graduations": [],
        "certification_regressions": [],
        "dynamic_dispatch_contract_changes": [],
        "owners": [],
    }
    previous_owners: dict[str, dict[str, Any]] = {}
    if previous is not None:
        previous_reviewed = int(
            previous["decomp_checkpoint"]["reviewed_target_linked_c_bodies"]
        )
        result["reviewed_target_linked_bodies"].update(
            {
                "previous": previous_reviewed,
                "delta": current_reviewed - previous_reviewed,
            }
        )
        previous_owners = {
            owner["id"]: owner for owner in previous.get("owners", [])
        }

        previous_registry = {
            normalize_entry(row["entry"]): row
            for row in previous.get("registry", [])
        }
        for current in snapshot["registry"]:
            prior = previous_registry.get(normalize_entry(current["entry"]))
            if prior is None:
                continue
            previous_status = prior["source_status"]
            current_status = current["source_status"]
            previous_producer_status = prior.get(
                "producer_source_status", previous_status
            )
            current_producer_status = current.get(
                "producer_source_status", current_status
            )
            selected_changed = previous_status != current_status
            producer_changed = (
                previous_producer_status != current_producer_status
            )
            if not selected_changed and not producer_changed:
                continue
            change = {
                "entry": current["entry"],
                "name": current["name"],
                "previous_status": previous_status,
                "current_status": current_status,
                "source_paths": current["source_paths"],
                "owners": current_owner_membership.get(
                    normalize_entry(current["entry"]), []
                ),
            }
            if selected_changed:
                result["source_status_changes"].append(change)
            if producer_changed:
                result["producer_source_status_changes"].append(
                    {
                        **change,
                        "previous_status": previous_producer_status,
                        "current_status": current_producer_status,
                        "source_paths": current.get(
                            "producer_source_paths", []
                        ),
                    }
                )
            if (
                current_status == "consumer_reviewed_semantic"
                and previous_status != "consumer_reviewed_semantic"
            ):
                result["consumer_semantic_promotions"].append(change)
            elif (
                previous_status == "consumer_reviewed_semantic"
                and current_status == "reviewed_semantic"
            ):
                result["consumer_semantic_superseded"].append(change)
            if (
                current_producer_status == "reviewed_semantic"
                and previous_producer_status != "reviewed_semantic"
            ):
                producer_change = {
                    **change,
                    "previous_status": previous_producer_status,
                    "current_status": current_producer_status,
                    "source_paths": current.get(
                        "producer_source_paths", []
                    ),
                }
                result["semantic_promotions"].append(producer_change)
                if producer_change["owners"]:
                    result[
                        "owner_relevant_semantic_promotions"
                    ].append(producer_change)
                else:
                    result[
                        "owner_external_semantic_promotions"
                    ].append(producer_change)
                if previous_producer_status == "semantic_certified_bulk":
                    result["certification_graduations"].append(
                        producer_change
                    )
            elif (
                previous_producer_status == "reviewed_semantic"
                and current_producer_status != "reviewed_semantic"
            ):
                result["semantic_regressions"].append(
                    {
                        **change,
                        "previous_status": previous_producer_status,
                        "current_status": current_producer_status,
                        "source_paths": current.get(
                            "producer_source_paths", []
                        ),
                    }
                )
            if (
                current_producer_status == "semantic_certified_bulk"
                and previous_producer_status
                not in {"semantic_certified_bulk", "reviewed_semantic"}
            ):
                result["certification_promotions"].append(
                    {
                        **change,
                        "previous_status": previous_producer_status,
                        "current_status": current_producer_status,
                    }
                )
            elif (
                previous_producer_status == "semantic_certified_bulk"
                and current_producer_status
                not in {"semantic_certified_bulk", "reviewed_semantic"}
            ):
                result["certification_regressions"].append(
                    {
                        **change,
                        "previous_status": previous_producer_status,
                        "current_status": current_producer_status,
                    }
                )

        previous_contracts = {
            contract["id"]: contract
            for owner in previous.get("owners", [])
            for contract in owner.get(
                "dynamic_dispatch_contracts", []
            )
        }
        current_contracts = {
            contract["id"]: contract
            for owner in snapshot["owners"]
            for contract in owner.get(
                "dynamic_dispatch_contracts", []
            )
        }
        for contract_id in sorted(
            set(previous_contracts) | set(current_contracts)
        ):
            prior_contract = previous_contracts.get(contract_id)
            current_contract = current_contracts.get(contract_id)
            previous_status = (
                prior_contract.get("status")
                if prior_contract is not None
                else None
            )
            current_status = (
                current_contract.get("status")
                if current_contract is not None
                else None
            )
            if previous_status == current_status:
                continue
            result["dynamic_dispatch_contract_changes"].append(
                {
                    "id": contract_id,
                    "owner": (
                        current_contract or prior_contract
                    ).get("owner"),
                    "previous_status": previous_status,
                    "current_status": current_status,
                }
            )

    for owner in snapshot["owners"]:
        prior = previous_owners.get(owner["id"])
        current_missing = sum(
            count
            for status, count in owner["status_counts"].items()
            if status not in CLOSURE_SOURCE_STATUSES
        )
        prior_missing = None
        if prior is not None:
            prior_missing = sum(
                count
                for status, count in prior["status_counts"].items()
                if status not in CLOSURE_SOURCE_STATUSES
            )
        status_changes = []
        if prior is not None:
            previous_members = {
                normalize_entry(member["entry"]): member
                for member in prior.get("members", [])
            }
            for member in owner["members"]:
                old_member = previous_members.get(
                    normalize_entry(member["entry"])
                )
                if (
                    old_member is not None
                    and old_member["source_status"]
                    != member["source_status"]
                ):
                    status_changes.append(
                        {
                            "entry": member["entry"],
                            "name": member["name"],
                            "previous_status": old_member["source_status"],
                            "current_status": member["source_status"],
                        }
                    )
        result["owners"].append(
            {
                "id": owner["id"],
                "entry": owner["entry"],
                "missing_entries": {
                    "current": current_missing,
                    "previous": prior_missing,
                    "delta": (
                        current_missing - prior_missing
                        if prior_missing is not None
                        else None
                    ),
                },
                "unresolved_indirect_edges": {
                    "current": owner["unresolved_indirect_edges"],
                    "previous": (
                        prior["unresolved_indirect_edges"]
                        if prior is not None
                        else None
                    ),
                },
                "unresolved_dynamic_dispatch_contracts": {
                    "current": owner.get(
                        "unresolved_dynamic_dispatch_contracts", 0
                    ),
                    "previous": (
                        prior.get(
                            "unresolved_dynamic_dispatch_contracts", 0
                        )
                        if prior is not None
                        else None
                    ),
                },
                "source_graph_closed": {
                    "current": owner["source_graph_closed"],
                    "previous": (
                        prior["source_graph_closed"]
                        if prior is not None
                        else None
                    ),
                },
                "source_status_changes": status_changes,
            }
        )
    return result


def write_latest(
    output_root: Path,
    destination: Path,
    snapshot: dict[str, Any],
) -> None:
    manifest_path = destination / "manifest.json"
    latest = {
        "format": "oot3d_source_owner_closure_latest_v1",
        "snapshot_id": snapshot["snapshot_id"],
        "revision": snapshot["source"]["revision"],
        "package": str(destination.resolve()),
        "manifest_sha256": payload_sha256(manifest_path.read_bytes()),
    }
    (output_root / "latest.json").write_text(
        json.dumps(latest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def verify_package_artifacts(
    destination: Path, manifest: dict[str, Any]
) -> None:
    for relative, expected_hash in manifest.get(
        "artifact_sha256", {}
    ).items():
        candidate = destination.joinpath(*PurePosixPath(relative).parts)
        if not candidate.is_file():
            raise ValueError(
                f"immutable snapshot artifact is missing: {candidate}"
            )
        actual_hash = payload_sha256(candidate.read_bytes())
        if actual_hash != expected_hash:
            raise ValueError(
                f"immutable snapshot artifact hash mismatch: {candidate}"
            )


def build_owner_runtime_import_work_order(
    snapshot: dict[str, Any],
    owner: dict[str, Any],
    closure_sha256: str,
    source_file_sha256: dict[str, str],
) -> dict[str, Any]:
    return {
        "format": "oot3d_owner_runtime_import_work_order_v1",
        "snapshot_id": snapshot["snapshot_id"],
        "decomp_revision": snapshot["source"]["revision"],
        "consumer_input_sha256": snapshot["consumer_input_sha256"],
        "owner": owner["id"],
        "entry": owner["entry"],
        "actual_name": owner["actual_name"],
        "canonical_name": owner["canonical_name"],
        "expected_name": owner["expected_name"],
        "identity_resolution": owner.get("identity_resolution"),
        "source_closure": {
            "closure_sha256": closure_sha256,
            "function_count": owner["closure_function_count"],
            "status_counts": owner["status_counts"],
            "unresolved_indirect_edges": owner[
                "unresolved_indirect_edges"
            ],
            "unresolved_dynamic_dispatch_contracts": owner.get(
                "unresolved_dynamic_dispatch_contracts", 0
            ),
            "open_manual_blockers": sum(
                blocker.get("status") != "resolved"
                for blocker in owner["manual_blockers"]
            ),
            "closed": owner["source_graph_closed"],
        },
        "source_candidate": {
            "artifact_prefix": (
                f"owners/{owner['id']}/source_candidate"
            ),
            "logical_source_file_count": len(
                owner["candidate_source_files"]
            ),
            "logical_source_files": owner["candidate_source_files"],
            "materialized_file_count": len(source_file_sha256),
            "tree_sha256": canonical_sha256(source_file_sha256),
        },
        "runtime_activation": {
            "state": (
                "accepted"
                if owner["runtime_import_ready"]
                else "acceptance_required"
            ),
            "approved": owner["approved_for_runtime_import"],
            "ready": owner["runtime_import_ready"],
            "acceptance": owner["runtime_import_acceptance"],
            "validation": owner.get("runtime_import_validation", {}),
            "automatic_activation_forbidden": True,
            "live_decomp_build_dependency_forbidden": True,
        },
        "required_acceptance_evidence": list(
            RUNTIME_IMPORT_ACCEPTANCE_EVIDENCE
        ),
    }


def write_snapshot(
    snapshot: dict[str, Any],
    output_root: Path,
    decomp_root: Path,
    previous: dict[str, Any] | None = None,
    replace: bool = False,
) -> Path:
    output_root.mkdir(parents=True, exist_ok=True)
    destination = output_root / snapshot["snapshot_id"]
    if destination.exists():
        if not replace:
            manifest_path = destination / "manifest.json"
            if not manifest_path.is_file():
                raise ValueError(
                    f"incomplete immutable snapshot exists: {destination}"
                )
            existing = json.loads(manifest_path.read_text(encoding="utf-8"))
            if (
                existing.get("snapshot_id") != snapshot["snapshot_id"]
                or existing.get("consumer_input_sha256")
                != snapshot["consumer_input_sha256"]
            ):
                raise ValueError(
                    f"snapshot identity collision at {destination}"
                )
            verify_package_artifacts(destination, existing)
            write_latest(output_root, destination, snapshot)
            return destination
        shutil.rmtree(destination)
    destination.mkdir(parents=True)

    registry_rows = []
    for row in snapshot["registry"]:
        registry_rows.append(
            {
                **row,
                "source_paths": ";".join(row["source_paths"]),
                "producer_source_paths": ";".join(
                    row["producer_source_paths"]
                ),
                "consumer_source_overlay": (
                    json.dumps(
                        row["consumer_source_overlay"],
                        sort_keys=True,
                        separators=(",", ":"),
                    )
                    if row["consumer_source_overlay"] is not None
                    else ""
                ),
                "source_status_override": (
                    json.dumps(
                        row["source_status_override"],
                        sort_keys=True,
                        separators=(",", ":"),
                    )
                    if row["source_status_override"] is not None
                    else ""
                ),
                "source_certification": (
                    json.dumps(
                        row["source_certification"],
                        sort_keys=True,
                        separators=(",", ":"),
                    )
                    if row["source_certification"] is not None
                    else ""
                ),
                "profile_heat": json.dumps(
                    row["profile_heat"], sort_keys=True, separators=(",", ":")
                ),
            }
        )
    (destination / "source_entry_registry.csv").write_text(
        csv_text(
            registry_rows,
            [
                "entry",
                "name",
                "signature",
                "source_status",
                "original_source_status",
                "producer_source_status",
                "source_status_override",
                "source_certification",
                "source_paths",
                "producer_source_paths",
                "consumer_source_overlay",
                "runtime_typed_entry",
                "caller_count",
                "call_count",
                "weighted_profile_heat",
                "profile_heat",
            ],
        ),
        encoding="utf-8",
        newline="\n",
    )

    worklist_rows = []
    for item in snapshot["worklist"]:
        worklist_rows.append(
            {
                **item,
                "owners": ";".join(
                    f"{owner['id']}@{owner['depth']}" for owner in item["owners"]
                ),
                "relations": ";".join(item["relations"]),
                "source_certification": (
                    json.dumps(
                        item["source_certification"],
                        sort_keys=True,
                        separators=(",", ":"),
                    )
                    if item["source_certification"] is not None
                    else ""
                ),
            }
        )
    (destination / "owner_worklist.csv").write_text(
        csv_text(
            worklist_rows,
            [
                "priority_score",
                "entry",
                "name",
                "source_status",
                "source_certification",
                "owners",
                "relations",
                "root_owner_count",
                "direct_owner_count",
                "runtime_typed_entry",
                "caller_count",
                "weighted_profile_heat",
            ],
        ),
        encoding="utf-8",
        newline="\n",
    )

    owner_root = destination / "owners"
    owner_root.mkdir()
    for owner in snapshot["owners"]:
        owner_dir = owner_root / owner["id"]
        owner_dir.mkdir()
        (owner_dir / "closure.json").write_text(
            json.dumps(owner, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        (owner_dir / "candidate_source_files.txt").write_text(
            "".join(f"{path}\n" for path in owner["candidate_source_files"]),
            encoding="utf-8",
            newline="\n",
        )
        missing = []
        for member in owner["members"]:
            if member["source_status"] in CLOSURE_SOURCE_STATUSES:
                continue
            missing.append(
                {
                    **member,
                    "source_status_override": (
                        json.dumps(
                            member["source_status_override"],
                            sort_keys=True,
                            separators=(",", ":"),
                        )
                        if member["source_status_override"] is not None
                        else ""
                    ),
                    "source_certification": (
                        json.dumps(
                            member["source_certification"],
                            sort_keys=True,
                            separators=(",", ":"),
                        )
                        if member["source_certification"] is not None
                        else ""
                    ),
                }
            )
        (owner_dir / "missing_entries.csv").write_text(
            csv_text(
                missing,
                [
                    "depth",
                    "entry",
                    "name",
                    "source_status",
                    "original_source_status",
                    "source_status_override",
                    "source_certification",
                    "runtime_typed_entry",
                    "weighted_profile_heat",
                ],
            ),
            encoding="utf-8",
            newline="\n",
        )

        if owner["source_graph_closed"]:
            source_dir = owner_dir / "source_candidate"
            source_dir.mkdir(parents=True, exist_ok=True)
            producer_paths = {
                *owner["candidate_producer_source_files"],
                *snapshot.get("bundle_support_paths", []),
            }
            committed = archive_commit_files(
                decomp_root,
                snapshot["source"]["revision"],
                producer_paths,
            )
            for relative, payload in committed.files.items():
                target = source_dir.joinpath(*PurePosixPath(relative).parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(payload)
            overlay_hashes = {
                path: digest
                for overlay in (
                    snapshot.get("consumer_semantic_overlays") or {}
                ).get("entries", [])
                for path, digest in overlay["file_sha256"].items()
            }
            runtime_root = Path(snapshot["runtime_source_root"])
            for relative in owner["candidate_consumer_source_files"]:
                payload, _ = runtime_file_payload(runtime_root, relative)
                expected_hash = overlay_hashes.get(relative)
                if (
                    expected_hash is None
                    or payload_sha256(payload) != expected_hash
                ):
                    raise ValueError(
                        "consumer semantic source changed during package "
                        f"materialization: {relative}"
                    )
                target = source_dir.joinpath(
                    *PurePosixPath(relative).parts
                )
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(payload)
            source_file_sha256 = {
                path.relative_to(source_dir).as_posix(): payload_sha256(
                    path.read_bytes()
                )
                for path in sorted(source_dir.rglob("*"))
                if path.is_file()
            }
            closure_path = owner_dir / "closure.json"
            work_order = build_owner_runtime_import_work_order(
                snapshot,
                owner,
                payload_sha256(closure_path.read_bytes()),
                source_file_sha256,
            )
            (owner_dir / "runtime_import_work_order.json").write_text(
                json.dumps(work_order, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )

    report = render_report(snapshot, previous)
    (destination / "report.md").write_text(
        report, encoding="utf-8", newline="\n"
    )
    (destination / "decomp_work_request.json").write_text(
        json.dumps(
            build_decomp_work_request(snapshot),
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )
    (destination / "consumer_work_request.json").write_text(
        json.dumps(
            build_consumer_work_request(snapshot),
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )
    decomp_handoff = build_decomp_handoff(snapshot)
    (destination / "decomp_handoff.json").write_text(
        json.dumps(decomp_handoff, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    (destination / "decomp_handoff.md").write_text(
        render_decomp_handoff(decomp_handoff),
        encoding="utf-8",
        newline="\n",
    )
    (destination / "delta.json").write_text(
        json.dumps(
            build_delta(snapshot, previous),
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )

    manifest = {
        key: value
        for key, value in snapshot.items()
        if key not in {"registry", "worklist"}
    }
    manifest["counts"] = {
        "registry_entries": len(snapshot["registry"]),
        "worklist_entries": len(snapshot["worklist"]),
        "semantic_certified_bulk_entries": snapshot[
            "semantic_certified_bulk_count"
        ],
        "consumer_semantic_overlay_entries": snapshot[
            "consumer_semantic_overlay_count"
        ],
        "owner_import_acceptance_entries": snapshot[
            "owner_import_acceptance"
        ]["entry_count"],
        "owners": len(snapshot["owners"]),
        "closed_source_graphs": sum(
            owner["source_graph_closed"] for owner in snapshot["owners"]
        ),
        "runtime_import_ready_owners": sum(
            owner["runtime_import_ready"] for owner in snapshot["owners"]
        ),
        "open_dynamic_dispatch_contracts": sum(
            owner.get("unresolved_dynamic_dispatch_contracts", 0)
            for owner in snapshot["owners"]
        ),
    }
    manifest["artifact_sha256"] = {}
    for path in sorted(destination.rglob("*")):
        if path.is_file():
            manifest["artifact_sha256"][
                path.relative_to(destination).as_posix()
            ] = payload_sha256(path.read_bytes())
    (destination / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    write_latest(output_root, destination, snapshot)
    return destination


def load_package(manifest_path: Path) -> dict[str, Any]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    owners = []
    for owner_summary in manifest.get("owners", []):
        owner_path = (
            manifest_path.parent
            / "owners"
            / owner_summary["id"]
            / "closure.json"
        )
        owners.append(
            json.loads(owner_path.read_text(encoding="utf-8"))
            if owner_path.is_file()
            else owner_summary
        )
    manifest["owners"] = owners
    registry_path = manifest_path.parent / "source_entry_registry.csv"
    manifest["registry"] = (
        read_csv_bytes(
            registry_path.read_bytes(), str(registry_path)
        )
        if registry_path.is_file()
        else []
    )
    return manifest


def consumer_contract_sha256(snapshot: dict[str, Any]) -> str:
    inputs = snapshot.get("consumer_inputs", {})
    return canonical_sha256(
        {
            "configuration_sha256": inputs.get(
                "configuration_sha256",
                snapshot.get("configuration_sha256"),
            ),
            "profiles": inputs.get("profiles", []),
            "runtime_entry_registries": inputs.get(
                "runtime_entry_registries", []
            ),
            "consumer_semantic_overlays": inputs.get(
                "consumer_semantic_overlays"
            ),
            "owner_import_acceptance": inputs.get(
                "owner_import_acceptance"
            ),
        }
    )


def load_previous(
    output_root: Path,
    decomp_root: Path,
    current_snapshot: dict[str, Any],
) -> dict[str, Any] | None:
    current_revision = str(current_snapshot["source"]["revision"])
    current_input_hash = str(
        current_snapshot["consumer_input_sha256"]
    )
    current_contract_hash = consumer_contract_sha256(current_snapshot)
    candidates: list[tuple[int, int, int, int, str, Path]] = []
    if not output_root.is_dir():
        return None
    for package in output_root.iterdir():
        manifest_path = package / "manifest.json"
        if not package.is_dir() or not manifest_path.is_file():
            continue
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            revision = str(manifest["source"]["revision"])
        except (KeyError, json.JSONDecodeError):
            continue
        if revision == current_revision:
            continue
        ancestry = subprocess.run(
            [
                "git",
                "-C",
                str(decomp_root),
                "merge-base",
                "--is-ancestor",
                revision,
                current_revision,
            ],
            capture_output=True,
            text=True,
        )
        if ancestry.returncode != 0:
            continue
        distance = int(
            str(
                run_git(
                    decomp_root,
                    "rev-list",
                    "--count",
                    f"{revision}..{current_revision}",
                ).stdout
            ).strip()
        )
        input_rank = int(
            manifest.get("consumer_input_sha256") != current_input_hash
        )
        contract_rank = int(
            consumer_contract_sha256(manifest) != current_contract_hash
        )
        provenance_rank = int(
            not manifest.get("source", {}).get(
                "sidecar_checkout_revision"
            )
        )
        candidates.append(
            (
                distance,
                input_rank,
                contract_rank,
                provenance_rank,
                package.name,
                manifest_path,
            )
        )
    if not candidates:
        return None
    _, _, _, _, _, selected = min(candidates)
    return load_package(selected)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decomp-root", type=Path, required=True)
    parser.add_argument("--runtime-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--revision", default="HEAD")
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--replace", action="store_true")
    args = parser.parse_args()

    snapshot = build_snapshot(
        args.decomp_root,
        args.runtime_root,
        args.config,
        args.revision,
    )
    previous = load_previous(
        args.output_root,
        args.decomp_root,
        snapshot,
    )
    destination = write_snapshot(
        snapshot,
        args.output_root,
        args.decomp_root,
        previous=previous,
        replace=args.replace,
    )
    print(
        json.dumps(
            {
                "snapshot_id": snapshot["snapshot_id"],
                "revision": snapshot["source"]["revision"],
                "output": str(destination),
                "owners": len(snapshot["owners"]),
                "closed_source_graphs": sum(
                    owner["source_graph_closed"]
                    for owner in snapshot["owners"]
                ),
                "runtime_import_ready_owners": sum(
                    owner["runtime_import_ready"]
                    for owner in snapshot["owners"]
                ),
                "worklist_entries": len(snapshot["worklist"]),
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
