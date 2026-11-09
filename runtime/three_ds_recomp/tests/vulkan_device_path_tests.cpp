// The device negotiation for the AMD Windows reports (#117 RX 5700 XT,
// #118/#126 RX 6600, #119 RX 5600M, #120 Radeon iGPU, #125 Radeon 840M,
// #126 comment 6800U): the real device profile (pica_vulkan_device_profile.h)
// and the real path report (vulkan_device_path_report.h), fed the properties
// the reports' logs show (vendor 0x1002, amdvlk64 driver id AMD_PROPRIETARY).
// beta.1.2 linked library parts on this driver; the reports crash inside
// amdvlk64 while "Programs N/55" (library parts) are created. Since 083845de
// the driver defaults to monolithic pipelines with robust buffer access.
//
// --negative asserts the beta.1.2 expectation (library parts on AMD) and must
// fail: it shows the test tells the two policies apart.
#include "fast/renderer3ds/vulkan_device_path_report.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace Fast::Renderer3ds;

namespace {
int failures = 0;
void Check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

struct Reported {
    const char* Issue;
    const char* Name;
    bool RobustSupported;
};

VulkanDevicePath Negotiate(const char* name, VkDriverId driver, bool robustSupported, const char* environment) {
    VkPhysicalDeviceFeatures supported{};
    supported.robustBufferAccess = robustSupported ? VK_TRUE : VK_FALSE;
    supported.fragmentStoresAndAtomics = VK_TRUE;
    const auto enabled = PicaVulkanCoreFeatures(supported, driver);
    // Every reported device offers the library extensions (api 1.4.315/1.4.329).
    const auto policy = ResolvePicaPipelineLibraryPolicy(environment, true, true, true, driver);
    return VulkanDevicePath{name, driver, policy, true, robustSupported, enabled.robustBufferAccess == VK_TRUE, true};
}
} // namespace

int main(int argc, char** argv) {
    const bool negative = argc > 1 && std::strcmp(argv[1], "--negative") == 0;
    const Reported reports[] = {
        {"#117", "AMD Radeon RX 5700 XT", true},      {"#118", "AMD Radeon RX 6600", true},
        {"#119", "AMD Radeon(TM) RX 5600M Series", true}, {"#120", "AMD Radeon(TM) Graphics", true},
        {"#125", "AMD Radeon(TM) 840M Graphics", true}, {"#126", "AMD Radeon RX 6600", true},
        {"#126 comment", "AMD Radeon(TM) Graphics (6800U)", true},
    };
    for (const auto& report : reports) {
        const auto path = Negotiate(report.Name, VK_DRIVER_ID_AMD_PROPRIETARY, report.RobustSupported, nullptr);
        const auto line = DescribeVulkanDevicePath(path);
        std::printf("%s %s\n", report.Issue, line.c_str());
        if (negative) {
            Check(path.Libraries.Enabled, std::string(report.Issue) + " beta.1.2 expectation: library parts");
            continue;
        }
        Check(!path.Libraries.Enabled, std::string(report.Issue) + " uses monolithic pipelines");
        Check(std::strcmp(path.Libraries.Reason, "driver_policy") == 0, std::string(report.Issue) + " reason driver_policy");
        Check(path.RobustBufferAccessEnabled, std::string(report.Issue) + " robust buffer access on");
        Check(line.find("pipelines=monolithic") != std::string::npos &&
                  line.find("robust_buffer_access=on") != std::string::npos &&
                  line.find("driver=amd_proprietary") != std::string::npos,
              std::string(report.Issue) + " log line names the path");
    }
    if (!negative) {
        // Queried, not assumed: a driver that does not offer the feature does not get it enabled.
        const auto missing = Negotiate("AMD without robustBufferAccess", VK_DRIVER_ID_AMD_PROPRIETARY, false, nullptr);
        Check(!missing.RobustBufferAccessEnabled, "unsupported robust buffer access is not enabled");
        Check(std::strcmp(VulkanRobustBufferAccessState(missing), "required_unsupported") == 0,
              "unsupported robust buffer access is reported");
        // A diagnostic opt-in still reaches library parts and says so.
        const auto optIn = Negotiate("AMD Radeon RX 6600", VK_DRIVER_ID_AMD_PROPRIETARY, true, "1");
        Check(optIn.Libraries.Enabled && std::strcmp(optIn.Libraries.Reason, "explicit_opt_in") == 0,
              "explicit opt-in is reported");
        // Other drivers keep their own defaults: RADV (Linux, Steam Deck) links library parts
        // without robust buffer access; NVIDIA likewise.
        for (const auto driver : {VK_DRIVER_ID_MESA_RADV, VK_DRIVER_ID_NVIDIA_PROPRIETARY}) {
            const auto path = Negotiate("other", driver, true, nullptr);
            Check(path.Libraries.Enabled && !path.RobustBufferAccessEnabled, "non-AMD-Windows driver default");
        }
        const auto anv = Negotiate("Intel Mesa", VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA, true, nullptr);
        Check(!anv.Libraries.Enabled, "Intel Mesa stays monolithic");
    }
    std::printf("%s%s: %d failures\n", negative ? "NEGATIVE " : "", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
