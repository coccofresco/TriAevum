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
    from .precompiled_titles import MODEL, CATALOG, load_catalog, validate_title, checked_file
    from .release_platform import MACOS, WINDOWS, for_target, catalog_platform
    from .macos_macho_audit import bundle_errors as macos_bundle_errors
    from .macos_notices import notice_errors as macos_notice_errors
    from .macos_private_content import package_errors as macos_private_errors
    from .platform_policy import forge_profile_name, resolve_policy
    from .common import (RELEASE_MANIFEST_PATHS, load_json_object, normalize_relative_path,
                         release_manifest_path, sha256_file)
    from .input_adapters import validate_adapter
    from .source_contracts import source_contract_errors
    from .shader_corpus_layout import validate_bundled_corpus
    from .precomputed_tables import validate_precomputed_tables
    from .qualified_input_coverage import require_coverage
    from .vulkan_imports import vulkan_import_errors
    from . import release_signature
except ImportError:
    from product_contract import validate_product_info
    from precompiled_titles import MODEL, CATALOG, load_catalog, validate_title, checked_file
    from release_platform import MACOS, WINDOWS, for_target, catalog_platform
    from macos_macho_audit import bundle_errors as macos_bundle_errors
    from macos_notices import notice_errors as macos_notice_errors
    from macos_private_content import package_errors as macos_private_errors
    from platform_policy import forge_profile_name, resolve_policy
    from common import (RELEASE_MANIFEST_PATHS, load_json_object, normalize_relative_path,
                        release_manifest_path, sha256_file)
    from input_adapters import validate_adapter
    from source_contracts import source_contract_errors
    from shader_corpus_layout import validate_bundled_corpus
    from precomputed_tables import validate_precomputed_tables
    from qualified_input_coverage import require_coverage
    from vulkan_imports import vulkan_import_errors
    import release_signature


ROOT = Path(__file__).resolve().parent
DEFAULT_POLICY = ROOT / "release_policy.json"
DEFAULT_MANIFEST_NAME = "release-manifest.json"
_ABSOLUTE_HOST_PATH = re.compile(r"^(?:[A-Za-z]:[\\/]|/(?:home|Users|i|c|mnt)/)")
# The Rust Forge profile: the publisher's build receipt binds the packaged
# binaries to the corresponding source's lockfile, toolchain and notices.
RUST_FORGE_RECEIPT_FORMAT = "triaevum_rust_forge_build_v1"
RUST_FORGE_SOURCE_LOCK = "tools/triaevum_forge/Cargo.lock"
RUST_FORGE_SOURCE_TOOLCHAIN = "tools/triaevum_forge/rust-toolchain.toml"
RUST_FORGE_NOTICES = "LICENSES/Rust-Forge-crates.md"


def release_signature_path(manifest_path: Path) -> Path:
    """The publisher signature beside a release manifest (release_signature.py)."""
    return release_signature.signature_path(manifest_path)


def _signature_errors(manifest_path: Path, signature: Path, keys_path: Path) -> Iterable[str]:
    try:
        errors = release_signature.signature_errors(
            manifest_path.read_bytes(), signature.read_bytes(), release_signature.load_keys(keys_path))
    except (OSError, ValueError) as exc:
        errors = [f"cannot verify the release signature: {exc}"]
    yield from errors


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


def _binary_marker(path: Path, markers: Sequence[bytes]) -> bytes | None:
    """The first of `markers` found in the file, read in overlapping chunks."""
    if not markers:
        return None
    overlap = max(len(marker) for marker in markers) - 1
    tail = b""
    with path.open("rb") as stream:
        while chunk := stream.read(8 * 1024 * 1024):
            window = tail + chunk
            for marker in markers:
                if marker in window:
                    return marker
            tail = window[-overlap:] if overlap > 0 else b""
    return None


def _binary_pattern(path: Path, patterns: Sequence[re.Pattern[bytes]]) -> str | None:
    """The first text matching one of `patterns`, read in overlapping chunks."""
    if not patterns:
        return None
    overlap = 512
    tail = b""
    with path.open("rb") as stream:
        while chunk := stream.read(8 * 1024 * 1024):
            window = tail + chunk
            for pattern in patterns:
                match = pattern.search(window)
                if match is not None:
                    return match.group(0).decode("latin-1")
            tail = window[-overlap:]
    return None


def build_machine_path(path: Path, policy: dict[str, Any]) -> str | None:
    """A build-machine path embedded in a Rust Forge binary (the rust profile's
    markers), for publisher steps outside a desktop package audit."""
    markers = policy.get("forge_profiles", {}).get("rust", {}).get("binary_host_path_markers", [])
    return _binary_pattern(path, [re.compile(str(marker).encode("ascii")) for marker in markers])


def _rust_forge_errors(root: Path, declared: dict[str, dict[str, Any]], release: dict[str, Any],
                       target: str, binary_roles: set[str]) -> Iterable[str]:
    receipts = [path for path, item in declared.items() if item.get("role") == "forge_build_receipt"]
    if len(receipts) != 1:
        yield "Rust Forge package needs exactly one forge_build_receipt"
        return
    try:
        receipt = load_json_object(root / receipts[0])
    except (OSError, ValueError) as exc:
        yield f"cannot read the Rust Forge build receipt: {exc}"
        return
    if receipt.get("format") != RUST_FORGE_RECEIPT_FORMAT:
        yield "unsupported Rust Forge build receipt format"
    if receipt.get("target") != target:
        yield "Rust Forge build receipt target differs from the release target"
    if release.get("source_commit") and receipt.get("source_commit") != release.get("source_commit"):
        yield "Rust Forge build receipt source commit differs from the release"
    expected = {path: item for path, item in declared.items() if item.get("role") in binary_roles}
    binaries = receipt.get("binaries")
    recorded = {}
    for entry in binaries if isinstance(binaries, list) else []:
        if isinstance(entry, dict) and isinstance(entry.get("path"), str):
            recorded[entry["path"]] = entry
    if not isinstance(binaries, list) or len(recorded) != len(binaries):
        yield "Rust Forge build receipt binaries are malformed"
    if set(recorded) != set(expected):
        yield (f"Rust Forge build receipt names {sorted(recorded)}, "
               f"the package declares Forge binaries {sorted(expected)}")
    for path in sorted(set(recorded) & set(expected)):
        entry, item = recorded[path], expected[path]
        if (entry.get("role"), entry.get("bytes"), str(entry.get("sha256", "")).lower()) != (
                item.get("role"), item.get("bytes"), str(item.get("sha256", "")).lower()):
            yield f"Rust Forge build receipt does not match the packaged binary: {path}"
    notices = declared.get(RUST_FORGE_NOTICES, {})
    if receipt.get("notices_sha256") != notices.get("sha256"):
        yield "Rust Forge build receipt does not bind the packaged crate notices"
    sources = [path for path, item in declared.items() if item.get("role") == "corresponding_source"]
    if len(sources) != 1:
        yield "Rust Forge package needs exactly one corresponding source archive"
        return
    try:
        with zipfile.ZipFile(root / sources[0]) as source:
            lock = source.read(RUST_FORGE_SOURCE_LOCK)
            toolchain = source.read(RUST_FORGE_SOURCE_TOOLCHAIN).decode("utf-8")
            source_notices = source.read(RUST_FORGE_NOTICES)
    except (OSError, KeyError, UnicodeError, zipfile.BadZipFile) as exc:
        yield f"cannot verify the Rust Forge corresponding source: {exc}"
        return
    if receipt.get("cargo_lock_sha256") != hashlib.sha256(lock).hexdigest():
        yield "Rust Forge was not built from the corresponding source's Cargo.lock"
    channel = re.search(r'^\s*channel\s*=\s*"([^"]+)"', toolchain, re.MULTILINE)
    if channel is None or receipt.get("toolchain") != channel[1]:
        yield "Rust Forge toolchain differs from the corresponding source's rust-toolchain.toml"
    if hashlib.sha256(source_notices).hexdigest() != notices.get("sha256"):
        yield "packaged Rust crate notices differ from the corresponding source"


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


REPO_ROOT = ROOT.parents[1]


def title_package_composition(manifest: dict, repo_root: Path = REPO_ROOT) -> tuple[str, dict] | None:
    """The package's title composition (titles/<title>/forge/package-composition.json,
    C-DISTRO-12), keyed by the release's single revision recipe recipes/<title>.json.
    None when the title has no composition (OoT3D): its audit is unchanged."""
    files = manifest.get("files") if isinstance(manifest.get("files"), list) else []
    recipes = [str(item.get("path", "")) for item in files
               if isinstance(item, dict) and item.get("role") == "revision_recipe"]
    if len(recipes) != 1 or not re.fullmatch(r"recipes/[a-z0-9]+[.]json", recipes[0]):
        return None
    title = recipes[0][len("recipes/"):-len(".json")]
    path = repo_root / "titles" / title / "forge" / "package-composition.json"
    if not path.is_file():
        return None
    composition = load_json_object(path)
    if composition.get("format") != "triaevum_title_package_composition_v1" or composition.get("title") != title:
        raise ValueError(f"invalid {title} package composition")
    return title, composition


def _title_composition_errors(root: Path, title: str, composition: dict,
                              files: Iterable[str], target: str = WINDOWS.target) -> Iterable[str]:
    """`require` holds for every target; `require_by_target` adds the files a
    release target ships under its own name (the Windows iMMERSE DLL)."""
    present = set(files)
    for relative in sorted(present):
        if _matches_any(relative, composition.get("refuse", [])):
            yield f"{title} package must not contain {relative}"
    by_target = composition.get("require_by_target", {})
    if not isinstance(by_target, dict):
        raise ValueError(f"invalid {title} package composition require_by_target")
    for required in list(composition.get("require", [])) + list(by_target.get(target, [])):
        if required not in present:
            yield f"{title} package is missing {required}"
    heading = composition.get("readme_heading")
    if heading:
        try:
            first = (root / "README.md").read_text(encoding="utf-8").splitlines()[0].strip()
        except (OSError, UnicodeDecodeError, IndexError):
            first = ""
        if first != heading:
            yield f"{title} package README.md is not the title's README (first line: {first[:60]})"


def revision_recipe_path(declared: dict) -> str:
    """The release's own recipe catalog: Forge is per title (owner decision
    2026-11-08), so a release holds exactly one revision_recipe
    (recipes/<title>.json), whichever title the release prepares."""
    paths = [path for path, item in declared.items() if item.get("role") == "revision_recipe"]
    if len(paths) != 1:
        raise ValueError(f"release must declare exactly one revision_recipe, found {len(paths)}")
    return paths[0]


def audit_release(
    package_root: Path,
    *,
    manifest_path: Path | None = None,
    policy_path: Path = DEFAULT_POLICY,
    signing_keys: Path = release_signature.KEYS,
) -> ReleaseAuditResult:
    errors: list[str] = []
    root = package_root.resolve()
    manifest_path = (manifest_path or release_manifest_path(root)).resolve()

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
    try:
        platform = for_target(release.get("target", WINDOWS.target))
        policy = resolve_policy(policy, platform.target, forge_profile_name(policy, release))
    except ValueError as exc:
        return ReleaseAuditResult({}, (str(exc),))
    try:
        title_composition = title_package_composition(manifest)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return ReleaseAuditResult({}, (str(exc),))
    if title_composition is not None:
        # The shared required paths the title refuses (OoT3D's TopScreen files) do not apply.
        refused = title_composition[1].get("refuse", [])
        policy = {**policy, "required_paths": [path for path in policy.get("required_paths", [])
                                               if not _matches_any(str(path), refused)]}
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
    # A role whose every file must be one of the listed exact artifacts (the
    # third-party assets shipped with permission).
    pinned_role_hashes = {
        str(role): {str(digest).lower() for digest in digests}
        for role, digests in policy.get("pinned_role_sha256", {}).items()
        if isinstance(digests, list)
    }
    source_forbidden_prefixes = tuple(
        str(item).lower()
        for item in policy.get("source_archive_forbidden_prefixes", [])
    )

    signature_path = release_signature_path(manifest_path)
    signature_relative = signature_path.relative_to(root).as_posix()
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
        if relative in RELEASE_MANIFEST_PATHS:
            reject("the release manifest cannot inventory itself")
            continue
        if relative == signature_relative:
            reject("the release manifest cannot inventory its signature")
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

    # A separately licensed module (the iMMERSE library, the DLSS module and
    # NVIDIA's DLSS runtime) ships under its own role, never as an ordinary
    # runtime library.
    separate_modules = [str(name) for name in policy.get("separately_licensed_modules", [])]
    for relative, item in declared.items():
        if (_matches_any(relative.rsplit("/", 1)[-1], separate_modules)
                and item.get("role") != "separately_licensed_module"):
            reject(f"{relative} is separately licensed; declare it as separately_licensed_module")
    # Proprietary SDK code stays inside its separately licensed module: no
    # other binary may contain its marker.
    sdk_policy = policy.get("proprietary_sdk_markers", {})
    sdk_markers = [str(marker).encode("ascii") for marker in sdk_policy.get("markers", [])]
    sdk_roles = {str(role) for role in sdk_policy.get("scanned_roles", [])}
    # A library loaded only on demand (LoadLibrary) whose load may fail on an
    # older Vulkan loader without ending the process: its provider is then
    # simply not offered. Everything loaded at startup stays checked.
    vulkan_exempt_roles = {str(role) for role in policy.get("vulkan_import_exempt_roles", [])}
    # The Forge profile's own exclusions (no frozen Python payload beside a
    # Rust Forge) and its binaries' scan for build-machine paths.
    forbidden_package_patterns = [str(pattern) for pattern in policy.get("forbidden_package_patterns", [])]
    for role in sorted(roles_seen & {str(role) for role in policy.get("forbidden_roles", [])}):
        reject(f"role {role} is not allowed in a package with the {policy['forge_profile']} Forge profile")
    rust_binary_roles = {str(role) for role in policy.get("rust_forge_binary_roles", [])}
    try:
        host_path_markers = [re.compile(str(marker).encode("ascii"))
                             for marker in policy.get("binary_host_path_markers", [])]
    except (re.error, UnicodeError) as exc:
        return ReleaseAuditResult({}, (f"release policy binary_host_path_markers is malformed: {exc}",))

    manifest_relative = manifest_path.relative_to(root).as_posix()
    contract = release.get("runtime_contract")
    if contract is not None:
        try:
            validate_product_info(contract["product"], release.get("source_commit", ""))
            if contract["product"].get("private_title_loaded") is not False:
                reject("runtime contract identifies a private title plugin")
            if contract.get("runtime_sha256") != declared.get(platform.runtime, {}).get("sha256"):
                reject("runtime contract hash differs from packaged executable")
            # Global illumination is experimental: a public runtime neither
            # offers it nor selects it by default.
            if contract["product"]["capabilities"].get("experimental_global_illumination") is not False:
                reject("runtime contract does not exclude the experimental global illumination")
            # Developer tools (warp screen, warp points) never ship.
            if contract["product"]["capabilities"].get("developer_tools") is not False:
                reject("runtime contract does not exclude the developer tools")
            graphics = contract["product"]["default_config"]["Graphics"]
            if graphics.get("Effects", {}).get("GlobalIllumination", {}).get("Mode", "Off") != "Off":
                reject("runtime default configuration enables global illumination")
        except (KeyError, TypeError, AttributeError, ValueError) as exc:
            reject(f"invalid runtime contract: {exc}")
    actual: dict[str, Path] = {}
    for path in root.rglob("*"):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            reject(f"symbolic links are forbidden: {relative}")
            continue
        if path.is_file() and relative not in (manifest_relative, signature_relative):
            actual[relative] = path

    undeclared = sorted(set(actual) - set(declared))
    missing = sorted(set(declared) - set(actual))
    for relative in undeclared:
        reject(f"undeclared file in public package: {relative}")
    for relative in missing:
        reject(f"declared file is missing: {relative}")
    for relative in sorted(set(actual) | set(declared)):
        if _matches_any(relative, forbidden_package_patterns):
            reject(f"{relative} is forbidden in a package with the {policy['forge_profile']} Forge profile")

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
        if role in pinned_role_hashes and digest not in pinned_role_hashes[role]:
            reject(f"{relative} is not a pinned {role} artifact")
        # A Windows binary that imports a Vulkan command an older loader lacks
        # cannot start there at all (0xC0000139).
        if (platform.target == WINDOWS.target and pure.suffix.lower() in (".exe", ".dll")
                and role not in vulkan_exempt_roles):
            for error in vulkan_import_errors(path, relative, require_pe=False):
                reject(error)
        if role in sdk_roles:
            marker = _binary_marker(path, sdk_markers)
            if marker is not None:
                reject(f"{relative} contains proprietary SDK code ({marker.decode('ascii')}); "
                       "it belongs only in its separately licensed module")
        if role in rust_binary_roles and platform != MACOS:
            # The Mac bundle gate scans all native roles and outer Frameworks,
            # reporting only their package names, never a matched home path.
            leaked = _binary_pattern(path, host_path_markers)
            if leaked is not None:
                reject(f"{relative} embeds a build-machine path ({leaked[:80]}); "
                       "build it with the publisher's path remapping")
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

    # The publisher signature is optional for the audit (candidates are
    # unsigned); when present it must verify under an embedded Forge key.
    if signature_path.is_file():
        for error in _signature_errors(manifest_path, signature_path, signing_keys):
            reject(error)

    for required in policy.get("required_paths", []):
        if str(required) not in actual:
            reject(f"required release file is missing: {required}")
    for role in policy.get("required_roles", []):
        if str(role) not in roles_seen:
            reject(f"required release role is missing: {role}")
    if rust_binary_roles:
        for error in _rust_forge_errors(root, declared, release, platform.target, rust_binary_roles):
            reject(error)

    title_roles = {"precompiled_catalog", "precompiled_title", "translated_title_source", "title_build_source"}
    adapter_paths = set()
    macos_corpus_paths = set()
    if precompiled:
        for role in title_roles - roles_seen:
            reject(f"precompiled release is missing role: {role}")
        if roles_seen & {"forge_tool", "forge_toolchain_setup", "forge_link_library", "forge_tool_resource", "forge_clang_header"}:
            reject("user precompiled release must not include compiler/SDK acquisition payloads")
        try:
            catalog = load_catalog(root)
            validate_bundled_corpus(root, catalog, declared)
            if platform == MACOS:
                macos_corpus_paths = {path for path, item in declared.items()
                                      if item["role"] == "portable_shader_corpus"}
            validate_precomputed_tables(root, catalog, declared)
            if catalog_platform(catalog) != platform:
                raise ValueError("Catalog target differs from release target")
            recipes = load_json_object(root / revision_recipe_path(declared))["recipes"]
            require_coverage(catalog, {"recipes": recipes})
            expected_title_paths = {CATALOG}
            for title in catalog["titles"]:
                matches = [recipe for recipe in recipes if recipe["id"] == title["recipe"]]
                if len(matches) != 1:
                    raise ValueError("Catalog revision is absent or ambiguous in supported recipes")
                validate_title(root, matches[0], catalog=catalog)
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
                            # A profile-guided module is published only with the
                            # profile it was built with.
                            profiled = metadata["build"].get("profile_data_sha256")
                            profile = metadata.get("profile")
                            if profiled is not None:
                                if (not isinstance(profile, dict) or profile.get("path") != "title.profdata"
                                        or profile.get("sha256") != profiled):
                                    raise ValueError("Profile-guided title lacks its profile in the translated source")
                                if hashlib.sha256(source_zip.read(profile["path"])).hexdigest() != profiled:
                                    raise ValueError("Translated source profile mismatch")
                            elif profile is not None:
                                raise ValueError("Translated source carries a profile for a title built without one")
            if {name for name, item in declared.items() if item["role"] in title_roles} != expected_title_paths:
                raise ValueError("Uncatalogued title artifact in release")
        except (OSError, KeyError, TypeError, ValueError, zipfile.BadZipFile) as exc:
            reject(f"invalid precompiled release: {exc}")
    elif roles_seen & title_roles:
        reject("title artifacts require the precompiled distribution model")
    if not precompiled and roles_seen & {"portable_shader_corpus", "portable_pipeline_recipes",
                                         "portable_shader_qualification"}:
        reject("bundled shader artifacts require a validated precompiled catalog")
    if not precompiled and roles_seen & {"precomputed_interior_sun", "precomputed_scene_lights"}:
        reject("precomputed tables require a validated precompiled catalog")
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

    if title_composition is not None:
        for error in _title_composition_errors(root, *title_composition, list(actual) + list(declared),
                                               platform.target):
            reject(error)

    if platform == MACOS:
        for error in macos_notice_errors(lambda name: (root / name).read_bytes()):
            reject(error)
        for error in macos_private_errors(root, catalogued_corpus_paths=macos_corpus_paths):
            reject(error)
        for error in macos_bundle_errors(root, declared, release):
            reject(error)

    summary = {
        "package": str(root),
        "target": platform.target,
        "forge_profile": policy["forge_profile"],
        "manifest": manifest_relative,
        "declared_files": len(declared),
        "verified_files": verified_files,
        "bytes": total_bytes,
        "roles": sorted(roles_seen),
        "signed": signature_path.is_file(),
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
