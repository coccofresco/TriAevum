#include "a32_core_internal.h"

#include <cstddef>
#include <cstdint>

namespace oot3d::recomp::a32::internal {
namespace {

constexpr bool Matches(
    std::uint32_t raw,
    std::uint32_t mask,
    std::uint32_t value) noexcept {
    return (raw & mask) == value;
}

constexpr std::uint8_t Bits(
    std::uint32_t raw,
    unsigned shift,
    std::uint32_t mask) noexcept {
    return static_cast<std::uint8_t>((raw >> shift) & mask);
}

constexpr std::uint32_t ReadRegister(
    const GuestState& state,
    std::uint8_t index,
    std::uint32_t pc) noexcept {
    return index == 15U ? pc + 8U : state.r[index];
}

ExecutionResult Unsupported(std::uint32_t raw, std::uint32_t pc) noexcept {
    return {
        ExitKind::Unsupported,
        pc,
        FallbackReason::Core,
        raw,
    };
}

ExecutionResult Fallthrough(GuestState& state, std::uint32_t pc) noexcept {
    state.r[15] = pc + 4U;
    return {
        ExitKind::Fallthrough,
        pc + 4U,
        FallbackReason::None,
        0U,
    };
}

ExecutionResult Branch(GuestState& state, std::uint32_t target) noexcept {
    state.r[15] = target;
    return {
        ExitKind::Branch,
        target,
        FallbackReason::None,
        0U,
    };
}

ExecutionResult MemoryFault(
    GuestState& state,
    std::uint32_t pc,
    std::uint32_t address) noexcept {
    state.r[15] = pc;
    return {
        ExitKind::MemoryFault,
        pc,
        FallbackReason::None,
        address,
    };
}

constexpr std::uint32_t RotateRight(
    std::uint32_t value,
    unsigned amount) noexcept {
    amount &= 31U;
    return amount == 0U
               ? value
               : (value >> amount) | (value << (32U - amount));
}

std::uint32_t ShiftImmediate(
    std::uint32_t value,
    unsigned type,
    unsigned amount,
    bool carry) noexcept {
    switch (type & 3U) {
    case 0U:  // LSL
        return amount == 0U ? value : value << amount;
    case 1U:  // LSR; an encoded zero means 32.
        return amount == 0U ? 0U : value >> amount;
    case 2U:  // ASR; an encoded zero means 32.
        if (amount == 0U) {
            return (value & 0x80000000U) != 0U ? 0xFFFFFFFFU : 0U;
        }
        if ((value & 0x80000000U) == 0U) {
            return value >> amount;
        }
        return (value >> amount) | (0xFFFFFFFFU << (32U - amount));
    case 3U:  // ROR #0 is RRX.
        return amount == 0U
                   ? (carry ? 0x80000000U : 0U) | (value >> 1U)
                   : RotateRight(value, amount);
    }
    return value;
}

std::uint32_t DecodeSingleOffset(
    std::uint32_t raw,
    std::uint32_t pc,
    const GuestState& state) noexcept {
    if ((raw & (1U << 25U)) == 0U) {
        return raw & 0xFFFU;
    }
    const std::uint8_t rm = Bits(raw, 0U, 0xFU);
    const unsigned type = Bits(raw, 5U, 3U);
    const unsigned amount = Bits(raw, 7U, 0x1FU);
    return ShiftImmediate(
        ReadRegister(state, rm, pc),
        type,
        amount,
        (state.cpsr & kFlagC) != 0U);
}

struct Address {
    std::uint32_t access{};
    std::uint32_t writeback{};
    bool has_writeback{};
};

Address DecodeAddress(
    std::uint32_t raw,
    std::uint32_t base,
    std::uint32_t offset) noexcept {
    const bool pre = (raw & (1U << 24U)) != 0U;
    const bool up = (raw & (1U << 23U)) != 0U;
    const std::uint32_t adjusted = up ? base + offset : base - offset;
    return {
        pre ? adjusted : base,
        adjusted,
        !pre || (raw & (1U << 21U)) != 0U,
    };
}

constexpr std::uint32_t SignExtend8(std::uint8_t value) noexcept {
    return (value & 0x80U) != 0U
               ? 0xFFFFFF00U | static_cast<std::uint32_t>(value)
               : value;
}

constexpr std::uint32_t SignExtend16(std::uint16_t value) noexcept {
    return (value & 0x8000U) != 0U
               ? 0xFFFF0000U | static_cast<std::uint32_t>(value)
               : value;
}

enum class AccessWidth : std::uint8_t {
    Byte,
    Half,
    Word,
    Double,
};

struct Transfer {
    AccessWidth width{AccessWidth::Word};
    bool load{};
    bool signed_load{};
    std::uint8_t rn{};
    std::uint8_t rt{};
    std::uint32_t offset{};
    Address address{};
};

bool DecodeSingleTransfer(
    std::uint32_t raw,
    std::uint32_t pc,
    const GuestState& state,
    Transfer* transfer) noexcept {
    // cond=1111 is the unconditional instruction space.  Its supported
    // prefetch encodings are handled separately and must not become loads.
    if ((raw >> 28U) == 0xFU ||
        !Matches(raw, 0x0C000000U, 0x04000000U)) {
        return false;
    }
    // Register-offset A32 loads/stores reserve bit 4.  Encodings with it set
    // belong to media/coprocessor spaces and are not single transfers.
    if ((raw & (1U << 25U)) != 0U && (raw & (1U << 4U)) != 0U) {
        return false;
    }

    transfer->width = (raw & (1U << 22U)) != 0U
                          ? AccessWidth::Byte
                          : AccessWidth::Word;
    transfer->load = (raw & (1U << 20U)) != 0U;
    transfer->signed_load = false;
    transfer->rn = Bits(raw, 16U, 0xFU);
    transfer->rt = Bits(raw, 12U, 0xFU);
    if (transfer->load && transfer->rt == 15U &&
        transfer->width != AccessWidth::Word) {
        return false;
    }
    transfer->offset = DecodeSingleOffset(raw, pc, state);
    transfer->address = DecodeAddress(
        raw,
        ReadRegister(state, transfer->rn, pc),
        transfer->offset);
    return true;
}

bool DecodeMiscTransfer(
    std::uint32_t raw,
    std::uint32_t pc,
    const GuestState& state,
    Transfer* transfer) noexcept {
    if ((raw >> 28U) == 0xFU ||
        !Matches(raw, 0x0E000090U, 0x00000090U)) {
        return false;
    }
    const unsigned operation = Bits(raw, 5U, 3U);
    if (operation == 0U) {
        return false;
    }

    const bool immediate = (raw & (1U << 22U)) != 0U;
    const bool encoded_load = (raw & (1U << 20U)) != 0U;
    if (!immediate && (raw & (1U << 24U)) == 0U &&
        (raw & (1U << 21U)) != 0U && !encoded_load &&
        operation == 1U && (raw & 0xF00U) != 0U) {
        // Capstone's STRHT register form retains the architectural zero
        // field here; the load-T aliases below intentionally treat it as a
        // don't-care field, matching the pinned classifier.
        return false;
    }
    switch (operation) {
    case 1U:
        transfer->width = AccessWidth::Half;
        transfer->load = encoded_load;
        transfer->signed_load = false;
        break;
    case 2U:
        transfer->width = encoded_load
                              ? AccessWidth::Byte
                              : AccessWidth::Double;
        transfer->load = true;
        transfer->signed_load = encoded_load;
        break;
    case 3U:
        transfer->width = encoded_load
                              ? AccessWidth::Half
                              : AccessWidth::Double;
        transfer->load = encoded_load;
        transfer->signed_load = encoded_load;
        break;
    default:
        return false;
    }

    transfer->rn = Bits(raw, 16U, 0xFU);
    transfer->rt = Bits(raw, 12U, 0xFU);
    // A double transfer may not use a pair containing PC.
    if (transfer->width == AccessWidth::Double && transfer->rt >= 14U) {
        return false;
    }
    if (transfer->load && transfer->rt == 15U) {
        return false;
    }
    // P=0,W=1 selects unprivileged T forms for the byte/halfword family;
    // there is no corresponding LDRD/STRD encoding.
    if (transfer->width == AccessWidth::Double &&
        (raw & (1U << 24U)) == 0U &&
        (raw & (1U << 21U)) != 0U) {
        return false;
    }
    transfer->offset = immediate
                           ? ((raw >> 4U) & 0xF0U) | (raw & 0xFU)
                           : ReadRegister(state, Bits(raw, 0U, 0xFU), pc);
    transfer->address = DecodeAddress(
        raw,
        ReadRegister(state, transfer->rn, pc),
        transfer->offset);
    return true;
}

ExecutionResult ExecuteTransfer(
    const Transfer& transfer,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory) {
    const std::uint32_t address = transfer.address.access;
    bool branch = false;
    std::uint32_t branch_target = 0U;

    if (transfer.load) {
        switch (transfer.width) {
        case AccessWidth::Byte: {
            std::uint8_t loaded = 0U;
            if (!memory.Read8(address, &loaded)) {
                return MemoryFault(state, pc, address);
            }
            const std::uint32_t value = transfer.signed_load
                                            ? SignExtend8(loaded)
                                            : loaded;
            state.r[transfer.rt] = value;
            branch = transfer.rt == 15U;
            branch_target = value;
            break;
        }
        case AccessWidth::Half: {
            std::uint16_t loaded = 0U;
            if (!memory.Read16(address, &loaded)) {
                return MemoryFault(state, pc, address);
            }
            const std::uint32_t value = transfer.signed_load
                                            ? SignExtend16(loaded)
                                            : loaded;
            state.r[transfer.rt] = value;
            branch = transfer.rt == 15U;
            branch_target = value;
            break;
        }
        case AccessWidth::Word: {
            std::uint32_t loaded = 0U;
            if (!memory.Read32(address, &loaded)) {
                return MemoryFault(state, pc, address);
            }
            state.r[transfer.rt] = loaded;
            branch = transfer.rt == 15U;
            branch_target = loaded;
            break;
        }
        case AccessWidth::Double: {
            std::uint64_t loaded = 0U;
            std::uint32_t fault_address = address;
            if (!memory.Read64(address, &loaded, &fault_address)) {
                return MemoryFault(state, pc, fault_address);
            }
            const std::uint8_t rt2 =
                static_cast<std::uint8_t>(transfer.rt + 1U);
            state.r[transfer.rt] = static_cast<std::uint32_t>(loaded);
            state.r[rt2] = static_cast<std::uint32_t>(loaded >> 32U);
            break;
        }
        }
    } else {
        const std::uint32_t first =
            ReadRegister(state, transfer.rt, pc);
        switch (transfer.width) {
        case AccessWidth::Byte:
            if (!memory.Write8(address, static_cast<std::uint8_t>(first))) {
                return MemoryFault(state, pc, address);
            }
            break;
        case AccessWidth::Half:
            if (!memory.Write16(address, static_cast<std::uint16_t>(first))) {
                return MemoryFault(state, pc, address);
            }
            break;
        case AccessWidth::Word:
            if (!memory.Write32(address, first)) {
                return MemoryFault(state, pc, address);
            }
            break;
        case AccessWidth::Double: {
            const std::uint64_t value =
                static_cast<std::uint64_t>(ReadRegister(
                    state,
                    static_cast<std::uint8_t>(transfer.rt + 1U),
                    pc))
                    << 32U |
                first;
            std::uint32_t fault_address = address;
            if (!memory.Write64(address, value, &fault_address)) {
                return MemoryFault(state, pc, fault_address);
            }
            break;
        }
        }
    }

    // The reference executor applies writeback after every successful access,
    // including after the destination register has been loaded.
    if (transfer.address.has_writeback) {
        state.r[transfer.rn] = transfer.address.writeback;
    }
    return branch ? Branch(state, branch_target) : Fallthrough(state, pc);
}

bool IsPrefetch(std::uint32_t raw) noexcept {
    // PLD/PLDW immediate and register encodings.
    if (Matches(raw, 0xFF30F000U, 0xF510F000U) ||
        Matches(raw, 0xFF30F010U, 0xF710F000U)) {
        return true;
    }
    // PLI fixes bit 22, unlike the PLD/PLDW selector above.
    return Matches(raw, 0xFF70F000U, 0xF450F000U) ||
           Matches(raw, 0xFF70F010U, 0xF650F000U);
}

ExecutionResult ExecuteBlockTransfer(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory) {
    if ((raw >> 28U) == 0xFU ||
        !Matches(raw, 0x0E000000U, 0x08000000U)) {
        return Unsupported(raw, pc);
    }

    const std::uint16_t register_list =
        static_cast<std::uint16_t>(raw & 0xFFFFU);
    const std::uint8_t rn = Bits(raw, 16U, 0xFU);
    const bool writeback = (raw & (1U << 21U)) != 0U;
    if (register_list == 0U || (raw & (1U << 22U)) != 0U ||
        rn == 15U ||
        (writeback && (register_list & (1U << rn)) != 0U)) {
        return Unsupported(raw, pc);
    }

    unsigned count = 0U;
    for (unsigned index = 0U; index < 16U; ++index) {
        if ((register_list & (1U << index)) != 0U) {
            ++count;
        }
    }

    const bool load = (raw & (1U << 20U)) != 0U;
    const bool up = (raw & (1U << 23U)) != 0U;
    const bool pre = (raw & (1U << 24U)) != 0U;
    const std::uint32_t base = state.r[rn];
    const std::uint32_t byte_count = count * 4U;
    std::uint32_t address = up
                                ? base + (pre ? 4U : 0U)
                                : base - byte_count + (pre ? 0U : 4U);
    const std::uint32_t final_base =
        up ? base + byte_count : base - byte_count;
    auto stored = state.r;
    stored[15] = pc + 12U;
    bool branch = false;
    std::uint32_t branch_target = 0U;

    for (std::uint8_t index = 0U; index < 16U; ++index) {
        if ((register_list & (1U << index)) == 0U) {
            continue;
        }
        if (load) {
            std::uint32_t value = 0U;
            if (!memory.Read32(address, &value)) {
                return MemoryFault(state, pc, address);
            }
            state.r[index] = value;
            if (index == 15U) {
                branch = true;
                branch_target = value;
            }
        } else if (!memory.Write32(address, stored[index])) {
            return MemoryFault(state, pc, address);
        }
        address += 4U;
    }

    if (writeback) {
        state.r[rn] = final_base;
    }
    return branch ? Branch(state, branch_target) : Fallthrough(state, pc);
}

enum class ExclusiveKind : std::uint8_t {
    None,
    Load,
    Store,
};

struct Exclusive {
    ExclusiveKind kind{ExclusiveKind::None};
    std::uint8_t size{};
    std::uint8_t rn{};
    std::uint8_t rd{};
    std::uint8_t rt{};
};

bool DecodeExclusive(std::uint32_t raw, Exclusive* exclusive) noexcept {
    if ((raw >> 28U) == 0xFU) {
        return false;
    }
    const std::uint8_t opcode = Bits(raw, 20U, 0xFFU);
    const bool load = (opcode & 1U) != 0U;
    if (load) {
        if (!Matches(raw, 0x0FF00FFFU,
                     static_cast<std::uint32_t>(opcode) << 20U | 0xF9FU)) {
            return false;
        }
    } else if (!Matches(
                   raw,
                   0x0FF00FF0U,
                   static_cast<std::uint32_t>(opcode) << 20U | 0xF90U)) {
        return false;
    }

    switch (opcode) {
    case 0x18U:
    case 0x19U:
        exclusive->size = 4U;
        break;
    case 0x1AU:
    case 0x1BU:
        exclusive->size = 8U;
        break;
    case 0x1CU:
    case 0x1DU:
        exclusive->size = 1U;
        break;
    case 0x1EU:
    case 0x1FU:
        exclusive->size = 2U;
        break;
    default:
        return false;
    }

    exclusive->kind = load ? ExclusiveKind::Load : ExclusiveKind::Store;
    exclusive->rn = Bits(raw, 16U, 0xFU);
    exclusive->rd = Bits(raw, 12U, 0xFU);
    exclusive->rt = load ? exclusive->rd : Bits(raw, 0U, 0xFU);
    if (exclusive->rn == 15U || exclusive->rd == 15U ||
        exclusive->rt == 15U) {
        return false;
    }
    if (exclusive->size == 8U) {
        // A32 encodes a consecutive even/odd pair.  Capstone deliberately
        // ignores encoded bit zero and rejects pairs that would include PC.
        exclusive->rt = static_cast<std::uint8_t>(exclusive->rt & 0xEU);
        if (exclusive->rt >= 14U) {
            return false;
        }
    }
    return true;
}

bool IsExclusiveEncodingSpace(std::uint32_t raw) noexcept {
    const std::uint8_t opcode = Bits(raw, 20U, 0xFFU);
    return (raw >> 28U) != 0xFU && opcode >= 0x18U && opcode <= 0x1FU &&
           Matches(raw, 0x00000FF0U, 0x00000F90U);
}

ExecutionResult ExecuteExclusive(
    const Exclusive& exclusive,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory) {
    const std::uint32_t address =
        ReadRegister(state, exclusive.rn, pc);
    if (exclusive.kind == ExclusiveKind::Load) {
        std::uint64_t value = 0U;
        std::uint64_t token = 0U;
        std::uint32_t fault_address = address;
        if (!memory.LoadExclusive(
                address,
                exclusive.size,
                &value,
                &token,
                &fault_address)) {
            return MemoryFault(state, pc, fault_address);
        }
        state.r[exclusive.rt] = static_cast<std::uint32_t>(value);
        if (exclusive.size == 8U) {
            state.r[exclusive.rt + 1U] =
                static_cast<std::uint32_t>(value >> 32U);
        }
        state.exclusive_address = address;
        state.exclusive_token = token;
        state.exclusive_size = exclusive.size;
        state.exclusive_valid = true;
        return Fallthrough(state, pc);
    }

    ExclusiveStoreResult store_result =
        ExclusiveStoreResult::ReservationLost;
    if (state.exclusive_valid && state.exclusive_address == address &&
        state.exclusive_size == exclusive.size) {
        std::uint64_t value = ReadRegister(state, exclusive.rt, pc);
        if (exclusive.size == 8U) {
            value |= static_cast<std::uint64_t>(ReadRegister(
                         state,
                         static_cast<std::uint8_t>(exclusive.rt + 1U),
                         pc))
                     << 32U;
        }
        std::uint32_t fault_address = address;
        store_result = memory.StoreExclusive(
            address,
            exclusive.size,
            value,
            state.exclusive_token,
            &fault_address);
        if (store_result == ExclusiveStoreResult::MemoryFault) {
            return MemoryFault(state, pc, fault_address);
        }
    }

    // Failed accesses leave the monitor and status destination untouched;
    // successful architectural STREX completion always clears the monitor.
    const bool success = store_result == ExclusiveStoreResult::Success;
    state.r[exclusive.rd] = success ? 0U : 1U;
    state.exclusive_address = 0U;
    state.exclusive_token = 0U;
    state.exclusive_size = 0U;
    state.exclusive_valid = false;
    return exclusive.rd == 15U
               ? Branch(state, success ? 0U : 1U)
               : Fallthrough(state, pc);
}

bool IsSwap(std::uint32_t raw) noexcept {
    return (raw >> 28U) != 0xFU &&
           Matches(raw, 0x0FB000F0U, 0x01000090U);
}

ExecutionResult ExecuteSwap(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory) {
    const bool byte = (raw & (1U << 22U)) != 0U;
    const std::uint8_t rn = Bits(raw, 16U, 0xFU);
    const std::uint8_t rd = Bits(raw, 12U, 0xFU);
    const std::uint8_t rt = Bits(raw, 0U, 0xFU);
    if (rn == 15U || rd == 15U || rt == 15U) {
        return Unsupported(raw, pc);
    }
    const std::uint32_t address = ReadRegister(state, rn, pc);
    std::uint32_t old = 0U;
    std::uint32_t fault_address = address;
    if (!memory.AtomicSwap(
            address,
            byte ? 1U : 4U,
            ReadRegister(state, rt, pc),
            &old,
            &fault_address)) {
        return MemoryFault(state, pc, fault_address);
    }
    state.r[rd] = old;
    return Fallthrough(state, pc);
}

}  // namespace

ExecutionResult ExecuteCoreMemory(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory) {
    if (IsPrefetch(raw)) {
        return Fallthrough(state, pc);
    }

    Exclusive exclusive{};
    if (DecodeExclusive(raw, &exclusive)) {
        return ExecuteExclusive(exclusive, pc, state, memory);
    }
    if (IsExclusiveEncodingSpace(raw)) {
        return Unsupported(raw, pc);
    }
    if (IsSwap(raw)) {
        return ExecuteSwap(raw, pc, state, memory);
    }

    if (Matches(raw, 0x0E000000U, 0x08000000U)) {
        return ExecuteBlockTransfer(raw, pc, state, memory);
    }

    Transfer transfer{};
    if (DecodeSingleTransfer(raw, pc, state, &transfer) ||
        DecodeMiscTransfer(raw, pc, state, &transfer)) {
        return ExecuteTransfer(transfer, pc, state, memory);
    }
    return Unsupported(raw, pc);
}

}  // namespace oot3d::recomp::a32::internal
