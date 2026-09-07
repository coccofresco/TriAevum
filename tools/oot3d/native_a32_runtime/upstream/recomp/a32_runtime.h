#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::recomp::a32 {

constexpr std::uint32_t kFlagN = 1U << 31;
constexpr std::uint32_t kFlagZ = 1U << 30;
constexpr std::uint32_t kFlagC = 1U << 29;
constexpr std::uint32_t kFlagV = 1U << 28;

enum class Opcode : std::uint8_t {
    MovImm,
    MovReg,
    Add,
    Sub,
    Cmp,
    Ldr32,
    Str32,
    Branch,
    BranchReg,
    Svc,
    Core,
    CoreSystem,
    CoreAlu,
    CoreMemory,
    VfpTransport,
    VfpScalar,
    Unsupported,
};

enum class Condition : std::uint8_t {
    Eq = 0,
    Ne = 1,
    Cs = 2,
    Cc = 3,
    Mi = 4,
    Pl = 5,
    Vs = 6,
    Vc = 7,
    Hi = 8,
    Ls = 9,
    Ge = 10,
    Lt = 11,
    Gt = 12,
    Le = 13,
    Al = 14,
    Nv = 15,
};

enum OpFlag : std::uint8_t {
    SetFlags = 1U << 0,
    Immediate = 1U << 1,
    Link = 1U << 2,
    Writeback = 1U << 3,
    PostIndex = 1U << 4,
    SubtractOffset = 1U << 5,
};

constexpr std::uint32_t EncodeMetadata(
    Opcode opcode,
    Condition condition,
    std::uint8_t flags = 0) noexcept {
    return static_cast<std::uint32_t>(opcode) |
           (static_cast<std::uint32_t>(condition) << 8) |
           (static_cast<std::uint32_t>(flags) << 12);
}

constexpr Opcode DecodeOpcode(std::uint32_t metadata) noexcept {
    return static_cast<Opcode>(metadata & 0xFFU);
}

constexpr Condition DecodeCondition(std::uint32_t metadata) noexcept {
    return static_cast<Condition>((metadata >> 8) & 0xFU);
}

constexpr std::uint8_t DecodeFlags(std::uint32_t metadata) noexcept {
    return static_cast<std::uint8_t>((metadata >> 12) & 0xFFU);
}

enum class ExitKind : std::uint8_t {
    Fallthrough,
    Branch,
    Svc,
    Wait,
    MemoryFault,
    BlockLimit,
    MissingBlock,
    Fallback,
    Unsupported,
};

enum class FallbackReason : std::uint8_t {
    None,
    Core,
    Vfp,
    Unsupported,
    MissingBlock,
};

enum class ExclusiveStoreResult : std::uint8_t {
    Success,
    ReservationLost,
    MemoryFault,
};

struct GuestState {
    std::array<std::uint32_t, 16> r{};
    std::uint32_t cpsr{};
    std::uint32_t fpscr{};
    // CP15 TPIDRURW, initialized by the embedding runtime for each guest
    // thread and read by the observed OoT3D synchronization helpers.
    std::uint32_t thread_pointer{};
    // VFP11 D0-D15 alias consecutive pairs S[2*d], S[2*d+1].
    std::array<std::uint32_t, 32> vfp{};
    std::uint32_t exclusive_address{};
    std::uint64_t exclusive_token{};
    std::uint8_t exclusive_size{};
    bool exclusive_valid{};
};

class MemoryBus {
public:
    virtual ~MemoryBus() = default;
    virtual bool Read32(std::uint32_t address, std::uint32_t* value) = 0;
    virtual bool Write32(std::uint32_t address, std::uint32_t value) = 0;
    // Sized defaults preserve existing word-only buses.  Real MMIO-backed
    // integrations should override them so byte/halfword accesses do not
    // become read-modify-write transactions.
    virtual bool Read8(std::uint32_t address, std::uint8_t* value);
    virtual bool Read16(std::uint32_t address, std::uint16_t* value);
    virtual bool Write8(std::uint32_t address, std::uint8_t value);
    virtual bool Write16(std::uint32_t address, std::uint16_t value);
    // Wide and atomic operations default to a precise fault.  A production
    // guest-memory integration overrides them as one logical transaction;
    // silently decomposing them would permit partial writes or stale STREX
    // success after another observer changed memory.
    virtual bool Read64(
        std::uint32_t address,
        std::uint64_t* value,
        std::uint32_t* fault_address);
    virtual bool Write64(
        std::uint32_t address,
        std::uint64_t value,
        std::uint32_t* fault_address);
    virtual bool LoadExclusive(
        std::uint32_t address,
        std::uint8_t size,
        std::uint64_t* value,
        std::uint64_t* token,
        std::uint32_t* fault_address);
    virtual ExclusiveStoreResult StoreExclusive(
        std::uint32_t address,
        std::uint8_t size,
        std::uint64_t value,
        std::uint64_t token,
        std::uint32_t* fault_address);
    virtual bool AtomicSwap(
        std::uint32_t address,
        std::uint8_t size,
        std::uint32_t replacement,
        std::uint32_t* previous,
        std::uint32_t* fault_address);
};

struct PackedOp {
    std::uint32_t raw{};
    std::uint32_t metadata{EncodeMetadata(
        Opcode::Unsupported, Condition::Al)};
};

static_assert(sizeof(PackedOp) == 8, "PackedOp must remain shard-friendly");

struct Block {
    std::uint32_t pc{};
    const PackedOp* ops{};
    std::uint32_t op_count{};
    bool native_candidate{};
};

struct Function {
    std::uint32_t entry{};
    // Exclusive end address.
    std::uint32_t end{};
    const char* name{};
};

struct BlockShard {
    std::uint32_t first_pc{};
    std::uint32_t last_pc{};
    const Block* blocks{};
    std::uint32_t block_count{};
};

struct Registry {
    const BlockShard* shards{};
    std::uint32_t shard_count{};
    const Function* functions{};
    std::uint32_t function_count{};
};

struct ExecutionResult {
    ExitKind kind{ExitKind::Fallthrough};
    std::uint32_t pc{};
    FallbackReason fallback{FallbackReason::None};
    // Fault address, SVC immediate, or another exit-specific payload.
    std::uint32_t detail{};
};

using FallbackCallback = ExecutionResult (*)(
    FallbackReason reason,
    std::uint32_t pc,
    const PackedOp& op,
    GuestState& state,
    MemoryBus& memory,
    void* user);

using BlockEntryCallback = void (*)(
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory,
    void* user);

using NativeFunctionCallback = bool (*)(
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory,
    ExecutionResult* result,
    void* user);

using NativeBlockCallback = bool (*)(
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory,
    ExecutionResult* result,
    std::uint32_t block_budget,
    std::uint32_t* blocks_consumed,
    void* user);

bool ConditionPassed(Condition condition, std::uint32_t cpsr) noexcept;

const Block* FindBlock(const Registry& registry, std::uint32_t pc) noexcept;
const Function* FindFunction(const Registry& registry, std::uint32_t pc) noexcept;

ExecutionResult ExecuteBlock(
    const Block& block,
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback = nullptr,
    void* fallback_user = nullptr,
    NativeFunctionCallback native_function = nullptr,
    void* native_function_user = nullptr,
    const std::uint32_t* native_function_pcs = nullptr,
    std::size_t native_function_pc_count = 0U);

ExecutionResult Dispatch(
    const Registry& registry,
    std::uint32_t entry_pc,
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback = nullptr,
    void* fallback_user = nullptr,
    std::uint32_t block_limit = 1'000'000U,
    BlockEntryCallback block_entry = nullptr,
    void* block_entry_user = nullptr,
    const std::uint32_t* block_entry_pcs = nullptr,
    std::size_t block_entry_pc_count = 0U,
    NativeFunctionCallback native_function = nullptr,
    void* native_function_user = nullptr,
    const std::uint32_t* native_function_pcs = nullptr,
    std::size_t native_function_pc_count = 0U,
    NativeBlockCallback native_block = nullptr,
    void* native_block_user = nullptr);

}  // namespace oot3d::recomp::a32
