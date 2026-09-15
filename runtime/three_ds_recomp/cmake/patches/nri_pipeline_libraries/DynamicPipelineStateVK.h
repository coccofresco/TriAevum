// TriAevum: pipeline-owned native values for Vulkan dynamic raster/depth state.
#pragma once
#include <vulkan/vulkan.h>
#include <array>

namespace triaevum_nri {
struct DynamicPipelineState {
    bool Enabled = false;
    VkCullModeFlags Cull = 0;
    VkFrontFace Front = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineDepthStencilStateCreateInfo Depth{};
    PFN_vkCmdSetCullModeEXT CullMode = nullptr;
    PFN_vkCmdSetFrontFaceEXT FrontFace = nullptr;
    PFN_vkCmdSetDepthTestEnableEXT DepthTest = nullptr;
    PFN_vkCmdSetDepthWriteEnableEXT DepthWrite = nullptr;
    PFN_vkCmdSetDepthCompareOpEXT DepthCompare = nullptr;
    PFN_vkCmdSetDepthBoundsTestEnableEXT DepthBoundsTest = nullptr;
    PFN_vkCmdSetStencilTestEnableEXT StencilTest = nullptr;
    PFN_vkCmdSetStencilOpEXT StencilOp = nullptr;
    PFN_vkCmdSetStencilCompareMask StencilCompareMask = nullptr;
    PFN_vkCmdSetStencilWriteMask StencilWriteMask = nullptr;
    PFN_vkCmdSetStencilReference StencilReference = nullptr;

    static constexpr std::array<VkDynamicState, 11> States{
        VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,
        VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE};

    bool Capture(VkDevice device, PFN_vkGetDeviceProcAddr get,
                 const VkPipelineRasterizationStateCreateInfo& raster,
                 const VkPipelineDepthStencilStateCreateInfo& depth) {
#define TRIAEVUM_LOAD(field, type, name) field = reinterpret_cast<type>(get(device, name))
        TRIAEVUM_LOAD(CullMode, PFN_vkCmdSetCullModeEXT, "vkCmdSetCullModeEXT");
        TRIAEVUM_LOAD(FrontFace, PFN_vkCmdSetFrontFaceEXT, "vkCmdSetFrontFaceEXT");
        TRIAEVUM_LOAD(DepthTest, PFN_vkCmdSetDepthTestEnableEXT, "vkCmdSetDepthTestEnableEXT");
        TRIAEVUM_LOAD(DepthWrite, PFN_vkCmdSetDepthWriteEnableEXT, "vkCmdSetDepthWriteEnableEXT");
        TRIAEVUM_LOAD(DepthCompare, PFN_vkCmdSetDepthCompareOpEXT, "vkCmdSetDepthCompareOpEXT");
        TRIAEVUM_LOAD(DepthBoundsTest, PFN_vkCmdSetDepthBoundsTestEnableEXT, "vkCmdSetDepthBoundsTestEnableEXT");
        TRIAEVUM_LOAD(StencilTest, PFN_vkCmdSetStencilTestEnableEXT, "vkCmdSetStencilTestEnableEXT");
        TRIAEVUM_LOAD(StencilOp, PFN_vkCmdSetStencilOpEXT, "vkCmdSetStencilOpEXT");
        TRIAEVUM_LOAD(StencilCompareMask, PFN_vkCmdSetStencilCompareMask, "vkCmdSetStencilCompareMask");
        TRIAEVUM_LOAD(StencilWriteMask, PFN_vkCmdSetStencilWriteMask, "vkCmdSetStencilWriteMask");
        TRIAEVUM_LOAD(StencilReference, PFN_vkCmdSetStencilReference, "vkCmdSetStencilReference");
#undef TRIAEVUM_LOAD
        Enabled = CullMode && FrontFace && DepthTest && DepthWrite && DepthCompare &&
            DepthBoundsTest && StencilTest && StencilOp && StencilCompareMask && StencilWriteMask && StencilReference;
        Cull = raster.cullMode; Front = raster.frontFace; Depth = depth; Depth.pNext = nullptr;
        return Enabled;
    }

    void Apply(VkCommandBuffer command) const {
        if (!Enabled) return;
        CullMode(command, Cull); FrontFace(command, Front);
        DepthTest(command, Depth.depthTestEnable); DepthWrite(command, Depth.depthWriteEnable);
        DepthCompare(command, Depth.depthCompareOp); DepthBoundsTest(command, Depth.depthBoundsTestEnable);
        StencilTest(command, Depth.stencilTestEnable);
        const auto face = [&](VkStencilFaceFlags flags, const VkStencilOpState& s) {
            StencilOp(command, flags, s.failOp, s.passOp, s.depthFailOp, s.compareOp);
            StencilCompareMask(command, flags, s.compareMask);
            StencilWriteMask(command, flags, s.writeMask);
            StencilReference(command, flags, s.reference);
        };
        face(VK_STENCIL_FACE_FRONT_BIT, Depth.front);
        face(VK_STENCIL_FACE_BACK_BIT, Depth.back);
    }
};
} // namespace triaevum_nri
