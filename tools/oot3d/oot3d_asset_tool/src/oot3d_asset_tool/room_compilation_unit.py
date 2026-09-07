from __future__ import annotations

import csv
import hashlib
import json
import re
from collections import Counter, defaultdict
from copy import deepcopy
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from .binary import BinaryView
from .native_actor_contract_common import (
    OBJECT_TABLE,
    actor_profile,
    code_offset,
    object_path,
)
from .native_abi_catalog import load_native_abi_catalog
from .zar import ZarArchive
from .zsi_scene_index import export_zsi_scene_index


FORMAT = "oot3d_room_compilation_unit_v1"
SCHEMA_VERSION = 1
COMPILER_VERSION = "1.7.0"
SEMANTIC_ROUTE_FORMAT = "oot3d_semantic_route_catalog_v1"
ASSET_CATALOG_FORMAT = "oot3d_asset_catalog_v1"
EVIDENCE_SNAPSHOT_FORMAT = "oot3d_zelda3drecomp_evidence_snapshot_v1"
ROOM_CALLBACK_RUNTIME_CONTRACT_FORMAT = "oot3d_room_scene_callback_native_contracts_v1"

ACTOR_STRUCTURAL_FIELD_CATALOGS = (
    "analysis/codebin_actor_embedded_fields.csv",
    "analysis/codebin_actor_animation_table_fields.csv",
    "analysis/codebin_actor_face_animation_fields.csv",
    "analysis/codebin_actor_material_binding_fields.csv",
    "analysis/codebin_actor_material_animation_fields.csv",
    "analysis/codebin_actor_typed_helper_fields.csv",
    "analysis/codebin_actor_native_record_fields.csv",
    "analysis/codebin_actor_fixed_stride_array_fields.csv",
    "analysis/codebin_actor_tail_scalar_fields.csv",
    "analysis/codebin_actor_ambiguous_storage_fields.csv",
    "analysis/codebin_actor_native_storage_span_fields.csv",
)

ACTOR_CONSUMER_SIGNATURE_CATALOGS = (
    "analysis/codebin_actor_callback_body_closure_tranche_162_signatures.csv",
    "analysis/codebin_ready_large_band_tranche_144_signatures.csv",
    "analysis/codebin_actor_common_layout_closure_tranche_167_signatures.csv",
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_sha256(value: object) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected a JSON object")
    return value


def read_hex_u32(value: object, label: str) -> int:
    try:
        result = int(str(value), 0)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{label} is not an integer: {value!r}") from exc
    if not 0 <= result <= 0xFFFFFFFF:
        raise ValueError(f"{label} is outside uint32: {value!r}")
    return result


def load_room_callback_runtime_contracts(
    path: Path, code_bin: Path
) -> dict[str, dict[str, Any]]:
    document = read_json(path)
    if (
        document.get("format") != ROOM_CALLBACK_RUNTIME_CONTRACT_FORMAT
        or document.get("status") != "verified_partial_coverage"
    ):
        raise ValueError("unsupported room-scene callback runtime contract registry")
    code_identity = document.get("code_bin")
    if not isinstance(code_identity, dict):
        raise ValueError("room callback registry lacks code.bin identity")
    code = code_bin.read_bytes()
    if hashlib.sha256(code).hexdigest() != code_identity.get("sha256"):
        raise ValueError("room callback registry targets a different code.bin")
    code_base = read_hex_u32(code_identity.get("base_address"), "code.bin base")

    result: dict[str, dict[str, Any]] = {}
    for contract in document.get("contracts", []):
        if not isinstance(contract, dict):
            raise ValueError("room callback registry contains a non-object contract")
        callback_address = read_hex_u32(
            contract.get("callback_address"), "room callback address"
        )
        callback_key = f"{callback_address:08x}"
        function_size = contract.get("function_size")
        if not isinstance(function_size, int) or function_size <= 0:
            raise ValueError("room callback function size is invalid")
        function_offset = callback_address - code_base
        function_end = function_offset + function_size
        if function_offset < 0 or function_end > len(code):
            raise ValueError("room callback function is outside code.bin")
        if hashlib.sha256(
            code[function_offset:function_end]
        ).hexdigest() != contract.get("function_sha256"):
            raise ValueError(
                f"room callback {callback_key} differs from its verified function body"
            )

        literal_words = contract.get("literal_words")
        if not isinstance(literal_words, list) or not literal_words:
            raise ValueError("room callback contract lacks literal verification")
        for literal in literal_words:
            if not isinstance(literal, dict):
                raise ValueError("room callback literal is not an object")
            address = read_hex_u32(literal.get("address"), "literal address")
            expected = read_hex_u32(literal.get("value"), "literal value")
            offset = address - code_base
            if offset < 0 or offset + 4 > len(code):
                raise ValueError("room callback literal is outside code.bin")
            actual = int.from_bytes(code[offset : offset + 4], "little")
            if actual != expected:
                raise ValueError(
                    f"room callback literal 0x{address:08X} differs from code.bin"
                )

        operations = contract.get("operations")
        if not isinstance(operations, list) or not operations:
            raise ValueError("room callback contract has no typed operations")
        for operation in operations:
            if (
                not isinstance(operation, dict)
                or operation.get("kind") != "material_tev_constant_alpha"
                or operation.get("native_operation") != 2
            ):
                raise ValueError(
                    "room callback contains an unsupported typed operation"
                )
            alpha = operation.get("alpha")
            random = alpha.get("random") if isinstance(alpha, dict) else None
            if not isinstance(random, dict):
                raise ValueError("room callback random function contract is missing")
            function_address = read_hex_u32(
                random.get("function_address"), "room callback random function address"
            )
            function_size = random.get("function_size")
            function_sha256 = random.get("function_sha256")
            state_address = read_hex_u32(
                random.get("state_address"), "room callback random state address"
            )
            if (
                not isinstance(function_size, int)
                or function_size <= 0
                or not isinstance(function_sha256, str)
                or len(function_sha256) != 64
            ):
                raise ValueError("room callback random function identity is invalid")
            function_offset = function_address - code_base
            function_end = function_offset + function_size
            state_offset = state_address - code_base
            if (
                function_offset < 0
                or function_end > len(code)
                or state_offset < 0
                or state_offset + 8 > len(code)
            ):
                raise ValueError("room callback random function data is outside code.bin")
            if (
                hashlib.sha256(code[function_offset:function_end]).hexdigest()
                != function_sha256
            ):
                raise ValueError("room callback random function differs from code.bin")
        if callback_key in result:
            raise ValueError(f"duplicate room callback contract: {callback_key}")
        result[callback_key] = {
            "format": "oot3d_room_scene_callback_native_runtime_v1",
            "status": "native_typed_operations_ready",
            "callback_address": address_hex(callback_address),
            "callback_name": contract.get("callback_name"),
            "role": contract.get("role"),
            "function_size": function_size,
            "function_sha256": contract.get("function_sha256"),
            "literal_verification_count": len(literal_words),
            "runtime_binding_status": contract.get("runtime_binding_status"),
            "operations": deepcopy(operations),
            "source_registry": path.name,
        }
    return result


def normalize_address(value: object) -> str:
    text = str(value or "0").strip().lower().removeprefix("0x")
    try:
        return f"{int(text or '0', 16):08x}"
    except ValueError as exc:
        raise ValueError(f"invalid native address: {value!r}") from exc


def reconcile_indirect_root_evidence(
    catalog: dict[str, Any], indirect_roots: list[dict[str, str]]
) -> list[dict[str, str]]:
    catalog_indirect = {
        normalize_address(row.get("address"))
        for row in catalog["functions"]
        if row.get("closure_kind") == "indirect_root"
    }
    superseded_indirect = {
        normalize_address(prior.get("address"))
        for row in catalog["functions"]
        for prior in row.get("superseded_evidence", [])
        if prior.get("closure_kind") == "indirect_root"
    }
    imported_indirect = {
        normalize_address(row.get("entry")) for row in indirect_roots
    }
    if (
        catalog_indirect.intersection(superseded_indirect)
        or catalog_indirect.union(superseded_indirect) != imported_indirect
    ):
        raise ValueError(
            "native ABI catalog indirect-root closure differs from source evidence"
        )
    return [
        row
        for row in indirect_roots
        if normalize_address(row.get("entry")) in catalog_indirect
    ]


def address_hex(value: int) -> str:
    return f"0x{value:08X}"


def logical_file(path: Path, logical_path: str) -> dict[str, Any]:
    if not path.is_file():
        raise ValueError(f"required native source is missing: {path}")
    return {
        "logical_path": logical_path.replace("\\", "/"),
        "byte_length": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as source:
        return [dict(row) for row in csv.DictReader(source)]


@dataclass(frozen=True)
class EvidenceSnapshot:
    root: Path
    manifest: dict[str, Any]
    files: dict[str, dict[str, Any]]
    derived_files: dict[str, dict[str, Any]]

    @classmethod
    def load(cls, root: Path) -> "EvidenceSnapshot":
        root = root.resolve()
        manifest_path = root / "manifest.json"
        manifest = read_json(manifest_path)
        if manifest.get("format") != EVIDENCE_SNAPSHOT_FORMAT:
            raise ValueError(f"{manifest_path}: unsupported evidence snapshot format")

        def verified_files(records: object, label: str) -> dict[str, dict[str, Any]]:
            if not isinstance(records, list):
                raise ValueError(f"{manifest_path}: {label} records are not an array")
            result: dict[str, dict[str, Any]] = {}
            for row in records:
                if not isinstance(row, dict) or not isinstance(row.get("path"), str):
                    raise ValueError(f"{manifest_path}: invalid {label} file record")
                relative = Path(row["path"])
                if relative.is_absolute() or ".." in relative.parts:
                    raise ValueError(f"{manifest_path}: unsafe {label} path {relative}")
                source = root / relative
                if not source.is_file():
                    raise ValueError(f"{label} snapshot file is missing: {source}")
                expected_size = row.get("size")
                if (
                    not isinstance(expected_size, int)
                    or source.stat().st_size != expected_size
                ):
                    raise ValueError(f"{label} snapshot size mismatch: {source}")
                actual = sha256_file(source)
                if actual != row.get("sha256"):
                    raise ValueError(f"{label} snapshot hash mismatch: {source}")
                normalized = relative.as_posix()
                if normalized in result:
                    raise ValueError(f"{manifest_path}: duplicate {label} path {normalized}")
                result[normalized] = dict(row)
            return result

        files = verified_files(manifest.get("files", []), "evidence")
        derived_files = verified_files(
            manifest.get("derived_artifacts", []), "derived evidence"
        )
        if set(files) & set(derived_files):
            raise ValueError(f"{manifest_path}: evidence and derived paths overlap")
        if len(files) != int(manifest.get("file_count", -1)):
            raise ValueError(f"{manifest_path}: evidence file count mismatch")
        if "derived_artifact_count" in manifest and len(derived_files) != int(
            manifest["derived_artifact_count"]
        ):
            raise ValueError(f"{manifest_path}: derived evidence file count mismatch")
        return cls(
            root=root,
            manifest=manifest,
            files=files,
            derived_files=derived_files,
        )

    def path(self, relative: str) -> Path:
        normalized = Path(relative).as_posix()
        if normalized not in self.files:
            raise ValueError(f"required evidence is absent from snapshot: {normalized}")
        return self.root / normalized

    def derived_path(self, relative: str) -> Path:
        normalized = Path(relative).as_posix()
        if normalized not in self.derived_files:
            raise ValueError(
                f"required derived evidence is absent from snapshot: {normalized}"
            )
        return self.root / normalized


class EvidenceIndex:
    def __init__(self, snapshot: EvidenceSnapshot):
        self.snapshot = snapshot
        self.actor_init_by_record = self._rows_by_address(
            "analysis/codebin_actor_init_records.csv", "record_address"
        )
        self.callback_signature_by_entry = self._rows_by_address(
            "analysis/codebin_actor_callback_signatures.csv", "entry"
        )
        self.callback_entry_by_entry = self._rows_by_address(
            "analysis/codebin_actor_callback_entries.csv", "entry"
        )
        inventory_rows = self._rows_by_address(
            "analysis/codebin_function_inventory.csv", "entry"
        )
        duplicate_inventory = [
            address for address, rows in inventory_rows.items() if len(rows) != 1
        ]
        if duplicate_inventory:
            raise ValueError(
                "function inventory contains duplicate native entries: "
                + ", ".join(duplicate_inventory[:8])
            )
        self.function_inventory_by_entry = {
            address: rows[0] for address, rows in inventory_rows.items()
        }
        self.callback_body_by_entry = self._optional_rows_by_address(
            "analysis/codebin_actor_callback_body_closure_tranche_162.csv",
            "entry",
        )
        self.consumer_signature_by_entry: dict[
            str, list[dict[str, str]]
        ] = defaultdict(list)
        for relative in ACTOR_CONSUMER_SIGNATURE_CATALOGS:
            if relative not in snapshot.files:
                continue
            for row in read_csv_rows(snapshot.path(relative)):
                if not row.get("entry"):
                    continue
                record = dict(row)
                record["snapshot_file"] = relative
                self.consumer_signature_by_entry[
                    normalize_address(row["entry"])
                ].append(record)

        self.actor_struct_fields_by_structure: dict[
            str, list[dict[str, str]]
        ] = defaultdict(list)
        structural_catalogs = list(ACTOR_STRUCTURAL_FIELD_CATALOGS)
        structural_catalogs.extend(
            relative
            for relative in sorted(snapshot.files)
            if relative not in structural_catalogs
            and Path(relative).name.startswith("codebin_actor_")
            and "_tranche_" in Path(relative).name
            and Path(relative).name.endswith("_fields.csv")
        )
        for priority, relative in enumerate(structural_catalogs):
            if relative not in snapshot.files:
                continue
            for row in read_csv_rows(snapshot.path(relative)):
                structure = row.get("structure", "")
                if not structure:
                    continue
                record = dict(row)
                record["snapshot_file"] = relative
                record["catalog_priority"] = str(priority)
                self.actor_struct_fields_by_structure[structure].append(record)
        self.workflow_by_entry: dict[str, list[dict[str, str]]] = defaultdict(list)
        self.workflow_by_actor: dict[str, list[dict[str, str]]] = defaultdict(list)
        self.workflow_signature_by_entry: dict[
            str, list[dict[str, str]]
        ] = defaultdict(list)
        self.workflow_action_targets_by_actor: dict[
            str, list[dict[str, str]]
        ] = defaultdict(list)
        self.workflow_struct_fields_by_structure: dict[
            str, list[dict[str, str]]
        ] = defaultdict(list)
        self.indirect_roots: list[dict[str, str]] = []
        indirect_root_addresses: set[str] = set()
        for relative in sorted(snapshot.files):
            name = Path(relative).name
            if (
                name.startswith("codebin_actor_workflow_tranche_")
                and len(name) == len("codebin_actor_workflow_tranche_000.csv")
                and name.endswith(".csv")
            ):
                for row in read_csv_rows(snapshot.root / relative):
                    if row.get("entry"):
                        record = dict(row)
                        record["snapshot_file"] = relative
                        self.workflow_by_entry[
                            normalize_address(row["entry"])
                        ].append(record)
                        if row.get("actor"):
                            self.workflow_by_actor[row["actor"]].append(record)
                continue

            suffix_indexes = (
                (
                    "_signatures.csv",
                    "entry",
                    self.workflow_signature_by_entry,
                ),
                (
                    "_action_targets.csv",
                    "owner",
                    self.workflow_action_targets_by_actor,
                ),
                (
                    "_struct_fields.csv",
                    "structure",
                    self.workflow_struct_fields_by_structure,
                ),
            )
            for suffix, key_column, index in suffix_indexes:
                prefix = "codebin_actor_workflow_tranche_"
                tranche = name[len(prefix) : -len(suffix)] if (
                    name.startswith(prefix) and name.endswith(suffix)
                ) else ""
                if len(tranche) != 3 or not tranche.isdigit():
                    continue
                for row in read_csv_rows(snapshot.root / relative):
                    key = row.get(key_column, "")
                    if not key:
                        continue
                    record = dict(row)
                    record["snapshot_file"] = relative
                    if key_column == "entry":
                        key = normalize_address(key)
                    index[key].append(record)
                break

            indirect_catalog = (
                (
                    name.startswith("codebin_indirect_ready_")
                    and "_roots_tranche_" in name
                )
                or name.startswith("codebin_actor_indirect_native_roots_tranche_")
            )
            if indirect_catalog and name.endswith(".csv") and not name.endswith(
                ("_signatures.csv", "_symbols.csv")
            ):
                tranche = name[-7:-4]
                if not tranche.isdigit():
                    continue
                signature_relative = relative[:-4] + "_signatures.csv"
                signature_rows = {
                    normalize_address(row.get("entry")): row
                    for row in read_csv_rows(snapshot.path(signature_relative))
                }
                root_rows = read_csv_rows(snapshot.root / relative)
                if len(root_rows) != len(signature_rows):
                    raise ValueError(
                        f"indirect-root tranche {tranche} has incomplete ABI evidence"
                    )
                for row in root_rows:
                    address = normalize_address(row.get("entry"))
                    signature = signature_rows.get(address)
                    if (
                        signature is None
                        or signature.get("name") != row.get("name")
                        or address in indirect_root_addresses
                    ):
                        raise ValueError(
                            f"indirect-root tranche {tranche} has ambiguous identity "
                            f"at 0x{address.upper()}"
                        )
                    indirect_root_addresses.add(address)
                    record = dict(row)
                    record.update(
                        {
                            "return_type": signature.get("return_type", ""),
                            "param_types": signature.get("param_types", ""),
                            "param_names": signature.get("param_names", ""),
                            "source": signature.get("source", ""),
                            "tranche": tranche,
                            "snapshot_file": relative,
                            "signature_snapshot_file": signature_relative,
                        }
                    )
                    self.indirect_roots.append(record)

        self.indirect_roots.sort(key=lambda row: int(row["entry"], 0))

        self.native_abi_catalog: dict[str, Any] | None = None
        self.native_functions: list[dict[str, Any]] = []
        self.native_function_by_address: dict[str, dict[str, Any]] = {}
        if "native_abi_catalog.json" in snapshot.derived_files:
            catalog = load_native_abi_catalog(
                snapshot.derived_path("native_abi_catalog.json")
            )
            if (
                catalog.get("source_snapshot_id")
                != snapshot.manifest.get("snapshot_id")
                or catalog.get("source_base_revision")
                != snapshot.manifest.get("source_base_revision")
                or catalog.get("code_bin_sha256")
                != snapshot.manifest.get("code_bin_sha256")
            ):
                raise ValueError(
                    "native ABI catalog provenance differs from its evidence snapshot"
                )
            self.indirect_roots = reconcile_indirect_root_evidence(
                catalog, self.indirect_roots
            )
            self.native_abi_catalog = catalog
            self.native_functions = list(catalog["functions"])
            self.native_function_by_address = {
                normalize_address(row["address"]): row
                for row in self.native_functions
            }

        self.room_callback_table = read_csv_rows(
            snapshot.path("analysis/codebin_room_scene_callback_table.csv")
        )
        self.room_callback_entries = self._rows_by_address(
            "analysis/codebin_room_scene_callback_entries.csv", "entry"
        )
        self.room_semantics = read_csv_rows(
            snapshot.path("analysis/codebin_room_scene_semantics.csv")
        )
        self.room_lifecycle_signatures = {
            row.get("name", ""): row
            for row in read_csv_rows(
                snapshot.path("analysis/codebin_scene_room_lifecycle_signatures.csv")
            )
        }
        self.room_lifecycle_struct_fields = read_csv_rows(
            snapshot.path("analysis/codebin_scene_room_lifecycle_struct_fields.csv")
        )
        self.object_bank_signatures = {
            row.get("name", ""): row
            for row in read_csv_rows(
                snapshot.path("analysis/codebin_object_bank_async_signatures.csv")
            )
        }

    def _rows_by_address(
        self, relative: str, column: str
    ) -> dict[str, list[dict[str, str]]]:
        result: dict[str, list[dict[str, str]]] = defaultdict(list)
        for row in read_csv_rows(self.snapshot.path(relative)):
            if row.get(column):
                result[normalize_address(row[column])].append(row)
        return result

    def _optional_rows_by_address(
        self, relative: str, column: str
    ) -> dict[str, list[dict[str, str]]]:
        if relative not in self.snapshot.files:
            return defaultdict(list)
        return self._rows_by_address(relative, column)

    def room_callback_config(self, config_index: int) -> dict[str, Any]:
        matches = [
            row
            for row in self.room_callback_table
            if int(row.get("config_index", "-1")) == config_index
        ]
        if len(matches) != 1:
            raise ValueError(
                f"scene draw config {config_index} has {len(matches)} callback table rows"
            )
        table = matches[0]
        callbacks: dict[str, Any] = {}
        for role, field in (
            ("init", "init_entry"),
            ("cleanup", "cleanup_entry"),
            ("prepare_draw", "prepare_draw_entry"),
        ):
            key = normalize_address(table[field])
            entries = self.room_callback_entries.get(key, [])
            callbacks[role] = {
                "address": f"0x{key.upper()}",
                "name": entries[0].get("new_name", "") if entries else "",
                "confidence": entries[0].get("confidence", "") if entries else "",
                "evidence_status": (
                    "callback_identity_recovered"
                    if entries
                    else "callback_identity_missing"
                ),
            }
        semantics = [
            dict(row)
            for row in self.room_semantics
            if int(row.get("scene_config", "-1")) == config_index
        ]
        return {
            "config_index": config_index,
            "config_name": table.get("config_name", ""),
            "record_address": f"0x{normalize_address(table.get('record_address')).upper()}",
            "callbacks": callbacks,
            "semantic_operations": semantics,
            "evidence_source": "immutable_zelda3drecomp_snapshot",
        }

    def object_bank_contract(self) -> dict[str, Any]:
        required = ("ResourceFile_GetSize", "Object_Spawn")
        missing = [name for name in required if name not in self.object_bank_signatures]
        if missing:
            raise ValueError(
                f"object-bank evidence is incomplete: {', '.join(missing)}"
            )
        return {
            "path_table_address": address_hex(OBJECT_TABLE),
            "path_record_stride": 0x44,
            "path_record_cached_size_offset": 0x40,
            "missing_file_semantics": (
                "ResourceFile_GetSize preserves size zero for the native expected "
                "file-not-found result; Object_Spawn skips ZAR setup when archive_size is zero"
            ),
            "unmaterialized_reference_policy": "native_zero_size_object_bank",
            "evidence": {
                name: deepcopy(self.object_bank_signatures[name]) for name in required
            },
        }

    def _consumer_signature(
        self, address: str, name: str
    ) -> dict[str, Any]:
        candidates = [
            row
            for row in self.consumer_signature_by_entry.get(address, [])
            if row.get("name") == name
        ]
        candidates.extend(
            row
            for row in self.callback_signature_by_entry.get(address, [])
            if row.get("name") == name
        )
        if candidates:
            contracts = {
                (
                    row.get("return_type", ""),
                    row.get("param_types", ""),
                    row.get("param_names", ""),
                )
                for row in candidates
            }
            if len(contracts) != 1:
                raise ValueError(
                    f"native consumer {name} has conflicting reviewed signatures"
                )
            return dict(candidates[0])

        native = self.native_function_by_address.get(address)
        if native is None or native.get("name") != name:
            return {}
        return {
            "name": name,
            "return_type": native.get("return_type", ""),
            "param_types": ";".join(native.get("parameter_types", [])),
            "param_names": ";".join(native.get("parameter_names", [])),
            "source": native.get("source", ""),
            "snapshot_file": native.get("signature_evidence_file", ""),
        }

    def _consumer_function_contract(
        self,
        address: str,
        *,
        role: str,
        depth: int,
    ) -> dict[str, Any]:
        inventory = self.function_inventory_by_entry.get(address)
        if inventory is None:
            raise ValueError(
                f"native consumer dependency 0x{address.upper()} is absent from inventory"
            )
        name = inventory.get("maintained_name", "") or inventory.get("name", "")
        if not name:
            raise ValueError(
                f"native consumer dependency 0x{address.upper()} has no identity"
            )
        body_rows = self.callback_body_by_entry.get(address, [])
        if len(body_rows) > 1:
            raise ValueError(f"native callback body evidence is ambiguous for {name}")
        body = body_rows[0] if body_rows else {}
        byte_length = int(inventory.get("size", "0"), 0)
        if body and (
            body.get("name") != name
            or int(body.get("size", "0"), 0) != byte_length
        ):
            raise ValueError(
                f"native callback body evidence differs from inventory for {name}"
            )
        signature = self._consumer_signature(address, name)
        native = self.native_function_by_address.get(address, {})
        family = (
            body.get("family", "")
            or native.get("family", "")
            or inventory.get("module", "").replace("/", "_")
            or "native_consumer"
        )
        if body:
            body_status = "reviewed_original_callback_body"
            evidence_file = (
                "analysis/codebin_actor_callback_body_closure_tranche_162.csv"
            )
        elif inventory.get("maintained_kind") == "function":
            body_status = "reviewed_original_function_body"
            evidence_file = "analysis/codebin_function_inventory.csv"
        else:
            body_status = "original_function_inventory_only"
            evidence_file = "analysis/codebin_function_inventory.csv"
        return {
            "address": f"0x{address.upper()}",
            "name": name,
            "family": family,
            "byte_length": byte_length,
            "confidence": (
                body.get("confidence", "")
                or inventory.get("maintained_confidence", "")
                or inventory.get("confidence", "")
            ),
            "return_type": signature.get(
                "return_type", inventory.get("return_type", "")
            ),
            "param_types": signature.get("param_types", ""),
            "param_names": signature.get("param_names", ""),
            "source": signature.get(
                "source", inventory.get("maintained_source_file", "")
            ),
            "evidence_file": evidence_file,
            "signature_evidence_file": signature.get("snapshot_file", ""),
            "consumer_role": role,
            "consumer_depth": depth,
            "body_status": body_status,
        }

    def actor_consumer_closure(
        self, actor_init_row: dict[str, str] | None
    ) -> dict[str, Any]:
        owner = actor_init_row.get("owner", "") if actor_init_row else ""
        if not owner:
            return {
                "status": "consumer_evidence_unavailable",
                "roots": [],
                "functions": [],
                "call_edges": [],
                "unresolved_dependencies": [],
            }

        functions: dict[str, dict[str, Any]] = {}
        roots: list[dict[str, Any]] = []
        queue: list[tuple[str, int]] = []
        for slot in ("init", "destroy", "update", "draw"):
            raw_address = actor_init_row.get(f"{slot}_entry", "")
            if not raw_address or int(raw_address, 0) == 0:
                continue
            address = normalize_address(raw_address)
            function = self._consumer_function_contract(
                address, role="native_lifecycle_consumer", depth=0
            )
            expected_name = actor_init_row.get(f"{slot}_name", "")
            if expected_name and function["name"] != expected_name:
                reviewed_aliases = {
                    row.get("new_name", "")
                    for row in self.callback_entry_by_entry.get(address, [])
                } | {
                    row.get("name", "")
                    for row in self.callback_signature_by_entry.get(address, [])
                }
                if expected_name not in reviewed_aliases:
                    raise ValueError(
                        f"ActorInit {owner} {slot} differs from function inventory"
                    )
            functions[address] = function
            roots.append(
                {
                    "slot": slot,
                    "address": function["address"],
                    "name": function["name"],
                    "actor_init_name": expected_name,
                    "body_status": function["body_status"],
                }
            )
            queue.append((address, 0))

        expanded: set[str] = set()
        edges: dict[tuple[str, str], dict[str, Any]] = {}
        unresolved: set[str] = set()
        while queue:
            caller_address, caller_depth = queue.pop(0)
            if caller_address in expanded:
                continue
            caller = functions[caller_address]
            if caller_depth > 0 and not caller["name"].startswith(f"{owner}_"):
                continue
            expanded.add(caller_address)
            inventory = self.function_inventory_by_entry[caller_address]
            callee_addresses = [
                normalize_address(value)
                for value in inventory.get("callees", "").split(";")
                if value.strip()
            ]
            if int(inventory.get("callee_count", "0"), 0) != len(
                callee_addresses
            ):
                raise ValueError(
                    f"native consumer inventory has inconsistent callees for "
                    f"{caller['name']}"
                )
            for callee_address in callee_addresses:
                callee_inventory = self.function_inventory_by_entry.get(
                    callee_address
                )
                if callee_inventory is None:
                    unresolved.add(f"0x{callee_address.upper()}")
                    continue
                callee_name = (
                    callee_inventory.get("maintained_name", "")
                    or callee_inventory.get("name", "")
                )
                local = callee_name.startswith(f"{owner}_")
                role = (
                    "actor_local_consumer_helper"
                    if local
                    else "native_service_dependency"
                )
                candidate = self._consumer_function_contract(
                    callee_address, role=role, depth=caller_depth + 1
                )
                existing = functions.get(callee_address)
                if existing is None:
                    functions[callee_address] = candidate
                elif (
                    existing["name"] != candidate["name"]
                    or existing["byte_length"] != candidate["byte_length"]
                ):
                    raise ValueError(
                        f"native consumer closure identity conflicts at "
                        f"0x{callee_address.upper()}"
                    )
                elif local and existing["consumer_role"] == "native_service_dependency":
                    existing["consumer_role"] = role
                    existing["consumer_depth"] = min(
                        existing["consumer_depth"], caller_depth + 1
                    )
                edge_key = (caller_address, callee_address)
                edges[edge_key] = {
                    "caller_address": f"0x{caller_address.upper()}",
                    "caller_name": caller["name"],
                    "callee_address": f"0x{callee_address.upper()}",
                    "callee_name": candidate["name"],
                    "relation": "direct_arm_call",
                }
                if local and callee_address not in expanded:
                    queue.append((callee_address, caller_depth + 1))

        complete_roots = sum(
            row["body_status"] == "reviewed_original_callback_body"
            for row in roots
        )
        return {
            "status": (
                "original_lifecycle_consumers_recovered"
                if roots and complete_roots == len(roots)
                else "lifecycle_consumer_inventory_partial"
                if roots
                else "consumer_evidence_unavailable"
            ),
            "roots": roots,
            "functions": sorted(
                functions.values(), key=lambda row: int(row["address"], 0)
            ),
            "call_edges": sorted(
                edges.values(),
                key=lambda row: (
                    int(row["caller_address"], 0),
                    int(row["callee_address"], 0),
                ),
            ),
            "unresolved_dependencies": sorted(unresolved),
        }

    def actor_structure_fields(
        self,
        structure: str,
        structure_size: int,
    ) -> list[dict[str, Any]]:
        scalar_widths = {
            "s8": 1,
            "u8": 1,
            "char": 1,
            "undef1": 1,
            "s16": 2,
            "u16": 2,
            "undef2": 2,
            "s32": 4,
            "u32": 4,
            "f32": 4,
            "void*": 4,
            "undef4": 4,
        }

        def is_width_only(type_name: str) -> bool:
            return type_name in {"undef1", "undef2", "undef4"}

        def scalar_width(type_name: str) -> int | None:
            if type_name.endswith("*"):
                return 4
            return scalar_widths.get(type_name)

        def is_storage_only(field: dict[str, Any]) -> bool:
            return (
                re.fullmatch(r"undef[124](?:\[\d+\])?", str(field["type"]))
                is not None
                or Path(field["evidence_file"]).name
                in {
                    "codebin_actor_tail_scalar_fields.csv",
                    "codebin_actor_ambiguous_storage_fields.csv",
                }
            )

        def is_workflow_semantic(field: dict[str, Any]) -> bool:
            name = Path(field["evidence_file"]).name
            return (
                name.startswith("codebin_actor_workflow_tranche_")
                and name.endswith("_struct_fields.csv")
            )

        def is_typed_root_contract(field: dict[str, Any]) -> bool:
            name = Path(field["evidence_file"]).name
            return (
                name.startswith("codebin_actor_typed_root_contracts_tranche_")
                and name.endswith("_fields.csv")
            )

        def is_typed_corpus_scalar(field: dict[str, Any]) -> bool:
            name = Path(field["evidence_file"]).name
            return (
                name.startswith("codebin_actor_typed_corpus_tranche_")
                and name.endswith("_fields.csv")
            )

        def record_rejected_root(
            accepted: dict[str, Any], rejected: dict[str, Any]
        ) -> None:
            accepted.setdefault("rejected_root_contracts", []).append(
                {
                    "type": rejected["type"],
                    "name": rejected["name"],
                    "evidence_file": rejected["evidence_file"],
                    "reason": "conflicts_with_actor_workflow_semantic_field",
                }
            )

        def record_covered_scalar(
            root: dict[str, Any], scalar: dict[str, Any]
        ) -> None:
            root["evidence_files"] = sorted(
                set(root["evidence_files"] + scalar["evidence_files"])
            )
            root.setdefault("covered_scalar_fields", []).append(
                {
                    "type": scalar["type"],
                    "name": scalar["name"],
                    "evidence_file": scalar["evidence_file"],
                    "reason": "scalar_access_is_inside_typed_root_contract",
                }
            )

        def is_native_prefix_refinement(
            base: dict[str, Any], concrete: dict[str, Any]
        ) -> bool:
            base_type = str(base["type"])
            concrete_type = str(concrete["type"])
            suffix = concrete_type.removeprefix(base_type)
            return (
                Path(base["evidence_file"]).name
                == "codebin_actor_typed_helper_fields.csv"
                and base_type.startswith("Oot3d")
                and concrete_type.startswith(base_type)
                and bool(suffix)
                and suffix[0].isupper()
            )

        rows = list(self.actor_struct_fields_by_structure.get(structure, []))
        rows.extend(
            {
                **row,
                "catalog_priority": "-1",
            }
            for row in self.workflow_struct_fields_by_structure.get(structure, [])
        )
        fields_by_offset: dict[int, dict[str, Any]] = {}
        for row in sorted(
            rows,
            key=lambda item: (
                int(item.get("offset", "0"), 0),
                int(item.get("catalog_priority", "999"), 0),
            ),
        ):
            row_size = int(row.get("structure_size", "0"), 0)
            offset = int(row.get("offset", "0"), 0)
            if row_size != structure_size or not 0 <= offset < structure_size:
                raise ValueError(
                    f"actor structure evidence for {structure} differs from ActorInit"
                )
            candidate = {
                "offset": offset,
                "offset_hex": f"0x{offset:04X}",
                "type": row.get("type", ""),
                "name": row.get("name", ""),
                "confidence": row.get("confidence", ""),
                "source": row.get("source", ""),
                "evidence_file": row.get("snapshot_file", ""),
                "evidence_files": [row.get("snapshot_file", "")],
            }
            existing = fields_by_offset.get(offset)
            if existing is None:
                fields_by_offset[offset] = candidate
                continue
            if existing["type"] != candidate["type"]:
                if is_workflow_semantic(existing) and is_typed_root_contract(
                    candidate
                ):
                    record_rejected_root(existing, candidate)
                    continue
                if is_workflow_semantic(candidate) and is_typed_root_contract(
                    existing
                ):
                    record_rejected_root(candidate, existing)
                    fields_by_offset[offset] = candidate
                    continue
                if is_typed_root_contract(candidate) and is_typed_corpus_scalar(
                    existing
                ):
                    record_covered_scalar(candidate, existing)
                    fields_by_offset[offset] = candidate
                    continue
                if is_typed_root_contract(existing) and is_typed_corpus_scalar(
                    candidate
                ):
                    record_covered_scalar(existing, candidate)
                    continue
                candidate_refines_existing = is_native_prefix_refinement(
                    existing, candidate
                )
                existing_refines_candidate = is_native_prefix_refinement(
                    candidate, existing
                )
                if candidate_refines_existing or existing_refines_candidate:
                    if candidate_refines_existing:
                        candidate["evidence_files"] = sorted(
                            set(
                                existing["evidence_files"]
                                + candidate["evidence_files"]
                            )
                        )
                        fields_by_offset[offset] = candidate
                    else:
                        existing["evidence_files"] = sorted(
                            set(
                                existing["evidence_files"]
                                + candidate["evidence_files"]
                            )
                        )
                    continue
                existing_width = scalar_width(existing["type"])
                candidate_width = scalar_width(candidate["type"])
                concrete_over_width = is_width_only(
                    existing["type"]
                ) ^ is_width_only(candidate["type"])
                semantic_over_storage = is_storage_only(existing) ^ is_storage_only(
                    candidate
                )
                if (
                    not semantic_over_storage
                    and (
                        existing_width is None
                        or candidate_width is None
                        or existing_width != candidate_width
                        or not concrete_over_width
                    )
                ):
                    raise ValueError(
                        f"actor structure {structure} has conflicting exact fields at "
                        f"0x{offset:04X}: {existing['type']} vs {candidate['type']}"
                    )
                if is_width_only(existing["type"]) or (
                    is_storage_only(existing) and not is_storage_only(candidate)
                ):
                    candidate["evidence_files"] = sorted(
                        set(
                            existing["evidence_files"]
                            + candidate["evidence_files"]
                        )
                    )
                    fields_by_offset[offset] = candidate
                    continue
            existing["evidence_files"] = sorted(
                set(existing["evidence_files"] + candidate["evidence_files"])
            )
        return list(fields_by_offset.values())

    def actor_behavior_graph(
        self, actor_init_row: dict[str, str] | None
    ) -> dict[str, Any]:
        owner = actor_init_row.get("owner", "") if actor_init_row else ""
        structure = (
            actor_init_row.get("structure_name", "") if actor_init_row else ""
        )
        workflow_rows = list(self.workflow_by_actor.get(owner, []))
        native_functions = self.native_behavior_functions(owner, structure)
        consumer = self.actor_consumer_closure(actor_init_row)
        if not owner or not structure or (
            not workflow_rows
            and not native_functions
            and not consumer["functions"]
        ):
            return {
                "status": "workflow_graph_unavailable",
                "owner": owner,
                "structure": structure,
                "structure_size": None,
                "function_count": 0,
                "action_transition_count": 0,
                "structure_field_count": 0,
                "indirect_root_count": 0,
                "native_abi_function_count": 0,
                "consumer_status": "consumer_evidence_unavailable",
                "consumer_root_count": 0,
                "consumer_function_count": 0,
                "consumer_call_edge_count": 0,
                "actor_local_consumer_function_count": 0,
                "native_service_dependency_count": 0,
                "initial_action_candidates": [],
                "functions": [],
                "action_transitions": [],
                "structure_fields": [],
                "consumer_roots": [],
                "consumer_call_edges": [],
                "unresolved_consumer_dependencies": [],
                "consumer_runtime_binding_status": "consumer_evidence_not_available",
                "runtime_binding_status": "behavior_evidence_not_available",
            }

        functions_by_address: dict[str, dict[str, Any]] = {}
        address_by_name: dict[str, str] = {}

        def add_function(function: dict[str, Any]) -> None:
            address = normalize_address(function["address"])
            name = function.get("name", "")
            if not name:
                raise ValueError(
                    f"actor behavior {owner} contains an unnamed native function"
                )
            same_address = functions_by_address.get(address)
            same_name_address = address_by_name.get(name)
            if same_address is None and same_name_address is None:
                functions_by_address[address] = dict(function)
                address_by_name[name] = address
                return
            if (
                same_address is None
                or same_address.get("name") != name
                or same_name_address != address
            ):
                raise ValueError(
                    f"actor behavior {owner} contains a duplicate function identity"
                )
            for key in (
                "consumer_role",
                "consumer_depth",
                "body_status",
            ):
                value = function.get(key)
                if value not in (None, ""):
                    same_address[key] = value
            if (
                not same_address.get("signature_evidence_file")
                and function.get("signature_evidence_file")
            ):
                same_address["signature_evidence_file"] = function[
                    "signature_evidence_file"
                ]

        for row in sorted(
            workflow_rows, key=lambda item: int(item["entry"], 0)
        ):
            address = normalize_address(row["entry"])
            name = row.get("name", "")
            signature_rows = self.workflow_signature_by_entry.get(address, [])
            matching_signatures = [
                signature
                for signature in signature_rows
                if signature.get("name") == name
            ]
            if len(matching_signatures) > 1:
                raise ValueError(
                    f"actor workflow {owner} has ambiguous signature evidence for {name}"
                )
            signature = matching_signatures[0] if matching_signatures else {}
            add_function(
                {
                    "address": f"0x{address.upper()}",
                    "name": name,
                    "family": row.get("family", ""),
                    "byte_length": int(row.get("size", "0"), 0),
                    "confidence": row.get("confidence", ""),
                    "return_type": signature.get("return_type", ""),
                    "param_types": signature.get("param_types", ""),
                    "param_names": signature.get("param_names", ""),
                    "source": signature.get("source", row.get("snapshot_file", "")),
                    "evidence_file": row.get("snapshot_file", ""),
                }
            )

        for function in native_functions:
            add_function(function)
        for function in consumer["functions"]:
            add_function(function)

        functions = sorted(
            functions_by_address.values(), key=lambda row: int(row["address"], 0)
        )
        function_names = set(address_by_name)
        function_addresses = set(functions_by_address)

        transitions: list[dict[str, Any]] = []
        initial_action_candidates: set[str] = set()
        for row in sorted(
            self.workflow_action_targets_by_actor.get(owner, []),
            key=lambda item: (
                int(item.get("literal_cell", "0"), 0),
                int(item.get("value", "0"), 0),
            ),
        ):
            source_name = row.get("source_function", "")
            target_name = row.get("target_name", "")
            target_address = normalize_address(row.get("value"))
            if source_name not in function_names:
                raise ValueError(
                    f"actor workflow {owner} transition source is not in its function graph: "
                    f"{source_name}"
                )
            if row.get("status") == "exact_function" and (
                target_name not in function_names or target_address not in function_addresses
            ):
                raise ValueError(
                    f"actor workflow {owner} transition target is not in its function graph: "
                    f"{target_name}"
                )
            if source_name == f"{owner}_Init":
                initial_action_candidates.add(target_name)
            transitions.append(
                {
                    "source_function": source_name,
                    "literal_address": (
                        f"0x{normalize_address(row.get('literal_cell')).upper()}"
                    ),
                    "target_address": f"0x{target_address.upper()}",
                    "target_name": target_name,
                    "evidence": row.get("evidence", ""),
                    "status": row.get("status", ""),
                    "evidence_file": row.get("snapshot_file", ""),
                }
            )

        structure_size = (
            int(actor_init_row.get("instance_size", "0"), 0)
            if actor_init_row and actor_init_row.get("instance_size")
            else 0
        )
        if structure_size <= 0:
            raise ValueError(f"actor behavior {owner} has no bounded instance size")
        fields = self.actor_structure_fields(structure, structure_size)
        consumer_function_count = sum(
            bool(function.get("consumer_role")) for function in functions
        )
        actor_local_consumer_function_count = sum(
            function.get("consumer_role") == "actor_local_consumer_helper"
            for function in functions
        )
        native_service_dependency_count = sum(
            function.get("consumer_role") == "native_service_dependency"
            for function in functions
        )
        native_abi_function_count = sum(
            bool(function.get("closure_kind")) for function in functions
        )

        return {
            "status": (
                "workflow_graph_recovered"
                if workflow_rows
                else "native_consumer_graph_recovered"
                if consumer["roots"]
                else "native_function_set_recovered"
            ),
            "owner": owner,
            "structure": structure,
            "structure_size": structure_size,
            "function_count": len(functions),
            "action_transition_count": len(transitions),
            "structure_field_count": len(fields),
            "indirect_root_count": sum(
                function.get("closure_kind") == "indirect_root"
                for function in functions
            ),
            "native_abi_function_count": native_abi_function_count,
            "consumer_status": consumer["status"],
            "consumer_root_count": len(consumer["roots"]),
            "consumer_function_count": consumer_function_count,
            "consumer_call_edge_count": len(consumer["call_edges"]),
            "actor_local_consumer_function_count": (
                actor_local_consumer_function_count
            ),
            "native_service_dependency_count": native_service_dependency_count,
            "initial_action_candidates": sorted(initial_action_candidates),
            "functions": functions,
            "action_transitions": transitions,
            "structure_fields": fields,
            "consumer_roots": consumer["roots"],
            "consumer_call_edges": consumer["call_edges"],
            "unresolved_consumer_dependencies": consumer[
                "unresolved_dependencies"
            ],
            "consumer_runtime_binding_status": (
                "consumer_must_lower_original_bodies_and_bind_native_services"
                if consumer["roots"]
                else "consumer_evidence_not_available"
            ),
            "runtime_binding_status": "consumer_must_bind_native_behavior_graph",
        }

    def indirect_behavior_functions(self, structure: str) -> list[dict[str, Any]]:
        if not structure:
            return []
        owner_pointer = f"{structure}*"
        expected_family_prefix = (
            "player_" if structure == "Oot3dPlayer" else "actor_"
        )
        functions: list[dict[str, Any]] = []
        for row in self.indirect_roots:
            parameter_types = [
                value.strip()
                for value in row.get("param_types", "").split(";")
                if value.strip()
            ]
            if (
                not row.get("family", "").startswith(expected_family_prefix)
                or owner_pointer not in parameter_types
            ):
                continue
            functions.append(
                {
                    "address": f"0x{normalize_address(row['entry']).upper()}",
                    "name": row.get("name", ""),
                    "family": row.get("family", ""),
                    "byte_length": int(row.get("size", "0"), 0),
                    "confidence": row.get("confidence", ""),
                    "return_type": row.get("return_type", ""),
                    "param_types": row.get("param_types", ""),
                    "param_names": row.get("param_names", ""),
                    "source": row.get("source", ""),
                    "evidence_file": row.get("snapshot_file", ""),
                    "signature_evidence_file": row.get(
                        "signature_snapshot_file", ""
                    ),
                    "discovery_kind": "zero_direct_caller_native_root",
                    "closure_kind": "indirect_root",
                    "owner_resolution": "exact_concrete_instance_pointer_in_native_abi",
                    "source_tranche": int(row.get("tranche", "0")),
                }
            )
        return functions

    @staticmethod
    def _native_function_contract(
        row: dict[str, Any], owner_resolution: str
    ) -> dict[str, Any]:
        parameter_types = row.get("parameter_types", [])
        parameter_names = row.get("parameter_names", [])
        contract = {
            "address": f"0x{normalize_address(row.get('address')).upper()}",
            "name": row.get("name", ""),
            "family": row.get("family", ""),
            "byte_length": row.get("byte_length", 0),
            "confidence": row.get("confidence", ""),
            "return_type": row.get("return_type", ""),
            "param_types": ";".join(parameter_types),
            "param_names": ";".join(parameter_names),
            "source": row.get("source", ""),
            "evidence_file": row.get("evidence_file", ""),
            "signature_evidence_file": row.get("signature_evidence_file", ""),
            "closure_kind": row.get("closure_kind", ""),
            "owner_resolution": owner_resolution,
            "source_tranche": row.get("source_tranche", 0),
        }
        if row.get("closure_kind") == "indirect_root":
            contract["discovery_kind"] = "zero_direct_caller_native_root"
        return contract

    def native_behavior_functions(
        self, owner: str, structure: str
    ) -> list[dict[str, Any]]:
        if not owner or not structure:
            return []
        if not self.native_functions:
            return self.indirect_behavior_functions(structure)

        owner_pointer = f"{structure}*"
        expected_family_prefix = (
            "player_" if structure == "Oot3dPlayer" else "actor_"
        )
        result: list[dict[str, Any]] = []
        seen_addresses: set[str] = set()
        for row in self.native_functions:
            family = str(row.get("family", ""))
            parameter_types = row.get("parameter_types", [])
            if not isinstance(parameter_types, list):
                raise ValueError("native ABI catalog parameter types are not an array")
            owner_resolution = ""
            if family.startswith(expected_family_prefix) and owner_pointer in parameter_types:
                owner_resolution = "exact_concrete_instance_pointer_in_native_abi"
            elif (
                row.get("closure_kind") == "maintained_abi"
                and family.startswith("actor_")
                and str(row.get("name", "")).startswith(f"{owner}_")
                and "Oot3dPlayState*" in parameter_types
                and "void*" in parameter_types
            ):
                owner_resolution = (
                    "exact_maintained_actor_symbol_prefix_and_native_context_abi"
                )
            if not owner_resolution:
                continue
            address = normalize_address(row.get("address"))
            if address in seen_addresses:
                raise ValueError(
                    f"native ABI catalog has duplicate profile binding at 0x{address.upper()}"
                )
            seen_addresses.add(address)
            result.append(self._native_function_contract(row, owner_resolution))
        return result

    def native_abi_catalog_summary(
        self, profile_bound_function_count: int
    ) -> dict[str, Any] | None:
        if self.native_abi_catalog is None:
            return None
        catalog = self.native_abi_catalog
        return {
            "format": catalog["format"],
            "status": catalog["status"],
            "source_snapshot_id": catalog["source_snapshot_id"],
            "source_base_revision": catalog["source_base_revision"],
            "code_bin_sha256": catalog["code_bin_sha256"],
            "payload_sha256": catalog["payload_sha256"],
            "function_count": catalog["function_count"],
            "native_pointer_function_count": catalog[
                "native_pointer_function_count"
            ],
            "closure_kind_counts": deepcopy(catalog["closure_kind_counts"]),
            "profile_bound_function_count": profile_bound_function_count,
            "ownership_policy": catalog["ownership_policy"],
            "runtime_binding_status": "catalog_mounted_once_profile_bindings_in_rcu",
        }

    def indirect_root_catalog(self, profile_bound_root_count: int) -> dict[str, Any]:
        family_counts = Counter(row.get("family", "") for row in self.indirect_roots)
        actor_candidate_count = sum(
            family.startswith(("actor_", "player_"))
            for family in (row.get("family", "") for row in self.indirect_roots)
        )
        source_catalogs = sorted(
            {row.get("snapshot_file", "") for row in self.indirect_roots}
            | {row.get("signature_snapshot_file", "") for row in self.indirect_roots}
        )
        return {
            "format": "oot3d_indirect_native_root_catalog_v1",
            "status": (
                "reviewed_native_roots_indexed"
                if self.indirect_roots
                else "native_root_evidence_unavailable"
            ),
            "source_snapshot_id": self.snapshot.manifest.get("snapshot_id"),
            "root_count": len(self.indirect_roots),
            "actor_candidate_count": actor_candidate_count,
            "profile_bound_root_count": profile_bound_root_count,
            "unbound_actor_candidate_count": (
                actor_candidate_count - profile_bound_root_count
            ),
            "family_counts": dict(sorted(family_counts.items())),
            "source_catalogs": [value for value in source_catalogs if value],
            "ownership_policy": (
                "bind only when the reviewed native ABI contains the exact concrete "
                "ActorInit instance pointer and the semantic family matches"
            ),
            "runtime_binding_status": "profile_bound_roots_require_consumer_binding",
        }

    def room_lifecycle_contract(self, transition_actor_count: int) -> dict[str, Any]:
        function_roles = {
            "initialize": "Room_Init",
            "request": "Room_RequestNewRoom",
            "process_request": "Room_ProcessRoomRequest",
            "destroy": "Room_Destroy",
            "cleanup_room_actors": "Actor_CleanupRoomActors",
            "spawn_transition_actors": "Actor_SpawnTransitionActors",
            "queue_resource_cleanup": "Room_QueueResourceCleanup",
            "process_resource_cleanup": "Room_ProcessResourceCleanupQueue",
        }
        missing = [
            name
            for name in function_roles.values()
            if name not in self.room_lifecycle_signatures
        ]
        if missing:
            raise ValueError(
                f"room-lifecycle evidence is incomplete: {', '.join(missing)}"
            )

        required_fields = {
            ("Oot3dRoomContext", "current_room"),
            ("Oot3dRoomContext", "previous_room"),
            ("Oot3dRoomContext", "load_state"),
            ("Oot3dTransitionActorEntry", "front_room"),
            ("Oot3dTransitionActorEntry", "back_room"),
            ("Oot3dTransitionActorEntry", "actor_id"),
            ("Oot3dTransitionActorEntry", "params"),
            ("Oot3dRoomCleanupQueue", "delay"),
        }
        fields = {
            (row.get("structure", ""), row.get("name", "")): row
            for row in self.room_lifecycle_struct_fields
        }
        missing_fields = sorted(required_fields - fields.keys())
        if missing_fields:
            missing_text = ", ".join(
                f"{structure}.{name}" for structure, name in missing_fields
            )
            raise ValueError(
                f"room-lifecycle structure evidence is incomplete: {missing_text}"
            )

        functions: dict[str, Any] = {}
        for role, name in function_roles.items():
            row = self.room_lifecycle_signatures[name]
            functions[role] = {
                "address": f"0x{normalize_address(row.get('entry')).upper()}",
                "name": name,
                "return_type": row.get("return_type", ""),
                "param_types": row.get("param_types", ""),
                "confidence": row.get("confidence", ""),
                "source": row.get("source", ""),
                "notes": row.get("notes", ""),
            }

        return {
            "format": "oot3d_room_lifecycle_contract_v1",
            "status": "workflow_semantics_recovered",
            "evidence_source": "immutable_zelda3drecomp_snapshot",
            "source_snapshot_id": self.snapshot.manifest.get("snapshot_id"),
            "functions": functions,
            "state_model": {
                "idle": 0,
                "loading": 1,
                "terminal": 2,
                "max_resident_room_count": 2,
                "initial_buffer_count": 3 if transition_actor_count else 1,
                "buffer_count_without_transition_actors": 1,
                "buffer_count_with_transition_actors": 3,
            },
            "actor_residency": {
                "room_actor_retention_policy": ("negative_room_or_current_or_previous"),
                "transition_actor_residency_policy": (
                    "front_or_back_matches_current_or_previous"
                ),
                "transition_actor_id_spawn_mask": 0x1FFF,
                "transition_actor_params_index_stride": 0x400,
                "transition_actor_spawn_marker": "negate_source_actor_id",
            },
            "resource_residency": {
                "previous_room_cleanup_before_next_request": True,
                "cleanup_delay_ticks": 1,
                "room_commands_install_on_request_completion": True,
            },
            "structure_evidence": [
                deepcopy(fields[key]) for key in sorted(required_fields)
            ],
        }


def catalog_source_container(record: dict[str, Any]) -> str | None:
    identity = record.get("source_identity")
    if not isinstance(identity, str) or ":" not in identity:
        return None
    remainder = identity.split(":", 1)[1]
    return remainder.split("!", 1)[0].replace("\\", "/").lower()


def catalog_record_summary(record: dict[str, Any]) -> dict[str, Any]:
    return {
        "asset_id": record.get("asset_id"),
        "family": record.get("family"),
        "source_identity": record.get("source_identity"),
        "canonical_resources": list(record.get("canonical_resources", [])),
        "dependencies": list(record.get("dependencies", [])),
        "required_engine_capabilities": list(
            record.get("required_engine_capabilities", [])
        ),
        "support_tier": record.get("support_tier"),
        "runtime_state": record.get("runtime_state"),
        "provenance_manifest": record.get("provenance_manifest"),
        "ownership": deepcopy(record.get("ownership", {})),
    }


def archive_descriptor(path: Path, logical_path: str) -> dict[str, Any]:
    descriptor = logical_file(path, logical_path)
    archive = ZarArchive.from_path(path)
    type_counts = Counter(member.type_name for member in archive.files)
    descriptor.update(
        {
            "format": "zar",
            "member_count": len(archive.files),
            "type_counts": dict(sorted(type_counts.items())),
            "members": [
                {
                    "index": member.index,
                    "name": member.name,
                    "type": member.type_name,
                    "type_local_index": member.type_local_index,
                    "offset": member.offset,
                    "byte_length": member.size,
                }
                for member in archive.files
            ],
        }
    )
    return descriptor


def selected_command(setup: dict[str, Any], command_id: int) -> dict[str, Any] | None:
    for command in setup.get("commands", []):
        if command.get("command_id") == command_id:
            return command
    return None


def selected_entries(command: dict[str, Any] | None) -> list[dict[str, Any]]:
    if not command:
        return []
    decoded = command.get("decoded", {})
    selected = decoded.get("selected_candidate")
    if not isinstance(selected, dict):
        return []
    entries = selected.get("entries", [])
    return [dict(entry) for entry in entries if isinstance(entry, dict)]


def trim_setup(setup: dict[str, Any]) -> dict[str, Any]:
    result = deepcopy(setup)
    for command in result.get("commands", []):
        decoded = command.get("decoded")
        if isinstance(decoded, dict) and "candidates" in decoded:
            decoded.pop("candidates", None)
    return result


def trim_room_commands(commands: list[dict[str, Any]]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for command in commands:
        decoded = deepcopy(command.get("decoded", {}))
        if isinstance(decoded, dict):
            # Object, actor and light payloads remain in their dedicated room
            # contracts or in the original packaged ZSI. The command contract
            # keeps offsets/counts and scalar state without duplicating payloads.
            decoded.pop("entries", None)
            decoded.pop("records", None)
        result.append(
            {
                key: deepcopy(command[key])
                for key in (
                    "offset",
                    "offset_hex",
                    "command_id",
                    "command_id_hex",
                    "command_name",
                    "parameter",
                    "command_word",
                    "command_word_hex",
                    "argument",
                    "argument_hex",
                )
                if key in command
            }
        )
        result[-1]["decoded"] = decoded
    return result


def room_unit(
    room: dict[str, Any],
    source_path: Path,
    *,
    initial_room_index: int | None,
    setup_index: int,
) -> dict[str, Any]:
    setup_matches = [
        setup for setup in room.get("setups", []) if setup.get("index") == setup_index
    ]
    if len(setup_matches) != 1:
        raise ValueError(
            f"native room {room.get('room_index')} setup {setup_index} has "
            f"{len(setup_matches)} records"
        )
    room_setup = setup_matches[0]
    actor_payload = room_setup.get("actor_list", {})
    selected = actor_payload.get("selected_candidate")
    if not isinstance(selected, dict):
        selected = {}
    object_payload = room_setup.get("object_list", {})
    room_index = room.get("room_index")
    return {
        "room_index": room_index,
        "setup_index": setup_index,
        "initially_active": room_index == initial_room_index,
        "source": logical_file(source_path, f"scene/{room['room_path']}"),
        "commands": trim_room_commands(room_setup.get("commands", [])),
        "mesh_resources": deepcopy(room.get("embedded_cmbs", [])),
        "object_list": {
            "status": object_payload.get("status", "missing"),
            "source_offset": object_payload.get("source_offset"),
            "entry_size": object_payload.get("entry_size"),
            "entry_count": object_payload.get("entry_count", 0),
            "entries": deepcopy(object_payload.get("entries", [])),
            "unknown_entries": deepcopy(object_payload.get("unknown_entries", [])),
            "raw_entries": deepcopy(object_payload.get("raw_entries", [])),
        },
        "actor_list": {
            "status": actor_payload.get("status", "missing"),
            "confidence": selected.get("confidence", ""),
            "source_offset": selected.get("start_offset"),
            "entry_size": selected.get("entry_size"),
            "entry_count": selected.get("entry_count", 0),
            "entries": deepcopy(selected.get("entries", [])),
            "candidate_count": actor_payload.get("candidate_count", 0),
            "evidence": deepcopy(actor_payload.get("evidence", [])),
        },
    }


def callback_contract(
    slot: str,
    address: int,
    init_row: dict[str, str] | None,
    evidence: EvidenceIndex,
) -> dict[str, Any]:
    if address == 0:
        return {
            "slot": slot,
            "address": "0x00000000",
            "name": "",
            "evidence_status": "not_present_in_actor_profile",
            "runtime_binding_status": "not_required",
        }
    key = normalize_address(address_hex(address))
    name = ""
    if init_row:
        name = init_row.get(f"{slot}_name", "")
    signature_rows = evidence.callback_signature_by_entry.get(key, [])
    entry_rows = evidence.callback_entry_by_entry.get(key, [])
    workflow_rows = evidence.workflow_by_entry.get(key, [])
    if workflow_rows:
        status = "workflow_semantics_recovered"
    elif signature_rows:
        status = "typed_signature_recovered"
    elif entry_rows or name:
        status = "actor_init_identity_recovered"
    else:
        status = "callback_identity_unresolved"
    return {
        "slot": slot,
        "address": f"0x{key.upper()}",
        "name": name or (signature_rows[0].get("name", "") if signature_rows else ""),
        "evidence_status": status,
        "signature": deepcopy(signature_rows[0]) if signature_rows else None,
        "workflow_evidence": deepcopy(workflow_rows),
        "runtime_binding_status": "consumer_must_bind_native_callback",
    }


def actor_profile_contract(
    view: BinaryView,
    actor_id: int,
    actor_name: str,
    evidence: EvidenceIndex,
) -> tuple[dict[str, Any] | None, dict[str, Any] | None]:
    try:
        profile = actor_profile(view, actor_id)
        object_rom_path = object_path(view, profile["object_id"])
    except Exception as exc:
        return None, {
            "kind": "actor_profile",
            "key": f"actor:0x{actor_id:04X}",
            "severity": "composition",
            "reason": str(exc),
        }

    record_key = normalize_address(address_hex(profile["profile_address"]))
    init_rows = evidence.actor_init_by_record.get(record_key, [])
    init_row = init_rows[0] if len(init_rows) == 1 else None
    callbacks = {
        slot: callback_contract(slot, profile[f"{slot}_address"], init_row, evidence)
        for slot in ("init", "destroy", "update", "draw")
    }
    behavior_graph = evidence.actor_behavior_graph(init_row)
    if (
        behavior_graph["structure_size"] is not None
        and behavior_graph["structure_size"] != profile["instance_size"]
    ):
        raise ValueError(
            f"actor workflow {behavior_graph['owner']} structure size differs from "
            "its native ActorInit profile"
        )
    callback_states = Counter(
        callback["evidence_status"]
        for callback in callbacks.values()
        if callback["evidence_status"] != "not_present_in_actor_profile"
    )
    contract = {
        "profile_key": f"actor:0x{actor_id:04X}",
        "actor_id": actor_id,
        "actor_id_hex": f"0x{actor_id:04X}",
        "actor_name": actor_name,
        "profile_address": address_hex(profile["profile_address"]),
        "overlay_table_address": address_hex(profile["overlay_table_address"]),
        "overlay_entry_address": address_hex(profile["overlay_entry_address"]),
        "category": profile["category"],
        "flags": profile["flags"],
        "flags_hex": address_hex(profile["flags"]),
        "instance_size": profile["instance_size"],
        "object_id": profile["object_id"],
        "object_id_hex": f"0x{profile['object_id']:04X}",
        "object_rom_path": object_rom_path.replace("\\", "/"),
        "callbacks": callbacks,
        "callback_evidence_counts": dict(sorted(callback_states.items())),
        "behavior_graph": behavior_graph,
        "actor_init_evidence": deepcopy(init_row),
        "actor_init_evidence_status": (
            "exact_snapshot_record"
            if init_row
            else "snapshot_record_missing_or_ambiguous"
        ),
        "runtime_binding_status": "consumer_must_bind_native_profile",
    }
    gap = None
    if init_row is None:
        gap = {
            "kind": "actor_profile_evidence",
            "key": contract["profile_key"],
            "severity": "behavior",
            "reason": (
                "ActorInit profile decoded from code.bin but immutable evidence row "
                "is missing or ambiguous"
            ),
        }
    return contract, gap


def actor_instances(
    player: dict[str, Any] | None,
    rooms: list[dict[str, Any]],
    setup: dict[str, Any],
    *,
    transition_actor_id_spawn_mask: int,
) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    if player:
        result.append(
            {
                "instance_key": "player-entry",
                "source_kind": "scene_spawn",
                "room_index": player.get("room_index"),
                "entry": deepcopy(player["entry"]),
            }
        )
    for room in rooms:
        for entry in room["actor_list"]["entries"]:
            result.append(
                {
                    "instance_key": f"room-{room['room_index']}-actor-{entry['index']}",
                    "source_kind": "room_actor_list",
                    "room_index": room["room_index"],
                    "entry": deepcopy(entry),
                }
            )
    transition = selected_command(setup, 0x0E)
    if transition:
        for entry in transition.get("decoded", {}).get("entries", []):
            result.append(
                {
                    "instance_key": f"transition-actor-{entry['index']}",
                    "source_kind": "scene_transition_actor_list",
                    "room_index": None,
                    "entry": deepcopy(entry),
                }
            )
    for instance in result:
        actor_id = int(instance["entry"].get("actor_id", -1))
        profile_actor_id = (
            actor_id & transition_actor_id_spawn_mask
            if instance["source_kind"] == "scene_transition_actor_list"
            else actor_id
        )
        instance["profile_actor_id"] = profile_actor_id
        instance["profile_key"] = (
            f"actor:0x{profile_actor_id:04X}"
            if 0 <= profile_actor_id <= 0xFFFF
            else None
        )
    return result


def compile_room_compilation_unit(
    *,
    scene_root: Path,
    code_bin: Path,
    semantic_route_catalog_path: Path,
    asset_catalog_path: Path,
    scene_resource_table_path: Path,
    room_callback_contracts_path: Path,
    evidence_snapshot_root: Path,
    route_id: str,
    setup_index: int | None = None,
) -> dict[str, Any]:
    scene_root = scene_root.resolve()
    code_bin = code_bin.resolve()
    route_catalog = read_json(semantic_route_catalog_path)
    asset_catalog = read_json(asset_catalog_path)
    scene_resource_table = read_json(scene_resource_table_path)
    snapshot = EvidenceSnapshot.load(evidence_snapshot_root)
    evidence = EvidenceIndex(snapshot)

    code_bin_sha256 = sha256_file(code_bin)
    if code_bin_sha256 != snapshot.manifest.get("code_bin_sha256"):
        raise ValueError(
            "code.bin does not match the immutable Zelda3drecomp evidence snapshot"
        )
    room_callback_runtime_contracts = load_room_callback_runtime_contracts(
        room_callback_contracts_path, code_bin
    )

    if route_catalog.get("format") != SEMANTIC_ROUTE_FORMAT:
        raise ValueError("unsupported semantic route catalog format")
    if asset_catalog.get("format") != ASSET_CATALOG_FORMAT:
        raise ValueError("unsupported asset catalog format")
    routes = [
        row
        for row in route_catalog.get("records", [])
        if row.get("route_id") == route_id
    ]
    if len(routes) != 1:
        raise ValueError(f"semantic route {route_id!r} has {len(routes)} matches")
    route = routes[0]
    if route.get("status") != "resolved":
        raise ValueError(f"semantic route {route_id!r} is not resolved")
    native = route.get("native", {})
    scene_stem = native.get("scene_stem")
    scene_path_name = native.get("scene_path")
    if not isinstance(scene_stem, str) or not isinstance(scene_path_name, str):
        raise ValueError("semantic route lacks native scene identity")

    allowed_setups = [int(value) for value in native.get("setup_indices", [])]
    if setup_index is None:
        if len(allowed_setups) != 1:
            raise ValueError(
                "route has multiple native setups; select --setup-index explicitly"
            )
        setup_index = allowed_setups[0]
    elif allowed_setups and setup_index not in allowed_setups:
        raise ValueError(f"setup {setup_index} is outside the semantic route")

    native_index = export_zsi_scene_index(
        scene_root,
        scene_stems=[scene_stem],
        sample_limit=1024,
        full_entries=True,
        include_room_mesh_summaries=True,
    )
    scene_matches = [
        row
        for row in native_index.get("records", [])
        if row.get("scene_path") == scene_path_name
    ]
    if len(scene_matches) != 1:
        raise ValueError(
            f"native scene {scene_path_name!r} has {len(scene_matches)} index records"
        )
    scene = scene_matches[0]
    setup_matches = [
        row for row in scene.get("setups", []) if row.get("index") == setup_index
    ]
    if len(setup_matches) != 1:
        raise ValueError(f"native setup {setup_index} has {len(setup_matches)} records")
    setup = trim_setup(setup_matches[0])

    room_bindings = native.get("room_bindings", [])
    if room_bindings:
        requested_room_indices = [
            int(row["native_room_index"]) for row in room_bindings
        ]
    else:
        room_command = selected_command(setup, 0x04)
        requested_room_indices = [
            int(row["room_index"])
            for row in (room_command or {})
            .get("decoded", {})
            .get("room_references", [])
            if isinstance(row.get("room_index"), int)
        ]
    if len(requested_room_indices) != len(set(requested_room_indices)):
        raise ValueError("native room bindings contain duplicate room indices")

    entrance_index = native.get("local_entrance_index")
    entrance_entries = selected_entries(selected_command(setup, 0x06))
    entrance = None
    if isinstance(entrance_index, int):
        entrance = next(
            (
                entry
                for entry in entrance_entries
                if entry.get("index") == entrance_index
            ),
            None,
        )
        if entrance is None:
            raise ValueError(
                f"local native entrance {entrance_index} is absent from setup {setup_index}"
            )
    spawn_entries = selected_entries(selected_command(setup, 0x00))
    player = None
    initial_room_index = None
    if entrance is not None:
        spawn_index = int(entrance["spawn"])
        spawn = next(
            (entry for entry in spawn_entries if entry.get("index") == spawn_index),
            None,
        )
        if spawn is None:
            raise ValueError(
                f"native spawn {spawn_index} is absent from setup {setup_index}"
            )
        initial_room_index = int(entrance["room"])
        player = {
            "global_entrance_index": native.get("global_entrance_index"),
            "local_entrance_index": entrance_index,
            "entrance_entry": deepcopy(entrance),
            "spawn_index": spawn_index,
            "room_index": initial_room_index,
            "entry": deepcopy(spawn),
            "spatial_authority": "oot3d_native_zsi",
            "camera_authority": native.get("camera_source", "oot3d_native"),
            "player_state_authority": native.get(
                "player_entry_state_source", "oot3d_native"
            ),
        }

    room_by_index = {row.get("room_index"): row for row in scene.get("rooms", [])}
    rooms: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    for room_index in requested_room_indices:
        source = room_by_index.get(room_index)
        if source is None:
            unresolved.append(
                {
                    "kind": "room_source",
                    "key": f"room:{room_index}",
                    "severity": "composition",
                    "reason": "route-bound native room is absent from the ZSI scene index",
                }
            )
            continue
        room_path = scene_root / str(source["room_path"])
        compiled = room_unit(
            source,
            room_path,
            initial_room_index=initial_room_index,
            setup_index=setup_index,
        )
        if not str(compiled["actor_list"]["status"]).endswith("identified"):
            unresolved.append(
                {
                    "kind": "room_actor_list",
                    "key": f"room:{room_index}",
                    "severity": "composition",
                    "reason": str(compiled["actor_list"]["status"]),
                }
            )
        rooms.append(compiled)

    transition_command = selected_command(setup, 0x0E)
    transition_actor_count = len(
        (transition_command or {}).get("decoded", {}).get("entries", [])
    )
    room_lifecycle = evidence.room_lifecycle_contract(transition_actor_count)
    instances = actor_instances(
        player,
        rooms,
        setup,
        transition_actor_id_spawn_mask=int(
            room_lifecycle["actor_residency"]["transition_actor_id_spawn_mask"]
        ),
    )
    actor_names: dict[int, str] = {}
    for instance in instances:
        entry = instance["entry"]
        actor_id = int(instance.get("profile_actor_id", entry.get("actor_id", -1)))
        if 0 <= actor_id <= 0xFFFF:
            actor_names.setdefault(
                actor_id, str(entry.get("actor_name", f"ACTOR_0x{actor_id:04X}"))
            )

    code_bytes = code_bin.read_bytes()
    code_view = BinaryView(code_bytes, "exefs/code.bin")
    profiles: list[dict[str, Any]] = []
    for actor_id in sorted(actor_names):
        contract, gap = actor_profile_contract(
            code_view, actor_id, actor_names[actor_id], evidence
        )
        if contract is not None:
            profiles.append(contract)
        if gap is not None:
            unresolved.append(gap)

    profile_by_key = {row["profile_key"]: row for row in profiles}
    for instance in instances:
        if instance["profile_key"] not in profile_by_key:
            unresolved.append(
                {
                    "kind": "actor_instance_profile",
                    "key": instance["instance_key"],
                    "severity": "composition",
                    "reason": f"native profile unavailable: {instance['profile_key']}",
                }
            )

    catalog_records = [
        row for row in asset_catalog.get("records", []) if isinstance(row, dict)
    ]
    catalog_by_container: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in catalog_records:
        container = catalog_source_container(record)
        if container:
            catalog_by_container[container].append(record)

    object_reasons: dict[int, set[str]] = defaultdict(set)
    for profile in profiles:
        object_reasons[int(profile["object_id"])].add(profile["profile_key"])
    for room in rooms:
        for row in (
            room["object_list"]["entries"] + room["object_list"]["unknown_entries"]
        ):
            object_id = row.get("object_id")
            if (
                isinstance(object_id, int)
                and object_id > 0
                and row.get("object_bank_dependency_candidate", True)
            ):
                try:
                    object_path(code_view, object_id)
                except Exception:
                    continue
                object_reasons[object_id].add(f"room:{room['room_index']}")
    special = selected_command(setup, 0x07)
    keep_object_id = (special or {}).get("decoded", {}).get("keep_object_id")
    if isinstance(keep_object_id, int):
        object_reasons[keep_object_id].add("scene:special_files")

    romfs_root = scene_root.parent
    object_bank_contract = evidence.object_bank_contract()
    object_dependencies: list[dict[str, Any]] = []
    for object_id in sorted(object_reasons):
        try:
            rom_path = object_path(code_view, object_id).replace("\\", "/")
        except Exception as exc:
            object_dependencies.append(
                {
                    "object_id": object_id,
                    "object_id_hex": f"0x{object_id:04X}",
                    "required_by": sorted(object_reasons[object_id]),
                    "source": None,
                    "catalog_bindings": [],
                    "catalog_binding_status": "native_archive_unresolved",
                }
            )
            unresolved.append(
                {
                    "kind": "object_archive",
                    "key": f"object:0x{object_id:04X}",
                    "severity": "composition",
                    "reason": str(exc),
                }
            )
            continue

        path_record_offset = code_offset(
            OBJECT_TABLE + object_id * 0x44, 0x44, code_view
        )
        path_record = {
            "record_address": address_hex(OBJECT_TABLE + object_id * 0x44),
            "record_stride": 0x44,
            "logical_path": rom_path,
            "cached_size_initial_value": code_view.u32(path_record_offset + 0x40),
        }
        host_path = romfs_root / Path(rom_path)
        if not host_path.is_file():
            object_dependencies.append(
                {
                    "object_id": object_id,
                    "object_id_hex": f"0x{object_id:04X}",
                    "required_by": sorted(object_reasons[object_id]),
                    "native_path_record": path_record,
                    "source": None,
                    "payload_status": "native_unmaterialized_object_bank_reference",
                    "required_engine_capabilities": ["native_zero_size_object_bank"],
                    "catalog_bindings": [],
                    "catalog_binding_status": "not_applicable_native_unmaterialized_reference",
                }
            )
            continue

        try:
            source = archive_descriptor(host_path, rom_path)
        except Exception as exc:
            object_dependencies.append(
                {
                    "object_id": object_id,
                    "object_id_hex": f"0x{object_id:04X}",
                    "required_by": sorted(object_reasons[object_id]),
                    "native_path_record": path_record,
                    "source": None,
                    "payload_status": "native_archive_parse_failed",
                    "catalog_bindings": [],
                    "catalog_binding_status": "native_archive_unresolved",
                }
            )
            unresolved.append(
                {
                    "kind": "object_archive",
                    "key": f"object:0x{object_id:04X}",
                    "severity": "composition",
                    "reason": str(exc),
                }
            )
            continue

        bindings = [
            catalog_record_summary(row)
            for row in catalog_by_container.get(rom_path.lower(), [])
        ]
        object_dependencies.append(
            {
                "object_id": object_id,
                "object_id_hex": f"0x{object_id:04X}",
                "required_by": sorted(object_reasons[object_id]),
                "native_path_record": path_record,
                "source": source,
                "payload_status": "native_zar_materialized",
                "catalog_bindings": bindings,
                "catalog_binding_status": (
                    "catalog_assets_resolved"
                    if bindings
                    else "catalog_assets_unresolved"
                ),
            }
        )
        if not bindings:
            unresolved.append(
                {
                    "kind": "object_asset_binding",
                    "key": f"object:0x{object_id:04X}",
                    "severity": "runtime_binding",
                    "reason": f"native archive {rom_path} has no asset-catalog member bindings",
                }
            )

    scene_path = scene_root / scene_path_name
    scene_archive_path = scene_root / f"{scene_stem}.zar"
    source_assets: dict[str, Any] = {
        "code_bin": logical_file(code_bin, "exefs/code.bin"),
        "scene_zsi": logical_file(scene_path, f"scene/{scene_path_name}"),
        "room_zsi": [room["source"] for room in rooms],
        "scene_zar": None,
    }
    if scene_archive_path.is_file():
        source_assets["scene_zar"] = archive_descriptor(
            scene_archive_path, f"scene/{scene_stem}.zar"
        )
    else:
        unresolved.append(
            {
                "kind": "scene_archive",
                "key": f"scene:{scene_stem}.zar",
                "severity": "composition",
                "reason": "native scene ZAR is missing",
            }
        )

    scene_id = native.get("scene_id")
    resource_rows = [
        row
        for row in scene_resource_table.get("rows", [])
        if row.get("zsi_path") == scene_path_name and row.get("scene_id") == scene_id
    ]
    if len(resource_rows) != 1:
        raise ValueError(
            f"native scene resource record for {scene_path_name} has {len(resource_rows)} matches"
        )
    scene_resource = deepcopy(resource_rows[0])
    draw_config = int(scene_resource["metadata_b1"])
    room_callbacks = evidence.room_callback_config(draw_config)
    prepare_draw_address = normalize_address(
        room_callbacks["callbacks"]["prepare_draw"]["address"]
    )
    runtime_contract = room_callback_runtime_contracts.get(prepare_draw_address)
    room_callbacks["runtime_contract"] = (
        deepcopy(runtime_contract)
        if runtime_contract is not None
        else {
            "format": "oot3d_room_scene_callback_native_runtime_v1",
            "status": "native_typed_operations_unavailable",
            "callback_address": f"0x{prepare_draw_address.upper()}",
            "operations": [],
            "source_registry": room_callback_contracts_path.name,
        }
    )
    scene_catalog_bindings = [
        catalog_record_summary(row)
        for row in catalog_records
        if (
            str(row.get("source_identity", "")).lower()
            in {f"scene:{scene_path_name}".lower(), f"room:{scene_path_name}".lower()}
            or catalog_source_container(row)
            in {scene_path_name.lower(), f"scene/{scene_path_name}".lower()}
        )
    ]
    callback_counts = Counter(
        callback["evidence_status"]
        for profile in profiles
        for callback in profile["callbacks"].values()
        if callback["evidence_status"] != "not_present_in_actor_profile"
    )
    behavior_graph_profile_count = sum(
        profile["behavior_graph"]["status"]
        in {
            "workflow_graph_recovered",
            "native_function_set_recovered",
            "native_consumer_graph_recovered",
        }
        for profile in profiles
    )
    behavior_function_count = sum(
        profile["behavior_graph"]["function_count"] for profile in profiles
    )
    behavior_action_transition_count = sum(
        profile["behavior_graph"]["action_transition_count"]
        for profile in profiles
    )
    behavior_structure_field_count = sum(
        profile["behavior_graph"]["structure_field_count"] for profile in profiles
    )
    behavior_indirect_root_count = sum(
        profile["behavior_graph"].get("indirect_root_count", 0)
        for profile in profiles
    )
    behavior_native_abi_function_count = sum(
        profile["behavior_graph"].get("native_abi_function_count", 0)
        for profile in profiles
    )
    behavior_consumer_root_count = sum(
        profile["behavior_graph"].get("consumer_root_count", 0)
        for profile in profiles
    )
    behavior_consumer_function_count = sum(
        profile["behavior_graph"].get("consumer_function_count", 0)
        for profile in profiles
    )
    behavior_consumer_call_edge_count = sum(
        profile["behavior_graph"].get("consumer_call_edge_count", 0)
        for profile in profiles
    )
    behavior_actor_local_consumer_function_count = sum(
        profile["behavior_graph"].get(
            "actor_local_consumer_function_count", 0
        )
        for profile in profiles
    )
    behavior_native_service_dependency_count = sum(
        profile["behavior_graph"].get("native_service_dependency_count", 0)
        for profile in profiles
    )
    indirect_root_catalog = evidence.indirect_root_catalog(
        behavior_indirect_root_count
    )
    native_abi_catalog = evidence.native_abi_catalog_summary(
        behavior_native_abi_function_count
    )
    severity_counts = Counter(str(row["severity"]) for row in unresolved)
    composition_complete = severity_counts["composition"] == 0
    behavior_complete = (
        callback_counts["callback_identity_unresolved"] == 0
        and all(
            profile["actor_init_evidence_status"] == "exact_snapshot_record"
            for profile in profiles
        )
        and all(
            callback["evidence_status"] == "workflow_semantics_recovered"
            for profile in profiles
            for callback in profile["callbacks"].values()
            if callback["evidence_status"] != "not_present_in_actor_profile"
        )
    )
    status = (
        "composition_incomplete"
        if not composition_complete
        else "composition_complete_behavior_complete"
        if behavior_complete
        else "composition_complete_behavior_incomplete"
    )

    unit: dict[str, Any] = {
        "format": FORMAT,
        "schema_version": SCHEMA_VERSION,
        "status": status,
        "identity": {
            "unit_id": f"{route_id}:setup-{setup_index}",
            "route_id": route_id,
            "scene_id": scene_id,
            "scene_stem": scene_stem,
            "scene_path": scene_path_name,
            "setup_index": setup_index,
            "native_room_indices": requested_room_indices,
            "initial_room_index": initial_room_index,
        },
        "authority_policy": {
            "semantic_control_plane": "oot_n64_gameplay_scaffold",
            "native_content": "oot3d_original_files_and_code_bin",
            "spatial_state": "oot3d_native",
            "timing": "oot3d_native",
            "rendering": "oot3d_native",
            "hud_menu_and_connected_logic": "oot_n64_ship",
            "emulator": "validation_only",
            "runtime_asset_substitution": "forbidden",
        },
        "provenance": {
            "compiler": {
                "name": "oot3d_asset_tool.room_compilation_unit",
                "version": COMPILER_VERSION,
            },
            "semantic_route_catalog": {
                "format": route_catalog.get("format"),
                "sha256": sha256_file(semantic_route_catalog_path),
            },
            "asset_catalog": {
                "format": asset_catalog.get("format"),
                "sha256": sha256_file(asset_catalog_path),
            },
            "scene_resource_table": {
                "sha256": sha256_file(scene_resource_table_path),
            },
            "zelda3drecomp_evidence_snapshot": {
                "snapshot_id": snapshot.manifest.get("snapshot_id"),
                "manifest_sha256": sha256_file(snapshot.root / "manifest.json"),
                "source_base_revision": snapshot.manifest.get("source_base_revision"),
                "source_branch": snapshot.manifest.get("source_branch"),
                "source_worktree_dirty": snapshot.manifest.get("source_worktree_dirty"),
                "source_worktree_change_count": snapshot.manifest.get(
                    "source_worktree_change_count"
                ),
                "source_selection_mode": snapshot.manifest.get(
                    "source_selection_mode", "live_worktree"
                ),
                "generated_artifact_count": snapshot.manifest.get(
                    "generated_artifact_count", 0
                ),
                "code_bin_sha256": snapshot.manifest.get("code_bin_sha256"),
                "file_count": snapshot.manifest.get("file_count"),
            },
        },
        "semantic_request": deepcopy(route.get("scaffold", {})),
        "semantic_variant_evidence": deepcopy(route.get("evidence", {})),
        "native_route": deepcopy(native),
        "source_assets": source_assets,
        "scene_resource_record": scene_resource,
        "room_callback_contract": room_callbacks,
        "room_lifecycle_contract": room_lifecycle,
        "object_bank_contract": object_bank_contract,
        "indirect_native_root_catalog": indirect_root_catalog,
        "native_abi_catalog": native_abi_catalog,
        "scene_setup": setup,
        "collision_headers": deepcopy(scene.get("collision_header_candidates", [])),
        "entrypoint": player,
        "rooms": rooms,
        "actor_instances": instances,
        "actor_profiles": profiles,
        "object_dependencies": object_dependencies,
        "scene_catalog_bindings": scene_catalog_bindings,
        "closure": {
            "composition_status": (
                "composition_complete"
                if composition_complete
                else "composition_incomplete"
            ),
            "behavior_evidence_status": (
                "behavior_evidence_complete"
                if behavior_complete
                else "behavior_evidence_incomplete"
            ),
            "runtime_binding_status": "consumer_binding_required",
            "room_count": len(rooms),
            "actor_instance_count": len(instances),
            "unique_actor_profile_count": len(profiles),
            "object_dependency_count": len(object_dependencies),
            "materialized_object_archive_count": sum(
                row.get("payload_status") == "native_zar_materialized"
                for row in object_dependencies
            ),
            "unmaterialized_object_bank_count": sum(
                row.get("payload_status")
                == "native_unmaterialized_object_bank_reference"
                for row in object_dependencies
            ),
            "callback_evidence_counts": dict(sorted(callback_counts.items())),
            "behavior_graph_profile_count": behavior_graph_profile_count,
            "behavior_function_count": behavior_function_count,
            "behavior_action_transition_count": behavior_action_transition_count,
            "behavior_structure_field_count": behavior_structure_field_count,
            "behavior_indirect_root_count": behavior_indirect_root_count,
            "behavior_native_abi_function_count": (
                behavior_native_abi_function_count
            ),
            "behavior_consumer_root_count": behavior_consumer_root_count,
            "behavior_consumer_function_count": behavior_consumer_function_count,
            "behavior_consumer_call_edge_count": behavior_consumer_call_edge_count,
            "behavior_actor_local_consumer_function_count": (
                behavior_actor_local_consumer_function_count
            ),
            "behavior_native_service_dependency_count": (
                behavior_native_service_dependency_count
            ),
            "unresolved_count": len(unresolved),
            "unresolved_severity_counts": dict(sorted(severity_counts.items())),
        },
        "unresolved": sorted(
            unresolved,
            key=lambda row: (str(row["severity"]), str(row["kind"]), str(row["key"])),
        ),
    }
    digest_payload = deepcopy(unit)
    unit["identity"]["payload_sha256"] = canonical_sha256(digest_payload)
    validate_room_compilation_unit(unit)
    return unit


def validate_room_lifecycle_contract(
    unit: dict[str, Any], lifecycle: dict[str, Any]
) -> None:
    if (
        lifecycle.get("format") != "oot3d_room_lifecycle_contract_v1"
        or lifecycle.get("status") != "workflow_semantics_recovered"
    ):
        raise ValueError("unsupported room lifecycle contract")
    functions = lifecycle.get("functions")
    required_roles = {
        "initialize",
        "request",
        "process_request",
        "destroy",
        "cleanup_room_actors",
        "spawn_transition_actors",
        "queue_resource_cleanup",
        "process_resource_cleanup",
    }
    if not isinstance(functions, dict) or set(functions) != required_roles:
        raise ValueError("room lifecycle function closure is incomplete")
    for role, function in functions.items():
        if not isinstance(function, dict) or not function.get("name"):
            raise ValueError(f"room lifecycle function {role} lacks identity")
        if int(normalize_address(function.get("address")), 16) == 0:
            raise ValueError(f"room lifecycle function {role} has a null address")

    state_model = lifecycle.get("state_model")
    actor_residency = lifecycle.get("actor_residency")
    resource_residency = lifecycle.get("resource_residency")
    if not all(
        isinstance(value, dict)
        for value in (state_model, actor_residency, resource_residency)
    ):
        raise ValueError("room lifecycle state policies are incomplete")
    state_values = [state_model.get(key) for key in ("idle", "loading", "terminal")]
    if (
        any(
            not isinstance(value, int) or not 0 <= value <= 0xFF
            for value in state_values
        )
        or len(set(state_values)) != 3
        or state_model.get("max_resident_room_count") != 2
        or state_model.get("buffer_count_without_transition_actors") != 1
        or state_model.get("buffer_count_with_transition_actors") != 3
    ):
        raise ValueError("room lifecycle state model is invalid")
    transition_actor_count = sum(
        instance.get("source_kind") == "scene_transition_actor_list"
        for instance in unit.get("actor_instances", [])
    )
    if state_model.get("initial_buffer_count") != (3 if transition_actor_count else 1):
        raise ValueError("room lifecycle initial buffer count is inconsistent")
    if (
        actor_residency.get("room_actor_retention_policy")
        != "negative_room_or_current_or_previous"
        or actor_residency.get("transition_actor_residency_policy")
        != "front_or_back_matches_current_or_previous"
        or actor_residency.get("transition_actor_id_spawn_mask") != 0x1FFF
        or actor_residency.get("transition_actor_params_index_stride") != 0x400
        or actor_residency.get("transition_actor_spawn_marker")
        != "negate_source_actor_id"
        or resource_residency.get("previous_room_cleanup_before_next_request")
        is not True
        or resource_residency.get("room_commands_install_on_request_completion")
        is not True
        or resource_residency.get("cleanup_delay_ticks") != 1
    ):
        raise ValueError("room lifecycle consumer policies are unsupported")


def validate_room_callback_contract(contract: dict[str, Any]) -> None:
    if not contract:
        return
    callbacks = contract.get("callbacks")
    runtime = contract.get("runtime_contract")
    if not isinstance(callbacks, dict) or not isinstance(runtime, dict):
        raise ValueError("room callback contract is incomplete")
    prepare_draw = callbacks.get("prepare_draw")
    if not isinstance(prepare_draw, dict):
        raise ValueError("room callback contract lacks prepare_draw identity")
    if runtime.get("format") != "oot3d_room_scene_callback_native_runtime_v1":
        raise ValueError("unsupported room callback runtime contract")
    if normalize_address(runtime.get("callback_address")) != normalize_address(
        prepare_draw.get("address")
    ):
        raise ValueError("room callback runtime address differs from callback table")
    status = runtime.get("status")
    operations = runtime.get("operations")
    if status == "native_typed_operations_unavailable":
        if operations != []:
            raise ValueError("unavailable room callback unexpectedly has operations")
        return
    if status != "native_typed_operations_ready" or not isinstance(operations, list):
        raise ValueError("room callback runtime status is unsupported")
    if not operations:
        raise ValueError("ready room callback has no operations")
    for operation in operations:
        if (
            not isinstance(operation, dict)
            or operation.get("kind") != "material_tev_constant_alpha"
            or operation.get("native_operation") != 2
        ):
            raise ValueError("room callback operation is unsupported")
        target = operation.get("target")
        alpha = operation.get("alpha")
        if not isinstance(target, dict) or not isinstance(alpha, dict):
            raise ValueError("room callback operation is incomplete")
        if (
            not isinstance(target.get("room_index"), int)
            or not isinstance(target.get("resource_index"), int)
            or target.get("room_index") < 0
            or target.get("resource_index") < 0
            or target.get("constant_index") not in range(6)
            or not isinstance(target.get("material_indices"), list)
            or not target.get("material_indices")
            or any(
                not isinstance(value, int) or value < 0
                for value in target["material_indices"]
            )
        ):
            raise ValueError("room callback material target is invalid")
        rgb = operation.get("color_rgb_f32_bits")
        if not isinstance(rgb, list) or len(rgb) != 3:
            raise ValueError("room callback RGB vector is invalid")
        for value in [
            *rgb,
            alpha.get("base_f32_bits"),
            alpha.get("selector_scale_f32_bits"),
        ]:
            read_hex_u32(value, "room callback float bits")
        selector = alpha.get("selector")
        random = alpha.get("random")
        if not isinstance(selector, dict) or not isinstance(random, dict):
            raise ValueError("room callback alpha evaluator is incomplete")
        for fact_name in ("alternate_fact", "required_fact"):
            fact = selector.get(fact_name)
            if (
                not isinstance(fact, dict)
                or not isinstance(fact.get("key"), str)
                or not isinstance(fact.get("equals"), str)
            ):
                raise ValueError("room callback fact predicate is invalid")
        if random.get("function") != "Rand_S16Offset":
            raise ValueError("room callback random evaluator is unsupported")
        read_hex_u32(
            random.get("function_address"),
            "room callback random function_address",
        )
        read_hex_u32(
            random.get("state_address"), "room callback random state_address"
        )
        if (
            not isinstance(random.get("function_size"), int)
            or random["function_size"] <= 0
            or not isinstance(random.get("function_sha256"), str)
            or len(random["function_sha256"]) != 64
        ):
            raise ValueError("room callback random function identity is invalid")
        for key in (
            "lcg_multiplier",
            "lcg_increment",
            "scale_f32_bits",
            "middle_test_add_u32",
            "middle_test_less_than_u32",
            "low_compare_f32_bits",
        ):
            read_hex_u32(random.get(key), f"room callback random {key}")


def validate_actor_behavior_graph(
    profile: dict[str, Any], graph: dict[str, Any]
) -> None:
    status = graph.get("status")
    functions = graph.get("functions")
    transitions = graph.get("action_transitions")
    fields = graph.get("structure_fields")
    initial_actions = graph.get("initial_action_candidates")
    consumer_roots = graph.get("consumer_roots", [])
    consumer_edges = graph.get("consumer_call_edges", [])
    unresolved_consumer_dependencies = graph.get(
        "unresolved_consumer_dependencies", []
    )
    if not all(
        isinstance(value, list)
        for value in (
            functions,
            transitions,
            fields,
            initial_actions,
            consumer_roots,
            consumer_edges,
            unresolved_consumer_dependencies,
        )
    ):
        raise ValueError("actor behavior graph arrays are incomplete")
    if (
        graph.get("function_count") != len(functions)
        or graph.get("action_transition_count") != len(transitions)
        or graph.get("structure_field_count") != len(fields)
    ):
        raise ValueError("actor behavior graph counts are inconsistent")
    indirect_roots = [
        function
        for function in functions
        if isinstance(function, dict)
        and function.get("discovery_kind") == "zero_direct_caller_native_root"
    ]
    if graph.get("indirect_root_count", 0) != len(indirect_roots):
        raise ValueError("actor behavior indirect-root count is inconsistent")
    native_abi_functions = [
        function
        for function in functions
        if isinstance(function, dict)
        and function.get("closure_kind")
        in {"indirect_root", "parent_owned_root", "maintained_abi"}
    ]
    if graph.get("native_abi_function_count", 0) != len(native_abi_functions):
        raise ValueError("actor behavior native-ABI function count is inconsistent")
    consumer_functions = [
        function
        for function in functions
        if isinstance(function, dict) and function.get("consumer_role")
    ]
    if (
        graph.get("consumer_root_count", 0) != len(consumer_roots)
        or graph.get("consumer_function_count", 0) != len(consumer_functions)
        or graph.get("consumer_call_edge_count", 0) != len(consumer_edges)
        or graph.get("actor_local_consumer_function_count", 0)
        != sum(
            function.get("consumer_role") == "actor_local_consumer_helper"
            for function in consumer_functions
        )
        or graph.get("native_service_dependency_count", 0)
        != sum(
            function.get("consumer_role") == "native_service_dependency"
            for function in consumer_functions
        )
    ):
        raise ValueError("actor original-consumer counts are inconsistent")

    if status == "workflow_graph_unavailable":
        if (
            functions
            or transitions
            or fields
            or initial_actions
            or graph.get("structure_size") is not None
            or graph.get("indirect_root_count", 0) != 0
            or graph.get("native_abi_function_count", 0) != 0
            or consumer_roots
            or consumer_edges
            or unresolved_consumer_dependencies
            or graph.get("consumer_root_count", 0) != 0
            or graph.get("consumer_function_count", 0) != 0
            or graph.get("consumer_call_edge_count", 0) != 0
            or graph.get("consumer_status", "consumer_evidence_unavailable")
            != "consumer_evidence_unavailable"
            or graph.get("runtime_binding_status")
            != "behavior_evidence_not_available"
        ):
            raise ValueError("unavailable actor behavior graph contains evidence")
        return
    if (
        status
        not in {
            "workflow_graph_recovered",
            "native_function_set_recovered",
            "native_consumer_graph_recovered",
        }
        or not graph.get("owner")
        or not graph.get("structure")
        or graph.get("runtime_binding_status")
        != "consumer_must_bind_native_behavior_graph"
        or not functions
    ):
        raise ValueError("actor behavior graph identity is incomplete")
    structure_size = graph.get("structure_size")
    if (
        not isinstance(structure_size, int)
        or structure_size <= 0
        or structure_size != profile.get("instance_size")
    ):
        raise ValueError("actor behavior graph structure size differs from ActorInit")

    function_by_name: dict[str, str] = {}
    function_by_address: dict[str, str] = {}
    function_addresses: set[str] = set()
    for function in functions:
        if not isinstance(function, dict):
            raise ValueError("actor behavior function is not an object")
        name = function.get("name")
        address = normalize_address(function.get("address"))
        if (
            not isinstance(name, str)
            or not name
            or int(address, 16) == 0
            or name in function_by_name
            or address in function_addresses
            or not isinstance(function.get("byte_length"), int)
            or function["byte_length"] <= 0
        ):
            raise ValueError("actor behavior function identity is invalid")
        function_by_name[name] = address
        function_by_address[address] = name
        function_addresses.add(address)
        consumer_role = function.get("consumer_role", "")
        if consumer_role:
            if (
                consumer_role
                not in {
                    "native_lifecycle_consumer",
                    "actor_local_consumer_helper",
                    "native_service_dependency",
                }
                or not isinstance(function.get("consumer_depth"), int)
                or function["consumer_depth"] < 0
                or function.get("body_status")
                not in {
                    "reviewed_original_callback_body",
                    "reviewed_original_function_body",
                    "original_function_inventory_only",
                }
            ):
                raise ValueError("actor original-consumer function metadata is invalid")
            if (
                consumer_role == "native_lifecycle_consumer"
                and function["consumer_depth"] != 0
            ):
                raise ValueError("actor lifecycle consumer must be a closure root")
        closure_kind = function.get("closure_kind")
        if closure_kind in {"indirect_root", "parent_owned_root", "maintained_abi"}:
            parameter_types = str(function.get("param_types", "")).split(";")
            owner_resolution = function.get("owner_resolution")
            if (
                not isinstance(function.get("source_tranche"), int)
                or function["source_tranche"] <= 0
            ):
                raise ValueError("actor behavior native-ABI tranche is invalid")
            if owner_resolution == "exact_concrete_instance_pointer_in_native_abi":
                if f"{graph['structure']}*" not in parameter_types:
                    raise ValueError("actor behavior concrete ABI ownership is invalid")
            elif owner_resolution == (
                "exact_maintained_actor_symbol_prefix_and_native_context_abi"
            ):
                if (
                    closure_kind != "maintained_abi"
                    or not str(name).startswith(f"{graph['owner']}_")
                    or "Oot3dPlayState*" not in parameter_types
                    or "void*" not in parameter_types
                ):
                    raise ValueError("actor behavior maintained ABI ownership is invalid")
            else:
                raise ValueError("actor behavior native-ABI ownership is unresolved")

    root_names: set[str] = set()
    for root in consumer_roots:
        if not isinstance(root, dict):
            raise ValueError("actor original-consumer root is not an object")
        name = root.get("name")
        address = normalize_address(root.get("address"))
        function = next(
            (
                row
                for row in consumer_functions
                if row.get("name") == name
                and normalize_address(row.get("address")) == address
            ),
            None,
        )
        if (
            root.get("slot") not in {"init", "destroy", "update", "draw"}
            or function is None
            or function.get("consumer_role") != "native_lifecycle_consumer"
            or root.get("body_status") != function.get("body_status")
            or name in root_names
        ):
            raise ValueError("actor original-consumer root identity is invalid")
        root_names.add(name)

    edge_keys: set[tuple[str, str]] = set()
    for edge in consumer_edges:
        if not isinstance(edge, dict):
            raise ValueError("actor original-consumer call edge is not an object")
        caller_address = normalize_address(edge.get("caller_address"))
        callee_address = normalize_address(edge.get("callee_address"))
        key = (caller_address, callee_address)
        if (
            edge.get("relation") != "direct_arm_call"
            or function_by_address.get(caller_address) != edge.get("caller_name")
            or function_by_address.get(callee_address) != edge.get("callee_name")
            or key in edge_keys
        ):
            raise ValueError("actor original-consumer call edge is invalid")
        edge_keys.add(key)

    for address in unresolved_consumer_dependencies:
        if int(normalize_address(address), 16) == 0:
            raise ValueError("actor unresolved consumer dependency is null")
    if consumer_roots:
        if (
            graph.get("consumer_status")
            not in {
                "original_lifecycle_consumers_recovered",
                "lifecycle_consumer_inventory_partial",
            }
            or graph.get("consumer_runtime_binding_status")
            != "consumer_must_lower_original_bodies_and_bind_native_services"
        ):
            raise ValueError("actor original-consumer binding status is invalid")
    elif graph.get(
        "consumer_runtime_binding_status", "consumer_evidence_not_available"
    ) != "consumer_evidence_not_available":
        raise ValueError("actor behavior has consumer binding without roots")

    for transition in transitions:
        if not isinstance(transition, dict):
            raise ValueError("actor behavior transition is not an object")
        source = transition.get("source_function")
        target = transition.get("target_name")
        target_address = normalize_address(transition.get("target_address"))
        if source not in function_by_name:
            raise ValueError("actor behavior transition source is outside its graph")
        if transition.get("status") == "exact_function" and (
            target not in function_by_name
            or function_by_name[target] != target_address
        ):
            raise ValueError("actor behavior transition target is outside its graph")
        if int(normalize_address(transition.get("literal_address")), 16) == 0:
            raise ValueError("actor behavior transition has a null literal address")

    if any(action not in function_by_name for action in initial_actions):
        raise ValueError("actor initial action candidate is outside its graph")
    field_offsets: set[int] = set()
    for field in fields:
        if not isinstance(field, dict):
            raise ValueError("actor behavior structure field is not an object")
        offset = field.get("offset")
        if (
            not isinstance(offset, int)
            or offset < 0
            or offset >= structure_size
            or offset in field_offsets
            or not field.get("name")
            or not field.get("type")
        ):
            raise ValueError("actor behavior structure field is invalid")
        field_offsets.add(offset)


def validate_room_compilation_unit(unit: dict[str, Any]) -> None:
    if unit.get("format") != FORMAT or unit.get("schema_version") != SCHEMA_VERSION:
        raise ValueError("unsupported room compilation unit format")
    if unit.get("status") not in {
        "composition_incomplete",
        "composition_complete_behavior_incomplete",
        "composition_complete_behavior_complete",
    }:
        raise ValueError("invalid room compilation unit status")
    identity = unit.get("identity")
    if not isinstance(identity, dict) or not isinstance(identity.get("route_id"), str):
        raise ValueError("room compilation unit identity is incomplete")
    digest = identity.get("payload_sha256")
    if not isinstance(digest, str) or len(digest) != 64:
        raise ValueError("room compilation unit payload digest is missing")
    payload = deepcopy(unit)
    payload["identity"].pop("payload_sha256", None)
    if canonical_sha256(payload) != digest:
        raise ValueError("room compilation unit payload digest mismatch")

    lifecycle = unit.get("room_lifecycle_contract")
    if not isinstance(lifecycle, dict):
        if unit.get("status") != "composition_incomplete":
            raise ValueError("room compilation unit lacks its room lifecycle contract")
    else:
        validate_room_lifecycle_contract(unit, lifecycle)

    room_callbacks = unit.get("room_callback_contract")
    if not isinstance(room_callbacks, dict):
        raise ValueError("room callback contract is not an object")
    validate_room_callback_contract(room_callbacks)

    room_indices = [room.get("room_index") for room in unit.get("rooms", [])]
    if len(room_indices) != len(set(room_indices)):
        raise ValueError("room compilation unit has duplicate rooms")
    setup_index = identity.get("setup_index")
    for room in unit.get("rooms", []):
        if room.get("setup_index") != setup_index:
            raise ValueError("compiled room setup does not match unit setup")
        commands = room.get("commands")
        if not isinstance(commands, list) or not commands:
            raise ValueError("compiled room lacks its native command stream")
        if len(commands) > 0x20 or commands[-1].get("command_id") != 0x14:
            raise ValueError(
                "compiled room command stream is not bounded by end marker"
            )
    profile_keys = {
        profile.get("profile_key") for profile in unit.get("actor_profiles", [])
    }
    behavior_graph_profile_count = 0
    behavior_function_count = 0
    behavior_action_transition_count = 0
    behavior_structure_field_count = 0
    behavior_indirect_root_count = 0
    behavior_native_abi_function_count = 0
    behavior_consumer_root_count = 0
    behavior_consumer_function_count = 0
    behavior_consumer_call_edge_count = 0
    behavior_actor_local_consumer_function_count = 0
    behavior_native_service_dependency_count = 0
    for profile in unit.get("actor_profiles", []):
        graph = profile.get("behavior_graph")
        if not isinstance(graph, dict):
            raise ValueError("actor profile lacks its behavior graph contract")
        validate_actor_behavior_graph(profile, graph)
        if graph.get("status") in {
            "workflow_graph_recovered",
            "native_function_set_recovered",
            "native_consumer_graph_recovered",
        }:
            behavior_graph_profile_count += 1
        behavior_function_count += graph["function_count"]
        behavior_action_transition_count += graph["action_transition_count"]
        behavior_structure_field_count += graph["structure_field_count"]
        behavior_indirect_root_count += graph.get("indirect_root_count", 0)
        behavior_native_abi_function_count += graph.get(
            "native_abi_function_count", 0
        )
        behavior_consumer_root_count += graph.get("consumer_root_count", 0)
        behavior_consumer_function_count += graph.get(
            "consumer_function_count", 0
        )
        behavior_consumer_call_edge_count += graph.get(
            "consumer_call_edge_count", 0
        )
        behavior_actor_local_consumer_function_count += graph.get(
            "actor_local_consumer_function_count", 0
        )
        behavior_native_service_dependency_count += graph.get(
            "native_service_dependency_count", 0
        )

    indirect_catalog = unit.get("indirect_native_root_catalog")
    if indirect_catalog is not None:
        if (
            not isinstance(indirect_catalog, dict)
            or indirect_catalog.get("format")
            != "oot3d_indirect_native_root_catalog_v1"
            or indirect_catalog.get("status")
            != "reviewed_native_roots_indexed"
            or indirect_catalog.get("runtime_binding_status")
            != "profile_bound_roots_require_consumer_binding"
            or indirect_catalog.get("profile_bound_root_count")
            != behavior_indirect_root_count
            or not isinstance(indirect_catalog.get("root_count"), int)
            or indirect_catalog["root_count"] < behavior_indirect_root_count
            or not isinstance(indirect_catalog.get("actor_candidate_count"), int)
            or indirect_catalog["actor_candidate_count"]
            < behavior_indirect_root_count
            or indirect_catalog.get("unbound_actor_candidate_count")
            != indirect_catalog["actor_candidate_count"]
            - behavior_indirect_root_count
        ):
            raise ValueError("indirect native-root catalog is inconsistent")

    native_abi_catalog = unit.get("native_abi_catalog")
    if native_abi_catalog is not None:
        if not isinstance(native_abi_catalog, dict):
            raise ValueError("native ABI catalog summary is not an object")
        closure_counts = native_abi_catalog.get("closure_kind_counts")
        if (
            native_abi_catalog.get("format") != "oot3d_native_abi_catalog_v1"
            or native_abi_catalog.get("status")
            != "reviewed_native_abi_catalog_complete"
            or native_abi_catalog.get("runtime_binding_status")
            != "catalog_mounted_once_profile_bindings_in_rcu"
            or native_abi_catalog.get("profile_bound_function_count")
            != behavior_native_abi_function_count
            or not isinstance(native_abi_catalog.get("function_count"), int)
            or native_abi_catalog["function_count"]
            < behavior_native_abi_function_count
            or not isinstance(closure_counts, dict)
            or sum(closure_counts.values()) != native_abi_catalog["function_count"]
            or not isinstance(native_abi_catalog.get("payload_sha256"), str)
            or len(native_abi_catalog["payload_sha256"]) != 64
        ):
            raise ValueError("native ABI catalog summary is inconsistent")

    closure = unit.get("closure")
    if not isinstance(closure, dict) or any(
        closure.get(key) != expected
        for key, expected in (
            ("behavior_graph_profile_count", behavior_graph_profile_count),
            ("behavior_function_count", behavior_function_count),
            (
                "behavior_action_transition_count",
                behavior_action_transition_count,
            ),
            ("behavior_structure_field_count", behavior_structure_field_count),
        )
    ) or closure.get(
        "behavior_indirect_root_count", 0
    ) != behavior_indirect_root_count or closure.get(
        "behavior_native_abi_function_count", 0
    ) != behavior_native_abi_function_count or closure.get(
        "behavior_consumer_root_count", 0
    ) != behavior_consumer_root_count or closure.get(
        "behavior_consumer_function_count", 0
    ) != behavior_consumer_function_count or closure.get(
        "behavior_consumer_call_edge_count", 0
    ) != behavior_consumer_call_edge_count or closure.get(
        "behavior_actor_local_consumer_function_count", 0
    ) != behavior_actor_local_consumer_function_count or closure.get(
        "behavior_native_service_dependency_count", 0
    ) != behavior_native_service_dependency_count:
        raise ValueError("room compilation behavior graph closure is inconsistent")
    for instance in unit.get("actor_instances", []):
        if instance.get("profile_key") not in profile_keys:
            if not any(
                gap.get("kind") == "actor_instance_profile"
                and gap.get("key") == instance.get("instance_key")
                for gap in unit.get("unresolved", [])
            ):
                raise ValueError(
                    f"actor instance lacks profile and explicit gap: {instance.get('instance_key')}"
                )
    for source in source_descriptors(unit):
        digest = source.get("sha256")
        if not isinstance(digest, str) or len(digest) != 64:
            raise ValueError(
                f"source asset lacks SHA-256: {source.get('logical_path')}"
            )


def source_descriptors(unit: dict[str, Any]) -> Iterable[dict[str, Any]]:
    assets = unit.get("source_assets", {})
    for key in ("code_bin", "scene_zsi", "scene_zar"):
        source = assets.get(key)
        if isinstance(source, dict):
            yield source
    for source in assets.get("room_zsi", []):
        if isinstance(source, dict):
            yield source
    for dependency in unit.get("object_dependencies", []):
        source = dependency.get("source")
        if isinstance(source, dict):
            yield source


def write_room_compilation_unit(unit: dict[str, Any], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(unit, indent=2, sort_keys=True, ensure_ascii=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def room_compilation_unit_markdown(unit: dict[str, Any]) -> str:
    identity = unit["identity"]
    closure = unit["closure"]
    lifecycle = unit["room_lifecycle_contract"]
    lines = [
        "# OOT3D Room Compilation Unit",
        "",
        f"- Unit: `{identity['unit_id']}`",
        f"- Status: `{unit['status']}`",
        f"- Native scene/setup: `{identity['scene_path']}` / `{identity['setup_index']}`",
        f"- Native rooms: `{', '.join(str(value) for value in identity['native_room_indices'])}`",
        f"- Initial room: `{identity['initial_room_index']}`",
        (
            f"- Actor instances/profiles: `{closure['actor_instance_count']}` / "
            f"`{closure['unique_actor_profile_count']}`"
        ),
        f"- Native object archives: `{closure['object_dependency_count']}`",
        f"- Composition: `{closure['composition_status']}`",
        f"- Behavior evidence: `{closure['behavior_evidence_status']}`",
        f"- Profile-bound indirect roots: `{closure.get('behavior_indirect_root_count', 0)}`",
        (
            f"- Original lifecycle consumers/functions/edges: "
            f"`{closure.get('behavior_consumer_root_count', 0)}` / "
            f"`{closure.get('behavior_consumer_function_count', 0)}` / "
            f"`{closure.get('behavior_consumer_call_edge_count', 0)}`"
        ),
        f"- Runtime binding: `{closure['runtime_binding_status']}`",
        (
            f"- Room lifecycle: `{lifecycle['status']}` from snapshot "
            f"`{lifecycle['source_snapshot_id']}`"
        ),
        f"- Unresolved: `{closure['unresolved_count']}`",
        "",
        "## Authority",
        "",
        (
            "The N64 scaffold selects the semantic request only. Scene setup, "
            "entrypoint, rooms, actors, object archives, transforms, parameters, "
            "camera source, lighting, materials and timing remain native OOT3D "
            "data/code identities."
        ),
        "",
        "## Unresolved",
        "",
    ]
    if not unit["unresolved"]:
        lines.append("None.")
    else:
        lines.extend(
            f"- `{row['severity']}` `{row['kind']}` `{row['key']}`: {row['reason']}"
            for row in unit["unresolved"]
        )
    return "\n".join(lines) + "\n"


def write_room_compilation_markdown(unit: dict[str, Any], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        room_compilation_unit_markdown(unit), encoding="utf-8", newline="\n"
    )
