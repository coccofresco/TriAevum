#include "fast/renderer3ds/vulkan_breadcrumbs.h"
#include "fast/renderer3ds/vulkan_device_fault.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <size_t Size>
void CopyText(char (&destination)[Size], const char* text) {
    std::snprintf(destination, Size, "%s", text);
}

void RequireEqual(const std::string& actual, const std::string& expected, const char* message) {
    if (actual != expected) {
        std::cerr << "expected: " << expected << "\nactual:   " << actual << '\n';
        throw std::runtime_error(message);
    }
}

void DeviceFaultPolicy() {
    using namespace Fast::Renderer3ds;
    VkPhysicalDeviceFaultFeaturesEXT features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
    Require(!VulkanDeviceFaultSupported(false, features), "no extension, no feature");
    Require(!VulkanDeviceFaultSupported(true, features), "the extension alone is not enough");
    features.deviceFault = VK_TRUE;
    Require(!VulkanDeviceFaultSupported(false, features), "a feature without the extension is ignored");
    Require(VulkanDeviceFaultSupported(true, features), "extension and feature enable the report");

    const auto enabled = VulkanDeviceFaultFeatures();
    Require(enabled.sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT && enabled.pNext == nullptr,
            "enabled feature structure");
    Require(enabled.deviceFault == VK_TRUE && enabled.deviceFaultVendorBinary == VK_FALSE,
            "only deviceFault is requested");

    VulkanDeviceFaultReporter reporter;
    Require(!reporter.Enabled(), "a reporter starts unbound");
    Require(!reporter.Consume(VK_ERROR_DEVICE_LOST, "vkQueueSubmit(frame)").has_value(),
            "an unbound reporter is a no-op");
    reporter.Bind(VK_NULL_HANDLE, true);
    Require(!reporter.Enabled(), "no device, no command");
}

void DeviceFaultLine() {
    using namespace Fast::Renderer3ds;
    std::array<VkDeviceFaultAddressInfoEXT, 2> addresses{};
    addresses[0] = {VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT, 0x7F001000ULL, 0x1000ULL};
    addresses[1] = {VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT, 0xDEADBEEF00ULL, 0x4ULL};
    std::array<VkDeviceFaultVendorInfoEXT, 1> vendors{};
    CopyText(vendors[0].description, "page fault");
    vendors[0].vendorFaultCode = 0x2AULL;
    vendors[0].vendorFaultData = 0x1234ULL;

    VkDeviceFaultCountsEXT counts{VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
    counts.addressInfoCount = uint32_t(addresses.size());
    counts.vendorInfoCount = uint32_t(vendors.size());
    VkDeviceFaultInfoEXT info{VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT};
    CopyText(info.description, "GPU hang \"gfx\"\nring");
    info.pAddressInfos = addresses.data();
    info.pVendorInfos = vendors.data();
    RequireEqual(FormatVulkanDeviceFaultLine("vkQueueSubmit(frame)", VK_ERROR_DEVICE_LOST, counts, info),
                 "TRIAEVUM_VULKAN_DEVICE_FAULT operation=\"vkQueueSubmit(frame)\" result=-4 "
                 "description=\"GPU hang \\\"gfx\\\"\\x0Aring\" "
                 "address_infos=[{type=read_invalid,address=0x7F001000,precision=0x1000},"
                 "{type=instruction_pointer_fault,address=0xDEADBEEF00,precision=0x4}] "
                 "vendor_infos=[{description=\"page fault\",code=0x2A,data=0x1234}]",
                 "fault line formatting");

    // Counts without arrays (a driver that reports no detail) stay empty, and
    // an unterminated description is bounded by its array.
    VkDeviceFaultInfoEXT bare{VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT};
    std::memset(bare.description, 'x', sizeof(bare.description));
    const auto line = FormatVulkanDeviceFaultLine(nullptr, VK_ERROR_DEVICE_LOST, counts, bare);
    Require(line.find("operation=\"unknown\"") != std::string::npos, "missing operation name");
    Require(line.find("description=\"" + std::string(VK_MAX_DESCRIPTION_SIZE, 'x') + "\"") != std::string::npos,
            "unterminated description is bounded");
    Require(line.find("address_infos=[] vendor_infos=[]") != std::string::npos, "absent arrays are empty");
    Require(line.find('\n') == std::string::npos, "the report is one line");
}

void BreadcrumbPolicy() {
    using namespace Fast::Renderer3ds;
    using Mechanism = VulkanBreadcrumbMechanism;
    Require(VulkanBreadcrumbsRequested("1"), "1 requests breadcrumbs");
    for (const char* value : {static_cast<const char*>(nullptr), "", "0", "yes", "11"})
        Require(!VulkanBreadcrumbsRequested(value), "only 1 requests breadcrumbs");
    Require(ResolveVulkanBreadcrumbMechanism(false, true, true) == Mechanism::Off,
            "unrequested breadcrumbs stay off whatever the device offers");
    Require(ResolveVulkanBreadcrumbMechanism(true, true, true) == Mechanism::BufferMarker,
            "buffer markers are preferred");
    Require(ResolveVulkanBreadcrumbMechanism(true, false, true) == Mechanism::Checkpoints,
            "checkpoints are the fallback");
    Require(ResolveVulkanBreadcrumbMechanism(true, false, false) == Mechanism::Off,
            "no mechanism, no breadcrumbs");
    Require(VulkanBreadcrumbExtension(Mechanism::Off) == nullptr, "off enables no extension");
    Require(std::strcmp(VulkanBreadcrumbExtension(Mechanism::BufferMarker), VK_AMD_BUFFER_MARKER_EXTENSION_NAME) == 0,
            "buffer marker extension");
    Require(std::strcmp(VulkanBreadcrumbExtension(Mechanism::Checkpoints),
                        VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME) == 0,
            "checkpoint extension");

    VulkanBreadcrumbTrail trail;
    Require(!trail.Active() && !trail.Describe().has_value(), "an inactive trail describes nothing");
    trail.Record(VK_NULL_HANDLE, "frame_begin");
    Require(!trail.Initialize(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, Mechanism::Off) && !trail.Active(),
            "off never initializes");
}

void BreadcrumbHistory() {
    using namespace Fast::Renderer3ds;
    VulkanBreadcrumbHistory history;
    Require(history.LastId() == 0U && !history.Find(0U).has_value() && !history.Find(1U).has_value(),
            "empty history");
    Require(history.Push("frame_begin", 7U) == 1U, "ids start at one");
    Require(history.Push("native_pica", 7U) == 2U, "ids are monotonic");
    const auto entry = history.Find(2U);
    Require(entry.has_value() && entry->Frame == 7U && std::strcmp(entry->Label, "native_pica") == 0,
            "an id names its pass and frame");
    for (uint32_t index = 0; index < VulkanBreadcrumbHistory::kCapacity; ++index)
        history.Push("grass", 8U);
    Require(!history.Find(2U).has_value(), "overwritten ids are unknown, not misnamed");
    Require(history.Find(history.LastId()).has_value(), "the newest id is kept");
}

void BreadcrumbLine() {
    using namespace Fast::Renderer3ds;
    VulkanBreadcrumbHistory history;
    history.Push("frame_begin", 3U);
    history.Push("native_pica", 3U);
    history.Push("after native_pica", 3U);
    history.Push("scanout", 3U);
    history.Push("frame_end", 3U);
    RequireEqual(FormatVulkanBreadcrumbLine(VulkanBreadcrumbMechanism::BufferMarker, history, 3U, 1U),
                 "TRIAEVUM_VULKAN_BREADCRUMBS mechanism=amd_buffer_marker recorded=5 started=3 completed=1 "
                 "last_completed={id=1,frame=3,pass=\"frame_begin\"} "
                 "in_flight=[{id=2,frame=3,pass=\"native_pica\"},{id=3,frame=3,pass=\"after native_pica\"},"
                 "{id=4,frame=3,pass=\"scanout\"}]",
                 "breadcrumb line formatting");
    RequireEqual(FormatVulkanBreadcrumbLine(VulkanBreadcrumbMechanism::Checkpoints, history, 0U, 0U),
                 "TRIAEVUM_VULKAN_BREADCRUMBS mechanism=nv_checkpoints recorded=5 started=0 completed=0 "
                 "last_completed=none in_flight=[{id=1,frame=3,pass=\"frame_begin\"}]",
                 "nothing executed yet");
    RequireEqual(FormatVulkanBreadcrumbLine(VulkanBreadcrumbMechanism::BufferMarker, history, 5U, 5U),
                 "TRIAEVUM_VULKAN_BREADCRUMBS mechanism=amd_buffer_marker recorded=5 started=5 completed=5 "
                 "last_completed={id=5,frame=3,pass=\"frame_end\"} in_flight=[]",
                 "everything recorded completed");

    VulkanBreadcrumbHistory longHistory;
    for (int index = 0; index < 40; ++index)
        longHistory.Push("pass", 1U);
    const auto line = FormatVulkanBreadcrumbLine(VulkanBreadcrumbMechanism::BufferMarker, longHistory, 39U, 0U);
    Require(line.find("{id=16,") != std::string::npos && line.find("{id=17,") == std::string::npos &&
                line.find(",...]") != std::string::npos,
            "in-flight list is bounded");
}
} // namespace

int main() {
    try {
        DeviceFaultPolicy();
        DeviceFaultLine();
        BreadcrumbPolicy();
        BreadcrumbHistory();
        BreadcrumbLine();
    } catch (const std::exception& error) {
        std::cerr << "vulkan_device_loss_diagnostics_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "vulkan_device_loss_diagnostics_tests passed\n";
    return 0;
}
