from __future__ import annotations

import csv
import hashlib
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any, Iterable


FORMAT = "oot3d_native_abi_catalog_v1"
SCHEMA_VERSION = 1

_CATALOG_PATTERNS = (
    (
        "indirect_root",
        re.compile(
            r"^analysis/codebin_indirect_ready_.+_roots_tranche_(\d{3})\.csv$"
        ),
    ),
    (
        "parent_owned_root",
        re.compile(r"^analysis/codebin_parent_owned_.+_tranche_(\d{3})\.csv$"),
    ),
    (
        "maintained_abi",
        re.compile(
            r"^analysis/codebin_maintained_abi_.+closure_tranche_(\d{3})\.csv$"
        ),
    ),
    (
        "callable_identity",
        re.compile(
            r"^analysis/codebin_callable_identity_closure_tranche_(\d{3})\.csv$"
        ),
    ),
    (
        "actor_private_native",
        re.compile(
            r"^analysis/codebin_actor_private_native_closure_tranche_(\d{3})\.csv$"
        ),
    ),
    (
        "actor_priority_native",
        re.compile(
            r"^analysis/codebin_actor_priority_native_closure_tranche_(\d{3})\.csv$"
        ),
    ),
    (
        "indirect_root",
        re.compile(
            r"^analysis/codebin_actor_indirect_native_roots_tranche_(\d{3})\.csv$"
        ),
    ),
)

_SUPPLEMENTAL_SIGNATURE_CATALOGS = frozenset(
    {
        "analysis/codebin_embedded_helper_signatures.csv",
    }
)

_SUBSYSTEM_CONTRACTS = (
    (
        "skeletal_animation",
        (
            "Animation_Change",
            "Animation_OnFrameImpl",
            "SkelAnime_InitLink",
            "SkelAnime_SetUpdate",
            "SkelAnime_Update",
        ),
    ),
    (
        "actor_shadow",
        (
            "ActorShadow_Draw",
            "ActorShadow_DrawFeet",
        ),
    ),
    (
        "dynamic_collision",
        (
            "BgCheck_LineTestImpl",
            "BgCheck_RaycastFloorImpl",
            "BgCheck_SphVsWallImpl",
            "DynaPoly_SetBgActor",
        ),
    ),
    (
        "effect_runtime",
        (
            "Effect_Add",
            "EffectSs_Spawn",
        ),
    ),
    (
        "water_surface",
        (
            "WaterBox_GetSurface1",
            "WaterBox_GetSurfaceImpl",
        ),
    ),
    ("scene_lighting", ("LightContext_InsertLight",)),
    ("gameplay_camera", ("Gameplay_CameraSetAtEye",)),
)


def canonical_sha256(value: object) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def _normalize_address(value: object) -> str:
    text = str(value or "0").strip().lower().removeprefix("0x")
    try:
        number = int(text or "0", 16)
    except ValueError as exc:
        raise ValueError(f"invalid native ABI address: {value!r}") from exc
    if not 0 < number <= 0xFFFFFFFF:
        raise ValueError(f"native ABI address is outside code.bin: {value!r}")
    return f"0x{number:08X}"


def _read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None:
            raise ValueError(f"native ABI catalog has no CSV header: {path}")
        return [dict(row) for row in reader]


def _split_types(value: object) -> list[str]:
    return [part.strip() for part in str(value or "").split(";") if part.strip()]


def _catalog_identity(relative: str) -> tuple[str, int] | None:
    for closure_kind, pattern in _CATALOG_PATTERNS:
        match = pattern.fullmatch(relative)
        if match is not None:
            return closure_kind, int(match.group(1))
    return None


def _source_tranche(value: object) -> int:
    match = re.search(r"(?:batch|tranche)_(\d{3})", str(value or ""))
    return int(match.group(1)) if match is not None else 0


def _native_pointer_types(parameter_types: list[str]) -> list[str]:
    return sorted(
        {
            value
            for value in parameter_types
            if re.fullmatch(r"Oot3d[A-Za-z0-9_]+\*", value)
        }
    )


def _is_concrete_actor_pointer(value: object) -> bool:
    text = str(value or "")
    return (
        re.fullmatch(r"Oot3d[A-Za-z0-9_]+\*", text) is not None
        and text not in {"Oot3dActor*", "Oot3dPlayState*"}
    )


def _is_typed_actor_refinement(
    candidate: dict[str, Any], baseline: dict[str, Any]
) -> bool:
    candidate_types = candidate["parameter_types"]
    baseline_types = baseline["parameter_types"]
    return (
        candidate["address"] == baseline["address"]
        and candidate["byte_length"] == baseline["byte_length"]
        and candidate["return_type"] == baseline["return_type"]
        and candidate["source_tranche"] > baseline["source_tranche"]
        and len(candidate_types) == len(baseline_types)
        and bool(candidate_types)
        and bool(baseline_types)
        and baseline_types[0] == "Oot3dActor*"
        and _is_concrete_actor_pointer(candidate_types[0])
        and candidate["closure_kind"]
        in {"actor_private_native", "actor_priority_native"}
        and str(candidate["family"]).startswith("actor_")
        and str(baseline["family"]).startswith("actor_")
    )


def _merge_reviewed_function(
    existing: dict[str, Any], candidate: dict[str, Any]
) -> dict[str, Any]:
    if _is_typed_actor_refinement(candidate, existing):
        winner, superseded = candidate, existing
    elif _is_typed_actor_refinement(existing, candidate):
        winner, superseded = existing, candidate
    else:
        raise ValueError(
            "native ABI reviewed catalogs overlap at "
            f"{candidate['address']} ({candidate['name']})"
        )

    merged = dict(winner)
    history = list(winner.get("superseded_evidence", []))
    history.extend(superseded.get("superseded_evidence", []))
    history.append(
        {
            key: value
            for key, value in superseded.items()
            if key != "superseded_evidence"
        }
    )
    history.sort(
        key=lambda row: (
            int(row["source_tranche"]),
            str(row["evidence_file"]),
            str(row["name"]),
        )
    )
    merged["superseded_evidence"] = history
    return merged


def _supplemental_inventory(
    snapshot_root: Path, selected_files: set[str]
) -> dict[str, dict[str, str]]:
    relative = "analysis/codebin_function_inventory.csv"
    if relative not in selected_files:
        raise ValueError("supplemental native ABI catalogs require function inventory")
    rows: dict[str, dict[str, str]] = {}
    for row in _read_csv(snapshot_root / relative):
        address = _normalize_address(row.get("entry"))
        if address in rows:
            raise ValueError(f"function inventory duplicates {address}")
        rows[address] = row
    return rows


def _build_subsystem_contracts(
    functions: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    by_name = {str(row["name"]): row for row in functions}
    result: list[dict[str, Any]] = []
    for contract_id, required_names in _SUBSYSTEM_CONTRACTS:
        required = list(required_names)
        available = [name for name in required if name in by_name]
        missing = [name for name in required if name not in by_name]
        rows = [by_name[name] for name in available]
        result.append(
            {
                "id": contract_id,
                "status": (
                    "reviewed_native_contract_complete"
                    if not missing
                    else "reviewed_native_contract_incomplete"
                ),
                "required_functions": required,
                "available_functions": available,
                "missing_functions": missing,
                "source_tranches": sorted(
                    {
                        int(row["source_tranche"])
                        for row in rows
                        if int(row["source_tranche"]) > 0
                    }
                ),
                "evidence_files": sorted(
                    {
                        str(row["evidence_file"])
                        for row in rows
                    }
                ),
            }
        )
    return result


def build_native_abi_catalog(
    snapshot_root: Path,
    selected_files: Iterable[str],
    *,
    snapshot_id: str,
    source_base_revision: str,
    code_bin_sha256: str,
) -> dict[str, Any]:
    selected = set(selected_files)
    functions_by_address: dict[str, dict[str, Any]] = {}
    sources: list[dict[str, Any]] = []

    for relative in sorted(selected):
        identity = _catalog_identity(relative)
        if identity is None:
            continue
        closure_kind, tranche = identity
        signature_relative = relative[:-4] + "_signatures.csv"
        catalog_path = snapshot_root / relative
        signature_path = snapshot_root / signature_relative
        if not catalog_path.is_file() or not signature_path.is_file():
            raise ValueError(
                f"native ABI tranche {tranche} lacks its reviewed catalog pair"
            )

        rows = _read_csv(catalog_path)
        signature_rows = _read_csv(signature_path)
        signatures: dict[str, dict[str, str]] = {}
        for signature in signature_rows:
            address = _normalize_address(signature.get("entry"))
            if address in signatures:
                raise ValueError(
                    f"native ABI tranche {tranche} duplicates signature {address}"
                )
            signatures[address] = signature
        if len(rows) != len(signatures):
            raise ValueError(
                f"native ABI tranche {tranche} has incomplete signature coverage"
            )

        source_count = 0
        for row in rows:
            address = _normalize_address(row.get("entry"))
            signature = signatures.get(address)
            name = str(row.get("name", ""))
            if signature is None or signature.get("name") != name:
                raise ValueError(
                    f"native ABI tranche {tranche} has ambiguous identity at {address}"
                )
            parameter_types = _split_types(signature.get("param_types"))
            parameter_names = _split_types(signature.get("param_names"))
            if parameter_names and len(parameter_names) != len(parameter_types):
                raise ValueError(
                    f"native ABI function {name} has inconsistent parameter names"
                )
            native_pointer_types = _native_pointer_types(parameter_types)
            try:
                byte_length = int(str(row.get("size", "0")), 0)
            except ValueError as exc:
                raise ValueError(f"native ABI function {name} has invalid size") from exc
            if byte_length <= 0:
                raise ValueError(f"native ABI function {name} has empty body")

            function = {
                "address": address,
                "name": name,
                "family": str(row.get("family", "")),
                "byte_length": byte_length,
                "confidence": str(row.get("confidence", "")),
                "return_type": str(signature.get("return_type", "")),
                "parameter_types": parameter_types,
                "parameter_names": parameter_names,
                "native_pointer_types": native_pointer_types,
                "closure_kind": closure_kind,
                "source_tranche": tranche,
                "source": str(signature.get("source", "")),
                "evidence_file": relative,
                "signature_evidence_file": signature_relative,
                "notes": str(row.get("notes", signature.get("notes", ""))),
            }
            existing = functions_by_address.get(address)
            functions_by_address[address] = (
                function
                if existing is None
                else _merge_reviewed_function(existing, function)
            )
            source_count += 1
        sources.append(
            {
                "closure_kind": closure_kind,
                "source_tranche": tranche,
                "evidence_file": relative,
                "signature_evidence_file": signature_relative,
                "function_count": source_count,
            }
        )

    functions = list(functions_by_address.values())
    names: set[str] = set()
    for function in functions:
        name = str(function["name"])
        if name in names:
            raise ValueError(f"native ABI reviewed catalogs duplicate name {name}")
        names.add(name)

    supplemental = sorted(selected & _SUPPLEMENTAL_SIGNATURE_CATALOGS)
    if supplemental:
        inventory = _supplemental_inventory(snapshot_root, selected)
        by_address = {str(row["address"]): row for row in functions}
        for relative in supplemental:
            source_count = 0
            for signature in _read_csv(snapshot_root / relative):
                address = _normalize_address(signature.get("entry"))
                name = str(signature.get("name", ""))
                parameter_types = _split_types(signature.get("param_types"))
                parameter_names = _split_types(signature.get("param_names"))
                if not name or len(parameter_names) != len(parameter_types):
                    raise ValueError(
                        f"supplemental native ABI {address} has malformed signature"
                    )
                existing = by_address.get(address)
                if existing is not None:
                    if (
                        existing["name"] != name
                        or existing["return_type"]
                        != str(signature.get("return_type", ""))
                        or existing["parameter_types"] != parameter_types
                        or existing["parameter_names"] != parameter_names
                    ):
                        raise ValueError(
                            f"supplemental native ABI conflicts at {address} ({name})"
                        )
                    continue
                if name in names:
                    raise ValueError(f"supplemental native ABI duplicates name {name}")
                inventory_row = inventory.get(address)
                if inventory_row is None:
                    raise ValueError(
                        f"supplemental native ABI lacks inventory row at {address}"
                    )
                try:
                    byte_length = int(str(inventory_row.get("size", "0")), 0)
                except ValueError as exc:
                    raise ValueError(
                        f"supplemental native ABI {name} has invalid body size"
                    ) from exc
                family = str(inventory_row.get("module", "")).replace("/", "_")
                if byte_length <= 0 or not family:
                    raise ValueError(
                        f"supplemental native ABI {name} lacks body ownership"
                    )
                source = str(signature.get("source", ""))
                function = {
                    "address": address,
                    "name": name,
                    "family": family,
                    "byte_length": byte_length,
                    "confidence": str(signature.get("confidence", "")),
                    "return_type": str(signature.get("return_type", "")),
                    "parameter_types": parameter_types,
                    "parameter_names": parameter_names,
                    "native_pointer_types": _native_pointer_types(parameter_types),
                    "closure_kind": "reviewed_signature",
                    "source_tranche": _source_tranche(source),
                    "source": source,
                    "evidence_file": relative,
                    "signature_evidence_file": relative,
                    "notes": str(signature.get("notes", "")),
                }
                functions.append(function)
                by_address[address] = function
                names.add(name)
                source_count += 1
            if source_count:
                sources.append(
                    {
                        "closure_kind": "reviewed_signature",
                        "source_tranche": 0,
                        "evidence_file": relative,
                        "signature_evidence_file": relative,
                        "function_count": source_count,
                    }
                )

    functions.sort(key=lambda row: int(row["address"], 0))
    sources.sort(
        key=lambda row: (
            int(row["source_tranche"]),
            str(row["closure_kind"]),
            str(row["evidence_file"]),
        )
    )
    closure_counts = Counter(str(row["closure_kind"]) for row in functions)
    family_counts = Counter(str(row["family"]) for row in functions)
    subsystem_contracts = _build_subsystem_contracts(functions)
    document: dict[str, Any] = {
        "format": FORMAT,
        "schema_version": SCHEMA_VERSION,
        "status": "reviewed_native_abi_catalog_complete",
        "source_snapshot_id": snapshot_id,
        "source_base_revision": source_base_revision,
        "code_bin_sha256": code_bin_sha256,
        "function_count": len(functions),
        "source_catalog_count": len(sources),
        "closure_kind_counts": dict(sorted(closure_counts.items())),
        "family_counts": dict(sorted(family_counts.items())),
        "native_pointer_function_count": sum(
            bool(row["native_pointer_types"]) for row in functions
        ),
        "superseded_evidence_count": sum(
            len(row.get("superseded_evidence", [])) for row in functions
        ),
        "subsystem_contract_count": len(subsystem_contracts),
        "complete_subsystem_contract_count": sum(
            row["status"] == "reviewed_native_contract_complete"
            for row in subsystem_contracts
        ),
        "sources": sources,
        "functions": functions,
        "subsystem_contracts": subsystem_contracts,
        "ownership_policy": (
            "native pointer types are evidence only; actor-profile ownership requires "
            "either an exact ActorInit structure pointer or an exact maintained actor "
            "symbol prefix plus native callback-context ABI match in the consuming room "
            "compiler; generic Oot3dActor pointers never establish ownership"
        ),
    }
    document["payload_sha256"] = canonical_sha256(document)
    validate_native_abi_catalog(document)
    return document


def validate_native_abi_catalog(document: dict[str, Any]) -> None:
    if (
        document.get("format") != FORMAT
        or document.get("schema_version") != SCHEMA_VERSION
        or document.get("status") != "reviewed_native_abi_catalog_complete"
    ):
        raise ValueError("unsupported native ABI catalog")
    functions = document.get("functions")
    sources = document.get("sources")
    subsystem_contracts = document.get("subsystem_contracts")
    if (
        not isinstance(functions, list)
        or not isinstance(sources, list)
        or not isinstance(subsystem_contracts, list)
    ):
        raise ValueError("native ABI catalog arrays are missing")
    if document.get("function_count") != len(functions):
        raise ValueError("native ABI function count does not close")
    if document.get("source_catalog_count") != len(sources):
        raise ValueError("native ABI source count does not close")

    addresses: set[str] = set()
    names: set[str] = set()
    observed_closures: Counter[str] = Counter()
    observed_families: Counter[str] = Counter()
    superseded_evidence_count = 0
    for function in functions:
        if not isinstance(function, dict):
            raise ValueError("native ABI function is not an object")
        address = _normalize_address(function.get("address"))
        name = str(function.get("name", ""))
        if address in addresses or not name or name in names:
            raise ValueError("native ABI function identity is duplicated or empty")
        addresses.add(address)
        names.add(name)
        if (
            not isinstance(function.get("parameter_types"), list)
            or not isinstance(function.get("parameter_names"), list)
            or not isinstance(function.get("native_pointer_types"), list)
            or not isinstance(function.get("byte_length"), int)
            or function["byte_length"] <= 0
            or not isinstance(function.get("source_tranche"), int)
        ):
            raise ValueError("native ABI function contract is malformed")
        observed_closures[str(function.get("closure_kind", ""))] += 1
        observed_families[str(function.get("family", ""))] += 1
        superseded = function.get("superseded_evidence", [])
        if not isinstance(superseded, list):
            raise ValueError("native ABI superseded evidence is malformed")
        for prior in superseded:
            if (
                not isinstance(prior, dict)
                or _normalize_address(prior.get("address")) != address
                or prior.get("byte_length") != function["byte_length"]
                or not isinstance(prior.get("source_tranche"), int)
                or prior["source_tranche"] >= function["source_tranche"]
                or not prior.get("name")
                or not isinstance(prior.get("parameter_types"), list)
            ):
                raise ValueError("native ABI superseded evidence is inconsistent")
        superseded_evidence_count += len(superseded)
    if document.get("closure_kind_counts") != dict(sorted(observed_closures.items())):
        raise ValueError("native ABI closure-kind counts do not close")
    if document.get("family_counts") != dict(sorted(observed_families.items())):
        raise ValueError("native ABI family counts do not close")
    if document.get("native_pointer_function_count") != sum(
        bool(function["native_pointer_types"]) for function in functions
    ):
        raise ValueError("native ABI pointer-function count does not close")
    if document.get("superseded_evidence_count", 0) != superseded_evidence_count:
        raise ValueError("native ABI superseded-evidence count does not close")

    if document.get("subsystem_contract_count") != len(subsystem_contracts):
        raise ValueError("native ABI subsystem contract count does not close")
    functions_by_name = {str(row["name"]): row for row in functions}
    contract_ids: set[str] = set()
    complete_contracts = 0
    for contract in subsystem_contracts:
        if not isinstance(contract, dict) or not isinstance(contract.get("id"), str):
            raise ValueError("native ABI subsystem contract is malformed")
        contract_id = contract["id"]
        if not contract_id or contract_id in contract_ids:
            raise ValueError("native ABI subsystem contract identity is duplicated")
        contract_ids.add(contract_id)
        required = contract.get("required_functions")
        available = contract.get("available_functions")
        missing = contract.get("missing_functions")
        if (
            not isinstance(required, list)
            or not isinstance(available, list)
            or not isinstance(missing, list)
            or len(required) != len(set(required))
            or available != [name for name in required if name in functions_by_name]
            or missing != [name for name in required if name not in functions_by_name]
            or set(available).intersection(missing)
            or set(available).union(missing) != set(required)
        ):
            raise ValueError("native ABI subsystem contract functions do not close")
        expected_status = (
            "reviewed_native_contract_complete"
            if not missing
            else "reviewed_native_contract_incomplete"
        )
        if contract.get("status") != expected_status:
            raise ValueError("native ABI subsystem contract status is inconsistent")
        if expected_status == "reviewed_native_contract_complete":
            complete_contracts += 1
    if document.get("complete_subsystem_contract_count") != complete_contracts:
        raise ValueError("native ABI complete subsystem count does not close")

    payload_sha256 = document.get("payload_sha256")
    digest_input = dict(document)
    digest_input.pop("payload_sha256", None)
    if payload_sha256 != canonical_sha256(digest_input):
        raise ValueError("native ABI catalog payload hash mismatch")


def load_native_abi_catalog(path: Path) -> dict[str, Any]:
    document = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(document, dict):
        raise ValueError(f"{path}: native ABI catalog is not an object")
    validate_native_abi_catalog(document)
    return document
