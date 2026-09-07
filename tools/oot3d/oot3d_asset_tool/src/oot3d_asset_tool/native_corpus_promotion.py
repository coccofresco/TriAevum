from __future__ import annotations

import csv
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import tarfile
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

from .native_layout_promotion import (
    ACTOR_INIT_RECORDS_PATH,
    STRUCT_FIELD_PATTERN,
    build_native_layout_promotion,
)


FORMAT = "oot3d_native_corpus_promotion_v1"
GENERATOR_VERSION = "1.0.0"

_CORPUS_ROOT = "build/analysis/decomp_batches"
_FUNCTION_INVENTORY = "analysis/codebin_function_inventory.csv"
_DATA_SYMBOL_PATTERN = re.compile(
    r"^analysis/codebin_.+_data_symbols\.csv$", re.IGNORECASE
)
_SIGNATURE_PATTERN = re.compile(
    r"^analysis/codebin_.+_signatures\.csv$", re.IGNORECASE
)
_SECTION_PATTERN = re.compile(r"(?m)^## (?P<name>[^\r\n]+)\r?$")
_ENTRY_PATTERN = re.compile(
    r"(?m)^- entry: `0x(?P<address>[0-9A-Fa-f]+)`\s*$"
)
_STATUS_PATTERN = re.compile(r"(?m)^- decompile_status: (?P<status>\S+)\s*$")
_BODY_PATTERN = re.compile(
    r"(?s)### Decompiled C\s*\r?\n\s*```c\s*\r?\n(?P<body>.*?)\r?\n```"
)
_RAW_SYMBOL_PATTERN = re.compile(r"\b(?:FUN|DAT)_[0-9A-Fa-f]{8}\b")
_IDENTIFIER_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

_HAZARD_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("ghidra_undefined_types", re.compile(r"\bundefined(?:1|2|4|8|16)?\b")),
    ("anonymous_function_symbols", re.compile(r"\bFUN_[0-9A-Fa-f]{8}\b")),
    ("raw_data_symbols", re.compile(r"\bDAT_[0-9A-Fa-f]{8}\b")),
    ("raw_unknown_globals", re.compile(r"\b_?UNK_[0-9A-Fa-f]+\b")),
    ("fpscr_residue", re.compile(r"\bin_fpscr\b")),
    (
        "unrecovered_indirect_control",
        re.compile(
            r"Could not recover jumptable|Treating indirect jump as call"
        ),
    ),
    ("generic_code_type", re.compile(r"\bcode\s*\*")),
)


@dataclass(frozen=True)
class CorpusCandidate:
    address: str
    name: str
    source_path: str
    decompile_status: str
    body: str
    typed: bool

    @property
    def usable(self) -> bool:
        return self.decompile_status == "ok" and bool(self.body.strip())

    @property
    def body_sha256(self) -> str:
        return hashlib.sha256(self.body.encode("utf-8")).hexdigest()


@dataclass(frozen=True)
class PromotionPackage:
    manifest: dict[str, Any]
    report: str
    support_header: str
    layout_header: str
    source_files: dict[str, str]


def canonical_sha256(value: object) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def _normalize_address(value: object) -> str:
    text = str(value or "0").strip().removeprefix("0x").removeprefix("0X")
    try:
        number = int(text or "0", 16)
    except ValueError as exc:
        raise ValueError(f"invalid code.bin address: {value!r}") from exc
    if not 0 < number <= 0xFFFFFFFF:
        raise ValueError(f"code.bin address is outside the 32-bit range: {value!r}")
    return f"0x{number:08X}"


def _git(
    repository: Path, *arguments: str, text: bool = True
) -> subprocess.CompletedProcess[str] | subprocess.CompletedProcess[bytes]:
    command = ["git", "-C", str(repository), *arguments]
    try:
        return subprocess.run(
            command,
            check=True,
            capture_output=True,
            text=text,
        )
    except FileNotFoundError as exc:
        raise ValueError("git executable is required for revision-pinned promotion") from exc
    except subprocess.CalledProcessError as exc:
        if isinstance(exc.stderr, bytes):
            detail = exc.stderr.decode("utf-8", errors="replace").strip()
        else:
            detail = exc.stderr.strip() if isinstance(exc.stderr, str) else ""
        raise ValueError(
            f"git command failed in {repository}: {' '.join(arguments)}"
            + (f": {detail}" if detail else "")
        ) from exc


def _corpus_digest(files: dict[str, bytes]) -> str:
    digest = hashlib.sha256()
    for path, payload in sorted(files.items()):
        digest.update(path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(payload).hexdigest().encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _read_generated_corpus_sidecar(
    repository: Path, resolved: str, head: str
) -> dict[str, bytes]:
    if head != resolved:
        raise ValueError(
            "generated decompiler corpus is not committed, so its checkout HEAD must "
            f"match the requested revision ({head} versus {resolved})"
        )

    tracked = str(
        _git(repository, "ls-files", "--", _CORPUS_ROOT).stdout
    ).splitlines()
    if tracked:
        raise ValueError(
            "generated corpus sidecar unexpectedly mixes tracked files with ignored "
            f"output: {tracked[0]}"
        )
    ignored = subprocess.run(
        ["git", "-C", str(repository), "check-ignore", "--quiet", "--", _CORPUS_ROOT],
        check=False,
        capture_output=True,
    )
    if ignored.returncode != 0:
        raise ValueError(
            f"generated corpus sidecar is not ignored by Git: {_CORPUS_ROOT}"
        )

    root = repository.joinpath(*PurePosixPath(_CORPUS_ROOT).parts)
    if not root.is_dir() or root.is_symlink():
        raise ValueError(f"generated corpus sidecar is missing or unsafe: {root}")
    result: dict[str, bytes] = {}
    for path in sorted(root.rglob("*.md")):
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"generated corpus sidecar contains an unsafe path: {path}")
        relative = path.relative_to(repository).as_posix()
        result[relative] = path.read_bytes()
    if not result:
        raise ValueError(f"generated corpus sidecar contains no Markdown files: {root}")
    return result


def _read_revision_files(
    repository: Path, revision: str
) -> tuple[str, str, int, dict[str, bytes], dict[str, Any]]:
    repository = repository.resolve()
    if not (repository / ".git").exists():
        raise ValueError(f"Zelda3drecomp root is not a Git checkout: {repository}")

    resolved = str(
        _git(repository, "rev-parse", f"{revision}^{{commit}}").stdout
    ).strip()
    head = str(_git(repository, "rev-parse", "HEAD").stdout).strip()
    status = str(_git(repository, "status", "--porcelain=v1", "-uno").stdout)
    worktree_change_count = len([line for line in status.splitlines() if line])

    tree = str(
        _git(repository, "ls-tree", "-r", "--name-only", resolved, "analysis").stdout
    ).splitlines()
    data_symbol_paths = sorted(
        path for path in tree if _DATA_SYMBOL_PATTERN.fullmatch(path)
    )
    struct_field_paths = sorted(
        path for path in tree if STRUCT_FIELD_PATTERN.fullmatch(path)
    )
    signature_paths = sorted(
        path for path in tree if _SIGNATURE_PATTERN.fullmatch(path)
    )
    corpus_tree = str(
        _git(
            repository,
            "ls-tree",
            "-r",
            "--name-only",
            resolved,
            _CORPUS_ROOT,
        ).stdout
    ).splitlines()
    archive_paths = [
        _FUNCTION_INVENTORY,
        *data_symbol_paths,
        *struct_field_paths,
        *signature_paths,
    ]
    if ACTOR_INIT_RECORDS_PATH in tree:
        archive_paths.append(ACTOR_INIT_RECORDS_PATH)
    if corpus_tree:
        archive_paths.insert(0, _CORPUS_ROOT)
    archive = _git(
        repository,
        "archive",
        "--format=tar",
        resolved,
        *archive_paths,
        text=False,
    ).stdout
    if not isinstance(archive, bytes):
        raise ValueError("git archive unexpectedly returned text output")

    files: dict[str, bytes] = {}
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as source:
        for member in source.getmembers():
            if not member.isfile():
                continue
            extracted = source.extractfile(member)
            if extracted is None:
                raise ValueError(f"cannot read archived evidence file: {member.name}")
            files[PurePosixPath(member.name).as_posix()] = extracted.read()

    if _FUNCTION_INVENTORY not in files:
        raise ValueError(
            f"revision {resolved} does not contain {_FUNCTION_INVENTORY}"
        )
    corpus_files = {
        path: payload
        for path, payload in files.items()
        if path.startswith(f"{_CORPUS_ROOT}/") and path.endswith(".md")
    }
    if corpus_files:
        corpus_mode = "git_archive_commit"
    else:
        corpus_files = _read_generated_corpus_sidecar(repository, resolved, head)
        files.update(corpus_files)
        corpus_mode = "git_commit_plus_generated_corpus_sidecar"
    corpus_source = {
        "mode": corpus_mode,
        "file_count": len(corpus_files),
        "total_bytes": sum(len(payload) for payload in corpus_files.values()),
        "aggregate_sha256": _corpus_digest(corpus_files),
    }
    return resolved, head, worktree_change_count, files, corpus_source


def _parse_corpus(path: str, payload: bytes) -> list[CorpusCandidate]:
    text = payload.decode("utf-8", errors="replace").replace("\r\n", "\n")
    sections = list(_SECTION_PATTERN.finditer(text))
    candidates: list[CorpusCandidate] = []
    for index, match in enumerate(sections):
        end = sections[index + 1].start() if index + 1 < len(sections) else len(text)
        section = text[match.end() : end]
        entry_match = _ENTRY_PATTERN.search(section)
        if entry_match is None:
            continue
        status_match = _STATUS_PATTERN.search(section)
        body_match = _BODY_PATTERN.search(section)
        body = body_match.group("body").strip("\n") if body_match else ""
        candidates.append(
            CorpusCandidate(
                address=_normalize_address(entry_match.group("address")),
                name=match.group("name").strip(),
                source_path=path,
                decompile_status=(
                    status_match.group("status") if status_match is not None else ""
                ),
                body=body,
                typed="typed" in PurePosixPath(path).stem.lower(),
            )
        )
    return candidates


def _read_csv(payload: bytes, source: str) -> list[dict[str, str]]:
    text = payload.decode("utf-8-sig", errors="strict")
    reader = csv.DictReader(io.StringIO(text))
    if reader.fieldnames is None:
        raise ValueError(f"CSV evidence has no header: {source}")
    return [dict(row) for row in reader]


def _load_inventory(files: dict[str, bytes]) -> dict[str, dict[str, str]]:
    result: dict[str, dict[str, str]] = {}
    for row in _read_csv(files[_FUNCTION_INVENTORY], _FUNCTION_INVENTORY):
        address = _normalize_address(row.get("entry"))
        if address in result:
            raise ValueError(f"duplicate function inventory address: {address}")
        result[address] = row
    return result


def _load_data_symbol_map(files: dict[str, bytes]) -> dict[str, str]:
    result: dict[str, str] = {}
    for path, payload in sorted(files.items()):
        if _DATA_SYMBOL_PATTERN.fullmatch(path) is None:
            continue
        for row in _read_csv(payload, path):
            old_name = str(row.get("old_name") or "").strip()
            new_name = str(row.get("new_name") or "").strip()
            if not old_name or not _IDENTIFIER_PATTERN.fullmatch(new_name):
                continue
            key = old_name.upper()
            previous = result.get(key)
            if previous is not None and previous != new_name:
                raise ValueError(
                    f"conflicting maintained data symbol for {old_name}: "
                    f"{previous} versus {new_name}"
                )
            result[key] = new_name
    return result


def _extract_room_closure(
    room_unit: dict[str, Any],
) -> tuple[dict[str, dict[str, Any]], int, list[dict[str, str]]]:
    if room_unit.get("format") != "oot3d_room_compilation_unit_v1":
        raise ValueError("native corpus promotion requires an OOT3D room compilation unit")
    profiles = room_unit.get("actor_profiles")
    if not isinstance(profiles, list) or not profiles:
        raise ValueError("room compilation unit contains no actor profiles")

    closure: dict[str, dict[str, Any]] = {}
    reference_count = 0
    root_slots: list[dict[str, str]] = []
    for profile in profiles:
        profile_name = str(profile.get("actor_name") or profile.get("profile_key") or "")
        graph = profile.get("behavior_graph")
        if not isinstance(graph, dict):
            raise ValueError(f"actor profile has no behavior graph: {profile_name}")
        functions = graph.get("functions")
        if not isinstance(functions, list):
            raise ValueError(f"actor profile has no function inventory: {profile_name}")
        reference_count += len(functions)
        for function in functions:
            address = _normalize_address(function.get("address"))
            record = closure.setdefault(
                address,
                {
                    "address": address,
                    "name": str(function.get("name") or ""),
                    "byte_length": int(function.get("byte_length") or 0),
                    "families": set(),
                    "closure_kinds": set(),
                    "profiles": set(),
                    "direct_callees": {},
                    "root_slots": [],
                    "metadata": dict(function),
                },
            )
            if record["name"] != str(function.get("name") or ""):
                raise ValueError(f"conflicting function names at {address}")
            if record["byte_length"] != int(function.get("byte_length") or 0):
                raise ValueError(f"conflicting function lengths at {address}")
            if function.get("family"):
                record["families"].add(str(function["family"]))
            if function.get("closure_kind"):
                record["closure_kinds"].add(str(function["closure_kind"]))
            record["profiles"].add(profile_name)

        for edge in graph.get("consumer_call_edges", []):
            caller = _normalize_address(edge.get("caller_address"))
            if caller not in closure:
                continue
            callee = _normalize_address(edge.get("callee_address"))
            closure[caller]["direct_callees"][callee] = {
                "address": callee,
                "name": str(edge.get("callee_name") or ""),
                "relation": str(edge.get("relation") or ""),
            }

        for root in graph.get("consumer_roots", []):
            address = _normalize_address(root.get("address"))
            slot = {
                "address": address,
                "name": str(root.get("name") or ""),
                "profile": profile_name,
                "slot": str(root.get("slot") or ""),
            }
            root_slots.append(slot)
            if address in closure:
                closure[address]["root_slots"].append(slot)

    return closure, reference_count, root_slots


def _function_name_map(
    inventory: dict[str, dict[str, str]], closure: dict[str, dict[str, Any]]
) -> dict[str, str]:
    result: dict[str, str] = {}
    for address, row in inventory.items():
        candidate = str(row.get("maintained_name") or row.get("name") or "").strip()
        if _IDENTIFIER_PATTERN.fullmatch(candidate) and not candidate.startswith("FUN_"):
            result[address] = candidate
    for address, function in closure.items():
        candidate = str(function["name"] or "")
        if _IDENTIFIER_PATTERN.fullmatch(candidate):
            result[address] = candidate
    return result


def _select_candidate(
    expected_name: str, candidates: Iterable[CorpusCandidate]
) -> CorpusCandidate | None:
    usable = [candidate for candidate in candidates if candidate.usable]
    if not usable:
        return None
    return min(
        usable,
        key=lambda candidate: (
            not candidate.typed,
            candidate.name != expected_name,
            candidate.source_path,
            candidate.body_sha256,
        ),
    )


def _normalize_body_symbols(
    body: str,
    *,
    corpus_name: str,
    expected_name: str,
    function_names: dict[str, str],
    data_symbols: dict[str, str],
) -> tuple[str, list[dict[str, Any]]]:
    replacements: Counter[tuple[str, str, str]] = Counter()

    if (
        corpus_name != expected_name
        and _IDENTIFIER_PATTERN.fullmatch(corpus_name)
        and _IDENTIFIER_PATTERN.fullmatch(expected_name)
    ):
        pattern = re.compile(rf"\b{re.escape(corpus_name)}\b")
        body, count = pattern.subn(expected_name, body)
        if count:
            replacements[(corpus_name, expected_name, "function_definition")] += count

    def replace_raw(match: re.Match[str]) -> str:
        token = match.group(0)
        prefix, address_text = token.split("_", 1)
        if prefix == "FUN":
            replacement = function_names.get(_normalize_address(address_text))
            kind = "function_symbol"
        else:
            replacement = data_symbols.get(token.upper())
            kind = "data_symbol"
        if replacement is None or replacement == token:
            return token
        replacements[(token, replacement, kind)] += 1
        return replacement

    promoted = _RAW_SYMBOL_PATTERN.sub(replace_raw, body)
    records = [
        {"from": old, "to": new, "kind": kind, "count": count}
        for (old, new, kind), count in sorted(replacements.items())
    ]
    return promoted, records


def _normalize_host_pointer_casts(
    body: str,
) -> tuple[str, list[dict[str, Any]]]:
    pointer_names = {
        match.group("name")
        for match in re.finditer(
            r"\b(?:const\s+)?[A-Za-z_][A-Za-z0-9_]*\s*\*+\s*"
            r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)",
            body,
        )
    }
    replacements: list[dict[str, Any]] = []
    promoted = body
    address_pattern = re.compile(
        r"\((?:u?int|u?long)\)&"
        r"(?P<expression>\([^\r\n;]+?\)"
        r"(?:\.[A-Za-z_][A-Za-z0-9_]*(?:\[[^\]\r\n]+\])?)*)"
    )
    promoted, address_count = address_pattern.subn(
        lambda match: f"oot3d_host_address(&{match.group('expression')})", promoted
    )
    if address_count:
        replacements.append(
            {
                "from": "32-bit integer cast of member address",
                "to": "oot3d_host_address(&member)",
                "kind": "host_pointer_arithmetic",
                "count": address_count,
            }
        )
    for name in sorted(pointer_names, key=lambda value: (-len(value), value)):
        pattern = re.compile(rf"\((?:u?int|u?long)\)\s*{re.escape(name)}\b")
        promoted, count = pattern.subn(f"oot3d_host_address({name})", promoted)
        if count:
            replacements.append(
                {
                    "from": f"32-bit integer cast of {name}",
                    "to": f"oot3d_host_address({name})",
                    "kind": "host_pointer_arithmetic",
                    "count": count,
                }
            )
    return promoted, replacements


def _hazards(body: str, expected_name: str) -> list[str]:
    result = [name for name, pattern in _HAZARD_PATTERNS if pattern.search(body)]
    if _extract_definition_signature(body, expected_name) is None:
        result.append("function_definition_name_mismatch")
    return sorted(result)


def _extract_definition_signature(body: str, expected_name: str) -> str | None:
    for match in re.finditer(rf"\b{re.escape(expected_name)}\s*\(", body):
        open_paren = body.find("(", match.start())
        depth = 0
        close_paren = -1
        for index in range(open_paren, len(body)):
            character = body[index]
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth == 0:
                    close_paren = index
                    break
        if close_paren < 0:
            continue
        suffix = body[close_paren + 1 :]
        if not suffix.lstrip().startswith("{"):
            continue
        line_start = body.rfind("\n", 0, match.start()) + 1
        prefix = body[line_start : match.start()].strip()
        if not prefix or any(token in prefix for token in (";", "{", "}")):
            continue
        signature = body[line_start : close_paren + 1].strip()
        if signature:
            return signature
    return None


def _compile_readiness(body_present: bool, hazards: list[str]) -> str:
    if not body_present:
        return "missing_decompiled_body"
    if "function_definition_name_mismatch" in hazards:
        return "requires_function_boundary_recovery"
    if "unrecovered_indirect_control" in hazards:
        return "requires_control_flow_recovery"
    if hazards:
        return "requires_symbol_or_type_normalization"
    return "requires_generated_layout_and_service_declarations"


def _safe_module_path(value: object) -> PurePosixPath:
    raw = str(value or "unknown").strip().replace("\\", "/").strip("/")
    path = PurePosixPath(raw or "unknown")
    if any(part in {"", ".", ".."} for part in path.parts):
        raise ValueError(f"unsafe logical module path: {value!r}")
    return path


def _source_path(module: object, address: str, name: str) -> str:
    safe_name = re.sub(r"[^A-Za-z0-9_]+", "_", name).strip("_") or "anonymous"
    address_token = address.removeprefix("0x")
    return (
        PurePosixPath("src")
        / _safe_module_path(module)
        / f"{address_token}_{safe_name}.cpp"
    ).as_posix()


def _split_types(value: object) -> list[str]:
    if isinstance(value, list):
        return [str(item).strip() for item in value if str(item).strip()]
    return [part.strip() for part in str(value or "").split(";") if part.strip()]


def _metadata_signature(function: dict[str, Any]) -> str | None:
    metadata = function["metadata"]
    name = str(function["name"])
    if not _IDENTIFIER_PATTERN.fullmatch(name):
        return None
    return_type = str(metadata.get("return_type") or "void").strip()
    parameter_types = _split_types(metadata.get("param_types"))
    parameter_names = _split_types(metadata.get("param_names"))
    parameters = []
    for index, parameter_type in enumerate(parameter_types):
        parameter_name = (
            parameter_names[index] if index < len(parameter_names) else f"arg{index}"
        )
        if not _IDENTIFIER_PATTERN.fullmatch(parameter_name):
            parameter_name = f"arg{index}"
        parameters.append(f"{parameter_type} {parameter_name}")
    return f"{return_type} {name}({', '.join(parameters) if parameters else 'void'})"


def _reviewed_service_signatures(
    files: dict[str, bytes], bodies: Iterable[str], closure_names: set[str]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], int]:
    body_text = "\n".join(bodies)
    called_names = set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", body_text))
    candidates: dict[str, dict[str, set[str]]] = defaultdict(
        lambda: defaultdict(set)
    )
    catalog_count = 0
    for path, payload in sorted(files.items()):
        if _SIGNATURE_PATTERN.fullmatch(path) is None:
            continue
        catalog_count += 1
        for row in _read_csv(payload, path):
            name = str(row.get("name") or "").strip()
            if (
                name not in called_names
                or name in closure_names
                or _IDENTIFIER_PATTERN.fullmatch(name) is None
            ):
                continue
            parameter_types = _split_types(row.get("param_types"))
            parameter_names = _split_types(row.get("param_names"))
            if len(parameter_types) != len(parameter_names):
                raise ValueError(f"signature parameter mismatch for {name} in {path}")
            parameters = []
            for index, (type_name, parameter_name) in enumerate(
                zip(parameter_types, parameter_names)
            ):
                if type_name == "...":
                    parameters.append("...")
                    continue
                if not _IDENTIFIER_PATTERN.fullmatch(parameter_name):
                    parameter_name = f"arg{index}"
                parameters.append(f"{type_name} {parameter_name}")
            return_type = str(row.get("return_type") or "void").strip()
            signature = (
                f"{return_type} {name}"
                f"({', '.join(parameters) if parameters else 'void'})"
            )
            candidates[name][signature].add(path)

    resolved: list[dict[str, Any]] = []
    conflicts: list[dict[str, Any]] = []
    for name, signatures in sorted(candidates.items()):
        if len(signatures) == 1:
            signature, evidence_files = next(iter(signatures.items()))
            resolved.append(
                {
                    "name": name,
                    "signature": signature,
                    "evidence_files": sorted(evidence_files),
                }
            )
        else:
            conflicts.append(
                {
                    "name": name,
                    "candidates": [
                        {
                            "signature": signature,
                            "evidence_files": sorted(evidence_files),
                        }
                        for signature, evidence_files in sorted(signatures.items())
                    ],
                }
            )
    return resolved, conflicts, catalog_count


def _support_header(
    functions: Iterable[dict[str, Any]],
    layout_types: Iterable[str],
    service_signatures: Iterable[dict[str, Any]],
) -> str:
    records = list(functions)
    services = list(service_signatures)
    type_names: set[str] = set(layout_types)
    prototypes: dict[str, str] = {}
    conflicting_names: set[str] = set()
    for function in records:
        metadata = function["metadata"]
        name = str(function["name"])
        signature = function.get("decompiled_signature") or _metadata_signature(function)
        if signature is None:
            continue
        type_names.update(re.findall(r"\bOot3d[A-Za-z0-9_]+\b", signature))
        for value in [
            str(metadata.get("return_type") or ""),
            *_split_types(metadata.get("param_types")),
        ]:
            type_names.update(re.findall(r"\bOot3d[A-Za-z0-9_]+\b", value))
        previous = prototypes.get(name)
        if previous is not None and previous != signature:
            conflicting_names.add(name)
        else:
            prototypes[name] = signature
    for service in services:
        type_names.update(
            re.findall(r"\bOot3d[A-Za-z0-9_]+\b", service["signature"])
        )

    lines = [
        "#pragma once",
        "",
        "// Generated analysis scaffold. Structure layouts and service declarations",
        "// remain explicit promotion inputs; this header does not claim host ABI parity.",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "using s8 = std::int8_t;",
        "using u8 = std::uint8_t;",
        "using s16 = std::int16_t;",
        "using u16 = std::uint16_t;",
        "using s32 = std::int32_t;",
        "using u32 = std::uint32_t;",
        "using s64 = std::int64_t;",
        "using u64 = std::uint64_t;",
        "using f32 = float;",
        "using f64 = double;",
        "using byte = std::uint8_t;",
        "using sbyte = std::int8_t;",
        "using ushort = std::uint16_t;",
        "using uint = std::uint32_t;",
        "using ulong = std::uint32_t;",
        "using longlong = std::int64_t;",
        "using ulonglong = std::uint64_t;",
        "using undefined = std::uint8_t;",
        "using undef1 = std::uint8_t;",
        "using undef2 = std::uint16_t;",
        "using undef4 = std::uint32_t;",
        "using undefined1 = std::uint8_t;",
        "using undefined2 = std::uint16_t;",
        "using undefined4 = std::uint32_t;",
        "using undefined8 = std::uint64_t;",
        "using code = void();",
        "",
        "template <typename T>",
        "inline std::uintptr_t oot3d_host_address(T* value) {",
        "    return reinterpret_cast<std::uintptr_t>(value);",
        "}",
        "",
    ]
    lines.extend(f"struct {name};" for name in sorted(type_names))
    lines.extend(
        [
            "",
            '#include "oot3d_native_layouts.hpp"',
            "",
            "// Closure function declarations.",
        ]
    )
    for function in sorted(records, key=lambda item: item["address"]):
        name = str(function["name"])
        if name in conflicting_names or name not in prototypes:
            continue
        lines.append(f"{prototypes[name]};")
    lines.extend(["", "// Reviewed native service declarations."])
    for service in services:
        lines.append(f"{service['signature']};")
    lines.append("")
    return "\n".join(lines)


def _source_file(
    function: dict[str, Any],
    body: str,
    source_revision: str,
    corpus_path: str,
    logical_source_path: str,
) -> str:
    return "\n".join(
        [
            "// Generated from original OOT3D decompiler evidence.",
            f"// Zelda3drecomp revision: {source_revision}",
            f"// Native address: {function['address']}",
            f"// Assigned logical source: {logical_source_path}",
            '#include "oot3d_native_corpus_forward.hpp"',
            "",
            f'#line 1 "{corpus_path}"',
            body.rstrip(),
            "",
        ]
    )


def build_native_corpus_promotion(
    room_unit_path: Path,
    zelda3drecomp_root: Path,
    *,
    revision: str = "HEAD",
) -> PromotionPackage:
    room_unit = json.loads(room_unit_path.read_text(encoding="utf-8-sig"))
    closure, reference_count, root_slots = _extract_room_closure(room_unit)
    (
        source_revision,
        source_head,
        worktree_change_count,
        files,
        corpus_source,
    ) = _read_revision_files(zelda3drecomp_root, revision)
    inventory = _load_inventory(files)
    data_symbols = _load_data_symbol_map(files)
    function_names = _function_name_map(inventory, closure)

    candidates: dict[str, list[CorpusCandidate]] = defaultdict(list)
    for path, payload in sorted(files.items()):
        if not path.startswith(f"{_CORPUS_ROOT}/") or not path.endswith(".md"):
            continue
        for candidate in _parse_corpus(path, payload):
            if candidate.address in closure:
                candidates[candidate.address].append(candidate)

    function_records: list[dict[str, Any]] = []
    source_files: dict[str, str] = {}
    promoted_bodies: dict[str, str] = {}
    hazard_counts: Counter[str] = Counter()
    readiness_counts: Counter[str] = Counter()
    source_unit_groups: dict[str, list[str]] = defaultdict(list)
    total_body_bytes = 0
    promoted_body_bytes = 0
    typed_body_count = 0
    root_body_addresses: set[str] = set()

    for address, function in sorted(closure.items()):
        row = inventory.get(address)
        if row is None:
            raise ValueError(f"closure function is missing from source inventory: {address}")
        expected_name = str(function["name"])
        chosen = _select_candidate(expected_name, candidates.get(address, []))
        usable_candidates = [
            candidate for candidate in candidates.get(address, []) if candidate.usable
        ]
        body_variant_count = len(
            {candidate.body_sha256 for candidate in usable_candidates}
        )
        total_body_bytes += int(function["byte_length"])

        promoted_body = ""
        replacements: list[dict[str, Any]] = []
        hazards: list[str] = []
        decompiled_signature: str | None = None
        output_source_path: str | None = None
        output_source_sha256: str | None = None
        if chosen is not None:
            promoted_body, replacements = _normalize_body_symbols(
                chosen.body,
                corpus_name=chosen.name,
                expected_name=expected_name,
                function_names=function_names,
                data_symbols=data_symbols,
            )
            promoted_body, pointer_replacements = _normalize_host_pointer_casts(
                promoted_body
            )
            replacements.extend(pointer_replacements)
            promoted_bodies[address] = promoted_body
            decompiled_signature = _extract_definition_signature(
                promoted_body, expected_name
            )
            hazards = _hazards(promoted_body, expected_name)
            promoted_body_bytes += int(function["byte_length"])
            typed_body_count += int(chosen.typed)
            module = row.get("module") or "unknown"
            output_source_path = _source_path(module, address, expected_name)
            source_text = _source_file(
                function,
                promoted_body,
                source_revision,
                chosen.source_path,
                str(row.get("logical_file") or ""),
            )
            output_source_sha256 = hashlib.sha256(
                source_text.encode("utf-8")
            ).hexdigest()
            if output_source_path in source_files:
                raise ValueError(f"duplicate promoted source path: {output_source_path}")
            source_files[output_source_path] = source_text
            source_unit_groups[str(row.get("logical_file") or "")].append(address)
            if function["root_slots"]:
                root_body_addresses.add(address)

        readiness = _compile_readiness(chosen is not None, hazards)
        readiness_counts[readiness] += 1
        hazard_counts.update(hazards)
        record = {
            "address": address,
            "name": expected_name,
            "byte_length": int(function["byte_length"]),
            "families": sorted(function["families"]),
            "closure_kinds": sorted(function["closure_kinds"]),
            "profiles": sorted(function["profiles"]),
            "is_consumer_root": bool(function["root_slots"]),
            "root_slots": sorted(
                function["root_slots"],
                key=lambda item: (item["profile"], item["slot"]),
            ),
            "direct_callees": sorted(
                function["direct_callees"].values(),
                key=lambda item: item["address"],
            ),
            "module": str(row.get("module") or ""),
            "logical_source_path": str(row.get("logical_file") or ""),
            "body_status": "extracted" if chosen is not None else "missing",
            "body_typed": bool(chosen and chosen.typed),
            "corpus_path": chosen.source_path if chosen is not None else None,
            "corpus_function_name": chosen.name if chosen is not None else None,
            "original_body_sha256": (
                chosen.body_sha256 if chosen is not None else None
            ),
            "promoted_body_sha256": (
                hashlib.sha256(promoted_body.encode("utf-8")).hexdigest()
                if chosen is not None
                else None
            ),
            "decompiled_signature": decompiled_signature,
            "candidate_count": len(usable_candidates),
            "body_variant_count": body_variant_count,
            "symbol_replacements": replacements,
            "hazards": hazards,
            "compile_readiness": readiness,
            "output_source_path": output_source_path,
            "output_source_sha256": output_source_sha256,
            "metadata": function["metadata"],
        }
        function_records.append(record)

    layout_promotion = build_native_layout_promotion(
        files, promoted_bodies.values()
    )
    layout_types = [
        record["name"] for record in layout_promotion.manifest["structures"]
    ]
    service_signatures, service_conflicts, signature_catalog_count = (
        _reviewed_service_signatures(
            files,
            promoted_bodies.values(),
            {str(record["name"]) for record in function_records},
        )
    )
    support_header = _support_header(
        function_records, layout_types, service_signatures
    )
    support_header_sha256 = hashlib.sha256(
        support_header.encode("utf-8")
    ).hexdigest()
    layout_header_sha256 = hashlib.sha256(
        layout_promotion.header.encode("utf-8")
    ).hexdigest()
    translation_units = []
    for logical_path, addresses in sorted(source_unit_groups.items()):
        translation_units.append(
            {
                "logical_source_path": logical_path,
                "function_count": len(addresses),
                "function_addresses": sorted(addresses),
            }
        )

    identity = room_unit.get("identity") or {}
    root_unique_count = len({slot["address"] for slot in root_slots})
    body_count = sum(record["body_status"] == "extracted" for record in function_records)
    missing_count = len(function_records) - body_count
    missing_body_bytes = total_body_bytes - promoted_body_bytes
    manifest: dict[str, Any] = {
        "format": FORMAT,
        "generator": {
            "name": "oot3d_asset_tool.native_corpus_promotion",
            "version": GENERATOR_VERSION,
        },
        "source": {
            "repository": "Zelda3drecomp",
            "revision": source_revision,
            "requested_revision": revision,
            "head_at_promotion": source_head,
            "mode": corpus_source["mode"],
            "worktree_changes_ignored": worktree_change_count,
            "committed_evidence_mode": "git_archive_commit",
            "generated_corpus": corpus_source,
        },
        "room_compilation_unit": {
            "unit_id": identity.get("unit_id"),
            "route_id": identity.get("route_id"),
            "setup_index": identity.get("setup_index"),
            "payload_sha256": identity.get("payload_sha256"),
        },
        "policy": {
            "original_oot3d_bodies_only": True,
            "n64_runtime_body_fallback": False,
            "missing_body_stub_generation": False,
            "tracked_source_worktree_consumed": False,
            "generated_corpus_sidecar_consumed": (
                corpus_source["mode"]
                == "git_commit_plus_generated_corpus_sidecar"
            ),
            "one_function_per_output_source": True,
        },
        "counts": {
            "profile_function_references": reference_count,
            "unique_functions": len(function_records),
            "native_body_bytes": total_body_bytes,
            "extracted_bodies": body_count,
            "extracted_body_bytes": promoted_body_bytes,
            "typed_bodies": typed_body_count,
            "missing_bodies": missing_count,
            "missing_body_bytes": missing_body_bytes,
            "consumer_root_slots": len(root_slots),
            "unique_consumer_roots": root_unique_count,
            "consumer_roots_with_body": len(root_body_addresses),
            "logical_translation_units": len(translation_units),
            "output_source_files": len(source_files),
            "functions_without_listed_hazards": sum(
                record["body_status"] == "extracted" and not record["hazards"]
                for record in function_records
            ),
        },
        "hazard_counts": dict(sorted(hazard_counts.items())),
        "compile_readiness_counts": dict(sorted(readiness_counts.items())),
        "support_header": {
            "path": "include/oot3d_native_corpus_forward.hpp",
            "sha256": support_header_sha256,
            "status": "native_partial_layout_scaffold",
        },
        "native_layouts": {
            **layout_promotion.manifest,
            "path": "include/oot3d_native_layouts.hpp",
            "sha256": layout_header_sha256,
        },
        "native_services": {
            "signature_catalog_files": signature_catalog_count,
            "resolved_called_services": len(service_signatures),
            "conflicting_called_services": len(service_conflicts),
            "services": service_signatures,
            "conflicts": service_conflicts,
        },
        "translation_units": translation_units,
        "functions": function_records,
    }
    manifest["payload_sha256"] = canonical_sha256(manifest)
    validate_native_corpus_promotion(manifest)
    report = _promotion_report(manifest)
    return PromotionPackage(
        manifest=manifest,
        report=report,
        support_header=support_header,
        layout_header=layout_promotion.header,
        source_files=source_files,
    )


def validate_native_corpus_promotion(manifest: dict[str, Any]) -> None:
    if manifest.get("format") != FORMAT:
        raise ValueError("invalid native corpus promotion format")
    functions = manifest.get("functions")
    if not isinstance(functions, list) or not functions:
        raise ValueError("native corpus promotion contains no functions")
    addresses = [record.get("address") for record in functions]
    if len(addresses) != len(set(addresses)):
        raise ValueError("native corpus promotion contains duplicate addresses")
    counts = manifest.get("counts")
    if not isinstance(counts, dict):
        raise ValueError("native corpus promotion contains no counts")
    extracted = [record for record in functions if record.get("body_status") == "extracted"]
    missing = [record for record in functions if record.get("body_status") == "missing"]
    if counts.get("unique_functions") != len(functions):
        raise ValueError("native corpus unique function count mismatch")
    if counts.get("extracted_bodies") != len(extracted):
        raise ValueError("native corpus extracted body count mismatch")
    if counts.get("missing_bodies") != len(missing):
        raise ValueError("native corpus missing body count mismatch")
    if counts.get("output_source_files") != len(extracted):
        raise ValueError("native corpus source file count mismatch")
    digest = manifest.get("payload_sha256")
    payload = dict(manifest)
    payload.pop("payload_sha256", None)
    if digest != canonical_sha256(payload):
        raise ValueError("native corpus promotion payload digest mismatch")


def _promotion_report(manifest: dict[str, Any]) -> str:
    counts = manifest["counts"]
    layouts = manifest["native_layouts"]
    services = manifest["native_services"]
    source = manifest["source"]
    room = manifest["room_compilation_unit"]
    lines = [
        "# OOT3D Native Corpus Promotion",
        "",
        f"- Source revision: `{source['revision']}`",
        f"- Source mode: `{source['mode']}`",
        f"- Ignored source worktree changes: {source['worktree_changes_ignored']}",
        f"- Generated corpus files: {source['generated_corpus']['file_count']}",
        f"- Generated corpus bytes: {source['generated_corpus']['total_bytes']}",
        "- Generated corpus digest: "
        f"`{source['generated_corpus']['aggregate_sha256']}`",
        f"- Room unit: `{room.get('unit_id')}`",
        f"- Room unit payload: `{room.get('payload_sha256')}`",
        f"- Promotion payload: `{manifest['payload_sha256']}`",
        "",
        "## Coverage",
        "",
        f"- Profile function references: {counts['profile_function_references']}",
        f"- Unique closure functions: {counts['unique_functions']}",
        f"- Extracted C bodies: {counts['extracted_bodies']}",
        f"- Typed C bodies: {counts['typed_bodies']}",
        f"- Missing C bodies: {counts['missing_bodies']}",
        f"- Native body bytes: {counts['native_body_bytes']}",
        f"- Extracted body bytes: {counts['extracted_body_bytes']}",
        f"- Missing body bytes: {counts['missing_body_bytes']}",
        f"- Consumer root slots: {counts['consumer_root_slots']}",
        f"- Unique consumer roots: {counts['unique_consumer_roots']}",
        f"- Consumer roots with body: {counts['consumer_roots_with_body']}",
        f"- Logical translation units: {counts['logical_translation_units']}",
        f"- Per-function source files: {counts['output_source_files']}",
        "",
        "## Native Layouts",
        "",
        f"- Versioned field catalogs: {layouts['field_catalog_files']}",
        f"- Field evidence rows: {layouts['field_catalog_rows']}",
        f"- Closure structure references: {layouts['closure_type_references']}",
        f"- Selected structures: {layouts['selected_structures']}",
        f"- Generated partial structures: {layouts['generated_structures']}",
        f"- Forward-only structures: {layouts['forward_only_structures']}",
        f"- Emitted named fields: {layouts['emitted_fields']}",
        f"- Reviewed called services: {services['resolved_called_services']}",
        f"- Conflicting called services: {services['conflicting_called_services']}",
        "",
        "## Compile Readiness",
        "",
    ]
    for name, count in manifest["compile_readiness_counts"].items():
        lines.append(f"- `{name}`: {count}")
    lines.extend(["", "## Remaining Hazards", ""])
    if manifest["hazard_counts"]:
        for name, count in manifest["hazard_counts"].items():
            lines.append(f"- `{name}`: {count}")
    else:
        lines.append("- None")
    lines.extend(["", "## Missing Bodies", ""])
    missing = [
        function
        for function in manifest["functions"]
        if function["body_status"] == "missing"
    ]
    if missing:
        for function in missing:
            lines.append(
                f"- `{function['address']}` `{function['name']}` "
                f"({function['byte_length']} bytes, `{function['logical_source_path']}`)"
            )
    else:
        lines.append("- None")
    lines.extend(
        [
            "",
            "## Policy",
            "",
            "The package contains only bodies extracted from the pinned OOT3D corpus.",
            "Committed inventories and maintained symbols are read from the requested",
            "Git revision. If the generated Ghidra corpus is not committed, promotion",
            "requires the same checkout HEAD and records the complete sidecar digest.",
            "Missing functions remain explicit; no N64 body, no-op, or synthesized gameplay",
            "implementation is emitted. Generated forward declarations are an analysis",
            "scaffold and do not claim host ABI compatibility.",
            "",
        ]
    )
    return "\n".join(lines)


def write_native_corpus_promotion(
    package: PromotionPackage, output_root: Path, *, replace: bool = False
) -> Path:
    output_root = output_root.resolve()
    output_root.parent.mkdir(parents=True, exist_ok=True)
    if output_root.exists():
        marker = output_root / "manifest.json"
        if not replace:
            raise ValueError(f"native corpus output already exists: {output_root}")
        if not marker.is_file():
            raise ValueError(
                f"refusing to replace an unrecognized output directory: {output_root}"
            )
        existing = json.loads(marker.read_text(encoding="utf-8-sig"))
        if existing.get("format") != FORMAT:
            raise ValueError(
                f"refusing to replace a directory with another format: {output_root}"
            )

    staging = output_root.parent / f".{output_root.name}.staging-{os.getpid()}"
    if staging.exists():
        raise ValueError(f"native corpus staging path already exists: {staging}")
    try:
        staging.mkdir()
        include_path = staging / "include" / "oot3d_native_corpus_forward.hpp"
        include_path.parent.mkdir(parents=True)
        include_path.write_text(package.support_header, encoding="utf-8", newline="\n")
        layout_path = staging / "include" / "oot3d_native_layouts.hpp"
        layout_path.write_text(package.layout_header, encoding="utf-8", newline="\n")
        for relative, source in sorted(package.source_files.items()):
            path = staging.joinpath(*PurePosixPath(relative).parts)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8", newline="\n")
        (staging / "manifest.json").write_text(
            json.dumps(package.manifest, indent=2, sort_keys=True, ensure_ascii=True)
            + "\n",
            encoding="utf-8",
            newline="\n",
        )
        (staging / "report.md").write_text(
            package.report, encoding="utf-8", newline="\n"
        )

        if output_root.exists():
            shutil.rmtree(output_root)
        staging.rename(output_root)
    except Exception:
        if staging.exists():
            shutil.rmtree(staging)
        raise
    return output_root


def promote_native_corpus(
    room_unit_path: Path,
    zelda3drecomp_root: Path,
    output_root: Path,
    *,
    revision: str = "HEAD",
    replace: bool = False,
) -> PromotionPackage:
    package = build_native_corpus_promotion(
        room_unit_path,
        zelda3drecomp_root,
        revision=revision,
    )
    write_native_corpus_promotion(package, output_root, replace=replace)
    return package
