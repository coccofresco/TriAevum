#include "oot3d_source_process_image.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace Oot3dSourceRuntime {
namespace {

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

std::optional<std::vector<std::byte>> ReadFile(const std::filesystem::path& path,
                                               std::string* error) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        SetError(error, "cannot open code.bin: " + path.string());
        return std::nullopt;
    }
    const std::streampos end = stream.tellg();
    if (end < 0) {
        SetError(error, "cannot determine code.bin size");
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        SetError(error, "cannot read complete code.bin");
        return std::nullopt;
    }
    return bytes;
}

GuestMemoryAccess AccessFor(bool writable, bool executable) {
    GuestMemoryAccess access = GuestMemoryAccess::Read;
    if (writable) {
        access = access | GuestMemoryAccess::Write;
    }
    if (executable) {
        access = access | GuestMemoryAccess::Execute;
    }
    return access;
}

bool ValidRange(GuestAddress address, std::size_t size) {
    return size != 0 &&
           static_cast<std::uint64_t>(address) + size <=
               static_cast<std::uint64_t>(std::numeric_limits<GuestAddress>::max()) + 1;
}

bool ValidatePrimaryThreadMapping(GuestAddressSpace& memory,
                                  const SourcePrimaryThreadDescriptor& thread,
                                  std::string* error) {
    if (!ValidRange(thread.StackBaseAddress, thread.StackSize) ||
        !ValidRange(thread.TlsBaseAddress, thread.TlsSize) ||
        thread.ThreadPointer < thread.TlsBaseAddress ||
        thread.ThreadPointer >=
            static_cast<std::uint64_t>(thread.TlsBaseAddress) + thread.TlsSize ||
        memory.ResolveWrite(thread.StackBaseAddress, thread.StackSize).size() !=
            thread.StackSize ||
        memory.ResolveWrite(thread.TlsBaseAddress, thread.TlsSize).size() !=
            thread.TlsSize) {
        SetError(error, "primary thread stack/TLS is not writable in the process image");
        return false;
    }
    return true;
}

} // namespace

std::optional<MountedSourceProcessImage> LoadSourceProcessImage(
    const SourceProcessImageDescriptor& descriptor,
    const std::filesystem::path& codeBinPath, std::string* error) {
    if (descriptor.PageSize == 0 ||
        (descriptor.PageSize & (descriptor.PageSize - 1U)) != 0 ||
        descriptor.Segments.empty()) {
        SetError(error, "invalid source process image descriptor");
        return std::nullopt;
    }

    auto code = ReadFile(codeBinPath, error);
    if (!code.has_value()) {
        return std::nullopt;
    }
    if (code->size() != descriptor.CodeBinSize) {
        SetError(error, "code.bin size does not match source process profile");
        return std::nullopt;
    }

    MountedSourceProcessImage mounted;
    mounted.EntryAddress = descriptor.EntryAddress;
    mounted.PrimaryThread = descriptor.PrimaryThread;

    for (const SourceImageZeroRegion& region : descriptor.ZeroRegions) {
        if (!ValidRange(region.Address, region.MappedSize) ||
            region.Address % descriptor.PageSize != 0 ||
            region.MappedSize % descriptor.PageSize != 0 ||
            !mounted.Memory.MapZeroed(
                region.Address, region.MappedSize,
                AccessFor(region.Writable, region.Executable),
                std::string(region.Name))) {
            SetError(error, "cannot map source process zero region: " +
                                std::string(region.Name));
            return std::nullopt;
        }
    }

    for (const SourceImageSystemRegion& region : descriptor.SystemRegions) {
        if (!ValidRange(region.Address, region.MappedSize) ||
            region.Address % descriptor.PageSize != 0 ||
            region.MappedSize % descriptor.PageSize != 0 ||
            region.InitialValueOffset > descriptor.InitialValues.size() ||
            region.InitialValueCount >
                descriptor.InitialValues.size() - region.InitialValueOffset) {
            SetError(error, "invalid system region in source process profile");
            return std::nullopt;
        }
        std::vector<std::byte> bytes(region.MappedSize);
        for (const SourceImageInitialValue& initial :
             descriptor.InitialValues.subspan(region.InitialValueOffset,
                                               region.InitialValueCount)) {
            if ((initial.Size != 1 && initial.Size != 2 && initial.Size != 4 &&
                 initial.Size != 8) ||
                initial.Offset > bytes.size() ||
                initial.Size > bytes.size() - initial.Offset) {
                SetError(error, "invalid system initial value in source process profile");
                return std::nullopt;
            }
            for (std::size_t byte = 0; byte < initial.Size; ++byte) {
                bytes[initial.Offset + byte] =
                    static_cast<std::byte>(initial.Value >> (byte * 8U));
            }
        }
        if (!mounted.Memory.Map(region.Address, std::move(bytes),
                                AccessFor(region.Writable, region.Executable),
                                std::string(region.Name))) {
            SetError(error, "cannot map source process system region: " +
                                std::string(region.Name));
            return std::nullopt;
        }
    }

    for (const SourceImageSegment& segment : descriptor.Segments) {
        if (!ValidRange(segment.Address, segment.MappedSize) ||
            segment.Address % descriptor.PageSize != 0 ||
            segment.MappedSize % descriptor.PageSize != 0 ||
            segment.FileSize > segment.MappedSize ||
            segment.FileOffset > code->size() ||
            segment.FileSize > code->size() - segment.FileOffset) {
            SetError(error, "invalid segment in source process profile");
            return std::nullopt;
        }
        std::vector<std::byte> bytes(segment.MappedSize);
        std::ranges::copy_n(code->begin() + segment.FileOffset,
                            segment.FileSize, bytes.begin());
        if (!mounted.Memory.Map(segment.Address, std::move(bytes),
                                AccessFor(segment.Writable, segment.Executable),
                                std::string(segment.Name))) {
            SetError(error, "cannot map source process segment: " +
                                std::string(segment.Name));
            return std::nullopt;
        }
    }
    if (!ValidatePrimaryThreadMapping(mounted.Memory, mounted.PrimaryThread, error)) {
        return std::nullopt;
    }
    return mounted;
}

std::optional<DirectMountedSourceProcessImage> LoadDirectMappedSourceProcessImage(
    const SourceProcessImageDescriptor& descriptor,
    const std::filesystem::path& codeBinPath, std::string* error) {
    if (descriptor.PageSize == 0 ||
        (descriptor.PageSize & (descriptor.PageSize - 1U)) != 0 ||
        descriptor.Segments.empty()) {
        SetError(error, "invalid source process image descriptor");
        return std::nullopt;
    }
    auto code = ReadFile(codeBinPath, error);
    if (!code.has_value()) {
        return std::nullopt;
    }
    if (code->size() != descriptor.CodeBinSize) {
        SetError(error, "code.bin size does not match source process profile");
        return std::nullopt;
    }

    DirectMountedSourceProcessImage mounted;
    mounted.EntryAddress = descriptor.EntryAddress;
    mounted.PrimaryThread = descriptor.PrimaryThread;
    for (const SourceImageZeroRegion& region : descriptor.ZeroRegions) {
        if (!ValidRange(region.Address, region.MappedSize) ||
            region.Address % descriptor.PageSize != 0 ||
            region.MappedSize % descriptor.PageSize != 0 ||
            !mounted.Memory.MapZeroed(
                region.Address, region.MappedSize,
                AccessFor(region.Writable, region.Executable),
                std::string(region.Name))) {
            SetError(error, "cannot direct-map source zero region: " +
                                std::string(region.Name));
            return std::nullopt;
        }
    }
    for (const SourceImageSystemRegion& region : descriptor.SystemRegions) {
        std::vector<std::byte> bytes(region.MappedSize);
        if (region.InitialValueOffset > descriptor.InitialValues.size() ||
            region.InitialValueCount >
                descriptor.InitialValues.size() - region.InitialValueOffset) {
            SetError(error, "invalid direct-mapped system initial values");
            return std::nullopt;
        }
        for (const SourceImageInitialValue& initial :
             descriptor.InitialValues.subspan(region.InitialValueOffset,
                                               region.InitialValueCount)) {
            if (initial.Offset > bytes.size() ||
                initial.Size > bytes.size() - initial.Offset) {
                SetError(error, "invalid direct-mapped system initial value");
                return std::nullopt;
            }
            for (std::size_t byte = 0; byte < initial.Size; ++byte) {
                bytes[initial.Offset + byte] =
                    static_cast<std::byte>(initial.Value >> (byte * 8U));
            }
        }
        if (!mounted.Memory.Map(region.Address, std::move(bytes),
                                AccessFor(region.Writable, region.Executable),
                                std::string(region.Name))) {
            SetError(error, "cannot direct-map source system region: " +
                                std::string(region.Name));
            return std::nullopt;
        }
    }
    for (const SourceImageSegment& segment : descriptor.Segments) {
        if (segment.FileOffset > code->size() ||
            segment.FileSize > code->size() - segment.FileOffset ||
            segment.FileSize > segment.MappedSize) {
            SetError(error, "invalid direct-mapped source segment");
            return std::nullopt;
        }
        std::vector<std::byte> bytes(segment.MappedSize);
        std::ranges::copy_n(code->begin() + segment.FileOffset,
                            segment.FileSize, bytes.begin());
        if (!mounted.Memory.Map(segment.Address, std::move(bytes),
                                AccessFor(segment.Writable, segment.Executable),
                                std::string(segment.Name))) {
            SetError(error, "cannot direct-map source segment: " +
                                std::string(segment.Name));
            return std::nullopt;
        }
    }
    if (!ValidatePrimaryThreadMapping(mounted.Memory, mounted.PrimaryThread, error)) {
        return std::nullopt;
    }
    return mounted;
}

} // namespace Oot3dSourceRuntime
