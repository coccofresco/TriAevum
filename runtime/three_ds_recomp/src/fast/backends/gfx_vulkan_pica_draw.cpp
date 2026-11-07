// Native PICA on Vulkan: draw submission, composition anchors, interactive grass and shadow maps.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_pica_internal.h"
#include "fast/renderer3ds/pica_exclude_scissor.h"
#include "fast/renderer3ds/pica_draw_coverage.h"
#include "fast/renderer3ds/pica_fragment_balance.h"
#include "fast/renderer3ds/pica_surface_appearance.h"
#include "fast/extension/pica_rim_light.h"
#include "fast/renderer3ds/pica_surface_debug.h"
#include "fast/renderer3ds/pica_surface_emission.h"
#include "fast/renderer3ds/pica_native_point_replacement.h"
#include "fast/extension/grass_interaction_bridge.h"
#include "fast/extension/grass_texture_twins.h"
#include "fast/extension/grass_overlay_mask.h"
#include "fast/extension/grass_placement_audit.h"
#include "fast/extension/grass_stage_profile.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace Fast {

bool PublishNativePicaGrassSurface(const GfxNativePicaDrawView& draw, uint64_t frameId,
                                   std::unordered_map<uint64_t, uint32_t>& occurrences,
                                   const Extension::InteractiveGrassSettings& settings, bool worldSurface,
                                   bool fragmentUsesTexture0, bool perspectiveProjection,
                                   Extension::GrassGeometryRegistry& geometryRegistry, Extension::GrassDrawMemo& memo,
                                   uint64_t sequenceId, uint64_t drawOrder, bool masksOnly = false);
void AuditNativePicaGrassGate(const GfxNativePicaDrawView& draw, const Extension::InteractiveGrassSettings& settings,
                              const char* gate);

Renderer3ds::PicaShadowCasterAnchor* GfxRenderingAPIVulkan::ShadowCasterAnchor(
    uint64_t geometryIdentity, const Renderer3ds::PicaTemporalIdentity& identity, uint64_t submissionId) {
    // Only a proven instance owns an anchor: without one, several placements
    // of one mesh would share it, so the stateless version is used instead.
    // An identity that two draws of one frame share (one renderer drawing a
    // packet twice) is ambiguous for as long as its anchor lives.
    if (geometryIdentity == 0U || !identity.Available())
        return nullptr;
    uint64_t key = 1469598103934665603ULL;
    for (const uint64_t value : {geometryIdentity, identity.Owner, identity.Instance, identity.Part, identity.Slot,
                                 identity.Lifetime})
        key ^= value + 0x9e3779b97f4a7c15ULL + (key << 6U) + (key >> 2U);
    auto& anchor = mShadowCasterAnchors[key];
    if (anchor.LastFrame == mFrameCounter && anchor.LastSubmission != submissionId)
        anchor.Ambiguous = true;
    anchor.LastFrame = mFrameCounter;
    anchor.LastSubmission = submissionId;
    return anchor.Ambiguous ? nullptr : &anchor;
}

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

        Extension::PicaCompositionSchedule compiled;
        std::string compileError;
        if (!compiled.Compile(sequence, &compileError)) {
            throw std::runtime_error(
                "native PICA composition schedule failed: " +
                compileError);
        }
        if (mTemporalMotionEnabled && !mPicaTemporalHistory.PrepareSequence(sequence, mFrameCounter) &&
            mPicaTemporalHistory.DuplicateTargetSequences == 1U)
            std::fprintf(stderr, "TRIAEVUM_TEMPORAL_DUPLICATE_TARGET frame=%llu: a target was presented twice in one "
                         "host frame; its second sequence draws without motion history\n",
                         static_cast<unsigned long long>(mFrameCounter));
        mNativePicaCompositionSchedule = std::move(compiled);
        mGrassDrawOrdinals.clear();
        mGrassOverlayOccurrences.clear();
        if (mInteractiveGrassProviderPlan.Valid() &&
            sequence.Purpose == Renderer3ds::PicaCompositionPurpose::Presentation)
            for (size_t i = 0; i < sequence.Draws.size(); ++i)
                mGrassDrawOrdinals.emplace(sequence.Draws[i].SubmissionId, i);
        // Prepared casters belong to exactly one published sequence.
        mPreparedShadowCasterFrame.BeginFrame(mCurrentFrame, mFrameCounter);
        mPreparedShadowCasters.clear();
        // Caster instances unseen for ten seconds give up their anchors.
        if (mFrameCounter >= mNextShadowCasterAnchorSweep) {
            mNextShadowCasterAnchorSweep = mFrameCounter + 600U;
            std::erase_if(mShadowCasterAnchors, [this](const auto& entry) {
                return entry.second.LastFrame + 600U < mFrameCounter;
            });
        }
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
    if (const auto* anchor = Extension::FindPicaEffectPassAnchor(
            mNativePicaCompositionSchedule, mDirectionalShadowSchedulePlan, reference.Target);
        anchor != nullptr && !anchor->AtTargetEnd && anchor->BeforeSubmissionId == draw.SubmissionId)
        TryRenderDirectionalShadowMap(target, mFrameGraphicsSettings, *anchor);
    if (const auto* anchor = Extension::FindPicaEffectPassAnchor(
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
            Extension::EffectGeometryProviderInvocationKind::DeclaredBoundary);
    if (timed)
        grassMilliseconds += std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    const auto& effects = mFrameGraphicsSettings.Effects;
    // Scene stages, same-extent anti-aliasing included: it resolves the scene
    // here so the UI that follows is drawn over the result, never through it.
    const bool sceneEffects = effects.AmbientOcclusion != Extension::AmbientOcclusionMode::Off ||
        effects.Reflections != Extension::ReflectionMode::Off ||
        effects.GlobalIllumination.Mode != Extension::GlobalIlluminationMode::Off ||
        (effects.Toon != Extension::ToonMode::Off && effects.ToonStyle.OutlineEnabled) ||
        mSceneAntiAliasing;
    // Presentation stages (AA, upscalers) read the display's world surface.
    // When an overlay (native UI, the isolated title logo) reaches the target
    // before its transfer, that surface is proven only by the scene
    // completion recorded at this boundary.
    const bool fxaaBeforeUi = mSpatialAaMode == 1U && draw.Composition.ExcludeFromPresentationFxaa;
    if (!mNativeFidelityProfile && (sceneEffects || mPresentationWorldSurface || fxaaBeforeUi) &&
        NativeSceneOverlayDraw(draw)) {
        const auto* beforeUi = mNativePicaCompositionSchedule.FindAnchorBeforeDraw(
            Extension::EffectStage::BeforeUi, reference.Target, draw.SubmissionId);
        if (beforeUi != nullptr) {
            GpuTextureCopySourceImport composed;
            if (sceneEffects || fxaaBeforeUi) {
                std::string error;
                if (!ComposeNativePicaSceneForCopy(target, draw.SubmissionId, composed, &error, beforeUi, fxaaBeforeUi))
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
    // Editor geometry is presentation decoration. The effect graph has already
    // consumed/published the clean scene, including the upscaler snapshot.
    // Native UI is submitted afterwards, so it remains above the wireframe.
    if (!mNativeFidelityProfile && NativeSceneOverlayDraw(draw) &&
        mNativePicaCompositionSchedule.FindAnchorBeforeDraw(Extension::EffectStage::BeforeUi, reference.Target,
                                                            draw.SubmissionId) != nullptr)
        TryRenderWorldDebugLines(target);
}

GfxRenderingAPIVulkan::NativePicaGeometryPlacement GfxRenderingAPIVulkan::PlaceNativePicaGeometry(
    const Extension::PicaGeometryResolveResult& resolution, Oot3dVulkanNativePicaCpuTimings* cpuTimings) {
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
    const VkDeviceSize baseOffset = EnsureFrameVertexCapacity(geometry.Payload.size());
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
        Extension::PicaExtensionPassEnabled(mPicaExtensionGraph, Extension::PicaExtensionPass::DirectionalShadowLighting);
    const bool pointMaps =
        mPointShadowMapBarrierPlan.Valid() && mPointShadowSchedulePlan.Valid() &&
        Extension::PicaExtensionPassEnabled(mPicaExtensionGraph, Extension::PicaExtensionPass::PointShadowLighting);
    return mFrameActive && mNativePicaCompositionSchedule.Valid() &&
           mNativePicaCompositionSchedule.Purpose() == Renderer3ds::PicaCompositionPurpose::Presentation &&
           (directionalMap || pointMaps || mInteractiveGrassProviderPlan.Valid()) &&
           mNriPicaPipelineBridge.OwnedDrawsEnabled() && draw.Domain == Renderer3ds::PicaCompositionDomain::Scene &&
           (draw.Composition.Layer == Renderer3ds::PicaCompositionLayer::OpaqueWorld ||
            (mInteractiveGrassProviderPlan.Valid() &&
             draw.Composition.Layer == Renderer3ds::PicaCompositionLayer::TransparentWorld)) &&
           !draw.Composition.NativeProjectedShadow;
}

const Renderer3ds::PicaWaterTextureCatalog* GfxRenderingAPIVulkan::WaterTextureCatalog() {
    if (!mWaterTextureCatalogLoaded) {
        mWaterTextureCatalogLoaded = true;
        if (const char* path = std::getenv("TRIAEVUM_WATER_TEXTURE_CATALOG"); path != nullptr && *path != '\0') {
            std::ifstream stream(std::filesystem::u8path(path), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            mWaterTextureCatalog = Renderer3ds::PicaWaterTextureCatalog::Parse(text);
            if (mWaterTextureCatalog)
                spdlog::info("Water texture catalog: {} textures", mWaterTextureCatalog->Size());
            else
                spdlog::warn("Water texture catalog is unreadable: {}", path);
        }
    }
    return mWaterTextureCatalog ? &*mWaterTextureCatalog : nullptr;
}

const std::array<Renderer3ds::PicaWaterRing, Renderer3ds::kPicaWaterRings>&
GfxRenderingAPIVulkan::AdvanceWaterRipples(const Renderer3ds::PicaSceneWaterBodies& bodies, float seconds) {
    if (mWaterRipplesFrame != mFrameCounter) {
        mWaterRipplesFrame = mFrameCounter;
        // The actors of the native frame on screen (the grass interactors).
        const auto actors = Extension::GrassInteractionBridge::Instance().LatestActors();
        std::vector<Renderer3ds::PicaWaterMover> movers;
        movers.reserve(actors.size());
        for (const auto& actor : actors)
            movers.push_back({actor.StableId, actor.Position, actor.Radius, 2.0F * actor.HalfHeight});
        mWaterRipples.Advance(seconds, movers.data(), movers.size(), bodies);
        mWaterRings = mWaterRipples.Rings();
    }
    return mWaterRings;
}

GfxRenderingAPIVulkan::NativePicaWaterSurface GfxRenderingAPIVulkan::ResolveNativePicaWaterSurface(
    const GfxNativePicaDrawView& draw) {
    NativePicaWaterSurface result;
    // One material per draw: a water texture in any of its slots makes all of
    // it water.
    uint8_t roles = 0;
    if (const auto* catalog = WaterTextureCatalog())
        for (const auto& texture : draw.Textures)
            if (texture.NativeType == 0U && texture.Slot < 3U)
                if (const auto hash = ResolveNativeTextureBaseLevelContentHash(texture))
                    roles |= catalog->Roles(*hash, texture.Width, texture.Height);
    const bool catalogWater = (roles & Renderer3ds::kPicaWaterTextureWater) != 0U;
    const bool bodies = draw.LightBinding.Water && draw.LightBinding.Water->Count != 0U;
    Extension::DirectionalShadowMatrix viewToWorld;
    if ((!bodies && !catalogWater) || !draw.GeometryIdentityAvailable ||
        draw.VertexCount == 0U || draw.VertexBindings.empty() || draw.VertexAttributes.empty() ||
        draw.VertexUniformBytes.size() < Extension::kPicaPackedVertexFloatOffset || !draw.LightBinding.WorldToView ||
        !Extension::InvertDirectionalShadowMatrix(*draw.LightBinding.WorldToView, viewToWorld))
        return result;
    std::vector<Extension::PicaNriSourceVertexStream> streams;
    std::vector<Extension::PicaNriSourceVertexBinding> sourceBindings;
    std::vector<Extension::PicaNriSourceVertexAttribute> attributes;
    streams.reserve(draw.VertexBindings.size());
    sourceBindings.reserve(draw.VertexBindings.size());
    for (const auto& binding : draw.VertexBindings) {
        streams.push_back({ { binding.Binding, binding.ByteStride, binding.PerInstance }, binding.Bytes });
        sourceBindings.push_back({ binding.Binding, binding.ByteStride, binding.PerInstance });
    }
    attributes.reserve(draw.VertexAttributes.size());
    for (const auto& attribute : draw.VertexAttributes)
        attributes.push_back({ attribute.Location, attribute.Binding,
                               static_cast<Extension::PicaNriVertexScalar>(attribute.Format),
                               attribute.ComponentCount, attribute.ByteOffset });
    const auto layout = Extension::BuildPicaNriVertexInputLayout(sourceBindings, attributes);
    if (!layout.Valid() || layout.Bindings.empty() || layout.Attributes.empty()) return result;
    // The ordered submission resolves the same geometry again: a cache hit.
    const auto resolution = mPicaGeometryRegistry.Resolve({
        draw.GeometryIdentity, draw.GeometryContentVersion, draw.GeometryIdentityAvailable, mFrameCounter,
        streams, attributes, draw.IndexBytes, draw.Indexed, draw.IndicesAre16Bit, draw.VertexCount, true,
    });
    if (resolution.Geometry == nullptr) throw std::runtime_error("native PICA geometry preparation failed");
    for (const uint64_t evicted : mPicaGeometryRegistry.TakeEvictedIdentities()) RetireNativePicaGeometry(evicted);
    const auto& geometry = *resolution.Geometry;
    if (!resolution.Cacheable || !geometry.PackedValid) return result;
    std::vector<Extension::PicaSceneVertexBufferBinding> bindings;
    bindings.reserve(geometry.PackedBindings.size());
    for (const auto& binding : geometry.PackedBindings)
        bindings.push_back({ binding.Binding, binding.Offset, binding.Size, binding.Stride });
    uint32_t booleanUniforms = 0;
    std::memcpy(&booleanUniforms, draw.VertexUniformBytes.data() + Extension::kPicaPackedVertexBooleanMaskOffset,
                sizeof(booleanUniforms));
    // The scene distance field's cache: one mesh per geometry version.
    const uint64_t version = geometry.ContentVersion ^ (geometry.StructuralSignature * 0x9e3779b97f4a7c15ULL) ^
                             geometry.Identity ^ draw.VertexShaderKey;
    const auto instance = Renderer3ds::BuildPicaGiInstance(
        { geometry.Payload, bindings, layout.Attributes, layout.Bindings, 0, geometry.IndexOffset,
          geometry.VertexOrIndexCount, draw.BaseVertex, geometry.Indexed },
        ToPicaSceneTopology(draw.Topology), draw.TemporalVertexProgram.Hooks, booleanUniforms,
        draw.VertexUniformBytes.subspan(Extension::kPicaPackedVertexFloatOffset), viewToWorld, version,
        mNativePicaGeometryBuffers[geometry.Identity].GiGeometry);
    if (!instance.Valid()) return result;
    result.WorldBounds = instance.WorldBounds;
    if (bodies) {
        const auto& water = *draw.LightBinding.Water;
        for (uint32_t i = 0; i < std::min(water.Count, Renderer3ds::kMaximumPicaSceneWaterBodies); ++i)
            result.ReachesWater |= Renderer3ds::PicaWaterSurfaceReaches(
                instance.WorldBounds.Minimum, instance.WorldBounds.Maximum, water.Bodies[i]);
        result.Body = Renderer3ds::MatchPicaWaterSurface(instance.WorldBounds.Minimum,
                                                         instance.WorldBounds.Maximum, water);
    }
    result.Roles = roles;
    if (catalogWater) {
        result.WholeDraw = true;
        // Its triangles by slope, once per rigid placement.
        const auto split = [&] {
            return std::make_shared<const Renderer3ds::PicaWaterTriangles>(Renderer3ds::SplitPicaWaterTriangles(
                instance.Mesh->Positions, instance.Mesh->Indices, instance.WorldFromLocal));
        };
        if (instance.RigidSignature != 0U) {
            if (mWaterTriangles.size() > 1024U) mWaterTriangles.clear();
            auto& cached = mWaterTriangles[instance.RigidSignature];
            if (!cached) cached = split();
            result.Pieces = cached;
            result.PiecesKey = instance.RigidSignature;
        } else {
            result.Pieces = split();
            result.PiecesKey = std::bit_cast<uint32_t>(instance.WorldBounds.Minimum[0]) * 0x9e3779b97f4a7c15ULL ^
                               std::bit_cast<uint32_t>(instance.WorldBounds.Maximum[2]);
        }
        if (!result.ReachesWater)
            result.Plane = Renderer3ds::PicaWaterSurfaceFromBounds(instance.WorldBounds.Minimum,
                                                                   instance.WorldBounds.Maximum);
    }
    return result;
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
        // All masks of this sample must be available before grass is inserted
        // at the opaque boundary. This preparation never submits native draws.
        if (mInteractiveGrassProviderPlan.Valid() && !draw.Composition.NativeActorGeometry) {
            static const bool maskDiagnostics = std::getenv("TRIAEVUM_GRASS_MASK_DIAGNOSTICS") != nullptr;
            const auto begin =
                maskDiagnostics ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            const bool captured = PublishNativePicaGrassSurface(
                draw, mFrameCounter, mGrassOverlayOccurrences, mFrameGraphicsSettings.Grass, true,
                draw.FragmentShaderHooks.SamplesTexture(0),
                Extension::UsesPicaPerspectiveProjection(draw.VertexUniformBytes), mGrassGeometryRegistry, mGrassDrawMemo,
                mNativePicaCompositionSchedule.SequenceId(), mGrassDrawOrdinals.at(draw.SubmissionId), true);
            if (maskDiagnostics) {
                static uint64_t frame = 0;
                static size_t attempts = 0, captures = 0;
                static double milliseconds = 0;
                if (frame != mFrameCounter) {
                    if (attempts)
                        std::fprintf(stderr,
                                     "TRIAEVUM_GRASS_MASK frame=%llu preparation_draws=%zu captured=%zu cpu_ms=%.3f\n",
                                     static_cast<unsigned long long>(frame), attempts, captures, milliseconds);
                    frame = mFrameCounter;
                    attempts = captures = 0;
                    milliseconds = 0;
                }
                ++attempts;
                captures += captured;
                milliseconds +=
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            }
        }
        const bool shadowPreparation =
            (mDirectionalShadowMapBarrierPlan.Valid() && mDirectionalShadowSchedulePlan.Valid() &&
             Extension::PicaExtensionPassEnabled(mPicaExtensionGraph,
                                             Extension::PicaExtensionPass::DirectionalShadowLighting)) ||
            (mPointShadowMapBarrierPlan.Valid() && mPointShadowSchedulePlan.Valid() &&
             Extension::PicaExtensionPassEnabled(mPicaExtensionGraph, Extension::PicaExtensionPass::PointShadowLighting));
        if (!shadowPreparation || draw.Composition.Layer != Renderer3ds::PicaCompositionLayer::OpaqueWorld)
            return true;
        // Caster admission: depth-writing perspective geometry with its
        // captured view; an alpha-tested caster also needs its alpha test.
        Extension::DirectionalShadowMatrix viewToWorld;
        const bool admitted =
            AdmitsShadowCasterGeometry(draw) &&
            draw.Topology != GfxNativePicaTopology::TriangleFan && !draw.VertexShaderSource.empty() &&
            !draw.FragmentShaderSource.empty() && draw.VertexCount != 0U && !draw.VertexBindings.empty() &&
            !draw.VertexAttributes.empty() && draw.VertexUniformBytes.size() >= Extension::kPicaPackedVertexFloatOffset &&
            Extension::UsesPicaPerspectiveProjection(draw.VertexUniformBytes) && draw.LightBinding.WorldToView &&
            Extension::InvertDirectionalShadowMatrix(*draw.LightBinding.WorldToView, viewToWorld);
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
        auto& shadowDebug = Renderer3ds::PicaSurfaceDebug::Instance();
        if (shadowDebug.ShadowTracing()) {
            char fields[256];
            std::snprintf(fields, sizeof(fields),
                ",\"admitted\":%u,\"geometry\":\"%016llx\",\"native_actor\":%u,\"owner\":%llu",
                admitted ? 1U : 0U, static_cast<unsigned long long>(draw.GeometryIdentity),
                draw.Composition.NativeActorGeometry ? 1U : 0U,
                static_cast<unsigned long long>(draw.Composition.NativeActorIdentity));
            shadowDebug.ShadowRecord("shadow_prepare", mFrameCounter, draw.SubmissionId, fields);
        }
#endif
        if (!admitted) {
            mDiagnostics.RecordNriDirectionalShadowPreparation(false, false);
            return true;
        }

        std::vector<Extension::PicaNriSourceVertexStream> streams;
        std::vector<Extension::PicaNriSourceVertexBinding> sourceBindings;
        std::vector<Extension::PicaNriSourceVertexAttribute> attributes;
        streams.reserve(draw.VertexBindings.size());
        sourceBindings.reserve(draw.VertexBindings.size());
        for (const auto& binding : draw.VertexBindings) {
            streams.push_back({ { binding.Binding, binding.ByteStride, binding.PerInstance }, binding.Bytes });
            sourceBindings.push_back({ binding.Binding, binding.ByteStride, binding.PerInstance });
        }
        attributes.reserve(draw.VertexAttributes.size());
        for (const auto& attribute : draw.VertexAttributes) {
            attributes.push_back({ attribute.Location, attribute.Binding,
                                   static_cast<Extension::PicaNriVertexScalar>(attribute.Format),
                                   attribute.ComponentCount, attribute.ByteOffset });
        }
        const auto layout = Extension::BuildPicaNriVertexInputLayout(sourceBindings, attributes);
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
        std::vector<Extension::PicaSceneVertexBufferBinding> bindings;
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
        std::memcpy(&booleanUniforms, draw.VertexUniformBytes.data() + Extension::kPicaPackedVertexBooleanMaskOffset,
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
            draw.VertexUniformBytes.subspan(Extension::kPicaPackedVertexFloatOffset), viewToWorld, version, shadowCache,
            resolution.Cacheable
                ? ShadowCasterAnchor(geometry.Identity, draw.Composition.TemporalIdentity, draw.SubmissionId)
                : nullptr);
        shadowCaster.Dynamic =
            draw.Composition.NativeActorGeometry && !draw.Composition.StaticShadowCacheEligible;
        shadowCaster.OwnerIdentity = draw.Composition.NativeActorIdentity;
        std::array<Extension::PicaSceneTextureBinding, 3> sceneTextures{};
        Extension::PicaSceneTextureBinding sceneLightingLut;
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
                sceneLightingLut.ImageFormat = Extension::PicaSceneTextureImageFormat::R32Uint;
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
        Extension::PicaSceneDrawRecordDesc record{
            .SubmissionId = draw.SubmissionId,
            .CommandListAddress = draw.CommandListAddress,
            .CommandListOffsetWords = draw.CommandListOffsetWords,
            .CompositionDomain = draw.CompositionDomain,
            .Composition = draw.Composition,
            .LightingWorldToView = draw.LightBinding.WorldToView,
            .SceneDirectionals = draw.LightBinding.SceneDirectionals,
            .ScenePoints = draw.LightBinding.ScenePoints,
                .RoomShadows = draw.LightBinding.RoomShadows,
            .NativeLightSources = draw.LightBinding.Sources,
            .ShadowCaster = shadowCaster,
            .Raster = {
                .SchemaVersion = Extension::kPicaSceneResolvedRasterStateSchemaVersion,
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
                .SchemaVersion = Extension::kPicaSceneRenderTargetStateSchemaVersion,
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
                .SchemaVersion = Extension::kPicaSceneResolvedMaterialStateSchemaVersion,
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
        const bool caster = status == Extension::PicaSceneRecordStatus::Recorded &&
            mPreparedShadowCasterFrame.DirectionalShadowCaster(mPreparedShadowCasterFrame.Draws().size() - 1U);
        if (caster) {
            mPreparedShadowCasters.insert_or_assign(draw.SubmissionId, shadowCaster);
        }
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
        if (shadowDebug.ShadowTracing()) {
            char fields[256];
            std::snprintf(fields, sizeof(fields), ",\"status\":%u,\"caster\":%u",
                static_cast<unsigned>(status), caster ? 1U : 0U);
            shadowDebug.ShadowRecord("shadow_prepared_record", mFrameCounter, draw.SubmissionId, fields);
        }
#endif
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
        if (draw.ScissorMode != 0U && draw.ScissorMode != 1U && draw.ScissorMode != 3U) {
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
            Extension::UsesFullSceneLighting(graphicsSettings.Effects.SurfaceLighting)) {
            // Consume native ordering and its graph boundaries, but do not
            // rasterize the legacy projected shadow into presentation or guides.
            // The canonical native-memory replay remains untouched.
            if (mSurfaceCalibrationTrace.Sampling())
                mSurfaceCalibrationTrace.Count("domain=scene replay=presentation path=skipped admitted=0 reason=native-projected-shadow", draw.VertexCount);
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
                 Extension::ReflectionMode::Off ||
             (graphicsSettings.Effects.GlobalIllumination.Mode ==
                  Extension::GlobalIlluminationMode::BrixelizerGi &&
              graphicsSettings.Effects.GlobalIllumination.Specular));
        std::array<Extension::ReflectionMaterialTextureIdentity, 3U>
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
        std::optional<Extension::ResolvedReflectionMaterialProfile>
            resolvedReflectionMaterial;
        if (reflectionsEnabled) {
            resolvedReflectionMaterial =
                Extension::ResolveReflectionMaterialProfile(
                    graphicsSettings.Effects.ReflectionMaterials,
                    std::span(
                        reflectionTextureIdentities.data(),
                        reflectionTextureCount));
        }
        const bool grassPerspectiveProjection =
            Extension::UsesPicaPerspectiveProjection(
                draw.VertexUniformBytes);
        // A draw the title lights as an object of its own keeps its native
        // lights in the full per-pixel model and receives none of the scene's
        // environment profile, shadows, GI or ambient occlusion.
        const bool isolatedLighting = !nativeMemoryOnly && draw.Composition.IsolatedLighting &&
            Extension::UsesFullSceneLighting(graphicsSettings.Effects.SurfaceLighting);
        // Authored appearance (additive light, colour no light produced) is
        // never relit or shadowed (pica_surface_appearance.h). Vertex lighting
        // is the typed surface program's boolean; unknown counts as lit.
        bool nativeVertexLighting = true;
        if (const auto* surfaceProgram = draw.TemporalVertexProgram.Hooks.SurfaceProgram();
            surfaceProgram != nullptr &&
            draw.VertexUniformBytes.size() >= Extension::kPicaPackedVertexBooleanMaskOffset + sizeof(uint32_t)) {
            uint32_t vertexBooleans = 0U;
            std::memcpy(&vertexBooleans, draw.VertexUniformBytes.data() + Extension::kPicaPackedVertexBooleanMaskOffset,
                        sizeof(vertexBooleans));
            nativeVertexLighting = ((vertexBooleans >> surfaceProgram->LightingBooleanUniform) & 1U) != 0U;
        }
        const auto surfaceAppearance = Renderer3ds::ClassifyPicaSurfaceAppearance(
            draw.Blend, nativeVertexLighting, draw.FragmentFeatures.FragmentLightingEnabled);
        // Gameplay water (pica_scene_water.h) keeps its native colour with
        // added optics: the title's water is ambient-only geometry, and relit
        // like ground its top faced the key light at full Lambert. Its
        // fragments are found in the surface shader (AssignPicaSurfaceWater).
        // Filter-blended water (a moat or channel drawn colour x itself) is
        // resolved too: the water path keeps its native colour anyway.
        NativePicaWaterSurface waterSurface;
        if (!nativeMemoryOnly && !mNativeFidelityProfile &&
            Extension::UsesFullSceneLighting(graphicsSettings.Effects.SurfaceLighting) &&
            (surfaceAppearance == Renderer3ds::PicaSurfaceAppearance::Relightable ||
             surfaceAppearance == Renderer3ds::PicaSurfaceAppearance::FilterComposition) &&
            draw.CompositionDomain == Renderer3ds::PicaCompositionDomain::Scene &&
            Renderer3ds::PicaCompositionReceivesSurfaceLighting(draw.Composition.Layer) && grassPerspectiveProjection)
            waterSurface = ResolveNativePicaWaterSurface(draw);
        // A filter-composed draw is relit nowhere, except water: a draw on a
        // water body or catalogued whole water, whose water fragments keep
        // native colour with water optics (the water blend is colour x itself).
        const bool filterWaterDraw =
            surfaceAppearance == Renderer3ds::PicaSurfaceAppearance::FilterComposition &&
            (waterSurface.WholeDraw || waterSurface.ReachesWater);
        if (surfaceAppearance == Renderer3ds::PicaSurfaceAppearance::FilterComposition && !filterWaterDraw)
            waterSurface = {};
        const bool authoredAppearance = Extension::UsesFullSceneLighting(graphicsSettings.Effects.SurfaceLighting) &&
            surfaceAppearance != Renderer3ds::PicaSurfaceAppearance::Relightable && !filterWaterDraw;
        const bool directionalShadowReceiver = !nativeMemoryOnly && !isolatedLighting && !authoredAppearance &&
            Extension::UsesFullSceneLighting(graphicsSettings.Effects.SurfaceLighting) &&
            mDirectionalShadowMapBarrierPlan.Valid() &&
            Extension::PicaExtensionPassEnabled(
                mPicaExtensionGraph,
                Extension::PicaExtensionPass::DirectionalShadowLighting) &&
            grassPerspectiveProjection;
        // Point lights with a cube map: only NRI-owned draws bind the atlas.
        const bool pointShadowReceiver = !nativeMemoryOnly && !mNativeFidelityProfile && !isolatedLighting &&
            !authoredAppearance &&
            graphicsSettings.Effects.PointShadows.Mode == Extension::PointShadowMode::CubeMaps &&
            Extension::UsesFullSceneLighting(graphicsSettings.Effects.SurfaceLighting) &&
            mPointShadowMapBarrierPlan.Valid() &&
            Extension::PicaExtensionPassEnabled(
                mPicaExtensionGraph, Extension::PicaExtensionPass::PointShadowLighting) &&
            mNriPicaPipelineBridge.OwnedDrawsEnabled() && grassPerspectiveProjection;
        Extension::NriPointShadowHistorySnapshot pointShadowHistory;
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
        std::optional<Renderer3ds::PicaFragmentBalanceUniforms> fragmentBalance;
        std::optional<Renderer3ds::PicaNativePointReplacement> pointReplacement;
        // Adapter capability: the draw's native fragment lights are scene
        // lights (shadow receivers, no second application of a scene light).
        bool nativeFragmentLitReceivers = false;
        const auto* environmentWorldToView = draw.LightBinding.WorldToView
            ? &*draw.LightBinding.WorldToView : nullptr;
        Extension::DirectionalShadowMatrix environmentViewToWorld{};
        const bool surfaceCandidate = !nativeMemoryOnly && !mNativeFidelityProfile &&
            graphicsSettings.Effects.SurfaceLighting != Extension::SurfaceLightingMode::Native &&
            draw.CompositionDomain == Renderer3ds::PicaCompositionDomain::Scene &&
            Renderer3ds::PicaCompositionReceivesSurfaceLighting(draw.Composition.Layer) &&
            grassPerspectiveProjection && draw.FragmentOperationMode != 3U &&
            draw.VertexUniformBytes.size() >= Extension::kPicaPackedVertexFloatOffset;
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
        // Developer lab: candidates are numbered per frame; a debug range keeps
        // its native appearance (finding the draw an effect belongs to).
        auto& surfaceDebug = Renderer3ds::PicaSurfaceDebug::Instance();
        const uint32_t surfaceOrdinal = surfaceCandidate ? surfaceDebug.NextOrdinal() : 0U;
        const bool surfaceExempt = surfaceCandidate && surfaceDebug.Exempt(surfaceOrdinal);
        if (surfaceDebug.Census()) {
            // Every draw: what it samples, how it blends, its TEV constants.
            Renderer3ds::PicaTevProgram tev{};
            const bool tevKnown = draw.FragmentUniformBytes.size() >=
                Extension::kPicaPackedFragmentTevProgramOffset + sizeof(tev);
            if (tevKnown)
                std::memcpy(&tev, draw.FragmentUniformBytes.data() + Extension::kPicaPackedFragmentTevProgramOffset,
                            sizeof(tev));
            char head[320];
            std::snprintf(head, sizeof(head),
                "{\"census\":1,\"frame\":%llu,\"submission\":%llu,\"layer\":%u,\"actor\":%d,\"blend\":[%d,%u,%u],"
                "\"depthWrite\":%d,\"depthTest\":%d,\"domain\":%u,\"fragmentMode\":%u,\"vertices\":%u,"
                "\"geometry\":\"%llx\",\"textures\":[",
                static_cast<unsigned long long>(surfaceDebug.Frame()),
                static_cast<unsigned long long>(draw.SubmissionId), static_cast<unsigned>(draw.Composition.Layer),
                draw.Composition.NativeActorGeometry ? 1 : 0, draw.Blend.Enabled ? 1 : 0,
                static_cast<unsigned>(draw.Blend.SourceRgb), static_cast<unsigned>(draw.Blend.DestRgb),
                draw.DepthWriteEnabled ? 1 : 0, draw.DepthTestEnabled ? 1 : 0,
                static_cast<unsigned>(draw.CompositionDomain), static_cast<unsigned>(draw.FragmentOperationMode),
                draw.VertexCount, static_cast<unsigned long long>(draw.GeometryIdentity));
            std::string record = head;
            for (size_t i = 0; i < draw.Textures.size(); ++i) {
                const auto& texture = draw.Textures[i];
                char entry[128];
                std::snprintf(entry, sizeof(entry), "%s[\"%016llx\",\"%016llx\",%u,%u,%u,\"%08x\"]", i ? "," : "",
                              static_cast<unsigned long long>(
                                  texture.NativeContentHashAvailable ? texture.NativeContentHash : 0ULL),
                              static_cast<unsigned long long>(
                                  texture.NativeBaseLevelContentHashAvailable
                                      ? texture.NativeBaseLevelContentHash
                                      : ResolveNativeTextureBaseLevelContentHash(texture).value_or(0ULL)),
                              static_cast<unsigned>(texture.Width), static_cast<unsigned>(texture.Height),
                              static_cast<unsigned>(texture.NativeFormat), texture.PhysicalAddress);
                record += entry;
            }
            record += "],\"tevConstants\":[";
            for (size_t stage = 0; stage < 6U; ++stage) {
                float colour[4] = {};
                const size_t offset = Extension::kPicaPackedFragmentTevConstantsOffset + stage * sizeof(colour);
                if (draw.FragmentUniformBytes.size() >= offset + sizeof(colour))
                    std::memcpy(colour, draw.FragmentUniformBytes.data() + offset, sizeof(colour));
                char entry[64];
                std::snprintf(entry, sizeof(entry), "%s[%.3f,%.3f,%.3f,%.3f]", stage ? "," : "", colour[0],
                              colour[1], colour[2], colour[3]);
                record += entry;
            }
            char sources[48];
            std::snprintf(sources, sizeof(sources), "],\"tevSources\":\"%04x\"}",
                          tevKnown ? Renderer3ds::PicaTevColorSourceMask(tev) : 0U);
            record += sources;
            surfaceDebug.Write(record);
        }
#else
        constexpr bool surfaceExempt = false;
#endif
        if (surfaceCandidate && !surfaceExempt && !authoredAppearance) {
            uint32_t booleanUniforms = 0U;
            std::memcpy(&booleanUniforms, draw.VertexUniformBytes.data() +
                        Extension::kPicaPackedVertexBooleanMaskOffset, sizeof(booleanUniforms));
            if (environmentWorldToView &&
                (!mSurfaceLightingView || mSurfaceLightingView->WorldToView != *environmentWorldToView))
                mSurfaceLightingView = Renderer3ds::BuildPicaSurfaceLightingView(*environmentWorldToView);
            // The scene environment profile, or the native environment for an
            // isolated draw.
            auto isolatedEnvironment = mFrameEnvironment;
            isolatedEnvironment.Enabled = false;
            const bool balanceTitleDraw = NativePicaFragmentLightingOperand() &&
                mFrameEnvironment.Enabled && !isolatedLighting;
            const bool balanceWaterEligible = Renderer3ds::PicaNativeFragmentOperandWaterEligible(
                NativePicaFragmentLightingOperand(),
                draw.LightBinding.Water ? draw.LightBinding.Water->Count : 0U,
                waterSurface.WorldBounds.Available, waterSurface.ReachesWater, waterSurface.WholeDraw);
            const bool nativePointDraw = NativePicaFragmentLightingOperand() && !isolatedLighting &&
                draw.FragmentFeatures.FragmentLightingEnabled;
            const bool fragmentCapture = nativePointDraw && balanceWaterEligible &&
                Extension::UsesFullSceneLighting(graphicsSettings.Effects.SurfaceLighting) &&
                draw.FragmentShaderHooks.ValidFor(draw.FragmentShaderSource) &&
                draw.FragmentShaderHooks.NativeLightingCapture.Valid(draw.FragmentShaderSource.size(),
                    draw.FragmentShaderHooks.Offset(Renderer3ds::PicaShaderHook::PicaLighting));
            auto surfaceBinding = draw.LightBinding;
            if (nativePointDraw) {
                // A native-fragment receiver must not receive a second copy of
                // its Local light through the legacy supplemental path. Only
                // the identity-proved ledger below can publish points here.
                surfaceBinding.ScenePoints.reset();
            }
            if (balanceTitleDraw && fragmentCapture)
                fragmentBalance = Renderer3ds::BuildPicaFragmentBalanceUniforms(true,
                    draw.FragmentFeatures.FragmentLighting, surfaceBinding, mFrameEnvironment);
            if (fragmentCapture) {
                pointReplacement = Renderer3ds::BuildPicaNativePointReplacement(true,
                    draw.FragmentFeatures.FragmentLighting, draw.LightBinding, mFrameEnvironment, fragmentBalance);
                if (pointReplacement) {
                    fragmentBalance = pointReplacement->Balance;
                    surfaceBinding.ScenePoints = std::make_shared<const Renderer3ds::PicaScenePointLights>(
                        pointReplacement->Points);
                }
            }
            if (fragmentBalance) {
                Renderer3ds::PicaTevProgram tev{};
                if (draw.FragmentUniformBytes.size() < Extension::kPicaPackedFragmentTevProgramOffset + sizeof(tev)) {
                    fragmentBalance.reset(); // Unknown material composition keeps the native draw.
                    pointReplacement.reset();
                    surfaceBinding.ScenePoints.reset();
                }
                else {
                    std::memcpy(&tev, draw.FragmentUniformBytes.data() +
                        Extension::kPicaPackedFragmentTevProgramOffset, sizeof(tev));
                    Renderer3ds::ApplyPicaFragmentSecondaryMaterialFactor(*fragmentBalance, tev);
                }
            }
            // An unqualified fragment-lit draw keeps the no-profile native
            // reference (pica_fragment_balance.h).
            const bool balanceFallback = Renderer3ds::PicaFragmentLitTitleDrawKeepsNative(
                balanceTitleDraw, fragmentBalance.has_value(), draw.FragmentFeatures.FragmentLightingEnabled,
                Extension::UsesFullSceneLighting(graphicsSettings.Effects.SurfaceLighting));
            const auto& surfaceEnvironment = (isolatedLighting || balanceFallback)
                ? isolatedEnvironment : mFrameEnvironment;
            surfaceLighting = Renderer3ds::BuildPicaSurfaceLightingUniforms(
                draw.TemporalVertexProgram.Hooks, booleanUniforms,
                draw.VertexUniformBytes.subspan(Extension::kPicaPackedVertexFloatOffset),
                surfaceBinding, surfaceEnvironment,
                environmentWorldToView && mSurfaceLightingView ? &*mSurfaceLightingView : nullptr,
                pointShadowHistory.Valid() ? &pointShadowAssignment : nullptr,
                fragmentBalance.has_value());
            if (!surfaceLighting || surfaceLighting->Control[0] != 0U) fragmentBalance.reset();
            if (surfaceLighting) {
                if (!Extension::UsesFullSceneLighting(graphicsSettings.Effects.SurfaceLighting) ||
                    isolatedLighting || draw.FragmentFeatures.FragmentLightingEnabled)
                    surfaceLighting->BakedReferenceUp = {};
                if (Extension::PicaDrawReceivesRimLight(mFrameEnvironment, draw.CompositionDomain,
                        draw.Composition, draw.Blend, surfaceAppearance,
                        draw.DepthTestEnabled, draw.DepthWriteEnabled, draw.FragmentOperationMode))
                    Renderer3ds::AssignPicaSurfaceRim(*surfaceLighting, mFrameEnvironment.Rim);
                // Relative source weights (the guide's 8-bit radiance would
                // clip gains above one); the gain applies when GI is composed.
                const auto sources =
                    Extension::ResolveGlobalIlluminationSources(graphicsSettings.Effects.GlobalIllumination);
                surfaceLighting->GlobalIlluminationSources = isolatedLighting
                    ? std::array<float, 4>{}
                    : std::array<float, 4>{sources.Weights[0], sources.Weights[1], sources.Weights[2], 0.0F};
            }
            // A draw lit per pixel by native fragment lights already applies
            // the scene lights among them: they are not added a second time
            // (adapter capability: the title's native lights are scene lights).
            nativeFragmentLitReceivers = NativePicaFragmentLitShadowReceivers() &&
                surfaceLighting && surfaceLighting->Control[0] == 0U &&
                draw.FragmentFeatures.FragmentLightingEnabled;
            if (nativeFragmentLitReceivers) {
                const auto native = Extension::DecodePicaNativeDrawEnvironment(
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
            // Water in a mixed mesh (a room's water and banks in one draw),
            // or a whole water draw (on its own plane when it has no body).
            if (surfaceLighting && waterSurface.WorldBounds.Available && mSurfaceLightingView &&
                (draw.LightBinding.Water || waterSurface.WholeDraw)) {
                Renderer3ds::PicaSceneWaterBodies plane;
                if (waterSurface.Plane) {
                    plane.Bodies[0] = *waterSurface.Plane;
                    plane.Count = 1U;
                }
                const float seconds =
                    std::chrono::duration<float>(std::chrono::steady_clock::now() - mWaterEpoch).count();
                const auto* rings = draw.LightBinding.Water
                    ? &AdvanceWaterRipples(*draw.LightBinding.Water, std::fmod(std::max(seconds, 0.0F), 3600.0F))
                    : nullptr;
                Renderer3ds::AssignPicaSurfaceWater(
                    *surfaceLighting, *mSurfaceLightingView,
                    waterSurface.Plane || !draw.LightBinding.Water ? plane : *draw.LightBinding.Water,
                    waterSurface.WorldBounds.Minimum, waterSurface.WorldBounds.Maximum, seconds,
                    waterSurface.WholeDraw, rings);
                // A tall catalog waterfall: foam where it lands (its base).
                const auto& minimum = waterSurface.WorldBounds.Minimum;
                const auto& maximum = waterSurface.WorldBounds.Maximum;
                if (waterSurface.WholeDraw &&
                    (waterSurface.Roles & Renderer3ds::kPicaWaterTextureWaterfall) != 0U &&
                    maximum[1] - minimum[1] > 0.5F * std::max(maximum[0] - minimum[0], maximum[2] - minimum[2]))
                    surfaceLighting->WaterRipples[3] = minimum[1];
            }
            // Its texture's scrolling (coordinate 0) over native frames.
            if (surfaceLighting && surfaceLighting->WaterViewTranslation[3] > 0.0F) {
                const auto& sample = mPicaSceneFrame.TemporalSample();
                const auto* coordinate = draw.TemporalVertexProgram.Hooks.TextureCoordinate(0);
                const auto floats = draw.VertexUniformBytes.subspan(Extension::kPicaPackedVertexFloatOffset);
                const auto translation = [&](uint8_t uniform, float& value) {
                    const size_t offset = static_cast<size_t>(uniform) * 16U + 12U;
                    if (offset + sizeof(float) > floats.size()) return false;
                    std::memcpy(&value, floats.data() + offset, sizeof(float));
                    return std::isfinite(value);
                };
                std::array<float, 2> shift{};
                if (sample.Available() && coordinate != nullptr &&
                    translation(coordinate->MatrixRowUUniform, shift[0]) &&
                    translation(coordinate->MatrixRowVUniform, shift[1])) {
                    if (mWaterFlow.size() > 4096U) mWaterFlow.clear();
                    auto& track = mWaterFlow[draw.GeometryIdentity];
                    Renderer3ds::AdvancePicaWaterFlow(track, sample.CurrentSourceFrameId,
                                                      static_cast<float>(sample.NativeRateHz), shift);
                    if (track.Valid())
                        surfaceLighting->WaterFlow = {track.Velocity[0], track.Velocity[1], 1.0F, 0.0F};
                }
                surfaceLighting->WaterFlow[3] =
                    graphicsSettings.Effects.Reflections == Extension::ReflectionMode::HiZ ? 1.0F : 0.0F;
            }
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
            // Developer view: water fragments in magenta (1), their ripple
            // slope (2), their flow (3: blue calm, red fast, green unknown) or their
            // reflection flag and reflectance (4: red, green).
            if (const char* debug = std::getenv("TRIAEVUM_DEV_WATER_DEBUG");
                debug != nullptr && surfaceLighting && surfaceLighting->WaterViewTranslation[3] > 0.0F)
                surfaceLighting->WaterOptics[3] = -static_cast<float>(std::clamp(std::atoi(debug), 1, 4));
#endif
            if (surfaceLighting && environmentWorldToView && directionalShadowReceiver &&
                !Extension::InvertDirectionalShadowMatrix(*environmentWorldToView, environmentViewToWorld))
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
            Extension::UsesPbrMaterials(graphicsSettings.Effects.SurfaceLighting) &&
            draw.FragmentShaderHooks.ValidFor(draw.FragmentShaderSource) &&
            draw.FragmentShaderHooks.Supports(Renderer3ds::PicaShaderHook::AfterNativeTev) &&
            (!draw.FragmentFeatures.FragmentLightingEnabled ||
             (draw.FragmentFeatures.FragmentLighting.Valid() &&
              draw.FragmentUniformBytes.size() >= Extension::kPicaPackedFragmentLightingProgramOffset +
                  sizeof(Renderer3ds::PicaLightingProgram))) &&
            draw.FragmentUniformBytes.size() >= Extension::kPicaPackedFragmentTevProgramOffset +
                sizeof(Renderer3ds::PicaTevProgram)) {
            Renderer3ds::PicaTevProgram tev;
            std::memcpy(&tev, draw.FragmentUniformBytes.data() +
                Extension::kPicaPackedFragmentTevProgramOffset, sizeof(tev));
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
                if (NativePicaFragmentLightingOperand() && pbrOptions.NativeFragmentLighting &&
                    draw.FragmentUniformBytes.size() >= Extension::kPicaPackedFragmentTevProgramOffset +
                        sizeof(Renderer3ds::PicaTevProgram)) {
                    Renderer3ds::PicaTevProgram tev{};
                    std::memcpy(&tev,draw.FragmentUniformBytes.data()+Extension::kPicaPackedFragmentTevProgramOffset,sizeof(tev));
                    pbrOptions.PreserveNativeUnlit = !Renderer3ds::PicaTevUsesFragmentLighting(tev);
                    if (pbrOptions.PreserveNativeUnlit) pbrOptions.NativeFragmentLighting = false;
                }
                const auto& layout = draw.FragmentFeatures.FragmentLighting;
                for (size_t slot = 0; slot < layout.ActiveLightCount; ++slot) {
                    const auto& light = layout.Lights[layout.LightPermutation[slot]];
                    pbrOptions.NativeAttenuationLuts |= light.SpotAttenuationEnabled || light.DistanceAttenuationEnabled;
                }
                Renderer3ds::PicaLightingProgram lighting;
                if (draw.FragmentUniformBytes.size() >= Extension::kPicaPackedFragmentLightingProgramOffset + sizeof(lighting))
                    std::memcpy(&lighting, draw.FragmentUniformBytes.data() +
                        Extension::kPicaPackedFragmentLightingProgramOffset, sizeof(lighting));
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
        // Installed native-fragment GGX consumes the same proved slot gains,
        // retaining native ambient while evaluating surviving/replacement
        // radiance once with its GGX BRDF and the existing cube visibility.
        pbrOptions.NativeBalance = pbrMaterial != nullptr && pbrOptions.NativeFragmentLighting &&
            fragmentBalance.has_value();
        if (graphicsSettings.Effects.SurfaceLighting == Extension::SurfaceLightingMode::NativePbr &&
            pbrMaterial == nullptr)
            surfaceLighting.reset();
        // AO texture relief reads the one texture whose colour the draw's TEV
        // passes to its output (never a bump or shadow unit); several colour
        // textures leave the draw without relief rather than guess a layer.
        uint8_t reliefTexture = 0xffU;
        if (!nativeMemoryOnly && sceneDraw && draw.DepthWriteEnabled &&
            graphicsSettings.Effects.AmbientOcclusion != Extension::AmbientOcclusionMode::Off &&
            graphicsSettings.Effects.AoRelief != Extension::AoTextureRelief::Off &&
            draw.FragmentUniformBytes.size() >= Extension::kPicaPackedFragmentTevProgramOffset +
                sizeof(Renderer3ds::PicaTevProgram)) {
            Renderer3ds::PicaTevProgram tev;
            std::memcpy(&tev, draw.FragmentUniformBytes.data() +
                Extension::kPicaPackedFragmentTevProgramOffset, sizeof(tev));
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
        // Nor does it reflect: its colour is not a surface's.
        if (authoredAppearance) resolvedReflectionMaterial.reset();
        const Extension::PicaShaderPipelineRequest variantRequest{
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
                // Authored appearance, like an isolated object, publishes no
                // ambient occlusion response and no GI albedo; nor does a
                // draw that is all water.
                isolatedLighting || authoredAppearance || waterSurface.Body.has_value() || waterSurface.WholeDraw,
            },
            mTemporalMotionEnabled && !nativeMemoryOnly,
            resolvedReflectionMaterial.has_value()
                ? std::optional<Extension::ReflectionMaterialParameters>(
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
            surfaceLighting && (fragmentBalance.has_value() || (surfaceLighting->Control[3] & 2U) != 0U),
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
            surfaceCandidate && surfaceDebug.ShadowCode(surfaceOrdinal) != 0U,
#endif
            fragmentBalance.has_value(),
            nativeFragmentLitReceivers,
        };
        static const Extension::EffectsSettings nativeMemoryEffects;
        const auto& variantEffects = nativeMemoryOnly ? nativeMemoryEffects : graphicsSettings.Effects;
        bool variantCacheHit = false;
        const auto& shaderVariant =
            mPicaShaderPipelineCache.Resolve(
                variantRequest, variantEffects, &variantCacheHit);
        // Cache hits too: the recording is of requests, not of misses.
        mPicaVariantRequestRecorder.Observe(draw.CanonicalPipelineId, variantRequest, variantEffects,
                                            graphicsSettings, mFrameGraphicsSettingsRevision, shaderVariant);
        const bool fragmentBalanceApplied = fragmentBalance && Extension::HasPicaShaderInstrumentationFeature(
            shaderVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::FragmentLightingBalance);
        if (pointReplacement && !fragmentBalanceApplied) {
            // A fallback shader (or a TEV-unlit material) cannot suppress the
            // native counterpart. Never leave its supplemental copy enabled.
            if (surfaceLighting) surfaceLighting->Control[1] = 0;
            pointReplacement.reset();
        }
        if (surfaceLighting && (fragmentBalanceApplied || !Extension::HasPicaShaderInstrumentationFeature(
                shaderVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::FragmentLightingOperand)))
            Renderer3ds::FoldPicaSurfaceMaterialForPbr(*surfaceLighting,
                fragmentBalanceApplied || pbrOptions.PreserveNativeUnlit);
        // Both evaluators own native slots through the same ledger. Fold the
        // explicit additions once, then remove their duplicate native terms.
        // Do this only after admission: fallback shaders keep their old inputs.
        if (surfaceLighting && fragmentBalanceApplied)
            Renderer3ds::PreparePicaFragmentProfileSurface(*surfaceLighting, *fragmentBalance, mFrameEnvironment);
        if (surfaceLighting && pointReplacement && fragmentBalance)
            Renderer3ds::ApplyPicaNativePointReplacementControls(*surfaceLighting, *pointReplacement, mFrameEnvironment);
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
        if (surfaceCandidate && surfaceDebug.Tracing()) {
            // One candidate: its native colour computation and what Modern did.
            Renderer3ds::PicaTevProgram tev{};
            const bool tevKnown = draw.FragmentUniformBytes.size() >=
                Extension::kPicaPackedFragmentTevProgramOffset + sizeof(tev);
            if (tevKnown)
                std::memcpy(&tev, draw.FragmentUniformBytes.data() + Extension::kPicaPackedFragmentTevProgramOffset,
                            sizeof(tev));
            const bool pbrApplied = Extension::HasPicaShaderInstrumentationFeature(
                shaderVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::PbrMaterial);
            const bool perPixelApplied = Extension::HasPicaShaderInstrumentationFeature(
                shaderVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::NativePerPixelLighting);
            const bool vertexLightingKnown = draw.TemporalVertexProgram.Hooks.SurfaceProgram() != nullptr;
            char line[1024];
            std::snprintf(line, sizeof(line),
                "{\"frame\":%llu,\"ordinal\":%u,\"submission\":%llu,\"layer\":%u,\"provenance\":%u,"
                "\"actor\":%d,\"actorIdentity\":\"%llx\",\"isolated\":%d,\"vertexLit\":%d,\"fragmentLighting\":%d,"
                "\"fog\":%d,\"blend\":[%d,%u,%u,%u],\"alphaTest\":%d,\"depthWrite\":%d,\"textures\":%zu,"
                "\"vertices\":%u,\"tevColorSources\":\"%04x\",\"tevKnown\":%d,\"fragmentProgram\":\"%llx\","
                "\"geometry\":\"%llx\",\"surfaceUniforms\":%d,\"perPixel\":%d,\"pbr\":%d,\"exempt\":%d,"
                "\"appearance\":%u,\"vertexLightingKnown\":%d}",
                static_cast<unsigned long long>(surfaceDebug.Frame()), surfaceOrdinal,
                static_cast<unsigned long long>(draw.SubmissionId), static_cast<unsigned>(draw.Composition.Layer),
                static_cast<unsigned>(draw.Composition.Provenance), draw.Composition.NativeActorGeometry ? 1 : 0,
                static_cast<unsigned long long>(draw.Composition.NativeActorIdentity),
                draw.Composition.IsolatedLighting ? 1 : 0,
                nativeVertexLighting ? 1 : 0,
                draw.FragmentFeatures.FragmentLightingEnabled ? 1 : 0, draw.FragmentFeatures.FogEnabled ? 1 : 0,
                draw.Blend.Enabled ? 1 : 0, static_cast<unsigned>(draw.Blend.EquationRgb),
                static_cast<unsigned>(draw.Blend.SourceRgb), static_cast<unsigned>(draw.Blend.DestRgb),
                draw.AlphaTestEnabled ? 1 : 0, draw.DepthWriteEnabled ? 1 : 0, draw.Textures.size(),
                draw.VertexCount, tevKnown ? Renderer3ds::PicaTevColorSourceMask(tev) : 0U, tevKnown ? 1 : 0,
                static_cast<unsigned long long>(draw.CanonicalFragmentProgramId),
                static_cast<unsigned long long>(draw.GeometryIdentity), surfaceLighting.has_value() ? 1 : 0,
                perPixelApplied ? 1 : 0, pbrApplied ? 1 : 0, surfaceExempt ? 1 : 0,
                static_cast<unsigned>(surfaceAppearance), vertexLightingKnown ? 1 : 0);
            std::string record = line;
            {
                // Texture-matrix translations (row U/V .w) of each coordinate:
                // two traced frames give scrolling speed (flowing water, lava).
                const auto floats = draw.VertexUniformBytes.size() > Extension::kPicaPackedVertexFloatOffset
                    ? draw.VertexUniformBytes.subspan(Extension::kPicaPackedVertexFloatOffset)
                    : std::span<const uint8_t>{};
                const auto readW = [&](uint8_t uniform, float& value) {
                    const size_t offset = static_cast<size_t>(uniform) * 16U + 12U;
                    if (uniform >= 96U || offset + sizeof(float) > floats.size()) return false;
                    std::memcpy(&value, floats.data() + offset, sizeof(float));
                    return std::isfinite(value);
                };
                std::string shifts = ",\"texShift\":[";
                for (uint8_t coordinate = 0; coordinate < 3U; ++coordinate) {
                    const auto* layout = draw.TemporalVertexProgram.Hooks.TextureCoordinate(coordinate);
                    float u = 0.0F, v = 0.0F;
                    char text[64];
                    if (layout != nullptr && readW(layout->MatrixRowUUniform, u) && readW(layout->MatrixRowVUniform, v))
                        std::snprintf(text, sizeof(text), "%s[%.5f,%.5f]", coordinate ? "," : "", u, v);
                    else
                        std::snprintf(text, sizeof(text), "%snull", coordinate ? "," : "");
                    shifts += text;
                }
                record.pop_back();
                record += shifts + "]}";
            }
            {
                char route[384];
                std::snprintf(route, sizeof(route),
                    ",\"fragmentOperand\":%d,\"waterCount\":%u,\"waterBoundsKnown\":%d,"
                    "\"waterClassifierMode\":%.0f,\"surfaceControl\":[%u,%u,%u,%u]}",
                    Extension::HasPicaShaderInstrumentationFeature(shaderVariant.AppliedFeatures,
                        Extension::PicaShaderInstrumentationFeature::FragmentLightingOperand) ? 1 : 0,
                    draw.LightBinding.Water ? draw.LightBinding.Water->Count : 0U,
                    waterSurface.WorldBounds.Available ? 1 : 0,
                    surfaceLighting ? surfaceLighting->WaterViewTranslation[3] : 0.0F,
                    surfaceLighting ? surfaceLighting->Control[0] : 0U,
                    surfaceLighting ? surfaceLighting->Control[1] : 0U,
                    surfaceLighting ? surfaceLighting->Control[2] : 0U,
                    surfaceLighting ? surfaceLighting->Control[3] : 0U);
                record.pop_back(); record += route;
            }
            {
                // The draw's native light slots and environment lights as the
                // title classified them: scope (scene/local), origin, kind.
                const auto kindName = [](Renderer3ds::PicaNativeLightSource::Type kind) {
                    return kind == Renderer3ds::PicaNativeLightSource::Type::Directional ? "directional"
                        : kind == Renderer3ds::PicaNativeLightSource::Type::Point      ? "point"
                                                                                        : "none";
                };
                const auto nativeTrace = Extension::DecodePicaNativeDrawEnvironment(
                    draw.VertexUniformBytes, draw.FragmentUniformBytes, draw.FragmentFeatures).FragmentLighting;
                std::string lights = ",\"classifiedLights\":[";
                for (size_t slot = 0; slot < draw.LightBinding.Sources.size(); ++slot) {
                    const auto& source = draw.LightBinding.Sources[slot];
                    char text[1024];
                    const auto& n = nativeTrace.Lights[slot];
                    std::snprintf(text, sizeof(text),
                                  "%s{\"slot\":%zu,\"kind\":\"%s\",\"scope\":\"%s\",\"origin\":\"%s\",\"id\":\"%llx\","
                                  "\"position\":[%.9g,%.9g,%.9g],\"radius\":%.9g,\"unattenuated\":[%.9g,%.9g,%.9g],"
                                  "\"reference\":[%.9g,%.9g,%.9g],\"nativeDiffuse\":[%.9g,%.9g,%.9g],"
                                  "\"nativePosition\":[%.9g,%.9g,%.9g],\"nativeAttenuation\":[%.9g,%.9g]}",
                                  slot ? "," : "", slot, kindName(source.Kind),
                                  Renderer3ds::PicaNativeLightScopeName(source.Scope),
                                  Renderer3ds::PicaNativeLightOriginName(source.Origin),
                                  static_cast<unsigned long long>(source.SourceIdentity),
                                  source.PositionWorld[0], source.PositionWorld[1], source.PositionWorld[2], source.Radius,
                                  source.UnattenuatedColor[0], source.UnattenuatedColor[1], source.UnattenuatedColor[2],
                                  source.ReferenceColor[0], source.ReferenceColor[1], source.ReferenceColor[2],
                                  n.Diffuse[0], n.Diffuse[1], n.Diffuse[2],
                                  n.PositionOrDirectionView[0], n.PositionOrDirectionView[1], n.PositionOrDirectionView[2],
                                  n.DistanceAttenuationBias, n.DistanceAttenuationScale);
                    lights += text;
                }
                lights += "],\"selectedPoints\":[";
                if (const auto& points = draw.LightBinding.ScenePoints) {
                    for (uint32_t p = 0; p < points->Count; ++p) {
                        const auto& point = points->Lights[p];
                        std::array<float, 3> positionView{};
                        if (mSurfaceLightingView) for (size_t r = 0; r < 3; ++r) {
                            positionView[r] = mSurfaceLightingView->Rows[r][3];
                            for (size_t c = 0; c < 3; ++c)
                                positionView[r] += mSurfaceLightingView->Rows[r][c] * point.PositionWorld[c];
                        }
                        char text[512];
                        std::snprintf(text, sizeof(text),
                            "%s{\"id\":\"%llx\",\"position\":[%.9g,%.9g,%.9g],\"radius\":%.9g,\"color\":[%.9g,%.9g,%.9g],"
                            "\"replacementPositionView\":[%.9g,%.9g,%.9g]}",
                            p ? "," : "", static_cast<unsigned long long>(point.SourceIdentity),
                            point.PositionWorld[0], point.PositionWorld[1], point.PositionWorld[2], point.Radius,
                            point.Color[0], point.Color[1], point.Color[2],
                            positionView[0], positionView[1], positionView[2]);
                        lights += text;
                    }
                }
                lights += "],\"environmentLights\":";
                if (const auto& scene = draw.LightBinding.SceneDirectionals) {
                    char text[160];
                    std::snprintf(text, sizeof(text), "{\"count\":%u,\"scope\":\"%s\",\"bakedOutdoorRoom\":%d,\"ids\":[",
                                  scene->Count, Renderer3ds::PicaNativeLightScopeName(scene->Scope),
                                  scene->BakedOutdoorRoom ? 1 : 0);
                    lights += text;
                    for (uint32_t i = 0; i < std::min<uint32_t>(scene->Count, scene->Lights.size()); ++i) {
                        std::snprintf(text, sizeof(text), "%s\"%llx\"", i ? "," : "",
                                      static_cast<unsigned long long>(scene->Lights[i].SourceIdentity));
                        lights += text;
                    }
                    lights += "]}";
                } else {
                    lights += "null";
                }
                record.pop_back();
                record += lights + "}";
            }
            if (waterSurface.WorldBounds.Available) {
                const auto& b = waterSurface.WorldBounds;
                char water[256];
                std::snprintf(water, sizeof(water),
                    ",\"worldMin\":[%.1f,%.1f,%.1f],\"worldMax\":[%.1f,%.1f,%.1f],\"waterBody\":%d,"
                    "\"reachesWater\":%d,\"wholeWater\":%d,\"waterPlane\":%d}",
                    b.Minimum[0], b.Minimum[1], b.Minimum[2], b.Maximum[0], b.Maximum[1], b.Maximum[2],
                    waterSurface.Body ? static_cast<int>(*waterSurface.Body) : -1, waterSurface.ReachesWater ? 1 : 0,
                    waterSurface.WholeDraw ? 1 : 0, waterSurface.Plane ? 1 : 0);
                record.pop_back();
                record += water;
            }
            {
                // Texture identities (base-level content hash, size, slot), as
                // texture-keyed materials select them: building water catalogs.
                std::string ids = ",\"textureIds\":[";
                for (const auto& texture : draw.Textures) {
                    const auto hash = ResolveNativeTextureBaseLevelContentHash(texture);
                    char text[96];
                    std::snprintf(text, sizeof(text), "%s\"%016llx:%ux%u:%u\"", ids.back() == '[' ? "" : ",",
                                  static_cast<unsigned long long>(hash.value_or(0U)), texture.Width, texture.Height,
                                  static_cast<unsigned>(texture.Slot));
                    ids += text;
                }
                record.pop_back();
                record += ids + "]}";
            }
            if (surfaceLighting) {
                // The native material and lights the replacement read.
                const auto& s = *surfaceLighting;
                char material[512];
                std::snprintf(material, sizeof(material),
                    ",\"pbrInstalled\":%u,\"pbrNativeBalance\":%u,\"pbrNativeUnlit\":%u,"
                    "\"materialDiffuse\":[%.3f,%.3f,%.3f],\"materialAmbient\":[%.3f,%.3f,%.3f],"
                    "\"resolvedAmbient\":[%.3f,%.3f,%.3f],\"nativeLights\":[[%.3f,%.3f,%.3f,%.0f],"
                    "[%.3f,%.3f,%.3f,%.0f],[%.3f,%.3f,%.3f,%.0f]]}",
                    shaderVariant.Decisions.Has(Extension::PicaEffectDecisions::Pbr) ? 1U : 0U,
                    shaderVariant.Decisions.PbrNativeBalance ? 1U : 0U,
                    shaderVariant.Decisions.PbrNativeUnlit ? 1U : 0U,
                    s.MaterialDiffuse[0], s.MaterialDiffuse[1], s.MaterialDiffuse[2], s.MaterialAmbient[0],
                    s.MaterialAmbient[1], s.MaterialAmbient[2], s.ResolvedAmbient[0], s.ResolvedAmbient[1],
                    s.ResolvedAmbient[2], s.Lights[0].Diffuse[0], s.Lights[0].Diffuse[1], s.Lights[0].Diffuse[2],
                    s.Lights[0].DirectionEnabled[3], s.Lights[1].Diffuse[0], s.Lights[1].Diffuse[1],
                    s.Lights[1].Diffuse[2], s.Lights[1].DirectionEnabled[3], s.Lights[2].Diffuse[0],
                    s.Lights[2].Diffuse[1], s.Lights[2].Diffuse[2], s.Lights[2].DirectionEnabled[3]);
                record.pop_back();
                record += material;
            }
            if (pointReplacement) {
                char replacement[160];
                std::snprintf(replacement, sizeof(replacement),
                    ",\"pointReplacement\":{\"nativeSlotMask\":%u,\"points\":%u,\"ggxNativeFallback\":%u,\"ids\":[",
                    pointReplacement->ReplacedSlots, pointReplacement->Points.Count,
                     pbrMaterial && !pbrOptions.NativeBalance ? 1U : 0U);
                record.pop_back();
                record += replacement;
                for (uint32_t i = 0; i < pointReplacement->Points.Count; ++i) {
                    char identity[32];
                    std::snprintf(identity, sizeof(identity), "%s\"%llx\"", i ? "," : "",
                        static_cast<unsigned long long>(pointReplacement->Points.Lights[i].SourceIdentity));
                    record += identity;
                }
                record += "]},\"nativePointControls\":[";
                bool firstControl = true;
                if (surfaceLighting) for (uint32_t i = 0; i < 8; ++i) {
                    const auto& gain = pointReplacement->Balance.Gains[i];
                    if (gain[3] < 4) continue;
                    const auto p = static_cast<uint32_t>(gain[3]) - 4U;
                    const auto& color = surfaceLighting->ScenePoints[p].Color;
                    char control[256];
                    std::snprintf(control, sizeof(control),
                        "%s{\"slot\":%u,\"pointRow\":%u,\"intensity\":%.9g,\"radiusScale\":%.9g,"
                        "\"tint\":[%.9g,%.9g,%.9g],\"shadowRow\":%.9g}", firstControl ? "" : ",", i, p,
                        gain[0], gain[1], color[0], color[1], color[2], color[3]);
                    record += control; firstControl = false;
                }
                record += "]}";
            }
            if (fragmentBalance) {
                char balance[256];
                std::snprintf(balance,sizeof(balance),
                    ",\"fragmentBalance\":%u,\"balanceKeyIndex\":%u,\"secondaryMaterialFactor\":%u,\"balanceGlobalAmbientGain\":%.9g}",
                    fragmentBalanceApplied?1U:0U,fragmentBalance->Control[1],fragmentBalance->Control[2],
                    static_cast<double>(fragmentBalance->Parameters[0]));
                record.pop_back();record+=balance;
            }
            surfaceDebug.Write(record);
            // An exempted candidate's native shaders, next to the trace.
            if (surfaceExempt)
                surfaceDebug.WriteSource(surfaceOrdinal, draw.VertexShaderSource, draw.FragmentShaderSource);
        }
#endif
        if (pbrMaterial != nullptr && pbrMaterial->Material->ColorReplacement &&
            Extension::HasPicaShaderInstrumentationFeature(
                shaderVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::PbrMaterial))
            textures[pbrOptions.TextureSlot] = &pbrMaterial->Images[Renderer3ds::kPicaPbrTextureCount];
        const auto variantCacheStats =
            mPicaShaderPipelineCache.Stats();
        mDiagnostics.RecordPicaShaderSelection(
            shaderVariant.IsCanonical(),
            shaderVariant.RequestedFeatures !=
                Extension::PicaShaderInstrumentationFeature::None,
            static_cast<uint32_t>(std::min<size_t>(
                variantCacheStats.CanonicalEntries,
                std::numeric_limits<uint32_t>::max())),
            static_cast<uint32_t>(std::min<size_t>(
                variantCacheStats.InstrumentationEntries,
                std::numeric_limits<uint32_t>::max())),
            shaderVariant.DirectFragmentHooksUsed,
            Extension::HasPicaShaderInstrumentationFeature(
                shaderVariant.RequestedFeatures,
                Extension::PicaShaderInstrumentationFeature::TemporalVertex),
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
            Extension::PicaAotShaderStage::Vertex,
            effectiveDraw.VertexShaderSource,
            effectiveDraw.CanonicalDescriptorSchemaVersion,
            effectiveDraw.CanonicalPipelineId,
            effectiveDraw.VertexShaderKey,
            mFrameGraphicsSettingsRevision);
        mPicaEffectiveShaderInventory.Observe(
            Extension::PicaAotShaderStage::Fragment,
            effectiveDraw.FragmentShaderSource,
            effectiveDraw.CanonicalDescriptorSchemaVersion,
            effectiveDraw.CanonicalPipelineId,
            effectiveDraw.FragmentShaderKey,
            mFrameGraphicsSettingsRevision);

        Extension::TraceOutlineDraw(mFrameCounter, draw, grassPerspectiveProjection,
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
                            Extension::PicaToonEligibility::Eligible
                        ? 1U
                        : 0U);
            }
        }
        if (graphicsSettings.Effects.Toon != Extension::ToonMode::Off) {
            auto& telemetry = Extension::PicaToonTelemetry::Instance();
            const uint64_t telemetryTotal =
                telemetry.Record(shaderVariant.ToonEligibility);
            if (shaderVariant.ToonEligibility ==
                Extension::PicaToonEligibility::Eligible) {
                mDiagnostics.RecordToonDraw(
                    shaderVariant.ToonMaterialPath);
            }
            if (telemetryTotal == 512U) {
                const auto snapshot = telemetry.Snapshot();
                spdlog::info(
                    "PICA toon classifier: applied={}, outside-scene={}, no-depth={}, blended={}, no-rgb={}, unsupported={}",
                    snapshot.Count(Extension::PicaToonEligibility::Eligible),
                    snapshot.Count(Extension::PicaToonEligibility::OutsideScene),
                    snapshot.Count(Extension::PicaToonEligibility::NoDepth),
                    snapshot.Count(Extension::PicaToonEligibility::Blended),
                    snapshot.Count(Extension::PicaToonEligibility::NoRgbOutput),
                    snapshot.Count(Extension::PicaToonEligibility::UnsupportedShader));
            }
        }
        if (Extension::HasPicaShaderInstrumentationFeature(
                shaderVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::NormalGuide))
            mDiagnostics.RecordNormalGuideDraw(
                shaderVariant.TypedNormalGuide,
                Extension::HasPicaShaderInstrumentationFeature(
                    shaderVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::NormalGuideTextureRelief),
                draw.FragmentFeatures.FragmentLightingEnabled &&
                    shaderVariant.Decisions.Normal == Extension::PicaEffectDecisions::NormalPath::Material,
                shaderVariant.Decisions.Normal == Extension::PicaEffectDecisions::NormalPath::Pbr);
        if (shaderVariant.AmbientGuideEligibility ==
                Extension::PicaAmbientOcclusionGuideEligibility::
                    AppliedExact ||
            shaderVariant.AmbientGuideEligibility ==
                Extension::PicaAmbientOcclusionGuideEligibility::
                    AppliedFallback) {
            mDiagnostics.RecordAmbientOcclusionAmbientGuideDraw(
                shaderVariant.AmbientGuideEligibility ==
                    Extension::PicaAmbientOcclusionGuideEligibility::
                        AppliedExact);
        }
        if (reflectionsEnabled) {
            const auto eligibility =
                shaderVariant.ReflectionEligibility;
            const bool profiled =
                eligibility ==
                Extension::PicaReflectionMaterialEligibility::
                    ExplicitTextureProfile;
            mDiagnostics.RecordReflectionMaterialDraw(
                eligibility ==
                        Extension::PicaReflectionMaterialEligibility::
                            Eligible ||
                    profiled,
                profiled,
                eligibility ==
                    Extension::PicaReflectionMaterialEligibility::
                        NoSpecularTevUse,
                eligibility ==
                    Extension::PicaReflectionMaterialEligibility::
                        UnsupportedShader);
            const bool worldMaterialCandidate =
                eligibility ==
                    Extension::PicaReflectionMaterialEligibility::Eligible ||
                eligibility ==
                    Extension::PicaReflectionMaterialEligibility::
                        ExplicitTextureProfile ||
                eligibility ==
                    Extension::PicaReflectionMaterialEligibility::
                        NoSpecularTevUse ||
                eligibility ==
                    Extension::PicaReflectionMaterialEligibility::
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
            shader.VertexSource = Extension::IdentifyPicaAotShaderSource(
                effectiveDraw.VertexShaderSource);
            shader.FragmentSource = Extension::IdentifyPicaAotShaderSource(
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
                Extension::SceneViewRuntime::Instance()
                    .LatestPerspective();
            if (latestGrassPerspective)
                if (const auto projection = Extension::DecodePicaPerspectiveProjection(draw.VertexUniformBytes))
                    latestGrassPerspective = Extension::ApplySampleProjection(*latestGrassPerspective, *projection);
            if (latestGrassPerspective && draw.LightBinding.WorldToView)
                latestGrassPerspective = Extension::ResolveSamplePerspective(
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
                grassSourceDecision.Authorized() && grassPerspective != nullptr &&
                PublishNativePicaGrassSurface(
                    draw, mFrameCounter, mGrassSurfaceOccurrencesThisFrame, graphicsSettings.Grass, opaqueWorldDraw,
                    shaderVariant.GrassTexture0Sampled, grassPerspectiveProjection, mGrassGeometryRegistry,
                    mGrassDrawMemo, mNativePicaCompositionSchedule.SequenceId(),
                    mGrassDrawOrdinals.at(draw.SubmissionId));
            mInteractiveGrassProviderExecution.RecordSourceInspection(
                grassSourceDecision, grassSourcePublished);
            if (!grassSourceDecision.Authorized() || grassPerspective == nullptr)
                AuditNativePicaGrassGate(draw, graphicsSettings.Grass,
                                         !grassSourceDecision.Authorized() ? "provider_unauthorized"
                                                                           : "no_perspective_camera");
        } else if (!nativeMemoryOnly && sceneDraw && mInteractiveGrassProviderPlan.Valid()) {
            AuditNativePicaGrassGate(
                draw, graphicsSettings.Grass,
                draw.Composition.Layer == ::Oot3d::Renderer::PicaCompositionLayer::TransparentWorld ? "layer_transparent_world"
                : draw.Composition.Layer == ::Oot3d::Renderer::PicaCompositionLayer::Atmosphere   ? "layer_atmosphere"
                : draw.Composition.Layer == ::Oot3d::Renderer::PicaCompositionLayer::Ui           ? "layer_ui"
                                                                                                  : "layer_unknown");
        }
        finishCpuStage(cpuTimings.GrassSurfaceMilliseconds);

        auto& renderTarget = GetOrCreateNativePicaRenderTarget(draw);
        finishCpuStage(cpuTimings.RenderTargetStateMilliseconds);
        ExecuteNativePicaBeforeDrawAnchors(renderTarget, draw,
            cpuTimings.DirectionalShadowMilliseconds, cpuTimings.GrassRenderMilliseconds);
        if (collectCpuTimings) cpuStageStart = CpuClock::now();
        renderTarget.WBuffering = draw.WBuffering;
        // The isolated title logo once the boundary before it has resolved the
        // scene: a layer over that result, drawn like native UI is (no
        // projection jitter, no motion, no scene-effect input, and its pixels
        // keep the completed scene's serials for the display and the upscaler).
        // Without a boundary (no scene stage, or world draws after it) it is
        // a scene draw as before.
        const bool sceneOverlayDraw = sceneDraw && !nativeMemoryOnly &&
            !mNativeFidelityProfile && draw.Composition.IsolatedLighting &&
            renderTarget.SceneEffectsResolvedSerial != 0U &&
            renderTarget.SceneEffectsResolvedSerial == renderTarget.ContentWriteSerial;
        if (sceneOverlayDraw) mDiagnostics.RecordSceneOverlayDraw();
        const bool reflectionEnvironmentEligible =
            sceneDraw && !sceneOverlayDraw &&
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
        Extension::RigidMotionSample rigidMotion{};
        const std::array<float, 2> jitterUv = mTemporalJitterEnabled && !nativeMemoryOnly && !sceneOverlayDraw
            ? std::array<float, 2>{
                  mTemporalJitterPixels[0] /
                      static_cast<float>(renderTarget.Width),
                  mTemporalJitterPixels[1] /
                      static_cast<float>(renderTarget.Height)}
            : std::array<float, 2>{};
        std::array<float, 4> rigidOriginClip{};
        const bool temporalEligible = mTemporalMotionEnabled && !nativeMemoryOnly &&
            sceneDraw && !sceneOverlayDraw &&
            draw.FragmentOperationMode == 0U &&
            draw.DepthTestEnabled && draw.DepthWriteEnabled;
        std::span<const uint8_t> previousVertexUniformBytes = draw.VertexUniformBytes;
        std::array<float, 2> previousJitterNdc{2.0F * jitterUv[0], 2.0F * jitterUv[1]};
        Renderer3ds::PicaTemporalMatch temporalMatch;
        if (temporalEligible) {
            temporalMatch = mPicaTemporalHistory.Track(draw, mFrameCounter,
                mPicaSceneFrame.TemporalSample(), previousJitterNdc);
            if (temporalMatch.Exact()) {
                previousVertexUniformBytes = temporalMatch.Previous->Bytes;
                previousJitterNdc = temporalMatch.Previous->JitterNdc;
            }
        }
        const bool repeatedTemporalIdentity =
            temporalMatch.Reason == Renderer3ds::PicaTemporalMatchReason::Ambiguous;
        const bool rigidEligible = temporalEligible && temporalMatch.Exact() &&
            draw.TemporalVertexProgram.Hooks.TransformProgram() != nullptr &&
            Extension::DecodeCommonRigidOriginClip(draw.VertexUniformBytes, draw.FramebufferFlipped, rigidOriginClip);
        if (rigidEligible) {
            std::array<float, 4> previousClip{};
            if (Extension::DecodeCommonRigidOriginClip(previousVertexUniformBytes, draw.FramebufferFlipped, previousClip) &&
                rigidOriginClip[3] > 1.0e-7F && previousClip[3] > 1.0e-7F) {
                for (size_t axis = 0; axis < 2; ++axis)
                    rigidMotion.MotionUv[axis] = (previousClip[axis] / previousClip[3] -
                        rigidOriginClip[axis] / rigidOriginClip[3]) * 0.5F + previousJitterNdc[axis] * 0.5F - jitterUv[axis];
                rigidMotion.Valid = std::isfinite(rigidMotion.MotionUv[0]) && std::isfinite(rigidMotion.MotionUv[1]) &&
                    std::abs(rigidMotion.MotionUv[0]) <= 1.0F && std::abs(rigidMotion.MotionUv[1]) <= 1.0F;
            }
        }
        const bool exactMotionHistory = temporalMatch.Exact() && shaderVariant.TemporalVertexApplied &&
            shaderVariant.RigidMotionApplied;
        const auto motionProvenance = Renderer3ds::ResolvePicaMotionProvenance(exactMotionHistory,
            rigidMotion.Valid && shaderVariant.RigidMotionApplied,
            temporalEligible && draw.TemporalCameraOnly && shaderVariant.RigidMotionApplied);
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
        const Extension::PicaShaderPipelineResult* selectedVariant = &shaderVariant;
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
                    // Linked from the startup program parts, never a recipe of
                    // the portable pack: it stays out of the pipeline inventory.
                    auto& uberPipeline = GetOrCreateNativePicaPipeline(
                        uberDraw, *uberProgram, shaderVariant.Reactive, shaderVariant.Domain,
                        shaderVariant.RequestedFeatures, shaderVariant.AppliedFeatures, false, false, false,
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
                counters.UberSurfaceDraws += decisions.Has(Extension::PicaEffectDecisions::SurfaceLighting) ? 1U : 0U;
                counters.UberPbrDraws += decisions.Has(Extension::PicaEffectDecisions::Pbr) ? 1U : 0U;
                counters.UberReliefDraws += decisions.Has(Extension::PicaEffectDecisions::Relief) ? 1U : 0U;
                counters.UberToonDraws += decisions.Has(Extension::PicaEffectDecisions::Toon) ? 1U : 0U;
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
        if (mSurfaceCalibrationTrace.Sampling())
            TraceSurfaceCalibrationDraw(draw, drawVariant, surfaceLighting, surfaceCandidate, surfaceExempt,
                authoredAppearance, isolatedLighting, grassPerspectiveProjection, selectedDraw == &uberDraw);
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
        auto nriDrawOwnership = Extension::PreparePicaNriDrawOwnership(
            nriOwnedDrawsEnabled, nriOwnedPipelineReady);
        std::vector<Extension::PicaNriSourceVertexStream>
            nriDrawSourceStreams;
        std::vector<Extension::PicaNriSourceVertexAttribute>
            nriDrawSourceAttributes;
        nriDrawSourceStreams.reserve(draw.VertexBindings.size());
        for (const auto& binding : draw.VertexBindings) {
            nriDrawSourceStreams.push_back(
                { { binding.Binding, binding.ByteStride, binding.PerInstance }, binding.Bytes });
        }
        nriDrawSourceAttributes.reserve(draw.VertexAttributes.size());
        for (const auto& attribute : draw.VertexAttributes) {
            nriDrawSourceAttributes.push_back({ attribute.Location, attribute.Binding,
                                                static_cast<Extension::PicaNriVertexScalar>(attribute.Format),
                                                attribute.ComponentCount, attribute.ByteOffset });
        }
        const bool directionalShadowGeometry =
            mDirectionalShadowMapBarrierPlan.Valid() &&
            Extension::PicaExtensionPassEnabled(
                mPicaExtensionGraph,
                Extension::PicaExtensionPass::DirectionalShadowLighting);
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
            Extension::HasPicaShaderInstrumentationFeature(
                drawVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::PbrMaterial)) {
            std::array<Extension::PicaNriSourceVertexBinding, 16> sourceBindings{};
            if (nriDrawSourceStreams.size() > sourceBindings.size())
                throw std::runtime_error("PICA seam input exceeds native binding count");
            for (size_t i = 0; i < nriDrawSourceStreams.size(); ++i)
                sourceBindings[i] = nriDrawSourceStreams[i].Binding;
            const auto layout = Extension::BuildPicaNriVertexInputLayout(
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
                Extension::kPicaPackedVertexBooleanMaskOffset, sizeof(booleans));
            auto& cache = geometryResolution.Cacheable
                ? mNativePicaGeometryBuffers[geometry.Identity].Seams : transientSeams;
            seamData = &cache.Resolve(view, draw.TemporalVertexProgram.Hooks, booleans,
                draw.VertexUniformBytes.subspan(Extension::kPicaPackedVertexFloatOffset),
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
        std::vector<Extension::NriPicaVertexBufferBindingDesc>
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

        Extension::NriDirectionalShadowHistorySnapshot
            directionalShadowHistory;
        Extension::PicaDirectionalShadowReceiverBinding
            directionalShadowBinding{};
        const bool directionalShadowShaderApplied =
            drawVariant.DirectionalShadowEligibility ==
                Extension::PicaDirectionalShadowLightingEligibility::VertexPrimary ||
            drawVariant.DirectionalShadowEligibility ==
                Extension::PicaDirectionalShadowLightingEligibility::FragmentPrimary;
        if (directionalShadowReceiver &&
            directionalShadowShaderApplied) {
            directionalShadowHistory =
                mNriDirectionalShadowPass.FindLatestHistory(
                    draw.RenderTargetNamespace,
                    draw.FramebufferColorPhysicalAddress,
                    mFrameCounter);
            if (directionalShadowHistory.Valid()) {
                const auto nativeEnvironment =
                    Extension::DecodePicaNativeDrawEnvironment(
                        draw.VertexUniformBytes,
                        draw.FragmentUniformBytes,
                        draw.FragmentFeatures);
                Extension::DirectionalShadowMatrix receiverViewToWorld;
                if (draw.LightBinding.WorldToView &&
                    Extension::InvertDirectionalShadowMatrix(*draw.LightBinding.WorldToView, receiverViewToWorld)) {
                    // Native fragment lights receive the shadow as themselves:
                    // through the PBR material's native loop, or, without
                    // per-vertex lighting, in the native per-pixel result.
                    const bool nativeFragmentLights =
                        (pbrMaterial != nullptr && pbrOptions.NativeFragmentLighting) ||
                        Extension::HasPicaShaderInstrumentationFeature(drawVariant.AppliedFeatures,
                            Extension::PicaShaderInstrumentationFeature::NativeFragmentLitShadows);
                    if (surfaceLighting &&
                        Extension::HasPicaShaderInstrumentationFeature(drawVariant.AppliedFeatures,
                            Extension::PicaShaderInstrumentationFeature::NativePerPixelLighting) &&
                        (surfaceLighting->EnvironmentParameters[3] != 0.0F ||
                         surfaceLighting->Control[0] != 0U ||
                         surfaceLighting->Control[2] != 0U ||
                         nativeFragmentLights)) {
                        std::array<Extension::PicaSurfaceShadowLight, 12> lights{};
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
                        directionalShadowBinding = Extension::BuildPicaSurfaceShadowReceiverBinding(
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
                Extension::PicaDirectionalShadowReceiverClass::LightingUnavailable,
            directionalShadowReceiverClass !=
                    Extension::PicaDirectionalShadowReceiverClass::LightingUnavailable &&
                directionalShadowReceiverClass !=
                    Extension::PicaDirectionalShadowReceiverClass::LightingDisabled,
            directionalShadowReceiverClass ==
                Extension::PicaDirectionalShadowReceiverClass::AmbientOnly,
            directionalShadowReceiverClass ==
                Extension::PicaDirectionalShadowReceiverClass::DirectUnmatched,
            directionalShadowReceiverClass ==
                Extension::PicaDirectionalShadowReceiverClass::DirectMatched,
            directionalShadowHistoryBound);
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
        if (surfaceCandidate && Extension::HasPicaShaderInstrumentationFeature(
                drawVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::ShadowProbe))
            directionalShadowBinding.Uniforms.Receiver[2] = float(surfaceDebug.ShadowCode(surfaceOrdinal));
#endif
        const auto& directionalShadowUniforms =
            directionalShadowBinding.Uniforms;
        const auto toonUniforms = Extension::PackToonSurfaceParameters(
            graphicsSettings.Effects.Toon, graphicsSettings.Effects.ToonStyle, false);
        // The superset effect program reads its decisions (and the toon
        // parameters) from the toon binding; a specialized program reads
        // the draw's own where isolated lighting would change its text.
        std::optional<Extension::PicaUberEffectBlock> effectBlock;
        if (selectedProgram->EffectBlock || drawVariant.ReadsDrawControl) {
            std::array<float, 32> toon{};
            static_assert(sizeof(toonUniforms) == sizeof(toon));
            std::memcpy(toon.data(), &toonUniforms, sizeof(toon));
            effectBlock = Extension::PackPicaUberEffectBlock(drawVariant.Decisions, toon);
        }

        const bool uniformToon = effectBlock.has_value() || Extension::HasPicaShaderInstrumentationFeature(
            drawVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::Toon);
        const std::span<const uint8_t> toonBytes = effectBlock
            ? std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&*effectBlock), sizeof(*effectBlock))
            : std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&toonUniforms), sizeof(toonUniforms));
        const bool uniformSurface = Extension::HasPicaShaderInstrumentationFeature(
            drawVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::NativePerPixelLighting);
        const bool drawBalanceApplied=fragmentBalance && Extension::HasPicaShaderInstrumentationFeature(
            drawVariant.AppliedFeatures,Extension::PicaShaderInstrumentationFeature::FragmentLightingBalance);
        const bool uniformPbr = Extension::HasPicaShaderInstrumentationFeature(
            drawVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::PbrMaterial);
        const bool uniformShadow = Extension::HasPicaShaderInstrumentationFeature(
            drawVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::DirectionalShadowLighting);
        // Without a dissolve the receiver never reads the older layout: its
        // range ends before it.
        const VkDeviceSize directionalShadowUniformSize = uniformShadow
            ? directionalShadowUniforms.UsedBytes() : draw.FragmentUniformBytes.size();
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
        if (surfaceCandidate && surfaceDebug.ShadowSelected(surfaceOrdinal)) {
            const bool shadowBlob = uniformShadow && surfaceDebug.ShadowBlob(
                mFrameCounter, draw.SubmissionId, "b12", &directionalShadowUniforms,
                static_cast<size_t>(directionalShadowUniformSize));
            const bool surfaceBlob = uniformSurface && surfaceDebug.ShadowBlob(
                mFrameCounter, draw.SubmissionId, "surface", &*surfaceLighting, sizeof(*surfaceLighting));
            const bool vertexBlob = surfaceDebug.ShadowBlob(mFrameCounter, draw.SubmissionId,
                "vertex", draw.VertexUniformBytes.data(), draw.VertexUniformBytes.size());
            const bool sourceBlob = surfaceDebug.ShadowBlob(mFrameCounter, draw.SubmissionId, "fragment.glsl",
                selectedDraw->FragmentShaderSource.data(), selectedDraw->FragmentShaderSource.size());
            const bool nativeFragmentBlob=surfaceDebug.ShadowBlob(mFrameCounter,draw.SubmissionId,
                "native-fragment",draw.FragmentUniformBytes.data(),draw.FragmentUniformBytes.size());
            const bool balanceBlob=fragmentBalance && surfaceDebug.ShadowBlob(mFrameCounter,draw.SubmissionId,
                "balance",&*fragmentBalance,sizeof(*fragmentBalance));
            char balanceFields[192];
            std::snprintf(balanceFields,sizeof(balanceFields),
                ",\"native_fragment_saved\":%u,\"balance_saved\":%u,\"balance_applied\":%u",
                nativeFragmentBlob?1U:0U,balanceBlob?1U:0U,
                Extension::HasPicaShaderInstrumentationFeature(drawVariant.AppliedFeatures,
                    Extension::PicaShaderInstrumentationFeature::FragmentLightingBalance)?1U:0U);
            surfaceDebug.ShadowRecord("fragment_balance",mFrameCounter,draw.SubmissionId,balanceFields);
            const auto prepared = mPreparedShadowCasters.find(draw.SubmissionId);
            char fields[1536];
            std::snprintf(fields, sizeof(fields),
                ",\"ordinal\":%u,\"geometry\":\"%016llx\",\"target_namespace\":%llu,\"target_color\":%u"
                ",\"receiver\":%u,\"shader_applied\":%u,\"class\":%u,\"history_valid\":%u"
                ",\"history_frame\":%llu,\"bound\":%u,\"flags\":%u,\"slot_mask\":%u"
                ",\"prepared_caster\":%u,\"debug_code\":%u,\"debug_applied\":%u"
                ",\"requested_features\":%u,\"applied_features\":%u,\"fragment_key\":\"%016llx\""
                ",\"b12_bytes\":%llu,\"b12_saved\":%u,\"surface_saved\":%u,\"vertex_saved\":%u"
                ",\"source_saved\":%u,\"uses_uber\":%u",
                surfaceOrdinal, static_cast<unsigned long long>(draw.GeometryIdentity),
                static_cast<unsigned long long>(draw.RenderTargetNamespace), draw.FramebufferColorPhysicalAddress,
                directionalShadowReceiver ? 1U : 0U, directionalShadowShaderApplied ? 1U : 0U,
                static_cast<unsigned>(directionalShadowReceiverClass), directionalShadowHistory.Valid() ? 1U : 0U,
                static_cast<unsigned long long>(directionalShadowHistory.State.ProducedFrameId),
                directionalShadowHistoryBound ? 1U : 0U, directionalShadowUniforms.Extent[3],
                static_cast<unsigned>(directionalShadowUniforms.Receiver[3]),
                prepared != mPreparedShadowCasters.end() ? 1U : 0U, surfaceDebug.ShadowCode(surfaceOrdinal),
                drawVariant.Decisions.ShadowProbe ? 1U : 0U,
                static_cast<unsigned>(drawVariant.RequestedFeatures), static_cast<unsigned>(drawVariant.AppliedFeatures),
                static_cast<unsigned long long>(drawVariant.FragmentShaderKey),
                uniformShadow ? static_cast<unsigned long long>(directionalShadowUniformSize) : 0ULL,
                shadowBlob ? 1U : 0U, surfaceBlob ? 1U : 0U, vertexBlob ? 1U : 0U,
                sourceBlob ? 1U : 0U, effectBlock.has_value() ? 1U : 0U);
            surfaceDebug.ShadowRecord("shadow_receiver", mFrameCounter, draw.SubmissionId, fields);
        }
#endif
        const VkDeviceSize surfacePayloadSize =
            Renderer3ds::PicaSurfaceUniformUploadSize(uniformSurface, drawBalanceApplied);
        const std::array<VkDeviceSize, 7> uniformSizes{
            draw.VertexUniformBytes.size(), previousVertexUniformBytes.size(),
            draw.FragmentUniformBytes.size(), uniformShadow ? directionalShadowUniformSize : 0U,
            uniformToon ? toonBytes.size() : 0U,
            surfacePayloadSize,
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
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
        const VkDeviceSize surfaceUploadStart = frame.UniformBytesUsed;
#endif
        if (uniformSurface && drawBalanceApplied) {
            const Renderer3ds::PicaBalancedSurfaceUniforms packed{*surfaceLighting, *fragmentBalance};
            surfaceUniformOffset = frame.BalancedSurfaceUniforms.Resolve(packed, uploadUniform);
        } else if (uniformSurface) {
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
        const VkDeviceSize surfaceUniformSize = uniformSurface ? surfacePayloadSize : directionalShadowUniformSize;
#if defined(TRIAEVUM_RENDERER_DEVELOPER_TOOLS)
        if (uniformSurface && surfaceDebug.ShadowSelected(surfaceOrdinal)) {
            char fields[256];
            std::snprintf(fields, sizeof(fields),
                ",\"balanced\":%u,\"payload_bytes\":%llu,\"reused\":%u,\"offset\":%llu",
                drawBalanceApplied ? 1U : 0U, static_cast<unsigned long long>(surfacePayloadSize),
                frame.UniformBytesUsed == surfaceUploadStart ? 1U : 0U,
                static_cast<unsigned long long>(surfaceUniformOffset));
            surfaceDebug.ShadowRecord("surface_upload", mFrameCounter, draw.SubmissionId, fields);
        }
#endif
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
        nriDrawOwnership = Extension::FinalizePicaNriDrawOwnership(
            nriDrawOwnership,
            mPicaDynamicRenderingScope.ActiveOwnedByNri());
        nriOwnedDraw = nriDrawOwnership.SubmitOwned;
        if (!nriOwnedDraw &&
            !Extension::PicaNriVulkanFallbackAllowed(
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
                Extension::kPicaPackedVertexFloatOffset) {
                uint32_t booleanUniforms = 0;
                std::memcpy(&booleanUniforms,
                            draw.VertexUniformBytes.data() +
                                Extension::kPicaPackedVertexBooleanMaskOffset,
                            sizeof(booleanUniforms));
                frameContent.GeometryCoversViewport =
                    Renderer3ds::PicaDrawGeometryCoversViewport(
                        draw, booleanUniforms,
                        draw.VertexUniformBytes.subspan(
                            Extension::kPicaPackedVertexFloatOffset));
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
        // A title's interface frame longer than its screen (UiFrameExtensionY,
        // which the title sets on draws whose viewport is the screen) holds
        // placed UI: the canvas stretches over the frame and the draw's clip
        // Y narrows to match, so the screen keeps its proportions at the
        // frame's centre. Screen content keeps its own canvas.
        const float uiFrameExtensionY =
            compositionDomain == Renderer3ds::PicaCompositionDomain::Ui &&
                    draw.UiFrameExtensionY > 1.0F
                ? draw.UiFrameExtensionY
                : 1.0F;
        const auto rasterCanvas = ::Oot3d::Renderer::ResolvePicaRasterCanvas(
            compositionDomain, frameContentResolution.Content,
            draw.FramebufferWidth, draw.FramebufferHeight, renderTarget.Width,
            renderTarget.Height,
            Extension::NativeTopScreenStorageSamplingX(
                {draw.FramebufferWidth, draw.FramebufferHeight},
                NativeTopScreenOutputExtent()),
            uiFrameExtensionY);
        if (uiFrameExtensionY > 1.0F) {
            const std::array<float, 2> clipScaleDelta{0.0F, 1.0F / uiFrameExtensionY - 1.0F};
            for (const VkDeviceSize offset : {vertexUniformOffset, previousVertexUniformOffset}) {
                std::memcpy(static_cast<uint8_t*>(frame.UniformBuffer.Mapped) + offset +
                                Extension::kPicaPackedVertexClipScaleDeltaOffset,
                            clipScaleDelta.data(), sizeof(clipScaleDelta));
            }
        }
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
            // A scissor of the whole viewport (the screen) spans the whole
            // frame; a smaller one bounds content, which the narrowed clip
            // space keeps at the screen's proportions around its centre.
            const bool wholeScreenScissor =
                static_cast<float>(x1) <= draw.ViewportX && static_cast<float>(y1) <= draw.ViewportY &&
                static_cast<float>(x2) >= draw.ViewportX + draw.ViewportWidth &&
                static_cast<float>(y2) >= draw.ViewportY + draw.ViewportHeight;
            const auto scissorCanvas =
                wholeScreenScissor
                    ? rasterCanvas
                    : ::Oot3d::Renderer::PicaUiFrameContentCanvas(
                          rasterCanvas, draw.ViewportY + draw.ViewportHeight * 0.5F, uiFrameExtensionY);
            mScissor = {
                {static_cast<int32_t>(scissorCanvas.X + static_cast<float>(x1) *
                                      scissorCanvas.ScaleX),
                 static_cast<int32_t>(scissorCanvas.Y + static_cast<float>(y1) *
                                      scissorCanvas.ScaleY)},
                {static_cast<uint32_t>(static_cast<float>(
                     x2 > x1 ? x2 - x1 : 0U) * scissorCanvas.ScaleX),
                 static_cast<uint32_t>(static_cast<float>(
                     y2 > y1 ? y2 - y1 : 0U) * scissorCanvas.ScaleY)}};
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
            // An overlay (native UI, the isolated title logo) consumes the
            // completed scene; its pixels must neither invalidate that
            // completion nor become inputs to scene effects.
            const bool overlayDraw =
                draw.CompositionDomain == Renderer3ds::PicaCompositionDomain::Ui ||
                sceneOverlayDraw;
            const bool retainResolvedScene = overlayDraw &&
                renderTarget.SceneEffectsResolvedSerial != 0U &&
                renderTarget.SceneEffectsResolvedSerial == renderTarget.ContentWriteSerial;
            const bool retainSceneBeforeUi = overlayDraw &&
                renderTarget.SceneBeforeUiSerial != 0U &&
                renderTarget.SceneBeforeUiSerial == renderTarget.ContentWriteSerial;
            const bool excludePresentationFxaa = overlayDraw &&
                (draw.Composition.ExcludeFromPresentationFxaa ||
                 (renderTarget.PresentationFxaaExcludedSerial != 0U &&
                  renderTarget.PresentationFxaaExcludedSerial == renderTarget.ContentWriteSerial));
            renderTarget.ContentWriteSerial = ++mGpuTextureCopyWriteSerial;
            if (excludePresentationFxaa)
                renderTarget.PresentationFxaaExcludedSerial = renderTarget.ContentWriteSerial;
            if (retainResolvedScene)
                renderTarget.SceneEffectsResolvedSerial = renderTarget.ContentWriteSerial;
            if (retainSceneBeforeUi)
                renderTarget.SceneBeforeUiSerial = renderTarget.ContentWriteSerial;
            const bool fullTargetRaster =
                draw.ScissorMode != 1U && compositionDomain == ::Fast::Renderer3ds::PicaCompositionDomain::Screen &&
                mViewport.x <= 0.0F && mViewport.y <= 0.0F &&
                mViewport.x + static_cast<float>(mViewport.width) >= static_cast<float>(renderTarget.Width) &&
                mViewport.y + static_cast<float>(mViewport.height) >= static_cast<float>(renderTarget.Height) &&
                mScissor.offset.x <= 0 && mScissor.offset.y <= 0 &&
                static_cast<uint32_t>(mScissor.offset.x) + mScissor.extent.width >= renderTarget.Width &&
                static_cast<uint32_t>(mScissor.offset.y) + mScissor.extent.height >= renderTarget.Height;
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
            static_cast<float>(Renderer3ds::ResolvePicaMotionProvenance(false,
                rigidMotion.Valid, temporalEligible && draw.TemporalCameraOnly)),
            exactMotionHistory ? 1.0F : 0.0F};
        nativeDrawPush.Jitter = {
            2.0F * jitterUv[0], 2.0F * jitterUv[1],
            previousJitterNdc[0], previousJitterNdc[1]};
        if (temporalEligible) {
            mDiagnostics.RecordTemporalCorrespondence(static_cast<uint32_t>(temporalMatch.Reason),
                static_cast<uint32_t>(motionProvenance));
            mDiagnostics.RecordTemporalDraw(
                rigidMotion.Valid, exactMotionHistory,
                exactMotionHistory && !rigidEligible, repeatedTemporalIdentity);
        }

        Extension::NriPicaOwnedDrawDesc nriDraw;
        Extension::PicaNriVertexInputLayout sceneVertexLayout;
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
            const auto bindTexture = [anisotropy = mTextureAnisotropy](Extension::NriPicaTextureBindingDesc& destination,
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
                // Graphics.Textures.Anisotropy, as RecreateSampler applies it.
                destination.Anisotropy = !integer && destination.MinFilter == VK_FILTER_LINEAR
                    ? static_cast<uint8_t>(anisotropy) : uint8_t{1};
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
                // The contact shadows' visibility where this sample has it.
                const bool contactBound = directionalShadowHistoryBound && directionalShadowHistory.ContactValid();
                auto& destination = nriDraw.ContactShadowTexture;
                destination.Image = contactBound ? directionalShadowHistory.ContactImage : mFallbackTexture.NriImage;
                destination.Format = contactBound ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
                destination.Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
                destination.Width = contactBound ? directionalShadowHistory.ContactWidth : mFallbackTexture.Width;
                destination.Height = contactBound ? directionalShadowHistory.ContactHeight : mFallbackTexture.Height;
                destination.MipLevels = 1U;
                destination.MinFilter = VK_FILTER_LINEAR;
                destination.MagFilter = VK_FILTER_LINEAR;
                destination.MipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                destination.AddressU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                destination.AddressV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                destination.MinLod = 0.0F;
                destination.MaxLod = 0.0F;
                destination.Integer = false;
            }
            {
                // The cube atlas only where the variant reads it.
                const bool pointShadowBound = pointShadowHistory.Valid() &&
                    Extension::HasPicaShaderInstrumentationFeature(
                        drawVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::PointShadowLighting);
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
            // The whole scene carries the projection jitter, as a temporal
            // upscaler takes every input texel to be jittered. A scene draw
            // whose vertex program does not add it (no temporal variant: the
            // sky, backdrops and effects drawn without depth) moves by the
            // same sub-pixel step through its viewport.
            if (sceneDraw && (jitterUv[0] != 0.0F || jitterUv[1] != 0.0F) &&
                !Extension::HasPicaShaderInstrumentationFeature(
                    drawVariant.AppliedFeatures, Extension::PicaShaderInstrumentationFeature::TemporalVertex)) {
                nriDraw.Viewport.x += jitterUv[0] * nriDraw.Viewport.width;
                nriDraw.Viewport.y += jitterUv[1] * nriDraw.Viewport.height;
            }
            nriDraw.Scissor = mScissor;
            std::copy(std::begin(draw.Blend.ConstantColor),
                      std::end(draw.Blend.ConstantColor),
                      nriDraw.BlendConstants.begin());
            nriDraw.StencilTest = draw.Stencil.Enabled;
            nriDraw.StencilReference = draw.Stencil.Reference;

            std::vector<Extension::PicaNriSourceVertexBinding> shadowSourceBindings;
            shadowSourceBindings.reserve(draw.VertexBindings.size());
            for (const auto& binding : draw.VertexBindings) {
                shadowSourceBindings.push_back({ binding.Binding, binding.ByteStride, binding.PerInstance });
            }
            sceneVertexLayout =
                Extension::BuildPicaNriVertexInputLayout(shadowSourceBindings, nriDrawSourceAttributes);
        }
        // An isolated draw (the title logo) is an object of its own: it casts
        // no shadow into the scene and is no scene distance field geometry.
        const bool directionalShadowCaster =
            nriOwnedDraw && opaqueWorldDraw && !isolatedLighting && sceneVertexLayout.Valid() &&
            !sceneVertexLayout.Bindings.empty() &&
            !sceneVertexLayout.Attributes.empty() &&
            mDirectionalShadowMapBarrierPlan.Valid() &&
            Extension::PicaExtensionPassEnabled(
                mPicaExtensionGraph,
                Extension::PicaExtensionPass::DirectionalShadowLighting) &&
            AdmitsOpaqueSceneGeometry(draw);
        if (nriOwnedDraw && !nativeMemoryOnly) {
            std::vector<Extension::PicaSceneVertexBufferBinding>
                sceneVertexBindings;
            sceneVertexBindings.reserve(nriVertexBindings.size());
            for (const auto& binding : nriVertexBindings) {
                sceneVertexBindings.push_back(
                    { binding.Binding, binding.Offset, binding.Size,
                      binding.Stride });
            }
            Renderer3ds::VirtualShadowCaster shadowCaster;
            Extension::DirectionalShadowMatrix shadowViewToWorld;
            const auto prepared = mPreparedShadowCasters.find(draw.SubmissionId);
            if (directionalShadowCaster && prepared != mPreparedShadowCasters.end()) {
                // Prepared from the same inputs for this sample's map.
                shadowCaster = prepared->second;
                mDiagnostics.RecordNriDirectionalShadowPreparedReuse();
            } else if (directionalShadowCaster && draw.LightBinding.WorldToView &&
                draw.VertexUniformBytes.size() >= Extension::kPicaPackedVertexFloatOffset &&
                Extension::InvertDirectionalShadowMatrix(*draw.LightBinding.WorldToView, shadowViewToWorld)) {
                uint32_t booleanUniforms = 0;
                std::memcpy(&booleanUniforms, draw.VertexUniformBytes.data() +
                            Extension::kPicaPackedVertexBooleanMaskOffset, sizeof(booleanUniforms));
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
                    draw.VertexUniformBytes.subspan(Extension::kPicaPackedVertexFloatOffset),
                    shadowViewToWorld, version, shadowCache,
                    geometryResolution.Cacheable
                        ? ShadowCasterAnchor(geometry.Identity, draw.Composition.TemporalIdentity,
                                             draw.SubmissionId)
                        : nullptr);
            }
            shadowCaster.Dynamic =
                draw.Composition.NativeActorGeometry && !draw.Composition.StaticShadowCacheEligible;
            shadowCaster.OwnerIdentity = draw.Composition.NativeActorIdentity;
            if (opaqueWorldDraw && draw.LightBinding.WorldToView)
                renderTarget.SampleView.Observe(mFrameCounter, *draw.LightBinding.WorldToView);
            if (draw.LightBinding.Water && draw.CompositionDomain == Renderer3ds::PicaCompositionDomain::Scene) {
                renderTarget.WaterBodies = draw.LightBinding.Water;
                renderTarget.WaterBodiesFrame = mFrameCounter;
                if (draw.LightBinding.Water->Count != 0U) mReflectableWaterFrame = mFrameCounter;
            }
            if (waterSurface.Pieces && draw.CompositionDomain == Renderer3ds::PicaCompositionDomain::Scene) {
                if (renderTarget.WaterPiecesFrame != mFrameCounter) {
                    renderTarget.WaterPieces.clear();
                    renderTarget.WaterPieceKeys.clear();
                    renderTarget.WaterPiecesFrame = mFrameCounter;
                }
                if (std::find(renderTarget.WaterPieceKeys.begin(), renderTarget.WaterPieceKeys.end(),
                              waterSurface.PiecesKey) == renderTarget.WaterPieceKeys.end()) {
                    renderTarget.WaterPieceKeys.push_back(waterSurface.PiecesKey);
                    renderTarget.WaterPieces.push_back(waterSurface.Pieces);
                }
            }
            if (waterSurface.Plane && draw.CompositionDomain == Renderer3ds::PicaCompositionDomain::Scene) {
                auto& planes = renderTarget.WaterPlanes;
                if (renderTarget.WaterPlanesFrame != mFrameCounter) {
                    planes.Count = 0U;
                    renderTarget.WaterPlanesFrame = mFrameCounter;
                }
                const auto end = planes.Bodies.begin() + planes.Count;
                if (planes.Count < Renderer3ds::kMaximumPicaSceneWaterBodies &&
                    std::find(planes.Bodies.begin(), end, *waterSurface.Plane) == end)
                    planes.Bodies[planes.Count++] = *waterSurface.Plane;
                mReflectableWaterFrame = mFrameCounter;
            }
            if (opaqueWorldDraw)
                if (const auto projection = Extension::DecodePicaPerspectiveProjection(draw.VertexUniformBytes))
                    renderTarget.SampleProjection.Observe(mFrameCounter, *projection);
            // Native frame geometry for the scene distance field: the shadow
            // caster admission, from the first non-synthetic execution.
            const bool sceneDistanceFieldGeometry =
                Extension::PicaExtensionPassEnabled(
                    mPicaExtensionGraph, Extension::PicaExtensionPass::SceneDistanceField) &&
                opaqueWorldDraw && !isolatedLighting && sceneVertexLayout.Valid() &&
                !sceneVertexLayout.Bindings.empty() &&
                AdmitsOpaqueSceneGeometry(draw) && draw.LightBinding.WorldToView &&
                draw.VertexUniformBytes.size() >= Extension::kPicaPackedVertexFloatOffset;
            if (sceneDistanceFieldGeometry) {
                if (!mBrixelizerScene.Available() && !mBrixelizerInitializationAttempted) {
                    mBrixelizerInitializationAttempted = true;
                    if (!mBrixelizerScene.Initialize(mPhysicalDevice, mDevice, kFramesInFlight))
                        std::fprintf(stderr, "TRIAEVUM_SCENE_DISTANCE_FIELD unavailable=\"%s\"\n",
                                     mBrixelizerScene.UnavailableReason().c_str());
                }
                const auto& sample = mPicaSceneFrame.TemporalSample();
                Extension::DirectionalShadowMatrix giViewToWorld;
                if (!sample.Synthetic &&
                    mBrixelizerScene.AcceptsExecution(sample.CurrentSourceFrameId, draw.RenderTargetNamespace) &&
                    Extension::InvertDirectionalShadowMatrix(*draw.LightBinding.WorldToView, giViewToWorld)) {
                    uint32_t booleanUniforms = 0;
                    std::memcpy(&booleanUniforms, draw.VertexUniformBytes.data() +
                                Extension::kPicaPackedVertexBooleanMaskOffset, sizeof(booleanUniforms));
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
                        draw.VertexUniformBytes.subspan(Extension::kPicaPackedVertexFloatOffset),
                        giViewToWorld, giVersion, giCache);
                    mBrixelizerScene.Submit(giInstance);
                }
            }
            std::array<Extension::PicaSceneTextureBinding, 3>
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
                    ? Extension::PicaSceneTextureImageFormat::R32Uint
                    : Extension::PicaSceneTextureImageFormat::Rgba8;
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
            // A surface the title follows (lights that are its glow): how
            // strongly this draw shows it.
            auto& surfaceEmissions = Renderer3ds::PicaSurfaceEmissionRegistry::Instance();
            std::optional<float> surfaceEmission;
            for (const auto& texture : sceneTextures) {
                if (!texture.Bound || !texture.NativeBaseLevelContentHashAvailable ||
                    !surfaceEmissions.Follows(texture.NativeBaseLevelContentHash))
                    continue;
                if (!surfaceEmission) {
                    Renderer3ds::PicaTevProgram tev{};
                    std::array<std::array<float, 4>, 6> constants{};
                    std::array<float, 4> bufferColor{};
                    const auto& bytes = draw.FragmentUniformBytes;
                    if (bytes.size() >= Extension::kPicaPackedFragmentTevProgramOffset + sizeof(tev) &&
                        bytes.size() >= Extension::kPicaPackedFragmentCombinerBufferOffset + sizeof(bufferColor)) {
                        std::memcpy(&tev, bytes.data() + Extension::kPicaPackedFragmentTevProgramOffset, sizeof(tev));
                        std::memcpy(constants.data(), bytes.data() + Extension::kPicaPackedFragmentTevConstantsOffset,
                                    sizeof(constants));
                        std::memcpy(bufferColor.data(), bytes.data() + Extension::kPicaPackedFragmentCombinerBufferOffset,
                                    sizeof(bufferColor));
                        surfaceEmission = Renderer3ds::PicaUnitSurfaceEmission(tev, constants, bufferColor, draw.Blend);
                    } else {
                        surfaceEmission = 1.0F;
                    }
                }
                surfaceEmissions.Observe(texture.NativeBaseLevelContentHash, *surfaceEmission);
            }
            Extension::PicaSceneTextureBinding orderedLightingLut;
            if (lightingLutInteger) {
                orderedLightingLut.NativeImageHandle = ToPicaSceneNativeHandle(lightingLutTexture->NriImage);
                orderedLightingLut.ImageWidth = lightingLutTexture->Width;
                orderedLightingLut.ImageHeight = lightingLutTexture->Height;
                orderedLightingLut.MipLevels = 1U;
                orderedLightingLut.ImageFormat = Extension::PicaSceneTextureImageFormat::R32Uint;
                orderedLightingLut.Bound = true;
            }
            const Extension::PicaSceneCullMode sceneCull = ToPicaSceneCullMode(draw);
            const Extension::PicaSceneFrontFace sceneFront = ToPicaSceneFrontFace(draw);
            mPicaSceneFrame.Record({
                .SubmissionId = draw.SubmissionId,
                .CommandListAddress = draw.CommandListAddress,
                .CommandListOffsetWords = draw.CommandListOffsetWords,
                .CompositionDomain = compositionDomain,
                .Composition = draw.Composition,
                .LightingWorldToView = draw.LightBinding.WorldToView,
                .SceneDirectionals = draw.LightBinding.SceneDirectionals,
                .ScenePoints = draw.LightBinding.ScenePoints,
                .RoomShadows = draw.LightBinding.RoomShadows,
                .NativeLightSources = draw.LightBinding.Sources,
                .ShadowCaster = shadowCaster,
                .Raster = {
                    .SchemaVersion =
                        Extension::kPicaSceneResolvedRasterStateSchemaVersion,
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
                        Extension::kPicaSceneRenderTargetStateSchemaVersion,
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
                        Extension::kPicaSceneResolvedMaterialStateSchemaVersion,
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
                selectedDraw != &uberDraw, true, !nriOwnedDraw,
                selectedDraw == &uberDraw && nriOwnedDraw
                    ? NativePicaPipelineAcquisition::Libraries
                    : NativePicaPipelineAcquisition::Blocking)
            : nullptr;
        // Exclude scissor (mode 1) rasterizes the target minus a rectangle.
        // Vulkan takes one scissor per draw: partition the visible region into
        // disjoint rectangles and submit the same draw once per rectangle, so
        // blending and stencil still execute exactly once per fragment.
        const VkRect2D originalScissor = mScissor;
        Renderer3ds::PicaScissorParts scissorParts;
        scissorParts.Rects[0] = { mScissor.offset.x, mScissor.offset.y, mScissor.extent.width, mScissor.extent.height };
        scissorParts.Count = (mScissor.extent.width != 0U && mScissor.extent.height != 0U) ? 1U : 0U;
        if (draw.ScissorMode == 1U) {
            const uint32_t x2 = uint32_t{ draw.ScissorX2 } + 1U;
            const uint32_t y2 = uint32_t{ draw.ScissorY2 } + 1U;
            const Renderer3ds::PicaScissorRect hole{
                static_cast<int32_t>(rasterCanvas.X + draw.ScissorX1 * framebufferScaleX),
                static_cast<int32_t>(rasterCanvas.Y + draw.ScissorY1 * framebufferScaleY),
                static_cast<uint32_t>((x2 > draw.ScissorX1 ? x2 - draw.ScissorX1 : 0U) * framebufferScaleX),
                static_cast<uint32_t>((y2 > draw.ScissorY1 ? y2 - draw.ScissorY1 : 0U) * framebufferScaleY)
            };
            scissorParts = Renderer3ds::ExcludePicaScissor(scissorParts.Rects[0], hole);
        }
        bool nriOwnedDrawBound = false;
        bool nriPipelineBound = false;
        for (uint32_t scissorPart = 0; scissorPart < scissorParts.Count; ++scissorPart) {
            const auto part = scissorParts.Rects[scissorPart];
            mScissor = { { part.X, part.Y }, { part.Width, part.Height } };
            nriDraw.Scissor = mScissor;
            // Coverage runs before the native draw: stencil must still have its
            // original value if that draw modifies it. The auxiliary pass is read-only.
            nriDraw.PipelineId = coveragePipeline ? coveragePipeline->Id : pipelineRecord.Id;
            nriOwnedDrawBound = nriOwnedDraw && mNriPicaPipelineBridge.BindOwnedDraw(nriDraw);
            if (nriOwnedDraw && !nriOwnedDrawBound) {
                throw std::runtime_error("NRI PICA draw binding failed in an NRI-owned "
                                         "rendering scope");
            }
            if (nriOwnedDrawBound && coveragePipeline != nullptr) {
                nriDraw.PipelineId = pipelineRecord.Id;
                if (!mNriPicaPipelineBridge.DrawBoundGeometry(nriDraw))
                    throw std::runtime_error("NRI native draw after outline coverage failed");
            }
            nriPipelineBound = nriOwnedDrawBound;
            if (!nriOwnedDrawBound) {
                pipeline = GetOrCreateNativePicaPipeline(*selectedDraw, *selectedProgram, drawVariant.Reactive,
                                                         drawVariant.Domain, drawVariant.RequestedFeatures,
                                                         drawVariant.AppliedFeatures, false, false, true)
                               .Vulkan;
                nriPipelineBound = mNriPicaPipelineBridge.Bind(mCurrentFrame, pipeline);
                if (!nriPipelineBound)
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                for (const auto& [binding, offset] : bindingOffsets) {
                    vkCmdBindVertexBuffers(commandBuffer, binding, 1, &geometryBuffer, &offset);
                }
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mNativePicaPipelineLayout, 0, 1,
                                        &descriptor, 0, nullptr);
                vkCmdSetViewport(commandBuffer, 0, 1, &mViewport);
                vkCmdSetScissor(commandBuffer, 0, 1, &mScissor);
                vkCmdSetBlendConstants(commandBuffer, draw.Blend.ConstantColor);
                vkCmdPushConstants(commandBuffer, mNativePicaPipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(nativeDrawPush),
                                   &nativeDrawPush);
                if (draw.Indexed) {
                    vkCmdBindIndexBuffer(commandBuffer, geometryBuffer, indexOffset, VK_INDEX_TYPE_UINT16);
                    if (coveragePipeline != nullptr) {
                        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, coveragePipeline->Vulkan);
                        vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, draw.BaseVertex, 0);
                        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    }
                    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, draw.BaseVertex, 0);
                } else {
                    if (coveragePipeline != nullptr) {
                        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, coveragePipeline->Vulkan);
                        vkCmdDraw(commandBuffer, draw.VertexCount, 1, 0, 0);
                        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    }
                    vkCmdDraw(commandBuffer, draw.VertexCount, 1, 0, 0);
                }
            }
        }
        mScissor = originalScissor;
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
            Extension::ResolvePicaAlphaCoveragePolicy(
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

void GfxRenderingAPIVulkan::TryRenderWorldDebugLines(NativePicaRenderTarget& target) {
    const auto lights = Extension::WorldDebugLineRuntime::Instance().Latest();
    auto combined = Extension::GrassSceneBridge::Instance().MaskHighlight(
        { mFrameCounter, target.Key.RenderTargetNamespace, target.Key.ColorPhysicalAddress });
    if (lights) {
        combined.WidthPixels = lights->WidthPixels;
        combined.HiddenOpacity = lights->HiddenOpacity;
        combined.Lines.insert(combined.Lines.end(), lights->Lines.begin(), lights->Lines.end());
    }
    if (combined.Lines.empty())
        return;
    const auto* lines = &combined;
    // The projection the target's draws applied, so the lines meet its depth.
    const auto perspective = SamplePerspective(&target);
    if (!perspective.has_value() || !perspective->CameraAvailable) return;
    const std::array<float, 2> jitterNdc = mTemporalJitterEnabled
        ? std::array<float, 2>{ 2.0F * mTemporalJitterPixels[0] / static_cast<float>(target.Width),
                                2.0F * mTemporalJitterPixels[1] / static_cast<float>(target.Height) }
        : std::array<float, 2>{};
    BeginNativePicaRenderPass(target);
    const bool drawn = mWorldDebugLinePass.Draw(
        mCommandBuffers[mCurrentFrame], mCurrentFrame, mFrameCounter, target.Width, target.Height, *lines,
        perspective->WorldToClip, jitterNdc, mFramePicaAttachmentRequirements);
    // The next native draw starts its render pass afresh, with its own state.
    if (drawn) EndNativePicaRenderPass();
    static bool reported = false;
    if (!reported && (drawn || !mWorldDebugLinePass.UnavailableReason().empty())) {
        reported = true;
        std::fprintf(stderr, "TRIAEVUM_WORLD_DEBUG_LINES drawn=%d lines=%zu target=%ux%u reason=%s\n", drawn ? 1 : 0,
                     lines->Lines.size(), target.Width, target.Height, mWorldDebugLinePass.UnavailableReason().c_str());
    }
}

bool GfxRenderingAPIVulkan::TryRenderInteractiveGrass(
    NativePicaRenderTarget& target,
    const Extension::GraphicsSettings& settings,
    Extension::EffectGeometryProviderInvocationKind invocationKind) {
    Renderer3ds::SlowStepScope slowStep("interactive_grass");
    const auto latestPerspective = SamplePerspective(&target);
    (void)mNativeSceneView.Publish(
        mFrameCounter, mPicaSceneFrame, latestPerspective);
    (void)mPicaScenePublications.Publish(
        mFrameCounter, mPicaSceneFrame, mNativeSceneView);
    const auto* perspective =
        mPicaScenePublications.ResolvePerspectiveCamera();
    const Extension::EffectGeometryProviderInvocation invocation{
        invocationKind,
        Extension::EffectStage::BeforeTransparent,
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
            Extension::EffectGeometryProviderExecutionOutcome::Skipped);
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
                ? Extension::EffectGeometryProviderExecutionOutcome::Reused
                : Extension::EffectGeometryProviderExecutionOutcome::Skipped);
        return reused;
    }
    EndNativePicaRenderPass();
    mGpuProfiler.BeginScope(
        mCurrentFrame, Extension::GpuProfileScope::Grass,
        mCommandBuffers[mCurrentFrame]);
    Extension::NriDirectionalShadowHistorySnapshot grassShadowHistory;
    if (settings.Grass.Appearance.ReceiveLighting &&
        mInteractiveGrassProviderPlan.Requires(Extension::EffectResource::DirectionalShadowHistory)) {
        // Resolve atlas and page-table version together at the declared native
        // insertion anchor, just like other receivers at this boundary.
        grassShadowHistory = mNriDirectionalShadowPass.FindLatestHistory(
            target.Key.RenderTargetNamespace, target.Key.ColorPhysicalAddress, mFrameCounter);
    }
    const bool prepared = mInteractiveGrassPass.Prepare(
        mCommandBuffers[mCurrentFrame], target.Width, target.Height, *perspective, settings.Grass,
        Extension::PackToonSurfaceParameters(settings.Effects.Toon, settings.Effects.ToonStyle, false),
        mPicaSceneFrame.ResolvedDrawStream(), mFrameCounter, target.Key.RenderTargetNamespace,
        target.Key.ColorPhysicalAddress, mCurrentFrame, mInteractiveGrassProviderPlan, grassShadowHistory,
        mTemporalJitterPixels, mTemporalJitterEnabled, mTemporalMotionEnabled,
        Extension::GrassStageProfileEnabled() ? &mGpuProfiler : nullptr);
    bool executed = false;
    if (prepared) {
        BeginNativePicaRenderPass(target);
        mGpuProfiler.Mark(mCurrentFrame, "grass_draw", mCommandBuffers[mCurrentFrame]);
        Extension::GrassStageTimer drawTimer(Extension::GrassStage::Draw);
        if (Extension::GrassStageProfileEnabled())
            mGpuProfiler.BeginScope(mCurrentFrame, Extension::GpuProfileScope::GrassBlades, mCommandBuffers[mCurrentFrame]);
        executed = mInteractiveGrassPass.DrawPrepared(
            mCommandBuffers[mCurrentFrame],
            target.Width, target.Height,
            mInteractiveGrassProviderPlan,
            mFramePicaAttachmentRequirements);
        if (Extension::GrassStageProfileEnabled())
            mGpuProfiler.EndScope(mCurrentFrame, Extension::GpuProfileScope::GrassBlades, mCommandBuffers[mCurrentFrame]);
    }
    mGpuProfiler.EndScope(
        mCurrentFrame, Extension::GpuProfileScope::Grass,
        mCommandBuffers[mCurrentFrame]);
    mGrassExecutedThisFrame |= executed;
    if (executed) {
        mGrassRenderedTargetsThisFrame.insert(target.Key);
        mDiagnostics.RecordGrassMotion(
            mInteractiveGrassPass.LastMotionBladeCount());
    }
    const auto grassTelemetry =
        Extension::GrassRenderTelemetry::Instance().Snapshot();
    const bool failed =
        grassTelemetry.Status == Extension::GrassRenderStatus::Error ||
        (prepared && !executed);
    mInteractiveGrassProviderExecution.RecordInvocation(
        invocationKind, decision,
        executed
            ? Extension::EffectGeometryProviderExecutionOutcome::Executed
            : failed
                  ? Extension::EffectGeometryProviderExecutionOutcome::Failed
                  : Extension::EffectGeometryProviderExecutionOutcome::Skipped);

    if (std::getenv("OOT3D_GRASS_DIAGNOSTICS") != nullptr ||
        std::getenv("OOT3D_GRAPHICS_GRASS_AUTO") != nullptr) {
        static uint32_t grassSmokeReports = 0U;
        static uint64_t lastGrassSmokeFrame = 0U;
        static auto lastGrassSmokeStatus = Extension::GrassRenderStatus::Disabled;
        const auto telemetry =
            Extension::GrassRenderTelemetry::Instance().Snapshot();
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
                Extension::GrassRenderStatusName(telemetry.Status),
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
    const Extension::GraphicsSettings& settings,
    const Extension::PicaCompositionStageAnchor& anchor) {
    Renderer3ds::SlowStepScope slowStep("directional_shadows");
    const auto boundary = Extension::BuildPicaExtensionScheduleBoundary(
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
        boundary.Stage == Extension::EffectStage::BeforeOpaque);
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
                target.Width,
                target.Height,
                mTemporalJitterEnabled ? mTemporalJitterSequence : 0U,
                [this](const char* label) {
                    mGpuProfiler.Mark(mCurrentFrame, label, mCommandBuffers[mCurrentFrame]);
                },
            },
            mPreparedShadowCasterView,
            mDirectionalShadowMapBarrierPlan);
    // The contact shadows' marks end where the map's own work ends.
    if (executionRequested && settings.Effects.DirectionalShadows.ContactShadows.Enabled)
        mGpuProfiler.Mark(mCurrentFrame, "directional_shadows", mCommandBuffers[mCurrentFrame]);
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
            keyframes.DisplayedDirectionTowardSource, keyframes.LagDegrees, keyframes.ConstructionUnbounded);
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
    const Extension::GraphicsSettings& settings,
    const Extension::PicaCompositionStageAnchor& anchor) {
    Renderer3ds::SlowStepScope slowStep("point_shadows");
    const auto boundary = Extension::BuildPicaExtensionScheduleBoundary(
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
    // Close the segment: the scene draws that follow are not point-map work.
    mGpuProfiler.Mark(mCurrentFrame, "after point_shadows", mCommandBuffers[mCurrentFrame]);
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

// Occurrences of one draw identity are counted per render target, so the
// n-th draw of a surface has the same world instance in every target.
static uint64_t GrassTargetOccurrenceKey(uint64_t drawIdentity, uint64_t renderTargetNamespace,
                                         uint32_t colorPhysicalAddress) noexcept {
    uint64_t key = drawIdentity;
    key ^= renderTargetNamespace + 0x9e3779b97f4a7c15ULL + (key << 6U) + (key >> 2U);
    key ^= uint64_t{colorPhysicalAddress} + 0x9e3779b97f4a7c15ULL + (key << 6U) + (key >> 2U);
    return key;
}

// A first-ever source texture needs its native signature before it can be
// classified (twin rules, overlays): decode it into the source cache once.
template <typename Texture>
static void ObserveNativePicaGrassTexture(const Texture& texture, uint64_t textureHash) {
    if (texture.NativeType != 0 || !texture.CopyReferences.empty() || texture.Width < 16 || texture.Height < 16 ||
        Extension::GrassTextureSourceCache::Instance().Find(textureHash, false))
        return;
    const auto size = NativePicaEncodedMipSize(texture.NativeFormat, texture.Width, texture.Height);
    std::vector<uint8_t> rgba;
    if (size && *size <= texture.NativeBytes.size() &&
        Extension::DecodePicaTextureRgba8(texture.NativeFormat, texture.Width, texture.Height,
                                      texture.NativeBytes.first(*size), rgba, nullptr)) {
        auto& cache = Extension::GrassTextureSourceCache::Instance();
        cache.ObserveDecoded(textureHash, texture.Width, texture.Height, rgba);
        (void)cache.Find(textureHash, true);
    }
}

// The placement audit's identity of a draw's texture 0.
static std::optional<Extension::GrassAuditTexture> DescribeNativePicaGrassTexture(
    const GfxNativePicaDrawView& draw, const Extension::InteractiveGrassSettings& settings) {
    const auto texture = std::find_if(draw.Textures.begin(), draw.Textures.end(),
                                      [](const auto& value) { return value.Slot == 0U; });
    if (texture == draw.Textures.end()) return std::nullopt;
    const auto hash = ResolveNativeTextureBaseLevelContentHash(*texture);
    if (!hash) return std::nullopt;
    ObserveNativePicaGrassTexture(*texture, *hash);
    const uint64_t observed =
        Extension::GrassTextureSourceCache::Instance().ResolveObservedHash(*hash, texture->Width, texture->Height);
    return Extension::GrassAuditTexture{
        observed, texture->Width, texture->Height,
        Extension::GrassTwinRules::Instance().Configured(settings.Rules, observed, texture->Width, texture->Height, 0U)};
}

void AuditNativePicaGrassGate(const GfxNativePicaDrawView& draw, const Extension::InteractiveGrassSettings& settings,
                              const char* gate) {
    if (!Extension::GrassPlacementAuditEnabled() || settings.Rules.empty()) return;
    if (const auto texture = DescribeNativePicaGrassTexture(draw, settings))
        Extension::AuditGrassSource(*texture, gate, draw.VertexCount);
}

bool ResolveNativePicaGrassSurface(const GfxNativePicaDrawView& draw, uint64_t frameId,
                                   std::unordered_map<uint64_t, uint32_t>& occurrences,
                                   const Extension::InteractiveGrassSettings& settings, bool worldSurface,
                                   bool fragmentUsesTexture0, bool perspectiveProjection,
                                   Extension::GrassGeometryRegistry& geometryRegistry, uint64_t sequenceId,
                                   uint64_t drawOrder, bool masksOnly, Extension::GrassSceneMesh& resolved,
                                   bool& cacheable, const char** rejection = nullptr) {
    cacheable = true;
    // The first stage that drops the draw, for the placement audit.
    const auto reject = [rejection](const char* reason) {
        if (rejection) *rejection = reason;
        return false;
    };
    Extension::GrassStageTimer eligibilityTimer(Extension::GrassStage::Eligibility);
    if (settings.Rules.empty() ||
        (masksOnly && (draw.Composition.NativeActorGeometry || draw.Composition.NativeProjectedShadow ||
                       !draw.GeometryIdentityAvailable || draw.Stencil.Enabled))) {
        return reject("no_rules");
    }
    const auto texture = std::find_if(
        draw.Textures.begin(), draw.Textures.end(),
        [](const auto& value) { return value.Slot == 0U; });
    const Extension::GrassSurfaceDrawInfo eligibility{
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
        Extension::IsGrassPrimitiveTopologySupported(
            draw.Topology),
    };
    const bool eligible =
        Extension::IsGrassSurfaceDrawEligible(eligibility);
    static const bool diagnostics =
        std::getenv("OOT3D_GRASS_DIAGNOSTICS") != nullptr;
    const bool maskCandidate = masksOnly && eligibility.ColorOperation && worldSurface && eligibility.HasPosition &&
                               eligibility.DepthTest && !eligibility.DepthCompareAlways &&
                               draw.DepthCompare != GfxNativePicaCompareFunction::Never && perspectiveProjection &&
                               eligibility.WritesRgb && eligibility.SupportedTopology;
    if (!eligible && !maskCandidate && !diagnostics && rejection == nullptr) {
        return false;
    }
    eligibilityTimer.Stop();
    Extension::GrassStageTimer hashTimer(Extension::GrassStage::TextureHash);
    const auto textureHash = texture != draw.Textures.end() ? ResolveNativeTextureBaseLevelContentHash(*texture)
                                                            : std::optional<uint64_t>(0);
    hashTimer.Stop();
    if (!textureHash.has_value()) {
        return reject("texture_hash_unavailable");
    }
    // Source priority includes texture twins. A first-ever eligible texture
    // needs its native signature before it can be classified as an overlay.
    // Keep that signature locally so subsequent scene-entry cache hits require
    // no grass texture decode merely to repeat the source/neither decision.
    Extension::GrassStageTimer ruleEligibilityTimer(Extension::GrassStage::Eligibility);
    if (eligible && texture != draw.Textures.end())
        ObserveNativePicaGrassTexture(*texture, *textureHash);
    const uint64_t observedTextureHash = texture == draw.Textures.end()
                                             ? 0
                                             : Extension::GrassTextureSourceCache::Instance().ResolveObservedHash(
                                                   *textureHash, texture->Width, texture->Height);
    // Its own rule, or the rule of a texture it is the same image as.
    const bool configuredSource = texture != draw.Textures.end() &&
                                  Extension::GrassTwinRules::Instance().Configured(settings.Rules, observedTextureHash,
                                                                               texture->Width, texture->Height, 0U);
    if (diagnostics && texture != draw.Textures.end()) {
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
    if (masksOnly ? (!maskCandidate || (eligible && configuredSource)) : (!eligible || !configuredSource)) {
        if (masksOnly) return false;
        return reject(!eligible ? Extension::GrassSurfaceIneligibility(eligibility) : "unconfigured");
    }
    ruleEligibilityTimer.Stop();
    const auto rejectConfiguredSource = [&](const char* reason) {
        if (rejection) *rejection = reason;
        if (masksOnly && std::getenv("TRIAEVUM_GRASS_MASK_DIAGNOSTICS")) {
            static std::unordered_set<uint64_t> reported;
            const uint64_t key = draw.GeometryIdentity ^ std::hash<std::string_view>{}(reason);
            if (reported.size() < 4096 && reported.insert(key).second)
                std::fprintf(
                    stderr, "TRIAEVUM_GRASS_MASK excluded=%s order=%llu native=%016llx rule_hash=%016llx\n", reason,
                    static_cast<unsigned long long>(drawOrder),
                    static_cast<unsigned long long>(texture == draw.Textures.end() ? 0 : texture->NativeContentHash),
                    static_cast<unsigned long long>(observedTextureHash));
        }
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
    Extension::GrassStageTimer tevTimer(Extension::GrassStage::Tev);
    std::shared_ptr<Extension::GrassOverlayAlpha> overlay;
    Extension::GrassOverlayAlphaInputs alphaInputs;
    if (masksOnly) {
        overlay = std::make_shared<Extension::GrassOverlayAlpha>();
        if (texture != draw.Textures.end()) {
            if (const auto* rule =
                    Extension::FindGrassMaskRule(settings.Masks, observedTextureHash, texture->Width, texture->Height))
                overlay->Rule = *rule;
            overlay->Rule.Coverage.Target = { "", observedTextureHash, texture->Width, texture->Height };
        }
        const auto& blend = draw.Blend;
        overlay->Translucent = UsesTranslucentBlend(blend);
        if (overlay->Translucent && !(blend.EquationRgb == GfxNativeBlendEquation::Add &&
                                      blend.SourceRgb == GfxNativeBlendFactor::SourceAlpha &&
                                      blend.DestRgb == GfxNativeBlendFactor::OneMinusSourceAlpha))
            overlay->NativeSupported = false;
        overlay->AlphaTest = draw.AlphaTestEnabled;
        overlay->Compare = draw.AlphaCompare;
        overlay->Reference = draw.AlphaReference;
        if (overlay->Translucent || overlay->AlphaTest) {
            if (draw.FragmentUniformBytes.size() <
                Extension::kPicaPackedFragmentTevProgramOffset + sizeof(overlay->Program)) {
                overlay->NativeSupported = false;
            } else {
                std::memcpy(&overlay->Program,
                            draw.FragmentUniformBytes.data() + Extension::kPicaPackedFragmentTevProgramOffset,
                            sizeof(overlay->Program));
                std::memcpy(overlay->Constants.data(), draw.FragmentUniformBytes.data(), sizeof(overlay->Constants));
                std::memcpy(overlay->Buffer.data(),
                            draw.FragmentUniformBytes.data() + Extension::kPicaPackedFragmentCombinerBufferOffset,
                            sizeof(overlay->Buffer));
                alphaInputs = Extension::GrassOverlayRequiredAlphaInputs(*overlay);
                overlay->NativeSupported &= alphaInputs.Supported;
            }
        }
    }
    // Even constant-alpha draws retain real UVs: custom rules and the editor's
    // texture-alpha histogram must address the same atlas as the native draw.
    const bool needsTexture = !masksOnly || texture != draw.Textures.end();
    if (needsTexture &&
        (texture == draw.Textures.end() || texture->NativeType != 0 || !texture->CopyReferences.empty()))
        return rejectConfiguredSource("mask requires an immutable 2D texture");
    const auto materialWrapS =
        needsTexture ? DecodeNativePicaGrassWrap(texture->NativeWrapS) : std::optional(Extension::GrassTextureWrap::Repeat);
    const auto materialWrapT =
        needsTexture ? DecodeNativePicaGrassWrap(texture->NativeWrapT) : std::optional(Extension::GrassTextureWrap::Repeat);
    if (!materialWrapS || !materialWrapT)
        return rejectConfiguredSource("unsupported texture wrap");
    const auto* position = FindNativeAttribute(draw, draw.PositionAttributeLocation);
    const auto* uv = needsTexture ? FindNativeAttribute(draw, draw.TexCoord0AttributeLocation) : position;
    if (!position || !uv || position->ComponentCount < 3 || uv->ComponentCount < 2)
        return rejectConfiguredSource("invalid vertex attributes");
    const auto* positionBinding = FindNativeBinding(draw, position->Binding);
    const auto* uvBinding = FindNativeBinding(draw, uv->Binding);
    if (!positionBinding || !uvBinding || positionBinding->PerInstance || uvBinding->PerInstance)
        return rejectConfiguredSource("invalid vertex bindings");
    auto textureCoordinateTransform = needsTexture ? Extension::DecodePicaGrassTextureCoordinateTransform(
                                                         draw.VertexShaderSource, draw.FragmentShaderSource,
                                                         draw.VertexUniformBytes, draw.TexCoord0AttributeLocation,
                                                         &draw.TemporalVertexProgram.Hooks, &draw.FragmentShaderHooks)
                                                   : Extension::PicaGrassTextureCoordinateTransform{};
    if (!needsTexture) {
        textureCoordinateTransform.RawToSample = {};
        textureCoordinateTransform.Eligibility = Extension::PicaGrassTextureCoordinateEligibility::Applied;
    }
    if (!textureCoordinateTransform.Applied())
        return rejectConfiguredSource(
            Extension::PicaGrassTextureCoordinateEligibilityName(textureCoordinateTransform.Eligibility));
    if (overlay) {
        overlay->WrapS = *materialWrapS;
        overlay->WrapT = *materialWrapT;
        overlay->Linear = needsTexture && (texture->MinLinear || texture->MagLinear);
        if (needsTexture) {
            const auto size = NativePicaEncodedMipSize(texture->NativeFormat, texture->Width, texture->Height);
            if (!size || *size > texture->NativeBytes.size())
                return rejectConfiguredSource("missing native texture base level");
            // This immutable snapshot is only decoded on placement/editor work,
            // never on a persistent hit or a later presentation.
            const auto bytes = std::make_shared<const std::vector<uint8_t>>(texture->NativeBytes.begin(),
                                                                            texture->NativeBytes.begin() + *size);
            overlay->LoadTextures = [bytes, hash = *textureHash, format = texture->NativeFormat, width = texture->Width,
                                     height = texture->Height, alphaInputs](auto& alpha) {
                auto& cache = Extension::GrassTextureSourceCache::Instance();
                if (!cache.Contains(hash)) {
                    std::vector<uint8_t> rgba;
                    if (Extension::DecodePicaTextureRgba8(format, width, height, *bytes, rgba, nullptr))
                        cache.ObserveDecoded(hash, width, height, rgba);
                }
                for (size_t c = 0; c < 4; ++c)
                    if ((alphaInputs.TextureChannels & (1U << c)) || c == 3) {
                        alpha.Texture[c] = cache.AcquireMaskShared(hash, static_cast<Extension::GrassSampleChannel>(c));
                        if (!alpha.Texture[c])
                            alpha.NativeSupported = false;
                    }
                if (alpha.Rule.Mode == Extension::GrassMaskMode::Custom)
                    alpha.CustomMask = cache.AcquireMaskShared(hash, alpha.Rule.Coverage.Channel);
            };
        }
    }

    tevTimer.Stop();
    Extension::GrassStageTimer worldTimer(Extension::GrassStage::World);
    cacheable = false; // Transient camera/transform failures must be retried.
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

    Extension::DirectionalShadowMatrix capturedViewToWorld;
    if (!draw.LightBinding.WorldToView ||
        !Extension::InvertDirectionalShadowMatrix(*draw.LightBinding.WorldToView, capturedViewToWorld)) {
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

    worldTimer.Stop();
    cacheable = true;
    Extension::GrassStageTimer geometryTimer(Extension::GrassStage::Geometry);
    Extension::GrassGeometryRequest geometryRequest{
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
    };
    if (overlay && alphaInputs.Primary) {
        const auto primary =
            Extension::DecodeGrassOverlayPrimaryAlpha(draw.TemporalVertexProgram.Hooks, draw.VertexUniformBytes);
        overlay->NativeSupported &= primary.Valid;
        if (primary.Valid && primary.InputRegister != 0xff) {
            const auto* color = FindNativeAttribute(draw, primary.InputRegister);
            const auto* binding = color ? FindNativeBinding(draw, color->Binding) : nullptr;
            if (!color || color->ComponentCount < 4 || !binding || binding->PerInstance) {
                overlay->NativeSupported = false;
            } else {
                const uint16_t scalarSize = color->Format == GfxNativePicaVertexFormat::Float         ? 4
                                            : color->Format == GfxNativePicaVertexFormat::SignedShort ? 2
                                                                                                      : 1;
                geometryRequest.Alpha = { binding->Bytes, binding->ByteStride, color->Format, 1,
                                          uint16_t(color->ByteOffset + 3 * scalarSize) };
            }
        }
        if (primary.Valid)
            geometryRequest.AlphaScale = primary.Scale;
    }
    if (overlay)
        overlay->Version = Extension::GrassOverlayAlphaVersion(*overlay, *textureHash);
    const auto grassGeometryResolution = geometryRegistry.Resolve(geometryRequest);
    if (grassGeometryResolution.Geometry == nullptr) {
        return rejectConfiguredSource("geometry extraction failed");
    }
    geometryTimer.Stop();
    const auto& grassGeometry = *grassGeometryResolution.Geometry;
    if (overlay && std::getenv("TRIAEVUM_GRASS_MASK_DIAGNOSTICS")) {
        static std::unordered_set<uint64_t> reported;
        const auto key = overlay->Version ^ grassGeometry.ContentVersion;
        if (reported.size() < 4096 && reported.insert(key).second) {
            float minAlpha = 1, maxAlpha = 1;
            if (grassGeometry.Alphas && !grassGeometry.Alphas->empty()) {
                const auto [low, high] =
                    std::minmax_element(grassGeometry.Alphas->begin(), grassGeometry.Alphas->end());
                minAlpha = *low;
                maxAlpha = *high;
            }
            std::fprintf(
                stderr,
                "TRIAEVUM_GRASS_MASK program native=%016llx rgba8=%016llx order=%llu supported=%d translucent=%d "
                "alpha_test=%d compare=%u reference=%u primary=[%.9g,%.9g] buffer=[%.9g,%.9g,%.9g,%.9g] control=%08x\n",
                static_cast<unsigned long long>(texture == draw.Textures.end() ? 0 : texture->NativeContentHash),
                static_cast<unsigned long long>(observedTextureHash), static_cast<unsigned long long>(drawOrder),
                overlay->NativeSupported ? 1 : 0, overlay->Translucent ? 1 : 0, overlay->AlphaTest ? 1 : 0,
                unsigned(overlay->Compare), overlay->Reference, minAlpha, maxAlpha, overlay->Buffer[0],
                overlay->Buffer[1], overlay->Buffer[2], overlay->Buffer[3], overlay->Program.Control[0]);
            const auto& uvRows = geometryRequest.TextureCoordinates.RawToSample;
            std::fprintf(stderr,
                         "TRIAEVUM_GRASS_MASK geometry draw_content=%016llx geometry=%016llx alpha_scale=%.9g "
                         "uv=[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]\n",
                         static_cast<unsigned long long>(draw.GeometryContentVersion),
                         static_cast<unsigned long long>(grassGeometry.ContentVersion), geometryRequest.AlphaScale,
                         uvRows[0], uvRows[1], uvRows[2], uvRows[3], uvRows[4], uvRows[5]);
            for (size_t stage = 0; stage < 6; ++stage) {
                const auto& w = overlay->Program.Stages[stage];
                const auto& c = overlay->Constants[stage];
                std::fprintf(
                    stderr,
                    "TRIAEVUM_GRASS_MASK tev stage=%zu words=[%08x,%08x,%08x,%08x] constant=[%.9g,%.9g,%.9g,%.9g]\n",
                    stage, w[0], w[1], w[2], w[3], c[0], c[1], c[2], c[3]);
            }
        }
    }
    Extension::GrassSceneMesh mesh;
    mesh.SubmissionId = draw.SubmissionId;
    mesh.DrawOrder = drawOrder;
    mesh.SequenceId = sequenceId;
    mesh.Overlay = overlay;
    mesh.NativeTextureIdentity = texture == draw.Textures.end() ? 0 : texture->NativeContentHash;
    if (!masksOnly) {
        const auto inherited = Extension::GrassTwinRules::Instance().Inherited(settings.Rules, observedTextureHash,
                                                                           texture->Width, texture->Height);
        const auto rule = std::find_if(settings.Rules.begin(), settings.Rules.end(), [&](const auto& r) {
            return inherited ? r.RuleId == inherited->RuleId : r.Target.Rgba8Hash == observedTextureHash;
        });
        if (rule != settings.Rules.end()) {
            mesh.ConfiguredRuleId = rule->RuleId;
            mesh.ConfiguredRuleHash = rule->Target.Rgba8Hash;
        }
    }
    mesh.CoordinateVersion = Extension::PicaGrassTextureCoordinateTransformVersion(textureCoordinateTransform);
    mesh.Alphas = grassGeometry.Alphas;
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
    if (masksOnly)
        drawIdentity = mesh.GeometryId ^ 0x67726173736d6173ULL;
    const uint32_t occurrence = occurrences[GrassTargetOccurrenceKey(
        drawIdentity, draw.RenderTargetNamespace, draw.FramebufferColorPhysicalAddress)]++;
    mesh.InstanceId = drawIdentity ^
        (static_cast<uint64_t>(occurrence) + 0x9e3779b97f4a7c15ULL +
         (drawIdentity << 6U) + (drawIdentity >> 2U));
    mesh.WorldInstanceId = mesh.InstanceId == 0U ? 1U : mesh.InstanceId;
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
    mesh.TextureWidth = texture == draw.Textures.end() ? 0 : texture->Width;
    mesh.TextureHeight = texture == draw.Textures.end() ? 0 : texture->Height;
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
    mesh.Shading = Extension::DecodeGrassShadingEnvironment(
        draw.VertexUniformBytes, draw.FragmentUniformBytes,
        draw.FragmentFeatures, capturedViewToWorld);
    mesh.Vertices = grassGeometry.Vertices;
    mesh.Indices = grassGeometry.Indices;
    mesh.ObservedTextureHash = observedTextureHash;
    mesh.ObservedTextureResolved = true;
    resolved = std::move(mesh);
    cacheable = true;
    return true;
}

bool PublishNativePicaGrassSurface(const GfxNativePicaDrawView& draw, uint64_t frameId,
                                   std::unordered_map<uint64_t, uint32_t>& occurrences,
                                   const Extension::InteractiveGrassSettings& settings, bool worldSurface,
                                   bool fragmentUsesTexture0, bool perspectiveProjection,
                                   Extension::GrassGeometryRegistry& geometryRegistry, Extension::GrassDrawMemo& memo,
                                   uint64_t sequenceId, uint64_t drawOrder, bool masksOnly) {
    using namespace Extension;
    GrassStageTimer total(masksOnly ? GrassStage::Overlay : GrassStage::Source);
    static const bool bypass = [] {
        const auto* p = std::getenv("TRIAEVUM_GRASS_DRAW_MEMO");
        return p && std::string_view(p) == "0";
    }();
    // The placement audit records each source draw's outcome; masks are not sources.
    const bool audit = !masksOnly && GrassPlacementAuditEnabled() && !settings.Rules.empty();
    const auto auditOutcome = [&](const char* outcome) {
        if (const auto texture = DescribeNativePicaGrassTexture(draw, settings))
            AuditGrassSource(*texture, outcome, draw.VertexCount);
    };
    GrassStageTimer lookup(GrassStage::Memo);
    const auto key =
        BuildGrassDrawMemoKey(draw, memo.Rules(settings, frameId), GrassTextureSourceCache::Instance().Generation(),
                              worldSurface, fragmentUsesTexture0, perspectiveProjection, masksOnly);
    auto* entry = key ? memo.Find(*key, frameId) : nullptr;
    if (entry && GrassStageProfileEnabled())
        ++GrassStageCurrent().SignatureRepeat;
    if (entry && !bypass) {
        if (!entry->Mesh) {
            if (GrassStageProfileEnabled()) {
                ++GrassStageCurrent().MemoHit;
                ++GrassStageCurrent().NeitherHit;
            }
            if (audit) {
                // The memo keeps no reason: resolve again, without publishing
                // or storing, and flag a stored verdict the draw contradicts.
                GrassSceneMesh unused;
                bool cacheable = false;
                const char* rejection = "rejected";
                std::unordered_map<uint64_t, uint32_t> scratchOccurrences;
                static thread_local GrassGeometryRegistry scratchRegistry;
                auditOutcome(ResolveNativePicaGrassSurface(draw, frameId, scratchOccurrences, settings, worldSurface,
                                                           fragmentUsesTexture0, perspectiveProjection,
                                                           scratchRegistry, sequenceId, drawOrder, masksOnly, unused,
                                                           cacheable, &rejection)
                                 ? "memo_stale_neither"
                                 : rejection);
            }
            return false;
        }
        auto mesh = *entry->Mesh;
        if (RefreshGrassMemoDraw(mesh, draw, memo.CameraInverse(draw))) {
            lookup.Stop();
            if (GrassStageProfileEnabled()) {
                ++GrassStageCurrent().MemoHit;
                if (mesh.PicaModelToClip == entry->Mesh->PicaModelToClip)
                    ++GrassStageCurrent().ClipSame;
                else
                    ++GrassStageCurrent().ClipChanged;
            }
            entry->Mesh->PicaModelToClip = mesh.PicaModelToClip;
            uint64_t identity = mesh.GeometryId ^ (mesh.TextureHash + 0x9e3779b97f4a7c15ULL + (mesh.GeometryId << 6) +
                                                   (mesh.GeometryId >> 2));
            if (masksOnly)
                identity = mesh.GeometryId ^ 0x67726173736d6173ULL;
            const uint32_t occurrence = occurrences[GrassTargetOccurrenceKey(
                identity, draw.RenderTargetNamespace, draw.FramebufferColorPhysicalAddress)]++;
            mesh.InstanceId = identity ^ (uint64_t(occurrence) + 0x9e3779b97f4a7c15ULL + (identity << 6) +
                                          (identity >> 2));
            mesh.WorldInstanceId = mesh.InstanceId == 0U ? 1U : mesh.InstanceId;
            mesh.InstanceId ^=
                draw.RenderTargetNamespace + 0x9e3779b97f4a7c15ULL + (mesh.InstanceId << 6) + (mesh.InstanceId >> 2);
            mesh.InstanceId ^= uint64_t(draw.FramebufferColorPhysicalAddress) + 0x9e3779b97f4a7c15ULL +
                               (mesh.InstanceId << 6) + (mesh.InstanceId >> 2);
            if (!mesh.InstanceId)
                mesh.InstanceId = 1;
            mesh.FrameId = frameId;
            mesh.SubmissionId = draw.SubmissionId;
            mesh.SequenceId = sequenceId;
            mesh.DrawOrder = drawOrder;
            if (audit)
                auditOutcome("published");
            GrassStageTimer publish(GrassStage::Publish);
            GrassSceneBridge::Instance().Publish(std::move(mesh));
            return true;
        }
        if (GrassStageProfileEnabled())
            ++GrassStageCurrent().WorldInvalidations;
    }
    lookup.Stop();
    if (GrassStageProfileEnabled())
        ++GrassStageCurrent().MemoMiss;
    GrassSceneMesh mesh;
    bool cacheable = false;
    const char* rejection = "rejected";
    const bool resolved = ResolveNativePicaGrassSurface(draw, frameId, occurrences, settings, worldSurface,
                                                        fragmentUsesTexture0, perspectiveProjection, geometryRegistry,
                                                        sequenceId, drawOrder, masksOnly, mesh, cacheable,
                                                        audit ? &rejection : nullptr);
    if (key && cacheable)
        memo.Store(*key, resolved ? std::optional(mesh) : std::nullopt, frameId);
    if (audit)
        auditOutcome(resolved ? "published" : rejection);
    if (resolved) {
        GrassStageTimer publish(GrassStage::Publish);
        GrassSceneBridge::Instance().Publish(std::move(mesh));
    }
    return resolved;
}

} // namespace Fast

#endif
