#pragma once

#include "oot3d_guest_address_space.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Oot3dSourceRuntime {

struct SourceImageSegment {
    std::string_view Name;
    GuestAddress Address = 0;
    std::size_t MappedSize = 0;
    std::size_t FileOffset = 0;
    std::size_t FileSize = 0;
    bool Writable = false;
    bool Executable = false;
};

struct SourceImageInitialValue {
    std::size_t Offset = 0;
    std::size_t Size = 0;
    std::uint64_t Value = 0;
};

struct SourceImageSystemRegion {
    std::string_view Name;
    GuestAddress Address = 0;
    std::size_t MappedSize = 0;
    bool Writable = false;
    bool Executable = false;
    std::size_t InitialValueOffset = 0;
    std::size_t InitialValueCount = 0;
};

struct SourceImageZeroRegion {
    std::string_view Name;
    GuestAddress Address = 0;
    std::size_t MappedSize = 0;
    bool Writable = true;
    bool Executable = false;
};

struct SourcePrimaryThreadDescriptor {
    GuestAddress StackBaseAddress = 0;
    std::size_t StackSize = 0;
    GuestAddress ThreadPointer = 0;
    GuestAddress TlsBaseAddress = 0;
    std::size_t TlsSize = 0;
    std::uint32_t Argument0 = 0;
    std::uint32_t InitialCpsr = 0;
    std::uint32_t InitialFpscr = 0;
    std::uint32_t Priority = 0;
};

struct SourceProcessImageDescriptor {
    std::string_view CodeBinSha256;
    std::size_t CodeBinSize = 0;
    GuestAddress EntryAddress = 0;
    std::uint32_t PageSize = 0;
    std::span<const SourceImageSegment> Segments;
    std::span<const SourceImageSystemRegion> SystemRegions;
    std::span<const SourceImageZeroRegion> ZeroRegions;
    std::span<const SourceImageInitialValue> InitialValues;
    SourcePrimaryThreadDescriptor PrimaryThread;
};

struct MountedSourceProcessImage {
    MappedGuestAddressSpace Memory;
    GuestAddress EntryAddress = 0;
    SourcePrimaryThreadDescriptor PrimaryThread;
};

struct DirectMountedSourceProcessImage {
    DirectMappedGuestAddressSpace Memory;
    GuestAddress EntryAddress = 0;
    SourcePrimaryThreadDescriptor PrimaryThread;
};

std::optional<MountedSourceProcessImage> LoadSourceProcessImage(
    const SourceProcessImageDescriptor& descriptor,
    const std::filesystem::path& codeBinPath,
    std::string* error = nullptr);

std::optional<DirectMountedSourceProcessImage> LoadDirectMappedSourceProcessImage(
    const SourceProcessImageDescriptor& descriptor,
    const std::filesystem::path& codeBinPath,
    std::string* error = nullptr);

} // namespace Oot3dSourceRuntime
