#pragma once

#include "fast/renderer3ds/pica_composition.h"
#include "fast/renderer3ds/pica_frame_timing.h"
#include "fast/renderer3ds/pica_native_state.h"
#include "fast/renderer3ds/pica_light_binding.h"
#include "fast/renderer3ds/pica_shader_hooks.h"
#include "fast/renderer3ds/pica_shader_source_identity.h"
#include "fast/renderer3ds/pica_texture_copy_plan.h"
#include "fast/renderer3ds/pica_vertex_artifact.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Fast::Renderer3ds {

struct PicaDrawView {
    uint64_t SubmissionId = 0;
    uint64_t RenderTargetNamespace = 0;
    uint32_t CommandListAddress = 0;
    uint32_t CommandListOffsetWords = 0;
    PicaCompositionDomain CompositionDomain =
        PicaCompositionDomain::Unknown;
    PicaCompositionAttribution Composition;
    PicaNativeLightBinding LightBinding;
    uint32_t CanonicalDescriptorSchemaVersion = 0;
    uint64_t CanonicalVertexProgramId = 0;
    uint64_t CanonicalFragmentProgramId = 0;
    uint64_t CanonicalRasterStateId = 0;
    uint64_t CanonicalPipelineId = 0;
    uint64_t CanonicalDynamicStateId = 0;
    uint64_t CanonicalFullRegisterStateId = 0;
    uint64_t VertexShaderKey = 0;
    uint64_t FragmentShaderKey = 0;
    std::string_view VertexShaderSource;
    std::string_view FragmentShaderSource;
    PicaShaderSourceIdentity VertexShaderSourceIdentity;
    PicaShaderSourceIdentity FragmentShaderSourceIdentity;
    PicaTemporalVertexProgramView TemporalVertexProgram;
    PicaShaderHookLayout FragmentShaderHooks;
    // The always-available program of the same draw (hybrid preparation):
    // its parametric fragment program, drawn while the specialized program
    // above is still being prepared. Empty when the draw has none.
    uint64_t UberFragmentShaderKey = 0;
    std::string_view UberFragmentShaderSource;
    PicaShaderSourceIdentity UberFragmentShaderSourceIdentity;
    PicaShaderHookLayout UberFragmentShaderHooks;
    PicaFragmentFeatureView FragmentFeatures;
    std::span<const uint8_t> VertexUniformBytes;
    std::span<const uint8_t> FragmentUniformBytes;
    std::span<const PicaVertexBindingView> VertexBindings;
    std::span<const PicaVertexAttributeView> VertexAttributes;
    uint64_t GeometryIdentity = 0;
    uint64_t GeometryContentVersion = 0;
    bool GeometryIdentityAvailable = false;
    uint8_t PositionAttributeLocation = 0xff;
    uint8_t TexCoord0AttributeLocation = 0xff;
    std::span<const uint8_t> IndexBytes;
    bool Indexed = false;
    bool IndicesAre16Bit = false;
    int32_t BaseVertex = 0;
    uint32_t VertexCount = 0;
    std::span<const PicaTextureView> Textures;
    PicaLightingLutView LightingLut;
    PicaTopology Topology = PicaTopology::TriangleList;
    NativeCullMode CullMode = NativeCullMode::KeepAll;
    float ViewportX = 0.0F;
    float ViewportY = 0.0F;
    float ViewportWidth = 0.0F;
    float ViewportHeight = 0.0F;
    // The title's interface frame along native Y relative to the screen
    // (ResolvePicaRasterCanvas): 1 keeps the native screen as the UI canvas.
    // Only a whole-screen draw that stays UI uses it.
    float UiFrameExtensionY = 1.0F;
    float DepthRange = 0.0F;
    float NearPlane = 0.0F;
    bool WBuffering = false;
    bool FramebufferFlipped = false;
    uint16_t FramebufferWidth = 0;
    uint16_t FramebufferHeight = 0;
    uint32_t FramebufferColorPhysicalAddress = 0;
    uint32_t FramebufferDepthPhysicalAddress = 0;
    uint8_t FramebufferColorFormat = 0;
    uint8_t FramebufferDepthFormat = 0;
    uint8_t ScissorMode = 0;
    uint16_t ScissorX1 = 0;
    uint16_t ScissorY1 = 0;
    uint16_t ScissorX2 = 0;
    uint16_t ScissorY2 = 0;
    uint8_t ColorWriteMask = 0;
    uint8_t FragmentOperationMode = 0;
    PicaLogicOperation LogicOperation = PicaLogicOperation::Copy;
    NativeBlendState Blend;
    bool AlphaTestEnabled = false;
    PicaCompareFunction AlphaCompare = PicaCompareFunction::Always;
    uint8_t AlphaReference = 0;
    bool DepthTestEnabled = false;
    bool DepthWriteEnabled = false;
    PicaCompareFunction DepthCompare = PicaCompareFunction::Always;
    PicaStencilState Stencil;
    std::span<const PicaVertexArtifact> VertexArtifacts;
};

// Projects the full transient draw facade into the compact ordered metadata
// retained by the shared Nintendo 3DS composition scheduler.
[[nodiscard]] inline PicaCompositionDrawReference
BuildPicaCompositionDrawReference(const PicaDrawView& draw) noexcept {
    return {
        draw.SubmissionId,
        {
            draw.RenderTargetNamespace,
            draw.FramebufferColorPhysicalAddress,
            draw.FramebufferDepthPhysicalAddress,
            draw.FramebufferWidth,
            draw.FramebufferHeight,
            draw.FramebufferColorFormat,
            draw.FramebufferDepthFormat,
        },
        draw.CompositionDomain,
        draw.Composition,
    };
}

enum class PicaPresentationMode : uint8_t {
    Replace,
    AlphaOverlay,
};

struct PicaDisplayTransferView {
    uint64_t CompletionId = 0;
    uint64_t RenderTargetNamespace = 0;
    uint32_t InputPhysicalAddress = 0;
    uint32_t OutputPhysicalAddress = 0;
    uint16_t InputWidth = 0;
    uint16_t InputHeight = 0;
    uint16_t OutputWidth = 0;
    uint16_t OutputHeight = 0;
    uint32_t Flags = 0;
    bool Present = false;
    PicaPresentationMode PresentationMode = PicaPresentationMode::Replace;
    uint32_t TextureCopyBytes = 0;
    uint64_t AfterDrawSubmissionId = 0;
    // Real ordered execution still owing the guest a visible device write and
    // one completion. Visibility may be backed by an immutable GPU producer
    // plus guarded CPU materialization; it need not mean copying into RAM now.
    // Replays/restored transfers leave this false and never complete twice.
    bool MaterializeGuest = false;
    // Not a GX operation: the ordered point at which a colour render target,
    // written by earlier draws, is sampled as a texture by a later one. The
    // backend captures the target as an immutable producer version for the
    // copy references at InputPhysicalAddress (Input/Output name the target,
    // the extents are its native size); no guest write or completion exists.
    bool RenderTargetCapture = false;
};

struct PicaPhysicalMemoryWrite {
    uint32_t Address = 0;
    std::vector<uint8_t> Before;
    std::vector<uint8_t> Bytes;
};

struct PicaMemoryFillView {
    uint64_t RenderTargetNamespace = 0;
    uint32_t StartPhysicalAddress = 0;
    uint32_t EndPhysicalAddress = 0;
    uint32_t Value = 0;
    uint16_t Control = 0;
};

enum class PicaTextureSnapshotFormat : uint8_t {
    Rgba8 = 0,
    R32Uint = 1,
};

// Producer identity of a record an ordered raw PICA TextureCopy produced. The
// triple (RenderTargetNamespace, CompletionId, AfterDrawSubmissionId) is the
// same key the backend registers the copy version under and the same identity a
// sampled draw references, so a restore rebuilds the producer instead of
// guessing it from content.
struct PicaTextureCopyIdentity {
    uint64_t RenderTargetNamespace = 0;
    uint64_t CompletionId = 0;
    uint64_t AfterDrawSubmissionId = 0;
    // Copy source (the native color target the raw copy encoded).
    uint32_t SourcePhysicalAddress = 0;
    uint32_t SourceWidth = 0;
    uint32_t SourceHeight = 0;
    uint8_t SourceNativeFormat = 0;
    // Destination subranges this producer owns.
    std::vector<PicaRawCopySpan> Spans;
    uint8_t ColorEncoding = 0;
    bool SceneEffectsResolved = false;
};

// Portable decoded texture-cache entry. The source PICA identity is retained
// so resumed draws resolve the same immutable texture even after guest memory
// at its original physical address has been reused.
struct PicaTextureCacheEntrySnapshot {
    uint64_t ContentHash = 0;
    uint64_t ReplacementGeneration = 0;
    uint32_t PhysicalAddress = 0;
    uint16_t SourceWidth = 0;
    uint16_t SourceHeight = 0;
    uint8_t NativeFormat = 0;
    uint8_t NativeType = 0;
    uint8_t NativeWrapS = 0;
    uint8_t NativeWrapT = 0;
    bool MinLinear = false;
    bool MagLinear = false;
    bool MipLinear = false;
    int16_t LodBiasRaw = 0;
    uint8_t MinMipLevel = 0;
    uint8_t MaxMipLevel = 0;
    bool CustomReplacement = false;
    uint32_t ImageWidth = 0;
    uint32_t ImageHeight = 0;
    uint8_t MipLevels = 1;
    PicaTextureSnapshotFormat ImageFormat =
        PicaTextureSnapshotFormat::Rgba8;
    uint64_t CustomReplacementHash = 0;
    bool CustomReplacementPending = false;
    bool CustomReplacementReady = false;
    // Ordered raw-copy producer of this record, present only when the record is
    // a GPU copy version. Ownership is never inferred from ContentHash, from
    // the image size or from an absent hash: an entry with HasCopyProducer
    // false is an ordinary texture. ImageWidth/ImageHeight/PixelBytes above
    // remain the full captured GPU image at its internal extent.
    bool HasCopyProducer = false;
    PicaTextureCopyIdentity CopyProducer;
    std::vector<uint8_t> PixelBytes;
};

// Portable color state for an incrementally rendered native PICA target.
// GPU handles and backend layouts deliberately remain outside this contract.
struct PicaRenderTargetColorSnapshot {
    uint64_t RenderTargetNamespace = 0;
    uint32_t ColorPhysicalAddress = 0;
    uint32_t DepthPhysicalAddress = 0;
    uint16_t FramebufferWidth = 0;
    uint16_t FramebufferHeight = 0;
    uint8_t FramebufferColorFormat = 0;
    uint8_t FramebufferDepthFormat = 0;
    // 0 (kPicaGuestExtentRenderScalePermille): stored at the framebuffer's
    // guest extent with colour, depth and stencil only, recreated at the
    // restoring renderer's scale. Otherwise a legacy snapshot at that scale
    // with every guide.
    uint16_t RenderScalePermille = 1000;
    uint32_t ImageWidth = 0;
    uint32_t ImageHeight = 0;
    uint8_t SampleCount = 1;
    bool WBuffering = false;
    // The saved colour is the content a resolved native boundary left, so a
    // display of it before any further draw must not re-run the scene stages.
    bool SceneEffectsResolved = false;
    std::vector<uint8_t> ColorRgba8;
    std::vector<uint8_t> NormalGuideRgba8;
    std::vector<uint8_t> MaterialGuideRgba8;
    std::vector<uint8_t> RigidMotionGuideRgba16FloatLe;
    std::vector<uint8_t> AmbientGuideRgba8;
    std::vector<uint8_t> ShadowR32UintLe;
    std::vector<float> DepthValues;
    std::vector<uint8_t> StencilValues;
};

// Portable scanout state for display images that can outlive their source
// render target contents. This is required by incrementally updated native
// UI surfaces and intentionally excludes backend handles and image layouts.
struct PicaDisplayImageColorSnapshot {
    uint64_t RenderTargetNamespace = 0;
    uint32_t OutputPhysicalAddress = 0;
    uint32_t ImageWidth = 0;
    uint32_t ImageHeight = 0;
    bool Initialized = false;
    // Same meaning as the color-target flag: the saved colour already carries
    // resolved native effects, so a display of this image must not re-run the
    // scene stages even when its source target is gone.
    bool SceneEffectsResolved = false;
    std::vector<uint8_t> ColorRgba8;

    bool HasDepthTarget = false;
    uint64_t DepthTargetNamespace = 0;
    uint32_t DepthTargetColorPhysicalAddress = 0;
    uint32_t DepthTargetDepthPhysicalAddress = 0;
    uint16_t DepthTargetFramebufferWidth = 0;
    uint16_t DepthTargetFramebufferHeight = 0;
    uint8_t DepthTargetFramebufferColorFormat = 0;
    uint8_t DepthTargetFramebufferDepthFormat = 0;
    uint16_t DepthTargetRenderScalePermille = 1000;
};

struct PicaPresentationStateSnapshot {
    std::vector<PicaDisplayImageColorSnapshot> DisplayImages;
    std::optional<PicaDisplayTransferView> LastPresentedTransfer;
};

// Renderer-facing PICA transport shared by Nintendo 3DS title adapters. It
// contains no title gameplay, actor, camera or effect-activation policy.
class PicaRenderBackend {
  public:
    using PhysicalMemoryRead = std::function<bool(uint32_t, std::span<uint8_t>)>;
    using PhysicalMemoryCommit = std::function<bool(std::span<const PicaPhysicalMemoryWrite>)>;
    using PhysicalMemoryDefer = std::function<bool(
        std::span<const PicaRawCopySpan>, PhysicalMemoryRead)>;
    using PhysicalMemoryComplete = std::function<bool(uint64_t)>;
    using PhysicalMemoryBarrier = std::function<bool()>;
    void SetPicaPhysicalMemoryAccess(PhysicalMemoryRead read, PhysicalMemoryCommit commit,
                                     PhysicalMemoryDefer defer, PhysicalMemoryComplete complete,
                                     PhysicalMemoryBarrier barrier) {
        mPhysicalMemoryRead = std::move(read);
        mPhysicalMemoryCommit = std::move(commit);
        mPhysicalMemoryDefer = std::move(defer);
        mPhysicalMemoryComplete = std::move(complete);
        mPhysicalMemoryBarrier = std::move(barrier);
    }
    virtual ~PicaRenderBackend() = default;

    virtual bool PublishPicaCompositionSequence(
        const PicaCompositionSequenceView& sequence,
        std::string* error = nullptr) {
        if (sequence.SchemaVersion !=
            kPicaCompositionSequenceSchemaVersion) {
            if (error != nullptr) {
                *error = "native PICA composition sequence schema is unsupported";
            }
            return false;
        }
        return true;
    }

    virtual bool SubmitPicaDraw(
        const PicaDrawView& draw, std::string* error = nullptr) = 0;
    // Draws of the published composition sequence can be handed over before
    // the sequence executes, for backend work that must consume them ahead of
    // their native order (geometry another pass needs before the draws that
    // depend on it). The backend selects them from the published metadata.
    // Preparing never rasterizes, consumes the sequence or replaces the later
    // ordered submission of the same draw. A backend that does not prepare
    // keeps the default: no draw is handed over.
    [[nodiscard]] virtual bool WantsPicaSequenceDrawPreparation(
        const PicaCompositionDrawReference&) const {
        return false;
    }
    virtual bool PreparePicaSequenceDraw(
        const PicaDrawView&, std::string* = nullptr) {
        return true;
    }
    virtual bool SubmitPicaDisplayTransfer(
        const PicaDisplayTransferView& transfer,
        std::string* error = nullptr) = 0;
    virtual bool ClearPicaRenderTarget(
        uint64_t renderTargetNamespace, uint32_t colorPhysicalAddress,
        std::string* error = nullptr) = 0;
    virtual bool SubmitPicaMemoryFill(
        const PicaMemoryFillView& fill, std::string* error = nullptr) = 0;
    // Copy versions a retained plan can still replay. The backend keeps those
    // GPU producers alive behind a weak completion token plus their retirement
    // fence and must not destroy a version that is still in the set. Replacing
    // the set releases the versions that left it; an empty set is a valid
    // unpin and never an error.
    virtual bool SetPinnedPicaTextureCopies(
        std::span<const PicaTextureCopyReference> copies,
        std::string* error = nullptr) {
        if (copies.empty()) {
            return true;
        }
        if (error != nullptr) {
            *error =
                "rendering backend does not pin native PICA texture copies";
        }
        return false;
    }
    // The render-target namespace whose execution is the canonical native
    // one. A copy version another namespace never executed (a superseded
    // visual frame) may be imported from it, never in the other direction.
    virtual void SetCanonicalPicaRenderTargetNamespace(uint64_t) {}
    virtual bool CapturePicaTextureCache(
        std::vector<PicaTextureCacheEntrySnapshot>& snapshots,
        std::string* error = nullptr) {
        snapshots.clear();
        if (error != nullptr) {
            *error = "rendering backend does not capture native PICA texture cache";
        }
        return false;
    }
    virtual bool RestorePicaTextureCache(
        std::span<const PicaTextureCacheEntrySnapshot> snapshots,
        std::string* error = nullptr) {
        if (snapshots.empty()) {
            return true;
        }
        if (error != nullptr) {
            *error = "rendering backend does not restore native PICA texture cache";
        }
        return false;
    }
    virtual bool CapturePicaColorTargets(
        std::vector<PicaRenderTargetColorSnapshot>& snapshots,
        std::string* error = nullptr) {
        snapshots.clear();
        if (error != nullptr) {
            *error = "rendering backend does not capture native PICA color targets";
        }
        return false;
    }
    virtual bool RestorePicaColorTargets(
        std::span<const PicaRenderTargetColorSnapshot> snapshots,
        std::string* error = nullptr) {
        if (snapshots.empty()) {
            return true;
        }
        if (error != nullptr) {
            *error = "rendering backend does not restore native PICA color targets";
        }
        return false;
    }
    virtual bool CapturePicaPresentationState(
        PicaPresentationStateSnapshot& snapshot,
        std::string* error = nullptr) {
        snapshot = {};
        if (error != nullptr) {
            *error = "rendering backend does not capture native PICA presentation state";
        }
        return false;
    }
    virtual bool RestorePicaPresentationState(
        const PicaPresentationStateSnapshot& snapshot,
        std::string* error = nullptr) {
        if (snapshot.DisplayImages.empty() &&
            !snapshot.LastPresentedTransfer.has_value()) {
            return true;
        }
        if (error != nullptr) {
            *error = "rendering backend does not restore native PICA presentation state";
        }
        return false;
    }

    // Completion IDs are opaque renderer fence tokens. A title adapter owns
    // their mapping to the guest GSP interrupt/service contract.
    virtual bool QueuePicaCompletion(
        uint64_t, std::string* error = nullptr) {
        if (error != nullptr) {
            *error = "rendering backend does not track PICA completions";
        }
        return false;
    }
    virtual std::vector<uint64_t> TakePicaCompletions() {
        return {};
    }

    // Invalidates all backend state derived from a Nintendo 3DS PICA stream.
    // Title gameplay and guest-service reset policy remain outside this call.
    virtual bool ResetPicaState(std::string* error = nullptr) {
        if (error != nullptr) {
            *error = "rendering backend cannot reset PICA state";
        }
        return false;
    }

    virtual bool PublishPicaFrameTemporalSample(
        const PicaFrameTemporalSample&) {
        return false;
    }
  protected:
    PhysicalMemoryRead mPhysicalMemoryRead;
    PhysicalMemoryCommit mPhysicalMemoryCommit;
    PhysicalMemoryDefer mPhysicalMemoryDefer;
    PhysicalMemoryComplete mPhysicalMemoryComplete;
    PhysicalMemoryBarrier mPhysicalMemoryBarrier;
};

} // namespace Fast::Renderer3ds
