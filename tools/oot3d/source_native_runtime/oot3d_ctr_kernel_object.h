#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <mutex>
#include <vector>

#include "oot3d_guest_address_space.h"

namespace Oot3dSourceRuntime {

enum class CtrKernelObjectKind : std::uint8_t {
    Event,
    Mutex,
    Semaphore,
    SharedMemory,
    AddressArbiter,
    ResourceLimit,
    Thread,
};

struct CtrKernelObject {
    CtrKernelObjectKind Kind = CtrKernelObjectKind::Event;
    std::string Name;
    std::uint32_t AvailableCount = 0;
    std::uint32_t MaximumCount = 1;
    std::vector<std::byte> SharedMemory;
    bool AutoReset = true;
    std::uint64_t OwnerThreadId = 0;
    GuestAddressSpace* MappedMemory = nullptr;
    GuestAddress MappedAddress = 0;
    GuestAddress ThreadEntry = 0;
    GuestAddress ThreadArgument = 0;
    GuestAddress ThreadStackTop = 0;
    std::int32_t ThreadPriority = 0;
    std::int32_t ThreadProcessorId = 0;
    mutable std::mutex StateMutex;
};

} // namespace Oot3dSourceRuntime
