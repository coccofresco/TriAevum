#pragma once

#include "fast/renderer3ds/pica_native_state.h"
#include "fast/renderer3ds/pica_shader_hooks.h"

#include <array>
#include <cstdint>
#include <string>

namespace Fast::Renderer3ds {

inline constexpr uint32_t kPicaPerspectiveCameraSchemaVersion = 1U;
inline constexpr uint32_t kPicaSceneResolvedRasterStateSchemaVersion = 1U;
inline constexpr uint32_t kPicaSceneRenderTargetStateSchemaVersion = 1U;
inline constexpr uint32_t kPicaSceneResolvedMaterialStateSchemaVersion = 1U;

// Renderer-facing perspective and pose state derived from a 3DS title's
// camera hooks. Guest addresses are provenance only and never drive policy.
struct PicaPerspectiveCameraState {
    uint64_t Serial = 0;
    uint32_t GuestFunction = 0;
    uint32_t GuestReturnAddress = 0;
    float Left = 0.0F;
    float Right = 0.0F;
    float Bottom = 0.0F;
    float Top = 0.0F;
    float NearPlane = 0.0F;
    float FarPlane = 0.0F;
    std::array<float, 16> Projection{};
    std::array<float, 3> Eye{};
    std::array<float, 3> At{};
    std::array<float, 16> WorldToClip{};
    bool CameraAvailable = false;
};

enum class PicaSceneTopology : uint8_t {
    TriangleList,
    TriangleStrip,
};

enum class PicaSceneCullMode : uint8_t {
    None,
    Front,
    Back,
};

enum class PicaSceneFrontFace : uint8_t {
    Clockwise,
    CounterClockwise,
};

enum class PicaSceneShaderStage : uint8_t {
    Vertex,
    Fragment,
};

enum class PicaSceneTextureImageFormat : uint8_t {
    Rgba8,
    R32Uint,
};

struct PicaSceneBufferResource {
    uintptr_t NativeHandle = 0;
    uint64_t Size = 0;
};

struct PicaSceneVertexBufferBinding {
    uint32_t Binding = 0;
    uint64_t Offset = 0;
    uint64_t Size = 0;
    uint32_t Stride = 0;
};

struct PicaSceneShaderProgram {
    PicaSceneShaderStage Stage = PicaSceneShaderStage::Vertex;
    uint64_t Key = 0;
    std::string Source;
    PicaVertexShaderHookLayout VertexHooks;

    [[nodiscard]] bool VertexHooksAvailable() const noexcept {
        return Stage == PicaSceneShaderStage::Vertex &&
               VertexHooks.ValidFor(Source);
    }
};

struct PicaSceneTextureBinding {
    uintptr_t NativeImageHandle = 0;
    uint64_t NativeContentHash = 0;
    uint64_t NativeBaseLevelContentHash = 0;
    uint64_t ReplacementContentHash = 0;
    uint32_t PhysicalAddress = 0;
    uint32_t ImageWidth = 0;
    uint32_t ImageHeight = 0;
    uint32_t MipLevels = 0;
    uint16_t SourceWidth = 0;
    uint16_t SourceHeight = 0;
    int16_t LodBiasRaw = 0;
    uint8_t Slot = 0;
    uint8_t NativeFormat = 0;
    uint8_t NativeType = 0;
    uint8_t NativeWrapS = 0;
    uint8_t NativeWrapT = 0;
    uint8_t MinMipLevel = 0;
    uint8_t MaxMipLevel = 0;
    PicaSceneTextureImageFormat ImageFormat =
        PicaSceneTextureImageFormat::Rgba8;
    bool Bound = false;
    bool NativeContentHashAvailable = false;
    bool NativeBaseLevelContentHashAvailable = false;
    bool CustomReplacement = false;
    bool MinLinear = false;
    bool MagLinear = false;
    bool MipLinear = false;
};

// Native register values and the exact viewport/scissor applied to the
// renderer-owned target are retained as distinct coordinate spaces.
struct PicaSceneResolvedRasterState {
    uint32_t SchemaVersion = 0U;
    float NativeViewportX = 0.0F;
    float NativeViewportY = 0.0F;
    float NativeViewportWidth = 0.0F;
    float NativeViewportHeight = 0.0F;
    float ResolvedViewportX = 0.0F;
    float ResolvedViewportY = 0.0F;
    float ResolvedViewportWidth = 0.0F;
    float ResolvedViewportHeight = 0.0F;
    float DepthRange = 0.0F;
    float NearPlane = 0.0F;
    uint8_t ScissorMode = 0U;
    uint16_t NativeScissorX1 = 0U;
    uint16_t NativeScissorY1 = 0U;
    uint16_t NativeScissorX2 = 0U;
    uint16_t NativeScissorY2 = 0U;
    int32_t ResolvedScissorX = 0;
    int32_t ResolvedScissorY = 0;
    uint32_t ResolvedScissorWidth = 0U;
    uint32_t ResolvedScissorHeight = 0U;
    bool WBuffering = false;
    bool FramebufferFlipped = false;

    [[nodiscard]] bool Available() const noexcept {
        return SchemaVersion == kPicaSceneResolvedRasterStateSchemaVersion;
    }
};

// Compact reference to a renderer-owned image. The handle and format remain
// backend values consumed only through the interop contract.
struct PicaSceneGpuImageReference {
    uintptr_t NativeHandle = 0U;
    uint64_t ResourceGeneration = 0U;
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    uint32_t Format = 0U;
    bool Sampleable = false;

    [[nodiscard]] bool Available() const noexcept {
        return NativeHandle != 0U && ResourceGeneration != 0U &&
               Width != 0U && Height != 0U && Format != 0U;
    }
};

struct PicaSceneRenderTargetState {
    uint32_t SchemaVersion = 0U;
    uint64_t RenderTargetNamespace = 0U;
    uint32_t ColorPhysicalAddress = 0U;
    uint32_t DepthPhysicalAddress = 0U;
    uint16_t NativeWidth = 0U;
    uint16_t NativeHeight = 0U;
    uint8_t NativeColorFormat = 0U;
    uint8_t NativeDepthFormat = 0U;
    uint32_t SampleCount = 1U;
    PicaSceneGpuImageReference ResolvedColor;
    PicaSceneGpuImageReference ResolvedDepth;

    [[nodiscard]] bool Available() const noexcept {
        return SchemaVersion == kPicaSceneRenderTargetStateSchemaVersion;
    }

    [[nodiscard]] bool GpuResourcesAvailable() const noexcept {
        return Available() && ResolvedColor.Available() &&
               ResolvedDepth.Available();
    }
};

// Exact fixed-function and fragment-program state resolved from PICA. Texture
// and uniform resources remain separate bindings on each title's draw record.
struct PicaSceneResolvedMaterialState {
    uint32_t SchemaVersion = 0U;
    PicaSceneTopology Topology = PicaSceneTopology::TriangleList;
    PicaSceneCullMode CullMode = PicaSceneCullMode::None;
    PicaSceneFrontFace FrontFace = PicaSceneFrontFace::CounterClockwise;
    uint8_t FragmentOperationMode = 0U;
    uint8_t ColorWriteMask = 0U;
    PicaLogicOperation LogicOperation = PicaLogicOperation::Copy;
    NativeBlendState Blend;
    bool AlphaTestEnabled = false;
    PicaCompareFunction AlphaCompare = PicaCompareFunction::Always;
    uint8_t AlphaReference = 0U;
    bool DepthTestEnabled = false;
    bool DepthWriteEnabled = false;
    PicaCompareFunction DepthCompare = PicaCompareFunction::Always;
    PicaStencilState Stencil;
    PicaFragmentFeatureView FragmentFeatures;

    [[nodiscard]] bool Available() const noexcept {
        return SchemaVersion == kPicaSceneResolvedMaterialStateSchemaVersion;
    }
};

} // namespace Fast::Renderer3ds
