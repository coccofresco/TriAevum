// Grass motion vectors with the production shader resolver (built-in or compiled): the compactor keeps each
// blade's previous interaction bend, and a blade point moved by its motion
// vector lands where the previous frame drew the same point.
#include "fast/oot3d/grass_draw_state.h"
#include "fast/oot3d/grass_gpu_instance_compactor.h"
#include "fast/oot3d/grass_indexed_topology.h"
#include "fast/oot3d/grass_instance_layout.h"
#include "fast/oot3d/grass_interaction_field.h"
#include "fast/oot3d/grass_shader_sources.h"
#include "fast/oot3d/outline_occlusion_pass.h"
#include "fast/renderer/shaderc_compiler.h"
#include "vulkan_test_device.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <numbers>
#include <span>
#include <vector>

namespace {
using namespace Fast::Oot3d;
using Fast::Renderer::SpirvStage;

constexpr uint32_t kExtent = 192U;
constexpr uint32_t kSegments = 4U;
constexpr uint32_t kBlades = 9U;
constexpr std::array<VkFormat, 5> kColorFormats{
    VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM};

float HalfToFloat(uint16_t value) {
    const uint32_t exponent = (value >> 10U) & 31U, mantissa = value & 1023U;
    const float magnitude = exponent == 0U ? std::ldexp(float(mantissa), -24)
        : exponent == 31U ? INFINITY : std::ldexp(float(mantissa | 1024U), int(exponent) - 25);
    return (value & 0x8000U) != 0U ? -magnitude : magnitude;
}

struct Camera {
    std::array<float, 3> Eye{};
    std::array<float, 3> At{};
};

// Column-major world to Vulkan clip (depth 0..1), 50 degree field of view.
std::array<float, 16> WorldToClip(const Camera& camera) {
    std::array<float, 3> forward{};
    for (int i = 0; i < 3; ++i) forward[i] = camera.At[i] - camera.Eye[i];
    const auto normalize = [](std::array<float, 3> v) {
        const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        return std::array<float, 3>{v[0] / length, v[1] / length, v[2] / length};
    };
    const auto cross = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return std::array<float, 3>{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    const auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    forward = normalize(forward);
    const auto side = normalize(cross(forward, {0.0F, 1.0F, 0.0F}));
    const auto up = cross(side, forward);
    const float focal = 1.0F / std::tan(25.0F * std::numbers::pi_v<float> / 180.0F);
    const float near = 1.0F, far = 1000.0F, a = far / (near - far), b = near * far / (near - far);
    const std::array<std::array<float, 4>, 4> rows{{
        {focal * side[0], focal * side[1], focal * side[2], -focal * dot(side, camera.Eye)},
        {focal * up[0], focal * up[1], focal * up[2], -focal * dot(up, camera.Eye)},
        {-a * forward[0], -a * forward[1], -a * forward[2], a * dot(forward, camera.Eye) + b},
        {forward[0], forward[1], forward[2], -dot(forward, camera.Eye)},
    }};
    std::array<float, 16> result{};
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) result[column * 4 + row] = rows[row][column];
    return result;
}

// One frame of the scene: camera, wind time, jitter and the collider sweep.
struct SceneFrame {
    uint64_t FrameId = 0;
    Camera View;
    float WindTime = 0.0F;
    std::array<float, 2> JitterNdc{};
    std::array<float, 3> ActorFrom{};
    std::array<float, 3> ActorTo{};
};

GrassEnvironmentRecord BuildRecord(const SceneFrame& frame, const SceneFrame* previous, bool previousBends) {
    GrassEnvironmentRecord record{};
    record.WindDirectionTime = {1.0F, 0.0F, frame.WindTime, 0.5F};
    record.WindPrimary = {2.84F, 1.0F, 0.35F, 0.35F};
    record.WindDetail = {0.42F, 0.44F, 0.8F, 1.0F};
    record.CameraPosition = {frame.View.Eye[0], frame.View.Eye[1], frame.View.Eye[2], 0.0F};
    // Unlit, root black and tip white: a blade point's gray is its height.
    record.AppearanceRoot = {0.0F, 0.0F, 0.0F, 1.0F};
    record.AppearanceTip = {1.0F, 1.0F, 1.0F, 1.0F};
    record.TextureBrightnessFlags = {1.0F, 1.0F, 0.0F, 0.0F};
    record.ViewForward = {0.0F, 0.0F, -1.0F, 0.0F};
    record.ViewSide = {1.0F, 0.0F, 0.0F, 0.0F};
    record.ViewUp = {0.0F, 1.0F, 0.0F, 0.0F};
    // No distance LOD: every blade keeps full visibility.
    record.TuftLod = {1.0F, 1.0e6F, 1.0F, 0.0F};
    record.DistanceLod = {1.0e6F, 1.0F, 1.0F, 0.0F};
    record.TuftStyle = {0.0F, 1.0F, 0.0F, 0.0F};
    if (previous != nullptr) {
        record.PreviousPositionToClip = WorldToClip(previous->View);
        record.PreviousCameraTime = {previous->View.Eye[0], previous->View.Eye[1], previous->View.Eye[2],
                                     previous->WindTime};
        record.MotionState = {previous->JitterNdc[0], previous->JitterNdc[1], 1.0F, previousBends ? 1.0F : 0.0F};
    }
    return record;
}

struct Image {
    VkImage Handle{};
    VkDeviceMemory Memory{};
    VkImageView View{};
};

struct Readback {
    std::vector<uint8_t> Color;
    std::vector<uint8_t> Material;
    std::vector<float> Motion;
    bool Covered(size_t pixel) const { return Color[pixel * 4U + 3U] >= 128U; }
};

class Harness {
  public:
    explicit Harness(Gpu& gpu) : mGpu(gpu), mDevice(gpu.Device) {
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = gpu.Family;
        Check(vkCreateCommandPool(mDevice, &pool, nullptr, &mCommandPool));
        CreateTargets();
        CreatePipeline();
        CreateCopyPipeline();
        mRecord = gpu.MakeBuffer(sizeof(GrassEnvironmentRecord), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        mDummy = gpu.MakeBuffer(256U, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memset(mDummy.Mapped, 0, 256U);
        mIndices = BuildGrassIndexedTopology();
        mIndexBuffer = gpu.MakeBuffer(mIndices.Indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        std::memcpy(mIndexBuffer.Mapped, mIndices.Indices.data(), mIndices.Indices.size() * sizeof(uint32_t));
        for (auto& readback : mReadback)
            readback = gpu.MakeBuffer(kExtent * kExtent * 8U, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }

    ~Harness() {
        vkDeviceWaitIdle(mDevice);
        for (auto& readback : mReadback) mGpu.Free(readback);
        mGpu.Free(mIndexBuffer);
        mGpu.Free(mDummy);
        mGpu.Free(mRecord);
        vkDestroyPipeline(mDevice, mCopyPipeline, nullptr);
        vkDestroyPipelineLayout(mDevice, mCopyLayout, nullptr);
        vkDestroyDescriptorPool(mDevice, mCopyPool, nullptr);
        vkDestroyDescriptorSetLayout(mDevice, mCopySetLayout, nullptr);
        vkDestroyPipeline(mDevice, mPipeline, nullptr);
        vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
        vkDestroyDescriptorPool(mDevice, mDescriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(mDevice, mSetLayout, nullptr);
        vkDestroySampler(mDevice, mSampler, nullptr);
        vkDestroyFramebuffer(mDevice, mFramebuffer, nullptr);
        vkDestroyRenderPass(mDevice, mRenderPass, nullptr);
        for (auto& image : mTargets) Destroy(image);
        Destroy(mDepth);
        Destroy(mLighting);
        vkDestroyCommandPool(mDevice, mCommandPool, nullptr);
    }

    Gpu& Device() const { return mGpu; }

    void Submit(const std::function<void(VkCommandBuffer)>& record) {
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = mCommandPool;
        allocation.commandBufferCount = 1U;
        VkCommandBuffer command{};
        Check(vkAllocateCommandBuffers(mDevice, &allocation, &command));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(vkBeginCommandBuffer(command, &begin));
        record(command);
        Check(vkEndCommandBuffer(command));
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence{};
        Check(vkCreateFence(mDevice, &fenceInfo, nullptr, &fence));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1U;
        submit.pCommandBuffers = &command;
        Check(vkQueueSubmit(mGpu.Queue, 1U, &submit, fence));
        Check(vkWaitForFences(mDevice, 1U, &fence, VK_TRUE, 30'000'000'000ULL));
        vkDestroyFence(mDevice, fence, nullptr);
        vkFreeCommandBuffers(mDevice, mCommandPool, 1U, &command);
    }

    // Reads a device-local storage buffer through a compute copy.
    std::vector<uint32_t> ReadWords(VkBuffer source, uint32_t count) {
        auto target = mGpu.MakeBuffer(std::max<VkDeviceSize>(count * 4U, 4U), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        const std::array<VkDescriptorBufferInfo, 2> buffers{{{source, 0U, VK_WHOLE_SIZE},
                                                              {target.Handle, 0U, VK_WHOLE_SIZE}}};
        std::array<VkWriteDescriptorSet, 2> writes{};
        for (uint32_t index = 0U; index < 2U; ++index) {
            writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[index].dstSet = mCopySet;
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1U;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[index].pBufferInfo = &buffers[index];
        }
        vkUpdateDescriptorSets(mDevice, 2U, writes.data(), 0U, nullptr);
        Submit([&](VkCommandBuffer command) {
            VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            before.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0U, 1U, &before, 0U, nullptr, 0U, nullptr);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, mCopyPipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, mCopyLayout, 0U, 1U, &mCopySet, 0U,
                                    nullptr);
            vkCmdPushConstants(command, mCopyLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0U, sizeof(count), &count);
            vkCmdDispatch(command, (count + 63U) / 64U, 1U, 1U);
            VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            after.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            after.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0U, 1U,
                                 &after, 0U, nullptr, 0U, nullptr);
        });
        std::vector<uint32_t> result(count);
        std::memcpy(result.data(), target.Mapped, count * 4U);
        mGpu.Free(target);
        return result;
    }

    Readback Draw(VkBuffer instances, uint32_t instanceCount, VkBuffer previousBends,
                  const GrassEnvironmentRecord& record, const GrassPushConstants& push) {
        std::memcpy(mRecord.Mapped, &record, sizeof(record));
        const VkDescriptorBufferInfo recordInfo{mRecord.Handle, 0U, sizeof(record)};
        const VkDescriptorBufferInfo dummyInfo{mDummy.Handle, 0U, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo rootsInfo{instances, 0U, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo bendsInfo{previousBends, 0U, VK_WHOLE_SIZE};
        const VkDescriptorImageInfo lightingInfo{mSampler, mLighting.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 9> writes{};
        for (uint32_t binding = 0U; binding < writes.size(); ++binding) {
            auto& write = writes[binding];
            write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = mDescriptorSet;
            write.dstBinding = binding;
            write.descriptorCount = 1U;
            write.descriptorType = binding == 1U ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                 : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pImageInfo = binding == 1U ? &lightingInfo : nullptr;
            write.pBufferInfo = binding == 0U ? &recordInfo : binding == 2U ? &rootsInfo
                              : binding == 8U ? &bendsInfo : binding == 1U ? nullptr : &dummyInfo;
        }
        vkUpdateDescriptorSets(mDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0U, nullptr);
        const auto& range = mIndices.Blades[kSegments - 1U][push.Flags[2] - 1U];
        Submit([&](VkCommandBuffer command) {
            std::array<VkClearValue, 6> clears{};
            clears[5].depthStencil = {1.0F, 0U};
            VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            begin.renderPass = mRenderPass;
            begin.framebuffer = mFramebuffer;
            begin.renderArea = {{0, 0}, {kExtent, kExtent}};
            begin.clearValueCount = static_cast<uint32_t>(clears.size());
            begin.pClearValues = clears.data();
            vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0U, 1U,
                                    &mDescriptorSet, 0U, nullptr);
            const VkDeviceSize offset = 0U;
            vkCmdBindVertexBuffers(command, 0U, 1U, &instances, &offset);
            vkCmdBindIndexBuffer(command, mIndexBuffer.Handle, 0U, VK_INDEX_TYPE_UINT32);
            const VkViewport viewport{0.0F, 0.0F, float(kExtent), float(kExtent), 0.0F, 1.0F};
            const VkRect2D scissor{{0, 0}, {kExtent, kExtent}};
            vkCmdSetViewport(command, 0U, 1U, &viewport);
            vkCmdSetScissor(command, 0U, 1U, &scissor);
            vkCmdPushConstants(command, mPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0U, sizeof(push), &push);
            vkCmdDrawIndexed(command, range.Count, instanceCount, range.First, 0, 0U);
            vkCmdEndRenderPass(command);
            const std::array<uint32_t, 3> attachments{0U, 2U, 3U};
            for (size_t index = 0U; index < attachments.size(); ++index) {
                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U};
                copy.imageExtent = {kExtent, kExtent, 1U};
                vkCmdCopyImageToBuffer(command, mTargets[attachments[index]].Handle,
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mReadback[index].Handle, 1U, &copy);
            }
            VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0U, 1U, &host,
                                 0U, nullptr, 0U, nullptr);
        });
        Readback result;
        const size_t pixels = size_t(kExtent) * kExtent;
        result.Color.assign(static_cast<uint8_t*>(mReadback[0].Mapped),
                            static_cast<uint8_t*>(mReadback[0].Mapped) + pixels * 4U);
        result.Material.assign(static_cast<uint8_t*>(mReadback[1].Mapped),
                               static_cast<uint8_t*>(mReadback[1].Mapped) + pixels * 4U);
        const auto* halves = static_cast<const uint16_t*>(mReadback[2].Mapped);
        result.Motion.resize(pixels * 4U);
        for (size_t index = 0U; index < result.Motion.size(); ++index) result.Motion[index] = HalfToFloat(halves[index]);
        return result;
    }

  private:
    Image MakeImage(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t width,
                    uint32_t layers = 1U) {
        Image image;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, width, 1U};
        info.mipLevels = 1U;
        info.arrayLayers = layers;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.usage = usage;
        Check(vkCreateImage(mDevice, &info, nullptr, &image.Handle));
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(mDevice, image.Handle, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = mGpu.Type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(vkAllocateMemory(mDevice, &allocation, nullptr, &image.Memory));
        Check(vkBindImageMemory(mDevice, image.Handle, image.Memory, 0U));
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = image.Handle;
        view.viewType = layers > 1U ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {aspect, 0U, 1U, 0U, layers};
        Check(vkCreateImageView(mDevice, &view, nullptr, &image.View));
        return image;
    }

    void Destroy(Image& image) {
        vkDestroyImageView(mDevice, image.View, nullptr);
        vkDestroyImage(mDevice, image.Handle, nullptr);
        vkFreeMemory(mDevice, image.Memory, nullptr);
        image = {};
    }

    void CreateTargets() {
        std::array<VkAttachmentDescription, 6> attachments{};
        std::array<VkAttachmentReference, 5> colors{};
        std::array<VkImageView, 6> views{};
        for (uint32_t index = 0U; index < kColorFormats.size(); ++index) {
            mTargets[index] = MakeImage(kColorFormats[index],
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                kExtent);
            attachments[index] = {0U, kColorFormats[index], VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
                VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
            colors[index] = {index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            views[index] = mTargets[index].View;
        }
        mDepth = MakeImage(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT, kExtent);
        attachments[5] = {0U, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        views[5] = mDepth.View;
        const VkAttachmentReference depth{5U, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<uint32_t>(colors.size());
        subpass.pColorAttachments = colors.data();
        subpass.pDepthStencilAttachment = &depth;
        const VkSubpassDependency dependency{0U, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0U};
        VkRenderPassCreateInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        render.attachmentCount = static_cast<uint32_t>(attachments.size());
        render.pAttachments = attachments.data();
        render.subpassCount = 1U;
        render.pSubpasses = &subpass;
        render.dependencyCount = 1U;
        render.pDependencies = &dependency;
        Check(vkCreateRenderPass(mDevice, &render, nullptr, &mRenderPass));
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = mRenderPass;
        framebuffer.attachmentCount = static_cast<uint32_t>(views.size());
        framebuffer.pAttachments = views.data();
        framebuffer.width = framebuffer.height = kExtent;
        framebuffer.layers = 1U;
        Check(vkCreateFramebuffer(mDevice, &framebuffer, nullptr, &mFramebuffer));
        // Native surface lighting is bound but unread (NativeLighting.y == 0).
        mLighting = MakeImage(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT, 1U, 3U);
        Submit([&](VkCommandBuffer command) {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = mLighting.Handle;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 3U};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0U,
                                 0U, nullptr, 0U, nullptr, 1U, &barrier);
        });
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        Check(vkCreateSampler(mDevice, &sampler, nullptr, &mSampler));
    }

    VkShaderModule Module(std::span<const uint32_t> words) {
        Require(!words.empty(), "grass shader resolution failed");
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = words.size() * sizeof(uint32_t);
        info.pCode = words.data();
        VkShaderModule module{};
        Check(vkCreateShaderModule(mDevice, &info, nullptr, &module));
        return module;
    }

    // Same bindings, vertex input and blending as InteractiveGrassPass.
    void CreatePipeline() {
        std::array<VkDescriptorSetLayoutBinding, 9> bindings{};
        for (uint32_t binding = 0U; binding < bindings.size(); ++binding)
            bindings[binding] = {binding, binding == 1U ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                        : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U,
                                 VkShaderStageFlags(binding == 0U ? VK_SHADER_STAGE_VERTEX_BIT |
                                                                        VK_SHADER_STAGE_FRAGMENT_BIT
                                                                  : VK_SHADER_STAGE_VERTEX_BIT), nullptr};
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = static_cast<uint32_t>(bindings.size());
        layout.pBindings = bindings.data();
        Check(vkCreateDescriptorSetLayout(mDevice, &layout, nullptr, &mSetLayout));
        const std::array<VkDescriptorPoolSize, 2> sizes{{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8U},
                                                         {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1U}}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1U;
        pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
        pool.pPoolSizes = sizes.data();
        Check(vkCreateDescriptorPool(mDevice, &pool, nullptr, &mDescriptorPool));
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = mDescriptorPool;
        allocation.descriptorSetCount = 1U;
        allocation.pSetLayouts = &mSetLayout;
        Check(vkAllocateDescriptorSets(mDevice, &allocation, &mDescriptorSet));
        const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U,
                                       sizeof(GrassPushConstants)};
        VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1U;
        pipelineLayout.pSetLayouts = &mSetLayout;
        pipelineLayout.pushConstantRangeCount = 1U;
        pipelineLayout.pPushConstantRanges = &push;
        Check(vkCreatePipelineLayout(mDevice, &pipelineLayout, nullptr, &mPipelineLayout));

        Fast::Renderer::CachedPassShaderCompiler shaders;
        const auto vertex = Module(shaders.Resolve(BuildGrassVertexShader(), SpirvStage::Vertex, "grass_test.vert"));
        const auto fragment = Module(shaders.Resolve(BuildGrassFragmentShader(), SpirvStage::Fragment,
                                                     "grass_test.frag", { { "GRASS_AUXILIARY_OUTPUTS", "1" } }));
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0U, VK_SHADER_STAGE_VERTEX_BIT, vertex,
             "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0U, VK_SHADER_STAGE_FRAGMENT_BIT, fragment,
             "main", nullptr}}};
        const VkVertexInputBindingDescription binding{0U, sizeof(GrassInstance), VK_VERTEX_INPUT_RATE_INSTANCE};
        const std::array<VkVertexInputAttributeDescription, 6> attributes{{
            {0U, 0U, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GrassInstance, BaseHeight)},
            {1U, 0U, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GrassInstance, BendAndHalfWidth)},
            {2U, 0U, VK_FORMAT_R32G32_SFLOAT, offsetof(GrassInstance, WidthAxis)},
            {3U, 0U, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GrassInstance, WorldNormal)},
            {4U, 0U, VK_FORMAT_R32_UINT, offsetof(GrassInstance, SurfaceColor)},
            {5U, 0U, VK_FORMAT_R32G32_UINT, offsetof(GrassInstance, SurfaceReference)}}};
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1U;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1U;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1.0F;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState guide{};
        guide.colorWriteMask = 0xFU;
        VkPipelineColorBlendAttachmentState motion{};
        ConfigureOutlineCoverageBlend(motion, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                  VK_COLOR_COMPONENT_B_BIT);
        const std::array<VkPipelineColorBlendAttachmentState, 5> blends{guide, guide, guide, motion, guide};
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = static_cast<uint32_t>(blends.size());
        blend.pAttachments = blends.data();
        const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamic.pDynamicStates = dynamicStates.data();
        VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.stageCount = static_cast<uint32_t>(stages.size());
        pipeline.pStages = stages.data();
        pipeline.pVertexInputState = &vertexInput;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &multisample;
        pipeline.pDepthStencilState = &depth;
        pipeline.pColorBlendState = &blend;
        pipeline.pDynamicState = &dynamic;
        pipeline.layout = mPipelineLayout;
        pipeline.renderPass = mRenderPass;
        Check(vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1U, &pipeline, nullptr, &mPipeline));
        vkDestroyShaderModule(mDevice, vertex, nullptr);
        vkDestroyShaderModule(mDevice, fragment, nullptr);
    }

    void CreateCopyPipeline() {
        const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
            {0U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}}};
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = 2U;
        layout.pBindings = bindings.data();
        Check(vkCreateDescriptorSetLayout(mDevice, &layout, nullptr, &mCopySetLayout));
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2U};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1U;
        pool.poolSizeCount = 1U;
        pool.pPoolSizes = &size;
        Check(vkCreateDescriptorPool(mDevice, &pool, nullptr, &mCopyPool));
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = mCopyPool;
        allocation.descriptorSetCount = 1U;
        allocation.pSetLayouts = &mCopySetLayout;
        Check(vkAllocateDescriptorSets(mDevice, &allocation, &mCopySet));
        const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0U, sizeof(uint32_t)};
        VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1U;
        pipelineLayout.pSetLayouts = &mCopySetLayout;
        pipelineLayout.pushConstantRangeCount = 1U;
        pipelineLayout.pPushConstantRanges = &push;
        Check(vkCreatePipelineLayout(mDevice, &pipelineLayout, nullptr, &mCopyLayout));
        const auto shader = mGpu.Shader(R"glsl(#version 450
layout(local_size_x=64) in;
layout(std430,set=0,binding=0) readonly buffer Source { uint words[]; } source;
layout(std430,set=0,binding=1) writeonly buffer Target { uint words[]; } target;
layout(push_constant) uniform Copy { uint count; } copy;
void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index < copy.count) target.words[index] = source.words[index];
}
)glsl", shaderc_compute_shader);
        VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0U,
                          VK_SHADER_STAGE_COMPUTE_BIT, shader, "main", nullptr};
        pipeline.layout = mCopyLayout;
        Check(vkCreateComputePipelines(mDevice, VK_NULL_HANDLE, 1U, &pipeline, nullptr, &mCopyPipeline));
        vkDestroyShaderModule(mDevice, shader, nullptr);
    }

    Gpu& mGpu;
    VkDevice mDevice{};
    VkCommandPool mCommandPool{};
    std::array<Image, 5> mTargets{};
    Image mDepth;
    Image mLighting;
    VkSampler mSampler{};
    VkRenderPass mRenderPass{};
    VkFramebuffer mFramebuffer{};
    VkDescriptorSetLayout mSetLayout{};
    VkDescriptorPool mDescriptorPool{};
    VkDescriptorSet mDescriptorSet{};
    VkPipelineLayout mPipelineLayout{};
    VkPipeline mPipeline{};
    VkDescriptorSetLayout mCopySetLayout{};
    VkDescriptorPool mCopyPool{};
    VkDescriptorSet mCopySet{};
    VkPipelineLayout mCopyLayout{};
    VkPipeline mCopyPipeline{};
    Gpu::Buffer mRecord;
    Gpu::Buffer mDummy;
    Gpu::Buffer mIndexBuffer;
    std::array<Gpu::Buffer, 3> mReadback{};
    GrassIndexedTopology mIndices;
};

// A row of blades across the view; the actor sweeps through it.
struct Scene {
    std::vector<GrassWorldAnchor> Anchors;
    std::vector<uint32_t> Visible;
    GrassInteractionField Field;
    InteractiveGrassSettings Settings;
    uint64_t StaticRevision = 1U;

    Scene() {
        for (uint32_t index = 0U; index < kBlades; ++index) {
            GrassWorldAnchor anchor;
            anchor.BaseHeight = {-24.0F + 6.0F * float(index), 0.0F, 0.0F, 30.0F};
            anchor.HalfWidthPhase = {1.5F, 0.37F * float(index)};
            anchor.StableId = 977U * index + 11U;
            Anchors.push_back(anchor);
            // Visible order differs from static order: history follows anchors.
            Visible.push_back(kBlades - 1U - index);
        }
        Settings.CollisionPush = 1.0F;
        Settings.CollisionVelocityResponse = 0.0F;
        Settings.ColliderRadiusMultiplier = 1.0F;
        Settings.ColliderHeightMultiplier = 1.0F;
        Settings.MaximumBend = 0.8F;
        Settings.InteractionVerticalMargin = 12.0F;
    }

    GrassGpuInstanceCompactionResult Compact(Harness& harness, GrassGpuInstanceCompactor& compactor,
                                             const SceneFrame& frame, uint32_t slot) {
        GrassInteractor actor;
        actor.PreviousPosition = frame.ActorFrom;
        actor.Position = frame.ActorTo;
        actor.Radius = 10.0F;
        actor.HalfHeight = 20.0F;
        const std::array<GrassInteractor, 1> actors{actor};
        GrassGpuInstanceCompactionResult result;
        bool compacted = false;
        const std::array<std::span<const GrassWorldAnchor>, 1> segments{std::span<const GrassWorldAnchor>(Anchors)};
        harness.Submit([&](VkCommandBuffer command) {
            compacted = compactor.Compact({command, slot, frame.FrameId, StaticRevision,
                                           GrassStaticAnchorSegments{segments, Anchors.size()}, Visible, &Field,
                                           actors, &Settings}, result);
        });
        Require(compacted, "grass compaction failed");
        return result;
    }
};

std::vector<std::array<float, 2>> Bends(Harness& harness, const GrassGpuInstanceCompactionResult& result) {
    const auto words = harness.ReadWords(result.InstanceBuffer, kBlades * uint32_t(sizeof(GrassInstance) / 4U));
    std::vector<std::array<float, 2>> bends(kBlades);
    for (uint32_t index = 0U; index < kBlades; ++index) {
        const size_t base = index * (sizeof(GrassInstance) / 4U);
        std::memcpy(bends[index].data(), &words[base + 4U], sizeof(float) * 2U);
    }
    return bends;
}

std::vector<std::array<float, 2>> PreviousBends(Harness& harness, const GrassGpuInstanceCompactionResult& result) {
    const auto words = harness.ReadWords(result.PreviousBendBuffer, kBlades * 2U);
    std::vector<std::array<float, 2>> bends(kBlades);
    std::memcpy(bends.data(), words.data(), words.size() * sizeof(uint32_t));
    return bends;
}

bool SameBits(const std::vector<std::array<float, 2>>& a, const std::vector<std::array<float, 2>>& b) {
    return std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0;
}

float LargestChange(const std::vector<std::array<float, 2>>& a, const std::vector<std::array<float, 2>>& b) {
    float result = 0.0F;
    for (size_t index = 0U; index < a.size(); ++index)
        result = std::max(result, std::hypot(a[index][0] - b[index][0], a[index][1] - b[index][1]));
    return result;
}

GrassPushConstants Push(const SceneFrame& frame, uint32_t planes) {
    GrassPushConstants push;
    push.PositionToClip = WorldToClip(frame.View);
    push.JitterNdc = {frame.JitterNdc[0], frame.JitterNdc[1], 0.0F, 0.0F};
    push.Flags = {0U, kSegments, planes, 0U};
    return push;
}

// Current blade pixels moved by their motion: the fraction that lands on the
// previous frame's blades (the 2x2 texels around the point), the mean height
// (gray) error there, and the fraction missing them by more than a pixel.
struct Reprojection {
    double Landed = 0.0;
    double HeightError = 0.0;
    double FarMisses = 0.0;
};

Reprojection Reproject(const Readback& previous, const Readback& current, bool useMotion) {
    size_t covered = 0U, landed = 0U, far = 0U;
    double error = 0.0;
    const auto coveredAt = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < int(kExtent) && y < int(kExtent) &&
               previous.Covered(size_t(y) * kExtent + size_t(x));
    };
    for (uint32_t y = 0U; y < kExtent; ++y) {
        for (uint32_t x = 0U; x < kExtent; ++x) {
            const size_t pixel = size_t(y) * kExtent + x;
            if (!current.Covered(pixel)) continue;
            ++covered;
            const float u = (float(x) + 0.5F) / kExtent + (useMotion ? current.Motion[pixel * 4U] : 0.0F);
            const float v = (float(y) + 0.5F) / kExtent + (useMotion ? current.Motion[pixel * 4U + 1U] : 0.0F);
            const int x0 = int(std::floor(u * kExtent - 0.5F)), y0 = int(std::floor(v * kExtent - 0.5F));
            int best = -1;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    if (!coveredAt(x0 + dx, y0 + dy)) continue;
                    const size_t target = size_t(y0 + dy) * kExtent + size_t(x0 + dx);
                    const int difference = std::abs(int(previous.Color[target * 4U]) - int(current.Color[pixel * 4U]));
                    if (best < 0 || difference < best) best = difference;
                }
            }
            if (best >= 0) {
                ++landed;
                error += best / 255.0;
                continue;
            }
            bool near = false;
            for (int dy = -1; dy <= 2 && !near; ++dy)
                for (int dx = -1; dx <= 2 && !near; ++dx) near = coveredAt(x0 + dx, y0 + dy);
            if (!near) ++far;
        }
    }
    Require(covered > 500U, "blades cover too few pixels");
    return {double(landed) / double(covered), landed != 0U ? error / double(landed) : 1.0,
            double(far) / double(covered)};
}

void ExpectMotionGuides(const Readback& frame, bool exact) {
    size_t covered = 0U;
    for (size_t pixel = 0U; pixel < size_t(kExtent) * kExtent; ++pixel) {
        if (!frame.Covered(pixel)) continue;
        ++covered;
        const bool valid = frame.Motion[pixel * 4U + 2U] > 0.5F;
        const bool reactive = frame.Material[pixel * 4U + 3U] >= 128U;
        Require(valid == exact && reactive == !exact,
                exact ? "exact blade motion must be valid and non-reactive"
                      : "blades without history keep camera motion and the reactive mask");
        if (!exact)
            Require(frame.Motion[pixel * 4U] == 0.0F && frame.Motion[pixel * 4U + 1U] == 0.0F,
                    "blades without history must not write rigid motion");
    }
    Require(covered > 500U, "blades cover too few pixels");
}

void TestBladeMotion(Harness& harness, uint32_t planes) {
    GrassGpuInstanceCompactor compactor;
    Fast::Renderer::CachedPassShaderCompiler shaders;
    Scene scene;
    Require(compactor.Initialize(harness.Device().Physical, harness.Device().Device, shaders),
            compactor.UnavailableReason().c_str());
    // The camera moves, wind advances, the jitter changes and the actor
    // sweeps from the left blades to the middle ones.
    const SceneFrame previous{100U, {{0.0F, 15.0F, 70.0F}, {0.0F, 15.0F, 0.0F}}, 10.0F, {0.004F, -0.003F},
                              {-20.0F, 0.0F, -3.0F}, {-12.0F, 0.0F, -3.0F}};
    const SceneFrame current{101U, {{4.0F, 16.0F, 68.0F}, {1.0F, 15.0F, 0.0F}}, 10.4F, {-0.005F, 0.002F},
                             {-12.0F, 0.0F, -3.0F}, {4.0F, 0.0F, -3.0F}};
    const auto first = scene.Compact(harness, compactor, previous, 0U);
    const auto firstBends = Bends(harness, first);
    const auto firstFrame = harness.Draw(first.InstanceBuffer, kBlades, first.PreviousBendBuffer,
                                         BuildRecord(previous, nullptr, false), Push(previous, planes));
    ExpectMotionGuides(firstFrame, false);
    const auto second = scene.Compact(harness, compactor, current, 1U);
    Require(SameBits(PreviousBends(harness, second), firstBends), "previous bends must be the drawn ones");
    Require(LargestChange(Bends(harness, second), firstBends) > 0.2F, "the actor sweep must move blades");
    const auto exact = harness.Draw(second.InstanceBuffer, kBlades, second.PreviousBendBuffer,
                                    BuildRecord(current, &previous, true), Push(current, planes));
    ExpectMotionGuides(exact, true);
    // What the motion pass had before: camera motion only (wind time and
    // bend unchanged), and no motion at all.
    SceneFrame cameraOnly = previous;
    cameraOnly.WindTime = current.WindTime;
    const auto camera = harness.Draw(second.InstanceBuffer, kBlades, second.PreviousBendBuffer,
                                     BuildRecord(current, &cameraOnly, false), Push(current, planes));
    const auto exactMatch = Reproject(firstFrame, exact, true);
    const auto cameraMatch = Reproject(firstFrame, camera, true);
    const auto staticMatch = Reproject(firstFrame, exact, false);
    const auto print = [](const char* name, const Reprojection& match) {
        std::cout << ' ' << name << " landed=" << match.Landed << " height=" << match.HeightError
                  << " far=" << match.FarMisses;
    };
    std::cout << "planes=" << planes;
    print("exact", exactMatch);
    print("camera-only", cameraMatch);
    print("no-motion", staticMatch);
    std::cout << '\n';
    Require(exactMatch.Landed > 0.97 && exactMatch.HeightError < 0.02 && exactMatch.FarMisses < 0.005,
            "exact blade motion must land on the same blade point");
    Require(cameraMatch.Landed < exactMatch.Landed - 0.1 || cameraMatch.HeightError > 4.0 * exactMatch.HeightError,
            "camera-only motion must miss wind and interaction");
    Require(staticMatch.Landed < exactMatch.Landed - 0.1 || staticMatch.HeightError > 4.0 * exactMatch.HeightError,
            "the scene must move between the frames");
    compactor.Shutdown();
}

void TestBendHistory(Harness& harness) {
    GrassGpuInstanceCompactor compactor;
    Fast::Renderer::CachedPassShaderCompiler shaders;
    Scene scene;
    Require(compactor.Initialize(harness.Device().Physical, harness.Device().Device, shaders),
            compactor.UnavailableReason().c_str());
    const auto frame = [](uint64_t id, float x) {
        return SceneFrame{id, {}, 0.0F, {}, {x - 8.0F, 0.0F, -3.0F}, {x, 0.0F, -3.0F}};
    };
    auto result = scene.Compact(harness, compactor, frame(200U, -16.0F), 0U);
    auto bends = Bends(harness, result);
    Require(SameBits(PreviousBends(harness, result), bends), "a first frame has no previous bend");
    result = scene.Compact(harness, compactor, frame(201U, -4.0F), 1U);
    auto next = Bends(harness, result);
    Require(LargestChange(next, bends) > 0.2F, "the actor sweep must move blades");
    Require(SameBits(PreviousBends(harness, result), bends), "consecutive frames keep the previous bend");
    // A second invocation in the same frame reads the bend it replaced.
    result = scene.Compact(harness, compactor, frame(201U, -4.0F), 0U);
    Require(SameBits(Bends(harness, result), next), "a repeated invocation computes the same bends");
    Require(LargestChange(PreviousBends(harness, result), bends) < 2.0e-3F,
            "a repeated invocation keeps the previous frame's bend");
    // A skipped frame, a temporal reset and a new static set have no history.
    result = scene.Compact(harness, compactor, frame(203U, 8.0F), 1U);
    bends = Bends(harness, result);
    Require(SameBits(PreviousBends(harness, result), bends), "a frame gap drops the history");
    result = scene.Compact(harness, compactor, frame(204U, 14.0F), 0U);
    Require(SameBits(PreviousBends(harness, result), bends) && !SameBits(Bends(harness, result), bends),
            "history resumes after a gap");
    compactor.ResetHistory();
    result = scene.Compact(harness, compactor, frame(205U, 20.0F), 1U);
    Require(SameBits(PreviousBends(harness, result), Bends(harness, result)), "a temporal reset drops the history");
    ++scene.StaticRevision;
    result = scene.Compact(harness, compactor, frame(206U, 26.0F), 0U);
    Require(SameBits(PreviousBends(harness, result), Bends(harness, result)), "a new static set drops the history");
    compactor.Shutdown();
    std::cout << "bend history: consecutive, repeated, gap, reset and static-set cases passed\n";
}

void TestIdleCompactionReuse(Harness& harness) {
    GrassGpuInstanceCompactor compactor;
    Fast::Renderer::CachedPassShaderCompiler shaders;
    Scene scene;
    Require(compactor.Initialize(harness.Device().Physical, harness.Device().Device, shaders),
            compactor.UnavailableReason().c_str());
    uint64_t frame = 500, selection = 1;
    const auto compact = [&](uint32_t slot, bool allow = true, std::span<const GrassInteractor> actors = {}) {
        GrassGpuInstanceCompactionResult result;
        const std::array<std::span<const GrassWorldAnchor>, 1> segments{
            std::span<const GrassWorldAnchor>(scene.Anchors)};
        harness.Submit([&](VkCommandBuffer command) {
            Require(compactor.Compact({ command, slot, frame++, scene.StaticRevision,
                                        GrassStaticAnchorSegments{segments, scene.Anchors.size()}, scene.Visible,
                                        &scene.Field, actors, &scene.Settings, selection, allow },
                                      result),
                    "idle compaction failed");
        });
        return result;
    };
    const auto first = compact(0);
    const auto second = compact(1);
    Require(!first.Reused && !second.Reused, "each fenced slot must be initialized");
    const auto original = harness.ReadWords(first.InstanceBuffer, kBlades * uint32_t(sizeof(GrassInstance) / 4));
    const SceneFrame view{ 1, { { 0, 15, 70 }, { 0, 15, 0 } }, 10, {}, {}, {} };
    const auto before = harness.Draw(first.InstanceBuffer, kBlades, first.PreviousBendBuffer,
                                     BuildRecord(view, nullptr, false), Push(view, 1));
    auto hit = compact(0);
    Require(hit.Reused && hit.SelectionUploadReused && hit.DynamicUploadedBytes == 0,
            "idle exact selection must reuse GPU data");
    Require(original == harness.ReadWords(hit.InstanceBuffer, kBlades * uint32_t(sizeof(GrassInstance) / 4)),
            "cached instance payload must be bit identical");
    const auto after = harness.Draw(hit.InstanceBuffer, kBlades, hit.PreviousBendBuffer,
                                    BuildRecord(view, nullptr, false), Push(view, 1));
    Require(before.Color == after.Color && before.Material == after.Material && before.Motion == after.Motion,
            "cached blade capture must be pixel identical");
    auto dynamic = compact(0, false);
    Require(!dynamic.Reused && dynamic.SelectionUploadReused,
            "motion vectors retain their per-frame bend-history dispatch");
    ++selection;
    std::reverse(scene.Visible.begin(), scene.Visible.end());
    auto changed = compact(0);
    Require(!changed.Reused && !changed.SelectionUploadReused, "selection edit invalidates both caches");
    ++scene.StaticRevision;
    changed = compact(0);
    Require(!changed.Reused, "static edit invalidates compacted payload");
    GrassInteractor actor;
    actor.Position = actor.PreviousPosition = { 0, 0, 0 };
    actor.Radius = 10;
    actor.HalfHeight = 20;
    actor.Velocity = { 300, 0, 0 };
    const std::array<GrassInteractor, 1> actors{ actor };
    changed = compact(0, true, actors);
    Require(!changed.Reused, "actors cannot reuse idle output");
    scene.Field.UpdateActors(1.F / 30, scene.Settings, actors);
    Require(!scene.Field.AtRest(), "interaction fixture must be active");
    changed = compact(1);
    Require(!changed.Reused, "a relaxing interaction field cannot reuse idle output");
    scene.Field.Reset();
    compactor.ResetHistory();
    changed = compact(0);
    Require(!changed.Reused && !changed.SelectionUploadReused, "reset invalidates GPU cache");
    compactor.Shutdown();
    std::cout << "idle compaction: exact GPU payload/capture, selection/static/actor/field/motion/reset invalidation "
                 "passed\n";
}

} // namespace

int main() {
    try {
        VkPhysicalDeviceFeatures features{};
        features.independentBlend = VK_TRUE;
        Gpu gpu(features, VK_API_VERSION_1_2);
        {
            Harness harness(gpu);
            TestIdleCompactionReuse(harness);
            TestBendHistory(harness);
            TestBladeMotion(harness, 2U);
            TestBladeMotion(harness, 1U);
        }
        std::cout << "grass motion GPU tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "grass motion GPU test failed: " << error.what() << '\n';
        return 1;
    }
}
