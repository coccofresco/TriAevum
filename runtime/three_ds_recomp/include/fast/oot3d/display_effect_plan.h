#pragma once

#include "fast/oot3d/effect_graph.h"
#include "fast/oot3d/graphics_settings.h"
#include "fast/renderer3ds/pica_composition.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fast::Oot3d {

enum class DisplayEffectPass : uint8_t {
    Guides,
    AmbientOcclusion,
    HiZ,
    Reflections,
    Outline,
    Composite,
    Motion,
    Taa,
    TemporalUpscaler,
    Nis,
    Smaa,
    Scanout,
    WorkingColor,
    SceneComposition,
    GlobalIllumination,
    Count,
};

[[nodiscard]] std::string_view DisplayEffectPassName(
    DisplayEffectPass pass) noexcept;

struct DisplayEffectPlanInput {
    bool AlphaOverlay = false;
    bool WorldSurface = false;
    bool PerspectiveAvailable = false;
    bool CameraAvailable = false;
    bool SmaaAvailable = false;
    bool ForceTaa = false;
    bool ForceMotion = false;
    AmbientOcclusionMode AmbientOcclusion = AmbientOcclusionMode::Off;
    // The AO provider accumulates over frames with the scene's motion vectors.
    bool AmbientOcclusionTemporal = false;
    // The AO provider reads the face normal of the geometry guide.
    bool AmbientOcclusionFaceNormal = false;
    ReflectionMode Reflections = ReflectionMode::Off;
    // Diffuse GI is requested and its provider is available.
    bool GlobalIllumination = false;
    // Specular GI is requested: the reflection source when no screen-space
    // provider is selected, the fill of HiZ misses with HiZ.
    bool GlobalIlluminationSpecular = false;
    // The diffuse GI provider is the screen-space gather: it reads the native
    // depth, the geometry guide's face normal, the surface guide and the
    // motion vectors, and has no specular term.
    bool GlobalIlluminationScreenSpace = false;
    ToonMode Toon = ToonMode::Off;
    bool OutlineEnabled = false;
    AntiAliasingMode AntiAliasing = AntiAliasingMode::Off;
    UpscalerProvider Upscaler = UpscalerProvider::Nis;
    // The selected upscaler's provider can execute on this device now. A
    // provider that is missing or was withdrawn after a failure plans no
    // upscaler, so the display presents without one instead of declaring an
    // output nobody writes.
    bool UpscalerAvailable = true;
    bool GuideDiagnostics = false;
    // This boundary composes the scene inside the frame, at the native
    // consumer's own order, instead of at presentation. Only scene stages may
    // run: presentation stages (upscalers, scanout) belong to the display that
    // later presents this scene.
    bool NativeSceneBoundary = false;
    // This native boundary is the scene's completion before native UI on its
    // target: its composition is written back into the target and the UI draws
    // over it. Same-extent anti-aliasing (TAA, SMAA) is a scene stage that
    // resolves here, so native UI never enters it; raw-copy boundaries never
    // run it, and a display without this boundary runs it at presentation.
    bool BeforeNativeUi = false;
    // This boundary's scene stages were already resolved for this scene
    // identity by an earlier declared boundary, so only presentation stages may
    // run and they read the resolved colour instead of re-applying the scene
    // effects a second time.
    bool SceneEffectsResolved = false;
    // The display also carries its resolved scene before native UI
    // (SceneBeforeUi). An upscaler then reconstructs that scene and lays the
    // display's UI over its output instead of upscaling the UI with it.
    bool SceneBeforeUi = false;
    Renderer3ds::PicaCompositionPurpose Purpose =
        Renderer3ds::PicaCompositionPurpose::Presentation;
};

struct DisplayEffectPlan {
    ReflectionMode ReflectionProvider = ReflectionMode::Off;
    UpscalerProvider Upscaler = UpscalerProvider::Nis;
    // The declared scene colour of this boundary before any presentation stage:
    // the resource a native consumer samples, anti-aliased at a boundary before
    // native UI. It stays SceneColor when no scene stage applies, so the native
    // target is already the composed scene.
    EffectResource SceneOutput = EffectResource::SceneColor;

    // Compatibility projections. They are populated from Graph after it is
    // compiled and are never the authority used by renderer dispatch.
    bool AmbientOcclusion = false;
    bool Outline = false;
    bool Reflections = false;
    bool GlobalIllumination = false;
    // The Reflections pass resolves GI specular instead of a screen-space trace.
    bool ReflectionsFromGlobalIllumination = false;
    // The Reflections pass traces HiZ and fills its misses with GI specular.
    bool ReflectionMissesFromGlobalIllumination = false;
    bool FidelityFxSssr = false;
    bool TemporalMetadata = false;
    bool Taa = false;
    bool Smaa = false;
    bool Nis = false;
    bool Fsr = false;
    bool Dlss = false;
    bool TemporalUpscaler = false;
    // The upscaler reads the scene before native UI and lays the display's UI
    // over its output (it also reads SceneColor for that UI).
    bool NativeUiOverlay = false;
    bool MotionVectors = false;
    bool RequiresGuideTarget = false;
    CompiledEffectGraph Graph;

    [[nodiscard]] const CompiledEffectPass* FindPass(
        DisplayEffectPass pass) const noexcept;
    [[nodiscard]] bool Enabled(DisplayEffectPass pass) const noexcept;
    [[nodiscard]] std::span<const EffectResourceUse> Bindings(
        DisplayEffectPass pass) const noexcept;
    [[nodiscard]] size_t DeclaredBindingCount() const noexcept;
};

enum class DisplayEffectExecutionOutcome : uint8_t {
    Unresolved,
    Executed,
    Reused,
    Fused,
    Skipped,
    Failed,
};

struct DisplayEffectExecutionSummary {
    uint32_t DeclaredPassMask = 0;
    uint32_t ExecutedPassMask = 0;
    uint32_t ReusedPassMask = 0;
    uint32_t FusedPassMask = 0;
    uint32_t SkippedPassMask = 0;
    uint32_t FailedPassMask = 0;
    uint32_t UnresolvedPassMask = 0;
    uint32_t DeclaredPassCount = 0;
    uint32_t ExecutedPassCount = 0;
    uint32_t ReusedPassCount = 0;
    uint32_t FusedPassCount = 0;
    uint32_t SkippedPassCount = 0;
    uint32_t FailedPassCount = 0;
    uint32_t UnresolvedPassCount = 0;
    uint32_t ActiveBindingCount = 0;
    uint32_t UndeclaredRecordCount = 0;
    uint32_t DuplicateRecordCount = 0;

    [[nodiscard]] bool FullyAccounted() const noexcept {
        return UnresolvedPassCount == 0U &&
               UndeclaredRecordCount == 0U &&
               DuplicateRecordCount == 0U;
    }
    [[nodiscard]] bool Successful() const noexcept {
        return FullyAccounted() && FailedPassCount == 0U;
    }
};

// Fixed-size execution state for the compiled display graph. This is kept
// separate from the graph declaration so one immutable plan can describe
// multiple display transfers without mutable backend state leaking into it.
class DisplayEffectExecutionLedger {
  public:
    explicit DisplayEffectExecutionLedger(
        const DisplayEffectPlan& plan) noexcept;

    [[nodiscard]] bool Record(
        DisplayEffectPass pass,
        DisplayEffectExecutionOutcome outcome) noexcept;
    [[nodiscard]] DisplayEffectExecutionSummary Summary() const noexcept;

  private:
    static constexpr size_t kPassCount =
        static_cast<size_t>(DisplayEffectPass::Count);

    const DisplayEffectPlan* mPlan = nullptr;
    std::array<DisplayEffectExecutionOutcome, kPassCount> mOutcomes{};
    uint32_t mUndeclaredRecordCount = 0;
    uint32_t mDuplicateRecordCount = 0;
};

// Pure per-view policy. Backend code executes the resulting pass flags but no
// longer re-derives feature dependencies from UI/environment state.
[[nodiscard]] DisplayEffectPlan BuildDisplayEffectPlan(
    const DisplayEffectPlanInput& input);

// The declared stage domains of a display graph. A pass belongs to a domain by
// its declared stage, never by its name.
enum class DisplayEffectDomain : uint8_t {
    // The scene a native consumer composes at its own order: everything at or
    // before the transparent domain.
    Scene,
    // Everything at or after the pre-UI domain: the stages that present this
    // scene, including UI.
    Presentation,
};

// Projects a compiled display graph onto one declared domain. A domain pass is
// kept, and so is every pass it transitively reads, with dependencies rebuilt
// from the declared resource edges. A read of an already-resolved resource is
// re-pointed at the colour that carries it, so a scene stage the boundary
// already applied is never applied twice while auxiliary scene data stays
// re-derivable. Passes are selected by stage and resource only, so a new scene
// pass joins a boundary and leaves a resolved presentation by declaring its
// stage and resources alone.
[[nodiscard]] std::vector<EffectPass> ProjectDisplayEffectPasses(
    const CompiledEffectGraph& graph, DisplayEffectDomain domain,
    std::span<const EffectResource> resolvedResources,
    EffectResource resolvedInput);

} // namespace Fast::Oot3d
