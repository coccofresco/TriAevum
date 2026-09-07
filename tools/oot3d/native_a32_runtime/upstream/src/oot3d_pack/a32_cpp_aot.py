from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Sequence

import capstone

from .arm_decode import DecodedInstruction, decode_arm
from .arm_ir import (
    arm_core_runtime_category,
    arm_core_stops_linear_flow,
    arm_core_supported,
    arm_vfp_scalar_supported,
    arm_vfp_transport_supported,
)
from .arm_lift import is_instruction_liftable


DEFAULT_BASE = 0x00100000
DEFAULT_SHARD_SIZE = 8_192
SCHEMA = "oot3d-a32-cpp-aot-v3"
GENERATED_HEADER = "oot3d_a32_generated.h"
REGISTRY_SOURCE = "registry.cpp"
_GENERATOR_SOURCE_NAMES = (
    "a32_cpp_aot.py",
    "arm_decode.py",
    "arm_ir.py",
    "arm_lift.py",
)
_GENERATOR_SOURCE_PREFIX = "src/oot3d_pack"

_CONDITION_CPP = (
    "Eq",
    "Ne",
    "Cs",
    "Cc",
    "Mi",
    "Pl",
    "Vs",
    "Vc",
    "Hi",
    "Ls",
    "Ge",
    "Lt",
    "Gt",
    "Le",
    "Al",
    "Nv",
)


@dataclass(frozen=True)
class FunctionRecord:
    entry: int
    end: int
    name: str


@dataclass(frozen=True)
class EmittedOp:
    pc: int
    raw: int
    opcode: str
    condition: str
    flags: int
    category: str


@dataclass(frozen=True)
class EmittedBlock:
    pc: int
    ops: tuple[EmittedOp, ...]


@dataclass(frozen=True)
class EmittedShard:
    index: int
    blocks: tuple[EmittedBlock, ...]

    @property
    def first_pc(self) -> int:
        return self.blocks[0].pc

    @property
    def last_pc(self) -> int:
        block = self.blocks[-1]
        return block.pc + (len(block.ops) - 1) * 4


@dataclass
class FlowResult:
    decoded_reachable: set[int]
    unknown_stops: set[int]
    control_targets: set[int]
    block_starts: set[int]
    pointer_roots: set[int] = field(default_factory=set)
    pointer_sources: dict[int, tuple[int, ...]] = field(default_factory=dict)
    explicit_lr_calls: set[int] = field(default_factory=set)


@dataclass(frozen=True)
class GenerationResult:
    manifest: dict[str, object]
    files: tuple[Path, ...]


def _parse_int(value: str) -> int:
    return int(value.strip(), 0)


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_path(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _generator_fingerprint() -> tuple[
    str,
    dict[str, str],
    dict[str, str],
]:
    """Hash every Python source that can change generated AOT output."""

    package = Path(__file__).resolve().parent
    sources = {
        f"{_GENERATOR_SOURCE_PREFIX}/{name}": _sha256_path(package / name)
        for name in _GENERATOR_SOURCE_NAMES
    }
    dependencies = {"capstone": capstone.__version__}
    digest = hashlib.sha256()
    for path, source_sha256 in sorted(sources.items()):
        digest.update(f"source:{path}".encode("utf-8"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(source_sha256))
    for name, version in sorted(dependencies.items()):
        digest.update(f"dependency:{name}".encode("utf-8"))
        digest.update(b"\0")
        digest.update(version.encode("utf-8"))
    return digest.hexdigest(), sources, dependencies


def _read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None:
            raise ValueError(f"{path} has no CSV header")
        return list(reader)


def _load_inputs(
    inventory_path: Path,
    boundary_audit_path: Path,
    code_size: int,
    base: int,
) -> tuple[list[FunctionRecord], list[FunctionRecord], set[int], set[int]]:
    inventory_rows = _read_csv(inventory_path)
    audit_rows = _read_csv(boundary_audit_path)
    required_inventory = {"entry", "size", "name"}
    required_audit = {"entry"}
    if inventory_rows and not required_inventory.issubset(inventory_rows[0]):
        missing = sorted(required_inventory - set(inventory_rows[0]))
        raise ValueError(f"inventory is missing columns: {', '.join(missing)}")
    if audit_rows and not required_audit.issubset(audit_rows[0]):
        raise ValueError("boundary audit is missing column: entry")

    excluded_entries = {_parse_int(row["entry"]) for row in audit_rows}
    functions: list[FunctionRecord] = []
    seen_entries: set[int] = set()
    slots: set[int] = set()
    image_end = base + code_size
    for row in inventory_rows:
        entry = _parse_int(row["entry"])
        size = _parse_int(row["size"])
        if entry in seen_entries:
            raise ValueError(f"duplicate inventory entry 0x{entry:08X}")
        if size <= 0:
            raise ValueError(f"non-positive size for inventory entry 0x{entry:08X}")
        if entry < base or entry + size > image_end:
            raise ValueError(
                f"inventory interval 0x{entry:08X}+0x{size:X} is outside code image"
            )
        seen_entries.add(entry)
        function = FunctionRecord(entry, entry + size, row.get("name", ""))
        functions.append(function)
        aligned_end = entry + size - (size % 4)
        slots.update(range(entry, aligned_end, 4))

    unknown_exclusions = excluded_entries - seen_entries
    if unknown_exclusions:
        first = min(unknown_exclusions)
        raise ValueError(
            "boundary audit entry is absent from inventory: " f"0x{first:08X}"
        )
    callable_functions = [
        function for function in functions if function.entry not in excluded_entries
    ]
    callable_slots: set[int] = set()
    for function in callable_functions:
        aligned_end = function.entry + (function.end - function.entry) - (
            (function.end - function.entry) % 4
        )
        callable_slots.update(range(function.entry, aligned_end, 4))
    functions.sort(key=lambda item: (item.entry, item.end, item.name))
    callable_functions.sort(key=lambda item: (item.entry, item.end, item.name))
    return functions, callable_functions, slots, callable_slots


class _Decoder:
    def __init__(self, code: bytes, base: int) -> None:
        self.code = code
        self.base = base
        self.cache: dict[int, DecodedInstruction] = {}

    def get(self, pc: int) -> DecodedInstruction:
        item = self.cache.get(pc)
        if item is None:
            item = decode_arm(self.code, pc - self.base, pc)
            self.cache[pc] = item
        return item


def _is_always(item: DecodedInstruction) -> bool:
    # Capstone uses ``None`` for architecturally unconditional encodings whose
    # raw condition field is 0xF.  Treat both representations as unconditional.
    return item.condition in {None, "al"}


def _writes_pc(item: DecodedInstruction) -> bool:
    return bool(
        (item.kind == "data_processing" and item.rd == 15)
        or (item.kind == "memory" and item.load and item.rd == 15)
        or (
            item.state == "arm"
            and arm_core_stops_linear_flow(item.raw)
        )
    )


def _lr_setup_return_site(item: DecodedInstruction) -> int | None:
    if (
        not _is_always(item)
        or item.kind != "data_processing"
        or item.rd != 14
    ):
        return None
    if item.opcode == "mov" and item.rm == 15:
        return item.pc + 8
    if item.rn == 15 and item.imm is not None:
        if item.opcode == "add":
            return item.pc + 8 + item.imm
        if item.opcode == "sub":
            return item.pc + 8 - item.imm
    return None


def _writes_lr(item: DecodedInstruction) -> bool:
    if item.kind == "call" or item.rd == 14:
        return True
    raw = item.raw
    return bool(
        item.state == "arm"
        and (raw & 0x0E000000) == 0x08000000
        and raw & (1 << 20)
        and raw & (1 << 14)
    )


def _has_explicit_lr_call_setup(
    decoder: _Decoder,
    slots: set[int],
    pc: int,
    literal_data: set[int],
) -> bool:
    """Recognize the pre-BLX ARM idiom that writes LR then branches via PC/BX."""

    expected_return = pc + 4
    for distance in range(1, 17):
        candidate_pc = pc - distance * 4
        if candidate_pc not in slots or candidate_pc in literal_data:
            return False
        candidate = decoder.get(candidate_pc)
        return_site = _lr_setup_return_site(candidate)
        if return_site is not None:
            return return_site == expected_return
        if _writes_lr(candidate):
            return False
        if (
            candidate.kind in {"branch", "call", "indirect_branch"}
            or _writes_pc(candidate)
        ):
            return False
    return False


def _walk_cfg(
    decoder: _Decoder,
    slots: set[int],
    entries: Iterable[int],
    literal_data: set[int],
    follow_explicit_lr_calls: bool = True,
) -> FlowResult:
    queue = deque(sorted(set(entries)))
    visited: set[int] = set()
    reachable: set[int] = set()
    unknown_stops: set[int] = set()
    control_targets = set(queue)
    block_starts = set(queue)
    explicit_lr_calls: set[int] = set()

    def enqueue(pc: int, *, block_start: bool = False) -> None:
        if pc in slots and pc not in literal_data:
            queue.append(pc)
            if block_start:
                block_starts.add(pc)

    while queue:
        pc = queue.popleft()
        if pc in visited or pc not in slots or pc in literal_data:
            continue
        visited.add(pc)
        item = decoder.get(pc)
        if item.kind == "unknown":
            unknown_stops.add(pc)
            continue
        reachable.add(pc)
        fallthrough = pc + 4

        if item.kind == "branch":
            if item.target is not None and item.target % 4 == 0:
                control_targets.add(item.target)
                enqueue(item.target, block_start=True)
            if not _is_always(item):
                enqueue(fallthrough, block_start=True)
            continue
        if item.kind == "call":
            if item.target is not None and item.target % 4 == 0:
                # A direct call proves its destination is code.  Callable
                # callees are already roots, while this enqueue also preserves
                # audited internal destinations that are not standalone roots.
                control_targets.add(item.target)
                enqueue(item.target, block_start=True)
            enqueue(fallthrough, block_start=True)
            continue
        if item.kind == "indirect_branch":
            # Returns, BX/BLX register and computed dispatches need runtime
            # state.  BLX register is nevertheless a call: it writes LR and
            # its return site must be dispatchable even when the instruction
            # itself is unconditional.  A predicated BX/BLX also needs the
            # fallthrough for the condition-failed path.
            is_register_call = (item.raw & 0x0FFFFFF0) == 0x012FFF30
            has_explicit_link = follow_explicit_lr_calls and (
                _has_explicit_lr_call_setup(decoder, slots, pc, literal_data)
            )
            if has_explicit_link:
                explicit_lr_calls.add(pc)
            if is_register_call or has_explicit_link or not _is_always(item):
                enqueue(fallthrough, block_start=True)
            continue
        if _writes_pc(item):
            has_explicit_link = (
                follow_explicit_lr_calls
                and _has_explicit_lr_call_setup(decoder, slots, pc, literal_data)
            )
            if has_explicit_link:
                explicit_lr_calls.add(pc)
            if has_explicit_link or not _is_always(item):
                enqueue(fallthrough, block_start=True)
            continue

        enqueue(fallthrough, block_start=item.kind == "svc")

    return FlowResult(
        reachable,
        unknown_stops,
        control_targets,
        block_starts,
        explicit_lr_calls=explicit_lr_calls,
    )


_VFP_LITERAL_OPERAND = re.compile(
    r"^[sd](?:[0-9]|[12][0-9]|3[01]), "
    r"\[pc(?:, #[+-]?(?:0x[0-9a-f]+|[0-9]+))?\]$"
)


def _embedded_ascii_words(
    decoder: _Decoder,
    reachable: set[int],
    slots: set[int],
) -> set[int]:
    allowed = frozenset((0, 9, 10, 13, *range(0x20, 0x7F)))
    result: set[int] = set()
    run: list[int] = []

    def finish() -> None:
        if len(run) < 2:
            return
        data = b"".join(
            decoder.code[pc - decoder.base : pc - decoder.base + 4]
            for pc in run
        )
        printable = sum(byte != 0 for byte in data)
        units = [
            int.from_bytes(data[offset : offset + 2], "little")
            for offset in range(0, len(data), 2)
        ]
        try:
            terminator = units.index(0)
        except ValueError:
            terminator = len(units)
        text_units = units[:terminator]
        looks_utf16 = len(text_units) >= 2 and all(
            unit <= 0x7F for unit in text_units
        )
        if 0 in data and printable >= 4 and not looks_utf16:
            result.update(run)

    previous: int | None = None
    for pc in sorted(slots):
        data = decoder.code[pc - decoder.base : pc - decoder.base + 4]
        qualifies = pc not in reachable and all(byte in allowed for byte in data)
        if qualifies and (previous is None or pc == previous + 4):
            run.append(pc)
        elif qualifies:
            finish()
            run = [pc]
        else:
            finish()
            run = []
        previous = pc
    finish()
    return result


def _embedded_utf16_words(
    decoder: _Decoder,
    reachable: set[int],
    slots: set[int],
) -> set[int]:
    result: set[int] = set()
    run: list[int] = []

    def unit_qualifies(unit: int) -> bool:
        return unit == 0 or (
            chr(unit).isprintable()
            and (
                0x20 <= unit <= 0x024F
                or 0x3000 <= unit <= 0x30FF
                or 0x3400 <= unit <= 0x9FFF
                or 0xFF00 <= unit <= 0xFFEF
            )
        )

    def word_qualifies(pc: int) -> bool:
        data = decoder.code[pc - decoder.base : pc - decoder.base + 4]
        units = (
            int.from_bytes(data[0:2], "little"),
            int.from_bytes(data[2:4], "little"),
        )
        return all(unit_qualifies(unit) for unit in units)

    def finish() -> None:
        if len(run) < 2:
            return
        data = b"".join(
            decoder.code[pc - decoder.base : pc - decoder.base + 4]
            for pc in run
        )
        units = [
            int.from_bytes(data[offset : offset + 2], "little")
            for offset in range(0, len(data), 2)
        ]
        if 0 not in units:
            return
        while units and units[-1] == 0:
            units.pop()
        ascii_prefix = data.split(b"\0", 1)[0]
        looks_ascii = len(ascii_prefix) >= 4 and all(
            0x20 <= byte < 0x7F for byte in ascii_prefix
        )
        if len(units) >= 2 and all(unit != 0 for unit in units) and not looks_ascii:
            result.update(run)

    previous: int | None = None
    for pc in sorted(slots):
        qualifies = pc not in reachable and word_qualifies(pc)
        if qualifies and (previous is None or pc == previous + 4):
            run.append(pc)
        elif qualifies:
            finish()
            run = [pc]
        else:
            finish()
            run = []
        previous = pc
    finish()
    return result


def _relative_pointer_table_words(
    decoder: _Decoder,
    reachable: set[int],
    slots: set[int],
) -> set[int]:
    result: set[int] = set()
    run: list[int] = []
    image_start = decoder.base
    image_end = decoder.base + len(decoder.code)

    def finish() -> None:
        if len(run) >= 8:
            result.update(run)

    previous: int | None = None
    for pc in sorted(slots):
        raw = decoder.get(pc).raw
        displacement = raw - 0x100000000 if raw & 0x80000000 else raw
        target = (pc + displacement) & 0xFFFFFFFF
        qualifies = False
        if (
            pc not in reachable
            and target % 4 == 0
            and image_start <= target < image_end
        ):
            target_item = decoder.get(target)
            qualifies = (
                target_item.state == "arm"
                and target_item.raw >> 28 == 0xE
                and target_item.kind != "unknown"
            )
        if qualifies and (previous is None or pc == previous + 4):
            run.append(pc)
        elif qualifies:
            finish()
            run = [pc]
        else:
            finish()
            run = []
        previous = pc
    finish()
    return result


def _literal_targets(
    decoder: _Decoder,
    reachable: set[int],
    slots: set[int],
    classify_embedded_data: bool = True,
) -> tuple[set[int], dict[int, tuple[int, ...]], set[int]]:
    mutable_sources: dict[int, list[int]] = {}
    dynamic_entries: set[int] = set()
    image_start = decoder.base
    image_end = decoder.base + len(decoder.code)

    def record(target: int, source: int, width: int) -> None:
        if target % 4 or target < image_start or target + width > image_end:
            return
        for offset in range(0, width, 4):
            word = target + offset
            if word in slots:
                mutable_sources.setdefault(word, []).append(source)

    for pc in sorted(reachable):
        item = decoder.get(pc)
        if item.kind == "memory" and item.load and item.rn == 15:
            if item.imm is not None:
                record(((pc + 8) & ~3) + item.imm, pc, 4)

        raw = item.raw
        if (raw & 0x0F3F0E00) not in {0x0D1F0A00, 0x0D1F0B00}:
            continue
        if (item.canonical_mnemonic or "") != "vldr":
            continue
        if _VFP_LITERAL_OPERAND.fullmatch(item.operand_text or "") is None:
            continue
        displacement = (raw & 0xFF) * 4
        target = pc + 8 + (displacement if raw & (1 << 23) else -displacement)
        width = 8 if (item.operand_text or "").startswith("d") else 4
        record(target, pc, width)

    # GCC emits dense ARM switch dispatches as
    # ``cmp Rm,#N; ldrlo pc,[pc,Rm,lsl#2]``.  The table begins at PC+8 and
    # contains N absolute A32 destinations.  Some prologues keep the compare
    # flags live across a longer setup sequence, hence the bounded backward
    # search for the last flag writer rather than an adjacency requirement.
    for pc in sorted(reachable):
        item = decoder.get(pc)
        raw = item.raw
        condition = (raw >> 28) & 0xF
        if (
            (raw & 0x0FFFFFF0) != 0x079FF100
            or condition not in {0x3, 0x9}
        ):
            continue
        index_register = raw & 0xF
        if index_register == 15:
            continue

        bound: int | None = None
        for distance in range(1, 65):
            candidate_pc = pc - distance * 4
            if candidate_pc not in slots:
                break
            candidate = decoder.get(candidate_pc)
            if candidate.kind == "call":
                break
            if candidate.setflags:
                if (
                    candidate.kind == "data_processing"
                    and candidate.opcode == "cmp"
                    and candidate.rn == index_register
                    and candidate.imm is not None
                    and _is_always(candidate)
                ):
                    bound = candidate.imm
                break
            if (
                candidate.kind in {"branch", "indirect_branch"}
                and _is_always(candidate)
            ) or _writes_pc(candidate):
                break

        if bound is None:
            continue
        count = bound if condition == 0x3 else bound + 1
        if count <= 0 or count > 256:
            continue

        table_words: list[int] = []
        table_targets: list[int] = []
        table_start = pc + 8
        for index in range(count):
            word = table_start + index * 4
            if word < image_start or word + 4 > image_end:
                table_words = []
                break
            offset = word - image_start
            target = int.from_bytes(
                decoder.code[offset : offset + 4], "little"
            )
            if target % 4 or target < image_start or target >= image_end:
                table_words = []
                break
            table_words.append(word)
            table_targets.append(target)
        if not table_words:
            continue
        for word in table_words:
            record(word, pc, 4)
        dynamic_entries.update(target for target in table_targets if target in slots)

    if classify_embedded_data:
        for word in sorted(_embedded_ascii_words(decoder, reachable, slots)):
            record(word, word, 4)
        for word in sorted(_embedded_utf16_words(decoder, reachable, slots)):
            record(word, word, 4)
        for word in sorted(
            _relative_pointer_table_words(decoder, reachable, slots)
        ):
            record(word, word, 4)
            raw = decoder.get(word).raw
            displacement = raw - 0x100000000 if raw & 0x80000000 else raw
            dynamic_entries.add((word + displacement) & 0xFFFFFFFF)

    sources = {
        target: tuple(sorted(set(source_pcs)))
        for target, source_pcs in mutable_sources.items()
    }
    return set(sources), sources, dynamic_entries


def _address_aware_flow_fixed_point(
    decoder: _Decoder,
    slots: set[int],
    entries: Iterable[int],
    follow_explicit_lr_calls: bool = True,
    classify_embedded_data: bool = True,
) -> tuple[FlowResult, set[int], dict[int, tuple[int, ...]], int]:
    stable_entries = tuple(sorted(set(entries)))
    literal_data: set[int] = set()
    dynamic_entries: set[int] = set()
    last_state: tuple[
        frozenset[int], frozenset[int], frozenset[int]
    ] | None = None
    for iteration in range(1, 33):
        flow = _walk_cfg(
            decoder,
            slots,
            (*stable_entries, *sorted(dynamic_entries)),
            literal_data,
            follow_explicit_lr_calls,
        )
        targets, sources, jump_targets = _literal_targets(
            decoder,
            flow.decoded_reachable,
            slots,
            classify_embedded_data,
        )
        new_dynamic_entries = dynamic_entries | jump_targets
        new_literal_data = (
            targets - flow.control_targets - new_dynamic_entries
        )
        state = (
            frozenset(new_literal_data),
            frozenset(flow.decoded_reachable),
            frozenset(new_dynamic_entries),
        )
        if state == last_state:
            filtered_sources = {
                target: source_pcs
                for target, source_pcs in sources.items()
                if target in new_literal_data
            }
            return flow, new_literal_data, filtered_sources, iteration
        literal_data = new_literal_data
        dynamic_entries = new_dynamic_entries
        last_state = state
    raise RuntimeError("literal/CFG fixed point did not converge in 32 iterations")


def _pointer_entry_roots(
    decoder: _Decoder,
    slots: set[int],
    root_domain: set[int],
    flow: FlowResult,
    literal_data: set[int],
) -> tuple[set[int], dict[int, tuple[int, ...]]]:
    """Find exact A32 entry pointers outside the already decoded CFG."""

    unresolved = root_domain - flow.decoded_reachable - literal_data
    executed = flow.decoded_reachable | flow.unknown_stops
    mutable_sources: dict[int, list[int]] = {}
    for offset in range(0, len(decoder.code) - 3, 4):
        source = decoder.base + offset
        target = int.from_bytes(decoder.code[offset : offset + 4], "little")
        if target not in unresolved or source in executed:
            continue
        item = decoder.get(target)
        # An aligned word that stores the exact address is strong target
        # evidence, but code/data share the image.  Require the destination to
        # begin with a decoded, architecturally unconditional A32 word; this
        # rejects string/data pointers without guessing an ABI or function end.
        if item.state != "arm" or item.raw >> 28 != 0xE or item.kind == "unknown":
            continue
        mutable_sources.setdefault(target, []).append(source)
    sources = {
        target: tuple(sorted(set(source_pcs)))
        for target, source_pcs in mutable_sources.items()
    }
    return set(sources), sources


def _address_aware_flow(
    decoder: _Decoder,
    slots: set[int],
    entries: Iterable[int],
    pointer_root_domain: set[int] | None = None,
    follow_explicit_lr_calls: bool = True,
    classify_embedded_data: bool = True,
) -> tuple[FlowResult, set[int], dict[int, tuple[int, ...]], int]:
    stable_entries = tuple(sorted(set(entries)))
    flow, literal_data, literal_sources, iterations = (
        _address_aware_flow_fixed_point(
            decoder,
            slots,
            stable_entries,
            follow_explicit_lr_calls,
            classify_embedded_data,
        )
    )
    pointer_roots, pointer_sources = _pointer_entry_roots(
        decoder,
        slots,
        slots if pointer_root_domain is None else pointer_root_domain,
        flow,
        literal_data,
    )
    if pointer_roots:
        flow, literal_data, literal_sources, iterations = (
            _address_aware_flow_fixed_point(
                decoder,
                slots,
                (*stable_entries, *sorted(pointer_roots)),
                follow_explicit_lr_calls,
                classify_embedded_data,
            )
        )
    flow.pointer_roots = pointer_roots
    flow.pointer_sources = pointer_sources
    return flow, literal_data, literal_sources, iterations


def _opcode_and_category(item: DecodedInstruction) -> tuple[str, str]:
    if item.kind == "data_processing":
        if item.opcode == "mov":
            return ("MovImm" if item.imm is not None else "MovReg", "fast_path")
        if item.opcode == "add":
            return "Add", "fast_path"
        if item.opcode == "sub":
            return "Sub", "fast_path"
        if item.opcode == "cmp":
            return "Cmp", "fast_path"
    if item.kind == "memory":
        return ("Ldr32" if item.load else "Str32", "fast_path")
    if item.kind in {"branch", "call"} and item.target is not None:
        if not item.thumb_target:
            return "Branch", "fast_path"
    if item.kind == "indirect_branch":
        opcode = item.raw & 0x0FFFFFF0
        if opcode in {0x012FFF10, 0x012FFF30}:
            return "BranchReg", "fast_path"
    if item.kind == "svc":
        return "Svc", "fast_path"
    if item.state == "arm" and arm_core_supported(item.raw):
        core_category = arm_core_runtime_category(item.raw)
        return {
            "system": ("CoreSystem", "direct_core_system"),
            "alu": ("CoreAlu", "direct_core_alu"),
            "memory": ("CoreMemory", "direct_core_memory"),
        }[core_category]
    if item.state == "arm" and arm_vfp_transport_supported(item.raw):
        return "VfpTransport", "direct_vfp_transport"
    if item.state == "arm" and arm_vfp_scalar_supported(item.raw):
        return "VfpScalar", "direct_vfp_scalar"
    return "Unsupported", "fallback"


def _metadata_flags(item: DecodedInstruction) -> int:
    flags = 0
    if item.setflags:
        flags |= 1  # OpFlag::SetFlags
    if item.kind == "data_processing" and item.imm is not None:
        flags |= 2  # OpFlag::Immediate
    if item.kind == "call" or (
        item.kind == "indirect_branch"
        and (item.raw & 0x0FFFFFF0) == 0x012FFF30
    ):
        flags |= 4  # OpFlag::Link
    if item.kind == "memory" and item.imm is not None and item.imm < 0:
        flags |= 32  # OpFlag::SubtractOffset
    return flags


def _emitted_ops(
    decoder: _Decoder,
    reachable: set[int],
) -> dict[int, EmittedOp]:
    emitted: dict[int, EmittedOp] = {}
    for pc in sorted(reachable):
        item = decoder.get(pc)
        opcode, category = _opcode_and_category(item)
        # Capstone represents architecturally unconditional cond=0xF encodings
        # with no condition.  Treat those as AL; native cond=0xF instructions
        # retain Nv explicitly and therefore remain distinguishable.
        condition = (
            "Al"
            if item.condition is None
            else _CONDITION_CPP[(item.raw >> 28) & 0xF]
        )
        if pc in emitted:
            raise AssertionError(f"duplicate emitted PC 0x{pc:08X}")
        emitted[pc] = EmittedOp(
            pc, item.raw, opcode, condition, _metadata_flags(item), category
        )
    return emitted


def _ends_block(item: DecodedInstruction) -> bool:
    return bool(
        item.kind in {"branch", "call", "indirect_branch", "svc", "unknown"}
        or _writes_pc(item)
    )


def _make_blocks(
    decoder: _Decoder,
    emitted: dict[int, EmittedOp],
    block_starts: set[int],
    shard_size: int,
) -> list[EmittedBlock]:
    blocks: list[EmittedBlock] = []
    current: list[EmittedOp] = []
    previous: EmittedOp | None = None
    for pc in sorted(emitted):
        op = emitted[pc]
        must_split = bool(
            current
            and (
                previous is None
                or pc != previous.pc + 4
                or pc in block_starts
                or _ends_block(decoder.get(previous.pc))
                or len(current) >= shard_size
            )
        )
        if must_split:
            blocks.append(EmittedBlock(current[0].pc, tuple(current)))
            current = []
        current.append(op)
        previous = op
    if current:
        blocks.append(EmittedBlock(current[0].pc, tuple(current)))
    return blocks


def _make_shards(
    blocks: Sequence[EmittedBlock], shard_size: int
) -> list[EmittedShard]:
    shards: list[EmittedShard] = []
    current: list[EmittedBlock] = []
    current_ops = 0
    for block in blocks:
        count = len(block.ops)
        if current and current_ops + count > shard_size:
            shards.append(EmittedShard(len(shards), tuple(current)))
            current = []
            current_ops = 0
        current.append(block)
        current_ops += count
    if current:
        shards.append(EmittedShard(len(shards), tuple(current)))
    return shards


def _render_header() -> str:
    return """#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <span>

namespace oot3d::recomp {

const a32::Registry& GetA32GeneratedRegistry() noexcept;
void ConfigureA32GeneratedNativeCandidates(
    std::span<const std::uint32_t> entry_points) noexcept;

}  // namespace oot3d::recomp
"""


def _render_shard(shard: EmittedShard, native_blocks: set[int]) -> str:
    lines = [
        f'#include "{GENERATED_HEADER}"',
        "",
        "namespace oot3d::recomp {",
        "",
    ]
    ops_symbol = f"kShard{shard.index:04d}Ops"
    lines.append(f"static constexpr a32::PackedOp {ops_symbol}[] = {{")
    for block in shard.blocks:
        for op in block.ops:
            lines.append(
                "    {"
                f"0x{op.raw:08X}U, a32::EncodeMetadata(a32::Opcode::{op.opcode}, "
                f"a32::Condition::{op.condition}, 0x{op.flags:02X}U)"
                "},"
            )
    lines.extend(["};", ""])
    lines.append(f"a32::Block kShard{shard.index:04d}Blocks[] = {{")
    offset = 0
    for block in shard.blocks:
        native = ", true" if block.pc in native_blocks else ""
        lines.append(
            f"    {{0x{block.pc:08X}U, {ops_symbol} + {offset}U, "
            f"{len(block.ops)}U{native}}},"
        )
        offset += len(block.ops)
    lines.extend(
        [
            "};",
            "",
            "}  // namespace oot3d::recomp",
            "",
        ]
    )
    return "\n".join(lines)


def _cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def _render_registry(
    shards: Sequence[EmittedShard], callable_functions: Sequence[FunctionRecord]
) -> str:
    lines = [
        f'#include "{GENERATED_HEADER}"',
        "",
        "namespace oot3d::recomp {",
        "",
    ]
    for shard in shards:
        lines.append(f"extern a32::Block kShard{shard.index:04d}Blocks[];")
    if shards:
        lines.extend(["", "static constexpr a32::BlockShard kShards[] = {"])
        for shard in shards:
            lines.append(
                f"    {{0x{shard.first_pc:08X}U, 0x{shard.last_pc:08X}U, "
                f"kShard{shard.index:04d}Blocks, {len(shard.blocks)}U}},"
            )
        lines.extend(["};", ""])

    if callable_functions:
        lines.append("static constexpr a32::Function kFunctions[] = {")
        for function in callable_functions:
            lines.append(
                f"    {{0x{function.entry:08X}U, 0x{function.end:08X}U, "
                f"{_cpp_string(function.name)}}},"
            )
        lines.extend(["};", ""])

    lines.extend(
        [
            "static constexpr a32::Registry kRegistry = {",
            (
                f"    kShards, {len(shards)}U,"
                if shards
                else "    nullptr, 0U,"
            ),
            (
                f"    kFunctions, {len(callable_functions)}U,"
                if callable_functions
                else "    nullptr, 0U,"
            ),
            "};",
            "",
            "const a32::Registry& GetA32GeneratedRegistry() noexcept {",
            "    return kRegistry;",
            "}",
            "",
            "void ConfigureA32GeneratedNativeCandidates(",
            "    std::span<const std::uint32_t> entry_points) noexcept {",
            "    for (const a32::BlockShard& shard : kShards) {",
            "        for (std::uint32_t index = 0U; index < shard.block_count; ++index) {",
            "            const_cast<a32::Block&>(shard.blocks[index]).native_candidate = false;",
            "        }",
            "    }",
            "    for (const std::uint32_t pc : entry_points) {",
            "        if (const a32::Block* block = a32::FindBlock(kRegistry, pc);",
            "            block != nullptr) {",
            "            const_cast<a32::Block*>(block)->native_candidate = true;",
            "        }",
            "    }",
            "}",
            "",
            "}  // namespace oot3d::recomp",
            "",
        ]
    )
    return "\n".join(lines)


def _partition_counts(
    slots: set[int],
    emitted: dict[int, EmittedOp],
    literal_data: set[int],
) -> dict[str, int]:
    counts = {
        "emitted_fast_path": 0,
        "emitted_core": 0,
        "emitted_vfp_transport": 0,
        "emitted_vfp_scalar": 0,
        "emitted_delegated_vfp": 0,
        "fallback": 0,
        "literal_data": len(literal_data & slots),
        "unreachable": 0,
    }
    key_for_category = {
        "fast_path": "emitted_fast_path",
        "direct_core_system": "emitted_core",
        "direct_core_alu": "emitted_core",
        "direct_core_memory": "emitted_core",
        "direct_vfp_transport": "emitted_vfp_transport",
        "direct_vfp_scalar": "emitted_vfp_scalar",
        "delegated_vfp": "emitted_delegated_vfp",
        "fallback": "fallback",
    }
    for op in emitted.values():
        counts[key_for_category[op.category]] += 1
    counts["unreachable"] = len(slots) - sum(counts.values())
    if counts["unreachable"] < 0 or sum(counts.values()) != len(slots):
        raise AssertionError("AOT slot partition is neither disjoint nor complete")
    for category in ("system", "alu", "memory"):
        counts[f"emitted_core_{category}"] = sum(
            op.category == f"direct_core_{category}"
            for op in emitted.values()
        )
    return counts


def _content_digest(contents: dict[str, bytes]) -> str:
    digest = hashlib.sha256()
    for name in sorted(contents):
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(contents[name]).digest())
    return digest.hexdigest()


def _write_if_different(path: Path, data: bytes) -> bool:
    """Atomically update a generated file only when its bytes changed."""

    try:
        if path.read_bytes() == data:
            return False
    except FileNotFoundError:
        pass
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_bytes(data)
    temporary.replace(path)
    return True


def _reuse_generated_output(
    output: Path,
    *,
    code_sha256: str,
    inventory_sha256: str,
    boundary_audit_sha256: str,
    native_blocks_sha256: str | None,
    generator_sha256: str,
    generator_bundle_sha256: str,
    generator_sources: dict[str, str],
    generator_dependencies: dict[str, str],
    base: int,
    executable_size: int | None,
    shard_size: int,
) -> GenerationResult | None:
    manifest_path = output / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return None
    if not isinstance(manifest, dict):
        return None
    inputs = manifest.get("inputs")
    configuration = manifest.get("configuration")
    if not isinstance(inputs, dict) or not isinstance(configuration, dict):
        return None
    expected = (
        manifest.get("schema") == SCHEMA
        and manifest.get("base") == base
        and manifest.get("code_sha256") == code_sha256
        and manifest.get("generator_sha256") == generator_sha256
        and manifest.get("generator_bundle_sha256")
        == generator_bundle_sha256
        and manifest.get("generator_sources") == generator_sources
        and manifest.get("generator_dependencies")
        == generator_dependencies
        and inputs.get("inventory_sha256") == inventory_sha256
        and inputs.get("boundary_audit_sha256") == boundary_audit_sha256
        and inputs.get("native_blocks_sha256") == native_blocks_sha256
        and configuration.get("shard_size") == shard_size
        and configuration.get("executable_size") == executable_size
    )
    records = manifest.get("files")
    if not expected or not isinstance(records, list):
        return None

    names: set[str] = set()
    for record in records:
        if not isinstance(record, dict):
            return None
        name = record.get("path")
        if not isinstance(name, str) or Path(name).name != name or name in names:
            return None
        path = output / name
        try:
            if path.stat().st_size != record.get("bytes"):
                return None
        except OSError:
            return None
        if _sha256_path(path) != record.get("sha256"):
            return None
        names.add(name)

    actual_generated = {GENERATED_HEADER, REGISTRY_SOURCE} | {
        path.name for path in output.glob("shard_*.cpp")
    }
    if names != actual_generated:
        return None
    files = tuple(output / name for name in sorted(names | {"manifest.json"}))
    return GenerationResult(manifest, files)


def generate(
    *,
    code_path: Path,
    inventory_path: Path,
    boundary_audit_path: Path,
    native_blocks_path: Path | None,
    output: Path,
    base: int = DEFAULT_BASE,
    executable_size: int | None = None,
    expected_sha256: str | None = None,
    shard_size: int = DEFAULT_SHARD_SIZE,
) -> GenerationResult:
    if base < 0 or base > 0xFFFFFFFF or base % 4:
        raise ValueError("base must be an aligned uint32 address")
    if executable_size is not None and (
        executable_size <= 0 or executable_size % 4
    ):
        raise ValueError("executable size must be a positive multiple of four")
    if shard_size <= 0:
        raise ValueError("shard size must be positive")
    code = code_path.read_bytes()
    code_sha256 = _sha256_bytes(code)
    if expected_sha256 is not None and code_sha256.lower() != expected_sha256.lower():
        raise ValueError(
            f"code SHA-256 mismatch: expected {expected_sha256.lower()}, "
            f"got {code_sha256}"
        )
    inventory_sha256 = _sha256_path(inventory_path)
    boundary_audit_sha256 = _sha256_path(boundary_audit_path)
    native_blocks_sha256 = (
        _sha256_path(native_blocks_path) if native_blocks_path is not None else None
    )
    native_blocks: set[int] = set()
    if native_blocks_path is not None:
        native_document = json.loads(native_blocks_path.read_text(encoding="utf-8"))
        if native_document.get("format") != "oot3d_true_aot_blocks_v1":
            raise ValueError("invalid true-AOT block manifest")
        native_blocks = {int(item["pc"]) for item in native_document.get("blocks", [])}
    (
        generator_bundle_sha256,
        generator_sources,
        generator_dependencies,
    ) = _generator_fingerprint()
    generator_sha256 = generator_sources[
        f"{_GENERATOR_SOURCE_PREFIX}/a32_cpp_aot.py"
    ]
    reused = _reuse_generated_output(
        output,
        code_sha256=code_sha256,
        inventory_sha256=inventory_sha256,
        boundary_audit_sha256=boundary_audit_sha256,
        native_blocks_sha256=native_blocks_sha256,
        generator_sha256=generator_sha256,
        generator_bundle_sha256=generator_bundle_sha256,
        generator_sources=generator_sources,
        generator_dependencies=generator_dependencies,
        base=base,
        executable_size=executable_size,
        shard_size=shard_size,
    )
    if reused is not None:
        return reused
    functions, callable_functions, slots, callable_slots = _load_inputs(
        inventory_path, boundary_audit_path, len(code), base
    )
    decoder = _Decoder(code, base)
    if executable_size is not None and executable_size > len(code):
        raise ValueError("executable size exceeds the code image")
    # Ghidra function bodies can contain disjoint address ranges. Inventory
    # intervals remain the only callable roots and coverage denominator, while
    # proven direct control flow may recover body chunks anywhere in .text.
    flow_slots = slots
    if executable_size is not None:
        flow_slots = slots | set(range(base, base + executable_size, 4))
    # Callable entries are the only roots, but direct control flow may enter an
    # audited internal boundary outside the callable interval union.  Permit
    # those reached slots in generated output without promoting the boundary to
    # a standalone function or inflating the callable coverage denominator.
    flow, literal_data, literal_sources, fixed_point_iterations = (
        _address_aware_flow(
            decoder,
            flow_slots,
            (function.entry for function in callable_functions),
            flow_slots if executable_size is not None else callable_slots,
        )
    )
    # A reached undecodable word is material runtime work, not silent absence:
    # emit it as the terminal Unsupported op so the callback receives raw+PC.
    emitted = _emitted_ops(
        decoder, flow.decoded_reachable | flow.unknown_stops
    )
    if set(emitted) & literal_data:
        raise AssertionError("literal data leaked into emitted AOT operations")
    blocks = _make_blocks(decoder, emitted, flow.block_starts, shard_size)
    block_entries = {block.pc for block in blocks}
    missing_native_blocks = native_blocks - block_entries
    if missing_native_blocks:
        raise ValueError(
            "true-AOT entry is not a generated block: "
            f"0x{min(missing_native_blocks):08X}"
        )
    shards = _make_shards(blocks, shard_size)
    flattened_pcs = [op.pc for shard in shards for block in shard.blocks for op in block.ops]
    if len(flattened_pcs) != len(set(flattened_pcs)):
        raise AssertionError("generated shards contain duplicate PCs")
    if set(flattened_pcs) != set(emitted):
        raise AssertionError("generated shards do not cover the emitted PC set exactly")

    callable_emitted = {
        pc: op for pc, op in emitted.items() if pc in callable_slots
    }
    internal_emitted = {
        pc: op for pc, op in emitted.items() if pc not in callable_slots
    }
    callable_literal_data = literal_data & callable_slots
    partition = _partition_counts(
        callable_slots, callable_emitted, callable_literal_data
    )
    inventory_partition = _partition_counts(
        slots,
        {pc: op for pc, op in emitted.items() if pc in slots},
        literal_data & slots,
    )
    baseline_raw = sum(
        is_instruction_liftable(decoder.get(pc)) for pc in callable_slots
    )
    baseline_code = sum(
        is_instruction_liftable(decoder.get(pc))
        for pc in callable_slots
        if pc not in literal_data
    )
    reachable_code = len(callable_emitted)
    fallback_count = sum(
        op.category == "fallback" for op in callable_emitted.values()
    )
    internal_categories = {
        category: sum(op.category == category for op in internal_emitted.values())
        for category in (
            "fast_path",
            "direct_core_system",
            "direct_core_alu",
            "direct_core_memory",
            "direct_vfp_transport",
            "direct_vfp_scalar",
            "delegated_vfp",
            "fallback",
        )
    }
    embedded_ascii = (
        _embedded_ascii_words(decoder, flow.decoded_reachable, slots)
        & literal_data
    )
    embedded_utf16 = (
        _embedded_utf16_words(decoder, flow.decoded_reachable, slots)
        & literal_data
    )
    relative_pointer_tables = (
        _relative_pointer_table_words(
            decoder, flow.decoded_reachable, slots
        )
        & literal_data
    )
    counts: dict[str, int] = {
        "inventory_functions": len(functions),
        "callable_entries": len(callable_functions),
        "excluded_boundaries": len(functions) - len(callable_functions),
        "total_interval_slots": len(callable_slots),
        "inventory_unique": len(slots),
        "baseline_liftable_raw": baseline_raw,
        "baseline_liftable": baseline_code,
        "reachable_code": reachable_code,
        "aot_predecoded": reachable_code - fallback_count,
        "generated_ops": len(emitted),
        "reachable_internal": len(internal_emitted),
        "blocks": len(blocks),
        "shards": len(shards),
        "literal_sources": len({source for sources in literal_sources.values() for source in sources}),
        "embedded_ascii_words": len(embedded_ascii),
        "embedded_utf16_words": len(embedded_utf16),
        "relative_pointer_table_words": len(relative_pointer_tables),
        "explicit_lr_call_sites": len(flow.explicit_lr_calls),
        "pointer_roots": len(flow.pointer_roots),
        "pointer_root_sources": len(
            {
                source
                for sources in flow.pointer_sources.values()
                for source in sources
            }
        ),
        "unknown_cfg_stops": len(flow.unknown_stops),
        **partition,
    }
    counts["direct_cpp"] = (
        partition["emitted_fast_path"]
        + partition["emitted_core"]
        + partition["emitted_vfp_transport"]
        + partition["emitted_vfp_scalar"]
    )
    counts["delegated"] = partition["emitted_delegated_vfp"]
    counts["data_or_unreachable"] = (
        partition["literal_data"] + partition["unreachable"]
    )
    bytes_by_category = {
        key: value * 4
        for key, value in counts.items()
        if key
        in {
            "total_interval_slots",
            "baseline_liftable_raw",
            "baseline_liftable",
            "reachable_code",
            "aot_predecoded",
            "direct_cpp",
            "delegated",
            "emitted_fast_path",
            "emitted_core",
            "emitted_vfp_transport",
            "emitted_vfp_scalar",
            "emitted_delegated_vfp",
            "fallback",
            "literal_data",
            "unreachable",
            "data_or_unreachable",
        }
    }
    callable_fallback = sum(
        op.category == "fallback" for op in callable_emitted.values()
    )
    callable_predecoded = len(callable_emitted) - callable_fallback
    metrics = {
        "inventory_functions": len(functions),
        "callable_functions": len(callable_functions),
        "excluded_functions": len(functions) - len(callable_functions),
        "inventory_unique_slots": len(slots),
        "callable_unique_slots": len(callable_slots),
        "reachable_slots": len(callable_emitted),
        "predecoded_slots": callable_predecoded,
        "direct_cpp_slots": counts["direct_cpp"],
        "direct_core_slots": partition["emitted_core"],
        "direct_vfp_transport_slots": partition["emitted_vfp_transport"],
        "direct_vfp_scalar_slots": partition["emitted_vfp_scalar"],
        "delegated_vfp_slots": partition["emitted_delegated_vfp"],
        "fallback_slots": callable_fallback,
        "data_or_unreachable_slots": len(callable_slots) - len(callable_emitted),
        "reached_internal_slots": len(internal_emitted),
    }
    if metrics["reachable_slots"] != (
        metrics["predecoded_slots"] + metrics["fallback_slots"]
    ) or metrics["callable_unique_slots"] != (
        metrics["reachable_slots"] + metrics["data_or_unreachable_slots"]
    ) or metrics["predecoded_slots"] != (
        metrics["direct_cpp_slots"]
        + metrics["delegated_vfp_slots"]
    ):
        raise AssertionError("callable AOT metrics do not form an exact partition")

    text_contents: dict[str, str] = {
        GENERATED_HEADER: _render_header(),
        REGISTRY_SOURCE: _render_registry(shards, callable_functions),
    }
    for shard in shards:
        text_contents[f"shard_{shard.index:04d}.cpp"] = _render_shard(
            shard, native_blocks
        )
    contents = {
        name: content.encode("utf-8") for name, content in text_contents.items()
    }
    file_records = [
        {
            "path": name,
            "bytes": len(contents[name]),
            "sha256": _sha256_bytes(contents[name]),
        }
        for name in sorted(contents)
    ]
    manifest: dict[str, object] = {
        "schema": SCHEMA,
        "base": base,
        "code_sha256": code_sha256,
        "generator_sha256": generator_sha256,
        "generator_bundle_sha256": generator_bundle_sha256,
        "generator_sources": generator_sources,
        "generator_dependencies": generator_dependencies,
        "inputs": {
            "inventory_sha256": inventory_sha256,
            "boundary_audit_sha256": boundary_audit_sha256,
            "native_blocks_sha256": native_blocks_sha256,
        },
        "configuration": {
            "shard_size": shard_size,
            "executable_size": executable_size,
            "literal_cfg_fixed_point_iterations": fixed_point_iterations,
        },
        "counts": counts,
        "metrics": metrics,
        "bytes": bytes_by_category,
        "partition": {
            key: {"slots": value, "bytes": value * 4}
            for key, value in partition.items()
        },
        "inventory_partition": {
            key: {"slots": value, "bytes": value * 4}
            for key, value in inventory_partition.items()
        },
        "reached_internal": {
            "slots": len(internal_emitted),
            "bytes": len(internal_emitted) * 4,
            **internal_categories,
        },
        "files": file_records,
        "generated_content_sha256": _content_digest(contents),
    }
    manifest_bytes = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode(
        "utf-8"
    )

    output.mkdir(parents=True, exist_ok=True)
    expected_names = set(contents) | {"manifest.json"}
    for stale in output.glob("shard_*.cpp"):
        if stale.name not in expected_names:
            stale.unlink()
    for name, data in contents.items():
        _write_if_different(output / name, data)
    _write_if_different(output / "manifest.json", manifest_bytes)
    files = tuple(output / name for name in sorted(expected_names))
    return GenerationResult(manifest, files)


def build_parser() -> argparse.ArgumentParser:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(
        description="Generate deterministic address-aware OoT3D A32 C++ AOT shards."
    )
    parser.add_argument(
        "--code",
        type=Path,
        default=root / "build/oot3d-native/extracted/exefs/code.bin",
    )
    parser.add_argument(
        "--inventory",
        type=Path,
        default=root / "analysis/codebin_function_inventory.csv",
    )
    parser.add_argument(
        "--boundary-audit",
        type=Path,
        default=root / "analysis/codebin_callable_boundary_residue_audit_166.csv",
    )
    parser.add_argument("--native-blocks", type=Path)
    parser.add_argument(
        "--output", type=Path, default=root / "build/generated/oot3d_a32"
    )
    parser.add_argument("--base", type=_parse_int, default=DEFAULT_BASE)
    parser.add_argument("--executable-size", type=_parse_int)
    parser.add_argument("--expected-sha256")
    parser.add_argument("--shard-size", type=int, default=DEFAULT_SHARD_SIZE)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    result = generate(
        code_path=args.code,
        inventory_path=args.inventory,
        boundary_audit_path=args.boundary_audit,
        native_blocks_path=args.native_blocks,
        output=args.output,
        base=args.base,
        executable_size=args.executable_size,
        expected_sha256=args.expected_sha256,
        shard_size=args.shard_size,
    )
    counts = result.manifest["counts"]
    assert isinstance(counts, dict)
    print(
        "A32 C++ AOT: "
        f"{counts['reachable_code']}/{counts['total_interval_slots']} reachable slots, "
        f"{counts['reachable_internal']} reached internal slots, "
        f"{counts['literal_data']} literal-data slots, "
        f"{counts['shards']} shards"
    )
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
