#pragma once

#include "oot3d_native_a32_memory.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Oot3dNativeGame {

struct NativeA32CallResult {
    bool Completed = false;
    oot3d::recomp::a32::ExecutionResult Exit;
    std::string Error;
};

class NativeA32ExecutionRuntime final : public oot3d::recomp::a32::MemoryBus {
  public:
    NativeA32ExecutionRuntime(std::span<const uint8_t> codeImage,
                              uint32_t codeBaseAddress);

    bool Available() const;
    NativeA32CallResult Call(uint32_t entryAddress,
                            oot3d::recomp::a32::GuestState& state,
                            uint32_t blockLimit = 1'000'000U);
    std::optional<uint32_t> AllocateScratch(size_t size,
                                            size_t alignment = 16);
    void ResetScratch();

    bool Read8(uint32_t address, uint8_t* value) override;
    bool Read16(uint32_t address, uint16_t* value) override;
    bool Read32(uint32_t address, uint32_t* value) override;
    bool Read64(uint32_t address, uint64_t* value,
                uint32_t* faultAddress) override;
    bool Write8(uint32_t address, uint8_t value) override;
    bool Write16(uint32_t address, uint16_t value) override;
    bool Write32(uint32_t address, uint32_t value) override;
    bool Write64(uint32_t address, uint64_t value,
                 uint32_t* faultAddress) override;
    bool LoadExclusive(uint32_t address, uint8_t size, uint64_t* value,
                       uint64_t* token, uint32_t* faultAddress) override;
    oot3d::recomp::a32::ExclusiveStoreResult StoreExclusive(
        uint32_t address, uint8_t size, uint64_t value, uint64_t token,
        uint32_t* faultAddress) override;
    bool AtomicSwap(uint32_t address, uint8_t size, uint32_t replacement,
                    uint32_t* previous, uint32_t* faultAddress) override;

    uint32_t CodeBaseAddress() const;
    size_t CodeImageSize() const;
    uint64_t SuccessfulCallCount() const;
    uint64_t FailedCallCount() const;

  private:
    struct ReturnContext {
        uint32_t Sentinel = 0;
        bool Returned = false;
    };

    static oot3d::recomp::a32::ExecutionResult HandleFallback(
        oot3d::recomp::a32::FallbackReason reason, uint32_t pc,
        const oot3d::recomp::a32::PackedOp& op,
        oot3d::recomp::a32::GuestState& state,
        oot3d::recomp::a32::MemoryBus& memory, void* user);

    NativeA32Memory mMemory;
    uint32_t mCodeBaseAddress = 0;
    size_t mCodeImageSize = 0;
    size_t mScratchOffset = 0;
    uint64_t mSuccessfulCallCount = 0;
    uint64_t mFailedCallCount = 0;
};

} // namespace Oot3dNativeGame
