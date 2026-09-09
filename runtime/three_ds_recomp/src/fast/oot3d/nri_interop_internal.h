#pragma once

#ifdef ENABLE_OOT3D_NRI

#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"

#include <NRI.h>

namespace Fast::Oot3d {

struct NriTextureTransitionDesc {
    VkImage Image = VK_NULL_HANDLE;
    ResourceTransition Transition{};
    uint32_t MipOffset = 0;
    uint32_t MipNum = 0;
    // Semaphore wait scopes must participate in the transition's source scope,
    // even when the old contents are discarded (UNDEFINED has no access stage).
    nri::StageBits ExternalWaitStages = nri::StageBits::NONE;
};

// Narrow internal bridge used by renderer passes that record NRI commands on
// Vulkan command buffers already wrapped by NriInteropContext. This keeps NRI
// types out of the renderer's public API.
class NriInteropAccess final {
  public:
    static nri::Device* Device(NriInteropContext& context);
    static nri::CoreInterface* Core(NriInteropContext& context);
    static nri::CommandBuffer* CommandBuffer(NriInteropContext& context,
                                              uint32_t frameIndex);
    static nri::Descriptor* TextureView(NriInteropContext& context,
                                        VkImage image, bool storage);
    static nri::Texture* Texture(NriInteropContext& context,
                                 VkImage image);
    static nri::Buffer* Buffer(NriInteropContext& context,
                               VkBuffer buffer);
    static nri::Descriptor* ColorAttachmentView(
        NriInteropContext& context, VkImage image);
    static nri::Descriptor* DepthAttachmentView(
        NriInteropContext& context, VkImage image);
    static nri::Descriptor* CreateTextureView(NriInteropContext& context,
                                              VkImage image, bool storage,
                                              uint32_t mipOffset,
                                              uint32_t mipNum = 1);
    static bool DestroyTextureView(NriInteropContext& context,
                                   VkImage image,
                                   nri::Descriptor* descriptor);
    static bool CmdTextureBarrier(NriInteropContext& context,
                                  uint32_t frameIndex, VkImage image,
                                  const ResourceTransition& transition,
                                  uint32_t mipOffset = 0,
                                  uint32_t mipNum = 0);
    static bool CmdTextureBarriers(
        NriInteropContext& context, uint32_t frameIndex,
        const NriTextureTransitionDesc* transitions,
        uint32_t transitionNum);
    static bool CmdGlobalBarrier(
        NriInteropContext& context, uint32_t frameIndex,
        nri::AccessStage before, nri::AccessStage after);
    static nri::Pipeline* WrapGraphicsPipeline(
        NriInteropContext& context, VkPipeline pipeline);
    static void DestroyPipelineWrapper(
        NriInteropContext& context, nri::Pipeline* pipeline);
    static bool CmdSetPipeline(NriInteropContext& context,
                               uint32_t frameIndex,
                               nri::Pipeline* pipeline);
};

} // namespace Fast::Oot3d

#endif
