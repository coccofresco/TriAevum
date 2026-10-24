#include "fast/extension/pica_nri_pipeline_state.h"
#include "vulkan_test_device.h"
#include <array>
#include <cmath>

// A black additive fragment with alpha one is a native RGB no-op. It must
// not erase the underlying scene's AO/outline coverage across its quad.
int main() {
  try {
    VkPhysicalDeviceFeatures features{};
    features.independentBlend = VK_TRUE;
    Gpu gpu(features);
    const auto device = gpu.Device;
    constexpr uint32_t width = 3, height = 3;
    constexpr auto format = VK_FORMAT_R32G32B32A32_SFLOAT;
    std::array<VkImage, 2> images{};
    std::array<VkDeviceMemory, 2> memory{};
    std::array<VkImageView, 2> views{};
    std::array<VkAttachmentDescription, 2> attachments{};
    for (size_t i = 0; i < images.size(); ++i) {
      VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      image.imageType = VK_IMAGE_TYPE_2D;
      image.format = format;
      image.extent = {width, height, 1};
      image.mipLevels = image.arrayLayers = 1;
      image.samples = VK_SAMPLE_COUNT_1_BIT;
      image.usage =
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      Check(vkCreateImage(device, &image, nullptr, &images[i]));
      VkMemoryRequirements requirements{};
      vkGetImageMemoryRequirements(device, images[i], &requirements);
      VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      allocation.allocationSize = requirements.size;
      allocation.memoryTypeIndex = gpu.Type(
          requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      Check(vkAllocateMemory(device, &allocation, nullptr, &memory[i]));
      Check(vkBindImageMemory(device, images[i], memory[i], 0));
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view.image = images[i];
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = format;
      view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      Check(vkCreateImageView(device, &view, nullptr, &views[i]));
      attachments[i].format = format;
      attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
      attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      attachments[i].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    std::array<VkAttachmentReference, 5> references{};
    for (auto &ref : references)
      ref = {VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    references[0].attachment = 0;
    references[4].attachment = 1;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = references.size();
    subpass.pColorAttachments = references.data();
    VkSubpassDependency dependency{
        0,
        VK_SUBPASS_EXTERNAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        0};
    VkRenderPassCreateInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    render.attachmentCount = attachments.size();
    render.pAttachments = attachments.data();
    render.subpassCount = 1;
    render.pSubpasses = &subpass;
    render.dependencyCount = 1;
    render.pDependencies = &dependency;
    VkRenderPass renderPass{};
    Check(vkCreateRenderPass(device, &render, nullptr, &renderPass));
    VkFramebufferCreateInfo framebufferInfo{
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = views.size();
    framebufferInfo.pAttachments = views.data();
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;
    VkFramebuffer framebuffer{};
    Check(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer));
    VkPipelineLayoutCreateInfo layoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout layout{};
    Check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout));
    const auto vs = gpu.Shader("#version 450\nvoid main(){vec2 "
                               "p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);"
                               "gl_Position=vec4(p*2-1,0,1);}",
                               shaderc_vertex_shader);
    const auto fs = gpu.Shader(R"glsl(#version 450
layout(location=0) out vec4 native_color;
layout(location=4) out vec4 scene_guide;
void main() {
    float alpha=(gl_FragCoord.x-0.5)*0.5;
    native_color=vec4(0.0,0.0,0.0,alpha);
    scene_guide=vec4(1.0,1.0,1.0,alpha);
})glsl",
                               shaderc_fragment_shader);
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{
        {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
          VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr},
         {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
          VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr}}};
    VkPipelineVertexInputStateCreateInfo vertex{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0, 0, float(width), float(height), 0, 1};
    VkPipelineViewportStateCreateInfo viewportInfo{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportInfo.viewportCount = viewportInfo.scissorCount = 1;
    viewportInfo.pViewports = &viewport;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    const VkDynamicState dynamicScissor = VK_DYNAMIC_STATE_SCISSOR;
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 1;
    dynamic.pDynamicStates = &dynamicScissor;
    std::array<VkPipeline, 3> pipelines{};
    using Factor = Fast::Renderer3ds::NativeBlendFactor;
    const std::array destination{Factor::One, Factor::OneMinusSourceAlpha,
                                 Factor::Zero};
    for (size_t i = 0; i < pipelines.size(); ++i) {
      Fast::Renderer3ds::PicaDrawView draw;
      draw.ColorWriteMask = 15;
      draw.Blend.Enabled = true;
      draw.Blend.SourceRgb = i == 2 ? Factor::One : Factor::SourceAlpha;
      draw.Blend.DestRgb = destination[i];
      Fast::Extension::PicaFragmentInstrumentationOutputLayout outputs;
      outputs.WritesAmbientGuide = outputs.SceneDomainAmbientOverlay =
          outputs.SceneDomainBlendedOverlay = true;
      const auto state = Fast::Extension::BuildPicaNriPipelineState(
          draw, outputs, {Fast::Renderer3ds::kAllPicaAuxiliaryOutputs},
          VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_D32_SFLOAT);
      VkPipelineColorBlendStateCreateInfo blend{
          VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
      blend.attachmentCount = references.size();
      blend.pAttachments = state.Colors.data();
      VkGraphicsPipelineCreateInfo pipeline{
          VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
      pipeline.stageCount = stages.size();
      pipeline.pStages = stages.data();
      pipeline.pVertexInputState = &vertex;
      pipeline.pInputAssemblyState = &assembly;
      pipeline.pViewportState = &viewportInfo;
      pipeline.pRasterizationState = &raster;
      pipeline.pMultisampleState = &samples;
      pipeline.pColorBlendState = &blend;
      pipeline.pDynamicState = &dynamic;
      pipeline.layout = layout;
      pipeline.renderPass = renderPass;
      Check(vkCreateGraphicsPipelines(device, {}, 1, &pipeline, nullptr,
                                      &pipelines[i]));
    }
    auto readback = gpu.MakeBuffer(width * height * 4 * sizeof(float) * 2,
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkCommandPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = gpu.Family;
    VkCommandPool pool{};
    Check(vkCreateCommandPool(device, &poolInfo, nullptr, &pool));
    VkCommandBufferAllocateInfo commandInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = pool;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer command{};
    Check(vkAllocateCommandBuffers(device, &commandInfo, &command));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    Check(vkBeginCommandBuffer(command, &begin));
    const std::array<VkClearValue, 2> clear{
        {{{.25F, .5F, .75F, 1}}, {{0, 0, 0, 1}}}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = renderPass;
    pass.framebuffer = framebuffer;
    pass.renderArea = {{0, 0}, {width, height}};
    pass.clearValueCount = clear.size();
    pass.pClearValues = clear.data();
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    for (uint32_t row = 0; row < pipelines.size(); ++row) {
      VkRect2D scissor{{0, int32_t(row)}, {width, 1}};
      vkCmdSetScissor(command, 0, 1, &scissor);
      vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipelines[row]);
      vkCmdDraw(command, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(command);
    for (size_t i = 0; i < images.size(); ++i) {
      VkBufferImageCopy copy{};
      copy.bufferOffset = i * width * height * 4 * sizeof(float);
      copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      copy.imageExtent = {width, height, 1};
      vkCmdCopyImageToBuffer(command, images[i],
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             readback.Handle, 1, &copy);
    }
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr,
                         0, nullptr);
    Check(vkEndCommandBuffer(command));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    Check(vkQueueSubmit(gpu.Queue, 1, &submit, VK_NULL_HANDLE));
    Check(vkQueueWaitIdle(gpu.Queue));
    const auto *pixels =
        static_cast<const std::array<float, 4> *>(readback.Mapped);
    unsigned failures = 0;
    for (uint32_t row = 0; row < height; ++row)
      for (uint32_t x = 0; x < width; ++x) {
        const float transmission = row == 0   ? 1.0F
                                   : row == 1 ? 1.0F - x * .5F
                                              : 0.0F;
        const size_t index = row * width + x;
        for (size_t channel = 0; channel < 3; ++channel)
          Require(std::abs(pixels[index][channel] -
                           (channel + 1) * .25F * transmission) < 1e-6F,
                  "Native RGB blend changed");
        if (std::abs(pixels[width * height + index][3] - transmission) >
            1e-6F) {
          std::cerr << "row=" << row << " alpha=" << x * .5F
                    << " expected coverage=" << transmission
                    << " actual=" << pixels[width * height + index][3] << '\n';
          ++failures;
        }
      }
    vkDestroyCommandPool(device, pool, nullptr);
    gpu.Free(readback);
    for (auto pipeline : pipelines)
      vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    for (size_t i = 0; i < images.size(); ++i) {
      vkDestroyImageView(device, views[i], nullptr);
      vkDestroyImage(device, images[i], nullptr);
      vkFreeMemory(device, memory[i], nullptr);
    }
    Require(failures == 0, "Native overlay erased or leaked scene coverage");
    std::cout << "Scene coverage GPU: additive preservation, alpha "
                 "transmission, opaque replacement and native RGB passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
