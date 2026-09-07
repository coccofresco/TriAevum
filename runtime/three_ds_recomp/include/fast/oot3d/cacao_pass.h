#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/cacao_settings_profile.h"
#include "fast/oot3d/effect_graph_pass_barriers.h"
#include "fast/oot3d/pica_surface_coordinates.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;
struct NriEffectGraphTransientImageBinding;

class CacaoPass final {
  public:
    CacaoPass();
    ~CacaoPass();
    CacaoPass(const CacaoPass&) = delete;
    CacaoPass& operator=(const CacaoPass&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    NriInteropContext& interop);
    bool Configure(const NriEffectGraphTransientImageBinding& output,
                   VkImageView depthView,
                   VkImageView picaNormalGuideView,
                   const CacaoSettings& settings);
    bool Execute(VkCommandBuffer commandBuffer,
                 const std::array<float, 16>& projection,
                 const EffectPassBarrierPlan& barrierPlan,
                 std::optional<PicaSurfaceCoordinates> coordinates = std::nullopt);
    void InvalidateScreenResources();
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] VkImageView OutputView() const;
    [[nodiscard]] bool OutputOwnedByNri() const;
    [[nodiscard]] CacaoNormalSource NormalSource() const;
    [[nodiscard]] bool UsesPicaNormalGuide() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution()
        const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
