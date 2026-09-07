"""Lower verified structural A32 IR directly to a native title plugin.

The backend emits LLVM IR and COFF objects without producing title-derived C
or C++ sources.  Guest blocks call a stable, title-neutral executor supplied by
the game module; native control flow and dispatch remain title-specific and are
compiled into the private plugin.
"""

from __future__ import annotations

import hashlib
import os
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence

try:
    from .bundle_paths import distribution_path
    from .common import atomic_write_json, load_json_object, sha256_file
except ImportError:
    from bundle_paths import distribution_path
    from common import atomic_write_json, load_json_object, sha256_file


A32_ROOT = distribution_path("tools/oot3d/native_a32_runtime")
UPSTREAM_SRC = A32_ROOT / "upstream/src"
if str(UPSTREAM_SRC) not in sys.path:
    sys.path.insert(0, str(UPSTREAM_SRC))

from oot3d_pack.arm_decode import DecodedInstruction, decode_arm  # noqa: E402
from oot3d_pack.arm_ir import (  # noqa: E402
    arm_core_runtime_category,
    arm_core_supported,
    arm_vfp_scalar_supported,
    arm_vfp_transport_supported,
)


FORMAT = "triaevum_direct_aot_plugin_v1"
PROGRAM_FORMAT = "oot3d_whole_aot_program_v1"
SELECTION_FORMAT = "oot3d_whole_aot_function_selection_v1"
PLUGIN_ABI = 1
PROGRAM_STRUCT_SIZE_X64 = 48
PROFILE = "x86_64-windows-llvm-direct-aot-v1"


class DirectAotError(ValueError):
    pass


@dataclass(frozen=True)
class DirectAotToolchain:
    llc: Path
    linker: Path
    target_triple: str = "x86_64-pc-windows-msvc"
    profile: str = PROFILE


@dataclass(frozen=True)
class DirectBlock:
    pc: int
    operations: tuple[tuple[int, int], ...]


@dataclass(frozen=True)
class DirectFunction:
    entry: int
    name: str
    blocks: tuple[DirectBlock, ...]


_OPCODES = {
    "MovImm": 0,
    "MovReg": 1,
    "Add": 2,
    "Sub": 3,
    "Cmp": 4,
    "Ldr32": 5,
    "Str32": 6,
    "Branch": 7,
    "BranchReg": 8,
    "Svc": 9,
    "Core": 10,
    "CoreSystem": 11,
    "CoreAlu": 12,
    "CoreMemory": 13,
    "VfpTransport": 14,
    "VfpScalar": 15,
}


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _opcode(item: DecodedInstruction) -> str:
    if item.kind == "data_processing":
        if item.opcode == "mov":
            return "MovImm" if item.imm is not None else "MovReg"
        if item.opcode == "add":
            return "Add"
        if item.opcode == "sub":
            return "Sub"
        if item.opcode == "cmp":
            return "Cmp"
    if item.kind == "memory":
        return "Ldr32" if item.load else "Str32"
    if item.kind in {"branch", "call"} and item.target is not None:
        if not item.thumb_target:
            return "Branch"
    if item.kind == "indirect_branch" and (
        item.raw & 0x0FFFFFF0
    ) in {0x012FFF10, 0x012FFF30}:
        return "BranchReg"
    if item.kind == "svc":
        return "Svc"
    if item.kind == "system" and item.mnemonic in {"mrs", "msr"}:
        return "CoreSystem"
    if item.state == "arm" and arm_core_supported(item.raw):
        return {
            "system": "CoreSystem",
            "alu": "CoreAlu",
            "memory": "CoreMemory",
        }[arm_core_runtime_category(item.raw)]
    if item.state == "arm" and arm_vfp_transport_supported(item.raw):
        return "VfpTransport"
    if item.state == "arm" and arm_vfp_scalar_supported(item.raw):
        return "VfpScalar"
    raise DirectAotError(
        f"instruction 0x{item.pc:08X} is outside the direct AOT closure"
    )


def _metadata(item: DecodedInstruction) -> int:
    flags = 0
    if item.setflags:
        flags |= 1
    if item.kind == "data_processing" and item.imm is not None:
        flags |= 2
    if item.kind == "call" or (
        item.kind == "indirect_branch"
        and (item.raw & 0x0FFFFFF0) == 0x012FFF30
    ):
        flags |= 4
    if item.kind == "memory" and item.imm is not None and item.imm < 0:
        flags |= 32
    condition = 14 if item.condition is None else (item.raw >> 28) & 0xF
    return _OPCODES[_opcode(item)] | (condition << 8) | (flags << 12)


def load_direct_program(
    program_path: Path, selection_path: Path, code_path: Path
) -> tuple[tuple[DirectFunction, ...], tuple[int, ...], tuple[int, ...]]:
    program = load_json_object(program_path)
    selection = load_json_object(selection_path)
    code = code_path.read_bytes()
    if program.get("format") != PROGRAM_FORMAT:
        raise DirectAotError("unsupported structural AOT program")
    if selection.get("format") != SELECTION_FORMAT:
        raise DirectAotError("unsupported whole-AOT function selection")
    if _sha256_bytes(code) != str(program.get("code_sha256", "")).lower():
        raise DirectAotError("code.bin does not match the structural AOT program")

    base = int(program["base"])
    blocks = {int(item["id"]): item for item in program.get("blocks", [])}
    records = {
        int(item["entry"]): item for item in program.get("functions", [])
    }
    selected_entries = {
        int(item["entry"]) for item in selection.get("functions", [])
    }
    external_entries = {
        int(item["entry"])
        for item in selection.get("external_functions", [])
    }
    functions: list[DirectFunction] = []
    decoded: dict[int, tuple[int, int]] = {}

    for selected in selection.get("functions", []):
        entry = int(selected["entry"])
        record = records.get(entry)
        if record is None or not record.get("closed_static_cfg"):
            raise DirectAotError(
                f"selected function 0x{entry:08X} has no closed structural CFG"
            )
        for call in record.get("direct_calls", []):
            target = int(call["target"])
            if target not in selected_entries and target not in external_entries:
                raise DirectAotError(
                    f"call from 0x{entry:08X} escapes the selected closure"
                )
        function_blocks: list[DirectBlock] = []
        for raw_block_id in record.get("blocks", []):
            block = blocks.get(int(raw_block_id))
            if block is None:
                raise DirectAotError(
                    f"function 0x{entry:08X} references a missing block"
                )
            pc = int(block["pc"])
            end_pc = int(block["end_pc"])
            if pc < base or end_pc <= pc or (end_pc - pc) % 4 != 0:
                raise DirectAotError(f"invalid block interval at 0x{pc:08X}")
            operations: list[tuple[int, int]] = []
            for instruction_pc in range(pc, end_pc, 4):
                operation = decoded.get(instruction_pc)
                if operation is None:
                    offset = instruction_pc - base
                    if offset < 0 or offset + 4 > len(code):
                        raise DirectAotError(
                            f"instruction 0x{instruction_pc:08X} is outside code.bin"
                        )
                    raw = int.from_bytes(code[offset : offset + 4], "little")
                    item = decode_arm(code, offset, instruction_pc)
                    operation = (raw, _metadata(item))
                    decoded[instruction_pc] = operation
                operations.append(operation)
            function_blocks.append(DirectBlock(pc, tuple(operations)))
        functions.append(
            DirectFunction(
                entry,
                str(record.get("name", f"function_{entry:08x}")),
                tuple(function_blocks),
            )
        )

    ordered_functions = tuple(sorted(functions, key=lambda item: item.entry))
    owner_by_pc = {function.entry: function.entry for function in ordered_functions}
    for function in ordered_functions:
        for block in sorted(function.blocks, key=lambda item: item.pc):
            owner_by_pc.setdefault(block.pc, function.entry)
    function_index = {
        function.entry: index for index, function in enumerate(ordered_functions)
    }
    dispatch_pcs = tuple(sorted(owner_by_pc))
    dispatch_owners = tuple(
        function_index[owner_by_pc[pc]] for pc in dispatch_pcs
    )
    if not ordered_functions or not dispatch_pcs:
        raise DirectAotError("direct AOT closure is empty")
    return ordered_functions, dispatch_pcs, dispatch_owners


def _llvm_array(values: Sequence[int], llvm_type: str) -> str:
    return ", ".join(f"{llvm_type} {value}" for value in values)


def _function_symbol(entry: int) -> str:
    return f"triaevum_aot_function_{entry:08x}"


def render_function_shard(functions: Sequence[DirectFunction]) -> str:
    lines = [
        '; TriAevum direct AOT function shard',
        'target triple = "x86_64-pc-windows-msvc"',
        '',
        '%triaevum.Flow = type { i8, i32, i32 }',
        '%triaevum.PackedOp = type { i32, i32 }',
        '',
    ]
    for function in functions:
        for block in function.blocks:
            name = f"ops_{function.entry:08x}_{block.pc:08x}"
            operations = ", ".join(
                f"%triaevum.PackedOp {{ i32 {raw}, i32 {metadata} }}"
                for raw, metadata in block.operations
            )
            lines.append(
                f"@{name} = private constant [{len(block.operations)} x "
                f"%triaevum.PackedOp] [{operations}], align 4"
            )
        lines.append("")

        symbol = _function_symbol(function.entry)
        lines.extend(
            [
                f"define void @{symbol}(ptr %output, ptr %frame, ptr %context, "
                "i32 %entry_pc, ptr %execute_block) {",
                "entry:",
                "  %flow = alloca %triaevum.Flow, align 4",
                "  br label %dispatch",
                "",
                "dispatch:",
            ]
        )
        incoming = ["[ %entry_pc, %entry ]"] + [
            f"[ %next_{index}, %continue_{index} ]"
            for index in range(len(function.blocks))
        ]
        lines.append(f"  %pc = phi i32 {', '.join(incoming)}")
        lines.append("  switch i32 %pc, label %unknown [")
        for index, block in enumerate(function.blocks):
            lines.append(f"    i32 {block.pc}, label %block_{index}")
        lines.extend(["  ]", ""])

        for index, block in enumerate(function.blocks):
            ops = f"@ops_{function.entry:08x}_{block.pc:08x}"
            lines.extend(
                [
                    f"block_{index}:",
                    "  call void %execute_block(ptr %flow, ptr %frame, "
                    f"ptr %context, i32 {block.pc}, ptr {ops}, "
                    f"i32 {len(block.operations)})",
                    f"  %kind_ptr_{index} = getelementptr %triaevum.Flow, "
                    "ptr %flow, i32 0, i32 0",
                    f"  %kind_{index} = load i8, ptr %kind_ptr_{index}, align 4",
                    f"  %continues_{index} = icmp ule i8 %kind_{index}, 1",
                    f"  br i1 %continues_{index}, label %continue_{index}, "
                    "label %done",
                    "",
                    f"continue_{index}:",
                    f"  %pc_ptr_{index} = getelementptr %triaevum.Flow, "
                    "ptr %flow, i32 0, i32 1",
                    f"  %next_{index} = load i32, ptr %pc_ptr_{index}, align 4",
                    "  br label %dispatch",
                    "",
                ]
            )
        lines.extend(
            [
                "done:",
                "  %resolved = load %triaevum.Flow, ptr %flow, align 4",
                "  store %triaevum.Flow %resolved, ptr %output, align 4",
                "  ret void",
                "",
                "unknown:",
                "  %unknown_kind = getelementptr %triaevum.Flow, ptr %output, "
                "i32 0, i32 0",
                "  store i8 1, ptr %unknown_kind, align 4",
                "  %unknown_pc = getelementptr %triaevum.Flow, ptr %output, "
                "i32 0, i32 1",
                "  store i32 %pc, ptr %unknown_pc, align 4",
                "  %unknown_detail = getelementptr %triaevum.Flow, ptr %output, "
                "i32 0, i32 2",
                "  store i32 0, ptr %unknown_detail, align 4",
                "  ret void",
                "}",
                "",
            ]
        )
    return "\n".join(lines)


def render_registry(
    functions: Sequence[DirectFunction],
    dispatch_pcs: Sequence[int],
    dispatch_owners: Sequence[int],
) -> str:
    declarations = [
        f"declare void @{_function_symbol(function.entry)}("
        "ptr, ptr, ptr, i32, ptr)"
        for function in functions
    ]
    function_values = ", ".join(
        f"ptr @{_function_symbol(function.entry)}" for function in functions
    )
    return "\n".join(
        [
            "; TriAevum direct AOT registry",
            'target triple = "x86_64-pc-windows-msvc"',
            "",
            "%triaevum.Program = type { i32, i32, ptr, ptr, i64, ptr, i64 }",
            *declarations,
            "",
            f"@dispatch_pcs = private constant [{len(dispatch_pcs)} x i32] "
            f"[{_llvm_array(dispatch_pcs, 'i32')}], align 4",
            f"@dispatch_owners = private constant [{len(dispatch_owners)} x i32] "
            f"[{_llvm_array(dispatch_owners, 'i32')}], align 4",
            f"@functions = private constant [{len(functions)} x ptr] "
            f"[{function_values}], align 8",
            "@program = private constant %triaevum.Program { "
            f"i32 {PLUGIN_ABI}, i32 {PROGRAM_STRUCT_SIZE_X64}, "
            "ptr @dispatch_pcs, ptr @dispatch_owners, "
            f"i64 {len(dispatch_pcs)}, ptr @functions, i64 {len(functions)} "
            "}, align 8",
            "",
            "define dllexport ptr @triaevum_title_aot_query(i32 %requested_abi) {",
            f"  %supported = icmp eq i32 %requested_abi, {PLUGIN_ABI}",
            "  %result = select i1 %supported, ptr @program, ptr null",
            "  ret ptr %result",
            "}",
            "",
            "define dllexport ptr @triaevum_title_whole_aot_query(i32 %requested_abi) {",
            "  ret ptr null",
            "}",
            "",
        ]
    )


def _tool(path: Path, label: str) -> Path:
    result = path.expanduser().resolve()
    if not result.is_file() or result.is_symlink():
        raise DirectAotError(f"{label} is unavailable: {result}")
    return result


def _run(arguments: Sequence[str], cwd: Path, label: str) -> None:
    completed = subprocess.run(
        list(arguments), cwd=cwd, capture_output=True, text=True, check=False
    )
    if completed.returncode != 0:
        details = (completed.stderr or completed.stdout or "no diagnostics").strip()
        raise DirectAotError(
            f"{label} failed with status {completed.returncode}: {details}"
        )


def _stable_shards(
    functions: Sequence[DirectFunction], shard_count: int
) -> tuple[tuple[DirectFunction, ...], ...]:
    buckets: list[list[DirectFunction]] = [[] for _ in range(shard_count)]
    weights = [0] * shard_count
    for function in sorted(
        functions,
        key=lambda item: (
            -sum(len(block.operations) for block in item.blocks),
            item.entry,
        ),
    ):
        preferred = (((function.entry >> 2) * 0x9E3779B1) & 0xFFFFFFFF)
        preferred = (preferred * shard_count) >> 32
        candidates = sorted(
            range(shard_count),
            key=lambda index: (
                weights[index] + (0 if index == preferred else 1), index
            ),
        )
        target = candidates[0]
        buckets[target].append(function)
        weights[target] += sum(len(block.operations) for block in function.blocks)
    return tuple(
        tuple(sorted(bucket, key=lambda item: item.entry)) for bucket in buckets
    )


def build_direct_aot_plugin(
    *,
    program_path: Path,
    selection_path: Path,
    code_path: Path,
    cache_root: Path,
    toolchain: DirectAotToolchain,
    shard_count: int = 128,
    jobs: int = 8,
) -> dict[str, Any]:
    if shard_count <= 0 or shard_count > 512:
        raise DirectAotError("direct AOT shard count must be between 1 and 512")
    if jobs <= 0 or jobs > 32:
        raise DirectAotError("direct AOT jobs must be between 1 and 32")
    if toolchain.profile != PROFILE:
        raise DirectAotError(f"unsupported direct AOT profile: {toolchain.profile}")
    llc = _tool(toolchain.llc, "LLVM object compiler")
    linker = _tool(toolchain.linker, "LLVM COFF linker")
    llc_sha256 = sha256_file(llc)
    linker_sha256 = sha256_file(linker)
    backend_sha256 = sha256_file(Path(__file__))
    toolchain_identity = hashlib.sha256(
        (
            PROFILE
            + "\0"
            + llc_sha256
            + "\0"
            + linker_sha256
            + "\0"
            + toolchain.target_triple
        ).encode("ascii")
    ).hexdigest()
    translator_identity = hashlib.sha256(
        (FORMAT + "\0" + backend_sha256 + "\0" + toolchain_identity).encode(
            "ascii"
        )
    ).hexdigest()
    started = time.perf_counter()

    identity = hashlib.sha256()
    identity.update(FORMAT.encode("ascii"))
    for label, path, digest in (
        ("program", program_path, sha256_file(program_path)),
        ("selection", selection_path, sha256_file(selection_path)),
        ("code", code_path, sha256_file(code_path)),
        ("backend", Path(__file__), backend_sha256),
        ("llc", llc, llc_sha256),
        ("linker", linker, linker_sha256),
    ):
        identity.update(label.encode("ascii"))
        identity.update(b"\0")
        identity.update(bytes.fromhex(digest))
    identity.update(toolchain.target_triple.encode("ascii"))
    identity.update(f"\0{shard_count}".encode("ascii"))
    cache_key = identity.hexdigest()
    root = cache_root.expanduser().resolve() / cache_key
    manifest_path = root / "direct-aot.json"
    plugin_path = root / "triaevum_title_aot.dll"
    if manifest_path.is_file() and plugin_path.is_file():
        manifest = load_json_object(manifest_path)
        if (
            manifest.get("format") == FORMAT
            and manifest.get("cache_key") == cache_key
            and manifest.get("plugin_sha256") == sha256_file(plugin_path)
        ):
            return {
                **manifest,
                "status": "reused",
                "plugin": str(plugin_path),
                "elapsed_seconds": time.perf_counter() - started,
            }

    functions, dispatch_pcs, dispatch_owners = load_direct_program(
        program_path, selection_path, code_path
    )

    root.mkdir(parents=True, exist_ok=True)
    ir_root = root / "ir"
    object_root = root / "objects"
    ir_root.mkdir(exist_ok=True)
    object_root.mkdir(exist_ok=True)
    shards = _stable_shards(functions, shard_count)
    sources: list[tuple[Path, Path]] = []
    for index, shard in enumerate(shards):
        source = ir_root / f"shard_{index:03d}.ll"
        source_bytes = render_function_shard(shard).encode("utf-8")
        if not source.is_file() or source.read_bytes() != source_bytes:
            source.write_bytes(source_bytes)
        sources.append((source, object_root / f"shard_{index:03d}.obj"))
    registry_source = ir_root / "registry.ll"
    registry_bytes = render_registry(
        functions, dispatch_pcs, dispatch_owners
    ).encode("utf-8")
    if (
        not registry_source.is_file()
        or registry_source.read_bytes() != registry_bytes
    ):
        registry_source.write_bytes(registry_bytes)
    sources.append((registry_source, object_root / "registry.obj"))

    compile_queue: list[tuple[Path, Path, Path]] = []
    for source, output in sources:
        stamp = output.with_suffix(".json")
        object_key = _sha256_bytes(
            source.read_bytes()
            + bytes.fromhex(llc_sha256)
            + toolchain.target_triple.encode("ascii")
        )
        valid = False
        if output.is_file() and stamp.is_file():
            state = load_json_object(stamp)
            valid = (
                state.get("cache_key") == object_key
                and state.get("sha256") == sha256_file(output)
            )
        if not valid:
            compile_queue.append((source, output, stamp))

    def compile_one(item: tuple[Path, Path, Path]) -> None:
        source, output, stamp = item
        temporary = output.with_suffix(f".{os.getpid()}.tmp.obj")
        temporary.unlink(missing_ok=True)
        _run(
            (
                str(llc),
                "--filetype=obj",
                f"--mtriple={toolchain.target_triple}",
                "--O=2",
                str(source),
                "-o",
                str(temporary),
            ),
            root,
            f"compiling {source.name}",
        )
        temporary.replace(output)
        atomic_write_json(
            stamp,
            {
                "cache_key": _sha256_bytes(
                    source.read_bytes()
                    + bytes.fromhex(llc_sha256)
                    + toolchain.target_triple.encode("ascii")
                ),
                "sha256": sha256_file(output),
            },
        )

    with ThreadPoolExecutor(max_workers=jobs) as executor:
        tuple(executor.map(compile_one, compile_queue))

    with tempfile.TemporaryDirectory(prefix=".link-", dir=root) as temporary:
        temporary_root = Path(temporary)
        linked = temporary_root / "triaevum_title_aot.dll"
        response = temporary_root / "objects.rsp"
        response.write_text(
            "\n".join(f'"{output}"' for _, output in sources) + "\n",
            encoding="utf-8",
        )
        _run(
            (
                str(linker),
                "/dll",
                "/noentry",
                "/nodefaultlib",
                "/machine:x64",
                "/opt:ref",
                "/opt:icf",
                "/Brepro",
                "/export:triaevum_title_aot_query",
                "/export:triaevum_title_whole_aot_query",
                f"/out:{linked}",
                f"@{response}",
            ),
            root,
            "linking direct AOT plugin",
        )
        linked.replace(plugin_path)

    manifest = {
        "format": FORMAT,
        "status": "built",
        "backend": "llvm_direct_aot_plugin",
        "profile": toolchain.profile,
        "toolchain_identity_sha256": toolchain_identity,
        "translator_identity_sha256": translator_identity,
        "cache_key": cache_key,
        "plugin": str(plugin_path),
        "plugin_sha256": sha256_file(plugin_path),
        "plugin_bytes": plugin_path.stat().st_size,
        "program_sha256": sha256_file(program_path),
        "code_sha256": sha256_file(code_path),
        "functions": len(functions),
        "dispatch_entries": len(dispatch_pcs),
        "shards": shard_count,
        "objects_compiled": len(compile_queue),
        "objects_reused": len(sources) - len(compile_queue),
    }
    atomic_write_json(manifest_path, manifest)
    return {
        **manifest,
        "plugin": str(plugin_path),
        "elapsed_seconds": time.perf_counter() - started,
    }
