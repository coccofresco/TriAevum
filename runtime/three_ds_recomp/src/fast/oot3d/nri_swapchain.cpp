#include "fast/oot3d/nri_swapchain.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"

#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"

#include <NRI.h>
#include <Extensions/NRIRayTracing.h>
#include <Extensions/NRISwapChain.h>
#include <Extensions/NRIWrapperVK.h>
#endif

#include <cstdlib>
#include <string_view>
#include <vector>

namespace Fast::Oot3d {
namespace {

#ifdef ENABLE_OOT3D_NRI
VkFormat ToVkSwapchainFormat(nri::Format format) {
    switch (format) {
        case nri::Format::BGRA8_UNORM:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case nri::Format::BGRA8_SRGB:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case nri::Format::RGBA8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case nri::Format::RGBA8_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

NriSwapchainOperationResult ToOperationResult(nri::Result result) {
    if (result == nri::Result::SUCCESS)
        return NriSwapchainOperationResult::Success;
    if (result == nri::Result::OUT_OF_DATE)
        return NriSwapchainOperationResult::OutOfDate;
    return NriSwapchainOperationResult::Failure;
}
#endif

} // namespace

struct NriSwapchain::Impl {
    NriInteropContext* Interop = nullptr;
    std::vector<NriSwapchainImage> Images;
    std::vector<VkSemaphore> AcquireSemaphores;
    std::vector<VkSemaphore> ReleaseSemaphores;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t AcquiredImage = UINT32_MAX;
    bool Initialized = false;
    std::string Reason = "NRI swapchain is not initialized";
#ifdef ENABLE_OOT3D_NRI
    nri::Device* Device = nullptr;
    nri::CoreInterface* Core = nullptr;
    nri::WrapperVKInterface Wrapper{};
    nri::SwapChainInterface SwapchainInterface{};
    nri::Queue* Queue = nullptr;
    nri::SwapChain* Swapchain = nullptr;
    VkDevice VkDeviceHandle = VK_NULL_HANDLE;
    std::vector<nri::Descriptor*> ImageViews;
    std::vector<nri::Fence*> AcquireFences;
    std::vector<nri::Fence*> ReleaseFences;
#endif
};

NriSwapchain::NriSwapchain() : mImpl(std::make_unique<Impl>()) {
}

NriSwapchain::~NriSwapchain() {
    Shutdown();
}

bool NriSwapchain::Initialize(NriInteropContext& interop) {
    Shutdown();
    if (const char* enabled =
            std::getenv("OOT3D_GRAPHICS_NRI_SWAPCHAIN");
        enabled != nullptr && std::string_view(enabled) == "0") {
        mImpl->Reason = "NRI swapchain disabled by environment";
        return false;
    }
#ifndef _WIN32
    (void)interop;
    mImpl->Reason =
        "NRI swapchain ownership is currently supported on Windows";
    return false;
#elif !defined(ENABLE_OOT3D_NRI)
    (void)interop;
    mImpl->Reason = "NRI swapchain was not compiled";
    return false;
#else
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
    mImpl->Interop = &interop;
    mImpl->Device = NriInteropAccess::Device(interop);
    mImpl->Core = NriInteropAccess::Core(interop);
    if (mImpl->Device == nullptr || mImpl->Core == nullptr ||
        nriGetInterface(
            *mImpl->Device, "WrapperVKInterface",
            sizeof(mImpl->Wrapper),
            &mImpl->Wrapper) != nri::Result::SUCCESS ||
        nriGetInterface(
            *mImpl->Device, "SwapChainInterface",
            sizeof(mImpl->SwapchainInterface),
            &mImpl->SwapchainInterface) != nri::Result::SUCCESS ||
        mImpl->Core->GetQueue(
            *mImpl->Device, nri::QueueType::GRAPHICS, 0U,
            mImpl->Queue) != nri::Result::SUCCESS ||
        mImpl->Queue == nullptr) {
        const std::string reason =
            "NRI swapchain interfaces are unavailable";
        Shutdown();
        mImpl->Reason = reason;
        return false;
    }
    mImpl->VkDeviceHandle = reinterpret_cast<VkDevice>(
        mImpl->Core->GetDeviceNativeObject(mImpl->Device));
    if (mImpl->VkDeviceHandle == VK_NULL_HANDLE) {
        const std::string reason =
            "NRI swapchain native Vulkan device is unavailable";
        Shutdown();
        mImpl->Reason = reason;
        return false;
    }
    mImpl->Initialized = true;
    mImpl->Reason.clear();
    return true;
#endif
}

bool NriSwapchain::Create(const NriSwapchainDesc& desc) {
    Destroy();
#ifndef ENABLE_OOT3D_NRI
    (void)desc;
    return false;
#else
    if (!Supported() || !ValidateNriSwapchainDesc(desc)) {
        mImpl->Reason = "NRI swapchain description is invalid";
        return false;
    }

    nri::SwapChainDesc nriDesc{};
    nriDesc.window.windows.hwnd = desc.NativeWindow;
    nriDesc.queue = mImpl->Queue;
    nriDesc.width = static_cast<nri::Dim_t>(desc.Width);
    nriDesc.height = static_cast<nri::Dim_t>(desc.Height);
    nriDesc.textureNum = static_cast<uint8_t>(desc.DesiredImageCount);
    nriDesc.format = nri::SwapChainFormat::BT709_G22_8BIT;
    nriDesc.flags = desc.Vsync ? nri::SwapChainBits::VSYNC
                              : nri::SwapChainBits::NONE;
    if (desc.AllowTearing)
        nriDesc.flags |= nri::SwapChainBits::ALLOW_TEARING;
    nriDesc.queuedFrameNum = static_cast<uint8_t>(desc.FrameCount);
    nriDesc.scaling = nri::Scaling::STRETCH;
    nriDesc.gravityX = nri::Gravity::CENTERED;
    nriDesc.gravityY = nri::Gravity::CENTERED;
    if (mImpl->SwapchainInterface.CreateSwapChain(
            *mImpl->Device, nriDesc,
            mImpl->Swapchain) != nri::Result::SUCCESS ||
        mImpl->Swapchain == nullptr) {
        mImpl->Reason = "NRI CreateSwapChain failed";
        Destroy();
        return false;
    }

    uint32_t imageCount = 0;
    nri::Texture* const* textures =
        mImpl->SwapchainInterface.GetSwapChainTextures(
            *mImpl->Swapchain, imageCount);
    if (textures == nullptr || imageCount < 2U) {
        mImpl->Reason = "NRI swapchain returned no usable images";
        Destroy();
        return false;
    }
    const nri::TextureDesc& first = mImpl->Core->GetTextureDesc(
        *textures[0]);
    mImpl->Format = ToVkSwapchainFormat(first.format);
    mImpl->Width = first.width;
    mImpl->Height = first.height;
    if (mImpl->Format == VK_FORMAT_UNDEFINED ||
        mImpl->Width == 0U || mImpl->Height == 0U) {
        mImpl->Reason = "NRI swapchain returned an unsupported format";
        Destroy();
        return false;
    }

    mImpl->Images.reserve(imageCount);
    mImpl->ImageViews.reserve(imageCount);
    for (uint32_t index = 0; index < imageCount; ++index) {
        const nri::TextureDesc& textureDesc =
            mImpl->Core->GetTextureDesc(*textures[index]);
        if (textureDesc.format != first.format ||
            textureDesc.width != first.width ||
            textureDesc.height != first.height) {
            mImpl->Reason = "NRI swapchain image contracts differ";
            Destroy();
            return false;
        }
        nri::TextureViewDesc viewDesc{};
        viewDesc.texture = textures[index];
        viewDesc.type = nri::TextureView::COLOR_ATTACHMENT;
        viewDesc.format = textureDesc.format;
        viewDesc.mipNum = 1;
        viewDesc.layerNum = 1;
        nri::Descriptor* view = nullptr;
        if (mImpl->Core->CreateTextureView(
                viewDesc, view) != nri::Result::SUCCESS ||
            view == nullptr) {
            mImpl->Reason =
                "NRI swapchain color-attachment view creation failed";
            Destroy();
            return false;
        }
        const auto image = reinterpret_cast<VkImage>(
            mImpl->Core->GetTextureNativeObject(textures[index]));
        const auto nativeView = reinterpret_cast<VkImageView>(
            mImpl->Core->GetDescriptorNativeObject(view));
        if (image == VK_NULL_HANDLE || nativeView == VK_NULL_HANDLE) {
            mImpl->Core->DestroyDescriptor(view);
            mImpl->Reason =
                "NRI swapchain native image extraction failed";
            Destroy();
            return false;
        }
        mImpl->ImageViews.push_back(view);
        mImpl->Images.push_back({image, nativeView});
    }

    mImpl->AcquireFences.resize(desc.FrameCount, nullptr);
    mImpl->AcquireSemaphores.resize(desc.FrameCount, VK_NULL_HANDLE);
    for (uint32_t index = 0; index < desc.FrameCount; ++index) {
        VkSemaphoreCreateInfo semaphoreInfo{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vkCreateSemaphore(
                mImpl->VkDeviceHandle, &semaphoreInfo, nullptr,
                &mImpl->AcquireSemaphores[index]) != VK_SUCCESS) {
            mImpl->Reason =
                "NRI swapchain acquire-semaphore creation failed";
            Destroy();
            return false;
        }
        nri::FenceVKDesc fenceDesc{};
        fenceDesc.vkTimelineSemaphore =
            reinterpret_cast<uint64_t>(
                mImpl->AcquireSemaphores[index]);
        if (mImpl->Wrapper.CreateFenceVK(
                *mImpl->Device, fenceDesc,
                mImpl->AcquireFences[index]) != nri::Result::SUCCESS ||
            mImpl->AcquireFences[index] == nullptr) {
            mImpl->Reason =
                "NRI swapchain acquire-semaphore wrapping failed";
            Destroy();
            return false;
        }
    }

    mImpl->ReleaseFences.resize(imageCount, nullptr);
    mImpl->ReleaseSemaphores.resize(imageCount, VK_NULL_HANDLE);
    for (uint32_t index = 0; index < imageCount; ++index) {
        VkSemaphoreCreateInfo semaphoreInfo{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vkCreateSemaphore(
                mImpl->VkDeviceHandle, &semaphoreInfo, nullptr,
                &mImpl->ReleaseSemaphores[index]) != VK_SUCCESS) {
            mImpl->Reason =
                "NRI swapchain release-semaphore creation failed";
            Destroy();
            return false;
        }
        nri::FenceVKDesc fenceDesc{};
        fenceDesc.vkTimelineSemaphore =
            reinterpret_cast<uint64_t>(
                mImpl->ReleaseSemaphores[index]);
        if (mImpl->Wrapper.CreateFenceVK(
                *mImpl->Device, fenceDesc,
                mImpl->ReleaseFences[index]) != nri::Result::SUCCESS ||
            mImpl->ReleaseFences[index] == nullptr) {
            mImpl->Reason =
                "NRI swapchain release-semaphore wrapping failed";
            Destroy();
            return false;
        }
    }
    mImpl->Reason.clear();
    return true;
#endif
}

void NriSwapchain::Destroy() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        for (nri::Fence* fence : mImpl->ReleaseFences)
            if (fence != nullptr) mImpl->Core->DestroyFence(fence);
        for (nri::Fence* fence : mImpl->AcquireFences)
            if (fence != nullptr) mImpl->Core->DestroyFence(fence);
        for (nri::Descriptor* view : mImpl->ImageViews)
            if (view != nullptr) mImpl->Core->DestroyDescriptor(view);
    }
    if (mImpl->VkDeviceHandle != VK_NULL_HANDLE) {
        for (VkSemaphore semaphore : mImpl->ReleaseSemaphores)
            if (semaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(
                    mImpl->VkDeviceHandle, semaphore, nullptr);
        for (VkSemaphore semaphore : mImpl->AcquireSemaphores)
            if (semaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(
                    mImpl->VkDeviceHandle, semaphore, nullptr);
    }
    mImpl->ReleaseFences.clear();
    mImpl->AcquireFences.clear();
    mImpl->ImageViews.clear();
    if (mImpl->Swapchain != nullptr) {
        mImpl->SwapchainInterface.DestroySwapChain(mImpl->Swapchain);
        mImpl->Swapchain = nullptr;
    }
#endif
    mImpl->Images.clear();
    mImpl->AcquireSemaphores.clear();
    mImpl->ReleaseSemaphores.clear();
    mImpl->Format = VK_FORMAT_UNDEFINED;
    mImpl->Width = 0;
    mImpl->Height = 0;
    mImpl->AcquiredImage = UINT32_MAX;
}

void NriSwapchain::Shutdown() {
    Destroy();
    *mImpl = {};
    mImpl->Reason = "NRI swapchain is not initialized";
}

NriSwapchainOperationResult NriSwapchain::Acquire(
    uint32_t frameIndex, uint32_t& imageIndex) {
#ifndef ENABLE_OOT3D_NRI
    (void)frameIndex;
    (void)imageIndex;
    return NriSwapchainOperationResult::Failure;
#else
    if (!Active() || frameIndex >= mImpl->AcquireFences.size())
        return NriSwapchainOperationResult::Failure;
    const auto result =
        mImpl->SwapchainInterface.AcquireNextTexture(
            *mImpl->Swapchain, *mImpl->AcquireFences[frameIndex],
            imageIndex);
    if (result == nri::Result::SUCCESS)
        mImpl->AcquiredImage = imageIndex;
    else
        mImpl->Reason = result == nri::Result::OUT_OF_DATE
            ? "NRI swapchain is out of date"
            : "NRI AcquireNextTexture failed";
    return ToOperationResult(result);
#endif
}

NriSwapchainOperationResult NriSwapchain::Present(
    uint32_t imageIndex) {
#ifndef ENABLE_OOT3D_NRI
    (void)imageIndex;
    return NriSwapchainOperationResult::Failure;
#else
    if (!Active() || imageIndex != mImpl->AcquiredImage ||
        imageIndex >= mImpl->ReleaseFences.size())
        return NriSwapchainOperationResult::Failure;
    const auto result = mImpl->SwapchainInterface.QueuePresent(
        *mImpl->Swapchain, *mImpl->ReleaseFences[imageIndex]);
    if (result == nri::Result::SUCCESS)
        mImpl->AcquiredImage = UINT32_MAX;
    else
        mImpl->Reason = result == nri::Result::OUT_OF_DATE
            ? "NRI swapchain is out of date"
            : "NRI QueuePresent failed";
    return ToOperationResult(result);
#endif
}

bool NriSwapchain::Supported() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Initialized && mImpl->Device != nullptr &&
           mImpl->Core != nullptr && mImpl->Queue != nullptr &&
           mImpl->Interop != nullptr && mImpl->Interop->Available();
#else
    return false;
#endif
}

bool NriSwapchain::Active() const {
#ifdef ENABLE_OOT3D_NRI
    return Supported() && mImpl->Swapchain != nullptr &&
           !mImpl->Images.empty();
#else
    return false;
#endif
}

VkFormat NriSwapchain::Format() const {
    return mImpl->Format;
}

uint32_t NriSwapchain::Width() const {
    return mImpl->Width;
}

uint32_t NriSwapchain::Height() const {
    return mImpl->Height;
}

std::span<const NriSwapchainImage> NriSwapchain::Images() const {
    return mImpl->Images;
}

VkSemaphore NriSwapchain::AcquireSemaphore(
    uint32_t frameIndex) const {
    return frameIndex < mImpl->AcquireSemaphores.size()
        ? mImpl->AcquireSemaphores[frameIndex] : VK_NULL_HANDLE;
}

VkSemaphore NriSwapchain::ReleaseSemaphore(
    uint32_t imageIndex) const {
    return imageIndex < mImpl->ReleaseSemaphores.size()
        ? mImpl->ReleaseSemaphores[imageIndex] : VK_NULL_HANDLE;
}

const std::string& NriSwapchain::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
