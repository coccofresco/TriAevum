// Native PICA on Vulkan: draw submission, composition anchors, interactive grass and shadow maps.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_pica_internal.h"
#include "fast/renderer3ds/pica_draw_coverage.h"

namespace Fast {

bool PublishNativePicaGrassSurface(
    const GfxNativePicaDrawView& draw, uint64_t frameId,
    std::unordered_map<uint64_t, uint32_t>& occurrences,
    const Oot3d::InteractiveGrassSettings& settings,
    bool worldSurface, bool fragmentUsesTexture0,
    bool perspectiveProjection,
    Oot3d::GrassGeometryRegistry& geometryRegistry);

bool GfxRenderingAPIVulkan::PublishPicaCompositionSequence(
    const ::Fast::Renderer3ds::PicaCompositionSequenceView& sequence,
    std::string* error) {
    try {
        if (!mFrameActive) {
            throw std::runtime_error(
                "native PICA composition sequence published outside an active Vulkan frame");
        }
        if (mNativePicaCompositionSchedule.Valid()) {
            const auto previous = mNativePicaCompositionSchedule.Stats();
            if (previous.ConsumedDrawCount != previous.DrawCount) {
                throw std::runtime_error(
                    "previous native PICA composition sequence was not fully consumed");
            }
            if (mNativePicaCompositionSchedule.SequenceId() ==
                sequence.SequenceId) {
                throw std::runtime_error(
                    "native PICA composition sequence identity was reused");
            }
        }

        Oot3d::PicaCompositionSchedule compiled;
        std::string compileError;
        if (!compiled.Compile(sequence, &compileError)) {
            throw std::runtime_error(
                "native PICA composition schedule failed: " +
                compileError);
        }
        mNativePicaCompositionSchedule = std::move(compiled);
        // Prepared casters belong to exactly one published sequence.
        mPreparedShadowCasterFrame.BeginFrame(mCurrentFrame, mFrameCounter);
        mPreparedShadowCasters.clear();
        mDiagnostics.RecordPicaCompositionSchedule(
            mNativePicaCompositionSchedule.Stats());
        return true;
    } catch (const std::exception& exception) {
        SetNativeError(error, exception.what());
        return false;
    }
}

void GfxRenderingAPIVulkan::ExecuteNativePicaBeforeDrawAnchors(
    NativePicaRenderTarget& target, const GfxNativePicaDrawView& draw,
    double& shadowMilliseconds, double& grassMilliseconds) {
    if (mNativePicaCompositionSchedule.Purpose() ==
        Renderer3ds::PicaCompositionPurpose::NativeMemory) return;
    const auto reference = Renderer3ds::BuildPicaCompositionDrawReference(draw);
    const bool timed = mDiagnostics.Enabled();
    using Clock = std::chrono::steady_clock;
    auto begin = timed ? Clock::now() : Clock::time_point{};
    if (const auto* anchor = Oot3d::FindPicaEffectPassAnchor(
            mNativePicaCompositionSchedule, mDirectionalShadowSchedulePlan, reference.Target);
        anchor != nullptr && !anchor->AtTargetEnd && anchor->BeforeSubmissionId == draw.SubmissionId)
        TryRenderDirectionalShadowMap(target, mFrameGraphicsSettings, *anchor);
    if (const auto* anchor = Oot3d::FindPicaEffectPassAnchor(
            mNativePicaCompositionSchedule, mPointShadowSchedulePlan, reference.Target);
        anchor != nullptr && !anchor->AtTargetEnd && anchor->BeforeSubmissionId == draw.SubmissionId)
        TryRenderPointShadowMaps(target, mFrameGraphicsSettings, *anchor);
    if (timed) {
        const auto now = Clock::now();
        shadowMilliseconds += std::chrono::duration<double, std::milli>(now - begin).count();
        begin = now;
    }
    const auto* grassAnchor = mInteractiveGrassProviderPlan.Valid()
        ? mNativePicaCompositionSchedule.FindPublishedGeometryAnchor(reference.Target) : nullptr;
    if (grassAnchor != nullptr && !grassAnchor->AtTargetEnd &&
        grassAnchor->BeforeSubmissionId == draw.SubmissionId)
        TryRenderInteractiveGrass(target, mFrameGraphicsSettings,
            Oot3d::EffectGeometryProviderInvocationKind::DeclaredBoundary);
    if (timed)
        grassMilliseconds += std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    const auto& effects = mFrameGraphicsSettings.Effects;
    // Scene stages, same-extent anti-aliasing included: it resolves the scene
    // here so the UI that follows is drawn over the result, never through it.
    const bool sceneEffects = effects.AmbientOcclusion != Oot3d::AmbientOcclusionMode::Off ||
        effects.Reflections != Oot3d::ReflectionMode::Off ||
        effects.GlobalIllumination.Mode != Oot3d::GlobalIlluminationMode::Off ||
        (effects.Toon != Oot3d::ToonMode::Off && effects.ToonStyle.OutlineEnabled) ||
        mSceneAntiAliasing;
    // Presentation stages (AA, upscalers) read the display's world surface.
    // When native UI reaches the target before its transfer, that surface is
    // proven only by the scene completion recorded at this boundary.
    if (!mNativeFidelityProfile && (sceneEffects || mPresentationWorldSurface) &&
        draw.CompositionDomain == Renderer3ds::PicaCompositionDomain::Ui) {
        const auto* beforeUi = mNativePicaCompositionSchedule.FindAnchorBeforeDraw(
            Oot3d::EffectStage::BeforeUi, reference.Target, draw.SubmissionId);
        if (beforeUi != nullptr) {
            GpuTextureCopySourceImport composed;
            if (sceneEffects) {
                std::string error;
                if (!ComposeNativePicaSceneForCopy(target, draw.SubmissionId, composed, &error, beforeUi))
                    throw std::runtime_error(error);
            }
            if (composed.Image == VK_NULL_HANDLE || composed.Image == target.ColorImage) {
                // No scene stage was composed here: the target's current
                // content is already the scene as of this boundary, before UI.
                if (mPresentationWorldSurface)
                    target.SceneEffectsResolvedSerial = target.ContentWriteSerial;
            } else {
                if (!mNriPicaDisplayCopyPass.Execute({
                        mCurrentFrame, mFrameCounter, composed.Image, target.ColorImage,
                        composed.Format, {target.Width, target.Height, target.Width, target.Height},
                        composed.ShaderRead, true, true}))
                    throw std::runtime_error("native pre-UI scene commit failed");
                mDiagnostics.RecordNriPicaDisplayCopy(
                    true, mNriPicaDisplayCopyPass.LastBarrierCount());
                target.ContentWriteSerial = ++mGpuTextureCopyWriteSerial;
                target.SceneEffectsResolvedSerial = target.ContentWriteSerial;
            }
            // An upscaler changes the extent, so it cannot resolve here: keep
            // the scene this boundary completed, which the display transfer
            // transfers beside the display for the upscaler to reconstruct.
            if (mPresentationUpscaler && target.SceneEffectsResolvedSerial != 0U &&
                target.SceneEffectsResolvedSerial == target.ContentWriteSerial) {
                EndNativePicaRenderPass();
                if (mRenderPassActive) {
                    vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
                    mRenderPassActive = false;
                    mOverlayRenderPassActive = false;
                }
                auto& snapshot = GetOrCreateNativePicaDisplayImage(
                    mNativePicaSceneBeforeUiTargets, target.Key.RenderTargetNamespace,
                    target.Key.ColorPhysicalAddress, target.Width, target.Height);
                if (!mNriPicaDisplayCopyPass.Execute({
                        mCurrentFrame, mFrameCounter, target.ColorImage, snapshot.Image,
                        VK_FORMAT_R8G8B8A8_UNORM, {target.Width, target.Height, target.Width, target.Height},
                        false, snapshot.Initialized}))
                    throw std::runtime_error("native scene before UI snapshot failed");
                snapshot.Initialized = true;
                target.SceneBeforeUiSerial = target.ContentWriteSerial;
                mDiagnostics.RecordSceneBeforeUi(true, false);
            }
        }
    }
}

GfxRenderingAPIVulkan::NativePicaGeometryPlacement GfxRenderingAPIVulkan::PlaceNativePicaGeometry(
    const Oot3d::PicaGeometryResolveResult& resolution, Oot3dVulkanNativePicaCpuTimings* cpuTimings) {
    const auto& geometry = *resolution.Geometry;
    auto& frame = mFrameResources[mCurrentFrame];
    if (resolution.Cacheable) {
        auto& cached = mNativePicaGeometryBuffers[geometry.Identity];
        auto& buffer = cached.Buffers[mCurrentFrame];
        const VkDeviceSize payloadSize = static_cast<VkDeviceSize>(geometry.Payload.size());
        if (buffer.Buffer == VK_NULL_HANDLE || buffer.Size < payloadSize) {
            if (buffer.Buffer != VK_NULL_HANDLE) {
                mNriInterop.ForgetBuffer(buffer.Buffer);
            }
            DestroyBuffer(buffer);
            buffer = CreateBuffer(payloadSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
            cached.ContentVersions[mCurrentFrame] = 0U;
            cached.StructuralSignatures[mCurrentFrame] = 0U;
        }
        if (cached.ContentVersions[mCurrentFrame] != geometry.ContentVersion ||
            cached.StructuralSignatures[mCurrentFrame] != geometry.StructuralSignature) {
            std::memcpy(buffer.Mapped, geometry.Payload.data(), geometry.Payload.size());
            if (cpuTimings != nullptr) {
                cpuTimings->GeometryPersistentUploadBytes += geometry.Payload.size();
            }
            cached.ContentVersions[mCurrentFrame] = geometry.ContentVersion;
            cached.StructuralSignatures[mCurrentFrame] = geometry.StructuralSignature;
        }
        cached.LastUsedFrame = mFrameCounter;
        if (cpuTimings != nullptr) {
            cpuTimings->GeometryPersistentDraws = 1U;
        }
        return { buffer.Buffer, buffer.Size, static_cast<uint8_t*>(buffer.Mapped), 0U, true };
    }
    const VkDeviceSize baseOffset = (frame.VertexBytesUsed + 15U) & ~VkDeviceSize(15U);
    if (baseOffset + geometry.Payload.size() > frame.VertexBuffer.Size) {
        throw std::runtime_error("native PICA transient geometry arena exhausted");
    }
    std::memcpy(static_cast<uint8_t*>(frame.VertexBuffer.Mapped) + baseOffset, geometry.Payload.data(),
                geometry.Payload.size());
    frame.VertexBytesUsed = baseOffset + geometry.Payload.size();
    if (cpuTimings != nullptr) {
        cpuTimings->GeometryTransientUploadBytes += geometry.Payload.size();
        cpuTimings->GeometryTransientDraws = 1U;
    }
    return { frame.VertexBuffer.Buffer, frame.VertexBuffer.Size, static_cast<uint8_t*>(frame.VertexBuffer.Mapped),
             baseOffset, false };
}

bool GfxRenderingAPIVulkan::WantsPicaSequenceDrawPreparation(
    const Renderer3ds::PicaCompositionDrawReference& draw) const {
    // The directional and point shadow maps run at the opaque boundary of
    // this sample, so they need its opaque world before native order reaches it.
    const bool directionalMap =
        mDirectionalShadowMapBarrierPlan.Valid() && mDirectionalShadowSchedulePlan.Valid() &&
        Oot3d::PicaExtensionPassEnabled(mPicaExtensionGraph, Oot3d::PicaExtensionPass::DirectionalShadowLighting);
    const bool pointMaps =
        mPointShadowMapBarrierPlan.Valid() && mPointShadowSchedulePlan.Valid() &&
        Oot3d::PicaExtensionPassEnabled(mPicaExtensionGraph, Oot3d::PicaExtensionPass::PointShadowLighting);
    return mFrameActive && mNativePicaCompositionSchedule.Valid() &&
           mNativePicaCompositionSchedule.Purpose() == Renderer3ds::PicaCompositionPurpose::Presentation &&
           (directionalMap || pointMaps) &&
           mNriPicaPipelineBridge.OwnedDrawsEnabled() &&
           draw.Domain == Renderer3ds::PicaCompositionDomain::Scene &&
           draw.Composition.Layer == Renderer3ds::PicaCompositionLayer::OpaqueWorld &&
           !draw.Composition.NativeProjectedShadow;
}

bool GfxRenderingAPIVulkan::PreparePicaSequenceDraw(
    const GfxNativePicaDrawView& draw, std::string* error) {
    try {
        if (!mFrameActive || !mNativePicaCompositionSchedule.Valid() || !mPreparedShadowCasterFrame.Active() ||
            mPreparedShadowCasterFrame.FrameId() != mFrameCounter) {
            throw std::runtime_error("native PICA draw prepared outside its published sequence");
        }
        if (!WantsPicaSequenceDrawPreparation(Renderer3ds::BuildPicaCompositionDrawReference(draw))) {
            return true;
        }
        // Caster admission: depth-writing perspective geometry with its
        // captured view; an alpha-tested caster also needs its alpha test.
        Oot3d::DirectionalShadowMatrix viewToWorld;
        const bool admitted =
            AdmitsShadowCasterGeometry(draw) &&
            draw.Topology != GfxNativePicaTopology::TriangleFan && !draw.VertexShaderSource.empty() &&
            !draw.FragmentShaderSource.empty() && draw.VertexCount != 0U && !draw.VertexBindings.empty() &&
            !draw.VertexAttributes.empty() && draw.VertexUniformBytes.size() >= Oot3d::kPicaPackedVertexFloatOffset &&
            Oot3d::UsesPicaPerspectiveProjection(draw.VertexUniformBytes) && draw.LightBinding.WorldToView &&
            Oot3d::InvertDirectionalShadowMatrix(*draw.LightBinding.WorldToView, viewToWorld);
        if (!admitted) {
            mDiagnostics.RecordNriDirectionalShadowPreparation(false, false);
            return true;
        }

        std::vector<Oot3d::PicaNriSourceVertexStream> streams;
        std::vector<Oot3d::PicaNriSourceVertexBinding> sourceBindings;
        std::vector<Oot3d::PicaNriSourceVertexAttribute> attributes;
        streams.reserve(draw.VertexBindings.size());
        sourceBindings.reserve(draw.VertexBindings.size());
        for (const auto& binding : draw.VertexBindings) {
            streams.push_back({ { binding.Binding, binding.ByteStride, binding.PerInstance }, binding.Bytes });
            sourceBindings.push_back({ binding.Binding, binding.ByteStride, binding.PerInstance });
        }
        attributes.reserve(draw.VertexAttributes.size());
        for (const auto& attribute : draw.VertexAttributes) {
            attributes.push_back({ attribute.Location, attribute.Binding,
                                   static_cast<Oot3d::PicaNriVertexScalar>(attribute.Format),
                                   attribute.ComponentCount, attribute.ByteOffset });
        }
        const auto layout = Oot3d::BuildPicaNriVertexInputLayout(sourceBindings, attributes);
        const auto resolution = mPicaGeometryRegistry.Resolve({
            draw.GeometryIdentity,
            draw.GeometryContentVersion,
            draw.GeometryIdentityAvailable,
            mFrameCounter,
            streams,
            attributes,
            draw.IndexBytes,
            draw.Indexed,
            draw.IndicesAre16Bit,
            draw.VertexCount,
            true,
        });
        if (resolution.Geometry == nullptr) {
            throw std::runtime_error("native PICA geometry preparation failed");
        }
        for (const uint64_t evicted : mPicaGeometryRegistry.TakeEvictedIdentities()) {
            RetireNativePicaGeometry(evicted);
        }
        const auto& geometry = *resolution.Geometry;
        auto& frame = mFrameResources[mCurrentFrame];
        // An alpha-tested caster samples the textures and lighting LUT the
        // ordered draw resolves, through the same cache (custom replacements
        // included); a texture that cannot be resolved leaves it out of the map.
        const bool alphaTested = RunsDiscardingAlphaTest(draw);
        std::array<TextureRecord*, 3> casterTextures{ &mFallbackTexture, &mFallbackTexture, &mFallbackTexture };
        TextureRecord* casterLightingLut = nullptr;
        if (alphaTested) {
            std::string textureError;
            bool resolved = true;
            for (const auto& texture : draw.Textures) {
                TextureRecord* record = GetOrCreateNativePicaTexture(
                    texture, &textureError, mFrameAzaharTextureGeneration, draw.RenderTargetNamespace,
                    texture.NativeContentHashAvailable ? &texture.NativeContentHash : nullptr);
                resolved = resolved && record != nullptr;
                if (record != nullptr)
                    casterTextures[texture.Slot] = record;
            }
            if (resolved && !draw.LightingLut.PackedEntries.empty()) {
                casterLightingLut = GetOrCreateNativePicaLightingLut(draw.LightingLut, &textureError);
                resolved = casterLightingLut != nullptr;
            }
            mDiagnostics.RecordNriDirectionalShadowPreparedAlpha(resolved);
            if (!resolved) {
                mDiagnostics.RecordNriDirectionalShadowPreparation(false, !resolution.Cacheable);
                return true;
            }
        }
        // The ordered submission owns the transient arena: a caster that does
        // not fit stays out of this sample's map rather than exhausting it.
        const bool placeable = resolution.Cacheable ||
            ((frame.VertexBytesUsed + 15U) & ~VkDeviceSize(15U)) + geometry.Payload.size() <= frame.VertexBuffer.Size;
        if (!geometry.PackedValid || !layout.Valid() || layout.Bindings.empty() || layout.Attributes.empty() ||
            !placeable) {
            mDiagnostics.RecordNriDirectionalShadowPreparation(false, !resolution.Cacheable);
            return true;
        }
        const auto placement = PlaceNativePicaGeometry(resolution, nullptr);
        std::vector<Oot3d::PicaSceneVertexBufferBinding> bindings;
        bindings.reserve(geometry.PackedBindings.size());
        for (const auto& binding : geometry.PackedBindings) {
            bindings.push_back({ binding.Binding, placement.BaseOffset + binding.Offset, binding.Size, binding.Stride });
        }
        // The map reads these uniforms before the ordered submission uploads
        // its own copy. Both blocks are reserved first: growth replaces the arena.
        const auto alignedUniform = [this](VkDeviceSize bytes) {
            return (bytes + mUniformBufferAlignment - 1U) & ~(mUniformBufferAlignment - 1U);
        };
        EnsureFrameUniformCapacity(
            alignedUniform(draw.VertexUniformBytes.size()) +
                (alphaTested ? alignedUniform(draw.FragmentUniformBytes.size()) : 0U),
            mUniformBufferAlignment);
        const auto uploadUniform = [&](std::span<const uint8_t> bytes) {
            const VkDeviceSize offset = alignedUniform(frame.UniformBytesUsed);
            std::memcpy(static_cast<uint8_t*>(frame.UniformBuffer.Mapped) + offset, bytes.data(), bytes.size());
            frame.UniformBytesUsed = offset + bytes.size();
            return offset;
        };
        const VkDeviceSize vertexUniformOffset = uploadUniform(draw.VertexUniformBytes);
        const VkDeviceSize fragmentUniformOffset = alphaTested ? uploadUniform(draw.FragmentUniformBytes) : 0U;

        uint32_t booleanUniforms = 0;
        std::memcpy(&booleanUniforms, draw.VertexUniformBytes.data() + Oot3d::kPicaPackedVertexBooleanMaskOffset,
                    sizeof(booleanUniforms));
        Renderer3ds::PicaShadowCasterCache transientCache;
        auto& shadowCache = resolution.Cacheable
            ? mNativePicaGeometryBuffers.at(geometry.Identity).ShadowCaster : transientCache;
        const uint64_t version = resolution.Cacheable
            ? geometry.ContentVersion ^ (geometry.StructuralSignature * 0x9e3779b97f4a7c15ULL) ^
              geometry.Identity ^ draw.VertexShaderKey : 0;
        auto shadowCaster = Renderer3ds::BuildPicaShadowCaster(
            {geometry.Payload, bindings, layout.Attributes, layout.Bindings, placement.BaseOffset,
             geometry.IndexOffset, geometry.VertexOrIndexCount, draw.BaseVertex, geometry.Indexed},
            draw.TemporalVertexProgram.Hooks, booleanUniforms,
            draw.VertexUniformBytes.subspan(Oot3d::kPicaPackedVertexFloatOffset), viewToWorld, version, shadowCache);
        shadowCaster.Dynamic = draw.Composition.NativeActorGeometry;
        shadowCaster.OwnerIdentity = draw.Composition.NativeActorIdentity;
        std::array<Oot3d::PicaSceneTextureBinding, 3> sceneTextures{};
        Oot3d::PicaSceneTextureBinding sceneLightingLut;
        if (alphaTested) {
            std::array<uint64_t, 3> replacementHashes{};
            for (size_t slot = 0; slot < sceneTextures.size(); ++slot) {
                const auto* texture = casterTextures[slot];
                auto& destination = sceneTextures[slot];
                destination.NativeImageHandle = ToPicaSceneNativeHandle(texture->NriImage);
                destination.ReplacementContentHash = texture->CustomReplacementPixelHash;
                destination.ImageWidth = texture->Width;
                destination.ImageHeight = texture->Height;
                destination.MipLevels = texture->UploadedMipLevels;
                destination.Slot = static_cast<uint8_t>(slot);
                destination.CustomReplacement = texture->CustomReplacementApplied;
                replacementHashes[slot] = texture->CustomReplacementPixelHash;
            }
            for (const auto& texture : draw.Textures) {
                auto& destination = sceneTextures[texture.Slot];
                destination.NativeContentHash = texture.NativeContentHash;
                destination.NativeContentHashAvailable = texture.NativeContentHashAvailable;
                destination.PhysicalAddress = texture.PhysicalAddress;
                destination.SourceWidth = texture.Width;
                destination.SourceHeight = texture.Height;
                destination.LodBiasRaw = texture.LodBiasRaw;
                destination.NativeFormat = texture.NativeFormat;
                destination.NativeType = texture.NativeType;
                destination.NativeWrapS = texture.NativeWrapS;
                destination.NativeWrapT = texture.NativeWrapT;
                destination.MinMipLevel = texture.MinMipLevel;
                destination.MaxMipLevel = texture.MaxMipLevel;
                destination.MinLinear = texture.MinLinear;
                destination.MagLinear = texture.MagLinear;
                destination.MipLinear = texture.MipLinear;
                destination.Bound = true;
            }
            if (casterLightingLut != nullptr) {
                sceneLightingLut.NativeImageHandle = ToPicaSceneNativeHandle(casterLightingLut->NriImage);
                sceneLightingLut.ImageWidth = casterLightingLut->Width;
                sceneLightingLut.ImageHeight = casterLightingLut->Height;
                sceneLightingLut.MipLevels = 1U;
                sceneLightingLut.ImageFormat = Oot3d::PicaSceneTextureImageFormat::R32Uint;
                sceneLightingLut.Bound = true;
            }
            // The cutout is part of the caster's content: a changed texture or
            // alpha state invalidates the pages it touches.
            if (shadowCaster.ContentVersion != 0U) {
                shadowCaster.ContentVersion ^= HashAlphaCasterMaterial(draw, replacementHashes) +
                    0x9e3779b97f4a7c15ULL + (shadowCaster.ContentVersion << 6U) + (shadowCaster.ContentVersion >> 2U);
            }
        }

        // Only what the map consumes is recorded: the canonical programs stand
        // in for the effective ones. Fragment state and textures are recorded
        // for an alpha-tested caster only; no resolved raster/target value is
        // known before the ordered submission.
        Oot3d::PicaSceneDrawRecordDesc record{
            .SubmissionId = draw.SubmissionId,
            .CommandListAddress = draw.CommandListAddress,
            .CommandListOffsetWords = draw.CommandListOffsetWords,
            .CompositionDomain = draw.CompositionDomain,
            .Composition = draw.Composition,
            .LightingWorldToView = draw.LightBinding.WorldToView,
            .SceneDirectionals = draw.LightBinding.SceneDirectionals,
            .ScenePoints = draw.LightBinding.ScenePoints,
            .NativeLightSources = draw.LightBinding.Sources,
            .ShadowCaster = shadowCaster,
            .Raster = {
                .SchemaVersion = Oot3d::kPicaSceneResolvedRasterStateSchemaVersion,
                .NativeViewportX = draw.ViewportX,
                .NativeViewportY = draw.ViewportY,
                .NativeViewportWidth = draw.ViewportWidth,
                .NativeViewportHeight = draw.ViewportHeight,
                .DepthRange = draw.DepthRange,
                .NearPlane = draw.NearPlane,
                .ScissorMode = draw.ScissorMode,
                .NativeScissorX1 = draw.ScissorX1,
                .NativeScissorY1 = draw.ScissorY1,
                .NativeScissorX2 = draw.ScissorX2,
                .NativeScissorY2 = draw.ScissorY2,
                .WBuffering = draw.WBuffering,
                .FramebufferFlipped = draw.FramebufferFlipped,
            },
            .RenderTarget = {
                .SchemaVersion = Oot3d::kPicaSceneRenderTargetStateSchemaVersion,
                .RenderTargetNamespace = draw.RenderTargetNamespace,
                .ColorPhysicalAddress = draw.FramebufferColorPhysicalAddress,
                .DepthPhysicalAddress = draw.FramebufferDepthPhysicalAddress,
                .NativeWidth = draw.FramebufferWidth,
                .NativeHeight = draw.FramebufferHeight,
                .NativeColorFormat = draw.FramebufferColorFormat,
                .NativeDepthFormat = draw.FramebufferDepthFormat,
                .SampleCount = static_cast<uint32_t>(mNativePicaSampleCount),
            },
            .CanonicalDescriptorSchemaVersion = draw.CanonicalDescriptorSchemaVersion,
            .CanonicalVertexProgramId = draw.CanonicalVertexProgramId,
            .CanonicalFragmentProgramId = draw.CanonicalFragmentProgramId,
            .CanonicalRasterStateId = draw.CanonicalRasterStateId,
            .CanonicalPipelineId = draw.CanonicalPipelineId,
            .CanonicalDynamicStateId = draw.CanonicalDynamicStateId,
            .CanonicalFullRegisterStateId = draw.CanonicalFullRegisterStateId,
            .VertexShaderKey = draw.VertexShaderKey,
            .VertexShaderSource = draw.VertexShaderSource,
            .FragmentShaderKey = draw.FragmentShaderKey,
            .FragmentShaderSource = draw.FragmentShaderSource,
            .EffectiveVertexShaderKey = draw.VertexShaderKey,
            .EffectiveVertexShaderSource = draw.VertexShaderSource,
            .EffectiveFragmentShaderKey = draw.FragmentShaderKey,
            .EffectiveFragmentShaderSource = draw.FragmentShaderSource,
            .GeometryIdentity = draw.GeometryIdentity,
            .GeometryContentVersion = draw.GeometryContentVersion,
            .GeometryIdentityAvailable = draw.GeometryIdentityAvailable,
            .VertexLayoutBindings = layout.Bindings,
            .VertexLayoutAttributes = layout.Attributes,
            .Material = {
                .SchemaVersion = Oot3d::kPicaSceneResolvedMaterialStateSchemaVersion,
                .Topology = ToPicaSceneTopology(draw.Topology),
                .CullMode = ToPicaSceneCullMode(draw),
                .FrontFace = ToPicaSceneFrontFace(draw),
                .FragmentOperationMode = draw.FragmentOperationMode,
                .ColorWriteMask = draw.ColorWriteMask,
                .LogicOperation = draw.LogicOperation,
                .Blend = draw.Blend,
                .AlphaTestEnabled = draw.AlphaTestEnabled,
                .AlphaCompare = draw.AlphaCompare,
                .AlphaReference = draw.AlphaReference,
                .DepthTestEnabled = draw.DepthTestEnabled,
                .DepthWriteEnabled = draw.DepthWriteEnabled,
                .DepthCompare = draw.DepthCompare,
                .Stencil = draw.Stencil,
                .FragmentFeatures = draw.FragmentFeatures,
            },
            .UniformBuffer = {
                ToPicaSceneNativeHandle(frame.UniformBuffer.Buffer),
                frame.UniformBuffer.Size,
            },
            .VertexUniformOffset = vertexUniformOffset,
            .VertexUniformSize = draw.VertexUniformBytes.size(),
            .GeometryBuffer = {
                ToPicaSceneNativeHandle(placement.Buffer),
                placement.Size,
            },
            .VertexBindings = bindings,
            .Indexed = draw.Indexed,
            .IndexOffset = placement.BaseOffset + geometry.IndexOffset,
            .VertexOrIndexCount = draw.Indexed ? geometry.VertexOrIndexCount : draw.VertexCount,
            .BaseVertex = draw.BaseVertex,
            .PerspectiveProjection = true,
            .DirectionalShadowCaster = true,
            .VertexShaderHooks = draw.TemporalVertexProgram.Hooks,
            .PackedVertexUniforms = draw.VertexUniformBytes,
            .VertexSourceIdentity = draw.VertexShaderSourceIdentity,
            .FragmentSourceIdentity = draw.FragmentShaderSourceIdentity,
            .EffectiveVertexSourceIdentity = draw.VertexShaderSourceIdentity,
            .EffectiveFragmentSourceIdentity = draw.FragmentShaderSourceIdentity,
        };
        if (alphaTested) {
            record.FragmentUniformOffset = fragmentUniformOffset;
            record.FragmentUniformSize = draw.FragmentUniformBytes.size();
            record.Textures = sceneTextures;
            record.LightingLut = sceneLightingLut;
            record.FragmentShaderHooks = draw.FragmentShaderHooks;
            record.PackedFragmentUniforms = draw.FragmentUniformBytes;
            // The parametric base of the always-available pair evaluates the
            // same TEV and alpha test from the packed uniforms: its caster is
            // one of a finite built-in family.
            if (!draw.UberFragmentShaderSource.empty() &&
                draw.UberFragmentShaderHooks.ValidFor(draw.UberFragmentShaderSource)) {
                record.FragmentShaderKey = draw.UberFragmentShaderKey;
                record.FragmentShaderSource = draw.UberFragmentShaderSource;
                record.EffectiveFragmentShaderKey = draw.UberFragmentShaderKey;
                record.EffectiveFragmentShaderSource = draw.UberFragmentShaderSource;
                record.FragmentSourceIdentity = draw.UberFragmentShaderSourceIdentity;
                record.EffectiveFragmentSourceIdentity = draw.UberFragmentShaderSourceIdentity;
                record.FragmentShaderHooks = draw.UberFragmentShaderHooks;
            }
        }
        const auto status = mPreparedShadowCasterFrame.Record(record);
        const bool caster = status == Oot3d::PicaSceneRecordStatus::Recorded &&
            mPreparedShadowCasterFrame.DirectionalShadowCaster(mPreparedShadowCasterFrame.Draws().size() - 1U);
        if (caster) {
            mPreparedShadowCasters.insert_or_assign(draw.SubmissionId, shadowCaster);
        }
        mDiagnostics.RecordNriDirectionalShadowPreparation(caster, !resolution.Cacheable);
        return true;
    } catch (const std::exception& exception) {
        SetNativeError(error, exception.what());
        return false;
    }
}

bool GfxRenderingAPIVulkan::SubmitPicaDraw(
    const GfxNativePicaDrawView& draw, std::string* error) {
    Renderer3ds::SlowStepScope slowStep("pica_draw");
    try {
        const bool collectCpuTimings = mDiagnostics.Enabled();
        using CpuClock = std::chrono::steady_clock;
        auto cpuStart = CpuClock::time_point{};
        auto cpuStageStart = CpuClock::time_point{};
        Oot3dVulkanNativePicaCpuTimings cpuTimings;
        if (collectCpuTimings) {
            cpuStart = CpuClock::now();
            cpuStageStart = cpuStart;
            cpuTimings.DrawCount = 1U;
        }
        const auto finishCpuStage =
            [&](double& destinationMilliseconds) {
                if (!collectCpuTimings) {
                    return;
                }
                const auto now = CpuClock::now();
                destinationMilliseconds +=
                    std::chrono::duration<double, std::milli>(
                        now - cpuStageStart).count();
                cpuStageStart = now;
            };
        if (!mFrameActive) {
            throw std::runtime_error(
                "native PICA draw submitted outside an active Vulkan frame");
        }
        if (draw.VertexShaderSource.empty() ||
            draw.FragmentShaderSource.empty() || draw.VertexCount == 0 ||
            draw.VertexBindings.empty() || draw.VertexAttributes.empty()) {
            throw std::runtime_error("native PICA draw is incomplete");
        }
        if (mPicaAotShaderPack.Loaded() &&
            (draw.CanonicalDescriptorSchemaVersion == 0U ||
             draw.CanonicalDescriptorSchemaVersion !=
                 mPicaAotShaderPack.DescriptorSchemaVersion())) {
            throw std::runtime_error(
                "native PICA draw descriptor schema does not match the "
                "AOT shader pack");
        }
        if (draw.FragmentOperationMode != 0U &&
            draw.FragmentOperationMode != 3U) {
            throw std::runtime_error(
                "native PICA non-default fragment operation is unsupported");
        }
        if (draw.ScissorMode == 1U) {
            throw std::runtime_error(
                "native PICA exclude scissor requires shader emulation");
        }
        if (draw.ScissorMode != 0U && draw.ScissorMode != 3U) {
            throw std::runtime_error("invalid native PICA scissor mode");
        }
        if (draw.FramebufferWidth == 0 || draw.FramebufferHeight == 0) {
            throw std::runtime_error("native PICA framebuffer dimensions are zero");
        }

        if (mNativePicaCompositionSchedule.Valid()) {
            std::string scheduleError;
            const bool matched =
                mNativePicaCompositionSchedule.ConsumeDraw(
                    Renderer3ds::BuildPicaCompositionDrawReference(draw),
                    &scheduleError);
            mDiagnostics.RecordPicaCompositionScheduleDraw(matched);
            if (!matched) {
                throw std::runtime_error(scheduleError);
            }
        }
        const bool sceneDraw =
            draw.CompositionDomain ==
                ::Oot3d::Renderer::PicaCompositionDomain::Scene;
        const bool opaqueWorldDraw =
            sceneDraw &&
            draw.Composition.Layer ==
                ::Oot3d::Renderer::PicaCompositionLayer::OpaqueWorld;
        const bool nativeMemoryOnly = mNativePicaCompositionSchedule.Purpose() ==
            Renderer3ds::PicaCompositionPurpose::NativeMemory;

        const auto& graphicsSettings = mFrameGraphicsSettings;
        if (!nativeMemoryOnly && !mNativeFidelityProfile && sceneDraw &&
            draw.Composition.NativeProjectedShadow &&
            Oot3d::UsesFullSceneLighting(graphicsSettings.Effects.SurfaceLighting)) {
            // Consume native ordering and its graph boundaries, but do not
            // rasterize the legacy projected shadow into presentation or guides.
            // The canonical native-memory replay remains untouched.
            auto& target = GetOrCreateNativePicaRenderTarget(draw);
            ExecuteNativePicaBeforeDrawAnchors(target, draw,
                cpuTimings.DirectionalShadowMilliseconds, cpuTimings.GrassRenderMilliseconds);
            return true;
        }
        const uint64_t azaharTextureGeneration =
            mFrameAzaharTextureGeneration;
        // Material guide values: screen-space reflections or GI specular
        // (Brixelizer GI: a reflection source resolved through the same guide).
        const bool reflectionsEnabled = !nativeMemoryOnly &&
            (graphicsSettings.Effects.Reflections !=
                 Oot3d::ReflectionMode::Off ||
             (graphicsSettings.Effects.GlobalIllumination.Mode ==
                  Oot3d::GlobalIlluminationMode::BrixelizerGi &&
              graphicsSettings.Effects.GlobalIllumination.Specular));
        std::array<Oot3d::ReflectionMaterialTextureIdentity, 3U>
            reflectionTextureIdentities{};
        size_t reflectionTextureCount = 0U;
        if (reflectionsEnabled) {
            for (const auto& texture : draw.Textures) {
                const auto contentHash =
                    ResolveNativeTextureBaseLevelContentHash(texture);
                if (!contentHash.has_value() || texture.NativeType != 0U ||
                    texture.Slot >= 3U ||
                    reflectionTextureCount >=
                        reflectionTextureIdentities.size()) {
                    continue;
                }
                reflectionTextureIdentities[
                    reflectionTextureCount++] = {
                    *contentHash,
                    texture.Width,
                    texture.Height,
                    texture.Slot,
                };
            }
        }
        std::optional<Oot3d::ResolvedReflectionMaterialProfile>
            resolvedReflectionMaterial;
        if (reflectionsEnabled) {
            resolvedReflectionMaterial =
                Oot3d::ResolveReflectionMaterialProfile(
                    graphicsSettings.Effects.ReflectionMaterials,
                    std::span(
                        reflectionTextureIdentities.data(),
                        reflectionTextureCount));
        }
        const bool grassPerspectiveProjection =
            Oot3d::UsesPicaPerspectiveProjection(
                draw.VertexUniformBytes);
        // A draw the title lights as an object of its own keeps its native
        // lights in the full per-pixel model and receives none of the scene's
        // environment profile, shadows, GI or ambient occlusion.
        const bool isolatedLighting = !nativeMemoryOnly && draw.Composition.IsolatedLighting &&
            Oot3d::UsesFullSceneLighting(graphicsSettings.Effects.SurfaceLighting);
        const bool directionalShadowReceiver = !nativeMemoryOnly && !isolatedLighting &&
            Oot3d::UsesFullSceneLighting(graphicsSettings.Effects.SurfaceLighting) &&
            mDirectionalShadowMapBarrierPlan.Valid() &&
            Oot3d::PicaExtensionPassEnabled(
                mPicaExtensionGraph,
                Oot3d::PicaExtensionPass::DirectionalShadowLighting) &&
            grassPerspectiveProjection;
        // Point lights with a cube map: only NRI-owned draws bind the atlas.
        const bool pointShadowReceiver = !nativeMemoryOnly && !mNativeFidelityProfile && !isolatedLighting &&
            graphicsSettings.Effects.PointShadows.Mode == Oot3d::PointShadowMode::CubeMaps &&
            Oot3d::UsesFullSceneLighting(graphicsSettings.Effects.SurfaceLighting) &&
            mPointShadowMapBarrierPlan.Valid() &&
            Oot3d::PicaExtensionPassEnabled(
                mPicaExtensionGraph, Oot3d::PicaExtensionPass::PointShadowLighting) &&
            mNriPicaPipelineBridge.OwnedDrawsEnabled() && grassPerspectiveProjection;
        Oot3d::NriPointShadowHistorySnapshot pointShadowHistory;
        Renderer3ds::PicaPointShadowAssignment pointShadowAssignment;
        if (pointShadowReceiver) {
            pointShadowHistory = mNriDirectionalShadowPass.FindLatestPointHistory(
                draw.RenderTargetNamespace, draw.FramebufferColorPhysicalAddress, mFrameCounter);
            if (pointShadowHistory.Valid()) {
                const auto& layout = pointShadowHistory.Layout;
                for (uint32_t row = 0; row < layout.SlotCount; ++row)
                    if (pointShadowHistory.Slots[row].Valid())
                        pointShadowAssignment.Identities[row] = pointShadowHistory.Slots[row].SourceIdentity;
                pointShadowAssignment.Parameters = {
                    pointShadowHistory.Range, pointShadowHistory.Strength,
                    static_cast<float>(layout.FaceTexels),
                    static_cast<float>(Renderer3ds::kPointShadowBorderTexels) };
                pointShadowAssignment.Atlas = {
                    1.0F / static_cast<float>(layout.Width), 1.0F / static_cast<float>(layout.Height),
                    Renderer3ds::kPointShadowNearFraction, 1.0F };
            }
        }
        std::optional<Renderer3ds::PicaSurfaceLightingUniforms> surfaceLighting;
        const auto* environmentWorldToView = draw.LightBinding.WorldToView
            ? &*draw.LightBinding.WorldToView : nullptr;
        Oot3d::DirectionalShadowMatrix environmentViewToWorld{};
        if (!nativeMemoryOnly && !mNativeFidelityProfile &&
            graphicsSettings.Effects.SurfaceLighting != Oot3d::SurfaceLightingMode::Native &&
            draw.CompositionDomain == Renderer3ds::PicaCompositionDomain::Scene &&
            Renderer3ds::PicaCompositionReceivesSurfaceLighting(draw.Composition.Layer) &&
            grassPerspectiveProjection && draw.FragmentOperationMode != 3U &&
            draw.VertexUniformBytes.size() >= Oot3d::kPicaPackedVertexFloatOffset) {
            uint32_t booleanUniforms = 0U;
            std::memcpy(&booleanUniforms, draw.VertexUniformBytes.data() +
                        Oot3d::kPicaPackedVertexBooleanMaskOffset, sizeof(booleanUniforms));
            if (environmentWorldToView &&
                (!mSurfaceLightingView || mSurfaceLightingView->WorldToView != *environmentWorldToView))
                mSurfaceLightingView = Renderer3ds::BuildPicaSurfaceLightingView(*environmentWorldToView);
            // The scene environment profile, or the native environment for an
            // isolated draw.
            auto isolatedEnvironment = mFrameEnvironment;
            isolatedEnvironment.Enabled = false;
            surfaceLighting = Renderer3ds::BuildPicaSurfaceLightingUniforms(
                draw.TemporalVertexProgram.Hooks, booleanUniforms,
                draw.VertexUniformBytes.subspan(Oot3d::kPicaPackedVertexFloatOffset),
                draw.LightBinding, isolatedLighting ? isolatedEnvironment : mFrameEnvironment,
                environmentWorldToView && mSurfaceLightingView ? &*mSurfaceLightingView : nullptr,
                pointShadowHistory.Valid() ? &pointShadowAssignment : nullptr);
            if (surfaceLighting) {
                // Relative source weights (the guide's 8-bit radiance would
                // clip gains above one); the gain applies when GI is composed.
                const auto sources =
                    Oot3d::ResolveGlobalIlluminationSources(graphicsSettings.Effects.GlobalIllumination);
                surfaceLighting->GlobalIlluminationSources = isolatedLighting
                    ? std::array<float, 4>{}
                    : std::array<float, 4>{sources.Weights[0], sources.Weights[1], sources.Weights[2], 0.0F};
            }
            // A draw lit per pixel by native fragment lights already applies
            // the scene lights among them: they are not added a second time.
            if (surfaceLighting && surfaceLighting->Control[0] == 0U &&
                draw.FragmentFeatures.FragmentLightingEnabled) {
                const auto native = Oot3d::DecodePicaNativeDrawEnvironment(
                    draw.VertexUniformBytes, draw.FragmentUniformBytes, draw.FragmentFeatures).FragmentLighting;
                std::array<std::array<float, 3>, std::tuple_size_v<decltype(native.Lights)>> directions{};
                size_t directionCount = 0;
                for (size_t index = 0; index < native.Lights.size(); ++index)
                    if (native.Lights[index].Active && native.Lights[index].ValuesAvailable &&
                        native.Layout.Lights[index].Directional)
                        directions[directionCount++] = native.Lights[index].PositionOrDirectionView;
                (void)Renderer3ds::ExcludePicaSceneDirectionalsLitNatively(
                    *surfaceLighting, std::span(directions.data(), directionCount));
            }
            if (surfaceLighting && environmentWorldToView && directionalShadowReceiver &&
                !Oot3d::InvertDirectionalShadowMatrix(*environmentWorldToView, environmentViewToWorld))
                surfaceLighting.reset();
        }
        std::array<TextureRecord*, 3> textures{
            &mFallbackTexture, &mFallbackTexture, &mFallbackTexture};
        std::array<VkFormat, 3> textureFormats{
            VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM};
        std::array<bool, 3> integerTextures{};
        for (const auto& texture : draw.Textures) {
            if (texture.Slot >= textures.size()) {
                throw std::runtime_error("native PICA texture slot is invalid");
            }
            if (texture.NativeType != 0U &&
                !(texture.Slot == 0U &&
                  (texture.NativeType == 2U || texture.NativeType == 3U))) {
                throw std::runtime_error(
                    "native PICA texture type is unsupported by sampler2D");
            }
            const auto resolutionStart = std::chrono::steady_clock::now();
            MaterializeRestoredGpuTextureCopyProducers();
            TextureRecord* record =
                GetOrCreateNativePicaTexture(
                    texture, error, azaharTextureGeneration,
                    draw.RenderTargetNamespace,
                    texture.NativeContentHashAvailable
                        ? &texture.NativeContentHash
                        : nullptr);
            if (record == nullptr) {
                return false;
            }
            const auto finished = std::chrono::steady_clock::now();
            const auto elapsed = [](const auto& from, const auto& to) {
                return std::chrono::duration<double>(to - from).count();
            };
            if (texture.CopyReferences.empty() == false) {
                // Producer references resolved into the sampled image on the
                // GPU: no CPU hash of the copy's ranges, no decode of them and no
                // queue idle.
                mGpuTextureCopySamplerCost.BindSeconds +=
                    elapsed(resolutionStart, finished);
            } else {
                mGpuTextureCopySamplerCost.ResolveSeconds +=
                    elapsed(resolutionStart, finished);
            }
            textures[texture.Slot] = record;
            textureFormats[texture.Slot] =
                texture.NativeType == 2U ? VK_FORMAT_R32_UINT
                                         : VK_FORMAT_R8G8B8A8_UNORM;
            integerTextures[texture.Slot] =
                texture.NativeType == 2U;
        }
        TextureRecord* lightingLutTexture = &mFallbackTexture;
        bool lightingLutInteger = false;
        if (!draw.LightingLut.PackedEntries.empty()) {
            lightingLutTexture =
                GetOrCreateNativePicaLightingLut(draw.LightingLut, error);
            if (lightingLutTexture == nullptr) {
                return false;
            }
            lightingLutInteger = true;
        }
        finishCpuStage(cpuTimings.TextureMilliseconds);
        const auto nativeTextures = textures;
        NativePicaMaterialBundle* pbrMaterial = nullptr;
        Renderer3ds::PicaPbrShaderOptions pbrOptions;
        std::optional<Renderer3ds::PicaPbrUniforms> pbrUniforms;
        std::optional<Renderer3ds::PicaPbrMaterialParameters> materialPreview;
        std::array<TextureRecord*, Renderer3ds::kPicaPbrTextureCount> pbrTextures;
        pbrTextures.fill(&mFallbackTexture);
        VkSampler pbrSampler = mFallbackTexture.Sampler;
        if (surfaceLighting &&
            Oot3d::UsesPbrMaterials(graphicsSettings.Effects.SurfaceLighting) &&
            draw.FragmentShaderHooks.ValidFor(draw.FragmentShaderSource) &&
            draw.FragmentShaderHooks.Supports(Renderer3ds::PicaShaderHook::AfterNativeTev) &&
            (!draw.FragmentFeatures.FragmentLightingEnabled ||
             (draw.FragmentFeatures.FragmentLighting.Valid() &&
              draw.FragmentUniformBytes.size() >= Oot3d::kPicaPackedFragmentLightingProgramOffset +
                  sizeof(Renderer3ds::PicaLightingProgram))) &&
            draw.FragmentUniformBytes.size() >= Oot3d::kPicaPackedFragmentTevProgramOffset +
                sizeof(Renderer3ds::PicaTevProgram)) {
            Renderer3ds::PicaTevProgram tev;
            std::memcpy(&tev, draw.FragmentUniformBytes.data() +
                Oot3d::kPicaPackedFragmentTevProgramOffset, sizeof(tev));
            const uint8_t colorMask = Renderer3ds::PicaTevMaterialTextureMask(tev) & 7U;
            uint8_t automaticMask = colorMask;
            if (draw.FragmentFeatures.FragmentLighting.BumpMode != Renderer3ds::PicaFragmentLightingBumpMode::None)
                automaticMask &= static_cast<uint8_t>(
                    ~(1U << draw.FragmentFeatures.FragmentLighting.BumpTextureUnit));
            std::shared_ptr<const ::Oot3d::Renderer::AzaharPbrMaterial> selected;
            uint8_t selectedSlot = 0xffU;
            for (uint8_t slot = 0; slot < 3U; ++slot) {
                const auto* sample = draw.FragmentShaderHooks.TextureSample(slot);
                if ((colorMask & (1U << slot)) == 0U || integerTextures[slot] || sample == nullptr ||
                    sample->Coordinate > 2U ||
                    (sample->Operation != Renderer3ds::PicaTextureCoordinateOperation::NativeVFlip &&
                     sample->Operation != Renderer3ds::PicaTextureCoordinateOperation::ProjectedNativeVFlip))
                    continue;
                const auto resolved = ::Oot3d::Renderer::AzaharTexturePackRuntime::Instance().
                    ResolveMaterialOrQueue(textures[slot]->CustomReplacementHash, azaharTextureGeneration);
                if (!resolved.Material) continue;
                const uint8_t explicitSlot = resolved.Preview
                    ? resolved.Preview->TextureSlot : resolved.Material->Parameters.TextureSlot;
                if ((explicitSlot == 0xffU && automaticMask != (1U << slot)) ||
                    (explicitSlot != 0xffU && explicitSlot != slot)) continue;
                if (selected) {
                    selected.reset();
                    break; // Two authored material selectors are ambiguous, not an ordering rule.
                }
                selected = resolved.Material;
                materialPreview = resolved.Preview;
                selectedSlot = slot;
            }
            if (selected) pbrMaterial = GetOrCreateNativePicaMaterial(selected);
            if (pbrMaterial != nullptr) {
                pbrOptions.TextureSlot = selectedSlot;
                pbrOptions.NativeFragmentLighting = draw.FragmentFeatures.FragmentLightingEnabled;
                const auto& layout = draw.FragmentFeatures.FragmentLighting;
                for (size_t slot = 0; slot < layout.ActiveLightCount; ++slot) {
                    const auto& light = layout.Lights[layout.LightPermutation[slot]];
                    pbrOptions.NativeAttenuationLuts |= light.SpotAttenuationEnabled || light.DistanceAttenuationEnabled;
                }
                Renderer3ds::PicaLightingProgram lighting;
                if (draw.FragmentUniformBytes.size() >= Oot3d::kPicaPackedFragmentLightingProgramOffset + sizeof(lighting))
                    std::memcpy(&lighting, draw.FragmentUniformBytes.data() +
                        Oot3d::kPicaPackedFragmentLightingProgramOffset, sizeof(lighting));
                uint8_t mapMask = 0U;
                for (size_t role = 0; role < pbrTextures.size(); ++role) {
                    if (!pbrMaterial->Material->Textures[role]) continue;
                    mapMask |= static_cast<uint8_t>(1U << role);
                    pbrTextures[role] = &pbrMaterial->Images[role];
                }
                pbrUniforms = Renderer3ds::BuildPicaPbrUniforms(
                    materialPreview ? *materialPreview : pbrMaterial->Material->Parameters, mapMask, lighting);
                pbrOptions.SeamSoftening = mPbrSeamsAvailable && pbrUniforms->SeamSettings[0] > 0 &&
                    std::bit_cast<uint32_t>(pbrUniforms->SeamSettings[2]) != 0 &&
                    draw.FragmentShaderHooks.TextureSample(selectedSlot)->Operation ==
                        Renderer3ds::PicaTextureCoordinateOperation::NativeVFlip;
                pbrSampler = nativeTextures[selectedSlot]->Sampler;
                resolvedReflectionMaterial.reset();
            }
        }
        if (graphicsSettings.Effects.SurfaceLighting == Oot3d::SurfaceLightingMode::NativePbr &&
            pbrMaterial == nullptr)
            surfaceLighting.reset();
        // AO texture relief reads the one texture whose colour the draw's TEV
        // passes to its output (never a bump or shadow unit); several colour
        // textures leave the draw without relief rather than guess a layer.
        uint8_t reliefTexture = 0xffU;
        if (!nativeMemoryOnly && sceneDraw && draw.DepthWriteEnabled &&
            graphicsSettings.Effects.AmbientOcclusion != Oot3d::AmbientOcclusionMode::Off &&
            graphicsSettings.Effects.AoRelief != Oot3d::AoTextureRelief::Off &&
            draw.FragmentUniformBytes.size() >= Oot3d::kPicaPackedFragmentTevProgramOffset +
                sizeof(Renderer3ds::PicaTevProgram)) {
            Renderer3ds::PicaTevProgram tev;
            std::memcpy(&tev, draw.FragmentUniformBytes.data() +
                Oot3d::kPicaPackedFragmentTevProgramOffset, sizeof(tev));
            uint8_t colorTextures = Renderer3ds::PicaTevMaterialTextureMask(tev) & 7U;
            if (draw.FragmentFeatures.FragmentLighting.BumpMode != Renderer3ds::PicaFragmentLightingBumpMode::None)
                colorTextures &= static_cast<uint8_t>(
                    ~(1U << draw.FragmentFeatures.FragmentLighting.BumpTextureUnit));
            for (uint8_t slot = 0; slot < 3U; ++slot)
                if (integerTextures[slot]) colorTextures &= static_cast<uint8_t>(~(1U << slot));
            if (std::has_single_bit(colorTextures)) {
                const auto slot = static_cast<uint8_t>(std::countr_zero(colorTextures));
                const auto* sample = draw.FragmentShaderHooks.TextureSample(slot);
                if (sample != nullptr &&
                    sample->Operation == Renderer3ds::PicaTextureCoordinateOperation::NativeVFlip)
                    reliefTexture = slot;
            }
            mDiagnostics.RecordAoReliefDraw(reliefTexture < 3U);
        }
        finishCpuStage(cpuTimings.TextureMilliseconds);
        const Oot3d::PicaShaderPipelineRequest variantRequest{
            draw.VertexShaderSource,
            draw.VertexShaderKey,
            draw.FragmentShaderSource,
            draw.FragmentShaderKey,
            {
                draw.FramebufferWidth,
                draw.FramebufferHeight,
                draw.FragmentOperationMode,
                draw.DepthTestEnabled,
                draw.DepthWriteEnabled,
                draw.Blend,
                draw.ColorWriteMask,
                grassPerspectiveProjection,
                directionalShadowReceiver,
                draw.CompositionDomain,
                draw.DepthCompare,
                draw.Composition.Layer,
                pointShadowReceiver && surfaceLighting.has_value(),
                isolatedLighting,
            },
            mTemporalMotionEnabled && !nativeMemoryOnly,
            resolvedReflectionMaterial.has_value()
                ? std::optional<Oot3d::ReflectionMaterialParameters>(
                      resolvedReflectionMaterial->Parameters)
                : std::nullopt,
            mNativeFidelityProfile || nativeMemoryOnly,
            draw.TemporalVertexProgram,
            draw.FragmentShaderHooks,
            draw.VertexShaderSourceIdentity,
            draw.FragmentShaderSourceIdentity,
            surfaceLighting.has_value(),
            pbrOptions,
            reliefTexture,
        };
        static const Oot3d::EffectsSettings nativeMemoryEffects;
        bool variantCacheHit = false;
        const auto& shaderVariant =
            mPicaShaderPipelineCache.Resolve(
                variantRequest,
                nativeMemoryOnly ? nativeMemoryEffects : graphicsSettings.Effects,
                &variantCacheHit);
        if (pbrMaterial != nullptr && pbrMaterial->Material->ColorReplacement &&
            Oot3d::HasPicaShaderInstrumentationFeature(
                shaderVariant.AppliedFeatures, Oot3d::PicaShaderInstrumentationFeature::PbrMaterial))
            textures[pbrOptions.TextureSlot] = &pbrMaterial->Images[Renderer3ds::kPicaPbrTextureCount];
        const auto variantCacheStats =
            mPicaShaderPipelineCache.Stats();
        mDiagnostics.RecordPicaShaderSelection(
            shaderVariant.IsCanonical(),
            shaderVariant.RequestedFeatures !=
                Oot3d::PicaShaderInstrumentationFeature::None,
            static_cast<uint32_t>(std::min<size_t>(
                variantCacheStats.CanonicalEntries,
                std::numeric_limits<uint32_t>::max())),
            static_cast<uint32_t>(std::min<size_t>(
                variantCacheStats.InstrumentationEntries,
                std::numeric_limits<uint32_t>::max())),
            shaderVariant.DirectFragmentHooksUsed,
            Oot3d::HasPicaShaderInstrumentationFeature(
                shaderVariant.RequestedFeatures,
                Oot3d::PicaShaderInstrumentationFeature::TemporalVertex),
            shaderVariant.DirectTemporalVertexProgramUsed,
            static_cast<uint32_t>(std::min<uint64_t>(
                variantCacheStats.CanonicalOutputAudits,
                std::numeric_limits<uint32_t>::max())),
            static_cast<uint32_t>(std::min<uint64_t>(
                variantCacheStats.CanonicalOutputContractRejects,
                std::numeric_limits<uint32_t>::max())),
            static_cast<uint32_t>(std::min<uint64_t>(
                variantCacheStats.CanonicalCompatibilityAnalyses,
                std::numeric_limits<uint32_t>::max())),
            static_cast<uint32_t>(std::min<uint64_t>(
                variantCacheStats.TypedInstrumentationContractRejects,
                std::numeric_limits<uint32_t>::max())));
        cpuTimings.ShaderVariantCacheHits =
            variantCacheHit ? 1U : 0U;
        cpuTimings.ShaderVariantCacheMisses =
            variantCacheHit ? 0U : 1U;
        cpuTimings.ShaderVariantCacheEntries =
            static_cast<uint32_t>(std::min<size_t>(
                variantCacheStats.CanonicalEntries +
                    variantCacheStats.InstrumentationEntries,
                std::numeric_limits<uint32_t>::max()));
        GfxNativePicaDrawView effectiveDraw = draw;
        effectiveDraw.VertexShaderSource =
            shaderVariant.VertexShaderSource;
        effectiveDraw.VertexShaderKey =
            shaderVariant.VertexShaderKey;
        effectiveDraw.VertexShaderSourceIdentity =
            shaderVariant.VertexShaderSourceIdentity;
        effectiveDraw.FragmentShaderSource =
            shaderVariant.FragmentShaderSource;
        effectiveDraw.FragmentShaderKey =
            shaderVariant.FragmentShaderKey;
        effectiveDraw.FragmentShaderSourceIdentity =
            shaderVariant.FragmentShaderSourceIdentity;
        mPicaEffectiveShaderInventory.Observe(
            Oot3d::PicaAotShaderStage::Vertex,
            effectiveDraw.VertexShaderSource,
            effectiveDraw.CanonicalDescriptorSchemaVersion,
            effectiveDraw.CanonicalPipelineId,
            effectiveDraw.VertexShaderKey,
            mFrameGraphicsSettingsRevision);
        mPicaEffectiveShaderInventory.Observe(
            Oot3d::PicaAotShaderStage::Fragment,
            effectiveDraw.FragmentShaderSource,
            effectiveDraw.CanonicalDescriptorSchemaVersion,
            effectiveDraw.CanonicalPipelineId,
            effectiveDraw.FragmentShaderKey,
            mFrameGraphicsSettingsRevision);

        Oot3d::TraceOutlineDraw(mFrameCounter, draw, grassPerspectiveProjection,
                               shaderVariant.FragmentOutputs.WritesOutlineGeometryGuide);
        static const bool toonDiagnosticsEnabled = [] {
            const char* value =
                std::getenv("OOT3D_GRAPHICS_TOON_DIAGNOSTICS");
            return value != nullptr &&
                   std::string_view(value) == "1";
        }();
        if (toonDiagnosticsEnabled) {
            static uint32_t diagnosticDrawCount = 0;
            if (diagnosticDrawCount++ < 24U) {
                std::fprintf(stderr,
                             "OOT3D_TOON draw=%u mode=%u target=%ux%u depth=%u/%u blend=%u mask=%02x eligibility=%u "
                             "applied=%u\n",
                    diagnosticDrawCount,
                    static_cast<unsigned>(graphicsSettings.Effects.Toon),
                    draw.FramebufferWidth, draw.FramebufferHeight,
                    draw.DepthTestEnabled ? 1U : 0U,
                    draw.DepthWriteEnabled ? 1U : 0U,
                    draw.Blend.Enabled ? 1U : 0U, draw.ColorWriteMask,
                    static_cast<unsigned>(
                        shaderVariant.ToonEligibility),
                    shaderVariant.ToonEligibility ==
                            Oot3d::PicaToonEligibility::Eligible
                        ? 1U
                        : 0U);
            }
        }
        if (graphicsSettings.Effects.Toon != Oot3d::ToonMode::Off) {
            auto& telemetry = Oot3d::PicaToonTelemetry::Instance();
            const uint64_t telemetryTotal =
                telemetry.Record(shaderVariant.ToonEligibility);
            if (shaderVariant.ToonEligibility ==
                Oot3d::PicaToonEligibility::Eligible) {
                mDiagnostics.RecordToonDraw(
                    shaderVariant.ToonMaterialPath);
            }
            if (telemetryTotal == 512U) {
                const auto snapshot = telemetry.Snapshot();
                spdlog::info(
                    "PICA toon classifier: applied={}, outside-scene={}, no-depth={}, blended={}, no-rgb={}, unsupported={}",
                    snapshot.Count(Oot3d::PicaToonEligibility::Eligible),
                    snapshot.Count(Oot3d::PicaToonEligibility::OutsideScene),
                    snapshot.Count(Oot3d::PicaToonEligibility::NoDepth),
                    snapshot.Count(Oot3d::PicaToonEligibility::Blended),
                    snapshot.Count(Oot3d::PicaToonEligibility::NoRgbOutput),
                    snapshot.Count(Oot3d::PicaToonEligibility::UnsupportedShader));
            }
        }
        if (Oot3d::HasPicaShaderInstrumentationFeature(
                shaderVariant.AppliedFeatures, Oot3d::PicaShaderInstrumentationFeature::NormalGuide))
            mDiagnostics.RecordNormalGuideDraw(
                shaderVariant.TypedNormalGuide,
                Oot3d::HasPicaShaderInstrumentationFeature(
                    shaderVariant.AppliedFeatures, Oot3d::PicaShaderInstrumentationFeature::NormalGuideTextureRelief));
        if (shaderVariant.AmbientGuideEligibility ==
                Oot3d::PicaAmbientOcclusionGuideEligibility::
                    AppliedExact ||
            shaderVariant.AmbientGuideEligibility ==
                Oot3d::PicaAmbientOcclusionGuideEligibility::
                    AppliedFallback) {
            mDiagnostics.RecordAmbientOcclusionAmbientGuideDraw(
                shaderVariant.AmbientGuideEligibility ==
                    Oot3d::PicaAmbientOcclusionGuideEligibility::
                        AppliedExact);
        }
        if (reflectionsEnabled) {
            const auto eligibility =
                shaderVariant.ReflectionEligibility;
            const bool profiled =
                eligibility ==
                Oot3d::PicaReflectionMaterialEligibility::
                    ExplicitTextureProfile;
            mDiagnostics.RecordReflectionMaterialDraw(
                eligibility ==
                        Oot3d::PicaReflectionMaterialEligibility::
                            Eligible ||
                    profiled,
                profiled,
                eligibility ==
                    Oot3d::PicaReflectionMaterialEligibility::
                        NoSpecularTevUse,
                eligibility ==
                    Oot3d::PicaReflectionMaterialEligibility::
                        UnsupportedShader);
            const bool worldMaterialCandidate =
                eligibility ==
                    Oot3d::PicaReflectionMaterialEligibility::Eligible ||
                eligibility ==
                    Oot3d::PicaReflectionMaterialEligibility::
                        ExplicitTextureProfile ||
                eligibility ==
                    Oot3d::PicaReflectionMaterialEligibility::
                        NoSpecularTevUse ||
                eligibility ==
                    Oot3d::PicaReflectionMaterialEligibility::
                        UnsupportedShader;
            if (worldMaterialCandidate) {
                for (size_t textureIndex = 0U;
                     textureIndex < reflectionTextureCount;
                     ++textureIndex) {
                    const auto& texture =
                        reflectionTextureIdentities[textureIndex];
                    const bool selectedProfile =
                        profiled &&
                        resolvedReflectionMaterial.has_value() &&
                        resolvedReflectionMaterial->TextureHash ==
                            texture.ContentHash &&
                        resolvedReflectionMaterial->MapperSlot ==
                            texture.MapperSlot;
                    mDiagnostics.RecordReflectionTextureUsage(
                        texture.ContentHash,
                        texture.Width,
                        texture.Height,
                        texture.MapperSlot,
                        selectedProfile,
                        selectedProfile
                            ? resolvedReflectionMaterial->RuleId
                            : 0U,
                        selectedProfile
                            ? static_cast<uint8_t>(
                                  resolvedReflectionMaterial->Profile)
                            : uint8_t{0},
                        selectedProfile
                            ? resolvedReflectionMaterial->Parameters
                                  .Reflectivity
                            : 0.0F,
                        selectedProfile
                            ? resolvedReflectionMaterial->Parameters
                                  .Roughness
                            : 1.0F);
                }
            }
            // Read once: the CRT environment lookup locks and scans per call.
            static const bool reflectionMaterialDiagnostics = [] {
                const char* value = std::getenv("OOT3D_GRAPHICS_REFLECTION_MATERIAL_DIAGNOSTICS");
                return value != nullptr && std::string_view(value) == "1";
            }();
            if (reflectionMaterialDiagnostics) {
                static uint32_t reflectionMaterialDiagnosticCount = 0;
                const bool explicitProfileMatched =
                    resolvedReflectionMaterial.has_value();
                if (reflectionMaterialDiagnosticCount++ < 24U ||
                    explicitProfileMatched) {
                    const auto shader =
                        effectiveDraw.FragmentShaderSource;
                    std::fprintf(
                        stderr,
                        "OOT3D_REFLECTION_MATERIAL draw=%u eligibility=%u "
                        "target=%ux%u depth=%u/%u mask=%02x guide=%u "
                        "signal=%u marker=%u secondary=%u "
                        "profile_matched=%u profile_applied=%u "
                        "texture=%016llx\n",
                        reflectionMaterialDiagnosticCount,
                        static_cast<unsigned>(eligibility),
                        draw.FramebufferWidth, draw.FramebufferHeight,
                        draw.DepthTestEnabled ? 1U : 0U,
                        draw.DepthWriteEnabled ? 1U : 0U,
                        draw.ColorWriteMask,
                        shader.find("pica_material_guide") !=
                            std::string_view::npos,
                        shader.find("oot3d_specular_signal") !=
                            std::string_view::npos,
                        shader.find(
                            "OOT3D_PICA_MATERIAL_TOON_POINT") !=
                            std::string_view::npos,
                        shader.find("secondary_fragment_color") !=
                            std::string_view::npos,
                        explicitProfileMatched ? 1U : 0U,
                        profiled ? 1U : 0U,
                        static_cast<unsigned long long>(
                            resolvedReflectionMaterial.has_value()
                                ? resolvedReflectionMaterial->TextureHash
                                : reflectionTextureCount == 0U
                                      ? 0U
                                      : reflectionTextureIdentities[0]
                                            .ContentHash));
                }
            }
        }
        if (shaderVariant.Reactive) {
            mDiagnostics.RecordReactiveWorldDraw();
        }
        finishCpuStage(cpuTimings.ShaderVariantMilliseconds);

        const auto shaderKey = Renderer3ds::BuildPicaShaderModuleIdentity(
            effectiveDraw.VertexShaderSourceIdentity,
            effectiveDraw.FragmentShaderSourceIdentity,
            effectiveDraw.CanonicalDescriptorSchemaVersion,
            mNriPicaPipelineBridge.Available(), shaderVariant.FragmentOutputs);
        auto& nativeShaderCache = shaderVariant.IsCanonical()
            ? mCanonicalNativePicaShaders
            : mInstrumentedNativePicaShaders;
        auto shaderIt = nativeShaderCache.find(shaderKey);
        if (shaderIt == nativeShaderCache.end()) {
            NativePicaShaderProgram shader;
            shader.VertexSource = Oot3d::IdentifyPicaAotShaderSource(
                effectiveDraw.VertexShaderSource);
            shader.FragmentSource = Oot3d::IdentifyPicaAotShaderSource(
                effectiveDraw.FragmentShaderSource);
            shader.FragmentOutputs = shaderVariant.FragmentOutputs;
            auto preparation = std::make_shared<NativePicaProgramPreparation>();
            preparation->Stem =
                "native_pica_" + std::to_string(effectiveDraw.VertexShaderKey) + "_" +
                std::to_string(effectiveDraw.FragmentShaderKey);
            const auto vertexArtifact = Renderer3ds::FindPicaVertexArtifact(
                effectiveDraw.VertexArtifacts, effectiveDraw.VertexShaderSource);
            if (!vertexArtifact.Spirv.empty()) {
                shader.NriVertexSpirv.assign(vertexArtifact.Spirv.begin(), vertexArtifact.Spirv.end());
                ++mNativeVertexArtifactHits;
            } else {
                preparation->ResolveVertex = true;
                preparation->VertexSource = std::string(effectiveDraw.VertexShaderSource);
            }
            preparation->Nri = mNriPicaPipelineBridge.Available();
            if (preparation->Nri)
                preparation->FragmentSource = std::string(effectiveDraw.FragmentShaderSource);
            preparation->DescriptorSchemaVersion = effectiveDraw.CanonicalDescriptorSchemaVersion;
            preparation->CanonicalPipelineId = effectiveDraw.CanonicalPipelineId;
            preparation->FragmentShaderKey = effectiveDraw.FragmentShaderKey;
            preparation->SettingsRevision = mFrameGraphicsSettingsRevision;
            if (preparation->ResolveVertex || preparation->Nri) {
                if (mPreparationQueue) {
                    preparation->Task = mPreparationQueue->Submit(
                        [this, preparation] { PrepareNativePicaProgram(*preparation); });
                    ++mNativePicaPreparationCounters.ProgramTasks;
                } else {
                    PrepareNativePicaProgram(*preparation);
                }
                shader.Preparation = std::move(preparation);
            }
            shaderIt = nativeShaderCache.emplace(shaderKey, std::move(shader)).first;
        }
        // Inline preparation has nothing to wait for; queued work is adopted
        // (or completed) when the draw selects its program below.
        if (!mPreparationQueue) AdoptNativePicaProgram(shaderIt->second, true);
        finishCpuStage(cpuTimings.ShaderCacheMilliseconds);

        if (!nativeMemoryOnly && mInteractiveGrassProviderPlan.Valid() && opaqueWorldDraw) {
            auto latestGrassPerspective =
                Oot3d::SceneViewRuntime::Instance()
                    .LatestPerspective();
            if (latestGrassPerspective)
                if (const auto projection = Oot3d::DecodePicaPerspectiveProjection(draw.VertexUniformBytes))
                    latestGrassPerspective = Oot3d::ApplySampleProjection(*latestGrassPerspective, *projection);
            if (latestGrassPerspective && draw.LightBinding.WorldToView)
                latestGrassPerspective = Oot3d::ResolveSamplePerspective(
                    *latestGrassPerspective, *draw.LightBinding.WorldToView);
            (void)mNativeSceneView.Publish(
                mFrameCounter, mPicaSceneFrame,
                latestGrassPerspective);
            (void)mPicaScenePublications.Publish(
                mFrameCounter, mPicaSceneFrame, mNativeSceneView);
            const auto* grassPerspective =
                mPicaScenePublications.ResolvePerspectiveCamera();
            const auto grassSourceDecision =
                mInteractiveGrassProviderPlan.ResolveInputs({
                    mPicaScenePublications.View(),
                });
            const bool grassSourcePublished =
                grassSourceDecision.Authorized() &&
                grassPerspective != nullptr &&
                PublishNativePicaGrassSurface(
                draw, mFrameCounter,
                mGrassSurfaceOccurrencesThisFrame,
                graphicsSettings.Grass,
                opaqueWorldDraw,
                shaderVariant.GrassTexture0Sampled,
                grassPerspectiveProjection,
                mGrassGeometryRegistry);
            mInteractiveGrassProviderExecution.RecordSourceInspection(
                grassSourceDecision, grassSourcePublished);
        }
        finishCpuStage(cpuTimings.GrassSurfaceMilliseconds);

        auto& renderTarget = GetOrCreateNativePicaRenderTarget(draw);
        finishCpuStage(cpuTimings.RenderTargetStateMilliseconds);
        ExecuteNativePicaBeforeDrawAnchors(renderTarget, draw,
            cpuTimings.DirectionalShadowMilliseconds, cpuTimings.GrassRenderMilliseconds);
        if (collectCpuTimings) cpuStageStart = CpuClock::now();
        renderTarget.WBuffering = draw.WBuffering;
        const bool reflectionEnvironmentEligible =
            sceneDraw &&
            draw.FragmentOperationMode == 0U &&
            draw.DepthTestEnabled && draw.DepthWriteEnabled &&
            (draw.ColorWriteMask & 0x7U) != 0U;
        renderTarget.ReflectionEnvironment.Observe(
            mFrameCounter, draw.FragmentShaderSource,
            draw.FragmentUniformBytes, reflectionEnvironmentEligible, draw.FragmentShaderKey);
        finishCpuStage(cpuTimings.ReflectionEnvironmentMilliseconds);
        cpuTimings.SceneStateMilliseconds =
            cpuTimings.RenderTargetStateMilliseconds +
            cpuTimings.DirectionalShadowMilliseconds +
            cpuTimings.GrassRenderMilliseconds +
            cpuTimings.ReflectionEnvironmentMilliseconds;
        Oot3d::RigidMotionSample rigidMotion{};
        const std::array<float, 2> jitterUv = mTemporalJitterEnabled && !nativeMemoryOnly
            ? std::array<float, 2>{
                  mTemporalJitterPixels[0] /
                      static_cast<float>(renderTarget.Width),
                  mTemporalJitterPixels[1] /
                      static_cast<float>(renderTarget.Height)}
            : std::array<float, 2>{};
        std::array<float, 4> rigidOriginClip{};
        const bool temporalEligible = mTemporalMotionEnabled && !nativeMemoryOnly &&
            sceneDraw &&
            draw.FragmentOperationMode == 0U &&
            draw.DepthTestEnabled && draw.DepthWriteEnabled;
        uint64_t temporalIdentity = draw.GeometryIdentityAvailable ? draw.GeometryIdentity : draw.VertexShaderKey;
        bool repeatedTemporalIdentity = false;
        if (temporalEligible) {
            const auto mixIdentity = [&temporalIdentity](uint64_t value) {
                temporalIdentity ^= value + 0x9e3779b97f4a7c15ULL +
                            (temporalIdentity << 6U) +
                            (temporalIdentity >> 2U);
            };
            if (!draw.GeometryIdentityAvailable) {
                for (const auto& binding : draw.VertexBindings) {
                    if (!binding.PerInstance) {
                        mixIdentity(HashNativeBytes(binding.Bytes));
                    }
                }
                mixIdentity(HashNativeBytes(draw.IndexBytes));
            }
            const uint32_t occurrence =
                mRigidMotionOccurrencesThisFrame[temporalIdentity]++;
            mixIdentity(occurrence);
            repeatedTemporalIdentity = occurrence != 0U;
            mixIdentity(draw.RenderTargetNamespace);
            if (temporalIdentity == 0U) temporalIdentity = 1U;
        }
        const bool rigidEligible = temporalEligible &&
            Oot3d::DecodeCommonRigidOriginClip(
                draw.VertexUniformBytes, draw.FramebufferFlipped,
                rigidOriginClip);
        if (rigidEligible) {
            rigidMotion = mRigidMotionTracker.Track(
                temporalIdentity, mFrameCounter,
                rigidOriginClip, jitterUv);
        }
        std::span<const uint8_t> previousVertexUniformBytes =
            draw.VertexUniformBytes;
        // Owns the previous sample's uniforms for this draw: the history entry
        // is replaced below, which would free the storage under the span.
        std::vector<uint8_t> previousVertexUniformStorage;
        std::array<float, 2> previousJitterNdc{
            2.0F * jitterUv[0], 2.0F * jitterUv[1]};
        bool exactMotionHistory = false;
        if (temporalEligible) {
            const auto previous =
                mPreviousPicaVertexUniforms.find(temporalIdentity);
            if (previous != mPreviousPicaVertexUniforms.end() &&
                previous->second.FrameId + 1U == mFrameCounter &&
                previous->second.Bytes.size() ==
                    draw.VertexUniformBytes.size()) {
                previousVertexUniformStorage = std::move(previous->second.Bytes);
                previousVertexUniformBytes = previousVertexUniformStorage;
                previousJitterNdc = previous->second.JitterNdc;
                exactMotionHistory = true;
            }
            PreviousPicaVertexUniforms next;
            next.FrameId = mFrameCounter;
            next.Bytes.assign(draw.VertexUniformBytes.begin(),
                              draw.VertexUniformBytes.end());
            next.JitterNdc = {
                2.0F * jitterUv[0], 2.0F * jitterUv[1]};
            mPreviousPicaVertexUniforms[temporalIdentity] = std::move(next);
        }
        finishCpuStage(cpuTimings.TemporalStateMilliseconds);
        cpuTimings.DrawStateMilliseconds =
            cpuTimings.GrassSurfaceMilliseconds +
            cpuTimings.SceneStateMilliseconds +
            cpuTimings.TemporalStateMilliseconds;
        const size_t previousPipelineCount = mNativePicaPipelines.size();
        // Hybrid preparation: the specialized program draws once it and its
        // pipeline are ready, the draw's always-available program until then.
        // A draw with neither completes its specialized program here.
        const auto backgroundAcquisition = mPreparationQueue
            ? NativePicaPipelineAcquisition::Background : NativePicaPipelineAcquisition::Blocking;
        const Oot3d::PicaShaderPipelineResult* selectedVariant = &shaderVariant;
        NativePicaShaderProgram* selectedProgram = &shaderIt->second;
        Renderer3ds::PicaDevicePipelineRecord* selectedPipeline = nullptr;
        GfxNativePicaDrawView uberDraw;
        const GfxNativePicaDrawView* selectedDraw = &effectiveDraw;
        if (mNativePicaPreparationMode != NativePicaPreparationMode::Uber &&
            AdoptNativePicaProgram(shaderIt->second, false)) {
            bool specializedPending = false;
            selectedPipeline = &GetOrCreateNativePicaPipeline(
                effectiveDraw, shaderIt->second, shaderVariant.Reactive,
                shaderVariant.Domain, shaderVariant.RequestedFeatures,
                shaderVariant.AppliedFeatures, true, false, false,
                backgroundAcquisition, &specializedPending);
            if (specializedPending) selectedPipeline = nullptr;
        }
        if (selectedPipeline == nullptr && UsesAlwaysAvailablePrograms()) {
            auto unavailable = NativePicaUberUnavailable::NoParametricProgram;
            if (!draw.UberFragmentShaderSource.empty()) {
                // The always-available program executes the specialized
                // variant's decisions: its outputs, features and bindings
                // stay the variant's, only the program differs.
                if (auto* uberProgram = ResolveUberNativePicaProgram(shaderVariant, effectiveDraw, unavailable)) {
                    uberDraw = effectiveDraw;
                    uberDraw.FragmentShaderSource = draw.UberFragmentShaderSource;
                    uberDraw.FragmentShaderKey = draw.UberFragmentShaderKey;
                    uberDraw.FragmentShaderSourceIdentity = draw.UberFragmentShaderSourceIdentity;
                    uberDraw.FragmentShaderHooks = draw.UberFragmentShaderHooks;
                    const auto acquireStart = std::chrono::steady_clock::now();
                    auto& uberPipeline = GetOrCreateNativePicaPipeline(
                        uberDraw, *uberProgram, shaderVariant.Reactive, shaderVariant.Domain,
                        shaderVariant.RequestedFeatures, shaderVariant.AppliedFeatures, true, false, false,
                        NativePicaPipelineAcquisition::Libraries);
                    const double acquired = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - acquireStart).count();
                    mNativePicaPreparationCounters.UberAcquireMilliseconds += acquired;
                    mNativePicaPreparationCounters.MaximumUberAcquireMilliseconds =
                        std::max(mNativePicaPreparationCounters.MaximumUberAcquireMilliseconds, acquired);
                    if (mNriPicaPipelineBridge.OwnedPipelineReady(uberPipeline.Id)) {
                        selectedPipeline = &uberPipeline;
                        selectedProgram = uberProgram;
                        selectedDraw = &uberDraw;
                    } else {
                        unavailable = NativePicaUberUnavailable::PipelineFailed;
                    }
                }
            }
            if (selectedPipeline == nullptr && unavailable == NativePicaUberUnavailable::NoParametricProgram) {
                static const bool diagnostics = [] {
                    const char* value = std::getenv("TRIAEVUM_UBER_DIAGNOSTICS");
                    return value != nullptr && value[0] == '1';
                }();
                if (diagnostics)
                    std::fprintf(stderr,
                        "TRIAEVUM_UBER_NO_PAIR fragment=%016llx bytes=%zu domain=%u op=%u memory_only=%u requested=%08x\n",
                        static_cast<unsigned long long>(draw.FragmentShaderKey), draw.FragmentShaderSource.size(),
                        static_cast<unsigned>(draw.CompositionDomain), static_cast<unsigned>(draw.FragmentOperationMode),
                        nativeMemoryOnly ? 1U : 0U, static_cast<unsigned>(shaderVariant.RequestedFeatures));
            }
            if (selectedPipeline == nullptr) {
                ++mNativePicaPreparationCounters.UberUnavailable[static_cast<size_t>(unavailable)];
            } else {
                auto& counters = mNativePicaPreparationCounters;
                const auto& decisions = shaderVariant.Decisions;
                ++counters.UberDraws;
                counters.UberSurfaceDraws += decisions.Has(Oot3d::PicaEffectDecisions::SurfaceLighting) ? 1U : 0U;
                counters.UberPbrDraws += decisions.Has(Oot3d::PicaEffectDecisions::Pbr) ? 1U : 0U;
                counters.UberReliefDraws += decisions.Has(Oot3d::PicaEffectDecisions::Relief) ? 1U : 0U;
                counters.UberToonDraws += decisions.Has(Oot3d::PicaEffectDecisions::Toon) ? 1U : 0U;
            }
        }
        if (selectedPipeline == nullptr) {
            const auto completionStart = std::chrono::steady_clock::now();
            AdoptNativePicaProgram(shaderIt->second, true);
            selectedPipeline = &GetOrCreateNativePicaPipeline(
                effectiveDraw, shaderIt->second, shaderVariant.Reactive,
                shaderVariant.Domain, shaderVariant.RequestedFeatures,
                shaderVariant.AppliedFeatures);
            if (mPreparationQueue) {
                ++mNativePicaPreparationCounters.CompletedDraws;
                const double milliseconds = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - completionStart).count();
                mNativePicaPreparationCounters.CompleteMilliseconds += milliseconds;
                mNativePicaPreparationCounters.MaximumCompleteMilliseconds =
                    std::max(mNativePicaPreparationCounters.MaximumCompleteMilliseconds, milliseconds);
            }
        } else if (selectedProgram == &shaderIt->second) {
            ++mNativePicaPreparationCounters.SpecializedDraws;
        }
        mDiagnostics.RecordNriPicaShaderContract(
            selectedProgram->NriDescriptorContract,
            mNriPicaPipelineBridge.DescriptorLayoutOwnedByNri());
        // Everything below binds what the selected program declares.
        const auto& drawVariant = *selectedVariant;
        auto& pipelineRecord = *selectedPipeline;
        VkPipeline pipeline = pipelineRecord.Vulkan;
        cpuTimings.PipelineLookupHits = mNativePicaPipelines.size() == previousPipelineCount ? 1U : 0U;
        cpuTimings.PipelineCreations = mNativePicaPipelines.size() != previousPipelineCount ? 1U : 0U;
        cpuTimings.PipelineEntries = static_cast<uint32_t>(mNativePicaPipelines.size());
        finishCpuStage(cpuTimings.PipelineMilliseconds);
        const bool nriOwnedDrawsEnabled =
            mNriPicaPipelineBridge.OwnedDrawsEnabled();
        const bool nriOwnedPipelineReady =
            mNriPicaPipelineBridge.OwnedPipelineReady(pipelineRecord.Id);
        auto nriDrawOwnership = Oot3d::PreparePicaNriDrawOwnership(
            nriOwnedDrawsEnabled, nriOwnedPipelineReady);
        std::vector<Oot3d::PicaNriSourceVertexStream>
            nriDrawSourceStreams;
        std::vector<Oot3d::PicaNriSourceVertexAttribute>
            nriDrawSourceAttributes;
        nriDrawSourceStreams.reserve(draw.VertexBindings.size());
        for (const auto& binding : draw.VertexBindings) {
            nriDrawSourceStreams.push_back(
                { { binding.Binding, binding.ByteStride, binding.PerInstance }, binding.Bytes });
        }
        nriDrawSourceAttributes.reserve(draw.VertexAttributes.size());
        for (const auto& attribute : draw.VertexAttributes) {
            nriDrawSourceAttributes.push_back({ attribute.Location, attribute.Binding,
                                                static_cast<Oot3d::PicaNriVertexScalar>(attribute.Format),
                                                attribute.ComponentCount, attribute.ByteOffset });
        }
        const bool directionalShadowGeometry =
            mDirectionalShadowMapBarrierPlan.Valid() &&
            Oot3d::PicaExtensionPassEnabled(
                mPicaExtensionGraph,
                Oot3d::PicaExtensionPass::DirectionalShadowLighting);
        const auto geometryResolution = mPicaGeometryRegistry.Resolve({
            draw.GeometryIdentity,
            draw.GeometryContentVersion,
            draw.GeometryIdentityAvailable,
            mFrameCounter,
            nriDrawSourceStreams,
            nriDrawSourceAttributes,
            draw.IndexBytes,
            draw.Indexed,
            draw.IndicesAre16Bit,
            draw.VertexCount,
            nriDrawOwnership.Prepared || directionalShadowGeometry || pbrOptions.SeamSoftening,
        });
        if (geometryResolution.Geometry == nullptr) {
            throw std::runtime_error("native PICA geometry preparation failed");
        }
        for (const uint64_t evicted : mPicaGeometryRegistry.TakeEvictedIdentities()) {
            RetireNativePicaGeometry(evicted);
        }
        const auto& geometry = *geometryResolution.Geometry;
        if (nriDrawOwnership.Prepared && !geometry.PackedValid) {
            nriDrawOwnership.Prepared = false;
        }
        Renderer3ds::PicaPbrSeamCache transientSeams;
        const Renderer3ds::PicaPbrSeamData* seamData = nullptr;
        if (pbrOptions.SeamSoftening && geometry.PackedValid &&
            Oot3d::HasPicaShaderInstrumentationFeature(
                drawVariant.AppliedFeatures, Oot3d::PicaShaderInstrumentationFeature::PbrMaterial)) {
            std::array<Oot3d::PicaNriSourceVertexBinding, 16> sourceBindings{};
            if (nriDrawSourceStreams.size() > sourceBindings.size())
                throw std::runtime_error("PICA seam input exceeds native binding count");
            for (size_t i = 0; i < nriDrawSourceStreams.size(); ++i)
                sourceBindings[i] = nriDrawSourceStreams[i].Binding;
            const auto layout = Oot3d::BuildPicaNriVertexInputLayout(
                std::span(sourceBindings).first(nriDrawSourceStreams.size()), nriDrawSourceAttributes);
            std::array<Renderer3ds::PicaSceneVertexBufferBinding, 16> bindings{};
            if (geometry.PackedBindings.size() > bindings.size())
                throw std::runtime_error("PICA seam geometry exceeds native binding count");
            for (size_t i = 0; i < geometry.PackedBindings.size(); ++i) {
                const auto& b = geometry.PackedBindings[i];
                bindings[i] = {b.Binding, b.Offset, b.Size, b.Stride};
            }
            const Renderer3ds::PicaShadowGeometryView view{
                geometry.Payload, std::span(bindings).first(geometry.PackedBindings.size()),
                layout.Attributes, layout.Bindings, 0, geometry.IndexOffset,
                geometry.VertexOrIndexCount, draw.BaseVertex, geometry.Indexed};
            uint32_t booleans = 0;
            std::memcpy(&booleans, draw.VertexUniformBytes.data() +
                Oot3d::kPicaPackedVertexBooleanMaskOffset, sizeof(booleans));
            auto& cache = geometryResolution.Cacheable
                ? mNativePicaGeometryBuffers[geometry.Identity].Seams : transientSeams;
            seamData = &cache.Resolve(view, draw.TemporalVertexProgram.Hooks, booleans,
                draw.VertexUniformBytes.subspan(Oot3d::kPicaPackedVertexFloatOffset),
                draw.FragmentShaderHooks.TextureSample(pbrOptions.TextureSlot)->Coordinate,
                draw.Topology == GfxNativePicaTopology::TriangleStrip,
                geometryResolution.Cacheable ? geometry.ContentVersion ^
                    (geometry.StructuralSignature * 0x9e3779b97f4a7c15ULL) : 0);
            pbrUniforms->SeamSettings[3] = std::bit_cast<float>(seamData->TriangleCount);
        }
        if (materialPreview && materialPreview->SeamSoftening && pbrMaterial) {
            const std::string_view status = !mPbrSeamsAvailable
                ? "Unavailable: GPU fragment primitive IDs are not supported"
                : materialPreview->SeamStrength == 0 ? "Inactive: seam strength is zero"
                : materialPreview->SeamChannels == 0 ? "Inactive: no seam channels selected"
                : pbrUniforms && std::bit_cast<uint32_t>(pbrUniforms->SeamSettings[2]) == 0
                    ? "Inactive: selected channels have no loaded auxiliary maps"
                : seamData ? std::string_view(seamData->Status)
                : "Unavailable: native geometry or shader contract is incompatible";
            ::Oot3d::Renderer::AzaharTexturePackRuntime::Instance().ReportMaterialSeams(
                pbrMaterial->Material->Identity.NativeHash, pbrMaterial->Material->Generation, status);
        }
        const auto geometryStats = mPicaGeometryRegistry.Stats();
        cpuTimings.GeometryRegistryHits = geometryResolution.CacheHit ? 1U : 0U;
        cpuTimings.GeometryRegistryMisses = geometryResolution.Cacheable && !geometryResolution.CacheHit ? 1U : 0U;
        cpuTimings.GeometryDynamicBuilds = geometryResolution.Cacheable ? 0U : 1U;
        cpuTimings.GeometryRegistryEntries =
            static_cast<uint32_t>(std::min<size_t>(geometryStats.Entries, std::numeric_limits<uint32_t>::max()));
        bool nriOwnedDraw = nriDrawOwnership.Prepared;

        auto& frame = mFrameResources[mCurrentFrame];
        VkCommandBuffer commandBuffer = mCommandBuffers[mCurrentFrame];
        const auto placement = PlaceNativePicaGeometry(geometryResolution, &cpuTimings);
        const VkBuffer geometryBuffer = placement.Buffer;
        const VkDeviceSize geometryBufferSize = placement.Size;
        uint8_t* const geometryMappedMemory = placement.Mapped;
        const VkDeviceSize geometryBaseOffset = placement.BaseOffset;
        const bool persistentGeometry = placement.Persistent;

        std::vector<std::pair<uint32_t, VkDeviceSize>> bindingOffsets;
        bindingOffsets.reserve(geometry.SourceBindings.size());
        for (const auto& binding : geometry.SourceBindings) {
            bindingOffsets.emplace_back(binding.Binding, geometryBaseOffset + binding.Offset);
        }
        std::vector<Oot3d::NriPicaVertexBufferBindingDesc>
            nriVertexBindings;
        if (geometry.PackedValid) {
            nriVertexBindings.reserve(geometry.PackedBindings.size());
            for (const auto& binding : geometry.PackedBindings) {
                nriVertexBindings.push_back({
                    binding.Binding,
                    geometryBaseOffset + binding.Offset,
                    binding.Size,
                    binding.Stride,
                });
            }
        }

        const VkDeviceSize indexOffset = geometryBaseOffset + geometry.IndexOffset;
        const uint32_t indexCount = geometry.Indexed ? geometry.VertexOrIndexCount : 0U;

        Oot3d::NriDirectionalShadowHistorySnapshot
            directionalShadowHistory;
        Oot3d::PicaDirectionalShadowReceiverBinding
            directionalShadowBinding{};
        const bool directionalShadowShaderApplied =
            drawVariant.DirectionalShadowEligibility ==
                Oot3d::PicaDirectionalShadowLightingEligibility::VertexPrimary ||
            drawVariant.DirectionalShadowEligibility ==
                Oot3d::PicaDirectionalShadowLightingEligibility::FragmentPrimary;
        if (directionalShadowReceiver &&
            directionalShadowShaderApplied) {
            directionalShadowHistory =
                mNriDirectionalShadowPass.FindLatestHistory(
                    draw.RenderTargetNamespace,
                    draw.FramebufferColorPhysicalAddress,
                    mFrameCounter);
            if (directionalShadowHistory.Valid()) {
                const auto nativeEnvironment =
                    Oot3d::DecodePicaNativeDrawEnvironment(
                        draw.VertexUniformBytes,
                        draw.FragmentUniformBytes,
                        draw.FragmentFeatures);
                Oot3d::DirectionalShadowMatrix receiverViewToWorld;
                if (draw.LightBinding.WorldToView &&
                    Oot3d::InvertDirectionalShadowMatrix(*draw.LightBinding.WorldToView, receiverViewToWorld)) {
                    // Native fragment lights receive the shadow as themselves:
                    // through the PBR material's native loop, or, without
                    // per-vertex lighting, in the native per-pixel result.
                    const bool nativeFragmentLights =
                        (pbrMaterial != nullptr && pbrOptions.NativeFragmentLighting) ||
                        (surfaceLighting && surfaceLighting->Control[0] == 0U &&
                         draw.FragmentFeatures.FragmentLightingEnabled);
                    if (surfaceLighting &&
                        Oot3d::HasPicaShaderInstrumentationFeature(drawVariant.AppliedFeatures,
                            Oot3d::PicaShaderInstrumentationFeature::NativePerPixelLighting) &&
                        (surfaceLighting->EnvironmentParameters[3] != 0.0F ||
                         surfaceLighting->Control[0] != 0U ||
                         surfaceLighting->Control[2] != 0U ||
                         nativeFragmentLights)) {
                        std::array<Oot3d::PicaSurfaceShadowLight, 12> lights{};
                        if (nativeFragmentLights) {
                            const auto& native = nativeEnvironment.FragmentLighting;
                            for (size_t index = 0; index < native.Lights.size(); ++index) {
                                const auto& source = native.Lights[index];
                                lights[index].DirectionViewTowardSource = source.PositionOrDirectionView;
                                lights[index].ShaderSlot = static_cast<uint8_t>(index);
                                lights[index].Enabled = source.Active && source.ValuesAvailable;
                                lights[index].Point = !native.Layout.Lights[index].Directional;
                            }
                        } else {
                            for (size_t slot = 0; slot < surfaceLighting->Lights.size(); ++slot) {
                                const auto& source = surfaceLighting->Lights[slot];
                                std::copy_n(source.DirectionEnabled.begin(), 3,
                                    lights[slot].DirectionViewTowardSource.begin());
                                lights[slot].ShaderSlot = static_cast<uint8_t>(slot);
                                lights[slot].Enabled = source.DirectionEnabled[3] != 0.0F;
                                lights[slot].Point = source.PositionRadius[3] > 0.0F;
                            }
                        }
                        if (surfaceLighting->EnvironmentParameters[3] != 0.0F) {
                            // Only the authored key casts; native slots retain their own matching.
                            const auto& direction = surfaceLighting->EnvironmentDirections[0];
                            std::copy_n(direction.begin(), 3, lights[8].DirectionViewTowardSource.begin());
                            lights[8].ShaderSlot = 8;
                            lights[8].Enabled = direction[3] > 0.0F;
                        }
                        for (uint32_t slot = 0; slot < surfaceLighting->Control[2]; ++slot) {
                            std::copy_n(surfaceLighting->SceneDirections[slot].begin(), 3,
                                lights[10 + slot].DirectionViewTowardSource.begin());
                            lights[10 + slot].ShaderSlot = static_cast<uint8_t>(10 + slot);
                            lights[10 + slot].Enabled = true;
                        }
                        directionalShadowBinding = Oot3d::BuildPicaSurfaceShadowReceiverBinding(
                            receiverViewToWorld,
                            lights, directionalShadowHistory.State);
                    }
                }
            }
        }
        const auto directionalShadowReceiverClass =
            directionalShadowBinding.Classification;
        const bool directionalShadowHistoryBound =
            directionalShadowBinding.Bound();
        mDiagnostics.RecordNriDirectionalShadowReceiver(
            directionalShadowReceiver && directionalShadowShaderApplied,
            directionalShadowReceiverClass !=
                Oot3d::PicaDirectionalShadowReceiverClass::LightingUnavailable,
            directionalShadowReceiverClass !=
                    Oot3d::PicaDirectionalShadowReceiverClass::LightingUnavailable &&
                directionalShadowReceiverClass !=
                    Oot3d::PicaDirectionalShadowReceiverClass::LightingDisabled,
            directionalShadowReceiverClass ==
                Oot3d::PicaDirectionalShadowReceiverClass::AmbientOnly,
            directionalShadowReceiverClass ==
                Oot3d::PicaDirectionalShadowReceiverClass::DirectUnmatched,
            directionalShadowReceiverClass ==
                Oot3d::PicaDirectionalShadowReceiverClass::DirectMatched,
            directionalShadowHistoryBound);
        const auto& directionalShadowUniforms =
            directionalShadowBinding.Uniforms;
        const auto toonUniforms = Oot3d::PackToonSurfaceParameters(
            graphicsSettings.Effects.Toon, graphicsSettings.Effects.ToonStyle, false);
        // The superset effect program reads its decisions (and the toon
        // parameters) from the toon binding.
        std::optional<Oot3d::PicaUberEffectBlock> effectBlock;
        if (selectedProgram->EffectBlock) {
            std::array<float, 32> toon{};
            static_assert(sizeof(toonUniforms) == sizeof(toon));
            std::memcpy(toon.data(), &toonUniforms, sizeof(toon));
            effectBlock = Oot3d::PackPicaUberEffectBlock(drawVariant.Decisions, toon);
        }

        const bool uniformToon = effectBlock.has_value() || Oot3d::HasPicaShaderInstrumentationFeature(
            drawVariant.AppliedFeatures, Oot3d::PicaShaderInstrumentationFeature::Toon);
        const std::span<const uint8_t> toonBytes = effectBlock
            ? std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&*effectBlock), sizeof(*effectBlock))
            : std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&toonUniforms), sizeof(toonUniforms));
        const bool uniformSurface = Oot3d::HasPicaShaderInstrumentationFeature(
            drawVariant.AppliedFeatures, Oot3d::PicaShaderInstrumentationFeature::NativePerPixelLighting);
        const bool uniformPbr = Oot3d::HasPicaShaderInstrumentationFeature(
            drawVariant.AppliedFeatures, Oot3d::PicaShaderInstrumentationFeature::PbrMaterial);
        const bool uniformShadow = Oot3d::HasPicaShaderInstrumentationFeature(
            drawVariant.AppliedFeatures, Oot3d::PicaShaderInstrumentationFeature::DirectionalShadowLighting);
        // Without a dissolve the receiver never reads the older layout: its
        // range ends before it.
        const VkDeviceSize directionalShadowUniformSize = uniformShadow
            ? directionalShadowUniforms.UsedBytes() : draw.FragmentUniformBytes.size();
        const std::array<VkDeviceSize, 7> uniformSizes{
            draw.VertexUniformBytes.size(), previousVertexUniformBytes.size(),
            draw.FragmentUniformBytes.size(), uniformShadow ? directionalShadowUniformSize : 0U,
            uniformToon ? toonBytes.size() : 0U,
            uniformSurface ? sizeof(*surfaceLighting) : 0U,
            uniformPbr ? sizeof(*pbrUniforms) : 0U};
        VkDeviceSize uniformBytes = 0;
        for (const auto size : uniformSizes)
            uniformBytes += (size + mUniformBufferAlignment - 1U) & ~(mUniformBufferAlignment - 1U);
        const auto seamBytes = seamData && !seamData->Words.empty()
            ? std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(seamData->Words.data()),
                                      seamData->Words.size() * sizeof(seamData->Words[0]))
            : std::span<const uint8_t>{};
        uniformBytes += (seamBytes.size() + mUniformBufferAlignment - 1U) & ~(mUniformBufferAlignment - 1U);
        EnsureFrameUniformCapacity(uniformBytes, mUniformBufferAlignment);

        const auto uploadUniform = [&](std::span<const uint8_t> bytes) {
            const VkDeviceSize offset =
                (frame.UniformBytesUsed + mUniformBufferAlignment - 1U) &
                ~(mUniformBufferAlignment - 1U);
            if (bytes.empty() || offset + bytes.size() >
                                     frame.UniformBuffer.Size) {
                throw std::runtime_error(
                    "native PICA per-frame uniform arena exhausted");
            }
            std::memcpy(static_cast<uint8_t*>(frame.UniformBuffer.Mapped) +
                            offset,
                        bytes.data(), bytes.size());
            frame.UniformBytesUsed = offset + bytes.size();
            return offset;
        };
        const VkDeviceSize vertexUniformOffset =
            uploadUniform(draw.VertexUniformBytes);
        const VkDeviceSize previousVertexUniformOffset =
            uploadUniform(previousVertexUniformBytes);
        const VkDeviceSize fragmentUniformOffset =
            uploadUniform(draw.FragmentUniformBytes);
        const VkDeviceSize directionalShadowUniformOffset = uniformShadow
            ? uploadUniform(std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(&directionalShadowUniforms),
                static_cast<size_t>(directionalShadowUniformSize)))
            : fragmentUniformOffset;
        // Inactive extension bindings reuse an existing range; no upload.
        const VkDeviceSize toonUniformOffset = uniformToon
            ? uploadUniform(toonBytes)
            : directionalShadowUniformOffset;
        const VkDeviceSize toonUniformSize = uniformToon
            ? toonBytes.size() : directionalShadowUniformSize;
        VkDeviceSize surfaceUniformOffset = directionalShadowUniformOffset;
        if (uniformSurface) {
            // Compare host-cached state, not potentially write-combined mapped
            // upload memory. The offset belongs to this frame and arena only.
            if (!frame.SurfaceLightingUniformOffset ||
                std::memcmp(&frame.SurfaceLightingUniforms, &*surfaceLighting, sizeof(*surfaceLighting)) != 0) {
                frame.SurfaceLightingUniformOffset = uploadUniform(std::span<const uint8_t>(
                    reinterpret_cast<const uint8_t*>(&*surfaceLighting), sizeof(*surfaceLighting)));
                frame.SurfaceLightingUniforms = *surfaceLighting;
            }
            surfaceUniformOffset = *frame.SurfaceLightingUniformOffset;
        }
        const VkDeviceSize surfaceUniformSize = uniformSurface
            ? sizeof(*surfaceLighting) : directionalShadowUniformSize;
        const VkDeviceSize pbrUniformOffset = uniformPbr
            ? uploadUniform(std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(&pbrUniforms.value()), sizeof(*pbrUniforms)))
            : directionalShadowUniformOffset;
        const VkDeviceSize pbrUniformSize = uniformPbr ? sizeof(*pbrUniforms) : directionalShadowUniformSize;
        const VkDeviceSize seamOffset = seamBytes.empty() ? fragmentUniformOffset : uploadUniform(seamBytes);
        const VkDeviceSize seamSize = seamBytes.empty() ? 16U : seamBytes.size();
        finishCpuStage(cpuTimings.UploadMilliseconds);

        const VkDescriptorSet descriptor = AllocateNativePicaDescriptorSet();
        const VkDescriptorBufferInfo vertexUniform{
            frame.UniformBuffer.Buffer, vertexUniformOffset,
            draw.VertexUniformBytes.size()};
        const VkDescriptorBufferInfo fragmentUniform{
            frame.UniformBuffer.Buffer, fragmentUniformOffset,
            draw.FragmentUniformBytes.size()};
        const VkDescriptorBufferInfo previousVertexUniform{
            frame.UniformBuffer.Buffer, previousVertexUniformOffset,
            previousVertexUniformBytes.size()};
        const VkDescriptorBufferInfo directionalShadowUniform{
            frame.UniformBuffer.Buffer,
            directionalShadowUniformOffset,
            directionalShadowUniformSize};
        const VkDescriptorBufferInfo toonUniform{
            frame.UniformBuffer.Buffer, toonUniformOffset, toonUniformSize};
        const VkDescriptorBufferInfo surfaceUniform{
            frame.UniformBuffer.Buffer, surfaceUniformOffset, surfaceUniformSize};
        const VkDescriptorBufferInfo pbrUniform{
            frame.UniformBuffer.Buffer, pbrUniformOffset, pbrUniformSize};
        const VkDescriptorBufferInfo seamBuffer{frame.UniformBuffer.Buffer, seamOffset, seamSize};
        std::array<VkDescriptorImageInfo, Renderer3ds::kPicaPbrTextureCount> pbrImageInfos{};
        for (size_t role = 0; role < pbrImageInfos.size(); ++role)
            pbrImageInfos[role] = {pbrSampler, pbrTextures[role]->View, pbrTextures[role]->ImageLayout};
        std::array<VkDescriptorImageInfo, 3> imageInfos{};
        for (size_t slot = 0; slot < imageInfos.size(); ++slot) {
            imageInfos[slot] = {nativeTextures[slot]->Sampler, textures[slot]->View,
                                textures[slot]->ImageLayout};
        }
        const VkDescriptorImageInfo shadowStorage{
            VK_NULL_HANDLE, renderTarget.ShadowView, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo directionalShadowImage{
            directionalShadowHistoryBound
                ? directionalShadowHistory.Sampler
                : mFallbackTexture.Sampler,
            directionalShadowHistoryBound
                ? directionalShadowHistory.View
                : mFallbackTexture.View,
            directionalShadowHistoryBound
                ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                : mFallbackTexture.ImageLayout};
        const VkDescriptorImageInfo lightingLutImage{
            lightingLutTexture->Sampler,
            lightingLutTexture->View,
            lightingLutTexture->ImageLayout};
        std::array<VkWriteDescriptorSet, 14 + Renderer3ds::kPicaPbrTextureCount> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     descriptor, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                     nullptr, &vertexUniform, nullptr};
        for (uint32_t slot = 0; slot < 3; ++slot) {
            writes[slot + 1U] = {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptor,
                slot + 1U, 0, 1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                &imageInfos[slot], nullptr, nullptr};
        }
        writes[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     descriptor, 4, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                     nullptr, &fragmentUniform, nullptr};
        writes[5] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     descriptor, 5, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                     &shadowStorage, nullptr, nullptr};
        writes[6] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     descriptor, 6, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                     nullptr, &previousVertexUniform, nullptr};
        writes[7] = {
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
            descriptor, 10, 0, 1,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            &directionalShadowImage, nullptr, nullptr};
        writes[8] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                     descriptor, 12, 0, 1,
                     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                     nullptr, &directionalShadowUniform, nullptr};
        writes[9] = {
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
            descriptor, 13, 0, 1,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            &lightingLutImage, nullptr, nullptr};
        writes[10] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                      descriptor, 15, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                      nullptr, &toonUniform, nullptr};
        writes[11] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                      descriptor, 16, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                      nullptr, &surfaceUniform, nullptr};
        writes[12] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                      descriptor, 17, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                      nullptr, &pbrUniform, nullptr};
        for (uint32_t role = 0; role < pbrImageInfos.size(); ++role)
            writes[13U + role] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                descriptor, 18U + role, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                &pbrImageInfos[role], nullptr, nullptr};
        writes.back() = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
            descriptor, 26, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &seamBuffer, nullptr};
        vkUpdateDescriptorSets(mDevice, static_cast<uint32_t>(writes.size() - (pbrOptions.SeamSoftening ? 0U : 1U)),
                               writes.data(), 0, nullptr);
        finishCpuStage(cpuTimings.DescriptorMilliseconds);

        const bool samplesLiveShadow = std::any_of(
            textures.begin(), textures.end(), [](const TextureRecord* texture) {
                return texture != nullptr && texture->LiveRenderTargetAlias;
            });
        if (samplesLiveShadow) {
            EndNativePicaRenderPass();
            VkMemoryBarrier shadowVisibility{
                VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            shadowVisibility.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            shadowVisibility.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                                 &shadowVisibility, 0, nullptr, 0, nullptr);
        }
        BeginNativePicaRenderPass(renderTarget);
        ApplyPendingNativePicaMemoryFills(renderTarget);
        // Preparation is independent of the render scope. Ownership must be
        // finalized only after the current target's scope has been opened;
        // checking the previous scope here used to force its first draw
        // through the Vulkan fallback.
        nriDrawOwnership = Oot3d::FinalizePicaNriDrawOwnership(
            nriDrawOwnership,
            mPicaDynamicRenderingScope.ActiveOwnedByNri());
        nriOwnedDraw = nriDrawOwnership.SubmitOwned;
        if (!nriOwnedDraw &&
            !Oot3d::PicaNriVulkanFallbackAllowed(
                nriDrawOwnership)) {
            throw std::runtime_error(
                "NRI PICA draw ownership is unavailable in an "
                "NRI-owned rendering scope [owned_draws=" +
                std::to_string(nriOwnedDrawsEnabled) +
                ", owned_pipeline=" +
                std::to_string(nriOwnedPipelineReady) +
                ", packed_geometry=" +
                std::to_string(geometry.PackedValid) +
                ", submission_id=" +
                std::to_string(draw.SubmissionId) +
                ", vertex_shader=" +
                std::to_string(draw.VertexShaderKey) +
                ", fragment_shader=" +
                std::to_string(draw.FragmentShaderKey) +
                ", packed_error=" + geometry.PackedError + "]");
        }
        // A UI-classified draw whose geometry covers the colour target this
        // frame's scene drew into is not a placed UI element but frame content
        // the title composites over the whole native screen, so it reaches
        // every output pixel and our recomposition stays the single owner of
        // the output. When it shows the rendered frame - a wash over it, or a
        // redraw of GPU copies of rendered output (the title's full-frame effect
        // over the copied scene) - it takes the scene's mapping, so the redraw
        // lands on the pixels it copied; an opaque picture of its own (a movie
        // frame) is shown whole on the native canvas, with black bars around
        // it; an overlay with its own alpha covers the target undistorted.
        // Placed elements - HUD, minimap, dialogue, menus, the file-select
        // screen - also draw over the scene with a whole-target viewport, but
        // cover only part of it: they stay `Ui` and keep the uniform native
        // canvas that their layout and pointer mapping depend on.
        const float viewportRight = draw.ViewportX + draw.ViewportWidth;
        const float viewportBottom = draw.ViewportY + draw.ViewportHeight;
        Renderer3ds::PicaUiFrameContentEvidence frameContent;
        frameContent.CoversWholeTarget =
            draw.ViewportX <= 0.0F && draw.ViewportY <= 0.0F &&
            viewportRight >= static_cast<float>(draw.FramebufferWidth) &&
            viewportBottom >= static_cast<float>(draw.FramebufferHeight);
        const bool wholeTargetUi =
            draw.CompositionDomain == Renderer3ds::PicaCompositionDomain::Ui &&
            frameContent.CoversWholeTarget &&
            mPicaSceneFrame.HasSceneDrawOnColorTarget(
                draw.FramebufferColorPhysicalAddress);
        if (wholeTargetUi) {
            frameContent.SceneDrewOnTarget = true;
            frameContent.Untextured = draw.Textures.empty();
            frameContent.SamplesOnlyRenderedCopies =
                !draw.Textures.empty() &&
                std::all_of(draw.Textures.begin(), draw.Textures.end(),
                            [](const auto& texture) {
                                return !texture.CopyReferences.empty();
                            });
            frameContent.SamplesOnlyAlphaFreeTextures =
                !draw.Textures.empty() &&
                std::none_of(draw.Textures.begin(), draw.Textures.end(),
                             [](const auto& texture) {
                                 return Renderer3ds::PicaTextureFormatCarriesAlpha(
                                     texture.NativeFormat);
                             });
            if (draw.VertexUniformBytes.size() >=
                Oot3d::kPicaPackedVertexFloatOffset) {
                uint32_t booleanUniforms = 0;
                std::memcpy(&booleanUniforms,
                            draw.VertexUniformBytes.data() +
                                Oot3d::kPicaPackedVertexBooleanMaskOffset,
                            sizeof(booleanUniforms));
                frameContent.GeometryCoversViewport =
                    Renderer3ds::PicaDrawGeometryCoversViewport(
                        draw, booleanUniforms,
                        draw.VertexUniformBytes.subspan(
                            Oot3d::kPicaPackedVertexFloatOffset));
            }
        }
        const auto frameContentResolution =
            Renderer3ds::ResolvePicaUiFrameContent(draw.CompositionDomain,
                                                   frameContent);
        const auto compositionDomain = frameContentResolution.Domain;
        if (wholeTargetUi) {
            mDiagnostics.RecordWholeTargetUiDraw(
                compositionDomain ==
                Renderer3ds::PicaCompositionDomain::Screen);
        }
        const auto rasterCanvas = ::Oot3d::Renderer::ResolvePicaRasterCanvas(
            compositionDomain, frameContentResolution.Content,
            draw.FramebufferWidth, draw.FramebufferHeight, renderTarget.Width,
            renderTarget.Height,
            Oot3d::NativeTopScreenStorageSamplingX(
                {draw.FramebufferWidth, draw.FramebufferHeight},
                NativeTopScreenOutputExtent()));
        const float framebufferScaleX = rasterCanvas.ScaleX;
        const float framebufferScaleY = rasterCanvas.ScaleY;
        mViewport = {rasterCanvas.X + draw.ViewportX * framebufferScaleX,
                     rasterCanvas.Y + draw.ViewportY * framebufferScaleY,
                     draw.ViewportWidth * framebufferScaleX,
                     draw.ViewportHeight * framebufferScaleY, 0.0F, 1.0F};
        if (draw.ScissorMode == 3U) {
            const uint32_t x1 = draw.ScissorX1;
            const uint32_t y1 = draw.ScissorY1;
            const uint32_t x2 = static_cast<uint32_t>(draw.ScissorX2) + 1U;
            const uint32_t y2 = static_cast<uint32_t>(draw.ScissorY2) + 1U;
            mScissor = {
                {static_cast<int32_t>(rasterCanvas.X + static_cast<float>(x1) *
                                      framebufferScaleX),
                 static_cast<int32_t>(rasterCanvas.Y + static_cast<float>(y1) *
                                      framebufferScaleY)},
                {static_cast<uint32_t>(static_cast<float>(
                     x2 > x1 ? x2 - x1 : 0U) * framebufferScaleX),
                 static_cast<uint32_t>(static_cast<float>(
                     y2 > y1 ? y2 - y1 : 0U) * framebufferScaleY)}};
        } else {
            mScissor = {{0, 0},
                        {renderTarget.Width, renderTarget.Height}};
        }
        mViewportSet = true;
        mScissorSet = true;
        // Resolved-colour provenance of the target this draw writes, evaluated
        // here because only now are the resolved texture records, the effective
        // typed composition domain and the effective raster extent all known.
        // The conditions are: the draw is composed as full-frame screen content,
        // its effective viewport and scissor cover the whole target, every
        // non-fallback texture it samples is a resolved copy record, and the UV
        // footprint it samples lies inside that record's owned (producer-covered)
        // rectangle - so the pixels actually written are resolved copy pixels,
        // not the CPU-owned remainder of a padded texture. Nothing here uses the
        // raw draw viewport as a geometry proof, and a tiny mesh cannot qualify.
        {
            // Native UI consumes the completed scene; its pixels must neither
            // invalidate that completion nor become inputs to scene effects.
            const bool retainResolvedScene =
                draw.CompositionDomain == Renderer3ds::PicaCompositionDomain::Ui &&
                renderTarget.SceneEffectsResolvedSerial != 0U &&
                renderTarget.SceneEffectsResolvedSerial == renderTarget.ContentWriteSerial;
            const bool retainSceneBeforeUi =
                draw.CompositionDomain == Renderer3ds::PicaCompositionDomain::Ui &&
                renderTarget.SceneBeforeUiSerial != 0U &&
                renderTarget.SceneBeforeUiSerial == renderTarget.ContentWriteSerial;
            renderTarget.ContentWriteSerial = ++mGpuTextureCopyWriteSerial;
            if (retainResolvedScene)
                renderTarget.SceneEffectsResolvedSerial = renderTarget.ContentWriteSerial;
            if (retainSceneBeforeUi)
                renderTarget.SceneBeforeUiSerial = renderTarget.ContentWriteSerial;
            const bool fullTargetRaster =
                compositionDomain ==
                    ::Fast::Renderer3ds::PicaCompositionDomain::Screen &&
                mViewport.x <= 0.0F && mViewport.y <= 0.0F &&
                mViewport.x + static_cast<float>(mViewport.width) >=
                    static_cast<float>(renderTarget.Width) &&
                mViewport.y + static_cast<float>(mViewport.height) >=
                    static_cast<float>(renderTarget.Height) &&
                mScissor.offset.x <= 0 &&
                mScissor.offset.y <= 0 &&
                static_cast<uint32_t>(mScissor.offset.x) + mScissor.extent.width >=
                    renderTarget.Width &&
                static_cast<uint32_t>(mScissor.offset.y) + mScissor.extent.height >=
                    renderTarget.Height;
            if (fullTargetRaster) {
                bool sampledResolvedCopy = false;
                bool resolvedContentDraw = true;
                for (const TextureRecord* sampled : textures) {
                    if (sampled == nullptr || sampled == &mFallbackTexture) {
                        continue;
                    }
                    if (sampled->SourceResolvedColour == false ||
                        SampledFootprintInsideOwnedRect(draw, *sampled) == false) {
                        resolvedContentDraw = false;
                        break;
                    }
                    sampledResolvedCopy = true;
                }
                if (resolvedContentDraw && sampledResolvedCopy) {
                    // This draw's own write is resolved content, so the target's
                    // current content version carries the resolved scene effects;
                    // any later draw, clear or fill of it bumps the write serial
                    // and the accessor answers false again.
                    renderTarget.SceneEffectsResolvedSerial =
                        renderTarget.ContentWriteSerial;
                }
            }
        }
        const bool ownPicture = frameContentResolution.Content ==
                                Renderer3ds::PicaScreenContent::OwnPicture;
        if (compositionDomain == Renderer3ds::PicaCompositionDomain::Ui ||
            ownPicture) {
            const auto clip = Renderer3ds::ClipToPicaCanvas(
                {mScissor.offset.x, mScissor.offset.y,
                 mScissor.extent.width, mScissor.extent.height},
                rasterCanvas, draw.FramebufferWidth, draw.FramebufferHeight);
            mScissor = {{clip.X, clip.Y}, {clip.Width, clip.Height}};
        }
        if (ownPicture) {
            // A picture of its own (a movie frame) is shown whole on the
            // native canvas, and the pixels around it are black bars, never
            // what the title drew behind the picture.
            const auto bars = Renderer3ds::SurroundPicaCanvas(
                rasterCanvas, draw.FramebufferWidth, draw.FramebufferHeight,
                renderTarget.Width, renderTarget.Height);
            PaintNativePicaPictureBars(bars);
            mDiagnostics.RecordOwnPictureDraw(bars.Count);
        } else if (frameContentResolution.Content ==
                   Renderer3ds::PicaScreenContent::PictureOverlay) {
            mDiagnostics.RecordPictureOverlayDraw();
        }
        struct NativeDrawPush {
            std::array<float, 4> RigidMotion{};
            std::array<float, 4> Jitter{};
        } nativeDrawPush;
        nativeDrawPush.RigidMotion = {
            rigidMotion.MotionUv[0], rigidMotion.MotionUv[1],
            rigidMotion.Valid ? 1.0F : 0.0F,
            exactMotionHistory ? 1.0F : 0.0F};
        nativeDrawPush.Jitter = {
            2.0F * jitterUv[0], 2.0F * jitterUv[1],
            previousJitterNdc[0], previousJitterNdc[1]};
        if (temporalEligible)
            mDiagnostics.RecordTemporalDraw(
                rigidMotion.Valid, exactMotionHistory,
                exactMotionHistory && !rigidEligible, repeatedTemporalIdentity);

        Oot3d::NriPicaOwnedDrawDesc nriDraw;
        Oot3d::PicaNriVertexInputLayout sceneVertexLayout;
        if (nriOwnedDraw) {
            nriDraw.FrameIndex = mCurrentFrame;
            nriDraw.FrameId = mFrameCounter;
            nriDraw.PipelineId = pipelineRecord.Id;
            nriDraw.UniformBuffer = frame.UniformBuffer.Buffer;
            nriDraw.UniformBufferSize = frame.UniformBuffer.Size;
            nriDraw.UniformMappedMemory =
                static_cast<uint8_t*>(frame.UniformBuffer.Mapped);
            nriDraw.SeamGeometry = {seamOffset, pbrOptions.SeamSoftening ? seamSize : 0U};
            nriDraw.Uniforms = {{
                {vertexUniformOffset,
                 draw.VertexUniformBytes.size()},
                {fragmentUniformOffset,
                 draw.FragmentUniformBytes.size()},
                {previousVertexUniformOffset,
                 previousVertexUniformBytes.size()},
                {directionalShadowUniformOffset,
                 directionalShadowUniformSize},
                {toonUniformOffset, toonUniformSize},
                {surfaceUniformOffset, surfaceUniformSize},
                {pbrUniformOffset, pbrUniformSize},
            }};
            nriDraw.VertexBuffer = geometryBuffer;
            nriDraw.VertexBufferSize = geometryBufferSize;
            nriDraw.VertexMappedMemory = geometryMappedMemory;
            nriDraw.VertexContentPersistent = persistentGeometry;
            nriDraw.VertexBindings = nriVertexBindings;
            nriDraw.Indexed = draw.Indexed;
            nriDraw.IndexOffset = indexOffset;
            nriDraw.VertexOrIndexCount =
                draw.Indexed ? indexCount : draw.VertexCount;
            nriDraw.BaseVertex = draw.BaseVertex;
            const auto bindTexture = [](Oot3d::NriPicaTextureBindingDesc& destination,
                                        const TextureRecord& texture, const TextureRecord& sampler,
                                        VkFormat format, bool integer) {
                destination.Image = texture.NriImage;
                destination.Format = format;
                destination.Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
                if (texture.ImageLayout == VK_IMAGE_LAYOUT_GENERAL)
                    destination.Usage |= VK_IMAGE_USAGE_STORAGE_BIT;
                destination.Width = texture.Width;
                destination.Height = texture.Height;
                destination.MipLevels = texture.MipLevels;
                destination.MinFilter = ToNativePicaVkFilter(sampler.SamplerState.MinFilter);
                destination.MagFilter = ToNativePicaVkFilter(sampler.SamplerState.MagFilter);
                destination.MipmapMode = ToNativePicaVkMipmapMode(sampler.SamplerState.MinFilter);
                destination.AddressU = ToNativePicaVkAddressMode(sampler.SamplerState.WrapS);
                destination.AddressV = ToNativePicaVkAddressMode(sampler.SamplerState.WrapT);
                destination.MipBias = sampler.SamplerState.LodBias;
                const uint32_t highestMip = texture.UploadedMipLevels > 0U ? texture.UploadedMipLevels - 1U : 0U;
                destination.MaxLod = static_cast<float>(std::min(sampler.SamplerState.MaxMipLevel, highestMip));
                destination.MinLod = static_cast<float>(std::min(sampler.SamplerState.MinMipLevel, highestMip));
                destination.Integer = integer;
            };
            for (size_t slot = 0; slot < nriDraw.Textures.size(); ++slot)
                bindTexture(nriDraw.Textures[slot], *textures[slot], *nativeTextures[slot],
                    textureFormats[slot], integerTextures[slot]);
            for (size_t role = 0; role < nriDraw.PbrTextures.size(); ++role)
                bindTexture(nriDraw.PbrTextures[role], *pbrTextures[role],
                    pbrMaterial ? *nativeTextures[pbrOptions.TextureSlot] : mFallbackTexture,
                    VK_FORMAT_R8G8B8A8_UNORM, false);
            {
                auto& destination =
                    nriDraw.DirectionalShadowTexture;
                destination.Image = directionalShadowHistoryBound
                    ? directionalShadowHistory.Image
                    : mFallbackTexture.NriImage;
                destination.Format = directionalShadowHistoryBound
                    ? directionalShadowHistory.Format
                    : VK_FORMAT_R8G8B8A8_UNORM;
                destination.Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
                destination.Width = directionalShadowHistoryBound
                    ? directionalShadowHistory.State.Virtual.AtlasResolution *
                          directionalShadowHistory.State.AtlasRegions
                    : mFallbackTexture.Width;
                destination.Height = directionalShadowHistoryBound
                    ? directionalShadowHistory.State.Virtual.AtlasResolution
                    : mFallbackTexture.Height;
                destination.MipLevels = 1U;
                destination.MinFilter = VK_FILTER_NEAREST;
                destination.MagFilter = VK_FILTER_NEAREST;
                destination.MipmapMode =
                    VK_SAMPLER_MIPMAP_MODE_NEAREST;
                destination.AddressU =
                    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                destination.AddressV =
                    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                destination.MinLod = 0.0F;
                destination.MaxLod = 0.0F;
                destination.Integer = false;
            }
            {
                // The cube atlas only where the variant reads it.
                const bool pointShadowBound = pointShadowHistory.Valid() &&
                    Oot3d::HasPicaShaderInstrumentationFeature(
                        drawVariant.AppliedFeatures, Oot3d::PicaShaderInstrumentationFeature::PointShadowLighting);
                if (pointShadowReceiver)
                    mDiagnostics.RecordNriPointShadowReceiver(pointShadowBound);
                auto& destination = nriDraw.PointShadowTexture;
                destination.Image = pointShadowBound ? pointShadowHistory.Image : mFallbackTexture.NriImage;
                destination.Format = pointShadowBound ? pointShadowHistory.Format : VK_FORMAT_R8G8B8A8_UNORM;
                destination.Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
                destination.Width = pointShadowBound ? pointShadowHistory.Layout.Width : mFallbackTexture.Width;
                destination.Height = pointShadowBound ? pointShadowHistory.Layout.Height : mFallbackTexture.Height;
                destination.MipLevels = 1U;
                destination.MinFilter = VK_FILTER_NEAREST;
                destination.MagFilter = VK_FILTER_NEAREST;
                destination.MipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                destination.AddressU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                destination.AddressV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                destination.MinLod = 0.0F;
                destination.MaxLod = 0.0F;
                destination.Integer = false;
            }
            {
                auto& destination = nriDraw.LightingLutTexture;
                destination.Image = lightingLutTexture->NriImage;
                destination.Format = lightingLutInteger
                    ? VK_FORMAT_R32_UINT
                    : VK_FORMAT_R8G8B8A8_UNORM;
                destination.Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
                destination.Width = lightingLutTexture->Width;
                destination.Height = lightingLutTexture->Height;
                destination.MipLevels = 1U;
                destination.MinFilter = VK_FILTER_NEAREST;
                destination.MagFilter = VK_FILTER_NEAREST;
                destination.MipmapMode =
                    VK_SAMPLER_MIPMAP_MODE_NEAREST;
                destination.AddressU =
                    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                destination.AddressV =
                    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                destination.MinLod = 0.0F;
                destination.MaxLod = 0.0F;
                destination.Integer = lightingLutInteger;
            }
            nriDraw.StorageImage = renderTarget.ShadowImage;
            nriDraw.StorageFormat = VK_FORMAT_R32_UINT;
            nriDraw.StorageWidth = renderTarget.Width;
            nriDraw.StorageHeight = renderTarget.Height;
            std::copy(nativeDrawPush.RigidMotion.begin(),
                      nativeDrawPush.RigidMotion.end(),
                      nriDraw.RootConstants.begin());
            std::copy(nativeDrawPush.Jitter.begin(),
                      nativeDrawPush.Jitter.end(),
                      nriDraw.RootConstants.begin() + 4);
            nriDraw.Viewport = mViewport;
            nriDraw.Scissor = mScissor;
            std::copy(std::begin(draw.Blend.ConstantColor),
                      std::end(draw.Blend.ConstantColor),
                      nriDraw.BlendConstants.begin());
            nriDraw.StencilTest = draw.Stencil.Enabled;
            nriDraw.StencilReference = draw.Stencil.Reference;

            std::vector<Oot3d::PicaNriSourceVertexBinding> shadowSourceBindings;
            shadowSourceBindings.reserve(draw.VertexBindings.size());
            for (const auto& binding : draw.VertexBindings) {
                shadowSourceBindings.push_back({ binding.Binding, binding.ByteStride, binding.PerInstance });
            }
            sceneVertexLayout =
                Oot3d::BuildPicaNriVertexInputLayout(shadowSourceBindings, nriDrawSourceAttributes);
        }
        const bool directionalShadowCaster =
            nriOwnedDraw && opaqueWorldDraw && sceneVertexLayout.Valid() &&
            !sceneVertexLayout.Bindings.empty() &&
            !sceneVertexLayout.Attributes.empty() &&
            mDirectionalShadowMapBarrierPlan.Valid() &&
            Oot3d::PicaExtensionPassEnabled(
                mPicaExtensionGraph,
                Oot3d::PicaExtensionPass::DirectionalShadowLighting) &&
            AdmitsOpaqueSceneGeometry(draw);
        if (nriOwnedDraw && !nativeMemoryOnly) {
            std::vector<Oot3d::PicaSceneVertexBufferBinding>
                sceneVertexBindings;
            sceneVertexBindings.reserve(nriVertexBindings.size());
            for (const auto& binding : nriVertexBindings) {
                sceneVertexBindings.push_back(
                    { binding.Binding, binding.Offset, binding.Size,
                      binding.Stride });
            }
            Renderer3ds::VirtualShadowCaster shadowCaster;
            Oot3d::DirectionalShadowMatrix shadowViewToWorld;
            const auto prepared = mPreparedShadowCasters.find(draw.SubmissionId);
            if (directionalShadowCaster && prepared != mPreparedShadowCasters.end()) {
                // Prepared from the same inputs for this sample's map.
                shadowCaster = prepared->second;
                mDiagnostics.RecordNriDirectionalShadowPreparedReuse();
            } else if (directionalShadowCaster && draw.LightBinding.WorldToView &&
                draw.VertexUniformBytes.size() >= Oot3d::kPicaPackedVertexFloatOffset &&
                Oot3d::InvertDirectionalShadowMatrix(*draw.LightBinding.WorldToView, shadowViewToWorld)) {
                uint32_t booleanUniforms = 0;
                std::memcpy(&booleanUniforms, draw.VertexUniformBytes.data() +
                            Oot3d::kPicaPackedVertexBooleanMaskOffset, sizeof(booleanUniforms));
                Renderer3ds::PicaShadowCasterCache transientCache;
                auto& shadowCache = geometryResolution.Cacheable
                    ? mNativePicaGeometryBuffers.at(geometry.Identity).ShadowCaster : transientCache;
                const uint64_t version = geometryResolution.Cacheable
                    ? geometry.ContentVersion ^ (geometry.StructuralSignature * 0x9e3779b97f4a7c15ULL) ^
                      geometry.Identity ^ draw.VertexShaderKey : 0;
                shadowCaster = Renderer3ds::BuildPicaShadowCaster(
                    {geometry.Payload, sceneVertexBindings, sceneVertexLayout.Attributes, sceneVertexLayout.Bindings,
                     geometryBaseOffset, geometry.IndexOffset, geometry.VertexOrIndexCount,
                     draw.BaseVertex, geometry.Indexed},
                    draw.TemporalVertexProgram.Hooks, booleanUniforms,
                    draw.VertexUniformBytes.subspan(Oot3d::kPicaPackedVertexFloatOffset),
                    shadowViewToWorld, version, shadowCache);
            }
            shadowCaster.Dynamic = draw.Composition.NativeActorGeometry;
            shadowCaster.OwnerIdentity = draw.Composition.NativeActorIdentity;
            if (opaqueWorldDraw && draw.LightBinding.WorldToView)
                renderTarget.SampleView.Observe(mFrameCounter, *draw.LightBinding.WorldToView);
            if (opaqueWorldDraw)
                if (const auto projection = Oot3d::DecodePicaPerspectiveProjection(draw.VertexUniformBytes))
                    renderTarget.SampleProjection.Observe(mFrameCounter, *projection);
            // Native frame geometry for the scene distance field: the shadow
            // caster admission, from the first non-synthetic execution.
            const bool sceneDistanceFieldGeometry =
                Oot3d::PicaExtensionPassEnabled(
                    mPicaExtensionGraph, Oot3d::PicaExtensionPass::SceneDistanceField) &&
                opaqueWorldDraw && sceneVertexLayout.Valid() && !sceneVertexLayout.Bindings.empty() &&
                AdmitsOpaqueSceneGeometry(draw) && draw.LightBinding.WorldToView &&
                draw.VertexUniformBytes.size() >= Oot3d::kPicaPackedVertexFloatOffset;
            if (sceneDistanceFieldGeometry) {
                if (!mBrixelizerScene.Available() && !mBrixelizerInitializationAttempted) {
                    mBrixelizerInitializationAttempted = true;
                    if (!mBrixelizerScene.Initialize(mPhysicalDevice, mDevice, kFramesInFlight))
                        std::fprintf(stderr, "TRIAEVUM_SCENE_DISTANCE_FIELD unavailable=\"%s\"\n",
                                     mBrixelizerScene.UnavailableReason().c_str());
                }
                const auto& sample = mPicaSceneFrame.TemporalSample();
                Oot3d::DirectionalShadowMatrix giViewToWorld;
                if (!sample.Synthetic &&
                    mBrixelizerScene.AcceptsExecution(sample.CurrentSourceFrameId, draw.RenderTargetNamespace) &&
                    Oot3d::InvertDirectionalShadowMatrix(*draw.LightBinding.WorldToView, giViewToWorld)) {
                    uint32_t booleanUniforms = 0;
                    std::memcpy(&booleanUniforms, draw.VertexUniformBytes.data() +
                                Oot3d::kPicaPackedVertexBooleanMaskOffset, sizeof(booleanUniforms));
                    Renderer3ds::PicaGiGeometryCache transientGiCache;
                    auto& giCache = geometryResolution.Cacheable
                        ? mNativePicaGeometryBuffers.at(geometry.Identity).GiGeometry : transientGiCache;
                    const uint64_t giVersion = geometryResolution.Cacheable
                        ? geometry.ContentVersion ^ (geometry.StructuralSignature * 0x9e3779b97f4a7c15ULL) ^
                          geometry.Identity ^ draw.VertexShaderKey : 0;
                    const auto giInstance = Renderer3ds::BuildPicaGiInstance(
                        {geometry.Payload, sceneVertexBindings, sceneVertexLayout.Attributes,
                         sceneVertexLayout.Bindings, geometryBaseOffset, geometry.IndexOffset,
                         geometry.VertexOrIndexCount, draw.BaseVertex, geometry.Indexed},
                        ToPicaSceneTopology(draw.Topology), draw.TemporalVertexProgram.Hooks, booleanUniforms,
                        draw.VertexUniformBytes.subspan(Oot3d::kPicaPackedVertexFloatOffset),
                        giViewToWorld, giVersion, giCache);
                    mBrixelizerScene.Submit(giInstance);
                }
            }
            std::array<Oot3d::PicaSceneTextureBinding, 3>
                sceneTextures{};
            for (size_t slot = 0; slot < sceneTextures.size(); ++slot) {
                const auto* texture = textures[slot];
                auto& destination = sceneTextures[slot];
                destination.NativeImageHandle =
                    ToPicaSceneNativeHandle(texture->NriImage);
                destination.ReplacementContentHash =
                    texture->CustomReplacementPixelHash;
                destination.ImageWidth = texture->Width;
                destination.ImageHeight = texture->Height;
                destination.MipLevels = texture->UploadedMipLevels;
                destination.Slot = static_cast<uint8_t>(slot);
                destination.ImageFormat = integerTextures[slot]
                    ? Oot3d::PicaSceneTextureImageFormat::R32Uint
                    : Oot3d::PicaSceneTextureImageFormat::Rgba8;
                destination.CustomReplacement =
                    texture->CustomReplacementApplied;
            }
            for (const auto& texture : draw.Textures) {
                auto& destination = sceneTextures[texture.Slot];
                destination.NativeContentHash =
                    texture.NativeContentHash;
                destination.NativeBaseLevelContentHash =
                    texture.NativeBaseLevelContentHash;
                destination.PhysicalAddress = texture.PhysicalAddress;
                destination.SourceWidth = texture.Width;
                destination.SourceHeight = texture.Height;
                destination.LodBiasRaw = texture.LodBiasRaw;
                destination.NativeFormat = texture.NativeFormat;
                destination.NativeType = texture.NativeType;
                destination.NativeWrapS = texture.NativeWrapS;
                destination.NativeWrapT = texture.NativeWrapT;
                destination.MinMipLevel = texture.MinMipLevel;
                destination.MaxMipLevel = texture.MaxMipLevel;
                destination.Bound = true;
                destination.NativeContentHashAvailable =
                    texture.NativeContentHashAvailable;
                destination.NativeBaseLevelContentHashAvailable =
                    texture.NativeBaseLevelContentHashAvailable;
                if (!destination.NativeBaseLevelContentHashAvailable) {
                    const auto baseLevelHash =
                        ResolveNativeTextureBaseLevelContentHash(texture);
                    if (baseLevelHash.has_value()) {
                        destination.NativeBaseLevelContentHash =
                            *baseLevelHash;
                        destination.NativeBaseLevelContentHashAvailable =
                            true;
                    }
                }
                destination.MinLinear = texture.MinLinear;
                destination.MagLinear = texture.MagLinear;
                destination.MipLinear = texture.MipLinear;
            }
            Oot3d::PicaSceneTextureBinding orderedLightingLut;
            if (lightingLutInteger) {
                orderedLightingLut.NativeImageHandle = ToPicaSceneNativeHandle(lightingLutTexture->NriImage);
                orderedLightingLut.ImageWidth = lightingLutTexture->Width;
                orderedLightingLut.ImageHeight = lightingLutTexture->Height;
                orderedLightingLut.MipLevels = 1U;
                orderedLightingLut.ImageFormat = Oot3d::PicaSceneTextureImageFormat::R32Uint;
                orderedLightingLut.Bound = true;
            }
            const Oot3d::PicaSceneCullMode sceneCull = ToPicaSceneCullMode(draw);
            const Oot3d::PicaSceneFrontFace sceneFront = ToPicaSceneFrontFace(draw);
            mPicaSceneFrame.Record({
                .SubmissionId = draw.SubmissionId,
                .CommandListAddress = draw.CommandListAddress,
                .CommandListOffsetWords = draw.CommandListOffsetWords,
                .CompositionDomain = compositionDomain,
                .Composition = draw.Composition,
                .LightingWorldToView = draw.LightBinding.WorldToView,
                .SceneDirectionals = draw.LightBinding.SceneDirectionals,
                .ScenePoints = draw.LightBinding.ScenePoints,
                .NativeLightSources = draw.LightBinding.Sources,
                .ShadowCaster = shadowCaster,
                .Raster = {
                    .SchemaVersion =
                        Oot3d::kPicaSceneResolvedRasterStateSchemaVersion,
                    .NativeViewportX = draw.ViewportX,
                    .NativeViewportY = draw.ViewportY,
                    .NativeViewportWidth = draw.ViewportWidth,
                    .NativeViewportHeight = draw.ViewportHeight,
                    .ResolvedViewportX = mViewport.x,
                    .ResolvedViewportY = mViewport.y,
                    .ResolvedViewportWidth = mViewport.width,
                    .ResolvedViewportHeight = mViewport.height,
                    .DepthRange = draw.DepthRange,
                    .NearPlane = draw.NearPlane,
                    .ScissorMode = draw.ScissorMode,
                    .NativeScissorX1 = draw.ScissorX1,
                    .NativeScissorY1 = draw.ScissorY1,
                    .NativeScissorX2 = draw.ScissorX2,
                    .NativeScissorY2 = draw.ScissorY2,
                    .ResolvedScissorX = mScissor.offset.x,
                    .ResolvedScissorY = mScissor.offset.y,
                    .ResolvedScissorWidth = mScissor.extent.width,
                    .ResolvedScissorHeight = mScissor.extent.height,
                    .WBuffering = draw.WBuffering,
                    .FramebufferFlipped = draw.FramebufferFlipped,
                },
                .RenderTarget = {
                    .SchemaVersion =
                        Oot3d::kPicaSceneRenderTargetStateSchemaVersion,
                    .RenderTargetNamespace =
                        draw.RenderTargetNamespace,
                    .ColorPhysicalAddress =
                        draw.FramebufferColorPhysicalAddress,
                    .DepthPhysicalAddress =
                        draw.FramebufferDepthPhysicalAddress,
                    .NativeWidth = draw.FramebufferWidth,
                    .NativeHeight = draw.FramebufferHeight,
                    .NativeColorFormat =
                        draw.FramebufferColorFormat,
                    .NativeDepthFormat =
                        draw.FramebufferDepthFormat,
                    .SampleCount =
                        static_cast<uint32_t>(mNativePicaSampleCount),
                    .ResolvedColor = {
                        .NativeHandle = ToPicaSceneNativeHandle(
                            renderTarget.ColorImage),
                        .ResourceGeneration =
                            renderTarget.ColorSurfaceGeneration,
                        .Width = renderTarget.Width,
                        .Height = renderTarget.Height,
                        .Format = static_cast<uint32_t>(
                            VK_FORMAT_R8G8B8A8_UNORM),
                        .Sampleable = true,
                    },
                    .ResolvedDepth = {
                        .NativeHandle = ToPicaSceneNativeHandle(
                            renderTarget.DepthImage),
                        .ResourceGeneration =
                            renderTarget.DepthSurfaceGeneration,
                        .Width = renderTarget.Width,
                        .Height = renderTarget.Height,
                        .Format = static_cast<uint32_t>(mDepthFormat),
                        .Sampleable = true,
                    },
                },
                .CanonicalDescriptorSchemaVersion =
                    draw.CanonicalDescriptorSchemaVersion,
                .CanonicalVertexProgramId =
                    draw.CanonicalVertexProgramId,
                .CanonicalFragmentProgramId =
                    draw.CanonicalFragmentProgramId,
                .CanonicalRasterStateId =
                    draw.CanonicalRasterStateId,
                .CanonicalPipelineId = draw.CanonicalPipelineId,
                .CanonicalDynamicStateId =
                    draw.CanonicalDynamicStateId,
                .CanonicalFullRegisterStateId =
                    draw.CanonicalFullRegisterStateId,
                .VertexShaderKey = draw.VertexShaderKey,
                .VertexShaderSource = draw.VertexShaderSource,
                .FragmentShaderKey = draw.FragmentShaderKey,
                .FragmentShaderSource = draw.FragmentShaderSource,
                .EffectiveVertexShaderKey =
                    effectiveDraw.VertexShaderKey,
                .EffectiveVertexShaderSource =
                    effectiveDraw.VertexShaderSource,
                .EffectiveFragmentShaderKey =
                    effectiveDraw.FragmentShaderKey,
                .EffectiveFragmentShaderSource =
                    effectiveDraw.FragmentShaderSource,
                .GeometryIdentity = draw.GeometryIdentity,
                .GeometryContentVersion =
                    draw.GeometryContentVersion,
                .GeometryIdentityAvailable =
                    draw.GeometryIdentityAvailable,
                .VertexLayoutBindings = sceneVertexLayout.Bindings,
                .VertexLayoutAttributes = sceneVertexLayout.Attributes,
                .Material = {
                    .SchemaVersion =
                        Oot3d::kPicaSceneResolvedMaterialStateSchemaVersion,
                    .Topology = ToPicaSceneTopology(draw.Topology),
                    .CullMode = sceneCull,
                    .FrontFace = sceneFront,
                    .FragmentOperationMode =
                        draw.FragmentOperationMode,
                    .ColorWriteMask = draw.ColorWriteMask,
                    .LogicOperation = draw.LogicOperation,
                    .Blend = draw.Blend,
                    .AlphaTestEnabled = draw.AlphaTestEnabled,
                    .AlphaCompare = draw.AlphaCompare,
                    .AlphaReference = draw.AlphaReference,
                    .DepthTestEnabled = draw.DepthTestEnabled,
                    .DepthWriteEnabled = draw.DepthWriteEnabled,
                    .DepthCompare = draw.DepthCompare,
                    .Stencil = draw.Stencil,
                    .FragmentFeatures = draw.FragmentFeatures,
                },
                .UniformBuffer = {
                    ToPicaSceneNativeHandle(frame.UniformBuffer.Buffer),
                    frame.UniformBuffer.Size,
                },
                .VertexUniformOffset = vertexUniformOffset,
                .VertexUniformSize = draw.VertexUniformBytes.size(),
                .PreviousVertexUniformOffset =
                    previousVertexUniformOffset,
                .PreviousVertexUniformSize =
                    exactMotionHistory
                        ? previousVertexUniformBytes.size()
                        : 0U,
                .PreviousVertexUniformHistoryAvailable =
                    exactMotionHistory,
                .FragmentUniformOffset = fragmentUniformOffset,
                .FragmentUniformSize =
                    draw.FragmentUniformBytes.size(),
                .SurfaceLightingUniformOffset = uniformSurface ? surfaceUniformOffset : 0U,
                .SurfaceLightingUniformSize = uniformSurface ? surfaceUniformSize : 0U,
                .GeometryBuffer = {
                    ToPicaSceneNativeHandle(geometryBuffer),
                    geometryBufferSize,
                },
                .VertexBindings = sceneVertexBindings,
                .Indexed = draw.Indexed,
                .IndexOffset = indexOffset,
                .VertexOrIndexCount =
                    draw.Indexed ? indexCount : draw.VertexCount,
                .BaseVertex = draw.BaseVertex,
                .Textures = sceneTextures,
                .LightingLut = orderedLightingLut,
                .PerspectiveProjection = grassPerspectiveProjection,
                .DirectionalShadowCaster = directionalShadowCaster,
                .VertexShaderHooks =
                    draw.TemporalVertexProgram.Hooks,
                .FragmentShaderHooks = draw.FragmentShaderHooks,
                .PackedVertexUniforms = draw.VertexUniformBytes,
                .PackedPreviousVertexUniforms =
                    exactMotionHistory
                        ? previousVertexUniformBytes
                        : std::span<const uint8_t>{},
                .PackedFragmentUniforms = draw.FragmentUniformBytes,
                .VertexSourceIdentity = draw.VertexShaderSourceIdentity,
                .FragmentSourceIdentity = draw.FragmentShaderSourceIdentity,
                .EffectiveVertexSourceIdentity = effectiveDraw.VertexShaderSourceIdentity,
                .EffectiveFragmentSourceIdentity = effectiveDraw.FragmentShaderSourceIdentity,
            });
        }
        const auto* coveragePipeline = drawVariant.FragmentOutputs.SceneDomainTransparentDepthOverlay
            ? &GetOrCreateNativePicaPipeline(
                *selectedDraw, *selectedProgram, drawVariant.Reactive,
                drawVariant.Domain, drawVariant.RequestedFeatures, drawVariant.AppliedFeatures,
                true, true, !nriOwnedDraw,
                selectedDraw == &uberDraw && nriOwnedDraw
                    ? NativePicaPipelineAcquisition::Libraries
                    : NativePicaPipelineAcquisition::Blocking)
            : nullptr;
        // Coverage runs before the native draw: stencil must still have its
        // original value if that draw modifies it. The auxiliary pass is read-only.
        nriDraw.PipelineId = coveragePipeline ? coveragePipeline->Id : pipelineRecord.Id;
        const bool nriOwnedDrawBound =
            nriOwnedDraw &&
            mNriPicaPipelineBridge.BindOwnedDraw(nriDraw);
        if (nriOwnedDraw && !nriOwnedDrawBound) {
            throw std::runtime_error(
                "NRI PICA draw binding failed in an NRI-owned "
                "rendering scope");
        }
        if (nriOwnedDrawBound && coveragePipeline != nullptr) {
            nriDraw.PipelineId = pipelineRecord.Id;
            if (!mNriPicaPipelineBridge.DrawBoundGeometry(nriDraw))
                throw std::runtime_error("NRI native draw after outline coverage failed");
        }
        bool nriPipelineBound = nriOwnedDrawBound;
        if (!nriOwnedDrawBound) {
            pipeline = GetOrCreateNativePicaPipeline(
                *selectedDraw, *selectedProgram, drawVariant.Reactive,
                drawVariant.Domain, drawVariant.RequestedFeatures,
                drawVariant.AppliedFeatures, false, false, true).Vulkan;
            nriPipelineBound =
                mNriPicaPipelineBridge.Bind(
                    mCurrentFrame, pipeline);
            if (!nriPipelineBound)
                vkCmdBindPipeline(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            for (const auto& [binding, offset] :
                 bindingOffsets) {
                vkCmdBindVertexBuffers(commandBuffer, binding, 1, &geometryBuffer, &offset);
            }
            vkCmdBindDescriptorSets(
                commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                mNativePicaPipelineLayout, 0, 1, &descriptor,
                0, nullptr);
            vkCmdSetViewport(commandBuffer, 0, 1, &mViewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &mScissor);
            vkCmdSetBlendConstants(
                commandBuffer, draw.Blend.ConstantColor);
            vkCmdPushConstants(
                commandBuffer, mNativePicaPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT |
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(nativeDrawPush), &nativeDrawPush);
            if (draw.Indexed) {
                vkCmdBindIndexBuffer(commandBuffer, geometryBuffer, indexOffset, VK_INDEX_TYPE_UINT16);
                if (coveragePipeline != nullptr) {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, coveragePipeline->Vulkan);
                    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, draw.BaseVertex, 0);
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                }
                vkCmdDrawIndexed(
                    commandBuffer, indexCount, 1, 0,
                    draw.BaseVertex, 0);
            } else {
                if (coveragePipeline != nullptr) {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, coveragePipeline->Vulkan);
                    vkCmdDraw(commandBuffer, draw.VertexCount, 1, 0, 0);
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                }
                vkCmdDraw(
                    commandBuffer, draw.VertexCount, 1, 0, 0);
            }
        }
        if (coveragePipeline != nullptr) {
            ++mDrawCallCountThisFrame;
            mDiagnostics.RecordOutlineOcclusionDraw();
        }
        finishCpuStage(cpuTimings.CommandMilliseconds);
        if (collectCpuTimings) {
            cpuTimings.TotalMilliseconds =
                std::chrono::duration<double, std::milli>(
                    CpuClock::now() - cpuStart).count();
            mDiagnostics.RecordNativePicaCpuTimings(cpuTimings);
        }
        mDiagnostics.RecordNriPicaPipelineBind(
            nriPipelineBound);
        mDiagnostics.RecordNriPicaOwnedPipeline(
            mNriPicaPipelineBridge.OwnedPipelineReady(pipelineRecord.Id));
        mDiagnostics.RecordNriPicaOwnedDraw(
            nriOwnedDrawBound,
            mNriPicaPipelineBridge.DescriptorsOwnedByNri(),
            mNriPicaPipelineBridge.LastDrawUploadsOwnedByNri(),
            mNriPicaPipelineBridge.LastDrawUploadedBytes());
        if (opaqueWorldDraw) {
            if (mInteractiveGrassProviderPlan.Valid()) {
                mGrassWorldTargetsThisFrame.insert(renderTarget.Key);
            }
        }
        if (draw.CompositionDomain ==
                ::Oot3d::Renderer::PicaCompositionDomain::Scene &&
            draw.DepthWriteEnabled) {
            ++mNativePicaDepthWritingDrawsThisFrame;
        }
        ++mDrawCallCountThisFrame;
        mDiagnostics.RecordNativePicaDraw(
            draw.SubmissionId, draw.RenderTargetNamespace,
            draw.Indexed ? indexCount : draw.VertexCount, draw.Indexed,
            draw.FramebufferColorPhysicalAddress,
            draw.FramebufferDepthPhysicalAddress, draw.FramebufferWidth,
            draw.FramebufferHeight, draw.DepthRange, draw.NearPlane,
            draw.DepthTestEnabled, draw.DepthWriteEnabled,
            draw.AlphaTestEnabled,
            Oot3d::ResolvePicaAlphaCoveragePolicy(
                mNativePicaSampleCount, draw.AlphaTestEnabled,
                draw.DepthTestEnabled,
                draw.DepthWriteEnabled, UsesTranslucentBlend(draw.Blend),
                draw.ColorWriteMask).Enable);
        return true;
    } catch (const std::exception& exception) {
        SetNativeError(error, exception.what());
        return false;
    }
}

bool GfxRenderingAPIVulkan::TryRenderInteractiveGrass(
    NativePicaRenderTarget& target,
    const Oot3d::GraphicsSettings& settings,
    Oot3d::EffectGeometryProviderInvocationKind invocationKind) {
    Renderer3ds::SlowStepScope slowStep("interactive_grass");
    const auto latestPerspective = SamplePerspective(&target);
    (void)mNativeSceneView.Publish(
        mFrameCounter, mPicaSceneFrame, latestPerspective);
    (void)mPicaScenePublications.Publish(
        mFrameCounter, mPicaSceneFrame, mNativeSceneView);
    const auto* perspective =
        mPicaScenePublications.ResolvePerspectiveCamera();
    const Oot3d::EffectGeometryProviderInvocation invocation{
        invocationKind,
        Oot3d::EffectStage::BeforeTransparent,
        {
            mPicaScenePublications.View(),
        },
        mGrassWorldTargetsThisFrame.contains(target.Key),
    };
    const auto decision =
        mInteractiveGrassProviderPlan.ResolveInvocation(invocation);
    if (!decision.Authorized() || perspective == nullptr) {
        mInteractiveGrassProviderExecution.RecordInvocation(
            invocationKind, decision,
            Oot3d::EffectGeometryProviderExecutionOutcome::Skipped);
        return false;
    }
    if (!mGrassInsertionAttemptedTargetsThisFrame
             .insert(target.Key)
             .second) {
        const bool reused =
            mGrassRenderedTargetsThisFrame.contains(target.Key);
        mInteractiveGrassProviderExecution.RecordInvocation(
            invocationKind, decision,
            reused
                ? Oot3d::EffectGeometryProviderExecutionOutcome::Reused
                : Oot3d::EffectGeometryProviderExecutionOutcome::Skipped);
        return reused;
    }
    EndNativePicaRenderPass();
    mGpuProfiler.BeginScope(
        mCurrentFrame, Oot3d::GpuProfileScope::Grass,
        mCommandBuffers[mCurrentFrame]);
    Oot3d::NriDirectionalShadowHistorySnapshot grassShadowHistory;
    if (settings.Grass.Appearance.ReceiveLighting &&
        mInteractiveGrassProviderPlan.Requires(Oot3d::EffectResource::DirectionalShadowHistory)) {
        // Resolve atlas and page-table version together at the declared native
        // insertion anchor, just like other receivers at this boundary.
        grassShadowHistory = mNriDirectionalShadowPass.FindLatestHistory(
            target.Key.RenderTargetNamespace, target.Key.ColorPhysicalAddress, mFrameCounter);
    }
    const bool prepared = mInteractiveGrassPass.Prepare(
        mCommandBuffers[mCurrentFrame], target.Width,
        target.Height, *perspective, settings.Grass,
        Oot3d::PackToonSurfaceParameters(settings.Effects.Toon, settings.Effects.ToonStyle, false),
        mPicaSceneFrame.ResolvedDrawStream(),
        mFrameCounter, target.Key.RenderTargetNamespace,
        target.Key.ColorPhysicalAddress, mCurrentFrame,
        mInteractiveGrassProviderPlan, grassShadowHistory,
        mTemporalJitterPixels, mTemporalJitterEnabled, mTemporalMotionEnabled);
    bool executed = false;
    if (prepared) {
        BeginNativePicaRenderPass(target);
        mGpuProfiler.Mark(mCurrentFrame, "grass_draw", mCommandBuffers[mCurrentFrame]);
        executed = mInteractiveGrassPass.DrawPrepared(
            mCommandBuffers[mCurrentFrame],
            target.Width, target.Height,
            mInteractiveGrassProviderPlan,
            mFramePicaAttachmentRequirements);
    }
    mGpuProfiler.EndScope(
        mCurrentFrame, Oot3d::GpuProfileScope::Grass,
        mCommandBuffers[mCurrentFrame]);
    mGrassExecutedThisFrame |= executed;
    if (executed) {
        mGrassRenderedTargetsThisFrame.insert(target.Key);
        mDiagnostics.RecordGrassMotion(
            mInteractiveGrassPass.LastMotionBladeCount());
    }
    const auto grassTelemetry =
        Oot3d::GrassRenderTelemetry::Instance().Snapshot();
    const bool failed =
        grassTelemetry.Status == Oot3d::GrassRenderStatus::Error ||
        (prepared && !executed);
    mInteractiveGrassProviderExecution.RecordInvocation(
        invocationKind, decision,
        executed
            ? Oot3d::EffectGeometryProviderExecutionOutcome::Executed
            : failed
                  ? Oot3d::EffectGeometryProviderExecutionOutcome::Failed
                  : Oot3d::EffectGeometryProviderExecutionOutcome::Skipped);

    if (std::getenv("OOT3D_GRASS_DIAGNOSTICS") != nullptr ||
        std::getenv("OOT3D_GRAPHICS_GRASS_AUTO") != nullptr) {
        static uint32_t grassSmokeReports = 0U;
        static uint64_t lastGrassSmokeFrame = 0U;
        static auto lastGrassSmokeStatus = Oot3d::GrassRenderStatus::Disabled;
        const auto telemetry =
            Oot3d::GrassRenderTelemetry::Instance().Snapshot();
        const bool initialReport = grassSmokeReports < 3U;
        const bool periodicReport =
            telemetry.FrameId >= lastGrassSmokeFrame + 15U;
        const bool diagnosticEvent =
            (telemetry.PlacementCacheMisses != 0U ||
             telemetry.CpuMilliseconds > 16.0) &&
            telemetry.FrameId >= lastGrassSmokeFrame + 5U;
        const bool statusChanged = telemetry.Status != lastGrassSmokeStatus;
        if (initialReport || periodicReport || diagnosticEvent || statusChanged) {
            ++grassSmokeReports;
            lastGrassSmokeFrame = telemetry.FrameId;
            lastGrassSmokeStatus = telemetry.Status;
            std::fprintf(
                stderr,
                "OOT3D grass smoke: frame=%llu status=%s camera=%d meshes=%zu "
                "textures=%zu rules=%zu masks=%zu matches=%zu "
                "scoped=%zu placements=%zu cache=%zu/%zu pending=%zu clusters=%zu "
                "visibility_nodes=%llu candidate_clusters=%llu "
                "world_cache=%llu/%llu/%zu "
                "async_build=%llu/%llu/%.3f "
                "anchors=%llu evaluated=%llu blades=%u draws=%u workers=%u "
                "compact=%d upload=%llu/%llu/%llu "
                "place_ms=%.3f select_ms=%.3f copy_ms=%.3f "
                "cpu_ms=%.3f miss_geometry=%llx miss_content=%llx "
                "reason=%s\n",
                static_cast<unsigned long long>(telemetry.FrameId),
                Oot3d::GrassRenderStatusName(telemetry.Status),
                perspective->CameraAvailable ? 1 : 0,
                telemetry.AvailableMeshes,
                telemetry.AvailableTextures,
                telemetry.ConfiguredRules, telemetry.ReadyMasks,
                telemetry.MatchingMeshes, telemetry.ScopedMeshes,
                telemetry.Placements,
                telemetry.PlacementCacheHits,
                telemetry.PlacementCacheMisses,
                telemetry.PendingPlacements,
                telemetry.Clusters,
                static_cast<unsigned long long>(telemetry.VisibilityNodesTested),
                static_cast<unsigned long long>(telemetry.CandidateClusters),
                static_cast<unsigned long long>(
                    telemetry.WorldPlacementCacheHits),
                static_cast<unsigned long long>(
                    telemetry.WorldPlacementCacheMisses),
                telemetry.WorldPlacementCacheEntries,
                static_cast<unsigned long long>(
                    telemetry.AsyncPlacementBuilds),
                static_cast<unsigned long long>(
                    telemetry.AsyncPlacementFailures),
                telemetry.AsyncPlacementBuildMilliseconds,
                static_cast<unsigned long long>(
                    telemetry.ExtractedAnchors),
                static_cast<unsigned long long>(
                    telemetry.EvaluatedAnchors),
                telemetry.VisibleBlades, telemetry.DrawCalls,
                telemetry.CullingWorkers,
                telemetry.GpuCompaction ? 1 : 0,
                static_cast<unsigned long long>(
                    telemetry.UploadedBytes),
                static_cast<unsigned long long>(
                    telemetry.DynamicUploadedBytes),
                static_cast<unsigned long long>(
                    telemetry.StaticUploadedBytes),
                telemetry.PlacementMilliseconds,
                telemetry.SelectionMilliseconds,
                telemetry.UploadMilliseconds,
                telemetry.CpuMilliseconds,
                static_cast<unsigned long long>(
                    telemetry.LastMissGeometryId),
                static_cast<unsigned long long>(
                    telemetry.LastMissContentVersion),
                mInteractiveGrassPass.UnavailableReason().c_str());
        }
    }
    static bool grassPassReported = false;
    if (mGrassExecutedThisFrame && !grassPassReported) {
        SPDLOG_INFO(
            "OOT3D Vulkan interactive grass drew {} blades",
            mInteractiveGrassPass.LastBladeCount());
        grassPassReported = true;
    }
    return executed;
}

bool GfxRenderingAPIVulkan::TryRenderDirectionalShadowMap(
    NativePicaRenderTarget& target,
    const Oot3d::GraphicsSettings& settings,
    const Oot3d::PicaCompositionStageAnchor& anchor) {
    Renderer3ds::SlowStepScope slowStep("directional_shadows");
    const auto boundary = Oot3d::BuildPicaExtensionScheduleBoundary(
        mNativePicaCompositionSchedule, anchor);
    const auto decision = mDirectionalShadowSchedulePlan.Resolve(boundary);
    if (!decision.Authorized()) {
        mDiagnostics.RecordNriDirectionalShadowSchedule(
            false, false,
            boundary.Kind ==
                ::Fast::Renderer::ExtensionScheduleBoundaryKind::SurfaceEnd);
        return false;
    }
    if (!mDirectionalShadowInsertionAttemptedTargetsThisFrame
             .insert({boundary.ScheduleId, target.Key})
             .second) {
        mDiagnostics.RecordNriDirectionalShadowSchedule(
            true, true,
            boundary.Kind ==
                ::Fast::Renderer::ExtensionScheduleBoundaryKind::SurfaceEnd);
        return false;
    }
    mDiagnostics.RecordNriDirectionalShadowSchedule(
        true, false,
        boundary.Kind ==
            ::Fast::Renderer::ExtensionScheduleBoundaryKind::SurfaceEnd);

    const auto latestPerspective = SamplePerspective(&target);
    (void)mNativeSceneView.Publish(
        mFrameCounter, mPicaSceneFrame, latestPerspective);
    // The map reads this sample's prepared casters, complete at the opaque
    // boundary before any of their receivers draws.
    (void)mPreparedShadowCasterView.Publish(
        mFrameCounter, mPreparedShadowCasterFrame, latestPerspective);
    mDiagnostics.RecordNriDirectionalShadowPreparedMap(
        boundary.Stage == Oot3d::EffectStage::BeforeOpaque);
    EndNativePicaRenderPass();
    if (mRenderPassActive) {
        vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
        mRenderPassActive = false;
        mOverlayRenderPassActive = false;
    }

    mGpuProfiler.Mark(mCurrentFrame, "directional_shadows", mCommandBuffers[mCurrentFrame]);
    const bool executionRequested = mPreparedShadowCasterView.Active();
    const bool executed =
        executionRequested &&
        mNriDirectionalShadowPass.ExecuteMap(
            {
                mCurrentFrame,
                mFrameCounter,
                target.Key.RenderTargetNamespace,
                target.Key.ColorPhysicalAddress,
                decision.Authorization,
                boundary.Surface,
                settings.Effects.DirectionalShadows,
                mFrameEnvironment.Enabled
                    ? std::optional(mFrameEnvironment.Lights[0]) : std::nullopt,
                mFrameEnvironment.Enabled ? mFrameEnvironment.NativeDirectStrength : 1.0F,
                mFrameEnvironment.Enabled ? &mFrameEnvironment : nullptr,
            },
            mPreparedShadowCasterView,
            mDirectionalShadowMapBarrierPlan);
    if (executionRequested) {
        const auto& shadowMapTransitions =
            mNriDirectionalShadowPass.LastShadowMapBarrierExecution();
        mDiagnostics.RecordEffectGraphInternalImageTransitions(
            shadowMapTransitions.PlannedGraphTransitionCount(),
            shadowMapTransitions.EmittedGraphTransitionCount(),
            shadowMapTransitions.PlannedPrivateTransitionCount(),
            shadowMapTransitions.EmittedPrivateTransitionCount());
        mDiagnostics.RecordNriDirectionalShadowGraphTransitions(
            shadowMapTransitions.PlannedGraphTransitionCount(),
            shadowMapTransitions.EmittedGraphTransitionCount());
    }

    const auto sceneStats = mPreparedShadowCasterFrame.Stats();
    mDiagnostics.RecordNriDirectionalShadowAttempt(
        sceneStats.DirectionalShadowCasterCount,
        sceneStats.DirectionalShadowNativeLightCasterCount,
        static_cast<uint32_t>(
            mNriDirectionalShadowPass.LastExecutionStage()));
    if (executed) {
        const auto& lightTelemetry =
            mNriDirectionalShadowPass.LastLightTelemetry();
        mDiagnostics.RecordNriVirtualShadowPages(
            lightTelemetry.Pages, lightTelemetry.UpdatedPages, lightTelemetry.AtlasBytes);
        const auto& keyframes = mNriDirectionalShadowPass.LastKeyframeTelemetry();
        mDiagnostics.RecordNriVirtualShadowKeyframes(
            keyframes.DisplayedLayouts, keyframes.NewerWeight, keyframes.ConstructionPages,
            keyframes.ConstructionBudget, keyframes.Cut, keyframes.Rotated,
            keyframes.DisplayedDirectionTowardSource, keyframes.LagDegrees);
        mDiagnostics.RecordNriDirectionalShadowLightSelection(
            lightTelemetry.CandidateCount,
            lightTelemetry.ClusterCount,
            lightTelemetry.GeometryLightWeight,
            lightTelemetry.WorldDirectionTowardSource[0],
            lightTelemetry.WorldDirectionTowardSource[1],
            lightTelemetry.WorldDirectionTowardSource[2]);
        mDiagnostics.RecordNriDirectionalShadow(
            mNriDirectionalShadowPass.LastCasterCount(),
            mNriDirectionalShadowPass.LastExecutionUsedNativeLight(),
            mNriDirectionalShadowPass.LastAlphaCasterCount());
    }
    return executed;
}

bool GfxRenderingAPIVulkan::TryRenderPointShadowMaps(
    NativePicaRenderTarget& target,
    const Oot3d::GraphicsSettings& settings,
    const Oot3d::PicaCompositionStageAnchor& anchor) {
    Renderer3ds::SlowStepScope slowStep("point_shadows");
    const auto boundary = Oot3d::BuildPicaExtensionScheduleBoundary(
        mNativePicaCompositionSchedule, anchor);
    const auto decision = mPointShadowSchedulePlan.Resolve(boundary);
    if (!decision.Authorized() ||
        !mPointShadowInsertionAttemptedTargetsThisFrame.insert({boundary.ScheduleId, target.Key}).second)
        return false;
    // The same prepared casters as the directional map, complete at the
    // opaque boundary before any of their receivers draws.
    (void)mPreparedShadowCasterView.Publish(
        mFrameCounter, mPreparedShadowCasterFrame, SamplePerspective(&target));
    if (!mPreparedShadowCasterView.Active())
        return false;
    EndNativePicaRenderPass();
    if (mRenderPassActive) {
        vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
        mRenderPassActive = false;
        mOverlayRenderPassActive = false;
    }
    mGpuProfiler.Mark(mCurrentFrame, "point_shadows", mCommandBuffers[mCurrentFrame]);
    const bool executed = mNriDirectionalShadowPass.ExecutePointMaps(
        {
            mCurrentFrame,
            mFrameCounter,
            target.Key.RenderTargetNamespace,
            target.Key.ColorPhysicalAddress,
            decision.Authorization,
            boundary.Surface,
            settings.Effects.PointShadows,
            mFrameEnvironment,
        },
        mPreparedShadowCasterView,
        mPointShadowMapBarrierPlan);
    const auto& telemetry = mNriDirectionalShadowPass.LastPointTelemetry();
    mDiagnostics.RecordNriPointShadowMap(
        executed, telemetry.CandidateLights, telemetry.Lights, telemetry.RenderedLights,
        telemetry.CasterDraws, telemetry.AlphaCasterDraws);
    return executed;
}

const GfxNativePicaVertexBindingView* FindNativeBinding(
    const GfxNativePicaDrawView& draw, uint8_t binding) {
    const auto found = std::find_if(
        draw.VertexBindings.begin(), draw.VertexBindings.end(),
        [binding](const auto& value) { return value.Binding == binding; });
    return found == draw.VertexBindings.end() ? nullptr : &*found;
}

const GfxNativePicaVertexAttributeView* FindNativeAttribute(
    const GfxNativePicaDrawView& draw, uint8_t location) {
    const auto found = std::find_if(
        draw.VertexAttributes.begin(), draw.VertexAttributes.end(),
        [location](const auto& value) { return value.Location == location; });
    return found == draw.VertexAttributes.end() ? nullptr : &*found;
}

bool PublishNativePicaGrassSurface(
    const GfxNativePicaDrawView& draw, uint64_t frameId,
    std::unordered_map<uint64_t, uint32_t>& occurrences,
    const Oot3d::InteractiveGrassSettings& settings,
    bool worldSurface, bool fragmentUsesTexture0,
    bool perspectiveProjection,
    Oot3d::GrassGeometryRegistry& geometryRegistry) {
    if (settings.Rules.empty()) {
        return false;
    }
    const auto texture = std::find_if(
        draw.Textures.begin(), draw.Textures.end(),
        [](const auto& value) { return value.Slot == 0U; });
    const Oot3d::GrassSurfaceDrawInfo eligibility{
        draw.FragmentOperationMode == 0U,
        worldSurface,
        texture != draw.Textures.end(),
        fragmentUsesTexture0,
        draw.PositionAttributeLocation != 0xff,
        draw.TexCoord0AttributeLocation != 0xff,
        draw.DepthTestEnabled,
        draw.DepthWriteEnabled,
        draw.DepthCompare ==
            GfxNativePicaCompareFunction::Always,
        perspectiveProjection,
        (draw.ColorWriteMask & 0x7U) != 0U,
        UsesTranslucentBlend(draw.Blend),
        Oot3d::IsGrassPrimitiveTopologySupported(
            draw.Topology),
    };
    const bool eligible =
        Oot3d::IsGrassSurfaceDrawEligible(eligibility);
    static const bool diagnostics =
        std::getenv("OOT3D_GRASS_DIAGNOSTICS") != nullptr;
    if (!eligible && !diagnostics) {
        return false;
    }
    const auto textureHash =
        ResolveNativeTextureBaseLevelContentHash(*texture);
    if (!textureHash.has_value()) {
        return false;
    }
    const uint64_t observedTextureHash =
        Oot3d::GrassTextureSourceCache::Instance()
            .ResolveObservedHash(
                *textureHash, texture->Width,
                texture->Height);
    const bool configuredSource = std::any_of(
        settings.Rules.begin(), settings.Rules.end(),
        [&](const auto& rule) {
            return rule.Target.Rgba8Hash ==
                       observedTextureHash &&
                (rule.Target.MapperSlotMask & 1U) != 0U &&
                (rule.Target.Width == 0U ||
                 rule.Target.Width == texture->Width) &&
                (rule.Target.Height == 0U ||
                 rule.Target.Height == texture->Height);
        });
    if (diagnostics) {
        static uint32_t sourceDrawReports = 0U;
        if (sourceDrawReports++ < 128U) {
            std::fprintf(
                stderr,
                "OOT3D grass source draw: hash=%016llx observed=%016llx slot=%u size=%ux%u configured=%d eligible=%d "
                "op=%d world=%d tex0=%d sample0=%d "
                "pos=%d uv=%d depth=%d/%d cmpalways=%d perspective=%d rgb=%d translucent=%d topology=%d\n",
                static_cast<unsigned long long>(*textureHash),
                static_cast<unsigned long long>(observedTextureHash),
                texture->Slot, texture->Width, texture->Height,
                configuredSource ? 1 : 0, eligible ? 1 : 0,
                eligibility.ColorOperation ? 1 : 0,
                eligibility.WorldSurface ? 1 : 0,
                eligibility.HasTexture0 ? 1 : 0,
                eligibility.FragmentUsesTexture0 ? 1 : 0,
                eligibility.HasPosition ? 1 : 0,
                eligibility.HasTexCoord0 ? 1 : 0,
                eligibility.DepthTest ? 1 : 0,
                eligibility.DepthWrite ? 1 : 0,
                eligibility.DepthCompareAlways ? 1 : 0,
                eligibility.PerspectiveProjection ? 1 : 0,
                eligibility.WritesRgb ? 1 : 0,
                eligibility.TranslucentBlend ? 1 : 0,
                static_cast<int>(draw.Topology));
        }
    }
    if (!eligible || !configuredSource) {
        return false;
    }
    const auto rejectConfiguredSource = [&](const char* reason) {
        if (diagnostics) {
            static uint32_t rejectionReports = 0U;
            if (rejectionReports++ < 64U) {
                std::fprintf(
                    stderr,
                    "OOT3D grass configured source rejected: hash=%016llx reason=%s\n",
                    static_cast<unsigned long long>(*textureHash), reason);
            }
        }
        return false;
    };
    const auto materialWrapS =
        DecodeNativePicaGrassWrap(texture->NativeWrapS);
    const auto materialWrapT =
        DecodeNativePicaGrassWrap(texture->NativeWrapT);
    if (!materialWrapS.has_value() ||
        !materialWrapT.has_value()) {
        return rejectConfiguredSource("unsupported texture wrap");
    }
    const auto* position = FindNativeAttribute(draw, draw.PositionAttributeLocation);
    const auto* uv = FindNativeAttribute(draw, draw.TexCoord0AttributeLocation);
    if (position == nullptr || uv == nullptr || position->ComponentCount < 3U ||
        uv->ComponentCount < 2U) {
        return rejectConfiguredSource("invalid vertex attributes");
    }
    const auto* positionBinding = FindNativeBinding(draw, position->Binding);
    const auto* uvBinding = FindNativeBinding(draw, uv->Binding);
    if (positionBinding == nullptr || uvBinding == nullptr ||
        positionBinding->PerInstance || uvBinding->PerInstance) {
        return rejectConfiguredSource("invalid vertex bindings");
    }
    const auto textureCoordinateTransform =
        Oot3d::DecodePicaGrassTextureCoordinateTransform(
            draw.VertexShaderSource,
            draw.FragmentShaderSource,
            draw.VertexUniformBytes,
            draw.TexCoord0AttributeLocation,
            &draw.TemporalVertexProgram.Hooks,
            &draw.FragmentShaderHooks);
    if (!textureCoordinateTransform.Applied()) {
        return rejectConfiguredSource(
            Oot3d::PicaGrassTextureCoordinateEligibilityName(
                textureCoordinateTransform.Eligibility));
    }

    // OoT3D's common rigid CMB vertex program transforms attribute 0 through
    // f[20..22], then f[4..6], and finally projection f[0..3]. Preserve that
    // exact model-to-clip path for rendering. A separately reconstructed
    // model-to-world anchor is used only for placement and interaction.
    // Skinned draws select a matrix palette with vertex attributes and need a
    // separate producer; they are intentionally excluded here.
    constexpr size_t kVertexBooleanMaskOffset = 0U;
    constexpr size_t kVertexFloatUniformOffset =
        16U + 4U * 4U * sizeof(uint32_t);
    constexpr size_t kProjectionUniformFirst = 0U;
    constexpr size_t kProjectionUniformRows = 4U;
    constexpr size_t kViewUniformFirst = 4U;
    constexpr size_t kViewUniformRows = 3U;
    constexpr size_t kModelUniformFirst = 20U;
    constexpr size_t kModelUniformRows = 3U;
    constexpr size_t kVec4Bytes = 4U * sizeof(float);
    const auto uniformOffset = [](size_t first) {
        return kVertexFloatUniformOffset + first * kVec4Bytes;
    };
    const size_t modelOffset = uniformOffset(kModelUniformFirst);
    if (draw.VertexUniformBytes.size() <
        modelOffset + kModelUniformRows * kVec4Bytes) {
        return rejectConfiguredSource("vertex uniforms unavailable");
    }
    uint32_t booleanMask = 0;
    std::memcpy(&booleanMask,
                draw.VertexUniformBytes.data() + kVertexBooleanMaskOffset,
                sizeof(booleanMask));
    if ((booleanMask & 4U) != 0U) {
        return rejectConfiguredSource("skinned geometry unsupported");
    }

    const auto identity = [] {
        return std::array<float, 16>{
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F};
    };
    const auto multiply = [](const std::array<float, 16>& left,
                             const std::array<float, 16>& right) {
        std::array<float, 16> result{};
        for (size_t row = 0; row < 4U; ++row) {
            for (size_t column = 0; column < 4U; ++column) {
                for (size_t inner = 0; inner < 4U; ++inner) {
                    result[row * 4U + column] +=
                        left[row * 4U + inner] *
                        right[inner * 4U + column];
                }
            }
        }
        return result;
    };
    std::array<float, 16> projection{};
    std::array<float, 16> viewTransform = identity();
    std::array<float, 16> modelTransform = identity();
    std::memcpy(projection.data(),
                draw.VertexUniformBytes.data() +
                    uniformOffset(kProjectionUniformFirst),
                kProjectionUniformRows * kVec4Bytes);
    std::memcpy(viewTransform.data(),
                draw.VertexUniformBytes.data() +
                    uniformOffset(kViewUniformFirst),
                kViewUniformRows * kVec4Bytes);
    std::memcpy(modelTransform.data(),
                draw.VertexUniformBytes.data() + modelOffset,
                kModelUniformRows * kVec4Bytes);
    const auto finiteMatrix = [](const auto& matrix) {
        return std::all_of(matrix.begin(), matrix.end(),
                           [](float value) { return std::isfinite(value); });
    };
    if (!finiteMatrix(projection) || !finiteMatrix(viewTransform) ||
        !finiteMatrix(modelTransform)) {
        return rejectConfiguredSource("non-finite transform");
    }
    const auto modelToView = multiply(viewTransform, modelTransform);

    Oot3d::DirectionalShadowMatrix capturedViewToWorld;
    if (!draw.LightBinding.WorldToView ||
        !Oot3d::InvertDirectionalShadowMatrix(*draw.LightBinding.WorldToView, capturedViewToWorld)) {
        return rejectConfiguredSource("captured camera unavailable");
    }
    // Placement and lighting must undo the camera paired with these exact
    // vertex uniforms, not the later live gameplay camera (or an Eye/At
    // reconstruction that loses roll).
    std::array<float, 16> viewToWorld;
    for (size_t row = 0; row < 4; ++row)
        for (size_t column = 0; column < 4; ++column)
            viewToWorld[row * 4 + column] = capturedViewToWorld[column * 4 + row];
    const auto modelToWorld = multiply(viewToWorld, modelToView);
    auto picaModelToClip = multiply(projection, modelToView);
    if (draw.FramebufferFlipped) {
        for (size_t column = 0; column < 4U; ++column) {
            picaModelToClip[1U * 4U + column] *= -1.0F;
        }
    }
    const float determinant =
        modelToWorld[0] * (modelToWorld[5] * modelToWorld[10] -
                           modelToWorld[6] * modelToWorld[9]) -
        modelToWorld[1] * (modelToWorld[4] * modelToWorld[10] -
                           modelToWorld[6] * modelToWorld[8]) +
        modelToWorld[2] * (modelToWorld[4] * modelToWorld[9] -
                           modelToWorld[5] * modelToWorld[8]);
    if (std::abs(determinant) < 1.0e-12F) {
        return rejectConfiguredSource("degenerate model transform");
    }

    const auto grassGeometryResolution =
        geometryRegistry.Resolve({
            draw.GeometryIdentity,
            draw.GeometryContentVersion,
            draw.GeometryIdentityAvailable,
            frameId,
            {
                positionBinding->Bytes,
                positionBinding->ByteStride,
                position->Format,
                position->ComponentCount,
                position->ByteOffset,
            },
            {
                uvBinding->Bytes,
                uvBinding->ByteStride,
                uv->Format,
                uv->ComponentCount,
                uv->ByteOffset,
            },
            draw.IndexBytes,
            draw.Indexed,
            draw.IndicesAre16Bit,
            draw.BaseVertex,
            draw.VertexCount,
            draw.Topology,
            textureCoordinateTransform,
        });
    if (grassGeometryResolution.Geometry == nullptr) {
        return rejectConfiguredSource("geometry extraction failed");
    }
    const auto& grassGeometry =
        *grassGeometryResolution.Geometry;
    Oot3d::GrassSceneMesh mesh;
    mesh.SubmissionId = draw.SubmissionId;
    mesh.GeometryId = grassGeometry.GeometryId;
    mesh.AnchorVersion = grassGeometry.AnchorVersion;
    mesh.ContentVersion = grassGeometry.ContentVersion;
    if (std::getenv("OOT3D_GRASS_DIAGNOSTICS") != nullptr) {
        static uint32_t coordinateReports = 0U;
        if (coordinateReports < 48U) {
            ++coordinateReports;
            const auto& transform =
                textureCoordinateTransform.RawToSample;
            std::fprintf(
                stderr,
                "OOT3D grass UV: frame=%llu geometry=%016llx "
                "texture=%016llx version=%016llx "
                "transform=[%.9g %.9g %.9g %.9g %.9g %.9g]\n",
                static_cast<unsigned long long>(frameId),
                static_cast<unsigned long long>(mesh.GeometryId),
                static_cast<unsigned long long>(*textureHash),
                static_cast<unsigned long long>(
                    mesh.ContentVersion),
                transform[0], transform[1], transform[2],
                transform[3], transform[4], transform[5]);
        }
    }
    mesh.TextureHash = *textureHash;
    uint64_t drawIdentity = mesh.GeometryId ^
        (mesh.TextureHash + 0x9e3779b97f4a7c15ULL + (mesh.GeometryId << 6U) +
         (mesh.GeometryId >> 2U));
    const uint32_t occurrence = occurrences[drawIdentity]++;
    mesh.InstanceId = drawIdentity ^
        (static_cast<uint64_t>(occurrence) + 0x9e3779b97f4a7c15ULL +
         (drawIdentity << 6U) + (drawIdentity >> 2U));
    mesh.InstanceId ^= draw.RenderTargetNamespace + 0x9e3779b97f4a7c15ULL +
                       (mesh.InstanceId << 6U) + (mesh.InstanceId >> 2U);
    mesh.InstanceId ^=
        static_cast<uint64_t>(
            draw.FramebufferColorPhysicalAddress) +
        0x9e3779b97f4a7c15ULL +
        (mesh.InstanceId << 6U) +
        (mesh.InstanceId >> 2U);
    if (mesh.InstanceId == 0U) mesh.InstanceId = 1U;
    mesh.RenderTargetNamespace =
        draw.RenderTargetNamespace;
    mesh.FramebufferColorPhysicalAddress =
        draw.FramebufferColorPhysicalAddress;
    mesh.FrameId = frameId;
    mesh.TextureWidth = texture->Width;
    mesh.TextureHeight = texture->Height;
    mesh.MapperSlot = 0;
    mesh.MaterialWrapS = *materialWrapS;
    mesh.MaterialWrapT = *materialWrapT;
    mesh.TransformBakedIntoVertices = false;
    mesh.PreserveWorldAnchor = true;
    mesh.ModelToWorld = modelToWorld;
    // CPU matrices above are row-major. GLSL consumes the push-constant mat4
    // as column-major, so transpose while storing the exact PICA transform.
    for (size_t row = 0; row < 4U; ++row) {
        for (size_t column = 0; column < 4U; ++column) {
            mesh.PicaModelToClip[column * 4U + row] =
                picaModelToClip[row * 4U + column];
        }
    }
    mesh.PicaModelToClipAvailable = true;
    mesh.Shading = Oot3d::DecodeGrassShadingEnvironment(
        draw.VertexUniformBytes, draw.FragmentUniformBytes,
        draw.FragmentFeatures, capturedViewToWorld);
    mesh.Vertices = grassGeometry.Vertices;
    mesh.Indices = grassGeometry.Indices;
    Oot3d::GrassSceneBridge::Instance().Publish(
        std::move(mesh));
    return true;
}

} // namespace Fast

#endif
