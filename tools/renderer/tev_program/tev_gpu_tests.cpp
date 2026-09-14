#include "fast/renderer3ds/pica_tev_program.h"
#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

using namespace Fast::Renderer3ds;
using Color = std::array<float, 4>;
struct alignas(16) Inputs {
    Color Primary, PrimaryFragment, SecondaryFragment;
    std::array<Color, 4> Textures;
    std::array<Color, 6> Constants;
    Color BufferColor;
};
struct Case { PicaTevProgram Program; Inputs Input; };
static_assert(sizeof(Inputs) == 224 && sizeof(Case) == 336);

#include "vulkan_test_device.h"
float Round(float value) { return std::floor(value * 255.0F + 0.5F) * (1.0F / 255.0F); }
float Saturate(float value) { return std::clamp(value, 0.0F, 1.0F); }

// Scalar test oracle; no generated GLSL or production arithmetic helpers.
Color Reference(const Case& test) {
    Color previous{}, buffer{}, pending = test.Input.BufferColor;
    const auto& input = test.Input;
    for (size_t stage = 0; stage < 6; ++stage) {
        const auto& words = test.Program.Stages[stage];
        const auto source = [&](unsigned selector) {
            if (selector == 0) {
                Color result = input.Primary;
                for (auto& v : result) v = Round(v);
                return result;
            }
            if (selector == 1) return input.PrimaryFragment;
            if (selector == 2) return input.SecondaryFragment;
            if (selector <= 6) return input.Textures.at(selector - 3);
            if (selector == 13) return buffer;
            if (selector == 14) return input.Constants[stage];
            return previous;
        };
        std::array<Color, 3> colors{};
        std::array<float, 3> alphas{};
        for (unsigned i = 0; i < 3; ++i) {
            auto rgb = (words[0] >> (i * 4)) & 15;
            auto alpha = (words[0] >> (16 + i * 4)) & 15;
            if (!stage && i < 2) {
                if (rgb == 15) rgb = (words[0] >> 8) & 15;
                if (alpha == 15) alpha = (words[0] >> 24) & 15;
            }
            auto cv = source(rgb);
            const auto av = source(alpha);
            const auto modifier = (words[1] >> (i * 4)) & 15;
            const auto lane = modifier & ~1U;
            for (unsigned c = 0; c < 3; ++c) {
                float v = cv[lane == 2 ? 3 : lane == 4 ? 0 : lane == 8 ? 1 : lane == 12 ? 2 : c];
                colors[i][c] = modifier & 1 ? 1.0F - v : v;
            }
            const auto aop = (words[1] >> (12 + i * 4)) & 7;
            float v = av[aop < 2 ? 3 : (aop / 2 - 1)];
            alphas[i] = aop & 1 ? 1.0F - v : v;
        }
        const auto operation = [](unsigned op, float a, float b, float c) {
            switch (op) {
            case 0: return a;
            case 1: return a * b;
            case 2: return a + b;
            case 3: return a + b - 0.5F;
            case 4: return b * (1.0F - c) + a * c;
            case 5: return a - b;
            case 8: return std::fma(a, b, c);
            case 9: return std::min(a + b, 1.0F) * c;
            default: throw std::runtime_error("invalid scalar operation");
            }
        };
        Color next{};
        const auto op = words[2] & 15;
        if (op == 6 || op == 7) {
            float dot = 0;
            for (unsigned c = 0; c < 3; ++c) dot += (colors[0][c] - 0.5F) * (colors[1][c] - 0.5F);
            next.fill(Round(Saturate(dot * 4.0F)));
        } else {
            for (unsigned c = 0; c < 3; ++c)
                next[c] = Round(Saturate(operation(op, colors[0][c], colors[1][c], colors[2][c])));
        }
        if (op != 7)
            next[3] = Round(Saturate(operation((words[2] >> 16) & 15, alphas[0], alphas[1], alphas[2])));
        for (unsigned c = 0; c < 4; ++c) {
            const auto scale = (words[3] >> (c == 3 ? 16 : 0)) & 3;
            next[c] = Saturate(next[c] * float(scale == 3 ? 1 : 1 << scale));
        }
        previous = next;
        buffer = pending;
        if (stage < 4) {
            for (unsigned c = 0; c < 4; ++c)
                if (test.Program.Control[0] & (1U << (stage + (c == 3 ? 12 : 8)))) pending[c] = next[c];
        }
    }
    return previous;
}

std::vector<Case> Cases() {
    std::mt19937 random(0x3D7E);
    constexpr unsigned sources[]{0, 1, 2, 3, 4, 5, 6, 13, 14, 15};
    constexpr unsigned operands[]{0, 1, 2, 3, 4, 5, 8, 9, 12, 13};
    constexpr unsigned alphaOps[]{0, 1, 2, 3, 4, 5, 8, 9};
    std::vector<Case> tests(4096);
    for (auto& test : tests) {
        std::array<uint32_t, 0x300> registers{};
        constexpr unsigned bases[]{0xC0, 0xC8, 0xD0, 0xD8, 0xF0, 0xF8};
        for (auto base : bases) {
            for (unsigned i = 0; i < 3; ++i) {
                registers[base] |= sources[random() % 10] << (i * 4);
                registers[base] |= sources[random() % 10] << (16 + i * 4);
                registers[base + 1] |= operands[random() % 10] << (i * 4);
                registers[base + 1] |= (random() % 8) << (12 + i * 4);
            }
            registers[base + 2] = (random() % 10) | (alphaOps[random() % 8] << 16);
            registers[base + 4] = (random() % 4) | ((random() % 4) << 16);
        }
        registers[0xE0] = random();
        Require(DecodePicaTevProgram(registers, test.Program) == PicaTevDecodeError::None, "valid program rejected");
        auto color = [&] { Color c; for (auto& f : c) f = float(random() % 256) / 255.0F; return c; };
        test.Input.Primary = color();
        test.Input.PrimaryFragment = color();
        test.Input.SecondaryFragment = color();
        for (auto& c : test.Input.Textures) c = color();
        for (auto& c : test.Input.Constants) c = color();
        test.Input.BufferColor = color();
    }
    PicaTevProgram rejected;
    std::array<uint32_t, 0x100> registers{};
    Require(DecodePicaTevProgram({}, rejected) == PicaTevDecodeError::TruncatedRegisters, "truncation accepted");
    registers[0xC0] = 7;
    Require(DecodePicaTevProgram(registers, rejected) == PicaTevDecodeError::Source, "invalid source accepted");
    registers[0xC0] = 0; registers[0xC1] = 6;
    Require(DecodePicaTevProgram(registers, rejected) == PicaTevDecodeError::ColorOperand, "invalid operand accepted");
    registers[0xC1] = 0; registers[0xC2] = 10;
    Require(DecodePicaTevProgram(registers, rejected) == PicaTevDecodeError::ColorOperation, "invalid RGB op accepted");
    registers[0xC2] = 6 << 16;
    Require(DecodePicaTevProgram(registers, rejected) == PicaTevDecodeError::AlphaOperation, "invalid alpha op accepted");
    // Prefix fixtures retain a chained half-byte rounding regression.
    for (size_t prefix = 0; prefix < 6; ++prefix) {
        auto& probe = tests[4090 + prefix];
        probe = tests[19];
        for (size_t stage = prefix + 1; stage < 6; ++stage)
            probe.Program.Stages[stage] = {0x000F000F, 0, 0, 0};
    }
    return tests;
}


#include "canonical_tev.h"

std::vector<Color> RunGpu(const std::vector<Case>& tests, const std::vector<Color>* canonicalExpected = nullptr) {
    Gpu gpu;
    const auto device = gpu.Device;
    constexpr uint32_t width = 256, height = 16;
    Require(tests.size() == width * height, "unexpected test grid");
    auto input = gpu.MakeBuffer(tests.size() * sizeof(Case), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto output = gpu.MakeBuffer(tests.size() * sizeof(Color), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    std::memcpy(input.Mapped, tests.data(), tests.size() * sizeof(Case));
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; li.bindingCount = 1; li.pBindings = &binding;
    VkDescriptorSetLayout layout{}; Check(vkCreateDescriptorSetLayout(device, &li, nullptr, &layout));
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pi.maxSets = 1; pi.poolSizeCount = 1; pi.pPoolSizes = &poolSize;
    VkDescriptorPool pool{}; Check(vkCreateDescriptorPool(device, &pi, nullptr, &pool));
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; ai.descriptorPool = pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &layout;
    VkDescriptorSet set{}; Check(vkAllocateDescriptorSets(device, &ai, &set));
    VkDescriptorBufferInfo bi{input.Handle, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; write.dstSet = set; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo = &bi;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.setLayoutCount = 1; pli.pSetLayouts = &layout;
    VkPipelineLayout pipelineLayout{}; Check(vkCreatePipelineLayout(device, &pli, nullptr, &pipelineLayout));
    constexpr auto format = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType = VK_IMAGE_TYPE_2D; ii.format = format; ii.extent = {width, height, 1}; ii.mipLevels = 1; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImage image{}; Check(vkCreateImage(device, &ii, nullptr, &image));
    VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(device, image, &mr);
    VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mi.allocationSize = mr.size; mi.memoryTypeIndex = gpu.Type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory imageMemory{}; Check(vkAllocateMemory(device, &mi, nullptr, &imageMemory)); Check(vkBindImageMemory(device, image, imageMemory, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view{}; Check(vkCreateImageView(device, &vi, nullptr, &view));
    VkAttachmentDescription attachment{}; attachment.format = format; attachment.samples = VK_SAMPLE_COUNT_1_BIT; attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &ref;
    VkSubpassDependency dependency{0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0};
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; ri.attachmentCount = 1; ri.pAttachments = &attachment; ri.subpassCount = 1; ri.pSubpasses = &subpass; ri.dependencyCount = 1; ri.pDependencies = &dependency;
    VkRenderPass renderPass{}; Check(vkCreateRenderPass(device, &ri, nullptr, &renderPass));
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fi.renderPass = renderPass; fi.attachmentCount = 1; fi.pAttachments = &view; fi.width = width; fi.height = height; fi.layers = 1;
    VkFramebuffer framebuffer{}; Check(vkCreateFramebuffer(device, &fi, nullptr, &framebuffer));
    auto vs = gpu.Shader("#version 450\nvoid main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);gl_Position=vec4(p*2.0-1.0,0.0,1.0);}", shaderc_vertex_shader);
    auto fs = gpu.Shader(std::string("#version 450\n") + std::string(PicaTevProgramGlsl()) +
        (canonicalExpected ? CanonicalTev(tests) : std::string()) + R"(
struct TestCase { PicaTevProgram program; PicaTevInputs inputs; };
layout(set=0,binding=0,std430) readonly buffer Cases { TestCase tests[]; };
layout(location=0) out vec4 color;
)" + (canonicalExpected ?
        "void main(){uint i=uint(gl_FragCoord.y)*256u+uint(gl_FragCoord.x);color=canonical(i,tests[i].inputs);}" :
        "void main(){uint i=uint(gl_FragCoord.y)*256u+uint(gl_FragCoord.x);color=pica_evaluate_tev(tests[i].program,tests[i].inputs);}"), shaderc_fragment_shader);
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0, 0, float(width), float(height), 0, 1}; VkRect2D rect{{0,0},{width,height}};
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewportState.viewportCount = 1; viewportState.pViewports = &viewport; viewportState.scissorCount = 1; viewportState.pScissors = &rect;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{}; blend.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo blendState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blendState.attachmentCount = 1; blendState.pAttachments = &blend;
    VkGraphicsPipelineCreateInfo graphics{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; graphics.stageCount = 2; graphics.pStages = stages.data(); graphics.pVertexInputState = &vertex; graphics.pInputAssemblyState = &assembly; graphics.pViewportState = &viewportState; graphics.pRasterizationState = &raster; graphics.pMultisampleState = &samples; graphics.pColorBlendState = &blendState; graphics.layout = pipelineLayout; graphics.renderPass = renderPass;
    VkPipeline pipeline{}; Check(vkCreateGraphicsPipelines(device, {}, 1, &graphics, nullptr, &pipeline));
    VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; ci.queueFamilyIndex = gpu.Family;
    VkCommandPool commands{}; Check(vkCreateCommandPool(device, &ci, nullptr, &commands));
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool = commands; ca.commandBufferCount = 1;
    VkCommandBuffer cmd{}; Check(vkAllocateCommandBuffers(device, &ca, &cmd));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; Check(vkBeginCommandBuffer(cmd, &begin));
    VkClearValue clear{};
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; rb.renderPass = renderPass; rb.framebuffer = framebuffer; rb.renderArea = rect; rb.clearValueCount = 1; rb.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE); vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline); vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set, 0, nullptr); vkCmdDraw(cmd, 3, 1, 0, 0); vkCmdEndRenderPass(cmd);
    VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, output.Handle, 1, &copy);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    Check(vkEndCommandBuffer(cmd));
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence{}; Check(vkCreateFence(device, &fenceInfo, nullptr, &fence));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
    Check(vkQueueSubmit(gpu.Queue, 1, &submit, fence)); Check(vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ULL));
    const auto* pixels = static_cast<const Color*>(output.Mapped);
    unsigned failures = 0; float maxError = 0;
    std::ofstream capture(canonicalExpected ? "pica_tev_canonical_gpu.ppm" : "pica_tev_gpu.ppm", std::ios::binary);
    Require(capture.is_open(), "cannot open framebuffer capture");
    capture << "P6\n256 16\n255\n";
    for (size_t i = 0; i < tests.size(); ++i) {
        const auto expected = canonicalExpected ? canonicalExpected->at(i) : Reference(tests[i]);
        for (unsigned c = 0; c < 4; ++c) {
            const float error = std::abs(expected[c] - pixels[i][c]);
            maxError = std::max(error, maxError);
            if (!std::isfinite(pixels[i][c]) || error > 0.00001F) {
                if (failures < 8) std::cerr << "case " << i << " channel " << c << " expected " << expected[c] << " got " << pixels[i][c] << '\n';
                ++failures;
            }
            if (c < 3) capture.put(static_cast<char>(std::lround(Saturate(pixels[i][c]) * 255.0F)));
        }
    }
    capture.close();
    std::vector<Color> result(pixels, pixels + tests.size());
    std::cout << (canonicalExpected ? "canonical GPU comparison: " : "scalar comparison: ");
    std::cout << "cases=" << tests.size() << " channels=" << tests.size()*4 << " mismatches=" << failures << " max_error=" << maxError << " fragment_modules=1 pipelines=1 draws=1\n";
    vkDestroyFence(device, fence, nullptr); vkDestroyCommandPool(device, commands, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr); vkDestroyShaderModule(device, vs, nullptr); vkDestroyShaderModule(device, fs, nullptr);
    vkDestroyFramebuffer(device, framebuffer, nullptr); vkDestroyRenderPass(device, renderPass, nullptr);
    vkDestroyImageView(device, view, nullptr); vkDestroyImage(device, image, nullptr); vkFreeMemory(device, imageMemory, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr); vkDestroyDescriptorPool(device, pool, nullptr); vkDestroyDescriptorSetLayout(device, layout, nullptr);
    gpu.Free(input); gpu.Free(output);
    Require(failures == 0, canonicalExpected ? "canonical and parametric TEV framebuffers differ" : "TEV framebuffer differs from scalar oracle");
    return result;
}

int main() {
    try {
        auto tests = Cases();
        RunGpu(tests);
        for (size_t i = 64; i < tests.size(); ++i) tests[i].Program = tests[i % 64].Program;
        const auto dynamic = RunGpu(tests);
        RunGpu(tests, &dynamic);
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
