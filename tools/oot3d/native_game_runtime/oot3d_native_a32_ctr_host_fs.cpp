// CTR host services: the fs:USER session and RomFS files.
#include "oot3d_native_a32_ctr_host_internal.h"

namespace Oot3dNativeGame {

// SendSyncRequest on a client session of a RomFS file or archive session.
NativeA32HostResult NativeA32CtrHostServices::HandleRomFsRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext& context, const std::optional<std::string>& sessionType,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
    const auto* fileHandle = LookupHandle(state.r[0]);
    if (fileHandle == nullptr || fileHandle->Object->FilePath.empty()) {
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Wait};
    }
    if (*sessionType == "client_session:file:savedata") {
        if (fileHandle->Object->FileBackend) {
            fileHandle->Object->FileSize =
                fileHandle->Object->FileBackend->Size();
        } else {
            std::error_code sizeError;
            const uint64_t fileSize = std::filesystem::file_size(
                fileHandle->Object->FilePath, sizeError);
            if (!sizeError) {
                fileHandle->Object->FileSize = fileSize;
            }
        }
    }
    if (event.Detail == kFileGetSizeRequest &&
        memory.Write32(commandBuffer, kFileGetSizeResponse) &&
        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
        memory.Write64(commandBuffer + 8U, fileHandle->Object->FileSize,
                       nullptr)) {
        state.r[0] = kResultSuccess;
        event.Name += ":GetSize";
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    if (event.Detail == kFileReadRequest) {
        uint64_t offset = 0;
        uint32_t length = 0;
        uint32_t descriptor = 0;
        uint32_t targetAddress = 0;
        uint32_t faultAddress = 0;
        if (memory.Read64(commandBuffer + 4U, &offset, &faultAddress) &&
            memory.Read32(commandBuffer + 12U, &length) &&
            memory.Read32(commandBuffer + 16U, &descriptor) &&
            memory.Read32(commandBuffer + 20U, &targetAddress) &&
            (descriptor & 0xFU) == 0xCU &&
            (descriptor >> 4U) >= length &&
            offset <= fileHandle->Object->FileSize &&
            length <= fileHandle->Object->FileSize - offset) {
            std::vector<uint8_t> bytes(length);
            bool readSucceeded = false;
            if (fileHandle->Object->FileBackend) {
                const bool isRomFs =
                    *sessionType == "client_session:file:romfs";
                const auto readStart =
                    mConfig.ProfileRuntime && isRomFs
                        ? RuntimeProfileClock::now()
                        : RuntimeProfileClock::time_point{};
                if (mConfig.ProfileRuntime && isRomFs) {
                    ++mRuntimeProfile.RomFsReadCalls;
                }
                uint32_t returnedSize = 0U;
                if (fileHandle->Object->FileOffset <=
                    std::numeric_limits<uint64_t>::max() - offset) {
                    readSucceeded =
                        fileHandle->Object->FileBackend->Read(
                            fileHandle->Object->FileOffset + offset,
                            bytes, &returnedSize) &&
                        returnedSize == length;
                }
                if (mConfig.ProfileRuntime && isRomFs) {
                    mRuntimeProfile.RomFsReadBytes +=
                        readSucceeded ? length : 0U;
                    mRuntimeProfile.RomFsReadNanoseconds +=
                        RuntimeProfileElapsedNanoseconds(readStart);
                }
            } else if (*sessionType == "client_session:file:romfs") {
                if (!fileHandle->Object->RomFsBacking) {
                    fileHandle->Object->RomFsBacking =
                        std::make_shared<RomFsBackingFile>(
                            fileHandle->Object->FilePath);
                }
                const auto readStart =
                    mConfig.ProfileRuntime
                        ? RuntimeProfileClock::now()
                        : RuntimeProfileClock::time_point{};
                bool openedStream = false;
                if (mConfig.ProfileRuntime) {
                    ++mRuntimeProfile.RomFsReadCalls;
                }
                if (fileHandle->Object->FileOffset <=
                    std::numeric_limits<uint64_t>::max() - offset) {
                    readSucceeded =
                        fileHandle->Object->RomFsBacking->Read(
                            fileHandle->Object->FileOffset + offset,
                            bytes, &openedStream);
                }
                if (mConfig.ProfileRuntime) {
                    mRuntimeProfile.RomFsStreamOpenCalls +=
                        openedStream ? 1U : 0U;
                    mRuntimeProfile.RomFsReadBytes +=
                        readSucceeded ? length : 0U;
                    mRuntimeProfile.RomFsReadNanoseconds +=
                        RuntimeProfileElapsedNanoseconds(readStart);
                }
            } else {
                std::ifstream stream(fileHandle->Object->FilePath,
                                     std::ios::binary);
                stream.seekg(
                    static_cast<std::streamoff>(
                        fileHandle->Object->FileOffset + offset),
                    std::ios::beg);
                readSucceeded =
                    stream &&
                    (bytes.empty() ||
                     static_cast<bool>(stream.read(
                         reinterpret_cast<char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()))));
            }
            if (readSucceeded &&
                memory.WriteBytes(targetAddress, bytes) &&
                memory.Write32(commandBuffer, kFileReadResponse) &&
                memory.Write32(commandBuffer + 4U, kResultSuccess) &&
                memory.Write32(commandBuffer + 8U, length) &&
                memory.Write32(commandBuffer + 12U, descriptor) &&
                memory.Write32(commandBuffer + 16U, targetAddress)) {
                state.r[0] = kResultSuccess;
                std::ostringstream readDetail;
                readDetail << ":Read:offset=0x" << std::hex << offset
                           << ":length=0x" << length << ":target=0x"
                           << targetAddress;
                event.Name += readDetail.str();
                event.Handled = true;
                event.Detail = length;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
        }
    }
    if (event.Detail == kFileWriteRequest &&
        (fileHandle->Object->FileOpenMode & 2U) != 0U) {
        uint64_t offset = 0;
        uint32_t length = 0;
        uint32_t flags = 0;
        uint32_t descriptor = 0;
        uint32_t sourceAddress = 0;
        uint32_t faultAddress = 0;
        const bool validRange =
            memory.Read64(commandBuffer + 4U, &offset, &faultAddress) &&
            memory.Read32(commandBuffer + 12U, &length) &&
            memory.Read32(commandBuffer + 16U, &flags) &&
            memory.Read32(commandBuffer + 20U, &descriptor) &&
            memory.Read32(commandBuffer + 24U, &sourceAddress) &&
            ((descriptor & 0xFU) == 0xAU ||
             (descriptor & 0xFU) == 0xEU) &&
            (descriptor >> 4U) >= length &&
            offset <= static_cast<uint64_t>(
                          std::numeric_limits<std::int64_t>::max()) &&
            offset <= std::numeric_limits<uint64_t>::max() - length;
        std::vector<uint8_t> bytes(length);
        if (validRange &&
            (length == 0U || memory.ReadBytes(sourceAddress, bytes))) {
            bool writeSucceeded = false;
            if (fileHandle->Object->FileBackend) {
                uint32_t writtenSize = 0U;
                writeSucceeded = fileHandle->Object->FileBackend->Write(
                                     offset, bytes, &writtenSize) &&
                                 writtenSize == length;
                if (writeSucceeded) {
                    fileHandle->Object->FileSize =
                        fileHandle->Object->FileBackend->Size();
                }
            } else {
                std::fstream stream(fileHandle->Object->FilePath,
                                    std::ios::binary | std::ios::in |
                                        std::ios::out);
                stream.seekp(static_cast<std::streamoff>(offset),
                             std::ios::beg);
                writeSucceeded =
                    stream &&
                    (bytes.empty() ||
                     stream.write(
                         reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()))) &&
                    ((flags & 0xFFU) == 0U ||
                     static_cast<bool>(stream.flush()));
                stream.close();
                std::error_code sizeError;
                fileHandle->Object->FileSize =
                    std::filesystem::file_size(
                        fileHandle->Object->FilePath, sizeError);
                writeSucceeded = writeSucceeded && !sizeError;
            }
            if (writeSucceeded) {
                if (memory.Write32(commandBuffer, kFileWriteResponse) &&
                    memory.Write32(commandBuffer + 4U,
                                   kResultSuccess) &&
                    memory.Write32(commandBuffer + 8U, length) &&
                    memory.Write32(commandBuffer + 12U, descriptor) &&
                    memory.Write32(commandBuffer + 16U,
                                   sourceAddress)) {
                    state.r[0] = kResultSuccess;
                    std::ostringstream writeDetail;
                    writeDetail << ":Write:offset=0x" << std::hex
                                << offset << ":length=0x" << length
                                << ":flags=0x" << flags;
                    event.Name += writeDetail.str();
                    event.Handled = true;
                    event.Detail = length;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
            }
        }
    }
    if (event.Detail == kFileSetSizeRequest &&
        (fileHandle->Object->FileOpenMode & 2U) != 0U) {
        uint64_t size = 0;
        uint32_t faultAddress = 0;
        const bool readable =
            memory.Read64(commandBuffer + 4U, &size, &faultAddress) &&
            size <= static_cast<uint64_t>(
                        std::numeric_limits<std::int64_t>::max());
        if (readable) {
            bool resized = false;
            if (fileHandle->Object->FileBackend) {
                resized = fileHandle->Object->FileBackend->Resize(size);
            } else {
                std::error_code resizeError;
                std::filesystem::resize_file(
                    fileHandle->Object->FilePath, size, resizeError);
                resized = !resizeError;
            }
            if (resized &&
                memory.Write32(commandBuffer, kFileSetSizeResponse) &&
                memory.Write32(commandBuffer + 4U, kResultSuccess)) {
                fileHandle->Object->FileSize = size;
                state.r[0] = kResultSuccess;
                event.Name += ":SetSize:" + std::to_string(size);
                event.Handled = true;
                event.Detail = static_cast<uint32_t>(size);
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
        }
    }
    // Set/GetPriority: the session's I/O priority. Reads complete
    // synchronously here, so it is only kept and reported back.
    if (event.Detail == kFileSetPriorityRequest) {
        uint32_t priority = 0;
        if (memory.Read32(commandBuffer + 4U, &priority) &&
            memory.Write32(commandBuffer, kFileSetPriorityResponse) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess)) {
            fileHandle->Object->FilePriority = priority;
            state.r[0] = kResultSuccess;
            event.Name += ":SetPriority:" + std::to_string(priority);
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }
    }
    if (event.Detail == kFileGetPriorityRequest &&
        memory.Write32(commandBuffer, kFileGetPriorityResponse) &&
        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
        memory.Write32(commandBuffer + 8U, fileHandle->Object->FilePriority)) {
        state.r[0] = kResultSuccess;
        event.Name += ":GetPriority";
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    if (event.Detail == kFileOpenLinkFileRequest) {
        // Another session on the same file. A file session here holds no
        // state of its own (reads carry their offset), so the link shares
        // the file and its backing stream. The fields are copied before
        // CreateHandle, which may reallocate the handle table.
        auto link = std::make_shared<KernelObject>();
        link->Type = fileHandle->Object->Type;
        link->FilePath = fileHandle->Object->FilePath;
        link->FileOffset = fileHandle->Object->FileOffset;
        link->FileSize = fileHandle->Object->FileSize;
        link->FileOpenMode = fileHandle->Object->FileOpenMode;
        link->FilePriority = fileHandle->Object->FilePriority;
        link->FileBackend = fileHandle->Object->FileBackend;
        link->RomFsBacking = fileHandle->Object->RomFsBacking;
        const uint32_t handle = CreateHandle(std::move(link));
        if (handle != 0U &&
            memory.Write32(commandBuffer, kFileOpenLinkFileResponse) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess) &&
            memory.Write32(commandBuffer + 8U, kMoveHandleDescriptor) &&
            memory.Write32(commandBuffer + 12U, handle)) {
            state.r[0] = kResultSuccess;
            event.Name += ":OpenLinkFile";
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }
    }
    if (event.Detail == kFileFlushRequest &&
        memory.Write32(commandBuffer, kFileFlushResponse) &&
        memory.Write32(commandBuffer + 4U, kResultSuccess)) {
        state.r[0] = kResultSuccess;
        event.Name += ":Flush";
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    if (event.Detail == kFileCloseRequest &&
        memory.Write32(commandBuffer, kFileCloseResponse) &&
        memory.Write32(commandBuffer + 4U, kResultSuccess)) {
        state.r[0] = kResultSuccess;
        event.Name += ":Close";
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    mSvcEvents.push_back(std::move(event));
    return {NativeA32HostAction::Wait};
}

// SendSyncRequest on a client session of fs:USER.
NativeA32HostResult NativeA32CtrHostServices::HandleFsRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext& context, const std::optional<std::string>& sessionType,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
    if (event.Detail == kFsInitializeRequest) {
        uint32_t descriptor = 0;
        if (memory.Read32(commandBuffer + 4U, &descriptor) &&
            descriptor == kCallingPidDescriptor &&
            memory.Write32(commandBuffer, kFsInitializeResponse) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess)) {
            state.r[0] = kResultSuccess;
            event.Name += ":Initialize";
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }
    }
    if (event.Detail == kFsInitializeWithSdkVersionRequest) {
        uint32_t sdkVersion = 0;
        uint32_t descriptor = 0;
        if (memory.Read32(commandBuffer + 4U, &sdkVersion) &&
            memory.Read32(commandBuffer + 8U, &descriptor) &&
            descriptor == kCallingPidDescriptor &&
            memory.Write32(commandBuffer,
                           kFsInitializeWithSdkVersionResponse) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess)) {
            state.r[0] = kResultSuccess;
            event.Name += ":InitializeWithSdkVersion:0x";
            AppendHex8(event.Name, sdkVersion);
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }
    }
    if (event.Detail == kFsSetPriorityRequest) {
        uint32_t priority = 0;
        if (memory.Read32(commandBuffer + 4U, &priority) &&
            memory.Write32(commandBuffer, kFsSetPriorityResponse) &&
            memory.Write32(commandBuffer + 4U, kResultSuccess)) {
            mFsPriority = priority;
            state.r[0] = kResultSuccess;
            event.Name += ":SetPriority:" + std::to_string(priority);
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }
    }
    if (event.Detail == kFsGetPriorityRequest &&
        memory.Write32(commandBuffer, kFsGetPriorityResponse) &&
        memory.Write32(commandBuffer + 4U, kResultSuccess) &&
        memory.Write32(commandBuffer + 8U, mFsPriority)) {
        state.r[0] = kResultSuccess;
        event.Name += ":GetPriority";
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    }
    if (event.Detail == kFsOpenFileDirectlyRequest) {
        std::array<uint32_t, 12> words{};
        bool readable = true;
        for (size_t index = 0; index < words.size(); ++index) {
            readable =
                readable &&
                memory.Read32(commandBuffer + 4U +
                                  static_cast<uint32_t>(index * 4U),
                              &words[index]);
        }
        if (readable) {
            event.Name += ":OpenFileDirectly:archive=0x";
            std::ostringstream request;
            request
                << std::hex << std::setw(8) << std::setfill('0')
                << words[1] << ":archive_path="
                << DescribeLowPath(memory, words[2], words[3], words[9])
                << "(type=" << words[2] << ",size=" << words[3]
                << ",desc=0x" << words[8] << ")"
                << ":file_path="
                << DescribeLowPath(memory, words[4], words[5],
                                   words[11])
                << "(type=" << words[4] << ",size=" << words[5]
                << ",desc=0x" << words[10] << ")"
                << ":mode=0x" << words[6] << ":attrs=0x" << words[7];
            event.Name += request.str();
            uint32_t pathType = 0;
            uint32_t pathReserved0 = 0;
            uint32_t pathReserved1 = 0;
            const bool requestsRomFs =
                words[1] == 3U && words[2] == 1U && words[3] == 1U &&
                words[4] == 2U && words[5] == 12U && words[6] == 1U &&
                (words[8] & 0xFU) == 2U &&
                (words[8] >> 14U) == words[3] &&
                (words[10] & 0xFU) == 2U &&
                (words[10] >> 14U) == words[5] &&
                memory.Read32(words[11], &pathType) &&
                memory.Read32(words[11] + 4U, &pathReserved0) &&
                memory.Read32(words[11] + 8U, &pathReserved1) &&
                pathType == 0U && pathReserved0 == 0U &&
                pathReserved1 == 0U;
            const bool opensRomFs = requestsRomFs &&
                                    mConfig.RomFsImageSize != 0U &&
                                    (mConfig.Filesystem != nullptr ||
                                     !mConfig.RomFsImagePath.empty());
            if (opensRomFs) {
                auto file = std::make_shared<KernelObject>();
                file->Type = "client_session:file:romfs";
                file->FileOffset = mConfig.RomFsImageOffset;
                file->FileSize = mConfig.RomFsImageSize;
                bool opened = false;
                if (mConfig.Filesystem != nullptr) {
                    file->FilePath = mConfig.RomFsContentPath;
                    opened =
                        mConfig.Filesystem->Open(
                            NativeA32CtrFilesystemRoot::Content,
                            mConfig.RomFsContentPath,
                            NativeA32CtrFileOpenRead,
                            &file->FileBackend) &&
                        file->FileBackend != nullptr &&
                        file->FileOffset <= file->FileBackend->Size() &&
                        file->FileSize <= file->FileBackend->Size() -
                                              file->FileOffset;
                } else {
                    file->FilePath = mConfig.RomFsImagePath;
                    file->RomFsBacking =
                        std::make_shared<RomFsBackingFile>(
                            file->FilePath);
                    opened = true;
                }
                const uint32_t handle =
                    opened ? CreateHandle(file) : 0U;
                if (handle != 0U &&
                    memory.Write32(commandBuffer,
                                   kFsOpenFileDirectlyResponse) &&
                    memory.Write32(commandBuffer + 4U,
                                   kResultSuccess) &&
                    memory.Write32(commandBuffer + 8U,
                                   kMoveHandleDescriptor) &&
                    memory.Write32(commandBuffer + 12U, handle)) {
                    state.r[0] = kResultSuccess;
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
                if (handle != 0U) {
                    CloseHandle(handle);
                }
            }
        }
    }
    if (event.Detail == kFsDeleteFileRequest) {
        uint64_t archiveHandle = 0;
        uint32_t pathType = 0, pathSize = 0, descriptor = 0, pathAddress = 0;
        uint32_t result = kFsInvalidPath;
        const bool readable =
            memory.Read64(commandBuffer + 8U, &archiveHandle, nullptr) &&
            memory.Read32(commandBuffer + 16U, &pathType) &&
            memory.Read32(commandBuffer + 20U, &pathSize) &&
            memory.Read32(commandBuffer + 24U, &descriptor) &&
            memory.Read32(commandBuffer + 28U, &pathAddress) &&
            (descriptor & 0x3FFFU) == 2U && (descriptor >> 14U) == pathSize &&
            memory.IsWritable(commandBuffer, 8U);
        if (readable) {
            const auto archive = std::find_if(mArchives.begin(), mArchives.end(),
                [archiveHandle](const ArchiveRecord& record) {
                    return record.Handle == archiveHandle;
                });
            const auto relative = DecodeArchiveRelativePath(memory, pathType, pathSize, pathAddress);
            if (archive == mArchives.end()) {
                result = kFsInvalidArchiveHandle;
            } else if (relative) {
                bool removed = false;
                bool success = false;
                if (archive->Id == kFsSaveDataArchiveId && mConfig.Filesystem) {
                    success = mConfig.Filesystem->RemoveFile(relative->generic_string(), &removed);
                } else if (archive->Id == kFsSaveDataArchiveId) {
                    std::error_code error;
                    std::error_code rootError;
                    const auto root = std::filesystem::weakly_canonical(archive->Root, rootError);
                    const auto target = root / *relative;
                    const auto parent = std::filesystem::weakly_canonical(target.parent_path(), error);
                    const auto within = parent.lexically_relative(root);
                    bool contained = !rootError && !error && !within.empty() && !within.is_absolute();
                    for (const auto& part : within) contained = contained && part != "..";
                    if (contained) {
                        const auto status = std::filesystem::symlink_status(target, error);
                        if (status.type() == std::filesystem::file_type::not_found &&
                            (!error || error == std::errc::no_such_file_or_directory)) {
                            success = true;
                        } else if (!error && std::filesystem::is_regular_file(status)) {
                            removed = std::filesystem::remove(target, error);
                            success = !error;
                        }
                    }
                }
                result = success ? (removed ? kResultSuccess : kFsFileNotFound) : kFsCommandNotAllowed;
            }
        }
        if (memory.Write32(commandBuffer, kFsDeleteFileResponse) &&
            memory.Write32(commandBuffer + 4U, result)) {
            state.r[0] = kResultSuccess;
            event.Name += ":DeleteFile:" + DescribeLowPath(memory, pathType, pathSize, pathAddress);
            event.Detail = result;
            event.Handled = true;
            mSvcEvents.push_back(std::move(event));
            return {NativeA32HostAction::Resume};
        }
    }
    if (event.Detail == kFsCreateFileRequest) {
        uint64_t archiveHandle = 0;
        uint32_t pathType = 0;
        uint32_t pathSize = 0;
        uint32_t attributes = 0;
        uint64_t fileSize = 0;
        uint32_t descriptor = 0;
        uint32_t pathAddress = 0;
        uint32_t faultAddress = 0;
        const bool readable =
            memory.Read64(commandBuffer + 8U, &archiveHandle,
                          &faultAddress) &&
            memory.Read32(commandBuffer + 16U, &pathType) &&
            memory.Read32(commandBuffer + 20U, &pathSize) &&
            memory.Read32(commandBuffer + 24U, &attributes) &&
            memory.Read64(commandBuffer + 28U, &fileSize,
                          &faultAddress) &&
            memory.Read32(commandBuffer + 36U, &descriptor) &&
            memory.Read32(commandBuffer + 40U, &pathAddress) &&
            (descriptor & 0x3FFU) == 2U &&
            (descriptor >> 14U) == pathSize;
        if (readable) {
            const auto archive = std::find_if(
                mArchives.begin(), mArchives.end(),
                [archiveHandle](const ArchiveRecord& record) {
                    return record.Handle == archiveHandle;
                });
            const auto relativePath = DecodeArchiveRelativePath(
                memory, pathType, pathSize, pathAddress);
            uint32_t result = kFsInvalidArchiveHandle;
            if (archive != mArchives.end() &&
                relativePath.has_value()) {
                if (mConfig.Filesystem != nullptr) {
                    const std::string logicalPath =
                        relativePath->generic_string();
                    std::shared_ptr<NativeA32CtrFile> existing;
                    if (mConfig.Filesystem->Open(
                            NativeA32CtrFilesystemRoot::Save,
                            logicalPath, NativeA32CtrFileOpenRead,
                            &existing)) {
                        result = kFsFileAlreadyExists;
                    } else if (fileSize <=
                               static_cast<uint64_t>(
                                   std::numeric_limits<
                                       std::int64_t>::max())) {
                        std::shared_ptr<NativeA32CtrFile> created;
                        if (mConfig.Filesystem->Open(
                                NativeA32CtrFilesystemRoot::Save,
                                logicalPath,
                                NativeA32CtrFileOpenRead |
                                    NativeA32CtrFileOpenWrite |
                                    NativeA32CtrFileOpenCreate |
                                    NativeA32CtrFileOpenTruncate,
                                &created) &&
                            created != nullptr &&
                            created->Resize(fileSize)) {
                            result = kResultSuccess;
                        }
                    }
                } else {
                    const auto filePath = archive->Root / *relativePath;
                    std::error_code fileError;
                    if (std::filesystem::exists(filePath, fileError)) {
                        result = kFsFileAlreadyExists;
                    } else if (!fileError &&
                               fileSize <=
                                   static_cast<uint64_t>(
                                       std::numeric_limits<
                                           std::int64_t>::max())) {
                        std::ofstream create(filePath,
                                             std::ios::binary |
                                                 std::ios::trunc);
                        if (create.good()) {
                            create.close();
                            std::filesystem::resize_file(
                                filePath, fileSize, fileError);
                            if (!fileError) {
                                result = kResultSuccess;
                            }
                        }
                    }
                }
            }
            if (memory.Write32(commandBuffer, kFsCreateFileResponse) &&
                memory.Write32(commandBuffer + 4U, result)) {
                state.r[0] = kResultSuccess;
                event.Name += ":CreateFile:" +
                              DescribeLowPath(memory, pathType,
                                              pathSize, pathAddress) +
                              ":size=" + std::to_string(fileSize) +
                              ":attrs=" + std::to_string(attributes);
                event.Detail = result;
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
        }
    }
    if (event.Detail == kFsOpenFileRequest) {
        uint32_t transaction = 0;
        uint64_t archiveHandle = 0;
        uint32_t pathType = 0;
        uint32_t pathSize = 0;
        uint32_t openMode = 0;
        uint32_t attributes = 0;
        uint32_t descriptor = 0;
        uint32_t pathAddress = 0;
        uint32_t faultAddress = 0;
        const bool readable =
            memory.Read32(commandBuffer + 4U, &transaction) &&
            memory.Read64(commandBuffer + 8U, &archiveHandle,
                          &faultAddress) &&
            memory.Read32(commandBuffer + 16U, &pathType) &&
            memory.Read32(commandBuffer + 20U, &pathSize) &&
            memory.Read32(commandBuffer + 24U, &openMode) &&
            memory.Read32(commandBuffer + 28U, &attributes) &&
            memory.Read32(commandBuffer + 32U, &descriptor) &&
            memory.Read32(commandBuffer + 36U, &pathAddress) &&
            (descriptor & 0x3FFU) == 2U &&
            (descriptor >> 14U) == pathSize;
        if (readable) {
            const auto archive = std::find_if(
                mArchives.begin(), mArchives.end(),
                [archiveHandle](const ArchiveRecord& record) {
                    return record.Handle == archiveHandle;
                });
            const auto relativePath = DecodeArchiveRelativePath(
                memory, pathType, pathSize, pathAddress);
            uint32_t result = kFsInvalidArchiveHandle;
            uint32_t fileSession = 0;
            if (archive != mArchives.end() &&
                relativePath.has_value()) {
                const bool establishWritableSaveFile =
                    archive->Id == kFsSaveDataArchiveId &&
                    (openMode & 2U) != 0U;
                if (mConfig.Filesystem != nullptr) {
                    uint32_t backendFlags = 0U;
                    backendFlags |= (openMode & 1U) != 0U
                                        ? NativeA32CtrFileOpenRead
                                        : 0U;
                    backendFlags |= (openMode & 2U) != 0U
                                        ? NativeA32CtrFileOpenWrite
                                        : 0U;
                    backendFlags |= ((openMode & 4U) != 0U ||
                                     establishWritableSaveFile)
                                        ? NativeA32CtrFileOpenCreate
                                        : 0U;
                    auto file = std::make_shared<KernelObject>();
                    file->Type = "client_session:file:savedata";
                    file->FilePath = *relativePath;
                    file->FileOffset = 0;
                    file->FileOpenMode = openMode;
                    if (mConfig.Filesystem->Open(
                            NativeA32CtrFilesystemRoot::Save,
                            relativePath->generic_string(),
                            backendFlags, &file->FileBackend) &&
                        file->FileBackend != nullptr) {
                        file->FileSize = file->FileBackend->Size();
                        fileSession = CreateHandle(file);
                        result = kResultSuccess;
                    }
                } else {
                    const auto filePath = archive->Root / *relativePath;
                    std::error_code fileError;
                    bool exists = std::filesystem::is_regular_file(
                        filePath, fileError);
                    if (!exists &&
                        fileError ==
                            std::errc::no_such_file_or_directory) {
                        fileError.clear();
                    }
                    if (!exists && !fileError &&
                        ((openMode & 4U) != 0U ||
                         establishWritableSaveFile)) {
                        std::ofstream create(
                            filePath, std::ios::binary | std::ios::app);
                        exists = create.good();
                    }
                    if (exists && !fileError) {
                        auto file = std::make_shared<KernelObject>();
                        file->Type = "client_session:file:savedata";
                        file->FilePath = filePath;
                        file->FileOffset = 0;
                        file->FileSize = std::filesystem::file_size(
                            filePath, fileError);
                        file->FileOpenMode = openMode;
                        if (!fileError) {
                            fileSession = CreateHandle(file);
                            result = kResultSuccess;
                        }
                    }
                }
                if (fileSession == 0U &&
                    result == kFsInvalidArchiveHandle) {
                    result = kFsFileNotFound;
                }
            }
            if (memory.Write32(commandBuffer, kFsOpenFileResponse) &&
                memory.Write32(commandBuffer + 4U, result) &&
                memory.Write32(commandBuffer + 8U,
                               kMoveHandleDescriptor) &&
                memory.Write32(commandBuffer + 12U, fileSession)) {
                state.r[0] = kResultSuccess;
                event.Name += ":OpenFile:" +
                              DescribeLowPath(memory, pathType,
                                              pathSize, pathAddress);
                std::ostringstream openDetail;
                openDetail << ":mode=0x" << std::hex << openMode
                           << ":attrs=0x" << attributes;
                event.Name += openDetail.str();
                event.Detail = result;
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
            if (fileSession != 0U) {
                CloseHandle(fileSession);
            }
        }
    }
    if (event.Detail == kFsOpenArchiveRequest) {
        uint32_t archiveId = 0;
        uint32_t pathType = 0;
        uint32_t pathSize = 0;
        uint32_t descriptor = 0;
        uint32_t pathAddress = 0;
        const bool validStaticBuffer =
            memory.Read32(commandBuffer + 4U, &archiveId) &&
            memory.Read32(commandBuffer + 8U, &pathType) &&
            memory.Read32(commandBuffer + 12U, &pathSize) &&
            memory.Read32(commandBuffer + 16U, &descriptor) &&
            memory.Read32(commandBuffer + 20U, &pathAddress) &&
            (descriptor & 0x3FFU) == 2U &&
            (descriptor >> 14U) == pathSize &&
            memory.IsMapped(pathAddress, pathSize);
        if (validStaticBuffer && archiveId == kFsSaveDataArchiveId &&
            pathType == 1U && pathSize == 1U &&
            (mConfig.Filesystem != nullptr ||
             !mConfig.SaveDataDirectory.empty())) {
            bool openedSaveRoot = false;
            if (mConfig.Filesystem != nullptr) {
                openedSaveRoot = mConfig.Filesystem->EnsureSaveRoot();
            } else {
                std::error_code directoryError;
                std::filesystem::create_directories(
                    mConfig.SaveDataDirectory, directoryError);
                openedSaveRoot = !directoryError;
            }
            if (openedSaveRoot) {
                const uint64_t archiveHandle = mNextArchiveHandle++;
                mArchives.push_back({archiveHandle, archiveId,
                                     mConfig.Filesystem != nullptr
                                         ? std::filesystem::path("save")
                                         : mConfig.SaveDataDirectory});
                uint32_t faultAddress = 0;
                if (memory.Write32(commandBuffer,
                                   kFsOpenArchiveResponse) &&
                    memory.Write32(commandBuffer + 4U,
                                   kResultSuccess) &&
                    memory.Write64(commandBuffer + 8U, archiveHandle,
                                   &faultAddress)) {
                    state.r[0] = kResultSuccess;
                    event.Name += ":OpenArchive:SaveData";
                    event.Detail = archiveId;
                    event.Handled = true;
                    mSvcEvents.push_back(std::move(event));
                    return {NativeA32HostAction::Resume};
                }
                mArchives.pop_back();
            }
        }
    }
    if (event.Detail == kFsControlArchiveRequest) {
        uint64_t archiveHandle = 0;
        uint32_t action = 0;
        uint32_t inputSize = 0;
        uint32_t outputSize = 0;
        uint32_t inputDescriptor = 0;
        uint32_t inputAddress = 0;
        uint32_t outputDescriptor = 0;
        uint32_t outputAddress = 0;
        uint32_t faultAddress = 0;
        const bool readable =
            memory.Read64(commandBuffer + 4U, &archiveHandle,
                          &faultAddress) &&
            memory.Read32(commandBuffer + 12U, &action) &&
            memory.Read32(commandBuffer + 16U, &inputSize) &&
            memory.Read32(commandBuffer + 20U, &outputSize) &&
            memory.Read32(commandBuffer + 24U, &inputDescriptor) &&
            memory.Read32(commandBuffer + 28U, &inputAddress) &&
            memory.Read32(commandBuffer + 32U, &outputDescriptor) &&
            memory.Read32(commandBuffer + 36U, &outputAddress) &&
            inputDescriptor == ((inputSize << 4U) | 0xAU) &&
            outputDescriptor == ((outputSize << 4U) | 0xCU) &&
            memory.IsMapped(inputAddress, inputSize) &&
            memory.IsMapped(outputAddress, outputSize);
        if (readable) {
            const auto archive = std::find_if(
                mArchives.begin(), mArchives.end(),
                [archiveHandle](const ArchiveRecord& record) {
                    return record.Handle == archiveHandle;
                });
            const uint32_t result = archive == mArchives.end()
                                        ? kFsInvalidArchiveHandle
                                        : kResultSuccess;
            const bool outputInitialized =
                result != kResultSuccess || outputSize == 0U ||
                memory.Fill(outputAddress, outputSize, 0U);
            if (outputInitialized &&
                memory.Write32(commandBuffer,
                               kFsControlArchiveResponse) &&
                memory.Write32(commandBuffer + 4U, result)) {
                state.r[0] = kResultSuccess;
                event.Name += ":ControlArchive:handle=0x";
                std::ostringstream controlDetail;
                controlDetail
                    << std::hex << archiveHandle << ":action=0x"
                    << action << std::dec << ":input=" << inputSize
                    << ":output=" << outputSize << ":result=0x"
                    << std::hex << result;
                event.Name += controlDetail.str();
                event.Detail = result;
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
        }
    }
    if (event.Detail == kFsCloseArchiveRequest) {
        uint64_t archiveHandle = 0;
        uint32_t faultAddress = 0;
        if (memory.Read64(commandBuffer + 4U, &archiveHandle,
                          &faultAddress)) {
            const auto archive = std::find_if(
                mArchives.begin(), mArchives.end(),
                [archiveHandle](const ArchiveRecord& record) {
                    return record.Handle == archiveHandle;
                });
            const uint32_t result = archive == mArchives.end()
                                        ? kFsInvalidArchiveHandle
                                        : kResultSuccess;
            if (memory.Write32(commandBuffer,
                               kFsCloseArchiveResponse) &&
                memory.Write32(commandBuffer + 4U, result)) {
                if (archive != mArchives.end()) {
                    mArchives.erase(archive);
                }
                state.r[0] = kResultSuccess;
                event.Name += ":CloseArchive:handle=0x";
                std::ostringstream handleDetail;
                handleDetail << std::hex << archiveHandle
                             << ":result=0x" << result;
                event.Name += handleDetail.str();
                event.Detail = result;
                event.Handled = true;
                mSvcEvents.push_back(std::move(event));
                return {NativeA32HostAction::Resume};
            }
        }
    }
    mSvcEvents.push_back(std::move(event));
    return {NativeA32HostAction::Wait};
}

} // namespace Oot3dNativeGame
