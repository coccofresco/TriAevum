#pragma once

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/effect_graph_pass_barriers.h"
#include <array>
#include <memory>
#include <vulkan/vulkan.h>

namespace Fast::Oot3d {
class NriInteropContext;

// Provider-private adapter: preserve texels/coverage, transform only vectors.
// Callers declare their normal-guide read in the effect graph and retire GPU
// work before Configure/Invalidate. No native color or lighting is changed.
class NormalSpacePass final {
  public:
    NormalSpacePass();
    ~NormalSpacePass();
    void Initialize(NriInteropContext& interop);
    void Configure(uint32_t width, uint32_t height, VkImage input, VkFormat format);
    void Execute(uint32_t frameSlot, const std::array<float, 16>& normalTransform,
                 EffectPassBarrierExecution& barriers);
    void Invalidate();
    [[nodiscard]] VkImage OutputImage() const;
  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
} // namespace Fast::Oot3d
#endif
