#include "oot3d_ctr_fs_service.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace Oot3dSourceRuntime {
namespace {

constexpr std::uint32_t kInitializeRequest = 0x08010002U;
constexpr std::uint32_t kInitializeResponse = 0x08010040U;
constexpr std::uint32_t kOpenFileDirectlyRequest = 0x08030204U;
constexpr std::uint32_t kOpenFileDirectlyResponse = 0x08030042U;
constexpr std::uint32_t kOpenFileRequest = 0x080201C2U;
constexpr std::uint32_t kOpenFileResponse = 0x08020042U;
constexpr std::uint32_t kCreateFileRequest = 0x08080202U;
constexpr std::uint32_t kCreateFileResponse = 0x08080040U;
constexpr std::uint32_t kOpenArchiveRequest = 0x080C00C2U;
constexpr std::uint32_t kOpenArchiveResponse = 0x080C00C0U;
constexpr std::uint32_t kCloseArchiveRequest = 0x080E0080U;
constexpr std::uint32_t kCloseArchiveResponse = 0x080E0040U;
constexpr std::uint32_t kControlArchiveRequest = 0x080D0144U;
constexpr std::uint32_t kControlArchiveResponse = 0x080D0040U;
constexpr std::uint32_t kInvalidArchiveHandle = 0xC8804465U;
constexpr std::uint32_t kFileNotFound = 0xC8804470U;
constexpr std::uint32_t kFileAlreadyExists = 0xC82044B4U;
constexpr std::uint32_t kFileReadRequest = 0x080200C2U;
constexpr std::uint32_t kFileReadResponse = 0x08020082U;
constexpr std::uint32_t kFileGetSizeRequest = 0x08040000U;
constexpr std::uint32_t kFileGetSizeResponse = 0x080400C0U;
constexpr std::uint32_t kFileCloseRequest = 0x08080000U;
constexpr std::uint32_t kFileCloseResponse = 0x08080040U;
constexpr std::uint32_t kFileWriteRequest = 0x08030102U;
constexpr std::uint32_t kFileWriteResponse = 0x08030082U;
constexpr std::uint32_t kFileSetSizeRequest = 0x08050080U;
constexpr std::uint32_t kFileSetSizeResponse = 0x08050040U;
constexpr std::uint32_t kFileFlushRequest = 0x08090000U;
constexpr std::uint32_t kFileFlushResponse = 0x08090040U;
constexpr std::uint32_t kCallingPidDescriptor = 0x20U;
constexpr std::uint32_t kMoveHandleDescriptor = 0x10U;

std::uint64_t ReadU64(std::span<const std::uint32_t> words,
                      std::size_t index) {
    return words[index] | (static_cast<std::uint64_t>(words[index + 1]) << 32U);
}

void WriteU64(std::span<std::uint32_t> words, std::size_t index,
              std::uint64_t value) {
    words[index] = static_cast<std::uint32_t>(value);
    words[index + 1] = static_cast<std::uint32_t>(value >> 32U);
}

std::optional<std::filesystem::path> DecodeRelativePath(
    GuestAddressSpace& memory, std::uint32_t type, std::uint32_t size,
    GuestAddress address) {
    if ((type != 3U && type != 4U) || size == 0 || size > 0x1000U ||
        (type == 4U && (size & 1U) != 0U)) {
        return std::nullopt;
    }
    const auto bytes = memory.ResolveRead(address, size);
    if (bytes.size() != size) {
        return std::nullopt;
    }
    std::filesystem::path path;
    if (type == 3U) {
        std::string value;
        for (const std::byte byte : bytes) {
            const char character = static_cast<char>(std::to_integer<unsigned char>(byte));
            if (character == 0) break;
            value.push_back(character);
        }
        path = value;
    } else {
        std::u16string value;
        for (std::size_t index = 0; index < bytes.size(); index += 2) {
            const char16_t character = static_cast<char16_t>(
                std::to_integer<unsigned char>(bytes[index]) |
                (std::to_integer<unsigned char>(bytes[index + 1]) << 8U));
            if (character == 0) break;
            value.push_back(character);
        }
        path = value;
    }
    while (path.has_root_directory()) path = path.relative_path();
    path = path.lexically_normal();
    if (path.empty() || path.has_root_name() || path.has_root_directory()) return std::nullopt;
    for (const auto& component : path) {
        if (component == "..") return std::nullopt;
    }
    return path;
}

std::optional<std::filesystem::path> ResolveConfinedPath(
    const std::filesystem::path& root,
    const std::filesystem::path& relative) {
    std::error_code error;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
    if (error) return std::nullopt;
    const auto candidate = std::filesystem::weakly_canonical(root / relative, error);
    if (error) return std::nullopt;
    auto rootPart = canonicalRoot.begin();
    auto candidatePart = candidate.begin();
    for (; rootPart != canonicalRoot.end(); ++rootPart, ++candidatePart) {
        if (candidatePart == candidate.end() || *rootPart != *candidatePart) {
            return std::nullopt;
        }
    }
    return candidate;
}

} // namespace

CtrFileSession::CtrFileSession(GuestAddressSpace& memory,
                               std::filesystem::path path,
                               std::uint64_t fileOffset,
                               std::uint64_t fileSize)
    : mMemory(memory), mPath(std::move(path)), mFileOffset(fileOffset),
      mFileSize(fileSize) {}

CtrFileSession::CtrFileSession(GuestAddressSpace& memory,
                               std::filesystem::path path,
                               std::uint32_t openMode)
    : mMemory(memory), mPath(std::move(path)), mOpenMode(openMode),
      mWritable((openMode & 2U) != 0U) {
    std::error_code error;
    mFileSize = std::filesystem::file_size(mPath, error);
}

CtrResult CtrFileSession::Dispatch(std::span<std::uint32_t> commandBuffer) {
    if (commandBuffer.empty()) {
        return CtrIpcRouter::UnhandledResult;
    }
    if (commandBuffer[0] == kFileGetSizeRequest && commandBuffer.size() >= 4) {
        if (mFileOffset == 0) {
            std::error_code error;
            const auto currentSize = std::filesystem::file_size(mPath, error);
            if (error) return CtrIpcRouter::UnhandledResult;
            mFileSize = currentSize;
        }
        commandBuffer[0] = kFileGetSizeResponse;
        commandBuffer[1] = 0;
        WriteU64(commandBuffer, 2, mFileSize);
        return 0;
    }
    if (commandBuffer[0] == kFileCloseRequest && commandBuffer.size() >= 2) {
        commandBuffer[0] = kFileCloseResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (commandBuffer[0] == kFileFlushRequest && commandBuffer.size() >= 2) {
        commandBuffer[0] = kFileFlushResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (commandBuffer[0] == kFileSetSizeRequest && commandBuffer.size() >= 3 &&
        mWritable) {
        const std::uint64_t size = ReadU64(commandBuffer, 1);
        std::error_code error;
        std::filesystem::resize_file(mPath, size, error);
        if (error) return CtrIpcRouter::UnhandledResult;
        mFileSize = size;
        commandBuffer[0] = kFileSetSizeResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (commandBuffer[0] == kFileWriteRequest && commandBuffer.size() >= 7 &&
        mWritable) {
        const std::uint64_t offset = ReadU64(commandBuffer, 1);
        const std::uint32_t length = commandBuffer[3];
        const std::uint32_t flags = commandBuffer[4];
        const std::uint32_t descriptor = commandBuffer[5];
        const GuestAddress source = commandBuffer[6];
        const auto bytes = mMemory.ResolveRead(source, length);
        if (((descriptor & 0xFU) != 0xAU && (descriptor & 0xFU) != 0xEU) ||
            (descriptor >> 4U) < length || bytes.size() != length ||
            offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
            length > std::numeric_limits<std::uint64_t>::max() - offset) {
            return CtrIpcRouter::UnhandledResult;
        }
        std::fstream stream(mPath, std::ios::binary | std::ios::in | std::ios::out);
        stream.seekp(static_cast<std::streamoff>(offset));
        if (!stream || (length != 0 && !stream.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(length)))) {
            return CtrIpcRouter::UnhandledResult;
        }
        if ((flags & 0xffU) != 0U) stream.flush();
        if (!stream) return CtrIpcRouter::UnhandledResult;
        mFileSize = std::max(mFileSize, offset + length);
        commandBuffer[0] = kFileWriteResponse;
        commandBuffer[1] = 0;
        commandBuffer[2] = length;
        commandBuffer[3] = descriptor;
        commandBuffer[4] = source;
        return 0;
    }
    if (commandBuffer[0] != kFileReadRequest || commandBuffer.size() < 6) {
        return CtrIpcRouter::UnhandledResult;
    }
    const std::uint64_t offset = ReadU64(commandBuffer, 1);
    const std::uint32_t length = commandBuffer[3];
    const std::uint32_t descriptor = commandBuffer[4];
    const GuestAddress target = commandBuffer[5];
    if ((descriptor & 0xFU) != 0xCU || (descriptor >> 4U) < length ||
        offset > mFileSize || length > mFileSize - offset) {
        return CtrIpcRouter::UnhandledResult;
    }
    auto destination = mMemory.ResolveWrite(target, length);
    if (destination.size() != length) {
        return CtrIpcRouter::UnhandledResult;
    }
    std::ifstream stream(mPath, std::ios::binary);
    stream.seekg(static_cast<std::streamoff>(mFileOffset + offset));
    if (!stream || (length != 0 && !stream.read(
            reinterpret_cast<char*>(destination.data()),
            static_cast<std::streamsize>(length)))) {
        return CtrIpcRouter::UnhandledResult;
    }
    commandBuffer[0] = kFileReadResponse;
    commandBuffer[1] = 0;
    commandBuffer[2] = length;
    commandBuffer[3] = descriptor;
    commandBuffer[4] = target;
    return 0;
}

CtrFsUserService::CtrFsUserService(GuestAddressSpace& memory,
                                   CtrIpcRouter& router,
                                   CtrFsProfile profile)
    : mMemory(memory), mRouter(router), mProfile(std::move(profile)) {}

CtrResult CtrFsUserService::Dispatch(
    std::span<std::uint32_t> commandBuffer) {
    if (commandBuffer.empty()) {
        return CtrIpcRouter::UnhandledResult;
    }
    if (commandBuffer[0] == kInitializeRequest && commandBuffer.size() >= 2 &&
        commandBuffer[1] == kCallingPidDescriptor) {
        commandBuffer[0] = kInitializeResponse;
        commandBuffer[1] = 0;
        return 0;
    }
    if (commandBuffer[0] == kOpenArchiveRequest && commandBuffer.size() >= 6) {
        const std::uint32_t size = commandBuffer[3];
        const auto path = mMemory.ResolveRead(commandBuffer[5], size);
        if (commandBuffer[1] != 4U || commandBuffer[2] != 1U || size != 1U ||
            commandBuffer[4] != ((size << 14U) | 2U) || path.size() != 1 ||
            path[0] != std::byte{0} || mProfile.SaveDataDirectory.empty()) {
            return CtrIpcRouter::UnhandledResult;
        }
        std::error_code error;
        std::filesystem::create_directories(mProfile.SaveDataDirectory, error);
        if (error) return CtrIpcRouter::UnhandledResult;
        const std::uint64_t handle = mNextArchiveHandle++;
        mArchives.emplace(handle, mProfile.SaveDataDirectory.lexically_normal());
        commandBuffer[0] = kOpenArchiveResponse;
        commandBuffer[1] = 0;
        WriteU64(commandBuffer, 2, handle);
        return 0;
    }
    if (commandBuffer[0] == kCloseArchiveRequest && commandBuffer.size() >= 3) {
        const std::uint64_t handle = ReadU64(commandBuffer, 1);
        commandBuffer[0] = kCloseArchiveResponse;
        commandBuffer[1] = mArchives.erase(handle) != 0 ? 0 : kInvalidArchiveHandle;
        return 0;
    }
    if (commandBuffer[0] == kControlArchiveRequest && commandBuffer.size() >= 10) {
        const std::uint64_t handle = ReadU64(commandBuffer, 1);
        const std::uint32_t inputSize = commandBuffer[4];
        const std::uint32_t outputSize = commandBuffer[5];
        const auto input = mMemory.ResolveRead(commandBuffer[7], inputSize);
        auto output = mMemory.ResolveWrite(commandBuffer[9], outputSize);
        if (commandBuffer[6] != ((inputSize << 4U) | 0xAU) ||
            commandBuffer[8] != ((outputSize << 4U) | 0xCU) ||
            input.size() != inputSize || output.size() != outputSize) {
            return CtrIpcRouter::UnhandledResult;
        }
        const std::uint32_t result = mArchives.contains(handle)
                                         ? 0
                                         : kInvalidArchiveHandle;
        if (result == 0) std::ranges::fill(output, std::byte{0});
        commandBuffer[0] = kControlArchiveResponse;
        commandBuffer[1] = result;
        return 0;
    }
    if (commandBuffer[0] == kOpenFileRequest && commandBuffer.size() >= 10) {
        const std::uint64_t archiveHandle = ReadU64(commandBuffer, 2);
        const std::uint32_t pathType = commandBuffer[4];
        const std::uint32_t pathSize = commandBuffer[5];
        const std::uint32_t openMode = commandBuffer[6];
        const auto archive = mArchives.find(archiveHandle);
        const auto relative = DecodeRelativePath(mMemory, pathType, pathSize,
                                                 commandBuffer[9]);
        if (commandBuffer[8] != ((pathSize << 14U) | 2U) ||
            archive == mArchives.end() || !relative) {
            return CtrIpcRouter::UnhandledResult;
        }
        const auto confined = ResolveConfinedPath(archive->second, *relative);
        if (!confined) return CtrIpcRouter::UnhandledResult;
        const auto& filePath = *confined;
        std::error_code error;
        bool exists = std::filesystem::is_regular_file(filePath, error);
        if (!exists && error == std::errc::no_such_file_or_directory) {
            error.clear();
        }
        if (!exists && !error && (openMode & (2U | 4U)) != 0U) {
            std::filesystem::create_directories(filePath.parent_path(), error);
            std::ofstream create(filePath, std::ios::binary | std::ios::app);
            exists = !error && create.good();
        }
        std::uint32_t result = kFileNotFound;
        CtrHandle fileHandle = 0;
        if (exists && !error) {
            auto file = std::make_shared<CtrFileSession>(mMemory, filePath, openMode);
            if (mRouter.OpenSession("file:savedata", file, fileHandle) == 0) result = 0;
        }
        commandBuffer[0] = kOpenFileResponse;
        commandBuffer[1] = result;
        commandBuffer[2] = kMoveHandleDescriptor;
        commandBuffer[3] = fileHandle;
        return 0;
    }
    if (commandBuffer[0] == kCreateFileRequest && commandBuffer.size() >= 11) {
        const std::uint64_t archiveHandle = ReadU64(commandBuffer, 2);
        const std::uint32_t pathType = commandBuffer[4];
        const std::uint32_t pathSize = commandBuffer[5];
        const std::uint64_t fileSize = ReadU64(commandBuffer, 7);
        const auto archive = mArchives.find(archiveHandle);
        const auto relative = DecodeRelativePath(mMemory, pathType, pathSize,
                                                 commandBuffer[10]);
        std::uint32_t result = kInvalidArchiveHandle;
        if (commandBuffer[9] == ((pathSize << 14U) | 2U) &&
            archive != mArchives.end() && relative) {
            const auto confined = ResolveConfinedPath(archive->second, *relative);
            if (!confined) return CtrIpcRouter::UnhandledResult;
            const auto& filePath = *confined;
            std::error_code error;
            if (std::filesystem::exists(filePath, error)) {
                result = kFileAlreadyExists;
            } else if (!error) {
                std::filesystem::create_directories(filePath.parent_path(), error);
                std::ofstream create(filePath, std::ios::binary | std::ios::trunc);
                create.close();
                if (!error) std::filesystem::resize_file(filePath, fileSize, error);
                if (!error) result = 0;
            }
        }
        commandBuffer[0] = kCreateFileResponse;
        commandBuffer[1] = result;
        return 0;
    }
    if (commandBuffer[0] != kOpenFileDirectlyRequest ||
        commandBuffer.size() < 13 || mProfile.RomFsImagePath.empty() ||
        mProfile.RomFsImageSize == 0) {
        return CtrIpcRouter::UnhandledResult;
    }
    const bool opensRomFs =
        commandBuffer[2] == 3U && commandBuffer[3] == 1U &&
        commandBuffer[4] == 1U && commandBuffer[5] == 2U &&
        commandBuffer[6] == 12U && commandBuffer[7] == 1U &&
        (commandBuffer[9] & 0xFU) == 2U &&
        (commandBuffer[9] >> 14U) == commandBuffer[4] &&
        (commandBuffer[11] & 0xFU) == 2U &&
        (commandBuffer[11] >> 14U) == commandBuffer[6];
    const auto path = mMemory.ResolveRead(commandBuffer[12], 12);
    const auto archivePath = mMemory.ResolveRead(commandBuffer[10], 1);
    std::error_code fileError;
    const auto imageSize = std::filesystem::file_size(
        mProfile.RomFsImagePath, fileError);
    if (!opensRomFs || path.size() != 12 ||
        archivePath.size() != 1 || archivePath[0] != std::byte{0} ||
        std::ranges::any_of(path, [](std::byte value) {
            return value != std::byte{0};
        }) || fileError || mProfile.RomFsImageOffset > imageSize ||
        mProfile.RomFsImageSize > imageSize - mProfile.RomFsImageOffset) {
        return CtrIpcRouter::UnhandledResult;
    }
    auto file = std::make_shared<CtrFileSession>(
        mMemory, mProfile.RomFsImagePath, mProfile.RomFsImageOffset,
        mProfile.RomFsImageSize);
    CtrHandle handle = 0;
    if (mRouter.OpenSession("file:romfs", file, handle) < 0) {
        return CtrIpcRouter::UnhandledResult;
    }
    commandBuffer[0] = kOpenFileDirectlyResponse;
    commandBuffer[1] = 0;
    commandBuffer[2] = kMoveHandleDescriptor;
    commandBuffer[3] = handle;
    return 0;
}

} // namespace Oot3dSourceRuntime
