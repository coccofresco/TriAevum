#pragma once

#include "fast/renderer3ds/pica_composition.h"
#include "fast/renderer3ds/pica_frame_timing.h"
#include "fast/renderer3ds/pica_native_state.h"
#include "fast/renderer3ds/pica_shader_hooks.h"
#include "fast/renderer3ds/pica_shader_source_identity.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
    uint16_t RenderScalePermille = 1000;
    uint32_t ImageWidth = 0;
    uint32_t ImageHeight = 0;
    uint8_t SampleCount = 1;
    bool WBuffering = false;
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
    virtual bool SubmitPicaDisplayTransfer(
        const PicaDisplayTransferView& transfer,
        std::string* error = nullptr) = 0;
    virtual bool ClearPicaRenderTarget(
        uint64_t renderTargetNamespace, uint32_t colorPhysicalAddress,
        std::string* error = nullptr) = 0;
    virtual bool SubmitPicaMemoryFill(
        const PicaMemoryFillView& fill, std::string* error = nullptr) = 0;
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
};

} // namespace Fast::Renderer3ds
