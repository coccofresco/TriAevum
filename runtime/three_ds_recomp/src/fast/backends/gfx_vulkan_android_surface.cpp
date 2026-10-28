// Vulkan backend: Android surface loss and rebuild.
//
// The Activity's window surface is destroyed when the app goes to the
// background (Home, another app, the screen off) and a new one is created
// when it returns. The VkSurfaceKHR made for the old ANativeWindow then
// reports VK_ERROR_SURFACE_LOST_KHR (or OUT_OF_DATE) and no swapchain can be
// made for it again. That is a pause, not a fatal error: no frame is begun
// while there is no surface, and on return the surface, the swapchain, the
// present worker and the NRI scanout are rebuilt for the new window. The game
// itself is held by the host (the in-game menu's hold) and by SDL, which
// blocks the main thread while the Activity is paused. Scene resources, PICA
// targets and pipelines are untouched: only presentation is rebuilt, as on a
// resize. Idea from JamalC77's Quest fork (02e67ec, 2289e42, GPL-3.0),
// reimplemented for the phone's window host.
#if defined(ENABLE_OOT3D_VULKAN) && defined(__ANDROID__)
#include "gfx_vulkan_internal.h"

#include <SDL2/SDL_syswm.h>
#include <cstdio>

namespace Fast {

void GfxRenderingAPIVulkan::NoteAndroidSurfaceLost(const char* where) noexcept {
    if (!mAndroidSurfaceLost.exchange(true))
        std::fprintf(stderr, "TRIAEVUM_SURFACE_LOST where=%s\n", where);
}

void GfxRenderingAPIVulkan::DropAndroidSurface(const char* reason) {
    WaitForAllPresents();
    StopPresentWorker();
    CheckVk(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle(drop Android surface)");
    DestroySwapchainResources();
    if (mSurface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
        mSurface = VK_NULL_HANDLE;
    }
    mAndroidNativeWindow = nullptr;
    mAndroidSurfaceLost.store(true);
    // Results of the dropped swapchain must not query or rebuild a surface
    // that is gone; the rebuild makes a fresh swapchain anyway.
    mSwapchainSuboptimal.store(false);
    mPresentSwapchainDirty.store(false);
    mSwapchainDirty = false;
    const auto pending = static_cast<VkResult>(mPresentError.load());
    if (pending == VK_ERROR_SURFACE_LOST_KHR || pending == VK_ERROR_OUT_OF_DATE_KHR)
        mPresentError.store(VK_SUCCESS);
    std::fprintf(stderr, "TRIAEVUM_SURFACE_DROPPED reason=%s\n", reason);
}

bool GfxRenderingAPIVulkan::EnsureAndroidSurface() {
    auto* window = static_cast<SDL_Window*>(mWindowBackend->GetNativeWindow());
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    void* nativeWindow = nullptr;
    if (window != nullptr && SDL_GetWindowWMInfo(window, &info) == SDL_TRUE)
        nativeWindow = info.info.android.window;
    // SDL marks the window minimized when the Activity pauses, before the
    // surface goes away, and clears the native window once it is destroyed.
    const bool minimized = window != nullptr && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0U;
    if (minimized || nativeWindow == nullptr) {
        if (mSurface != VK_NULL_HANDLE)
            DropAndroidSurface(minimized ? "minimized" : "no_window");
        return false;
    }
    if (mSurface != VK_NULL_HANDLE && !mAndroidSurfaceLost.load() && nativeWindow == mAndroidNativeWindow)
        return true;
    // Startup keeps the surface Init made: its window is the current one.
    if (mSurface != VK_NULL_HANDLE && mAndroidNativeWindow == nullptr && !mAndroidSurfaceLost.load()) {
        mAndroidNativeWindow = nativeWindow;
        return true;
    }
    if (mSurface != VK_NULL_HANDLE)
        DropAndroidSurface(nativeWindow != mAndroidNativeWindow ? "new_window" : "lost");
    try {
        CreateSurface();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "TRIAEVUM_SURFACE_REBUILD_FAILED stage=surface error=\"%s\"\n", error.what());
        return false;
    }
    VkBool32 supported = VK_FALSE;
    if (vkGetPhysicalDeviceSurfaceSupportKHR(mPhysicalDevice, mPresentQueueFamily, mSurface, &supported) !=
            VK_SUCCESS ||
        supported != VK_TRUE) {
        std::fprintf(stderr, "TRIAEVUM_SURFACE_REBUILD_FAILED stage=present_support\n");
        vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
        mSurface = VK_NULL_HANDLE;
        return false;
    }
    mAndroidNativeWindow = nativeWindow;
    mAndroidSurfaceLost.store(false);
    try {
        CreateSwapchainResources();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "TRIAEVUM_SURFACE_REBUILD_FAILED stage=swapchain error=\"%s\"\n", error.what());
        DropAndroidSurface("swapchain_failed");
        return false;
    }
    if (mSwapchainDirty) {
        // A zero extent: the next frames retry through RecreateSwapchain.
        return false;
    }
    StartPresentWorker();
    if (mNriPicaScanoutPass.Available() && !mNriPicaScanoutPass.Configure(mSwapchainFormat)) {
        SPDLOG_WARN("OOT3D NRI scanout reconfigure failed: {}", mNriPicaScanoutPass.UnavailableReason());
    }
    std::fprintf(stderr, "TRIAEVUM_SURFACE_REBUILT width=%u height=%u\n", mSwapchainExtent.width,
                 mSwapchainExtent.height);
    return true;
}

} // namespace Fast

#endif
