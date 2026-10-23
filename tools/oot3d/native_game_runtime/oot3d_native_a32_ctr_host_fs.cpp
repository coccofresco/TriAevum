// CTR host services: the fs:USER session and RomFS files.
#include "oot3d_native_a32_ctr_host_internal.h"

#include <cstdio>

namespace Oot3dNativeGame {

void NativeA32CtrHostServices::WriteFsRequestSummary() const {
    std::fprintf(stderr, "TRIAEVUM_FS_REQUEST_SUMMARY unserved=%llu\n",
                 static_cast<unsigned long long>(mRuntimeProfile.FsUnservedRequests));
}

NativeA32HostResult NativeA32CtrHostServices::FailFsRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    uint32_t commandBuffer, const std::optional<std::string>& sessionType,
    NativeA32CtrSvcEvent event, uint32_t result, std::string_view reason,
    uint64_t offset, uint64_t length) {
    const uint32_t header = event.Detail;
    const auto* handle = LookupHandle(state.r[0]);
    std::string path = handle ? handle->Object->FilePath.generic_string() : "";
    if (sessionType == "client_session:fs:USER") {
        uint32_t typeOffset = 0U, addressOffset = 0U;
        switch (header) {
        case kFsOpenFileRequest: typeOffset = 16U; addressOffset = 36U; break;
        case kFsCreateFileRequest: typeOffset = 16U; addressOffset = 40U; break;
        case kFsDeleteFileRequest: typeOffset = 16U; addressOffset = 28U; break;
        case kFsOpenFileDirectlyRequest: typeOffset = 20U; addressOffset = 48U; break;
        case kFsOpenArchiveRequest: typeOffset = 8U; addressOffset = 20U; break;
        default: break;
        }
        uint32_t type = 0U, size = 0U, address = 0U;
        if (typeOffset && memory.Read32(commandBuffer + typeOffset, &type) &&
            memory.Read32(commandBuffer + typeOffset + 4U, &size) &&
            memory.Read32(commandBuffer + addressOffset, &address))
            path = DescribeLowPath(memory, type, size, address);
    }
    const uint64_t count = ++mRuntimeProfile.FsUnservedRequests;
    // First 8, then powers of two: bounded output even if the title retries.
    if (count <= 8U || (count & (count - 1U)) == 0U) {
        std::ostringstream diagnostic;
        diagnostic << "TRIAEVUM_FS_REQUEST_UNSERVED count=" << count
                   << " session=" << sessionType.value_or("invalid")
                   << " header=0x" << std::hex << header
                   << " handle=0x" << state.r[0] << " pc=0x" << event.Pc
                   << std::dec << " thread=" << event.ThreadId
                   << " path=" << std::quoted(path)
                   << " offset=" << offset << " length=" << length
                   << " size=" << (handle ? handle->Object->FileSize : 0U)
                   << " result=0x" << std::hex << result
                   << " reason=" << reason << " request=" << std::quoted(event.Name);
        std::fprintf(stderr, "%s\n", diagnostic.str().c_str());
    }
    event.Name += ":Unserved:" + std::string(reason);
    event.Handled = true;
    event.Detail = result;
    mSvcEvents.push_back(std::move(event));
    // Descriptor/handle validation belongs to SendSyncRequest, not FS IPC.
    if (result == kInvalidBufferDescriptor || result == kInvalidSessionHandle ||
        result == kSessionClosed) {
        state.r[0] = result;
        return {NativeA32HostAction::Resume};
    }
    std::array<uint32_t, 5> reply{(header & 0xFFFF0000U) | 0x40U, result};
    size_t words = 2U;
    const bool file = sessionType && sessionType->starts_with("client_session:file:");
    if (file && (header == kFileReadRequest || header == kFileWriteRequest)) {
        const uint32_t descriptorOffset = header == kFileReadRequest ? 16U : 20U;
        reply[0] = (header & 0xFFFF0000U) | 0x82U;
        words = 5U;
        if (!memory.Read32(commandBuffer + descriptorOffset, &reply[3]) ||
            !memory.Read32(commandBuffer + descriptorOffset + 4U, &reply[4])) {
            return {NativeA32HostAction::Fault, std::nullopt, 0U,
                    "FS error reply has an unreadable mapped-buffer descriptor"};
        }
    } else if ((file && header == kFileGetSizeRequest) ||
               (!file && header == kFsOpenArchiveRequest)) {
        reply[0] = (header & 0xFFFF0000U) | 0xC0U;
        words = 4U;
    } else if (!file && (header == kFsOpenFileRequest || header == kFsOpenFileDirectlyRequest)) {
        reply[0] = (header & 0xFFFF0000U) | 0x42U;
        reply[2] = kMoveHandleDescriptor;
        words = 4U;
    }
    if (!memory.IsWritable(commandBuffer, words * sizeof(uint32_t))) {
        return {NativeA32HostAction::Fault, std::nullopt, 0U,
                "FS reply command buffer is not writable"};
    }
    for (size_t i = 0; i < words; ++i) {
        if (!memory.Write32(commandBuffer + static_cast<uint32_t>(i * 4U), reply[i])) {
            return {NativeA32HostAction::Fault, std::nullopt, 0U, "FS reply write failed"};
        }
    }
    state.r[0] = kResultSuccess;
    return {NativeA32HostAction::Resume};
}


// SendSyncRequest on a client session of a RomFS file or archive session.
NativeA32HostResult NativeA32CtrHostServices::HandleRomFsRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext& context, const std::optional<std::string>& sessionType,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
    static_cast<void>(context);
    const auto* fileHandle = LookupHandle(state.r[0]);
    const auto fail = [&](uint32_t result, std::string_view reason,
                          uint64_t offset = 0U, uint64_t length = 0U) {
        return FailFsRequest(state, memory, commandBuffer, sessionType, event,
                             result, reason, offset, length);
    };
    if (!fileHandle || fileHandle->Object->FilePath.empty()) {
        return fail(kInvalidSessionHandle, "invalid-file-handle");
    }
    auto& file = *fileHandle->Object;
    if (file.FileClosed) {
        return fail(kSessionClosed, "closed-file");
    }
    const bool romfs = *sessionType == "client_session:file:romfs";
    // Validate the reply BEFORE any disk mutation or backend allocation.
    const uint32_t replyBytes = event.Detail == kFileReadRequest || event.Detail == kFileWriteRequest
                                    ? 20U : event.Detail == kFileGetSizeRequest ? 16U : 8U;
    if (!memory.IsWritable(commandBuffer, replyBytes)) {
        return fail(kFsBackendFailure, "unwritable-command-buffer");
    }
    const auto reply = [&](uint32_t header, std::string_view name,
                           uint64_t value = 0U, uint32_t descriptor = 0U,
                           uint32_t address = 0U) -> NativeA32HostResult {
        bool written = memory.Write32(commandBuffer, header) &&
                       memory.Write32(commandBuffer + 4U, kResultSuccess);
        if (header == kFileGetSizeResponse) {
            written = written && memory.Write64(commandBuffer + 8U, value, nullptr);
        } else if (header == kFileReadResponse || header == kFileWriteResponse) {
            written = written && memory.Write32(commandBuffer + 8U, static_cast<uint32_t>(value)) &&
                      memory.Write32(commandBuffer + 12U, descriptor) &&
                      memory.Write32(commandBuffer + 16U, address);
        }
        if (!written) return fail(kFsBackendFailure, "reply-write-failed");
        state.r[0] = kResultSuccess;
        event.Name += ":" + std::string(name);
        event.Handled = true;
        mSvcEvents.push_back(std::move(event));
        return {NativeA32HostAction::Resume};
    };
    if (!romfs) {
        if (file.FileBackend) {
            file.FileSize = file.FileBackend->Size();
        } else {
            std::error_code error;
            const auto size = std::filesystem::file_size(file.FilePath, error);
            if (error) return fail(kFsBackendFailure, "stat-failed");
            file.FileSize = size;
        }
    }
    if (event.Detail == kFileGetSizeRequest) {
        return reply(kFileGetSizeResponse, "GetSize", file.FileSize);
    }
    if (event.Detail == kFileReadRequest || event.Detail == kFileWriteRequest) {
        const bool writing = event.Detail == kFileWriteRequest;
        uint64_t offset = 0U;
        uint32_t length = 0U, flags = 0U, descriptor = 0U, address = 0U;
        const uint32_t descriptorOffset = writing ? 20U : 16U;
        if (!memory.Read64(commandBuffer + 4U, &offset, nullptr) ||
            !memory.Read32(commandBuffer + 12U, &length) ||
            (writing && !memory.Read32(commandBuffer + 16U, &flags)) ||
            !memory.Read32(commandBuffer + descriptorOffset, &descriptor) ||
            !memory.Read32(commandBuffer + descriptorOffset + 4U, &address)) {
            return fail(kInvalidBufferDescriptor, "unreadable-request");
        }
        const uint32_t permission = descriptor & 0xFU;
        if ((writing ? (permission != 0xAU && permission != 0xEU)
                     : (permission != 0xCU && permission != 0xEU)) ||
            (descriptor >> 4U) < length ||
            (length != 0U && (writing ? !memory.IsMapped(address, length)
                                     : !memory.IsWritable(address, length)))) {
            return fail(kInvalidBufferDescriptor, "invalid-mapped-buffer", offset, length);
        }
        if ((writing && (romfs || (file.FileOpenMode & 2U) == 0U)) ||
            (!writing && !romfs && (file.FileOpenMode & 1U) == 0U)) {
            return fail(kFsInvalidOpenFlags, "open-mode", offset, length);
        }
        const uint64_t maximum = static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max());
        if (offset > maximum || length > maximum - offset ||
            file.FileOffset > maximum - offset) {
            return fail(kFsOutOfRange, "offset-overflow", offset, length);
        }
        if (writing) {
            std::vector<uint8_t> bytes(length);
            if (length && !memory.ReadBytes(address, bytes)) {
                return fail(kInvalidBufferDescriptor, "source-read-failed", offset, length);
            }
            bool success = false;
            uint32_t written = 0U;
            if (file.FileBackend) {
                success = file.FileBackend->Write(offset, bytes, &written) && written == length;
                if (success) file.FileSize = file.FileBackend->Size();
            } else {
                std::fstream stream(file.FilePath, std::ios::binary | std::ios::in | std::ios::out);
                stream.seekp(static_cast<std::streamoff>(offset));
                if (length) stream.write(reinterpret_cast<const char*>(bytes.data()), length);
                // The backend contract is write-through; Flush can then be a no-op.
                stream.flush();
                success = static_cast<bool>(stream);
                stream.close();
                success = success && !stream.fail();
                std::error_code error;
                const auto size = std::filesystem::file_size(file.FilePath, error);
                success = success && !error;
                if (success) { file.FileSize = size; written = length; }
            }
            if (!success) return fail(kFsBackendFailure, "backend-write-failed", offset, length);
            return reply(kFileWriteResponse, "Write", written, descriptor, address);
        }
        // A read across EOF is a successful short read. For the RomFS service
        // view, do not expose bytes past its configured slice of the image.
        const uint32_t requested = romfs
            ? static_cast<uint32_t>(offset >= file.FileSize ? 0U :
                  std::min<uint64_t>(length, file.FileSize - offset)) : length;
        std::vector<uint8_t> bytes(requested);
        uint32_t returned = 0U;
        bool success = false;
        const auto start = mConfig.ProfileRuntime && romfs ? RuntimeProfileClock::now()
                                                         : RuntimeProfileClock::time_point{};
        if (mConfig.ProfileRuntime && romfs) ++mRuntimeProfile.RomFsReadCalls;
        if (file.FileBackend) {
            success = file.FileBackend->Read(file.FileOffset + offset, bytes, &returned) &&
                      returned <= requested;
        } else if (romfs) {
            if (!file.RomFsBacking)
                file.RomFsBacking = std::make_shared<RomFsBackingFile>(
                    file.FilePath, mConfig.RomFsOverlay, mConfig.RomFsImageOffset);
            bool opened = false;
            success = file.RomFsBacking->Read(file.FileOffset + offset, bytes, &opened);
            returned = success ? requested : 0U;
            if (mConfig.ProfileRuntime && opened) ++mRuntimeProfile.RomFsStreamOpenCalls;
        } else {
            std::ifstream stream(file.FilePath, std::ios::binary);
            stream.seekg(static_cast<std::streamoff>(file.FileOffset + offset));
            success = static_cast<bool>(stream);
            if (success && requested) {
                stream.read(reinterpret_cast<char*>(bytes.data()), requested);
                returned = static_cast<uint32_t>(stream.gcount());
                success = !stream.bad() && (stream.good() || stream.eof());
            }
        }
        if (mConfig.ProfileRuntime && romfs) {
            mRuntimeProfile.RomFsReadBytes += success ? returned : 0U;
            mRuntimeProfile.RomFsReadNanoseconds += RuntimeProfileElapsedNanoseconds(start);
        }
        if (!success) return fail(kFsBackendFailure, "backend-read-failed", offset, length);
        if (returned && !memory.WriteBytes(address, std::span<const uint8_t>(bytes.data(), returned))) {
            return fail(kInvalidBufferDescriptor, "target-write-failed", offset, length);
        }
        return reply(kFileReadResponse, "Read", returned, descriptor, address);
    }
    // Set/GetPriority: the session's I/O priority. Reads complete
    // synchronously here, so it is only kept and reported back.
    if (event.Detail == kFileSetPriorityRequest) {
        uint32_t priority = 0;
        if (!memory.Read32(commandBuffer + 4U, &priority))
            return fail(kInvalidBufferDescriptor, "unreadable-priority");
        file.FilePriority = priority;
        return reply(kFileSetPriorityResponse, "SetPriority");
    }
    if (event.Detail == kFileGetPriorityRequest) {
        if (!memory.Write32(commandBuffer + 8U, file.FilePriority))
            return fail(kFsBackendFailure, "reply-write-failed");
        return reply(kFileGetPriorityResponse, "GetPriority");
    }
    if (event.Detail == kFileOpenLinkFileRequest) {
        // Another session on the same file. A file session here holds no
        // state of its own (reads carry their offset), so the link shares
        // the file and its backing stream. The fields are copied before
        // CreateHandle, which may reallocate the handle table.
        auto link = std::make_shared<KernelObject>();
        link->Type = file.Type;
        link->FilePath = file.FilePath;
        link->FileOffset = file.FileOffset;
        link->FileSize = file.FileSize;
        link->FileOpenMode = file.FileOpenMode;
        link->FilePriority = file.FilePriority;
        link->FileBackend = file.FileBackend;
        link->RomFsBacking = file.RomFsBacking;
        const uint32_t handle = CreateHandle(std::move(link));
        if (handle == 0U || !memory.Write32(commandBuffer + 8U, kMoveHandleDescriptor) ||
            !memory.Write32(commandBuffer + 12U, handle))
            return fail(kFsBackendFailure, "link-handle-failed");
        return reply(kFileOpenLinkFileResponse, "OpenLinkFile");
    }
    if (event.Detail == kFileSetSizeRequest) {
        uint64_t size = 0U;
        if (!memory.Read64(commandBuffer + 4U, &size, nullptr))
            return fail(kInvalidBufferDescriptor, "unreadable-size");
        if (romfs || (file.FileOpenMode & 2U) == 0U)
            return fail(kFsInvalidOpenFlags, "open-mode", 0U, size);
        if (size > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
            return fail(kFsOutOfRange, "size-overflow", 0U, size);
        bool resized = false;
        if (file.FileBackend) resized = file.FileBackend->Resize(size);
        else {
            std::error_code error;
            std::filesystem::resize_file(file.FilePath, size, error);
            resized = !error;
        }
        if (!resized) return fail(kFsBackendFailure, "backend-resize-failed", 0U, size);
        file.FileSize = size;
        return reply(kFileSetSizeResponse, "SetSize");
    }
    if (event.Detail == kFileFlushRequest) {
        // RootedFilesystemBackendV1::Write flushes before returning success.
        return reply(kFileFlushResponse, "Flush");
    }
    if (event.Detail == kFileCloseRequest) {
        // FSFILE_Close releases the backing stream. svcCloseHandle separately
        // releases the session handle; keep it valid until that SVC arrives.
        file.FileBackend.reset();
        file.RomFsBacking.reset();
        file.FileClosed = true;
        return reply(kFileCloseResponse, "Close");
    }
    return fail(kFsNotImplemented, "unsupported-file-command");
}

// SendSyncRequest on a client session of fs:USER.
NativeA32HostResult NativeA32CtrHostServices::HandleFsRequest(
    oot3d::recomp::a32::GuestState& state, NativeA32Memory& memory,
    NativeA32HostContext& context, const std::optional<std::string>& sessionType,
    uint32_t commandBuffer, NativeA32CtrSvcEvent event) {
    // Validate before opening, creating, deleting, or resizing host files.
    const uint32_t replyBytes = event.Detail == kFsOpenFileRequest ||
                                event.Detail == kFsOpenFileDirectlyRequest ||
                                event.Detail == kFsOpenArchiveRequest ? 16U : 8U;
    if (!memory.IsWritable(commandBuffer, replyBytes)) {
        return FailFsRequest(state, memory, commandBuffer, sessionType, event,
                             kFsBackendFailure, "unwritable-command-buffer");
    }
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
                                    (mConfig.RomFsLayers != nullptr ||
                                     (mConfig.RomFsImageSize != 0U &&
                                      (mConfig.Filesystem != nullptr ||
                                       !mConfig.RomFsImagePath.empty())));
            if (!requestsRomFs) {
                return FailFsRequest(state, memory, commandBuffer, sessionType, event,
                                     kFsInvalidPath, "unsupported-romfs-path");
            }
            if (!opensRomFs) {
                return FailFsRequest(state, memory, commandBuffer, sessionType, event,
                                     0xC8804464U, "romfs-unavailable");
            }
            if (opensRomFs) {
                auto file = std::make_shared<KernelObject>();
                file->Type = "client_session:file:romfs";
                file->FileOffset = mConfig.RomFsImageOffset;
                file->FileSize = RomFsServiceViewSize();
                bool opened = false;
                if (mConfig.RomFsLayers != nullptr) {
                    // The rebuilt level 3 is the whole view, from offset 0.
                    file->FilePath = mConfig.Filesystem != nullptr
                                         ? std::filesystem::path(mConfig.RomFsContentPath)
                                         : mConfig.RomFsImagePath;
                    file->FileOffset = 0U;
                    file->FileSize = mConfig.RomFsLayers->Size();
                    file->FileBackend = mConfig.RomFsLayers;
                    opened = true;
                } else if (mConfig.Filesystem != nullptr) {
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
                            file->FilePath, mConfig.RomFsOverlay,
                            mConfig.RomFsImageOffset);
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
                return FailFsRequest(state, memory, commandBuffer, sessionType, event,
                                     kFsBackendFailure, "romfs-open-failed");
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
            uint32_t result = archive == mArchives.end() ? kFsInvalidArchiveHandle : kFsInvalidPath;
            if (archive != mArchives.end() &&
                relativePath.has_value()) {
                result = kFsBackendFailure;
                if (archive->Id != kFsSaveDataArchiveId) result = kFsCommandNotAllowed;
                else if (fileSize > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) result = kFsOutOfRange;
                else if (mConfig.Filesystem != nullptr) {
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
            if (result != kResultSuccess && result != kFsFileAlreadyExists) {
                event.Name += ":CreateFile:" + DescribeLowPath(memory, pathType, pathSize, pathAddress);
                return FailFsRequest(state, memory, commandBuffer, sessionType, event,
                                     result, "create-file-failed", 0U, fileSize);
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
            uint32_t result = archive == mArchives.end() ? kFsInvalidArchiveHandle : kFsInvalidPath;
            uint32_t fileSession = 0;
            if (archive != mArchives.end() &&
                relativePath.has_value()) {
                result = kFsFileNotFound;
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
            return FailFsRequest(state, memory, commandBuffer, sessionType, event,
                                 kFsBackendFailure, "save-root-open-failed");
        }
        if (validStaticBuffer) {
            return FailFsRequest(state, memory, commandBuffer, sessionType, event,
                                 kFsInvalidPath, "unsupported-archive-path");
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
    const uint32_t id = event.Detail >> 16U;
    const bool knownCommand = id == 0x801U || id == 0x802U || id == 0x803U ||
                              id == 0x804U || id == 0x808U || id == 0x80CU ||
                              id == 0x80DU || id == 0x80EU;
    return FailFsRequest(state, memory, commandBuffer, sessionType, event,
                         knownCommand ? kInvalidBufferDescriptor : kFsNotImplemented,
                         knownCommand ? "malformed-fs-request" : "unsupported-fs-command");
}

} // namespace Oot3dNativeGame
