#include "oot3d_native_a32_memory.h"

#include <algorithm>
#include <limits>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

bool RangeFits(uint32_t baseAddress, size_t size) {
    return size != 0 &&
           static_cast<uint64_t>(baseAddress) + static_cast<uint64_t>(size) <=
               static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1U;
}

bool RangesOverlap(uint32_t leftBase, size_t leftSize, uint32_t rightBase,
                   size_t rightSize) {
    const uint64_t leftEnd = static_cast<uint64_t>(leftBase) + leftSize;
    const uint64_t rightEnd = static_cast<uint64_t>(rightBase) + rightSize;
    return static_cast<uint64_t>(leftBase) < rightEnd &&
           static_cast<uint64_t>(rightBase) < leftEnd;
}

bool IsSupportedSize(uint8_t size) {
    return size == 1 || size == 2 || size == 4 || size == 8;
}

} // namespace

NativeA32Memory::NativeA32Memory(const NativeA32Memory& other)
    : mRegions(other.mRegions), mWriteGeneration(other.mWriteGeneration),
      mWriteTraceFingerprint(other.mWriteTraceFingerprint),
      mTraceWriteFingerprint(other.mTraceWriteFingerprint),
      mFastReadMismatchCount(other.mFastReadMismatchCount),
      mLastFastReadMismatchAddress(other.mLastFastReadMismatchAddress),
      mLastFastReadMismatchValue(other.mLastFastReadMismatchValue),
      mLastCheckedReadMismatchValue(other.mLastCheckedReadMismatchValue) {
    RebuildPageTable();
}

NativeA32Memory& NativeA32Memory::operator=(const NativeA32Memory& other) {
    if (this != &other) {
        mRegions = other.mRegions;
        mWriteGeneration = other.mWriteGeneration;
        mWriteTraceFingerprint = other.mWriteTraceFingerprint;
        mTraceWriteFingerprint = other.mTraceWriteFingerprint;
        mFastReadMismatchCount = other.mFastReadMismatchCount;
        mLastFastReadMismatchAddress = other.mLastFastReadMismatchAddress;
        mLastFastReadMismatchValue = other.mLastFastReadMismatchValue;
        mLastCheckedReadMismatchValue = other.mLastCheckedReadMismatchValue;
        RebuildPageTable();
    }
    return *this;
}

void NativeA32Memory::SetError(std::string* error, std::string_view message) {
    if (error != nullptr) {
        *error = message;
    }
}

bool NativeA32Memory::MapRegion(const NativeA32MemoryRegionConfig& config,
                                std::string* error) {
    if (config.Name.empty()) {
        SetError(error, "guest memory region has no name");
        return false;
    }
    if (!RangeFits(config.BaseAddress, config.Size)) {
        SetError(error, "guest memory region has an invalid address range");
        return false;
    }
    if (config.InitialBytes.size() > config.Size) {
        SetError(error, "guest memory initial image exceeds its region");
        return false;
    }
    if (mRegions.size() >= AmbiguousPage - 1U) {
        SetError(error, "guest memory has too many regions for its page table");
        return false;
    }
    for (const auto& region : mRegions) {
        if (RangesOverlap(config.BaseAddress, config.Size, region.BaseAddress,
                          region.Bytes.size())) {
            SetError(error, "guest memory regions overlap");
            return false;
        }
    }

    Region region;
    region.Name = config.Name;
    region.BaseAddress = config.BaseAddress;
    region.Bytes.assign(config.Size, 0);
    std::copy(config.InitialBytes.begin(), config.InitialBytes.end(),
              region.Bytes.begin());
    region.Writable = config.Writable;
    region.Executable = config.Executable;
    mRegions.push_back(std::move(region));
    std::sort(mRegions.begin(), mRegions.end(),
              [](const Region& left, const Region& right) {
                  return left.BaseAddress < right.BaseAddress;
              });
    RebuildPageTable();
    return true;
}

void NativeA32Memory::RebuildPageTable() {
    mPageRegions.assign(PageCount, 0U);
    for (auto& group : mFastPageGroups) {
        group.reset();
    }
    for (size_t regionIndex = 0; regionIndex < mRegions.size();
         ++regionIndex) {
        auto& region = mRegions[regionIndex];
        const uint64_t lastAddress =
            static_cast<uint64_t>(region.BaseAddress) +
            region.Bytes.size() - 1U;
        const size_t firstPage = region.BaseAddress >> PageShift;
        const size_t lastPage = static_cast<size_t>(lastAddress >> PageShift);
        const uint16_t pageValue =
            static_cast<uint16_t>(regionIndex + 1U);
        for (size_t page = firstPage; page <= lastPage; ++page) {
            auto& entry = mPageRegions[page];
            if (entry == 0U) {
                entry = pageValue;
            } else if (entry != pageValue) {
                entry = AmbiguousPage;
            }

            const uint64_t pageAddress =
                static_cast<uint64_t>(page) << PageShift;
            const uint64_t regionEnd =
                static_cast<uint64_t>(region.BaseAddress) +
                region.Bytes.size();
            if (pageAddress >= region.BaseAddress &&
                pageAddress + PageSize <= regionEnd) {
                const size_t groupIndex = page / FastGroupPageCount;
                const size_t groupPage = page & FastGroupPageMask;
                auto& group = mFastPageGroups[groupIndex];
                if (group == nullptr) {
                    group = std::make_unique<FastPage[]>(
                        FastGroupPageCount);
                }
                auto& fastPage = group[groupPage];
                const size_t offset = static_cast<size_t>(
                    pageAddress - region.BaseAddress);
                fastPage.Read = region.Bytes.data() + offset;
                fastPage.Write = region.Writable
                                     ? region.Bytes.data() + offset
                                     : nullptr;
            }
        }
    }
}

NativeA32Memory::Region* NativeA32Memory::FindRegion(uint32_t address,
                                                     size_t size) {
    return const_cast<Region*>(
        static_cast<const NativeA32Memory*>(this)->FindRegion(address, size));
}

const NativeA32Memory::Region* NativeA32Memory::FindRegion(
    uint32_t address, size_t size) const {
    if (!RangeFits(address, size)) {
        return nullptr;
    }
    const uint64_t end = static_cast<uint64_t>(address) + size;
    const auto contains = [&](size_t index) {
        const auto& region = mRegions[index];
        return address >= region.BaseAddress &&
               end <= static_cast<uint64_t>(region.BaseAddress) +
                          region.Bytes.size();
    };
    if (!mPageRegions.empty()) {
        const uint16_t pageEntry = mPageRegions[address >> PageShift];
        if (pageEntry != 0U && pageEntry != AmbiguousPage) {
            const size_t index = pageEntry - 1U;
            if (index < mRegions.size() && contains(index)) {
                return &mRegions[index];
            }
        }
    }
    auto found = std::upper_bound(
        mRegions.begin(), mRegions.end(), address,
        [](uint32_t candidate, const Region& region) {
            return candidate < region.BaseAddress;
        });
    if (found == mRegions.begin()) {
        return nullptr;
    }
    const size_t index =
        static_cast<size_t>(std::distance(mRegions.begin(), found - 1));
    if (contains(index)) {
        return &mRegions[index];
    }
    return nullptr;
}

bool NativeA32Memory::IsMapped(uint32_t address, size_t size) const {
    return FindRegion(address, size) != nullptr;
}

bool NativeA32Memory::IsWritable(uint32_t address, size_t size) const {
    const auto* region = FindRegion(address, size);
    return region != nullptr && region->Writable;
}

bool NativeA32Memory::IsExecutable(uint32_t address, size_t size) const {
    const auto* region = FindRegion(address, size);
    return region != nullptr && region->Executable;
}

size_t NativeA32Memory::RegionCount() const {
    return mRegions.size();
}

uint64_t NativeA32Memory::WriteGeneration() const {
    return mWriteGeneration;
}

std::optional<uint64_t>
NativeA32Memory::RangeWriteGeneration(uint32_t address,
                                      size_t size) const noexcept {
  if (FindRegion(address, size) == nullptr) {
    return std::nullopt;
  }

  // Whole-AOT writers already advance this token. Keeping invalidation
  // conservative preserves their precompiled NativeA32Memory ABI.
  return mWriteGeneration;
}

void NativeA32Memory::EnableWriteTraceFingerprint(bool enabled) noexcept {
    mTraceWriteFingerprint = enabled;
    mWriteTraceFingerprint = 14695981039346656037ULL;
    mPendingTracedPointerWrites.clear();
}

uint64_t NativeA32Memory::WriteTraceFingerprint() noexcept {
    FlushTracedPointerWrites();
    return mWriteTraceFingerprint;
}

uint64_t NativeA32Memory::FastReadMismatchCount() const noexcept {
    return mFastReadMismatchCount;
}

uint32_t NativeA32Memory::LastFastReadMismatchAddress() const noexcept {
    return mLastFastReadMismatchAddress;
}

uint64_t NativeA32Memory::LastFastReadMismatchValue() const noexcept {
    return mLastFastReadMismatchValue;
}

uint64_t NativeA32Memory::LastCheckedReadMismatchValue() const noexcept {
    return mLastCheckedReadMismatchValue;
}

void NativeA32Memory::RecordTracedWrite(
    uint32_t address, std::span<const uint8_t> bytes) noexcept {
    if (!mTraceWriteFingerprint) {
        return;
    }
    const auto append = [&](uint8_t byte) {
        mWriteTraceFingerprint ^= byte;
        mWriteTraceFingerprint *= 1099511628211ULL;
    };
    for (size_t index = 0; index < sizeof(address); ++index) {
        append(static_cast<uint8_t>(address >> (index * 8U)));
    }
    const uint64_t size = bytes.size();
    for (size_t index = 0; index < sizeof(size); ++index) {
        append(static_cast<uint8_t>(size >> (index * 8U)));
    }
    for (const uint8_t byte : bytes) {
        append(byte);
    }
}

void NativeA32Memory::FlushTracedPointerWrites() noexcept {
    for (const auto& [address, size] : mPendingTracedPointerWrites) {
        const auto* region = FindRegion(address, size);
        if (region == nullptr) {
            continue;
        }
        const size_t offset =
            static_cast<size_t>(address - region->BaseAddress);
        RecordTracedWrite(
            address,
            std::span<const uint8_t>(region->Bytes.data() + offset, size));
    }
    mPendingTracedPointerWrites.clear();
}

uint64_t NativeA32Memory::ContentFingerprint() const noexcept {
    uint64_t hash = 14695981039346656037ULL;
    const auto append = [&](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t index = 0; index < size; ++index) {
            hash ^= bytes[index];
            hash *= 1099511628211ULL;
        }
    };
    for (const auto& region : mRegions) {
        append(region.Name.data(), region.Name.size());
        append(&region.BaseAddress, sizeof(region.BaseAddress));
        append(&region.Writable, sizeof(region.Writable));
        append(&region.Executable, sizeof(region.Executable));
        if (!region.Bytes.empty()) {
            append(region.Bytes.data(), region.Bytes.size());
        }
    }
    return hash;
}

uint64_t NativeA32Memory::StateFingerprint() const noexcept {
    uint64_t hash = 14695981039346656037ULL;
    const auto append = [&](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t index = 0; index < size; ++index) {
            hash ^= bytes[index];
            hash *= 1099511628211ULL;
        }
    };
    append(&mWriteGeneration, sizeof(mWriteGeneration));
    for (const auto& region : mRegions) {
        append(region.Name.data(), region.Name.size());
        append(&region.BaseAddress, sizeof(region.BaseAddress));
        append(&region.Writable, sizeof(region.Writable));
        append(&region.Executable, sizeof(region.Executable));
        if (!region.Bytes.empty()) {
            append(region.Bytes.data(), region.Bytes.size());
        }
    }
    return hash;
}

nlohmann::json NativeA32Memory::CaptureState() const {
    nlohmann::json regions = nlohmann::json::array();
    for (const auto& region : mRegions) {
        regions.push_back({
            {"name", region.Name},
            {"base_address", region.BaseAddress},
            {"writable", region.Writable},
            {"executable", region.Executable},
            {"bytes", nlohmann::json::binary(region.Bytes)},
        });
    }
    return {
        {"format", "oot3d_native_a32_memory_state_v1"},
        {"write_generation", mWriteGeneration},
        {"regions", std::move(regions)},
    };
}

bool NativeA32Memory::RestoreState(const nlohmann::json& state,
                                   std::string* error) {
    try {
        if (!state.is_object() ||
            state.value("format", std::string{}) !=
                "oot3d_native_a32_memory_state_v1" ||
            !state.contains("regions") || !state["regions"].is_array()) {
            SetError(error, "native A32 memory state format is invalid");
            return false;
        }
        NativeA32Memory staged;
        for (const auto& encoded : state["regions"]) {
            if (!encoded.is_object() || !encoded.contains("bytes") ||
                !encoded["bytes"].is_binary()) {
                SetError(error, "native A32 memory region state is invalid");
                return false;
            }
            const auto& bytes = encoded["bytes"].get_binary();
            NativeA32MemoryRegionConfig config;
            config.Name = encoded.at("name").get<std::string>();
            config.BaseAddress = encoded.at("base_address").get<uint32_t>();
            config.Size = bytes.size();
            config.Writable = encoded.at("writable").get<bool>();
            config.Executable = encoded.at("executable").get<bool>();
            config.InitialBytes = std::span<const uint8_t>(bytes.data(),
                                                           bytes.size());
            if (!staged.MapRegion(config, error)) {
                return false;
            }
        }
        staged.mWriteGeneration =
            state.at("write_generation").get<uint64_t>();
        if (staged.mWriteGeneration == 0U) {
            SetError(error,
                     "native A32 memory write generation is invalid");
            return false;
        }
        staged.mTraceWriteFingerprint = mTraceWriteFingerprint;
        *this = std::move(staged);
        // Fast-page entries contain pointers into region backing storage.
        // Rebind them after replacing the complete memory image so restoring a
        // savestate cannot retain pointers owned by the staged instance.
        RebuildPageTable();
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = std::string("native A32 memory state decode failed: ") +
                     exception.what();
        }
        return false;
    }
}

bool NativeA32Memory::ReadBytes(uint32_t address,
                                std::span<uint8_t> bytes) const {
    if (bytes.empty()) {
        return true;
    }
    const auto* region = FindRegion(address, bytes.size());
    if (region == nullptr) {
        return false;
    }
    const size_t offset = static_cast<size_t>(address - region->BaseAddress);
    std::copy_n(region->Bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                bytes.size(), bytes.begin());
    return true;
}

const uint8_t* NativeA32Memory::GetReadPointer(uint32_t address,
                                               size_t size) const {
    const auto* region = FindRegion(address, size);
    if (region == nullptr) {
        return nullptr;
    }
    return region->Bytes.data() +
           static_cast<size_t>(address - region->BaseAddress);
}

uint8_t* NativeA32Memory::GetWritePointer(uint32_t address, size_t size) {
    auto* region = FindRegion(address, size);
    if (region == nullptr || !region->Writable) {
        return nullptr;
    }
    uint8_t* destination = region->Bytes.data() +
                           static_cast<size_t>(address - region->BaseAddress);
    if (mTraceWriteFingerprint) {
        mPendingTracedPointerWrites.emplace_back(address, size);
    }
    ++mWriteGeneration;
    return destination;
}

std::optional<uint32_t>
NativeA32Memory::GetGuestAddress(const void* pointer,
                                 size_t size) const noexcept {
    if (pointer == nullptr || size == 0U) {
        return std::nullopt;
    }
    const uintptr_t candidate = reinterpret_cast<uintptr_t>(pointer);
    for (const auto& region : mRegions) {
        const uintptr_t base =
            reinterpret_cast<uintptr_t>(region.Bytes.data());
        if (candidate < base) {
            continue;
        }
        const uintptr_t rawOffset = candidate - base;
        if (rawOffset > region.Bytes.size()) {
            continue;
        }
        const size_t offset = static_cast<size_t>(rawOffset);
        if (size > region.Bytes.size() - offset) {
            continue;
        }
        const uint64_t guestAddress =
            static_cast<uint64_t>(region.BaseAddress) + offset;
        if (guestAddress <= std::numeric_limits<uint32_t>::max()) {
            return static_cast<uint32_t>(guestAddress);
        }
    }
    return std::nullopt;
}

bool NativeA32Memory::WriteBytes(uint32_t address,
                                 std::span<const uint8_t> bytes) {
    if (bytes.empty()) {
        return true;
    }
    auto* region = FindRegion(address, bytes.size());
    if (region == nullptr || !region->Writable) {
        return false;
    }
    const size_t offset = static_cast<size_t>(address - region->BaseAddress);
    std::copy(bytes.begin(), bytes.end(),
              region->Bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    if (mTraceWriteFingerprint) [[unlikely]] {
        RecordTracedWrite(address, bytes);
    }
    ++mWriteGeneration;
    return true;
}

bool NativeA32Memory::WriteHostBytes(
    uint32_t address, std::span<const uint8_t> bytes) {
    if (bytes.empty()) {
        return true;
    }
    auto* region = FindRegion(address, bytes.size());
    if (region == nullptr) {
        return false;
    }
    const size_t offset = static_cast<size_t>(address - region->BaseAddress);
    std::copy(bytes.begin(), bytes.end(),
              region->Bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    if (mTraceWriteFingerprint) [[unlikely]] {
        RecordTracedWrite(address, bytes);
    }
    ++mWriteGeneration;
    return true;
}

bool NativeA32Memory::Fill(uint32_t address, size_t size, uint8_t value) {
    if (size == 0) {
        return true;
    }
    auto* region = FindRegion(address, size);
    if (region == nullptr || !region->Writable) {
        return false;
    }
    const size_t offset = static_cast<size_t>(address - region->BaseAddress);
    std::fill_n(region->Bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                size, value);
    if (mTraceWriteFingerprint) [[unlikely]] {
        RecordTracedWrite(
            address,
            std::span<const uint8_t>(region->Bytes.data() + offset, size));
    }
    ++mWriteGeneration;
    return true;
}

bool NativeA32Memory::ReadSized(uint32_t address, uint8_t size,
                                uint64_t* value,
                                uint32_t* faultAddress) const {
    if (value == nullptr || !IsSupportedSize(size)) {
        if (faultAddress != nullptr) {
            *faultAddress = address;
        }
        return false;
    }
    const auto* region = FindRegion(address, size);
    if (region == nullptr) {
        if (faultAddress != nullptr) {
            *faultAddress = address;
        }
        return false;
    }
    const size_t offset = static_cast<size_t>(address - region->BaseAddress);
    uint64_t decoded = 0;
    for (uint8_t index = 0; index < size; ++index) {
        decoded |= static_cast<uint64_t>(region->Bytes[offset + index])
                   << (index * 8U);
    }
    *value = decoded;
    return true;
}

bool NativeA32Memory::WriteSized(uint32_t address, uint8_t size,
                                 uint64_t value, uint32_t* faultAddress) {
    if (!IsSupportedSize(size)) {
        if (faultAddress != nullptr) {
            *faultAddress = address;
        }
        return false;
    }
    auto* region = FindRegion(address, size);
    if (region == nullptr || !region->Writable) {
        if (faultAddress != nullptr) {
            *faultAddress = address;
        }
        return false;
    }
    const size_t offset = static_cast<size_t>(address - region->BaseAddress);
    for (uint8_t index = 0; index < size; ++index) {
        region->Bytes[offset + index] =
            static_cast<uint8_t>(value >> (index * 8U));
    }
    if (mTraceWriteFingerprint) [[unlikely]] {
        RecordTracedWrite(
            address,
            std::span<const uint8_t>(region->Bytes.data() + offset, size));
    }
    ++mWriteGeneration;
    return true;
}

bool NativeA32Memory::Read8(uint32_t address, uint8_t* value) {
    return ReadFast(address, value);
}

bool NativeA32Memory::Read16(uint32_t address, uint16_t* value) {
    return ReadFast(address, value);
}

bool NativeA32Memory::Read32(uint32_t address, uint32_t* value) {
    return ReadFast(address, value);
}

bool NativeA32Memory::Read64(uint32_t address, uint64_t* value,
                             uint32_t* faultAddress) {
    return ReadFast(address, value, faultAddress);
}

bool NativeA32Memory::Write8(uint32_t address, uint8_t value) {
    return WriteFast(address, value);
}

bool NativeA32Memory::Write16(uint32_t address, uint16_t value) {
    return WriteFast(address, value);
}

bool NativeA32Memory::Write32(uint32_t address, uint32_t value) {
    return WriteFast(address, value);
}

bool NativeA32Memory::Write64(uint32_t address, uint64_t value,
                             uint32_t* faultAddress) {
    return WriteFast(address, value, faultAddress);
}

bool NativeA32Memory::LoadExclusive(uint32_t address, uint8_t size,
                                    uint64_t* value, uint64_t* token,
                                    uint32_t* faultAddress) {
    if (token == nullptr || !ReadSized(address, size, value, faultAddress)) {
        return false;
    }
    *token = mWriteGeneration;
    return true;
}

oot3d::recomp::a32::ExclusiveStoreResult NativeA32Memory::StoreExclusive(
    uint32_t address, uint8_t size, uint64_t value, uint64_t token,
    uint32_t* faultAddress) {
    if (token != mWriteGeneration) {
        return oot3d::recomp::a32::ExclusiveStoreResult::ReservationLost;
    }
    if (!WriteSized(address, size, value, faultAddress)) {
        return oot3d::recomp::a32::ExclusiveStoreResult::MemoryFault;
    }
    return oot3d::recomp::a32::ExclusiveStoreResult::Success;
}

bool NativeA32Memory::AtomicSwap(uint32_t address, uint8_t size,
                                 uint32_t replacement, uint32_t* previous,
                                 uint32_t* faultAddress) {
    uint64_t oldValue = 0;
    if (previous == nullptr || !ReadSized(address, size, &oldValue, faultAddress) ||
        !WriteSized(address, size, replacement, faultAddress)) {
        return false;
    }
    *previous = static_cast<uint32_t>(oldValue);
    return true;
}

} // namespace Oot3dNativeGame
