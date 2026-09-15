#include "GraphicsPipelineLibrariesVK.h"
#include <iostream>
#include <set>
#include <stdexcept>
#include <type_traits>

static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class T> static T Handle(uintptr_t id) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(id);
    else return static_cast<T>(id);
}
struct Driver {
    uintptr_t Next = 1;
    unsigned Parts = 0, Links = 0, Destroyed = 0;
    std::array<unsigned, 4> PerPart{};
    std::set<VkPipeline> Live;
} driver;
static VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice, VkPipelineCache, uint32_t count,
    const VkGraphicsPipelineCreateInfo* info, const VkAllocationCallbacks*, VkPipeline* result) {
    Check(count == 1, "unexpected batch");
    const auto* chain = static_cast<const VkBaseInStructure*>(info->pNext);
    if (info->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR) {
        Check(chain && chain->sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
              "missing subset declaration");
        const auto subset = reinterpret_cast<const VkGraphicsPipelineLibraryCreateInfoEXT*>(chain)->flags;
        const auto part = subset == 1 ? 0U : subset == 2 ? 1U : subset == 4 ? 2U : 3U;
        ++driver.PerPart[part]; ++driver.Parts;
        Check((info->stageCount == 1) == (part == 1 || part == 2), "wrong shader-bearing subset");
    } else {
        Check(chain && chain->sType == VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR,
              "monolithic call escaped subset factory");
        const auto& link = *reinterpret_cast<const VkPipelineLibraryCreateInfoKHR*>(chain);
        Check(link.libraryCount == 4, "missing link input");
        for (uint32_t i = 0; i < link.libraryCount; ++i)
            Check(driver.Live.count(link.pLibraries[i]) != 0, "retired library used for linking");
        ++driver.Links;
    }
    *result = Handle<VkPipeline>(driver.Next++);
    driver.Live.insert(*result);
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL Destroy(VkDevice, VkPipeline pipeline, const VkAllocationCallbacks*) {
    Check(driver.Live.erase(pipeline) == 1, "pipeline freed twice"); ++driver.Destroyed;
}
int main() try {
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState color{};
    bs.attachmentCount = 1; bs.pAttachments = &color;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &format;
    const VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamicStates;
    std::array<uint32_t, 4> vertex{1, 2, 3, 4}, fragment{5, 6, 7, 8};
    triaevum_nri::ProgramBytes programs[]{{vertex.data(), sizeof(vertex)}, {fragment.data(), sizeof(fragment)}};
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto& stage : stages) {
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkGraphicsPipelineCreateInfo input{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    input.pNext = &rendering; input.stageCount = 2; input.pStages = stages;
    input.pVertexInputState = &vi; input.pInputAssemblyState = &ia; input.pViewportState = &vp;
    input.pRasterizationState = &rs; input.pMultisampleState = &ms; input.pDepthStencilState = &ds;
    input.pColorBlendState = &bs; input.pDynamicState = &dynamic;
    input.layout = Handle<VkPipelineLayout>(1000);
    triaevum_nri::GraphicsPipelineLibraries pool;
    const auto build = [&] {
        VkPipeline out{};
        Check(pool.Create({}, Create, Destroy, nullptr, {}, input, programs, &out) == VK_SUCCESS,
              "valid library request failed");
        Destroy({}, out, nullptr);
    };
    build(); Check(driver.Parts == 4 && driver.Links == 1, "initial library build incorrect");
    build(); Check(driver.Parts == 4 && driver.Links == 2, "identical state rebuilt shader libraries");
    color.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    build(); Check(driver.Parts == 5 && driver.PerPart[3] == 2, "output change recompiled shader stage");
    ds.depthWriteEnable = true;
    build(); Check(driver.Parts == 6 && driver.PerPart[2] == 2, "depth state not owned by fragment library");
    auto vertexCopy = vertex; programs[0].Data = vertexCopy.data();
    build(); Check(driver.Parts == 6, "equal program at another address created a library");
    vertexCopy[0] = 9;
    build(); Check(driver.Parts == 7 && driver.PerPart[1] == 2, "changed vertex bytes not isolated");
    VkPipeline out{};
    VkBaseInStructure unknown{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr};
    rendering.pNext = &unknown;
    Check(pool.Create({}, Create, Destroy, nullptr, {}, input, programs, &out) == VK_ERROR_FEATURE_NOT_PRESENT,
          "unknown extension state silently dropped");
    rendering.pNext = nullptr;
    const auto partsBefore = driver.Parts;
    for (unsigned mask = 1; mask < 150; ++mask) {
        color.colorWriteMask = mask; // Mock state identities stress retention, not Vulkan valid usage.
        build();
    }
    Check(driver.Parts > partsBefore && driver.Live.size() <= 134, "retained parts are not bounded");
    pool.Clear({}, Destroy, nullptr);
    Check(driver.Live.empty(), "layout teardown leaked library handles");
    std::cout << "pipeline parts: exact state isolation, program identity, reuse, rejection and retirement passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
