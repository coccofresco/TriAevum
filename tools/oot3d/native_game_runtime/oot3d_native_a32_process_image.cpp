#include "oot3d_native_a32_process_image.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

using Json = nlohmann::json;

void SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

uint32_t ReadU32(const Json& source, const char* field) {
    const auto& value = source.at(field);
    if (!value.is_number_unsigned()) {
        throw std::runtime_error(std::string(field) + " must be unsigned");
    }
    const uint64_t decoded = value.get<uint64_t>();
    if (decoded > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string(field) + " exceeds uint32");
    }
    return static_cast<uint32_t>(decoded);
}

size_t ReadSize(const Json& source, const char* field) {
    const auto& value = source.at(field);
    if (!value.is_number_unsigned()) {
        throw std::runtime_error(std::string(field) + " must be unsigned");
    }
    const uint64_t decoded = value.get<uint64_t>();
    if (decoded > std::numeric_limits<size_t>::max()) {
        throw std::runtime_error(std::string(field) + " exceeds size_t");
    }
    return static_cast<size_t>(decoded);
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open code.bin: " + path.string());
    }
    const auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error("cannot determine code.bin size");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("cannot read complete code.bin");
    }
    return bytes;
}

bool IsDevicePath(const std::filesystem::path& path) {
    const std::string encoded = path.generic_string();
    const size_t marker = encoded.find(":/");
    return marker != std::string::npos && marker != 0U;
}

std::filesystem::path
ResolvePackagedInputPath(const std::filesystem::path& manifestPath,
                         std::string encoded) {
    std::replace(encoded.begin(), encoded.end(), '\\', '/');
    std::filesystem::path input(encoded);
    if (input.is_absolute() || IsDevicePath(input)) {
        return input.lexically_normal();
    }

    const auto manifestDirectory = manifestPath.parent_path();
    const auto first = input.begin();
    if (first != input.end() && *first == manifestDirectory.filename()) {
        return (manifestDirectory.parent_path() / input).lexically_normal();
    }
    return (manifestDirectory / input).lexically_normal();
}

std::optional<NativeA32ProcessImageManifest>
ParseNativeA32ProcessImageManifest(const Json& document,
                                   const std::filesystem::path& path,
                                   bool validateHostFiles, std::string* error) {
    try {
        if (document.value("format", "") !=
            "oot3d_native_process_manifest_v1") {
            throw std::runtime_error(
                "unsupported native process manifest format");
        }

        const auto& source = document.at("source");
        const auto& process = document.at("process");
        const auto& thread = document.at("primary_thread");
        NativeA32ProcessImageManifest result;
        result.ManifestPath = IsDevicePath(path)
                                  ? path.lexically_normal()
                                  : std::filesystem::absolute(path);
        result.CodeBinPath = ResolvePackagedInputPath(
            result.ManifestPath, source.at("code_bin_path").get<std::string>());
        result.CodeBinSha256 = source.at("code_bin_sha256").get<std::string>();
        result.CodeBinSize = ReadSize(source, "code_bin_size");
        result.RomFsImagePath = ResolvePackagedInputPath(
            result.ManifestPath,
            source.at("romfs_image_path").get<std::string>());
        result.RomFsImageFileSize =
            source.at("romfs_image_file_size").get<uint64_t>();
        result.RomFsImageOffset =
            source.at("romfs_service_offset").get<uint64_t>();
        result.RomFsImageSize = source.at("romfs_service_size").get<uint64_t>();
        result.ProcessName = process.at("name").get<std::string>();
        result.EntryAddress = ReadU32(process, "entrypoint");
        result.PageSize = ReadU32(process, "page_size");
        if (result.CodeBinSha256.size() != 64 ||
            result.RomFsImagePath.empty() || result.RomFsImageSize == 0 ||
            result.RomFsImageOffset > result.RomFsImageFileSize ||
            result.RomFsImageSize >
                result.RomFsImageFileSize - result.RomFsImageOffset ||
            result.PageSize == 0 ||
            (result.PageSize & (result.PageSize - 1U)) != 0) {
            throw std::runtime_error("invalid code hash or process page size");
        }
        if (validateHostFiles &&
            (!std::filesystem::is_regular_file(result.RomFsImagePath) ||
             std::filesystem::file_size(result.RomFsImagePath) !=
                 result.RomFsImageFileSize)) {
            throw std::runtime_error(
                "native process RomFS image is missing or has changed size");
        }

        std::set<std::string> segmentNames;
        for (const auto& item : process.at("segments")) {
            NativeA32ProcessImageSegment segment;
            segment.Name = item.at("name").get<std::string>();
            segment.Address = ReadU32(item, "address");
            segment.MappedSize = ReadSize(item, "mapped_size");
            segment.DeclaredCodeSize = ReadSize(item, "declared_code_size");
            segment.FileOffset = ReadSize(item, "file_offset");
            segment.FileSize = ReadSize(item, "file_size");
            segment.Writable = item.at("writable").get<bool>();
            segment.Executable = item.at("executable").get<bool>();
            if (segment.Name.empty() ||
                !segmentNames.insert(segment.Name).second ||
                segment.MappedSize == 0 ||
                segment.DeclaredCodeSize > segment.FileSize ||
                segment.FileSize > segment.MappedSize ||
                segment.Address % result.PageSize != 0 ||
                segment.MappedSize % result.PageSize != 0 ||
                segment.FileOffset > result.CodeBinSize ||
                segment.FileSize > result.CodeBinSize - segment.FileOffset) {
                throw std::runtime_error("invalid native process segment");
            }
            result.Segments.push_back(std::move(segment));
        }
        if (result.Segments.empty()) {
            throw std::runtime_error("native process manifest has no segments");
        }

        for (const auto& item : process.at("system_regions")) {
            NativeA32ProcessSystemRegion region;
            region.Name = item.at("name").get<std::string>();
            region.Address = ReadU32(item, "address");
            region.MappedSize = ReadSize(item, "mapped_size");
            region.Writable = item.at("writable").get<bool>();
            region.Executable = item.at("executable").get<bool>();
            if (region.Name.empty() ||
                !segmentNames.insert(region.Name).second ||
                region.MappedSize == 0 ||
                region.Address % result.PageSize != 0 ||
                region.MappedSize % result.PageSize != 0) {
                throw std::runtime_error(
                    "invalid native process system region");
            }
            region.InitialBytes.assign(region.MappedSize, 0);
            std::vector<bool> initialized(region.MappedSize, false);
            for (const auto& value : item.at("initial_values")) {
                const size_t offset = ReadSize(value, "offset");
                const size_t size = ReadSize(value, "size");
                const uint64_t decoded = value.at("value").get<uint64_t>();
                if ((size != 1 && size != 2 && size != 4 && size != 8) ||
                    offset > region.MappedSize ||
                    size > region.MappedSize - offset ||
                    (size < 8 && decoded >= (uint64_t{1} << (size * 8U)))) {
                    throw std::runtime_error(
                        "invalid system region initial value");
                }
                for (size_t index = 0; index < size; ++index) {
                    if (initialized[offset + index]) {
                        throw std::runtime_error(
                            "overlapping system region initial values");
                    }
                    initialized[offset + index] = true;
                    region.InitialBytes[offset + index] =
                        static_cast<uint8_t>(decoded >> (index * 8U));
                }
            }
            result.SystemRegions.push_back(std::move(region));
        }

        const auto readResourceValues = [](const Json& values,
                                           std::array<uint64_t, 10>& output) {
            if (!values.is_array() || values.size() != output.size()) {
                throw std::runtime_error(
                    "CTR resource limit table must contain ten values");
            }
            for (size_t index = 0; index < output.size(); ++index) {
                if (!values[index].is_number_unsigned()) {
                    throw std::runtime_error(
                        "CTR resource limit value must be unsigned");
                }
                output[index] = values[index].get<uint64_t>();
            }
        };
        const auto& resourceLimit = process.at("resource_limit");
        readResourceValues(resourceLimit.at("limit_values"),
                           result.ResourceLimitValues);
        readResourceValues(resourceLimit.at("initial_values"),
                           result.ResourceCurrentValues);
        const auto& linearHeap = process.at("linear_heap");
        result.LinearHeapBaseAddress = ReadU32(linearHeap, "base_address");
        result.LinearHeapSize = ReadSize(linearHeap, "size");
        if (result.LinearHeapSize == 0 ||
            result.LinearHeapBaseAddress % result.PageSize != 0 ||
            result.LinearHeapSize % result.PageSize != 0 ||
            static_cast<uint64_t>(result.LinearHeapBaseAddress) +
                    result.LinearHeapSize >
                static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) +
                    1U) {
            throw std::runtime_error("invalid CTR linear heap range");
        }
        const auto& heap = process.at("heap");
        result.HeapBaseAddress = ReadU32(heap, "base_address");
        result.HeapSize = ReadSize(heap, "size");
        if (result.HeapSize == 0 ||
            result.HeapBaseAddress % result.PageSize != 0 ||
            result.HeapSize % result.PageSize != 0 ||
            static_cast<uint64_t>(result.HeapBaseAddress) + result.HeapSize >
                static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) +
                    1U) {
            throw std::runtime_error("invalid CTR heap range");
        }

        result.PrimaryThread.EntryAddress = result.EntryAddress;
        result.PrimaryThread.StackBaseAddress =
            ReadU32(thread, "stack_base_address");
        result.PrimaryThread.StackSize = ReadSize(thread, "stack_size");
        result.PrimaryThread.TlsBaseAddress =
            ReadU32(thread, "tls_base_address");
        result.PrimaryThread.TlsSize = ReadSize(thread, "tls_size");
        result.PrimaryThread.ThreadPointer = ReadU32(thread, "thread_pointer");
        result.PrimaryThread.Argument0 = ReadU32(thread, "argument0");
        result.PrimaryThread.InitialCpsr = ReadU32(thread, "initial_cpsr");
        result.PrimaryThread.InitialFpscr = ReadU32(thread, "initial_fpscr");
        result.PrimaryThread.Priority = ReadU32(thread, "priority");
        return result;
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return std::nullopt;
    }
}

bool MountNativeA32ProcessImageBytes(
    NativeA32Process& process, const NativeA32ProcessImageManifest& manifest,
    std::span<const uint8_t> code, std::string* error) {
    try {
        if (code.size() != manifest.CodeBinSize) {
            throw std::runtime_error(
                "code.bin size does not match process manifest");
        }
        for (const auto& region : manifest.SystemRegions) {
            std::string mapError;
            if (!process.MapRegion({region.Name, region.Address,
                                    region.MappedSize, region.Writable,
                                    region.Executable, region.InitialBytes},
                                   &mapError)) {
                throw std::runtime_error("cannot map " + region.Name + ": " +
                                         mapError);
            }
        }
        for (const auto& segment : manifest.Segments) {
            const auto initial =
                code.subspan(segment.FileOffset, segment.FileSize);
            std::string mapError;
            if (!process.MapRegion({segment.Name, segment.Address,
                                    segment.MappedSize, segment.Writable,
                                    segment.Executable, initial},
                                   &mapError)) {
                throw std::runtime_error("cannot map " + segment.Name + ": " +
                                         mapError);
            }
        }
        std::string threadError;
        if (!process.CreatePrimaryThread(manifest.PrimaryThread,
                                         &threadError)) {
            throw std::runtime_error("cannot create primary thread: " +
                                     threadError);
        }
        return true;
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return false;
    }
}

} // namespace

std::optional<NativeA32ProcessImageManifest>
LoadNativeA32ProcessImageManifest(const std::filesystem::path& path,
                                  std::string* error) {
    try {
        std::ifstream stream(path);
        if (!stream) {
            throw std::runtime_error("cannot open native process manifest");
        }
        Json document;
        stream >> document;
        return ParseNativeA32ProcessImageManifest(document, path, true, error);
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return std::nullopt;
    }
}

std::optional<NativeA32ProcessImageManifest>
LoadNativeA32ProcessImageManifest(std::span<const uint8_t> encodedManifest,
                                  std::string* error) {
    try {
        if (encodedManifest.empty()) {
            throw std::runtime_error("native process manifest is empty");
        }
        const Json document =
            Json::parse(encodedManifest.begin(), encodedManifest.end());
        return ParseNativeA32ProcessImageManifest(
            document, std::filesystem::path("process_manifest"), false, error);
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return std::nullopt;
    }
}

bool MountNativeA32ProcessImage(NativeA32Process& process,
                                const NativeA32ProcessImageManifest& manifest,
                                const std::filesystem::path& codeBinOverride,
                                std::string* error) {
    try {
        const auto codePath =
            codeBinOverride.empty() ? manifest.CodeBinPath : codeBinOverride;
        const auto code = ReadFile(codePath);
        return MountNativeA32ProcessImageBytes(process, manifest, code, error);
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return false;
    }
}

bool MountNativeA32ProcessImage(NativeA32Process& process,
                                const NativeA32ProcessImageManifest& manifest,
                                std::span<const uint8_t> codeBin,
                                std::string* error) {
    return MountNativeA32ProcessImageBytes(process, manifest, codeBin, error);
}

} // namespace Oot3dNativeGame
