#include "oot3d_ctr_ipc_router.h"
#include "oot3d_source_data_bindings.h"
#include "oot3d_source_execution_stack.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <utility>
#include <thread>

extern "C" void* oot3d_host_resolve_target_function(std::uintptr_t address);

namespace Oot3dSourceRuntime {

bool CtrIpcRouter::RegisterSession(CtrHandle handle, std::string name,
                                   std::shared_ptr<CtrIpcSession> session) {
    std::lock_guard guard(mStateMutex);
    if (handle == 0 || name.empty() || session == nullptr) {
        return false;
    }
    if (std::ranges::any_of(mSessions, [&](const auto& item) {
            return item.first == handle;
        })) return false;
    mSessions.emplace_back(handle,
                           SessionRecord{std::move(name), std::move(session)});
    return true;
}

bool CtrIpcRouter::RegisterPort(std::string name,
                                std::shared_ptr<CtrIpcSession> session) {
    std::lock_guard guard(mStateMutex);
    if (name.empty() || session == nullptr) {
        return false;
    }
    if (std::ranges::any_of(mPorts, [&](const auto& port) {
            return port.first == name;
        })) return false;
    mPorts.emplace_back(std::move(name), std::move(session));
    return true;
}

CtrResult CtrIpcRouter::OpenSession(std::string name,
                                    std::shared_ptr<CtrIpcSession> session,
                                    CtrHandle& outHandle) {
    std::lock_guard guard(mStateMutex);
    if (name.empty() || session == nullptr) {
        return UnhandledResult;
    }
    const auto handle = AllocateHandle();
    if (!handle || !RegisterSession(*handle, std::move(name), std::move(session)))
        return UnhandledResult;
    outHandle = *handle;
    return 0;
}

std::optional<CtrHandle> CtrIpcRouter::AllocateHandle() {
    std::lock_guard guard(mStateMutex);
    const CtrHandle firstCandidate = mNextHandle;
    do {
        if (mNextHandle == 0) ++mNextHandle;
        const CtrHandle candidate = mNextHandle++;
        if (!Contains(candidate))
            return candidate;
    } while (mNextHandle != firstCandidate);
    return std::nullopt;
}

CtrResult CtrIpcRouter::OpenKernelObject(
    std::shared_ptr<CtrKernelObject> object, CtrHandle& outHandle) {
    std::lock_guard guard(mStateMutex);
    if (!object) return UnhandledResult;
    const auto handle = AllocateHandle();
    if (!handle) return UnhandledResult;
    mObjects.emplace_back(*handle, std::move(object));
    outHandle = *handle;
    return 0;
}

CtrResult CtrIpcRouter::DuplicateHandle(CtrHandle source,
                                        CtrHandle& outHandle) {
    std::lock_guard guard(mStateMutex);
    const auto found = std::ranges::find_if(mObjects, [&](const auto& item) { return item.first == source; });
    if (found == mObjects.end()) return UnhandledResult;
    return OpenKernelObject(found->second, outHandle);
}

std::shared_ptr<CtrKernelObject> CtrIpcRouter::KernelObject(
    CtrHandle handle) const {
    std::lock_guard guard(mStateMutex);
    const auto found = std::ranges::find_if(mObjects, [&](const auto& item) { return item.first == handle; });
    return found == mObjects.end() ? nullptr : found->second;
}

CtrResult CtrIpcRouter::MapSharedMemory(CtrHandle handle,
                                        GuestAddressSpace& memory,
                                        GuestAddress address) {
    const auto object = KernelObject(handle);
    if (!object || object->Kind != CtrKernelObjectKind::SharedMemory ||
        object->SharedMemory.empty()) return UnhandledResult;
    auto target = memory.ResolveWrite(address, object->SharedMemory.size());
    if (target.size() != object->SharedMemory.size()) return UnhandledResult;
    std::copy(object->SharedMemory.begin(), object->SharedMemory.end(),
              target.begin());
    object->MappedMemory = &memory;
    object->MappedAddress = address;
    return 0;
}

CtrResult CtrIpcRouter::ControlMemory(
    GuestAddressSpace& memory, GuestAddress& outAddress,
    GuestAddress address0, GuestAddress address1, std::uint32_t size,
    std::uint32_t operation, std::uint32_t permissions) {
    std::lock_guard guard(mStateMutex);
    (void)address1;
    if (address0 == 0 || size == 0 || (address0 & 0xFFFU) != 0 ||
        (size & 0xFFFU) != 0) return UnhandledResult;
    const std::uint32_t operationKind = operation & 0xFFU;
    if (operationKind == 1U) {
        bool unmapped = false;
        if (auto* mapped = dynamic_cast<MappedGuestAddressSpace*>(&memory))
            unmapped = mapped->Unmap(address0, size);
        else if (auto* direct = dynamic_cast<DirectMappedGuestAddressSpace*>(&memory))
            unmapped = direct->Unmap(address0, size);
        if (!unmapped) return UnhandledResult;
        outAddress = address0;
        return 0;
    }
    if (operationKind != 3U) return UnhandledResult;
    GuestMemoryAccess access = GuestMemoryAccess::Read;
    if ((permissions & 2U) != 0) access = access | GuestMemoryAccess::Write;
    if ((permissions & 4U) != 0) access = access | GuestMemoryAccess::Execute;
    bool mapped = memory.ResolveRead(address0, size).size() == size;
    if (!mapped) {
        if (auto* owned = dynamic_cast<MappedGuestAddressSpace*>(&memory))
            mapped = owned->MapZeroed(address0, size, access, "svcControlMemory");
        else if (auto* direct = dynamic_cast<DirectMappedGuestAddressSpace*>(&memory))
            mapped = direct->MapZeroed(address0, size, access, "svcControlMemory");
    }
    if (!mapped) return UnhandledResult;
    mCommittedBytes += size;
    outAddress = address0;
    return 0;
}

CtrResult CtrIpcRouter::MapMemoryBlock(
    CtrHandle memoryBlock, GuestAddressSpace& memory, GuestAddress address,
    std::uint32_t permissions, std::uint32_t otherPermissions) {
    (void)permissions;
    (void)otherPermissions;
    return MapSharedMemory(memoryBlock, memory, address);
}

CtrResult CtrIpcRouter::UnmapMemoryBlock(
    CtrHandle memoryBlock, GuestAddressSpace& memory, GuestAddress address) {
    const auto object = KernelObject(memoryBlock);
    if (!object || object->Kind != CtrKernelObjectKind::SharedMemory ||
        object->MappedMemory != &memory || object->MappedAddress != address)
        return UnhandledResult;
    object->MappedMemory = nullptr;
    object->MappedAddress = 0;
    return 0;
}

CtrResult CtrIpcRouter::CreateThread(
    CtrHandle& outHandle, GuestAddress entry, GuestAddress argument,
    GuestAddress stackTop, std::int32_t priority, std::int32_t processorId) {
    std::lock_guard guard(mStateMutex);
    if (mThreadMemory == nullptr || entry == 0 || stackTop == 0 ||
        priority < 0 || priority > 0x3f || processorId < -2 || processorId > 3)
        return UnhandledResult;
    auto* direct = dynamic_cast<DirectMappedGuestAddressSpace*>(mThreadMemory);
    if (direct == nullptr || mNextThreadTls == 0 ||
        !direct->MapZeroed(mNextThreadTls, mPrimaryThread.TlsSize,
                           GuestMemoryAccess::Read | GuestMemoryAccess::Write,
                           "guest_thread_tls"))
        return UnhandledResult;

    auto object = std::make_shared<CtrKernelObject>();
    object->Kind = CtrKernelObjectKind::Thread;
    object->Name = "thread:guest";
    object->AvailableCount = 0;
    object->ThreadEntry = entry;
    object->ThreadArgument = argument;
    object->ThreadStackTop = stackTop;
    object->ThreadPriority = priority;
    object->ThreadProcessorId = processorId;
    const auto result = OpenKernelObject(object, outHandle);
    if (result != 0) return result;

    SourcePrimaryThreadDescriptor threadDescriptor = mPrimaryThread;
    threadDescriptor.TlsBaseAddress = mNextThreadTls;
    threadDescriptor.ThreadPointer = mNextThreadTls;
    threadDescriptor.Argument0 = argument;
    threadDescriptor.Priority = static_cast<std::uint32_t>(priority);
    mNextThreadTls += static_cast<GuestAddress>(mPrimaryThread.TlsSize);
    auto* memory = mThreadMemory;
    auto* services = static_cast<CtrHostServices*>(this);
    std::thread([memory, services, threadDescriptor, object, entry, argument] {
        ScopedSourceAddressSpace sourceBinding(*memory);
        ScopedCtrHostServices binding(*memory, threadDescriptor, *services);
        using Entry = void (*)(std::uint32_t);
        auto function = reinterpret_cast<Entry>(
            oot3d_host_resolve_target_function(entry));
        struct EntryContext {
            Entry Function = nullptr;
            std::uint32_t Argument = 0;
        } context{function, argument};
        SourceExecutionStack sourceStack;
        std::string error;
        if (function == nullptr) {
            std::fprintf(stderr,
                         "OOT3D source worker entry 0x%08x is not registered\n",
                         entry);
        } else if (!sourceStack.Initialize(&error)) {
            std::fprintf(stderr,
                         "OOT3D source worker stack unavailable: %s\n",
                         error.c_str());
        } else {
            const auto invocation = sourceStack.Invoke(
                [](void* opaque) {
                    const auto& entryContext =
                        *static_cast<EntryContext*>(opaque);
                    entryContext.Function(entryContext.Argument);
                },
                &context);
            if (!invocation.Invoked) {
                std::fprintf(stderr,
                             "OOT3D source worker entry was not invoked\n");
            } else if (invocation.Exception != nullptr) {
                try {
                    std::rethrow_exception(invocation.Exception);
                } catch (const std::exception& exception) {
                    std::fprintf(stderr, "OOT3D source worker failed: %s\n",
                                 exception.what());
                } catch (...) {
                    std::fprintf(stderr,
                                 "OOT3D source worker failed with an unknown exception\n");
                }
            }
        }
        {
            std::lock_guard stateGuard(object->StateMutex);
            object->AvailableCount = 1;
        }
    }).detach();
    return 0;
}

void CtrIpcRouter::ConfigureThreadRuntime(
    GuestAddressSpace& memory, SourcePrimaryThreadDescriptor primaryThread) {
    std::lock_guard guard(mStateMutex);
    mThreadMemory = &memory;
    mPrimaryThread = primaryThread;
    mNextThreadTls = primaryThread.TlsBaseAddress +
                     static_cast<GuestAddress>(primaryThread.TlsSize);
}

CtrResult CtrIpcRouter::SignalObject(CtrHandle handle,
                                     std::uint32_t releaseCount) {
    const auto object = KernelObject(handle);
    if (!object || releaseCount == 0 ||
        object->Kind == CtrKernelObjectKind::SharedMemory ||
        object->Kind == CtrKernelObjectKind::Mutex) return UnhandledResult;
    std::lock_guard stateGuard(object->StateMutex);
    object->AvailableCount = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        object->MaximumCount,
        static_cast<std::uint64_t>(object->AvailableCount) + releaseCount));
    return 0;
}

CtrResult CtrIpcRouter::ClearEvent(CtrHandle handle) {
    const auto object = KernelObject(handle);
    if (!object || object->Kind != CtrKernelObjectKind::Event)
        return UnhandledResult;
    std::lock_guard stateGuard(object->StateMutex);
    object->AvailableCount = 0;
    return 0;
}

CtrResult CtrIpcRouter::ReleaseMutex(CtrHandle handle,
                                     std::uint64_t threadId) {
    const auto object = KernelObject(handle);
    if (!object || object->Kind != CtrKernelObjectKind::Mutex)
        return UnhandledResult;
    std::lock_guard stateGuard(object->StateMutex);
    if (object->OwnerThreadId != threadId) return UnhandledResult;
    object->OwnerThreadId = 0;
    object->AvailableCount = 1;
    return 0;
}

CtrResult CtrIpcRouter::CreateEvent(CtrHandle& outHandle,
                                    std::uint32_t resetType) {
    if (resetType > 2) return UnhandledResult;
    auto object = std::make_shared<CtrKernelObject>();
    object->Kind = CtrKernelObjectKind::Event;
    object->Name = "event:guest";
    object->AutoReset = resetType == 0;
    return OpenKernelObject(std::move(object), outHandle);
}

CtrResult CtrIpcRouter::CreateAddressArbiter(CtrHandle& outHandle) {
    auto object = std::make_shared<CtrKernelObject>();
    object->Kind = CtrKernelObjectKind::AddressArbiter;
    object->Name = "address_arbiter:guest";
    return OpenKernelObject(std::move(object), outHandle);
}

CtrResult CtrIpcRouter::GetResourceLimit(CtrHandle& outHandle,
                                         CtrHandle process) {
    if (process != 0xffff8001U) return UnhandledResult;
    auto object = std::make_shared<CtrKernelObject>();
    object->Kind = CtrKernelObjectKind::ResourceLimit;
    object->Name = "resource_limit:current_process";
    return OpenKernelObject(std::move(object), outHandle);
}

std::int64_t CtrIpcRouter::GetResourceLimitCurrentValue(
    CtrHandle resourceLimit, std::uint32_t type) {
    std::lock_guard guard(mStateMutex);
    const auto object = KernelObject(resourceLimit);
    if (!object || object->Kind != CtrKernelObjectKind::ResourceLimit || type != 1)
        return -1;
    return static_cast<std::int64_t>(mCommittedBytes);
}

CtrResult CtrIpcRouter::ArbitrateAddress(CtrHandle arbiter,
                                         GuestAddressSpace& memory,
                                         GuestAddress address,
                                         std::uint32_t type,
                                         std::int32_t value) {
    const auto object = KernelObject(arbiter);
    if (!object || object->Kind != CtrKernelObjectKind::AddressArbiter ||
        (address & 3U) != 0 || type > 2) {
        return UnhandledResult;
    }
    auto bytes = memory.ResolveWrite(address, sizeof(std::int32_t));
    if (bytes.size() != sizeof(std::int32_t) ||
        reinterpret_cast<std::uintptr_t>(bytes.data()) % alignof(std::int32_t)) {
        return UnhandledResult;
    }
    auto* word = reinterpret_cast<std::int32_t*>(bytes.data());
    std::unique_lock lock(mArbitrationMutex);
    auto generationEntry = std::ranges::find_if(
        mArbitrationGeneration, [&](const auto& item) { return item.first == address; });
    if (generationEntry == mArbitrationGeneration.end()) {
        mArbitrationGeneration.emplace_back(address, 0);
        generationEntry = std::prev(mArbitrationGeneration.end());
    }
    auto& generation = generationEntry->second;
    if (type == 0) {
        ++generation;
        if (value < 0) {
            mArbitrationChanged.notify_all();
        } else {
            for (std::int32_t index = 0; index < value; ++index) {
                mArbitrationChanged.notify_one();
            }
        }
        return 0;
    }
    if (type == 2) {
        --*word;
    }
    if (*word >= value) return 0;
    const std::uint64_t observed = generation;
    mArbitrationChanged.wait(lock, [&] { return generation != observed; });
    return 0;
}

CtrIpcRouter::WaitResult CtrIpcRouter::WaitSynchronization(
    std::span<const CtrHandle> handles, bool waitAll,
    std::uint64_t threadId) {
    if (handles.empty()) return {};
    std::vector<std::shared_ptr<CtrKernelObject>> objects;
    objects.reserve(handles.size());
    for (const CtrHandle handle : handles) {
        auto object = KernelObject(handle);
        if (!object || object->Kind == CtrKernelObjectKind::SharedMemory)
            return {};
        objects.push_back(std::move(object));
    }
    const auto ready = [threadId](CtrKernelObject& object) {
        std::lock_guard stateGuard(object.StateMutex);
        return object.AvailableCount != 0 ||
               (object.Kind == CtrKernelObjectKind::Mutex &&
                object.OwnerThreadId == threadId);
    };
    std::size_t selected = 0;
    if (waitAll) {
        if (!std::ranges::all_of(objects, [&](const auto& object) {
                return ready(*object);
            })) return {0, 0, false};
    } else {
        const auto found = std::ranges::find_if(objects, [&](const auto& object) {
            return ready(*object);
        });
        if (found == objects.end()) return {0, 0, false};
        selected = static_cast<std::size_t>(found - objects.begin());
    }
    const auto consume = [threadId](CtrKernelObject& object) {
        std::lock_guard stateGuard(object.StateMutex);
        if (object.Kind == CtrKernelObjectKind::Mutex) {
            if (object.OwnerThreadId == 0) {
                object.AvailableCount = 0;
                object.OwnerThreadId = threadId;
            }
        } else if (object.AvailableCount != 0 &&
                   (object.Kind == CtrKernelObjectKind::Semaphore ||
                    object.AutoReset)) {
            --object.AvailableCount;
        }
    };
    if (waitAll) {
        for (auto& object : objects) consume(*object);
    } else {
        consume(*objects[selected]);
    }
    return {0, static_cast<std::uint32_t>(selected), true};
}

bool CtrIpcRouter::Contains(CtrHandle handle) const {
    std::lock_guard guard(mStateMutex);
    return std::ranges::any_of(mSessions, [&](const auto& item) { return item.first == handle; }) ||
           std::ranges::any_of(mObjects, [&](const auto& item) { return item.first == handle; });
}

std::optional<std::string_view> CtrIpcRouter::SessionName(CtrHandle handle) const {
    std::lock_guard guard(mStateMutex);
    const auto found = std::ranges::find_if(mSessions, [&](const auto& item) { return item.first == handle; });
    if (found == mSessions.end()) {
        return std::nullopt;
    }
    return found->second.Name;
}

std::size_t CtrIpcRouter::SessionCount() const {
    std::lock_guard guard(mStateMutex);
    return mSessions.size();
}

CtrResult CtrIpcRouter::SendSyncRequest(
    CtrHandle handle, std::span<std::uint32_t> commandBuffer) {
    std::lock_guard guard(mStateMutex);
    CtrIpcEvent event;
    event.Operation = CtrIpcEvent::Kind::SendSyncRequest;
    event.Handle = handle;
    if (!commandBuffer.empty()) {
        event.CommandHeader = commandBuffer[0];
    }

    const auto found = std::ranges::find_if(mSessions, [&](const auto& item) { return item.first == handle; });
    if (found == mSessions.end() || commandBuffer.empty()) {
        event.Result = UnhandledResult;
        mEvents.push_back(std::move(event));
        return UnhandledResult;
    }

    event.Handled = true;
    event.SessionName = found->second.Name;
    event.Result = found->second.Session->Dispatch(commandBuffer);
    mEvents.push_back(event);
    return event.Result;
}

CtrResult CtrIpcRouter::CloseHandle(CtrHandle handle) {
    std::lock_guard guard(mStateMutex);
    CtrIpcEvent event;
    event.Operation = CtrIpcEvent::Kind::CloseHandle;
    event.Handle = handle;
    const auto found = std::ranges::find_if(mSessions, [&](const auto& item) { return item.first == handle; });
    if (found == mSessions.end()) {
        const auto object = std::ranges::find_if(mObjects, [&](const auto& item) { return item.first == handle; });
        if (object != mObjects.end()) {
            event.Handled = true;
            event.SessionName = object->second->Name;
            event.Result = 0;
            mObjects.erase(object);
            mEvents.push_back(std::move(event));
            return 0;
        }
        event.Result = UnhandledResult;
        mEvents.push_back(std::move(event));
        return UnhandledResult;
    }
    event.Handled = true;
    event.SessionName = found->second.Name;
    event.Result = 0;
    mSessions.erase(found);
    mEvents.push_back(event);
    return event.Result;
}

CtrResult CtrIpcRouter::ConnectToPort(CtrHandle& outHandle,
                                      std::string_view portName) {
    std::lock_guard guard(mStateMutex);
    const auto port = std::ranges::find_if(mPorts, [&](const auto& candidate) {
        return candidate.first == portName;
    });
    if (port == mPorts.end()) {
        return UnhandledResult;
    }
    return OpenSession(port->first, port->second, outHandle);
}

const std::vector<CtrIpcEvent>& CtrIpcRouter::Events() const {
    return mEvents;
}

void CtrIpcRouter::ClearEvents() {
    mEvents.clear();
}

} // namespace Oot3dSourceRuntime
