#include "fast/extension/hiz_reflection.h"
#include "fast/extension/reflection_ibl.h"
#include "vulkan_test_device.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
struct Image {
  VkImage Handle{};
  VkImageView View{};
  VkDeviceMemory Memory{};
};
struct HiZPush {
  uint32_t Width, Height;
  float InvWidth, InvHeight, Strength, MaxDistance, Thickness, EdgeFade;
  uint32_t MaxSteps, Mips;
  float ProjectionX, ProjectionY, OffsetX, OffsetY, Near, Far, RoughnessBias,
      NormalSigma;
  uint32_t Orientation;
};
static_assert(sizeof(HiZPush) == 76);
struct ResolvePush {
  std::array<float, 16> InverseProjection{};
  uint32_t Width, Height;
  float RoughnessBias = 0;
  uint32_t Orientation = 0;
};
static_assert(sizeof(ResolvePush) == 80);
float Half(uint16_t bits) {
  const int exponent = (bits >> 10) & 31;
  const int fraction = bits & 1023;
  const float sign = bits & 32768 ? -1.0F : 1.0F;
  if (exponent == 31)
    return fraction ? std::numeric_limits<float>::quiet_NaN()
                    : sign * std::numeric_limits<float>::infinity();
  return sign * (exponent ? std::ldexp(1.0F + fraction / 1024.0F, exponent - 15)
                          : std::ldexp(float(fraction), -24));
}

// Adjacent bright pixels surround every zero-weight pixel. Applying gain before
// the bilateral filter, or using temporal alpha as coverage, fails this
// fixture.
struct Fixture {
  static constexpr uint32_t Width = 13, Height = 3, Count = Width * Height;
  Gpu Device{
      VkPhysicalDeviceFeatures{.shaderStorageImageExtendedFormats = VK_TRUE}};
  std::array<Image, 5> Images{};
  std::array<Gpu::Buffer, 4> Uploads{};
  Gpu::Buffer Result{};
  VkSampler Sampler{};
  VkDescriptorSetLayout SetLayout{};
  VkDescriptorPool Pool{};
  VkDescriptorSet Set{};
  VkPipelineLayout Layout{};
  VkPipeline Pipeline{};
  VkCommandPool Commands{};
  VkCommandBuffer Command{};
  bool Resolve;

  Image MakeImage(VkFormat format) {
    Image image;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {Width, Height, 1};
    info.mipLevels = info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    Check(vkCreateImage(Device.Device, &info, nullptr, &image.Handle));
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(Device.Device, image.Handle, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = Device.Type(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(Device.Device, &allocation, nullptr, &image.Memory));
    Check(vkBindImageMemory(Device.Device, image.Handle, image.Memory, 0));
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.Handle;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Check(vkCreateImageView(Device.Device, &view, nullptr, &image.View));
    return image;
  }

  explicit Fixture(bool resolve) : Resolve(resolve) {
    const std::array<VkFormat, 5> formats =
        resolve ? std::array<VkFormat, 5>{VK_FORMAT_R32G32B32A32_SFLOAT,
                                          VK_FORMAT_R8G8B8A8_UNORM,
                                          VK_FORMAT_R8G8B8A8_UNORM,
                                          VK_FORMAT_R32G32B32A32_SFLOAT,
                                          VK_FORMAT_R16G16B16A16_SFLOAT}
                : std::array<VkFormat, 5>{
                      VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32_SFLOAT,
                      VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                      VK_FORMAT_R16G16B16A16_SFLOAT};
    for (size_t i = 0; i < Images.size(); ++i) {
      Images[i] = MakeImage(formats[i]);
      if (i < Uploads.size())
        Uploads[i] = Device.MakeBuffer(
            Count * (formats[i] == VK_FORMAT_R32G32B32A32_SFLOAT ? 16 : 4),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    }
    Result = Device.MakeBuffer(Count * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Check(vkCreateSampler(Device.Device, &sampler, nullptr, &Sampler));
    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};
    const uint32_t bindingCount = resolve ? 6 : 7;
    for (uint32_t i = 0; i < bindingCount; ++i) {
      bindings[i] = {i,
                     i < 4    ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                     : i == 4 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                              : VK_DESCRIPTOR_TYPE_SAMPLER,
                     1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo setLayout{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayout.bindingCount = bindingCount;
    setLayout.pBindings = bindings.data();
    Check(vkCreateDescriptorSetLayout(Device.Device, &setLayout, nullptr,
                                      &SetLayout));
    const std::array<VkDescriptorPoolSize, 3> sizes{
        {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4},
         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
         {VK_DESCRIPTOR_TYPE_SAMPLER, 2}}};
    VkDescriptorPoolCreateInfo pool{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1;
    pool.poolSizeCount = uint32_t(sizes.size());
    pool.pPoolSizes = sizes.data();
    Check(vkCreateDescriptorPool(Device.Device, &pool, nullptr, &Pool));
    VkDescriptorSetAllocateInfo allocation{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = Pool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &SetLayout;
    Check(vkAllocateDescriptorSets(Device.Device, &allocation, &Set));
    std::array<VkDescriptorImageInfo, 7> images{};
    std::array<VkWriteDescriptorSet, 7> writes{};
    for (uint32_t i = 0; i < bindingCount; ++i) {
      images[i] = {i >= 5 ? Sampler : VK_NULL_HANDLE,
                   i < 5 ? Images[i].View : VK_NULL_HANDLE,
                   i == 4 ? VK_IMAGE_LAYOUT_GENERAL
                          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      writes[i].dstSet = Set;
      writes[i].dstBinding = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType = bindings[i].descriptorType;
      writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(Device.Device, bindingCount, writes.data(), 0,
                           nullptr);
    const VkPushConstantRange push{
        VK_SHADER_STAGE_COMPUTE_BIT, 0,
        uint32_t(resolve ? sizeof(ResolvePush) : sizeof(HiZPush))};
    VkPipelineLayoutCreateInfo layout{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &SetLayout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    Check(vkCreatePipelineLayout(Device.Device, &layout, nullptr, &Layout));
    const auto shader = Device.Shader(
        resolve ? Fast::Extension::BuildReflectionMaterialResolveComputeShader()
                : Fast::Extension::BuildHiZReflectionBilateralFilterShader(),
        shaderc_compute_shader);
    VkComputePipelineCreateInfo pipeline{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline.layout = Layout;
    pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                      nullptr,
                      0,
                      VK_SHADER_STAGE_COMPUTE_BIT,
                      shader,
                      "main",
                      nullptr};
    Check(vkCreateComputePipelines(Device.Device, VK_NULL_HANDLE, 1, &pipeline,
                                   nullptr, &Pipeline));
    vkDestroyShaderModule(Device.Device, shader, nullptr);
    VkCommandPoolCreateInfo commands{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commands.queueFamilyIndex = Device.Family;
    Check(vkCreateCommandPool(Device.Device, &commands, nullptr, &Commands));
    VkCommandBufferAllocateInfo command{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command.commandPool = Commands;
    command.commandBufferCount = 1;
    Check(vkAllocateCommandBuffers(Device.Device, &command, &Command));
  }

  void Barrier(size_t index, VkImageLayout oldLayout, VkImageLayout newLayout,
               VkAccessFlags from, VkAccessFlags to,
               VkPipelineStageFlags source, VkPipelineStageFlags destination) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = from;
    barrier.dstAccessMask = to;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    barrier.image = Images[index].Handle;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(Command, source, destination, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
  }

  void Run() {
    constexpr std::array<uint8_t, 4> weights{0, 127, 254, 64};
    for (uint32_t i = 0; i < Count; ++i) {
      static_cast<std::array<float, 4> *>(Uploads[0].Mapped)[i] = {0.25F, 0.5F,
                                                                   0.75F, 0.5F};
      static_cast<std::array<uint8_t, 4> *>(
          Uploads[Resolve ? 1 : 2].Mapped)[i] = {128, 128, 255, 255};
      // Temporal reactivity is deliberately full on every material, even weight
      // zero.
      static_cast<std::array<uint8_t, 4> *>(
          Uploads[Resolve ? 2 : 3].Mapped)[i] = {
          204, 128, weights[(i % Width) % weights.size()], 255};
      if (Resolve)
        static_cast<std::array<float, 4> *>(Uploads[3].Mapped)[i] = {
            0.75F, 0.125F, 0, 0};
      else
        static_cast<float *>(Uploads[1].Mapped)[i] = 30.0F;
    }
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    Check(vkBeginCommandBuffer(Command, &begin));
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {Width, Height, 1};
    for (size_t i = 0; i < Uploads.size(); ++i) {
      Barrier(i, VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT);
      vkCmdCopyBufferToImage(Command, Uploads[i].Handle, Images[i].Handle,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
      Barrier(i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    Barrier(4, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
            VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
    vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0,
                            1, &Set, 0, nullptr);
    if (Resolve) {
      ResolvePush push{{}, Width, Height};
      push.InverseProjection[0] = push.InverseProjection[5] =
          push.InverseProjection[10] = push.InverseProjection[15] = 1;
      vkCmdPushConstants(Command, Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                         sizeof(push), &push);
    } else {
      HiZPush push{};
      push.Width = Width;
      push.Height = Height;
      push.InvWidth = 1.0F / Width;
      push.InvHeight = 1.0F / Height;
      push.Thickness = push.NormalSigma = 1;
      vkCmdPushConstants(Command, Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                         sizeof(push), &push);
    }
    vkCmdDispatch(Command, (Width + 7) / 8, (Height + 7) / 8, 1);
    Barrier(4, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyImageToBuffer(Command, Images[4].Handle,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Result.Handle,
                           1, &copy);
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0,
                         nullptr);
    Check(vkEndCommandBuffer(Command));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &Command;
    Check(vkQueueSubmit(Device.Queue, 1, &submit, VK_NULL_HANDLE));
    Check(vkQueueWaitIdle(Device.Queue));
    const auto *result = static_cast<const uint16_t *>(Result.Mapped);
    for (uint32_t i = 0; i < Count; ++i) {
      const uint8_t encoded = weights[(i % Width) % weights.size()];
      const float expected = (Resolve ? 0.7F : 0.5F) * encoded / 127.0F;
      const float alpha = Half(result[i * 4 + 3]);
      Require(std::abs(alpha - expected) < 0.001F,
              "reflection gain leaked across pixels, clipped at 1, or used "
              "temporal alpha");
      if (encoded == 0)
        Require(alpha == 0,
                "zero material weight retained a reflection contribution");
      if (encoded || Resolve)
        for (uint32_t c = 0; c < 3; ++c)
          Require(std::abs(Half(result[i * 4 + c]) - 0.25F * (c + 1)) < 0.001F,
                  "material gain changed reflected radiance instead of its "
                  "contribution");
    }
    std::cout << (Resolve ? "SSSR material resolve" : "Hi-Z bilateral resolve")
              << ": zero, neutral, 2x and fractional weights passed across "
                 "adjacent pixels\n";
  }

  ~Fixture() {
    vkDestroyCommandPool(Device.Device, Commands, nullptr);
    vkDestroyPipeline(Device.Device, Pipeline, nullptr);
    vkDestroyPipelineLayout(Device.Device, Layout, nullptr);
    vkDestroyDescriptorPool(Device.Device, Pool, nullptr);
    vkDestroyDescriptorSetLayout(Device.Device, SetLayout, nullptr);
    vkDestroySampler(Device.Device, Sampler, nullptr);
    for (auto buffer : Uploads)
      Device.Free(buffer);
    Device.Free(Result);
    for (const auto &image : Images) {
      vkDestroyImageView(Device.Device, image.View, nullptr);
      vkDestroyImage(Device.Device, image.Handle, nullptr);
      vkFreeMemory(Device.Device, image.Memory, nullptr);
    }
  }
};
} // namespace

int main() try {
  Fixture(false).Run();
  Fixture(true).Run();
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
