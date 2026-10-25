#include "fast/renderer3ds/vulkan_breadcrumbs.h"

#include <algorithm>
#include <string_view>
#include <vector>

#include "fast/renderer3ds/vulkan_commands.h"

namespace Fast::Renderer3ds {

namespace {

constexpr uint32_t kInFlightLimit = 16;
constexpr VkDeviceSize kStartedOffset = 0;
constexpr VkDeviceSize kCompletedOffset = sizeof(uint32_t);
constexpr VkDeviceSize kMarkerBytes = 2 * sizeof(uint32_t);

void AppendEntry(std::string& line, const VulkanBreadcrumbHistory& history, uint32_t id) {
    line += "{id=" + std::to_string(id);
    if (const auto entry = history.Find(id)) {
        line += ",frame=" + std::to_string(entry->Frame) + ",pass=\"";
        line += entry->Label != nullptr ? entry->Label : "unnamed";
        line += "\"}";
    } else {
        line += ",pass=unknown}";
    }
}

} // namespace

const char* VulkanBreadcrumbMechanismName(VulkanBreadcrumbMechanism mechanism) noexcept {
    switch (mechanism) {
        case VulkanBreadcrumbMechanism::BufferMarker:
            return "amd_buffer_marker";
        case VulkanBreadcrumbMechanism::Checkpoints:
            return "nv_checkpoints";
        case VulkanBreadcrumbMechanism::Off:
            break;
    }
    return "off";
}

bool VulkanBreadcrumbsRequested(const char* environment) noexcept {
    return environment != nullptr && std::string_view(environment) == "1";
}

VulkanBreadcrumbMechanism ResolveVulkanBreadcrumbMechanism(
    bool requested, bool bufferMarkerAvailable, bool checkpointsAvailable) noexcept {
    if (!requested)
        return VulkanBreadcrumbMechanism::Off;
    if (bufferMarkerAvailable)
        return VulkanBreadcrumbMechanism::BufferMarker;
    return checkpointsAvailable ? VulkanBreadcrumbMechanism::Checkpoints : VulkanBreadcrumbMechanism::Off;
}

const char* VulkanBreadcrumbExtension(VulkanBreadcrumbMechanism mechanism) noexcept {
    switch (mechanism) {
        case VulkanBreadcrumbMechanism::BufferMarker:
            return VK_AMD_BUFFER_MARKER_EXTENSION_NAME;
        case VulkanBreadcrumbMechanism::Checkpoints:
            return VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME;
        case VulkanBreadcrumbMechanism::Off:
            break;
    }
    return nullptr;
}

uint32_t VulkanBreadcrumbHistory::Push(const char* label, uint64_t frame) noexcept {
    // Zero stays reserved for "no marker written" across the 32-bit wrap.
    if (++mLastId == 0U)
        mLastId = 1U;
    mEntries[mLastId % kCapacity] = {mLastId, frame, label};
    return mLastId;
}

std::optional<VulkanBreadcrumbHistory::Entry> VulkanBreadcrumbHistory::Find(uint32_t id) const noexcept {
    const auto& entry = mEntries[id % kCapacity];
    if (id == 0U || entry.Id != id)
        return std::nullopt;
    return entry;
}

std::string FormatVulkanBreadcrumbLine(
    VulkanBreadcrumbMechanism mechanism, const VulkanBreadcrumbHistory& history,
    uint32_t started, uint32_t completed) {
    const uint32_t recorded = history.LastId();
    std::string line = "TRIAEVUM_VULKAN_BREADCRUMBS mechanism=";
    line += VulkanBreadcrumbMechanismName(mechanism);
    line += " recorded=" + std::to_string(recorded) + " started=" + std::to_string(started) +
            " completed=" + std::to_string(completed) + " last_completed=";
    if (completed != 0U)
        AppendEntry(line, history, completed);
    else
        line += "none";
    line += " in_flight=[";
    // The work after the last started marker may have begun as well.
    const uint32_t last = std::min(std::max(started, completed) + 1U, recorded);
    uint32_t listed = 0;
    for (uint32_t id = completed + 1U; id != 0U && id <= last; ++id) {
        if (listed == kInFlightLimit) {
            line += ",...";
            break;
        }
        if (listed++ != 0U)
            line += ',';
        AppendEntry(line, history, id);
    }
    line += ']';
    return line;
}

bool VulkanBreadcrumbTrail::Initialize(
    VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue, VulkanBreadcrumbMechanism mechanism) {
    Shutdown();
    if (mechanism == VulkanBreadcrumbMechanism::Off || device == VK_NULL_HANDLE)
        return false;
    mDevice = device;
    mQueue = queue;
    if (mechanism == VulkanBreadcrumbMechanism::Checkpoints) {
        mSetCheckpoint = VulkanDeviceCommand<PFN_vkCmdSetCheckpointNV>(device, "vkCmdSetCheckpointNV");
        mGetCheckpointData =
            VulkanDeviceCommand<PFN_vkGetQueueCheckpointDataNV>(device, "vkGetQueueCheckpointDataNV");
        if (mSetCheckpoint == nullptr || mGetCheckpointData == nullptr || queue == VK_NULL_HANDLE) {
            Shutdown();
            return false;
        }
        mMechanism = mechanism;
        return true;
    }
    mWriteBufferMarker = VulkanDeviceCommand<PFN_vkCmdWriteBufferMarkerAMD>(device, "vkCmdWriteBufferMarkerAMD");
    if (mWriteBufferMarker == nullptr) {
        Shutdown();
        return false;
    }
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = kMarkerBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &mBuffer) != VK_SUCCESS) {
        mBuffer = VK_NULL_HANDLE;
        Shutdown();
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, mBuffer, &requirements);
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory);
    // Host-coherent memory outlives the device's loss and needs no flush.
    constexpr VkMemoryPropertyFlags kFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = UINT32_MAX;
    for (uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
        if ((requirements.memoryTypeBits & (1U << index)) != 0U &&
            (memory.memoryTypes[index].propertyFlags & kFlags) == kFlags) {
            type = index;
            break;
        }
    }
    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = type;
    void* mapped = nullptr;
    if (type == UINT32_MAX || vkAllocateMemory(device, &allocateInfo, nullptr, &mMemory) != VK_SUCCESS) {
        mMemory = VK_NULL_HANDLE;
        Shutdown();
        return false;
    }
    if (vkBindBufferMemory(device, mBuffer, mMemory, 0) != VK_SUCCESS ||
        vkMapMemory(device, mMemory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
        Shutdown();
        return false;
    }
    mMarkers = static_cast<volatile uint32_t*>(mapped);
    mMarkers[0] = 0U;
    mMarkers[1] = 0U;
    mMechanism = mechanism;
    return true;
}

void VulkanBreadcrumbTrail::Shutdown() noexcept {
    if (mDevice != VK_NULL_HANDLE) {
        if (mMarkers != nullptr)
            vkUnmapMemory(mDevice, mMemory);
        if (mBuffer != VK_NULL_HANDLE)
            vkDestroyBuffer(mDevice, mBuffer, nullptr);
        if (mMemory != VK_NULL_HANDLE)
            vkFreeMemory(mDevice, mMemory, nullptr);
    }
    mMechanism = VulkanBreadcrumbMechanism::Off;
    mDevice = VK_NULL_HANDLE;
    mQueue = VK_NULL_HANDLE;
    mBuffer = VK_NULL_HANDLE;
    mMemory = VK_NULL_HANDLE;
    mMarkers = nullptr;
    mWriteBufferMarker = nullptr;
    mSetCheckpoint = nullptr;
    mGetCheckpointData = nullptr;
}

void VulkanBreadcrumbTrail::Record(VkCommandBuffer commandBuffer, const char* label) {
    if (mMechanism == VulkanBreadcrumbMechanism::Off || commandBuffer == VK_NULL_HANDLE)
        return;
    uint32_t id = 0;
    {
        std::lock_guard lock(mMutex);
        id = mHistory.Push(label, mFrame);
    }
    if (mMechanism == VulkanBreadcrumbMechanism::BufferMarker) {
        mWriteBufferMarker(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mBuffer, kStartedOffset, id);
        mWriteBufferMarker(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mBuffer, kCompletedOffset, id);
    } else {
        mSetCheckpoint(commandBuffer, reinterpret_cast<const void*>(static_cast<uintptr_t>(id)));
    }
}

std::optional<std::string> VulkanBreadcrumbTrail::Describe() {
    if (mMechanism == VulkanBreadcrumbMechanism::Off)
        return std::nullopt;
    uint32_t started = 0;
    uint32_t completed = 0;
    if (mMechanism == VulkanBreadcrumbMechanism::BufferMarker) {
        started = mMarkers[0];
        completed = mMarkers[1];
    } else {
        // Each checkpoint reports the last marker a pipeline stage passed.
        uint32_t count = 0;
        mGetCheckpointData(mQueue, &count, nullptr);
        std::vector<VkCheckpointDataNV> checkpoints(count, {VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV});
        if (count != 0U)
            mGetCheckpointData(mQueue, &count, checkpoints.data());
        for (uint32_t index = 0; index < count && index < checkpoints.size(); ++index) {
            const auto id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(checkpoints[index].pCheckpointMarker));
            if (checkpoints[index].stage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
                completed = std::max(completed, id);
            else
                started = std::max(started, id);
        }
    }
    std::lock_guard lock(mMutex);
    return FormatVulkanBreadcrumbLine(mMechanism, mHistory, started, completed);
}

} // namespace Fast::Renderer3ds
