"""Build a backend-neutral structural program from the pinned OOT3D A32 front-end.

This module deliberately does not emit the existing PackedOp representation.  It
turns the reviewed code/data split and CFG into functions, shared basic blocks,
static calls, address-taken entries and explicit dynamic exits.  A later backend
can lower the normalized instruction records to C++ or LLVM IR without changing
the recovery pass.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter, deque
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


ROOT = Path(__file__).resolve().parent
REPO_ROOT = ROOT.parents[2]
UPSTREAM = ROOT / "upstream"
UPSTREAM_SRC = UPSTREAM / "src"
if str(UPSTREAM_SRC) not in sys.path:
    sys.path.insert(0, str(UPSTREAM_SRC))

from oot3d_pack import a32_cpp_aot as pinned  # noqa: E402
from oot3d_pack.arm_decode import DecodedInstruction  # noqa: E402
from oot3d_pack.arm_ir import (  # noqa: E402
    arm_core_runtime_category,
    arm_core_supported,
    arm_vfp_scalar_supported,
    arm_vfp_transport_supported,
)


FORMAT = "oot3d_whole_aot_program_v1"
CACHE_FORMAT = "oot3d_whole_aot_program_cache_v1"
DEFAULT_BASE = 0x00100000


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_path(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _is_always(item: DecodedInstruction) -> bool:
    return item.condition in {None, "al"}


def _is_register_call(item: DecodedInstruction) -> bool:
    return (
        item.kind == "indirect_branch"
        and (item.raw & 0x0FFFFFF0) == 0x012FFF30
    )


def _is_return(item: DecodedInstruction) -> bool:
    if item.kind == "indirect_branch":
        return (
            (item.raw & 0x0FFFFFF0) == 0x012FFF10
            and (item.raw & 0xF) == 14
        )
    if (
        item.kind == "data_processing"
        and item.rd == 15
        and item.opcode == "mov"
        and item.rm == 14
    ):
        return True
    raw = item.raw
    if (
        (raw & 0x0E000000) == 0x08000000
        and bool(raw & (1 << 20))
        and bool(raw & (1 << 15))
        and ((raw >> 16) & 0xF) == 13
    ):
        return True
    return bool(
        item.kind == "memory"
        and item.load
        and item.rd == 15
        and item.rn == 13
    )


def _semantic_category(item: DecodedInstruction) -> str:
    if item.kind in {"branch", "call", "indirect_branch"}:
        return "control"
    if item.kind == "svc":
        return "service"
    if item.kind == "unknown":
        return "unknown"
    if item.state == "arm" and arm_core_supported(item.raw):
        category = arm_core_runtime_category(item.raw)
        return f"core_{category}" if category else "core"
    if item.state == "arm" and arm_vfp_transport_supported(item.raw):
        return "vfp_transport"
    if item.state == "arm" and arm_vfp_scalar_supported(item.raw):
        return "vfp_scalar"
    return f"decoded_{item.kind}"


@dataclass(frozen=True, slots=True)
class AotInstruction:
    pc: int
    raw: int
    semantic: str
    mnemonic: str
    condition: str | None
    opcode: str | None
    rd: int | None
    rn: int | None
    rm: int | None
    imm: int | None
    setflags: bool
    load: bool | None
    target: int | None
    svc_id: int | None
    family: str | None
    operand_text: str | None

    @classmethod
    def from_decoded(cls, item: DecodedInstruction) -> "AotInstruction":
        return cls(
            pc=item.pc,
            raw=item.raw,
            semantic=_semantic_category(item),
            mnemonic=item.canonical_mnemonic or item.mnemonic,
            condition=item.condition,
            opcode=item.opcode,
            rd=item.rd,
            rn=item.rn,
            rm=item.rm,
            imm=item.imm,
            setflags=item.setflags,
            load=item.load,
            target=item.target,
            svc_id=item.svc_id,
            family=item.family,
            operand_text=item.operand_text,
        )

    def to_dict(self) -> dict[str, object]:
        result: dict[str, object] = {
            "pc": self.pc,
            "raw": self.raw,
            "semantic": self.semantic,
            "mnemonic": self.mnemonic,
        }
        optional = {
            "condition": self.condition,
            "opcode": self.opcode,
            "rd": self.rd,
            "rn": self.rn,
            "rm": self.rm,
            "imm": self.imm,
            "load": self.load,
            "target": self.target,
            "svc_id": self.svc_id,
            "family": self.family,
            "operand_text": self.operand_text,
        }
        result.update({key: value for key, value in optional.items() if value is not None})
        if self.setflags:
            result["setflags"] = True
        return result


@dataclass(frozen=True, slots=True)
class AotEdge:
    site: int
    kind: str
    target: int | None = None
    condition: str | None = None
    value: int | None = None

    def to_dict(self) -> dict[str, object]:
        result: dict[str, object] = {"site": self.site, "kind": self.kind}
        if self.target is not None:
            result["target"] = self.target
        if self.condition is not None:
            result["condition"] = self.condition
        if self.value is not None:
            result["value"] = self.value
        return result


@dataclass(frozen=True, slots=True)
class AotBlock:
    index: int
    pc: int
    end_pc: int
    instructions: tuple[AotInstruction, ...]
    successors: tuple[AotEdge, ...]
    body_sha256: str

    def to_dict(self, include_instructions: bool) -> dict[str, object]:
        categories = Counter(item.semantic for item in self.instructions)
        result: dict[str, object] = {
            "id": self.index,
            "pc": self.pc,
            "end_pc": self.end_pc,
            "instruction_count": len(self.instructions),
            "body_sha256": self.body_sha256,
            "categories": dict(sorted(categories.items())),
            "successors": [edge.to_dict() for edge in self.successors],
        }
        if include_instructions:
            result["instructions"] = [item.to_dict() for item in self.instructions]
        return result


@dataclass(frozen=True, slots=True)
class AotFunction:
    entry: int
    end: int | None
    name: str
    origin: str
    block_ids: tuple[int, ...]
    direct_calls: tuple[tuple[int, int], ...]
    tail_calls: tuple[tuple[int, int], ...]
    indirect_sites: tuple[tuple[int, str], ...]
    unresolved_static_edges: tuple[AotEdge, ...]
    closed_static_cfg: bool
    body_sha256: str

    def to_dict(self) -> dict[str, object]:
        result: dict[str, object] = {
            "entry": self.entry,
            "name": self.name,
            "origin": self.origin,
            "blocks": list(self.block_ids),
            "direct_calls": [
                {"site": site, "target": target}
                for site, target in self.direct_calls
            ],
            "tail_calls": [
                {"site": site, "target": target}
                for site, target in self.tail_calls
            ],
            "indirect_sites": [
                {"site": site, "kind": kind}
                for site, kind in self.indirect_sites
            ],
            "unresolved_static_edges": [
                edge.to_dict() for edge in self.unresolved_static_edges
            ],
            "closed_static_cfg": self.closed_static_cfg,
            "body_sha256": self.body_sha256,
        }
        if self.end is not None:
            result["end"] = self.end
        return result


@dataclass(frozen=True, slots=True)
class AotProgram:
    base: int
    executable_size: int
    code_sha256: str
    inventory_sha256: str
    boundary_audit_sha256: str
    fixed_point_iterations: int
    literal_data: frozenset[int]
    pointer_roots: frozenset[int]
    blocks: tuple[AotBlock, ...]
    functions: tuple[AotFunction, ...]
    unclaimed_blocks: tuple[int, ...]

    def to_manifest(self, include_instructions: bool = False) -> dict[str, object]:
        category_counts: Counter[str] = Counter()
        for block in self.blocks:
            category_counts.update(item.semantic for item in block.instructions)
        direct_calls = sum(len(function.direct_calls) for function in self.functions)
        tail_calls = sum(len(function.tail_calls) for function in self.functions)
        indirect_sites = sum(len(function.indirect_sites) for function in self.functions)
        unresolved_static_edges = sum(
            len(function.unresolved_static_edges) for function in self.functions
        )
        return {
            "format": FORMAT,
            "base": self.base,
            "executable_size": self.executable_size,
            "code_sha256": self.code_sha256,
            "inputs": {
                "inventory_sha256": self.inventory_sha256,
                "boundary_audit_sha256": self.boundary_audit_sha256,
            },
            "configuration": {
                "literal_cfg_fixed_point_iterations": self.fixed_point_iterations,
                "instructions_embedded": include_instructions,
            },
            "counts": {
                "functions": len(self.functions),
                "inventory_functions": sum(
                    function.origin == "inventory" for function in self.functions
                ),
                "synthetic_functions": sum(
                    function.origin != "inventory" for function in self.functions
                ),
                "blocks": len(self.blocks),
                "instructions": sum(
                    len(block.instructions) for block in self.blocks
                ),
                "literal_words": len(self.literal_data),
                "pointer_roots": len(self.pointer_roots),
                "direct_calls": direct_calls,
                "tail_calls": tail_calls,
                "indirect_sites": indirect_sites,
                "unresolved_static_edges": unresolved_static_edges,
                "closed_static_cfg_functions": sum(
                    function.closed_static_cfg for function in self.functions
                ),
                "unclaimed_blocks": len(self.unclaimed_blocks),
            },
            "instruction_categories": dict(sorted(category_counts.items())),
            "unclaimed_blocks": list(self.unclaimed_blocks),
            "functions": [function.to_dict() for function in self.functions],
            "blocks": [
                block.to_dict(include_instructions) for block in self.blocks
            ],
        }


def _switch_targets(
    decoder: pinned._Decoder,
    literal_sources: dict[int, tuple[int, ...]],
    valid_pcs: set[int],
) -> dict[int, tuple[int, ...]]:
    """Recover dispatch-to-case edges from the literal table evidence."""

    mutable: dict[int, set[int]] = {}
    for word, sources in literal_sources.items():
        offset = word - decoder.base
        if offset < 0 or offset + 4 > len(decoder.code):
            continue
        target = int.from_bytes(decoder.code[offset : offset + 4], "little")
        if target not in valid_pcs:
            continue
        for source in sources:
            raw = decoder.get(source).raw
            if (raw & 0x0FFFFFF0) == 0x079FF100:
                mutable.setdefault(source, set()).add(target)
    return {
        source: tuple(sorted(targets))
        for source, targets in sorted(mutable.items())
    }


def _edge_target(
    site: int,
    kind: str,
    target: int,
    condition: str | None,
) -> AotEdge:
    return AotEdge(site=site, kind=kind, target=target, condition=condition)


def _successors(
    item: DecodedInstruction,
    explicit_lr_calls: set[int],
    switch_targets: dict[int, tuple[int, ...]],
) -> tuple[AotEdge, ...]:
    pc = item.pc
    fallthrough = pc + 4
    condition = None if _is_always(item) else item.condition
    edges: list[AotEdge] = []
    if item.kind == "branch":
        if item.target is not None:
            edges.append(_edge_target(pc, "branch", item.target, condition))
        if condition is not None:
            edges.append(_edge_target(pc, "fallthrough", fallthrough, condition))
        return tuple(edges)
    if item.kind == "call":
        if item.target is not None:
            edges.append(_edge_target(pc, "direct_call", item.target, condition))
        edges.append(_edge_target(pc, "resume", fallthrough, condition))
        return tuple(edges)
    if item.kind == "indirect_branch":
        if _is_return(item):
            edges.append(AotEdge(pc, "return", condition=condition))
            if condition is not None:
                edges.append(
                    _edge_target(pc, "fallthrough", fallthrough, condition)
                )
        elif _is_register_call(item) or pc in explicit_lr_calls:
            edges.append(AotEdge(pc, "indirect_call", condition=condition))
            edges.append(_edge_target(pc, "resume", fallthrough, condition))
        else:
            targets = switch_targets.get(pc, ())
            if targets:
                edges.extend(
                    _edge_target(pc, "switch_case", target, condition)
                    for target in targets
                )
            else:
                edges.append(AotEdge(pc, "indirect_branch", condition=condition))
            if condition is not None:
                edges.append(
                    _edge_target(pc, "fallthrough", fallthrough, condition)
                )
        return tuple(edges)
    if item.kind == "svc":
        edges.append(AotEdge(pc, "svc", condition=condition, value=item.svc_id))
        edges.append(_edge_target(pc, "resume", fallthrough, condition))
        return tuple(edges)
    if item.kind == "unknown":
        return (AotEdge(pc, "unknown", condition=condition),)
    if pinned._writes_pc(item):
        if _is_return(item):
            edges.append(AotEdge(pc, "return", condition=condition))
            if condition is not None:
                edges.append(
                    _edge_target(pc, "fallthrough", fallthrough, condition)
                )
        elif pc in explicit_lr_calls:
            edges.append(AotEdge(pc, "indirect_call", condition=condition))
            edges.append(_edge_target(pc, "resume", fallthrough, condition))
        else:
            targets = switch_targets.get(pc, ())
            if targets:
                edges.extend(
                    _edge_target(pc, "switch_case", target, condition)
                    for target in targets
                )
            else:
                edges.append(AotEdge(pc, "indirect_branch", condition=condition))
            if condition is not None:
                edges.append(
                    _edge_target(pc, "fallthrough", fallthrough, condition)
                )
        return tuple(edges)
    return (_edge_target(pc, "fallthrough", fallthrough, None),)


def _make_blocks(
    decoder: pinned._Decoder,
    reachable: set[int],
    block_starts: set[int],
    explicit_lr_calls: set[int],
    switch_targets: dict[int, tuple[int, ...]],
) -> tuple[AotBlock, ...]:
    groups: list[list[int]] = []
    current: list[int] = []
    previous: int | None = None
    for pc in sorted(reachable):
        if current and (
            previous is None
            or pc != previous + 4
            or pc in block_starts
            or pinned._ends_block(decoder.get(previous))
        ):
            groups.append(current)
            current = []
        current.append(pc)
        previous = pc
    if current:
        groups.append(current)

    blocks: list[AotBlock] = []
    for index, pcs in enumerate(groups):
        instructions = tuple(
            AotInstruction.from_decoded(decoder.get(pc)) for pc in pcs
        )
        body_digest = hashlib.sha256()
        body_digest.update(pcs[0].to_bytes(4, "little"))
        for item in instructions:
            body_digest.update(item.raw.to_bytes(4, "little"))
        terminal = decoder.get(pcs[-1])
        successors = _successors(
            terminal, explicit_lr_calls, switch_targets
        )
        blocks.append(
            AotBlock(
                index=index,
                pc=pcs[0],
                end_pc=pcs[-1] + 4,
                instructions=instructions,
                successors=successors,
                body_sha256=body_digest.hexdigest(),
            )
        )
    return tuple(blocks)


_COMPLEMENTARY_CONDITIONS = {
    "eq": "ne",
    "ne": "eq",
    "hs": "lo",
    "lo": "hs",
    "mi": "pl",
    "pl": "mi",
    "vs": "vc",
    "vc": "vs",
    "hi": "ls",
    "ls": "hi",
    "ge": "lt",
    "lt": "ge",
    "gt": "le",
    "le": "gt",
}


def _normalize_complementary_branch_chains(
    blocks: tuple[AotBlock, ...], protected_entries: set[int]
) -> tuple[AotBlock, ...]:
    """Elide a provably impossible second fallthrough.

    A common compiler idiom emits B<cond>, then one or more instructions
    predicated by <!cond>, followed by B<!cond>. The second branch is
    guaranteed on the sole path entering that block. Keep this proof
    deliberately narrow so address-taken entries, flag writers and blocks
    with another predecessor retain their original CFG.
    """

    incoming: dict[int, list[tuple[AotBlock, AotEdge]]] = {}
    for predecessor in blocks:
        for edge in predecessor.successors:
            if edge.target is not None:
                incoming.setdefault(edge.target, []).append((predecessor, edge))

    normalized: list[AotBlock] = []
    for current in blocks:
        predecessors = incoming.get(current.pc, [])
        branch_edges = [edge for edge in current.successors if edge.kind == "branch"]
        fallthrough_edges = [
            edge for edge in current.successors if edge.kind == "fallthrough"
        ]
        branch_condition = (
            branch_edges[0].condition if len(branch_edges) == 1 else None
        )
        writes_condition_flags = any(
            instruction.setflags
            or (
                (instruction.raw & 0x0FFF0FFF) == 0x0EF10A10
                and ((instruction.raw >> 12) & 0xF) == 15
            )
            or (
                (instruction.raw & 0x0FB0FFF0) == 0x0120F000
                and bool((instruction.raw >> 16) & 0x8)
            )
            or (
                (instruction.raw & 0x0E000000) == 0x08000000
                and bool(instruction.raw & (1 << 20))
                and bool(instruction.raw & (1 << 22))
                and bool(instruction.raw & (1 << 15))
            )
            for instruction in current.instructions[:-1]
        )
        can_prove = (
            current.pc not in protected_entries
            and bool(current.instructions)
            and len(predecessors) == 1
            and predecessors[0][1].kind == "fallthrough"
            and predecessors[0][0].end_pc == current.pc
            and len(branch_edges) == 1
            and len(fallthrough_edges) == 1
            and branch_condition is not None
            and predecessors[0][1].condition is not None
            and _COMPLEMENTARY_CONDITIONS.get(predecessors[0][1].condition)
            == branch_condition
            and not writes_condition_flags
            and all(
                instruction.condition == branch_condition
                for instruction in current.instructions
            )
        )
        if not can_prove:
            normalized.append(current)
            continue
        guaranteed_branch = AotEdge(
            site=branch_edges[0].site,
            kind="branch",
            target=branch_edges[0].target,
            condition=None,
            value=branch_edges[0].value,
        )
        normalized.append(
            AotBlock(
                index=current.index,
                pc=current.pc,
                end_pc=current.end_pc,
                instructions=current.instructions,
                successors=(guaranteed_branch,),
                body_sha256=current.body_sha256,
            )
        )
    return tuple(normalized)


def _function_closure(
    entry: int,
    entry_to_block: dict[int, int],
    blocks: tuple[AotBlock, ...],
    function_entries: set[int],
) -> tuple[
    tuple[int, ...],
    tuple[tuple[int, int], ...],
    tuple[tuple[int, int], ...],
    tuple[tuple[int, str], ...],
    tuple[AotEdge, ...],
    bool,
]:
    start = entry_to_block.get(entry)
    if start is None:
        return (), (), (), (), (), False
    queue = deque([start])
    visited: set[int] = set()
    direct_calls: set[tuple[int, int]] = set()
    tail_calls: set[tuple[int, int]] = set()
    indirect_sites: set[tuple[int, str]] = set()
    unresolved: set[AotEdge] = set()
    closed = True
    traversable = {"branch", "fallthrough", "resume", "switch_case"}
    while queue:
        block_id = queue.popleft()
        if block_id in visited:
            continue
        visited.add(block_id)
        block = blocks[block_id]
        for edge in block.successors:
            if edge.kind == "direct_call":
                if edge.target is not None:
                    direct_calls.add((edge.site, edge.target))
                    if edge.target not in entry_to_block:
                        unresolved.add(edge)
                        closed = False
                else:
                    unresolved.add(edge)
                    closed = False
                continue
            if edge.kind in {"indirect_call", "indirect_branch"}:
                indirect_sites.add((edge.site, edge.kind))
                continue
            if edge.kind == "unknown":
                indirect_sites.add((edge.site, edge.kind))
                unresolved.add(edge)
                closed = False
                continue
            if edge.kind not in traversable:
                continue
            if edge.target is None:
                unresolved.add(edge)
                closed = False
                continue
            target_block = entry_to_block.get(edge.target)
            if target_block is None:
                unresolved.add(edge)
                closed = False
                continue
            # A static branch to another entry is a tail call.  Fallthrough,
            # call resume and switch cases may legitimately enter an
            # address-taken/internal label, so their blocks remain shared by
            # both function closures.
            if (
                edge.kind == "branch"
                and edge.target in function_entries
                and edge.target != entry
            ):
                tail_calls.add((edge.site, edge.target))
                continue
            queue.append(target_block)
    return (
        tuple(sorted(visited)),
        tuple(sorted(direct_calls)),
        tuple(sorted(tail_calls)),
        tuple(sorted(indirect_sites)),
        tuple(
            sorted(
                unresolved,
                key=lambda edge: (
                    edge.site,
                    edge.kind,
                    -1 if edge.target is None else edge.target,
                ),
            )
        ),
        closed,
    )


def _body_hash(block_ids: Iterable[int], blocks: tuple[AotBlock, ...]) -> str:
    digest = hashlib.sha256()
    for block_id in sorted(block_ids):
        block = blocks[block_id]
        digest.update(bytes.fromhex(block.body_sha256))
    return digest.hexdigest()


def build_program(
    code: bytes,
    inventory_path: Path,
    boundary_audit_path: Path,
    *,
    base: int = DEFAULT_BASE,
    executable_size: int | None = None,
) -> AotProgram:
    if executable_size is None:
        executable_size = len(code)
    if executable_size <= 0 or executable_size > len(code):
        raise ValueError("executable size is outside the code image")
    _, callable_functions, slots, _ = pinned._load_inputs(
        inventory_path, boundary_audit_path, len(code), base
    )
    decoder = pinned._Decoder(code, base)
    flow_slots = slots | set(range(base, base + executable_size, 4))
    flow, literal_data, literal_sources, iterations = pinned._address_aware_flow(
        decoder,
        flow_slots,
        (function.entry for function in callable_functions),
        flow_slots,
    )
    reachable = flow.decoded_reachable | flow.unknown_stops
    dynamic_targets = _switch_targets(decoder, literal_sources, reachable)
    blocks = _make_blocks(
        decoder,
        reachable,
        flow.block_starts,
        flow.explicit_lr_calls,
        dynamic_targets,
    )
    entry_to_block = {block.pc: block.index for block in blocks}
    if len(entry_to_block) != len(blocks):
        raise AssertionError("duplicate basic-block entry")

    inventory_by_entry = {function.entry: function for function in callable_functions}
    direct_targets = {
        edge.target
        for block in blocks
        for edge in block.successors
        if edge.kind == "direct_call" and edge.target in entry_to_block
    }
    roots = set(inventory_by_entry) | set(flow.pointer_roots) | direct_targets
    roots &= set(entry_to_block)
    blocks = _normalize_complementary_branch_chains(blocks, roots)

    def root_record(entry: int) -> tuple[int | None, str, str]:
        record = inventory_by_entry.get(entry)
        if record is not None:
            return record.end, record.name, "inventory"
        if entry in flow.pointer_roots:
            return None, f"address_taken_{entry:08X}", "address_taken"
        return None, f"direct_target_{entry:08X}", "direct_target"

    function_entries = set(roots)
    closure_records: list[
        tuple[
            int,
            int | None,
            str,
            str,
            tuple[int, ...],
            tuple[tuple[int, int], ...],
            tuple[tuple[int, int], ...],
            tuple[tuple[int, str], ...],
            tuple[AotEdge, ...],
            bool,
        ]
    ] = []
    claimed: set[int] = set()
    for entry in sorted(roots):
        end, name, origin = root_record(entry)
        block_ids, calls, tails, indirect, unresolved, closed = _function_closure(
            entry, entry_to_block, blocks, function_entries
        )
        claimed.update(block_ids)
        closure_records.append(
            (
                entry,
                end,
                name,
                origin,
                block_ids,
                calls,
                tails,
                indirect,
                unresolved,
                closed,
            )
        )

    # Any residue reached by the reviewed global CFG is preserved explicitly.
    # It is not silently attributed to the nearest Ghidra interval.
    for block_id in range(len(blocks)):
        if block_id in claimed:
            continue
        entry = blocks[block_id].pc
        function_entries.add(entry)
        block_ids, calls, tails, indirect, unresolved, closed = _function_closure(
            entry, entry_to_block, blocks, function_entries
        )
        if not block_ids:
            block_ids = (block_id,)
            closed = False
        claimed.update(block_ids)
        closure_records.append(
            (
                entry,
                None,
                f"cfg_residue_{entry:08X}",
                "cfg_residue",
                block_ids,
                calls,
                tails,
                indirect,
                unresolved,
                closed,
            )
        )

    aot_functions = tuple(
        AotFunction(
            entry=entry,
            end=end,
            name=name,
            origin=origin,
            block_ids=block_ids,
            direct_calls=calls,
            tail_calls=tails,
            indirect_sites=indirect,
            unresolved_static_edges=unresolved,
            closed_static_cfg=closed,
            body_sha256=_body_hash(block_ids, blocks),
        )
        for (
            entry,
            end,
            name,
            origin,
            block_ids,
            calls,
            tails,
            indirect,
            unresolved,
            closed,
        )
        in sorted(closure_records, key=lambda row: row[0])
    )
    unclaimed = tuple(sorted(set(range(len(blocks))) - claimed))
    return AotProgram(
        base=base,
        executable_size=executable_size,
        code_sha256=_sha256_bytes(code),
        inventory_sha256=_sha256_path(inventory_path),
        boundary_audit_sha256=_sha256_path(boundary_audit_path),
        fixed_point_iterations=iterations,
        literal_data=frozenset(literal_data),
        pointer_roots=frozenset(flow.pointer_roots),
        blocks=blocks,
        functions=aot_functions,
        unclaimed_blocks=unclaimed,
    )


def _write_if_different(path: Path, contents: bytes) -> bool:
    if path.is_file() and path.read_bytes() == contents:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(contents)
    return True


def _cache_key(args: argparse.Namespace) -> str:
    digest = hashlib.sha256()
    digest.update(FORMAT.encode("ascii"))
    digest.update(str(args.base).encode("ascii"))
    digest.update(str(args.executable_size).encode("ascii"))
    digest.update(str(bool(args.include_instructions)).encode("ascii"))
    sources = (
        Path(__file__),
        UPSTREAM_SRC / "oot3d_pack/a32_cpp_aot.py",
        UPSTREAM_SRC / "oot3d_pack/arm_decode.py",
        UPSTREAM_SRC / "oot3d_pack/arm_ir.py",
    )
    for path in (*sources, args.code, args.inventory, args.boundary_audit):
        digest.update(str(path.resolve()).encode("utf-8"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(_sha256_path(path)))
    return digest.hexdigest()


def _stamp_path(output: Path) -> Path:
    return output.with_name(output.name + ".stamp.json")


def _read_cached_counts(
    output: Path, cache_key: str
) -> dict[str, int] | None:
    stamp_path = _stamp_path(output)
    if not output.is_file() or not stamp_path.is_file():
        return None
    try:
        stamp = json.loads(stamp_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    if (
        stamp.get("format") != CACHE_FORMAT
        or stamp.get("cache_key") != cache_key
        or stamp.get("output_bytes") != output.stat().st_size
        or stamp.get("output_sha256") != _sha256_path(output)
    ):
        return None
    counts = stamp.get("counts")
    if not isinstance(counts, dict) or not all(
        isinstance(key, str) and isinstance(value, int)
        for key, value in counts.items()
    ):
        return None
    return counts


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Extract the structural OOT3D whole-program AOT artifact."
    )
    parser.add_argument("--code", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--boundary-audit", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--base", type=lambda value: int(value, 0), default=DEFAULT_BASE)
    parser.add_argument("--executable-size", type=lambda value: int(value, 0))
    parser.add_argument("--include-instructions", action="store_true")
    parser.add_argument("--force", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    cache_key = _cache_key(args)
    if not args.force:
        cached_counts = _read_cached_counts(args.output, cache_key)
        if cached_counts is not None:
            print(
                "Whole AOT program: "
                f"{cached_counts['functions']} functions, "
                f"{cached_counts['blocks']} blocks, "
                f"{cached_counts['instructions']} instructions, "
                f"{cached_counts['unclaimed_blocks']} unclaimed; "
                f"cached {args.output}"
            )
            return 0
    code = args.code.read_bytes()
    program = build_program(
        code,
        args.inventory,
        args.boundary_audit,
        base=args.base,
        executable_size=args.executable_size,
    )
    manifest = program.to_manifest(args.include_instructions)
    contents = (
        json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n"
    ).encode("utf-8")
    changed = _write_if_different(args.output, contents)
    counts = manifest["counts"]
    assert isinstance(counts, dict)
    stamp = {
        "format": CACHE_FORMAT,
        "cache_key": cache_key,
        "output_bytes": len(contents),
        "output_sha256": _sha256_bytes(contents),
        "counts": counts,
    }
    _write_if_different(
        _stamp_path(args.output),
        (json.dumps(stamp, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    print(
        "Whole AOT program: "
        f"{counts['functions']} functions, {counts['blocks']} blocks, "
        f"{counts['instructions']} instructions, "
        f"{counts['unclaimed_blocks']} unclaimed; "
        f"{'wrote' if changed else 'cached'} {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
