from __future__ import annotations

from .arm_decode import DecodedInstruction, decode_arm, decode_thumb
from .arm_ir import IRBlock, IROp, arm_core_supported, arm_vfp_supported


def is_instruction_liftable(item: DecodedInstruction) -> bool:
    if item.kind in {"data_processing", "memory", "branch", "call", "svc"}:
        return True
    if item.kind == "indirect_branch" and _arm_register_branch(item) is not None:
        return True
    return (
        item.state == "arm"
        and item.decoder == "capstone"
        and (arm_core_supported(item.raw) or arm_vfp_supported(item.raw))
    )


def lift_instruction(item: DecodedInstruction) -> list[IROp]:
    if item.kind == "data_processing":
        return [_lift_data_processing(item)]
    if item.kind == "memory":
        return [_lift_memory(item)]
    if item.kind in {"branch", "call"} and item.target is not None:
        target = item.target | 1 if item.thumb_target else item.target
        return [IROp("branch", _with_condition(item, {"target": target, "thumb_target": bool(item.thumb_target)}))]
    register_branch = _arm_register_branch(item)
    if item.kind == "indirect_branch" and register_branch is not None:
        rm, link = register_branch
        return [
            IROp(
                "branch_reg",
                _with_condition(
                    item,
                    {"rm": rm, "link": link, "pc": item.pc},
                ),
            )
        ]
    if item.kind == "svc":
        return [IROp("svc", _with_condition(item, {"svc_id": item.svc_id or 0}))]
    if (
        item.state == "arm"
        and item.decoder == "capstone"
        and arm_core_supported(item.raw)
    ):
        return [
            IROp(
                "arm_core",
                _with_condition(
                    item,
                    {
                        "raw": item.raw,
                        "pc": item.pc,
                        "mnemonic": item.canonical_mnemonic or item.mnemonic,
                        "family": item.family or item.kind,
                    },
                ),
            )
        ]
    if (
        item.state == "arm"
        and item.decoder == "capstone"
        and arm_vfp_supported(item.raw)
    ):
        return [
            IROp(
                "arm_vfp",
                _with_condition(
                    item,
                    {
                        "raw": item.raw,
                        "pc": item.pc,
                        "mnemonic": item.canonical_mnemonic or item.mnemonic,
                        "family": item.family or item.kind,
                    },
                ),
            )
        ]
    return [IROp("unsupported", _with_condition(item, {"pc": item.pc, "kind": item.kind}))]


def lift_arm_block(data: bytes, base_pc: int, max_instructions: int | None = None) -> IRBlock:
    block = IRBlock(pc=base_pc)
    offset = 0
    count = 0
    while offset + 4 <= len(data):
        item = decode_arm(data, offset, base_pc + offset)
        block.ops.extend(lift_instruction(item))
        offset += 4
        count += 1
        if _terminates_lifted_block(item):
            break
        if max_instructions is not None and count >= max_instructions:
            break
    return block


def lift_thumb_block(data: bytes, base_pc: int, max_instructions: int | None = None) -> IRBlock:
    block = IRBlock(pc=base_pc | 1)
    offset = 0
    count = 0
    while offset + 2 <= len(data):
        item = decode_thumb(data, offset, base_pc + offset)
        block.ops.extend(lift_instruction(item))
        offset += item.size
        count += 1
        if _terminates_lifted_block(item):
            break
        if max_instructions is not None and count >= max_instructions:
            break
    return block


def _lift_data_processing(item: DecodedInstruction) -> IROp:
    assert item.opcode is not None
    args = {
        "rd": item.rd,
        "rn": item.rn,
        "setflags": item.setflags,
    }
    if item.imm is not None:
        args["imm"] = item.imm
    elif item.rm is not None:
        args["rm"] = item.rm

    if item.opcode == "mov":
        if item.imm is not None:
            return IROp(
                "mov_imm",
                _with_condition(item, {"rd": item.rd, "imm": item.imm, "setflags": item.setflags}),
            )
        return IROp(
            "mov_reg",
            _with_condition(item, {"rd": item.rd, "rm": item.rm, "setflags": item.setflags}),
        )
    if item.opcode == "cmp":
        return IROp("cmp", _with_condition(item, args))
    return IROp(item.opcode, _with_condition(item, args))


def _lift_memory(item: DecodedInstruction) -> IROp:
    return IROp(
        "ldr" if item.load else "str",
        _with_condition(item, {"rd": item.rd, "rn": item.rn, "offset": item.imm or 0}),
    )


def _with_condition(item: DecodedInstruction, args: dict) -> dict:
    if item.condition is not None:
        args = dict(args)
        args["condition"] = item.condition
    return args


def _arm_register_branch(item: DecodedInstruction) -> tuple[int, bool] | None:
    if item.state != "arm" or item.size != 4:
        return None
    opcode = item.raw & 0x0FFFFFF0
    if opcode == 0x012FFF10:  # BX Rm
        return item.raw & 0xF, False
    if opcode == 0x012FFF30:  # BLX Rm
        return item.raw & 0xF, True
    return None


def _terminates_lifted_block(item: DecodedInstruction) -> bool:
    if item.kind not in {"branch", "call", "svc", "indirect_branch"}:
        return False
    if item.kind in {"branch", "indirect_branch"} and item.condition not in {
        None, "al"
    }:
        return False
    return True
