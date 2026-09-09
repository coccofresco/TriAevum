"""Audit a TriAevum public package against its exact release allowlist."""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import re
import stat
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence

try:
    from .product_contract import validate_product_info
    from .precompiled_titles import MODEL, CATALOG, load_catalog, select_title, checked_file
    from .common import load_json_object, normalize_relative_path, sha256_file
    from .input_adapters import validate_adapter
    from .source_contracts import source_contract_errors
except ImportError:
    from product_contract import validate_product_info
    from precompiled_titles import MODEL, CATALOG, load_catalog, select_title, checked_file
    from common import load_json_object, normalize_relative_path, sha256_file
    from input_adapters import validate_adapter
    from source_contracts import source_contract_errors


ROOT = Path(__file__).resolve().parent
DEFAULT_POLICY = ROOT / "release_policy.json"
DEFAULT_MANIFEST_NAME = "release-manifest.json"
_ABSOLUTE_HOST_PATH = re.compile(r"^(?:[A-Za-z]:[\\/]|/(?:home|Users|i|c|mnt)/)")


@dataclass(frozen=True)
class ReleaseAuditResult:
    summary: dict[str, object]
    errors: tuple[str, ...]

    @property
    def ok(self) -> bool:
        return not self.errors


def _json_policy_violations(
    value: Any,
    *,
    true_fields: set[str],
    false_fields: set[str],
    location: str = "$",
) -> Iterable[str]:
    if isinstance(value, dict):
        for key, child in value.items():
            child_location = f"{location}.{key}"
            if key in true_fields and child is True:
                yield f"{child_location} is forbidden when true"
            if key in false_fields and child is False:
                yield f"{child_location} is forbidden when false"
            yield from _json_policy_violations(
                child,
                true_fields=true_fields,
                false_fields=false_fields,
                location=child_location,
            )
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from _json_policy_violations(
                child,
                true_fields=true_fields,
                false_fields=false_fields,
                location=f"{location}[{index}]",
            )
    elif isinstance(value, str) and _ABSOLUTE_HOST_PATH.match(value):
        yield f"{location} contains an absolute host path"


def _matches_any(path: str, patterns: Iterable[str]) -> bool:
    lowered = path.lower()
    return any(fnmatch.fnmatchcase(lowered, pattern.lower()) for pattern in patterns)


def _path_policy_errors(
    relative: str,
    *,
    forbidden_names: set[str],
    forbidden_extensions: set[str],
    forbidden_components: set[str],
) -> Iterable[str]:
    pure = Path(relative)
    if pure.name.lower() in forbidden_names:
        yield f"forbidden filename: {relative}"
    if pure.suffix.lower() in forbidden_extensions:
        yield f"forbidden extension: {relative}"
    if any(part.lower() in forbidden_components for part in pure.parts):
        yield f"forbidden path component: {relative}"


def _source_archive_errors(
    archive: Path,
    *,
    forbidden_names: set[str],
    forbidden_extensions: set[str],
    forbidden_components: set[str],
    forbidden_hashes: set[str],
    forbidden_prefixes: tuple[str, ...],
    source_policy: dict[str, Any] | None = None,
) -> Iterable[str]:
    try:
        with zipfile.ZipFile(archive) as source:
            if source_policy is not None:
                yield from source_contract_errors(
                    {item.filename for item in source.infolist() if not item.is_dir()},
                    source.read, source_policy)
            for item in source.infolist():
                if item.is_dir():
                    continue
                try:
                    relative = normalize_relative_path(item.filename)
                except ValueError as exc:
                    yield f"unsafe source archive entry: {exc}"
                    continue
                mode = (item.external_attr >> 16) & 0xFFFF
                if stat.S_ISLNK(mode):
                    yield f"source archive contains a symbolic link: {relative}"
                    continue
                for error in _path_policy_errors(
                    relative,
                    forbidden_names=forbidden_names,
                    forbidden_extensions=forbidden_extensions,
                    forbidden_components=forbidden_components,
                ):
                    yield f"source archive {error}"
                lowered = relative.lower()
                if any(
                    lowered.startswith(prefix.rstrip("/") + "/")
                    for prefix in forbidden_prefixes
                ):
                    yield f"source archive forbidden source prefix: {relative}"
                if Path(relative).suffix.lower() == ".zip":
                    yield f"source archive contains an opaque nested archive: {relative}"
                digest = hashlib.sha256()
                with source.open(item, "r") as entry:
                    for chunk in iter(lambda: entry.read(1024 * 1024), b""):
                        digest.update(chunk)
                if digest.hexdigest() in forbidden_hashes:
                    yield f"source archive contains a known private artifact: {relative}"
    except (OSError, zipfile.BadZipFile, RuntimeError) as exc:
        yield f"corresponding-source ZIP cannot be audited: {exc}"


def audit_release(
    package_root: Path,
    *,
    manifest_path: Path | None = None,
    policy_path: Path = DEFAULT_POLICY,
) -> ReleaseAuditResult:
    errors: list[str] = []
    root = package_root.resolve()
    manifest_path = (manifest_path or root / DEFAULT_MANIFEST_NAME).resolve()

    def reject(message: str) -> None:
        if message not in errors:
            errors.append(message)

    if not root.is_dir():
        return ReleaseAuditResult({}, (f"package directory does not exist: {root}",))
    if root == Path(root.anchor):
        return ReleaseAuditResult({}, ("refusing to audit a volume root",))
    if not policy_path.is_file():
        return ReleaseAuditResult(
            {}, (f"release policy does not exist: {policy_path}",)
        )
    try:
        manifest_path.relative_to(root)
    except ValueError:
        return ReleaseAuditResult({}, ("release manifest is outside the package",))
    if not manifest_path.is_file():
        return ReleaseAuditResult(
            {}, (f"release manifest does not exist: {manifest_path}",)
        )

    try:
        policy = load_json_object(policy_path)
        manifest = load_json_object(manifest_path)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return ReleaseAuditResult({}, (f"cannot load release metadata: {exc}",))

    if policy.get("format") != "triaevum_public_release_policy_v1":
        reject("unsupported release policy format")
    if manifest.get("format") != policy.get("manifest_format"):
        reject("unsupported public release manifest format")

    release = manifest.get("release")
    if not isinstance(release, dict):
        reject("release metadata is missing")
        release = {}
    precompiled = release.get("distribution_model") == MODEL
    if release.get("contains_title_code") is not precompiled:
        reject("release.contains_title_code must accurately declare the distribution model")
    for field in (
        "contains_title_content",
        "proprietary_sdk_included",
    ):
        if release.get(field) is not False:
            reject(f"release.{field} must be false")
    if release.get("redistributable") is not True:
        reject("release.redistributable must be true")

    declared_items = manifest.get("files")
    if not isinstance(declared_items, list):
        reject("release manifest files must be an array")
        declared_items = []

    allowed_roles = policy.get("allowed_roles")
    if not isinstance(allowed_roles, dict):
        reject("release policy allowed_roles is malformed")
        allowed_roles = {}
    forbidden_names = {
        str(item).lower() for item in policy.get("forbidden_basenames", [])
    }
    forbidden_extensions = {
        str(item).lower() for item in policy.get("forbidden_extensions", [])
    }
    forbidden_components = {
        str(item).lower() for item in policy.get("forbidden_path_components", [])
    }
    forbidden_hashes = {
        str(item).lower() for item in policy.get("forbidden_sha256", [])
    }
    extension_role_exceptions = {
        str(extension).lower(): {str(role) for role in roles}
        for extension, roles in policy.get(
            "forbidden_extension_role_exceptions", {}
        ).items()
        if isinstance(roles, list)
    }
    source_forbidden_prefixes = tuple(
        str(item).lower()
        for item in policy.get("source_archive_forbidden_prefixes", [])
    )

    declared: dict[str, dict[str, Any]] = {}
    roles_seen: set[str] = set()
    for index, item in enumerate(declared_items):
        if not isinstance(item, dict):
            reject(f"files[{index}] is not an object")
            continue
        try:
            relative = normalize_relative_path(str(item.get("path", "")))
        except ValueError as exc:
            reject(str(exc))
            continue
        if relative == DEFAULT_MANIFEST_NAME:
            reject("the release manifest cannot inventory itself")
            continue
        if relative in declared:
            reject(f"duplicate release path: {relative}")
            continue
        role = str(item.get("role", ""))
        patterns = allowed_roles.get(role)
        if not isinstance(patterns, list):
            reject(f"unknown release role for {relative}: {role}")
        elif not _matches_any(relative, (str(pattern) for pattern in patterns)):
            reject(f"path {relative} is not allowed for role {role}")
        roles_seen.add(role)
        declared[relative] = item

    manifest_relative = manifest_path.relative_to(root).as_posix()
    contract = release.get("runtime_contract")
    if contract is not None:
        try:
            validate_product_info(contract["product"], release.get("source_commit", ""))
            if contract["product"].get("private_title_loaded") is not False:
                reject("runtime contract identifies a private title plugin")
            if contract.get("runtime_sha256") != declared.get("TriAevum.exe", {}).get("sha256"):
                reject("runtime contract hash differs from packaged executable")
        except (KeyError, TypeError, AttributeError, ValueError) as exc:
            reject(f"invalid runtime contract: {exc}")
    actual: dict[str, Path] = {}
    for path in root.rglob("*"):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            reject(f"symbolic links are forbidden: {relative}")
            continue
        if path.is_file() and relative != manifest_relative:
            actual[relative] = path

    undeclared = sorted(set(actual) - set(declared))
    missing = sorted(set(declared) - set(actual))
    for relative in undeclared:
        reject(f"undeclared file in public package: {relative}")
    for relative in missing:
        reject(f"declared file is missing: {relative}")

    true_fields = {str(item) for item in policy.get("forbidden_json_true_fields", [])}
    false_fields = {str(item) for item in policy.get("forbidden_json_false_fields", [])}

    total_bytes = 0
    verified_files = 0
    for relative in sorted(set(actual) & set(declared)):
        path = actual[relative]
        item = declared[relative]
        stat = path.stat()
        total_bytes += stat.st_size
        expected_bytes = item.get("bytes")
        if not isinstance(expected_bytes, int) or expected_bytes != stat.st_size:
            reject(f"byte count mismatch: {relative}")
        digest = sha256_file(path)
        if str(item.get("sha256", "")).lower() != digest:
            reject(f"SHA-256 mismatch: {relative}")
        if digest in forbidden_hashes:
            reject(f"known private artifact hash: {relative}")

        pure = Path(relative)
        role = str(item.get("role", ""))
        package_forbidden_extensions = {
            extension
            for extension in forbidden_extensions
            if role not in extension_role_exceptions.get(extension, set())
        }
        for error in _path_policy_errors(
            relative,
            forbidden_names=forbidden_names,
            forbidden_extensions=package_forbidden_extensions,
            forbidden_components=forbidden_components,
        ):
            reject(f"public package {error}")

        if role in ("corresponding_source", "translated_title_source", "title_build_source"):
            if contract is not None and role == "corresponding_source":
                try:
                    with zipfile.ZipFile(path) as source:
                        metadata = json.loads(source.read("SOURCE_ARCHIVE_MANIFEST.json"))
                    if metadata.get("source_commit") != release.get("source_commit"):
                        reject("corresponding source commit differs from runtime")
                except (OSError, KeyError, ValueError, zipfile.BadZipFile) as exc:
                    reject(f"cannot verify corresponding source identity: {exc}")
            for error in _source_archive_errors(
                path,
                forbidden_names=forbidden_names,
                forbidden_extensions=forbidden_extensions,
                forbidden_components=forbidden_components,
                forbidden_hashes=forbidden_hashes,
                forbidden_prefixes=source_forbidden_prefixes,
                source_policy=policy if role == "corresponding_source" else None,
            ):
                reject(error)

        if pure.suffix.lower() == ".json" and stat.st_size <= 16 * 1024 * 1024:
            try:
                payload = json.loads(path.read_text(encoding="utf-8-sig"))
                for violation in _json_policy_violations(
                    payload, true_fields=true_fields, false_fields=false_fields
                ):
                    reject(f"JSON policy violation in {relative}: {violation}")
            except (OSError, UnicodeError, json.JSONDecodeError) as exc:
                reject(f"invalid JSON release file {relative}: {exc}")
        verified_files += 1

    for required in policy.get("required_paths", []):
        if str(required) not in actual:
            reject(f"required release file is missing: {required}")
    for role in policy.get("required_roles", []):
        if str(role) not in roles_seen:
            reject(f"required release role is missing: {role}")

    title_roles = {"precompiled_catalog", "precompiled_title", "translated_title_source", "title_build_source"}
    adapter_paths = set()
    if precompiled:
        for role in title_roles - roles_seen:
            reject(f"precompiled release is missing role: {role}")
        if roles_seen & {"forge_tool", "forge_toolchain_setup", "forge_link_library", "forge_tool_resource", "forge_clang_header"}:
            reject("user precompiled release must not include compiler/SDK acquisition payloads")
        try:
            catalog = load_catalog(root)
            recipes = load_json_object(root / "recipes/oot3d.json")["recipes"]
            expected_title_paths = {CATALOG}
            for title in catalog["titles"]:
                matches = [recipe for recipe in recipes if recipe["id"] == title["recipe"]]
                if len(matches) != 1:
                    raise ValueError("Catalog revision is absent or ambiguous in supported recipes")
                select_title(root, matches[0], catalog=catalog)
                adapter = matches[0].get("input_adapter")
                if adapter is not None:
                    validate_adapter(root, matches[0])
                    relative = adapter["code_copies"]["path"]
                    if declared.get(relative, {}).get("role") != "input_copy_adapter":
                        raise ValueError("COPY adapter lacks its explicit release role")
                    adapter_paths.add(relative)
                if declared.get(title["plugin"]["path"], {}).get("role") != "precompiled_title":
                    raise ValueError("Title DLL lacks its explicit precompiled role")
                expected_title_paths.add(title["plugin"]["path"])
                sources = title.get("sources", [])
                if len(sources) != 2:
                    raise ValueError("Title requires translated and build corresponding source")
                for source_record, role in zip(sources, ("translated_title_source", "title_build_source")):
                    archive = checked_file(root, source_record)
                    if declared.get(source_record["path"], {}).get("role") != role:
                        raise ValueError("Title source lacks its explicit role")
                    expected_title_paths.add(source_record["path"])
                    with zipfile.ZipFile(archive) as source_zip:
                        metadata = json.loads(source_zip.read("TITLE_SOURCE_MANIFEST.json" if role == "translated_title_source" else "SOURCE_ARCHIVE_MANIFEST.json"))
                        if metadata.get("build_source_commit" if role == "translated_title_source" else "source_commit") != title["build_source_commit"]:
                            raise ValueError("Title build source identity mismatch")
                        if role == "translated_title_source":
                            if (metadata["build"]["plugin_sha256"] != title["plugin"]["sha256"]
                                    or metadata["build"]["code_sha256"] != title["inputs"]["code"]["sha256"]):
                                raise ValueError("Translated source does not correspond to title DLL/revision")
                            for name, digest in metadata["files"].items():
                                if hashlib.sha256(source_zip.read(name)).hexdigest() != digest:
                                    raise ValueError("Translated source inventory mismatch")
            if {name for name, item in declared.items() if item["role"] in title_roles} != expected_title_paths:
                raise ValueError("Uncatalogued title artifact in release")
        except (OSError, KeyError, TypeError, ValueError, zipfile.BadZipFile) as exc:
            reject(f"invalid precompiled release: {exc}")
    elif roles_seen & title_roles:
        reject("title artifacts require the precompiled distribution model")
    if {name for name, item in declared.items() if item["role"] == "input_copy_adapter"} != adapter_paths:
        reject("Uncatalogued COPY adapter in release")

    # Only this explicit field may claim title code. Other embedded metadata
    # remains subject to the normal no-ROM/no-assets policy.
    policy_manifest = {**manifest, "release": {**release, "contains_title_code": False}}
    manifest_violations = _json_policy_violations(
        policy_manifest, true_fields=true_fields, false_fields=false_fields
    )
    for violation in manifest_violations:
        reject(f"release manifest policy violation: {violation}")

    summary = {
        "package": str(root),
        "manifest": manifest_relative,
        "declared_files": len(declared),
        "verified_files": verified_files,
        "bytes": total_bytes,
        "roles": sorted(roles_seen),
    }
    return ReleaseAuditResult(summary, tuple(errors))


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--policy", type=Path, default=DEFAULT_POLICY)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    result = audit_release(
        args.package, manifest_path=args.manifest, policy_path=args.policy
    )
    payload = {"ok": result.ok, "summary": result.summary, "errors": result.errors}
    if args.json:
        print(json.dumps(payload, indent=2))
    elif result.ok:
        print(
            "TriAevum public release audit passed: "
            f"{result.summary['verified_files']} allowlisted files, "
            f"{result.summary['bytes']} bytes"
        )
    else:
        print("TriAevum public release audit failed:")
        for error in result.errors:
            print(f"  - {error}")
    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
