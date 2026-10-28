// Vulkan backend: presentation settings and the frame (StartFrame, EndFrame, FinishRender).
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_internal.h"
#include "fast/extension/grass_stage_profile.h"
#include "fast/renderer3ds/pica_surface_debug.h"
#include "fast/renderer3ds/pica_surface_emission.h"
#include "fast/backends/startup_checkpoints.h"
#include <atomic>

namespace Fast {

bool GfxRenderingAPIVulkan::ApplyPresentationSettings(const Extension::TitlePresentationSettingsView& requestedSettings,
                                                      std::string* error) {
    auto settings = requestedSettings;
#ifdef __ANDROID__
    // Android presents to the Activity surface only: fullscreen at its size.
    settings.WindowMode = 1;
    int32_t outputX = 0, outputY = 0;
    mWindowBackend->GetDimensions(&settings.Width, &settings.Height, &outputX, &outputY);
#else
    // At startup a windowed request opens inside the display's work area, as
    // the window host created it; a later choice in F1 is applied as asked.
    // The product's default request (nobody chose a size, or "Reset display
    // settings") opens at the default fit of the screen when the host enabled
    // that policy.
    if (settings.WindowMode == 0U) {
        const WindowExtent resolved = mWindowBackend->ResolveWindowedExtent(
            { settings.Width, settings.Height }, settings.DefaultExtent,
            settings.TransactionKind == static_cast<uint8_t>(Extension::RendererPresentationRequestKind::Initial));
        settings.Width = resolved.Width;
        settings.Height = resolved.Height;
    }
#endif
    if (!mInitialized || mFrameActive || settings.WindowMode > 2U ||
        settings.Width < 320U || settings.Height < 240U) {
        if (error != nullptr) {
            *error =
                "invalid or unsafe OOT3D presentation-settings transaction";
        }
        mDiagnostics.RecordPresentationTransaction(
            settings.TransactionKind, false, true);
        return false;
    }
    // The host may already have created the requested window. Adopt that state
    // instead of destroying a usable swapchain on the first gameplay frame.
    if (!mPresentationSettingsApplied && !mSwapchainDirty &&
        mSwapchainImages.size() >= 2U && mVsyncEnabled == settings.VSync) {
        uint32_t width = 0, height = 0;
        int32_t x = 0, y = 0;
        mWindowBackend->GetDimensions(&width, &height, &x, &y);
        const uint8_t mode = !mWindowBackend->IsFullscreen() ? 0U
            : mWindowBackend->IsWindowedFullscreen() ? 1U : 2U;
        if (mode == settings.WindowMode && width == settings.Width &&
            height == settings.Height) {
            mAppliedWindowMode = mode;
            mAppliedOutputWidth = width;
            mAppliedOutputHeight = height;
            mAppliedVsync = settings.VSync;
            mPresentationSettingsApplied = true;
        }
    }
    if (mPresentationSettingsApplied &&
        mAppliedWindowMode == settings.WindowMode &&
        mAppliedOutputWidth == settings.Width &&
        mAppliedOutputHeight == settings.Height &&
        mAppliedVsync == settings.VSync) {
        mDiagnostics.SetPresentationState({
            true,
            mAppliedWindowMode,
            mAppliedOutputWidth,
            mAppliedOutputHeight,
            mSwapchainExtent.width,
            mSwapchainExtent.height,
            mAppliedVsync,
        });
        mDiagnostics.RecordPresentationTransaction(
            settings.TransactionKind, true, false);
        return true;
    }

    Extension::TitlePresentationSettingsView previous;
    if (mPresentationSettingsApplied) {
        previous = {
            mAppliedWindowMode,
            mAppliedOutputWidth,
            mAppliedOutputHeight,
            mAppliedVsync,
        };
    } else {
        int32_t x = 0;
        int32_t y = 0;
        mWindowBackend->GetDimensions(
            &previous.Width, &previous.Height, &x, &y);
        const bool borderless =
            mWindowBackend->IsWindowedFullscreen();
        previous.WindowMode = !mWindowBackend->IsFullscreen()
            ? 0U
            : borderless ? 1U : 2U;
        previous.VSync = mVsyncEnabled;
    }

    const auto apply = [&](const Extension::TitlePresentationSettingsView& value,
                           bool allowInjectedFailure) {
        const bool fullscreen = value.WindowMode != 0U;
        const bool borderless = value.WindowMode == 1U;
        const bool currentlyBorderless =
            mWindowBackend->IsWindowedFullscreen();
        const uint8_t currentMode = !mWindowBackend->IsFullscreen()
            ? 0U
            : currentlyBorderless ? 1U : 2U;
        const bool fullscreenFlavorChanged =
            fullscreen && currentMode != 0U &&
            currentMode != value.WindowMode;
        const bool exclusiveModeChanged =
            currentMode == 2U && value.WindowMode == 2U;
        const bool leavingFullscreen =
            !fullscreen && currentMode != 0U;
        if (fullscreenFlavorChanged || exclusiveModeChanged ||
            leavingFullscreen) {
            mWindowBackend->SetFullscreen(false);
        }

        int32_t x = 0;
        int32_t y = 0;
        uint32_t oldWidth = 0;
        uint32_t oldHeight = 0;
        mWindowBackend->GetDimensions(
            &oldWidth, &oldHeight, &x, &y);
        mWindowBackend->SetWindowedFullscreen(borderless);
        if (!fullscreen || !borderless) {
            mWindowBackend->SetDimensions(
                value.Width, value.Height, x, y);
        }
        if (value.WindowMode == 2U &&
            !mWindowBackend->SetExclusiveFullscreenDisplayMode(
                value.Width, value.Height)) {
            throw std::runtime_error(
                "SDL rejected the requested exclusive display mode");
        }
        if (mWindowBackend->IsFullscreen() != fullscreen) {
            mWindowBackend->SetFullscreen(fullscreen);
        } else if (fullscreenFlavorChanged || exclusiveModeChanged) {
            mWindowBackend->SetFullscreen(true);
        }
        if (mWindowBackend->IsFullscreen() != fullscreen) {
            throw std::runtime_error(
                "SDL rejected the requested fullscreen mode");
        }
        if (fullscreen &&
            mWindowBackend->IsWindowedFullscreen() != borderless) {
            throw std::runtime_error(
                "SDL rejected the requested fullscreen flavor");
        }
        mVsyncEnabled = value.VSync;
        mSwapchainDirty = true;
        RecreateSwapchain();
        if (mSwapchainDirty || mSwapchainImages.size() < 2U) {
            throw std::runtime_error(
                "swapchain recreation did not produce usable images");
        }
        if (allowInjectedFailure) {
            const char* inject = std::getenv(
                "OOT3D_GRAPHICS_TEST_FAIL_PRESENTATION_APPLY");
            if (inject != nullptr &&
                std::string_view(inject) == "1") {
                throw std::runtime_error(
                    "injected presentation transaction failure");
            }
        }
    };

    WaitForAllPresents();
    CheckVk(vkDeviceWaitIdle(mDevice),
            "vkDeviceWaitIdle(OOT3D presentation settings)");
    try {
        apply(settings, mPresentationSettingsApplied);
    } catch (const std::exception& applyException) {
        try {
            apply(previous, false);
        } catch (const std::exception& rollbackException) {
            throw std::runtime_error(
                std::string("OOT3D presentation apply failed: ") +
                applyException.what() +
                "; rollback failed: " + rollbackException.what());
        }
        mAppliedWindowMode = previous.WindowMode;
        mAppliedOutputWidth = previous.Width;
        mAppliedOutputHeight = previous.Height;
        mAppliedVsync = previous.VSync;
        mPresentationSettingsApplied = true;
        mDiagnostics.SetPresentationState({
            true,
            mAppliedWindowMode,
            mAppliedOutputWidth,
            mAppliedOutputHeight,
            mSwapchainExtent.width,
            mSwapchainExtent.height,
            mAppliedVsync,
        });
        mDiagnostics.RecordPresentationTransaction(
            settings.TransactionKind, false, true);
        if (error != nullptr) {
            *error = std::string(applyException.what()) +
                "; previous presentation settings restored";
        }
        return false;
    }

    mAppliedWindowMode = settings.WindowMode;
    mAppliedOutputWidth = settings.Width;
    mAppliedOutputHeight = settings.Height;
    mAppliedVsync = settings.VSync;
    mPresentationSettingsApplied = true;
    mDiagnostics.SetPresentationState({
        true,
        mAppliedWindowMode,
        mAppliedOutputWidth,
        mAppliedOutputHeight,
        mSwapchainExtent.width,
        mSwapchainExtent.height,
        mAppliedVsync,
    });
    mDiagnostics.RecordPresentationTransaction(
        settings.TransactionKind, true, false);
    return true;
}

void GfxRenderingAPIVulkan::StartFrame() {
    Renderer3ds::SlowStepScope slowStep("start_frame");
    const char* traceEnvironment = std::getenv("TRIAEVUM_FRAME_START_TIMING");
    const bool traceStart = traceEnvironment != nullptr && std::string_view(traceEnvironment) == "1";
    auto startMark = traceStart ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    std::array<double, 7> startTimes{};
    const auto markStart = [&](size_t index) {
        if (!traceStart) return;
        const auto now = std::chrono::steady_clock::now();
        startTimes[index] = std::chrono::duration<double, std::milli>(now - startMark).count();
        startMark = now;
    };
    if (!mInitialized || mFrameActive || mFrameSubmitted) {
        return;
    }
    // Guest completion survives presentations. A visual replay, however, must
    // capture this sample's source image again instead of reusing alpha zero.
    std::erase_if(mHandledNativePicaTextureCopies,
                  [](const auto& entry) { return !entry.second.second; });
    auto& settingsRuntime = Extension::GraphicsSettingsRuntime::Instance();
    Extension::TickAmbientOcclusionDiagnostics(settingsRuntime, mFrameCounter);
    Extension::TickDisplayDiagnostics(settingsRuntime, mFrameCounter);
    if (!mPresentationSettingsHeld) settingsRuntime.TickPresentation();
    const auto initialGraphicsSettings = settingsRuntime.Snapshot();
    const auto presentationRequest =
        Extension::ResolveRendererPresentationRequest(
            initialGraphicsSettings,
            settingsRuntime.PresentationStatus(),
            mPresentationSettingsApplied);
    if (presentationRequest.has_value() && !mPresentationSettingsHeld) {
        const auto& value = presentationRequest->Value;
        const Extension::TitlePresentationSettingsView view{
            static_cast<uint8_t>(value.Window),
            value.Width,
            value.Height,
            value.VSync,
            static_cast<uint8_t>(presentationRequest->Kind),
            value.Provenance == WindowExtentProvenance::Default,
        };
        std::string presentationError;
        if (ApplyPresentationSettings(view, &presentationError)) {
            if (presentationRequest->RequiresAcknowledgement() &&
                !settingsRuntime.AcknowledgePresentationApplied(
                    initialGraphicsSettings)) {
                SPDLOG_ERROR(
                    "OOT3D renderer applied display settings but the "
                    "transaction could not be acknowledged");
            }
        } else {
            if (presentationRequest->Kind ==
                Extension::RendererPresentationRequestKind::Candidate) {
                settingsRuntime.RejectPresentationApply(
                    initialGraphicsSettings, presentationError);
            }
            SPDLOG_ERROR("OOT3D display settings were not applied: {}",
                         presentationError);
        }
    }
    mAmbientOcclusionExecutedThisFrame = false;
    mSceneDistanceFieldUpdatedThisFrame = false;
    mHiZExecutedThisFrame = false;
    mReflectionExecutedThisFrame = false;
    mReflectionProviderThisFrame =
        Extension::ReflectionProvider::Off;
    mReflectionOutputEncodingThisFrame =
        Extension::SceneColorEncoding::Unknown;
    mGrassExecutedThisFrame = false;
    mLinearColorExecutedThisFrame = false;
    Renderer3ds::PicaSurfaceEmissionRegistry::Instance().BeginFrame();
    mUpscalerExecutedThisFrame = false;
    mNativeUiOverlayExecutedThisFrame = false;
    mUpscalerUsedD3d12ThisFrame = false;
    mUpscalerOutputEncodingThisFrame =
        Extension::SceneColorEncoding::Unknown;
    mUpscalerEffectsCompositedThisFrame = false;
    mCompositeExecutedThisFrame = false;
    mCompositeOutputEncodingThisFrame =
        Extension::SceneColorEncoding::Unknown;
    mTaaExecutedThisFrame = false;
    mTaaOutputEncodingThisFrame =
        Extension::SceneColorEncoding::Unknown;
    mTaaEffectsCompositedThisFrame = false;
    mSmaaExecutedThisFrame = false;
    mSmaaEffectsCompositedThisFrame = false;
    mSmaaOutputEncodingThisFrame =
        Extension::SceneColorEncoding::Unknown;
    mSmaaRenderTargetNamespace = 0;
    mSmaaDisplayPhysicalAddress = 0;
    mSmaaWidth = 0;
    mSmaaHeight = 0;
    mGrassRenderedTargetsThisFrame.clear();
    mGrassInsertionAttemptedTargetsThisFrame.clear();
    mGrassWorldTargetsThisFrame.clear();
    mDirectionalShadowInsertionAttemptedTargetsThisFrame.clear();
    mPointShadowInsertionAttemptedTargetsThisFrame.clear();
    mGrassSurfaceOccurrencesThisFrame.clear();
    mNativePicaDepthWritingDrawsThisFrame = 0;
    mCustomTextureUploadBytesThisFrame = 0;
    if (mFrameCounter > 3U) {
        Extension::GrassSceneBridge::Instance().PruneBeforeFrame(mFrameCounter - 2U);
    }
    settingsRuntime.SetCapability(
        Extension::GraphicsCapability::ValidViewMetadata,
        Extension::SceneViewRuntime::Instance().LatestPerspective().has_value(),
        "No valid perspective frustum has been published by the game runtime");
    if (const char* grassSmoke = std::getenv("OOT3D_GRAPHICS_GRASS_AUTO");
        grassSmoke != nullptr && std::string_view(grassSmoke) == "1") {
        auto candidate = settingsRuntime.Snapshot();
        if (candidate.Grass.Rules.empty()) {
            const auto selectors =
                Extension::GrassSceneBridge::Instance().AvailableTextures();
            static bool grassSmokeSourceReported = false;
            if (!selectors.empty() && !grassSmokeSourceReported) {
                std::fprintf(stderr,
                    "OOT3D grass smoke: %zu source meshes, %zu texture selectors\n",
                    Extension::GrassSceneBridge::Instance().Size(), selectors.size());
                grassSmokeSourceReported = true;
            }
            uint64_t requestedHash = 0U;
            if (const char* hash = std::getenv("OOT3D_GRAPHICS_GRASS_HASH");
                hash != nullptr) {
                char* end = nullptr;
                requestedHash = std::strtoull(hash, &end, 16);
                if (end == hash || *end != '\0') requestedHash = 0U;
            }
            size_t addedRules = 0U;
            for (size_t index = 0;
                 index < selectors.size() && addedRules < 128U; ++index) {
                if (requestedHash != 0U &&
                    selectors[index].Rgba8Hash != requestedHash) continue;
                Extension::GrassPlacementRule rule;
                rule.RuleId = static_cast<uint32_t>(addedRules + 1U);
                rule.Target = selectors[index];
                rule.InputBlack = 0.0F;
                candidate.Grass.Rules.push_back(rule);
                ++addedRules;
            }
            if (!candidate.Grass.Rules.empty()) {
                candidate.Preset = Extension::GraphicsPreset::Custom;
                candidate.Grass.Quality = Extension::GrassQuality::Medium;
                candidate.Grass.Generation.InstancesPerSquareMeter =
                    3.0F;
                candidate.Grass.MaxInstancesPerRoom = 30000U;
                candidate.Grass.DrawDistance = 1200.0F;
                candidate.Grass.WindDirectionDegrees = 35.0F;
                candidate.Grass.WindStrength = 0.5F;
                candidate.Grass.WindSpeed = 1.2F;
                settingsRuntime.Apply(std::move(candidate));
            }
        }
    }
    auto graphicsSnapshot = settingsRuntime.SnapshotForRendering();
    // Water reflections with Reflections Off (EffectsSettings::WaterReflections):
    // the frame traces Hi-Z, and only water writes a reflection material.
    {
        auto& effects = graphicsSnapshot.Value.Effects;
        const auto capabilities = settingsRuntime.Capabilities();
        mWaterOnlyReflections = effects.Reflections == Extension::ReflectionMode::Off && effects.WaterReflections &&
            Extension::UsesFullSceneLighting(effects.SurfaceLighting) &&
            capabilities.Has(Extension::GraphicsCapability::NriInterop) &&
            capabilities.Has(Extension::GraphicsCapability::ValidViewMetadata);
        if (mWaterOnlyReflections) {
            effects.Reflections = Extension::ReflectionMode::HiZ;
            effects.ReflectionMaterials.clear();
            // The water's reflectance weighs the trace; open water reaches far.
            effects.ReflectionStrength = 1.0F;
            effects.ReflectionMaxDistance = std::max(effects.ReflectionMaxDistance, 3000.0F);
        }
    }
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
    Renderer3ds::PicaSurfaceDebug::Instance().BeginFrame();
#endif
    const auto& nextGraphicsSettings = graphicsSnapshot.Value;
    if (mFrameEnvironment != graphicsSnapshot.Environment) {
      if (!Renderer3ds::ContinuousPicaEnvironmentChange(mFrameEnvironment, graphicsSnapshot.Environment)) {
        mTemporalAaPass.ResetHistory();
        mTaaOutputValid = false;
        // Never use a previous scene/sun's depth map for a new lighting direction.
        if (mFrameEnvironment.ContinuityEpoch != graphicsSnapshot.Environment.ContinuityEpoch ||
            mFrameEnvironment.ScopeIdentity != graphicsSnapshot.Environment.ScopeIdentity ||
            mFrameEnvironment.Enabled != graphicsSnapshot.Environment.Enabled ||
            mFrameEnvironment.KeyDirection != graphicsSnapshot.Environment.KeyDirection ||
            mFrameEnvironment.KeyAzimuthOffset != graphicsSnapshot.Environment.KeyAzimuthOffset ||
            mFrameEnvironment.KeyElevationOffset != graphicsSnapshot.Environment.KeyElevationOffset ||
            mFrameEnvironment.CasterFaces != graphicsSnapshot.Environment.CasterFaces ||
            mFrameEnvironment.Lights[0].DirectionTowardSource !=
                graphicsSnapshot.Environment.Lights[0].DirectionTowardSource)
            mNriDirectionalShadowPass.InvalidateHistory();
      }
        // Continuous profile motion uses VirtualShadowKeyframes' existing
        // page budget/dissolve, just like the moving native sun. Do not reset
        // its construction (or TAA history) for every interpolated sample.
        mFrameEnvironment = graphicsSnapshot.Environment;
    }
    if (mFrameGraphicsSettings.Effects.DirectionalShadows.Mode != nextGraphicsSettings.Effects.DirectionalShadows.Mode ||
        mFrameGraphicsSettings.Effects.PointShadows.Mode != nextGraphicsSettings.Effects.PointShadows.Mode ||
        mFrameGraphicsSettings.Effects.SurfaceLighting != nextGraphicsSettings.Effects.SurfaceLighting)
        mNriDirectionalShadowPass.InvalidateHistory();
    if (mFrameGraphicsSettingsRevision != 0 &&
        (mFrameGraphicsSettings.Grass.Quality != nextGraphicsSettings.Grass.Quality ||
         mFrameGraphicsSettings.Effects.Toon != nextGraphicsSettings.Effects.Toon ||
         mFrameGraphicsSettings.Effects.SurfaceLighting != nextGraphicsSettings.Effects.SurfaceLighting ||
         mFrameGraphicsSettings.Effects.ToonStyle.OutlineEnabled != nextGraphicsSettings.Effects.ToonStyle.OutlineEnabled ||
         mFrameGraphicsSettings.Effects.AmbientOcclusion != nextGraphicsSettings.Effects.AmbientOcclusion ||
         mFrameGraphicsSettings.Effects.Reflections != nextGraphicsSettings.Effects.Reflections)) {
        // Never blend the old enhanced/native presentation into the newly selected one.
        mTemporalAaPass.ResetHistory();
        mTaaOutputValid = false;
    }
    mFrameGraphicsSettings = std::move(graphicsSnapshot.Value);
    mFrameGraphicsSettingsRevision = graphicsSnapshot.Revision;
    const auto& graphicsSettings = mFrameGraphicsSettings;
    if (const float anisotropy = std::min(static_cast<float>(std::max<uint8_t>(graphicsSettings.Anisotropy, 1U)),
                                          mMaxSamplerAnisotropy);
        anisotropy != mTextureAnisotropy) {
        // Every texture sampler takes the new filtering; the replaced
        // samplers retire after the frames that may still use them.
        mTextureAnisotropy = anisotropy;
        for (auto& [key, texture] : mNativePicaTextures) {
            if (texture.Sampler != VK_NULL_HANDLE) RecreateSampler(texture);
        }
    }
    mPicaShaderPipelineCache.BeginFrame(
        mFrameGraphicsSettingsRevision);
    auto& azaharTexturePacks =
        ::Oot3d::Renderer::AzaharTexturePackRuntime::Instance();
    if (mAzaharConfiguredSettingsRevision !=
        mFrameGraphicsSettingsRevision) {
        azaharTexturePacks.Configure({
            .DumpTextures =
                graphicsSettings.TexturePacks.Azahar.DumpTextures,
            .LoadCustomTextures =
                graphicsSettings.TexturePacks.Azahar
                    .LoadCustomTextures,
            .LoadDirectory =
                graphicsSettings.TexturePacks.Azahar.LoadDirectory,
            .DumpDirectory =
                graphicsSettings.TexturePacks.Azahar.DumpDirectory,
        });
        mAzaharConfiguredSettingsRevision =
            mFrameGraphicsSettingsRevision;
    }
    mFrameAzaharTextureGeneration =
        azaharTexturePacks.Generation();
    const auto materialRevision = azaharTexturePacks.MaterialRevision();
    if (mFrameMaterialRevision != materialRevision &&
        Extension::UsesPbrMaterials(graphicsSettings.Effects.SurfaceLighting)) {
        mTemporalAaPass.ResetHistory();
        mTaaOutputValid = false;
    }
    mFrameMaterialRevision = materialRevision;
    const char* taaEnvironment = std::getenv("OOT3D_GRAPHICS_TAA");
    const char* motionEnvironment = std::getenv("OOT3D_GRAPHICS_MOTION");
    const Extension::AntiAliasingFrameCapabilities aaCapabilities{
        mNativePicaDepthResolveMode != VK_RESOLVE_MODE_NONE,
        (mNativePicaSupportedSampleCounts & VK_SAMPLE_COUNT_2_BIT) != 0U,
        (mNativePicaSupportedSampleCounts & VK_SAMPLE_COUNT_4_BIT) != 0U,
        (mNativePicaSupportedSampleCounts & VK_SAMPLE_COUNT_8_BIT) != 0U};
    const auto aaPolicy = Extension::ResolveAntiAliasingFramePolicy(
        graphicsSettings, aaCapabilities,
        taaEnvironment != nullptr &&
            std::string_view(taaEnvironment) == "1",
        motionEnvironment != nullptr &&
            std::string_view(motionEnvironment) == "1");
    mNativeFidelityProfile =
        graphicsSettings.Preset == Extension::GraphicsPreset::Authentic;
    mTemporalJitterEnabled =
        !mNativeFidelityProfile && aaPolicy.TemporalJitter;
    mPresentationWorldSurface =
        !mNativeFidelityProfile && aaPolicy.PresentationWorldSurface;
    mSceneAntiAliasing = !mNativeFidelityProfile && aaPolicy.SceneAntiAliasing;
    mPresentationUpscaler = !mNativeFidelityProfile && aaPolicy.PresentationUpscaler &&
                            mNativeUiOverlayPass.Available();
    // The rendering snapshot already disables GI outside per-pixel lighting
    // and under native presentation.
    const bool globalIlluminationRequested =
        graphicsSettings.Effects.GlobalIllumination.Mode != Extension::GlobalIlluminationMode::Off &&
        !mNativeFidelityProfile;
    const bool screenSpaceGlobalIlluminationRequested =
        globalIlluminationRequested &&
        graphicsSettings.Effects.GlobalIllumination.Mode == Extension::GlobalIlluminationMode::ScreenSpace;
    // GI and temporal MXAO reproject their history with the scene's motion
    // vectors.
    const bool temporalAmbientOcclusionRequested =
        graphicsSettings.Effects.AmbientOcclusion == Extension::AmbientOcclusionMode::Mxao &&
        graphicsSettings.Effects.MxaoTemporal;
    mTemporalMotionEnabled =
        !mNativeFidelityProfile &&
        (aaPolicy.TemporalMotion || globalIlluminationRequested || temporalAmbientOcclusionRequested);
    const Extension::PicaAttachmentFeatureRequests extensionRequests{
        graphicsSettings.Effects.DirectionalShadows.Mode ==
            Extension::DirectionalShadowMode::VirtualShadowMaps,
        graphicsSettings.Effects.AmbientOcclusion != Extension::AmbientOcclusionMode::Off,
        graphicsSettings.Effects.Toon != Extension::ToonMode::Off &&
            graphicsSettings.Effects.ToonStyle.OutlineEnabled,
        graphicsSettings.Effects.Reflections !=
            Extension::ReflectionMode::Off,
        mTemporalMotionEnabled,
        graphicsSettings.Grass.Quality != Extension::GrassQuality::Off,
        globalIlluminationRequested,
        graphicsSettings.Effects.PointShadows.Mode == Extension::PointShadowMode::CubeMaps,
        Extension::EffectsReadFaceNormal(graphicsSettings.Effects) ||
            (screenSpaceGlobalIlluminationRequested &&
             Extension::GlobalIlluminationReadsFaceNormal(graphicsSettings.Effects.GlobalIllumination)),
        screenSpaceGlobalIlluminationRequested,
        graphicsSettings.Effects.DirectionalShadows.Mode == Extension::DirectionalShadowMode::VirtualShadowMaps &&
            graphicsSettings.Effects.DirectionalShadows.ContactShadows.Enabled,
        mWaterOnlyReflections,
    };
    if (!mPicaExtensionRequests.has_value() ||
        *mPicaExtensionRequests != extensionRequests) {
        auto graph = Extension::BuildPicaExtensionGraph(extensionRequests);
        if (!graph.Valid()) {
            throw std::runtime_error(
                "PICA extension graph is invalid: " + graph.Error);
        }
        auto directionalShadowMapBarriers =
            Extension::BuildEffectPassBarrierPlan(
                graph,
                Extension::PicaExtensionPassName(
                    Extension::PicaExtensionPass::DirectionalShadowMap));
        auto directionalShadowSchedule =
            Extension::BuildPicaEffectPassSchedulePlan(
                graph,
                Extension::PicaExtensionPassName(
                    Extension::PicaExtensionPass::DirectionalShadowMap),
                Extension::EffectContractKind::AuxiliaryOutput);
        auto pointShadowMapBarriers = Extension::BuildEffectPassBarrierPlan(
            graph, Extension::PicaExtensionPassName(Extension::PicaExtensionPass::PointShadowMap));
        auto pointShadowSchedule = Extension::BuildPicaEffectPassSchedulePlan(
            graph, Extension::PicaExtensionPassName(Extension::PicaExtensionPass::PointShadowMap),
            Extension::EffectContractKind::AuxiliaryOutput);
        if (extensionRequests.PointShadows &&
            (!pointShadowMapBarriers.Valid() || !pointShadowSchedule.Valid() ||
             !Extension::PicaExtensionPassEnabled(graph, Extension::PicaExtensionPass::PointShadowLighting))) {
            throw std::runtime_error("PICA extension graph did not compile point shadow contracts");
        }
        auto interactiveGrassProvider =
            Extension::BuildEffectGeometryProviderPlan(
                graph,
                Extension::PicaExtensionPassName(
                    Extension::PicaExtensionPass::InteractiveGrass));
        if (extensionRequests.DirectionalShadows &&
            (!directionalShadowMapBarriers.Valid() ||
             !directionalShadowSchedule.Valid() ||
             !Extension::PicaExtensionPassEnabled(
                 graph,
                 Extension::PicaExtensionPass::DirectionalShadowLighting))) {
            throw std::runtime_error(
                "PICA extension graph did not compile directional shadow contracts");
        }
        if (extensionRequests.InteractiveGrass &&
            !interactiveGrassProvider.Valid()) {
            throw std::runtime_error(
                "PICA extension graph did not compile the interactive grass geometry-provider contract");
        }
        // Disabling the field (GI off) releases its world: re-enabling it
        // later must not trace geometry of a world seen before.
        if (Extension::PicaExtensionPassEnabled(mPicaExtensionGraph, Extension::PicaExtensionPass::SceneDistanceField) &&
            !Extension::PicaExtensionPassEnabled(graph, Extension::PicaExtensionPass::SceneDistanceField)) {
            mBrixelizerGiPass.ResetHistory();
            mBrixelizerScene.Reset();
        }
        mPicaExtensionRequests = extensionRequests;
        mPicaExtensionGraph = std::move(graph);
        mInteractiveGrassProviderPlan =
            std::move(interactiveGrassProvider);
        mDirectionalShadowMapBarrierPlan =
            std::move(directionalShadowMapBarriers);
        mDirectionalShadowSchedulePlan =
            std::move(directionalShadowSchedule);
        mPointShadowMapBarrierPlan = std::move(pointShadowMapBarriers);
        mPointShadowSchedulePlan = std::move(pointShadowSchedule);
    }
    mInteractiveGrassProviderExecution.Reset(
        mInteractiveGrassProviderPlan);
    auto frameAttachments = mPicaExtensionGraph.Attachments;
    // Dynamic rendering leaves the guides no effect requires unbound; the
    // instrumented render pass binds every base guide.
    if (!mPicaDynamicRenderingScope.Available() && !frameAttachments.NativeColorOnly())
        frameAttachments.AuxiliaryOutputs |= Extension::kAllPicaAuxiliaryOutputs;
    if (mFramePicaAttachmentRequirements != frameAttachments) {
        // Effect activation is not a native framebuffer invalidation. Retain
        // color/depth, transfer snapshots and cached framebuffer variants.
        for (auto& [key, target] : mNativePicaRenderTargets) {
            target.AuxiliaryNeedsClear = true;
        }
    }
    mFramePicaAttachmentRequirements = frameAttachments;
    if (mNativeFidelityProfile &&
        !mFramePicaAttachmentRequirements.NativeColorOnly()) {
        throw std::runtime_error(
            "Native Fidelity requested auxiliary PICA attachments");
    }
    mTemporalJitterPixels = mTemporalJitterEnabled
        ? Extension::TemporalJitterForFrame(
              mTemporalJitterSequence,
              aaPolicy.TemporalJitterPhases).PixelOffset
        : std::array<float, 2>{};
    if (!mTemporalMotionEnabled) {
        mPicaTemporalHistory.Reset();
    }
    if (!mTemporalJitterEnabled) {
        mTaaOutputValid = false;
        mTemporalAaPass.ResetHistory();
    }
    if (graphicsSettings.Effects.AmbientOcclusion == Extension::AmbientOcclusionMode::Off) {
        // The image may remain allocated for cheap re-enabling, but it must no
        // longer be eligible for scanout composition as soon as AO is Off.
        mAmbientOcclusionOutputValid = false;
    }
    const float requestedInternalResolutionScale =
        Extension::ResolveInternalResolutionScale(graphicsSettings);
    mSpatialAaMode = aaPolicy.SpatialAaMode;
    if (mVsyncEnabled != graphicsSettings.VSync) {
        mVsyncEnabled = graphicsSettings.VSync;
        mSwapchainDirty = true;
    }
    markStart(0);
    WaitForFramePresent(mCurrentFrame);
    markStart(1);
    const VkSampleCountFlagBits requestedSampleCount =
        aaPolicy.MsaaSamples >= 8U ? VK_SAMPLE_COUNT_8_BIT
        : aaPolicy.MsaaSamples >= 4U ? VK_SAMPLE_COUNT_4_BIT
        : aaPolicy.MsaaSamples >= 2U ? VK_SAMPLE_COUNT_2_BIT
                                    : VK_SAMPLE_COUNT_1_BIT;
    if (requestedSampleCount != mNativePicaSampleCount) {
        ApplyNativePicaSampleCount(requestedSampleCount);
    }
#if defined(__ANDROID__)
    // A lost or absent Android surface (the Activity in the background) is a
    // pause: no frame until the surface and swapchain are rebuilt. Before the
    // suboptimal check below, which would query the surface.
    if (!EnsureAndroidSurface()) {
        return;
    }
#endif
    if (mPresentSwapchainDirty.exchange(false)) {
        mSwapchainDirty = true;
    }
    if (mSwapchainSuboptimal.exchange(false) && !mSwapchainDirty) {
        mSwapchainDirty = SwapchainSurfaceChanged();
    }
    const auto presentError = static_cast<VkResult>(mPresentError.exchange(VK_SUCCESS));
    if (presentError != VK_SUCCESS) {
#if defined(__ANDROID__)
        if (presentError == VK_ERROR_SURFACE_LOST_KHR) {
            NoteAndroidSurfaceLost("present");
            return;
        }
#endif
        CheckVk(presentError, "asynchronous vkQueuePresentKHR");
    }
    if (std::abs(requestedInternalResolutionScale -
                 mInternalResolutionScale) > 0.0005F) {
        ApplyInternalResolutionScale(requestedInternalResolutionScale);
    }
    if (mSwapchainDirty) {
        RecreateSwapchain();
        if (mSwapchainDirty) {
            return;
        }
    }
    PrewarmNativePicaPipelines();
    markStart(2);

    CheckVk(vkWaitForFences(mDevice, 1, &mInFlightFences[mCurrentFrame], VK_TRUE,
                            std::numeric_limits<uint64_t>::max()),
            "vkWaitForFences");
    markStart(3);
    // A fence handle is reused for another submission below. Retire all image
    // references to its completed submission before resetting it; otherwise a
    // later acquisition can wait for unrelated work using the same handle.
    std::replace(mImagesInFlight.begin(), mImagesInFlight.end(),
                 mInFlightFences[mCurrentFrame], VkFence{VK_NULL_HANDLE});
    mScanoutProbe.Consume(mCurrentFrame);
    ReleaseRetiredNativePicaGeometryBuffers(mCurrentFrame);
    ReleaseRetiredNativePicaDisplayImages(mCurrentFrame);
    RetireGpuTextureCopyProducers(mGpuCopySubmittedFrames[mCurrentFrame]);
    auto& frame = mFrameResources[mCurrentFrame];
    for (auto& image : frame.RetiredTextures) DestroyTexture(image);
    frame.RetiredTextures.clear();
    for (auto& buffer : frame.RetiredUniformBuffers) DestroyBuffer(buffer);
    frame.RetiredUniformBuffers.clear();
    for (auto& buffer : frame.RetiredVertexBuffers) DestroyBuffer(buffer);
    frame.RetiredVertexBuffers.clear();
    for (auto material = mNativePicaMaterials.begin(); material != mNativePicaMaterials.end();) {
        if (mFrameCounter <= material->second.LastUsedFrame + 240U) {
            ++material;
            continue;
        }
        RetireNativePicaMaterial(material->second.Active);
        RetireNativePicaMaterial(material->second.Pending);
        material = mNativePicaMaterials.erase(material);
    }
    // Retired into this slot, after its completed work was released: the
    // images are destroyed only once this frame's own submission completed.
    if (mNativePicaTexturesGeneration != mFrameAzaharTextureGeneration)
        RetireNativePicaTexturesOfOtherGenerations(mFrameAzaharTextureGeneration);
    ReleaseIdleNativePicaTextures();
    mCompletedNativePicaIds.insert(mCompletedNativePicaIds.end(),
                                   frame.NativePicaCompletionIds.begin(),
                                   frame.NativePicaCompletionIds.end());
    frame.NativePicaCompletionIds.clear();
    markStart(4);
    bool nriAcquire = false;
    VkResult acquire = VK_NOT_READY;
    if (mBenchmarkNoPresent) {
        // Private targets, one per frame in flight, guarded by the frame fence.
        mCurrentImage = mCurrentFrame;
        acquire = VK_SUCCESS;
    }
    for (; !mBenchmarkNoPresent;) {
        if (mNriSwapchain.Active()) {
            const auto result =
                mNriSwapchain.Acquire(mCurrentFrame, mCurrentImage);
            if (result ==
                Extension::NriSwapchainOperationResult::OutOfDate) {
                mSwapchainDirty = true;
                // Let the host pump window events before retrying acquisition.
                return;
            }
            if (result !=
                Extension::NriSwapchainOperationResult::Success) {
                if (result == Extension::NriSwapchainOperationResult::DeviceLost)
                    ReportDeviceLoss(VK_ERROR_DEVICE_LOST, "NRI AcquireNextTexture");
                throw std::runtime_error(
                    "NRI AcquireNextTexture failed: " +
                    mNriSwapchain.UnavailableReason());
            }
            nriAcquire = true;
            acquire = VK_SUCCESS;
            break;
        }
        {
            std::lock_guard swapchainLock(mSwapchainCallMutex);
            acquire = vkAcquireNextImageKHR(
                mDevice, mSwapchain, std::numeric_limits<uint64_t>::max(),
                mImageAvailableSemaphores[mCurrentFrame], VK_NULL_HANDLE,
                &mCurrentImage);
        }
        if (acquire == VK_NOT_READY || acquire == VK_TIMEOUT) {
            return;
        }
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            mSwapchainDirty = true;
            return;
        }
#if defined(__ANDROID__)
        if (acquire == VK_ERROR_SURFACE_LOST_KHR) {
            NoteAndroidSurfaceLost("acquire");
            return;
        }
#endif
        break;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        CheckVk(acquire, "vkAcquireNextImageKHR");
    }
    if (acquire == VK_SUBOPTIMAL_KHR) {
        mSwapchainSuboptimal.store(true);
    }

    markStart(5);
    // The presentation picture: the chosen aspect ratio's letterbox inside the
    // successfully acquired swapchain (the whole swapchain for Free). Native
    // PICA targets follow the picture, never the swapchain, so a change of
    // ratio rebuilds them like a resize does.
    mPresentationPicture = Extension::ResolvePresentationPicture(
        mSwapchainExtent.width, mSwapchainExtent.height,
        graphicsSnapshot.Value.Aspect);
    // Validate targets against the picture of the successfully acquired
    // swapchain, not the requested window size or the previous swapchain.
    // Mode/present-only changes retain targets; saved display pixels bridge
    // presentation-only frames.
    const auto pictureExtent = NativePicaOutputExtent();
    const bool staleTargetExtent = std::any_of(
        mNativePicaRenderTargets.begin(), mNativePicaRenderTargets.end(),
        [&](const auto& entry) {
            const auto& [key, target] = entry;
            const auto expected = Extension::ResolveNativePicaRenderExtent(
                {key.Width, key.Height},
                NativeTopScreenOutputExtent(),
                mInternalResolutionScale);
            return target.Width != expected.Width || target.Height != expected.Height;
        });
    if (staleTargetExtent) {
        ResetNativePicaRenderTargets(true);
    }
    settingsRuntime.PublishDisplayMetrics({
        mSwapchainExtent.width, mSwapchainExtent.height, 0, 0,
        mInternalResolutionScale,
        !mWindowBackend->IsFullscreen() ? Extension::WindowMode::Windowed :
        mWindowBackend->IsWindowedFullscreen() ? Extension::WindowMode::Borderless :
                                               Extension::WindowMode::ExclusiveFullscreen,
        pictureExtent.Width, pictureExtent.Height});

    if (mImagesInFlight[mCurrentImage] != VK_NULL_HANDLE) {
        CheckVk(vkWaitForFences(mDevice, 1, &mImagesInFlight[mCurrentImage], VK_TRUE,
                                std::numeric_limits<uint64_t>::max()),
                "vkWaitForFences(image)");
    }
    mImagesInFlight[mCurrentImage] = mInFlightFences[mCurrentFrame];
    if (frame.VertexBytesUsed > mPeakFrameVertexBytes) mPeakFrameVertexBytes = frame.VertexBytesUsed;
    frame.VertexBytesUsed = 0;
    frame.UniformBytesUsed = 0;
    frame.SurfaceLightingUniformOffset.reset();
    CheckVk(vkResetDescriptorPool(mDevice, frame.DescriptorPool, 0), "vkResetDescriptorPool");
    frame.TextureDescriptorSets.clear();
    CheckVk(vkResetFences(mDevice, 1, &mInFlightFences[mCurrentFrame]), "vkResetFences");
    CheckVk(vkResetCommandBuffer(mCommandBuffers[mCurrentFrame], 0), "vkResetCommandBuffer");

    mFrameAcquirePending = !mBenchmarkNoPresent;
    mFrameExternalComputeSplit = false;
    mFrameExternalWaitSemaphore = VK_NULL_HANDLE;
    mFrameExternalWaitValue = 0U;

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(mCommandBuffers[mCurrentFrame], &beginInfo), "vkBeginCommandBuffer");
    if (mNriInterop.Available() &&
        !mNriInterop.WrapFrameCommandBuffer(mCurrentFrame, mCommandBuffers[mCurrentFrame])) {
        SPDLOG_WARN("OOT3D Vulkan NRI could not wrap frame command buffer {}", mCurrentFrame);
    }
    mFrameActive = true;
    PrepareRecordedDlssFeature();
    ++mFrameCounter;
    mBreadcrumbs.SetFrame(mFrameCounter);
    mBreadcrumbs.Record(mCommandBuffers[mCurrentFrame], "frame_begin");
    Extension::BeginGrassStageFrame(mFrameCounter);
    mSurfaceCalibrationTrace.BeginFrame(mFrameCounter);
    mDrawCallCountThisFrame = 0;
    mRenderPassActive = false;
    mOverlayRenderPassActive = false;
    mNativePicaRenderPassActive = false;
    mActiveNativePicaRenderTarget = nullptr;
    mNativePicaPresentedThisFrame = false;
    mPicaSceneFrame.BeginFrame(mCurrentFrame, mFrameCounter);
    mPreparedShadowCasterFrame.BeginFrame(mCurrentFrame, mFrameCounter);
    mPreparedShadowCasters.clear();
    mPicaScenePublications.Reset();
    mDiagnostics.BeginFrame(mFrameCounter);
    mDiagnostics.RecordPicaShaderProfile(mNativeFidelityProfile);
    mDiagnostics.RecordPicaAttachmentContract(
        mFramePicaAttachmentRequirements.ColorAttachmentCount(),
        mFramePicaAttachmentRequirements.Key());
    const auto diagnosticCount = [](uint64_t value) {
        return static_cast<uint32_t>(std::min<uint64_t>(
            value, std::numeric_limits<uint32_t>::max()));
    };
    mDiagnostics.RecordPicaPipelinePrewarm(
        diagnosticCount(mNativePicaPipelines.size()),
        diagnosticCount(mPicaPipelineManifest.Entries().size()),
        mPicaPipelinePrewarmEnabled,
        diagnosticCount(mPicaPipelinePrewarmCreated),
        diagnosticCount(mPicaPipelinePrewarmReused),
        diagnosticCount(mPicaPipelinePrewarmSkipped));
    mDiagnostics.RecordEffectGraph(
        static_cast<uint32_t>(mPicaExtensionGraph.Passes.size()),
        static_cast<uint32_t>(
            mPicaExtensionGraph.ResourceLifetimes.size()),
        static_cast<uint32_t>(std::min<size_t>(
            mPicaExtensionGraph.DeclaredBindingCount(),
            std::numeric_limits<uint32_t>::max())),
        static_cast<uint32_t>(mPicaExtensionGraph.Barriers.size()));
    mDiagnostics.RecordRendererValidation(
        mValidationTelemetry.Snapshot());
    mDiagnostics.RecordNriSwapchainAcquire(
        nriAcquire,
        static_cast<uint32_t>(mSwapchainImages.size()),
        nriAcquire,
        nriAcquire);
    if (const auto timings = mGpuProfiler.BeginFrame(mCurrentFrame, mFrameCounter, mCommandBuffers[mCurrentFrame]);
        mFrameCounter > kFramesInFlight) {
        if (timings)
            mDiagnostics.SetGpuTimings(mFrameCounter - kFramesInFlight, *timings);
        if (Extension::GrassStageProfileEnabled()) {
            const auto measured = timings.value_or(Oot3dVulkanGpuTimings{});
            Extension::ReportGrassStageFrame(
                mFrameCounter - kFramesInFlight, measured.GrassMilliseconds.value_or(-1),
                measured.GrassCompactionMilliseconds.value_or(-1), measured.GrassBladesMilliseconds.value_or(-1),
                measured.GrassShadowsMilliseconds.value_or(-1), measured.GrassLightingMilliseconds.value_or(-1),
                measured.GrassCardsMilliseconds.value_or(-1));
        }
    }
    mViewport = { 0.0f, 0.0f, static_cast<float>(mSwapchainExtent.width),
                  static_cast<float>(mSwapchainExtent.height), 0.0f, 1.0f };
    mScissor = { { 0, 0 }, mSwapchainExtent };
    mViewportSet = true;
    mScissorSet = true;
    markStart(6);
    if (traceStart) {
        double total = 0;
        for (double ms : startTimes) total += ms;
        if (total >= 10.0) {
            std::fprintf(stderr, "TRIAEVUM_SLOW_FRAME_START frame=%llu settings_ms=%.3f present_wait_ms=%.3f transitions_ms=%.3f fence_ms=%.3f retire_ms=%.3f acquire_ms=%.3f finish_ms=%.3f\n",
                static_cast<unsigned long long>(mFrameCounter), startTimes[0], startTimes[1],
                startTimes[2], startTimes[3], startTimes[4], startTimes[5], startTimes[6]);
        }
    }
}

void GfxRenderingAPIVulkan::EndFrame() {
    Renderer3ds::SlowStepScope slowStep("end_frame");
    if (!mFrameActive) {
        return;
    }
    StoreDlssFeatureRecord();
    if (mOot3dShadow2dPassActive) {
        EndOot3dShadow2dDepthEncodePass();
    }
    EndNativePicaRenderPass();
    if (!mNativePicaPresentedThisFrame &&
        mLastPresentedNativePicaDisplayTransfer.has_value()) {
        std::string error;
        GfxNativePicaDisplayTransferView transfer =
            *mLastPresentedNativePicaDisplayTransfer;
        transfer.Present = true;
        if (!SubmitPicaDisplayTransfer(
                transfer, &error)) {
            throw std::runtime_error(
                "persistent native PICA scanout failed: " + error);
        }
    }
    if (!mNativePicaPresentedThisFrame) {
        BeginRenderPassIfNeeded();
    }
    if (mRenderPassActive) {
        vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
        mRenderPassActive = false;
        mOverlayRenderPassActive = false;
    }
    const auto sceneFrameStats = mPicaSceneFrame.Stats();
    if (sceneFrameStats.DrawCount != 0U) ++mTemporalJitterSequence;
    mScanoutProbe.Record(mCommandBuffers[mCurrentFrame], mSwapchainImages[mCurrentImage],
                         mSwapchainFormat, mSwapchainExtent, mCurrentFrame, mCurrentImage,
                         mFrameCounter, mNativePicaPresentedThisFrame);
    mDiagnostics.RecordPicaSceneFrame(sceneFrameStats);
    mGpuProfiler.EndFrame(mCurrentFrame, mCommandBuffers[mCurrentFrame]);
    mBreadcrumbs.Record(mCommandBuffers[mCurrentFrame], "frame_end");
    CheckVk(vkEndCommandBuffer(mCommandBuffers[mCurrentFrame]), "vkEndCommandBuffer");

    SubmitFrameCommandBuffer(mCommandBuffers[mCurrentFrame],
                             mInFlightFences[mCurrentFrame],
                             mRenderFinishedSemaphores[mCurrentImage]);
    mGpuCopySubmittedFrames[mCurrentFrame] = mFrameCounter;
    if (mNriPipelineCacheFlusher.Running())
        mNriPipelineCacheFlusher.Notice(mNriPicaPipelineBridge.PipelineStatistics().Created);
    mFrameActive = false;
    mFrameSubmitted = true;
}

void GfxRenderingAPIVulkan::FinishRender() {
    Renderer3ds::SlowStepScope slowStep("finish_render");
    if (!mFrameSubmitted) {
        return;
    }
    bool nriPresented = false;
    if (mBenchmarkNoPresent) {
        // Throughput benchmark: the frame is complete once submitted.
    } else if (mNriSwapchain.Active()) {
        const auto result = mNriSwapchain.Present(mCurrentImage);
        if (result == Extension::NriSwapchainOperationResult::OutOfDate) {
            mSwapchainDirty = true;
        } else if (result !=
                   Extension::NriSwapchainOperationResult::Success) {
            if (result == Extension::NriSwapchainOperationResult::DeviceLost)
                ReportDeviceLoss(VK_ERROR_DEVICE_LOST, "NRI QueuePresent");
            throw std::runtime_error(
                "NRI QueuePresent failed: " +
                mNriSwapchain.UnavailableReason());
        } else {
            nriPresented = true;
        }
    } else if (mAsyncPresentSupported) {
        PresentRequest request;
        request.Swapchain = mSwapchain;
        request.WaitSemaphore = mRenderFinishedSemaphores[mCurrentImage];
        request.ImageIndex = mCurrentImage;
        request.FrameIndex = mCurrentFrame;
        {
            std::lock_guard lock(mPresentMutex);
            request.Serial = mNextPresentSerial++;
            mFramePresentSerials[mCurrentFrame] = request.Serial;
            mPresentRequests.push_back(request);
        }
        mPresentRequestCondition.notify_one();
    } else {
        VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &mRenderFinishedSemaphores[mCurrentImage];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &mSwapchain;
        presentInfo.pImageIndices = &mCurrentImage;
        VkResult present;
        {
            std::lock_guard swapchainLock(mSwapchainCallMutex);
            present = vkQueuePresentKHR(mPresentQueue, &presentInfo);
        }
        if (present == VK_ERROR_OUT_OF_DATE_KHR) {
            mSwapchainDirty = true;
        } else if (present == VK_SUBOPTIMAL_KHR) {
            mSwapchainSuboptimal.store(true);
#if defined(__ANDROID__)
        } else if (present == VK_ERROR_SURFACE_LOST_KHR) {
            NoteAndroidSurfaceLost("present");
#endif
        } else if (present != VK_SUCCESS) {
            CheckVk(present, "vkQueuePresentKHR");
        }
    }
    {
        // The startup report's last renderer step: the first frame was queued.
        static std::atomic<bool> firstPresentReported{false};
        if (!firstPresentReported.exchange(true, std::memory_order_relaxed))
            StartupCheckpointf("first_frame_presented %ux%u", mSwapchainExtent.width, mSwapchainExtent.height);
    }
    mDiagnostics.RecordNriSwapchainPresent(nriPresented);
    mDiagnostics.RecordEffectGeometryProvider(
        mInteractiveGrassProviderExecution.Summary());
    mDiagnostics.RecordRendererValidation(
        mValidationTelemetry.Snapshot());
    mDiagnostics.EndFrame();
    FinishSurfaceCalibrationTrace();
    mFrameSubmitted = false;
    mCurrentFrame = (mCurrentFrame + 1) % kFramesInFlight;
}

} // namespace Fast

#endif
