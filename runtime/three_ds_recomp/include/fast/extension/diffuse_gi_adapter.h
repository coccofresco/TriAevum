#pragma once

// OOT3D adapter of the screen-space diffuse GI (docs/TRIAEVUM_SCREEN_SPACE_GI.md,
// "M5", adapter contract): the declared conventions of this renderer's guides
// turned into the module's inputs. Title-specific glue; the module itself
// (fast/renderer/diffuse_gi_*) knows nothing of PICA or OOT3D.
//
// Conventions declared here (never inferred by the module):
// - Depth: the native depth of the guide target, perspective D3D [0, 1]
//   (w = -z) or the PICA W-buffer, near and far of the scene view; a window
//   depth of 1 (the clear value) or more is sky.
// - Camera: storage UV of the guide target -> view NDC through
//   PicaSurfaceCoordinates::StorageToViewNdc, then the x and y rows of the
//   projection the native draws apply (the sample perspective); the jitter
//   (storage UV) moves the image, so a texel shows the unjittered point at
//   uv - jitter.
// - Geometric normal: the geometry guide's face normal (G, octahedral
//   unorm16x2, view space, oriented toward the eye; 0 unknown).
// - Source radiance: the surface guide's low bytes, the sRGB direct radiance
//   of the per-pixel lighting (its sources' relative weights; the largest
//   gain applies at composition): LdrApproximation, never calibrated HDR.
// - Validity: the surface guide's alpha (validity x overlay transmission);
//   zero is unavailable (clear, HUD, replacing surfaces).
// - Motion: the motion vectors' rg, previous minus current storage UV of the
//   same target, jitter included; scaled to the work image's UV.
// - Previous view: P^-1 W_prev W_cur^-1 P from the temporal view state.
// - Shading normal (M8): the normal guide (RGBA8, view space, 0.5 n + 0.5,
//   alpha coverage), the normal the per-pixel lighting shades the texel with:
//   the authored vertex normal interpolated like the native lighting's (or the
//   material / PBR normal of those lighting paths); the derivative flat normal
//   only for programs without a normal. When the AO texture relief is active
//   the guide carries its bend and the GI lobe follows it. Normalized and
//   turned to the face normal's side; none (zero) without coverage or for a
//   degenerate (resolve-blended, length below 1/4) vector.

#include "fast/extension/pica_surface_coordinates.h"
#include "fast/extension/scene_view_runtime.h"
#include "fast/extension/temporal_history_manager.h"
#include "fast/renderer/diffuse_gi_pass_plan.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Fast::Extension {

// The guide target's camera in the module's form (see above).
[[nodiscard]] Renderer::DiffuseGi::DepthCamera DiffuseGiGuideCamera(const PerspectiveViewState& view,
                                                                    PicaSurfaceCoordinates coordinates, bool wBuffer,
                                                                    const std::array<float, 2>& jitterUv) noexcept;

// View -> previous view of a temporal state whose two clip matrices share the
// projection (the history manager resets on a projection change); empty when
// the matrices are not invertible or the result is not rigid.
[[nodiscard]] std::optional<Renderer::DiffuseGi::RigidTransform>
DiffuseGiCurrentToPrevious(const std::array<float, 16>& projection, const TemporalViewState& temporal) noexcept;

// Push constants of the adapter kernel (std430, 64 bytes).
struct DiffuseGiAdapterParams {
    std::array<uint32_t, 4> Extent{}; // guide width, guide height, work width, work height
    std::array<uint32_t, 4> Scale{};  // work factor, phase, depth encoding (1 perspective, 3 W-buffer), write guide
    std::array<float, 4> Depth{};     // near, far, 0, 0
    std::array<float, 4> Motion{};    // guide UV -> work UV scale x, y, 0, 0
};
static_assert(sizeof(DiffuseGiAdapterParams) == 64);

// `shading`: the pass takes shading normals (M8): the kernel decodes the
// normal guide into them (and into the output guide's shading codes).
[[nodiscard]] DiffuseGiAdapterParams MakeDiffuseGiAdapterParams(const Renderer::DiffuseGi::PassExtent& extent,
                                                                const Renderer::DiffuseGi::DepthCamera& camera,
                                                                bool shading = false) noexcept;

// GLSL of the adapter kernel (precompiled in builtin_pass.spvpack as
// oot3d_diffuse_gi_inputs.comp). One invocation per guide texel: every texel
// writes its output-guide entry; the texels of the work grid also write the
// module's work inputs. Set 0: 0 native depth, 1 geometry guide (uint),
// 2 surface guide, 3 motion vectors (sampled images, texelFetch); 4-9 the
// pass buffers depth, class, radiance, normal, motion, output guide; M8: 10
// the normal guide (sampled), 11 the shading normals (vec4 per work texel),
// 12 the output guide's shading codes (uint per output texel).
[[nodiscard]] std::string BuildDiffuseGiAdapterShader();

// FP32 twin of one guide texel (tests): the decoded inputs of the texel.
struct DiffuseGiAdapterTexel {
    float Depth = 1.0F;   // window depth
    uint32_t Class = 3U;  // TexelClass
    std::array<float, 3> Radiance{};
    std::array<float, 3> Normal{};
    std::array<float, 2> Motion{};
    std::array<uint32_t, 2> Guide{}; // view depth bits, normal code
    std::array<float, 3> Shading{};  // M8 shading normal (zero: none)
    uint32_t ShadingGuide = 0U;      // M8 output-guide shading code
};
[[nodiscard]] DiffuseGiAdapterTexel DiffuseGiAdapterTexelF32(const DiffuseGiAdapterParams& params, float depth,
                                                             const std::array<uint32_t, 4>& geometry,
                                                             const std::array<float, 4>& surface,
                                                             const std::array<float, 2>& motion,
                                                             const std::array<float, 4>& normalGuide = {}) noexcept;

} // namespace Fast::Extension

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/renderer/diffuse_gi_pass.h"

#include <vulkan/vulkan.h>

namespace Fast::Extension {

// The adapter kernel: one pipeline per device, one descriptor set per pass
// instance and frame slot (rewritten when that slot records again).
class DiffuseGiAdapterKernel final {
  public:
    DiffuseGiAdapterKernel() = default;
    ~DiffuseGiAdapterKernel();
    DiffuseGiAdapterKernel(const DiffuseGiAdapterKernel&) = delete;
    DiffuseGiAdapterKernel& operator=(const DiffuseGiAdapterKernel&) = delete;
    // Safe on a worker thread.
    bool Create(VkDevice device, const std::vector<uint32_t>& spirv);
    void Destroy() noexcept;
    [[nodiscard]] bool Available() const noexcept { return mPipeline != VK_NULL_HANDLE; }
    [[nodiscard]] VkDescriptorSetLayout SetLayout() const noexcept { return mSetLayout; }
    [[nodiscard]] VkPipelineLayout PipelineLayout() const noexcept { return mPipelineLayout; }
    [[nodiscard]] VkPipeline Pipeline() const noexcept { return mPipeline; }
    [[nodiscard]] double CreationMilliseconds() const noexcept { return mMilliseconds; }

  private:
    VkDevice mDevice = VK_NULL_HANDLE;
    VkDescriptorSetLayout mSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
    VkPipeline mPipeline = VK_NULL_HANDLE;
    double mMilliseconds = 0.0;
};

struct DiffuseGiGuideViews {
    VkImageView Depth = VK_NULL_HANDLE;    // native depth, sampled (depth aspect)
    VkImageView Geometry = VK_NULL_HANDLE; // R32G32B32A32_UINT
    VkImageView Surface = VK_NULL_HANDLE;  // R16G16B16A16_UNORM
    VkImageView Motion = VK_NULL_HANDLE;   // R16G16B16A16_SFLOAT
    VkImageView Normal = VK_NULL_HANDLE;   // M8: R8G8B8A8_UNORM normal guide (null: no shading normals)
};

// Records the adapter dispatch of one frame into the pass's input buffers.
// The guides must be in a shader-read layout; the pass's leading barrier
// orders these writes before its reads.
bool RecordDiffuseGiAdapter(VkDevice device, VkCommandBuffer command, const DiffuseGiAdapterKernel& kernel,
                            VkDescriptorSet set, const DiffuseGiGuideViews& guides,
                            const Renderer::DiffuseGi::DiffuseGiPass& pass, const DiffuseGiAdapterParams& params);

} // namespace Fast::Extension
#endif
