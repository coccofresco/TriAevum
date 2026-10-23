// Vulkan backend: construction, Init and Shutdown, and the recorded DLSS feature.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_internal.h"
#include "fast/oot3d/grass_texture_source_cache.h"

namespace Fast {

GfxRenderingAPIVulkan::GfxRenderingAPIVulkan(GfxWindowBackendSDL2* windowBackend)
    : mWindowBackend(windowBackend),
      mSceneSurfaces([this](const Oot3d::SceneSurface& surface) {
          if (surface.NativeImage != 0)
              mNriInterop.ForgetTexture(reinterpret_cast<VkImage>(surface.NativeImage));
          mResourceStates.Forget(surface.NativeImage);
      }) {
}

GfxRenderingAPIVulkan::~GfxRenderingAPIVulkan() {
    Shutdown();
}

namespace {
constexpr std::string_view kDlssFeatureRecordTag = "triaevum-dlss-features-v2";
// One feature of the first runs' records (the presentation's).
constexpr std::string_view kDlssFeatureRecordTagV1 = "triaevum-dlss-feature-v1";

std::filesystem::path DlssFeatureRecordPath() {
    const auto directory = VulkanShaderCacheDirectory();
    return directory.empty() ? std::filesystem::path{} : directory / "dlss_feature.txt";
}

std::optional<Oot3d::NriDlssFeatureKey> ReadDlssFeatureKey(std::istream& input) {
    uint32_t width = 0, height = 0, quality = 0, srgb = 0, preset = 0;
    if (!(input >> width >> height >> quality >> srgb >> preset) || width == 0U || height == 0U ||
        quality > static_cast<uint32_t>(Oot3d::UpscalerQuality::UltraPerformance) || srgb > 1U ||
        preset > 0xFFU)
        return std::nullopt;
    return Oot3d::NriDlssFeatureKey{width, height, static_cast<Oot3d::UpscalerQuality>(quality),
                                    srgb != 0U, static_cast<uint8_t>(preset)};
}
} // namespace

void GfxRenderingAPIVulkan::LoadDlssFeatureRecord() {
    mDlssFeatureRecord.reset();
    mDlssPreparationPending = false;
    const auto path = DlssFeatureRecordPath();
    if (path.empty() || !mNriInterop.DlssAvailable()) return;
    std::ifstream input(path);
    std::string tag;
    if (!(input >> tag)) return;
    DlssFeatureRecord record;
    if (tag == kDlssFeatureRecordTagV1) {
        const auto key = ReadDlssFeatureKey(input);
        if (!key || !(input >> record.SwapchainWidth >> record.SwapchainHeight)) return;
        record.Keys.push_back(*key);
    } else if (tag == kDlssFeatureRecordTag) {
        size_t count = 0;
        if (!(input >> record.SwapchainWidth >> record.SwapchainHeight >> count) || count == 0U ||
            count > 8U)
            return;
        for (size_t index = 0; index < count; ++index) {
            const auto key = ReadDlssFeatureKey(input);
            if (!key) return;
            record.Keys.push_back(*key);
        }
    } else {
        return;
    }
    mDlssFeatureRecord = std::move(record);
    mDlssPreparationPending = true;
}

void GfxRenderingAPIVulkan::PrepareRecordedDlssFeature() {
    if (!mDlssPreparationPending.load()) return;
    // One attempt: a mismatch leaves the creation to the first dispatch.
    mDlssPreparationPending = false;
    const auto settings = Oot3d::GraphicsSettingsRuntime::Instance().Snapshot();
    const auto& record = *mDlssFeatureRecord;
    if (settings.AntiAliasing != Oot3d::AntiAliasingMode::Upscaler ||
        settings.Upscaler != Oot3d::UpscalerProvider::Dlss ||
        mSwapchainExtent.width != record.SwapchainWidth ||
        mSwapchainExtent.height != record.SwapchainHeight)
        return;
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
    if (mD3d12NgxProvider.FrameDispatchRequested()) return;
#endif
    for (const auto& key : record.Keys) {
        if (settings.UpscalerMode != key.Quality ||
            Oot3d::DlssRenderPreset(settings.UpscalerDlssModel, settings.UpscalerMode) != key.Preset)
            continue;
        const auto start = std::chrono::steady_clock::now();
        const bool prepared = mNriInterop.PrepareDlss(mCurrentFrame, key);
        std::fprintf(stderr, "TRIAEVUM_DLSS_FEATURE_PREPARED prepared=%d output=%ux%u ms=%.1f\n",
                     prepared ? 1 : 0, key.OutputWidth, key.OutputHeight,
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
}

void GfxRenderingAPIVulkan::StoreDlssFeatureRecord() {
    const uint64_t generation = mNriInterop.DlssFeatureGeneration();
    if (generation == mDlssFeatureRecordGeneration) return;
    mDlssFeatureRecordGeneration = generation;
    DlssFeatureRecord record{mNriInterop.DlssFeatures(), mSwapchainExtent.width, mSwapchainExtent.height};
    if (record.Keys.empty() || mDlssFeatureRecord == record) return;
    mDlssFeatureRecord = record;
    const auto path = DlssFeatureRecordPath();
    if (path.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::trunc);
    output << kDlssFeatureRecordTag << ' ' << record.SwapchainWidth << ' ' << record.SwapchainHeight << ' '
           << record.Keys.size() << '\n';
    for (const auto& key : record.Keys)
        output << key.OutputWidth << ' ' << key.OutputHeight << ' ' << static_cast<uint32_t>(key.Quality) << ' '
               << (key.InputSrgb ? 1 : 0) << ' ' << static_cast<uint32_t>(key.Preset) << '\n';
}

void GfxRenderingAPIVulkan::JoinStartupPreparation() {
    for (auto& thread : mStartupPreparationThreads)
        if (thread.joinable()) thread.join();
    mStartupPreparationThreads.clear();
}

void GfxRenderingAPIVulkan::Init() {
    if (mInitialized) {
        return;
    }
    if (mWindowBackend == nullptr || !mWindowBackend->UsesVulkan()) {
        throw std::runtime_error("Vulkan renderer was paired with a non-Vulkan SDL backend");
    }
    if (mWindowBackend->GetNativeWindow() == nullptr) {
        throw std::runtime_error(std::string("SDL Vulkan window creation failed: ") + SDL_GetError());
    }

    mDiagnostics.ReloadFromEnvironment();
    mValidationTelemetry.Reset();
    ConfigureNativePicaAotShaders();
    {
        const char* value = std::getenv("TRIAEVUM_SHADER_PREPARATION");
        // Every draw has an always-available program: specialized programs
        // are prepared in the background and swapped in when ready. `sync`
        // keeps the draw-time preparation for comparison.
        const std::string_view mode = value != nullptr ? value : "hybrid";
        mNativePicaPreparationMode =
            mode == "complete" ? NativePicaPreparationMode::Complete
            : mode == "hybrid" ? NativePicaPreparationMode::Hybrid
            : mode == "uber" ? NativePicaPreparationMode::Uber
            : NativePicaPreparationMode::Synchronous;
    }

    CreateInstance();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreatePipelineCache();
    CreateCommandResources();
    if (mNriInterop.Initialize(mInstance, mPhysicalDevice, mDevice, mGraphicsQueueFamily,
                               mSynchronization2Enabled,
                               mDynamicRenderingEnabled,
                               mNisVulkanFeaturesEnabled,
                               mFsrVulkanFeaturesEnabled,
                               mNriSwapchainExtensionsEnabled,
                               mVulkanValidation.Enabled()
                                   ? &mValidationTelemetry : nullptr,
                               mGraphicsPipelineLibrariesEnabled)) {
        SPDLOG_INFO("OOT3D Vulkan NRI v180 interop enabled");
        // A pass replacing screen resources mid-frame may have recorded them
        // earlier in this frame: execute the recorded prefix, then everything
        // still in flight, and continue recording in the same frame.
        mNriInterop.SetRecordedWorkBoundary([this] {
            FlushGpuTextureCopyPrefix();
            return vkDeviceWaitIdle(mDevice) == VK_SUCCESS;
        });
    } else {
        SPDLOG_INFO("OOT3D Vulkan NRI interop unavailable: {}",
                    mNriInterop.UnavailableReason());
    }
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
    const bool d3d12NgxReady = mD3d12NgxProvider.Initialize(mInstance, mPhysicalDevice, mDevice, mGraphicsQueueFamily,
                                                            kFramesInFlight, mD3d12InteropExtensionsEnabled);
    const auto& d3d12NgxStatus = mD3d12NgxProvider.Status();
    mDiagnostics.SetD3d12NgxProvider({
        d3d12NgxStatus.AdapterMatched,
        d3d12NgxStatus.DeviceReady,
        d3d12NgxStatus.NriReady,
        d3d12NgxStatus.DlssFeatureReady,
        d3d12NgxStatus.DlssContractResourcesReady,
        d3d12NgxStatus.DlssEvaluateReady,
        d3d12NgxStatus.ExternalMemoryReady,
        d3d12NgxStatus.SharedFenceReady,
        d3d12NgxStatus.ZeroCopyInteropReady,
        d3d12NgxStatus.VendorId,
        d3d12NgxStatus.DeviceId,
        d3d12NgxStatus.ProbeInputWidth,
        d3d12NgxStatus.ProbeInputHeight,
        d3d12NgxStatus.ProbeOutputWidth,
        d3d12NgxStatus.ProbeOutputHeight,
        d3d12NgxStatus.AdapterName,
        d3d12NgxStatus.Detail,
        d3d12NgxStatus.FrameBridgeReady,
        d3d12NgxStatus.FrameDispatchRequested,
        d3d12NgxStatus.FrameDispatchCount,
    });
    if (d3d12NgxReady) {
        const auto& status = mD3d12NgxProvider.Status();
        SPDLOG_INFO("OOT3D D3D12 NGX provider ready on {} (vendor={:#06x}, device={:#06x}): {}", status.AdapterName,
                    status.VendorId, status.DeviceId, status.Detail);
    } else {
        SPDLOG_INFO("OOT3D D3D12 NGX provider unavailable: {}", mD3d12NgxProvider.UnavailableReason());
    }
#endif
    if (!mNriSwapchainExtensionsEnabled) {
        constexpr const char* reason =
            "VK_KHR_get_surface_capabilities2 is unavailable";
        mDiagnostics.SetVulkanPresentationFallback(
            Oot3dVulkanPresentationFallbackReason::
                MissingSurfaceCapabilities2,
            reason);
        SPDLOG_INFO("OOT3D Vulkan NRI swapchain unavailable: {}",
                    reason);
    } else if (!mNriSwapchainQueueEligible) {
        constexpr const char* reason =
            "graphics and presentation use separate queue families";
        mDiagnostics.SetVulkanPresentationFallback(
            Oot3dVulkanPresentationFallbackReason::
                SeparateQueueFamilies,
            reason);
        SPDLOG_INFO("OOT3D Vulkan NRI swapchain unavailable: {}",
                    reason);
    } else if (!mNriSwapchain.Initialize(mNriInterop)) {
        mDiagnostics.SetVulkanPresentationFallback(
            Oot3dVulkanPresentationFallbackReason::
                NriInitializationFailed,
            mNriSwapchain.UnavailableReason());
        SPDLOG_INFO("OOT3D Vulkan NRI swapchain unavailable: {}",
                    mNriSwapchain.UnavailableReason());
    } else {
        mDiagnostics.SetVulkanPresentationFallback(
            Oot3dVulkanPresentationFallbackReason::None, {});
    }
    const char* nriPicaPipelines =
        std::getenv("OOT3D_GRAPHICS_NRI_PICA_PIPELINES");
    const bool nriPicaPipelinesEnabled =
        nriPicaPipelines == nullptr ||
        std::string_view(nriPicaPipelines) != "0";
    if (!nriPicaPipelinesEnabled ||
        !mNriPicaPipelineBridge.Initialize(mNriInterop, mAllNativePipelinesUseLibraries,
                                           mGraphicsPipelineLibrariesEnabled)) {
        SPDLOG_INFO("OOT3D Vulkan NRI PICA pipeline bridge unavailable: {}",
                    nriPicaPipelinesEnabled
                        ? mNriPicaPipelineBridge.UnavailableReason()
                        : "disabled by environment");
    } else {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
        std::vector<uint8_t> data;
        LoadPipelineCacheData(VulkanPipelineCachePath(true),
                              MakePipelineCacheHeader(properties), data,
                              kMaximumNriPipelineCacheBytes);
        const bool initialized = mNriPicaPipelineBridge.InitializePipelineCache(data);
        SPDLOG_INFO("NRI PICA pipeline cache: initialized={}, accepted {} bytes", initialized,
                    mNriPicaPipelineBridge.PipelineStatistics().InitialCacheBytes);
    }
    LoadDlssFeatureRecord();
    if (const auto directory = VulkanShaderCacheDirectory(); !directory.empty())
        mInteractiveGrassPass.SetCacheDirectory(directory / "grass_placements_v1");
    // Grass rule textures' signatures, for their twins in scenes that do not
    // draw them.
    if (const auto directory = VulkanShaderCacheDirectory(); !directory.empty())
        Oot3d::GrassTextureSourceCache::Instance().SetSignatureStore(
            directory / "grass_texture_signatures.bin");
    if ((mNativePicaPreparationMode == NativePicaPreparationMode::Hybrid ||
         mNativePicaPreparationMode == NativePicaPreparationMode::Uber) &&
        (!mGraphicsPipelineLibrariesEnabled || !mNriPicaPipelineBridge.OwnedDrawsEnabled())) {
        // No prepared parts to link: specialized programs complete instead.
        mNativePicaPreparationMode = NativePicaPreparationMode::Complete;
    }
    if (mNativePicaPreparationMode != NativePicaPreparationMode::Synchronous && !mPreparationQueue) {
        const unsigned hardware = std::max(1U, std::thread::hardware_concurrency());
        mPreparationQueue = std::make_unique<Renderer3ds::PicaPreparationQueue>(
            std::clamp(hardware / 4U, 1U, 3U));
    }
    std::fprintf(stderr, "TRIAEVUM_SHADER_PREPARATION mode=%s threads=%u libraries=%u all_libraries=%u\n",
        mNativePicaPreparationMode == NativePicaPreparationMode::Synchronous ? "sync"
        : mNativePicaPreparationMode == NativePicaPreparationMode::Complete ? "complete"
        : mNativePicaPreparationMode == NativePicaPreparationMode::Hybrid ? "hybrid" : "uber",
        mPreparationQueue ? mPreparationQueue->Threads() : 0U,
        unsigned(mGraphicsPipelineLibrariesEnabled), unsigned(mAllNativePipelinesUseLibraries));
    if (!mNriPicaTextureImageOwner.Initialize(mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA texture images unavailable: {}",
            mNriPicaTextureImageOwner.UnavailableReason());
    }
    if (!mNriPicaDisplayImageOwner.Initialize(
            mNriInterop,
            "OOT3D_GRAPHICS_NRI_PICA_DISPLAY_IMAGES",
            "NRI PICA display image ownership")) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA display images unavailable: {}",
            mNriPicaDisplayImageOwner.UnavailableReason());
    }
    if (!mNriPicaRenderTargetOwner.Initialize(mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA render targets unavailable: {}",
            mNriPicaRenderTargetOwner.UnavailableReason());
    }
    if (!mNriPicaRenderTargetInitPass.Initialize(mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA render-target initialization unavailable: {}",
            mNriPicaRenderTargetInitPass.UnavailableReason());
    }
    if (!mNriPicaDisplayCopyPass.Initialize(mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA display copies unavailable: {}",
            mNriPicaDisplayCopyPass.UnavailableReason());
    }
    if (!mNriPicaTextureCopyCapturePass.Initialize(mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA raw-copy capture unavailable: {}",
            mNriPicaTextureCopyCapturePass.UnavailableReason());
    }
    if (!mNriPicaTextureCopyDerivePass.Initialize(mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA raw-copy derivation unavailable: {}",
            mNriPicaTextureCopyDerivePass.UnavailableReason());
    }
    if (!mNriPicaMemoryFillClearPass.Initialize(mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA memory-fill clears unavailable: {}",
            mNriPicaMemoryFillClearPass.UnavailableReason());
    }
    constexpr uint64_t kNriPicaTextureUploadBytesPerFrame =
        64ULL * 1024ULL * 1024ULL;
    if (!mNriPicaTextureUploadPass.Initialize(
            mNriInterop, kNriPicaTextureUploadBytesPerFrame)) {
        SPDLOG_INFO(
            "OOT3D Vulkan NRI PICA texture uploads unavailable: {}",
            mNriPicaTextureUploadPass.UnavailableReason());
    }
    if (!mAmbientOcclusionPass.Initialize(mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan GTAO unavailable: {}", mAmbientOcclusionPass.UnavailableReason());
    }
    if (!mCacaoPass.Initialize(mPhysicalDevice, mDevice, mNriInterop))
        SPDLOG_INFO("OOT3D Vulkan CACAO unavailable: {}", mCacaoPass.UnavailableReason());
    if (!mMxaoPass.Initialize(mPhysicalDevice, mDevice, mNriInterop))
        SPDLOG_INFO("OOT3D Vulkan MXAO unavailable: {}", mMxaoPass.UnavailableReason());
    if (!mHiZDepthPyramidPass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan Hi-Z unavailable: {}",
                    mHiZDepthPyramidPass.UnavailableReason());
    }
    if (!mHiZReflectionPass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan SSR unavailable: {}",
                    mHiZReflectionPass.UnavailableReason());
    }
    if (!mFidelityFxSssrPass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan FidelityFX SSSR unavailable: {}",
                    mFidelityFxSssrPass.UnavailableReason());
    }
    if (!mBrixelizerGiPass.Initialize(mPhysicalDevice, mDevice, mNriInterop, mBrixelizerScene)) {
        SPDLOG_INFO("OOT3D Vulkan Brixelizer GI unavailable: {}", mBrixelizerGiPass.UnavailableReason());
    }
    // Screen-space GI creates its pipelines off the frame, the first time the
    // mode is requested.
    mScreenSpaceGi.Initialize(mPhysicalDevice, mDevice, mNriInterop, kFramesInFlight);
    if (!mReflectionIblPass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan reflection IBL unavailable: {}",
                    mReflectionIblPass.UnavailableReason());
    }
    if (!mReflectionMaterialResolvePass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan reflection material resolve unavailable: {}",
            mReflectionMaterialResolvePass.UnavailableReason());
    }
    if (!mLinearSceneColorPass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan linear scene color unavailable: {}",
                    mLinearSceneColorPass.UnavailableReason());
    }
    if (!mMotionVectorPass.Initialize(mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan motion vectors unavailable: {}",
                    mMotionVectorPass.UnavailableReason());
    }
    if (!mNriDirectionalShadowPass.Initialize(mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D NRI directional shadows unavailable: {}", mNriDirectionalShadowPass.UnavailableReason());
    }
    if (!mTemporalAaPass.Initialize(mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan TAA unavailable: {}",
                    mTemporalAaPass.UnavailableReason());
    }
    if (!mNriEffectGraphTransientImageArena.Initialize(
            mDevice, mNriInterop)) {
        SPDLOG_INFO(
            "OOT3D Vulkan effect-graph transient image arena unavailable: {}",
            mNriEffectGraphTransientImageArena.UnavailableReason());
    }
    if (!mSceneCompositePass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan scene composite unavailable: {}",
                    mSceneCompositePass.UnavailableReason());
    }
    if (!mSmaa1xPass.Initialize(
            mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan SMAA 1x unavailable: {}",
                    mSmaa1xPass.UnavailableReason());
    }
    if (!mNriUpscalerPass.Initialize(mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan NIS unavailable: {}",
                    mNriUpscalerPass.UnavailableReason());
    }
    if (!mUpscalerGuidePass.Initialize(mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan upscaler guide resampling unavailable: {}",
                    mUpscalerGuidePass.UnavailableReason());
    }
    if (!mNativeUiOverlayPass.Initialize(mPhysicalDevice, mDevice, mNriInterop)) {
        SPDLOG_INFO("OOT3D Vulkan native UI overlay unavailable: {}",
                    mNativeUiOverlayPass.UnavailableReason());
    }
    auto& settingsRuntime = Oot3d::GraphicsSettingsRuntime::Instance();
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::NriInterop,
                                  mNriInterop.Available(), mNriInterop.UnavailableReason());
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::NriDirectionalShadows,
        mNriDirectionalShadowPass.Available(),
        mNriDirectionalShadowPass.UnavailableReason());
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::SampledSceneColor, true);
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::SampledDepth, true);
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::NormalGuide, true);
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::MaterialGuide, true);
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::NativeSurfaceLighting, true);
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::FidelityFxCacao,
                                  mCacaoPass.Available(), mCacaoPass.UnavailableReason());
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::ImmerseMxao,
                                  mMxaoPass.Available(), mMxaoPass.UnavailableReason());
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::MotionVectors,
                                  mMotionVectorPass.Available(),
                                  mMotionVectorPass.UnavailableReason());
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::TemporalHistory,
                                  mMotionVectorPass.Available() &&
                                      mTemporalAaPass.Available(),
                                  mTemporalAaPass.Available()
                                      ? mMotionVectorPass.UnavailableReason()
                                       : mTemporalAaPass.UnavailableReason());
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::LinearHdrWorkingColor,
        mLinearSceneColorPass.Available(),
        mLinearSceneColorPass.UnavailableReason());
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::Smaa1x,
        mSmaa1xPass.Available(),
        mSmaa1xPass.UnavailableReason());
    const bool fidelityFxSssrAvailable =
        mFidelityFxSssrPass.Available() &&
        mMotionVectorPass.Available() &&
        mLinearSceneColorPass.Available() &&
        mReflectionIblPass.Available() &&
        mReflectionMaterialResolvePass.Available();
    std::string fidelityFxSssrReason;
    if (!mFidelityFxSssrPass.Available())
        fidelityFxSssrReason =
            mFidelityFxSssrPass.UnavailableReason();
    else if (!mMotionVectorPass.Available())
        fidelityFxSssrReason =
            mMotionVectorPass.UnavailableReason();
    else if (!mLinearSceneColorPass.Available())
        fidelityFxSssrReason =
            mLinearSceneColorPass.UnavailableReason();
    else if (!mReflectionIblPass.Available())
        fidelityFxSssrReason =
            mReflectionIblPass.UnavailableReason();
    else if (!mReflectionMaterialResolvePass.Available())
        fidelityFxSssrReason =
            mReflectionMaterialResolvePass.UnavailableReason();
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::FidelityFxSssr,
        fidelityFxSssrAvailable, fidelityFxSssrReason);
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::NriNisUpscaler,
        mNriUpscalerPass.Available(), mNriUpscalerPass.UnavailableReason());
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::NriFsrUpscaler,
        mNriUpscalerPass.FsrAvailable(),
        mNriInterop.FsrUnavailableReason());
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::NriXessUpscaler, false,
        "NRI XeSS is unavailable on the Vulkan backend");
    const bool dlssAvailable = mNgxVulkanRequirementsSatisfied &&
        mNriUpscalerPass.DlssAvailable();
    const auto& ngxRequirements =
        Oot3d::GetNriNgxVulkanRequirements();
    std::string dlssCapabilityDetail;
    if (!mNgxVulkanRequirementsSatisfied) {
        dlssCapabilityDetail = mNgxVulkanUnavailableReason.empty()
            ? "Required NGX Vulkan extensions are unavailable"
            : mNgxVulkanUnavailableReason;
    } else if (!mNriUpscalerPass.DlssAvailable()) {
        dlssCapabilityDetail = mNriInterop.DlssUnavailableReason();
    } else {
        dlssCapabilityDetail = "Official NVIDIA NGX " +
            ngxRequirements.RuntimeVersion + " (DLSS SR / DLAA)";
    }
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
    dlssCapabilityDetail += mD3d12NgxProvider.Available()
        ? "; D3D12 NGX persistent per-frame bridge ready"
        : "; D3D12 NGX provider unavailable: " +
              mD3d12NgxProvider.UnavailableReason();
#endif
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::NriDlssUpscaler,
        dlssAvailable, dlssCapabilityDetail);
    if (dlssAvailable) {
        SPDLOG_INFO("OOT3D Vulkan DLSS ready: {}",
                    dlssCapabilityDetail);
    } else {
        SPDLOG_INFO("OOT3D Vulkan DLSS unavailable: {}",
                    dlssCapabilityDetail);
    }
    // What a tester's console shows of the upscalers this device offers.
    std::fprintf(stderr,
                 "TRIAEVUM_UPSCALERS nis=%d fsr=%d dlss=%d nis_reason=\"%s\" fsr_reason=\"%s\" dlss_detail=\"%s\"\n",
                 mNriUpscalerPass.Available() ? 1 : 0, mNriUpscalerPass.FsrAvailable() ? 1 : 0,
                 dlssAvailable ? 1 : 0, mNriUpscalerPass.UnavailableReason().c_str(),
                 mNriInterop.FsrUnavailableReason().c_str(), dlssCapabilityDetail.c_str());
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::Msaa2x,
        (mNativePicaSupportedSampleCounts & VK_SAMPLE_COUNT_2_BIT) != 0U,
        "2x color/depth sampling is unavailable");
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::Msaa4x,
        (mNativePicaSupportedSampleCounts & VK_SAMPLE_COUNT_4_BIT) != 0U,
        "4x color/depth sampling is unavailable");
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::Msaa8x,
        (mNativePicaSupportedSampleCounts & VK_SAMPLE_COUNT_8_BIT) != 0U,
        "8x color/depth sampling is unavailable");
    settingsRuntime.SetCapability(
        Oot3d::GraphicsCapability::MultisampledDepthResolve,
        mNativePicaDepthResolveMode != VK_RESOLVE_MODE_NONE,
        "multisampled depth resolve is unavailable");
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::PresentTearing,
                                  mWindowBackend->CanDisableVsync());
    settingsRuntime.SetCapability(Oot3d::GraphicsCapability::ExclusiveFullscreen,
                                  true);
    mVsyncEnabled = settingsRuntime.Snapshot().VSync;
    CreateSyncObjects();
    CreateSwapchainResources();
    if (!mNriPicaScanoutPass.Initialize(mDevice, mNriInterop) ||
        !mNriPicaScanoutPass.Configure(mSwapchainFormat)) {
        SPDLOG_INFO("OOT3D Vulkan NRI PICA scanout unavailable: {}",
                    mNriPicaScanoutPass.UnavailableReason());
    }
    CreateShaderResources();
    bool picaDynamicRenderingEnabled = mDynamicRenderingEnabled;
    if (const char* overrideValue =
            std::getenv("OOT3D_GRAPHICS_PICA_DYNAMIC_RENDERING");
        overrideValue != nullptr &&
        std::string_view(overrideValue) == "0")
        picaDynamicRenderingEnabled = false;
    if (!mPicaDynamicRenderingScope.Initialize(
            mDevice, &mNriInterop, mDepthFormat,
            mNativePicaDepthResolveMode,
            picaDynamicRenderingEnabled)) {
        SPDLOG_INFO("OOT3D Vulkan PICA dynamic rendering unavailable: {}",
                    mPicaDynamicRenderingScope.UnavailableReason());
    }
    // Both GI modes are experimental: offered only by builds configured with
    // THREE_DS_RECOMP_OFFER_EXPERIMENTAL_GI. Elsewhere the settings keep them
    // Off and F1 shows why.
#ifdef TRIAEVUM_OFFER_EXPERIMENTAL_GI
    constexpr bool kOfferExperimentalGi = true;
#else
    constexpr bool kOfferExperimentalGi = false;
#endif
    constexpr const char* kExperimentalGiReason = "experimental, not part of this build";
    {
        // The GI albedo guide is an eighth colour attachment, bound only
        // through dynamic rendering.
        std::string reason;
        bool supported = Oot3d::BrixelizerScene::Supported(mInstance, mPhysicalDevice, reason);
        if (supported && !mPicaDynamicRenderingScope.Available()) {
            supported = false;
            reason = "global illumination requires PICA dynamic rendering";
        }
        if (!kOfferExperimentalGi) {
            supported = false;
            reason = kExperimentalGiReason;
        }
        Oot3d::GraphicsSettingsRuntime::Instance().SetCapability(
            Oot3d::GraphicsCapability::BrixelizerGlobalIllumination, supported, reason);
    }
    {
        // The screen-space GI reads the same albedo guide (dynamic rendering).
        std::string reason;
        bool supported = Oot3d::ScreenSpaceGiProvider::Supported(mPhysicalDevice, reason);
        if (supported && !mPicaDynamicRenderingScope.Available()) {
            supported = false;
            reason = "screen-space global illumination requires PICA dynamic rendering";
        }
        if (!kOfferExperimentalGi) {
            supported = false;
            reason = kExperimentalGiReason;
        }
        Oot3d::GraphicsSettingsRuntime::Instance().SetCapability(
            Oot3d::GraphicsCapability::ScreenSpaceGlobalIllumination, supported, reason);
    }
    if (!mInteractiveGrassPass.Initialize(mPhysicalDevice, mDevice,
                                          mNriInterop.Shaders(),
                                          mNativePicaCanonicalRenderPass,
                                          mNativePicaRenderPass,
                                          mNativePicaSampleCount,
                                          mPicaDynamicRenderingScope.Available(),
                                          mDepthFormat, mPipelineCache)) {
        SPDLOG_INFO("OOT3D Vulkan interactive grass unavailable: {}",
                    mInteractiveGrassPass.UnavailableReason());
    }
    mWorldDebugLinePass.Initialize(mPhysicalDevice, mDevice, mNriInterop.Shaders(),
                                   mNativePicaCanonicalRenderPass, mNativePicaRenderPass,
                                   mNativePicaSampleCount, mPicaDynamicRenderingScope.Available(),
                                   mDepthFormat, mPipelineCache);
    CreateFrameResources();
    mScanoutProbe.Initialize(mPhysicalDevice, mDevice, kFramesInFlight);
    mGpuProfiler.Initialize(mPhysicalDevice, mDevice, mGraphicsQueueFamily,
                            mDiagnostics.Enabled());
    CreateFallbackTexture();
    StartPresentWorker();
    mInitialized = true;
    // Emit the compatibility contract as soon as the opted-in backend is live;
    // subsequent frames replace it with bounded measurements.
    mDiagnostics.Flush();
}

void GfxRenderingAPIVulkan::Shutdown() {
    FinishNativePicaAotShaders();
    if (mInstance == VK_NULL_HANDLE) {
        return;
    }
    if (mDevice != VK_NULL_HANDLE) {
        WaitForAllPresents();
        StopPresentWorker();
        vkDeviceWaitIdle(mDevice);
        // Preparation still running finishes first: what it creates belongs
        // in the persisted pipeline caches.
        JoinStartupPreparation();
        if (mPreparationQueue) mPreparationQueue->Drain();
        StorePipelineCache();
        mScreenSpaceGi.Shutdown();
        mBrixelizerGiPass.Shutdown();
        mBrixelizerScene.Shutdown();
        ShutdownImGuiBackend();
        mSceneSurfaces.Clear();
        mResourceStates.Clear();
        mAmbientOcclusionPass.Shutdown();
        mCacaoPass.Shutdown();
        mMxaoPass.Shutdown();
        mFidelityFxSssrPass.Shutdown();
        mReflectionMaterialResolvePass.Shutdown();
        mReflectionIblPass.Shutdown();
        mLinearSceneColorPass.Shutdown();
        mHiZDepthPyramidPass.Shutdown();
        mHiZReflectionPass.Shutdown();
        mMotionVectorPass.Shutdown();
        mNriDirectionalShadowPass.Shutdown();
        mNriPicaScanoutPass.Shutdown();
        mNriUpscalerPass.Shutdown();
        mUpscalerGuidePass.Shutdown();
        mNativeUiOverlayPass.Shutdown();
        mSceneCompositePass.Shutdown();
        mSmaa1xPass.Shutdown();
        mNriEffectGraphTransientImageArena.Shutdown();
        mTemporalAaPass.Shutdown();
        mInteractiveGrassPass.Shutdown();
        mWorldDebugLinePass.Shutdown();
        mPicaDynamicRenderingScope.Shutdown();
        mNriPicaTextureUploadPass.Shutdown();
        // Background creations finish before their device objects go.
        JoinStartupPreparation();
        if (mPreparationQueue) mPreparationQueue->Drain();
        mNriPicaPipelineBridge.Shutdown();
        DestroyNativePicaGeometryResources();
        DestroyNativePicaShaderResources();
        mNriPicaDisplayCopyPass.Shutdown();
        mNriPicaTextureCopyCapturePass.Shutdown();
        mNriPicaTextureCopyDerivePass.Shutdown();
        mNriPicaMemoryFillClearPass.Shutdown();
        mNriPicaRenderTargetInitPass.Shutdown();
        mNriPicaRenderTargetOwner.Shutdown();
        mNriPicaDisplayImageOwner.Shutdown();
        mNriPicaTextureImageOwner.Shutdown();
        DestroySwapchainResources();
        mNriSwapchain.Shutdown();
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
        for (uint32_t slot = 0U; slot < kFramesInFlight; ++slot) {
            const VkImage output =
                mD3d12NgxProvider.FrameOutputImage(slot);
            if (output != VK_NULL_HANDLE)
                mNriInterop.ForgetTexture(output);
        }
        mD3d12NgxProvider.Shutdown();
#endif
        mNriInterop.Shutdown();
        mGpuProfiler.Shutdown();
        mScanoutProbe.Shutdown();
        DestroyGraphicsPipelines();
        for (auto& [id, framebuffer] : mOffscreenFramebuffers) {
            DestroyOffscreenFramebuffer(framebuffer);
        }
        mOffscreenFramebuffers.clear();
        for (auto& [id, texture] : mTextures) {
            DestroyTexture(texture);
        }
        mTextures.clear();
        for (auto& [id, cached] : mOot3dCachedVertexBuffers) {
            for (auto& buffer : cached.Buffers) {
                DestroyBuffer(buffer);
            }
        }
        mOot3dCachedVertexBuffers.clear();
        DestroyTexture(mFallbackTexture);
        DestroyTexture(mGeometryGuideFallback);
        for (VkSampler sampler : mRetiredSamplers) {
            vkDestroySampler(mDevice, sampler, nullptr);
        }
        mRetiredSamplers.clear();
        for (auto& frame : mFrameResources) {
            if (frame.DescriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(mDevice, frame.DescriptorPool, nullptr);
                frame.DescriptorPool = VK_NULL_HANDLE;
            }
            DestroyBuffer(frame.VertexBuffer);
            DestroyBuffer(frame.UniformBuffer);
            for (auto& buffer : frame.RetiredUniformBuffers) DestroyBuffer(buffer);
            frame.RetiredUniformBuffers.clear();
        }
        for (auto& [key, shader] : mShaders) {
            if (shader.VertexShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(mDevice, shader.VertexShader, nullptr);
            }
            if (shader.FragmentShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(mDevice, shader.FragmentShader, nullptr);
            }
        }
        mShaders.clear();
        if (mPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
            mPipelineLayout = VK_NULL_HANDLE;
        }
        if (mTextureDescriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(mDevice, mTextureDescriptorSetLayout, nullptr);
            mTextureDescriptorSetLayout = VK_NULL_HANDLE;
        }
        for (uint32_t index = 0; index < kFramesInFlight; ++index) {
            if (mImageAvailableSemaphores[index] != VK_NULL_HANDLE) {
                vkDestroySemaphore(mDevice, mImageAvailableSemaphores[index], nullptr);
                mImageAvailableSemaphores[index] = VK_NULL_HANDLE;
            }
            if (mInFlightFences[index] != VK_NULL_HANDLE) {
                vkDestroyFence(mDevice, mInFlightFences[index], nullptr);
                mInFlightFences[index] = VK_NULL_HANDLE;
            }
        }
        if (mRawCopyFence != VK_NULL_HANDLE) {
            vkDestroyFence(mDevice, mRawCopyFence, nullptr);
            mRawCopyFence = VK_NULL_HANDLE;
        }
        DestroyGpuTextureCopySurfaces();
        DestroyRawCopyEncodeResources();
        if (mOot3dShadow2dRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(mDevice, mOot3dShadow2dRenderPass, nullptr);
            mOot3dShadow2dRenderPass = VK_NULL_HANDLE;
        }
        if (mCommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(mDevice, mCommandPool, nullptr);
        }
        if (mPipelineCache != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(mDevice, mPipelineCache, nullptr);
            mPipelineCache = VK_NULL_HANDLE;
        }
        vkDestroyDevice(mDevice, nullptr);
    }
    mDiagnostics.EndFrame();
    mDiagnostics.Flush();
    if (mSurface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
    }
    mVulkanValidation.Shutdown();
    vkDestroyInstance(mInstance, nullptr);

    mInstance = VK_NULL_HANDLE;
    mSurface = VK_NULL_HANDLE;
    mPhysicalDevice = VK_NULL_HANDLE;
    mDevice = VK_NULL_HANDLE;
    mCommandPool = VK_NULL_HANDLE;
    mInitialized = false;
    mFrameActive = false;
    mRenderPassActive = false;
    mOverlayRenderPassActive = false;
    mFrameSubmitted = false;
    mOot3dShadow2dPassActive = false;
    mActiveOot3dShadow2dFramebufferId = -1;
    mBoundOot3dShadow2dFramebufferId = -1;
}

} // namespace Fast

#endif
