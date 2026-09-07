#include <switch.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

extern "C" {
u32 __nx_applet_type = AppletType_Application;
size_t __nx_heap_size = 0;

PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance,
                                                        const char* name);
}

namespace {

constexpr const char* kReceiptPath =
    "sdmc:/switch/oot3dre/switch-vulkan-probe.txt";
constexpr const char* kReceiptTemporaryPath =
    "sdmc:/switch/oot3dre/switch-vulkan-probe.tmp";
constexpr uint32_t kDesiredImageCount = 3;
constexpr uint32_t kFramesInFlight = 3;

[[noreturn]] void Fail(std::string_view operation, VkResult result) {
    std::ostringstream message;
    message << operation << " failed with VkResult "
            << static_cast<int>(result);
    throw std::runtime_error(message.str());
}

void Check(VkResult result, std::string_view operation) {
    if (result != VK_SUCCESS) {
        Fail(operation, result);
    }
}

template <typename T>
void LoadInstanceFunction(T& destination, VkInstance instance,
                          const char* name) {
    destination = reinterpret_cast<T>(
        vk_icdGetInstanceProcAddr(instance, name));
    if (destination == nullptr) {
        throw std::runtime_error(std::string("missing Vulkan entrypoint: ") +
                                 name);
    }
}

template <typename T>
void LoadDeviceFunction(T& destination,
                        PFN_vkGetDeviceProcAddr get_device_proc_addr,
                        VkDevice device, const char* name) {
    destination =
        reinterpret_cast<T>(get_device_proc_addr(device, name));
    if (destination == nullptr) {
        throw std::runtime_error(std::string("missing Vulkan entrypoint: ") +
                                 name);
    }
}

void EnsureReceiptDirectory() {
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/oot3dre", 0777);
}

void WriteReceipt(std::string_view status, std::string_view payload) {
    EnsureReceiptDirectory();
    std::ofstream stream(kReceiptTemporaryPath, std::ios::trunc);
    if (!stream) {
        return;
    }
    stream << "schema=1\n";
    stream << "status=" << status << '\n';
    stream << payload;
    if (payload.empty() || payload.back() != '\n') {
        stream << '\n';
    }
    stream.flush();
    if (!stream) {
        return;
    }
    stream.close();
    std::remove(kReceiptPath);
    std::rename(kReceiptTemporaryPath, kReceiptPath);
}

const char* PresentModeName(VkPresentModeKHR mode) {
    switch (mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "mailbox";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return "fifo_relaxed";
    case VK_PRESENT_MODE_FIFO_KHR:
        return "fifo";
    default:
        return "unknown";
    }
}

struct VulkanFunctions {
    PFN_vkCreateInstance create_instance = nullptr;
    PFN_vkDestroyInstance destroy_instance = nullptr;
    PFN_vkEnumeratePhysicalDevices enumerate_physical_devices = nullptr;
    PFN_vkGetPhysicalDeviceProperties get_physical_device_properties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties
        get_physical_device_memory_properties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties
        get_physical_device_queue_family_properties = nullptr;
    PFN_vkCreateDevice create_device = nullptr;
    PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
    PFN_vkCreateViSurfaceNN create_vi_surface = nullptr;
    PFN_vkDestroySurfaceKHR destroy_surface = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR get_surface_support = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR get_surface_capabilities =
        nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR get_surface_formats = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR get_present_modes = nullptr;

    PFN_vkDestroyDevice destroy_device = nullptr;
    PFN_vkGetDeviceQueue get_device_queue = nullptr;
    PFN_vkDeviceWaitIdle device_wait_idle = nullptr;
    PFN_vkCreateSwapchainKHR create_swapchain = nullptr;
    PFN_vkDestroySwapchainKHR destroy_swapchain = nullptr;
    PFN_vkGetSwapchainImagesKHR get_swapchain_images = nullptr;
    PFN_vkAcquireNextImageKHR acquire_next_image = nullptr;
    PFN_vkQueuePresentKHR queue_present = nullptr;
    PFN_vkCreateCommandPool create_command_pool = nullptr;
    PFN_vkDestroyCommandPool destroy_command_pool = nullptr;
    PFN_vkAllocateCommandBuffers allocate_command_buffers = nullptr;
    PFN_vkBeginCommandBuffer begin_command_buffer = nullptr;
    PFN_vkEndCommandBuffer end_command_buffer = nullptr;
    PFN_vkResetCommandBuffer reset_command_buffer = nullptr;
    PFN_vkCmdPipelineBarrier cmd_pipeline_barrier = nullptr;
    PFN_vkCmdCopyBufferToImage cmd_copy_buffer_to_image = nullptr;
    PFN_vkCreateBuffer create_buffer = nullptr;
    PFN_vkDestroyBuffer destroy_buffer = nullptr;
    PFN_vkGetBufferMemoryRequirements get_buffer_memory_requirements = nullptr;
    PFN_vkAllocateMemory allocate_memory = nullptr;
    PFN_vkFreeMemory free_memory = nullptr;
    PFN_vkBindBufferMemory bind_buffer_memory = nullptr;
    PFN_vkMapMemory map_memory = nullptr;
    PFN_vkUnmapMemory unmap_memory = nullptr;
    PFN_vkFlushMappedMemoryRanges flush_mapped_memory_ranges = nullptr;
    PFN_vkCreateSemaphore create_semaphore = nullptr;
    PFN_vkDestroySemaphore destroy_semaphore = nullptr;
    PFN_vkCreateFence create_fence = nullptr;
    PFN_vkDestroyFence destroy_fence = nullptr;
    PFN_vkWaitForFences wait_for_fences = nullptr;
    PFN_vkResetFences reset_fences = nullptr;
    PFN_vkQueueSubmit queue_submit = nullptr;

    void LoadGlobal() {
        LoadInstanceFunction(create_instance, VK_NULL_HANDLE,
                             "vkCreateInstance");
    }

    void LoadInstance(VkInstance instance) {
#define OOT3D_LOAD_INSTANCE(member, function_name)                         \
    LoadInstanceFunction(member, instance, "vk" #function_name)
        OOT3D_LOAD_INSTANCE(destroy_instance, DestroyInstance);
        OOT3D_LOAD_INSTANCE(enumerate_physical_devices,
                            EnumeratePhysicalDevices);
        OOT3D_LOAD_INSTANCE(get_physical_device_properties,
                            GetPhysicalDeviceProperties);
        OOT3D_LOAD_INSTANCE(get_physical_device_memory_properties,
                            GetPhysicalDeviceMemoryProperties);
        OOT3D_LOAD_INSTANCE(get_physical_device_queue_family_properties,
                            GetPhysicalDeviceQueueFamilyProperties);
        OOT3D_LOAD_INSTANCE(create_device, CreateDevice);
        OOT3D_LOAD_INSTANCE(get_device_proc_addr, GetDeviceProcAddr);
        OOT3D_LOAD_INSTANCE(create_vi_surface, CreateViSurfaceNN);
        OOT3D_LOAD_INSTANCE(destroy_surface, DestroySurfaceKHR);
        OOT3D_LOAD_INSTANCE(get_surface_support,
                            GetPhysicalDeviceSurfaceSupportKHR);
        OOT3D_LOAD_INSTANCE(get_surface_capabilities,
                            GetPhysicalDeviceSurfaceCapabilitiesKHR);
        OOT3D_LOAD_INSTANCE(get_surface_formats,
                            GetPhysicalDeviceSurfaceFormatsKHR);
        OOT3D_LOAD_INSTANCE(get_present_modes,
                            GetPhysicalDeviceSurfacePresentModesKHR);
#undef OOT3D_LOAD_INSTANCE
    }

    void LoadDevice(VkDevice device) {
#define OOT3D_LOAD_DEVICE(member, function_name)                           \
    LoadDeviceFunction(member, get_device_proc_addr, device,               \
                       "vk" #function_name)
        OOT3D_LOAD_DEVICE(destroy_device, DestroyDevice);
        OOT3D_LOAD_DEVICE(get_device_queue, GetDeviceQueue);
        OOT3D_LOAD_DEVICE(device_wait_idle, DeviceWaitIdle);
        OOT3D_LOAD_DEVICE(create_swapchain, CreateSwapchainKHR);
        OOT3D_LOAD_DEVICE(destroy_swapchain, DestroySwapchainKHR);
        OOT3D_LOAD_DEVICE(get_swapchain_images, GetSwapchainImagesKHR);
        OOT3D_LOAD_DEVICE(acquire_next_image, AcquireNextImageKHR);
        OOT3D_LOAD_DEVICE(queue_present, QueuePresentKHR);
        OOT3D_LOAD_DEVICE(create_command_pool, CreateCommandPool);
        OOT3D_LOAD_DEVICE(destroy_command_pool, DestroyCommandPool);
        OOT3D_LOAD_DEVICE(allocate_command_buffers, AllocateCommandBuffers);
        OOT3D_LOAD_DEVICE(begin_command_buffer, BeginCommandBuffer);
        OOT3D_LOAD_DEVICE(end_command_buffer, EndCommandBuffer);
        OOT3D_LOAD_DEVICE(reset_command_buffer, ResetCommandBuffer);
        OOT3D_LOAD_DEVICE(cmd_pipeline_barrier, CmdPipelineBarrier);
        OOT3D_LOAD_DEVICE(cmd_copy_buffer_to_image, CmdCopyBufferToImage);
        OOT3D_LOAD_DEVICE(create_buffer, CreateBuffer);
        OOT3D_LOAD_DEVICE(destroy_buffer, DestroyBuffer);
        OOT3D_LOAD_DEVICE(get_buffer_memory_requirements,
                          GetBufferMemoryRequirements);
        OOT3D_LOAD_DEVICE(allocate_memory, AllocateMemory);
        OOT3D_LOAD_DEVICE(free_memory, FreeMemory);
        OOT3D_LOAD_DEVICE(bind_buffer_memory, BindBufferMemory);
        OOT3D_LOAD_DEVICE(map_memory, MapMemory);
        OOT3D_LOAD_DEVICE(unmap_memory, UnmapMemory);
        OOT3D_LOAD_DEVICE(flush_mapped_memory_ranges,
                          FlushMappedMemoryRanges);
        OOT3D_LOAD_DEVICE(create_semaphore, CreateSemaphore);
        OOT3D_LOAD_DEVICE(destroy_semaphore, DestroySemaphore);
        OOT3D_LOAD_DEVICE(create_fence, CreateFence);
        OOT3D_LOAD_DEVICE(destroy_fence, DestroyFence);
        OOT3D_LOAD_DEVICE(wait_for_fences, WaitForFences);
        OOT3D_LOAD_DEVICE(reset_fences, ResetFences);
        OOT3D_LOAD_DEVICE(queue_submit, QueueSubmit);
#undef OOT3D_LOAD_DEVICE
    }
};

struct StagingBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
};

struct FrameSync {
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore rendered = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
};

struct Telemetry {
    uint64_t attempted = 0;
    uint64_t presented = 0;
    uint64_t suboptimal = 0;
    uint64_t acquire_errors = 0;
    uint64_t submit_errors = 0;
    uint64_t present_errors = 0;
    uint64_t acquire_ticks = 0;
    uint64_t submit_ticks = 0;
    uint64_t present_ticks = 0;
    uint64_t start_tick = 0;
};

class Probe {
public:
    Probe() = default;
    Probe(const Probe&) = delete;
    Probe& operator=(const Probe&) = delete;

    ~Probe() { Shutdown(); }

    void Initialize() {
        if (setenv("NVK_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1) != 0) {
            throw std::runtime_error("could not enable the explicit NVK opt-in");
        }

        functions_.LoadGlobal();
        CreateInstance();
        functions_.LoadInstance(instance_);
        CreateSurface();
        SelectPhysicalDeviceAndQueue();
        CreateDevice();
        functions_.LoadDevice(device_);
        functions_.get_device_queue(device_, queue_family_, 0, &queue_);
        CreateSwapchain();
        CreateCommandResources();
        CreateStagingBuffers();
        image_initialized_.assign(images_.size(), false);
    }

    std::string Run() {
        PadState pad{};
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);

        telemetry_.start_tick = armGetSystemTick();
        WriteReceipt("running", ReceiptPayload());

        std::string exit_reason = "applet_exit";
        while (appletMainLoop()) {
            padUpdate(&pad);
            if ((padGetButtonsDown(&pad) & HidNpadButton_Plus) != 0) {
                exit_reason = "plus";
                break;
            }

            const uint32_t slot =
                static_cast<uint32_t>(telemetry_.attempted % kFramesInFlight);
            ++telemetry_.attempted;
            FrameSync& frame = frames_[slot];

            Check(functions_.wait_for_fences(device_, 1, &frame.fence,
                                              VK_TRUE,
                                              std::numeric_limits<uint64_t>::max()),
                  "vkWaitForFences");

            uint32_t image_index = 0;
            const uint64_t acquire_start = armGetSystemTick();
            VkResult result = functions_.acquire_next_image(
                device_, swapchain_, std::numeric_limits<uint64_t>::max(),
                frame.acquired, VK_NULL_HANDLE, &image_index);
            telemetry_.acquire_ticks += armGetSystemTick() - acquire_start;
            if (result == VK_ERROR_OUT_OF_DATE_KHR) {
                ++telemetry_.acquire_errors;
                exit_reason = "acquire_out_of_date";
                break;
            }
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
                ++telemetry_.acquire_errors;
                Fail("vkAcquireNextImageKHR", result);
            }
            if (result == VK_SUBOPTIMAL_KHR) {
                ++telemetry_.suboptimal;
            }

            Check(functions_.reset_fences(device_, 1, &frame.fence),
                  "vkResetFences");
            Check(functions_.reset_command_buffer(frame.command, 0),
                  "vkResetCommandBuffer");
            RecordCopy(frame.command, slot, image_index);

            const VkPipelineStageFlags wait_stage =
                VK_PIPELINE_STAGE_TRANSFER_BIT;
            const VkSubmitInfo submit_info{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = nullptr,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &frame.acquired,
                .pWaitDstStageMask = &wait_stage,
                .commandBufferCount = 1,
                .pCommandBuffers = &frame.command,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &frame.rendered,
            };
            const uint64_t submit_start = armGetSystemTick();
            result = functions_.queue_submit(queue_, 1, &submit_info,
                                             frame.fence);
            telemetry_.submit_ticks += armGetSystemTick() - submit_start;
            if (result != VK_SUCCESS) {
                ++telemetry_.submit_errors;
                Fail("vkQueueSubmit", result);
            }

            const VkPresentInfoKHR present_info{
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .pNext = nullptr,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &frame.rendered,
                .swapchainCount = 1,
                .pSwapchains = &swapchain_,
                .pImageIndices = &image_index,
                .pResults = nullptr,
            };
            const uint64_t present_start = armGetSystemTick();
            result = functions_.queue_present(queue_, &present_info);
            telemetry_.present_ticks += armGetSystemTick() - present_start;
            if (result == VK_ERROR_OUT_OF_DATE_KHR) {
                ++telemetry_.present_errors;
                exit_reason = "present_out_of_date";
                break;
            }
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
                ++telemetry_.present_errors;
                Fail("vkQueuePresentKHR", result);
            }
            if (result == VK_SUBOPTIMAL_KHR) {
                ++telemetry_.suboptimal;
            }
            ++telemetry_.presented;

            if (telemetry_.presented == 300 ||
                (telemetry_.presented % 1800) == 0) {
                WriteReceipt("running", ReceiptPayload());
            }
        }

        if (device_ != VK_NULL_HANDLE && functions_.device_wait_idle != nullptr) {
            Check(functions_.device_wait_idle(device_), "vkDeviceWaitIdle");
        }
        return exit_reason;
    }

    std::string ReceiptPayload(std::string_view exit_reason = {}) const {
        const uint64_t now = armGetSystemTick();
        const uint64_t elapsed_ticks =
            telemetry_.start_tick == 0 ? 0 : now - telemetry_.start_tick;
        const double seconds =
            static_cast<double>(armTicksToNs(elapsed_ticks)) / 1.0e9;
        const double fps = seconds > 0.0
                               ? static_cast<double>(telemetry_.presented) /
                                     seconds
                               : 0.0;
        const auto average_microseconds = [](uint64_t ticks,
                                             uint64_t samples) {
            if (samples == 0) {
                return 0.0;
            }
            return static_cast<double>(armTicksToNs(ticks)) /
                   (1000.0 * static_cast<double>(samples));
        };

        std::ostringstream payload;
        payload << "backend=vulkan\n";
        payload << "driver_contract=nxvk_static_icd\n";
        payload << "device=" << device_name_ << '\n';
        payload << "extent=" << extent_.width << 'x' << extent_.height
                << '\n';
        payload << "surface_format=" << static_cast<int>(surface_format_.format)
                << '\n';
        payload << "present_mode=" << PresentModeName(present_mode_) << '\n';
        payload << "application_limiter=0\n";
        payload << "fifo_fallback="
                << (present_mode_ == VK_PRESENT_MODE_FIFO_KHR ? 1 : 0) << '\n';
        payload << "requested_images=" << requested_image_count_ << '\n';
        payload << "swapchain_images=" << images_.size() << '\n';
        payload << "frames_in_flight=" << kFramesInFlight << '\n';
        payload << "attempted=" << telemetry_.attempted << '\n';
        payload << "presented=" << telemetry_.presented << '\n';
        payload << "suboptimal=" << telemetry_.suboptimal << '\n';
        payload << "acquire_errors=" << telemetry_.acquire_errors << '\n';
        payload << "submit_errors=" << telemetry_.submit_errors << '\n';
        payload << "present_errors=" << telemetry_.present_errors << '\n';
        payload << std::fixed << std::setprecision(3);
        payload << "elapsed_seconds=" << seconds << '\n';
        payload << "presented_fps=" << fps << '\n';
        payload << "avg_acquire_us="
                << average_microseconds(telemetry_.acquire_ticks,
                                        telemetry_.attempted)
                << '\n';
        payload << "avg_submit_us="
                << average_microseconds(telemetry_.submit_ticks,
                                        telemetry_.presented)
                << '\n';
        payload << "avg_present_us="
                << average_microseconds(telemetry_.present_ticks,
                                        telemetry_.presented)
                << '\n';
        payload << "orientation=tl_red,tr_green,bl_blue,br_yellow,cyan_diagonal\n";
        if (!exit_reason.empty()) {
            payload << "exit_reason=" << exit_reason << '\n';
        }
        return payload.str();
    }

private:
    void CreateInstance() {
        const VkApplicationInfo application_info{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "OoT3D Switch Vulkan probe",
            .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .pEngineName = "standalone WSI probe",
            .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .apiVersion = VK_API_VERSION_1_0,
        };
        constexpr std::array<const char*, 2> extensions{
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_NN_VI_SURFACE_EXTENSION_NAME,
        };
        const VkInstanceCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = &application_info,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount =
                static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
        };
        Check(functions_.create_instance(&create_info, nullptr, &instance_),
              "vkCreateInstance");
    }

    void CreateSurface() {
        const VkViSurfaceCreateInfoNN create_info{
            .sType = VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN,
            .pNext = nullptr,
            .flags = 0,
            .window = nwindowGetDefault(),
        };
        Check(functions_.create_vi_surface(instance_, &create_info, nullptr,
                                           &surface_),
              "vkCreateViSurfaceNN");
    }

    void SelectPhysicalDeviceAndQueue() {
        uint32_t physical_device_count = 0;
        Check(functions_.enumerate_physical_devices(
                  instance_, &physical_device_count, nullptr),
              "vkEnumeratePhysicalDevices(count)");
        if (physical_device_count == 0) {
            throw std::runtime_error("Vulkan reported no physical device");
        }
        std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
        Check(functions_.enumerate_physical_devices(
                  instance_, &physical_device_count, physical_devices.data()),
              "vkEnumeratePhysicalDevices(data)");

        for (VkPhysicalDevice candidate : physical_devices) {
            uint32_t queue_count = 0;
            functions_.get_physical_device_queue_family_properties(
                candidate, &queue_count, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queue_count);
            functions_.get_physical_device_queue_family_properties(
                candidate, &queue_count, queues.data());
            for (uint32_t queue_index = 0; queue_index < queue_count;
                 ++queue_index) {
                VkBool32 supports_present = VK_FALSE;
                Check(functions_.get_surface_support(
                          candidate, queue_index, surface_, &supports_present),
                      "vkGetPhysicalDeviceSurfaceSupportKHR");
                if ((queues[queue_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) !=
                        0 &&
                    supports_present == VK_TRUE) {
                    physical_device_ = candidate;
                    queue_family_ = queue_index;
                    VkPhysicalDeviceProperties properties{};
                    functions_.get_physical_device_properties(
                        physical_device_, &properties);
                    device_name_ = properties.deviceName;
                    return;
                }
            }
        }
        throw std::runtime_error(
            "no queue family supports both graphics/transfer and VI present");
    }

    void CreateDevice() {
        constexpr float priority = 1.0F;
        const VkDeviceQueueCreateInfo queue_info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = queue_family_,
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
        constexpr std::array<const char*, 1> extensions{
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };
        const VkDeviceCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queue_info,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount =
                static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
            .pEnabledFeatures = nullptr,
        };
        Check(functions_.create_device(physical_device_, &create_info, nullptr,
                                       &device_),
              "vkCreateDevice");
    }

    void CreateSwapchain() {
        VkSurfaceCapabilitiesKHR capabilities{};
        Check(functions_.get_surface_capabilities(
                  physical_device_, surface_, &capabilities),
              "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        if ((capabilities.supportedUsageFlags &
             VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
            throw std::runtime_error(
                "VI swapchain images do not support transfer destination use");
        }

        extent_ = capabilities.currentExtent;
        if (extent_.width == std::numeric_limits<uint32_t>::max()) {
            extent_.width = std::clamp(1280U,
                                       capabilities.minImageExtent.width,
                                       capabilities.maxImageExtent.width);
            extent_.height = std::clamp(720U,
                                        capabilities.minImageExtent.height,
                                        capabilities.maxImageExtent.height);
        }

        uint32_t format_count = 0;
        Check(functions_.get_surface_formats(physical_device_, surface_,
                                             &format_count, nullptr),
              "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
        if (format_count == 0) {
            throw std::runtime_error("VI surface reported no formats");
        }
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        Check(functions_.get_surface_formats(physical_device_, surface_,
                                             &format_count, formats.data()),
              "vkGetPhysicalDeviceSurfaceFormatsKHR(data)");
        surface_format_ = formats.front();
        if (format_count == 1 &&
            surface_format_.format == VK_FORMAT_UNDEFINED) {
            surface_format_.format = VK_FORMAT_R8G8B8A8_UNORM;
        }
        for (const VkSurfaceFormatKHR& format : formats) {
            if (format.format == VK_FORMAT_R8G8B8A8_UNORM ||
                format.format == VK_FORMAT_B8G8R8A8_UNORM) {
                surface_format_ = format;
                break;
            }
        }
        if (surface_format_.format != VK_FORMAT_R8G8B8A8_UNORM &&
            surface_format_.format != VK_FORMAT_B8G8R8A8_UNORM) {
            throw std::runtime_error(
                "VI surface has no supported 32-bit RGBA/BGRA probe format");
        }

        uint32_t mode_count = 0;
        Check(functions_.get_present_modes(physical_device_, surface_,
                                           &mode_count, nullptr),
              "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
        std::vector<VkPresentModeKHR> modes(mode_count);
        if (mode_count != 0) {
            Check(functions_.get_present_modes(physical_device_, surface_,
                                               &mode_count, modes.data()),
                  "vkGetPhysicalDeviceSurfacePresentModesKHR(data)");
        }
        present_mode_ = SelectPresentMode(modes);

        requested_image_count_ =
            std::max(kDesiredImageCount, capabilities.minImageCount);
        if (capabilities.maxImageCount != 0) {
            requested_image_count_ =
                std::min(requested_image_count_, capabilities.maxImageCount);
        }

        VkSurfaceTransformFlagBitsKHR transform = capabilities.currentTransform;
        if ((capabilities.supportedTransforms &
             VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0) {
            transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        }
        const VkCompositeAlphaFlagBitsKHR composite_alpha =
            SelectCompositeAlpha(capabilities.supportedCompositeAlpha);

        const VkSwapchainCreateInfoKHR create_info{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .surface = surface_,
            .minImageCount = requested_image_count_,
            .imageFormat = surface_format_.format,
            .imageColorSpace = surface_format_.colorSpace,
            .imageExtent = extent_,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = transform,
            .compositeAlpha = composite_alpha,
            .presentMode = present_mode_,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE,
        };
        Check(functions_.create_swapchain(device_, &create_info, nullptr,
                                          &swapchain_),
              "vkCreateSwapchainKHR");

        uint32_t image_count = 0;
        Check(functions_.get_swapchain_images(device_, swapchain_, &image_count,
                                              nullptr),
              "vkGetSwapchainImagesKHR(count)");
        if (image_count == 0) {
            throw std::runtime_error("swapchain has no images");
        }
        images_.resize(image_count);
        Check(functions_.get_swapchain_images(device_, swapchain_, &image_count,
                                              images_.data()),
              "vkGetSwapchainImagesKHR(data)");
        images_.resize(image_count);
    }

    static VkPresentModeKHR SelectPresentMode(
        const std::vector<VkPresentModeKHR>& modes) {
        constexpr std::array<VkPresentModeKHR, 4> preference{
            VK_PRESENT_MODE_IMMEDIATE_KHR,
            VK_PRESENT_MODE_MAILBOX_KHR,
            VK_PRESENT_MODE_FIFO_RELAXED_KHR,
            VK_PRESENT_MODE_FIFO_KHR,
        };
        for (VkPresentModeKHR desired : preference) {
            if (std::find(modes.begin(), modes.end(), desired) != modes.end()) {
                return desired;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    static VkCompositeAlphaFlagBitsKHR SelectCompositeAlpha(
        VkCompositeAlphaFlagsKHR supported) {
        constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> preference{
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };
        for (VkCompositeAlphaFlagBitsKHR desired : preference) {
            if ((supported & desired) != 0) {
                return desired;
            }
        }
        throw std::runtime_error("VI surface has no composite alpha mode");
    }

    void CreateCommandResources() {
        const VkCommandPoolCreateInfo pool_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = queue_family_,
        };
        Check(functions_.create_command_pool(device_, &pool_info, nullptr,
                                             &command_pool_),
              "vkCreateCommandPool");

        std::array<VkCommandBuffer, kFramesInFlight> commands{};
        const VkCommandBufferAllocateInfo allocate_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = command_pool_,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = kFramesInFlight,
        };
        Check(functions_.allocate_command_buffers(device_, &allocate_info,
                                                  commands.data()),
              "vkAllocateCommandBuffers");

        for (uint32_t index = 0; index < kFramesInFlight; ++index) {
            frames_[index].command = commands[index];
            const VkSemaphoreCreateInfo semaphore_info{
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
            };
            Check(functions_.create_semaphore(device_, &semaphore_info, nullptr,
                                              &frames_[index].acquired),
                  "vkCreateSemaphore(acquired)");
            Check(functions_.create_semaphore(device_, &semaphore_info, nullptr,
                                              &frames_[index].rendered),
                  "vkCreateSemaphore(rendered)");
            const VkFenceCreateInfo fence_info{
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .pNext = nullptr,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT,
            };
            Check(functions_.create_fence(device_, &fence_info, nullptr,
                                          &frames_[index].fence),
                  "vkCreateFence");
        }
    }

    void CreateStagingBuffers() {
        const uint64_t pixel_count =
            static_cast<uint64_t>(extent_.width) * extent_.height;
        if (pixel_count == 0 ||
            pixel_count > std::numeric_limits<size_t>::max() /
                              sizeof(uint32_t)) {
            throw std::runtime_error("invalid swapchain extent for staging");
        }
        staging_size_ = pixel_count * sizeof(uint32_t);

        VkPhysicalDeviceMemoryProperties memory_properties{};
        functions_.get_physical_device_memory_properties(
            physical_device_, &memory_properties);

        for (uint32_t index = 0; index < kFramesInFlight; ++index) {
            StagingBuffer& staging = staging_[index];
            const VkBufferCreateInfo buffer_info{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .size = staging_size_,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
            };
            Check(functions_.create_buffer(device_, &buffer_info, nullptr,
                                           &staging.buffer),
                  "vkCreateBuffer");

            VkMemoryRequirements requirements{};
            functions_.get_buffer_memory_requirements(device_, staging.buffer,
                                                       &requirements);
            const uint32_t memory_type = FindHostMemoryType(
                requirements.memoryTypeBits, memory_properties);
            const VkMemoryPropertyFlags flags =
                memory_properties.memoryTypes[memory_type].propertyFlags;
            const VkMemoryAllocateInfo allocate_info{
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .pNext = nullptr,
                .allocationSize = requirements.size,
                .memoryTypeIndex = memory_type,
            };
            Check(functions_.allocate_memory(device_, &allocate_info, nullptr,
                                             &staging.memory),
                  "vkAllocateMemory");
            Check(functions_.bind_buffer_memory(device_, staging.buffer,
                                                staging.memory, 0),
                  "vkBindBufferMemory");
            Check(functions_.map_memory(device_, staging.memory, 0,
                                        VK_WHOLE_SIZE, 0, &staging.mapped),
                  "vkMapMemory");

            std::vector<uint32_t> pattern(
                static_cast<size_t>(pixel_count));
            BuildOrientationPattern(pattern, index);
            std::memcpy(staging.mapped, pattern.data(),
                        static_cast<size_t>(staging_size_));
            if ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
                const VkMappedMemoryRange range{
                    .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                    .pNext = nullptr,
                    .memory = staging.memory,
                    .offset = 0,
                    .size = VK_WHOLE_SIZE,
                };
                Check(functions_.flush_mapped_memory_ranges(device_, 1, &range),
                      "vkFlushMappedMemoryRanges");
            }
        }
    }

    static uint32_t FindHostMemoryType(
        uint32_t allowed,
        const VkPhysicalDeviceMemoryProperties& properties) {
        for (uint32_t pass = 0; pass < 2; ++pass) {
            for (uint32_t index = 0; index < properties.memoryTypeCount;
                 ++index) {
                if ((allowed & (1U << index)) == 0) {
                    continue;
                }
                const VkMemoryPropertyFlags flags =
                    properties.memoryTypes[index].propertyFlags;
                if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
                    continue;
                }
                if (pass == 0 &&
                    (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
                    continue;
                }
                return index;
            }
        }
        throw std::runtime_error("no host-visible Vulkan memory type");
    }

    uint32_t PackColor(uint8_t red, uint8_t green, uint8_t blue) const {
        if (surface_format_.format == VK_FORMAT_B8G8R8A8_UNORM) {
            return 0xFF000000U | (static_cast<uint32_t>(red) << 16U) |
                   (static_cast<uint32_t>(green) << 8U) | blue;
        }
        return 0xFF000000U | (static_cast<uint32_t>(blue) << 16U) |
               (static_cast<uint32_t>(green) << 8U) | red;
    }

    void BuildOrientationPattern(std::vector<uint32_t>& pixels,
                                 uint32_t phase) const {
        const uint32_t width = extent_.width;
        const uint32_t height = extent_.height;
        const uint32_t border = std::max(4U, std::min(width, height) / 96U);
        const uint32_t marker_width = std::max(12U, width / 48U);
        const uint32_t marker_center = (phase + 1U) * width / 4U;

        const uint32_t red = PackColor(235, 45, 45);
        const uint32_t green = PackColor(45, 210, 75);
        const uint32_t blue = PackColor(45, 90, 235);
        const uint32_t yellow = PackColor(235, 210, 40);
        const uint32_t white = PackColor(245, 245, 245);
        const uint32_t black = PackColor(8, 8, 12);
        const uint32_t cyan = PackColor(20, 235, 235);
        constexpr std::array<std::array<uint8_t, 3>, 3> marker_colors{{
            {{255, 70, 210}},
            {{255, 255, 255}},
            {{255, 130, 20}},
        }};
        const auto& marker_rgb = marker_colors[phase % marker_colors.size()];
        const uint32_t marker =
            PackColor(marker_rgb[0], marker_rgb[1], marker_rgb[2]);

        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t color = y < height / 2U
                                     ? (x < width / 2U ? red : green)
                                     : (x < width / 2U ? blue : yellow);
                if (x < border || x >= width - border || y < border ||
                    y >= height - border) {
                    color = white;
                }
                if (y >= height - 3U * border && y < height - border) {
                    color = black;
                }

                const uint64_t diagonal_x =
                    (static_cast<uint64_t>(y) * width) / height;
                if (std::llabs(static_cast<long long>(x) -
                               static_cast<long long>(diagonal_x)) <=
                    static_cast<long long>(border)) {
                    color = cyan;
                }

                if (y > height / 3U && y < (2U * height) / 3U &&
                    x + marker_width >= marker_center &&
                    x <= marker_center + marker_width) {
                    color = marker;
                }

                const uint32_t arrow_tip = height / 16U;
                const uint32_t arrow_base = height / 4U;
                if (y >= arrow_tip && y <= arrow_base) {
                    const uint32_t half_width =
                        ((y - arrow_tip) * width) / (6U * height);
                    const uint32_t center = width / 2U;
                    if (x + half_width >= center && x <= center + half_width) {
                        color = white;
                    }
                }
                pixels[static_cast<size_t>(y) * width + x] = color;
            }
        }
    }

    void RecordCopy(VkCommandBuffer command, uint32_t slot,
                    uint32_t image_index) {
        const VkCommandBufferBeginInfo begin_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };
        Check(functions_.begin_command_buffer(command, &begin_info),
              "vkBeginCommandBuffer");

        const VkImageSubresourceRange range{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        const bool initialized = image_initialized_[image_index];
        const VkImageMemoryBarrier to_transfer{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = initialized ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                     : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images_[image_index],
            .subresourceRange = range,
        };
        functions_.cmd_pipeline_barrier(
            command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
            &to_transfer);

        const VkBufferImageCopy copy{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .imageOffset = {0, 0, 0},
            .imageExtent = {extent_.width, extent_.height, 1},
        };
        functions_.cmd_copy_buffer_to_image(
            command, staging_[slot].buffer, images_[image_index],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        const VkImageMemoryBarrier to_present{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images_[image_index],
            .subresourceRange = range,
        };
        functions_.cmd_pipeline_barrier(
            command, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
            &to_present);
        Check(functions_.end_command_buffer(command), "vkEndCommandBuffer");
        image_initialized_[image_index] = true;
    }

    void Shutdown() noexcept {
        if (device_ != VK_NULL_HANDLE && functions_.device_wait_idle != nullptr) {
            functions_.device_wait_idle(device_);
        }
        if (device_ != VK_NULL_HANDLE) {
            for (StagingBuffer& staging : staging_) {
                if (staging.mapped != nullptr && functions_.unmap_memory != nullptr) {
                    functions_.unmap_memory(device_, staging.memory);
                    staging.mapped = nullptr;
                }
                if (staging.buffer != VK_NULL_HANDLE &&
                    functions_.destroy_buffer != nullptr) {
                    functions_.destroy_buffer(device_, staging.buffer, nullptr);
                    staging.buffer = VK_NULL_HANDLE;
                }
                if (staging.memory != VK_NULL_HANDLE &&
                    functions_.free_memory != nullptr) {
                    functions_.free_memory(device_, staging.memory, nullptr);
                    staging.memory = VK_NULL_HANDLE;
                }
            }
            for (FrameSync& frame : frames_) {
                if (frame.acquired != VK_NULL_HANDLE &&
                    functions_.destroy_semaphore != nullptr) {
                    functions_.destroy_semaphore(device_, frame.acquired,
                                                 nullptr);
                    frame.acquired = VK_NULL_HANDLE;
                }
                if (frame.rendered != VK_NULL_HANDLE &&
                    functions_.destroy_semaphore != nullptr) {
                    functions_.destroy_semaphore(device_, frame.rendered,
                                                 nullptr);
                    frame.rendered = VK_NULL_HANDLE;
                }
                if (frame.fence != VK_NULL_HANDLE &&
                    functions_.destroy_fence != nullptr) {
                    functions_.destroy_fence(device_, frame.fence, nullptr);
                    frame.fence = VK_NULL_HANDLE;
                }
            }
            if (command_pool_ != VK_NULL_HANDLE &&
                functions_.destroy_command_pool != nullptr) {
                functions_.destroy_command_pool(device_, command_pool_, nullptr);
                command_pool_ = VK_NULL_HANDLE;
            }
            if (swapchain_ != VK_NULL_HANDLE &&
                functions_.destroy_swapchain != nullptr) {
                functions_.destroy_swapchain(device_, swapchain_, nullptr);
                swapchain_ = VK_NULL_HANDLE;
            }
            if (functions_.destroy_device != nullptr) {
                functions_.destroy_device(device_, nullptr);
            }
            device_ = VK_NULL_HANDLE;
        }
        if (surface_ != VK_NULL_HANDLE && functions_.destroy_surface != nullptr) {
            functions_.destroy_surface(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE &&
            functions_.destroy_instance != nullptr) {
            functions_.destroy_instance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

    VulkanFunctions functions_{};
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;
    uint32_t requested_image_count_ = 0;
    VkExtent2D extent_{};
    VkSurfaceFormatKHR surface_format_{};
    VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkDeviceSize staging_size_ = 0;
    std::string device_name_ = "unknown";
    std::vector<VkImage> images_;
    std::vector<bool> image_initialized_;
    std::array<FrameSync, kFramesInFlight> frames_{};
    std::array<StagingBuffer, kFramesInFlight> staging_{};
    Telemetry telemetry_{};
};

} // namespace

int main() {
    try {
        Probe probe;
        probe.Initialize();
        const std::string exit_reason = probe.Run();
        WriteReceipt("complete", probe.ReceiptPayload(exit_reason));
        return 0;
    } catch (const std::exception& exception) {
        WriteReceipt("failed", std::string("detail=") + exception.what());
        return 1;
    }
}
