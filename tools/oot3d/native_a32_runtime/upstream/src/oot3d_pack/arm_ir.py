from __future__ import annotations

import re
from dataclasses import dataclass, field
from functools import lru_cache
from typing import Any

from capstone import (
    CS_ARCH_ARM,
    CS_MODE_ARM,
    CS_MODE_LITTLE_ENDIAN,
    CS_OP_IMM,
    CS_OP_MEM,
    CS_OP_REG,
    Cs,
)
from capstone.arm_const import (
    ARM_SFT_ASR,
    ARM_SFT_ASR_REG,
    ARM_SFT_LSL,
    ARM_SFT_LSL_REG,
    ARM_SFT_LSR,
    ARM_SFT_LSR_REG,
    ARM_SFT_ROR,
    ARM_SFT_ROR_REG,
    ARM_SFT_RRX,
    ARM_SFT_RRX_REG,
)

from . import arm_softfloat as _softfloat
from . import arm_softfloat64 as _softfloat64


FLAG_N = 1 << 31
FLAG_Z = 1 << 30
FLAG_C = 1 << 29
FLAG_V = 1 << 28
FLAG_Q = 1 << 27
MASK32 = 0xFFFFFFFF
FPSCR_VECTOR_MODE_MASK = 0x00370000
FPSCR_EXCEPTION_ENABLE_MASK = 0x00009F00


@dataclass(frozen=True)
class IROp:
    op: str
    args: dict[str, Any]


@dataclass
class IRBlock:
    pc: int
    ops: list[IROp] = field(default_factory=list)


@dataclass
class GuestState:
    regs: list[int] = field(default_factory=lambda: [0] * 16)
    cpsr: int = 0
    memory: dict[int, int] = field(default_factory=dict)
    exited: bool = False
    exit_reason: str | None = None
    exclusive_address: int | None = None
    exclusive_size: int = 0
    # VFP single-precision registers are kept as raw IEEE-754 words.  D0-D15
    # alias consecutive pairs (S[2*d], S[2*d+1]); no host-float state leaks
    # into transport or save/restore operations.
    vfp_regs: list[int] = field(default_factory=lambda: [0] * 32)
    fpscr: int = 0
    # CP15 TPIDRURW is architectural per-thread state.  Keeping it outside
    # the general registers lets an embedding runtime seed the exact guest
    # thread pointer without routing ordinary reads through a fallback.
    thread_pointer: int = 0


def add32(a: int, b: int) -> tuple[int, bool, bool]:
    full = (a & MASK32) + (b & MASK32)
    result = full & MASK32
    carry = full > MASK32
    overflow = (~(a ^ b) & (a ^ result) & 0x80000000) != 0
    return result, carry, overflow


def sub32(a: int, b: int) -> tuple[int, bool, bool]:
    result = (a - b) & MASK32
    carry = (a & MASK32) >= (b & MASK32)
    overflow = ((a ^ b) & (a ^ result) & 0x80000000) != 0
    return result, carry, overflow


def update_nzcv(
    cpsr: int, result: int, carry: bool | None = None, overflow: bool | None = None
) -> int:
    cpsr &= ~(FLAG_N | FLAG_Z)
    if result & 0x80000000:
        cpsr |= FLAG_N
    if (result & MASK32) == 0:
        cpsr |= FLAG_Z
    if carry is not None:
        cpsr = (cpsr | FLAG_C) if carry else (cpsr & ~FLAG_C)
    if overflow is not None:
        cpsr = (cpsr | FLAG_V) if overflow else (cpsr & ~FLAG_V)
    return cpsr


def condition_passed(condition: str | None, cpsr: int) -> bool:
    if condition is None or condition == "al":
        return True
    n = bool(cpsr & FLAG_N)
    z = bool(cpsr & FLAG_Z)
    c = bool(cpsr & FLAG_C)
    v = bool(cpsr & FLAG_V)
    if condition == "eq":
        return z
    if condition == "ne":
        return not z
    if condition == "cs":
        return c
    if condition == "cc":
        return not c
    if condition == "mi":
        return n
    if condition == "pl":
        return not n
    if condition == "vs":
        return v
    if condition == "vc":
        return not v
    if condition == "hi":
        return c and not z
    if condition == "ls":
        return (not c) or z
    if condition == "ge":
        return n == v
    if condition == "lt":
        return n != v
    if condition == "gt":
        return (not z) and (n == v)
    if condition == "le":
        return z or (n != v)
    if condition == "nv":
        return False
    raise ValueError(f"unknown condition: {condition}")


def execute_ir(block: IRBlock, state: GuestState) -> GuestState:
    for op in block.ops:
        name = op.op
        args = op.args
        if not condition_passed(args.get("condition"), state.cpsr):
            continue
        if name == "mov_imm":
            state.regs[args["rd"]] = args["imm"] & MASK32
            if args.get("setflags"):
                state.cpsr = update_nzcv(state.cpsr, state.regs[args["rd"]])
        elif name == "mov_reg":
            state.regs[args["rd"]] = state.regs[args["rm"]] & MASK32
            if args.get("setflags"):
                state.cpsr = update_nzcv(state.cpsr, state.regs[args["rd"]])
        elif name == "add":
            rhs = _rhs(args, state)
            result, carry, overflow = add32(state.regs[args["rn"]], rhs)
            state.regs[args["rd"]] = result
            if args.get("setflags"):
                state.cpsr = update_nzcv(state.cpsr, result, carry, overflow)
        elif name == "sub":
            rhs = _rhs(args, state)
            result, carry, overflow = sub32(state.regs[args["rn"]], rhs)
            state.regs[args["rd"]] = result
            if args.get("setflags"):
                state.cpsr = update_nzcv(state.cpsr, result, carry, overflow)
        elif name == "cmp":
            rhs = _rhs(args, state)
            result, carry, overflow = sub32(state.regs[args["rn"]], rhs)
            state.cpsr = update_nzcv(state.cpsr, result, carry, overflow)
        elif name == "ldr":
            addr = (state.regs[args["rn"]] + args.get("offset", 0)) & MASK32
            state.regs[args["rd"]] = _read32(state.memory, addr)
        elif name == "str":
            addr = (state.regs[args["rn"]] + args.get("offset", 0)) & MASK32
            _write32(state.memory, addr, state.regs[args["rd"]])
        elif name == "branch":
            state.regs[15] = args["target"] & MASK32
            state.exited = True
            state.exit_reason = "branch"
            break
        elif name == "svc":
            state.exited = True
            state.exit_reason = f"svc_{args['svc_id']:x}"
            break
        elif name == "arm_core":
            _execute_arm_core(args["raw"], args["pc"], state)
            if state.exited:
                break
        elif name == "arm_vfp":
            _execute_arm_vfp(args["raw"], args["pc"], state)
        elif name == "branch_reg":
            target = (
                args["pc"] + 8 if args["rm"] == 15 else state.regs[args["rm"]]
            ) & MASK32
            if args.get("link"):
                state.regs[14] = (args["pc"] + 4) & MASK32
            state.regs[15] = target
            state.exited = True
            state.exit_reason = "branch"
            break
        elif name == "unsupported":
            state.exited = True
            state.exit_reason = "unsupported"
            break
        else:
            raise ValueError(f"unknown IR op: {name}")
    return state


_ARM_CORE = Cs(CS_ARCH_ARM, CS_MODE_ARM | CS_MODE_LITTLE_ENDIAN)
_ARM_CORE.detail = True

_CORE_DATA_OPS = {
    "adc",
    "add",
    "adr",
    "and",
    "asr",
    "bic",
    "cmn",
    "cmp",
    "eor",
    "lsl",
    "lsr",
    "mov",
    "movt",
    "movw",
    "mvn",
    "orn",
    "orr",
    "ror",
    "rrx",
    "rsb",
    "rsc",
    "sbc",
    "sub",
    "teq",
    "tst",
}
_CORE_HINT_OPS = {
    "clrex",
    "dmb",
    "dsb",
    "hint",
    "isb",
    "nop",
    "sev",
    "wfe",
    "wfi",
    "yield",
}
_CORE_MEDIA_OPS = {
    "clz",
    "pkhbt",
    "pkhtb",
    "qadd",
    "qdadd",
    "qdsub",
    "qsub",
    "rev",
    "rev16",
    "revsh",
    "ssat",
    "sxtab",
    "sxtah",
    "sxtb",
    "sxtb16",
    "sxth",
    "uqsub8",
    "usat",
    "uxtab",
    "uxtah",
    "uxtb",
    "uxtb16",
    "uxth",
}
_CORE_MULTIPLY_OPS = {
    "mla",
    "mls",
    "mul",
    "smlal",
    "smlald",
    "smlaldx",
    "smlsld",
    "smlsldx",
    "smull",
    "umaal",
    "umlal",
    "umull",
}
_CORE_EXCLUSIVE_OPS = {
    "ldrex",
    "ldrexb",
    "ldrexd",
    "ldrexh",
    "strex",
    "strexb",
    "strexd",
    "strexh",
    "swp",
    "swpb",
}


def _decode_arm_core(raw: int, pc: int):
    return next(_ARM_CORE.disasm((raw & MASK32).to_bytes(4, "little"), pc, 1), None)


def _classify_arm_system(raw: int) -> str | None:
    """Return the exact CP15 forms present in the OoT3D callable corpus."""

    if raw >> 28 == 0xF or ((raw >> 12) & 0xF) == 15:
        return None
    fixed = raw & 0x0FFF0FFF
    if fixed == 0x0E1D0F70:
        return "mrc_tpidrurw"
    if fixed == 0x0E070F9A:
        return "dsb_legacy"
    if fixed == 0x0E070FBA:
        return "dmb_legacy"
    return None


@lru_cache(maxsize=None)
def arm_system_supported(raw: int) -> bool:
    return _classify_arm_system(raw) is not None


@lru_cache(maxsize=None)
def arm_core_supported(raw: int) -> bool:
    if arm_system_supported(raw):
        return True
    instruction = _decode_arm_core(raw, 0x00100000)
    if instruction is None:
        return False
    name = _ARM_CORE.insn_name(instruction.id)
    if name in _CORE_DATA_OPS or name in _CORE_HINT_OPS:
        return True
    if name in _CORE_EXCLUSIVE_OPS:
        rn = (raw >> 16) & 0xF
        rd = (raw >> 12) & 0xF
        rt = raw & 0xF
        if rn == 15 or rd == 15:
            return False
        if name.startswith("strex") or name.startswith("swp"):
            if rt == 15:
                return False
        if name in {"ldrexd", "strexd"}:
            pair = (rd if name == "ldrexd" else rt) & 0xE
            if pair >= 14:
                return False
        return True
    if name.startswith(("ldr", "str", "pld", "pli")) or (
        name in {"push", "pop"} and (raw & 0x0C000000) == 0x04000000
    ):
        if name.startswith(("ldrd", "strd")) and ((raw >> 12) & 0xF) >= 14:
            return False
        if name.startswith("ldr") and not name.startswith("ldrd"):
            destination = (raw >> 12) & 0xF
            if destination == 15 and name != "ldr":
                return False
        return True
    if name.startswith(("ldm", "stm")) or name in {"push", "pop"}:
        register_list = raw & 0xFFFF
        base = (raw >> 16) & 0xF
        writeback = bool(raw & (1 << 21))
        return (
            register_list != 0
            and not bool(raw & (1 << 22))
            and base != 15
            and not (writeback and bool(register_list & (1 << base)))
        )
    if name in _CORE_MULTIPLY_OPS:
        return True
    if re.fullmatch(r"smul[bt][bt]", name):
        return True
    if re.fullmatch(r"smla[bt][bt]", name):
        return True
    if re.fullmatch(r"smlal[bt][bt]", name):
        return True
    if name in _CORE_MEDIA_OPS:
        return True
    return False


@lru_cache(maxsize=None)
def arm_core_runtime_category(raw: int) -> str | None:
    """Return the disjoint native runtime decoder for a supported core op."""

    if arm_system_supported(raw):
        return "system"
    if not arm_core_supported(raw):
        return None
    instruction = _decode_arm_core(raw, 0x00100000)
    if instruction is None:  # pragma: no cover - guarded by supported().
        return None
    name = _ARM_CORE.insn_name(instruction.id)
    if (
        name in _CORE_EXCLUSIVE_OPS
        or name.startswith(("ldr", "str", "pld", "pli", "ldm", "stm"))
        or name in {"push", "pop"}
    ):
        return "memory"
    return "alu"


@lru_cache(maxsize=None)
def arm_core_stops_linear_flow(raw: int) -> bool:
    """Return whether an accepted ARM-core instruction can leave ``pc + 4``.

    The AOT CFG uses this alongside the native decoder.  Capstone deliberately
    keeps loads/blocks that write PC in their architectural families instead
    of classifying them as branches, so their control-flow effect has to be
    made explicit here.
    """
    instruction = _decode_arm_core(raw, 0x00100000)
    if instruction is None or not arm_core_supported(raw):
        return False
    name = _ARM_CORE.insn_name(instruction.id)

    if (raw & 0x0FFFFFFF) in {0x0320F002, 0x0320F003}:
        return True

    if (
        (name.startswith("ldm") or name == "pop")
        and (raw & 0x0E000000) == 0x08000000
        and bool(raw & (1 << 20))
    ):
        return bool(raw & (1 << 15))

    if (
        (name.startswith("ldr") or name == "pop")
        and (raw & 0x0C000000) == 0x04000000
        and bool(raw & (1 << 20))
    ):
        return ((raw >> 12) & 0xF) == 15

    operands = instruction.operands

    def operand_is_pc(index: int) -> bool:
        return (
            index < len(operands)
            and operands[index].type == CS_OP_REG
            and instruction.reg_name(operands[index].reg) == "pc"
        )

    if name in _CORE_DATA_OPS:
        return name not in {"cmn", "cmp", "teq", "tst"} and operand_is_pc(0)

    if name.startswith("ldr"):
        # LDRD has two destination registers before the memory operand.
        for operand in operands:
            if operand.type == CS_OP_MEM:
                break
            if operand.type == CS_OP_REG and instruction.reg_name(operand.reg) == "pc":
                return True
        return False

    if name in _CORE_MULTIPLY_OPS or re.fullmatch(r"(?:smul|smla|smlal)[bt][bt]", name):
        destination_count = (
            2
            if name
            in {
                "smlal",
                "smlald",
                "smlaldx",
                "smlsld",
                "smlsldx",
                "smull",
                "umaal",
                "umlal",
                "umull",
            }
            or name.startswith("smlal")
            else 1
        )
        return any(operand_is_pc(index) for index in range(destination_count))

    if name in _CORE_MEDIA_OPS:
        return operand_is_pc(0)

    if name.startswith("ldrex"):
        for operand in operands:
            if operand.type == CS_OP_MEM:
                break
            if operand.type == CS_OP_REG and instruction.reg_name(operand.reg) == "pc":
                return True
        return False

    if name.startswith("strex") or name in {"swp", "swpb"}:
        return operand_is_pc(0)

    return False


def _vfp_s_index(name: str) -> int | None:
    match = re.fullmatch(r"s([0-9]|[12][0-9]|3[01])", name)
    return int(match.group(1)) if match else None


def _vfp_d_index(name: str) -> int | None:
    # GuestState intentionally models the 32 architecturally aliased S lanes.
    # D16-D31 therefore remain outside this conservative backend.
    match = re.fullmatch(r"d([0-9]|1[0-5])", name)
    return int(match.group(1)) if match else None


def _is_vfp_core_register(name: str) -> bool:
    return name in _REGISTER_INDEX and _REGISTER_INDEX[name] != 15


def _vfp_transfer_lanes(instruction, operands) -> list[int] | None:
    if not operands or any(operand.type != CS_OP_REG for operand in operands):
        return None
    names = [instruction.reg_name(operand.reg) for operand in operands]
    singles = [_vfp_s_index(name) for name in names]
    if all(index is not None for index in singles):
        indices = [int(index) for index in singles]
        if indices != list(range(indices[0], indices[0] + len(indices))):
            return None
        return indices
    doubles = [_vfp_d_index(name) for name in names]
    if all(index is not None for index in doubles):
        indices = [int(index) for index in doubles]
        if indices != list(range(indices[0], indices[0] + len(indices))):
            return None
        return [lane for index in indices for lane in (index * 2, index * 2 + 1)]
    return None


def _classify_arm_vfp_transport(instruction, raw: int) -> str | None:
    """Return an exact bit-preserving VFP transport form.

    This intentionally covers only bit-preserving transfers and FPSCR moves.
    It remains separate from scalar floating-point support so historical
    transport-only censuses can retain their original boundary.
    """

    if raw >> 28 == 0xF:
        return None
    name = _ARM_CORE.insn_name(instruction.id)
    operands = instruction.operands

    if name in {"vldr", "vstr"}:
        if (
            len(operands) != 2
            or operands[0].type != CS_OP_REG
            or operands[1].type != CS_OP_MEM
            or instruction.writeback
        ):
            return None
        target = instruction.reg_name(operands[0].reg)
        memory = operands[1].mem
        base = instruction.reg_name(memory.base)
        if (
            _vfp_s_index(target) is None and _vfp_d_index(target) is None
        ) or memory.index:
            return None
        if base not in _REGISTER_INDEX or (base == "pc" and name == "vstr"):
            return None
        if abs(memory.disp) != (raw & 0xFF) * 4:
            return None
        return name

    if name == "vmov":
        if any(operand.type != CS_OP_REG for operand in operands):
            return None
        names = [instruction.reg_name(operand.reg) for operand in operands]
        if len(names) == 2:
            left, right = names
            left_s, right_s = _vfp_s_index(left), _vfp_s_index(right)
            if left_s is not None and right_s is not None:
                return "vmov_ss"
            if left_s is not None and _is_vfp_core_register(right):
                return "vmov_sr"
            if right_s is not None and _is_vfp_core_register(left):
                return "vmov_rs"
            if _vfp_d_index(left) is not None and _vfp_d_index(right) is not None:
                return "vmov_dd"
        if len(names) == 3:
            if _vfp_d_index(names[0]) is not None and all(
                _is_vfp_core_register(name) for name in names[1:]
            ):
                return "vmov_drr"
            if (
                all(_is_vfp_core_register(name) for name in names[:2])
                and _vfp_d_index(names[2]) is not None
            ):
                return "vmov_rrd"
        return None

    if name in {"vldmia", "vstmia"}:
        if (
            len(operands) < 2
            or operands[0].type != CS_OP_REG
            or not bool(raw & (1 << 23))
            or bool(raw & (1 << 24))
        ):
            return None
        base_name = instruction.reg_name(operands[0].reg)
        if not _is_vfp_core_register(base_name):
            return None
        if ((raw >> 16) & 0xF) != _REGISTER_INDEX[base_name]:
            return None
        if bool(raw & (1 << 21)) != bool(instruction.writeback):
            return None
        if bool(raw & (1 << 20)) != (name == "vldmia"):
            return None
        lanes = _vfp_transfer_lanes(instruction, operands[1:])
        if lanes is None or len(lanes) != (raw & 0xFF):
            return None
        return name

    if name in {"vpush", "vpop"}:
        lanes = _vfp_transfer_lanes(instruction, operands)
        if lanes is None or len(lanes) != (raw & 0xFF):
            return None
        load = name == "vpop"
        if (
            ((raw >> 16) & 0xF) != 13
            or not bool(raw & (1 << 21))
            or bool(raw & (1 << 20)) != load
            or bool(raw & (1 << 23)) != load
            or bool(raw & (1 << 24)) == load
        ):
            return None
        return name

    if name in {"fmstat", "vmrs", "vmsr"}:
        # Capstone accepts several encodings with reserved low bits set as
        # VMSR aliases.  Keep the semantic backend on the exact architectural
        # VMRS/VMSR patterns used by Dynarmic and the native C++ decoder.
        expected = 0x0EE10A10 if name == "vmsr" else 0x0EF10A10
        if (raw & 0x0FFF0FFF) != expected:
            return None
        if len(operands) != 2 or any(operand.type != CS_OP_REG for operand in operands):
            return None
        left = instruction.reg_name(operands[0].reg)
        right = instruction.reg_name(operands[1].reg)
        if name == "fmstat" and (left, right) == ("apsr_nzcv", "fpscr"):
            return "fmstat"
        if name == "vmrs" and right == "fpscr" and _is_vfp_core_register(left):
            return "vmrs"
        if name == "vmsr" and left == "fpscr" and _is_vfp_core_register(right):
            return "vmsr"
        return None

    return None


_VFP_SCALAR_TERNARY_F32 = {
    "vadd",
    "vdiv",
    "vmla",
    "vmls",
    "vmul",
    "vnmla",
    "vnmls",
    "vnmul",
    "vsub",
}
_VFP_SCALAR_UNARY_F32 = {"vabs", "vneg", "vsqrt"}
_VFP_SCALAR_COMPARE_F32 = {"vcmp", "vcmpe"}
_VFP_SCALAR_TERNARY_F64 = {"vadd", "vdiv", "vmla", "vmls", "vmul", "vsub"}
_VFP_SCALAR_UNARY_F64 = {"vneg", "vsqrt"}
_VFP_SCALAR_COMPARE_F64 = {"vcmp", "vcmpe"}
_VFP_CONDITION_SUFFIX = r"(?:eq|ne|hs|cs|lo|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?"


def _vfp_all_s_registers(instruction, operands, count: int) -> bool:
    return len(operands) == count and all(
        operand.type == CS_OP_REG
        and _vfp_s_index(instruction.reg_name(operand.reg)) is not None
        for operand in operands
    )


def _vfp_all_d_registers(instruction, operands, count: int) -> bool:
    return len(operands) == count and all(
        operand.type == CS_OP_REG
        and _vfp_d_index(instruction.reg_name(operand.reg)) is not None
        for operand in operands
    )


def _classify_arm_vfp_scalar(instruction, raw: int) -> str | None:
    """Return a supported scalar IEEE-754 binary32/binary64 form.

    The executor additionally checks FPSCR at run time.  F16, NEON, fused
    operations, comparison-with-zero, D16-D31, and legacy vector Len/Stride
    modes remain outside this tranche.
    """

    if raw >> 28 == 0xF:
        return None
    name = _ARM_CORE.insn_name(instruction.id)
    mnemonic = instruction.mnemonic.lower()
    operands = instruction.operands

    if name in _VFP_SCALAR_TERNARY_F32:
        if re.fullmatch(
            rf"{name}{_VFP_CONDITION_SUFFIX}\.f32", mnemonic
        ) and _vfp_all_s_registers(instruction, operands, 3):
            return f"{name}_f32"

    if name in _VFP_SCALAR_UNARY_F32:
        if re.fullmatch(
            rf"{name}{_VFP_CONDITION_SUFFIX}\.f32", mnemonic
        ) and _vfp_all_s_registers(instruction, operands, 2):
            return f"{name}_f32"

    if name in _VFP_SCALAR_COMPARE_F32:
        if re.fullmatch(
            rf"{name}{_VFP_CONDITION_SUFFIX}\.f32", mnemonic
        ) and _vfp_all_s_registers(instruction, operands, 2):
            return f"{name}_f32"

    if name in _VFP_SCALAR_TERNARY_F64:
        if re.fullmatch(
            rf"{name}{_VFP_CONDITION_SUFFIX}\.f64", mnemonic
        ) and _vfp_all_d_registers(instruction, operands, 3):
            return f"{name}_f64"

    if name in _VFP_SCALAR_UNARY_F64:
        if re.fullmatch(
            rf"{name}{_VFP_CONDITION_SUFFIX}\.f64", mnemonic
        ) and _vfp_all_d_registers(instruction, operands, 2):
            return f"{name}_f64"

    if name in _VFP_SCALAR_COMPARE_F64:
        if re.fullmatch(
            rf"{name}{_VFP_CONDITION_SUFFIX}\.f64", mnemonic
        ) and _vfp_all_d_registers(instruction, operands, 2):
            return f"{name}_f64"

    if name == "vcvt" and _vfp_all_s_registers(instruction, operands, 2):
        match = re.fullmatch(
            rf"vcvt{_VFP_CONDITION_SUFFIX}\."
            r"(f32|s32|u32)\.(f32|s32|u32)",
            mnemonic,
        )
        if match is not None and "f32" in match.groups():
            destination, source = match.groups()
            if destination != source:
                return f"vcvt_{destination}_{source}"

    if name == "vcvt" and len(operands) == 2:
        match = re.fullmatch(
            rf"vcvt{_VFP_CONDITION_SUFFIX}\."
            r"(f64|f32|s32|u32)\.(f64|f32|s32|u32)",
            mnemonic,
        )
        if match is not None:
            destination, source = match.groups()
            expected_d = (destination == "f64", source == "f64")
            actual_d = tuple(
                operand.type == CS_OP_REG
                and _vfp_d_index(instruction.reg_name(operand.reg)) is not None
                for operand in operands
            )
            if actual_d == expected_d and (destination, source) in {
                ("f64", "s32"),
                ("s32", "f64"),
                ("u32", "f64"),
                ("f64", "f32"),
                ("f32", "f64"),
            }:
                return f"vcvt_{destination}_{source}"

    return None


def _classify_arm_vfp(instruction, raw: int) -> str | None:
    return _classify_arm_vfp_transport(instruction, raw) or _classify_arm_vfp_scalar(
        instruction, raw
    )


def _require_vfp_scalar_fpscr(fpscr: int) -> None:
    if fpscr & FPSCR_VECTOR_MODE_MASK:
        raise RuntimeError(
            "legacy VFP vector Len/Stride mode is unsupported: "
            f"FPSCR=0x{fpscr & MASK32:08x}"
        )
    if fpscr & FPSCR_EXCEPTION_ENABLE_MASK:
        raise RuntimeError(
            "VFP exception trap-enable mode is unsupported: "
            f"FPSCR=0x{fpscr & MASK32:08x}"
        )


@lru_cache(maxsize=None)
def arm_vfp_transport_supported(raw: int) -> bool:
    instruction = _decode_arm_core(raw, 0x00100000)
    return (
        instruction is not None
        and _classify_arm_vfp_transport(instruction, raw) is not None
    )


@lru_cache(maxsize=None)
def arm_vfp_scalar_supported(raw: int) -> bool:
    instruction = _decode_arm_core(raw, 0x00100000)
    return (
        instruction is not None
        and _classify_arm_vfp_scalar(instruction, raw) is not None
    )


@lru_cache(maxsize=None)
def arm_vfp_supported(raw: int) -> bool:
    return arm_vfp_transport_supported(raw) or arm_vfp_scalar_supported(raw)


def _execute_arm_core(raw: int, pc: int, state: GuestState) -> None:
    instruction = _decode_arm_core(raw, pc)
    if instruction is None or not arm_core_supported(raw):
        raise ValueError(f"unsupported ARM core instruction 0x{raw:08x} at 0x{pc:08x}")
    system = _classify_arm_system(raw)
    if system == "mrc_tpidrurw":
        state.regs[(raw >> 12) & 0xF] = state.thread_pointer & MASK32
        return
    if system in {"dsb_legacy", "dmb_legacy"}:
        # The Python oracle is sequential.  The native executor supplies the
        # corresponding host fence; neither form changes guest registers.
        return

    name = _ARM_CORE.insn_name(instruction.id)
    if name in _CORE_DATA_OPS:
        _execute_core_data(instruction, raw, pc, state)
    elif name in _CORE_EXCLUSIVE_OPS:
        _execute_core_exclusive(instruction, name, pc, state)
    elif name.startswith(("ldr", "str", "pld", "pli")) or (
        name in {"push", "pop"} and (raw & 0x0C000000) == 0x04000000
    ):
        _execute_core_memory(instruction, raw, pc, state)
    elif name.startswith(("ldm", "stm")) or name in {"push", "pop"}:
        _execute_core_block_memory(raw, pc, state)
    elif name in _CORE_HINT_OPS:
        if name == "hint":
            name = instruction.mnemonic
        if name.startswith("clrex"):
            state.exclusive_address = None
            state.exclusive_size = 0
        elif name.startswith(("wfe", "wfi")):
            state.exited = True
            state.exit_reason = name[:3]
    elif name in _CORE_MULTIPLY_OPS or name.startswith(("smul", "smla")):
        _execute_core_multiply(instruction, name, pc, state)
    elif name in _CORE_MEDIA_OPS:
        _execute_core_media(instruction, name, pc, state)
    else:  # pragma: no cover - guarded by arm_core_supported.
        raise ValueError(f"unhandled ARM core mnemonic: {name}")


def _commit_vfp_scalar_result(
    destination: int,
    value: int,
    flags: int,
    state: GuestState,
) -> None:
    state.vfp_regs[destination] = value & MASK32
    state.fpscr = _softfloat.accumulate_fpscr(state.fpscr, flags)


def _read_vfp_d(state: GuestState, index: int) -> int:
    lane = index * 2
    return state.vfp_regs[lane] | (state.vfp_regs[lane + 1] << 32)


def _commit_vfp64_result(
    destination: int,
    value: int,
    flags: int,
    state: GuestState,
) -> None:
    lane = destination * 2
    state.vfp_regs[lane] = value & MASK32
    state.vfp_regs[lane + 1] = (value >> 32) & MASK32
    state.fpscr = _softfloat.accumulate_fpscr(state.fpscr, flags)


def _execute_arm_vfp_scalar(form: str, instruction, state: GuestState) -> None:
    _require_vfp_scalar_fpscr(state.fpscr)
    control = _softfloat.FPControl.from_fpscr(state.fpscr)
    operands = instruction.operands

    if form.endswith("_f64") and not form.startswith("vcvt_"):
        registers = [
            _vfp_d_index(instruction.reg_name(operand.reg)) for operand in operands
        ]
        assert all(register is not None for register in registers)
        indices64 = [int(register) for register in registers]
        if form in {"vadd_f64", "vdiv_f64", "vmul_f64", "vsub_f64"}:
            destination, left, right = indices64
            function = {
                "vadd_f64": _softfloat64.f64_add,
                "vdiv_f64": _softfloat64.f64_div,
                "vmul_f64": _softfloat64.f64_mul,
                "vsub_f64": _softfloat64.f64_sub,
            }[form]
            result = function(
                _read_vfp_d(state, left), _read_vfp_d(state, right), control
            )
            _commit_vfp64_result(destination, result.value, result.flags, state)
            return
        if form in {"vmla_f64", "vmls_f64"}:
            destination, left, right = indices64
            function = {
                "vmla_f64": _softfloat64.f64_mla,
                "vmls_f64": _softfloat64.f64_mls,
            }[form]
            result = function(
                _read_vfp_d(state, destination),
                _read_vfp_d(state, left),
                _read_vfp_d(state, right),
                control,
            )
            _commit_vfp64_result(destination, result.value, result.flags, state)
            return
        if form in {"vneg_f64", "vsqrt_f64"}:
            destination, source = indices64
            function = {
                "vneg_f64": _softfloat64.f64_neg,
                "vsqrt_f64": _softfloat64.f64_sqrt,
            }[form]
            result = function(_read_vfp_d(state, source), control)
            _commit_vfp64_result(destination, result.value, result.flags, state)
            return
        if form in {"vcmp_f64", "vcmpe_f64"}:
            left, right = indices64
            result = _softfloat64.f64_compare(
                _read_vfp_d(state, left),
                _read_vfp_d(state, right),
                control,
                signal_all_nans=form == "vcmpe_f64",
            )
            state.fpscr = (state.fpscr & ~(FLAG_N | FLAG_Z | FLAG_C | FLAG_V)) | (
                result.value & (FLAG_N | FLAG_Z | FLAG_C | FLAG_V)
            )
            state.fpscr = _softfloat.accumulate_fpscr(state.fpscr, result.flags)
            return

    if form in {
        "vcvt_f64_s32",
        "vcvt_s32_f64",
        "vcvt_u32_f64",
        "vcvt_f64_f32",
        "vcvt_f32_f64",
    }:
        destination_name = instruction.reg_name(operands[0].reg)
        source_name = instruction.reg_name(operands[1].reg)
        destination_d = _vfp_d_index(destination_name)
        source_d = _vfp_d_index(source_name)
        destination_s = _vfp_s_index(destination_name)
        source_s = _vfp_s_index(source_name)
        if form == "vcvt_f64_s32":
            assert destination_d is not None and source_s is not None
            result = _softfloat64.f64_from_i32(state.vfp_regs[source_s], control)
            _commit_vfp64_result(destination_d, result.value, result.flags, state)
        elif form == "vcvt_s32_f64":
            assert destination_s is not None and source_d is not None
            result = _softfloat64.f64_to_i32(_read_vfp_d(state, source_d), control)
            _commit_vfp_scalar_result(destination_s, result.value, result.flags, state)
        elif form == "vcvt_u32_f64":
            assert destination_s is not None and source_d is not None
            result = _softfloat64.f64_to_u32(_read_vfp_d(state, source_d), control)
            _commit_vfp_scalar_result(destination_s, result.value, result.flags, state)
        elif form == "vcvt_f64_f32":
            assert destination_d is not None and source_s is not None
            result = _softfloat64.f64_from_f32(state.vfp_regs[source_s], control)
            _commit_vfp64_result(destination_d, result.value, result.flags, state)
        else:
            assert destination_s is not None and source_d is not None
            result = _softfloat64.f32_from_f64(_read_vfp_d(state, source_d), control)
            _commit_vfp_scalar_result(destination_s, result.value, result.flags, state)
        return

    lanes = [_vfp_s_index(instruction.reg_name(operand.reg)) for operand in operands]
    assert all(lane is not None for lane in lanes)
    indices = [int(lane) for lane in lanes]

    if form in {
        "vadd_f32",
        "vdiv_f32",
        "vmul_f32",
        "vsub_f32",
    }:
        destination, left, right = indices
        function = {
            "vadd_f32": _softfloat.f32_add,
            "vdiv_f32": _softfloat.f32_div,
            "vmul_f32": _softfloat.f32_mul,
            "vsub_f32": _softfloat.f32_sub,
        }[form]
        result = function(state.vfp_regs[left], state.vfp_regs[right], control)
        _commit_vfp_scalar_result(destination, result.value, result.flags, state)
        return

    if form in {
        "vmla_f32",
        "vmls_f32",
        "vnmla_f32",
        "vnmls_f32",
        "vnmul_f32",
    }:
        destination, left, right = indices
        if form == "vnmul_f32":
            result = _softfloat.f32_nmul(
                state.vfp_regs[left], state.vfp_regs[right], control
            )
        else:
            function = {
                "vmla_f32": _softfloat.f32_mla,
                "vmls_f32": _softfloat.f32_mls,
                "vnmla_f32": _softfloat.f32_nmla,
                "vnmls_f32": _softfloat.f32_nmls,
            }[form]
            result = function(
                state.vfp_regs[destination],
                state.vfp_regs[left],
                state.vfp_regs[right],
                control,
            )
        _commit_vfp_scalar_result(destination, result.value, result.flags, state)
        return

    if form in {"vabs_f32", "vneg_f32", "vsqrt_f32"}:
        destination, source = indices
        function = {
            "vabs_f32": _softfloat.f32_abs,
            "vneg_f32": _softfloat.f32_neg,
            "vsqrt_f32": _softfloat.f32_sqrt,
        }[form]
        result = function(state.vfp_regs[source], control)
        _commit_vfp_scalar_result(destination, result.value, result.flags, state)
        return

    if form.startswith("vcvt_"):
        destination, source = indices
        source_value = state.vfp_regs[source]
        if form == "vcvt_f32_s32":
            result = _softfloat.f32_from_i32(source_value, control)
        elif form == "vcvt_f32_u32":
            result = _softfloat.f32_from_u32(source_value, control)
        elif form == "vcvt_s32_f32":
            result = _softfloat.f32_to_i32(
                source_value,
                control,
                rounding=_softfloat.RoundingMode.TOWARD_ZERO,
            )
        elif form == "vcvt_u32_f32":
            result = _softfloat.f32_to_u32(
                source_value,
                control,
                rounding=_softfloat.RoundingMode.TOWARD_ZERO,
            )
        else:  # pragma: no cover - guarded by the scalar classifier.
            raise ValueError(f"unhandled scalar VFP conversion: {form}")
        _commit_vfp_scalar_result(destination, result.value, result.flags, state)
        return

    if form in {"vcmp_f32", "vcmpe_f32"}:
        left, right = indices
        result = _softfloat.f32_compare(
            state.vfp_regs[left],
            state.vfp_regs[right],
            control,
            signal_all_nans=form == "vcmpe_f32",
        )
        state.fpscr = (state.fpscr & ~(FLAG_N | FLAG_Z | FLAG_C | FLAG_V)) | (
            result.value & (FLAG_N | FLAG_Z | FLAG_C | FLAG_V)
        )
        state.fpscr = _softfloat.accumulate_fpscr(state.fpscr, result.flags)
        return

    raise ValueError(f"unhandled scalar VFP form: {form}")  # pragma: no cover


def _execute_arm_vfp(raw: int, pc: int, state: GuestState) -> None:
    instruction = _decode_arm_core(raw, pc)
    transport_form = (
        _classify_arm_vfp_transport(instruction, raw)
        if instruction is not None
        else None
    )
    scalar_form = (
        _classify_arm_vfp_scalar(instruction, raw) if instruction is not None else None
    )
    form = transport_form or scalar_form
    if instruction is None or form is None:
        raise ValueError(f"unsupported ARM VFP instruction 0x{raw:08x} at 0x{pc:08x}")
    if scalar_form is not None:
        _execute_arm_vfp_scalar(scalar_form, instruction, state)
        return
    operands = instruction.operands

    if form in {"vldr", "vstr"}:
        target_name = instruction.reg_name(operands[0].reg)
        lane = _vfp_s_index(target_name)
        double = _vfp_d_index(target_name)
        assert lane is not None or double is not None
        memory = operands[1].mem
        address = (
            _read_register(instruction, memory.base, state, pc) + memory.disp
        ) & MASK32
        if double is not None:
            first = double * 2
            if form == "vldr":
                state.vfp_regs[first] = _read32(state.memory, address)
                state.vfp_regs[first + 1] = _read32(
                    state.memory, (address + 4) & MASK32
                )
            else:
                _write32(state.memory, address, state.vfp_regs[first])
                _write32(
                    state.memory,
                    (address + 4) & MASK32,
                    state.vfp_regs[first + 1],
                )
        elif form == "vldr":
            assert lane is not None
            state.vfp_regs[lane] = _read32(state.memory, address)
        else:
            assert lane is not None
            _write32(state.memory, address, state.vfp_regs[lane])
        return

    if form.startswith("vmov_"):
        names = [instruction.reg_name(operand.reg) for operand in operands]
        left_name = names[0]
        right_name = names[1]
        if form == "vmov_ss":
            left = _vfp_s_index(left_name)
            right = _vfp_s_index(right_name)
            assert left is not None and right is not None
            state.vfp_regs[left] = state.vfp_regs[right] & MASK32
        elif form == "vmov_sr":
            left = _vfp_s_index(left_name)
            assert left is not None
            state.vfp_regs[left] = _read_register(
                instruction, operands[1].reg, state, pc
            )
        elif form == "vmov_rs":
            right = _vfp_s_index(right_name)
            assert right is not None
            destination = _register_index(instruction, operands[0].reg)
            _write_register(destination, state.vfp_regs[right], state)
        elif form == "vmov_dd":
            left = _vfp_d_index(left_name)
            right = _vfp_d_index(right_name)
            assert left is not None and right is not None
            low, high = state.vfp_regs[right * 2 : right * 2 + 2]
            state.vfp_regs[left * 2] = low
            state.vfp_regs[left * 2 + 1] = high
        elif form == "vmov_drr":
            destination = _vfp_d_index(left_name)
            assert destination is not None
            state.vfp_regs[destination * 2] = _read_register(
                instruction, operands[1].reg, state, pc
            )
            state.vfp_regs[destination * 2 + 1] = _read_register(
                instruction, operands[2].reg, state, pc
            )
        elif form == "vmov_rrd":
            source = _vfp_d_index(names[2])
            assert source is not None
            _write_register(
                _register_index(instruction, operands[0].reg),
                state.vfp_regs[source * 2],
                state,
            )
            _write_register(
                _register_index(instruction, operands[1].reg),
                state.vfp_regs[source * 2 + 1],
                state,
            )
        else:  # pragma: no cover - guarded by the transport classifier.
            raise ValueError(f"unhandled VFP move form: {form}")
        return

    if form in {"vldmia", "vstmia"}:
        base = _register_index(instruction, operands[0].reg)
        lanes = _vfp_transfer_lanes(instruction, operands[1:])
        assert lanes is not None
        address = state.regs[base] & MASK32
        stored = list(state.vfp_regs)
        for lane in lanes:
            if form == "vldmia":
                state.vfp_regs[lane] = _read32(state.memory, address)
            else:
                _write32(state.memory, address, stored[lane])
            address = (address + 4) & MASK32
        if instruction.writeback:
            state.regs[base] = address
        return

    if form in {"vpush", "vpop"}:
        lanes = _vfp_transfer_lanes(instruction, operands)
        assert lanes is not None
        if form == "vpush":
            address = (state.regs[13] - 4 * len(lanes)) & MASK32
            state.regs[13] = address
            for lane in lanes:
                _write32(state.memory, address, state.vfp_regs[lane])
                address = (address + 4) & MASK32
        else:
            address = state.regs[13] & MASK32
            for lane in lanes:
                state.vfp_regs[lane] = _read32(state.memory, address)
                address = (address + 4) & MASK32
            state.regs[13] = address
        return

    if form == "fmstat":
        state.cpsr = (state.cpsr & ~(FLAG_N | FLAG_Z | FLAG_C | FLAG_V)) | (
            state.fpscr & (FLAG_N | FLAG_Z | FLAG_C | FLAG_V)
        )
        return
    if form == "vmrs":
        destination = _register_index(instruction, operands[0].reg)
        _write_register(destination, state.fpscr, state)
        return
    if form == "vmsr":
        state.fpscr = _read_register(instruction, operands[1].reg, state, pc) & MASK32
        return
    raise ValueError(f"unhandled ARM VFP form: {form}")  # pragma: no cover


_REGISTER_INDEX = {
    **{f"r{index}": index for index in range(13)},
    "sb": 9,
    "sl": 10,
    "fp": 11,
    "ip": 12,
    "sp": 13,
    "lr": 14,
    "pc": 15,
}


def _register_index(instruction, register_id: int) -> int:
    name = instruction.reg_name(register_id)
    if name not in _REGISTER_INDEX:
        raise ValueError(f"unsupported core register: {name}")
    return _REGISTER_INDEX[name]


def _read_register(instruction, register_id: int, state: GuestState, pc: int) -> int:
    index = _register_index(instruction, register_id)
    return (pc + 8) & MASK32 if index == 15 else state.regs[index] & MASK32


def _write_register(index: int, value: int, state: GuestState) -> None:
    state.regs[index] = value & MASK32
    if index == 15:
        state.exited = True
        state.exit_reason = "branch"


def _shift_value(
    value: int,
    shift_type: int,
    amount: int,
    carry_in: bool,
    *,
    register_shift: bool = False,
) -> tuple[int, bool]:
    value &= MASK32
    if register_shift:
        amount &= 0xFF
        if amount == 0:
            return value, carry_in
    if shift_type in {0, ARM_SFT_LSL, ARM_SFT_LSL_REG}:
        if amount == 0:
            return value, carry_in
        if amount < 32:
            return (value << amount) & MASK32, bool(value & (1 << (32 - amount)))
        if amount == 32:
            return 0, bool(value & 1)
        return 0, False
    if shift_type in {ARM_SFT_LSR, ARM_SFT_LSR_REG}:
        if amount == 0 and not register_shift:
            amount = 32
        if amount < 32:
            return value >> amount, bool(value & (1 << (amount - 1)))
        if amount == 32:
            return 0, bool(value & 0x80000000)
        return 0, False
    if shift_type in {ARM_SFT_ASR, ARM_SFT_ASR_REG}:
        if amount == 0 and not register_shift:
            amount = 32
        sign = bool(value & 0x80000000)
        if amount >= 32:
            return (MASK32 if sign else 0), sign
        signed = value - (1 << 32) if sign else value
        return (signed >> amount) & MASK32, bool(value & (1 << (amount - 1)))
    if shift_type in {ARM_SFT_RRX, ARM_SFT_RRX_REG}:
        return ((int(carry_in) << 31) | (value >> 1)) & MASK32, bool(value & 1)
    if shift_type in {ARM_SFT_ROR, ARM_SFT_ROR_REG}:
        if amount == 0 and not register_shift:
            return ((int(carry_in) << 31) | (value >> 1)) & MASK32, bool(value & 1)
        amount &= 31
        if amount == 0:
            return value, bool(value & 0x80000000)
        result = ((value >> amount) | (value << (32 - amount))) & MASK32
        return result, bool(result & 0x80000000)
    raise ValueError(f"unsupported ARM shift type: {shift_type}")


def _operand_value(
    instruction, operand, raw: int, pc: int, state: GuestState
) -> tuple[int, bool]:
    carry = bool(state.cpsr & FLAG_C)
    if operand.type == CS_OP_IMM:
        value = operand.imm & MASK32
        rotate = ((raw >> 8) & 0xF) * 2 if raw & (1 << 25) else 0
        return value, (bool(value & 0x80000000) if rotate else carry)
    if operand.type != CS_OP_REG:
        raise ValueError(f"unsupported ARM operand type: {operand.type}")
    value = _read_register(instruction, operand.reg, state, pc)
    shift_type = operand.shift.type
    if shift_type in {
        ARM_SFT_ASR_REG,
        ARM_SFT_LSL_REG,
        ARM_SFT_LSR_REG,
        ARM_SFT_ROR_REG,
        ARM_SFT_RRX_REG,
    }:
        amount = _read_register(instruction, operand.shift.value, state, pc)
        return _shift_value(value, shift_type, amount, carry, register_shift=True)
    return _shift_value(value, shift_type, operand.shift.value, carry)


def _add_with_carry(a: int, b: int, carry_in: bool) -> tuple[int, bool, bool]:
    unsigned = (a & MASK32) + (b & MASK32) + int(carry_in)
    result = unsigned & MASK32
    carry = unsigned > MASK32
    signed_a = a - (1 << 32) if a & 0x80000000 else a
    signed_b = b - (1 << 32) if b & 0x80000000 else b
    signed_result = signed_a + signed_b + int(carry_in)
    overflow = not (-(1 << 31) <= signed_result <= (1 << 31) - 1)
    return result, carry, overflow


def _execute_core_data(instruction, raw: int, pc: int, state: GuestState) -> None:
    name = _ARM_CORE.insn_name(instruction.id)
    operands = instruction.operands
    carry_in = bool(state.cpsr & FLAG_C)
    result = 0
    carry: bool | None = None
    overflow: bool | None = None
    destination: int | None = None

    if name in {"lsl", "lsr", "asr", "ror", "rrx"}:
        destination = _register_index(instruction, operands[0].reg)
        source = _read_register(instruction, operands[1].reg, state, pc)
        if name == "rrx":
            result, carry = _shift_value(source, ARM_SFT_RRX, 1, carry_in)
        elif len(operands) >= 3:
            amount = (
                _read_register(instruction, operands[2].reg, state, pc)
                if operands[2].type == CS_OP_REG
                else operands[2].imm
            )
            shift_type = {
                "lsl": ARM_SFT_LSL_REG
                if operands[2].type == CS_OP_REG
                else ARM_SFT_LSL,
                "lsr": ARM_SFT_LSR_REG
                if operands[2].type == CS_OP_REG
                else ARM_SFT_LSR,
                "asr": ARM_SFT_ASR_REG
                if operands[2].type == CS_OP_REG
                else ARM_SFT_ASR,
                "ror": ARM_SFT_ROR_REG
                if operands[2].type == CS_OP_REG
                else ARM_SFT_ROR,
            }[name]
            result, carry = _shift_value(
                source,
                shift_type,
                amount,
                carry_in,
                register_shift=operands[2].type == CS_OP_REG,
            )
        else:
            result, carry = _operand_value(instruction, operands[1], raw, pc, state)
    elif name == "adr":
        destination = _register_index(instruction, operands[0].reg)
        result = operands[1].imm & MASK32
    elif name in {"movw", "movt"}:
        destination = _register_index(instruction, operands[0].reg)
        immediate = operands[1].imm & 0xFFFF
        result = (
            immediate
            if name == "movw"
            else ((state.regs[destination] & 0xFFFF) | (immediate << 16))
        )
    elif name in {"mov", "mvn"}:
        destination = _register_index(instruction, operands[0].reg)
        value, carry = _operand_value(instruction, operands[1], raw, pc, state)
        result = value if name == "mov" else (~value & MASK32)
    else:
        tests = {"cmn", "cmp", "teq", "tst"}
        offset = 0 if name in tests else 1
        if name not in tests:
            destination = _register_index(instruction, operands[0].reg)
        left, _ = _operand_value(instruction, operands[offset], raw, pc, state)
        right, shift_carry = _operand_value(
            instruction, operands[offset + 1], raw, pc, state
        )
        if name in {"add", "cmn"}:
            result, carry, overflow = _add_with_carry(left, right, False)
        elif name == "adc":
            result, carry, overflow = _add_with_carry(left, right, carry_in)
        elif name in {"sub", "cmp"}:
            result, carry, overflow = _add_with_carry(left, ~right & MASK32, True)
        elif name == "sbc":
            result, carry, overflow = _add_with_carry(left, ~right & MASK32, carry_in)
        elif name == "rsb":
            result, carry, overflow = _add_with_carry(right, ~left & MASK32, True)
        elif name == "rsc":
            result, carry, overflow = _add_with_carry(right, ~left & MASK32, carry_in)
        elif name in {"and", "tst"}:
            result, carry = left & right, shift_carry
        elif name in {"eor", "teq"}:
            result, carry = left ^ right, shift_carry
        elif name == "orr":
            result, carry = left | right, shift_carry
        elif name == "orn":
            result, carry = left | (~right & MASK32), shift_carry
        elif name == "bic":
            result, carry = left & (~right & MASK32), shift_carry
        else:  # pragma: no cover - the support table and tests keep this closed.
            raise ValueError(f"unhandled data-processing instruction: {name}")

    if destination is not None:
        _write_register(destination, result, state)
    if raw & (1 << 20) or name in {"cmn", "cmp", "teq", "tst"}:
        state.cpsr = update_nzcv(state.cpsr, result, carry, overflow)


def _memory_address(
    instruction, mem_index: int, pc: int, state: GuestState
) -> tuple[int, int | None, int | None]:
    operand = instruction.operands[mem_index]
    memory = operand.mem
    base_index = _register_index(instruction, memory.base)
    base = _read_register(instruction, memory.base, state, pc)
    index = 0
    if memory.index:
        index_value = _read_register(instruction, memory.index, state, pc)
        index, _ = _shift_value(
            index_value,
            operand.shift.type,
            operand.shift.value,
            bool(state.cpsr & FLAG_C),
        )
        if operand.subtracted:
            index = -index
    address = (base + memory.disp + index) & MASK32
    post_writeback = None
    if len(instruction.operands) > mem_index + 1:
        post = instruction.operands[mem_index + 1]
        delta, _ = _operand_value(instruction, post, 0, pc, state)
        if post.subtracted:
            delta = -delta
        address = base
        post_writeback = (base + delta) & MASK32
    elif instruction.writeback:
        post_writeback = address
    return address, base_index, post_writeback


def _execute_core_memory(instruction, raw: int, pc: int, state: GuestState) -> None:
    name = _ARM_CORE.insn_name(instruction.id)
    if name.startswith(("pld", "pli")):
        return
    if name in {"push", "pop"} and (raw & 0x0C000000) == 0x04000000:
        load = bool(raw & (1 << 20))
        pre = bool(raw & (1 << 24))
        up = bool(raw & (1 << 23))
        writeback = bool(raw & (1 << 21)) or not pre
        base_index = (raw >> 16) & 0xF
        register = (raw >> 12) & 0xF
        base = (
            (pc + 8) & MASK32 if base_index == 15 else state.regs[base_index] & MASK32
        )
        offset = raw & 0xFFF
        updated = (base + offset if up else base - offset) & MASK32
        address = updated if pre else base
        if load:
            _write_register(register, _read32(state.memory, address), state)
        else:
            value = (
                (pc + 8) & MASK32 if register == 15 else state.regs[register] & MASK32
            )
            _write32(state.memory, address, value)
        if writeback:
            state.regs[base_index] = updated
        return
    operands = instruction.operands
    mem_index = next(
        index for index, operand in enumerate(operands) if operand.type == CS_OP_MEM
    )
    address, base_index, writeback = _memory_address(instruction, mem_index, pc, state)
    registers = [
        _register_index(instruction, operand.reg)
        for operand in operands[:mem_index]
        if operand.type == CS_OP_REG
    ]
    load = name.startswith("ldr")
    if name.startswith(("ldrsb", "strsb")):
        size, signed = 1, True
    elif name.startswith(("ldrsh", "strsh")):
        size, signed = 2, True
    elif name.startswith(("ldrb", "strb")):
        size, signed = 1, False
    elif name.startswith(("ldrh", "strh")):
        size, signed = 2, False
    elif name.startswith(("ldrd", "strd")):
        size, signed = 8, False
    else:
        size, signed = 4, False

    if load:
        if size == 8:
            values = (
                _read32(state.memory, address),
                _read32(state.memory, address + 4),
            )
            for register, value in zip(registers, values):
                _write_register(register, value, state)
        else:
            value = {
                1: _read8,
                2: _read16,
                4: _read32,
            }[size](state.memory, address)
            if signed:
                value = _sign_extend(value, size * 8)
            _write_register(registers[0], value, state)
    else:
        if size == 8:
            _write32(state.memory, address, state.regs[registers[0]])
            _write32(state.memory, address + 4, state.regs[registers[1]])
        else:
            value = state.regs[registers[0]]
            {1: _write8, 2: _write16, 4: _write32}[size](state.memory, address, value)
    if writeback is not None and base_index is not None:
        state.regs[base_index] = writeback


def _execute_core_block_memory(raw: int, pc: int, state: GuestState) -> None:
    load = bool(raw & (1 << 20))
    writeback = bool(raw & (1 << 21))
    up = bool(raw & (1 << 23))
    pre = bool(raw & (1 << 24))
    base_index = (raw >> 16) & 0xF
    registers = [index for index in range(16) if raw & (1 << index)]
    base = (pc + 8) & MASK32 if base_index == 15 else state.regs[base_index]
    count = len(registers)
    if up:
        address = base + (4 if pre else 0)
        final_base = base + 4 * count
    else:
        address = base - 4 * count + (0 if pre else 4)
        final_base = base - 4 * count
    address &= MASK32
    stored = list(state.regs)
    stored[15] = (pc + 12) & MASK32
    for register in registers:
        if load:
            _write_register(register, _read32(state.memory, address), state)
        else:
            _write32(state.memory, address, stored[register])
        address = (address + 4) & MASK32
    if writeback:
        state.regs[base_index] = final_base & MASK32


def _execute_core_multiply(instruction, name: str, pc: int, state: GuestState) -> None:
    registers = [
        _register_index(instruction, operand.reg)
        for operand in instruction.operands
        if operand.type == CS_OP_REG
    ]
    values = [
        _read_register(instruction, operand.reg, state, pc)
        for operand in instruction.operands
        if operand.type == CS_OP_REG
    ]
    if name == "mul":
        result = values[1] * values[2]
        _write_register(registers[0], result, state)
        flag_value, flag_bits = result & MASK32, 32
    elif name in {"mla", "mls"}:
        product = values[1] * values[2]
        result = values[3] + product if name == "mla" else values[3] - product
        _write_register(registers[0], result, state)
        flag_value, flag_bits = result & MASK32, 32
    elif name in {"smull", "umull", "smlal", "umlal", "umaal"}:
        signed = name.startswith("sm")
        left = _signed32(values[2]) if signed else values[2]
        right = _signed32(values[3]) if signed else values[3]
        product = left * right
        if name in {"smlal", "umlal"}:
            product += (values[1] << 32) | values[0]
        elif name == "umaal":
            product += values[0] + values[1]
        result64 = product & 0xFFFFFFFFFFFFFFFF
        _write_register(registers[0], result64, state)
        _write_register(registers[1], result64 >> 32, state)
        flag_value, flag_bits = result64, 64
    elif name in {"smlald", "smlaldx", "smlsld", "smlsldx"}:
        left_low = _signed_half(values[2], False)
        left_high = _signed_half(values[2], True)
        exchange = name.endswith("x")
        right_low = _signed_half(values[3], exchange)
        right_high = _signed_half(values[3], not exchange)
        products = left_low * right_low
        products += (-1 if name.startswith("smlsl") else 1) * (left_high * right_high)
        accumulator = (values[1] << 32) | values[0]
        result64 = (accumulator + products) & 0xFFFFFFFFFFFFFFFF
        _write_register(registers[0], result64, state)
        _write_register(registers[1], result64 >> 32, state)
        flag_value, flag_bits = result64, 64
    elif re.fullmatch(r"smul[bt][bt]", name):
        left = _signed_half(values[1], name[-2] == "t")
        right = _signed_half(values[2], name[-1] == "t")
        result = left * right
        _write_register(registers[0], result, state)
        flag_value, flag_bits = result & MASK32, 32
    elif re.fullmatch(r"smla[bt][bt]", name):
        left = _signed_half(values[1], name[-2] == "t")
        right = _signed_half(values[2], name[-1] == "t")
        signed_result = left * right + _signed32(values[3])
        _write_register(registers[0], signed_result, state)
        if not (-(1 << 31) <= signed_result <= (1 << 31) - 1):
            state.cpsr |= FLAG_Q
        flag_value, flag_bits = signed_result & MASK32, 32
    elif re.fullmatch(r"smlal[bt][bt]", name):
        left = _signed_half(values[2], name[-2] == "t")
        right = _signed_half(values[3], name[-1] == "t")
        accumulator = (values[1] << 32) | values[0]
        result64 = (accumulator + left * right) & 0xFFFFFFFFFFFFFFFF
        _write_register(registers[0], result64, state)
        _write_register(registers[1], result64 >> 32, state)
        flag_value, flag_bits = result64, 64
    else:  # pragma: no cover - support validation prevents this.
        raise ValueError(f"unhandled multiply instruction: {name}")
    if instruction.update_flags:
        if flag_bits == 64:
            state.cpsr &= ~(FLAG_N | FLAG_Z)
            if flag_value & (1 << 63):
                state.cpsr |= FLAG_N
            if flag_value == 0:
                state.cpsr |= FLAG_Z
        else:
            state.cpsr = update_nzcv(state.cpsr, flag_value)


def _execute_core_media(instruction, name: str, pc: int, state: GuestState) -> None:
    operands = instruction.operands
    destination = _register_index(instruction, operands[0].reg)
    values = [
        _operand_value(instruction, operand, 0, pc, state)[0]
        for operand in operands[1:]
    ]
    if name in {"sxtb", "sxth", "uxtb", "uxth"}:
        bits = 8 if name.endswith("b") else 16
        value = values[0] & ((1 << bits) - 1)
        result = _sign_extend(value, bits) if name.startswith("s") else value
    elif name in {"sxtab", "sxtah", "uxtab", "uxtah"}:
        bits = 8 if name.endswith("b") else 16
        value = values[1] & ((1 << bits) - 1)
        extended = _sign_extend(value, bits) if name.startswith("s") else value
        result = values[0] + extended
    elif name in {"sxtb16", "uxtb16"}:
        low = values[0] & 0xFF
        high = (values[0] >> 16) & 0xFF
        if name.startswith("s"):
            low, high = _sign_extend(low, 8), _sign_extend(high, 8)
        result = (low & 0xFFFF) | ((high & 0xFFFF) << 16)
    elif name == "clz":
        result = 32 - values[0].bit_length()
    elif name == "rev":
        result = int.from_bytes(values[0].to_bytes(4, "little"), "big")
    elif name == "rev16":
        value = values[0]
        result = ((value & 0x00FF00FF) << 8) | ((value & 0xFF00FF00) >> 8)
    elif name == "revsh":
        result = _sign_extend(((values[0] & 0xFF) << 8) | ((values[0] >> 8) & 0xFF), 16)
    elif name in {"usat", "ssat"}:
        saturation = operands[1].imm
        source = values[-1]
        if name == "usat":
            signed_source = _signed32(source)
            low, high = 0, (1 << saturation) - 1 if saturation else 0
        else:
            signed_source = _signed32(source)
            low, high = -(1 << (saturation - 1)), (1 << (saturation - 1)) - 1
        clamped = min(max(signed_source, low), high)
        if clamped != signed_source:
            state.cpsr |= FLAG_Q
        result = clamped
    elif name in {"pkhbt", "pkhtb"}:
        if name == "pkhbt":
            result = (values[0] & 0xFFFF) | (values[1] & 0xFFFF0000)
        else:
            result = (values[0] & 0xFFFF0000) | (values[1] & 0xFFFF)
    elif name in {"qadd", "qsub", "qdadd", "qdsub"}:
        left, right = _signed32(values[0]), _signed32(values[1])
        if name.startswith("qd"):
            doubled = right * 2
            saturated_right = min(max(doubled, -(1 << 31)), (1 << 31) - 1)
            if saturated_right != doubled:
                state.cpsr |= FLAG_Q
            right = saturated_right
        signed_result = left + right if name in {"qadd", "qdadd"} else left - right
        clamped = min(max(signed_result, -(1 << 31)), (1 << 31) - 1)
        if clamped != signed_result:
            state.cpsr |= FLAG_Q
        result = clamped
    elif name == "uqsub8":
        result = 0
        for lane in range(4):
            left = (values[0] >> (lane * 8)) & 0xFF
            right = (values[1] >> (lane * 8)) & 0xFF
            result |= max(0, left - right) << (lane * 8)
    else:  # pragma: no cover - support validation prevents this.
        raise ValueError(f"unhandled media instruction: {name}")
    _write_register(destination, result, state)


def _execute_core_exclusive(instruction, name: str, pc: int, state: GuestState) -> None:
    operands = instruction.operands
    mem_index = next(
        index for index, operand in enumerate(operands) if operand.type == CS_OP_MEM
    )
    address, _, _ = _memory_address(instruction, mem_index, pc, state)
    registers = [
        _register_index(instruction, operand.reg)
        for operand in operands[:mem_index]
        if operand.type == CS_OP_REG
    ]
    size = (
        1
        if name.endswith("b")
        else 2
        if name.endswith("h")
        else 8
        if name.endswith("d")
        else 4
    )
    if name.startswith("ldrex"):
        if size == 8:
            _write_register(registers[0], _read32(state.memory, address), state)
            _write_register(registers[1], _read32(state.memory, address + 4), state)
        else:
            value = {1: _read8, 2: _read16, 4: _read32}[size](state.memory, address)
            _write_register(registers[0], value, state)
        state.exclusive_address = address
        state.exclusive_size = size
    elif name.startswith("strex"):
        success = state.exclusive_address == address and state.exclusive_size == size
        if success:
            values = [state.regs[index] for index in registers[1:]]
            if size == 8:
                _write32(state.memory, address, values[0])
                _write32(state.memory, address + 4, values[1])
            else:
                {1: _write8, 2: _write16, 4: _write32}[size](
                    state.memory, address, values[0]
                )
        _write_register(registers[0], 0 if success else 1, state)
        state.exclusive_address = None
        state.exclusive_size = 0
    else:
        old = {1: _read8, 4: _read32}[size](state.memory, address)
        {1: _write8, 4: _write32}[size](state.memory, address, state.regs[registers[1]])
        _write_register(registers[0], old, state)


def _signed32(value: int) -> int:
    value &= MASK32
    return value - (1 << 32) if value & 0x80000000 else value


def _signed_half(value: int, top: bool) -> int:
    return _sign_extend((value >> (16 if top else 0)) & 0xFFFF, 16)


def _sign_extend(value: int, bits: int) -> int:
    sign = 1 << (bits - 1)
    return ((value & ((1 << bits) - 1)) ^ sign) - sign


def _rhs(args: dict[str, Any], state: GuestState) -> int:
    if "imm" in args:
        return args["imm"] & MASK32
    return state.regs[args["rm"]] & MASK32


def _read8(memory: dict[int, int], addr: int) -> int:
    word_addr = addr & ~3
    shift = (addr & 3) * 8
    return (_read32(memory, word_addr) >> shift) & 0xFF


def _read16(memory: dict[int, int], addr: int) -> int:
    return _read8(memory, addr) | (_read8(memory, (addr + 1) & MASK32) << 8)


def _read32(memory: dict[int, int], addr: int) -> int:
    if addr % 4:
        raise ValueError(f"unaligned read32: 0x{addr:x}")
    return memory.get(addr, 0) & MASK32


def _write8(memory: dict[int, int], addr: int, value: int) -> None:
    word_addr = addr & ~3
    shift = (addr & 3) * 8
    word = _read32(memory, word_addr)
    mask = 0xFF << shift
    _write32(memory, word_addr, (word & ~mask) | ((value & 0xFF) << shift))


def _write16(memory: dict[int, int], addr: int, value: int) -> None:
    _write8(memory, addr, value)
    _write8(memory, (addr + 1) & MASK32, value >> 8)


def _write32(memory: dict[int, int], addr: int, value: int) -> None:
    if addr % 4:
        raise ValueError(f"unaligned write32: 0x{addr:x}")
    memory[addr] = value & MASK32
