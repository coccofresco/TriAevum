from __future__ import annotations

from dataclasses import dataclass

try:
    from capstone import (
        CS_ARCH_ARM,
        CS_GRP_CALL,
        CS_GRP_JUMP,
        CS_GRP_RET,
        CS_MODE_ARM,
        CS_MODE_LITTLE_ENDIAN,
        CS_MODE_THUMB,
        CS_OP_IMM,
        Cs,
    )
except ImportError:  # pragma: no cover - the declared dependency is normally present.
    Cs = None  # type: ignore[assignment,misc]


@dataclass(frozen=True)
class DecodedInstruction:
    pc: int
    state: str
    size: int
    raw: int
    mnemonic: str
    kind: str
    condition: str | None = None
    target: int | None = None
    thumb_target: bool | None = None
    svc_id: int | None = None
    opcode: str | None = None
    rd: int | None = None
    rn: int | None = None
    rm: int | None = None
    imm: int | None = None
    setflags: bool = False
    load: bool | None = None
    family: str | None = None
    canonical_mnemonic: str | None = None
    operand_text: str | None = None
    decoder: str = "native"


_COND = [
    "eq",
    "ne",
    "cs",
    "cc",
    "mi",
    "pl",
    "vs",
    "vc",
    "hi",
    "ls",
    "ge",
    "lt",
    "gt",
    "le",
    "al",
    "nv",
]


if Cs is not None:
    _CAPSTONE_ARM = Cs(CS_ARCH_ARM, CS_MODE_ARM | CS_MODE_LITTLE_ENDIAN)
    _CAPSTONE_THUMB = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN)
    _CAPSTONE_ARM.detail = True
    _CAPSTONE_THUMB.detail = True
else:  # pragma: no cover - retained as a fail-soft path for source-only use.
    _CAPSTONE_ARM = None
    _CAPSTONE_THUMB = None


def decode_arm(data: bytes, offset: int, pc: int) -> DecodedInstruction:
    if offset + 4 > len(data):
        raise ValueError("not enough bytes for ARM instruction")
    raw = int.from_bytes(data[offset : offset + 4], "little")
    cond = (raw >> 28) & 0xF
    condition = _COND[cond]

    if (raw & 0x0F000000) == 0x0F000000:
        return DecodedInstruction(
            pc=pc,
            state="arm",
            size=4,
            raw=raw,
            mnemonic="svc",
            kind="svc",
            condition=condition,
            svc_id=raw & 0x00FFFFFF,
        )

    if cond != 0xF and (raw & 0x0E000000) == 0x0A000000:
        link = bool(raw & 0x01000000)
        imm24 = raw & 0x00FFFFFF
        if imm24 & 0x00800000:
            imm24 -= 0x01000000
        target = (pc + 8 + (imm24 << 2)) & 0xFFFFFFFF
        return DecodedInstruction(
            pc=pc,
            state="arm",
            size=4,
            raw=raw,
            mnemonic="bl" if link else "b",
            kind="call" if link else "branch",
            condition=condition,
            target=target,
            thumb_target=False,
        )

    data_proc = _decode_arm_data_processing(raw, pc, condition)
    if data_proc:
        return data_proc

    single_transfer = _decode_arm_single_transfer(raw, pc, condition)
    if single_transfer:
        return single_transfer

    # BX/BLX register. The target is register-dependent and must come from trace.
    if (raw & 0x0FFFFFF0) in {0x012FFF10, 0x012FFF30}:
        return DecodedInstruction(
            pc=pc,
            state="arm",
            size=4,
            raw=raw,
            mnemonic="blx_reg" if (raw & 0x20) else "bx",
            kind="indirect_branch",
            condition=condition,
        )

    fallback = _decode_with_capstone(data, offset, pc, "arm", None if cond == 0xF else condition)
    if fallback is not None:
        return fallback

    return DecodedInstruction(
        pc=pc,
        state="arm",
        size=4,
        raw=raw,
        mnemonic="unknown",
        kind="unknown",
        condition=condition,
    )


def _decode_arm_data_processing(raw: int, pc: int, condition: str) -> DecodedInstruction | None:
    if (raw & 0x0C000000) != 0:
        return None
    immediate = bool(raw & (1 << 25))
    opcode_id = (raw >> 21) & 0xF
    setflags = bool(raw & (1 << 20))
    rn = (raw >> 16) & 0xF
    rd = (raw >> 12) & 0xF
    operand2 = raw & 0xFFF

    opcodes = {
        0x2: "sub",
        0x4: "add",
        0xA: "cmp",
        0xD: "mov",
    }
    opcode = opcodes.get(opcode_id)
    if opcode is None:
        return None
    if opcode == "cmp":
        setflags = True

    imm = None
    rm = None
    if immediate:
        imm8 = operand2 & 0xFF
        rotate = ((operand2 >> 8) & 0xF) * 2
        imm = _ror(imm8, rotate)
    else:
        # Only the simple register operand form is lifted initially.
        if operand2 & 0xFF0:
            return None
        rm = operand2 & 0xF

    return DecodedInstruction(
        pc=pc,
        state="arm",
        size=4,
        raw=raw,
        mnemonic=opcode,
        kind="data_processing",
        condition=condition,
        opcode=opcode,
        rd=rd,
        rn=rn,
        rm=rm,
        imm=imm,
        setflags=setflags,
    )


def _decode_arm_single_transfer(raw: int, pc: int, condition: str) -> DecodedInstruction | None:
    if (raw & 0x0C000000) != 0x04000000:
        return None
    immediate_offset = not bool(raw & (1 << 25))
    pre_index = bool(raw & (1 << 24))
    add_offset = bool(raw & (1 << 23))
    byte_transfer = bool(raw & (1 << 22))
    writeback = bool(raw & (1 << 21))
    load = bool(raw & (1 << 20))
    rn = (raw >> 16) & 0xF
    rd = (raw >> 12) & 0xF
    if not immediate_offset or not pre_index or byte_transfer or writeback:
        return None
    offset = raw & 0xFFF
    if not add_offset:
        offset = -offset
    return DecodedInstruction(
        pc=pc,
        state="arm",
        size=4,
        raw=raw,
        mnemonic="ldr" if load else "str",
        kind="memory",
        condition=condition,
        opcode="ldr" if load else "str",
        rd=rd,
        rn=rn,
        imm=offset,
        load=load,
    )


def _ror(value: int, amount: int) -> int:
    amount &= 31
    value &= 0xFFFFFFFF
    if amount == 0:
        return value
    return ((value >> amount) | (value << (32 - amount))) & 0xFFFFFFFF


def decode_thumb(data: bytes, offset: int, pc: int) -> DecodedInstruction:
    if offset + 2 > len(data):
        raise ValueError("not enough bytes for Thumb instruction")
    half = int.from_bytes(data[offset : offset + 2], "little")

    data_proc = _decode_thumb_data_processing(half, pc)
    if data_proc:
        return data_proc

    memory = _decode_thumb_memory(half, pc)
    if memory:
        return memory

    if (half & 0xF800) == 0xE000:
        imm11 = half & 0x07FF
        if imm11 & 0x400:
            imm11 -= 0x800
        target = (pc + 4 + (imm11 << 1)) & 0xFFFFFFFF
        return DecodedInstruction(
            pc=pc,
            state="thumb",
            size=2,
            raw=half,
            mnemonic="b",
            kind="branch",
            target=target,
            thumb_target=True,
        )

    if (half & 0xF000) == 0xD000 and (half & 0x0F00) != 0x0F00:
        cond = (half >> 8) & 0xF
        imm8 = half & 0xFF
        if imm8 & 0x80:
            imm8 -= 0x100
        target = (pc + 4 + (imm8 << 1)) & 0xFFFFFFFF
        return DecodedInstruction(
            pc=pc,
            state="thumb",
            size=2,
            raw=half,
            mnemonic="b" + _COND[cond],
            kind="branch",
            condition=_COND[cond],
            target=target,
            thumb_target=True,
        )

    if half == 0xDF00 or (half & 0xFF00) == 0xDF00:
        return DecodedInstruction(
            pc=pc,
            state="thumb",
            size=2,
            raw=half,
            mnemonic="svc",
            kind="svc",
            svc_id=half & 0xFF,
        )

    if (half & 0xFF87) in {0x4700, 0x4780}:
        return DecodedInstruction(
            pc=pc,
            state="thumb",
            size=2,
            raw=half,
            mnemonic="blx_reg" if (half & 0x0080) else "bx",
            kind="indirect_branch",
        )

    # Thumb-2 BL/BLX immediate pair.
    if offset + 4 <= len(data) and (half & 0xF800) == 0xF000:
        second = int.from_bytes(data[offset + 2 : offset + 4], "little")
        if (second & 0xD000) == 0xD000:
            s = (half >> 10) & 1
            j1 = (second >> 13) & 1
            j2 = (second >> 11) & 1
            i1 = (~(j1 ^ s)) & 1
            i2 = (~(j2 ^ s)) & 1
            imm10 = half & 0x03FF
            imm11 = second & 0x07FF
            imm25 = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1)
            if s:
                imm25 -= 1 << 25
            target = (pc + 4 + imm25) & 0xFFFFFFFF
            return DecodedInstruction(
                pc=pc,
                state="thumb",
                size=4,
                raw=half | (second << 16),
                mnemonic="bl",
                kind="call",
                target=target,
                thumb_target=True,
            )

    fallback = _decode_with_capstone(data, offset, pc, "thumb", None)
    if fallback is not None:
        return fallback

    return DecodedInstruction(
        pc=pc,
        state="thumb",
        size=2,
        raw=half,
        mnemonic="unknown",
        kind="unknown",
    )


def _decode_thumb_data_processing(half: int, pc: int) -> DecodedInstruction | None:
    # MOVS/CMP/ADDS/SUBS Rd/Rn,#imm8
    if (half & 0xE000) == 0x2000:
        group = (half >> 11) & 0x3
        rd_rn = (half >> 8) & 0x7
        imm = half & 0xFF
        mapping = {
            0: ("mov", rd_rn, None, True),
            1: ("cmp", None, rd_rn, True),
            2: ("add", rd_rn, rd_rn, True),
            3: ("sub", rd_rn, rd_rn, True),
        }
        opcode, rd, rn, setflags = mapping[group]
        return DecodedInstruction(
            pc=pc,
            state="thumb",
            size=2,
            raw=half,
            mnemonic=opcode,
            kind="data_processing",
            opcode=opcode,
            rd=rd,
            rn=rn,
            imm=imm,
            setflags=setflags,
        )

    # ADD/SUB register/immediate, 3-bit operand.
    if (half & 0xF800) == 0x1800:
        immediate = bool(half & (1 << 10))
        subtract = bool(half & (1 << 9))
        rn = (half >> 3) & 0x7
        rd = half & 0x7
        op = (half >> 6) & 0x7
        return DecodedInstruction(
            pc=pc,
            state="thumb",
            size=2,
            raw=half,
            mnemonic="sub" if subtract else "add",
            kind="data_processing",
            opcode="sub" if subtract else "add",
            rd=rd,
            rn=rn,
            rm=None if immediate else op,
            imm=op if immediate else None,
            setflags=True,
        )

    return None


def _decode_thumb_memory(half: int, pc: int) -> DecodedInstruction | None:
    # STR/LDR word immediate: Rt, [Rn, #imm5*4]
    if (half & 0xF000) in {0x6000, 0x6800}:
        load = bool(half & 0x0800)
        imm5 = (half >> 6) & 0x1F
        rn = (half >> 3) & 0x7
        rd = half & 0x7
        return DecodedInstruction(
            pc=pc,
            state="thumb",
            size=2,
            raw=half,
            mnemonic="ldr" if load else "str",
            kind="memory",
            opcode="ldr" if load else "str",
            rd=rd,
            rn=rn,
            imm=imm5 << 2,
            load=load,
        )
    return None


_DATA_PROCESSING_ROOTS = {
    "adc", "add", "adr", "and", "asr", "bic", "cmn", "cmp", "eor",
    "lsl", "lsr", "mov", "movt", "movw", "mvn", "orn", "orr", "ror",
    "rrx", "rsb", "rsc", "sbc", "sub", "teq", "tst",
}
_MULTIPLY_ROOTS = {
    "mla", "mls", "mul", "smla", "smlal", "smlaw", "smmla", "smmls",
    "smmul", "smuad", "smul", "smull", "smusd", "umaal", "umlal",
    "umull", "usad8", "usada8",
}
_MEMORY_ROOTS = {
    "ldm", "ldr", "ldrex", "pop", "push", "stm", "str", "strex", "swp",
}
_MEDIA_PREFIXES = (
    "bfc", "bfi", "clz", "pkh", "qadd", "qdadd", "qdsub", "qsub", "rev",
    "rbit", "sbfx", "sel", "smlad", "smlald", "smlsd", "smlsld", "smuad",
    "smusd", "ssat", "sxt", "ubfx", "uh", "uq", "usat", "uxt",
)
_HINT_BARRIER_ROOTS = {"clrex", "dmb", "dsb", "isb", "nop", "sev", "wfe", "wfi", "yield"}
_SYSTEM_ROOTS = {"bkpt", "cps", "hvc", "mrs", "msr", "setend", "smc", "udf"}
_COPROCESSOR_ROOTS = {"cdp", "ldc", "mcr", "mcrr", "mrc", "mrrc", "stc"}


def _matching_root(root: str, candidates: set[str]) -> str | None:
    conditions = {*(_COND[:-2]), "hs", "lo"}
    suffixes = {"", "s", *conditions}
    suffixes.update(f"s{condition}" for condition in conditions)
    for candidate in sorted(candidates, key=lambda value: (-len(value), value)):
        if root.startswith(candidate) and root[len(candidate) :] in suffixes:
            return candidate
    return None


def _capstone_family(mnemonic: str) -> str:
    root = mnemonic.split(".", 1)[0]
    if root.startswith("v"):
        return "vfp_simd"
    if root.startswith(("ldm", "stm")):
        return "block_memory"
    memory_root = _matching_root(root, _MEMORY_ROOTS)
    if memory_root is not None or root.startswith(("ldr", "str", "pld", "pli")):
        if memory_root in {"ldm", "stm", "pop", "push"}:
            return "block_memory"
        if root.startswith(("ldrex", "strex", "swp")):
            return "exclusive_atomic"
        return "memory_extended"
    if _matching_root(root, _MULTIPLY_ROOTS) is not None or root.startswith(
        tuple(sorted(_MULTIPLY_ROOTS, key=lambda value: (-len(value), value)))
    ):
        return "multiply_accumulate"
    if _matching_root(root, _DATA_PROCESSING_ROOTS) is not None:
        return "data_processing_extended"
    if root.startswith(_MEDIA_PREFIXES):
        return "media_bitfield"
    if _matching_root(root, _HINT_BARRIER_ROOTS) is not None:
        return "hint_barrier"
    if _matching_root(root, _SYSTEM_ROOTS) is not None:
        return "system"
    if _matching_root(root, _COPROCESSOR_ROOTS) is not None:
        return "coprocessor"
    return "other_architectural"


def _decode_with_capstone(
    data: bytes,
    offset: int,
    pc: int,
    state: str,
    condition: str | None,
) -> DecodedInstruction | None:
    engine = _CAPSTONE_ARM if state == "arm" else _CAPSTONE_THUMB
    if engine is None:
        return None
    available = 4 if state == "arm" else min(4, len(data) - offset)
    if available <= 0:
        return None
    instruction = next(engine.disasm(data[offset : offset + available], pc, 1), None)
    if instruction is None or instruction.address != pc:
        return None

    raw = int.from_bytes(data[offset : offset + instruction.size], "little")
    groups = set(instruction.groups)
    target = None
    thumb_target = None
    kind = _capstone_family(instruction.mnemonic)
    if CS_GRP_CALL in groups:
        kind = "call"
    elif CS_GRP_JUMP in groups or CS_GRP_RET in groups:
        kind = "branch"
    if kind in {"call", "branch"}:
        if instruction.operands and instruction.operands[0].type == CS_OP_IMM:
            target = instruction.operands[0].imm & 0xFFFFFFFF
            if instruction.mnemonic.startswith("blx"):
                thumb_target = state == "arm"
            else:
                thumb_target = state == "thumb"
        else:
            kind = "indirect_branch"

    canonical_mnemonic = engine.insn_name(instruction.id)
    updates_flags = instruction.update_flags
    if state == "arm" and canonical_mnemonic in _DATA_PROCESSING_ROOTS:
        updates_flags = canonical_mnemonic in {"cmn", "cmp", "teq", "tst"} or bool(
            raw & (1 << 20)
        )

    return DecodedInstruction(
        pc=pc,
        state=state,
        size=instruction.size,
        raw=raw,
        mnemonic=instruction.mnemonic,
        kind=kind,
        condition=condition,
        target=target,
        thumb_target=thumb_target,
        family=_capstone_family(instruction.mnemonic),
        canonical_mnemonic=canonical_mnemonic,
        operand_text=instruction.op_str,
        decoder="capstone",
        setflags=updates_flags,
    )
