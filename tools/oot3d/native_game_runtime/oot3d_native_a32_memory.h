#pragma once

#include "recomp/a32_runtime.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Oot3dNativeGame {

struct NativeA32MemoryRegionConfig {
    std::string Name;
    uint32_t BaseAddress = 0;
    size_t Size = 0;
    bool Writable = false;
    bool Executable = false;
    std::span<const uint8_t> InitialBytes;
};

class NativeA32Memory final : public oot3d::recomp::a32::MemoryBus {
  public:
    NativeA32Memory() = default;
    NativeA32Memory(const NativeA32Memory& other);
    NativeA32Memory& operator=(const NativeA32Memory& other);
    NativeA32Memory(NativeA32Memory&& other) noexcept = default;
    NativeA32Memory& operator=(NativeA32Memory&& other) noexcept = default;

    bool MapRegion(const NativeA32MemoryRegionConfig& config,
                   std::string* error = nullptr);
    bool IsMapped(uint32_t address, size_t size) const;
    bool IsWritable(uint32_t address, size_t size) const;
    bool IsExecutable(uint32_t address, size_t size) const;
    size_t RegionCount() const;
    uint64_t WriteGeneration() const;
    std::optional<uint64_t> RangeWriteGeneration(uint32_t address,
                                                 size_t size) const noexcept;
    void EnableWriteTraceFingerprint(bool enabled) noexcept;
    uint64_t WriteTraceFingerprint() noexcept;
    uint64_t FastReadMismatchCount() const noexcept;
    uint32_t LastFastReadMismatchAddress() const noexcept;
    uint64_t LastFastReadMismatchValue() const noexcept;
    uint64_t LastCheckedReadMismatchValue() const noexcept;
    uint64_t ContentFingerprint() const noexcept;
    uint64_t StateFingerprint() const noexcept;
    nlohmann::json CaptureState() const;
    bool RestoreState(const nlohmann::json& state,
                      std::string* error = nullptr);

    bool ReadBytes(uint32_t address, std::span<uint8_t> bytes) const;
    const uint8_t* GetReadPointer(uint32_t address, size_t size = 1) const;
    uint8_t* GetWritePointer(uint32_t address, size_t size = 1);
    std::optional<uint32_t>
    GetGuestAddress(const void* pointer, size_t size = 1) const noexcept;
    bool WriteBytes(uint32_t address, std::span<const uint8_t> bytes);
    bool Fill(uint32_t address, size_t size, uint8_t value);

    // Service-owned shared memory remains host-writable even when the guest
    // maps it read-only.
    bool WriteHostBytes(uint32_t address, std::span<const uint8_t> bytes);
    template <typename T>
    bool WriteHost(uint32_t address, T value) {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
                      sizeof(T) == 8);
        static_assert(std::endian::native == std::endian::little);
        return WriteHostBytes(
            address,
            std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(&value), sizeof(value)));
    }

    // AOT code uses these non-virtual typed paths. Fully mapped guest pages
    // become one indexed host load; boundaries and protected pages preserve
    // the exact checked-memory behavior.
    template <typename T>
    bool ReadFast(uint32_t address, T* value,
                  uint32_t* faultAddress = nullptr) const noexcept {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
                      sizeof(T) == 8);
        static_assert(std::endian::native == std::endian::little);
        if (value == nullptr) {
            return false;
        }
        const size_t group = address >> FastGroupShift;
        const size_t page = (address >> PageShift) & FastGroupPageMask;
        const size_t offset = address & PageMask;
        if (offset <= PageSize - sizeof(T) &&
            mFastPageGroups[group] != nullptr) {
            const uint8_t* source = mFastPageGroups[group][page].Read;
            if (source != nullptr) {
                std::memcpy(value, source + offset, sizeof(T));
                if (mTraceWriteFingerprint) [[unlikely]] {
                    uint64_t checked = 0;
                    if (ReadSized(address, static_cast<uint8_t>(sizeof(T)),
                                  &checked, nullptr) &&
                        *value != static_cast<T>(checked)) {
                        ++mFastReadMismatchCount;
                        mLastFastReadMismatchAddress = address;
                        mLastFastReadMismatchValue = *value;
                        mLastCheckedReadMismatchValue = checked;
                    }
                }
                return true;
            }
        }
        uint64_t decoded = 0;
        if (!ReadSized(address, static_cast<uint8_t>(sizeof(T)), &decoded,
                       faultAddress)) {
            return false;
        }
        *value = static_cast<T>(decoded);
        return true;
    }

    template <typename T>
    bool WriteFast(uint32_t address, T value,
                   uint32_t* faultAddress = nullptr) noexcept {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
                      sizeof(T) == 8);
        static_assert(std::endian::native == std::endian::little);
        const size_t group = address >> FastGroupShift;
        const size_t page = (address >> PageShift) & FastGroupPageMask;
        const size_t offset = address & PageMask;
        if (offset <= PageSize - sizeof(T) &&
            mFastPageGroups[group] != nullptr) {
            uint8_t* destination = mFastPageGroups[group][page].Write;
            if (destination != nullptr) {
                std::memcpy(destination + offset, &value, sizeof(T));
                if (mTraceWriteFingerprint) [[unlikely]] {
                    RecordTracedWrite(
                        address,
                        std::span<const uint8_t>(
                            reinterpret_cast<const uint8_t*>(&value),
                            sizeof(value)));
                }
                ++mWriteGeneration;
                return true;
            }
        }
        return WriteSized(address, static_cast<uint8_t>(sizeof(T)), value,
                          faultAddress);
    }

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

  private:
    struct Region {
        std::string Name;
        uint32_t BaseAddress = 0;
        std::vector<uint8_t> Bytes;
        bool Writable = false;
        bool Executable = false;
    };

    struct FastPage {
        const uint8_t* Read = nullptr;
        uint8_t* Write = nullptr;
    };

    Region* FindRegion(uint32_t address, size_t size);
    const Region* FindRegion(uint32_t address, size_t size) const;
    void RebuildPageTable();
    bool ReadSized(uint32_t address, uint8_t size, uint64_t* value,
                   uint32_t* faultAddress) const;
    bool WriteSized(uint32_t address, uint8_t size, uint64_t value,
                    uint32_t* faultAddress);
    void RecordTracedWrite(uint32_t address,
                           std::span<const uint8_t> bytes) noexcept;
    void FlushTracedPointerWrites() noexcept;
    static void SetError(std::string* error, std::string_view message);

    std::vector<Region> mRegions;
    static constexpr uint32_t PageShift = 12U;
    static constexpr size_t PageSize = size_t{1} << PageShift;
    static constexpr size_t PageMask = PageSize - 1U;
    static constexpr size_t PageCount = size_t{1} << (32U - PageShift);
    static constexpr uint32_t FastGroupShift = 20U;
    static constexpr size_t FastGroupCount =
        size_t{1} << (32U - FastGroupShift);
    static constexpr size_t FastGroupPageCount =
        size_t{1} << (FastGroupShift - PageShift);
    static constexpr size_t FastGroupPageMask = FastGroupPageCount - 1U;
    static constexpr uint16_t AmbiguousPage = 0xFFFFU;
    std::vector<uint16_t> mPageRegions;
    std::array<std::unique_ptr<FastPage[]>, FastGroupCount> mFastPageGroups;
    uint64_t mWriteGeneration = 1;
    uint64_t mWriteTraceFingerprint = 14695981039346656037ULL;
    bool mTraceWriteFingerprint = false;
    mutable uint64_t mFastReadMismatchCount = 0;
    mutable uint32_t mLastFastReadMismatchAddress = 0;
    mutable uint64_t mLastFastReadMismatchValue = 0;
    mutable uint64_t mLastCheckedReadMismatchValue = 0;
    std::vector<std::pair<uint32_t, size_t>> mPendingTracedPointerWrites;
};

} // namespace Oot3dNativeGame
