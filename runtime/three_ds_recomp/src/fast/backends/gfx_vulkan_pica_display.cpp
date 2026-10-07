// Native PICA on Vulkan: display transfers, the display-effect graph, scene composition for copies,
// clears, overlays and memory fills.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_pica_internal.h"

namespace Fast {

namespace {

// The declared end-of-target providers of one composition target: the shadow
// map at its graph-authorized native anchor and the interactive geometry
// published for the target. Both the display transfer and native copy boundaries compose
// the same scene, so the providers and their anchors live here once instead of
// in two lists.
struct NativePicaTargetEndDispatch {
    const Oot3d::PicaCompositionStageAnchor* ShadowAnchor = nullptr;
    const Oot3d::PicaCompositionStageAnchor* PointShadowAnchor = nullptr;
    bool DispatchGeometry = false;
};

// `submissionId` is 0 for a display transfer, which composes at the target's own
// end, and the consumer's real order id for a native boundary, which composes
// the scene as of that submission: an anchor the native sequence has not
// reached is never flushed into the composition, and later draws are never
// moved across it. A boundary therefore accepts the target's declared anchor
// once the sequence has passed its insertion point - including a target-end
// anchor, which is reached only after every draw of the sequence.
[[nodiscard]] NativePicaTargetEndDispatch ResolveNativePicaTargetEndDispatch(
    const Oot3d::PicaCompositionSchedule& schedule,
    const ::Fast::Renderer::ExtensionPassSchedulePlan& shadowSchedule,
    const ::Fast::Renderer::ExtensionPassSchedulePlan& pointShadowSchedule,
    bool geometryProviderValid,
    const ::Oot3d::Renderer::PicaCompositionTargetReference& target,
    uint64_t submissionId, bool geometryTargetPublished,
    bool geometryAlreadyAttempted) {
    NativePicaTargetEndDispatch dispatch;
    if (!schedule.Valid()) {
        return dispatch;
    }
    if (shadowSchedule.Valid()) {
        const auto* anchor = Oot3d::FindPicaEffectPassAnchor(schedule, shadowSchedule, target);
        if (anchor != nullptr && schedule.Reached(*anchor) &&
            (submissionId != 0U || anchor->AtTargetEnd)) {
            dispatch.ShadowAnchor = anchor;
        }
    }
    if (pointShadowSchedule.Valid()) {
        const auto* anchor = Oot3d::FindPicaEffectPassAnchor(schedule, pointShadowSchedule, target);
        if (anchor != nullptr && schedule.Reached(*anchor) &&
            (submissionId != 0U || anchor->AtTargetEnd)) {
            dispatch.PointShadowAnchor = anchor;
        }
    }
    if (geometryProviderValid) {
        const auto* anchor = schedule.FindPublishedGeometryAnchor(target);
        dispatch.DispatchGeometry =
            anchor != nullptr && anchor->AtTargetEnd &&
            geometryTargetPublished && !geometryAlreadyAttempted &&
            (submissionId == 0U || schedule.Reached(*anchor));
    }
    return dispatch;
}

} // namespace

bool GfxRenderingAPIVulkan::SubmitPicaDisplayTransfer(
    const GfxNativePicaDisplayTransferView& transfer, std::string* error) {
    Renderer3ds::SlowStepScope slowStep("display_transfer");
    if (mFrameActive) mGpuProfiler.Mark(mCurrentFrame, "display_transfer_submit", mCommandBuffers[mCurrentFrame]);
    if (transfer.TextureCopyBytes != 0U) return SubmitNativePicaTextureCopy(transfer, error);
    try {
        if (!mFrameActive || transfer.CompletionId == 0U ||
            transfer.InputWidth == 0U || transfer.InputHeight == 0U ||
            transfer.OutputWidth == 0U || transfer.OutputHeight == 0U) {
            throw std::runtime_error(
                "native PICA display transfer is incomplete");
        }
        if ((transfer.Flags & 8U) != 0U ||
            (transfer.Flags & 0x10000U) != 0U ||
            ((transfer.Flags >> 24U) & 3U) > 2U) {
            throw std::runtime_error(
                "native PICA display transfer mode is unsupported");
        }
        if (!transfer.Present) {
            NativePicaRenderTarget* source = nullptr;
            for (auto& [key, target] : mNativePicaRenderTargets) {
                if (key.RenderTargetNamespace ==
                        transfer.RenderTargetNamespace &&
                    key.ColorPhysicalAddress ==
                    transfer.InputPhysicalAddress) {
                    source = &target;
                    break;
                }
            }
            if (source == nullptr) {
                throw std::runtime_error(
                    "native PICA display transfer source has no Vulkan render target");
            }
            if (transfer.InputWidth > source->Key.Width ||
                transfer.InputHeight > source->Key.Height) {
                throw std::runtime_error(
                    "native PICA display transfer exceeds its Vulkan render target");
            }
            std::string transferPlanError;
            const auto transferPlan =
                Oot3d::BuildPicaDisplayTransferPlan({
                    transfer.InputWidth,
                    transfer.InputHeight,
                    transfer.OutputWidth,
                    transfer.OutputHeight,
                    source->Width,
                    source->Height,
                    transfer.Flags,
                    // Target extents follow this policy for the acquired
                    // swapchain (stale targets are rebuilt at frame start).
                    Oot3d::IsNativeTopScreenTarget({source->Key.Width, source->Key.Height}) &&
                        Oot3d::NativeTopScreenAtPresentedDensity(
                            NativeTopScreenOutputExtent()),
                }, &transferPlanError);
            if (!transferPlan.has_value()) {
                throw std::runtime_error(
                    "native PICA display transfer plan failed: " +
                    transferPlanError);
            }
            // The presentation transfer (the 2:1 line filter) publishes the
            // scene extent; reduced or raw copies of the same target do not.
            if (Oot3d::IsNativeTopScreenTarget({source->Key.Width, source->Key.Height}) &&
                transferPlan->ScalingMode == 1U) {
                Oot3d::GraphicsSettingsRuntime::Instance().PublishSceneExtent(
                    transferPlan->DestinationHeight, transferPlan->DestinationWidth);
            }

            const auto& graphicsSettings =
                mFrameGraphicsSettings;
            const auto perspective = SamplePerspective(source);
            (void)mNativeSceneView.Publish(
                mFrameCounter, mPicaSceneFrame, perspective);
            const ::Oot3d::Renderer::PicaCompositionTargetReference
                compositionTarget{
                    source->Key.RenderTargetNamespace,
                    source->Key.ColorPhysicalAddress,
                    source->Key.DepthPhysicalAddress,
                    source->Key.Width,
                    source->Key.Height,
                    source->Key.ColorFormat,
                    source->Key.DepthFormat,
                };
            const auto targetEnd = ResolveNativePicaTargetEndDispatch(
                mNativePicaCompositionSchedule,
                mDirectionalShadowSchedulePlan,
                mPointShadowSchedulePlan,
                mInteractiveGrassProviderPlan.Valid(), compositionTarget,
                /*submissionId=*/0U,
                mGrassWorldTargetsThisFrame.contains(source->Key),
                mGrassInsertionAttemptedTargetsThisFrame.contains(
                    source->Key));
            if (targetEnd.ShadowAnchor != nullptr) {
                TryRenderDirectionalShadowMap(
                    *source, graphicsSettings, *targetEnd.ShadowAnchor);
            }
            if (targetEnd.PointShadowAnchor != nullptr) {
                TryRenderPointShadowMaps(
                    *source, graphicsSettings, *targetEnd.PointShadowAnchor);
            }
            if (targetEnd.DispatchGeometry) {
                TryRenderInteractiveGrass(
                    *source, graphicsSettings,
                    Oot3d::EffectGeometryProviderInvocationKind::DeclaredBoundary);
            }
            EndNativePicaRenderPass();
            if (mRenderPassActive) {
                vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
                mRenderPassActive = false;
                mOverlayRenderPassActive = false;
            }
            auto& display = GetOrCreateNativePicaDisplayImage(
                transfer.RenderTargetNamespace,
                transfer.OutputPhysicalAddress,
                transferPlan->DestinationWidth,
                transferPlan->DestinationHeight);
            display.GuestWidth = transfer.OutputWidth;
            display.GuestHeight = transfer.OutputHeight;
            const auto displayKey = std::make_pair(
                transfer.RenderTargetNamespace,
                transfer.OutputPhysicalAddress);
            const auto displayComposition =
                Oot3d::ResolvePicaDisplayComposition(
                    mNativePicaCompositionSchedule,
                    compositionTarget);
            display.CompositionDomain = displayComposition.Domain;
            display.CompositionSequenceId =
                displayComposition.SequenceId;
            display.SceneResolved =
                displayComposition.SceneResolved;
            // A display whose producing draw consumed a resolved scene
            // composition over the whole target already carries those scene
            // effects, so presentation must not apply them a second time.
            // Resolved here, at the moment the scene colour is snapshotted, and
            // carried on the display image: a later UI write to the same
            // attachment bumps the target's content serial and must not
            // invalidate the version that was actually imported, so this is
            // never re-queried when the snapshot is presented.
            display.SceneEffectsResolved =
                NativePicaSceneFullyResolvedForTarget(
                    source->Key.RenderTargetNamespace,
                    source->Key.ColorPhysicalAddress);
            mDiagnostics.RecordPicaDisplayComposition(
                display.CompositionDomain, display.SceneResolved);
            if (source->DepthView != VK_NULL_HANDLE &&
                (display.SceneResolved || display.SceneEffectsResolved)) {
                mNativePicaDisplayDepthTargets[displayKey] = source->Key;
            } else {
                mNativePicaDisplayDepthTargets.erase(displayKey);
            }
            mGpuProfiler.BeginScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::DisplayTransfer,
                mCommandBuffers[mCurrentFrame]);
            const VkImage displayCopySource = source->ColorImage;
            const bool nriDisplayCopy =
                mNriPicaDisplayCopyPass.Execute({
                    mCurrentFrame,
                    mFrameCounter,
                    displayCopySource,
                    display.Image,
                    VK_FORMAT_R8G8B8A8_UNORM,
                    *transferPlan,
                    false,
                    display.Initialized,
                });
            if (nriDisplayCopy) {
                mDiagnostics.RecordNriPicaDisplayCopy(
                    true,
                    mNriPicaDisplayCopyPass.LastBarrierCount());
            } else {
                VkCommandBuffer command = mCommandBuffers[mCurrentFrame];
                std::array<VkImageMemoryBarrier, 2> toTransfer{};
                toTransfer[0] = {
                    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                toTransfer[0].oldLayout =
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                toTransfer[0].newLayout =
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                toTransfer[0].srcQueueFamilyIndex =
                    VK_QUEUE_FAMILY_IGNORED;
                toTransfer[0].dstQueueFamilyIndex =
                    VK_QUEUE_FAMILY_IGNORED;
                toTransfer[0].image = displayCopySource;
                toTransfer[0].subresourceRange.aspectMask =
                    VK_IMAGE_ASPECT_COLOR_BIT;
                toTransfer[0].subresourceRange.levelCount = 1;
                toTransfer[0].subresourceRange.layerCount = 1;
                toTransfer[0].srcAccessMask =
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                toTransfer[0].dstAccessMask =
                    VK_ACCESS_TRANSFER_READ_BIT;
                toTransfer[1] = toTransfer[0];
                toTransfer[1].oldLayout =
                    display.Initialized
                        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        : VK_IMAGE_LAYOUT_UNDEFINED;
                toTransfer[1].newLayout =
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toTransfer[1].image = display.Image;
                toTransfer[1].srcAccessMask =
                    display.Initialized
                        ? VK_ACCESS_SHADER_READ_BIT
                        : 0U;
                toTransfer[1].dstAccessMask =
                    VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(
                    command,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                    0, nullptr,
                    static_cast<uint32_t>(toTransfer.size()),
                    toTransfer.data());
                VkImageBlit blit{};
                blit.srcSubresource.aspectMask =
                    VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.layerCount = 1;
                blit.srcOffsets[1] = {
                    static_cast<int32_t>(
                        transferPlan->BlitSourceWidth()),
                    static_cast<int32_t>(
                        transferPlan->BlitSourceHeight()),
                    1};
                blit.dstSubresource.aspectMask =
                    VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.layerCount = 1;
                blit.dstOffsets[1] = {
                    static_cast<int32_t>(
                        transferPlan->DestinationWidth),
                    static_cast<int32_t>(
                        transferPlan->DestinationHeight),
                    1};
                vkCmdBlitImage(
                    command, displayCopySource,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    display.Image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                    &blit,
                    transferPlan->HorizontalSamples == 1U &&
                            transferPlan->VerticalSamples == 1U
                        ? VK_FILTER_NEAREST
                        : VK_FILTER_LINEAR);
                std::array<VkImageMemoryBarrier, 2> fromTransfer =
                    toTransfer;
                fromTransfer[0].oldLayout =
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                fromTransfer[0].newLayout =
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                fromTransfer[0].srcAccessMask =
                    VK_ACCESS_TRANSFER_READ_BIT;
                fromTransfer[0].dstAccessMask =
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                fromTransfer[1].oldLayout =
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                fromTransfer[1].newLayout =
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                fromTransfer[1].srcAccessMask =
                    VK_ACCESS_TRANSFER_WRITE_BIT;
                fromTransfer[1].dstAccessMask =
                    VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(
                    command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr,
                    static_cast<uint32_t>(fromTransfer.size()),
                    fromTransfer.data());
            }
            // The scene this display showed before native UI, transferred
            // exactly as the display: an upscaler reconstructs it and lays the
            // display's UI over its output.
            display.SceneBeforeUi = false;
            if (mPresentationUpscaler && nriDisplayCopy && display.SceneEffectsResolved &&
                source->SceneBeforeUiSerial != 0U &&
                source->SceneBeforeUiSerial == source->ContentWriteSerial) {
                const auto snapshot = mNativePicaSceneBeforeUiTargets.find(
                    {source->Key.RenderTargetNamespace, source->Key.ColorPhysicalAddress});
                if (snapshot != mNativePicaSceneBeforeUiTargets.end() && snapshot->second.Initialized &&
                    snapshot->second.Width == source->Width && snapshot->second.Height == source->Height) {
                    auto& beforeUi = GetOrCreateNativePicaDisplayImage(
                        mNativePicaSceneBeforeUiDisplays, transfer.RenderTargetNamespace,
                        transfer.OutputPhysicalAddress, transferPlan->DestinationWidth,
                        transferPlan->DestinationHeight);
                    display.SceneBeforeUi = mNriPicaDisplayCopyPass.Execute({
                        mCurrentFrame, mFrameCounter, snapshot->second.Image, beforeUi.Image,
                        VK_FORMAT_R8G8B8A8_UNORM, *transferPlan, true, beforeUi.Initialized});
                    if (display.SceneBeforeUi)
                        beforeUi.Initialized = true;
                    mDiagnostics.RecordSceneBeforeUi(false, display.SceneBeforeUi);
                }
            }
            mGpuProfiler.EndScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::DisplayTransfer,
                mCommandBuffers[mCurrentFrame]);
            display.Initialized = true;
            mDiagnostics.RecordDisplayTransfer(false);
            return true;
        }

        const auto displayFound = mNativePicaDisplayImages.find(
            std::make_pair(transfer.RenderTargetNamespace,
                           transfer.OutputPhysicalAddress));
        if (displayFound == mNativePicaDisplayImages.end() ||
            !displayFound->second.Initialized) {
            if (transfer.RenderTargetNamespace == 0U &&
                mInvalidatedNativePicaRenderTargetAddresses.contains(
                    transfer.InputPhysicalAddress)) {
                return true;
            }
            throw std::runtime_error(
                "native PICA display transfer output has no GPU snapshot");
        }
        const auto& display = displayFound->second;
        return ExecuteNativePicaDisplayGraph(transfer, display, nullptr,
                                             nullptr, error);
    } catch (const std::exception& exception) {
        SetNativeError(error, exception.what());
        return false;
    }
}

bool GfxRenderingAPIVulkan::ExecuteNativePicaDisplayGraph(
    const GfxNativePicaDisplayTransferView& transfer,
    const NativePicaDisplayImage& display,
    NativePicaRenderTarget* copySource,
    GpuTextureCopySourceImport* copyOutput, std::string* error,
    bool beforeNativeUi) {
    Renderer3ds::SlowStepTrace slowSteps("display_graph");
    if (mFrameActive) mGpuProfiler.Mark(mCurrentFrame, "display_graph", mCommandBuffers[mCurrentFrame]);

        const auto& presentationSettings =
            mFrameGraphicsSettings;
        // The traced GI light carries the sources' relative weights; their
        // largest gain applies when it is composed.
        const auto giSources =
            Oot3d::ResolveGlobalIlluminationSources(presentationSettings.Effects.GlobalIllumination);
        // The camera of the scene this boundary or transfer composes.
        const NativePicaRenderTarget* sceneViewTarget = copySource;
        if (copyOutput == nullptr) {
            sceneViewTarget = nullptr;
            const auto depthAssociation = mNativePicaDisplayDepthTargets.find(
                std::make_pair(transfer.RenderTargetNamespace, transfer.OutputPhysicalAddress));
            if (depthAssociation != mNativePicaDisplayDepthTargets.end()) {
                const auto target = mNativePicaRenderTargets.find(depthAssociation->second);
                if (target != mNativePicaRenderTargets.end()) sceneViewTarget = &target->second;
            }
        }
        const auto latestPerspective = SamplePerspective(sceneViewTarget);
        (void)mNativeSceneView.Publish(
            mFrameCounter, mPicaSceneFrame, latestPerspective);
        const auto& presentationPerspective =
            mNativeSceneView.CurrentPerspective();
        const auto& nativeTemporalSample = mNativeSceneView.TemporalSample();

        EndNativePicaRenderPass();
        if (mRenderPassActive) {
            vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
            mRenderPassActive = false;
            mOverlayRenderPassActive = false;
        }
        // A native boundary (raw copy / native screen effect) composes the
        // scene for its consumer at the boundary's own order and materializes
        // it for that consumer. Presentation keeps the swapchain target.
        const bool copyBoundary = copyOutput != nullptr;
        VkCommandBuffer command = mCommandBuffers[mCurrentFrame];
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        if (!copyBoundary) {
            CreateNativePicaScanoutPipeline();
            descriptor = AllocateNativePicaScanoutDescriptorSet();
        }
        const bool alphaOverlay = !copyBoundary &&
            transfer.PresentationMode ==
                GfxNativePicaPresentationMode::AlphaOverlay;
        const auto environmentEnabled = [](const char* name) {
            const char* value = std::getenv(name);
            return value != nullptr && std::string_view(value) == "1";
        };
        const bool worldSurface =
            display.CompositionDomain ==
                ::Oot3d::Renderer::PicaCompositionDomain::Scene &&
            (display.SceneResolved || display.SceneEffectsResolved);
        // The scene before native UI transferred beside this display.
        const NativePicaDisplayImage* sceneBeforeUi = nullptr;
        if (!copyBoundary && display.SceneBeforeUi) {
            const auto found = mNativePicaSceneBeforeUiDisplays.find(
                {transfer.RenderTargetNamespace, transfer.OutputPhysicalAddress});
            if (found != mNativePicaSceneBeforeUiDisplays.end() && found->second.Initialized &&
                found->second.Width == display.Width && found->second.Height == display.Height)
                sceneBeforeUi = &found->second;
        }
        // One scene surface per frame owns the anti-aliasing history and
        // output. A boundary before native UI of another surface in the same
        // frame composes its other scene stages without them.
        const bool antiAliasingAvailableHere = [&] {
            if (!copyBoundary || !beforeNativeUi || copySource == nullptr) return true;
            const Oot3d::SceneSurfaceKey surface{
                transfer.RenderTargetNamespace, copySource->Key.ColorPhysicalAddress,
                Oot3d::SceneSurfaceKind::Output};
            const bool taaTaken = mTaaExecutedThisFrame && mTaaSurface.has_value() &&
                *mTaaSurface != surface;
            const bool smaaTaken = mSmaaExecutedThisFrame &&
                (mSmaaRenderTargetNamespace != transfer.RenderTargetNamespace ||
                 mSmaaDisplayPhysicalAddress != copySource->Key.ColorPhysicalAddress);
            return !taaTaken && !smaaTaken;
        }();
        // Screen-space GI joins the plan once its pipelines (created off the
        // frame) are ready; until then the boundary composes no GI.
        const bool screenSpaceGiRequested = presentationSettings.Effects.GlobalIllumination.Mode ==
                                            Oot3d::GlobalIlluminationMode::ScreenSpace;
        const bool screenSpaceGiReady = screenSpaceGiRequested && mScreenSpaceGi.Prepare();
        PublishGlobalIlluminationStatus(presentationSettings.Effects.GlobalIllumination.Mode);
        const auto effectPlan = Oot3d::BuildDisplayEffectPlan({
            .AlphaOverlay = alphaOverlay,
            .WorldSurface = worldSurface,
            .PerspectiveAvailable = presentationPerspective.has_value(),
            .CameraAvailable = presentationPerspective.has_value() &&
                presentationPerspective->CameraAvailable,
            .SmaaAvailable = mSmaa1xPass.Available(),
            .ForceTaa = environmentEnabled("OOT3D_GRAPHICS_TAA"),
            .ForceMotion = environmentEnabled("OOT3D_GRAPHICS_MOTION"),
            .AmbientOcclusion =
                presentationSettings.Effects.AmbientOcclusion,
            .AmbientOcclusionTemporal =
                presentationSettings.Effects.AmbientOcclusion == Oot3d::AmbientOcclusionMode::Mxao &&
                presentationSettings.Effects.MxaoTemporal && mMxaoPass.Available(),
            .AmbientOcclusionFaceNormal =
                Oot3d::EffectsReadFaceNormal(presentationSettings.Effects) && mMxaoPass.Available(),
            .Reflections = presentationSettings.Effects.Reflections,
            .GlobalIllumination =
                (presentationSettings.Effects.GlobalIllumination.Mode ==
                     Oot3d::GlobalIlluminationMode::BrixelizerGi &&
                 mBrixelizerGiPass.Available() && mBrixelizerScene.Available()) ||
                screenSpaceGiReady,
            .GlobalIlluminationSpecular = presentationSettings.Effects.GlobalIllumination.Specular,
            .GlobalIlluminationScreenSpace = screenSpaceGiRequested,
            .Toon = presentationSettings.Effects.Toon,
            .OutlineEnabled =
                presentationSettings.Effects.ToonStyle.OutlineEnabled,
            .AntiAliasing = presentationSettings.AntiAliasing,
            .Upscaler = presentationSettings.Upscaler,
            .GuideDiagnostics = Oot3d::PicaGuideDiagnosticMode() != 0,
            .NativeSceneBoundary = copyBoundary,
            .BeforeNativeUi = copyBoundary && beforeNativeUi && antiAliasingAvailableHere,
            .SceneEffectsResolved = display.SceneEffectsResolved,
            .SceneBeforeUi = sceneBeforeUi != nullptr,
            .Purpose = mNativePicaCompositionSchedule.Purpose(),
        });
        const auto motionBarrierPlan =
            Oot3d::BuildEffectPassBarrierPlan(
                effectPlan.Graph,
                Oot3d::DisplayEffectPassName(
                    Oot3d::DisplayEffectPass::Motion));
        const auto aoBarrierPlan =
            Oot3d::BuildEffectPassBarrierPlan(
                effectPlan.Graph,
                Oot3d::DisplayEffectPassName(
                    Oot3d::DisplayEffectPass::AmbientOcclusion));
        const auto hiZBarrierPlan =
            Oot3d::BuildEffectPassBarrierPlan(
                effectPlan.Graph,
                Oot3d::DisplayEffectPassName(
                    Oot3d::DisplayEffectPass::HiZ));
        const auto workingColorBarrierPlan =
            Oot3d::BuildEffectPassBarrierPlan(
                effectPlan.Graph,
                Oot3d::DisplayEffectPassName(
                    Oot3d::DisplayEffectPass::WorkingColor));
        const auto reflectionBarrierPlan =
            Oot3d::BuildEffectPassBarrierPlan(
                effectPlan.Graph,
                Oot3d::DisplayEffectPassName(
                    Oot3d::DisplayEffectPass::Reflections));
        const auto compositeBarrierPlan =
            Oot3d::BuildEffectPassBarrierPlan(
                effectPlan.Graph,
                Oot3d::DisplayEffectPassName(
                    Oot3d::DisplayEffectPass::Composite));
        const auto globalIlluminationBarrierPlan =
            Oot3d::BuildEffectPassBarrierPlan(
                effectPlan.Graph,
                Oot3d::DisplayEffectPassName(
                    Oot3d::DisplayEffectPass::GlobalIllumination));
        const auto taaBarrierPlan =
            Oot3d::BuildEffectPassBarrierPlan(
                effectPlan.Graph,
                Oot3d::DisplayEffectPassName(
                    Oot3d::DisplayEffectPass::Taa));
        const auto temporalUpscalerBarrierPlan =
            Oot3d::BuildEffectPassBarrierPlan(
                effectPlan.Graph,
                Oot3d::DisplayEffectPassName(
                    Oot3d::DisplayEffectPass::TemporalUpscaler));
        const auto nisBarrierPlan =
            Oot3d::BuildEffectPassBarrierPlan(
                effectPlan.Graph,
                Oot3d::DisplayEffectPassName(
                    Oot3d::DisplayEffectPass::Nis));
        const auto smaaBarrierPlan =
            Oot3d::BuildEffectPassBarrierPlan(
                effectPlan.Graph,
                Oot3d::DisplayEffectPassName(
                    Oot3d::DisplayEffectPass::Smaa));
        const auto scanoutBarrierPlan =
            Oot3d::BuildEffectPassBarrierPlan(
                effectPlan.Graph,
                Oot3d::DisplayEffectPassName(
                    Oot3d::DisplayEffectPass::Scanout));
        const auto sceneCompositionBarrierPlan =
            Oot3d::BuildEffectPassBarrierPlan(
                effectPlan.Graph,
                Oot3d::DisplayEffectPassName(
                    Oot3d::DisplayEffectPass::SceneComposition));
        if (!effectPlan.Graph.Valid()) {
            throw std::runtime_error(
                "display effect graph is invalid: " +
                effectPlan.Graph.Error);
        }
        const auto recordEffectPassTransitions =
            [this](const Oot3d::EffectPassBarrierExecution& execution) {
                mDiagnostics.RecordEffectGraphInternalImageTransitions(
                    execution.PlannedGraphTransitionCount(),
                    execution.EmittedGraphTransitionCount(),
                    execution.PlannedPrivateTransitionCount(),
                    execution.EmittedPrivateTransitionCount());
            };
        Oot3d::DisplayEffectExecutionLedger effectExecution(effectPlan);
        using EffectOutcome = Oot3d::DisplayEffectExecutionOutcome;
        // Every declared pass of this boundary is accounted for exactly once,
        // including the boundary composition that a native consumer samples.
        const auto recordDeclared = [&](Oot3d::DisplayEffectPass pass,
                                        EffectOutcome outcome) {
            slowSteps.Mark(Oot3d::DisplayEffectPassName(pass));
            mGpuProfiler.MarkCompleted(mCurrentFrame, Oot3d::DisplayEffectPassName(pass).data(),
                                       mCommandBuffers[mCurrentFrame]);
            if (effectPlan.Enabled(pass) &&
                !effectExecution.Record(pass, outcome)) {
                throw std::runtime_error(
                    "display effect execution ledger rejected " +
                    std::string(Oot3d::DisplayEffectPassName(pass)));
            }
        };
        const auto producedOutcome = [](bool attempted, bool produced,
                                        bool reused = false) {
            if (produced) {
                return attempted ? EffectOutcome::Executed
                                 : EffectOutcome::Reused;
            }
            if (reused) {
                return EffectOutcome::Reused;
            }
            return attempted ? EffectOutcome::Failed
                             : EffectOutcome::Skipped;
        };
        const bool displayEffectsPreviouslyPresented =
            mNativePicaPresentedThisFrame;
        mDiagnostics.RecordEffectGraph(
            static_cast<uint32_t>(effectPlan.Graph.Passes.size()),
            static_cast<uint32_t>(effectPlan.Graph.ResourceLifetimes.size()),
            static_cast<uint32_t>(std::min<size_t>(
                effectPlan.DeclaredBindingCount(),
                std::numeric_limits<uint32_t>::max())),
            static_cast<uint32_t>(effectPlan.Graph.Barriers.size()));
        const bool wantsAmbientOcclusion = effectPlan.Enabled(
            Oot3d::DisplayEffectPass::AmbientOcclusion);
        const bool wantsOutline = effectPlan.Enabled(
            Oot3d::DisplayEffectPass::Outline);
        const bool wantsReflections = effectPlan.Enabled(
            Oot3d::DisplayEffectPass::Reflections);
        const bool wantsHiZ = effectPlan.Enabled(
            Oot3d::DisplayEffectPass::HiZ);
        const bool wantsWorkingColor = effectPlan.Enabled(
            Oot3d::DisplayEffectPass::WorkingColor);
        const bool wantsFidelityFxSssr =
            wantsReflections && effectPlan.ReflectionProvider ==
                Oot3d::ReflectionMode::FidelityFxSssr;
        const bool wantsTemporalMetadata = effectPlan.TemporalMetadata;
        const bool wantsTaa = effectPlan.Enabled(
            Oot3d::DisplayEffectPass::Taa);
        const bool wantsSmaa = effectPlan.Enabled(
            Oot3d::DisplayEffectPass::Smaa);
        const bool wantsNis = effectPlan.Enabled(
            Oot3d::DisplayEffectPass::Nis);
        const bool wantsTemporalUpscaler = effectPlan.Enabled(
            Oot3d::DisplayEffectPass::TemporalUpscaler);
        const bool wantsFsr = wantsTemporalUpscaler &&
            effectPlan.Upscaler == Oot3d::UpscalerProvider::Fsr;
        const bool wantsDlss = wantsTemporalUpscaler &&
            effectPlan.Upscaler == Oot3d::UpscalerProvider::Dlss;
        const bool wantsMotionVectors = effectPlan.Enabled(
            Oot3d::DisplayEffectPass::Motion);
        const bool wantsGlobalIllumination = effectPlan.Enabled(
            Oot3d::DisplayEffectPass::GlobalIllumination);
        NativePicaRenderTarget* effectGuideTarget = nullptr;
        if (copyBoundary) {
            // A boundary declares its own source: the target whose scene it
            // composes, not a presentation transfer's display mapping.
            if (copySource != nullptr &&
                copySource->DepthView != VK_NULL_HANDLE) {
                PrepareNativePicaTargetAttachments(*copySource);
                effectGuideTarget = copySource;
            }
        } else if (!effectPlan.Graph.Attachments.NativeColorOnly()) {
            const auto depthAssociation = mNativePicaDisplayDepthTargets.find(
                std::make_pair(transfer.RenderTargetNamespace,
                               transfer.OutputPhysicalAddress));
            if (depthAssociation != mNativePicaDisplayDepthTargets.end()) {
                const auto target =
                    mNativePicaRenderTargets.find(depthAssociation->second);
                if (target != mNativePicaRenderTargets.end() &&
                    target->second.DepthView != VK_NULL_HANDLE) {
                    PrepareNativePicaTargetAttachments(target->second);
                    effectGuideTarget = &target->second;
                }
            }
        }
        const Oot3d::NriEffectGraphTransientImageBinding*
            transientMotionOutput = nullptr;
        const Oot3d::NriEffectGraphTransientImageBinding*
            transientReactiveOutput = nullptr;
        const Oot3d::NriEffectGraphTransientImageBinding*
            transientAmbientOcclusionOutput = nullptr;
        const Oot3d::NriEffectGraphTransientImageBinding*
            transientHiZOutput = nullptr;
        const Oot3d::NriEffectGraphTransientImageBinding*
            transientLinearWorkingColor = nullptr;
        const Oot3d::NriEffectGraphTransientImageBinding*
            transientReflectionOutput = nullptr;
        const Oot3d::NriEffectGraphTransientImageBinding*
            transientDiffuseIllumination = nullptr;
        const Oot3d::NriEffectGraphTransientImageBinding*
            transientSpecularIllumination = nullptr;
        const Oot3d::NriEffectGraphTransientImageBinding*
            transientCompositeOutput = nullptr;
        const Oot3d::NriEffectGraphTransientImageBinding*
            transientAntiAliasedOutput = nullptr;
        const Oot3d::NriEffectGraphTransientImageBinding*
            transientUpscaledOutput = nullptr;
        const bool smaaReadsComposite = effectPlan.Enabled(
            Oot3d::DisplayEffectPass::Composite);
        const uint32_t plannedSmaaWidth =
            smaaReadsComposite && effectGuideTarget != nullptr
                ? effectGuideTarget->Width
                : display.Width;
        const uint32_t plannedSmaaHeight =
            smaaReadsComposite && effectGuideTarget != nullptr
                ? effectGuideTarget->Height
                : display.Height;
        // The working colour converts the scene colour at that colour's
        // extent, unless a scene stage traced on the guide target (SSSR)
        // reads it; a native display transfer may have resized the colour.
        const bool workingColorOnGuideTarget =
            effectGuideTarget != nullptr && wantsFidelityFxSssr;
        const uint32_t plannedWorkingColorWidth = workingColorOnGuideTarget
            ? effectGuideTarget->Width : display.Width;
        const uint32_t plannedWorkingColorHeight = workingColorOnGuideTarget
            ? effectGuideTarget->Height : display.Height;
        // An upscaler's render resolution is its colour input's extent: the
        // display (SceneColor), the working colour, or a composition
        // allocated on the scene target.
        const auto colorInput = [&effectPlan](Oot3d::DisplayEffectPass pass) {
            for (const auto& use : effectPlan.Bindings(pass)) {
                if (Oot3d::ReadsEffectResource(use.Access) &&
                    Oot3d::EffectResourceCarriesColor(use.Resource))
                    return use.Resource;
            }
            return Oot3d::EffectResource::SceneColor;
        };
        const auto upscalerColor = colorInput(wantsTemporalUpscaler
            ? Oot3d::DisplayEffectPass::TemporalUpscaler
            : Oot3d::DisplayEffectPass::Nis);
        // The display and its scene before native UI share the display's
        // extent; the working colour has its planned one; a composition on the
        // scene target has the target's.
        const bool upscalerReadsGuideDomain = effectGuideTarget != nullptr &&
            (wantsTemporalUpscaler || wantsNis) &&
            upscalerColor != Oot3d::EffectResource::SceneColor &&
            upscalerColor != Oot3d::EffectResource::SceneBeforeUi &&
            upscalerColor != Oot3d::EffectResource::LinearWorkingColor;
        const uint32_t plannedUpscalerInputWidth = upscalerReadsGuideDomain
            ? effectGuideTarget->Width
            : upscalerColor == Oot3d::EffectResource::LinearWorkingColor
                ? plannedWorkingColorWidth : display.Width;
        const uint32_t plannedUpscalerInputHeight = upscalerReadsGuideDomain
            ? effectGuideTarget->Height
            : upscalerColor == Oot3d::EffectResource::LinearWorkingColor
                ? plannedWorkingColorHeight : display.Height;
        const auto plannedUpscalerContract =
            Oot3d::ResolveUpscalerContractFromRender(
                effectPlan.Upscaler, presentationSettings.UpscalerMode,
                plannedUpscalerInputWidth, plannedUpscalerInputHeight);
        const uint32_t plannedUpscalerWidth =
            plannedUpscalerContract.Output.Width;
        const uint32_t plannedUpscalerHeight =
            plannedUpscalerContract.Output.Height;
        std::array<Oot3d::EffectTransientImageRequirement, 11U>
            requirements{};
        size_t requirementCount = 0U;
        const auto requireImageWithMips = [
            &requirements, &requirementCount](
                Oot3d::EffectResource resource,
                VkFormat format, uint32_t width, uint32_t height,
                uint32_t mipLevels) {
            requirements[requirementCount++] = {
                resource,
                static_cast<uint32_t>(format),
                width,
                height,
                mipLevels,
                1U,
                static_cast<uint32_t>(VK_SAMPLE_COUNT_1_BIT),
                static_cast<uint32_t>(
                    VK_IMAGE_USAGE_STORAGE_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT),
            };
        };
        const auto requireImage = [&requireImageWithMips](
            Oot3d::EffectResource resource, VkFormat format,
            uint32_t width, uint32_t height) {
            requireImageWithMips(resource, format, width, height, 1U);
        };
        const auto requireRgba16f = [&requireImage](
            Oot3d::EffectResource resource,
            uint32_t width, uint32_t height) {
            requireImage(resource, VK_FORMAT_R16G16B16A16_SFLOAT,
                         width, height);
        };
        if (wantsMotionVectors && effectGuideTarget != nullptr) {
            requireRgba16f(
                Oot3d::EffectResource::MotionVectors,
                effectGuideTarget->Width, effectGuideTarget->Height);
            requireImage(
                Oot3d::EffectResource::ReactiveMask,
                VK_FORMAT_R16_SFLOAT,
                effectGuideTarget->Width, effectGuideTarget->Height);
        }
        if (wantsAmbientOcclusion && effectGuideTarget != nullptr) {
            requireImage(
                Oot3d::EffectResource::AmbientOcclusion,
                VK_FORMAT_R32_SFLOAT,
                effectGuideTarget->Width, effectGuideTarget->Height);
        }
        if (wantsHiZ && effectGuideTarget != nullptr) {
            requireImageWithMips(
                Oot3d::EffectResource::HierarchicalDepth,
                VK_FORMAT_R32_SFLOAT,
                effectGuideTarget->Width, effectGuideTarget->Height,
                Oot3d::ResolveHiZMipCount(
                    effectGuideTarget->Width,
                    effectGuideTarget->Height));
        }
        if (wantsWorkingColor && effectGuideTarget != nullptr) {
            requireRgba16f(
                Oot3d::EffectResource::LinearWorkingColor,
                plannedWorkingColorWidth, plannedWorkingColorHeight);
        }
        if (wantsReflections && effectGuideTarget != nullptr) {
            requireRgba16f(
                Oot3d::EffectResource::ReflectionColor,
                effectGuideTarget->Width, effectGuideTarget->Height);
        }
        if (wantsGlobalIllumination && effectGuideTarget != nullptr) {
            requireRgba16f(
                Oot3d::EffectResource::DiffuseIllumination,
                effectGuideTarget->Width, effectGuideTarget->Height);
            if (effectPlan.ReflectionsFromGlobalIllumination ||
                effectPlan.ReflectionMissesFromGlobalIllumination)
                requireRgba16f(
                    Oot3d::EffectResource::SpecularIllumination,
                    effectGuideTarget->Width, effectGuideTarget->Height);
        }
        if (effectPlan.Enabled(Oot3d::DisplayEffectPass::Composite) &&
            effectGuideTarget != nullptr) {
            requireRgba16f(
                Oot3d::EffectResource::CompositeColor,
                effectGuideTarget->Width, effectGuideTarget->Height);
        }
        if (wantsSmaa) {
            requireRgba16f(
                Oot3d::EffectResource::AntiAliasedColor,
                plannedSmaaWidth, plannedSmaaHeight);
        }
        if (wantsTemporalUpscaler || wantsNis) {
            requireRgba16f(
                Oot3d::EffectResource::UpscaledColor,
                plannedUpscalerWidth, plannedUpscalerHeight);
        }
        // An inactive graph declares no new bindings; it does not retire the
        // resources used by an earlier native composition anchor. Each plan
        // that recurs (a native boundary and the presentations of one frame)
        // keeps its own images. Replacing a cached configuration destroys
        // images that recorded commands may still use, so DeviceWaitIdle alone
        // cannot authorize it: it first completes the recorded frame prefix
        // and releases the providers' descriptors.
        if (requirementCount != 0U) {
            const std::span<const Oot3d::EffectTransientImageRequirement>
                activeRequirements(requirements.data(), requirementCount);
            const auto allocationPlan =
                Oot3d::BuildEffectTransientAllocationPlan(
                    effectPlan.Graph, activeRequirements);
            if (mNriEffectGraphTransientImageArena.ReplacesConfigurationFor(
                    allocationPlan)) {
                if (!mNriInterop.CompleteRecordedWork())
                    throw std::runtime_error(
                        "effect graph image clients: recorded work did not complete");
                ReleaseEffectGraphImageClients();
            }
            const bool arenaConfigured =
                mNriEffectGraphTransientImageArena.Configure(allocationPlan);
            const auto& arenaStats =
                mNriEffectGraphTransientImageArena.Stats();
            mDiagnostics.RecordEffectTransientImageArena(
                allocationPlan.Summary(), arenaConfigured,
                arenaStats.LastAllocatedSlotCount,
                arenaStats.LastConfigurationReused);
            if (arenaConfigured) {
                transientMotionOutput =
                    mNriEffectGraphTransientImageArena.Find(
                        Oot3d::EffectResource::MotionVectors);
                transientReactiveOutput =
                    mNriEffectGraphTransientImageArena.Find(
                        Oot3d::EffectResource::ReactiveMask);
                transientAmbientOcclusionOutput =
                    mNriEffectGraphTransientImageArena.Find(
                        Oot3d::EffectResource::AmbientOcclusion);
                transientHiZOutput =
                    mNriEffectGraphTransientImageArena.Find(
                        Oot3d::EffectResource::HierarchicalDepth);
                transientLinearWorkingColor =
                    mNriEffectGraphTransientImageArena.Find(
                        Oot3d::EffectResource::LinearWorkingColor);
                transientReflectionOutput =
                    mNriEffectGraphTransientImageArena.Find(
                        Oot3d::EffectResource::ReflectionColor);
                transientDiffuseIllumination =
                    mNriEffectGraphTransientImageArena.Find(
                        Oot3d::EffectResource::DiffuseIllumination);
                transientSpecularIllumination =
                    effectPlan.ReflectionsFromGlobalIllumination ||
                            effectPlan.ReflectionMissesFromGlobalIllumination
                        ? mNriEffectGraphTransientImageArena.Find(
                              Oot3d::EffectResource::SpecularIllumination)
                        : nullptr;
                transientCompositeOutput =
                    mNriEffectGraphTransientImageArena.Find(
                        Oot3d::EffectResource::CompositeColor);
                transientAntiAliasedOutput =
                    mNriEffectGraphTransientImageArena.Find(
                        Oot3d::EffectResource::AntiAliasedColor);
                transientUpscaledOutput =
                    mNriEffectGraphTransientImageArena.Find(
                        Oot3d::EffectResource::UpscaledColor);
            }
        }
        bool guidesMaterialized = effectGuideTarget != nullptr;
        if (guidesMaterialized) {
            for (const auto& binding : effectPlan.Bindings(
                     Oot3d::DisplayEffectPass::Guides)) {
                if (!Oot3d::WritesEffectResource(binding.Access)) {
                    continue;
                }
                switch (binding.Resource) {
                    case Oot3d::EffectResource::NormalGuide:
                        guidesMaterialized =
                            effectGuideTarget->NormalGuideView !=
                            VK_NULL_HANDLE;
                        break;
                    case Oot3d::EffectResource::MaterialGuide:
                        guidesMaterialized =
                            effectGuideTarget->MaterialGuideView !=
                            VK_NULL_HANDLE;
                        break;
                    case Oot3d::EffectResource::RigidMotionGuide:
                        guidesMaterialized =
                            effectGuideTarget->RigidMotionGuideView !=
                            VK_NULL_HANDLE;
                        break;
                    case Oot3d::EffectResource::AmbientGuide:
                        guidesMaterialized =
                            effectGuideTarget->AmbientGuideView !=
                            VK_NULL_HANDLE;
                        break;
                    case Oot3d::EffectResource::FogGuide:
                        guidesMaterialized = effectGuideTarget->FogGuideView != VK_NULL_HANDLE;
                        break;
                    case Oot3d::EffectResource::OutlineGeometryGuide:
                        guidesMaterialized = effectGuideTarget->OutlineGeometryGuideView != VK_NULL_HANDLE;
                        break;
                    case Oot3d::EffectResource::DiffuseAlbedoGuide:
                        guidesMaterialized = effectGuideTarget->DiffuseAlbedoGuideView != VK_NULL_HANDLE;
                        break;
                    default:
                        guidesMaterialized = false;
                        break;
                }
                if (!guidesMaterialized) {
                    break;
                }
            }
        }
        Oot3d::DisplayEffectResourceTable effectResources;
        const auto bindEffectImage = [&effectResources, this](
            Oot3d::EffectResource resource, VkImage image,
            VkImageView view, VkFormat format, uint32_t width,
            uint32_t height,
            Oot3d::SceneColorEncoding colorEncoding =
                Oot3d::SceneColorEncoding::Unknown) {
            if (!effectResources.BindImage(
                    resource, reinterpret_cast<uintptr_t>(image),
                    reinterpret_cast<uintptr_t>(view),
                    static_cast<uint32_t>(format), width, height,
                    mFrameCounter, colorEncoding)) {
                throw std::runtime_error(
                    "display effect resource binding is incomplete");
            }
        };
        const auto bindEffectSemanticObject = [&effectResources](
            Oot3d::EffectResource resource, const void* object,
            uint32_t schemaVersion, uint64_t generation) {
            if (!effectResources.BindSemanticObject(
                    resource, object, schemaVersion, generation)) {
                throw std::runtime_error(
                    "display effect semantic object binding is incomplete");
            }
        };
        bindEffectImage(
            Oot3d::EffectResource::SceneColor,
            display.Image, display.View,
            VK_FORMAT_R8G8B8A8_UNORM,
            display.Width, display.Height,
            Oot3d::NativePicaSceneColorEncoding());
        if (sceneBeforeUi != nullptr) {
            bindEffectImage(
                Oot3d::EffectResource::SceneBeforeUi,
                sceneBeforeUi->Image, sceneBeforeUi->View,
                VK_FORMAT_R8G8B8A8_UNORM,
                sceneBeforeUi->Width, sceneBeforeUi->Height,
                Oot3d::NativePicaSceneColorEncoding());
        }
        bindEffectImage(
            Oot3d::EffectResource::PresentationOutput,
            mSwapchainImages[mCurrentImage],
            mSwapchainImageViews[mCurrentImage], mSwapchainFormat,
            mSwapchainExtent.width, mSwapchainExtent.height);
        if (mPicaSceneFrame.Active() &&
            mPicaSceneFrame.FrameId() != 0U) {
            bindEffectSemanticObject(
                Oot3d::EffectResource::PicaSceneFrame,
                &mPicaSceneFrame,
                Oot3d::PicaSceneFrame::kSchemaVersion,
                mPicaSceneFrame.FrameId());
        }
        if (mNativeSceneView.Active()) {
            bindEffectSemanticObject(
                Oot3d::EffectResource::NativeSceneView,
                &mNativeSceneView,
                Oot3d::NativeSceneView::kSchemaVersion,
                mNativeSceneView.Generation());
        }
        if (effectGuideTarget != nullptr) {
            bindEffectImage(
                Oot3d::EffectResource::NativeDepth,
                effectGuideTarget->DepthImage,
                effectGuideTarget->DepthView, mDepthFormat,
                effectGuideTarget->Width, effectGuideTarget->Height);
            const auto bindGuide = [&](Oot3d::EffectResource resource,
                                       VkImage image, VkImageView view,
                                       VkFormat format) {
                if (image != VK_NULL_HANDLE && view != VK_NULL_HANDLE) {
                    bindEffectImage(
                        resource, image, view, format,
                        effectGuideTarget->Width,
                        effectGuideTarget->Height);
                }
            };
            bindGuide(
                Oot3d::EffectResource::NormalGuide,
                effectGuideTarget->NormalGuideImage,
                effectGuideTarget->NormalGuideView,
                VK_FORMAT_R8G8B8A8_UNORM);
            bindGuide(
                Oot3d::EffectResource::MaterialGuide,
                effectGuideTarget->MaterialGuideImage,
                effectGuideTarget->MaterialGuideView,
                VK_FORMAT_R8G8B8A8_UNORM);
            bindGuide(
                Oot3d::EffectResource::RigidMotionGuide,
                effectGuideTarget->RigidMotionGuideImage,
                effectGuideTarget->RigidMotionGuideView,
                VK_FORMAT_R16G16B16A16_SFLOAT);
            bindGuide(
                Oot3d::EffectResource::AmbientGuide,
                effectGuideTarget->AmbientGuideImage,
                effectGuideTarget->AmbientGuideView,
                VK_FORMAT_R8G8B8A8_UNORM);
            bindGuide(Oot3d::EffectResource::FogGuide,
                effectGuideTarget->FogGuideImage, effectGuideTarget->FogGuideView,
                VK_FORMAT_R8G8B8A8_UNORM);
            bindGuide(Oot3d::EffectResource::OutlineGeometryGuide, effectGuideTarget->OutlineGeometryGuideImage,
                      effectGuideTarget->OutlineGeometryGuideView, VK_FORMAT_R32G32B32A32_UINT);
            bindGuide(Oot3d::EffectResource::DiffuseAlbedoGuide, effectGuideTarget->DiffuseAlbedoGuideImage,
                      effectGuideTarget->DiffuseAlbedoGuideView, VK_FORMAT_R16G16B16A16_UNORM);
        }
        const auto resolveEffectPassReads = [
            &effectPlan, &effectResources, this](
                Oot3d::DisplayEffectPass pass) {
            auto reads = effectResources.ResolveReads(
                effectPlan.Bindings(pass));
            mDiagnostics.RecordDisplayEffectGraphBindings(
                reads.Validation, false);
            if (!reads.Complete()) {
                throw std::runtime_error(
                    "display effect pass has unresolved declared reads: " +
                    std::string(Oot3d::DisplayEffectPassName(pass)));
            }
            return reads;
        };
        const auto requireEffectImageRead = [](
            const Oot3d::EffectResourceReadSet& reads,
            Oot3d::EffectResource resource)
                -> const Oot3d::EffectResourceBinding& {
            const auto* binding = reads.Find(resource);
            if (binding == nullptr ||
                binding->Kind !=
                    Oot3d::EffectResourceBindingKind::Image) {
                throw std::runtime_error(
                    "display effect pass used an undeclared or non-image read");
            }
            return *binding;
        };
        const auto findEffectColorRead = [&effectPlan](
            const Oot3d::EffectResourceReadSet& reads,
            Oot3d::DisplayEffectPass pass)
                -> const Oot3d::EffectResourceBinding* {
            return reads.FindFirstColorRead(effectPlan.Bindings(pass));
        };
        std::optional<Oot3d::TemporalViewState> temporalState;
        // A native boundary resolves the same temporal state the display that
        // presents this view will use: TemporalHistoryManager::Prepare is keyed
        // by view and frame, so it returns that frame's state instead of
        // advancing history a second time. A scene stage that needs motion
        // vectors (SSSR) therefore executes at the boundary with the frame's own
        // jitter, and the presentation keeps its history untouched.
        if (wantsTemporalMetadata && effectGuideTarget != nullptr) {
            Oot3d::TemporalViewInput temporalInput{};
            temporalInput.Key = {
                effectGuideTarget->Key.RenderTargetNamespace,
                effectGuideTarget->Key.ColorPhysicalAddress,
                effectGuideTarget->Key.DepthPhysicalAddress};
            temporalInput.RenderedFrameId = mFrameCounter;
            temporalInput.SourceResetEpoch =
                nativeTemporalSample.Available() ? nativeTemporalSample.ContinuityEpoch : 0U;
            temporalInput.Width = effectGuideTarget->Width;
            temporalInput.Height = effectGuideTarget->Height;
            temporalInput.Projection = presentationPerspective->Projection;
            temporalInput.WorldToClip =
                presentationPerspective->WorldToClip;
            temporalInput.Eye = presentationPerspective->Eye;
            temporalInput.At = presentationPerspective->At;
            temporalInput.JitterUv = {
                mTemporalJitterPixels[0] /
                    static_cast<float>(effectGuideTarget->Width),
                mTemporalJitterPixels[1] /
                    static_cast<float>(effectGuideTarget->Height)};
            temporalState = mTemporalHistory.Prepare(temporalInput);
            mDiagnostics.RecordTemporalHistory(
                temporalState->HistoryValid, temporalState->CameraCut,
                static_cast<uint32_t>(temporalState->ResetReason));
        }
        const bool outlineForDisplay =
            wantsOutline && effectGuideTarget != nullptr;
        bool reflectionsForDisplay =
            wantsReflections && effectGuideTarget != nullptr &&
            effectGuideTarget->NormalGuideView != VK_NULL_HANDLE &&
            effectGuideTarget->MaterialGuideView != VK_NULL_HANDLE;
        // GI-sourced reflections without GI for this sample: the declared
        // reflection holds no reflection and is not composed.
        bool reflectionsSkipped = false;
        bool reflectionNriWrapped = false;
        uint64_t guideSampledResourceMask = 0U;
        const auto transitionEffectPassGuides = [
            &effectPlan, &effectResources, &guideSampledResourceMask,
            &command, this](Oot3d::DisplayEffectPass pass) {
            const auto* compiledPass = effectPlan.FindPass(pass);
            if (compiledPass == nullptr) {
                return;
            }
            const auto guideBarriers =
                Oot3d::BuildPicaGuideSamplingBarriers(
                    *compiledPass, effectResources,
                    guideSampledResourceMask);
            if (!guideBarriers.Complete()) {
                throw std::runtime_error(
                    "display effect graph has unresolved PICA guide barriers");
            }
            guideSampledResourceMask |=
                guideBarriers.ResourceMask;
            mDiagnostics.RecordEffectGraphGuideImageBarriers(
                guideBarriers.Count);
            if (guideBarriers.Count != 0U) {
                vkCmdPipelineBarrier(
                    command, guideBarriers.SourceStages,
                    guideBarriers.DestinationStages,
                    0, 0, nullptr, 0,
                    nullptr,
                    guideBarriers.Count,
                    guideBarriers.Images.data());
            }
        };
        // Every guide this graph sampled returns to its attachment role. A
        // boundary that composes mid-frame must do this before it returns,
        // because the native draws that follow it write those same guides.
        const auto restoreEffectPassGuides = [
            &effectResources, &guideSampledResourceMask,
            &command, this]() {
            if (guideSampledResourceMask == 0U) {
                return;
            }
            const auto guideBarriers =
                Oot3d::BuildPicaGuideAttachmentBarriers(
                    effectResources, guideSampledResourceMask);
            if (!guideBarriers.Complete()) {
                throw std::runtime_error(
                    "display effect graph has unresolved PICA guide barriers");
            }
            mDiagnostics.RecordEffectGraphGuideImageBarriers(
                guideBarriers.Count);
            if (guideBarriers.Count != 0U) {
                vkCmdPipelineBarrier(
                    command, guideBarriers.SourceStages,
                    guideBarriers.DestinationStages,
                    0, 0, nullptr, 0, nullptr,
                    guideBarriers.Count,
                    guideBarriers.Images.data());
            }
        };
        // A boundary samples the source's own colour target, which the native
        // draw path leaves in its attachment layout: the scene stages read it as
        // a texture, and the draws that follow attach it again. Same barrier
        // pair the native encode path uses for this image. Only a plan that
        // declares a read of that colour is transitioned: a plan whose scene
        // stages are already resolved for this version declares none.
        const bool boundarySamplesSourceColor =
            copyBoundary && copySource != nullptr &&
            copySource->ColorImage != VK_NULL_HANDLE &&
            std::any_of(
                effectPlan.Graph.Passes.begin(),
                effectPlan.Graph.Passes.end(),
                [](const Oot3d::CompiledEffectPass& pass) {
                    return std::any_of(
                        pass.Resources.begin(), pass.Resources.end(),
                        [](const Oot3d::EffectResourceUse& use) {
                            return use.Resource ==
                                       Oot3d::EffectResource::SceneColor &&
                                   Oot3d::ReadsEffectResource(use.Access);
                        });
                });
        const auto setSourceColorLayout = [&command, this](
            VkImage image, VkImageLayout from, VkImageLayout to,
            VkPipelineStageFlags sourceStage,
            VkPipelineStageFlags destinationStage,
            VkAccessFlags sourceAccess, VkAccessFlags destinationAccess) {
            VkImageMemoryBarrier barrier{
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout = from;
            barrier.newLayout = to;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U};
            barrier.srcAccessMask = sourceAccess;
            barrier.dstAccessMask = destinationAccess;
            vkCmdPipelineBarrier(command, sourceStage, destinationStage, 0U,
                                 0U, nullptr, 0U, nullptr, 1U, &barrier);
        };
        if (boundarySamplesSourceColor) {
            setSourceColorLayout(
                copySource->ColorImage,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT);
        }
        const auto restoreBoundarySourceColor = [&]() {
            if (!boundarySamplesSourceColor) {
                return;
            }
            setSourceColorLayout(
                copySource->ColorImage,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        };
        if (presentationPerspective.has_value() && !mSceneDistanceFieldUpdatedThisFrame &&
            mBrixelizerScene.Available() &&
            Oot3d::PicaExtensionPassEnabled(
                mPicaExtensionGraph, Oot3d::PicaExtensionPass::SceneDistanceField)) {
            mSceneDistanceFieldUpdatedThisFrame = true;
            if (mTitleWorldChanged) {
                // Another world: the field and every GI history start over
                // (this frame's collection too; the next frame refills it).
                mTitleWorldChanged = false;
                mBrixelizerGiPass.ResetHistory();
                mBrixelizerScene.Reset();
            }
            // Developer inspection: OOT3D_GRAPHICS_BRIXELIZER_DEBUG=<directory>
            // writes the field's debug view every 150 updates, in the storage
            // orientation of the scene guides (OOT3D_GRAPHICS_BRIXELIZER_DEBUG_MODE,
            // FFX_BRIXELIZER_TRACE_DEBUG_MODE_*, default 3 = normals).
            std::optional<Oot3d::BrixelizerDebugView> debugView;
            if (const char* directory = std::getenv("OOT3D_GRAPHICS_BRIXELIZER_DEBUG");
                directory != nullptr && *directory != '\0' && transfer.InputWidth != 0U &&
                (mBrixelizerScene.Stats().Updates + 1U) % 150U == 0U) {
                const auto coordinates = Oot3d::PicaSurfaceCoordinates::FromTransferFlags(transfer.Flags);
                auto& view = debugView.emplace();
                view.Projection = coordinates.FidelityFxClip(presentationPerspective->Projection);
                view.WorldToClip = coordinates.FidelityFxClip(presentationPerspective->WorldToClip);
                // Native storage extent at 3x (scene guides may not exist).
                view.Width = transfer.InputWidth * 3U;
                view.Height = transfer.InputHeight * 3U;
                if (const char* mode = std::getenv("OOT3D_GRAPHICS_BRIXELIZER_DEBUG_MODE"); mode != nullptr)
                    view.Mode = static_cast<uint32_t>(std::strtoul(mode, nullptr, 10));
                view.Path = std::string(directory) + "/sdf-u" +
                    std::to_string(mBrixelizerScene.Stats().Updates + 1U) + "-m" + std::to_string(view.Mode) + ".ppm";
            }
            if (mBrixelizerScene.Update(command, mCurrentFrame, presentationPerspective->Eye,
                                        debugView ? &*debugView : nullptr)) {
                const auto& field = mBrixelizerScene.Stats();
                if (field.Updates % 150U == 1U) {
                    std::fprintf(stderr,
                        "TRIAEVUM_SCENE_DISTANCE_FIELD update=%llu collected=%llu rejected=%llu static=%u "
                        "created=%llu evicted=%llu dynamic=%u dynamic_triangles=%llu meshes=%u "
                        "arena_vertex_kib=%llu arena_index_kib=%llu scratch_mib=%llu free_bricks=%u "
                        "static_bricks=%u dynamic_bricks=%u peak_references=%u peak_triangles=%u "
                        "peak_bricks=%u overflows=%llu gpu_ms_mean=%.3f gpu_ms_max=%.3f\n",
                        (unsigned long long)field.Updates, (unsigned long long)field.CollectedInstances,
                        (unsigned long long)field.RejectedInstances, field.StaticInstances,
                        (unsigned long long)field.StaticInstancesCreated,
                        (unsigned long long)field.StaticInstancesEvicted, field.DynamicInstances,
                        (unsigned long long)field.DynamicTriangles, field.ResidentMeshes,
                        (unsigned long long)(field.ArenaVertexBytes >> 10),
                        (unsigned long long)(field.ArenaIndexBytes >> 10),
                        (unsigned long long)(field.GpuScratchBytes >> 20), field.FreeBricks,
                        field.StaticBricksAllocated, field.DynamicBricksAllocated, field.PeakReferences,
                        field.PeakTriangles, field.PeakBricks, (unsigned long long)field.BudgetOverflows,
                        field.GpuSamples != 0U ? field.GpuMillisecondsTotal / double(field.GpuSamples) : 0.0,
                        field.GpuMillisecondsMax);
                }
            } else if (!mBrixelizerScene.UnavailableReason().empty()) {
                std::fprintf(stderr, "TRIAEVUM_SCENE_DISTANCE_FIELD error=\"%s\"\n",
                             mBrixelizerScene.UnavailableReason().c_str());
            }
        }
        // Motion vectors precede the scene stages that read them: temporal AO
        // and GI reproject their history with this frame's motion.
        bool motionNriWrapped = false;
        std::optional<Oot3d::SceneSurfaceKey> motionKey;
        if (effectGuideTarget != nullptr) {
            motionKey = Oot3d::SceneSurfaceKey{
                transfer.RenderTargetNamespace,
                effectGuideTarget->Key.ColorPhysicalAddress,
                Oot3d::SceneSurfaceKind::Motion};
        }
        const bool canReuseMotion =
            wantsMotionVectors && mMotionExecutedThisFrame &&
            motionKey.has_value() && mMotionSurface.has_value() &&
            *mMotionSurface == *motionKey;
        bool motionForDisplay = wantsMotionVectors && temporalState.has_value() &&
                                effectGuideTarget != nullptr &&
                                !mMotionExecutedThisFrame;
        const bool motionAttempted = motionForDisplay;
        if (motionForDisplay) {
            const auto motionReads = resolveEffectPassReads(
                Oot3d::DisplayEffectPass::Motion);
            const auto& motionDepthBinding = requireEffectImageRead(
                motionReads,
                Oot3d::EffectResource::NativeDepth);
            const auto& motionMaterialBinding = requireEffectImageRead(
                motionReads,
                Oot3d::EffectResource::MaterialGuide);
            const auto& motionRigidBinding = requireEffectImageRead(
                motionReads,
                Oot3d::EffectResource::RigidMotionGuide);
            const VkImage motionDepthImage =
                reinterpret_cast<VkImage>(motionDepthBinding.NativeImage);
            const VkFormat motionDepthFormat =
                static_cast<VkFormat>(motionDepthBinding.Format);
            const VkImage motionMaterialImage =
                reinterpret_cast<VkImage>(motionMaterialBinding.NativeImage);
            const VkFormat motionMaterialFormat =
                static_cast<VkFormat>(motionMaterialBinding.Format);
            const VkImage motionRigidImage =
                reinterpret_cast<VkImage>(motionRigidBinding.NativeImage);
            const VkFormat motionRigidFormat =
                static_cast<VkFormat>(motionRigidBinding.Format);
            if (mMotionSurface.has_value() &&
                *mMotionSurface != *motionKey) {
                mSceneSurfaces.Retire(*mMotionSurface);
                mMotionSurface.reset();
            }
            const auto convention = effectGuideTarget->WBuffering
                ? Oot3d::DepthConvention::WBuffer
                : Oot3d::DepthConvention::Perspective;
            transitionEffectPassGuides(
                Oot3d::DisplayEffectPass::Motion);
            mGpuProfiler.BeginScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::MotionVectors,
                command);
            motionForDisplay =
                transientMotionOutput != nullptr &&
                transientReactiveOutput != nullptr &&
                mMotionVectorPass.Configure(
                    *transientMotionOutput, *transientReactiveOutput,
                    motionDepthImage, motionDepthFormat,
                    motionMaterialImage, motionMaterialFormat,
                    motionRigidImage, motionRigidFormat) &&
                mMotionVectorPass.Execute(command, mCurrentFrame,
                    *temporalState, *presentationPerspective, convention,
                    motionBarrierPlan,
                    Oot3d::PicaSurfaceCoordinates::FromTransferFlags(transfer.Flags));
            mGpuProfiler.EndScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::MotionVectors,
                command);
            if (motionForDisplay) {
                const VkImage motionImage = mMotionVectorPass.OutputImage();
                mSceneSurfaces.Publish({*motionKey,
                    reinterpret_cast<uintptr_t>(motionImage),
                    effectGuideTarget->Width, effectGuideTarget->Height,
                    static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT),
                    0, true});
                motionNriWrapped = mNriInterop.WrapTexture(
                    motionImage, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_TYPE_2D,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    effectGuideTarget->Width, effectGuideTarget->Height);
                mMotionSurface = *motionKey;
                mMotionExecutedThisFrame = true;
            }
            mDiagnostics.RecordMotionVectors(
                motionForDisplay, temporalState->HistoryValid,
                motionNriWrapped, mMotionVectorPass.OutputsOwnedByNri(),
                mMotionVectorPass.ComputeOwnedByNri(),
                mMotionVectorPass.BarriersOwnedByNri());
            if (motionForDisplay) {
                recordEffectPassTransitions(
                    mMotionVectorPass.LastBarrierExecution());
            }
        }
        const bool motionAvailableForDisplay =
            motionForDisplay || canReuseMotion;
        if (motionAvailableForDisplay) {
            bindEffectImage(
                Oot3d::EffectResource::MotionVectors,
                mMotionVectorPass.OutputImage(),
                mMotionVectorPass.OutputView(),
                VK_FORMAT_R16G16B16A16_SFLOAT,
                effectGuideTarget->Width, effectGuideTarget->Height);
            bindEffectImage(
                Oot3d::EffectResource::ReactiveMask,
                mMotionVectorPass.ReactiveImage(),
                mMotionVectorPass.ReactiveView(),
                VK_FORMAT_R16_SFLOAT,
                effectGuideTarget->Width, effectGuideTarget->Height);
        }
        const bool cacao = presentationSettings.Effects.AmbientOcclusion == Oot3d::AmbientOcclusionMode::Cacao;
        const bool mxao = presentationSettings.Effects.AmbientOcclusion == Oot3d::AmbientOcclusionMode::Mxao;
        bool aoAttempted = false;
        bool aoExecutedNow = false;
        // An AO that declares the motion vectors (temporal MXAO) runs with them.
        const auto aoBindings = effectPlan.Bindings(Oot3d::DisplayEffectPass::AmbientOcclusion);
        const bool aoReadsMotion = std::any_of(
            aoBindings.begin(), aoBindings.end(), [](const Oot3d::EffectResourceUse& use) {
                return Oot3d::ReadsEffectResource(use.Access) &&
                       use.Resource == Oot3d::EffectResource::MotionVectors;
            });
        if (wantsAmbientOcclusion && effectGuideTarget != nullptr &&
            !mAmbientOcclusionExecutedThisFrame &&
            (!aoReadsMotion || motionAvailableForDisplay)) {
            aoAttempted = true;
            mAmbientOcclusionExecutedThisFrame = true;
            const auto aoReads = resolveEffectPassReads(
                Oot3d::DisplayEffectPass::AmbientOcclusion);
            const auto& aoDepthBinding = requireEffectImageRead(
                aoReads,
                Oot3d::EffectResource::NativeDepth);
            const auto& aoNormalBinding = requireEffectImageRead(
                aoReads,
                Oot3d::EffectResource::NormalGuide);
            transitionEffectPassGuides(
                Oot3d::DisplayEffectPass::AmbientOcclusion);
            mGpuProfiler.BeginScope(
                mCurrentFrame, Oot3d::GpuProfileScope::AmbientOcclusion,
                command);
            mAmbientOcclusionOutputValid = transientAmbientOcclusionOutput != nullptr;
            if (mAmbientOcclusionOutputValid && cacao) {
                mAmbientOcclusionOutputValid =
                    mCacaoPass.Configure(*transientAmbientOcclusionOutput,
                        reinterpret_cast<VkImageView>(aoDepthBinding.NativeView),
                        reinterpret_cast<VkImageView>(aoNormalBinding.NativeView),
                        Oot3d::ResolveCacaoSettings(presentationSettings.Effects)) &&
                    mCacaoPass.Execute(command, presentationPerspective->Projection, aoBarrierPlan,
                        Oot3d::PicaSurfaceCoordinates::FromTransferFlags(transfer.Flags));
            } else if (mAmbientOcclusionOutputValid && mxao) {
                // Temporal MXAO reprojects its history with the declared
                // motion vectors of this frame.
                const auto* aoMotion = aoReads.Find(Oot3d::EffectResource::MotionVectors);
                const bool mxaoTemporal = aoMotion != nullptr &&
                    aoMotion->Kind == Oot3d::EffectResourceBindingKind::Image && temporalState.has_value();
                const Oot3d::MxaoMotionInput mxaoMotion = mxaoTemporal
                    ? Oot3d::MxaoMotionInput{reinterpret_cast<VkImage>(aoMotion->NativeImage),
                                             static_cast<VkFormat>(aoMotion->Format), aoMotion->Width,
                                             aoMotion->Height}
                    : Oot3d::MxaoMotionInput{};
                const Oot3d::MxaoTemporalFrame mxaoFrame{mxaoTemporal ? &*temporalState : nullptr, mFrameCounter};
                // Face normals come from the declared geometry guide; without
                // it the shader reads the cleared fallback (no face normal).
                const VkImage aoGeometry =
                    aoReads.Declares(Oot3d::EffectResource::OutlineGeometryGuide)
                        ? reinterpret_cast<VkImage>(
                              requireEffectImageRead(aoReads, Oot3d::EffectResource::OutlineGeometryGuide).NativeImage)
                        : mGeometryGuideFallback.Image;
                mAmbientOcclusionOutputValid =
                    mMxaoPass.Configure(*transientAmbientOcclusionOutput,
                        reinterpret_cast<VkImage>(aoDepthBinding.NativeImage),
                        static_cast<VkFormat>(aoDepthBinding.Format),
                        reinterpret_cast<VkImage>(aoNormalBinding.NativeImage),
                        static_cast<VkFormat>(aoNormalBinding.Format),
                        aoGeometry, mxaoTemporal ? &mxaoMotion : nullptr) &&
                    mMxaoPass.Execute(mCurrentFrame, mFrameCounter, *presentationPerspective,
                        effectGuideTarget->WBuffering, presentationSettings.Effects, aoBarrierPlan,
                        Oot3d::PicaSurfaceCoordinates::FromTransferFlags(transfer.Flags),
                        mxaoTemporal ? &mxaoFrame : nullptr);
                mDiagnostics.RecordMxaoTemporal(mxaoTemporal, mMxaoPass.LastHistoryValid());
            } else if (mAmbientOcclusionOutputValid) {
                mAmbientOcclusionOutputValid =
                    mAmbientOcclusionPass.Configure(*transientAmbientOcclusionOutput,
                        reinterpret_cast<VkImage>(aoDepthBinding.NativeImage),
                        static_cast<VkFormat>(aoDepthBinding.Format),
                        reinterpret_cast<VkImage>(aoNormalBinding.NativeImage),
                        static_cast<VkFormat>(aoNormalBinding.Format)) &&
                    mAmbientOcclusionPass.Execute(mCurrentFrame, *presentationPerspective,
                        effectGuideTarget->WBuffering, presentationSettings.Effects, aoBarrierPlan,
                        Oot3d::PicaSurfaceCoordinates::FromTransferFlags(transfer.Flags));
            }
            mGpuProfiler.EndScope(
                mCurrentFrame, Oot3d::GpuProfileScope::AmbientOcclusion,
                command);
            aoExecutedNow = mAmbientOcclusionOutputValid;
            if (mAmbientOcclusionOutputValid) {
                recordEffectPassTransitions(
                    cacao  ? mCacaoPass.LastBarrierExecution()
                    : mxao ? mMxaoPass.LastBarrierExecution()
                           : mAmbientOcclusionPass.LastBarrierExecution());
                mDiagnostics.RecordAmbientOcclusionPass(
                    cacao  ? mCacaoPass.OutputOwnedByNri()
                    : mxao ? mMxaoPass.OutputOwnedByNri()
                           : mAmbientOcclusionPass.OutputOwnedByNri(),
                    !cacao || mCacaoPass.UsesPicaNormalGuide());
                mAmbientOcclusionRenderTargetNamespace =
                    transfer.RenderTargetNamespace;
                mAmbientOcclusionDisplayPhysicalAddress =
                    transfer.OutputPhysicalAddress;
            }
        }
        const bool aoForDisplay =
            mAmbientOcclusionOutputValid && wantsAmbientOcclusion &&
            mAmbientOcclusionRenderTargetNamespace == transfer.RenderTargetNamespace &&
            mAmbientOcclusionDisplayPhysicalAddress == transfer.OutputPhysicalAddress;
        if (aoForDisplay) {
            bindEffectImage(
                Oot3d::EffectResource::AmbientOcclusion,
                cacao  ? mCacaoPass.OutputImage()
                : mxao ? mMxaoPass.OutputImage()
                       : mAmbientOcclusionPass.OutputImage(),
                cacao  ? mCacaoPass.OutputView()
                : mxao ? mMxaoPass.OutputView()
                       : mAmbientOcclusionPass.OutputView(),
                VK_FORMAT_R32_SFLOAT, effectGuideTarget->Width,
                effectGuideTarget->Height);
        }
        // Diffuse GI: traced where the field, the motion vectors and a
        // perspective depth exist; otherwise the declared output is zero
        // irradiance, so the composite adds nothing and the graph stays whole.
        bool giForDisplay = false;
        // Screen-space GI: gathered from this surface's declared guides where
        // the motion vectors and the temporal view exist; otherwise the same
        // neutral output. One pass instance (and history) per surface.
        if (wantsGlobalIllumination && screenSpaceGiRequested && effectGuideTarget != nullptr &&
            transientDiffuseIllumination != nullptr) {
            const bool giAttempted =
                motionAvailableForDisplay && temporalState.has_value() && presentationPerspective.has_value();
            Oot3d::ScreenSpaceGiFrame giFrame;
            giFrame.FrameId = mFrameCounter;
            giFrame.FrameSlot = mCurrentFrame;
            giFrame.Surface = {effectGuideTarget->Key.RenderTargetNamespace,
                               effectGuideTarget->Key.ColorPhysicalAddress,
                               effectGuideTarget->Key.DepthPhysicalAddress};
            giFrame.Width = effectGuideTarget->Width;
            giFrame.Height = effectGuideTarget->Height;
            giFrame.WBuffer = effectGuideTarget->WBuffering;
            giFrame.Coordinates = Oot3d::PicaSurfaceCoordinates::FromTransferFlags(transfer.Flags);
            const auto& giSettings = presentationSettings.Effects.GlobalIllumination;
            if (giAttempted) {
                giFrame.View = &*presentationPerspective;
                giFrame.Temporal = &*temporalState;
                giFrame.JitterUv = temporalState->CurrentJitterUv;
                const auto giReads = resolveEffectPassReads(Oot3d::DisplayEffectPass::GlobalIllumination);
                const auto& depth = requireEffectImageRead(giReads, Oot3d::EffectResource::NativeDepth);
                const auto view = [&](Oot3d::EffectResource resource) {
                    return reinterpret_cast<VkImageView>(requireEffectImageRead(giReads, resource).NativeView);
                };
                giFrame.Guides.Depth = mScreenSpaceGi.SampledDepthView(
                    reinterpret_cast<VkImage>(depth.NativeImage), static_cast<VkFormat>(depth.Format),
                    depth.Width, depth.Height);
                giFrame.Guides.Geometry = view(Oot3d::EffectResource::OutlineGeometryGuide);
                giFrame.Guides.Surface = view(Oot3d::EffectResource::DiffuseAlbedoGuide);
                giFrame.Guides.Motion = view(Oot3d::EffectResource::MotionVectors);
                giFrame.Guides.Normal = view(Oot3d::EffectResource::NormalGuide);
                transitionEffectPassGuides(Oot3d::DisplayEffectPass::GlobalIllumination);
                // Another world, or a source switched off or returning: the
                // histories start over (the same rules as the traced GI).
                if (mTitleWorldIdentity != mScreenSpaceGiWorldIdentity ||
                    Oot3d::RestartsGlobalIlluminationHistory(mScreenSpaceGiSourceWeights, giSources.Weights))
                    mScreenSpaceGi.ResetHistory();
                mScreenSpaceGiWorldIdentity = mTitleWorldIdentity;
                mScreenSpaceGiSourceWeights = giSources.Weights;
                mGpuProfiler.BeginScope(mCurrentFrame, Oot3d::GpuProfileScope::GlobalIllumination, command);
                giForDisplay = mScreenSpaceGi.Execute(command, giFrame, giSettings, *transientDiffuseIllumination,
                                                      globalIlluminationBarrierPlan);
                mGpuProfiler.EndScope(mCurrentFrame, Oot3d::GpuProfileScope::GlobalIllumination, command);
            }
            if (!giForDisplay) {
                static uint64_t screenSpaceGiFallbacks = 0;
                if (screenSpaceGiFallbacks++ % 300U == 0U)
                    std::fprintf(stderr, "TRIAEVUM_SCREEN_SPACE_GI fallback=%llu reason=\"%s\"\n",
                                 (unsigned long long)screenSpaceGiFallbacks,
                                 !giAttempted ? "motion vectors or temporal view unavailable"
                                              : mScreenSpaceGi.UnavailableReason().c_str());
                if (!mScreenSpaceGi.ClearOutput(command, giFrame, giSettings, *transientDiffuseIllumination,
                                                globalIlluminationBarrierPlan))
                    throw std::runtime_error("screen-space global illumination output could not be cleared: " +
                                             mScreenSpaceGi.UnavailableReason());
            }
            if (const auto& gi = mScreenSpaceGi.Stats();
                gi.InstancesCreated != mReportedScreenSpaceGi.InstancesCreated ||
                gi.Reconfigurations != mReportedScreenSpaceGi.Reconfigurations ||
                gi.HistoryResets != mReportedScreenSpaceGi.HistoryResets) {
                mReportedScreenSpaceGi = gi;
                std::fprintf(stderr,
                             "TRIAEVUM_SCREEN_SPACE_GI frame=%llu surface=%ux%u factor=%u instances=%u "
                             "buffer_bytes=%llu executions=%llu republications=%llu clears=%llu "
                             "reconfigurations=%llu history_resets=%llu reset_frames=%llu last_reset=0x%x allocation_ms=%.1f\n",
                             (unsigned long long)mFrameCounter, giFrame.Width, giFrame.Height,
                             Oot3d::ScreenSpaceGiWorkFactor(giSettings.ScreenSpace.WorkScale), gi.Instances,
                             (unsigned long long)gi.BufferBytes, (unsigned long long)gi.Executions,
                             (unsigned long long)gi.Republications, (unsigned long long)gi.Clears,
                             (unsigned long long)gi.Reconfigurations, (unsigned long long)gi.HistoryResets,
                             (unsigned long long)gi.ResetFrames, gi.LastResetReasons, gi.AllocationMilliseconds);
            }
            recordEffectPassTransitions(mScreenSpaceGi.LastBarrierExecution());
            recordDeclared(Oot3d::DisplayEffectPass::GlobalIllumination,
                           giForDisplay ? EffectOutcome::Executed : EffectOutcome::Skipped);
            bindEffectImage(Oot3d::EffectResource::DiffuseIllumination,
                            transientDiffuseIllumination->Texture.Image,
                            transientDiffuseIllumination->Texture.SampledView,
                            VK_FORMAT_R16G16B16A16_SFLOAT, effectGuideTarget->Width,
                            effectGuideTarget->Height);
        } else if (wantsGlobalIllumination && effectGuideTarget != nullptr &&
            transientDiffuseIllumination != nullptr &&
            ((!effectPlan.ReflectionsFromGlobalIllumination &&
          !effectPlan.ReflectionMissesFromGlobalIllumination) ||
         transientSpecularIllumination != nullptr)) {
            const bool giAttempted = motionAvailableForDisplay && temporalState.has_value() &&
                                     !effectGuideTarget->WBuffering;
            const auto giCoordinates = Oot3d::PicaSurfaceCoordinates::FromTransferFlags(transfer.Flags);
            if (giAttempted) {
                const auto giReads = resolveEffectPassReads(Oot3d::DisplayEffectPass::GlobalIllumination);
                const auto image = [&](Oot3d::EffectResource resource) {
                    const auto& binding = requireEffectImageRead(giReads, resource);
                    return std::pair{reinterpret_cast<VkImage>(binding.NativeImage),
                                     static_cast<VkFormat>(binding.Format)};
                };
                Oot3d::BrixelizerGiInputs giInputs{};
                giInputs.Width = effectGuideTarget->Width;
                giInputs.Height = effectGuideTarget->Height;
                std::tie(giInputs.SurfaceGuide, giInputs.SurfaceGuideFormat) =
                    image(Oot3d::EffectResource::DiffuseAlbedoGuide);
                std::tie(giInputs.Depth, giInputs.DepthFormat) = image(Oot3d::EffectResource::NativeDepth);
                std::tie(giInputs.NormalGuide, giInputs.NormalGuideFormat) = image(Oot3d::EffectResource::NormalGuide);
                std::tie(giInputs.MaterialGuide, giInputs.MaterialGuideFormat) =
                    image(Oot3d::EffectResource::MaterialGuide);
                std::tie(giInputs.Motion, giInputs.MotionFormat) = image(Oot3d::EffectResource::MotionVectors);
                transitionEffectPassGuides(Oot3d::DisplayEffectPass::GlobalIllumination);
                // Sky light: rays leaving the scene receive the native sky
                // colour (linear) at its source weight.
                std::array<float, 3> giSky{};
                const auto sky = effectGuideTarget->ReflectionEnvironment.Resolve(mFrameCounter).Sky;
                for (size_t channel = 0; channel < giSky.size(); ++channel)
                    giSky[channel] = giSources.Weights[3] * std::pow(std::clamp(sky[channel], 0.0F, 1.0F), 2.2F);
                mGpuProfiler.BeginScope(mCurrentFrame, Oot3d::GpuProfileScope::GlobalIllumination, command);
                giForDisplay =
                    mBrixelizerGiPass.Configure(mFrameCounter, giInputs,
                                                presentationSettings.Effects.GlobalIllumination.Resolution,
                                                transientSpecularIllumination != nullptr, giSources.Weights) &&
                    mBrixelizerGiPass.Execute(command, mCurrentFrame, mFrameCounter, *temporalState,
                                              *presentationPerspective, giCoordinates,
                                              *transientDiffuseIllumination, transientSpecularIllumination,
                                              giSky, globalIlluminationBarrierPlan);
                mGpuProfiler.EndScope(mCurrentFrame, Oot3d::GpuProfileScope::GlobalIllumination, command);
            }
            if (!giForDisplay) {
                static uint64_t giFallbacks = 0;
                if (giFallbacks++ % 300U == 0U)
                    std::fprintf(stderr, "TRIAEVUM_GLOBAL_ILLUMINATION fallback=%llu reason=\"%s\"\n",
                                 (unsigned long long)giFallbacks,
                                 !giAttempted ? (effectGuideTarget->WBuffering ? "W-buffer depth"
                                                                               : "motion vectors unavailable")
                                              : mBrixelizerGiPass.UnavailableReason().c_str());
                if (!mBrixelizerGiPass.ClearOutput(mCurrentFrame, *transientDiffuseIllumination,
                                                   transientSpecularIllumination, globalIlluminationBarrierPlan))
                    throw std::runtime_error("global illumination output could not be cleared");
            }
            if (const auto& gi = mBrixelizerGiPass.Stats();
                gi.InstancesCreated != mReportedGiInstances || gi.WorldResets != mReportedGiResets ||
                gi.CreationFailures != mReportedGiCreationFailures) {
                mReportedGiInstances = gi.InstancesCreated;
                mReportedGiResets = gi.WorldResets;
                mReportedGiCreationFailures = gi.CreationFailures;
                std::fprintf(stderr,
                             "TRIAEVUM_GLOBAL_ILLUMINATION frame=%llu instances_created=%llu retired=%llu "
                             "world_resets=%llu source_resets=%llu creation_failures=%llu\n",
                             (unsigned long long)mFrameCounter, (unsigned long long)gi.InstancesCreated,
                             (unsigned long long)gi.InstancesRetired, (unsigned long long)gi.WorldResets,
                             (unsigned long long)gi.SourceResets, (unsigned long long)gi.CreationFailures);
            }
            recordEffectPassTransitions(mBrixelizerGiPass.LastBarrierExecution());
            recordDeclared(Oot3d::DisplayEffectPass::GlobalIllumination,
                           giForDisplay ? EffectOutcome::Executed : EffectOutcome::Skipped);
            bindEffectImage(Oot3d::EffectResource::DiffuseIllumination,
                            transientDiffuseIllumination->Texture.Image,
                            transientDiffuseIllumination->Texture.SampledView,
                            VK_FORMAT_R16G16B16A16_SFLOAT, effectGuideTarget->Width,
                            effectGuideTarget->Height);
            if (transientSpecularIllumination != nullptr)
                bindEffectImage(Oot3d::EffectResource::SpecularIllumination,
                                transientSpecularIllumination->Texture.Image,
                                transientSpecularIllumination->Texture.SampledView,
                                VK_FORMAT_R16G16B16A16_SFLOAT, effectGuideTarget->Width,
                                effectGuideTarget->Height);
        }
        const bool giComposed = wantsGlobalIllumination && effectGuideTarget != nullptr &&
                                transientDiffuseIllumination != nullptr;
        bool linearDepthForFsr = wantsFsr && effectGuideTarget != nullptr;
        const bool giReflections = effectPlan.ReflectionsFromGlobalIllumination;
        const bool hiZRequested =
            (reflectionsForDisplay && !giReflections) || linearDepthForFsr;
        const bool canReuseHiZ =
            hiZRequested && mHiZExecutedThisFrame && mHiZOutputValid &&
            mHiZRenderTargetNamespace == transfer.RenderTargetNamespace &&
            mHiZDisplayPhysicalAddress == transfer.OutputPhysicalAddress;
        const bool hiZAttempted = hiZRequested && !canReuseHiZ;
        bool hiZExecutedNow = false;
        if (hiZAttempted) {
            const auto hiZReads = resolveEffectPassReads(
                Oot3d::DisplayEffectPass::HiZ);
            const auto& hiZDepthBinding = requireEffectImageRead(
                hiZReads,
                Oot3d::EffectResource::NativeDepth);
            const VkImage hiZDepthImage =
                reinterpret_cast<VkImage>(hiZDepthBinding.NativeImage);
            const VkFormat hiZDepthFormat =
                static_cast<VkFormat>(hiZDepthBinding.Format);
            transitionEffectPassGuides(
                Oot3d::DisplayEffectPass::HiZ);
            mGpuProfiler.BeginScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::DepthPreparation,
                command);
            mHiZOutputValid = transientHiZOutput != nullptr &&
                mHiZDepthPyramidPass.Configure(
                    *transientHiZOutput,
                    hiZDepthImage, hiZDepthFormat) &&
                mHiZDepthPyramidPass.Execute(
                    command, mCurrentFrame,
                    presentationPerspective->NearPlane,
                    presentationPerspective->FarPlane,
                    effectGuideTarget->WBuffering
                        ? Oot3d::DepthConvention::WBuffer
                        : Oot3d::DepthConvention::Perspective,
                    hiZBarrierPlan);
            mGpuProfiler.EndScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::DepthPreparation,
                command);
            if (mHiZOutputValid) {
                hiZExecutedNow = true;
                mHiZRenderTargetNamespace = transfer.RenderTargetNamespace;
                mHiZDisplayPhysicalAddress = transfer.OutputPhysicalAddress;
                mHiZExecutedThisFrame = true;
                mDiagnostics.RecordHiZPass(
                    mHiZDepthPyramidPass.MipCount(),
                    effectGuideTarget->WBuffering
                        ? static_cast<uint32_t>(Oot3d::DepthConvention::WBuffer)
                        : static_cast<uint32_t>(
                              Oot3d::DepthConvention::Perspective),
                    mHiZDepthPyramidPass.OutputOwnedByNri(),
                    mHiZDepthPyramidPass.ComputeOwnedByNri(),
                    mHiZDepthPyramidPass.BarriersOwnedByNri());
                recordEffectPassTransitions(
                    mHiZDepthPyramidPass.LastBarrierExecution());
            }
        }
        const bool hiZForDisplay =
            mHiZOutputValid && effectGuideTarget != nullptr &&
            mHiZRenderTargetNamespace == transfer.RenderTargetNamespace &&
            mHiZDisplayPhysicalAddress == transfer.OutputPhysicalAddress;
        reflectionsForDisplay = reflectionsForDisplay && (giReflections || hiZForDisplay);
        linearDepthForFsr = linearDepthForFsr && hiZForDisplay;
        if (hiZForDisplay) {
            bindEffectImage(
                Oot3d::EffectResource::HierarchicalDepth,
                mHiZDepthPyramidPass.OutputImage(),
                mHiZDepthPyramidPass.OutputView(),
                VK_FORMAT_R32_SFLOAT, effectGuideTarget->Width,
                effectGuideTarget->Height);
        }
        std::optional<Oot3d::SceneSurfaceKey> linearKey;
        if (effectGuideTarget != nullptr) {
            linearKey = Oot3d::SceneSurfaceKey{
                transfer.RenderTargetNamespace,
                effectGuideTarget->Key.ColorPhysicalAddress,
                copyBoundary
                    ? Oot3d::SceneSurfaceKind::BoundaryWorkingColor
                    : Oot3d::SceneSurfaceKind::LinearWorkingColor};
        }
        const bool canReuseWorkingColor =
            wantsWorkingColor && mLinearColorExecutedThisFrame &&
            linearKey.has_value() && mLinearColorSurface.has_value() &&
            *mLinearColorSurface == *linearKey &&
            mLinearSceneColorPass.OutputImage() != VK_NULL_HANDLE &&
            mLinearSceneColorPass.OutputView() != VK_NULL_HANDLE;
        const bool workingColorAttempted =
            wantsWorkingColor && effectGuideTarget != nullptr &&
            !canReuseWorkingColor;
        bool linearWorkingColorForDisplay = canReuseWorkingColor;
        if (workingColorAttempted) {
            const auto workingColorReads = resolveEffectPassReads(
                Oot3d::DisplayEffectPass::WorkingColor);
            const auto* workingColorSourceRead = findEffectColorRead(
                workingColorReads, Oot3d::DisplayEffectPass::WorkingColor);
            if (workingColorSourceRead == nullptr ||
                workingColorSourceRead->Kind !=
                    Oot3d::EffectResourceBindingKind::Image) {
                throw std::runtime_error(
                    "working colour pass has no declared color input");
            }
            const auto& workingColorSourceBinding = *workingColorSourceRead;
            const VkImage workingColorSourceImage =
                reinterpret_cast<VkImage>(
                    workingColorSourceBinding.NativeImage);
            const VkFormat workingColorSourceFormat =
                static_cast<VkFormat>(workingColorSourceBinding.Format);
            const uint32_t workingColorSourceWidth =
                workingColorSourceBinding.Width;
            const uint32_t workingColorSourceHeight =
                workingColorSourceBinding.Height;
            const auto linearColorPolicy =
                Oot3d::BuildLinearSceneColorPolicy(
                    workingColorSourceBinding.ColorEncoding);
            linearWorkingColorForDisplay =
                linearColorPolicy.Valid &&
                transientLinearWorkingColor != nullptr &&
                mLinearSceneColorPass.Configure(
                    *transientLinearWorkingColor) &&
                mLinearSceneColorPass.Execute(
                    command, mCurrentFrame,
                    workingColorSourceImage,
                    workingColorSourceFormat,
                    workingColorSourceWidth,
                    workingColorSourceHeight,
                    linearColorPolicy.Source,
                    workingColorBarrierPlan);
            if (linearWorkingColorForDisplay) {
                recordEffectPassTransitions(
                    mLinearSceneColorPass.LastBarrierExecution());
                if (mLinearColorSurface.has_value() &&
                    *mLinearColorSurface != *linearKey) {
                    mSceneSurfaces.Retire(*mLinearColorSurface);
                    mLinearColorSurface.reset();
                }
                mSceneSurfaces.Publish({
                    *linearKey,
                    reinterpret_cast<uintptr_t>(
                        mLinearSceneColorPass.OutputImage()),
                    mLinearSceneColorPass.Width(),
                    mLinearSceneColorPass.Height(),
                    static_cast<uint32_t>(
                        VK_FORMAT_R16G16B16A16_SFLOAT),
                    0, true, Oot3d::SceneColorEncoding::Linear});
                mLinearColorSurface = *linearKey;
                mLinearColorExecutedThisFrame = true;
                mDiagnostics.RecordLinearWorkingColor(
                    linearColorPolicy.DecodeSrgb,
                    mLinearSceneColorPass.OutputOwnedByNri(),
                    mLinearSceneColorPass.ComputeOwnedByNri(),
                    mLinearSceneColorPass.BarriersOwnedByNri());
            }
        }
        if (linearWorkingColorForDisplay) {
            bindEffectImage(
                Oot3d::EffectResource::LinearWorkingColor,
                mLinearSceneColorPass.OutputImage(),
                mLinearSceneColorPass.OutputView(),
                VK_FORMAT_R16G16B16A16_SFLOAT,
                mLinearSceneColorPass.Width(),
                mLinearSceneColorPass.Height(),
                Oot3d::SceneColorEncoding::Linear);
        }
        VkImage activeReflectionImage = VK_NULL_HANDLE;
        VkImageView activeReflectionView = VK_NULL_HANDLE;
        Oot3d::SceneColorEncoding reflectionOutputEncoding =
            Oot3d::SceneColorEncoding::Unknown;
        Oot3d::ReflectionProvider activeReflectionProvider =
            Oot3d::ReflectionProvider::Off;
        std::optional<Oot3d::SceneSurfaceKey> reflectionKey;
        if (effectGuideTarget != nullptr) {
            reflectionKey = Oot3d::SceneSurfaceKey{
                transfer.RenderTargetNamespace,
                transfer.OutputPhysicalAddress,
                Oot3d::SceneSurfaceKind::Reflection};
        }
        const bool reflectionRequestedForDisplay =
            reflectionsForDisplay;
        const bool canReuseReflections =
            reflectionRequestedForDisplay &&
            mReflectionExecutedThisFrame && reflectionKey.has_value() &&
            mHiZReflectionSurface.has_value() &&
            *mHiZReflectionSurface == *reflectionKey;
        const bool reflectionsAttempted =
            reflectionRequestedForDisplay && !canReuseReflections;
        if (canReuseReflections) {
            activeReflectionProvider = mReflectionProviderThisFrame;
            reflectionOutputEncoding =
                mReflectionOutputEncodingThisFrame;
            if (activeReflectionProvider ==
                    Oot3d::ReflectionProvider::FidelityFxSssr ||
                activeReflectionProvider ==
                    Oot3d::ReflectionProvider::GlobalIllumination) {
                activeReflectionImage =
                    mReflectionMaterialResolvePass.OutputImage();
                activeReflectionView =
                    mReflectionMaterialResolvePass.OutputView();
            } else if (activeReflectionProvider ==
                           Oot3d::ReflectionProvider::HiZ) {
                activeReflectionImage =
                    mHiZReflectionPass.OutputImage();
                activeReflectionView =
                    mHiZReflectionPass.OutputView();
            }
            reflectionsForDisplay =
                activeReflectionProvider !=
                    Oot3d::ReflectionProvider::Off &&
                activeReflectionImage != VK_NULL_HANDLE &&
                activeReflectionView != VK_NULL_HANDLE;
        } else if (reflectionsAttempted && giReflections) {
            // GI specular is the reflection radiance: the material resolve
            // weights it by reflectivity, gain and the split-sum BRDF exactly
            // as it weights a screen-space trace.
            if (mHiZReflectionSurface.has_value() &&
                *mHiZReflectionSurface != *reflectionKey) {
                mSceneSurfaces.Retire(*mHiZReflectionSurface);
                mHiZReflectionSurface.reset();
            }
            const auto reflectionReads = resolveEffectPassReads(
                Oot3d::DisplayEffectPass::Reflections);
            const auto& normalBinding = requireEffectImageRead(
                reflectionReads, Oot3d::EffectResource::NormalGuide);
            const auto& materialBinding = requireEffectImageRead(
                reflectionReads, Oot3d::EffectResource::MaterialGuide);
            const auto& specularBinding = requireEffectImageRead(
                reflectionReads, Oot3d::EffectResource::SpecularIllumination);
            transitionEffectPassGuides(Oot3d::DisplayEffectPass::Reflections);
            mGpuProfiler.BeginScope(mCurrentFrame, Oot3d::GpuProfileScope::Reflection, command);
            const bool iblExecuted =
                giForDisplay && mReflectionIblPass.Available() &&
                mReflectionIblPass.Execute(
                    mCurrentFrame,
                    effectGuideTarget->ReflectionEnvironment.Resolve(mFrameCounter));
            if (iblExecuted)
                recordEffectPassTransitions(mReflectionIblPass.LastBarrierExecution());
            const bool resolved =
                iblExecuted && transientReflectionOutput != nullptr &&
                mReflectionMaterialResolvePass.Configure(*transientReflectionOutput) &&
                mReflectionMaterialResolvePass.Execute(
                    mCurrentFrame,
                    reinterpret_cast<VkImage>(specularBinding.NativeImage),
                    static_cast<VkFormat>(specularBinding.Format),
                    reinterpret_cast<VkImage>(normalBinding.NativeImage),
                    static_cast<VkFormat>(normalBinding.Format),
                    reinterpret_cast<VkImage>(materialBinding.NativeImage),
                    static_cast<VkFormat>(materialBinding.Format),
                    mReflectionIblPass.BrdfImage(), VK_FORMAT_R16G16_SFLOAT,
                    *presentationPerspective, 0.0F, reflectionBarrierPlan,
                    Oot3d::PicaSurfaceCoordinates::FromTransferFlags(transfer.Flags));
            if (resolved) {
                recordEffectPassTransitions(mReflectionMaterialResolvePass.LastBarrierExecution());
                activeReflectionProvider = Oot3d::ReflectionProvider::GlobalIllumination;
                activeReflectionImage = mReflectionMaterialResolvePass.OutputImage();
                activeReflectionView = mReflectionMaterialResolvePass.OutputView();
                reflectionOutputEncoding = Oot3d::SceneColorEncoding::Linear;
            } else if (!giForDisplay && transientReflectionOutput != nullptr &&
                       mReflectionMaterialResolvePass.Configure(*transientReflectionOutput) &&
                       mReflectionMaterialResolvePass.ClearOutput(mCurrentFrame, reflectionBarrierPlan)) {
                // Without GI for this sample nothing is composed: resolving the
                // cleared specular image would darken every reflective surface.
                // The declared reflection still exists, as no reflection (zero
                // weight) like the GI pass's zero irradiance, so its readers
                // resolve and the graph stays complete.
                recordEffectPassTransitions(mReflectionMaterialResolvePass.LastBarrierExecution());
                activeReflectionImage = mReflectionMaterialResolvePass.OutputImage();
                activeReflectionView = mReflectionMaterialResolvePass.OutputView();
                reflectionOutputEncoding = Oot3d::SceneColorEncoding::Linear;
                reflectionsSkipped = true;
            }
            reflectionsForDisplay = resolved;
            mGpuProfiler.EndScope(mCurrentFrame, Oot3d::GpuProfileScope::Reflection, command);
            if (reflectionsForDisplay) {
                mSceneSurfaces.Publish({*reflectionKey,
                    reinterpret_cast<uintptr_t>(activeReflectionImage),
                    effectGuideTarget->Width, effectGuideTarget->Height,
                    static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT),
                    0, true, reflectionOutputEncoding});
                reflectionNriWrapped = mNriInterop.WrapTexture(
                    activeReflectionImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TYPE_2D,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    effectGuideTarget->Width, effectGuideTarget->Height);
                mHiZReflectionSurface = *reflectionKey;
                mReflectionExecutedThisFrame = true;
                mReflectionProviderThisFrame = activeReflectionProvider;
                mReflectionOutputEncodingThisFrame = reflectionOutputEncoding;
            }
        } else if (reflectionsAttempted) {
            if (mHiZReflectionSurface.has_value() &&
                *mHiZReflectionSurface != *reflectionKey) {
                mSceneSurfaces.Retire(*mHiZReflectionSurface);
                mHiZReflectionSurface.reset();
            }
            const auto reflectionReads = resolveEffectPassReads(
                Oot3d::DisplayEffectPass::Reflections);
            const auto* reflectionSceneBinding = findEffectColorRead(
                reflectionReads,
                Oot3d::DisplayEffectPass::Reflections);
            if (reflectionSceneBinding == nullptr ||
                reflectionSceneBinding->Kind !=
                    Oot3d::EffectResourceBindingKind::Image) {
                throw std::runtime_error(
                    "reflection pass has no declared color input");
            }
            const auto& reflectionNormalBinding = requireEffectImageRead(
                reflectionReads,
                Oot3d::EffectResource::NormalGuide);
            const auto& reflectionMaterialBinding = requireEffectImageRead(
                reflectionReads,
                Oot3d::EffectResource::MaterialGuide);
            const auto& reflectionHiZBinding = requireEffectImageRead(
                reflectionReads,
                Oot3d::EffectResource::HierarchicalDepth);
            const bool sssrContract =
                effectPlan.ReflectionProvider ==
                    Oot3d::ReflectionMode::FidelityFxSssr;
            const auto* reflectionDepthBinding = sssrContract
                ? &requireEffectImageRead(
                      reflectionReads,
                      Oot3d::EffectResource::NativeDepth)
                : nullptr;
            const auto* reflectionMotionBinding = sssrContract
                ? &requireEffectImageRead(
                      reflectionReads,
                      Oot3d::EffectResource::MotionVectors)
                : nullptr;
            const VkImage reflectionSceneImage =
                reinterpret_cast<VkImage>(
                    reflectionSceneBinding->NativeImage);
            const VkFormat reflectionSceneFormat =
                static_cast<VkFormat>(reflectionSceneBinding->Format);
            const VkImage reflectionDepthImage =
                reflectionDepthBinding == nullptr
                    ? VK_NULL_HANDLE
                    : reinterpret_cast<VkImage>(
                          reflectionDepthBinding->NativeImage);
            const VkFormat reflectionDepthFormat =
                reflectionDepthBinding == nullptr
                    ? VK_FORMAT_UNDEFINED
                    : static_cast<VkFormat>(reflectionDepthBinding->Format);
            const VkImage reflectionNormalImage =
                reinterpret_cast<VkImage>(
                    reflectionNormalBinding.NativeImage);
            const VkFormat reflectionNormalFormat =
                static_cast<VkFormat>(reflectionNormalBinding.Format);
            const VkImage reflectionMaterialImage =
                reinterpret_cast<VkImage>(
                    reflectionMaterialBinding.NativeImage);
            const VkFormat reflectionMaterialFormat =
                static_cast<VkFormat>(reflectionMaterialBinding.Format);
            const VkImage reflectionHiZImage =
                reinterpret_cast<VkImage>(
                    reflectionHiZBinding.NativeImage);
            const VkFormat reflectionHiZFormat =
                static_cast<VkFormat>(reflectionHiZBinding.Format);
            const VkImage reflectionMotionImage =
                reflectionMotionBinding == nullptr
                    ? VK_NULL_HANDLE
                    : reinterpret_cast<VkImage>(
                          reflectionMotionBinding->NativeImage);
            const VkFormat reflectionMotionFormat =
                reflectionMotionBinding == nullptr
                    ? VK_FORMAT_UNDEFINED
                    : static_cast<VkFormat>(reflectionMotionBinding->Format);
            transitionEffectPassGuides(
                Oot3d::DisplayEffectPass::Reflections);
            mGpuProfiler.BeginScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::Reflection,
                command);
            activeReflectionProvider = Oot3d::SelectReflectionProvider(
                presentationSettings.Effects.Reflections,
                {
                    mHiZReflectionPass.Available() && mHiZOutputValid,
                    mFidelityFxSssrPass.Available() &&
                        mReflectionIblPass.Available() &&
                        mReflectionMaterialResolvePass.Available(),
                    motionAvailableForDisplay,
                    temporalState.has_value(),
                    !effectGuideTarget->WBuffering,
                    mLinearSceneColorPass.Available(),
                });
            if (presentationSettings.Effects.Reflections ==
                    Oot3d::ReflectionMode::FidelityFxSssr &&
                activeReflectionProvider !=
                    Oot3d::ReflectionProvider::FidelityFxSssr) {
                const std::string fallbackReason =
                    !mFidelityFxSssrPass.Available()
                        ? mFidelityFxSssrPass.UnavailableReason()
                    : !mReflectionIblPass.Available()
                        ? mReflectionIblPass.UnavailableReason()
                    : !mReflectionMaterialResolvePass.Available()
                        ? mReflectionMaterialResolvePass.UnavailableReason()
                    : effectGuideTarget->WBuffering
                        ? "FidelityFX SSSR does not support PICA W-buffer depth"
                    : !motionAvailableForDisplay
                        ? "FidelityFX SSSR motion vectors are unavailable"
                    : !mLinearSceneColorPass.Available()
                        ? mLinearSceneColorPass.UnavailableReason()
                        : "FidelityFX SSSR temporal metadata is unavailable";
                mDiagnostics.RecordFidelityFxSssrFallback(
                    fallbackReason);
            }
            if (activeReflectionProvider ==
                Oot3d::ReflectionProvider::FidelityFxSssr) {
                const auto reflectionEnvironment =
                    effectGuideTarget->ReflectionEnvironment.Resolve(
                        mFrameCounter);
                const bool reflectionIblExecuted =
                    mReflectionIblPass.Execute(
                        mCurrentFrame, reflectionEnvironment);
                const bool fidelityFxIblConfigured =
                    reflectionIblExecuted &&
                    mFidelityFxSssrPass.SetIblResources(
                        mReflectionIblPass.EnvironmentImage(),
                        Oot3d::ReflectionIblPass::EnvironmentSize,
                        Oot3d::ReflectionIblPass::EnvironmentMipCount,
                        mReflectionIblPass.BrdfImage(),
                        Oot3d::ReflectionIblPass::BrdfSize);
                if (reflectionIblExecuted) {
                    recordEffectPassTransitions(
                        mReflectionIblPass.LastBarrierExecution());
                    mDiagnostics.RecordReflectionIbl(
                        mReflectionIblPass.ProfileUpdatedLastExecute(),
                        mReflectionIblPass.PicaDerivedLastExecute(),
                        Oot3d::ReflectionIblPass::EnvironmentMipCount,
                        mReflectionIblPass.EnvironmentOwnedByNri(),
                        mReflectionIblPass.BrdfOwnedByNri(),
                        mReflectionIblPass.ComputeOwnedByNri(),
                        mReflectionIblPass.BarriersOwnedByNri());
                }
                const bool reflectionIblReady =
                    reflectionIblExecuted && fidelityFxIblConfigured;
                const bool rawFidelityFxExecuted =
                    reflectionIblReady &&
                    linearWorkingColorForDisplay &&
                    mFidelityFxSssrPass.Configure(
                        mFrameCounter,
                        effectGuideTarget->Width,
                        effectGuideTarget->Height,
                        reflectionSceneImage,
                        reflectionSceneFormat,
                        reflectionDepthImage,
                        reflectionDepthFormat,
                        reflectionMotionImage,
                        reflectionMotionFormat,
                        reflectionNormalImage,
                        reflectionNormalFormat,
                        reflectionMaterialImage,
                        reflectionMaterialFormat) &&
                    mFidelityFxSssrPass.Execute(
                        command, *temporalState,
                        *presentationPerspective,
                        presentationSettings.Effects, mCurrentFrame,
                        Oot3d::PicaSurfaceCoordinates::FromTransferFlags(transfer.Flags));
                const bool fidelityFxExecuted =
                    rawFidelityFxExecuted &&
                    transientReflectionOutput != nullptr &&
                    mReflectionMaterialResolvePass.Configure(
                        *transientReflectionOutput) &&
                    mReflectionMaterialResolvePass.Execute(
                        mCurrentFrame,
                        mFidelityFxSssrPass.OutputImage(),
                        VK_FORMAT_R16G16B16A16_SFLOAT,
                        reflectionNormalImage,
                        reflectionNormalFormat,
                        reflectionMaterialImage,
                        reflectionMaterialFormat,
                        mReflectionIblPass.BrdfImage(),
                        VK_FORMAT_R16G16_SFLOAT,
                        *presentationPerspective,
                        presentationSettings.Effects
                            .ReflectionRoughnessBias,
                        reflectionBarrierPlan,
                        Oot3d::PicaSurfaceCoordinates::FromTransferFlags(transfer.Flags));
                if (rawFidelityFxExecuted) {
                    recordEffectPassTransitions(
                        mFidelityFxSssrPass.LastBarrierExecution());
                }
                if (fidelityFxExecuted) {
                    recordEffectPassTransitions(
                        mReflectionMaterialResolvePass
                            .LastBarrierExecution());
                    activeReflectionImage =
                        mReflectionMaterialResolvePass.OutputImage();
                    activeReflectionView =
                        mReflectionMaterialResolvePass.OutputView();
                    mDiagnostics.RecordReflectionMaterialResolve(
                        mReflectionMaterialResolvePass.OutputOwnedByNri(),
                        mReflectionMaterialResolvePass.ComputeOwnedByNri(),
                        mReflectionMaterialResolvePass.BarriersOwnedByNri());
                    reflectionOutputEncoding =
                        Oot3d::SceneColorEncoding::Linear;
                } else {
                    mDiagnostics.RecordFidelityFxSssrFallback(
                        !reflectionIblExecuted
                            ? mReflectionIblPass.UnavailableReason()
                        : !fidelityFxIblConfigured
                            ? mFidelityFxSssrPass.UnavailableReason()
                        : !linearWorkingColorForDisplay
                            ? mLinearSceneColorPass.UnavailableReason()
                        : !rawFidelityFxExecuted
                            ? mFidelityFxSssrPass.UnavailableReason()
                            : mReflectionMaterialResolvePass
                                  .UnavailableReason());
                    activeReflectionProvider =
                        Oot3d::ReflectionProvider::HiZ;
                    reflectionOutputEncoding =
                        Oot3d::SceneColorEncoding::Unknown;
                }
            }
            if (activeReflectionProvider ==
                Oot3d::ReflectionProvider::HiZ) {
                const bool hiZExecuted =
                    transientReflectionOutput != nullptr &&
                    mHiZReflectionPass.Configure(
                        *transientReflectionOutput,
                        reflectionSceneImage,
                        reflectionSceneFormat,
                        reflectionHiZImage,
                        reflectionHiZFormat,
                        reflectionNormalImage,
                        reflectionNormalFormat,
                        reflectionMaterialImage,
                        reflectionMaterialFormat) &&
                    mHiZReflectionPass.Execute(
                        command, mCurrentFrame,
                        mHiZDepthPyramidPass.MipCount(),
                        *presentationPerspective,
                        presentationSettings.Effects,
                        reflectionBarrierPlan,
                        Oot3d::PicaSurfaceCoordinates::FromTransferFlags(transfer.Flags));
                if (hiZExecuted) {
                    recordEffectPassTransitions(
                        mHiZReflectionPass.LastBarrierExecution());
                    activeReflectionImage =
                        mHiZReflectionPass.OutputImage();
                    activeReflectionView =
                        mHiZReflectionPass.OutputView();
                    reflectionOutputEncoding =
                        reflectionSceneBinding == nullptr
                            ? Oot3d::NativePicaSceneColorEncoding()
                            : reflectionSceneBinding->ColorEncoding;
                    // GI specular fills what the screen-space trace missed,
                    // in place, weighted by the same material resolve.
                    if (effectPlan.ReflectionMissesFromGlobalIllumination && giForDisplay &&
                        mReflectionIblPass.Available() &&
                        mReflectionMaterialResolvePass.Available()) {
                        const auto& specularBinding = requireEffectImageRead(
                            reflectionReads, Oot3d::EffectResource::SpecularIllumination);
                        const bool iblExecuted = mReflectionIblPass.Execute(
                            mCurrentFrame,
                            effectGuideTarget->ReflectionEnvironment.Resolve(mFrameCounter));
                        if (iblExecuted)
                            recordEffectPassTransitions(mReflectionIblPass.LastBarrierExecution());
                        const float screenSpaceStrength =
                            std::max(presentationSettings.Effects.ReflectionStrength, 0.01F);
                        if (iblExecuted &&
                            mReflectionMaterialResolvePass.Configure(*transientReflectionOutput) &&
                            mReflectionMaterialResolvePass.Execute(
                                mCurrentFrame,
                                reinterpret_cast<VkImage>(specularBinding.NativeImage),
                                static_cast<VkFormat>(specularBinding.Format),
                                reflectionNormalImage, reflectionNormalFormat,
                                reflectionMaterialImage, reflectionMaterialFormat,
                                mReflectionIblPass.BrdfImage(), VK_FORMAT_R16G16_SFLOAT,
                                *presentationPerspective, 0.0F, reflectionBarrierPlan,
                                Oot3d::PicaSurfaceCoordinates::FromTransferFlags(transfer.Flags),
                                {true,
                                 presentationSettings.Effects.GlobalIllumination.SpecularIntensity *
                                     giSources.Gain / screenSpaceStrength,
                                 reflectionOutputEncoding == Oot3d::SceneColorEncoding::Srgb}))
                            recordEffectPassTransitions(
                                mReflectionMaterialResolvePass.LastBarrierExecution());
                    }
                } else {
                    activeReflectionProvider =
                        Oot3d::ReflectionProvider::Off;
                    reflectionOutputEncoding =
                        Oot3d::SceneColorEncoding::Unknown;
                }
            }
            reflectionsForDisplay =
                activeReflectionProvider !=
                    Oot3d::ReflectionProvider::Off &&
                activeReflectionImage != VK_NULL_HANDLE &&
                activeReflectionView != VK_NULL_HANDLE;
            mGpuProfiler.EndScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::Reflection,
                command);
            if (reflectionsForDisplay) {
                mSceneSurfaces.Publish({*reflectionKey,
                    reinterpret_cast<uintptr_t>(activeReflectionImage),
                    effectGuideTarget->Width, effectGuideTarget->Height,
                    static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT),
                    0, true, reflectionOutputEncoding});
                reflectionNriWrapped = mNriInterop.WrapTexture(
                    activeReflectionImage,
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_TYPE_2D,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    effectGuideTarget->Width, effectGuideTarget->Height);
                mHiZReflectionSurface = *reflectionKey;
                mReflectionExecutedThisFrame = true;
                mReflectionProviderThisFrame =
                    activeReflectionProvider;
                mReflectionOutputEncodingThisFrame =
                    reflectionOutputEncoding;
            }
        }
        if (reflectionsForDisplay || reflectionsSkipped) {
            bindEffectImage(
                Oot3d::EffectResource::ReflectionColor,
                activeReflectionImage, activeReflectionView,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                effectGuideTarget->Width, effectGuideTarget->Height,
                reflectionOutputEncoding);
        }
        std::optional<Oot3d::SceneSurfaceKey> taaKey;
        if (effectGuideTarget != nullptr) {
            taaKey = Oot3d::SceneSurfaceKey{
                transfer.RenderTargetNamespace,
                effectGuideTarget->Key.ColorPhysicalAddress,
                Oot3d::SceneSurfaceKind::Output};
        }
        const bool canReuseTaa =
            wantsTaa && mTaaExecutedThisFrame && mTaaOutputValid &&
            taaKey.has_value() && mTaaSurface.has_value() &&
            *mTaaSurface == *taaKey;
        std::optional<Oot3d::SceneSurfaceKey> compositeKey;
        if (effectGuideTarget != nullptr) {
            compositeKey = Oot3d::SceneSurfaceKey{
                transfer.RenderTargetNamespace,
                effectGuideTarget->Key.ColorPhysicalAddress,
                // A boundary composes the source's own domain, so its result is
                // not the presentation's composite: reusing it for a later
                // display of this target would scan out the pre-consumer scene.
                copyBoundary
                    ? Oot3d::SceneSurfaceKind::BoundaryComposition
                    : Oot3d::SceneSurfaceKind::Composite};
        }
        const bool canReuseComposite =
            effectPlan.Enabled(Oot3d::DisplayEffectPass::Composite) &&
            mCompositeExecutedThisFrame && compositeKey.has_value() &&
            mCompositeSurface.has_value() &&
            *mCompositeSurface == *compositeKey;
        const bool canReuseUpscaler =
            (wantsTemporalUpscaler || wantsNis) &&
            mUpscalerExecutedThisFrame &&
            mUpscalerProviderThisFrame == effectPlan.Upscaler &&
            mUpscalerRenderTargetNamespace ==
                transfer.RenderTargetNamespace &&
            mUpscalerDisplayPhysicalAddress ==
                transfer.OutputPhysicalAddress;
        // A boundary always composes when any scene stage applies, because its
        // consumer samples the composed colour instead of a later presentation
        // fusing those stages in. Presentation keeps its existing gate.
        bool compositeForTaa =
            (copyBoundary ||
             ((((wantsTaa && !canReuseTaa) ||
                (wantsTemporalUpscaler && !canReuseUpscaler)) &&
               motionAvailableForDisplay) ||
              (wantsNis && !canReuseUpscaler) ||
              (wantsSmaa && !mSmaaExecutedThisFrame) || giComposed)) &&
            (aoForDisplay || reflectionsForDisplay ||
             outlineForDisplay || giComposed) &&
            mSceneCompositePass.Available();
        const bool compositeAttempted = compositeForTaa;
        Oot3d::SceneColorEncoding compositeInputEncoding =
            linearWorkingColorForDisplay
                ? Oot3d::SceneColorEncoding::Linear
                : Oot3d::NativePicaSceneColorEncoding();
        if (compositeForTaa) {
            const auto compositeReads = resolveEffectPassReads(
                Oot3d::DisplayEffectPass::Composite);
            const auto* compositeColorBinding = findEffectColorRead(
                compositeReads,
                Oot3d::DisplayEffectPass::Composite);
            if (compositeColorBinding == nullptr ||
                compositeColorBinding->Kind !=
                    Oot3d::EffectResourceBindingKind::Image) {
                throw std::runtime_error(
                    "composite pass has no declared color input");
            }
            const VkImage compositeColorImage =
                reinterpret_cast<VkImage>(
                    compositeColorBinding->NativeImage);
            const VkFormat compositeColorFormat =
                static_cast<VkFormat>(compositeColorBinding->Format);
            compositeInputEncoding =
                compositeColorBinding->ColorEncoding;
            const auto optionalCompositeImage = [
                &compositeReads, &requireEffectImageRead,
                compositeColorImage](Oot3d::EffectResource resource) {
                return compositeReads.Declares(resource)
                    ? reinterpret_cast<VkImage>(
                          requireEffectImageRead(
                              compositeReads, resource).NativeImage)
                    : compositeColorImage;
            };
            const auto optionalCompositeFormat = [
                &compositeReads, &requireEffectImageRead,
                compositeColorFormat](Oot3d::EffectResource resource) {
                return compositeReads.Declares(resource)
                    ? static_cast<VkFormat>(
                          requireEffectImageRead(
                              compositeReads, resource).Format)
                    : compositeColorFormat;
            };
            const VkImage compositeAoImage = optionalCompositeImage(
                Oot3d::EffectResource::AmbientOcclusion);
            const VkFormat compositeAoFormat = optionalCompositeFormat(
                Oot3d::EffectResource::AmbientOcclusion);
            const VkImage compositeReflectionImage = optionalCompositeImage(
                Oot3d::EffectResource::ReflectionColor);
            const VkFormat compositeReflectionFormat =
                optionalCompositeFormat(
                    Oot3d::EffectResource::ReflectionColor);
            const VkImage compositeMaterialImage = optionalCompositeImage(
                Oot3d::EffectResource::MaterialGuide);
            const VkFormat compositeMaterialFormat = optionalCompositeFormat(
                Oot3d::EffectResource::MaterialGuide);
            const VkImage compositeNormalImage = optionalCompositeImage(
                Oot3d::EffectResource::NormalGuide);
            const VkFormat compositeNormalFormat = optionalCompositeFormat(
                Oot3d::EffectResource::NormalGuide);
            const VkImage compositeTransparentDepthImage =
                optionalCompositeImage(
                    Oot3d::EffectResource::RigidMotionGuide);
            const VkFormat compositeTransparentDepthFormat =
                optionalCompositeFormat(
                    Oot3d::EffectResource::RigidMotionGuide);
            const VkImage compositeAmbientImage = optionalCompositeImage(
                Oot3d::EffectResource::AmbientGuide);
            const VkFormat compositeAmbientFormat = optionalCompositeFormat(
                Oot3d::EffectResource::AmbientGuide);
            const VkImage compositeDepthImage = optionalCompositeImage(
                Oot3d::EffectResource::NativeDepth);
            const VkFormat compositeDepthFormat = optionalCompositeFormat(
                Oot3d::EffectResource::NativeDepth);
            transitionEffectPassGuides(
                Oot3d::DisplayEffectPass::Composite);
            mGpuProfiler.BeginScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::SceneComposite,
                command);
            slowSteps.Mark("scene_effects");
            compositeForTaa =
                transientCompositeOutput != nullptr && mSceneCompositePass.Configure(*transientCompositeOutput) &&
                mSceneCompositePass.Execute(
                    command, mCurrentFrame, mFrameCounter, compositeColorImage, compositeColorFormat, compositeAoImage,
                    compositeAoFormat, compositeReflectionImage, compositeReflectionFormat, compositeMaterialImage,
                    compositeMaterialFormat, compositeNormalImage, compositeNormalFormat,
                    compositeTransparentDepthImage, compositeTransparentDepthFormat, compositeAmbientImage,
                    compositeAmbientFormat, compositeDepthImage, compositeDepthFormat,
                    optionalCompositeImage(Oot3d::EffectResource::FogGuide),
                    optionalCompositeFormat(Oot3d::EffectResource::FogGuide),
                    compositeReads.Declares(Oot3d::EffectResource::OutlineGeometryGuide)
                        ? optionalCompositeImage(Oot3d::EffectResource::OutlineGeometryGuide)
                        : mGeometryGuideFallback.Image,
                    VK_FORMAT_R32G32B32A32_UINT,
                    optionalCompositeImage(Oot3d::EffectResource::DiffuseIllumination),
                    optionalCompositeFormat(Oot3d::EffectResource::DiffuseIllumination),
                    optionalCompositeImage(Oot3d::EffectResource::DiffuseAlbedoGuide),
                    optionalCompositeFormat(Oot3d::EffectResource::DiffuseAlbedoGuide), giForDisplay,
                    // Screen-space GI validation composes J at the sources'
                    // gain only (no artistic intensity).
                    (screenSpaceGiRequested && presentationSettings.Effects.GlobalIllumination.ScreenSpace.Validation
                         ? 1.0F
                         : presentationSettings.Effects.GlobalIllumination.DiffuseIntensity) *
                        giSources.Gain,
                    aoForDisplay,
                    presentationSettings.Effects.AmbientOcclusion == Oot3d::AmbientOcclusionMode::Mxao
                        ? static_cast<uint32_t>(presentationSettings.Effects.MxaoDebug) : 0U,
                    reflectionsForDisplay,
                    giReflections ? presentationSettings.Effects.GlobalIllumination.SpecularIntensity * giSources.Gain
                                  : presentationSettings.Effects.ReflectionStrength,
                    presentationSettings.Effects.ReflectionDebugView, outlineForDisplay,
                    presentationSettings.Effects.ToonStyle, compositeInputEncoding, compositeBarrierPlan);
            mGpuProfiler.EndScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::SceneComposite,
                command);
            if (compositeForTaa) {
                if (mCompositeSurface.has_value() &&
                    *mCompositeSurface != *compositeKey) {
                    mSceneSurfaces.Retire(*mCompositeSurface);
                    mCompositeSurface.reset();
                }
                const VkImage compositeImage =
                    mSceneCompositePass.OutputImage();
                mSceneSurfaces.Publish({*compositeKey,
                    reinterpret_cast<uintptr_t>(compositeImage),
                    effectGuideTarget->Width, effectGuideTarget->Height,
                    static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT),
                    0, true, mSceneCompositePass.OutputEncoding()});
                mNriInterop.WrapTexture(
                    compositeImage, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_TYPE_2D,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    effectGuideTarget->Width, effectGuideTarget->Height);
                mCompositeSurface = *compositeKey;
                mCompositeExecutedThisFrame = true;
                mCompositeOutputEncodingThisFrame =
                    mSceneCompositePass.OutputEncoding();
                mDiagnostics.RecordSceneCompositePass(
                    mSceneCompositePass.OutputOwnedByNri(),
                    mSceneCompositePass.ComputeOwnedByNri(),
                    mSceneCompositePass.BarriersOwnedByNri());
                recordEffectPassTransitions(
                    mSceneCompositePass.LastBarrierExecution());
                bindEffectImage(
                    Oot3d::EffectResource::CompositeColor,
                    mSceneCompositePass.OutputImage(),
                    mSceneCompositePass.OutputView(),
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    effectGuideTarget->Width,
                    effectGuideTarget->Height,
                    mSceneCompositePass.OutputEncoding());
            }
        }
        bool taaNriWrapped = false;
        bool taaForDisplay = canReuseTaa;
        bool taaAttempted = false;
        if (!taaForDisplay && wantsTaa && motionAvailableForDisplay &&
            temporalState.has_value() && !mTaaExecutedThisFrame) {
            taaAttempted = true;
            const auto taaReads = resolveEffectPassReads(
                Oot3d::DisplayEffectPass::Taa);
            const auto* colorBinding = findEffectColorRead(
                taaReads, Oot3d::DisplayEffectPass::Taa);
            if (colorBinding == nullptr ||
                colorBinding->Kind !=
                    Oot3d::EffectResourceBindingKind::Image) {
                throw std::runtime_error(
                    "TAA pass has no declared color input");
            }
            const auto& motionBinding = requireEffectImageRead(
                taaReads,
                Oot3d::EffectResource::MotionVectors);
            const VkImage taaInputImage =
                reinterpret_cast<VkImage>(colorBinding->NativeImage);
            const VkFormat taaInputFormat =
                static_cast<VkFormat>(colorBinding->Format);
            const VkImage taaMotionImage =
                reinterpret_cast<VkImage>(motionBinding.NativeImage);
            const VkFormat taaMotionFormat =
                static_cast<VkFormat>(motionBinding.Format);
            const Oot3d::SceneColorEncoding taaInputEncoding =
                colorBinding->ColorEncoding;
            if (mTaaSurface.has_value() && *mTaaSurface != *taaKey) {
                mSceneSurfaces.Retire(*mTaaSurface);
                mTaaSurface.reset();
            }
            transitionEffectPassGuides(
                Oot3d::DisplayEffectPass::Taa);
            mGpuProfiler.BeginScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::AntiAliasing,
                command);
            // TAA resolves its colour input, which a native display
            // transfer may have downscaled from the scene target.
            taaForDisplay = mTemporalAaPass.Configure(
                    colorBinding->Width, colorBinding->Height) &&
                mTemporalAaPass.Execute(
                    command, mCurrentFrame, mFrameCounter,
                    taaInputImage, taaInputFormat,
                    taaMotionImage, taaMotionFormat,
                    motionBinding.Width, motionBinding.Height,
                    temporalState->HistoryValid,
                    presentationSettings.TaaHistoryWeight,
                    presentationSettings.TaaClampExpansion,
                    presentationSettings.TaaSharpness,
                    taaInputEncoding,
                    taaBarrierPlan);
            mGpuProfiler.EndScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::AntiAliasing,
                command);
            if (taaForDisplay) {
                const VkImage taaImage = mTemporalAaPass.OutputImage();
                mSceneSurfaces.Publish({*taaKey,
                    reinterpret_cast<uintptr_t>(taaImage),
                    mTemporalAaPass.Width(), mTemporalAaPass.Height(),
                    static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT),
                    0, true, mTemporalAaPass.OutputEncoding()});
                taaNriWrapped = mNriInterop.WrapTexture(
                    taaImage, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_TYPE_2D,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    mTemporalAaPass.Width(), mTemporalAaPass.Height());
                mTaaSurface = *taaKey;
                mTaaExecutedThisFrame = true;
                mTaaOutputValid = true;
                mTaaOutputEncodingThisFrame =
                    mTemporalAaPass.OutputEncoding();
                mTaaEffectsCompositedThisFrame = compositeForTaa;
                recordEffectPassTransitions(
                    mTemporalAaPass.LastBarrierExecution());
                bindEffectImage(
                    Oot3d::EffectResource::TemporalColor,
                    mTemporalAaPass.OutputImage(),
                    mTemporalAaPass.OutputView(),
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    mTemporalAaPass.Width(),
                    mTemporalAaPass.Height(),
                    mTaaOutputEncodingThisFrame);
            }
            mDiagnostics.RecordTemporalAa(
                taaForDisplay,
                taaForDisplay && mTemporalAaPass.HistoryUsedLastExecute(),
                taaNriWrapped, mTemporalAaPass.HistoryOwnedByNri(),
                mTemporalAaPass.ComputeOwnedByNri(),
                mTemporalAaPass.BarriersOwnedByNri());
        }
        if (canReuseTaa) {
            bindEffectImage(
                Oot3d::EffectResource::TemporalColor,
                mTemporalAaPass.OutputImage(),
                mTemporalAaPass.OutputView(),
                VK_FORMAT_R16G16B16A16_SFLOAT,
                mTemporalAaPass.Width(), mTemporalAaPass.Height(),
                mTaaOutputEncodingThisFrame);
        }
        const auto retainUpscalerOutput = [this, &effectPlan, &transfer](
            Oot3d::SceneColorEncoding outputEncoding,
            bool effectsComposited,
            uint32_t width, uint32_t height) {
            mUpscalerExecutedThisFrame = true;
            mUpscalerProviderThisFrame = effectPlan.Upscaler;
            mUpscalerOutputEncodingThisFrame = outputEncoding;
            mUpscalerEffectsCompositedThisFrame = effectsComposited;
            mUpscalerRenderTargetNamespace = transfer.RenderTargetNamespace;
            mUpscalerDisplayPhysicalAddress =
                transfer.OutputPhysicalAddress;
            mUpscalerOutputWidth = width;
            mUpscalerOutputHeight = height;
        };
        // A temporal upscaler reconstructs at its colour input's extent. Its
        // guides keep the scene target's, which differs when the native display
        // transfer resized the colour: they are then resampled to the colour's
        // extent, and the jitter (applied in target pixels) is given in input
        // pixels.
        const auto prepareUpscalerGuides = [&](Oot3d::NriTemporalUpscaleDispatchDesc& desc) {
            const uint32_t guideWidth = effectGuideTarget->Width;
            const uint32_t guideHeight = effectGuideTarget->Height;
            desc.JitterPixels = Oot3d::UpscalerJitterInInputPixels(
                mTemporalJitterPixels, guideWidth, guideHeight, desc.InputWidth, desc.InputHeight);
            const bool resample = Oot3d::UpscalerGuidesNeedResample(
                desc.InputWidth, desc.InputHeight, guideWidth, guideHeight);
            mDiagnostics.RecordUpscalerGuides(resample, guideWidth, guideHeight);
            if (!resample)
                return true;
            if (!mUpscalerGuidePass.Execute(
                    mCurrentFrame,
                    {desc.DepthImage, desc.DepthFormat, desc.MotionImage, desc.MotionFormat,
                     desc.ReactiveImage, desc.ReactiveFormat, guideWidth, guideHeight},
                    desc.InputWidth, desc.InputHeight))
                return false;
            recordEffectPassTransitions(mUpscalerGuidePass.LastBarrierExecution());
            desc.DepthImage = mUpscalerGuidePass.DepthImage();
            desc.DepthFormat = VK_FORMAT_R32_SFLOAT;
            desc.MotionImage = mUpscalerGuidePass.MotionImage();
            desc.MotionFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
            desc.ReactiveImage = mUpscalerGuidePass.ReactiveImage();
            desc.ReactiveFormat = VK_FORMAT_R16_SFLOAT;
            return true;
        };
        bool fsrForDisplay = wantsFsr && canReuseUpscaler;
        bool fsrAttempted = false;
        uint32_t fsrOutputWidth = canReuseUpscaler
            ? mUpscalerOutputWidth : plannedUpscalerWidth;
        uint32_t fsrOutputHeight = canReuseUpscaler
            ? mUpscalerOutputHeight : plannedUpscalerHeight;
        if (!fsrForDisplay && wantsFsr && motionAvailableForDisplay &&
            linearDepthForFsr && temporalState.has_value() &&
            !mUpscalerExecutedThisFrame && mNriUpscalerPass.FsrAvailable()) {
            fsrAttempted = true;
            const auto fsrReads = resolveEffectPassReads(
                Oot3d::DisplayEffectPass::TemporalUpscaler);
            const auto* fsrInputBinding = findEffectColorRead(
                fsrReads,
                Oot3d::DisplayEffectPass::TemporalUpscaler);
            if (fsrInputBinding == nullptr ||
                fsrInputBinding->Kind !=
                    Oot3d::EffectResourceBindingKind::Image) {
                throw std::runtime_error(
                    "FSR pass has no declared color input");
            }
            const auto& fsrDepthBinding = requireEffectImageRead(
                fsrReads,
                Oot3d::EffectResource::HierarchicalDepth);
            const auto& fsrMotionBinding = requireEffectImageRead(
                fsrReads,
                Oot3d::EffectResource::MotionVectors);
            const auto& fsrReactiveBinding = requireEffectImageRead(
                fsrReads,
                Oot3d::EffectResource::ReactiveMask);
            Oot3d::NriTemporalUpscaleDispatchDesc fsr;
            fsr.FrameIndex = mCurrentFrame;
            fsr.InputImage = reinterpret_cast<VkImage>(
                fsrInputBinding->NativeImage);
            fsr.InputFormat =
                static_cast<VkFormat>(fsrInputBinding->Format);
            fsr.InputEncoding = fsrInputBinding->ColorEncoding;
            fsr.DepthImage = reinterpret_cast<VkImage>(
                fsrDepthBinding.NativeImage);
            fsr.DepthFormat =
                static_cast<VkFormat>(fsrDepthBinding.Format);
            fsr.MotionImage = reinterpret_cast<VkImage>(
                fsrMotionBinding.NativeImage);
            fsr.MotionFormat =
                static_cast<VkFormat>(fsrMotionBinding.Format);
            fsr.ReactiveImage = reinterpret_cast<VkImage>(
                fsrReactiveBinding.NativeImage);
            fsr.ReactiveFormat =
                static_cast<VkFormat>(fsrReactiveBinding.Format);
            fsr.InputWidth = fsrInputBinding->Width;
            fsr.InputHeight = fsrInputBinding->Height;
            fsr.Quality = presentationSettings.UpscalerMode;
            fsr.NearPlane = presentationPerspective->NearPlane;
            fsr.FarPlane = presentationPerspective->FarPlane;
            fsr.VerticalFovRadians = 2.0F * std::atan(
                std::abs(presentationPerspective->Top -
                         presentationPerspective->Bottom) /
                std::max(2.0F * presentationPerspective->NearPlane,
                         1.0e-6F));
            fsr.FrameTimeMilliseconds = nativeTemporalSample.Available()
                                            ? nativeTemporalSample.SampleDeltaSeconds * 1000.0F
                                            : 1000.0F / 30.0F;
            fsr.ViewUnitsPerMeter = 100.0F;
            fsr.Sharpness = presentationSettings.UpscalerSharpness;
            fsr.ResetHistory = !temporalState->HistoryValid ||
                               temporalState->CameraCut;
            transitionEffectPassGuides(
                Oot3d::DisplayEffectPass::TemporalUpscaler);
            mGpuProfiler.BeginScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::Upscaler,
                command);
            fsrForDisplay = transientUpscaledOutput != nullptr &&
                prepareUpscalerGuides(fsr) &&
                mNriUpscalerPass.Configure(*transientUpscaledOutput) &&
                mNriUpscalerPass.ExecuteFsr(
                    command, fsr, temporalUpscalerBarrierPlan);
            mGpuProfiler.EndScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::Upscaler,
                command);
            if (fsrForDisplay) {
                recordEffectPassTransitions(mNriUpscalerPass.LastBarrierExecution());
                retainUpscalerOutput(mNriUpscalerPass.OutputEncoding(), compositeForTaa, fsrOutputWidth,
                                     fsrOutputHeight);
            }
            mDiagnostics.RecordFsrUpscale(fsrForDisplay, fsr.ResetHistory, fsr.InputWidth, fsr.InputHeight,
                                          fsrOutputWidth, fsrOutputHeight);
            mDiagnostics.RecordUpscalerOutputOwnership(fsrForDisplay, mNriUpscalerPass.OutputOwnedByNri(),
                                                       mNriUpscalerPass.BarriersOwnedByNri());
        }
        bool dlssForDisplay = wantsDlss && canReuseUpscaler;
        bool dlssAttempted = false;
        uint32_t dlssOutputWidth = canReuseUpscaler ? mUpscalerOutputWidth : plannedUpscalerWidth;
        uint32_t dlssOutputHeight = canReuseUpscaler ? mUpscalerOutputHeight : plannedUpscalerHeight;
        slowSteps.Mark("composite_taa");
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
        const bool d3d12FrameDlssRequested = mD3d12NgxProvider.FrameDispatchRequested() &&
                                             mD3d12NgxProvider.Available() && !mNativePicaPresentedThisFrame &&
                                             !mFrameExternalComputeSplit;
#else
        constexpr bool d3d12FrameDlssRequested = false;
#endif
        const bool dlssImplementationAvailable = d3d12FrameDlssRequested || mNriUpscalerPass.DlssAvailable();
        if (!dlssForDisplay && wantsDlss && motionAvailableForDisplay && temporalState.has_value() &&
            effectGuideTarget != nullptr && !effectGuideTarget->WBuffering && !mUpscalerExecutedThisFrame &&
            dlssImplementationAvailable) {
            dlssAttempted = true;
            const auto dlssReads = resolveEffectPassReads(Oot3d::DisplayEffectPass::TemporalUpscaler);
            const auto* dlssInputBinding = findEffectColorRead(dlssReads, Oot3d::DisplayEffectPass::TemporalUpscaler);
            if (dlssInputBinding == nullptr || dlssInputBinding->Kind != Oot3d::EffectResourceBindingKind::Image) {
                throw std::runtime_error("DLSS pass has no declared color input");
            }
            const auto& dlssDepthBinding = requireEffectImageRead(dlssReads, Oot3d::EffectResource::NativeDepth);
            const auto& dlssMotionBinding = requireEffectImageRead(dlssReads, Oot3d::EffectResource::MotionVectors);
            const auto& dlssReactiveBinding = requireEffectImageRead(dlssReads, Oot3d::EffectResource::ReactiveMask);
            Oot3d::NriTemporalUpscaleDispatchDesc dlss;
            dlss.FrameIndex = mCurrentFrame;
            dlss.InputImage = reinterpret_cast<VkImage>(dlssInputBinding->NativeImage);
            dlss.InputFormat = static_cast<VkFormat>(dlssInputBinding->Format);
            dlss.InputEncoding = dlssInputBinding->ColorEncoding;
            // NGX DLSR consumes the native hardware-depth surface. W-buffer
            // views are rejected above instead of being mislabeled as HW depth.
            dlss.DepthImage = reinterpret_cast<VkImage>(dlssDepthBinding.NativeImage);
            dlss.DepthFormat = static_cast<VkFormat>(dlssDepthBinding.Format);
            dlss.MotionImage = reinterpret_cast<VkImage>(dlssMotionBinding.NativeImage);
            dlss.MotionFormat = static_cast<VkFormat>(dlssMotionBinding.Format);
            dlss.ReactiveImage = reinterpret_cast<VkImage>(dlssReactiveBinding.NativeImage);
            dlss.ReactiveFormat = static_cast<VkFormat>(dlssReactiveBinding.Format);
            dlss.InputWidth = dlssInputBinding->Width;
            dlss.InputHeight = dlssInputBinding->Height;
            dlss.Quality = presentationSettings.UpscalerMode;
            dlss.ResetHistory = !temporalState->HistoryValid || temporalState->CameraCut;
            dlss.DlssPreset = Oot3d::DlssRenderPreset(presentationSettings.UpscalerDlssModel,
                                                      presentationSettings.UpscalerMode);
            transitionEffectPassGuides(Oot3d::DisplayEffectPass::TemporalUpscaler);
            const bool dlssGuidesReady = prepareUpscalerGuides(dlss);
            bool d3d12FrameDlss = false;
            bool d3d12FrameContractReady = false;
            bool d3d12FramePrepared = false;
            bool d3d12FrameSplitSubmitted = false;
            bool d3d12FrameQueued = false;
            bool d3d12OutputAcquired = false;
            uint64_t d3d12FrameDispatchCount = 0U;
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
            if (d3d12FrameDlssRequested && dlssGuidesReady) {
                const bool dlssGuidesResampled =
                    dlss.DepthImage != reinterpret_cast<VkImage>(dlssDepthBinding.NativeImage);
                const Oot3d::D3d12NgxFrameContract contract{
                    dlss.InputWidth,
                    dlss.InputHeight,
                    dlssOutputWidth,
                    dlssOutputHeight,
                    kFramesInFlight,
                    dlss.Quality,
                    Oot3d::SceneColorIsSrgb(dlss.InputEncoding),
                };
                if (!mD3d12NgxProvider.ConfiguredForFrameContract(contract)) {
                    CheckNativeVk(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle(D3D12 NGX frame contract)");
                    for (uint32_t slot = 0U; slot < kFramesInFlight; ++slot) {
                        const VkImage oldOutput = mD3d12NgxProvider.FrameOutputImage(slot);
                        if (oldOutput != VK_NULL_HANDLE)
                            mNriInterop.ForgetTexture(oldOutput);
                    }
                    mD3d12NgxProvider.ConfigureFrameContract(contract);
                }
                d3d12FrameContractReady = mD3d12NgxProvider.ConfiguredForFrameContract(contract);
                Oot3d::D3d12NgxFrameInputs bridgeInputs{
                    reinterpret_cast<VkImageView>(dlssInputBinding->NativeView),
                    dlssGuidesResampled ? mUpscalerGuidePass.DepthView()
                                        : reinterpret_cast<VkImageView>(dlssDepthBinding.NativeView),
                    dlssGuidesResampled ? mUpscalerGuidePass.MotionView()
                                        : reinterpret_cast<VkImageView>(dlssMotionBinding.NativeView),
                    dlssGuidesResampled ? mUpscalerGuidePass.ReactiveView()
                                        : reinterpret_cast<VkImageView>(dlssReactiveBinding.NativeView),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    dlssGuidesResampled ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                        : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    dlss.JitterPixels,
                    dlss.ResetHistory,
                };
                Oot3d::D3d12NgxFrameSynchronization synchronization;
                if (d3d12FrameContractReady) {
                    d3d12FramePrepared =
                        mD3d12NgxProvider.PrepareFrame(command, mCurrentFrame, bridgeInputs, synchronization);
                }
                if (d3d12FramePrepared) {
                    VkCommandBuffer continuation = SplitFrameForExternalCompute(
                        synchronization.Semaphore, synchronization.VulkanInputsReady, synchronization.D3d12OutputReady);
                    if (continuation != VK_NULL_HANDLE) {
                        command = continuation;
                        d3d12FrameSplitSubmitted = true;
                        d3d12FrameQueued = mD3d12NgxProvider.QueuePreparedFrame(synchronization);
                        if (d3d12FrameQueued) {
                            d3d12OutputAcquired = mD3d12NgxProvider.RecordOutputAcquire(command, synchronization);
                        }
                        d3d12FrameDlss = d3d12FrameQueued && d3d12OutputAcquired;
                    }
                }
            }
            d3d12FrameDispatchCount = mD3d12NgxProvider.Status().FrameDispatchCount;
#endif
            dlssForDisplay = d3d12FrameDlss;
            if (!dlssForDisplay && dlssGuidesReady && mNriUpscalerPass.DlssAvailable()) {
                mGpuProfiler.BeginScope(mCurrentFrame, Oot3d::GpuProfileScope::Upscaler, command);
                dlssForDisplay = transientUpscaledOutput != nullptr &&
                                 mNriUpscalerPass.Configure(*transientUpscaledOutput) &&
                                 mNriUpscalerPass.ExecuteDlss(command, dlss, temporalUpscalerBarrierPlan);
                mGpuProfiler.EndScope(mCurrentFrame, Oot3d::GpuProfileScope::Upscaler, command);
            }
            if (dlssForDisplay) {
                mUpscalerUsedD3d12ThisFrame = d3d12FrameDlss;
                if (!d3d12FrameDlss) {
                    recordEffectPassTransitions(mNriUpscalerPass.LastBarrierExecution());
                }
                retainUpscalerOutput(d3d12FrameDlss ? dlss.InputEncoding : mNriUpscalerPass.OutputEncoding(),
                                     compositeForTaa, dlssOutputWidth, dlssOutputHeight);
            }
            mDiagnostics.RecordDlssUpscale(dlssForDisplay, dlss.ResetHistory, dlss.InputWidth, dlss.InputHeight,
                                           dlssOutputWidth, dlssOutputHeight);
            mDiagnostics.RecordD3d12NgxFrame(d3d12FrameDlssRequested, d3d12FrameContractReady, d3d12FramePrepared,
                                             d3d12FrameSplitSubmitted, d3d12FrameQueued, d3d12OutputAcquired,
                                             d3d12FrameDispatchCount);
            mDiagnostics.RecordUpscalerOutputOwnership(dlssForDisplay,
                                                       d3d12FrameDlss || mNriUpscalerPass.OutputOwnedByNri(),
                                                       d3d12FrameDlss || mNriUpscalerPass.BarriersOwnedByNri());
        }
        bool nisForDisplay = wantsNis && canReuseUpscaler;
        bool nisAttempted = false;
        uint32_t nisOutputWidth = canReuseUpscaler
            ? mUpscalerOutputWidth : plannedUpscalerWidth;
        uint32_t nisOutputHeight = canReuseUpscaler
            ? mUpscalerOutputHeight : plannedUpscalerHeight;
        if (!nisForDisplay && wantsNis && !mUpscalerExecutedThisFrame &&
            mNriUpscalerPass.Available()) {
            nisAttempted = true;
            const auto nisReads = resolveEffectPassReads(
                Oot3d::DisplayEffectPass::Nis);
            const auto* nisInputBinding = findEffectColorRead(
                nisReads, Oot3d::DisplayEffectPass::Nis);
            if (nisInputBinding == nullptr ||
                nisInputBinding->Kind !=
                    Oot3d::EffectResourceBindingKind::Image) {
                throw std::runtime_error(
                    "NIS pass has no declared color input");
            }
            const VkImage nisInputImage = reinterpret_cast<VkImage>(
                nisInputBinding->NativeImage);
            const VkFormat nisInputFormat =
                static_cast<VkFormat>(nisInputBinding->Format);
            const Oot3d::SceneColorEncoding nisInputEncoding =
                nisInputBinding->ColorEncoding;
            const uint32_t nisInputWidth = nisInputBinding->Width;
            const uint32_t nisInputHeight = nisInputBinding->Height;
            // The typed display-effect plan authorizes only a resolved scene
            // surface, so the shared provider never sees the secondary UI
            // output regardless of either target's dimensions.
            mGpuProfiler.BeginScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::Upscaler,
                command);
            nisForDisplay = transientUpscaledOutput != nullptr &&
                mNriUpscalerPass.Configure(*transientUpscaledOutput) &&
                mNriUpscalerPass.Execute(
                    command, mCurrentFrame,
                    nisInputImage, nisInputFormat,
                    nisInputWidth, nisInputHeight,
                    presentationSettings.UpscalerMode,
                    presentationSettings.UpscalerSharpness,
                    nisBarrierPlan,
                    nisInputEncoding);
            mGpuProfiler.EndScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::Upscaler,
                command);
            if (nisForDisplay) {
                recordEffectPassTransitions(
                    mNriUpscalerPass.LastBarrierExecution());
                retainUpscalerOutput(
                    mNriUpscalerPass.OutputEncoding(), compositeForTaa,
                    nisOutputWidth, nisOutputHeight);
            }
            mDiagnostics.RecordNisUpscale(
                nisForDisplay, nisInputWidth, nisInputHeight,
                nisOutputWidth, nisOutputHeight);
            mDiagnostics.RecordUpscalerOutputOwnership(
                nisForDisplay, mNriUpscalerPass.OutputOwnedByNri(),
                mNriUpscalerPass.BarriersOwnedByNri());
        }
        slowSteps.Mark("upscaler");
        const bool smaaEffectsNeedComposite =
            aoForDisplay || reflectionsForDisplay ||
            outlineForDisplay;
        const bool canReuseSmaa = wantsSmaa &&
            mSmaaExecutedThisFrame &&
            mSmaaRenderTargetNamespace ==
                transfer.RenderTargetNamespace &&
            mSmaaDisplayPhysicalAddress ==
                transfer.OutputPhysicalAddress &&
            mSmaaWidth == plannedSmaaWidth &&
            mSmaaHeight == plannedSmaaHeight;
        bool smaaForDisplay = canReuseSmaa;
        bool smaaAttempted = false;
        Oot3d::SceneColorEncoding smaaInputEncoding = canReuseSmaa
            ? mSmaaOutputEncodingThisFrame
            : compositeForTaa
                ? mSceneCompositePass.OutputEncoding()
                : Oot3d::NativePicaSceneColorEncoding();
        if (!smaaForDisplay && wantsSmaa &&
            !mSmaaExecutedThisFrame &&
            (!smaaEffectsNeedComposite || compositeForTaa)) {
            smaaAttempted = true;
            const auto smaaReads = resolveEffectPassReads(
                Oot3d::DisplayEffectPass::Smaa);
            const auto* smaaInputBinding = findEffectColorRead(
                smaaReads, Oot3d::DisplayEffectPass::Smaa);
            if (smaaInputBinding == nullptr ||
                smaaInputBinding->Kind !=
                    Oot3d::EffectResourceBindingKind::Image) {
                throw std::runtime_error(
                    "SMAA pass has no declared color input");
            }
            const VkImage smaaInputImage = reinterpret_cast<VkImage>(
                smaaInputBinding->NativeImage);
            const VkFormat smaaInputFormat =
                static_cast<VkFormat>(smaaInputBinding->Format);
            const uint32_t smaaInputWidth = smaaInputBinding->Width;
            const uint32_t smaaInputHeight = smaaInputBinding->Height;
            smaaInputEncoding = smaaInputBinding->ColorEncoding;
            mGpuProfiler.BeginScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::AntiAliasing,
                command);
            smaaForDisplay = transientAntiAliasedOutput != nullptr &&
                transientAntiAliasedOutput->Width == smaaInputWidth &&
                transientAntiAliasedOutput->Height == smaaInputHeight &&
                mSmaa1xPass.Configure(*transientAntiAliasedOutput) &&
                mSmaa1xPass.Execute(
                    command, mCurrentFrame, mFrameCounter,
                    smaaInputImage, smaaInputFormat,
                    smaaInputEncoding,
                    smaaBarrierPlan);
            mGpuProfiler.EndScope(
                mCurrentFrame,
                Oot3d::GpuProfileScope::AntiAliasing,
                command);
            if (smaaForDisplay) {
                recordEffectPassTransitions(
                    mSmaa1xPass.LastBarrierExecution());
                mSmaaExecutedThisFrame = true;
                mSmaaEffectsCompositedThisFrame =
                    compositeForTaa;
                mSmaaOutputEncodingThisFrame =
                    mSmaa1xPass.OutputEncoding();
                mSmaaRenderTargetNamespace =
                    transfer.RenderTargetNamespace;
                mSmaaDisplayPhysicalAddress =
                    transfer.OutputPhysicalAddress;
                mSmaaWidth = smaaInputWidth;
                mSmaaHeight = smaaInputHeight;
                mDiagnostics.RecordSmaa1x(
                    mSmaa1xPass.OutputsOwnedByNri(),
                    mSmaa1xPass.LookupsOwnedByNri(),
                    mSmaa1xPass.ComputeOwnedByNri(),
                    mSmaa1xPass.BarriersOwnedByNri(),
                    mSmaa1xPass.LookupUploadOwnedByNri());
            }
        }
        if (copyBoundary) {
            // A native boundary stops here: the scene stages are resolved, with
            // the same-extent anti-aliasing of a boundary before native UI, and
            // the boundary's declared output is what the native consumer
            // samples. Upscaling and scanout belong to the display that later
            // presents this scene, and this frame never presents.
            if (effectPlan.Enabled(Oot3d::DisplayEffectPass::Guides)) {
                recordDeclared(Oot3d::DisplayEffectPass::Guides,
                               guidesMaterialized ? EffectOutcome::Fused
                                                  : EffectOutcome::Failed);
            }
            recordDeclared(
                Oot3d::DisplayEffectPass::WorkingColor,
                producedOutcome(workingColorAttempted,
                                linearWorkingColorForDisplay,
                                canReuseWorkingColor));
            recordDeclared(
                Oot3d::DisplayEffectPass::AmbientOcclusion,
                producedOutcome(aoAttempted, aoForDisplay));
            recordDeclared(
                Oot3d::DisplayEffectPass::HiZ,
                producedOutcome(hiZAttempted, hiZExecutedNow, canReuseHiZ));
            recordDeclared(
                Oot3d::DisplayEffectPass::Reflections,
                reflectionsSkipped
                    ? EffectOutcome::Skipped
                    : producedOutcome(reflectionsAttempted, reflectionsForDisplay,
                                      canReuseReflections));
            recordDeclared(
                Oot3d::DisplayEffectPass::Motion,
                producedOutcome(motionAttempted, motionForDisplay,
                                canReuseMotion));
            if (effectPlan.Enabled(Oot3d::DisplayEffectPass::Outline)) {
                // The outline is fused into the composition at a boundary:
                // there is no presentation pass left to apply it in.
                EffectOutcome outlineOutcome = EffectOutcome::Skipped;
                if (outlineForDisplay && !guidesMaterialized) {
                    outlineOutcome = EffectOutcome::Failed;
                } else if (outlineForDisplay && compositeForTaa) {
                    outlineOutcome = EffectOutcome::Fused;
                }
                recordDeclared(Oot3d::DisplayEffectPass::Outline,
                               outlineOutcome);
            }
            recordDeclared(
                Oot3d::DisplayEffectPass::Composite,
                producedOutcome(compositeAttempted, compositeForTaa,
                                canReuseComposite));
            recordDeclared(
                Oot3d::DisplayEffectPass::Taa,
                producedOutcome(taaAttempted, taaForDisplay, canReuseTaa));
            recordDeclared(
                Oot3d::DisplayEffectPass::Smaa,
                producedOutcome(smaaAttempted, smaaForDisplay, canReuseSmaa));
            if (beforeNativeUi) {
                mDiagnostics.RecordBeforeUiAntiAliasing(
                    taaForDisplay || smaaForDisplay,
                    !antiAliasingAvailableHere);
            }
            // A declared scene stage that did not produce its output fails the
            // copy: an enabled effect must never silently sample the base
            // colour target. GI-sourced reflections without GI for this sample
            // produced theirs (no reflection), like the GI's zero irradiance.
            const bool declaredSceneStageFailed =
                (effectPlan.Enabled(Oot3d::DisplayEffectPass::AmbientOcclusion) &&
                 !aoForDisplay) ||
                (effectPlan.Enabled(Oot3d::DisplayEffectPass::Reflections) &&
                 !reflectionsForDisplay && !reflectionsSkipped) ||
                (effectPlan.Enabled(Oot3d::DisplayEffectPass::Outline) &&
                 !outlineForDisplay) ||
                (effectPlan.Enabled(Oot3d::DisplayEffectPass::WorkingColor) &&
                 !linearWorkingColorForDisplay) ||
                (effectPlan.Enabled(Oot3d::DisplayEffectPass::Composite) &&
                 !compositeForTaa) ||
                (wantsTaa && !taaForDisplay) ||
                (wantsSmaa && !smaaForDisplay);
            if (declaredSceneStageFailed) {
                mDiagnostics.RecordDisplayEffectGraphExecution(
                    effectExecution.Summary());
                // A declared stage that did not produce its output is a fatal
                // boundary failure, not a fallback to the base colour target.
                restoreEffectPassGuides();
                restoreBoundarySourceColor();
                SetNativeError(
                    error,
                    "native scene boundary did not resolve every declared "
                    "scene stage");
                return false;
            }
            // The declared scene colour, not an effect name, decides whether
            // this boundary composed anything: when no scene stage applies, the
            // native target already is the composed scene.
            const bool composedScene =
                effectPlan.SceneOutput != Oot3d::EffectResource::SceneColor;
            const auto compositionPass = effectPlan.FindPass(
                Oot3d::DisplayEffectPass::SceneComposition);
            if (compositionPass == nullptr || copySource == nullptr) {
                throw std::runtime_error(
                    "native scene boundary has no declared composition pass");
            }
            if (smaaForDisplay) {
                bindEffectImage(
                    Oot3d::EffectResource::AntiAliasedColor,
                    mSmaa1xPass.OutputImage(), mSmaa1xPass.OutputView(),
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    mSmaaWidth, mSmaaHeight,
                    mSmaaOutputEncodingThisFrame);
            }
            const auto compositionReads = resolveEffectPassReads(
                Oot3d::DisplayEffectPass::SceneComposition);
            const auto* compositionSource = findEffectColorRead(
                compositionReads,
                Oot3d::DisplayEffectPass::SceneComposition);
            if (compositionSource == nullptr ||
                compositionSource->Kind !=
                    Oot3d::EffectResourceBindingKind::Image ||
                !Oot3d::IsKnownSceneColorEncoding(
                    compositionSource->ColorEncoding)) {
                throw std::runtime_error(
                    "native scene boundary composition source is incomplete");
            }
            const uint32_t compositionWidth = copySource->Width;
            const uint32_t compositionHeight = copySource->Height;
            if (compositionWidth == 0U || compositionHeight == 0U) {
                throw std::runtime_error(
                    "native scene boundary source has no internal extent");
            }
            if (!composedScene) {
                // No scene stage applies to this version: either none is
                // declared, or the colour the source target already holds
                // carries them. The identity import of that target is the
                // composed scene, and it keeps the version's own provenance so
                // a consumer of this copy does not resolve the scene again.
                recordDeclared(Oot3d::DisplayEffectPass::SceneComposition,
                               EffectOutcome::Skipped);
                copyOutput->Image = copySource->ColorImage;
                copyOutput->Format = VK_FORMAT_R8G8B8A8_UNORM;
                copyOutput->Width = compositionWidth;
                copyOutput->Height = compositionHeight;
                copyOutput->ShaderRead = false;
                copyOutput->Encoding = Oot3d::NativePicaSceneColorEncoding();
                copyOutput->SceneEffectsResolved =
                    display.SceneEffectsResolved;
                mDiagnostics.RecordDisplayEffectGraphExecution(
                    effectExecution.Summary());
                restoreEffectPassGuides();
                restoreBoundarySourceColor();
                return true;
            }
            // The composed scene is materialized once, at the source's own
            // internal extent, in the encoded domain the native consumer
            // samples. The target is offscreen: no swapchain, no present, and
            // the pixel domain is the source's own, so the composition is a
            // format/encode conversion and not a resample.
            // ponytail: one reusable canvas per extent; a device wait on extent
            // change is the ceiling. Key it per extent if copy resolutions ever
            // thrash within a frame.
            if (mNativePicaSceneCopyCanvas.Image == VK_NULL_HANDLE ||
                mNativePicaSceneCopyCanvasWidth != compositionWidth ||
                mNativePicaSceneCopyCanvasHeight != compositionHeight) {
                if (mNativePicaSceneCopyCanvas.Image != VK_NULL_HANDLE) {
                    CheckNativeVk(
                        vkDeviceWaitIdle(mDevice),
                        "vkDeviceWaitIdle(native scene composition canvas)");
                    // The canvas is an NRI-owned texture, so the owned path
                    // releases its views and image together.
                    mNriInterop.DestroyOwnedTexture(
                        mNativePicaSceneCopyCanvas.Image);
                    mNativePicaSceneCopyCanvas = {};
                }
                Oot3d::NriOwnedTexture2DDesc canvas{};
                canvas.Width = compositionWidth;
                canvas.Height = compositionHeight;
                canvas.Format = VK_FORMAT_R8G8B8A8_UNORM;
                canvas.Usage =
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                if (!mNriInterop.CreateOwnedTexture2D(
                        canvas, mNativePicaSceneCopyCanvas)) {
                    throw std::runtime_error(
                        "native scene composition canvas is unavailable");
                }
                mNativePicaSceneCopyCanvasWidth = compositionWidth;
                mNativePicaSceneCopyCanvasHeight = compositionHeight;
            }
            bindEffectImage(
                Oot3d::EffectResource::SceneComposition,
                mNativePicaSceneCopyCanvas.Image,
                mNativePicaSceneCopyCanvas.ColorAttachmentView,
                VK_FORMAT_R8G8B8A8_UNORM, compositionWidth,
                compositionHeight);
            const VkImage composedImage = reinterpret_cast<VkImage>(
                compositionSource->NativeImage);
            const VkFormat composedFormat =
                static_cast<VkFormat>(compositionSource->Format);
            std::array<VkImage, 11> compositionImages{};
            std::array<VkFormat, 11> compositionFormats{};
            compositionImages.fill(composedImage);
            compositionFormats.fill(composedFormat);
            // The integer geometry guide cannot fall back to the color source.
            compositionImages[10] = mGeometryGuideFallback.Image;
            compositionFormats[10] = VK_FORMAT_R32G32B32A32_UINT;
            Oot3d::PicaScanoutPolicyInput compositionPolicy{};
            compositionPolicy.TransferFlags = 0U;
            compositionPolicy.AaMode = 0U;
            compositionPolicy.Width = compositionWidth;
            compositionPolicy.Height = compositionHeight;
            compositionPolicy.EffectsComposited = true;
            compositionPolicy.InputEncoding =
                compositionSource->ColorEncoding;
            compositionPolicy.TargetSrgb = false;
            if (!Oot3d::ValidatePicaScanoutPolicyInput(compositionPolicy)) {
                throw std::runtime_error(
                    "native scene composition color policy is invalid");
            }
            const Oot3d::PicaScanoutPushConstants composition =
                Oot3d::BuildPicaScanoutPushConstants(compositionPolicy);
            // The composition samples the same guide attachments the scene
            // stages do, through the same declared graph edges.
            transitionEffectPassGuides(
                Oot3d::DisplayEffectPass::SceneComposition);
            mGpuProfiler.BeginScope(
                mCurrentFrame, Oot3d::GpuProfileScope::SceneComposite,
                command);
            const bool nriComposition =
                mNriPicaScanoutPass.ConfigureOffscreen(
                    VK_FORMAT_R8G8B8A8_UNORM) &&
                mNriPicaScanoutPass.Execute(
                    {
                        .FrameSlot = mCurrentFrame,
                        .FrameId = mFrameCounter,
                        .CommandBuffer = command,
                        .TargetImage = mNativePicaSceneCopyCanvas.Image,
                        .TargetView =
                            mNativePicaSceneCopyCanvas.ColorAttachmentView,
                        .TargetFormat = VK_FORMAT_R8G8B8A8_UNORM,
                        .TargetWidth = compositionWidth,
                        .TargetHeight = compositionHeight,
                        .InputImages = compositionImages,
                        .InputFormats = compositionFormats,
                        .Push = composition,
                        .ViewportX = 0.0F,
                        .ViewportY = 0.0F,
                        .ViewportWidth =
                            static_cast<float>(compositionWidth),
                        .ViewportHeight =
                            static_cast<float>(compositionHeight),
                        .ScissorX = 0,
                        .ScissorY = 0,
                        .ScissorWidth = compositionWidth,
                        .ScissorHeight = compositionHeight,
                        .ClearColor = {},
                        .Overlay = false,
                        .OffscreenTarget = true,
                    },
                    sceneCompositionBarrierPlan);
            mGpuProfiler.EndScope(
                mCurrentFrame, Oot3d::GpuProfileScope::SceneComposite,
                command);
            if (!nriComposition) {
                // Without the materialization there is no composed source, and
                // sampling the base target instead would silently drop the
                // enabled effects.
                restoreEffectPassGuides();
                restoreBoundarySourceColor();
                SetNativeError(
                    error, "native scene composition pass is unavailable");
                return false;
            }
            recordDeclared(Oot3d::DisplayEffectPass::SceneComposition,
                           EffectOutcome::Executed);
            recordEffectPassTransitions(
                mNriPicaScanoutPass.LastBarrierExecution());
            copyOutput->Image = mNativePicaSceneCopyCanvas.Image;
            copyOutput->Format = VK_FORMAT_R8G8B8A8_UNORM;
            copyOutput->Width = compositionWidth;
            copyOutput->Height = compositionHeight;
            copyOutput->ShaderRead = true;
            // The materialization wrote the encoded display domain: the
            // composed colour is what a display-encoded native target holds.
            copyOutput->Encoding = Oot3d::NativePicaSceneColorEncoding();
            copyOutput->SceneEffectsResolved = true;
            mDiagnostics.RecordDisplayEffectGraphExecution(
                effectExecution.Summary());
            restoreEffectPassGuides();
            restoreBoundarySourceColor();
            return true;
        }
        const bool effectsCompositedForScanout =
            smaaForDisplay
                ? mSmaaEffectsCompositedThisFrame
                : (fsrForDisplay || dlssForDisplay || nisForDisplay)
                    ? mUpscalerEffectsCompositedThisFrame
                : taaForDisplay
                    ? mTaaEffectsCompositedThisFrame
                    : compositeForTaa;

        if (linearWorkingColorForDisplay) {
            bindEffectImage(
                Oot3d::EffectResource::LinearWorkingColor,
                mLinearSceneColorPass.OutputImage(),
                mLinearSceneColorPass.OutputView(),
                VK_FORMAT_R16G16B16A16_SFLOAT,
                mLinearSceneColorPass.Width(),
                mLinearSceneColorPass.Height(),
                Oot3d::SceneColorEncoding::Linear);
        }
        if (aoForDisplay) {
            bindEffectImage(
                Oot3d::EffectResource::AmbientOcclusion,
                cacao  ? mCacaoPass.OutputImage()
                : mxao ? mMxaoPass.OutputImage()
                       : mAmbientOcclusionPass.OutputImage(),
                cacao  ? mCacaoPass.OutputView()
                : mxao ? mMxaoPass.OutputView()
                       : mAmbientOcclusionPass.OutputView(),
                VK_FORMAT_R32_SFLOAT, effectGuideTarget->Width,
                effectGuideTarget->Height);
        }
        if (mHiZOutputValid && effectGuideTarget != nullptr &&
            mHiZRenderTargetNamespace == transfer.RenderTargetNamespace &&
            mHiZDisplayPhysicalAddress == transfer.OutputPhysicalAddress) {
            bindEffectImage(
                Oot3d::EffectResource::HierarchicalDepth,
                mHiZDepthPyramidPass.OutputImage(),
                mHiZDepthPyramidPass.OutputView(),
                VK_FORMAT_R32_SFLOAT, effectGuideTarget->Width,
                effectGuideTarget->Height);
        }
        if (reflectionsForDisplay || reflectionsSkipped) {
            bindEffectImage(
                Oot3d::EffectResource::ReflectionColor,
                activeReflectionImage, activeReflectionView,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                effectGuideTarget->Width, effectGuideTarget->Height,
                reflectionOutputEncoding);
        }
        if (compositeForTaa || canReuseComposite) {
            bindEffectImage(
                Oot3d::EffectResource::CompositeColor,
                mSceneCompositePass.OutputImage(),
                mSceneCompositePass.OutputView(),
                VK_FORMAT_R16G16B16A16_SFLOAT,
                effectGuideTarget->Width, effectGuideTarget->Height,
                compositeForTaa
                    ? mSceneCompositePass.OutputEncoding()
                    : mCompositeOutputEncodingThisFrame);
        }
        if (motionAvailableForDisplay) {
            bindEffectImage(
                Oot3d::EffectResource::MotionVectors,
                mMotionVectorPass.OutputImage(),
                mMotionVectorPass.OutputView(),
                VK_FORMAT_R16G16B16A16_SFLOAT,
                effectGuideTarget->Width, effectGuideTarget->Height);
        }
        if (taaForDisplay) {
            bindEffectImage(
                Oot3d::EffectResource::TemporalColor,
                mTemporalAaPass.OutputImage(),
                mTemporalAaPass.OutputView(),
                VK_FORMAT_R16G16B16A16_SFLOAT,
                mTemporalAaPass.Width(), mTemporalAaPass.Height(),
                mTaaOutputEncodingThisFrame);
        }
        if (fsrForDisplay || dlssForDisplay || nisForDisplay) {
            const uint32_t outputWidth = fsrForDisplay
                ? fsrOutputWidth
                : dlssForDisplay ? dlssOutputWidth : nisOutputWidth;
            const uint32_t outputHeight = fsrForDisplay
                ? fsrOutputHeight
                : dlssForDisplay ? dlssOutputHeight : nisOutputHeight;
            VkImage upscaledImage = mNriUpscalerPass.OutputImage();
            VkImageView upscaledView = mNriUpscalerPass.OutputView();
            #if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
            if (dlssForDisplay && mUpscalerUsedD3d12ThisFrame) {
                upscaledImage =
                    mD3d12NgxProvider.FrameOutputImage(mCurrentFrame);
                upscaledView =
                    mD3d12NgxProvider.FrameOutputView(mCurrentFrame);
            }
            #endif
            if (effectPlan.NativeUiOverlay) {
                // The upscaler reconstructed the scene before native UI: lay
                // the display's UI over it (once per frame, reused with it).
                bool overlaid = canReuseUpscaler && mNativeUiOverlayExecutedThisFrame;
                if (!overlaid) {
                    const auto overlayReads = resolveEffectPassReads(
                        wantsNis ? Oot3d::DisplayEffectPass::Nis
                                 : Oot3d::DisplayEffectPass::TemporalUpscaler);
                    const auto& shown = requireEffectImageRead(
                        overlayReads, Oot3d::EffectResource::SceneColor);
                    const auto* beforeUiBinding = &requireEffectImageRead(
                        overlayReads, Oot3d::EffectResource::SceneBeforeUi);
                    mGpuProfiler.BeginScope(
                        mCurrentFrame, Oot3d::GpuProfileScope::Upscaler, command);
                    overlaid = mNativeUiOverlayPass.Execute(
                        mCurrentFrame, mFrameCounter,
                        {reinterpret_cast<VkImage>(shown.NativeImage),
                         static_cast<VkFormat>(shown.Format),
                         reinterpret_cast<VkImage>(beforeUiBinding->NativeImage),
                         static_cast<VkFormat>(beforeUiBinding->Format),
                         shown.Width, shown.Height,
                         upscaledImage, VK_FORMAT_R16G16B16A16_SFLOAT,
                         outputWidth, outputHeight,
                         Oot3d::SceneColorIsLinear(mUpscalerOutputEncodingThisFrame)});
                    mGpuProfiler.EndScope(
                        mCurrentFrame, Oot3d::GpuProfileScope::Upscaler, command);
                    if (!overlaid) {
                        throw std::runtime_error(
                            "native UI overlay failed: " +
                            mNativeUiOverlayPass.UnavailableReason());
                    }
                    recordEffectPassTransitions(
                        mNativeUiOverlayPass.LastBarrierExecution());
                    mNativeUiOverlayExecutedThisFrame = true;
                    mDiagnostics.RecordNativeUiOverlay();
                }
                upscaledImage = mNativeUiOverlayPass.OutputImage();
                upscaledView = mNativeUiOverlayPass.OutputView();
            }
            bindEffectImage(
                Oot3d::EffectResource::UpscaledColor,
                upscaledImage, upscaledView,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                outputWidth, outputHeight,
                mUpscalerOutputEncodingThisFrame);
        }
        if (smaaForDisplay) {
            bindEffectImage(
                Oot3d::EffectResource::AntiAliasedColor,
                mSmaa1xPass.OutputImage(), mSmaa1xPass.OutputView(),
                VK_FORMAT_R16G16B16A16_SFLOAT,
                mSmaaWidth, mSmaaHeight,
                mSmaaOutputEncodingThisFrame);
        }

        transitionEffectPassGuides(Oot3d::DisplayEffectPass::Scanout);
        const auto physicalEffectPlan =
            Oot3d::BuildEffectGraphPhysicalPlan(
                effectPlan.Graph, effectResources);
        mDiagnostics.RecordDisplayEffectPhysicalPlan(
            physicalEffectPlan.Summary());

        const auto* scanoutPass = effectPlan.FindPass(
            Oot3d::DisplayEffectPass::Scanout);
        if (scanoutPass == nullptr) {
            throw std::runtime_error(
                "display effect graph has no compiled scanout pass");
        }
        const auto scanoutReads = resolveEffectPassReads(
            Oot3d::DisplayEffectPass::Scanout);
        const auto* graphScanoutSource = findEffectColorRead(
            scanoutReads, Oot3d::DisplayEffectPass::Scanout);
        if (graphScanoutSource == nullptr ||
            graphScanoutSource->Kind !=
                Oot3d::EffectResourceBindingKind::Image ||
            !Oot3d::IsKnownSceneColorEncoding(
                graphScanoutSource->ColorEncoding)) {
            throw std::runtime_error(
                "display effect scanout source contract is incomplete");
        }

        const VkImage scanoutImage = reinterpret_cast<VkImage>(
            graphScanoutSource->NativeImage);
        const VkImageView scanoutView = reinterpret_cast<VkImageView>(
            graphScanoutSource->NativeView);
        const VkFormat scanoutFormat =
            static_cast<VkFormat>(graphScanoutSource->Format);
        const Oot3d::SceneColorEncoding scanoutInputEncoding =
            graphScanoutSource->ColorEncoding;

        std::array<VkImage, 11> scanoutImages{};
        std::array<VkFormat, 11> scanoutFormats{};
        scanoutImages.fill(scanoutImage);
        scanoutFormats.fill(scanoutFormat);
        std::array<VkDescriptorImageInfo, 11> imageInfos{};
        imageInfos[0].sampler = mNativePicaScanoutSampler;
        imageInfos[0].imageView = scanoutView;
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos.fill(imageInfos[0]);
        // The integer geometry guide cannot fall back to the color source.
        scanoutImages[10] = mGeometryGuideFallback.Image;
        scanoutFormats[10] = VK_FORMAT_R32G32B32A32_UINT;
        imageInfos[10].imageView = mGeometryGuideFallback.View;
        constexpr std::array<Oot3d::EffectResource, 11> scanoutSlotResources{
            Oot3d::EffectResource::Count,
            Oot3d::EffectResource::AmbientOcclusion,
            Oot3d::EffectResource::NativeDepth,
            Oot3d::EffectResource::NormalGuide,
            Oot3d::EffectResource::HierarchicalDepth,
            Oot3d::EffectResource::MaterialGuide,
            Oot3d::EffectResource::ReflectionColor,
            Oot3d::EffectResource::AmbientGuide,
            Oot3d::EffectResource::RigidMotionGuide,
            Oot3d::EffectResource::FogGuide,
            Oot3d::EffectResource::OutlineGeometryGuide,
        };
        for (size_t slot = 1; slot < scanoutSlotResources.size(); ++slot) {
            if (!scanoutPass->ReadsResource(scanoutSlotResources[slot])) {
                continue;
            }
            const auto* binding =
                scanoutReads.Find(scanoutSlotResources[slot]);
            if (binding == nullptr || binding->Kind !=
                    Oot3d::EffectResourceBindingKind::Image) {
                throw std::runtime_error(
                    "display effect scanout used a non-image read");
            }
            scanoutImages[slot] = reinterpret_cast<VkImage>(
                binding->NativeImage);
            scanoutFormats[slot] = static_cast<VkFormat>(binding->Format);
            imageInfos[slot].imageView =
                reinterpret_cast<VkImageView>(binding->NativeView);
        }
        std::array<VkWriteDescriptorSet, 11> descriptorWrites{};
        for (uint32_t binding = 0; binding < descriptorWrites.size(); ++binding) {
            descriptorWrites[binding] = {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            descriptorWrites[binding].dstSet = descriptor;
            descriptorWrites[binding].dstBinding = binding;
            descriptorWrites[binding].descriptorCount = 1;
            descriptorWrites[binding].descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[binding].pImageInfo = &imageInfos[binding];
        }
        vkUpdateDescriptorSets(mDevice,
                               static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);

        const auto presentation = FitNativePicaPresentation(
            mSwapchainExtent.width, mSwapchainExtent.height,
            static_cast<float>(display.Height),
            static_cast<float>(display.Width));
        const VkViewport viewport{
            presentation.X, presentation.Y, presentation.Width,
            presentation.Height, 0.0F, 1.0F};
        const int32_t scissorLeft =
            static_cast<int32_t>(std::floor(presentation.X));
        const int32_t scissorTop =
            static_cast<int32_t>(std::floor(presentation.Y));
        const int32_t scissorRight = std::min(
            static_cast<int32_t>(mSwapchainExtent.width),
            static_cast<int32_t>(std::ceil(
                presentation.X + presentation.Width)));
        const int32_t scissorBottom = std::min(
            static_cast<int32_t>(mSwapchainExtent.height),
            static_cast<int32_t>(std::ceil(
                presentation.Y + presentation.Height)));
        const VkRect2D scissor{
            {scissorLeft, scissorTop},
            {static_cast<uint32_t>(scissorRight - scissorLeft),
             static_cast<uint32_t>(scissorBottom - scissorTop)}};
        const Oot3d::PicaScanoutPolicyInput scanoutPolicy{
                transfer.Flags,
                wantsSmaa ? 0U : mSpatialAaMode,
                display.Width,
                display.Height,
                mHiZDepthPyramidPass.MipCount(),
                aoForDisplay,
                outlineForDisplay,
                reflectionsForDisplay,
                taaForDisplay || fsrForDisplay || dlssForDisplay,
                effectsCompositedForScanout,
                scanoutInputEncoding,
                mSwapchainFormat == VK_FORMAT_B8G8R8A8_SRGB ||
                    mSwapchainFormat == VK_FORMAT_R8G8B8A8_SRGB,
                presentationSettings.Effects,
                presentationPerspective};
        if (!Oot3d::ValidatePicaScanoutPolicyInput(scanoutPolicy)) {
            throw std::runtime_error(
                "PICA scanout color policy is invalid");
        }
        const Oot3d::PicaScanoutPushConstants scanout =
            Oot3d::BuildPicaScanoutPushConstants(scanoutPolicy);
        mGpuProfiler.BeginScope(
            mCurrentFrame, Oot3d::GpuProfileScope::Scanout,
            command);
        const bool nriScanout = mNriPicaScanoutPass.Execute({
            mCurrentFrame,
            mFrameCounter,
            command,
            mSwapchainImages[mCurrentImage],
            mSwapchainImageViews[mCurrentImage],
            mSwapchainFormat,
            mSwapchainExtent.width,
            mSwapchainExtent.height,
            scanoutImages,
            scanoutFormats,
            scanout,
            viewport.x,
            viewport.y,
            viewport.width,
            viewport.height,
            scissor.offset.x,
            scissor.offset.y,
            scissor.extent.width,
            scissor.extent.height,
            {mClearColor[0], mClearColor[1],
             mClearColor[2], mClearColor[3]},
            alphaOverlay}, scanoutBarrierPlan);
        if (nriScanout) {
            recordEffectPassTransitions(
                mNriPicaScanoutPass.LastBarrierExecution());
            mDiagnostics.RecordNriScanout(
                true, mNriPicaScanoutPass.PipelineOwnedByNri(),
                mNriPicaScanoutPass.DescriptorsOwnedByNri(), true);
        } else {
            if (alphaOverlay) {
                VkRenderPassBeginInfo begin{
                    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                begin.renderPass = mOverlayRenderPass;
                begin.framebuffer =
                    mSwapchainFramebuffers[mCurrentImage];
                begin.renderArea.extent = mSwapchainExtent;
                vkCmdBeginRenderPass(command, &begin,
                                     VK_SUBPASS_CONTENTS_INLINE);
                mRenderPassActive = true;
                mOverlayRenderPassActive = false;
            } else {
                BeginRenderPassIfNeeded();
            }
            vkCmdSetViewport(command, 0, 1, &viewport);
            vkCmdSetScissor(command, 0, 1, &scissor);
            vkCmdBindPipeline(
                command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                alphaOverlay ? mNativePicaScanoutOverlayPipeline
                             : mNativePicaScanoutPipeline);
            vkCmdBindDescriptorSets(
                command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                mNativePicaScanoutPipelineLayout, 0, 1,
                &descriptor, 0, nullptr);
            vkCmdPushConstants(
                command, mNativePicaScanoutPipelineLayout,
                VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(scanout),
                &scanout);
            vkCmdDraw(command, 3, 1, 0, 0);
            vkCmdEndRenderPass(command);
            mRenderPassActive = false;
            mOverlayRenderPassActive = false;
        }
        mGpuProfiler.EndScope(
            mCurrentFrame, Oot3d::GpuProfileScope::Scanout,
            command);
        if (scanout.InputLinear != 0U) {
            mDiagnostics.RecordLinearScanout(
                scanout.EncodeSrgb != 0U);
        }
        if (aoForDisplay && guideSampledResourceMask != 0U &&
            (compositeForTaa || !effectsCompositedForScanout)) {
            mDiagnostics.RecordAmbientOcclusionAmbientComposite();
        }

        if (effectPlan.Enabled(Oot3d::DisplayEffectPass::Guides)) {
            recordDeclared(
                Oot3d::DisplayEffectPass::Guides,
                guidesMaterialized
                    ? displayEffectsPreviouslyPresented
                        ? EffectOutcome::Reused
                        : EffectOutcome::Fused
                    : effectGuideTarget != nullptr
                        ? EffectOutcome::Failed
                        : EffectOutcome::Skipped);
        }
        recordDeclared(
            Oot3d::DisplayEffectPass::WorkingColor,
            producedOutcome(
                workingColorAttempted,
                linearWorkingColorForDisplay,
                canReuseWorkingColor));
        recordDeclared(
            Oot3d::DisplayEffectPass::AmbientOcclusion,
            producedOutcome(aoAttempted, aoForDisplay));
        recordDeclared(
            Oot3d::DisplayEffectPass::HiZ,
            producedOutcome(hiZAttempted, hiZExecutedNow,
                            canReuseHiZ));
        recordDeclared(
            Oot3d::DisplayEffectPass::Reflections,
            reflectionsSkipped
                ? EffectOutcome::Skipped
                : producedOutcome(reflectionsAttempted,
                                  reflectionsForDisplay,
                                  canReuseReflections));
        if (effectPlan.Enabled(Oot3d::DisplayEffectPass::Outline)) {
            EffectOutcome outlineOutcome = EffectOutcome::Skipped;
            if (outlineForDisplay && !guidesMaterialized) {
                outlineOutcome = EffectOutcome::Failed;
            } else if (outlineForDisplay && canReuseSmaa &&
                       effectsCompositedForScanout) {
                outlineOutcome = EffectOutcome::Reused;
            } else if (outlineForDisplay &&
                       (compositeForTaa || scanout.Outline != 0U)) {
                outlineOutcome = EffectOutcome::Fused;
            }
            recordDeclared(Oot3d::DisplayEffectPass::Outline,
                           outlineOutcome);
        }
        recordDeclared(
            Oot3d::DisplayEffectPass::Composite,
            producedOutcome(
                compositeAttempted, compositeForTaa,
                canReuseComposite));
        recordDeclared(
            Oot3d::DisplayEffectPass::Motion,
            producedOutcome(motionAttempted, motionForDisplay,
                            canReuseMotion));
        recordDeclared(
            Oot3d::DisplayEffectPass::Taa,
            producedOutcome(taaAttempted, taaForDisplay));
        recordDeclared(
            Oot3d::DisplayEffectPass::TemporalUpscaler,
            producedOutcome(fsrAttempted || dlssAttempted,
                            fsrForDisplay || dlssForDisplay));
        recordDeclared(
            Oot3d::DisplayEffectPass::Nis,
            producedOutcome(nisAttempted, nisForDisplay));
        recordDeclared(
            Oot3d::DisplayEffectPass::Smaa,
            producedOutcome(smaaAttempted, smaaForDisplay,
                            canReuseSmaa));
        recordDeclared(Oot3d::DisplayEffectPass::Scanout,
                       EffectOutcome::Executed);
        mDiagnostics.RecordDisplayEffectGraphExecution(
            effectExecution.Summary());

        restoreEffectPassGuides();

        if (!alphaOverlay) {
            mLastPresentedNativePicaDisplayTransfer = transfer;
        }
        mNativePicaPresentedThisFrame = true;
        if (reflectionsForDisplay && reflectionsAttempted &&
            activeReflectionProvider != Oot3d::ReflectionProvider::GlobalIllumination) {
            if (activeReflectionProvider ==
                Oot3d::ReflectionProvider::HiZ) {
                mDiagnostics.RecordHiZReflection(
                    true,
                    presentationSettings.Effects.ReflectionDebugView,
                    reflectionNriWrapped,
                    mHiZReflectionPass.OutputsOwnedByNri(),
                    mHiZReflectionPass.ComputeOwnedByNri(),
                    mHiZReflectionPass.BarriersOwnedByNri());
            } else {
                mDiagnostics.RecordFidelityFxSssr(
                    reflectionNriWrapped,
                    mFidelityFxSssrPass.OutputOwnedByNri(),
                    temporalState.has_value() &&
                        temporalState->HistoryValid,
                    linearWorkingColorForDisplay);
            }
        }
        mDiagnostics.RecordDisplayTransfer(
            true, smaaForDisplay ? 2U : mSpatialAaMode,
            static_cast<uint32_t>(mNativePicaSampleCount));
        return true;
}

// Resolves the sampled source of a raw copy at its order boundary. The copy's
// consumer samples the renderer-composed scene when a scene stage applies at
// that boundary, and the identity import of the native colour target
// otherwise. A false return fails the copy: an enabled effect never silently
// falls back to sampling the base target.
bool GfxRenderingAPIVulkan::ComposeNativePicaSceneForCopy(
    NativePicaRenderTarget& source, uint64_t afterDrawSubmissionId,
    GpuTextureCopySourceImport& output, std::string* error,
    const Renderer3ds::PicaCompositionStageAnchor* beforeUi) {
    output = {};
    if (mFrameActive) mGpuProfiler.Mark(mCurrentFrame, "compose_for_copy", mCommandBuffers[mCurrentFrame]);
    if (!mFrameActive || source.ColorImage == VK_NULL_HANDLE ||
        source.Width == 0U || source.Height == 0U ||
        !mNativePicaCompositionSchedule.Valid()) {
        SetNativeError(
            error, "native scene boundary has no composable frame state");
        return false;
    }
    const auto perspective = SamplePerspective(&source);
    (void)mNativeSceneView.Publish(
        mFrameCounter, mPicaSceneFrame, perspective);
    const ::Oot3d::Renderer::PicaCompositionTargetReference compositionTarget{
        source.Key.RenderTargetNamespace,
        source.Key.ColorPhysicalAddress,
        source.Key.DepthPhysicalAddress,
        source.Key.Width,
        source.Key.Height,
        source.Key.ColorFormat,
        source.Key.DepthFormat,
    };
    const auto composition = Oot3d::ResolvePicaDisplayComposition(
        mNativePicaCompositionSchedule, compositionTarget);
    if (beforeUi != nullptr &&
        (beforeUi != mNativePicaCompositionSchedule.FindAnchorBeforeDraw(
             Oot3d::EffectStage::BeforeUi, compositionTarget, afterDrawSubmissionId) ||
         !mNativePicaCompositionSchedule.Reached(*beforeUi))) {
        SetNativeError(error, "native pre-UI composition has no reached schedule anchor");
        return false;
    }
    // The scene this copy samples is the scene as of this copy's own order, so
    // the declared end-of-target providers are dispatched only where the native
    // sequence has already reached them. Nothing later is flushed into the
    // composition, and transparent/UI content stays after it.
    const auto targetEnd = ResolveNativePicaTargetEndDispatch(
        mNativePicaCompositionSchedule,
        mDirectionalShadowSchedulePlan,
        mPointShadowSchedulePlan,
        mInteractiveGrassProviderPlan.Valid(), compositionTarget,
        afterDrawSubmissionId,
        mGrassWorldTargetsThisFrame.contains(source.Key),
        mGrassInsertionAttemptedTargetsThisFrame.contains(source.Key));
    const bool presentation =
        mNativePicaCompositionSchedule.Purpose() ==
        Renderer3ds::PicaCompositionPurpose::Presentation;
    if (presentation && targetEnd.ShadowAnchor != nullptr) {
        TryRenderDirectionalShadowMap(
            source, mFrameGraphicsSettings, *targetEnd.ShadowAnchor);
    }
    if (presentation && targetEnd.PointShadowAnchor != nullptr) {
        TryRenderPointShadowMaps(
            source, mFrameGraphicsSettings, *targetEnd.PointShadowAnchor);
    }
    if (presentation && targetEnd.DispatchGeometry) {
        TryRenderInteractiveGrass(
            source, mFrameGraphicsSettings,
            Oot3d::EffectGeometryProviderInvocationKind::DeclaredBoundary);
    }
    // The boundary is described by the target it composes, not by a display
    // transfer: the copy's own order, the source's own surface coordinates and
    // its own internal extent. Nothing here fabricates a present or rotates a
    // swapchain, and the composition never writes back into the source target,
    // so a repeated boundary recomposes the same scene instead of compounding
    // effects onto it.
    GfxNativePicaDisplayTransferView boundary{};
    boundary.AfterDrawSubmissionId = afterDrawSubmissionId;
    boundary.RenderTargetNamespace = source.Key.RenderTargetNamespace;
    boundary.InputPhysicalAddress = source.Key.ColorPhysicalAddress;
    boundary.OutputPhysicalAddress = source.Key.ColorPhysicalAddress;
    boundary.InputWidth = static_cast<uint16_t>(source.Width);
    boundary.InputHeight = static_cast<uint16_t>(source.Height);
    boundary.OutputWidth = static_cast<uint16_t>(source.Width);
    boundary.OutputHeight = static_cast<uint16_t>(source.Height);
    boundary.Flags = 0U;
    boundary.Present = false;
    NativePicaDisplayImage boundaryImage{};
    boundaryImage.Width = source.Width;
    boundaryImage.Height = source.Height;
    boundaryImage.Image = source.ColorImage;
    boundaryImage.View = source.ColorView;
    boundaryImage.Initialized = true;
    boundaryImage.NriOwned = false;
    boundaryImage.CompositionDomain = composition.Domain;
    boundaryImage.CompositionSequenceId = composition.SequenceId;
    // ConsumeDraw advances the schedule before submitting the draw. The
    // explicit pre-UI anchor proves this attachment has not received UI yet.
    boundaryImage.SceneResolved = beforeUi != nullptr || composition.SceneResolved;
    // The version this boundary consumes is the source's own: when a native
    // draw already sampled a resolved composition over this target, its colour
    // carries the scene stages and this boundary must not apply them again. The
    // provenance is resolved here, for this version, exactly as presentation
    // resolves it, so chaining raw copies and native effects stays generic.
    boundaryImage.SceneEffectsResolved =
        NativePicaSceneFullyResolvedForTarget(
            source.Key.RenderTargetNamespace,
            source.Key.ColorPhysicalAddress);
    if (!ExecuteNativePicaDisplayGraph(
            boundary, boundaryImage, &source, &output, error,
            beforeUi != nullptr)) {
        output = {};
        return false;
    }
    if (output.Image == VK_NULL_HANDLE) {
        output = {};
        SetNativeError(
            error, "native scene boundary declared no sampled source");
        return false;
    }
    return true;
}

bool GfxRenderingAPIVulkan::ClearPicaRenderTarget(
    uint64_t renderTargetNamespace, uint32_t colorPhysicalAddress,
    std::string* error) {
    if (mFrameActive) mGpuProfiler.Mark(mCurrentFrame, "clear_target", mCommandBuffers[mCurrentFrame]);
    try {
        if (!mFrameActive || colorPhysicalAddress == 0U) {
            throw std::runtime_error(
                "native PICA render-target clear is incomplete");
        }
        for (auto& [key, target] : mNativePicaRenderTargets) {
            if (key.RenderTargetNamespace != renderTargetNamespace ||
                key.ColorPhysicalAddress != colorPhysicalAddress) {
                continue;
            }
            if (target.ShadowImage != VK_NULL_HANDLE) {
                EndNativePicaRenderPass();
                VkImageMemoryBarrier barrier{
                    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = target.ShadowImage;
                barrier.subresourceRange.aspectMask =
                    VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.levelCount = 1U;
                barrier.subresourceRange.layerCount = 1U;
                barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                        VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                auto command = mCommandBuffers[mCurrentFrame];
                vkCmdPipelineBarrier(
                    command, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                    nullptr, 1, &barrier);
                VkClearColorValue emptyShadow{};
                emptyShadow.uint32[0] = 0xFFFFFFFFU;
                vkCmdClearColorImage(command, target.ShadowImage,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     &emptyShadow, 1,
                                     &barrier.subresourceRange);
                std::swap(barrier.oldLayout, barrier.newLayout);
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                        VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(
                    command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                    nullptr, 1, &barrier);
            }
            BeginNativePicaRenderPass(target);
            std::array<VkClearAttachment, 5> attachments{};
            attachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            attachments[0].colorAttachment = 0U;
            attachments[0].clearValue.color = {{0.0F, 0.0F, 0.0F, 0.0F}};
            attachments[1].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            attachments[1].colorAttachment = 1U;
            attachments[1].clearValue.color = {{0.5F, 0.5F, 1.0F, 0.0F}};
            attachments[2].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            attachments[2].colorAttachment = 2U;
            attachments[2].clearValue.color = {{0.0F, 0.0F, 0.0F, 0.0F}};
            attachments[3].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            attachments[3].colorAttachment = 3U;
            attachments[3].clearValue.color = {{0.0F, 0.0F, 0.0F, 0.0F}};
            attachments[4].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (key.DepthFormat == 3U) {
                attachments[4].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
            attachments[4].clearValue.depthStencil = {1.0F, 0U};
            // A colour-only pass has no guide attachments to clear.
            uint32_t attachmentCount = static_cast<uint32_t>(attachments.size());
            if (mActiveNativePicaAttachments.NativeColorOnly()) {
                attachments[1] = attachments[4];
                attachmentCount = 2U;
            }
            const VkClearRect clearRect{
                {{0, 0}, {target.Width, target.Height}}, 0U, 1U};
            vkCmdClearAttachments(mCommandBuffers[mCurrentFrame],
                                  attachmentCount,
                                  attachments.data(), 1U, &clearRect);
            // The cleared content is new: it carries no resolved scene effects
            // any more, so the provenance serial moves on.
            target.ContentWriteSerial = ++mGpuTextureCopyWriteSerial;
        }
        // A target not created yet already starts with the same transparent
        // color and default depth values in GetOrCreateNativePicaRenderTarget.
        return true;
    } catch (const std::exception& exception) {
        SetNativeError(error, exception.what());
        return false;
    }
}

bool GfxRenderingAPIVulkan::PrepareOverlay(
    std::string* error) {
    if (mFrameActive) mGpuProfiler.Mark(mCurrentFrame, "overlay_prepare", mCommandBuffers[mCurrentFrame]);
    if (!mFrameActive) {
        SetNativeError(error,
                       "native PICA overlay requested outside an active frame");
        return false;
    }
    if (mOverlayRenderPassActive && mRenderPassActive) {
        return true;
    }
    if (!mLastPresentedNativePicaDisplayTransfer.has_value()) {
        return true;
    }
    GfxNativePicaDisplayTransferView transfer =
        *mLastPresentedNativePicaDisplayTransfer;
    transfer.Present = true;
    if (!SubmitPicaDisplayTransfer(transfer, error)) {
        return false;
    }
    if (mOverlayRenderPass == VK_NULL_HANDLE ||
        mSwapchainFramebuffers.empty()) {
        SetNativeError(error, "native PICA overlay render pass is unavailable");
        return false;
    }

    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = mOverlayRenderPass;
    begin.framebuffer = mSwapchainFramebuffers[mCurrentImage];
    begin.renderArea.extent = mSwapchainExtent;
    vkCmdBeginRenderPass(mCommandBuffers[mCurrentFrame], &begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    mGpuProfiler.BeginScope(
        mCurrentFrame, Oot3d::GpuProfileScope::Overlay,
        mCommandBuffers[mCurrentFrame]);
    mRenderPassActive = true;
    mOverlayRenderPassActive = true;
    ApplyDynamicViewportAndScissor();
    mDiagnostics.RecordOverlay();
    return true;
}

bool GfxRenderingAPIVulkan::SubmitPicaMemoryFill(
    const GfxNativePicaMemoryFillView& fill, std::string* error) {
    try {
        if (!mFrameActive || fill.StartPhysicalAddress == 0U ||
            fill.EndPhysicalAddress <= fill.StartPhysicalAddress ||
            (fill.Control & 1U) == 0U) {
            throw std::runtime_error("native PICA memory fill is incomplete");
        }
        const auto previous = std::find_if(
            mPendingNativePicaMemoryFills.begin(),
            mPendingNativePicaMemoryFills.end(),
            [&](const auto& pending) {
                return pending.StartPhysicalAddress == fill.StartPhysicalAddress &&
                       pending.EndPhysicalAddress == fill.EndPhysicalAddress &&
                       pending.RenderTargetNamespace ==
                           fill.RenderTargetNamespace;
            });
        if (previous != mPendingNativePicaMemoryFills.end()) {
            *previous = fill;
        } else {
            mPendingNativePicaMemoryFills.push_back(fill);
        }
        for (auto& [key, target] : mNativePicaRenderTargets) {
            ApplyPendingNativePicaMemoryFills(target);
        }
        mDiagnostics.RecordMemoryFill();
        return true;
    } catch (const std::exception& exception) {
        SetNativeError(error, exception.what());
        return false;
    }
}

} // namespace Fast

#endif
