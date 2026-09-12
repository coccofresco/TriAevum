#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#if !defined(ENABLE_RENDERER3DS_VULKAN)
#define ENABLE_RENDERER3DS_VULKAN
#define FAST_OOT3D_UNDEFINE_RENDERER3DS_VULKAN
#endif
#if defined(ENABLE_OOT3D_NRI) && !defined(ENABLE_RENDERER3DS_NRI)
#define ENABLE_RENDERER3DS_NRI
#define FAST_OOT3D_UNDEFINE_RENDERER3DS_NRI
#endif

#include "fast/renderer3ds/nri_pica_pipeline_bridge.h"

#ifdef FAST_OOT3D_UNDEFINE_RENDERER3DS_NRI
#undef ENABLE_RENDERER3DS_NRI
#undef FAST_OOT3D_UNDEFINE_RENDERER3DS_NRI
#endif
#ifdef FAST_OOT3D_UNDEFINE_RENDERER3DS_VULKAN
#undef ENABLE_RENDERER3DS_VULKAN
#undef FAST_OOT3D_UNDEFINE_RENDERER3DS_VULKAN
#endif

#include <memory>

namespace Fast::Oot3d {

class NriInteropContext;

using Renderer3ds::NriPicaGraphicsPipelineDesc;
using Renderer3ds::NriPicaOwnedDrawDesc;
using Renderer3ds::NriPicaTextureBindingDesc;
using Renderer3ds::NriPicaUniformBindingDesc;
using Renderer3ds::NriPicaVertexBufferBindingDesc;

// Compatibility adapter for the existing Vulkan backend. All PICA execution
// mechanics live in the title-neutral Renderer3ds implementation.
class NriPicaPipelineBridge final {
  public:
    NriPicaPipelineBridge();
    ~NriPicaPipelineBridge();
    NriPicaPipelineBridge(const NriPicaPipelineBridge&) = delete;
    NriPicaPipelineBridge& operator=(const NriPicaPipelineBridge&) = delete;

    bool Initialize(NriInteropContext& interop);
    bool InitializePipelineCache(std::span<const uint8_t> data = {});
    [[nodiscard]] std::vector<uint8_t> GetPipelineCacheData() const;
    [[nodiscard]] Renderer3ds::NriPicaPipelineStatistics PipelineStatistics() const;
    bool CreateOwnedPipeline(
        VkPipeline fallbackPipeline,
        const NriPicaGraphicsPipelineDesc& desc);
    bool BindOwnedDraw(const NriPicaOwnedDrawDesc& desc);
    bool DrawBoundGeometry(const NriPicaOwnedDrawDesc& desc);
    bool Bind(uint32_t frameIndex, VkPipeline pipeline);
    void Forget(VkPipeline pipeline);
    void Reset();
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] bool DescriptorLayoutOwnedByNri() const;
    [[nodiscard]] bool DescriptorsOwnedByNri() const;
    [[nodiscard]] bool OwnedDrawsEnabled() const;
    [[nodiscard]] bool LastDrawUploadsOwnedByNri() const;
    [[nodiscard]] uint64_t LastDrawUploadedBytes() const;
    [[nodiscard]] bool OwnedPipelineReady(VkPipeline fallbackPipeline) const;
    [[nodiscard]] size_t OwnedPipelineCount() const;
    [[nodiscard]] size_t WrappedPipelineCount() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    class InteropAdapter;
    std::unique_ptr<InteropAdapter> mInteropAdapter;
    Renderer3ds::NriPicaPipelineBridge mBridge;
};

} // namespace Fast::Oot3d

#endif
