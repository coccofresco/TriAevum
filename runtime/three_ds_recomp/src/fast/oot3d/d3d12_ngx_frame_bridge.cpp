#include "d3d12_ngx_frame_bridge.h"

#if defined(_WIN32) && defined(ENABLE_OOT3D_VULKAN)

#include <NRI.h>
#include <Extensions/NRIUpscaler.h>
#include <Extensions/NRIWrapperD3D12.h>

#include <d3d12.h>
#include <dxgiformat.h>
#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan_win32.h>
#include <wrl/client.h>

#ifdef ERROR
#undef ERROR
#endif

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace Fast::Oot3d {
namespace {

using Microsoft::WRL::ComPtr;

constexpr uint32_t kInputResourceCount = 4U;
constexpr uint32_t kSharedResourceCount = 5U;
constexpr DWORD kFenceTimeoutMilliseconds = 10000U;

std::string FormatHresult(HRESULT value) {
    std::ostringstream text;
    text << "HRESULT 0x" << std::hex << std::setw(8) << std::setfill('0') << static_cast<uint32_t>(value);
    return text.str();
}

std::string FormatVkResult(VkResult value) {
    return "VkResult " + std::to_string(static_cast<int32_t>(value));
}

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t allowedTypes, VkMemoryPropertyFlags preferredFlags) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    uint32_t fallback = std::numeric_limits<uint32_t>::max();
    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((allowedTypes & (1U << index)) == 0U)
            continue;
        if (fallback == std::numeric_limits<uint32_t>::max())
            fallback = index;
        if ((properties.memoryTypes[index].propertyFlags & preferredFlags) == preferredFlags)
            return index;
    }
    return fallback;
}

bool WaitForFence(ID3D12Fence& fence, uint64_t value, std::string& error) {
    if (fence.GetCompletedValue() >= value)
        return true;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr) {
        error = "CreateEventW failed for the shared D3D12 fence";
        return false;
    }
    const HRESULT hr = fence.SetEventOnCompletion(value, event);
    const DWORD wait = SUCCEEDED(hr) ? WaitForSingleObject(event, kFenceTimeoutMilliseconds) : WAIT_FAILED;
    CloseHandle(event);
    if (FAILED(hr)) {
        error = "ID3D12Fence::SetEventOnCompletion failed: " + FormatHresult(hr);
        return false;
    }
    if (wait != WAIT_OBJECT_0) {
        error = "shared D3D12 fence wait timed out";
        return false;
    }
    return true;
}

nri::UpscalerMode ToNriUpscalerMode(UpscalerQuality quality) {
    switch (quality) {
        case UpscalerQuality::Native:
            return nri::UpscalerMode::NATIVE;
        case UpscalerQuality::UltraQuality:
            return nri::UpscalerMode::ULTRA_QUALITY;
        case UpscalerQuality::Quality:
            return nri::UpscalerMode::QUALITY;
        case UpscalerQuality::Balanced:
            return nri::UpscalerMode::BALANCED;
        case UpscalerQuality::Performance:
            return nri::UpscalerMode::PERFORMANCE;
        case UpscalerQuality::UltraPerformance:
            return nri::UpscalerMode::ULTRA_PERFORMANCE;
    }
    return nri::UpscalerMode::QUALITY;
}

struct SharedFormat {
    VkFormat Vulkan = VK_FORMAT_UNDEFINED;
    DXGI_FORMAT D3d12 = DXGI_FORMAT_UNKNOWN;
    nri::Format Nri = nri::Format::UNKNOWN;
};

constexpr SharedFormat kColorFormat{ VK_FORMAT_R16G16B16A16_SFLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                     nri::Format::RGBA16_SFLOAT };
constexpr SharedFormat kDepthFormat{ VK_FORMAT_R32_SFLOAT, DXGI_FORMAT_R32_FLOAT, nri::Format::R32_SFLOAT };
constexpr SharedFormat kMotionFormat{ VK_FORMAT_R16G16_SFLOAT, DXGI_FORMAT_R16G16_FLOAT, nri::Format::RG16_SFLOAT };
constexpr SharedFormat kReactiveFormat{ VK_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, nri::Format::R8_UNORM };

const char* BuildInputConversionShader() {
    return R"glsl(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(binding = 0) uniform sampler2D sourceColor;
layout(binding = 1) uniform sampler2D sourceDepth;
layout(binding = 2) uniform sampler2D sourceMotion;
layout(binding = 3) uniform sampler2D sourceReactive;
layout(binding = 4, rgba16f) uniform writeonly image2D bridgeColor;
layout(binding = 5, r32f) uniform writeonly image2D bridgeDepth;
layout(binding = 6, rg16f) uniform writeonly image2D bridgeMotion;
layout(binding = 7, r8) uniform writeonly image2D bridgeReactive;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 extent = imageSize(bridgeColor);
    if (any(greaterThanEqual(pixel, extent))) return;
    imageStore(bridgeColor, pixel, texelFetch(sourceColor, pixel, 0));
    imageStore(bridgeDepth, pixel,
               vec4(texelFetch(sourceDepth, pixel, 0).x, 0.0, 0.0, 1.0));
    imageStore(bridgeMotion, pixel,
               vec4(texelFetch(sourceMotion, pixel, 0).xy, 0.0, 1.0));
    imageStore(bridgeReactive, pixel,
               vec4(clamp(texelFetch(sourceReactive, pixel, 0).x,
                          0.0, 1.0), 0.0, 0.0, 1.0));
}
)glsl";
}

bool CompileComputeShader(std::vector<uint32_t>& spirv, std::string& error) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto result = compiler.CompileGlslToSpv(BuildInputConversionShader(), shaderc_compute_shader,
                                                  "oot3d_d3d12_ngx_frame_bridge.comp", options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        error = "D3D12 NGX bridge shader compilation failed: " + result.GetErrorMessage();
        return false;
    }
    spirv.assign(result.cbegin(), result.cend());
    return true;
}

} // namespace

bool D3d12NgxFrameContract::Valid() const noexcept {
    return InputWidth != 0U && InputHeight != 0U && OutputWidth != 0U && OutputHeight != 0U && FrameSlotCount != 0U &&
           FrameSlotCount <= 8U;
}

bool D3d12NgxFrameInputs::Valid() const noexcept {
    return Color != VK_NULL_HANDLE && Depth != VK_NULL_HANDLE && Motion != VK_NULL_HANDLE &&
           Reactive != VK_NULL_HANDLE && ColorLayout != VK_IMAGE_LAYOUT_UNDEFINED &&
           DepthLayout != VK_IMAGE_LAYOUT_UNDEFINED && MotionLayout != VK_IMAGE_LAYOUT_UNDEFINED &&
           ReactiveLayout != VK_IMAGE_LAYOUT_UNDEFINED;
}

bool D3d12NgxFrameSynchronization::Valid() const noexcept {
    return Semaphore != VK_NULL_HANDLE && VulkanInputsReady != 0U && D3d12OutputReady > VulkanInputsReady &&
           Serial != 0U;
}

struct D3d12NgxFrameBridge::Impl {
    struct SharedImage {
        VkImage Image = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        ComPtr<ID3D12Resource> D3d12Resource;
        nri::Texture* NriTexture = nullptr;
        nri::Descriptor* NriDescriptor = nullptr;
        SharedFormat Format{};
        uint32_t Width = 0;
        uint32_t Height = 0;
        bool VulkanLayoutInitialized = false;
    };

    struct Frame {
        std::array<SharedImage, kInputResourceCount> Inputs;
        SharedImage Output;
        VkDescriptorSet ConversionSet = VK_NULL_HANDLE;
        nri::CommandAllocator* CommandAllocator = nullptr;
        nri::CommandBuffer* CommandBuffer = nullptr;
        ID3D12GraphicsCommandList* NativeCommandList = nullptr;
        uint64_t PreparedSerial = 0U;
        uint64_t QueuedSerial = 0U;
        bool OutputOwnedByVulkan = false;
    };

    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice VulkanDevice = VK_NULL_HANDLE;
    uint32_t GraphicsQueueFamily = 0U;
    ID3D12Device* D3d12Device = nullptr;
    ID3D12CommandQueue* D3d12Queue = nullptr;
    nri::Device* NriDevice = nullptr;
    const nri::CoreInterface* Core = nullptr;
    const nri::UpscalerInterface* UpscalerInterface = nullptr;
    nri::WrapperD3D12Interface Wrapper{};
    nri::Queue* NriQueue = nullptr;
    nri::Upscaler* Upscaler = nullptr;

    ComPtr<ID3D12Fence> SharedFence;
    VkSemaphore SharedSemaphore = VK_NULL_HANDLE;
    uint64_t NextFenceValue = 3U;
    uint64_t LastQueuedFenceValue = 2U;
    uint64_t NextSerial = 1U;

    VkSampler ConversionSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout ConversionSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool ConversionPool = VK_NULL_HANDLE;
    VkPipelineLayout ConversionPipelineLayout = VK_NULL_HANDLE;
    VkPipeline ConversionPipeline = VK_NULL_HANDLE;

    D3d12NgxFrameContract Contract{};
    std::vector<Frame> Frames;
    bool ExternalMemoryReady = false;
    bool SharedFenceReady = false;
    std::string Reason = "D3D12 NGX frame bridge is not initialized";

    bool CreateSharedFence() {
        HRESULT hr = D3d12Device->CreateFence(0U, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&SharedFence));
        if (FAILED(hr)) {
            Reason = "D3D12 frame bridge fence creation failed: " + FormatHresult(hr);
            return false;
        }
        HANDLE handle = nullptr;
        hr = D3d12Device->CreateSharedHandle(SharedFence.Get(), nullptr, GENERIC_ALL, nullptr, &handle);
        if (FAILED(hr) || handle == nullptr) {
            Reason = "D3D12 frame bridge fence export failed: " + FormatHresult(hr);
            return false;
        }

        VkSemaphoreTypeCreateInfo timeline{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
        timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timeline.initialValue = 0U;
        VkSemaphoreCreateInfo create{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        create.pNext = &timeline;
        VkResult result = vkCreateSemaphore(VulkanDevice, &create, nullptr, &SharedSemaphore);
        if (result != VK_SUCCESS) {
            CloseHandle(handle);
            Reason = "Vulkan frame bridge timeline creation failed: " + FormatVkResult(result);
            return false;
        }
        const auto importSemaphore = reinterpret_cast<PFN_vkImportSemaphoreWin32HandleKHR>(
            vkGetDeviceProcAddr(VulkanDevice, "vkImportSemaphoreWin32HandleKHR"));
        if (importSemaphore == nullptr) {
            CloseHandle(handle);
            Reason = "vkImportSemaphoreWin32HandleKHR is unavailable";
            return false;
        }
        VkImportSemaphoreWin32HandleInfoKHR import{ VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR };
        import.semaphore = SharedSemaphore;
        import.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
        import.handle = handle;
        result = importSemaphore(VulkanDevice, &import);
        CloseHandle(handle);
        if (result != VK_SUCCESS) {
            Reason = "Vulkan frame bridge fence import failed: " + FormatVkResult(result);
            return false;
        }

        hr = D3d12Queue->Signal(SharedFence.Get(), 1U);
        if (FAILED(hr) || !WaitForFence(*SharedFence.Get(), 1U, Reason)) {
            if (SUCCEEDED(hr))
                return false;
            Reason = "D3D12 frame bridge verification signal failed: " + FormatHresult(hr);
            return false;
        }
        uint64_t observed = 0U;
        result = vkGetSemaphoreCounterValue(VulkanDevice, SharedSemaphore, &observed);
        if (result != VK_SUCCESS || observed < 1U) {
            Reason = "Vulkan did not observe the D3D12 frame bridge fence";
            return false;
        }
        VkSemaphoreSignalInfo signal{ VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO };
        signal.semaphore = SharedSemaphore;
        signal.value = 2U;
        result = vkSignalSemaphore(VulkanDevice, &signal);
        if (result != VK_SUCCESS || !WaitForFence(*SharedFence.Get(), 2U, Reason)) {
            if (result == VK_SUCCESS)
                return false;
            Reason = "Vulkan frame bridge verification signal failed: " + FormatVkResult(result);
            return false;
        }
        SharedFenceReady = true;
        return true;
    }

    bool CreateConversionPipeline() {
        std::array<VkDescriptorSetLayoutBinding, 8> bindings{};
        for (uint32_t index = 0; index < bindings.size(); ++index) {
            bindings[index].binding = index;
            bindings[index].descriptorCount = 1U;
            bindings[index].descriptorType = index < kInputResourceCount ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                                         : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout.bindingCount = static_cast<uint32_t>(bindings.size());
        layout.pBindings = bindings.data();
        VkResult result = vkCreateDescriptorSetLayout(VulkanDevice, &layout, nullptr, &ConversionSetLayout);
        if (result != VK_SUCCESS) {
            Reason = "D3D12 NGX conversion layout creation failed: " + FormatVkResult(result);
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayout{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipelineLayout.setLayoutCount = 1U;
        pipelineLayout.pSetLayouts = &ConversionSetLayout;
        result = vkCreatePipelineLayout(VulkanDevice, &pipelineLayout, nullptr, &ConversionPipelineLayout);
        if (result != VK_SUCCESS) {
            Reason = "D3D12 NGX conversion pipeline layout failed: " + FormatVkResult(result);
            return false;
        }

        VkSamplerCreateInfo sampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sampler.magFilter = VK_FILTER_NEAREST;
        sampler.minFilter = VK_FILTER_NEAREST;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = 0.0F;
        result = vkCreateSampler(VulkanDevice, &sampler, nullptr, &ConversionSampler);
        if (result != VK_SUCCESS) {
            Reason = "D3D12 NGX conversion sampler failed: " + FormatVkResult(result);
            return false;
        }

        std::vector<uint32_t> spirv;
        if (!CompileComputeShader(spirv, Reason))
            return false;
        VkShaderModuleCreateInfo moduleCreate{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        moduleCreate.codeSize = spirv.size() * sizeof(uint32_t);
        moduleCreate.pCode = spirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        result = vkCreateShaderModule(VulkanDevice, &moduleCreate, nullptr, &module);
        if (result != VK_SUCCESS) {
            Reason = "D3D12 NGX conversion shader module failed: " + FormatVkResult(result);
            return false;
        }
        VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = module;
        stage.pName = "main";
        VkComputePipelineCreateInfo pipeline{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        pipeline.stage = stage;
        pipeline.layout = ConversionPipelineLayout;
        result = vkCreateComputePipelines(VulkanDevice, VK_NULL_HANDLE, 1U, &pipeline, nullptr, &ConversionPipeline);
        vkDestroyShaderModule(VulkanDevice, module, nullptr);
        if (result != VK_SUCCESS) {
            Reason = "D3D12 NGX conversion pipeline failed: " + FormatVkResult(result);
            return false;
        }
        return true;
    }

    void DestroySharedImage(SharedImage& image) {
        if (Core != nullptr) {
            if (image.NriDescriptor != nullptr)
                Core->DestroyDescriptor(image.NriDescriptor);
            if (image.NriTexture != nullptr)
                Core->DestroyTexture(image.NriTexture);
        }
        image.NriDescriptor = nullptr;
        image.NriTexture = nullptr;
        image.D3d12Resource.Reset();
        if (VulkanDevice != VK_NULL_HANDLE) {
            if (image.View != VK_NULL_HANDLE)
                vkDestroyImageView(VulkanDevice, image.View, nullptr);
            if (image.Image != VK_NULL_HANDLE)
                vkDestroyImage(VulkanDevice, image.Image, nullptr);
            if (image.Memory != VK_NULL_HANDLE)
                vkFreeMemory(VulkanDevice, image.Memory, nullptr);
        }
        image = {};
    }

    bool CreateSharedImage(uint32_t width, uint32_t height, SharedFormat format, bool storageDescriptor,
                           SharedImage& image) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = 1U;
        heap.VisibleNodeMask = 1U;
        D3D12_RESOURCE_DESC resource{};
        resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource.Width = width;
        resource.Height = height;
        resource.DepthOrArraySize = 1U;
        resource.MipLevels = 1U;
        resource.Format = format.D3d12;
        resource.SampleDesc.Count = 1U;
        resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        // Vulkan writes every input through a storage view; retaining UAV on
        // the output also matches the DLSR output contract.
        resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        HRESULT hr =
            D3d12Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &resource, D3D12_RESOURCE_STATE_COMMON,
                                                 nullptr, IID_PPV_ARGS(&image.D3d12Resource));
        if (FAILED(hr)) {
            Reason = "D3D12 shared frame image creation failed: " + FormatHresult(hr);
            return false;
        }
        HANDLE handle = nullptr;
        hr = D3d12Device->CreateSharedHandle(image.D3d12Resource.Get(), nullptr, GENERIC_ALL, nullptr, &handle);
        if (FAILED(hr) || handle == nullptr) {
            Reason = "D3D12 shared frame image export failed: " + FormatHresult(hr);
            return false;
        }

        constexpr auto handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        VkExternalMemoryImageCreateInfo externalImage{ VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
        externalImage.handleTypes = handleType;
        VkImageCreateInfo create{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        create.pNext = &externalImage;
        create.imageType = VK_IMAGE_TYPE_2D;
        create.format = format.Vulkan;
        create.extent = { width, height, 1U };
        create.mipLevels = 1U;
        create.arrayLayers = 1U;
        create.samples = VK_SAMPLE_COUNT_1_BIT;
        create.tiling = VK_IMAGE_TILING_OPTIMAL;
        create.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult result = vkCreateImage(VulkanDevice, &create, nullptr, &image.Image);
        if (result != VK_SUCCESS) {
            CloseHandle(handle);
            Reason = "Vulkan shared frame image creation failed: " + FormatVkResult(result);
            return false;
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(VulkanDevice, image.Image, &requirements);
        const auto getHandleProperties = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
            vkGetDeviceProcAddr(VulkanDevice, "vkGetMemoryWin32HandlePropertiesKHR"));
        if (getHandleProperties == nullptr) {
            CloseHandle(handle);
            Reason = "vkGetMemoryWin32HandlePropertiesKHR is unavailable";
            return false;
        }
        VkMemoryWin32HandlePropertiesKHR handleProperties{ VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR };
        result = getHandleProperties(VulkanDevice, handleType, handle, &handleProperties);
        if (result != VK_SUCCESS) {
            CloseHandle(handle);
            Reason = "Vulkan rejected a D3D12 frame image handle: " + FormatVkResult(result);
            return false;
        }
        const uint32_t memoryType =
            FindMemoryType(PhysicalDevice, requirements.memoryTypeBits & handleProperties.memoryTypeBits,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memoryType == std::numeric_limits<uint32_t>::max()) {
            CloseHandle(handle);
            Reason = "Vulkan found no memory type for a D3D12 frame image";
            return false;
        }
        VkMemoryDedicatedAllocateInfo dedicated{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
        dedicated.image = image.Image;
        VkImportMemoryWin32HandleInfoKHR import{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR };
        import.pNext = &dedicated;
        import.handleType = handleType;
        import.handle = handle;
        VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.pNext = &import;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        result = vkAllocateMemory(VulkanDevice, &allocation, nullptr, &image.Memory);
        CloseHandle(handle);
        if (result != VK_SUCCESS) {
            Reason = "Vulkan D3D12 frame image import failed: " + FormatVkResult(result);
            return false;
        }
        result = vkBindImageMemory(VulkanDevice, image.Image, image.Memory, 0U);
        if (result != VK_SUCCESS) {
            Reason = "Vulkan shared frame image binding failed: " + FormatVkResult(result);
            return false;
        }
        VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view.image = image.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format.Vulkan;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1U;
        view.subresourceRange.layerCount = 1U;
        result = vkCreateImageView(VulkanDevice, &view, nullptr, &image.View);
        if (result != VK_SUCCESS) {
            Reason = "Vulkan shared frame image view failed: " + FormatVkResult(result);
            return false;
        }

        nri::TextureD3D12Desc wrap{};
        wrap.d3d12Resource = image.D3d12Resource.Get();
        wrap.format = static_cast<int32_t>(format.D3d12);
        if (Wrapper.CreateTextureD3D12(*NriDevice, wrap, image.NriTexture) != nri::Result::SUCCESS) {
            Reason = "NRI could not wrap a D3D12 frame image";
            return false;
        }
        nri::TextureViewDesc nriView{};
        nriView.texture = image.NriTexture;
        nriView.type = storageDescriptor ? nri::TextureView::STORAGE_TEXTURE : nri::TextureView::TEXTURE;
        nriView.format = format.Nri;
        nriView.mipNum = 1U;
        nriView.layerNum = 1U;
        if (Core->CreateTextureView(nriView, image.NriDescriptor) != nri::Result::SUCCESS) {
            Reason = "NRI could not create a D3D12 frame image view";
            return false;
        }
        image.Format = format;
        image.Width = width;
        image.Height = height;
        return true;
    }

    void DestroyFrames() {
        if (Upscaler != nullptr && UpscalerInterface != nullptr)
            UpscalerInterface->DestroyUpscaler(Upscaler);
        Upscaler = nullptr;
        for (auto& frame : Frames) {
            for (auto& input : frame.Inputs)
                DestroySharedImage(input);
            DestroySharedImage(frame.Output);
            if (Core != nullptr) {
                if (frame.CommandBuffer != nullptr)
                    Core->DestroyCommandBuffer(frame.CommandBuffer);
                if (frame.CommandAllocator != nullptr)
                    Core->DestroyCommandAllocator(frame.CommandAllocator);
            }
        }
        Frames.clear();
        if (ConversionPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(VulkanDevice, ConversionPool, nullptr);
            ConversionPool = VK_NULL_HANDLE;
        }
        Contract = {};
        ExternalMemoryReady = false;
    }

    bool CreateFrames(const D3d12NgxFrameContract& contract) {
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, contract.FrameSlotCount * kInputResourceCount };
        poolSizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, contract.FrameSlotCount * kInputResourceCount };
        VkDescriptorPoolCreateInfo pool{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool.maxSets = contract.FrameSlotCount;
        pool.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        pool.pPoolSizes = poolSizes.data();
        VkResult result = vkCreateDescriptorPool(VulkanDevice, &pool, nullptr, &ConversionPool);
        if (result != VK_SUCCESS) {
            Reason = "D3D12 NGX bridge descriptor pool failed: " + FormatVkResult(result);
            return false;
        }
        std::vector<VkDescriptorSetLayout> layouts(contract.FrameSlotCount, ConversionSetLayout);
        std::vector<VkDescriptorSet> sets(contract.FrameSlotCount);
        VkDescriptorSetAllocateInfo allocate{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocate.descriptorPool = ConversionPool;
        allocate.descriptorSetCount = contract.FrameSlotCount;
        allocate.pSetLayouts = layouts.data();
        result = vkAllocateDescriptorSets(VulkanDevice, &allocate, sets.data());
        if (result != VK_SUCCESS) {
            Reason = "D3D12 NGX bridge descriptor allocation failed: " + FormatVkResult(result);
            return false;
        }

        Frames.resize(contract.FrameSlotCount);
        for (uint32_t slot = 0; slot < contract.FrameSlotCount; ++slot) {
            auto& frame = Frames[slot];
            frame.ConversionSet = sets[slot];
            if (Core->CreateCommandAllocator(*NriQueue, frame.CommandAllocator) != nri::Result::SUCCESS ||
                Core->CreateCommandBuffer(*frame.CommandAllocator, frame.CommandBuffer) != nri::Result::SUCCESS) {
                Reason = "NRI D3D12 frame command resources failed";
                return false;
            }
            if (!CreateSharedImage(contract.InputWidth, contract.InputHeight, kColorFormat, false, frame.Inputs[0]) ||
                !CreateSharedImage(contract.InputWidth, contract.InputHeight, kDepthFormat, false, frame.Inputs[1]) ||
                !CreateSharedImage(contract.InputWidth, contract.InputHeight, kMotionFormat, false, frame.Inputs[2]) ||
                !CreateSharedImage(contract.InputWidth, contract.InputHeight, kReactiveFormat, false,
                                   frame.Inputs[3]) ||
                !CreateSharedImage(contract.OutputWidth, contract.OutputHeight, kColorFormat, true, frame.Output)) {
                return false;
            }
        }
        Contract = contract;
        ExternalMemoryReady = true;
        return true;
    }

    bool EnsureUpscaler(nri::CommandBuffer& commandBuffer) {
        if (Upscaler != nullptr)
            return true;
        nri::UpscalerDesc desc{};
        desc.upscaleResolution = { static_cast<nri::Dim_t>(Contract.OutputWidth),
                                   static_cast<nri::Dim_t>(Contract.OutputHeight) };
        desc.type = nri::UpscalerType::DLSR;
        desc.mode = ToNriUpscalerMode(Contract.Quality);
        desc.flags = (Contract.InputSrgb ? nri::UpscalerBits::SRGB : nri::UpscalerBits::HDR) |
                     nri::UpscalerBits::USE_REACTIVE | nri::UpscalerBits::MV_JITTERED;
        // No USE_EXPOSURE flag deliberately selects NGX auto-exposure.
        desc.commandBuffer = &commandBuffer;
        if (UpscalerInterface->CreateUpscaler(*NriDevice, desc, Upscaler) != nri::Result::SUCCESS ||
            Upscaler == nullptr) {
            Reason = "post-swapchain D3D12 NGX feature creation failed";
            Upscaler = nullptr;
            return false;
        }
        nri::UpscalerProps properties{};
        UpscalerInterface->GetUpscalerProps(*Upscaler, properties);
        const bool renderResolutionAccepted = Contract.InputWidth >= properties.renderResolutionMin.w &&
                                              Contract.InputHeight >= properties.renderResolutionMin.h &&
                                              Contract.InputWidth <= properties.renderResolution.w &&
                                              Contract.InputHeight <= properties.renderResolution.h;
        if (properties.upscaleResolution.w != Contract.OutputWidth ||
            properties.upscaleResolution.h != Contract.OutputHeight || !renderResolutionAccepted) {
            Reason = "NGX rejected the requested dynamic-resolution contract";
            UpscalerInterface->DestroyUpscaler(Upscaler);
            Upscaler = nullptr;
            return false;
        }
        return true;
    }

    bool RecordD3d12Frame(Frame& frame, const D3d12NgxFrameInputs& inputs) {
        Core->ResetCommandAllocator(*frame.CommandAllocator);
        if (Core->BeginCommandBuffer(*frame.CommandBuffer, nullptr) != nri::Result::SUCCESS) {
            Reason = "NRI could not begin the D3D12 frame command list";
            return false;
        }
        if (!EnsureUpscaler(*frame.CommandBuffer)) {
            Core->EndCommandBuffer(*frame.CommandBuffer);
            return false;
        }

        std::array<nri::TextureBarrierDesc, kSharedResourceCount> toDispatch{};
        for (uint32_t index = 0; index < kSharedResourceCount; ++index) {
            auto& barrier = toDispatch[index];
            const bool output = index == kInputResourceCount;
            barrier.texture = output ? frame.Output.NriTexture : frame.Inputs[index].NriTexture;
            barrier.before.access = nri::AccessBits::NONE;
            barrier.before.layout = nri::Layout::GENERAL;
            barrier.before.stages = nri::StageBits::NONE;
            barrier.after.access = output ? nri::AccessBits::SHADER_RESOURCE_STORAGE : nri::AccessBits::SHADER_RESOURCE;
            barrier.after.layout = output ? nri::Layout::SHADER_RESOURCE_STORAGE : nri::Layout::SHADER_RESOURCE;
            barrier.after.stages = nri::StageBits::COMPUTE_SHADER;
            barrier.mipNum = 1U;
            barrier.layerNum = 1U;
            barrier.planes = nri::PlaneBits::COLOR;
        }
        nri::BarrierDesc beginBarrier{};
        beginBarrier.textures = toDispatch.data();
        beginBarrier.textureNum = static_cast<uint32_t>(toDispatch.size());
        Core->CmdBarrier(*frame.CommandBuffer, beginBarrier);

        nri::DispatchUpscaleDesc dispatch{};
        dispatch.input = { frame.Inputs[0].NriTexture, frame.Inputs[0].NriDescriptor };
        dispatch.guides.upscaler.depth = { frame.Inputs[1].NriTexture, frame.Inputs[1].NriDescriptor };
        dispatch.guides.upscaler.mv = { frame.Inputs[2].NriTexture, frame.Inputs[2].NriDescriptor };
        dispatch.guides.upscaler.reactive = { frame.Inputs[3].NriTexture, frame.Inputs[3].NriDescriptor };
        dispatch.output = { frame.Output.NriTexture, frame.Output.NriDescriptor };
        dispatch.currentResolution = { static_cast<nri::Dim_t>(Contract.InputWidth),
                                       static_cast<nri::Dim_t>(Contract.InputHeight) };
        dispatch.cameraJitter = { inputs.JitterPixels[0], inputs.JitterPixels[1] };
        dispatch.mvScale = { static_cast<float>(Contract.InputWidth), static_cast<float>(Contract.InputHeight) };
        dispatch.flags = inputs.ResetHistory ? nri::DispatchUpscaleBits::RESET_HISTORY : nri::DispatchUpscaleBits::NONE;
        UpscalerInterface->CmdDispatchUpscale(*frame.CommandBuffer, *Upscaler, dispatch);

        std::array<nri::TextureBarrierDesc, kSharedResourceCount> toCommon{};
        for (uint32_t index = 0; index < kSharedResourceCount; ++index) {
            toCommon[index] = toDispatch[index];
            std::swap(toCommon[index].before, toCommon[index].after);
        }
        nri::BarrierDesc endBarrier{};
        endBarrier.textures = toCommon.data();
        endBarrier.textureNum = static_cast<uint32_t>(toCommon.size());
        Core->CmdBarrier(*frame.CommandBuffer, endBarrier);
        if (Core->EndCommandBuffer(*frame.CommandBuffer) != nri::Result::SUCCESS) {
            Reason = "NRI could not close the D3D12 frame command list";
            return false;
        }
        frame.NativeCommandList =
            static_cast<ID3D12GraphicsCommandList*>(Core->GetCommandBufferNativeObject(frame.CommandBuffer));
        if (frame.NativeCommandList == nullptr) {
            Reason = "NRI returned no native D3D12 frame command list";
            return false;
        }
        return true;
    }

    void UpdateConversionDescriptors(Frame& frame, const D3d12NgxFrameInputs& inputs) {
        const std::array<VkImageView, kInputResourceCount> sourceViews{ inputs.Color, inputs.Depth, inputs.Motion,
                                                                        inputs.Reactive };
        const std::array<VkImageLayout, kInputResourceCount> sourceLayouts{ inputs.ColorLayout, inputs.DepthLayout,
                                                                            inputs.MotionLayout,
                                                                            inputs.ReactiveLayout };
        std::array<VkDescriptorImageInfo, kInputResourceCount> sampled{};
        std::array<VkDescriptorImageInfo, kInputResourceCount> storage{};
        std::array<VkWriteDescriptorSet, 8> writes{};
        for (uint32_t index = 0; index < kInputResourceCount; ++index) {
            sampled[index].sampler = ConversionSampler;
            sampled[index].imageView = sourceViews[index];
            sampled[index].imageLayout = sourceLayouts[index];
            storage[index].imageView = frame.Inputs[index].View;
            storage[index].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            writes[index] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[index].dstSet = frame.ConversionSet;
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1U;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[index].pImageInfo = &sampled[index];
            writes[index + kInputResourceCount] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[index + kInputResourceCount].dstSet = frame.ConversionSet;
            writes[index + kInputResourceCount].dstBinding = index + kInputResourceCount;
            writes[index + kInputResourceCount].descriptorCount = 1U;
            writes[index + kInputResourceCount].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[index + kInputResourceCount].pImageInfo = &storage[index];
        }
        vkUpdateDescriptorSets(VulkanDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0U, nullptr);
    }

    void RecordVulkanInputs(VkCommandBuffer command, Frame& frame) {
        // The output remains Vulkan-owned while scanout samples it. Return it
        // to the external owner immediately before the next D3D12 write.
        if (frame.OutputOwnedByVulkan) {
            VkImageMemoryBarrier release{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            release.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            release.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            release.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            release.srcQueueFamilyIndex = GraphicsQueueFamily;
            release.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
            release.image = frame.Output.Image;
            release.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            release.subresourceRange.levelCount = 1U;
            release.subresourceRange.layerCount = 1U;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U, 0U, nullptr, 0U, nullptr, 1U, &release);
            frame.OutputOwnedByVulkan = false;
        } else if (!frame.Output.VulkanLayoutInitialized) {
            VkImageMemoryBarrier initialize{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            initialize.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            initialize.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            initialize.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
            initialize.dstQueueFamilyIndex = GraphicsQueueFamily;
            initialize.image = frame.Output.Image;
            initialize.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            initialize.subresourceRange.levelCount = 1U;
            initialize.subresourceRange.layerCount = 1U;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U,
                                 0U, nullptr, 0U, nullptr, 1U, &initialize);
            std::swap(initialize.srcQueueFamilyIndex, initialize.dstQueueFamilyIndex);
            initialize.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            initialize.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U,
                                 0U, nullptr, 0U, nullptr, 1U, &initialize);
            frame.Output.VulkanLayoutInitialized = true;
        }

        std::array<VkImageMemoryBarrier, kInputResourceCount> acquire{};
        for (uint32_t index = 0; index < kInputResourceCount; ++index) {
            auto& barrier = acquire[index];
            auto& image = frame.Inputs[index];
            barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.oldLayout = image.VulkanLayoutInitialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
            barrier.dstQueueFamilyIndex = GraphicsQueueFamily;
            barrier.image = image.Image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1U;
            barrier.subresourceRange.layerCount = 1U;
            image.VulkanLayoutInitialized = true;
        }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0U, 0U,
                             nullptr, 0U, nullptr, static_cast<uint32_t>(acquire.size()), acquire.data());
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, ConversionPipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, ConversionPipelineLayout, 0U, 1U,
                                &frame.ConversionSet, 0U, nullptr);
        vkCmdDispatch(command, (Contract.InputWidth + 7U) / 8U, (Contract.InputHeight + 7U) / 8U, 1U);

        std::array<VkImageMemoryBarrier, kInputResourceCount> release{};
        for (uint32_t index = 0; index < kInputResourceCount; ++index) {
            auto& barrier = release[index];
            barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = GraphicsQueueFamily;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
            barrier.image = frame.Inputs[index].Image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1U;
            barrier.subresourceRange.layerCount = 1U;
        }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U,
                             0U, nullptr, 0U, nullptr, static_cast<uint32_t>(release.size()), release.data());
    }

    bool SignalCompletionFallback(uint64_t inputsReady,
                                  uint64_t outputReady) {
        VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wait.semaphoreCount = 1U;
        wait.pSemaphores = &SharedSemaphore;
        wait.pValues = &inputsReady;
        const VkResult waitResult = vkWaitSemaphores(
            VulkanDevice, &wait,
            static_cast<uint64_t>(kFenceTimeoutMilliseconds) * 1000000ULL);
        if (waitResult != VK_SUCCESS) {
            Reason = "D3D12 frame bridge could not observe the Vulkan "
                     "prefix before fallback: " +
                     FormatVkResult(waitResult);
            return false;
        }
        const HRESULT hr = SharedFence->Signal(outputReady);
        if (FAILED(hr)) {
            Reason = "D3D12 frame bridge could not unblock Vulkan: " + FormatHresult(hr);
            return false;
        }
        LastQueuedFenceValue = std::max(LastQueuedFenceValue, outputReady);
        return true;
    }

    bool SignalSubmittedWorkFallback(uint64_t outputReady) {
        ComPtr<ID3D12Fence> completion;
        HRESULT hr = D3d12Device->CreateFence(
            0U, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&completion));
        if (FAILED(hr)) {
            Reason = "D3D12 frame bridge fallback fence creation failed: " +
                     FormatHresult(hr);
            return false;
        }
        hr = D3d12Queue->Signal(completion.Get(), 1U);
        if (FAILED(hr) || !WaitForFence(*completion.Get(), 1U, Reason)) {
            if (SUCCEEDED(hr)) return false;
            Reason = "D3D12 frame bridge fallback queue signal failed: " +
                     FormatHresult(hr);
            return false;
        }
        hr = SharedFence->Signal(outputReady);
        if (FAILED(hr)) {
            Reason = "D3D12 frame bridge fallback completion signal failed: " +
                     FormatHresult(hr);
            return false;
        }
        LastQueuedFenceValue = std::max(LastQueuedFenceValue, outputReady);
        return true;
    }
};

D3d12NgxFrameBridge::D3d12NgxFrameBridge() : mImpl(std::make_unique<Impl>()) {
}

D3d12NgxFrameBridge::~D3d12NgxFrameBridge() {
    Shutdown();
}

bool D3d12NgxFrameBridge::Initialize(VkPhysicalDevice physicalDevice, VkDevice vulkanDevice,
                                     uint32_t graphicsQueueFamily, ID3D12Device& d3d12Device,
                                     ID3D12CommandQueue& d3d12Queue, nri::Device& nriDevice,
                                     const nri::CoreInterface& core, const nri::UpscalerInterface& upscalerInterface,
                                     const D3d12NgxFrameContract& initialContract) {
    Shutdown();
    if (physicalDevice == VK_NULL_HANDLE || vulkanDevice == VK_NULL_HANDLE || !initialContract.Valid()) {
        mImpl->Reason = "D3D12 NGX frame bridge initialization is incomplete";
        return false;
    }
    mImpl->PhysicalDevice = physicalDevice;
    mImpl->VulkanDevice = vulkanDevice;
    mImpl->GraphicsQueueFamily = graphicsQueueFamily;
    mImpl->D3d12Device = &d3d12Device;
    mImpl->D3d12Queue = &d3d12Queue;
    mImpl->NriDevice = &nriDevice;
    mImpl->Core = &core;
    mImpl->UpscalerInterface = &upscalerInterface;
    if (nriGetInterface(nriDevice, "WrapperD3D12Interface", sizeof(mImpl->Wrapper), &mImpl->Wrapper) !=
            nri::Result::SUCCESS ||
        core.GetQueue(nriDevice, nri::QueueType::GRAPHICS, 0U, mImpl->NriQueue) != nri::Result::SUCCESS ||
        mImpl->NriQueue == nullptr) {
        mImpl->Reason = "NRI D3D12 wrapper/queue interface is unavailable";
        return false;
    }
    if (!mImpl->CreateSharedFence() || !mImpl->CreateConversionPipeline() || !Configure(initialContract)) {
        return false;
    }
    mImpl->Reason.clear();
    return true;
}

void D3d12NgxFrameBridge::Shutdown() {
    if (!mImpl)
        return;
    if (mImpl->SharedFence != nullptr && mImpl->LastQueuedFenceValue != 0U) {
        std::string ignored;
        WaitForFence(*mImpl->SharedFence.Get(), mImpl->LastQueuedFenceValue, ignored);
    }
    mImpl->DestroyFrames();
    if (mImpl->VulkanDevice != VK_NULL_HANDLE) {
        if (mImpl->ConversionPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(mImpl->VulkanDevice, mImpl->ConversionPipeline, nullptr);
        if (mImpl->ConversionPipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(mImpl->VulkanDevice, mImpl->ConversionPipelineLayout, nullptr);
        if (mImpl->ConversionSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(mImpl->VulkanDevice, mImpl->ConversionSetLayout, nullptr);
        if (mImpl->ConversionSampler != VK_NULL_HANDLE)
            vkDestroySampler(mImpl->VulkanDevice, mImpl->ConversionSampler, nullptr);
        if (mImpl->SharedSemaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(mImpl->VulkanDevice, mImpl->SharedSemaphore, nullptr);
    }
    *mImpl = {};
    mImpl->Reason = "D3D12 NGX frame bridge is not initialized";
}

bool D3d12NgxFrameBridge::ConfiguredFor(const D3d12NgxFrameContract& contract) const {
    return mImpl && contract.Valid() && mImpl->Contract == contract && mImpl->ExternalMemoryReady;
}

bool D3d12NgxFrameBridge::Configure(const D3d12NgxFrameContract& contract) {
    if (!mImpl || !contract.Valid() || mImpl->VulkanDevice == VK_NULL_HANDLE || mImpl->SharedFence == nullptr) {
        if (mImpl)
            mImpl->Reason = "invalid D3D12 NGX frame contract";
        return false;
    }
    if (ConfiguredFor(contract))
        return true;
    if (mImpl->LastQueuedFenceValue != 0U &&
        !WaitForFence(*mImpl->SharedFence.Get(), mImpl->LastQueuedFenceValue, mImpl->Reason)) {
        return false;
    }
    mImpl->DestroyFrames();
    if (!mImpl->CreateFrames(contract)) {
        mImpl->DestroyFrames();
        return false;
    }
    mImpl->Reason.clear();
    return true;
}

bool D3d12NgxFrameBridge::PrepareFrame(VkCommandBuffer vulkanCommandBuffer, uint32_t frameSlot,
                                       const D3d12NgxFrameInputs& inputs,
                                       D3d12NgxFrameSynchronization& synchronization) {
    synchronization = {};
    if (!Available() || !inputs.Valid() || vulkanCommandBuffer == VK_NULL_HANDLE || frameSlot >= mImpl->Frames.size()) {
        return false;
    }
    if (FAILED(mImpl->D3d12Device->GetDeviceRemovedReason())) {
        mImpl->Reason = "D3D12 device was removed before NGX frame dispatch";
        return false;
    }
    auto& frame = mImpl->Frames[frameSlot];
    if (!mImpl->RecordD3d12Frame(frame, inputs))
        return false;
    mImpl->UpdateConversionDescriptors(frame, inputs);
    mImpl->RecordVulkanInputs(vulkanCommandBuffer, frame);

    synchronization.Semaphore = mImpl->SharedSemaphore;
    synchronization.VulkanInputsReady = mImpl->NextFenceValue++;
    synchronization.D3d12OutputReady = mImpl->NextFenceValue++;
    synchronization.FrameSlot = frameSlot;
    synchronization.Serial = mImpl->NextSerial++;
    frame.PreparedSerial = synchronization.Serial;
    return true;
}

bool D3d12NgxFrameBridge::QueuePreparedFrame(const D3d12NgxFrameSynchronization& synchronization) {
    if (!mImpl || !synchronization.Valid() ||
        synchronization.Semaphore != mImpl->SharedSemaphore) {
        return false;
    }
    if (synchronization.FrameSlot >= mImpl->Frames.size()) {
        if (!mImpl->SignalCompletionFallback(
                synchronization.VulkanInputsReady,
                synchronization.D3d12OutputReady)) {
            throw std::runtime_error(mImpl->Reason);
        }
        return false;
    }
    auto& frame = mImpl->Frames[synchronization.FrameSlot];
    if (frame.PreparedSerial != synchronization.Serial || frame.NativeCommandList == nullptr) {
        if (!mImpl->SignalCompletionFallback(
                synchronization.VulkanInputsReady,
                synchronization.D3d12OutputReady)) {
            throw std::runtime_error(mImpl->Reason);
        }
        return false;
    }
    HRESULT hr = mImpl->D3d12Queue->Wait(mImpl->SharedFence.Get(), synchronization.VulkanInputsReady);
    if (FAILED(hr)) {
        mImpl->Reason = "D3D12 queue wait for Vulkan inputs failed: " + FormatHresult(hr);
        if (!mImpl->SignalCompletionFallback(
                synchronization.VulkanInputsReady,
                synchronization.D3d12OutputReady)) {
            throw std::runtime_error(mImpl->Reason);
        }
        return false;
    }
    ID3D12CommandList* lists[] = { frame.NativeCommandList };
    mImpl->D3d12Queue->ExecuteCommandLists(1U, lists);
    hr = mImpl->D3d12Queue->Signal(mImpl->SharedFence.Get(), synchronization.D3d12OutputReady);
    if (FAILED(hr)) {
        mImpl->Reason = "D3D12 queue signal for Vulkan output failed: " + FormatHresult(hr);
        if (!mImpl->SignalSubmittedWorkFallback(
                synchronization.D3d12OutputReady)) {
            throw std::runtime_error(mImpl->Reason);
        }
        return false;
    }
    mImpl->LastQueuedFenceValue = synchronization.D3d12OutputReady;
    frame.QueuedSerial = synchronization.Serial;
    return true;
}

bool D3d12NgxFrameBridge::RecordOutputAcquire(VkCommandBuffer vulkanCommandBuffer,
                                              const D3d12NgxFrameSynchronization& synchronization) {
    if (!mImpl || vulkanCommandBuffer == VK_NULL_HANDLE || !synchronization.Valid() ||
        synchronization.FrameSlot >= mImpl->Frames.size()) {
        return false;
    }
    auto& frame = mImpl->Frames[synchronization.FrameSlot];
    if (frame.QueuedSerial != synchronization.Serial)
        return false;
    VkImageMemoryBarrier acquire{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    acquire.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    acquire.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    acquire.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    acquire.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    acquire.dstQueueFamilyIndex = mImpl->GraphicsQueueFamily;
    acquire.image = frame.Output.Image;
    acquire.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    acquire.subresourceRange.levelCount = 1U;
    acquire.subresourceRange.layerCount = 1U;
    vkCmdPipelineBarrier(vulkanCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0U, 0U, nullptr,
                         0U, nullptr, 1U, &acquire);
    frame.Output.VulkanLayoutInitialized = true;
    frame.OutputOwnedByVulkan = true;
    return true;
}

bool D3d12NgxFrameBridge::Available() const {
    return mImpl && mImpl->VulkanDevice != VK_NULL_HANDLE && mImpl->D3d12Device != nullptr &&
           mImpl->D3d12Queue != nullptr && mImpl->ConversionPipeline != VK_NULL_HANDLE && mImpl->SharedFenceReady &&
           mImpl->ExternalMemoryReady && mImpl->Reason.empty();
}

bool D3d12NgxFrameBridge::ExternalMemoryReady() const {
    return mImpl && mImpl->ExternalMemoryReady;
}

bool D3d12NgxFrameBridge::SharedFenceReady() const {
    return mImpl && mImpl->SharedFenceReady;
}

VkImage D3d12NgxFrameBridge::OutputImage(uint32_t frameSlot) const {
    return mImpl && frameSlot < mImpl->Frames.size() ? mImpl->Frames[frameSlot].Output.Image : VK_NULL_HANDLE;
}

VkImageView D3d12NgxFrameBridge::OutputView(uint32_t frameSlot) const {
    return mImpl && frameSlot < mImpl->Frames.size() ? mImpl->Frames[frameSlot].Output.View : VK_NULL_HANDLE;
}

const D3d12NgxFrameContract& D3d12NgxFrameBridge::Contract() const {
    return mImpl->Contract;
}

const std::string& D3d12NgxFrameBridge::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
