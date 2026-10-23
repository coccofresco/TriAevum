// iMMERSE module of TriAevum (see ../LICENSE.md).
// iMMERSE MXAO, Copyright (c) Pascal Gilcher. All rights reserved.
// Not covered by TriAevum's GPL: this file stays under the original iMMERSE
// licence (../LICENSE-iMMERSE.txt) and is used under the author's explicit
// permission, granted exclusively to TriAevum.
#include "fast/immerse/mxao_shader.h"
#include "fast/oot3d/ambient_occlusion_composite.h"
#include "fast/renderer3ds/pica_geometry_guide.h"
#include "vulkan_test_device.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

using Fast::Immerse::MxaoConstants;

namespace {
struct Image {
    VkImage Handle{};
    VkImageView View{};
    VkDeviceMemory Memory{};
};

enum : uint32_t {
    kDepth,
    kNormals,
    kTiled,
    kRaw,
    kFiltered,
    kOutput,
    kMotion,
    kHistory0,
    kHistory1,
    kGeometry,
    kImageCount
};
enum : uint32_t { kDeinterleave, kOcclusion, kFilter, kResolve, kTemporal, kDebug, kProgramCount };
// The descriptor sets of MxaoPass: input (binding 2), output (binding 3) and
// history (binding 6); binding 5 is always the motion vectors.
enum : uint32_t {
    kDeinterleaveSet,
    kOcclusionSet,
    kFilterSet,
    kResolveRawSet,
    kResolveFilteredSet,
    kTemporalSet,
    kFilterAccumulatedSet = kTemporalSet + 2,
    kResolveAccumulatedSet = kFilterAccumulatedSet + 2,
    kSetCount = kResolveAccumulatedSet + 2
};

float HalfToFloat(uint16_t half) {
    const uint32_t sign = uint32_t(half >> 15) << 31;
    const uint32_t exponent = (half >> 10) & 0x1F;
    const uint32_t mantissa = half & 0x3FF;
    uint32_t bits = sign;
    if (exponent == 0 && mantissa != 0) {
        int shift = 0;
        uint32_t m = mantissa;
        while ((m & 0x400) == 0) {
            m <<= 1;
            ++shift;
        }
        bits |= uint32_t(127 - 15 - shift + 1) << 23 | (m & 0x3FF) << 13;
    } else if (exponent == 31) {
        bits |= 0x7F800000U | mantissa << 13;
    } else if (exponent != 0) {
        bits |= (exponent + 127 - 15) << 23 | mantissa << 13;
    }
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// Production MXAO kernels on native RGB8 normals. Odd extents exercise the
// padded tile grid and partial workgroups of every program.
struct MxaoFixture {
    static constexpr uint32_t Width = 129, Height = 97;
    Gpu Device{ VkPhysicalDeviceFeatures{ .shaderStorageImageExtendedFormats = VK_TRUE } };
    std::array<Image, kImageCount> Images{};
    Gpu::Buffer Depths{}, Normals{}, Motion{}, Result{}, Accumulation{}, Geometry{};
    VkSampler Sampler{};
    VkDescriptorSetLayout SetLayout{};
    VkDescriptorPool Pool{};
    static constexpr std::array<std::array<uint32_t, 3>, kSetCount> kSets{ { { kDepth, kTiled, kDepth },
                                                                             { kTiled, kRaw, kDepth },
                                                                             { kRaw, kFiltered, kDepth },
                                                                             { kRaw, kOutput, kDepth },
                                                                             { kFiltered, kOutput, kDepth },
                                                                             { kRaw, kHistory0, kHistory1 },
                                                                             { kRaw, kHistory1, kHistory0 },
                                                                             { kHistory0, kFiltered, kDepth },
                                                                             { kHistory1, kFiltered, kDepth },
                                                                             { kHistory0, kOutput, kDepth },
                                                                             { kHistory1, kOutput, kDepth } } };
    std::array<VkDescriptorSet, kSetCount> Sets{};
    VkPipelineLayout Layout{};
    std::array<VkPipeline, kProgramCount> Pipelines{};
    VkCommandPool Commands{};
    VkCommandBuffer Command{};
    // Temporal state: the accumulation written next and the history layouts.
    uint32_t NextHistory = 0;
    std::array<VkImageLayout, 2> HistoryLayouts{ VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED };

    Image MakeImage(VkFormat format, uint32_t width, uint32_t height) {
        Image image;
        VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = { width, height, 1 };
        info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        Check(vkCreateImage(Device.Device, &info, nullptr, &image.Handle));
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(Device.Device, image.Handle, &requirements);
        VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = Device.Type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(vkAllocateMemory(Device.Device, &allocation, nullptr, &image.Memory));
        Check(vkBindImageMemory(Device.Device, image.Handle, image.Memory, 0));
        VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view.image = image.Handle;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        Check(vkCreateImageView(Device.Device, &view, nullptr, &image.View));
        return image;
    }

    MxaoFixture() {
        const uint32_t tiledWidth = Fast::Immerse::MxaoTiledExtent(Width);
        const uint32_t tiledHeight = Fast::Immerse::MxaoTiledExtent(Height);
        Images = { MakeImage(VK_FORMAT_R32_SFLOAT, Width, Height),
                   MakeImage(VK_FORMAT_R8G8B8A8_UNORM, Width, Height),
                   MakeImage(VK_FORMAT_R32_SFLOAT, tiledWidth, tiledHeight),
                   MakeImage(VK_FORMAT_R16G16_SFLOAT, Width, Height),
                   MakeImage(VK_FORMAT_R16G16_SFLOAT, Width, Height),
                   MakeImage(VK_FORMAT_R32_SFLOAT, Width, Height),
                   MakeImage(VK_FORMAT_R32G32B32A32_SFLOAT, Width, Height),
                   MakeImage(VK_FORMAT_R16G16B16A16_SFLOAT, Width, Height),
                   MakeImage(VK_FORMAT_R16G16B16A16_SFLOAT, Width, Height),
                   MakeImage(VK_FORMAT_R32G32B32A32_UINT, Width, Height) };
        Depths = Device.MakeBuffer(Width * Height * sizeof(float), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        Normals = Device.MakeBuffer(Width * Height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        Motion = Device.MakeBuffer(Width * Height * 4 * sizeof(float), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        Result = Device.MakeBuffer(Width * Height * sizeof(float), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        Accumulation = Device.MakeBuffer(Width * Height * 4 * sizeof(uint16_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        Geometry = Device.MakeBuffer(Width * Height * 4 * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        VkSamplerCreateInfo sampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        Check(vkCreateSampler(Device.Device, &sampler, nullptr, &Sampler));
        std::array<VkDescriptorSetLayoutBinding, 8> bindings{};
        for (uint32_t i = 0; i < bindings.size(); ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            bindings[i].descriptorType = i == 4   ? VK_DESCRIPTOR_TYPE_SAMPLER
                                         : i == 3 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                  : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        }
        VkDescriptorSetLayoutCreateInfo setLayout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        setLayout.bindingCount = static_cast<uint32_t>(bindings.size());
        setLayout.pBindings = bindings.data();
        Check(vkCreateDescriptorSetLayout(Device.Device, &setLayout, nullptr, &SetLayout));
        const std::array<VkDescriptorPoolSize, 3> sizes{ { { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 6 * kSetCount },
                                                           { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kSetCount },
                                                           { VK_DESCRIPTOR_TYPE_SAMPLER, kSetCount } } };
        VkDescriptorPoolCreateInfo pool{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool.maxSets = static_cast<uint32_t>(Sets.size());
        pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
        pool.pPoolSizes = sizes.data();
        Check(vkCreateDescriptorPool(Device.Device, &pool, nullptr, &Pool));
        std::array<VkDescriptorSetLayout, kSetCount> layouts{};
        layouts.fill(SetLayout);
        VkDescriptorSetAllocateInfo allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocation.descriptorPool = Pool;
        allocation.descriptorSetCount = static_cast<uint32_t>(Sets.size());
        allocation.pSetLayouts = layouts.data();
        Check(vkAllocateDescriptorSets(Device.Device, &allocation, Sets.data()));
        for (uint32_t set = 0; set < Sets.size(); ++set) {
            std::array<VkDescriptorImageInfo, 8> images{};
            std::array<VkWriteDescriptorSet, 8> writes{};
            for (uint32_t binding = 0; binding < 8; ++binding) {
                const uint32_t index = binding == 2   ? kSets[set][0]
                                       : binding == 3 ? kSets[set][1]
                                       : binding == 5 ? uint32_t(kMotion)
                                       : binding == 6 ? kSets[set][2]
                                       : binding == 7 ? uint32_t(kGeometry)
                                                      : binding;
                images[binding].imageView = binding == 4 ? VK_NULL_HANDLE : Images[index].View;
                images[binding].imageLayout =
                    binding == 3 ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                images[binding].sampler = binding == 4 ? Sampler : VK_NULL_HANDLE;
                writes[binding] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                writes[binding].dstSet = Sets[set];
                writes[binding].dstBinding = binding;
                writes[binding].descriptorCount = 1;
                writes[binding].descriptorType = bindings[binding].descriptorType;
                writes[binding].pImageInfo = &images[binding];
            }
            vkUpdateDescriptorSets(Device.Device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
        const VkPushConstantRange push{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(MxaoConstants) };
        VkPipelineLayoutCreateInfo layout{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &SetLayout;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        Check(vkCreatePipelineLayout(Device.Device, &layout, nullptr, &Layout));
        const auto sources = Fast::Immerse::BuildMxaoPassShaders(
            Fast::Oot3d::AmbientOcclusionDebugShaderLibrary(), Fast::Renderer3ds::PicaGeometryGuideShaderLibrary());
        for (uint32_t program = 0; program < kProgramCount; ++program) {
            const auto shader = Device.Shader(sources[program], shaderc_compute_shader);
            VkComputePipelineCreateInfo pipeline{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            pipeline.layout = Layout;
            pipeline.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                               nullptr,
                               0,
                               VK_SHADER_STAGE_COMPUTE_BIT,
                               shader,
                               "main",
                               nullptr };
            Check(vkCreateComputePipelines(Device.Device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &Pipelines[program]));
            vkDestroyShaderModule(Device.Device, shader, nullptr);
        }
        VkCommandPoolCreateInfo commands{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        commands.queueFamilyIndex = Device.Family;
        Check(vkCreateCommandPool(Device.Device, &commands, nullptr, &Commands));
        VkCommandBufferAllocateInfo command{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        command.commandPool = Commands;
        command.commandBufferCount = 1;
        Check(vkAllocateCommandBuffers(Device.Device, &command, &Command));
    }

    void Barrier(const Image& image, VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags from,
                 VkAccessFlags to, VkPipelineStageFlags source, VkPipelineStageFlags destination) {
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.srcAccessMask = from;
        barrier.dstAccessMask = to;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.Handle;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(Command, source, destination, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    void WrittenToRead(uint32_t image) {
        Barrier(Images[image], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    void Dispatch(uint32_t program, uint32_t set, MxaoConstants constants, uint32_t flags, uint32_t width,
                  uint32_t height, uint32_t group) {
        constants.Flags |= flags;
        vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Pipelines[program]);
        vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1, &Sets[set], 0, nullptr);
        vkCmdPushConstants(Command, Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(Command, (width + group - 1) / group, (height + group - 1) / group, 1);
    }
    void Upload(const Gpu::Buffer& buffer, uint32_t image) {
        Barrier(Images[image], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { Width, Height, 1 };
        vkCmdCopyBufferToImage(Command, buffer.Handle, Images[image].Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &copy);
        Barrier(Images[image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    void Download(uint32_t image, VkImageLayout layout, const Gpu::Buffer& buffer) {
        Barrier(Images[image], layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { Width, Height, 1 };
        vkCmdCopyImageToBuffer(Command, Images[image].Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.Handle, 1,
                               &copy);
        Barrier(Images[image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout, VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    // The raw estimate survives into the next Run (shading rate), and a
    // nonzero debug view replaces the resolve with the debug program.
    bool KeepRaw = false;
    bool RawWritten = false;
    uint32_t DebugView = 0;
    // The geometry guide's face normal of every pixel (G); 0 = none.
    uint32_t FaceNormal = 0;

    // One frame. With `motion` the frame accumulates (temporal program) into
    // the next history image, reading the other one; Constants.Temporal says
    // whether that history is valid. `accumulation` receives the written
    // RGBA16F accumulation.
    std::vector<float> Run(const std::vector<float>& depth, const MxaoConstants& constants, uint32_t filter,
                           uint8_t coverage = 255, float slope = 0,
                           const std::vector<std::array<float, 4>>* motion = nullptr,
                           std::vector<std::array<float, 4>>* accumulation = nullptr) {
        auto* encodedDepth = static_cast<float*>(Depths.Mapped);
        auto* encodedNormals = static_cast<std::array<uint8_t, 4>*>(Normals.Mapped);
        const float length = std::sqrt(1 + slope * slope);
        const std::array<uint8_t, 4> normal = { static_cast<uint8_t>(std::round((.5F + .5F * slope / length) * 255)),
                                                128, static_cast<uint8_t>(std::round((.5F + .5F / length) * 255)),
                                                coverage };
        for (size_t i = 0; i < depth.size(); ++i) {
            encodedDepth[i] = constants.Depth[3] != 0 ? (depth[i] - 1) / 99 : (100 - 100 / depth[i]) / 99;
            encodedNormals[i] = normal;
        }
        if (motion != nullptr)
            std::memcpy(Motion.Mapped, motion->data(), motion->size() * sizeof((*motion)[0]));
        auto* geometry = static_cast<std::array<uint32_t, 4>*>(Geometry.Mapped);
        for (size_t i = 0; i < depth.size(); ++i)
            geometry[i] = { 0U, FaceNormal, FaceNormal != 0U ? Fast::Renderer3ds::kPicaGeometryGuideKindNative : 0U,
                            0U };
        Check(vkResetCommandPool(Device.Device, Commands, 0));
        VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        Check(vkBeginCommandBuffer(Command, &begin));
        Upload(Depths, kDepth);
        Upload(Normals, kNormals);
        Upload(Motion, kMotion);
        Upload(Geometry, kGeometry);
        for (uint32_t i : { kTiled, kRaw, kFiltered, kOutput })
            Barrier(Images[i],
                    i == kRaw && KeepRaw && RawWritten ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                       : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        RawWritten = true;
        const uint32_t tiledWidth = Fast::Immerse::MxaoTiledExtent(Width);
        const uint32_t tiledHeight = Fast::Immerse::MxaoTiledExtent(Height);
        Dispatch(kDeinterleave, kDeinterleaveSet, constants, 0, tiledWidth, tiledHeight, 8);
        WrittenToRead(kTiled);
        Dispatch(kOcclusion, kOcclusionSet, constants, 0, tiledWidth, tiledHeight, 16);
        WrittenToRead(kRaw);
        uint32_t filterSet = kFilterSet;
        uint32_t resolveSet = kResolveRawSet;
        const uint32_t written = NextHistory;
        if (motion != nullptr) {
            const uint32_t writtenImage = kHistory0 + written, historyImage = kHistory0 + (1 - written);
            Barrier(Images[writtenImage], HistoryLayouts[written], VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            if (HistoryLayouts[1 - written] != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                Barrier(Images[historyImage], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
                        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            HistoryLayouts[1 - written] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            Dispatch(kTemporal, kTemporalSet + written, constants, 0, Width, Height, 8);
            WrittenToRead(writtenImage);
            HistoryLayouts[written] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            filterSet = kFilterAccumulatedSet + written;
            resolveSet = kResolveAccumulatedSet + written;
            NextHistory = 1 - written;
        }
        if (filter == 2) {
            Dispatch(kFilter, filterSet, constants, 0, Width, Height, 8);
            WrittenToRead(kFiltered);
            resolveSet = kResolveFilteredSet;
        }
        if (DebugView != 0)
            Dispatch(kDebug, motion != nullptr ? kResolveAccumulatedSet + written : kResolveRawSet, constants,
                     DebugView << Fast::Immerse::kMxaoDebugViewShift, Width, Height, 8);
        else
            Dispatch(kResolve, resolveSet, constants,
                     filter == 0 ? Fast::Immerse::kMxaoFilterCopy : Fast::Immerse::kMxaoFilterMirrored, Width, Height,
                     8);
        Barrier(Images[kOutput], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { Width, Height, 1 };
        vkCmdCopyImageToBuffer(Command, Images[kOutput].Handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Result.Handle, 1,
                               &copy);
        if (motion != nullptr && accumulation != nullptr)
            Download(kHistory0 + written, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, Accumulation);
        VkMemoryBarrier host{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0,
                             nullptr, 0, nullptr);
        Check(vkEndCommandBuffer(Command));
        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &Command;
        Check(vkQueueSubmit(Device.Queue, 1, &submit, VK_NULL_HANDLE));
        Check(vkQueueWaitIdle(Device.Queue));
        std::vector<float> values(depth.size());
        std::memcpy(values.data(), Result.Mapped, values.size() * sizeof(float));
        const bool packed = (DebugView >= 1 && DebugView <= 3) || DebugView == 9 || DebugView == 10;
        for (float value : values)
            Require(std::isfinite(value) && value >= 0 && value <= (packed ? 2.0F : 1.0F),
                    "nonfinite or out-of-range MXAO visibility");
        if (motion != nullptr && accumulation != nullptr) {
            const auto* halves = static_cast<const uint16_t*>(Accumulation.Mapped);
            accumulation->resize(depth.size());
            for (size_t i = 0; i < depth.size(); ++i)
                for (size_t c = 0; c < 4; ++c)
                    (*accumulation)[i][c] = HalfToFloat(halves[i * 4 + c]);
        }
        return values;
    }

    ~MxaoFixture() {
        vkDestroyCommandPool(Device.Device, Commands, nullptr);
        for (auto pipeline : Pipelines)
            vkDestroyPipeline(Device.Device, pipeline, nullptr);
        vkDestroyPipelineLayout(Device.Device, Layout, nullptr);
        vkDestroyDescriptorPool(Device.Device, Pool, nullptr);
        vkDestroyDescriptorSetLayout(Device.Device, SetLayout, nullptr);
        vkDestroySampler(Device.Device, Sampler, nullptr);
        Device.Free(Depths);
        Device.Free(Normals);
        Device.Free(Motion);
        Device.Free(Result);
        Device.Free(Accumulation);
        Device.Free(Geometry);
        for (const auto& image : Images) {
            vkDestroyImageView(Device.Device, image.View, nullptr);
            vkDestroyImage(Device.Device, image.Handle, nullptr);
            vkFreeMemory(Device.Device, image.Memory, nullptr);
        }
    }
};

float Difference(const std::vector<float>& a, const std::vector<float>& b) {
    float difference = 0;
    for (size_t i = 0; i < a.size(); ++i)
        difference = std::max(difference, std::abs(a[i] - b[i]));
    return difference;
}
float MeanDifference(const std::vector<float>& a, const std::vector<float>& b) {
    double sum = 0;
    for (size_t i = 0; i < a.size(); ++i)
        sum += std::abs(a[i] - b[i]);
    return float(sum / double(a.size()));
}
float Minimum(const std::vector<float>& values) {
    return *std::min_element(values.begin(), values.end());
}
// The display colour of a packed debug normal (AmbientOcclusionDebugShaderLibrary).
std::array<float, 3> Unpack(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return { float((bits >> 15) & 255U) / 255.0F, float((bits >> 7) & 255U) / 255.0F, float(bits & 127U) / 127.0F };
}

// The original's options and the debug views: normals from depth ignore a
// shading normal that tilts away from the depth surface (the renderer's then
// darken the plane), IDGAF, a screen radius, the exponential fade, the shading
// rate keeping skipped pixels, and the views of the inputs.
void TestOriginalOptions(MxaoFixture& gpu, MxaoConstants constants, const std::vector<float>& flat,
                         const std::vector<float>& sloped, const std::vector<float>& scene) {
    using namespace Fast::Immerse;
    const std::vector<float> identity(flat.size(), 1);
    constexpr uint32_t W = MxaoFixture::Width, H = MxaoFixture::Height;
    // A plane tilted 39 degrees whose shading normal faces the viewer (an
    // interpolated normal across a corner): the renderer normal puts half the
    // plane above its tangent plane; the depth normal is the plane's own.
    auto steep = flat;
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x)
            steep[y * W + x] = 10 / (1 - .8F * (2 * (x + .5F) / W - 1));
    const auto interior = [&](const std::vector<float>& values) {
        float minimum = 1;
        for (uint32_t y = 2; y + 2 < H; ++y)
            for (uint32_t x = 2; x + 2 < W; ++x)
                minimum = std::min(minimum, values[y * W + x]);
        return minimum;
    };
    auto rendererNormals = constants;
    rendererNormals.Flags = 3; // Very high
    auto depthNormals = rendererNormals;
    depthNormals.Flags |= kMxaoDepthNormals;
    const float tilted = interior(gpu.Run(steep, rendererNormals, 0, 255, 0));
    const float reconstructed = interior(gpu.Run(steep, depthNormals, 0, 255, 0));
    std::cout << "plane under a facing shading normal: minimum visibility " << tilted << " renderer normals, "
              << reconstructed << " depth normals\n";
    Require(tilted < .95F, "a tilted renderer normal does not self-occlude");
    Require(reconstructed > .998F, "depth normals darken a sloped plane");
    for (uint32_t filter = 0; filter < 3; ++filter) {
        const float gentle = Minimum(gpu.Run(sloped, depthNormals, filter, 255, 0));
        std::cout << "16.7 degree plane, depth normals, filter " << filter << ": minimum visibility " << gentle << '\n';
        Require(gentle > .998F, "depth normals darken a sloped plane");
    }
    Require(Difference(gpu.Run(flat, depthNormals, 2), identity) < .0002F, "depth normals darken a flat plane");
    Require(Minimum(gpu.Run(scene, depthNormals, 1)) < .9F, "depth normals miss the steps");

    auto idgaf = constants;
    idgaf.Flags = 6;
    Require(Difference(gpu.Run(flat, idgaf, 2), identity) < .0002F, "IDGAF darkens an empty plane");
    Require(Minimum(gpu.Run(scene, idgaf, 2)) < .9F, "IDGAF misses the steps");

    auto screen = constants;
    screen.Flags |= kMxaoScreenRadius;
    screen.Response[3] = 2.5F * .03F;
    Require(Difference(gpu.Run(flat, screen, 1), identity) < .0002F, "a screen radius darkens an empty plane");
    // The original's radius 1 / 2.5 / 10: 3.9 / 9.7 / 39 pixels at any depth.
    std::array<float, 3> screenMinimum{};
    for (size_t i = 0; i < 3; ++i) {
        screen.Response[3] = std::array{ 1.0F, 2.5F, 10.0F }[i] * .03F;
        screenMinimum[i] = Minimum(gpu.Run(scene, screen, 1));
    }
    std::cout << "screen radius 1 / 2.5 / 10: minimum visibility " << screenMinimum[0] << " / " << screenMinimum[1]
              << " / " << screenMinimum[2] << '\n';
    Require(screenMinimum[2] < .8F && screenMinimum[1] < screenMinimum[0] && screenMinimum[2] < screenMinimum[1],
            "a screen radius misses the steps");

    // Depth 10 of far 100 is 0.1: a 0.01 fade distance removes it, 1.0 keeps it.
    auto faded = constants;
    faded.Flags |= kMxaoOriginalFade;
    faded.RadiusFade[2] = .01F;
    Require(Difference(gpu.Run(scene, faded, 1), identity) == 0, "the original fade keeps a distant surface");
    faded.RadiusFade[2] = 1;
    Require(Minimum(gpu.Run(scene, faded, 1)) < .9F, "the original fade removes a near surface");

    // Quarter rate after a full frame: phase 0 updates the tiles with even x
    // and y, every other pixel keeps the previous frame's estimate.
    const auto flatReference = gpu.Run(flat, constants, 0);
    gpu.KeepRaw = true;
    const auto shadingRate = [&](uint32_t rate, uint32_t phase, auto estimated) {
        const auto before = gpu.Run(scene, constants, 0);
        auto partial = constants;
        partial.Flags |= rate << kMxaoShadingRateShift | phase << kMxaoShadingPhaseShift;
        const auto after = gpu.Run(flat, partial, 0);
        size_t correct = 0, updated = 0;
        for (uint32_t y = 0; y < H; ++y)
            for (uint32_t x = 0; x < W; ++x) {
                const size_t i = y * W + x;
                const bool fresh = estimated(x % 4, y % 4);
                updated += fresh ? 1 : 0;
                correct += after[i] == (fresh ? flatReference[i] : before[i]) ? 1 : 0;
            }
        return updated != 0 && updated != W * H && correct == W * H;
    };
    Require(shadingRate(2, 0, [](uint32_t x, uint32_t y) { return (x & 1) + (y & 1) * 2 == 0; }),
            "the quarter shading rate, phase 0");
    Require(shadingRate(2, 3, [](uint32_t x, uint32_t y) { return (x & 1) + (y & 1) * 2 == 3; }),
            "the quarter shading rate, phase 3");
    Require(shadingRate(1, 1, [](uint32_t x, uint32_t y) { return ((x + y) & 1) == 1; }),
            "the half shading rate");
    gpu.KeepRaw = false;

    // Debug views. On the sloped plane with its true normal, the renderer and
    // depth normals agree with the encoded guide; with a facing guide they
    // disagree by the slope angle.
    const float length = std::sqrt(1 + .3F * .3F);
    const std::array<float, 3> expected{ .5F + .5F * .3F / length, .5F, .5F + .5F / length };
    size_t checked = 0;
    for (uint32_t view : { 2U, 3U }) {
        gpu.DebugView = view;
        const auto normals = gpu.Run(sloped, depthNormals, 0, 255, .3F);
        for (uint32_t y = 2; y + 2 < H; ++y)
            for (uint32_t x = 2; x + 2 < W; ++x) {
                const auto color = Unpack(normals[y * W + x]);
                for (size_t c = 0; c < 3; ++c)
                    Require(std::abs(color[c] - expected[c]) < (c == 2 ? 3.0F / 127 : 3.0F / 255),
                            view == 2 ? "renderer normal view" : "depth normal view");
                ++checked;
            }
    }
    gpu.DebugView = 1;
    const auto used = gpu.Run(sloped, depthNormals, 0, 255, 0);
    Require(std::abs(Unpack(used[(H / 2) * W + W / 2])[0] - expected[0]) < 3.0F / 255,
            "the normals in use are not the selected source");
    gpu.DebugView = 4;
    const auto agree = gpu.Run(sloped, depthNormals, 0, 255, .3F);
    const auto disagree = gpu.Run(sloped, depthNormals, 0, 255, 0);
    const float angle = std::atan(.3F) / (3.14159265F * .5F);
    Require(agree[(H / 2) * W + W / 2] < .02F, "agreeing normals disagree");
    Require(std::abs(disagree[(H / 2) * W + W / 2] - angle) < .02F, "the disagreement is not the slope angle");
    // Depth 10 inside a 0..50 fade: 0.8 faded, 0.2 of the range.
    auto fadeView = constants;
    fadeView.RadiusFade[2] = 0;
    fadeView.RadiusFade[3] = 50;
    gpu.DebugView = 7;
    Require(Difference(gpu.Run(flat, fadeView, 0), std::vector<float>(flat.size(), .8F)) < 1e-5F,
            "the fade view");
    gpu.DebugView = 5;
    Require(Difference(gpu.Run(flat, fadeView, 0), std::vector<float>(flat.size(), .2F)) < 1e-5F,
            "the depth view");
    gpu.DebugView = 0;
    std::cout << "original options: depth normals, IDGAF, screen radius, exponential fade, shading rates and "
              << checked << " debug normals passed\n";
}

// Face normals from the geometry guide: the exact plane of the surface, equal
// to the depth normal on planes, from depth where the guide has none, and the
// views that compare them.
void TestGeometryNormals(MxaoFixture& gpu, MxaoConstants constants, const std::vector<float>& flat,
                         const std::vector<float>& sloped, const std::vector<float>& scene) {
    using namespace Fast::Immerse;
    using Fast::Renderer3ds::EncodePicaGeometryGuideNormal;
    constexpr uint32_t W = MxaoFixture::Width, H = MxaoFixture::Height;
    const std::vector<float> identity(flat.size(), 1);
    auto depthNormals = constants;
    depthNormals.Flags = 3 | kMxaoDepthNormals; // Very high
    auto geometryNormals = constants;
    geometryNormals.Flags = 3 | kMxaoGeometryNormals;
    // View space, Z toward the viewer: the sloped plane's own normal, and the
    // normal of the fronto-parallel steps.
    const float length = std::sqrt(1 + .3F * .3F);
    const uint32_t slopedFace = EncodePicaGeometryGuideNormal({ .3F / length, 0, 1 / length });
    const uint32_t frontFace = EncodePicaGeometryGuideNormal({ 0, 0, 1 });

    // Without a face normal the source is the depth normal, bit for bit.
    gpu.FaceNormal = 0;
    Require(Difference(gpu.Run(scene, geometryNormals, 2), gpu.Run(scene, depthNormals, 2)) == 0,
            "without a face normal the geometry source is not the depth normal");

    // On planes the face normal is the depth plane: no self-occlusion.
    gpu.FaceNormal = slopedFace;
    const float slopedGap = Difference(gpu.Run(sloped, geometryNormals, 0), gpu.Run(sloped, depthNormals, 0));
    Require(Minimum(gpu.Run(sloped, geometryNormals, 2)) > .998F, "face normals darken a sloped plane");
    gpu.FaceNormal = frontFace;
    Require(Difference(gpu.Run(flat, geometryNormals, 2), identity) < .0002F, "face normals darken a flat plane");
    const auto stepsFace = gpu.Run(scene, geometryNormals, 2);
    const auto stepsDepth = gpu.Run(scene, depthNormals, 2);
    Require(Minimum(stepsFace) < .9F, "face normals miss the steps");
    std::cout << "face normals: sloped plane differs from depth normals by " << slopedGap
              << "; steps minimum visibility " << Minimum(stepsFace) << " (depth normals " << Minimum(stepsDepth)
              << "), mean difference " << MeanDifference(stepsFace, stepsDepth) << '\n';
    Require(slopedGap < .001F, "face and depth normals disagree on a plane");

    // Views: the face normal itself, and its angle to the depth normal over
    // 10 degrees (grey), magenta where there is none.
    gpu.FaceNormal = slopedFace;
    gpu.DebugView = 9;
    const auto shown = Unpack(gpu.Run(sloped, depthNormals, 0)[(H / 2) * W + W / 2]);
    const std::array<float, 3> expected{ .5F + .5F * .3F / length, .5F, .5F + .5F / length };
    for (size_t c = 0; c < 3; ++c)
        Require(std::abs(shown[c] - expected[c]) < (c == 2 ? 2.0F / 127 : 2.0F / 255), "the face normal view");
    gpu.DebugView = 10;
    const auto agree = gpu.Run(sloped, depthNormals, 0);
    float worst = 0;
    for (uint32_t y = 2; y + 2 < H; ++y)
        for (uint32_t x = 2; x + 2 < W; ++x)
            worst = std::max(worst, Unpack(agree[y * W + x])[0]);
    std::cout << "face vs depth normals on the sloped plane: at most " << worst * 10 << " degrees\n";
    Require(worst <= 3.0F / 255, "face and depth normals of a plane are more than 0.12 degrees apart");
    gpu.FaceNormal = frontFace;
    const auto apart = Unpack(gpu.Run(sloped, depthNormals, 0)[(H / 2) * W + W / 2]);
    Require(apart[0] > .99F && apart[1] > .99F, "a 16.7 degree disagreement is not saturated");
    gpu.FaceNormal = 0;
    const auto none = Unpack(gpu.Run(sloped, depthNormals, 0)[(H / 2) * W + W / 2]);
    Require(none[0] > .99F && none[1] < .01F && none[2] > .99F, "a missing face normal is not magenta");
    gpu.DebugView = 1;
    gpu.FaceNormal = slopedFace;
    const auto used = Unpack(gpu.Run(sloped, geometryNormals, 0)[(H / 2) * W + W / 2]);
    Require(std::abs(used[0] - expected[0]) < 2.0F / 255, "the normals in use are not the face normals");
    gpu.DebugView = 0;
    gpu.FaceNormal = 0;
}

// Temporal accumulation: an invalid history reproduces the original filter
// input, a static view converges towards more samples, a mismatched depth or
// an exceeded neighbourhood is rejected, and motion reprojects the history.
void TestTemporal(MxaoFixture& gpu, MxaoConstants constants, const std::vector<float>& scene) {
    using namespace Fast::Immerse;
    constexpr size_t count = MxaoFixture::Width * MxaoFixture::Height;
    const std::vector<std::array<float, 4>> still(count, { 0, 0, 0, 0 });
    std::vector<std::array<float, 4>> accumulation;
    // The identity transform: a static camera keeps each point's depth.
    std::array<float, 16> identity{};
    for (size_t i = 0; i < 4; ++i)
        identity[i * 5] = 1;
    std::array<float, 16> projection = identity;
    projection[10] = -1;
    projection[11] = -1;
    projection[14] = 0;
    projection[15] = 0;
    const auto row = MxaoPreviousDepthRow(identity, identity, projection, 100);
    // Clip w of view z is -z; MXAO's p.z is -z: the row returns p.z / far.
    Require(row[0] == 0 && row[1] == 0 && std::abs(row[2] - .01F) < 1e-7F && row[3] == 0,
            "previous depth row of a static view");
    Require(MxaoTemporalPhase(0) == 0 && MxaoTemporalPhase(1) == 8 && MxaoTemporalPhase(2) == 4 &&
                MxaoTemporalPhase(15) == 15 && MxaoTemporalPhase(16) == 0,
            "bit-reversed jitter phases");

    auto first = constants;
    first.Temporal = { 0, kMxaoTemporalHistoryLength, kMxaoTemporalDepthTolerance, kMxaoTemporalMovingHistoryLength };
    first.PreviousDepth = row;
    for (uint32_t filter = 0; filter < 3; ++filter) {
        Require(Difference(gpu.Run(scene, first, filter, 255, 0, &still, &accumulation), gpu.Run(scene, constants, filter)) ==
                    0,
                "an invalid history changes the filtered estimate");
        for (const auto& texel : accumulation)
            Require(texel[2] == 1, "an invalid history is not restarted");
    }

    // A static view: the rotating phases alone flicker from frame to frame,
    // their accumulation settles; both are compared with a dense estimate.
    auto reference = constants;
    reference.Flags = 5; // Extreme
    const auto dense = gpu.Run(scene, reference, 2);
    std::vector<float> single, accumulated, previousSingle, previousAccumulated;
    float singleFlicker = 0, accumulatedFlicker = 0;
    for (uint32_t frame = 0; frame < 24; ++frame) {
        auto rotated = constants;
        rotated.Flags = constants.Flags | MxaoTemporalPhase(frame) << kMxaoJitterPhaseShift;
        auto temporal = first;
        temporal.Temporal[0] = frame == 0 ? 0.0F : 1.0F;
        temporal.Flags = rotated.Flags;
        single = gpu.Run(scene, rotated, 2);
        accumulated = gpu.Run(scene, temporal, 2, 255, 0, &still, &accumulation);
        if (frame >= 16) {
            singleFlicker += MeanDifference(single, previousSingle);
            accumulatedFlicker += MeanDifference(accumulated, previousAccumulated);
        }
        previousSingle = single;
        previousAccumulated = accumulated;
    }
    size_t full = 0;
    for (const auto& texel : accumulation)
        full += texel[2] == kMxaoTemporalHistoryLength ? 1 : 0;
    const float singleError = MeanDifference(single, dense), temporalError = MeanDifference(accumulated, dense);
    std::cout << "temporal: history full on " << full << " of " << count << " pixels; frame-to-frame change "
              << singleFlicker / 8 << " rotating, " << accumulatedFlicker / 8 << " accumulated; mean error to "
              << "Extreme " << singleError << " single, " << temporalError << " accumulated\n";
    Require(full > count * 9 / 10, "a static view does not accumulate");
    Require(accumulatedFlicker < singleFlicker * .25F, "accumulation does not settle the rotating phases");
    Require(temporalError <= singleError, "accumulation moves away from the dense estimate");

    // A history at another depth (the scene moved 3 units away) is rejected.
    auto farther = scene;
    for (auto& value : farther)
        value += 3;
    auto rejected = first;
    rejected.Temporal[0] = 1;
    gpu.Run(farther, rejected, 1, 255, 0, &still, &accumulation);
    size_t restarted = 0;
    for (const auto& texel : accumulation)
        restarted += texel[2] == 1 ? 1 : 0;
    Require(restarted > count * 99 / 100, "a history at another depth is reused");

    // A fully occluded history over an unoccluded plane at the same depth is
    // clamped to this frame's range instead of being blended in.
    const std::vector<float> flat(count, 10);
    auto occluding = first;
    occluding.Response[0] = 2;
    gpu.Run(scene, occluding, 1, 255, 0, &still, &accumulation);
    auto open = first;
    open.Temporal[0] = 1;
    open.Response[0] = 0;
    gpu.Run(scene, open, 1, 255, 0, &still, &accumulation);
    for (const auto& texel : accumulation)
        Require(texel[0] == 0, "an occluded history survives an unoccluded frame");

    // Motion: the scene moves two texels right; its history follows it, the
    // revealed column restarts.
    auto shifted = scene;
    for (uint32_t y = 0; y < MxaoFixture::Height; ++y)
        for (uint32_t x = 0; x < MxaoFixture::Width; ++x)
            shifted[y * MxaoFixture::Width + x] = scene[y * MxaoFixture::Width + (x >= 2 ? x - 2 : 0)];
    std::vector<std::array<float, 4>> moving(count, { -2.0F / MxaoFixture::Width, 0, 0, 0 });
    for (uint32_t y = 0; y < MxaoFixture::Height; ++y)
        for (uint32_t x = 0; x < 2; ++x)
            moving[y * MxaoFixture::Width + x] = { 0, 0, 1, 0 };
    gpu.Run(scene, first, 1, 255, 0, &still, &accumulation);
    auto follow = first;
    follow.Temporal[0] = 1;
    gpu.Run(shifted, follow, 1, 255, 0, &moving, &accumulation);
    size_t followed = 0, interior = 0, revealed = 0;
    for (uint32_t y = 1; y + 1 < MxaoFixture::Height; ++y)
        for (uint32_t x = 0; x < MxaoFixture::Width; ++x) {
            const float history = accumulation[y * MxaoFixture::Width + x][2];
            if (x < 2)
                revealed += history == 1 ? 1 : 0;
            else if (x + 1 < MxaoFixture::Width) {
                ++interior;
                followed += history == 2 ? 1 : 0;
            }
        }
    Require(revealed == 2 * (MxaoFixture::Height - 2), "the revealed column reuses a history");
    Require(followed > interior * 95 / 100, "the moved scene loses its history");
}
} // namespace

int main() try {
    MxaoFixture gpu;
    MxaoConstants constants;
    constants.Width = MxaoFixture::Width;
    constants.Height = MxaoFixture::Height;
    constants.Flags = 1; // Medium
    constants.ViewX = { 2, 0, -1, 0 };
    constants.ViewY = { 0, 2, -1, 0 };
    constants.Depth = { 100, 100, 99, 0 };
    constants.RadiusFade = { 3, std::log(4.0F) / 3.0F, 50, 100 };
    constants.Response = { 1, 1.0F / 100.0F, 4.0F / (255.0F * 255.0F), 0 };
    const std::vector<float> flat(MxaoFixture::Width * MxaoFixture::Height, 10), identity(flat.size(), 1);
    auto sloped = flat;
    for (uint32_t y = 0; y < MxaoFixture::Height; ++y)
        for (uint32_t x = 0; x < MxaoFixture::Width; ++x)
            sloped[y * MxaoFixture::Width + x] = 10 / (1 - .3F * (2 * (x + .5F) / MxaoFixture::Width - 1));
    auto scene = flat;
    for (uint32_t y = 15; y < 80; ++y)
        for (uint32_t x = 15; x < 110; ++x) {
            if (x < 40)
                scene[y * MxaoFixture::Width + x] = 9.8F;
            else if (x > 75)
                scene[y * MxaoFixture::Width + x] = 8.5F;
        }
    for (uint32_t method = 0; method < 4; ++method) {
        auto variant = constants;
        variant.Method = method;
        for (uint32_t filter = 0; filter < 3; ++filter) {
            // Failed by 0.163 (ground truth, slice-count normalization) and by
            // one truncated half-precision step (stored visibility) before.
            Require(Difference(gpu.Run(flat, variant, filter), identity) < .0002F, "RGB8 empty plane darkens");
            Require(Difference(gpu.Run(sloped, variant, filter, 255, .3F), identity) < .001F,
                    "RGB8 sloped plane darkens");
            const auto occluded = gpu.Run(scene, variant, filter);
            std::cout << "method " << method << " filter " << filter << " minimum visibility " << Minimum(occluded)
                      << '\n';
            Require(Minimum(occluded) < .9F, "steps are not occluded");
            Require(Difference(occluded, gpu.Run(scene, variant, filter)) == 0, "MXAO depends on previous contents");
        }
        Require(Difference(gpu.Run(scene, variant, 1, 0), identity) == 0, "missing scene coverage darkens");
        auto disabled = variant;
        disabled.Response[0] = 0;
        Require(Difference(gpu.Run(scene, disabled, 1), identity) == 0, "zero amount alters visibility");
        auto faded = variant;
        faded.RadiusFade[2] = 1;
        faded.RadiusFade[3] = 2;
        Require(Difference(gpu.Run(scene, faded, 1), identity) == 0, "faded distance still darkens");
        auto deeper = variant;
        deeper.Response[0] = 2;
        const auto base = gpu.Run(scene, variant, 1), gamma = gpu.Run(scene, deeper, 1);
        for (size_t i = 0; i < base.size(); ++i)
            Require(gamma[i] <= base[i] + 1.0e-6F, "an amount above 1 lightens occlusion");
        auto wBuffer = variant;
        wBuffer.Depth = { 1, 100, 99, 1 };
        Require(Difference(base, gpu.Run(scene, wBuffer, 1)) < .002F, "W-buffer and perspective reconstruction disagree");
    }
    TestTemporal(gpu, constants, scene);
    TestOriginalOptions(gpu, constants, flat, sloped, scene);
    TestGeometryNormals(gpu, constants, flat, sloped, scene);
    std::cout << "MXAO GPU: RGB8 planes, occlusion for four methods and three filters, coverage, amount, fade, "
                 "W-buffer, odd extents, repeatability and temporal accumulation passed\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
