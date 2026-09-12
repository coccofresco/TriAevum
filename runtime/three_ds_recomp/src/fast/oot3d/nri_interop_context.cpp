#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/nri_stage_scope.h"

#ifdef ENABLE_OOT3D_VULKAN

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#ifdef ENABLE_OOT3D_NRI
#include <NRI.h>
#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRIHelper.h>
#include <Extensions/NRIRayTracing.h>
#include <Extensions/NRIUpscaler.h>
#include <Extensions/NRIWrapperVK.h>
#include "nri_interop_internal.h"
#endif

namespace Fast::Oot3d {

struct NriInteropContext::Impl {
    std::string Reason = "NRI support was not compiled into this build";
    RendererValidationTelemetry* ValidationTelemetry = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::Device* Device = nullptr;
    nri::CoreInterface Core{};
    nri::WrapperVKInterface Wrapper{};
    nri::UpscalerInterface UpscalerInterface{};
    nri::Upscaler* Nis = nullptr;
    nri::Upscaler* Fsr = nullptr;
    nri::Upscaler* Dlss = nullptr;
    uint32_t NisWidth = 0;
    uint32_t NisHeight = 0;
    UpscalerQuality NisQuality = UpscalerQuality::Quality;
    bool NisInputSrgb = true;
    std::string NisReason = "NRI NIS support is unavailable";
    uint32_t FsrWidth = 0;
    uint32_t FsrHeight = 0;
    UpscalerQuality FsrQuality = UpscalerQuality::Quality;
    bool FsrInputSrgb = true;
    std::string FsrReason = "NRI FSR support is unavailable";
    uint32_t DlssWidth = 0;
    uint32_t DlssHeight = 0;
    UpscalerQuality DlssQuality = UpscalerQuality::Quality;
    bool DlssInputSrgb = true;
    std::string DlssReason = "NRI DLSS support is unavailable";
    bool DynamicRendering = false;
    std::unordered_map<uint32_t, nri::CommandBuffer*> CommandBuffers;
    struct WrappedTexture {
        nri::Texture* Texture = nullptr;
        nri::Format Format = nri::Format::UNKNOWN;
        nri::Descriptor* Sampled = nullptr;
        nri::Descriptor* Storage = nullptr;
        nri::Descriptor* ColorAttachment = nullptr;
        nri::Descriptor* DepthAttachment = nullptr;
        std::vector<nri::Descriptor*> ExtraViews;
        bool Owned = false;
    };
    std::unordered_map<VkImage, WrappedTexture> Textures;
    std::unordered_map<VkBuffer, nri::Buffer*> Buffers;

    nri::Descriptor* GetUpscalerView(VkImage image, bool storage) {
        auto found = Textures.find(image);
        if (found == Textures.end()) return nullptr;
        nri::Descriptor*& descriptor = storage
            ? found->second.Storage : found->second.Sampled;
        if (descriptor != nullptr) return descriptor;
        nri::TextureViewDesc view{};
        view.texture = found->second.Texture;
        view.type = storage ? nri::TextureView::STORAGE_TEXTURE
                            : nri::TextureView::TEXTURE;
        view.format = found->second.Format;
        view.mipNum = 1;
        view.layerNum = 1;
        if (Core.CreateTextureView(view, descriptor) !=
            nri::Result::SUCCESS)
            descriptor = nullptr;
        return descriptor;
    }

    nri::UpscalerResource GetUpscalerResource(
        VkImage image, bool storage = false) {
        const auto found = Textures.find(image);
        if (found == Textures.end()) return {};
        return {found->second.Texture, GetUpscalerView(image, storage)};
    }

    static void NRI_CALL ValidationMessage(
        nri::Message messageType, const char* file, uint32_t line,
        const char* message, void* userArg) {
        auto* impl = static_cast<Impl*>(userArg);
        const char* text =
            message != nullptr ? message : "unspecified NRI message";
        const char* source = file != nullptr ? file : "NRI";
        if (impl != nullptr && impl->ValidationTelemetry != nullptr) {
            const auto severity =
                messageType == nri::Message::ERROR
                    ? RendererValidationSeverity::Error
                    : messageType == nri::Message::WARNING
                        ? RendererValidationSeverity::Warning
                        : RendererValidationSeverity::Info;
            impl->ValidationTelemetry->Record(
                RendererValidationSource::Nri, severity);
        }
        if (messageType == nri::Message::ERROR) {
            std::fprintf(stderr, "OOT3D NRI error [%s:%u]: %s\n", source, line, text);
            SPDLOG_ERROR(
                "OOT3D NRI validation [{}:{}]: {}",
                source, line, text);
        } else if (messageType == nri::Message::WARNING) {
            SPDLOG_WARN(
                "OOT3D NRI validation [{}:{}]: {}",
                source, line, text);
        } else {
            SPDLOG_INFO(
                "OOT3D NRI validation [{}:{}]: {}",
                source, line, text);
        }
    }

    static void NRI_CALL ContinueAfterValidationError(void*) {
        // The automated gate owns the failure policy. Keeping command
        // recording alive lets it collect every error in one run.
    }
#endif
};

#ifdef ENABLE_OOT3D_NRI
namespace {
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
} // namespace

nri::Device* NriInteropAccess::Device(NriInteropContext& context) {
    return context.mImpl->Device;
}

nri::CoreInterface* NriInteropAccess::Core(NriInteropContext& context) {
    return context.mImpl->Device != nullptr ? &context.mImpl->Core : nullptr;
}

nri::CommandBuffer* NriInteropAccess::CommandBuffer(
    NriInteropContext& context, uint32_t frameIndex) {
    const auto found = context.mImpl->CommandBuffers.find(frameIndex);
    return found != context.mImpl->CommandBuffers.end() ? found->second
                                                        : nullptr;
}

nri::Descriptor* NriInteropAccess::TextureView(
    NriInteropContext& context, VkImage image, bool storage) {
    const auto found = context.mImpl->Textures.find(image);
    if (found == context.mImpl->Textures.end()) return nullptr;
    nri::Descriptor*& descriptor = storage ? found->second.Storage
                                           : found->second.Sampled;
    if (descriptor != nullptr) return descriptor;
    nri::TextureViewDesc view{};
    view.texture = found->second.Texture;
    view.type = storage ? nri::TextureView::STORAGE_TEXTURE
                        : nri::TextureView::TEXTURE;
    view.format = found->second.Format;
    view.mipNum = 1;
    view.layerNum = 1;
    if (context.mImpl->Core.CreateTextureView(view, descriptor) !=
        nri::Result::SUCCESS)
        descriptor = nullptr;
    return descriptor;
}

nri::Texture* NriInteropAccess::Texture(
    NriInteropContext& context, VkImage image) {
    const auto found = context.mImpl->Textures.find(image);
    return found != context.mImpl->Textures.end()
        ? found->second.Texture : nullptr;
}

nri::Buffer* NriInteropAccess::Buffer(
    NriInteropContext& context, VkBuffer buffer) {
    const auto found = context.mImpl->Buffers.find(buffer);
    return found != context.mImpl->Buffers.end()
        ? found->second : nullptr;
}

nri::Descriptor* NriInteropAccess::ColorAttachmentView(
    NriInteropContext& context, VkImage image) {
    const auto found = context.mImpl->Textures.find(image);
    if (found == context.mImpl->Textures.end()) return nullptr;
    if (found->second.ColorAttachment != nullptr)
        return found->second.ColorAttachment;
    nri::TextureViewDesc view{};
    view.texture = found->second.Texture;
    view.type = nri::TextureView::COLOR_ATTACHMENT;
    view.format = found->second.Format;
    view.mipNum = 1;
    view.layerNum = 1;
    if (context.mImpl->Core.CreateTextureView(
            view, found->second.ColorAttachment) != nri::Result::SUCCESS)
        found->second.ColorAttachment = nullptr;
    return found->second.ColorAttachment;
}

nri::Descriptor* NriInteropAccess::DepthAttachmentView(
    NriInteropContext& context, VkImage image) {
    const auto found = context.mImpl->Textures.find(image);
    if (found == context.mImpl->Textures.end()) return nullptr;
    if (found->second.DepthAttachment != nullptr)
        return found->second.DepthAttachment;
    nri::TextureViewDesc view{};
    view.texture = found->second.Texture;
    view.type = nri::TextureView::DEPTH_STENCIL_ATTACHMENT;
    view.format = found->second.Format;
    view.mipNum = 1;
    view.layerNum = 1;
    if (context.mImpl->Core.CreateTextureView(
            view, found->second.DepthAttachment) !=
        nri::Result::SUCCESS)
        found->second.DepthAttachment = nullptr;
    return found->second.DepthAttachment;
}

nri::Descriptor* NriInteropAccess::CreateTextureView(
    NriInteropContext& context, VkImage image, bool storage,
    uint32_t mipOffset, uint32_t mipNum) {
    const auto found = context.mImpl->Textures.find(image);
    if (found == context.mImpl->Textures.end() || mipNum == 0U) return nullptr;
    nri::TextureViewDesc view{};
    view.texture = found->second.Texture;
    view.type = storage ? nri::TextureView::STORAGE_TEXTURE
                        : nri::TextureView::TEXTURE;
    view.format = found->second.Format;
    view.mipOffset = static_cast<nri::Dim_t>(mipOffset);
    view.mipNum = static_cast<nri::Dim_t>(mipNum);
    view.layerNum = 1;
    nri::Descriptor* descriptor = nullptr;
    if (context.mImpl->Core.CreateTextureView(view, descriptor) !=
        nri::Result::SUCCESS)
        return nullptr;
    found->second.ExtraViews.push_back(descriptor);
    return descriptor;
}

bool NriInteropAccess::DestroyTextureView(
    NriInteropContext& context, VkImage image,
    nri::Descriptor* descriptor) {
    if (descriptor == nullptr) return false;
    const auto texture = context.mImpl->Textures.find(image);
    if (texture == context.mImpl->Textures.end()) return false;
    auto& views = texture->second.ExtraViews;
    const auto view = std::find(views.begin(), views.end(), descriptor);
    if (view == views.end()) return false;
    context.mImpl->Core.DestroyDescriptor(descriptor);
    views.erase(view);
    return true;
}

bool NriInteropAccess::CmdTextureBarrier(
    NriInteropContext& context, uint32_t frameIndex, VkImage image,
    const ResourceTransition& transition, uint32_t mipOffset,
    uint32_t mipNum) {
    const NriTextureTransitionDesc desc{
        image, transition, mipOffset, mipNum};
    return CmdTextureBarriers(context, frameIndex, &desc, 1);
}

bool NriInteropAccess::CmdTextureBarriers(
    NriInteropContext& context, uint32_t frameIndex,
    const NriTextureTransitionDesc* transitions, uint32_t transitionNum) {
    const auto command = context.mImpl->CommandBuffers.find(frameIndex);
    if (command == context.mImpl->CommandBuffers.end() ||
        (transitionNum != 0U && transitions == nullptr))
        return false;

    const auto state = [](ResourceAccess access) {
        nri::AccessLayoutStage result{};
        switch (access) {
            case ResourceAccess::Undefined:
                result.access = nri::AccessBits::NONE;
                result.layout = nri::Layout::UNDEFINED;
                result.stages = nri::StageBits::NONE;
                break;
            case ResourceAccess::ColorAttachment:
                result.access = nri::AccessBits::COLOR_ATTACHMENT;
                result.layout = nri::Layout::COLOR_ATTACHMENT;
                result.stages = nri::StageBits::COLOR_ATTACHMENT;
                break;
            case ResourceAccess::DepthAttachment:
                result.access = nri::AccessBits::DEPTH_STENCIL_ATTACHMENT;
                result.layout = nri::Layout::DEPTH_STENCIL_ATTACHMENT;
                result.stages = nri::StageBits::DEPTH_STENCIL_ATTACHMENT;
                break;
            case ResourceAccess::ShaderRead:
                result.access = nri::AccessBits::SHADER_RESOURCE;
                result.layout = nri::Layout::SHADER_RESOURCE;
                result.stages =
                    nri::StageBits::VERTEX_SHADER |
                    nri::StageBits::FRAGMENT_SHADER |
                    nri::StageBits::COMPUTE_SHADER;
                break;
            case ResourceAccess::ComputeWrite:
                result.access = nri::AccessBits::SHADER_RESOURCE_STORAGE;
                result.layout = nri::Layout::SHADER_RESOURCE_STORAGE;
                result.stages = nri::StageBits::COMPUTE_SHADER;
                break;
            case ResourceAccess::TransferRead:
                result.access = nri::AccessBits::COPY_SOURCE;
                result.layout = nri::Layout::COPY_SOURCE;
                result.stages = nri::StageBits::COPY;
                break;
            case ResourceAccess::TransferWrite:
                result.access = nri::AccessBits::COPY_DESTINATION;
                result.layout = nri::Layout::COPY_DESTINATION;
                result.stages = nri::StageBits::COPY;
                break;
            case ResourceAccess::StorageClear:
                result.access = nri::AccessBits::CLEAR_STORAGE;
                result.layout = nri::Layout::SHADER_RESOURCE_STORAGE;
                result.stages = nri::StageBits::CLEAR_STORAGE;
                break;
            case ResourceAccess::StorageReadWrite:
                result.access =
                    nri::AccessBits::SHADER_RESOURCE_STORAGE;
                result.layout =
                    nri::Layout::SHADER_RESOURCE_STORAGE;
                result.stages =
                    nri::StageBits::VERTEX_SHADER |
                    nri::StageBits::FRAGMENT_SHADER |
                    nri::StageBits::COMPUTE_SHADER;
                break;
            case ResourceAccess::Present:
                result.access = nri::AccessBits::NONE;
                result.layout = nri::Layout::PRESENT;
                result.stages = nri::StageBits::NONE;
                break;
        }
        return result;
    };

    std::vector<nri::TextureBarrierDesc> textureBarriers;
    textureBarriers.reserve(transitionNum);
    for (uint32_t i = 0; i < transitionNum; ++i) {
        const auto& requested = transitions[i];
        if (!requested.Transition.Required()) continue;
        const auto texture = context.mImpl->Textures.find(requested.Image);
        if (texture == context.mImpl->Textures.end()) return false;
        nri::TextureBarrierDesc barrier{};
        barrier.texture = texture->second.Texture;
        barrier.before = state(requested.Transition.Before.Access);
        barrier.before.stages = MergeNriStageScopes(barrier.before.stages, requested.ExternalWaitStages);
        barrier.after = state(requested.Transition.After.Access);
        barrier.mipOffset =
            static_cast<nri::Dim_t>(requested.MipOffset);
        barrier.mipNum = static_cast<nri::Dim_t>(requested.MipNum);
        barrier.layerNum = nri::REMAINING;
        barrier.planes = nri::PlaneBits::ALL;
        textureBarriers.push_back(barrier);
    }
    if (textureBarriers.empty()) return true;
    const nri::BarrierDesc barrier{
        nullptr, 0, nullptr, 0, textureBarriers.data(),
        static_cast<uint32_t>(textureBarriers.size())};
    context.mImpl->Core.CmdBarrier(*command->second, barrier);
    return true;
}

bool NriInteropAccess::CmdGlobalBarrier(
    NriInteropContext& context, uint32_t frameIndex,
    nri::AccessStage before, nri::AccessStage after) {
    const auto command = context.mImpl->CommandBuffers.find(frameIndex);
    if (command == context.mImpl->CommandBuffers.end())
        return false;
    const nri::GlobalBarrierDesc global{before, after};
    const nri::BarrierDesc barrier{&global, 1, nullptr, 0, nullptr, 0};
    context.mImpl->Core.CmdBarrier(*command->second, barrier);
    return true;
}

nri::Pipeline* NriInteropAccess::WrapGraphicsPipeline(
    NriInteropContext& context, VkPipeline pipeline) {
    if (!context.Available() || pipeline == VK_NULL_HANDLE)
        return nullptr;
    nri::PipelineVKDesc desc{};
    desc.vkPipeline = reinterpret_cast<uint64_t>(pipeline);
    desc.vkPipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    nri::Pipeline* wrapped = nullptr;
    if (context.mImpl->Wrapper.CreatePipelineVK(
            *context.mImpl->Device, desc, wrapped) !=
        nri::Result::SUCCESS)
        return nullptr;
    return wrapped;
}

void NriInteropAccess::DestroyPipelineWrapper(
    NriInteropContext& context, nri::Pipeline* pipeline) {
    if (pipeline != nullptr)
        context.mImpl->Core.DestroyPipeline(pipeline);
}

bool NriInteropAccess::CmdSetPipeline(
    NriInteropContext& context, uint32_t frameIndex,
    nri::Pipeline* pipeline) {
    const auto command = context.mImpl->CommandBuffers.find(frameIndex);
    if (pipeline == nullptr ||
        command == context.mImpl->CommandBuffers.end())
        return false;
    context.mImpl->Core.CmdSetPipeline(*command->second, *pipeline);
    return true;
}
#endif

NriInteropContext::NriInteropContext() : mImpl(std::make_unique<Impl>()) {}
NriInteropContext::~NriInteropContext() { Shutdown(); }

bool NriInteropContext::Initialize(VkInstance instance, VkPhysicalDevice physicalDevice,
                                   VkDevice device, uint32_t graphicsQueueFamily,
                                   bool synchronization2Enabled,
                                   bool dynamicRenderingEnabled,
                                   bool nisDeviceFeaturesEnabled,
                                   bool fsrDeviceFeaturesEnabled,
                                   bool swapchainExtensionsEnabled,
                                   RendererValidationTelemetry*
                                       validationTelemetry) {
    Shutdown();
    mImpl->ValidationTelemetry = validationTelemetry;
#ifdef ENABLE_OOT3D_NRI
    if (!synchronization2Enabled) {
        mImpl->Reason = "VK_KHR_synchronization2 is unavailable";
        return false;
    }

    nri::QueueFamilyVKDesc queueFamily{};
    queueFamily.queueNum = 1;
    queueFamily.queueType = nri::QueueType::GRAPHICS;
    queueFamily.familyIndex = graphicsQueueFamily;

    nri::DeviceCreationVKDesc desc{};
    nri::CallbackInterface callback{};
    if (validationTelemetry != nullptr) {
        callback.MessageCallback = Impl::ValidationMessage;
        callback.AbortExecution =
            Impl::ContinueAfterValidationError;
        callback.userArg = mImpl.get();
        desc.callbackInterface = callback;
        desc.enableNRIValidation = true;
        validationTelemetry->SetEnabled(
            RendererValidationSource::Nri, true);
    }
    desc.vkInstance = instance;
    desc.vkPhysicalDevice = physicalDevice;
    desc.vkDevice = device;
    desc.queueFamilies = &queueFamily;
    desc.queueFamilyNum = 1;
    desc.minorVersion = 2;
    std::vector<const char*> deviceExtensions{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};
    if (dynamicRenderingEnabled)
        deviceExtensions.push_back(
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    desc.vkExtensions.deviceExtensions = deviceExtensions.data();
    desc.vkExtensions.deviceExtensionNum =
        static_cast<uint32_t>(deviceExtensions.size());
    std::vector<const char*> instanceExtensions;
#ifdef _WIN32
    if (swapchainExtensionsEnabled) {
        instanceExtensions = {
            "VK_KHR_surface",
            "VK_KHR_win32_surface",
            "VK_KHR_get_surface_capabilities2",
        };
    }
#else
    (void)swapchainExtensionsEnabled;
#endif
    desc.vkExtensions.instanceExtensions =
        instanceExtensions.empty() ? nullptr : instanceExtensions.data();
    desc.vkExtensions.instanceExtensionNum =
        static_cast<uint32_t>(instanceExtensions.size());
    if (nriCreateDeviceFromVKDevice(desc, mImpl->Device) != nri::Result::SUCCESS) {
        mImpl->Reason = "nriCreateDeviceFromVKDevice failed";
        return false;
    }
    mImpl->DynamicRendering = dynamicRenderingEnabled;
    if (nriGetInterface(*mImpl->Device, "CoreInterface", sizeof(nri::CoreInterface), &mImpl->Core) !=
            nri::Result::SUCCESS ||
        nriGetInterface(*mImpl->Device, "WrapperVKInterface", sizeof(nri::WrapperVKInterface), &mImpl->Wrapper) !=
            nri::Result::SUCCESS) {
        mImpl->Reason = "NRI Vulkan wrapper interfaces are unavailable";
        Shutdown();
        return false;
    }
    if (nriGetInterface(*mImpl->Device, "UpscalerInterface",
                        sizeof(nri::UpscalerInterface),
                        &mImpl->UpscalerInterface) == nri::Result::SUCCESS) {
        if (mImpl->UpscalerInterface.IsUpscalerSupported(
                *mImpl->Device, nri::UpscalerType::NIS) &&
            nisDeviceFeaturesEnabled)
            mImpl->NisReason.clear();
        if (mImpl->UpscalerInterface.IsUpscalerSupported(
                *mImpl->Device, nri::UpscalerType::FSR) &&
            fsrDeviceFeaturesEnabled)
            mImpl->FsrReason.clear();
        if (mImpl->UpscalerInterface.IsUpscalerSupported(
                *mImpl->Device, nri::UpscalerType::DLSR))
            mImpl->DlssReason.clear();
    } else {
        mImpl->UpscalerInterface = {};
        mImpl->NisReason = "NRI was built without a Vulkan NIS provider";
        mImpl->FsrReason = "NRI was built without a Vulkan FSR provider";
        mImpl->DlssReason = "NRI was built without a Vulkan DLSS provider";
    }
    mImpl->Reason.clear();
    return true;
#else
    (void)instance; (void)physicalDevice; (void)device;
    (void)graphicsQueueFamily; (void)synchronization2Enabled;
    (void)dynamicRenderingEnabled; (void)nisDeviceFeaturesEnabled;
    (void)fsrDeviceFeaturesEnabled;
    (void)swapchainExtensionsEnabled;
    return false;
#endif
}

void NriInteropContext::Shutdown() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Device != nullptr) {
        if (mImpl->Nis != nullptr &&
            mImpl->UpscalerInterface.DestroyUpscaler != nullptr) {
            mImpl->UpscalerInterface.DestroyUpscaler(mImpl->Nis);
        }
        mImpl->Nis = nullptr;
        if (mImpl->Fsr != nullptr &&
            mImpl->UpscalerInterface.DestroyUpscaler != nullptr)
            mImpl->UpscalerInterface.DestroyUpscaler(mImpl->Fsr);
        mImpl->Fsr = nullptr;
        if (mImpl->Dlss != nullptr &&
            mImpl->UpscalerInterface.DestroyUpscaler != nullptr)
            mImpl->UpscalerInterface.DestroyUpscaler(mImpl->Dlss);
        mImpl->Dlss = nullptr;
        for (auto& [_, texture] : mImpl->Textures) {
            for (nri::Descriptor* view : texture.ExtraViews)
                mImpl->Core.DestroyDescriptor(view);
            if (texture.Sampled != nullptr)
                mImpl->Core.DestroyDescriptor(texture.Sampled);
            if (texture.Storage != nullptr)
                mImpl->Core.DestroyDescriptor(texture.Storage);
            if (texture.ColorAttachment != nullptr)
                mImpl->Core.DestroyDescriptor(texture.ColorAttachment);
            if (texture.DepthAttachment != nullptr)
                mImpl->Core.DestroyDescriptor(texture.DepthAttachment);
            mImpl->Core.DestroyTexture(texture.Texture);
        }
        for (auto& [_, commandBuffer] : mImpl->CommandBuffers)
            mImpl->Core.DestroyCommandBuffer(commandBuffer);
        for (auto& [_, buffer] : mImpl->Buffers)
            mImpl->Core.DestroyBuffer(buffer);
        mImpl->Textures.clear();
        mImpl->Buffers.clear();
        mImpl->CommandBuffers.clear();
        nriDestroyDevice(mImpl->Device);
        mImpl->Device = nullptr;
        mImpl->Core = {};
        mImpl->Wrapper = {};
        mImpl->UpscalerInterface = {};
        mImpl->NisWidth = 0;
        mImpl->NisHeight = 0;
        mImpl->NisInputSrgb = true;
        mImpl->FsrWidth = 0;
        mImpl->FsrHeight = 0;
        mImpl->FsrInputSrgb = true;
        mImpl->DlssWidth = 0;
        mImpl->DlssHeight = 0;
        mImpl->DlssInputSrgb = true;
    }
#endif
}

bool NriInteropContext::WrapFrameCommandBuffer(uint32_t frameIndex,
                                                VkCommandBuffer commandBuffer) {
#ifdef ENABLE_OOT3D_NRI
    if (!Available()) return false;
    if (auto it = mImpl->CommandBuffers.find(frameIndex); it != mImpl->CommandBuffers.end()) {
        mImpl->Core.DestroyCommandBuffer(it->second);
        mImpl->CommandBuffers.erase(it);
    }
    nri::CommandBufferVKDesc desc{};
    desc.vkCommandBuffer = commandBuffer;
    desc.queueType = nri::QueueType::GRAPHICS;
    nri::CommandBuffer* wrapped = nullptr;
    if (mImpl->Wrapper.CreateCommandBufferVK(*mImpl->Device, desc, wrapped) != nri::Result::SUCCESS)
        return false;
    mImpl->CommandBuffers.emplace(frameIndex, wrapped);
    return true;
#else
    (void)frameIndex; (void)commandBuffer;
    return false;
#endif
}

bool NriInteropContext::WrapTexture(VkImage image, VkFormat format, VkImageType type,
                                    VkImageUsageFlags usage, uint32_t width, uint32_t height,
                                    uint32_t mipLevels, uint32_t layers,
                                    VkSampleCountFlagBits samples) {
#ifdef ENABLE_OOT3D_NRI
    if (!Available() || image == VK_NULL_HANDLE) return false;
    if (mImpl->Textures.contains(image)) return true;
    const nri::Format nriFormat =
        nri::nriConvertVKFormatToNRI(format);
    if (nriFormat == nri::Format::UNKNOWN) return false;
    nri::TextureVKDesc desc{};
    desc.vkImage = reinterpret_cast<uint64_t>(image);
    desc.vkFormat = static_cast<int32_t>(format);
    desc.vkImageType = static_cast<int32_t>(type);
    desc.vkImageUsageFlags = usage;
    desc.width = static_cast<nri::Dim_t>(width);
    desc.height = static_cast<nri::Dim_t>(height);
    desc.depth = 1;
    desc.mipNum = static_cast<nri::Dim_t>(mipLevels);
    desc.layerNum = static_cast<nri::Dim_t>(layers);
    desc.sampleNum = static_cast<nri::Sample_t>(samples);
    nri::Texture* wrapped = nullptr;
    if (mImpl->Wrapper.CreateTextureVK(*mImpl->Device, desc, wrapped) != nri::Result::SUCCESS)
        return false;
    Impl::WrappedTexture record;
    record.Texture = wrapped;
    record.Format = nriFormat;
    mImpl->Textures.emplace(image, record);
    return true;
#else
    (void)image; (void)format; (void)type; (void)usage; (void)width; (void)height;
    (void)mipLevels; (void)layers; (void)samples;
    return false;
#endif
}

bool NriInteropContext::WrapBuffer(
    VkBuffer buffer, uint64_t size, uint8_t* mappedMemory) {
#ifdef ENABLE_OOT3D_NRI
    if (!Available() || buffer == VK_NULL_HANDLE || size == 0U)
        return false;
    if (mImpl->Buffers.contains(buffer))
        return true;
    nri::BufferVKDesc desc{};
    desc.vkBuffer = reinterpret_cast<uint64_t>(buffer);
    desc.size = size;
    desc.mappedMemory = mappedMemory;
    nri::Buffer* wrapped = nullptr;
    if (mImpl->Wrapper.CreateBufferVK(
            *mImpl->Device, desc, wrapped) != nri::Result::SUCCESS)
        return false;
    mImpl->Buffers.emplace(buffer, wrapped);
    return true;
#else
    (void)buffer;
    (void)size;
    (void)mappedMemory;
    return false;
#endif
}

bool NriInteropContext::CreateOwnedTexture2D(
    uint32_t width, uint32_t height, VkFormat format,
    NriOwnedTexture2D& output, uint32_t mipLevels,
    bool createStorageView) {
    NriOwnedTexture2DDesc desc;
    desc.Width = width;
    desc.Height = height;
    desc.Format = format;
    desc.Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    if (createStorageView)
        desc.Usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    desc.MipLevels = mipLevels;
    return CreateOwnedTexture2D(desc, output);
}

bool NriInteropContext::CreateOwnedTexture2D(
    const NriOwnedTexture2DDesc& desc,
    NriOwnedTexture2D& output) {
    output = {};
#ifdef ENABLE_OOT3D_NRI
    if (!Available() || desc.Width == 0U || desc.Height == 0U ||
        desc.MipLevels == 0U || desc.Layers == 0U ||
        desc.Format == VK_FORMAT_UNDEFINED || desc.Usage == 0U)
        return false;
    if (desc.CubeSampledView &&
        (desc.Width != desc.Height || desc.Layers != 6U ||
         desc.Samples != VK_SAMPLE_COUNT_1_BIT))
        return false;
    const nri::Format nriFormat =
        nri::nriConvertVKFormatToNRI(desc.Format);
    if (nriFormat == nri::Format::UNKNOWN) return false;

    nri::TextureDesc textureDesc{};
    textureDesc.type = nri::TextureType::TEXTURE_2D;
    if ((desc.Usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0U)
        textureDesc.usage |=
            nri::TextureUsageBits::SHADER_RESOURCE;
    if ((desc.Usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0U)
        textureDesc.usage |=
            nri::TextureUsageBits::SHADER_RESOURCE_STORAGE;
    if ((desc.Usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0U)
        textureDesc.usage |=
            nri::TextureUsageBits::COLOR_ATTACHMENT;
    if ((desc.Usage &
         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U)
        textureDesc.usage |=
            nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT;
    if ((desc.Usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) != 0U)
        textureDesc.usage |=
            nri::TextureUsageBits::INPUT_ATTACHMENT;
    if (textureDesc.usage == nri::TextureUsageBits::NONE)
        return false;
    textureDesc.format = nriFormat;
    textureDesc.width = static_cast<nri::Dim_t>(desc.Width);
    textureDesc.height = static_cast<nri::Dim_t>(desc.Height);
    textureDesc.depth = 1;
    textureDesc.mipNum =
        static_cast<nri::Dim_t>(desc.MipLevels);
    textureDesc.layerNum =
        static_cast<nri::Dim_t>(desc.Layers);
    textureDesc.sampleNum =
        static_cast<nri::Sample_t>(desc.Samples);
    nri::Texture* texture = nullptr;
    if (mImpl->Core.CreateCommittedTexture(
            *mImpl->Device, nri::MemoryLocation::DEVICE, 0.0F,
            textureDesc, texture) != nri::Result::SUCCESS)
        return false;

    Impl::WrappedTexture record;
    record.Texture = texture;
    record.Format = nriFormat;
    record.Owned = true;
    const auto createView = [&](
                                nri::TextureView type,
                                nri::Descriptor*& descriptor) {
        nri::TextureViewDesc view{};
        view.texture = texture;
        view.type =
            type == nri::TextureView::TEXTURE && desc.CubeSampledView
                ? nri::TextureView::TEXTURE_CUBE
            : type == nri::TextureView::TEXTURE && desc.Layers > 1U
                ? nri::TextureView::TEXTURE_ARRAY
            : type == nri::TextureView::STORAGE_TEXTURE &&
                    desc.Layers > 1U
                ? nri::TextureView::STORAGE_TEXTURE_ARRAY
                : type;
        view.format = nriFormat;
        view.mipNum = type == nri::TextureView::TEXTURE
            ? static_cast<nri::Dim_t>(desc.MipLevels)
            : 1;
        view.layerNum =
            static_cast<nri::Dim_t>(desc.Layers);
        return mImpl->Core.CreateTextureView(view, descriptor) ==
               nri::Result::SUCCESS;
    };
    const bool sampled =
        (desc.Usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0U;
    const bool storage =
        (desc.Usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0U;
    const bool colorAttachment =
        (desc.Usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0U;
    const bool depthAttachment =
        (desc.Usage &
         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
    const bool viewsCreated =
        (!sampled ||
         createView(nri::TextureView::TEXTURE, record.Sampled)) &&
        (!storage ||
         createView(
             nri::TextureView::STORAGE_TEXTURE, record.Storage)) &&
        (!colorAttachment ||
         createView(
             nri::TextureView::COLOR_ATTACHMENT,
             record.ColorAttachment)) &&
        (!depthAttachment ||
         createView(
             nri::TextureView::DEPTH_STENCIL_ATTACHMENT,
             record.DepthAttachment));
    if (!viewsCreated) {
        if (record.Sampled != nullptr)
            mImpl->Core.DestroyDescriptor(record.Sampled);
        if (record.Storage != nullptr)
            mImpl->Core.DestroyDescriptor(record.Storage);
        if (record.ColorAttachment != nullptr)
            mImpl->Core.DestroyDescriptor(record.ColorAttachment);
        if (record.DepthAttachment != nullptr)
            mImpl->Core.DestroyDescriptor(record.DepthAttachment);
        mImpl->Core.DestroyTexture(texture);
        return false;
    }
    output.Image = reinterpret_cast<VkImage>(
        mImpl->Core.GetTextureNativeObject(texture));
    output.SampledView = reinterpret_cast<VkImageView>(
        mImpl->Core.GetDescriptorNativeObject(record.Sampled));
    output.StorageView = record.Storage != nullptr
        ? reinterpret_cast<VkImageView>(
              mImpl->Core.GetDescriptorNativeObject(record.Storage))
        : VK_NULL_HANDLE;
    output.ColorAttachmentView =
        record.ColorAttachment != nullptr
        ? reinterpret_cast<VkImageView>(
              mImpl->Core.GetDescriptorNativeObject(
                  record.ColorAttachment))
        : VK_NULL_HANDLE;
    output.DepthAttachmentView =
        record.DepthAttachment != nullptr
        ? reinterpret_cast<VkImageView>(
              mImpl->Core.GetDescriptorNativeObject(
                  record.DepthAttachment))
        : VK_NULL_HANDLE;
    const bool nativeObjectsReady =
        output.Image != VK_NULL_HANDLE &&
        (!sampled || output.SampledView != VK_NULL_HANDLE) &&
        (!storage || output.StorageView != VK_NULL_HANDLE) &&
        (!colorAttachment ||
         output.ColorAttachmentView != VK_NULL_HANDLE) &&
        (!depthAttachment ||
         output.DepthAttachmentView != VK_NULL_HANDLE);
    if (!nativeObjectsReady) {
        if (record.Sampled != nullptr)
            mImpl->Core.DestroyDescriptor(record.Sampled);
        if (record.Storage != nullptr)
            mImpl->Core.DestroyDescriptor(record.Storage);
        if (record.ColorAttachment != nullptr)
            mImpl->Core.DestroyDescriptor(record.ColorAttachment);
        if (record.DepthAttachment != nullptr)
            mImpl->Core.DestroyDescriptor(record.DepthAttachment);
        mImpl->Core.DestroyTexture(texture);
        output = {};
        return false;
    }
    mImpl->Textures.emplace(output.Image, record);
    return true;
#else
    (void)desc;
    return false;
#endif
}

bool NriInteropContext::OwnsTexture(VkImage image) const {
#ifdef ENABLE_OOT3D_NRI
    const auto found = mImpl->Textures.find(image);
    return found != mImpl->Textures.end() && found->second.Owned;
#else
    (void)image;
    return false;
#endif
}

bool NriInteropContext::DynamicRenderingAvailable() const {
#ifdef ENABLE_OOT3D_NRI
    return Available() && mImpl->DynamicRendering;
#else
    return false;
#endif
}

void NriInteropContext::DestroyOwnedTexture(VkImage image) {
#ifdef ENABLE_OOT3D_NRI
    auto it = mImpl->Textures.find(image);
    if (it == mImpl->Textures.end() || !it->second.Owned) return;
    for (nri::Descriptor* view : it->second.ExtraViews)
        mImpl->Core.DestroyDescriptor(view);
    if (it->second.Sampled != nullptr)
        mImpl->Core.DestroyDescriptor(it->second.Sampled);
    if (it->second.Storage != nullptr)
        mImpl->Core.DestroyDescriptor(it->second.Storage);
    if (it->second.ColorAttachment != nullptr)
        mImpl->Core.DestroyDescriptor(it->second.ColorAttachment);
    if (it->second.DepthAttachment != nullptr)
        mImpl->Core.DestroyDescriptor(it->second.DepthAttachment);
    mImpl->Core.DestroyTexture(it->second.Texture);
    mImpl->Textures.erase(it);
#else
    (void)image;
#endif
}

void NriInteropContext::ForgetTexture(VkImage image) {
#ifdef ENABLE_OOT3D_NRI
    auto it = mImpl->Textures.find(image);
    if (it != mImpl->Textures.end() && !it->second.Owned) {
        for (nri::Descriptor* view : it->second.ExtraViews)
            mImpl->Core.DestroyDescriptor(view);
        if (it->second.Sampled != nullptr)
            mImpl->Core.DestroyDescriptor(it->second.Sampled);
        if (it->second.Storage != nullptr)
            mImpl->Core.DestroyDescriptor(it->second.Storage);
        if (it->second.ColorAttachment != nullptr)
            mImpl->Core.DestroyDescriptor(it->second.ColorAttachment);
        if (it->second.DepthAttachment != nullptr)
            mImpl->Core.DestroyDescriptor(it->second.DepthAttachment);
        mImpl->Core.DestroyTexture(it->second.Texture);
        mImpl->Textures.erase(it);
    }
#else
    (void)image;
#endif
}

void NriInteropContext::ForgetBuffer(VkBuffer buffer) {
#ifdef ENABLE_OOT3D_NRI
    const auto found = mImpl->Buffers.find(buffer);
    if (found == mImpl->Buffers.end()) return;
    mImpl->Core.DestroyBuffer(found->second);
    mImpl->Buffers.erase(found);
#else
    (void)buffer;
#endif
}

bool NriInteropContext::NisAvailable() const {
#ifdef ENABLE_OOT3D_NRI
    return Available() && mImpl->UpscalerInterface.CreateUpscaler != nullptr &&
           mImpl->NisReason.empty();
#else
    return false;
#endif
}

const std::string& NriInteropContext::NisUnavailableReason() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->NisReason;
#else
    return mImpl->Reason;
#endif
}

bool NriInteropContext::DispatchNis(
    uint32_t frameIndex, VkImage inputImage, VkFormat inputFormat,
    uint32_t inputWidth, uint32_t inputHeight, VkImage outputImage,
    VkFormat outputFormat, uint32_t outputWidth, uint32_t outputHeight,
    UpscalerQuality quality, float sharpness,
    SceneColorEncoding inputEncoding) {
#ifdef ENABLE_OOT3D_NRI
    if (!NisAvailable() || inputWidth == 0U || inputHeight == 0U ||
        outputWidth == 0U || outputHeight == 0U ||
        !IsKnownSceneColorEncoding(inputEncoding))
        return false;
    const bool inputSrgb = SceneColorIsSrgb(inputEncoding);
    const auto command = mImpl->CommandBuffers.find(frameIndex);
    if (command == mImpl->CommandBuffers.end()) return false;
    if (!WrapTexture(inputImage, inputFormat, VK_IMAGE_TYPE_2D,
                     VK_IMAGE_USAGE_SAMPLED_BIT, inputWidth, inputHeight) ||
        !WrapTexture(outputImage, outputFormat, VK_IMAGE_TYPE_2D,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     outputWidth, outputHeight))
        return false;

    if (mImpl->Nis == nullptr || mImpl->NisWidth != outputWidth ||
        mImpl->NisHeight != outputHeight ||
        mImpl->NisQuality != quality ||
        mImpl->NisInputSrgb != inputSrgb) {
        if (mImpl->Nis != nullptr)
            mImpl->UpscalerInterface.DestroyUpscaler(mImpl->Nis);
        mImpl->Nis = nullptr;
        nri::UpscalerDesc desc{};
        desc.upscaleResolution = {
            static_cast<nri::Dim_t>(outputWidth),
            static_cast<nri::Dim_t>(outputHeight)};
        desc.type = nri::UpscalerType::NIS;
        desc.mode = ToNriUpscalerMode(quality);
        desc.flags = inputSrgb ? nri::UpscalerBits::SRGB
                               : nri::UpscalerBits::HDR;
        if (mImpl->UpscalerInterface.CreateUpscaler(
                *mImpl->Device, desc, mImpl->Nis) != nri::Result::SUCCESS) {
            mImpl->NisReason = "NRI failed to create the NIS upscaler";
            return false;
        }
        mImpl->NisWidth = outputWidth;
        mImpl->NisHeight = outputHeight;
        mImpl->NisQuality = quality;
        mImpl->NisInputSrgb = inputSrgb;
    }

    nri::Descriptor* inputView =
        mImpl->GetUpscalerView(inputImage, false);
    nri::Descriptor* outputView =
        mImpl->GetUpscalerView(outputImage, true);
    if (inputView == nullptr || outputView == nullptr) return false;

    const auto& input = mImpl->Textures.at(inputImage);
    const auto& output = mImpl->Textures.at(outputImage);
    nri::DispatchUpscaleDesc dispatch{};
    dispatch.input = {input.Texture, inputView};
    dispatch.output = {output.Texture, outputView};
    dispatch.currentResolution = {
        static_cast<nri::Dim_t>(inputWidth),
        static_cast<nri::Dim_t>(inputHeight)};
    dispatch.settings.nis.sharpness = std::clamp(sharpness, 0.0F, 1.0F);
    mImpl->UpscalerInterface.CmdDispatchUpscale(
        *command->second, *mImpl->Nis, dispatch);
    return true;
#else
    (void)frameIndex; (void)inputImage; (void)inputFormat;
    (void)inputWidth; (void)inputHeight; (void)outputImage;
    (void)outputFormat; (void)outputWidth; (void)outputHeight;
    (void)quality; (void)sharpness; (void)inputEncoding;
    return false;
#endif
}

bool NriInteropContext::FsrAvailable() const {
#ifdef ENABLE_OOT3D_NRI
    return Available() && mImpl->UpscalerInterface.CreateUpscaler != nullptr &&
           mImpl->FsrReason.empty();
#else
    return false;
#endif
}

const std::string& NriInteropContext::FsrUnavailableReason() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->FsrReason;
#else
    return mImpl->Reason;
#endif
}

bool NriInteropContext::DispatchFsr(
    const NriTemporalUpscaleDispatchDesc& desc) {
#ifdef ENABLE_OOT3D_NRI
    if (!FsrAvailable() || desc.InputWidth == 0U || desc.InputHeight == 0U ||
        desc.OutputWidth == 0U || desc.OutputHeight == 0U ||
        !IsKnownSceneColorEncoding(desc.InputEncoding))
        return false;
    const bool inputSrgb = SceneColorIsSrgb(desc.InputEncoding);
    const auto command = mImpl->CommandBuffers.find(desc.FrameIndex);
    if (command == mImpl->CommandBuffers.end()) return false;
    const auto wrapSampled = [&](VkImage image, VkFormat format,
                                 uint32_t width, uint32_t height) {
        return WrapTexture(image, format, VK_IMAGE_TYPE_2D,
                           VK_IMAGE_USAGE_SAMPLED_BIT, width, height);
    };
    if (!wrapSampled(desc.InputImage, desc.InputFormat,
                     desc.InputWidth, desc.InputHeight) ||
        !wrapSampled(desc.DepthImage, desc.DepthFormat,
                     desc.InputWidth, desc.InputHeight) ||
        !wrapSampled(desc.MotionImage, desc.MotionFormat,
                     desc.InputWidth, desc.InputHeight) ||
        !wrapSampled(desc.ReactiveImage, desc.ReactiveFormat,
                     desc.InputWidth, desc.InputHeight) ||
        !WrapTexture(desc.OutputImage, desc.OutputFormat, VK_IMAGE_TYPE_2D,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     desc.OutputWidth, desc.OutputHeight))
        return false;

    if (mImpl->Fsr == nullptr || mImpl->FsrWidth != desc.OutputWidth ||
        mImpl->FsrHeight != desc.OutputHeight ||
        mImpl->FsrQuality != desc.Quality ||
        mImpl->FsrInputSrgb != inputSrgb) {
        if (mImpl->Fsr != nullptr)
            mImpl->UpscalerInterface.DestroyUpscaler(mImpl->Fsr);
        mImpl->Fsr = nullptr;
        nri::UpscalerDesc upscaler{};
        upscaler.upscaleResolution = {
            static_cast<nri::Dim_t>(desc.OutputWidth),
            static_cast<nri::Dim_t>(desc.OutputHeight)};
        upscaler.type = nri::UpscalerType::FSR;
        upscaler.mode = ToNriUpscalerMode(desc.Quality);
        upscaler.flags =
            (inputSrgb ? nri::UpscalerBits::SRGB
                       : nri::UpscalerBits::HDR) |
            nri::UpscalerBits::USE_REACTIVE |
            nri::UpscalerBits::DEPTH_LINEAR |
            nri::UpscalerBits::MV_JITTERED;
        if (mImpl->UpscalerInterface.CreateUpscaler(
                *mImpl->Device, upscaler, mImpl->Fsr) !=
            nri::Result::SUCCESS) {
            mImpl->FsrReason =
                "NRI failed to create FSR (amd_fidelityfx_vk.dll missing or incompatible)";
            return false;
        }
        mImpl->FsrWidth = desc.OutputWidth;
        mImpl->FsrHeight = desc.OutputHeight;
        mImpl->FsrQuality = desc.Quality;
        mImpl->FsrInputSrgb = inputSrgb;
    }

    nri::DispatchUpscaleDesc dispatch{};
    dispatch.output =
        mImpl->GetUpscalerResource(desc.OutputImage, true);
    dispatch.input =
        mImpl->GetUpscalerResource(desc.InputImage);
    dispatch.guides.upscaler.depth =
        mImpl->GetUpscalerResource(desc.DepthImage);
    dispatch.guides.upscaler.mv =
        mImpl->GetUpscalerResource(desc.MotionImage);
    dispatch.guides.upscaler.reactive =
        mImpl->GetUpscalerResource(desc.ReactiveImage);
    if (dispatch.output.descriptor == nullptr ||
        dispatch.input.descriptor == nullptr ||
        dispatch.guides.upscaler.depth.descriptor == nullptr ||
        dispatch.guides.upscaler.mv.descriptor == nullptr ||
        dispatch.guides.upscaler.reactive.descriptor == nullptr)
        return false;
    dispatch.settings.fsr.zNear = desc.NearPlane;
    dispatch.settings.fsr.zFar = desc.FarPlane;
    dispatch.settings.fsr.verticalFov = desc.VerticalFovRadians;
    dispatch.settings.fsr.frameTime = desc.FrameTimeMilliseconds;
    dispatch.settings.fsr.viewSpaceToMetersFactor =
        1.0F / std::max(desc.ViewUnitsPerMeter, 1.0e-6F);
    dispatch.settings.fsr.sharpness = std::clamp(desc.Sharpness, 0.0F, 1.0F);
    dispatch.currentResolution = {
        static_cast<nri::Dim_t>(desc.InputWidth),
        static_cast<nri::Dim_t>(desc.InputHeight)};
    dispatch.cameraJitter = {
        desc.JitterPixels[0], desc.JitterPixels[1]};
    dispatch.mvScale = {
        static_cast<float>(desc.InputWidth),
        static_cast<float>(desc.InputHeight)};
    dispatch.flags = desc.ResetHistory
        ? nri::DispatchUpscaleBits::RESET_HISTORY
        : nri::DispatchUpscaleBits::NONE;
    mImpl->UpscalerInterface.CmdDispatchUpscale(
        *command->second, *mImpl->Fsr, dispatch);
    return true;
#else
    (void)desc;
    return false;
#endif
}

bool NriInteropContext::DlssAvailable() const {
#ifdef ENABLE_OOT3D_NRI
    return Available() && mImpl->UpscalerInterface.CreateUpscaler != nullptr &&
           mImpl->DlssReason.empty();
#else
    return false;
#endif
}

const std::string& NriInteropContext::DlssUnavailableReason() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->DlssReason;
#else
    return mImpl->Reason;
#endif
}

bool NriInteropContext::DispatchDlss(
    const NriTemporalUpscaleDispatchDesc& desc) {
#ifdef ENABLE_OOT3D_NRI
    if (!DlssAvailable() || desc.InputWidth == 0U || desc.InputHeight == 0U ||
        desc.OutputWidth == 0U || desc.OutputHeight == 0U ||
        !IsKnownSceneColorEncoding(desc.InputEncoding))
        return false;
    const bool inputSrgb = SceneColorIsSrgb(desc.InputEncoding);
    const auto command = mImpl->CommandBuffers.find(desc.FrameIndex);
    if (command == mImpl->CommandBuffers.end()) return false;
    const auto wrapSampled = [&](VkImage image, VkFormat format,
                                 uint32_t width, uint32_t height) {
        return WrapTexture(image, format, VK_IMAGE_TYPE_2D,
                           VK_IMAGE_USAGE_SAMPLED_BIT, width, height);
    };
    if (!wrapSampled(desc.InputImage, desc.InputFormat,
                     desc.InputWidth, desc.InputHeight) ||
        !wrapSampled(desc.DepthImage, desc.DepthFormat,
                     desc.InputWidth, desc.InputHeight) ||
        !wrapSampled(desc.MotionImage, desc.MotionFormat,
                     desc.InputWidth, desc.InputHeight) ||
        !wrapSampled(desc.ReactiveImage, desc.ReactiveFormat,
                     desc.InputWidth, desc.InputHeight) ||
        !WrapTexture(desc.OutputImage, desc.OutputFormat, VK_IMAGE_TYPE_2D,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     desc.OutputWidth, desc.OutputHeight))
        return false;

    if (mImpl->Dlss == nullptr || mImpl->DlssWidth != desc.OutputWidth ||
        mImpl->DlssHeight != desc.OutputHeight ||
        mImpl->DlssQuality != desc.Quality ||
        mImpl->DlssInputSrgb != inputSrgb) {
        if (mImpl->Dlss != nullptr)
            mImpl->UpscalerInterface.DestroyUpscaler(mImpl->Dlss);
        mImpl->Dlss = nullptr;
        nri::UpscalerDesc upscaler{};
        upscaler.upscaleResolution = {
            static_cast<nri::Dim_t>(desc.OutputWidth),
            static_cast<nri::Dim_t>(desc.OutputHeight)};
        upscaler.type = nri::UpscalerType::DLSR;
        upscaler.mode = ToNriUpscalerMode(desc.Quality);
        upscaler.flags =
            (inputSrgb ? nri::UpscalerBits::SRGB
                       : nri::UpscalerBits::HDR) |
            nri::UpscalerBits::USE_REACTIVE |
            nri::UpscalerBits::MV_JITTERED;
        // NGX records feature-creation commands into the already-open frame
        // command buffer, avoiding an out-of-band queue submission.
        upscaler.commandBuffer = command->second;
        if (mImpl->UpscalerInterface.CreateUpscaler(
                *mImpl->Device, upscaler, mImpl->Dlss) !=
            nri::Result::SUCCESS) {
            mImpl->DlssReason =
                "NRI failed to create DLSS (NGX runtime or GPU support unavailable)";
            return false;
        }
        mImpl->DlssWidth = desc.OutputWidth;
        mImpl->DlssHeight = desc.OutputHeight;
        mImpl->DlssQuality = desc.Quality;
        mImpl->DlssInputSrgb = inputSrgb;
    }

    nri::DispatchUpscaleDesc dispatch{};
    dispatch.output =
        mImpl->GetUpscalerResource(desc.OutputImage, true);
    dispatch.input =
        mImpl->GetUpscalerResource(desc.InputImage);
    dispatch.guides.upscaler.depth =
        mImpl->GetUpscalerResource(desc.DepthImage);
    dispatch.guides.upscaler.mv =
        mImpl->GetUpscalerResource(desc.MotionImage);
    dispatch.guides.upscaler.reactive =
        mImpl->GetUpscalerResource(desc.ReactiveImage);
    if (dispatch.output.descriptor == nullptr ||
        dispatch.input.descriptor == nullptr ||
        dispatch.guides.upscaler.depth.descriptor == nullptr ||
        dispatch.guides.upscaler.mv.descriptor == nullptr ||
        dispatch.guides.upscaler.reactive.descriptor == nullptr)
        return false;
    dispatch.currentResolution = {
        static_cast<nri::Dim_t>(desc.InputWidth),
        static_cast<nri::Dim_t>(desc.InputHeight)};
    dispatch.cameraJitter = {desc.JitterPixels[0], desc.JitterPixels[1]};
    dispatch.mvScale = {
        static_cast<float>(desc.InputWidth),
        static_cast<float>(desc.InputHeight)};
    dispatch.flags = desc.ResetHistory
        ? nri::DispatchUpscaleBits::RESET_HISTORY
        : nri::DispatchUpscaleBits::NONE;
    mImpl->UpscalerInterface.CmdDispatchUpscale(
        *command->second, *mImpl->Dlss, dispatch);
    return true;
#else
    (void)desc;
    return false;
#endif
}

bool NriInteropContext::Available() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Device != nullptr && mImpl->Reason.empty();
#else
    return false;
#endif
}

const std::string& NriInteropContext::UnavailableReason() const { return mImpl->Reason; }

} // namespace Fast::Oot3d

#endif
