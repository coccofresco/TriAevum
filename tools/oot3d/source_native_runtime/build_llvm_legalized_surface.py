#!/usr/bin/env python3
"""Compile the lowered source surface through pointer32 IR legalization."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import Counter
import functools
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from source_dependency_fingerprint import SourceDependencyFingerprinter


HOST_COMPILE_CONTRACT = (
    "-std=gnu11",
    "-DOOT3D_HOST_CTR_CONNECT_TO_PORT=1",
    "-DOOT3D_HOST_CTR_RESOURCE_LIMIT_QUERY=1",
    "-DOOT3D_HOST_TARGET_SYSTEM_TICK=1",
    "-DOOT3D_HOST_TARGET_THREAD_POINTER=1",
    "-DOOT3D_HOST_OWNER_ACTOR_GLOBAL_RESOLVER=1",
    "-DOOT3D_HOST_SYSTEM_ARENA_THREAD_RUNTIME=1",
    "-DglDepthFunc=oot3d_guest_glDepthFunc",
    "-DglEnable=oot3d_guest_glEnable",
    "-DglFrontFace=oot3d_guest_glFrontFace",
    "-DglViewport=oot3d_guest_glViewport",
    "-O2",
)
MSVC_COMPATIBILITY_CONTRACT = (
    "isnan-builtin-prelude;rename-guest-scalbn;clang-asm-labels-v3"
)
MSVC_GUEST_LIBC_REWRITE_CONTRACT = "guest-libc-source-abi-v4"
SOURCE_CACHE_CONTRACT = "llvm-legalized-source-cache-v4-behavioral-legalizer"
ARCHIVE_CACHE_CONTRACT = "llvm-legalized-archive-cache-v2"
REQUIRED_TARGET_WORD_LOWERING_CONTRACT = (
    "target-word-lowering-v5-manifested-abi"
)
GUEST_LIBC_SYMBOLS = (
    "mbstowcs", "memset", "scalbn", "sprintf", "strcmp", "wcscpy", "wcslen",
)


def mapped_ranges(process_manifest: dict) -> list[tuple[int, int]]:
    process = process_manifest["process"]
    regions = [*process["segments"], *process["system_regions"]]
    for name in ("heap", "linear_heap"):
        region = process.get(name)
        if region:
            regions.append({
                "address": region["base_address"],
                "mapped_size": region["size"],
            })
    return [
        (region["address"], region["address"] + region["mapped_size"])
        for region in regions
    ]


def run(command: list[str]) -> None:
    result = subprocess.run(command, capture_output=True, text=True, errors="replace")
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"{result.stdout[-2000:]}\n{result.stderr[-6000:]}"
        )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


@functools.lru_cache(maxsize=None)
def legalizer_behavior_contract(legalizer: Path) -> str:
    """Fingerprint pass behavior without unstable PE linker metadata."""
    pass_root = Path(__file__).resolve().parent / "llvm_pass"
    source = pass_root / "Oot3dGuestStorageLegalizer.cpp"
    build_contract = pass_root / "CMakeLists.txt"
    probe = pass_root / "guest_storage_contract_probe.ll"
    with tempfile.TemporaryDirectory(prefix="oot3d-legalizer-contract-") as temporary:
        output = Path(temporary) / "probe.bc"
        run([str(legalizer.resolve()), str(probe), "--strict", "-o", str(output)])
        digest = hashlib.sha256()
        for path in (source, build_contract, probe, output):
            digest.update(path.name.encode("utf-8"))
            digest.update(sha256_file(path).encode("ascii"))
        return digest.hexdigest()


def include_tree_digest(include_root: Path) -> str:
    digest = hashlib.sha256()
    for header in sorted(include_root.rglob("*.h")):
        digest.update(header.relative_to(include_root).as_posix().encode("utf-8"))
        digest.update(sha256_file(header).encode("ascii"))
    return digest.hexdigest()


def build_input_fingerprint(args: argparse.Namespace, source_root: Path) -> str:
    """Cheap Ninja-style invalidation for an already completed whole phase."""
    digest = hashlib.sha256()
    digest.update(SOURCE_CACHE_CONTRACT.encode("ascii"))
    digest.update(ARCHIVE_CACHE_CONTRACT.encode("ascii"))
    digest.update(REQUIRED_TARGET_WORD_LOWERING_CONTRACT.encode("ascii"))
    digest.update(args.target_triple.encode("ascii"))
    for item in (*HOST_COMPILE_CONTRACT, MSVC_COMPATIBILITY_CONTRACT,
                 MSVC_GUEST_LIBC_REWRITE_CONTRACT):
        digest.update(item.encode("ascii"))
    inputs = (
        args.clang.resolve(),
        args.llvm_ar.resolve(),
        args.legalizer.resolve(),
        args.snapshot_manifest.resolve(),
        args.declaration_report.resolve(),
        args.surface_contracts.resolve(),
        args.process_manifest.resolve(),
    )
    for path in inputs:
        stat = path.stat()
        digest.update(str(path).encode("utf-8"))
        digest.update(f"{stat.st_size}:{stat.st_mtime_ns}".encode("ascii"))
    digest.update(str(source_root).encode("utf-8"))
    for path in sorted(
            (item for item in source_root.rglob("*") if item.is_file()),
            key=lambda item: item.relative_to(source_root).as_posix()):
        stat = path.stat()
        digest.update(path.relative_to(source_root).as_posix().encode("utf-8"))
        digest.update(f"{stat.st_size}:{stat.st_mtime_ns}".encode("ascii"))
    return digest.hexdigest()


def validate_lowered_source_root(source_root: Path, snapshot: dict) -> dict:
    """Reject host builds that bypass the target-width ABI lowering phase."""

    manifest_path = source_root / "lowered_snapshot_manifest.json"
    if not manifest_path.is_file():
        raise SystemExit(
            "source-native whole-AOT requires a lowered source snapshot; "
            f"missing {manifest_path}"
        )
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise SystemExit(
            f"invalid lowered source manifest {manifest_path}: {error}"
        ) from error
    if manifest.get("lowering_contract") != (
            REQUIRED_TARGET_WORD_LOWERING_CONTRACT):
        raise SystemExit(
            "lowered source contract mismatch: expected "
            f"{REQUIRED_TARGET_WORD_LOWERING_CONTRACT}, got "
            f"{manifest.get('lowering_contract')!r}"
        )
    if manifest.get("decomp_revision") != snapshot.get("decomp_revision"):
        raise SystemExit(
            "lowered source and canonical snapshot revisions differ"
        )
    expected_sources = len(snapshot.get("canonical_sources", []))
    if manifest.get("source_count") != expected_sources:
        raise SystemExit(
            "lowered source manifest has an incomplete canonical surface: "
            f"expected {expected_sources}, got {manifest.get('source_count')!r}"
        )
    return manifest


def toolchain_contract(clang: Path, legalizer: Path,
                       target_triple: str) -> str:
    parts = [
        SOURCE_CACHE_CONTRACT,
        REQUIRED_TARGET_WORD_LOWERING_CONTRACT,
        sha256_file(clang.resolve()),
        legalizer_behavior_contract(legalizer.resolve()),
        target_triple,
        *HOST_COMPILE_CONTRACT,
    ]
    if target_triple == "x86_64-pc-windows-msvc":
        parts.append(MSVC_COMPATIBILITY_CONTRACT)
    return "\0".join(parts)


def legacy_shared_fingerprint(clang: Path, legalizer: Path,
                              include_root: Path,
                              target_triple: str) -> str:
    digest = hashlib.sha256()
    digest.update(sha256_file(clang.resolve()).encode("ascii"))
    digest.update(legalizer_behavior_contract(legalizer.resolve()).encode("ascii"))
    digest.update(target_triple.encode("ascii"))
    if target_triple == "x86_64-pc-windows-msvc":
        digest.update(MSVC_COMPATIBILITY_CONTRACT.encode("ascii"))
    digest.update("\0".join(HOST_COMPILE_CONTRACT).encode("ascii"))
    for header in sorted(include_root.rglob("*.h")):
        digest.update(header.relative_to(include_root).as_posix().encode("utf-8"))
        digest.update(sha256_file(header).encode("ascii"))
    return digest.hexdigest()


def finalized_source_fingerprint(base_fingerprint: str,
                                 rewrites_guest_libc: bool) -> str:
    value = base_fingerprint
    if rewrites_guest_libc:
        value += MSVC_GUEST_LIBC_REWRITE_CONTRACT
    return hashlib.sha256(value.encode("ascii")).hexdigest()


def write_source_stamp(path: Path, relative: str, fingerprint: str,
                       dependencies: list[dict], unresolved: list[str]) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps({
            "schema_version": 2,
            "source": relative,
            "fingerprint": fingerprint,
            "dependencies": dependencies,
            "unresolved_includes": unresolved,
        }, separators=(",", ":")) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    temporary.replace(path)


def write_json_atomic(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(payload, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    temporary.replace(path)


def rewrites_guest_libc(source_text: str, target_triple: str) -> bool:
    return (
        target_triple == "x86_64-pc-windows-msvc"
        and any(re.search(rf"\b{name}\b", source_text)
                for name in GUEST_LIBC_SYMBOLS)
    )


def baseline_source_matches(
        source_root: Path, baseline_root: Path, relative: str,
        dependencies: list[dict], include_trees_match: bool) -> bool:
    if not include_trees_match:
        return False
    source = source_root / relative
    baseline = baseline_root / relative
    if not baseline.is_file() or sha256_file(source) != sha256_file(baseline):
        return False
    # The include tree was compared as a whole above. Local headers beside a
    # recovered source are not in that tree, so compare those individually.
    for dependency in dependencies:
        display = dependency["path"]
        if display.startswith("include/"):
            continue
        candidate = baseline_root / display
        if (not candidate.is_file()
                or sha256_file(candidate) != dependency["sha256"]):
            return False
    return True


def current_source_cache_matches(output_root: Path, relative: str,
                                 fingerprint: str) -> bool:
    identity = hashlib.sha256(relative.encode("utf-8")).hexdigest()[:16]
    object_path = output_root / "objects" / f"{identity}.o"
    stamp = output_root / "stamps" / f"{identity}.json"
    if not object_path.is_file() or not stamp.is_file():
        return False
    try:
        return json.loads(stamp.read_text(encoding="utf-8")).get(
            "fingerprint"
        ) == fingerprint
    except (OSError, json.JSONDecodeError):
        return False


def compile_source(clang: Path, legalizer: Path, include_root: Path,
                   source_root: Path, output_root: Path,
                   base_fingerprint: str, dependencies: list[dict],
                   unresolved: list[str], legacy_fingerprint: str | None,
                   rewrites_guest_libc_symbols: bool,
                   target_triple: str, relative: str) -> tuple[str, str]:
    identity = hashlib.sha256(relative.encode("utf-8")).hexdigest()[:16]
    bitcode = output_root / "bitcode" / f"{identity}.bc"
    legalized = output_root / "legalized" / f"{identity}.bc"
    object_path = output_root / "objects" / f"{identity}.o"
    stamp = output_root / "stamps" / f"{identity}.json"
    for path in (bitcode, legalized, object_path):
        path.parent.mkdir(parents=True, exist_ok=True)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    original_source_path = source_root / relative
    fingerprint = finalized_source_fingerprint(
        base_fingerprint, rewrites_guest_libc_symbols
    )
    if object_path.exists() and stamp.exists():
        try:
            previous = json.loads(stamp.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            previous = {}
        if previous.get("fingerprint") == fingerprint:
            return str(object_path.resolve()), "cached"
        if (legacy_fingerprint is not None
                and previous.get("fingerprint") == legacy_fingerprint):
            write_source_stamp(
                stamp, relative, fingerprint, dependencies, unresolved
            )
            return str(object_path.resolve()), "migrated"
    source_text = original_source_path.read_text(encoding="utf-8")
    common = [
        str(clang),
        f"--target={target_triple}",
        f"-I{include_root}",
        f"-I{original_source_path.parent}",
        *HOST_COMPILE_CONTRACT[:-1],
        "-Wno-error=incompatible-pointer-types",
        "-Wno-error=int-conversion",
        "-w",
        HOST_COMPILE_CONTRACT[-1],
    ]
    if rewrites_guest_libc_symbols:
        # Guest libc implementations retain their recovered target ABI.  Once
        # namespaced away from the Windows CRT, callers without a local
        # declaration must not acquire an unrelated host prototype.
        common.append("-Wno-error=implicit-function-declaration")
    source_path = original_source_path
    if target_triple == "x86_64-pc-windows-msvc":
        compatible_text = source_text
        for guest_name in GUEST_LIBC_SYMBOLS:
            compatible_text = re.sub(
                rf"\b{guest_name}\b", f"oot3d_guest_{guest_name}",
                compatible_text)
        compatible_text = compatible_text.replace(
            "#if defined(__GNUC__)",
            "#if defined(__GNUC__) || defined(__clang__)")
        if compatible_text != source_text:
            source_path = output_root / "msvc_sources" / relative
            source_path.parent.mkdir(parents=True, exist_ok=True)
            source_path.write_text(compatible_text, encoding="utf-8", newline="\n")
    run([*common, "-emit-llvm", "-c", str(source_path), "-o", str(bitcode)])
    run([str(legalizer), str(bitcode), "--strict", "-o", str(legalized)])
    run([
        str(clang),
        f"--target={target_triple}",
        "-O2",
        "-mcmodel=large",
        "-c",
        str(legalized),
        "-o",
        str(object_path),
    ])
    write_source_stamp(
        stamp, relative, fingerprint, dependencies, unresolved
    )
    return str(object_path.resolve()), "rebuilt"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--llvm-ar", required=True, type=Path)
    parser.add_argument("--legalizer", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--snapshot-manifest", required=True, type=Path)
    parser.add_argument("--declaration-report", required=True, type=Path)
    parser.add_argument("--surface-contracts", required=True, type=Path)
    parser.add_argument("--process-manifest", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--baseline-source-root", type=Path)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--target-triple", default="x86_64-w64-windows-gnu",
        choices=("x86_64-w64-windows-gnu", "x86_64-pc-windows-msvc"),
    )
    args = parser.parse_args()

    snapshot = json.loads(args.snapshot_manifest.read_text(encoding="utf-8"))
    source_root = args.source_root.resolve()
    lowered_manifest = validate_lowered_source_root(source_root, snapshot)
    sources = [record["path"] for record in snapshot["canonical_sources"]]
    output = args.output_root.resolve()
    output.mkdir(parents=True, exist_ok=True)
    phase_fingerprint = build_input_fingerprint(args, source_root)
    previous_report_path = output / "legalized_surface_build.json"
    archive = output / "liboot3d_source_llvm_legalized_surface.a"
    archive_manifest_path = output / "archive_manifest.json"
    previous_report = {}
    if previous_report_path.is_file():
        try:
            previous_report = json.loads(
                previous_report_path.read_text(encoding="utf-8")
            )
        except json.JSONDecodeError:
            previous_report = {}
    if (previous_report.get("build_input_fingerprint") == phase_fingerprint
            and previous_report.get("failed_source_count") == 0
            and previous_report.get("compiled_source_count") == len(sources)
            and archive.is_file() and archive_manifest_path.is_file()):
        fast_report = dict(previous_report)
        fast_report["pipeline_fast_path"] = True
        print(json.dumps(fast_report, indent=2))
        return 0

    declarations = json.loads(args.declaration_report.read_text(encoding="utf-8"))
    surface = json.loads(args.surface_contracts.read_text(encoding="utf-8"))
    process = json.loads(args.process_manifest.read_text(encoding="utf-8"))
    external_data = {
        record["name"]: int(re.search(r"([0-9A-Fa-f]+)$", record["name"]).group(1), 16)
        for record in surface["external_symbols"]
        if record["category"] == "target_data_address"
    }
    # DAT_<address> declarations are target-memory aliases even when the
    # current semantic surface has not promoted them into its contract yet.
    for symbol in declarations["symbols"]:
        match = re.fullmatch(r"DAT_([0-9A-Fa-f]{8})", symbol["name"])
        if match:
            external_data.setdefault(symbol["name"], int(match.group(1), 16))
    ranges = mapped_ranges(process)
    declarations_by_name = {
        symbol["name"]: symbol for symbol in declarations["symbols"]
    }
    bindings = []
    for name, address in external_data.items():
        if not any(begin <= address < end for begin, end in ranges):
            continue
        declaration = declarations_by_name.get(name)
        disposition = (
            declaration["disposition"] if declaration is not None
            else "external_mapped"
        )
        bindings.append((name, address, disposition))
    bindings.sort()

    include_root = (args.source_root / "include").resolve()
    if args.target_triple == "x86_64-pc-windows-msvc":
        include_root = output / "msvc_include"
        shutil.copytree(args.source_root / "include", include_root,
                        dirs_exist_ok=True)
        prelude = include_root / "oot3d" / "target_native_prelude.h"
        prelude_text = prelude.read_text(encoding="utf-8")
        prelude_text = prelude_text.replace(
            "#define NAN(value) (_isnan((double)(value)))",
            "#define NAN(value) (__builtin_isnan((double)(value)))")
        prelude.write_text(prelude_text, encoding="utf-8", newline="\n")
    if previous_report:
        previous_count = previous_report.get("absolute_binding_count", 0)
        if (previous_report.get("decomp_revision") == snapshot["decomp_revision"]
                and previous_count > len(bindings)):
            raise SystemExit(
                "refusing to shrink absolute bindings for the same decomp revision: "
                f"{previous_count} -> {len(bindings)}; use the canonical surface contract"
            )
    assembly = output / "legalized_absolute_target_data_symbols.s"
    lines = ["# Generated for the LLVM-legalized source surface.", ".text"]
    if args.target_triple == "x86_64-pc-windows-msvc":
        lines.extend([
            ".globl __bss_start", ".set __bss_start, 0x0055a220",
            ".globl __bss_end", ".set __bss_end, 0x005c6608",
        ])
    for name, address, _ in bindings:
        lines.extend([f".globl {name}", f".set {name}, 0x{address:08x}"])
    assembly.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    assembly_object = output / "objects" / "absolute_data.o"
    assembly_object.parent.mkdir(parents=True, exist_ok=True)
    assembly_stamp = output / "stamps" / "absolute_data.json"
    assembly_stamp.parent.mkdir(parents=True, exist_ok=True)
    assembly_fingerprint = hashlib.sha256("\0".join((
        SOURCE_CACHE_CONTRACT,
        sha256_file(args.clang.resolve()),
        args.target_triple,
        sha256_file(assembly),
    )).encode("ascii")).hexdigest()
    assembly_cached = False
    if assembly_object.is_file() and assembly_stamp.is_file():
        try:
            assembly_cached = (
                json.loads(assembly_stamp.read_text(encoding="utf-8"))
                .get("fingerprint") == assembly_fingerprint
            )
        except json.JSONDecodeError:
            assembly_cached = False
    if not assembly_cached:
        run([
            str(args.clang), f"--target={args.target_triple}", "-c",
            str(assembly), "-o", str(assembly_object),
        ])
        assembly_stamp.write_text(
            json.dumps({"fingerprint": assembly_fingerprint}) + "\n",
            encoding="utf-8",
            newline="\n",
        )

    base_contract = toolchain_contract(
        args.clang.resolve(), args.legalizer.resolve(), args.target_triple
    )
    fingerprinter = SourceDependencyFingerprinter(source_root, include_root)
    baseline_root = (
        args.baseline_source_root.resolve()
        if args.baseline_source_root is not None else None
    )
    baseline_include_trees_match = False
    legacy_shared = None
    if baseline_root is not None:
        baseline_include = baseline_root / "include"
        if not baseline_include.is_dir():
            raise SystemExit(
                f"baseline include tree does not exist: {baseline_include}"
            )
        baseline_include_trees_match = (
            include_tree_digest(source_root / "include")
            == include_tree_digest(baseline_include)
        )
        if baseline_include_trees_match:
            legacy_shared = legacy_shared_fingerprint(
                args.clang.resolve(), args.legalizer.resolve(),
                include_root, args.target_triple
            )

    source_work = []
    for relative in sources:
        base_fingerprint, dependencies, unresolved = fingerprinter.fingerprint(
            source_root / relative, base_contract
        )
        source_text = (source_root / relative).read_text(encoding="utf-8")
        rewrites_guest_libc_symbols = rewrites_guest_libc(
            source_text, args.target_triple
        )
        current_fingerprint = finalized_source_fingerprint(
            base_fingerprint, rewrites_guest_libc_symbols
        )
        legacy_fingerprint = None
        if (not current_source_cache_matches(
                    output, relative, current_fingerprint
                )
                and baseline_root is not None and legacy_shared is not None
                and baseline_source_matches(
                    source_root, baseline_root, relative, dependencies,
                    baseline_include_trees_match
                )):
            legacy_payload = legacy_shared + sha256_file(source_root / relative)
            if rewrites_guest_libc_symbols:
                legacy_payload += MSVC_GUEST_LIBC_REWRITE_CONTRACT
            legacy_fingerprint = hashlib.sha256(
                legacy_payload.encode("ascii")
            ).hexdigest()
        source_work.append((
            relative, base_fingerprint, dependencies, unresolved,
            legacy_fingerprint, rewrites_guest_libc_symbols,
        ))

    objects = []
    statuses = Counter()
    failures = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
        futures = {
            executor.submit(
                compile_source,
                args.clang.resolve(),
                args.legalizer.resolve(),
                include_root,
                source_root,
                output,
                base_fingerprint,
                dependencies,
                unresolved,
                legacy_fingerprint,
                rewrites_guest_libc_symbols,
                args.target_triple,
                relative,
            ): relative
            for (relative, base_fingerprint, dependencies, unresolved,
                 legacy_fingerprint, rewrites_guest_libc_symbols) in source_work
        }
        for future in as_completed(futures):
            try:
                object_path, status = future.result()
                objects.append(object_path)
                statuses[status] += 1
            except Exception as error:
                failures.append({"source": futures[future], "error": str(error)})

    report = {
        "schema_version": 1,
        "decomp_revision": snapshot["decomp_revision"],
        "target_word_lowering_contract":
            lowered_manifest["lowering_contract"],
        "build_input_fingerprint": phase_fingerprint,
        "source_count": len(sources),
        "compiled_source_count": len(objects),
        "cached_source_count": statuses["cached"],
        "migrated_source_count": statuses["migrated"],
        "rebuilt_source_count": statuses["rebuilt"],
        "failed_source_count": len(failures),
        "assembly_cached": assembly_cached,
        "absolute_binding_count": len(bindings),
        "binding_disposition_counts": dict(sorted({
            disposition: sum(item[2] == disposition for item in bindings)
            for disposition in {item[2] for item in bindings}
        }.items())),
        "failures": failures,
    }
    write_json_atomic(output / "legalized_surface_build.json", report)
    if failures:
        print(json.dumps(report, indent=2))
        return 1

    object_paths = [assembly_object.resolve(), *(Path(path) for path in objects)]
    objects_by_member = {path.name: path for path in object_paths}
    expected_members = sorted(objects_by_member)
    member_hashes = {
        member: sha256_file(objects_by_member[member])
        for member in expected_members
    }
    archive_is_compatible = False
    previous_member_hashes = {}
    if archive.is_file() and archive_manifest_path.is_file():
        try:
            archive_manifest = json.loads(
                archive_manifest_path.read_text(encoding="utf-8")
            )
            archive_is_compatible = (
                archive_manifest.get("contract") == ARCHIVE_CACHE_CONTRACT
                and archive_manifest.get("members") == expected_members
            )
            if archive_is_compatible:
                previous_member_hashes = archive_manifest.get(
                    "member_sha256", {}
                )
        except json.JSONDecodeError:
            archive_is_compatible = False

    changed_objects = [
        str(objects_by_member[member])
        for member in expected_members
        if previous_member_hashes.get(member) != member_hashes[member]
    ]
    response = output / "archive_objects.rsp"
    if archive_is_compatible and not changed_objects:
        archive_mode = "reused"
        archive_updated_member_count = 0
    elif archive_is_compatible:
        response.write_text(
            "\n".join(changed_objects) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        run([str(args.llvm_ar), "rcs", str(archive), f"@{response}"])
        archive_mode = "incremental"
        archive_updated_member_count = len(changed_objects)
    else:
        response.write_text(
            "\n".join(
                str(objects_by_member[member]) for member in expected_members
            ) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        if archive.exists():
            archive.unlink()
        run([str(args.llvm_ar), "rcs", str(archive), f"@{response}"])
        archive_mode = "rebuilt"
        archive_updated_member_count = len(expected_members)

    write_json_atomic(archive_manifest_path, {
        "schema_version": 1,
        "contract": ARCHIVE_CACHE_CONTRACT,
        "members": expected_members,
        "member_sha256": member_hashes,
    })
    report["archive"] = str(archive)
    report["archive_mode"] = archive_mode
    report["archive_updated_member_count"] = archive_updated_member_count
    write_json_atomic(output / "legalized_surface_build.json", report)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
