#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/directional_shadows.h"
#include "fast/oot3d/effect_graph_pass_barriers.h"
#include "fast/oot3d/native_scene_view.h"
#include "fast/oot3d/pica_directional_shadow_lighting.h"
#include "fast/renderer/extension_schedule.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;

enum class NriDirectionalShadowExecutionStage : uint8_t {
    NotAttempted,
    InputAccepted,
    CastersMatched,
    NativeLightResolved,
    FramePlanReady,
    ResourcesReady,
    ShadowRasterized,
    HistoryPublished,
};

struct NriDirectionalShadowExecuteDesc {
    uint32_t FrameSlot = 0;
    uint64_t FrameId = 0;
    uint64_t RenderTargetNamespace = 0;
    uint32_t ColorPhysicalAddress = 0;
    ::Fast::Renderer::ExtensionPassAuthorization ScheduleAuthorization;
    ::Fast::Renderer::ExtensionSurfaceIdentity ExpectedSurface;
    DirectionalShadowSettings Settings;
};

struct NriDirectionalShadowHistorySnapshot {
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkSampler Sampler = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_D32_SFLOAT;
    PicaDirectionalShadowHistoryState State;

    [[nodiscard]] bool Valid() const noexcept {
        return Image != VK_NULL_HANDLE && View != VK_NULL_HANDLE &&
               Sampler != VK_NULL_HANDLE && State.Valid();
    }
};

struct NriDirectionalShadowLightTelemetry {
    DirectionalShadowVector WorldDirectionTowardSource{};
    uint32_t CandidateCount = 0U;
    uint32_t ClusterCount = 0U;
    float GeometryLightWeight = 0.0F;

    [[nodiscard]] bool Valid() const noexcept {
        return CandidateCount != 0U && ClusterCount != 0U &&
               GeometryLightWeight > 0.0F;
    }
};

// Isolated NRI-owned enhanced shadow-map producer. Eligible native PICA
// geometry is replayed into a two-slot temporal depth history. Visibility is
// consumed separately at the typed PICA lighting hook; this module never
// reads or rewrites the resolved scene color.
class NriDirectionalShadowPass final {
  public:
    NriDirectionalShadowPass();
    ~NriDirectionalShadowPass();
    NriDirectionalShadowPass(const NriDirectionalShadowPass&) = delete;
    NriDirectionalShadowPass& operator=(const NriDirectionalShadowPass&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device, NriInteropContext& interop);
    bool ExecuteMap(const NriDirectionalShadowExecuteDesc& desc,
                    const NativeSceneView& sceneView,
                    const EffectPassBarrierPlan& shadowMapBarrierPlan);
    [[nodiscard]] NriDirectionalShadowHistorySnapshot
    FindLatestHistory(uint64_t renderTargetNamespace,
                      uint32_t colorPhysicalAddress,
                      uint64_t beforeFrameId) const noexcept;
    void InvalidateScreenResources();
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] bool LastExecutionUsedNativeLight() const;
    [[nodiscard]] uint32_t LastCasterCount() const;
    [[nodiscard]] const NriDirectionalShadowLightTelemetry&
    LastLightTelemetry() const;
    [[nodiscard]] NriDirectionalShadowExecutionStage LastExecutionStage() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastShadowMapBarrierExecution() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
