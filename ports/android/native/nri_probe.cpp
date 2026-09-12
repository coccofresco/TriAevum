#include <NRI.h>
#include <Extensions/NRIDeviceCreation.h>

#include <cstdio>
#include <unistd.h>
#include <vector>

int main() {
    uint32_t count = 0;
    auto result = nri::nriEnumerateAdapters(nullptr, count);
    if (result != nri::Result::SUCCESS || count == 0) {
        std::fprintf(stderr, "NRI Vulkan adapter enumeration failed: %u\n", static_cast<unsigned>(result));
        return 1;
    }
    std::vector<nri::AdapterDesc> adapters(count);
    result = nri::nriEnumerateAdapters(adapters.data(), count);
    if (result != nri::Result::SUCCESS) return 2;
    nri::DeviceCreationDesc creation{};
    creation.graphicsAPI = nri::GraphicsAPI::VK;
    creation.adapterDesc = adapters.data();
    creation.disableVKRayTracing = true;
    nri::Device* device = nullptr;
    result = nri::nriCreateDevice(creation, device);
    if (result != nri::Result::SUCCESS) {
        std::fprintf(stderr, "NRI Vulkan device creation failed: %u\n", static_cast<unsigned>(result));
        return 3;
    }
    nri::CoreInterface core{};
    result = nri::nriGetInterface(*device, NRI_INTERFACE(nri::CoreInterface), &core);
    nri::Queue* queue = nullptr;
    if (result == nri::Result::SUCCESS) result = core.GetQueue(*device, nri::QueueType::GRAPHICS, 0, queue);
    const bool ready = result == nri::Result::SUCCESS && queue != nullptr;
    nri::nriDestroyDevice(device);
    std::printf("{\"nri_vulkan_device\":%s,\"adapters\":%u,\"host_page_size\":%ld,"
                "\"scope\":\"device_only_not_gameplay\"}\n", ready ? "true" : "false", count, sysconf(_SC_PAGESIZE));
    return ready ? 0 : 4;
}
