#ifdef ENABLE_OOT3D_VULKAN
#include "fast/backends/gfx_vulkan.h"
#include "fast/oot3d/pica_grass_texture_coordinates.h"
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/nri_pica_display_copy_pass.h"
#include "fast/renderer3ds/pica_framebuffer_encode.h"
#include "fast/renderer3ds/pica_framebuffer_encode_gpu.h"
#include "fast/renderer3ds/pica_texture_copy_plan.h"
#include "fast/renderer/spirv_cache.h"
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace Fast {
namespace {
void RequireCopyVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(operation);
}

// A framebuffer encode format and a texture format that describe the same
// packed native bytes. Formats 2 and 3 swap meaning between the two sides
// (RGB565 and RGBA5551); the byte layouts are identical, which is what a
// byte-exact surface copy needs.
uint8_t PicaTextureFormatFromFramebufferFormat(uint8_t framebufferFormat) {
    static constexpr std::array<uint8_t, 5> kTextureFormat{0U, 1U, 3U, 2U, 4U};
    return framebufferFormat < kTextureFormat.size()
               ? kTextureFormat[framebufferFormat]
               : 0xFFU;
}

bool PicaFramebufferFormatMatchesTextureFormat(uint8_t framebufferFormat,
                                               uint8_t textureFormat) {
    return PicaTextureFormatFromFramebufferFormat(framebufferFormat) ==
           textureFormat;
}
}
uint64_t GfxRenderingAPIVulkan::GpuTextureCopyReferenceIdentity(
    const GfxNativePicaTextureView& texture, uint64_t renderTargetNamespace) {
    // Deterministic over the resolved producer set: a lookup, a re-lookup and a
    // restored plan that re-emits the same references all agree on one identity,
    // and no per-lookup serial ever enters the texture key.
    std::vector<std::array<uint64_t, 4>> entries;
    entries.reserve(texture.CopyReferences.size());
    for (const auto& reference : texture.CopyReferences) {
        entries.push_back({reference.CompletionId,
                           reference.AfterDrawSubmissionId,
                           reference.PhysicalAddress, reference.ByteCount});
    }
    std::sort(entries.begin(), entries.end());
    uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](uint64_t value) {
        for (uint32_t byte = 0U; byte < 8U; ++byte) {
            hash ^= (value >> (byte * 8U)) & 0xFFULL;
            hash *= 1099511628211ULL;
        }
    };
    mix(renderTargetNamespace);
    mix(texture.PhysicalAddress);
    mix(texture.Width);
    mix(texture.Height);
    mix(texture.NativeFormat);
    for (const auto& entry : entries) {
        for (const uint64_t value : entry) {
            mix(value);
        }
    }
    return hash == 0U ? 1U : hash;
}

bool GfxRenderingAPIVulkan::BuildGpuTextureCopyDerivation(
    const GfxNativePicaTextureView& texture, uint64_t renderTargetNamespace,
    GpuTextureCopyDerivation& derivation, GpuTextureCopyFallback* fallback,
    std::string* error) const {
    derivation = {};
    const auto fail = [&](GpuTextureCopyFallback reason, const std::string& message) {
        if (fallback != nullptr) *fallback = reason;
        if (error != nullptr) *error = message;
        return false;
    };
    if (fallback != nullptr) *fallback = GpuTextureCopyFallback::None;
    if (texture.CopyReferences.empty()) {
        return fail(GpuTextureCopyFallback::Layout,
                    "the view declares no producer references");
    }
    if (texture.Width == 0U || texture.Height == 0U) {
        return fail(GpuTextureCopyFallback::Layout,
                    "the sampled texture has no extent");
    }
    if (NativePicaMipLevelCount(texture) != 1U) {
        return fail(GpuTextureCopyFallback::MipChain,
                    "a mimapped copied texture is not derivable");
    }
    const auto encodedBytes = NativePicaEncodedMipSize(
        texture.NativeFormat, texture.Width, texture.Height);
    if (!encodedBytes.has_value() || *encodedBytes == 0U) {
        return fail(GpuTextureCopyFallback::Layout,
                    "the sampled texture has no native byte size");
    }
    // Group the references by producer and translate each one back to the source
    // bytes it came from, using that producer's own copy walk. A texture may be
    // composed from several producers, and each reference is only the subrange
    // its producer still owns.
    std::vector<std::vector<Renderer3ds::PicaRawCopySpan>> groupedSpans;
    std::vector<const GpuTextureCopySurface*> groupedSurfaces;
    for (const auto& reference : texture.CopyReferences) {
        if (reference.ByteCount == 0U ||
            reference.PhysicalAddress < texture.PhysicalAddress ||
            uint64_t{reference.PhysicalAddress} + reference.ByteCount >
                uint64_t{texture.PhysicalAddress} + *encodedBytes) {
            return fail(GpuTextureCopyFallback::Layout,
                        "a producer reference lies outside the sampled texture");
        }
        auto producer = mGpuTextureCopyProducers.find(
            {renderTargetNamespace, reference.CompletionId,
             reference.AfterDrawSubmissionId});
        if (producer == mGpuTextureCopyProducers.end() &&
            mCanonicalRenderTargetNamespace.has_value() &&
            *mCanonicalRenderTargetNamespace != renderTargetNamespace) {
            // One guest operation that only the canonical namespace executed:
            // a superseded frame runs its native work but is never presented,
            // and a later presented frame (a feedback history) still samples
            // its result. Only the canonical version may be imported; a
            // canonical read never takes a presentation version.
            producer = mGpuTextureCopyProducers.find(
                {*mCanonicalRenderTargetNamespace, reference.CompletionId,
                 reference.AfterDrawSubmissionId});
        }
        if (producer == mGpuTextureCopyProducers.end()) {
            return fail(GpuTextureCopyFallback::UnknownProducer,
                        "the producer version is not registered in this "
                        "namespace");
        }
        const auto surface = mGpuTextureCopySurfaces.find(producer->second);
        if (surface == mGpuTextureCopySurfaces.end()) {
            return fail(GpuTextureCopyFallback::UnknownProducer,
                        "the producer surface is gone");
        }
        const auto& candidate = surface->second;
        if (candidate.Image == VK_NULL_HANDLE) {
            return fail(GpuTextureCopyFallback::UncapturedProducer,
                        "the producer has no captured GPU copy");
        }
        if (!PicaFramebufferFormatMatchesTextureFormat(candidate.SourceFormat,
                                                       texture.NativeFormat)) {
            return fail(GpuTextureCopyFallback::Layout,
                        "the declared native format differs from the copy");
        }
        Renderer3ds::PicaRawCopySpan mapped;
        bool found = false;
        for (const auto& span : candidate.Spans) {
            if (reference.PhysicalAddress < span.DestinationAddress ||
                uint64_t{reference.PhysicalAddress} + reference.ByteCount >
                    uint64_t{span.DestinationAddress} + span.ByteCount) {
                continue;
            }
            if (found) {
                return fail(GpuTextureCopyFallback::Layout,
                            "a producer reference spans several copy spans");
            }
            mapped.SourceAddress =
                span.SourceAddress +
                (reference.PhysicalAddress - span.DestinationAddress);
            mapped.DestinationAddress = reference.PhysicalAddress;
            mapped.ByteCount = reference.ByteCount;
            found = true;
        }
        if (!found) {
            return fail(GpuTextureCopyFallback::Layout,
                        "a producer reference does not lie in a copy span");
        }
        size_t index = 0U;
        const auto found2 = std::find(groupedSurfaces.begin(),
                                      groupedSurfaces.end(), &candidate);
        if (found2 == groupedSurfaces.end()) {
            derivation.Groups.push_back(
                GpuTextureCopyDerivation::Group{candidate.Identity, {}});
            groupedSpans.emplace_back();
            groupedSurfaces.push_back(&candidate);
            index = derivation.Groups.size() - 1U;
        } else {
            index = static_cast<size_t>(
                std::distance(groupedSurfaces.begin(), found2));
        }
        groupedSpans[index].push_back(mapped);
    }
    // The shared planner turns each producer's native byte spans into texel
    // regions and keeps every untouched gap, so a partial or strided copy maps
    // exactly instead of being widened to a whole surface.
    const uint8_t framebufferFormat = groupedSurfaces.front()->SourceFormat;
    for (size_t index = 0U; index < derivation.Groups.size(); ++index) {
        const GpuTextureCopySurface& producer = *groupedSurfaces[index];
        const uint32_t bytesPerPixel =
            framebufferFormat == 0U ? 4U : framebufferFormat == 1U ? 3U : 2U;
        std::vector<Renderer3ds::PicaTextureCopyRegion> regions;
        std::string planError;
        const Renderer3ds::PicaTiledCopySurface source{
            producer.SourceSurfaceAddress, producer.SourceWidth,
            producer.SourceHeight, bytesPerPixel, framebufferFormat};
        const Renderer3ds::PicaTiledCopySurface destination{
            texture.PhysicalAddress, texture.Width, texture.Height,
            bytesPerPixel, framebufferFormat};
        if (!Renderer3ds::BuildPicaTiledTextureCopyRegions(
                groupedSpans[index], source, destination, regions,
                &planError)) {
            return fail(GpuTextureCopyFallback::Layout,
                        "the copy's byte walk does not map onto the sampled "
                        "texture: " +
                            planError);
        }
        if (regions.empty()) {
            return fail(GpuTextureCopyFallback::Layout,
                        "the copy's owned ranges cover no texel");
        }
        derivation.Groups[index].Regions = std::move(regions);
    }
    // Density policy: per axis, the destination must carry at least the largest
    // internal-per-native ratio among the producers, rounded up, so a scaled
    // source is never reduced to the declared native layout and anisotropic or
    // non-integral scales (for example a native 480x400 source rendered at
    // 1440x1280) stay at internal-or-higher density per axis.
    uint32_t densityX = 1U;
    uint32_t densityY = 1U;
    bool scaledSource = false;
    for (const GpuTextureCopySurface* producer : groupedSurfaces) {
        if (producer->SourceWidth == 0U || producer->SourceHeight == 0U) {
            return fail(GpuTextureCopyFallback::Layout,
                        "the producer has no declared native geometry");
        }
        densityX = std::max(
            densityX, std::max(1U, (producer->ImageWidth +
                                    producer->SourceWidth - 1U) /
                                       producer->SourceWidth));
        densityY = std::max(
            densityY, std::max(1U, (producer->ImageHeight +
                                    producer->SourceHeight - 1U) /
                                       producer->SourceHeight));
        scaledSource =
            scaledSource || producer->ImageWidth > producer->SourceWidth ||
            producer->ImageHeight > producer->SourceHeight;
    }
    const uint32_t imageWidth = texture.Width * densityX;
    const uint32_t imageHeight = texture.Height * densityY;
    // Policy guard on the real numbers: a scaled source must never be served at
    // the declared native size, because that is exactly the downgrade the
    // fidelity requirement forbids.
    if (scaledSource &&
        (imageWidth <= texture.Width || imageHeight <= texture.Height)) {
        return fail(GpuTextureCopyFallback::Layout,
                    "the copy's internal density would collapse to the declared "
                    "native layout");
    }
    // Whether the declared layout is fully owned, so the sampled image carries
    // no CPU-owned pixel and its content is entirely the producers'.
    uint64_t coveredTexels = 0U;
    for (const auto& group : derivation.Groups) {
        for (const auto& region : group.Regions) {
            coveredTexels += uint64_t{region.Width} * region.Height;
        }
    }
    derivation.CoversDeclaredLayout =
        coveredTexels >= uint64_t{texture.Width} * texture.Height;
    uint32_t ownedX0 = 0xFFFFFFFFU;
    uint32_t ownedY0 = 0xFFFFFFFFU;
    for (const auto& group : derivation.Groups) {
        for (const auto& region : group.Regions) {
            ownedX0 = std::min(ownedX0, region.DestinationX);
            ownedY0 = std::min(ownedY0, region.DestinationY);
            derivation.OwnedX1 = std::max(derivation.OwnedX1,
                                          region.DestinationX + region.Width);
            derivation.OwnedY1 = std::max(derivation.OwnedY1,
                                          region.DestinationY + region.Height);
        }
    }
    derivation.OwnedX0 = ownedX0 == 0xFFFFFFFFU ? 0U : ownedX0;
    derivation.OwnedY0 = ownedY0 == 0xFFFFFFFFU ? 0U : ownedY0;
    derivation.Encoding = groupedSurfaces.front()->Encoding;
    derivation.SceneEffectsResolved = true;
    derivation.FirstProducerCompletionId =
        groupedSurfaces.front()->CompletionId;
    for (const GpuTextureCopySurface* producer : groupedSurfaces) {
        if (producer->SceneEffectsResolved == false ||
            producer->Encoding != derivation.Encoding) {
            derivation.SceneEffectsResolved = false;
            break;
        }
    }
    derivation.DensityX = densityX;
    derivation.DensityY = densityY;
    derivation.DeclaredWidth = texture.Width;
    derivation.DeclaredHeight = texture.Height;
    derivation.ImageWidth = imageWidth;
    derivation.ImageHeight = imageHeight;
    return true;
}

bool GfxRenderingAPIVulkan::RecordGpuTextureCopyBaseFill(
    const GpuTextureCopyDerivation& derivation, VkImage baseImage,
    TextureRecord& record) {
    if (baseImage == VK_NULL_HANDLE || record.Image == VK_NULL_HANDLE ||
        derivation.DeclaredWidth == 0U || derivation.DeclaredHeight == 0U) {
        return false;
    }
    Oot3d::NriPicaDisplayCopyRegionDesc desc;
    desc.FrameIndex = mCurrentFrame;
    desc.FrameId = mFrameCounter;
    desc.SourceImage = baseImage;
    desc.SourceFormat = VK_FORMAT_R8G8B8A8_UNORM;
    // The base is the decoded native layout, so its own extent is the space the
    // expansion samples from.
    desc.SourceWidth = derivation.DeclaredWidth;
    desc.SourceHeight = derivation.DeclaredHeight;
    desc.DestinationImage = record.Image;
    desc.DestinationFormat = VK_FORMAT_R8G8B8A8_UNORM;
    desc.DestinationWidth = record.Width;
    desc.DestinationHeight = record.Height;
    desc.DestinationInitialized = false;
    desc.DestinationX = 0U;
    desc.DestinationY = 0U;
    desc.Width = record.Width;
    desc.Height = record.Height;
    desc.NativeDeltaX = 0;
    desc.NativeDeltaY = 0;
    desc.DensityX = derivation.DensityX;
    desc.DensityY = derivation.DensityY;
    desc.SourceNativeWidth = derivation.DeclaredWidth;
    desc.SourceNativeHeight = derivation.DeclaredHeight;
    // RGBA8 pass-through: the base is already the decoded native pixel, so no
    // native format semantics may be applied a second time.
    desc.NativeFormat = 0U;
    if (!mNriPicaTextureCopyDerivePass.ExecuteRegion(desc)) {
        throw std::runtime_error(
            "raw PICA copy base fill dispatch was refused");
    }
    return true;
}

bool GfxRenderingAPIVulkan::RecordGpuTextureCopyDerivation(
    const GpuTextureCopyDerivation& derivation, TextureRecord& record) {
    if (record.Image == VK_NULL_HANDLE || derivation.Groups.empty()) {
        return false;
    }
    uint64_t regions = 0U;
    for (const auto& group : derivation.Groups) {
        const auto found = mGpuTextureCopySurfaces.find(group.SurfaceIdentity);
        if (found == mGpuTextureCopySurfaces.end()) {
            return false;
        }
        auto& surface = found->second;
        // Keep the capture alive for as long as this frame's command buffer can
        // still read it.
        surface.LastReferencedFrame = mFrameCounter;
        record.CopyProducerIdentities.push_back(group.SurfaceIdentity);
        for (const auto& region : group.Regions) {
            Oot3d::NriPicaDisplayCopyRegionDesc desc;
            desc.FrameIndex = mCurrentFrame;
            desc.FrameId = mFrameCounter;
            desc.SourceImage = surface.Image;
            desc.SourceFormat = VK_FORMAT_R8G8B8A8_UNORM;
            desc.SourceWidth = surface.ImageWidth;
            desc.SourceHeight = surface.ImageHeight;
            desc.DestinationImage = record.Image;
            desc.DestinationFormat = VK_FORMAT_R8G8B8A8_UNORM;
            desc.DestinationWidth = record.Width;
            desc.DestinationHeight = record.Height;
            // Partial views already have a base fill. Fully GPU-owned views
            // start undefined; only later regions preserve prior dispatches.
            desc.DestinationInitialized =
                !derivation.CoversDeclaredLayout || regions != 0U;
            desc.DestinationX = region.DestinationX * derivation.DensityX;
            desc.DestinationY = region.DestinationY * derivation.DensityY;
            desc.Width = region.Width * derivation.DensityX;
            desc.Height = region.Height * derivation.DensityY;
            // The region is a pure translation in native texel units; the
            // shader turns each destination texel of the derived density into a
            // fractional position inside the capture.
            desc.NativeDeltaX =
                static_cast<int32_t>(region.SourceX) -
                static_cast<int32_t>(region.DestinationX);
            desc.NativeDeltaY =
                static_cast<int32_t>(region.SourceY) -
                static_cast<int32_t>(region.DestinationY);
            // The capture keeps the attachment's bottom-up rows; the region's
            // rows are native memory rows.
            desc.SourceAttachmentRows = true;
            desc.DensityX = derivation.DensityX;
            desc.DensityY = derivation.DensityY;
            desc.SourceNativeWidth = surface.SourceWidth;
            desc.SourceNativeHeight = surface.SourceHeight;
            desc.NativeFormat = surface.SourceFormat;
            if (!mNriPicaTextureCopyDerivePass.ExecuteRegion(desc)) {
                throw std::runtime_error(
                    "raw PICA copy derivation dispatch was refused");
            }
            ++regions;
        }
    }
    mGpuTextureCopySamplerCost.Regions += regions;
    // Numeric density report so the policy can be asserted from a run instead of
    // inferred: a scaled source must show a derived extent above the declared
    // layout, at internal-or-higher density per axis.
    static uint32_t densityReports = 0U;
    if (densityReports < 4U) {
        ++densityReports;
        const GpuTextureCopySurface& first =
            mGpuTextureCopySurfaces.at(
                derivation.Groups.front().SurfaceIdentity);
        std::fprintf(stderr,
                     "TRIAEVUM_GPU_COPY_DENSITY native=%ux%u internal=%ux%u "
                     "declared=%ux%u derived=%ux%u density=%ux%u\n",
                     first.SourceWidth, first.SourceHeight, first.ImageWidth,
                     first.ImageHeight, derivation.DeclaredWidth,
                     derivation.DeclaredHeight, record.Width, record.Height,
                     derivation.DensityX, derivation.DensityY);
    }
    return regions != 0U;
}

bool GfxRenderingAPIVulkan::SetPinnedPicaTextureCopies(
    std::span<const Renderer3ds::PicaTextureCopyReference> retained,
    std::string* error) {
    // Pin updates may arrive while command buffers still use older producers.
    // Frame fences, not pin updates, authorize reclamation.
    std::set<std::pair<uint64_t, uint64_t>> pins;
    for (const auto& reference : retained) {
        // A copy's completion id identifies every version of it, across
        // namespaces; the subranges are informational.
        for (const auto& [producer, identity] : mGpuTextureCopyProducers) {
            if (std::get<1>(producer) == reference.CompletionId) {
                pins.insert({std::get<0>(producer), std::get<1>(producer)});
            }
        }
    }
    mGpuTextureCopyPins = std::move(pins);
    mGpuTextureCopyPinsDeclared = true;
    (void)error;
    return true;
}

void GfxRenderingAPIVulkan::RetireGpuTextureCopyProducers(uint64_t completedFrame) {
    std::vector<uint64_t> retired;
    for (const auto& [identity, surface] : mGpuTextureCopySurfaces) {
        if (surface.CapturedFrame > completedFrame ||
            surface.LastReferencedFrame > completedFrame ||
            (surface.GuestOwner && surface.GuestOwner.use_count() > 1))
            continue;
        if (mGpuTextureCopyPinsDeclared) {
            if (mGpuTextureCopyPins.contains({surface.RenderTargetNamespace, surface.CompletionId}))
                continue;
        } else {
            const auto current = mHandledNativePicaTextureCopies.find(
                {surface.RenderTargetNamespace, surface.DestinationAddress});
            if (current != mHandledNativePicaTextureCopies.end() &&
                current->second.first == surface.CompletionId) continue;
        }
        retired.push_back(identity);
    }
    for (const auto identity : retired) DestroyGpuTextureCopySurface(identity);
}

void GfxRenderingAPIVulkan::FlushGpuTextureCopyPrefix() {
    if (!mFrameActive) return;
    EndNativePicaRenderPass();
    const auto command = mCommandBuffers[mCurrentFrame];
    if (mRenderPassActive) {
        vkCmdEndRenderPass(command);
        mRenderPassActive = false;
        mOverlayRenderPassActive = false;
    }
    WaitForAllPresents();
    RequireCopyVk(vkEndCommandBuffer(command), "end materialization prefix");
    RequireCopyVk(vkResetFences(mDevice, 1, &mRawCopyFence), "reset materialization fence");
    SubmitFrameCommandBuffer(command, mRawCopyFence);
    RequireCopyVk(vkWaitForFences(mDevice, 1, &mRawCopyFence, VK_TRUE, UINT64_MAX),
                  "wait materialization fence");
    ++mRawCopyTiming.PrefixWaits;
    RequireCopyVk(vkResetCommandBuffer(command, 0), "reset materialization continuation");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    RequireCopyVk(vkBeginCommandBuffer(command, &begin), "begin materialization continuation");
    if (mNriInterop.Available() && !mNriInterop.WrapFrameCommandBuffer(mCurrentFrame, command))
        throw std::runtime_error("cannot wrap materialization continuation in NRI");
}

bool GfxRenderingAPIVulkan::ReadGpuTextureCopyBytes(
    uint64_t identity, uint32_t address, std::span<uint8_t> bytes) {
    const auto found = mGpuTextureCopySurfaces.find(identity);
    if (found == mGpuTextureCopySurfaces.end()) return false;
    auto& surface = found->second;
    if (surface.MaterializedNativeBytes.empty()) {
        // This is an explicit CPU visibility boundary, never sampler work.
        // Execute the recorded producer first; read the native capture, not a
        // mutable attachment or the optional-effects composition.
        FlushGpuTextureCopyPrefix();
        const auto rgba = CaptureNativePicaImageBytes(
            surface.NativeImage != VK_NULL_HANDLE ? surface.NativeImage : surface.Image,
            surface.ImageWidth, surface.ImageHeight, 4U,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_READ_BIT);
        surface.MaterializedNativeBytes = Renderer3ds::EncodePicaFramebuffer(
            rgba, surface.ImageWidth, surface.ImageHeight,
            surface.SourceWidth, surface.SourceHeight, surface.SourceFormat);
        ++mRawCopyTiming.Materializations;
        mRawCopyTiming.ReadbackBytes += rgba.size();
    }
    size_t done = 0;
    while (done < bytes.size()) {
        const uint64_t current = uint64_t{address} + done;
        const auto span = std::find_if(surface.Spans.begin(), surface.Spans.end(),
            [&](const auto& item) {
                return current >= item.DestinationAddress &&
                       current < uint64_t{item.DestinationAddress} + item.ByteCount;
            });
        if (span == surface.Spans.end()) return false;
        const size_t count = std::min(bytes.size() - done,
            static_cast<size_t>(uint64_t{span->DestinationAddress} + span->ByteCount - current));
        const uint64_t offset = uint64_t{span->SourceAddress} - surface.SourceSurfaceAddress +
                                current - span->DestinationAddress;
        if (offset + count > surface.MaterializedNativeBytes.size()) return false;
        std::memcpy(bytes.data() + done, surface.MaterializedNativeBytes.data() + offset, count);
        done += count;
    }
    mRawCopyTiming.WritebackBytes += bytes.size();
    return true;
}


void GfxRenderingAPIVulkan::DestroyRawCopyEncodeResources() {
    if (mDevice != VK_NULL_HANDLE) {
        if (mRawCopyEncodePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(mDevice, mRawCopyEncodePipeline, nullptr);
        }
        if (mRawCopyEncodePipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(mDevice, mRawCopyEncodePipelineLayout, nullptr);
        }
        if (mRawCopyEncodeDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(mDevice, mRawCopyEncodeDescriptorPool, nullptr);
        }
        if (mRawCopyEncodeDescriptorLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(mDevice, mRawCopyEncodeDescriptorLayout, nullptr);
        }
        if (mRawCopyEncodeSampler != VK_NULL_HANDLE) {
            vkDestroySampler(mDevice, mRawCopyEncodeSampler, nullptr);
        }
        if (mRawCopyEncodeImage.View != VK_NULL_HANDLE) {
            vkDestroyImageView(mDevice, mRawCopyEncodeImage.View, nullptr);
        }
        if (mRawCopyEncodeImage.Image != VK_NULL_HANDLE) {
            vkDestroyImage(mDevice, mRawCopyEncodeImage.Image, nullptr);
        }
        if (mRawCopyEncodeImage.Memory != VK_NULL_HANDLE) {
            vkFreeMemory(mDevice, mRawCopyEncodeImage.Memory, nullptr);
        }
    }
    mRawCopyEncodePipeline = VK_NULL_HANDLE;
    mRawCopyEncodePipelineLayout = VK_NULL_HANDLE;
    mRawCopyEncodeDescriptorPool = VK_NULL_HANDLE;
    mRawCopyEncodeDescriptorLayout = VK_NULL_HANDLE;
    mRawCopyEncodeDescriptorSet = VK_NULL_HANDLE;
    mRawCopyEncodeSampler = VK_NULL_HANDLE;
    mRawCopyEncodeImage = {};
    DestroyBuffer(mRawCopyEncodedBuffer);
    mRawCopyEncodedBuffer = {};
}

bool GfxRenderingAPIVulkan::EnsureRawCopyEncodeResources(uint32_t encodedBytes,
                                                        std::string* error) {
    const auto fail = [&](const char* message) {
        if (error != nullptr) *error = message;
        return false;
    };
    if (encodedBytes == 0U || mDevice == VK_NULL_HANDLE) {
        return fail("raw PICA copy GPU encode has no output extent");
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
    // The dispatch is recorded on the graphics queue, so its family must be
    // compute-capable; anything else is refused instead of silently failing at
    // submit time.
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice, &familyCount, families.data());
    if (mGraphicsQueueFamily >= families.size() ||
        (families[mGraphicsQueueFamily].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0U) {
        return fail("raw PICA copy GPU encode needs a compute-capable graphics queue");
    }
    // R32_UINT storage is the format the shader needs; when it is absent the
    // copy must fail visibly rather than fall back to something else.
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, VK_FORMAT_R32_UINT, &formatProperties);
    if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U ||
        (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) == 0U) {
        return fail("raw PICA copy GPU encode needs R32_UINT storage and transfer-source support");
    }
    const uint32_t wordCount = (encodedBytes + 3U) / 4U;
    const uint32_t wordsPerRow = std::min<uint32_t>(wordCount, 1024U);
    const uint32_t rows = (wordCount + wordsPerRow - 1U) / wordsPerRow;
    if (wordsPerRow == 0U || rows == 0U || rows > properties.limits.maxImageDimension2D ||
        wordsPerRow > properties.limits.maxImageDimension2D) {
        return fail("raw PICA copy GPU encode image extent is out of range");
    }
    const uint64_t groups = (uint64_t{wordCount} + 63U) / 64U;
    if (groups == 0U || groups > properties.limits.maxComputeWorkGroupCount[0]) {
        return fail("raw PICA copy GPU encode dispatch exceeds the workgroup limit");
    }
    if (mRawCopyEncodeImage.Capacity < static_cast<VkDeviceSize>(wordCount) * 4U) {
        if (mRawCopyEncodeImage.View != VK_NULL_HANDLE) {
            vkDestroyImageView(mDevice, mRawCopyEncodeImage.View, nullptr);
            mRawCopyEncodeImage.View = VK_NULL_HANDLE;
        }
        if (mRawCopyEncodeImage.Image != VK_NULL_HANDLE) {
            vkDestroyImage(mDevice, mRawCopyEncodeImage.Image, nullptr);
            mRawCopyEncodeImage.Image = VK_NULL_HANDLE;
        }
        if (mRawCopyEncodeImage.Memory != VK_NULL_HANDLE) {
            vkFreeMemory(mDevice, mRawCopyEncodeImage.Memory, nullptr);
            mRawCopyEncodeImage.Memory = VK_NULL_HANDLE;
        }
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R32_UINT;
        imageInfo.extent = {wordsPerRow, rows, 1U};
        imageInfo.mipLevels = 1U;
        imageInfo.arrayLayers = 1U;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        RequireCopyVk(vkCreateImage(mDevice, &imageInfo, nullptr, &mRawCopyEncodeImage.Image),
                "vkCreateImage(raw copy encode)");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(mDevice, mRawCopyEncodeImage.Image, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        RequireCopyVk(vkAllocateMemory(mDevice, &allocation, nullptr, &mRawCopyEncodeImage.Memory),
                "vkAllocateMemory(raw copy encode)");
        RequireCopyVk(vkBindImageMemory(mDevice, mRawCopyEncodeImage.Image, mRawCopyEncodeImage.Memory, 0U),
                "vkBindImageMemory(raw copy encode)");
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = mRawCopyEncodeImage.Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R32_UINT;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U};
        RequireCopyVk(vkCreateImageView(mDevice, &viewInfo, nullptr, &mRawCopyEncodeImage.View),
                "vkCreateImageView(raw copy encode)");
        mRawCopyEncodeImage.Capacity = static_cast<VkDeviceSize>(wordCount) * 4U;
        mRawCopyEncodeImage.Stride = wordsPerRow;
        mRawCopyEncodeImage.Height = rows;
    }
    // The word image is padded to whole rows so a word index can address it with
    // two divisions in the shader, and the readback copies whole rows: the
    // staging must hold the padded extent of the *retained* image, which can be
    // larger than this call's own word count.
    const VkDeviceSize stagedBytes =
        static_cast<VkDeviceSize>(mRawCopyEncodeImage.Stride) *
        mRawCopyEncodeImage.Height * 4U;
    if (mRawCopyEncodedBuffer.Size < stagedBytes) {
        DestroyBuffer(mRawCopyEncodedBuffer);
        mRawCopyEncodedBuffer =
            CreateReadbackBuffer(stagedBytes, &mRawCopyReadbackCoherent);
    }
    if (!EnsureRawCopySampler(error)) {
        return false;
    }
    if (mRawCopyEncodePipeline == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, 2U> bindings{};
        bindings[0] = {0U, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1U,
                       VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[1] = {1U, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1U,
                       VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        RequireCopyVk(vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr,
                                            &mRawCopyEncodeDescriptorLayout),
                "vkCreateDescriptorSetLayout(raw copy encode)");
        const VkDescriptorPoolSize poolSizes[2]{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1U},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1U}};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1U;
        poolInfo.poolSizeCount = 2U;
        poolInfo.pPoolSizes = poolSizes;
        RequireCopyVk(vkCreateDescriptorPool(mDevice, &poolInfo, nullptr,
                                       &mRawCopyEncodeDescriptorPool),
                "vkCreateDescriptorPool(raw copy encode)");
        VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocateInfo.descriptorPool = mRawCopyEncodeDescriptorPool;
        allocateInfo.descriptorSetCount = 1U;
        allocateInfo.pSetLayouts = &mRawCopyEncodeDescriptorLayout;
        RequireCopyVk(vkAllocateDescriptorSets(mDevice, &allocateInfo, &mRawCopyEncodeDescriptorSet),
                "vkAllocateDescriptorSets(raw copy encode)");
        const VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0U, 32U};
        VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1U;
        pipelineLayout.pSetLayouts = &mRawCopyEncodeDescriptorLayout;
        pipelineLayout.pushConstantRangeCount = 1U;
        pipelineLayout.pPushConstantRanges = &pushRange;
        RequireCopyVk(vkCreatePipelineLayout(mDevice, &pipelineLayout, nullptr,
                                       &mRawCopyEncodePipelineLayout),
                "vkCreatePipelineLayout(raw copy encode)");
        const std::string source(Renderer3ds::PicaFramebufferEncodeComputeSource);
        const auto spirv = mCompiledShaderCache.Resolve(
            source, Renderer::SpirvStage::Compute, [&] {
                return Renderer::CompileShadercSpirv(source, Renderer::SpirvStage::Compute,
                                                     "pica_framebuffer_encode.comp");
            });
        const VkShaderModule module = CreateShaderModuleFromSpirv(spirv);
        if (module == VK_NULL_HANDLE) {
            return fail("raw PICA copy GPU encode shader module could not be created");
        }
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = module;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = mRawCopyEncodePipelineLayout;
        const VkResult created =
            vkCreateComputePipelines(mDevice, mPipelineCache, 1U, &pipelineInfo, nullptr,
                                     &mRawCopyEncodePipeline);
        vkDestroyShaderModule(mDevice, module, nullptr);
        RequireCopyVk(created, "vkCreateComputePipelines(raw copy encode)");
    }
    return true;
}

bool GfxRenderingAPIVulkan::RecordRawCopyEncode(
    VkCommandBuffer command, VkImage sourceImage, VkImageView sourceView,
    uint32_t imageWidth, uint32_t imageHeight, uint32_t width, uint32_t height,
    uint8_t format, uint32_t encodedBytes) {
    const uint32_t bpp = format == 0U ? 4U : format == 1U ? 3U : 2U;
    // The CPU oracle refuses these; the shader cannot, so the host does, and the
    // half-texel numerator is bounded so it cannot wrap in 32-bit arithmetic.
    if (width == 0U || height == 0U || width % 8U != 0U || height % 8U != 0U ||
        format > 4U || imageWidth == 0U || imageHeight == 0U ||
        uint64_t{width} * height * bpp != encodedBytes ||
        (uint64_t{width} * 2U + 1U) * imageWidth > 0xFFFFFFFFull) {
        throw std::runtime_error("raw PICA copy GPU encode refuses this geometry");
    }
    const uint32_t wordCount = (encodedBytes + 3U) / 4U;
    VkDescriptorImageInfo sourceInfo{};
    sourceInfo.sampler = mRawCopyEncodeSampler;
    sourceInfo.imageView = sourceView;
    sourceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageView = mRawCopyEncodeImage.View;
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    std::array<VkWriteDescriptorSet, 2U> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = mRawCopyEncodeDescriptorSet;
    writes[0].dstBinding = 0U;
    writes[0].descriptorCount = 1U;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &sourceInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = mRawCopyEncodeDescriptorSet;
    writes[1].dstBinding = 1U;
    writes[1].descriptorCount = 1U;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &outputInfo;
    vkUpdateDescriptorSets(mDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0U, nullptr);

    const auto barrier = [&](VkImage image, VkImageLayout from, VkImageLayout to,
                             VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                             VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
        VkImageMemoryBarrier imageBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        imageBarrier.oldLayout = from;
        imageBarrier.newLayout = to;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = image;
        imageBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U};
        imageBarrier.srcAccessMask = srcAccess;
        imageBarrier.dstAccessMask = dstAccess;
        vkCmdPipelineBarrier(command, srcStage, dstStage, 0U, 0U, nullptr, 0U, nullptr, 1U,
                             &imageBarrier);
    };
    barrier(sourceImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT);
    // Every consumed byte is rewritten by the dispatch, so discarding the
    // previous contents is legal here.
    barrier(mRawCopyEncodeImage.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0U,
            VK_ACCESS_SHADER_WRITE_BIT);

    const uint32_t push[8] = {imageWidth, imageHeight, width, height,
                              static_cast<uint32_t>(format), bpp,
                              mRawCopyEncodeImage.Stride, encodedBytes};
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, mRawCopyEncodePipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, mRawCopyEncodePipelineLayout,
                            0U, 1U, &mRawCopyEncodeDescriptorSet, 0U, nullptr);
    vkCmdPushConstants(command, mRawCopyEncodePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0U,
                       sizeof(push), push);
    vkCmdDispatch(command, static_cast<uint32_t>((wordCount + 63U) / 64U), 1U, 1U);

    barrier(mRawCopyEncodeImage.Image, VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U};
    region.imageExtent = {mRawCopyEncodeImage.Stride, mRawCopyEncodeImage.Height, 1U};
    vkCmdCopyImageToBuffer(command, mRawCopyEncodeImage.Image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mRawCopyEncodedBuffer.Buffer, 1U,
                           &region);
    barrier(sourceImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    return true;
}

bool GfxRenderingAPIVulkan::EnsureRawCopySampler(std::string* error) {
    if (mRawCopyEncodeSampler != VK_NULL_HANDLE) {
        return true;
    }
    if (mDevice == VK_NULL_HANDLE) {
        if (error != nullptr) *error = "raw copy scratch sampler has no device";
        return false;
    }
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    RequireCopyVk(vkCreateSampler(mDevice, &samplerInfo, nullptr, &mRawCopyEncodeSampler),
                  "vkCreateSampler(raw copy scratch)");
    return true;
}

bool GfxRenderingAPIVulkan::CaptureGpuTextureCopySource(
    const GpuTextureCopySourceImport& source, GpuTextureCopySurface& surface) {
    // An NRI pass performs the capture: NRI owns the destination resource,
    // declares the transition of both imports and leaves the destination
    // shader-readable for whoever samples it. There is no readback, no decode
    // and no re-upload anywhere in this path.
    surface.Image = VK_NULL_HANDLE;
    surface.ImageWidth = 0U;
    surface.ImageHeight = 0U;
    if (source.Image == VK_NULL_HANDLE || source.Width == 0U ||
        source.Height == 0U || !mNriPicaTextureCopyCapturePass.Available()) {
        return false;
    }
    const uint32_t width = source.Width;
    const uint32_t height = source.Height;
    Oot3d::NriOwnedTexture2D captured;
    if (!mNriInterop.CreateOwnedTexture2D(width, height,
                                          VK_FORMAT_R8G8B8A8_UNORM, captured) ||
        captured.Image == VK_NULL_HANDLE) {
        return false;
    }
    // An identity plan samples the source 1:1 at its internal extent, so the
    // internal rendering resolution survives the copy instead of being reduced
    // to native bytes first.
    const Oot3d::PicaDisplayTransferPlan plan{
        width, height, width, height, 1U, 1U, 0U};
    const bool capturedOk = mNriPicaTextureCopyCapturePass.Execute(
        {mCurrentFrame,
         mFrameCounter,
         source.Image,
         captured.Image,
         source.Format,
         plan,
         source.ShaderRead,
         false});
    if (!capturedOk) {
        mNriInterop.DestroyOwnedTexture(captured.Image);
        return false;
    }
    surface.Image = captured.Image;
    surface.ImageWidth = width;
    surface.ImageHeight = height;
    surface.Encoding = source.Encoding;
    surface.SceneEffectsResolved = source.SceneEffectsResolved;
    surface.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return true;
}

void GfxRenderingAPIVulkan::MaterializeRestoredGpuTextureCopyProducers() {
    for (auto& [identity, surface] : mGpuTextureCopySurfaces) {
        if (surface.PendingPixels.empty()) {
            continue;
        }
        std::vector<uint8_t> pixels = std::move(surface.PendingPixels);
        surface.PendingPixels.clear();
        if (surface.Image != VK_NULL_HANDLE ||
            pixels.size() !=
                size_t{surface.ImageWidth} * surface.ImageHeight * 4U) {
            continue;
        }
        // The captured version is an immutable RGBA8 image, recreated here as
        // one: the sampler reads it exactly like a freshly captured copy.
        TextureRecord staged;
        try {
            CreateNativePicaTextureImage(staged, pixels, surface.ImageWidth,
                                         surface.ImageHeight, 1U,
                                         VK_FORMAT_R8G8B8A8_UNORM,
                                         /*storageView=*/true);
        } catch (const std::exception&) {
            DestroyTexture(staged);
            throw;
        }
        surface.Image = staged.Image;
        surface.Layout = staged.ImageLayout;
        // The staged record only carried the upload; the register owns the
        // image now, so its sampler and view go away.
        staged.Image = VK_NULL_HANDLE;
        staged.NriImage = VK_NULL_HANDLE;
        staged.Memory = VK_NULL_HANDLE;
        DestroyTexture(staged);
    }
}

void GfxRenderingAPIVulkan::PublishGpuTextureCopySurface(
    GpuTextureCopySurface& surface) {
    if (surface.Image == VK_NULL_HANDLE) {
        return;
    }
    surface.Identity = ++mGpuTextureCopySerial;
    surface.GuestOwner = std::make_shared<const uint64_t>(surface.Identity);
    mGpuTextureCopySurfaces.emplace(surface.Identity, surface);
    mRawCopyTiming.PeakLiveProducers =
        std::max<uint64_t>(mRawCopyTiming.PeakLiveProducers, mGpuTextureCopySurfaces.size());
    // Each execution is its own version, keyed by its order boundary, so a
    // replay that still references the earlier boundary resolves to the version
    // that boundary produced rather than to the newest one.
    mGpuTextureCopyProducers[{surface.RenderTargetNamespace,
                              surface.CompletionId,
                              surface.AfterDrawSubmissionId}] =
        surface.Identity;
}

void GfxRenderingAPIVulkan::DestroyGpuTextureCopySurface(uint64_t identity) {
    const auto found = mGpuTextureCopySurfaces.find(identity);
    if (found == mGpuTextureCopySurfaces.end()) {
        return;
    }
    // Derived images have their own storage, but cache entries tied to a
    // retired producer can no longer be looked up by a retained plan.
    // Callers have retired the GPU work before releasing either resource.
    for (auto texture = mNativePicaTextures.begin();
         texture != mNativePicaTextures.end();) {
        const auto& producers = texture->second.CopyProducerIdentities;
        if (std::find(producers.begin(), producers.end(), identity) !=
            producers.end()) {
            DestroyTexture(texture->second);
            texture = mNativePicaTextures.erase(texture);
        } else {
            ++texture;
        }
    }
    if (found->second.Image != VK_NULL_HANDLE) {
        mNriPicaTextureCopyCapturePass.ForgetTexture(found->second.Image);
        mNriInterop.DestroyOwnedTexture(found->second.Image);
    }
    if (found->second.NativeImage != VK_NULL_HANDLE) {
        mNriPicaTextureCopyCapturePass.ForgetTexture(found->second.NativeImage);
        mNriInterop.DestroyOwnedTexture(found->second.NativeImage);
    }
    for (auto producer = mGpuTextureCopyProducers.begin();
         producer != mGpuTextureCopyProducers.end();) {
        if (producer->second == identity) {
            producer = mGpuTextureCopyProducers.erase(producer);
        } else {
            ++producer;
        }
    }
    mGpuTextureCopySurfaces.erase(found);
}

void GfxRenderingAPIVulkan::DestroyGpuTextureCopySurfaces() {
    if (mPhysicalMemoryBarrier && !mPhysicalMemoryBarrier())
        throw std::runtime_error("cannot retire GPU copies with unresolved guest bytes");
    while (!mGpuTextureCopySurfaces.empty()) {
        DestroyGpuTextureCopySurface(mGpuTextureCopySurfaces.begin()->first);
    }
    mHandledNativePicaTextureCopies.clear();
    mGpuTextureCopyPins.clear();
    mGpuTextureCopyPinsDeclared = false;
}

void GfxRenderingAPIVulkan::RetireGpuTextureCopySurfacesForTargetReset() {
    // A target reset runs at frame start, on the render thread while the guest
    // may be running, so it cannot materialize guest bytes. A copy is an
    // immutable image of its own, independent of the targets: one that guest
    // memory still defers to stays readable, and the frame-fence sweep retires
    // it once the guest no longer owns it.
    std::vector<uint64_t> retired;
    for (const auto& [identity, surface] : mGpuTextureCopySurfaces) {
        if (!surface.GuestOwner || surface.GuestOwner.use_count() == 1) {
            retired.push_back(identity);
        }
    }
    for (const auto identity : retired) DestroyGpuTextureCopySurface(identity);
    mHandledNativePicaTextureCopies.clear();
    mGpuTextureCopyPins.clear();
    mGpuTextureCopyPinsDeclared = false;
}

namespace {
// Opt-in diagnostic dump of an immutable copy, so the sampled mapping can be
// asserted from pixels instead of inferred from extents and counters.
[[nodiscard]] const char* GpuTextureCopyDumpPrefix() {
    static const char* prefix = std::getenv("TRIAEVUM_GPU_COPY_DUMP");
    return prefix != nullptr && *prefix != '\0' ? prefix : nullptr;
}

void WriteGpuTextureCopyDump(const char* prefix, const char* suffix,
                             uint32_t width, uint32_t height, uint32_t cellWidth,
                             uint32_t cellHeight,
                             std::span<const uint8_t> rgba8) {
    if (prefix == nullptr || rgba8.size() <
                                 size_t{width} * height * 4U) {
        return;
    }
    // How much internal detail survives inside each native texel: the copy must
    // keep sampling the internal extent, while a native-byte round trip would
    // collapse every cell to one value.
    uint64_t detailCells = 0;
    uint64_t cellCount = 0;
    if (cellWidth != 0U && cellHeight != 0U) {
        for (uint32_t y = 0; y + cellHeight <= height; y += cellHeight) {
            for (uint32_t x = 0; x + cellWidth <= width; x += cellWidth) {
                ++cellCount;
                const auto* first = rgba8.data() + (size_t{y} * width + x) * 4U;
                bool varies = false;
                for (uint32_t cy = 0; cy < cellHeight && !varies; ++cy) {
                    for (uint32_t cx = 0; cx < cellWidth; ++cx) {
                        const auto* pixel =
                            rgba8.data() +
                            (size_t{y + cy} * width + x + cx) * 4U;
                        if (pixel[0] != first[0] || pixel[1] != first[1] ||
                            pixel[2] != first[2] || pixel[3] != first[3]) {
                            varies = true;
                            break;
                        }
                    }
                }
                detailCells += varies ? 1U : 0U;
            }
        }
    }
    const std::string path = std::string(prefix) + suffix;
    if (std::FILE* file = std::fopen(path.c_str(), "wb"); file != nullptr) {
        std::fprintf(file, "RGBA8 %u %u\n", width, height);
        std::fwrite(rgba8.data(), 1U, size_t{width} * height * 4U, file);
        std::fclose(file);
    }
    std::fprintf(stderr,
                 "TRIAEVUM_GPU_COPY_DUMP file=%s extent=%ux%u cells=%llu "
                 "cells_with_internal_detail=%llu\n",
                 path.c_str(), width, height,
                 static_cast<unsigned long long>(cellCount),
                 static_cast<unsigned long long>(detailCells));
}
}

void GfxRenderingAPIVulkan::DumpGpuTextureCopyCapture(
    const GpuTextureCopySurface& surface) {
    const char* prefix = GpuTextureCopyDumpPrefix();
    if (prefix == nullptr || surface.Image == VK_NULL_HANDLE) {
        return;
    }
    static bool dumped = false;
    if (dumped) {
        return;
    }
    dumped = true;
    try {
        const size_t bytes =
            size_t{surface.ImageWidth} * surface.ImageHeight * 4U;
        if (mRawCopyEncodedBuffer.Size < bytes) {
            DestroyBuffer(mRawCopyEncodedBuffer);
            mRawCopyEncodedBuffer =
                CreateReadbackBuffer(bytes, &mRawCopyReadbackCoherent);
        }
        const auto rgba = CaptureNativePicaImageBytes(
            surface.Image, surface.ImageWidth, surface.ImageHeight, 4U,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_READ_BIT);
        // Cell size in internal pixels for one declared native texel: the
        // proof that the copy kept the internal grid rather than a native one.
        const uint32_t cellWidth = surface.TextureWidth != 0U
            ? std::max(1U, surface.ImageWidth / surface.TextureWidth) : 0U;
        const uint32_t cellHeight = surface.TextureHeight != 0U
            ? std::max(1U, surface.ImageHeight / surface.TextureHeight) : 0U;
        WriteGpuTextureCopyDump(prefix, ".capture.raw", surface.ImageWidth,
                                surface.ImageHeight, cellWidth, cellHeight, rgba);
        // The same internal image is what a native-grid encode would reduce,
        // and what the sampler must not receive reduced.
        const auto oracle = Renderer3ds::EncodePicaFramebuffer(
            rgba, surface.ImageWidth, surface.ImageHeight, surface.TextureWidth,
            surface.TextureHeight, surface.TextureFormat);
        const auto encodedBytes = NativePicaEncodedMipSize(
            PicaTextureFormatFromFramebufferFormat(surface.TextureFormat),
            surface.TextureWidth, surface.TextureHeight);
        std::fprintf(stderr,
                     "TRIAEVUM_GPU_COPY_CAPTURE extent=%ux%u native=%ux%u "
                     "native_bytes=%llu\n",
                     surface.ImageWidth, surface.ImageHeight,
                     surface.TextureWidth, surface.TextureHeight,
                     static_cast<unsigned long long>(
                         encodedBytes.value_or(oracle.size())));
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "TRIAEVUM_GPU_COPY_DUMP_FAILED %s\n",
                     exception.what());
    }
}

void GfxRenderingAPIVulkan::DumpGpuTextureCopyConsumer(
    VkImage sampled, uint32_t sampledWidth, uint32_t sampledHeight,
    const GfxNativePicaTextureView& texture, uint64_t producerCompletionId,
    Oot3d::SceneColorEncoding encoding, bool sceneEffectsResolved) {
    const char* prefix = GpuTextureCopyDumpPrefix();
    if (prefix == nullptr || sampled == VK_NULL_HANDLE) {
        return;
    }
    static bool dumped = false;
    if (dumped) {
        return;
    }
    dumped = true;
    try {
        // The derive dispatch is still in the active frame, not on the queue.
        // Submit it before the separate diagnostic readback command.
        const VkCommandBuffer command = mCommandBuffers[mCurrentFrame];
        RequireCopyVk(vkEndCommandBuffer(command), "end copy diagnostic prefix");
        SubmitFrameCommandBuffer(command, VK_NULL_HANDLE);
        RequireCopyVk(vkQueueWaitIdle(mGraphicsQueue), "wait copy diagnostic prefix");
        RequireCopyVk(vkResetCommandBuffer(command, 0U), "reset copy diagnostic continuation");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        RequireCopyVk(vkBeginCommandBuffer(command, &begin), "begin copy diagnostic continuation");
        if (mNriInterop.Available() &&
            !mNriInterop.WrapFrameCommandBuffer(mCurrentFrame, command)) {
            throw std::runtime_error("cannot wrap copy diagnostic continuation in NRI");
        }
        // Read back through the image the record samples, so the dump proves
        // what the sampler sees, not what the producer intended.
        const auto rgba = CaptureNativePicaImageBytes(
            sampled, sampledWidth, sampledHeight, 4U,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_READ_BIT);
        const uint32_t cellWidth = texture.Width != 0U
            ? std::max(1U, sampledWidth / texture.Width) : 0U;
        const uint32_t cellHeight = texture.Height != 0U
            ? std::max(1U, sampledHeight / texture.Height) : 0U;
        WriteGpuTextureCopyDump(prefix, ".consumer.raw", sampledWidth,
                                sampledHeight, cellWidth, cellHeight, rgba);
        std::fprintf(stderr,
                     "TRIAEVUM_GPU_COPY_CONSUMER sampled=%ux%u declared=%ux%u "
                     "producer=%llu encoding=%u scene_effects=%u\n",
                     sampledWidth, sampledHeight, texture.Width, texture.Height,
                     static_cast<unsigned long long>(producerCompletionId),
                     static_cast<unsigned>(encoding),
                     sceneEffectsResolved ? 1U : 0U);
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "TRIAEVUM_GPU_COPY_DUMP_FAILED %s\n",
                     exception.what());
    }
}

bool GfxRenderingAPIVulkan::SampledFootprintInsideOwnedRect(
    const GfxNativePicaDrawView& draw, const TextureRecord& record) const {
    if (record.SourceResolvedColour == false ||
        record.SourceOwnedX1 == 0U || record.SourceOwnedY1 == 0U ||
        record.SourceDeclaredWidth == 0U || record.SourceDeclaredHeight == 0U ||
        draw.TexCoord0AttributeLocation == 0xFFU || draw.VertexCount == 0U) {
        return false;
    }
    const Renderer3ds::PicaVertexAttributeView* attribute = nullptr;
    for (const auto& candidate : draw.VertexAttributes) {
        if (candidate.Location == draw.TexCoord0AttributeLocation) {
            attribute = &candidate;
            break;
        }
    }
    const Renderer3ds::PicaVertexBindingView* binding = nullptr;
    if (attribute != nullptr) {
        for (const auto& candidate : draw.VertexBindings) {
            if (candidate.Binding == attribute->Binding) {
                binding = &candidate;
                break;
            }
        }
    }
    if (attribute == nullptr || binding == nullptr || binding->Bytes.empty() ||
        attribute->ComponentCount < 2U) {
        return false;
    }
    // The authoritative texture-coordinate path: the same decode the canonical
    // consumers of this program use, independent of whether an effect consumes
    // it, so the sampled space is never guessed. Its rows already carry the
    // native V flip and offset, so a footprint compared in that space needs no
    // orientation or unit assumption.
    const Oot3d::PicaGrassTextureCoordinateTransform transform =
        Oot3d::DecodePicaGrassTextureCoordinateTransform(
            draw.VertexShaderSource, draw.FragmentShaderSource,
            draw.VertexUniformBytes, draw.TexCoord0AttributeLocation,
            &draw.TemporalVertexProgram.Hooks,
            &draw.FragmentShaderHooks);
    if (transform.Applied() == false) {
        static bool footprintUnsupportedReported = false;
        if (footprintUnsupportedReported == false) {
            footprintUnsupportedReported = true;
            std::fprintf(
                stderr,
                "TRIAEVUM_GPU_COPY_FOOTPRINT_UNSUPPORTED reason=%s\n",
                Oot3d::PicaGrassTextureCoordinateEligibilityName(
                    transform.Eligibility));
        }
        return false;
    }
    // Typed attribute decode, the same component semantics the geometry registry
    // uses for this format enum.
    const uint32_t componentBytes =
        attribute->Format == Renderer3ds::PicaVertexFormat::SignedByte ||
                attribute->Format == Renderer3ds::PicaVertexFormat::UnsignedByte
            ? 1U
        : attribute->Format == Renderer3ds::PicaVertexFormat::SignedShort
            ? 2U
            : attribute->Format == Renderer3ds::PicaVertexFormat::Float ? 4U
                                                                        : 0U;
    const uint32_t stride = std::max<uint32_t>(binding->ByteStride, 2U * componentBytes);
    if (componentBytes == 0U ||
        uint64_t{attribute->ByteOffset} + 2U * componentBytes > stride ||
        uint64_t{stride} * draw.VertexCount > binding->Bytes.size()) {
        return false;
    }
    const auto readAttribute = [&](uint32_t index, float* output) {
        const size_t offset =
            static_cast<size_t>(index) * stride + attribute->ByteOffset;
        if (offset + 2U * componentBytes > binding->Bytes.size()) {
            return false;
        }
        const uint8_t* source = binding->Bytes.data() + offset;
        for (uint32_t component = 0U; component < 2U; ++component) {
            const uint8_t* value = source + component * componentBytes;
            switch (attribute->Format) {
                case Renderer3ds::PicaVertexFormat::SignedByte:
                    output[component] =
                        static_cast<float>(static_cast<int8_t>(*value));
                    break;
                case Renderer3ds::PicaVertexFormat::UnsignedByte:
                    output[component] = static_cast<float>(*value);
                    break;
                case Renderer3ds::PicaVertexFormat::SignedShort: {
                    int16_t raw = 0;
                    std::memcpy(&raw, value, sizeof(raw));
                    output[component] = static_cast<float>(raw);
                    break;
                }
                case Renderer3ds::PicaVertexFormat::Float:
                    std::memcpy(&output[component], value, sizeof(float));
                    break;
            }
        }
        return std::isfinite(output[0]) && std::isfinite(output[1]);
    };
    float minU = 0.0F, minV = 0.0F, maxU = 0.0F, maxV = 0.0F;
    bool any = false;
    const auto accumulate = [&](uint32_t index) {
        if (index >= draw.VertexCount) {
            return;
        }
        float rawUv[2] = {0.0F, 0.0F};
        if (readAttribute(index, rawUv) == false) {
            return;
        }
        const auto sample = Oot3d::ApplyPicaGrassTextureCoordinateTransform(
            transform, {rawUv[0], rawUv[1]});
        if (std::isfinite(sample[0]) == false ||
            std::isfinite(sample[1]) == false) {
            return;
        }
        if (any == false) {
            minU = maxU = sample[0];
            minV = maxV = sample[1];
            any = true;
            return;
        }
        minU = std::min(minU, sample[0]);
        maxU = std::max(maxU, sample[0]);
        minV = std::min(minV, sample[1]);
        maxV = std::max(maxV, sample[1]);
    };
    if (draw.Indexed) {
        const uint32_t indexBytes = draw.IndicesAre16Bit ? 2U : 1U;
        for (uint32_t i = 0U; i < draw.VertexCount; ++i) {
            const size_t offset = static_cast<size_t>(i) * indexBytes;
            if (offset + indexBytes > draw.IndexBytes.size()) {
                break;
            }
            uint32_t index = 0U;
            if (indexBytes == 2U) {
                uint16_t raw = 0U;
                std::memcpy(&raw, draw.IndexBytes.data() + offset, sizeof(raw));
                index = raw;
            } else {
                index = draw.IndexBytes[offset];
            }
            accumulate(index);
        }
    } else {
        for (uint32_t i = 0U; i < draw.VertexCount; ++i) {
            accumulate(i);
        }
    }
    if (any == false) {
        return false;
    }
    // The owned rectangle in the same sample space the transform produces.
    const float ownedU0 =
        static_cast<float>(record.SourceOwnedX0) /
        static_cast<float>(record.SourceDeclaredWidth);
    const float ownedU1 =
        static_cast<float>(record.SourceOwnedX1) /
        static_cast<float>(record.SourceDeclaredWidth);
    const float ownedV0 =
        static_cast<float>(record.SourceOwnedY0) /
        static_cast<float>(record.SourceDeclaredHeight);
    const float ownedV1 =
        static_cast<float>(record.SourceOwnedY1) /
        static_cast<float>(record.SourceDeclaredHeight);
    const float epsilon = 1.0e-3F;
    return minU >= ownedU0 - epsilon && maxU <= ownedU1 + epsilon &&
           minV >= ownedV0 - epsilon && maxV <= ownedV1 + epsilon;
}

bool GfxRenderingAPIVulkan::NativePicaSceneFullyResolvedForTarget(
    uint64_t renderTargetNamespace, uint32_t colorPhysicalAddress) const {
    if (colorPhysicalAddress == 0U) {
        return false;
    }
    for (const auto& [key, target] : mNativePicaRenderTargets) {
        if (key.RenderTargetNamespace != renderTargetNamespace ||
            key.ColorPhysicalAddress != colorPhysicalAddress) {
            continue;
        }
        return target.SceneEffectsResolvedSerial != 0U &&
               target.SceneEffectsResolvedSerial == target.ContentWriteSerial;
    }
    return false;
}

bool GfxRenderingAPIVulkan::GpuSurfaceProducerVersionExists(
    const GfxNativePicaDisplayTransferView& transfer) const {
    // The lifetime of a raw copy: one capture per operation and host sample
    // (a repeated execution within the sample reuses it), a new one when a
    // later visual sample re-executes the operation from its own source (the
    // handled set drops unmaterialized entries at every frame start).
    const auto handled = mHandledNativePicaTextureCopies.find(
        {transfer.RenderTargetNamespace, transfer.OutputPhysicalAddress});
    return handled != mHandledNativePicaTextureCopies.end() &&
           handled->second.first == transfer.CompletionId &&
           mGpuTextureCopyProducers.contains(
               {transfer.RenderTargetNamespace, transfer.CompletionId,
                transfer.AfterDrawSubmissionId});
}

bool GfxRenderingAPIVulkan::SubmitNativePicaRenderTargetCapture(
    const GfxNativePicaDisplayTransferView& transfer, std::string* error) {
    try {
        if (GpuSurfaceProducerVersionExists(transfer)) {
            return true;
        }
        if (!mFrameActive || transfer.Present || transfer.CompletionId == 0U) {
            throw std::runtime_error(
                "colour-target capture requires ordered offscreen work");
        }
        // Several targets may share the colour address (other depth buffers);
        // the one written last holds the version the read sees.
        NativePicaRenderTarget* source = nullptr;
        for (auto& [key, target] : mNativePicaRenderTargets) {
            if (key.RenderTargetNamespace == transfer.RenderTargetNamespace &&
                key.ColorPhysicalAddress == transfer.InputPhysicalAddress &&
                key.Width == transfer.InputWidth &&
                key.Height == transfer.InputHeight &&
                (source == nullptr ||
                 target.ContentWriteSerial > source->ContentWriteSerial)) {
                source = &target;
            }
        }
        if (source == nullptr) {
            // No GPU version exists (for example right after a restore): the
            // sampler keeps the bytes' CPU owner. Reported once, never silent.
            static bool reported = false;
            if (!reported) {
                reported = true;
                std::fprintf(stderr,
                             "TRIAEVUM_GPU_COLOR_TARGET_CAPTURE_MISSING "
                             "address=0x%08X extent=%ux%u\n",
                             transfer.InputPhysicalAddress, transfer.InputWidth,
                             transfer.InputHeight);
            }
            return true;
        }
        const uint32_t bytesPerPixel = source->Key.ColorFormat == 0U   ? 4U
                                       : source->Key.ColorFormat == 1U ? 3U
                                                                       : 2U;
        const uint32_t bytes =
            uint32_t{source->Key.Width} * source->Key.Height * bytesPerPixel;
        GpuTextureCopySurface surface;
        surface.RenderTargetNamespace = transfer.RenderTargetNamespace;
        surface.CompletionId = transfer.CompletionId;
        surface.AfterDrawSubmissionId = transfer.AfterDrawSubmissionId;
        surface.DestinationAddress = source->Key.ColorPhysicalAddress;
        surface.DestinationBytes = bytes;
        surface.TextureWidth = source->Key.Width;
        surface.TextureHeight = source->Key.Height;
        surface.TextureFormat = source->Key.ColorFormat;
        // The sampled bytes are the target's own: an identity walk over it.
        surface.SourceSurfaceAddress = source->Key.ColorPhysicalAddress;
        surface.SourceWidth = source->Key.Width;
        surface.SourceHeight = source->Key.Height;
        surface.SourceFormat = source->Key.ColorFormat;
        surface.Spans.push_back({source->Key.ColorPhysicalAddress,
                                 source->Key.ColorPhysicalAddress, bytes});
        surface.CapturedFrame = mFrameCounter;
        EndNativePicaRenderPass();
        if (mRenderPassActive) {
            vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
            mRenderPassActive = false;
            mOverlayRenderPassActive = false;
        }
        // The same boundary composition a raw copy of this target samples.
        GpuTextureCopySourceImport copySource;
        std::string composeError;
        if (!ComposeNativePicaSceneForCopy(*source, transfer.AfterDrawSubmissionId,
                                           copySource, &composeError) ||
            copySource.Image == VK_NULL_HANDLE ||
            copySource.Format != VK_FORMAT_R8G8B8A8_UNORM ||
            copySource.Width != source->Width || copySource.Height != source->Height) {
            static bool reported = false;
            if (!reported) {
                reported = true;
                std::fprintf(stderr,
                             "TRIAEVUM_GPU_COLOR_TARGET_NATIVE_SOURCE address=0x%08X "
                             "reason=%s\n",
                             transfer.InputPhysicalAddress, composeError.c_str());
            }
            // The target's own colour is the native truth this read sees.
            copySource = {};
            copySource.Image = source->ColorImage;
            copySource.Width = source->Width;
            copySource.Height = source->Height;
        }
        if (!CaptureGpuTextureCopySource(copySource, surface)) {
            throw std::runtime_error("colour-target capture failed: " +
                                     (mNriPicaTextureCopyCapturePass.Available()
                                          ? std::string("capture pass refused")
                                          : mNriPicaTextureCopyCapturePass
                                                .UnavailableReason()));
        }
        PublishGpuTextureCopySurface(surface);
        mHandledNativePicaTextureCopies.insert_or_assign(
            {transfer.RenderTargetNamespace, transfer.OutputPhysicalAddress},
            std::pair{transfer.CompletionId, false});
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

void GfxRenderingAPIVulkan::PublishTiledTransferProducer(
    const GfxNativePicaDisplayTransferView& transfer,
    const NativePicaDisplayImage& display) {
    if (GpuSurfaceProducerVersionExists(transfer)) {
        return;
    }
    // The view's output extent is post-scale; the plan takes the GX words.
    const uint32_t scaling = (transfer.Flags >> 24U) & 3U;
    const uint32_t declaredWidth =
        uint32_t{transfer.OutputWidth} << (scaling != 0U ? 1U : 0U);
    const uint32_t declaredHeight =
        uint32_t{transfer.OutputHeight} << (scaling == 2U ? 1U : 0U);
    const auto layout = Renderer3ds::PlanPicaTiledTransferResult(
        transfer.OutputPhysicalAddress,
        (uint32_t{transfer.InputHeight} << 16U) | transfer.InputWidth,
        (declaredHeight << 16U) | declaredWidth, transfer.Flags);
    if (!layout.has_value() || layout->Width != transfer.OutputWidth ||
        layout->Height != transfer.OutputHeight) {
        return;
    }
    const uint32_t bytes = layout->Width * layout->Height * layout->BytesPerPixel;
    GpuTextureCopySurface surface;
    surface.RenderTargetNamespace = transfer.RenderTargetNamespace;
    surface.CompletionId = transfer.CompletionId;
    surface.AfterDrawSubmissionId = transfer.AfterDrawSubmissionId;
    surface.DestinationAddress = layout->Address;
    surface.DestinationBytes = bytes;
    surface.TextureWidth = layout->Width;
    surface.TextureHeight = layout->Height;
    surface.TextureFormat = static_cast<uint8_t>(layout->NativeFormat);
    // The derivation's source is the filtered, converted RESULT: an identity
    // walk over it, so the transfer's scaling is never applied twice.
    surface.SourceSurfaceAddress = layout->Address;
    surface.SourceWidth = layout->Width;
    surface.SourceHeight = layout->Height;
    surface.SourceFormat = static_cast<uint8_t>(layout->NativeFormat);
    surface.Spans.push_back({layout->Address, layout->Address, bytes});
    surface.CapturedFrame = mFrameCounter;
    GpuTextureCopySourceImport source;
    source.Image = display.Image;
    source.Width = display.Width;
    source.Height = display.Height;
    source.ShaderRead = true;
    source.SceneEffectsResolved = display.SceneEffectsResolved;
    // The display image keeps the source attachment's rows, as the capture of
    // a raw copy does; it is mutable, so the version is captured now, before
    // a later transfer to the same output overwrites it.
    if (!CaptureGpuTextureCopySource(source, surface)) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            std::fprintf(stderr,
                         "TRIAEVUM_GPU_TILED_TRANSFER_CAPTURE_UNAVAILABLE "
                         "output=0x%08X\n",
                         transfer.OutputPhysicalAddress);
        }
        return;
    }
    PublishGpuTextureCopySurface(surface);
    mHandledNativePicaTextureCopies.insert_or_assign(
        {transfer.RenderTargetNamespace, transfer.OutputPhysicalAddress},
        std::pair{transfer.CompletionId, false});
}

bool GfxRenderingAPIVulkan::SubmitNativePicaTextureCopy(
    const GfxNativePicaDisplayTransferView& transfer, std::string* error) {
    try {
        const bool materializeGuest = transfer.MaterializeGuest;
        const auto previous = mHandledNativePicaTextureCopies.find(
            {transfer.RenderTargetNamespace, transfer.OutputPhysicalAddress});
        const bool alreadyExecuted =
            previous != mHandledNativePicaTextureCopies.end() &&
            previous->second.first == transfer.CompletionId;
        if (alreadyExecuted &&
            (materializeGuest == false || previous->second.second)) {
            return true;
        }
        if (!mFrameActive || transfer.Present || !mPhysicalMemoryRead || !mPhysicalMemoryCommit ||
            !mPhysicalMemoryComplete || mFrameExternalComputeSplit || mNativePicaPresentedThisFrame)
            throw std::runtime_error("raw PICA copy requires ordered offscreen work and coherent memory access");
        const uint32_t size = transfer.TextureCopyBytes & ~15U;
        const uint32_t inputGap = uint32_t{transfer.InputHeight} * 16U;
        const uint32_t outputGap = uint32_t{transfer.OutputHeight} * 16U;
        const uint32_t inputWidth = inputGap == 0 ? size : uint32_t{transfer.InputWidth} * 16U;
        const uint32_t outputWidth = outputGap == 0 ? size : uint32_t{transfer.OutputWidth} * 16U;
        if (!size || !inputWidth || !outputWidth)
            throw std::runtime_error("raw PICA copy has zero size or strided width");
        const uint64_t inputEnd = uint64_t{transfer.InputPhysicalAddress} + size + uint64_t{(size - 1) / inputWidth} * inputGap;
        const uint64_t outputEnd = uint64_t{transfer.OutputPhysicalAddress} + size + uint64_t{(size - 1) / outputWidth} * outputGap;
        if (inputEnd > (uint64_t{1} << 32) || outputEnd > (uint64_t{1} << 32))
            throw std::runtime_error("raw PICA copy wraps physical addresses");
        NativePicaRenderTarget* source = nullptr;
        for (auto& [key, target] : mNativePicaRenderTargets) {
            const uint32_t bpp = key.ColorFormat == 0 ? 4 : key.ColorFormat == 1 ? 3 : 2;
            const uint64_t end = uint64_t{key.ColorPhysicalAddress} + uint64_t{key.Width} * key.Height * bpp;
            if (key.RenderTargetNamespace == transfer.RenderTargetNamespace &&
                transfer.InputPhysicalAddress >= key.ColorPhysicalAddress && inputEnd <= end) {
                if (source != nullptr) throw std::runtime_error("raw PICA copy source ownership is ambiguous");
                source = &target;
            }
        }
        if (!source) throw std::runtime_error("raw PICA copy has no coherent GPU source");
        // Writing an existing attachment needs an upload as well as guest-memory
        // writeback. Never claim completion for that not-yet-supported case.
        for (const auto& [key, target] : mNativePicaRenderTargets) {
            const uint32_t bpp = key.ColorFormat == 0 ? 4 : key.ColorFormat == 1 ? 3 : 2;
            const uint64_t end = uint64_t{key.ColorPhysicalAddress} + uint64_t{key.Width} * key.Height * bpp;
            if (key.RenderTargetNamespace == transfer.RenderTargetNamespace &&
                key.ColorPhysicalAddress < outputEnd && end > transfer.OutputPhysicalAddress)
                throw std::runtime_error("raw PICA copy destination aliases an active GPU attachment");
        }
        const uint32_t copyBpp = source->Key.ColorFormat == 0U   ? 4U
                                 : source->Key.ColorFormat == 1U ? 3U
                                                                 : 2U;
        const uint32_t encodedBytes =
            uint32_t{source->Key.Width} * source->Key.Height * copyBpp;
        // The copy's byte walk in native addresses, before anything is recorded:
        // it decides whether the copy may be sampled as an image, and it is what
        // the guest writeback materializes.
        std::vector<Renderer3ds::PicaRawCopySpan> spans;
        {
            uint64_t input = transfer.InputPhysicalAddress;
            uint64_t output = transfer.OutputPhysicalAddress;
            uint32_t left = size, inputLeft = inputWidth, outputLeft = outputWidth;
            while (left) {
                const uint32_t count = std::min({left, inputLeft, outputLeft});
                spans.push_back({static_cast<uint32_t>(input),
                                 static_cast<uint32_t>(output), count});
                left -= count; inputLeft -= count; outputLeft -= count;
                input += count; output += count;
                if (!inputLeft) { input += inputGap; inputLeft = inputWidth; }
                if (!outputLeft) { output += outputGap; outputLeft = outputWidth; }
            }
        }
        // The immutable capture of the whole source is taken whenever the NRI
        // capture pass is available; which subranges a sampler may use is decided
        // per consumer from the bytes that consumer still owns.
        const bool captureAvailable =
            mNriPicaTextureCopyCapturePass.Available();
        GpuTextureCopySurface surface;
        surface.RenderTargetNamespace = transfer.RenderTargetNamespace;
        surface.CompletionId = transfer.CompletionId;
        surface.AfterDrawSubmissionId = transfer.AfterDrawSubmissionId;
        surface.DestinationAddress = transfer.OutputPhysicalAddress;
        surface.DestinationBytes = encodedBytes;
        surface.TextureWidth = source->Key.Width;
        surface.TextureHeight = source->Key.Height;
        surface.TextureFormat = source->Key.ColorFormat;
        surface.SourceSurfaceAddress = source->Key.ColorPhysicalAddress;
        surface.SourceWidth = source->Key.Width;
        surface.SourceHeight = source->Key.Height;
        surface.SourceFormat = source->Key.ColorFormat;
        surface.Spans = spans;
        surface.CapturedFrame = mFrameCounter;
        // Whether the copy may be sampled as an image is decided per consumer
        // from the subranges it still owns, through the same shared planner; the
        // capture itself always happens so a later partial consumer can use it.
        const bool sampleable = captureAvailable;
        EndNativePicaRenderPass();
        if (mRenderPassActive) {
            vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
            mRenderPassActive = false;
            mOverlayRenderPassActive = false;
        }
        // The compositor declares what this copy samples at this exact boundary:
        // the renderer-composed scene at the native effect anchor, or the
        // identity import of the native colour target when no scene effect
        // applies here. A composer that cannot produce a valid source fails the
        // copy, so an enabled effect can never silently sample the base target.
        GpuTextureCopySourceImport copySource;
        if (sampleable) {
            std::string composeError;
            if (!ComposeNativePicaSceneForCopy(
                    *source, transfer.AfterDrawSubmissionId, copySource,
                    &composeError)) {
                throw std::runtime_error(
                    "raw PICA copy scene source is unavailable: " +
                    composeError);
            }
            if (copySource.Image == VK_NULL_HANDLE ||
                copySource.Format != VK_FORMAT_R8G8B8A8_UNORM ||
                copySource.Width != source->Width ||
                copySource.Height != source->Height) {
                throw std::runtime_error(
                    "raw PICA copy scene source does not match the source's "
                    "internal extent and RGBA8 domain");
            }
        } else {
            // The byte walk is not a whole-surface translation, so this copy is
            // never sampled from the GPU and no scene source is declared: the
            // guest writeback below stays native truth. Reported, never silent.
            static bool unsampleableReported = false;
            if (!unsampleableReported) {
                unsampleableReported = true;
                std::fprintf(stderr,
                             "TRIAEVUM_GPU_COPY_UNSAMPLEABLE bytes=%u spans=%llu "
                             "native=%ux%u format=%u\n",
                             transfer.TextureCopyBytes,
                             static_cast<unsigned long long>(spans.size()),
                             source->Key.Width, source->Key.Height,
                             source->Key.ColorFormat);
            }
        }
        const VkCommandBuffer command = mCommandBuffers[mCurrentFrame];
        if (sampleable && !CaptureGpuTextureCopySource(copySource, surface)) {
            surface.Image = VK_NULL_HANDLE;
            static bool captureReported = false;
            if (!captureReported) {
                captureReported = true;
                std::fprintf(stderr,
                             "TRIAEVUM_GPU_COPY_CAPTURE_UNAVAILABLE reason=%s\n",
                             mNriPicaTextureCopyCapturePass.Available()
                                 ? "capture failed"
                                 : mNriPicaTextureCopyCapturePass
                                       .UnavailableReason()
                                       .c_str());
            }
        }
        PublishGpuTextureCopySurface(surface);
        static const bool encodeVerify =
            std::getenv("TRIAEVUM_RAW_COPY_ENCODE_VERIFY") != nullptr;
        mRawCopyEncodeVerify = encodeVerify;
        if (materializeGuest && surface.Image != VK_NULL_HANDLE && mPhysicalMemoryDefer &&
            !encodeVerify) {
            // Optional effects belong only to the sampled image. Preserve a
            // second native capture only when the composer selected another
            // image; Authentic shares the one immutable capture.
            if (copySource.Image != source->ColorImage) {
                GpuTextureCopySurface nativeSurface;
                GpuTextureCopySourceImport nativeSource;
                nativeSource.Image = source->ColorImage;
                nativeSource.Width = source->Width;
                nativeSource.Height = source->Height;
                if (!CaptureGpuTextureCopySource(nativeSource, nativeSurface))
                    throw std::runtime_error("cannot retain native copy bytes independently of scene effects");
                mGpuTextureCopySurfaces.at(surface.Identity).NativeImage = nativeSurface.Image;
            }
            const auto owner = surface.GuestOwner;
            if (mPhysicalMemoryDefer(spans,
                    [this, owner](uint32_t address, std::span<uint8_t> bytes) {
                        return ReadGpuTextureCopyBytes(*owner, address, bytes);
                    })) {
                // PPF promises logical visibility, not a host RAM memcpy.
                // Subsequent GPU commands are ordered after this capture; a
                // CPU read submits/waits the producer before returning bytes.
                if (!mPhysicalMemoryComplete(transfer.CompletionId))
                    throw std::runtime_error("raw PICA copy completion failed");
                mHandledNativePicaTextureCopies.insert_or_assign(
                    {transfer.RenderTargetNamespace, transfer.OutputPhysicalAddress},
                    std::pair{transfer.CompletionId, true});
                ++mRawCopyTiming.Copies;
                ++mRawCopyTiming.DeferredCopies;
                mRawCopyTiming.Spans += spans.size();
                if (GpuTextureCopyDumpPrefix()) {
                    FlushGpuTextureCopyPrefix();
                    DumpGpuTextureCopyCapture(surface);
                }
                return true;
            }
        }
        if (materializeGuest == false) {
            // The immutable capture stays in this frame's command buffer and
            // retires with it; the producer version is already registered, so a
            // draw later in the frame samples it and the guest owes nothing.
            mHandledNativePicaTextureCopies.insert_or_assign(
                {transfer.RenderTargetNamespace, transfer.OutputPhysicalAddress},
                std::pair{transfer.CompletionId, false});
            return true;
        }
        // An escaped host pointer or an explicit encode-oracle request requires
        // eager RAM visibility. This is a memory contract, never a scene rule.
        std::string encodeError;
        if (!EnsureRawCopyEncodeResources(encodedBytes, &encodeError))
            throw std::runtime_error("raw PICA copy GPU encode is unavailable: " + encodeError);
        if (!RecordRawCopyEncode(command, source->ColorImage, source->ColorView,
                source->Width, source->Height, source->Key.Width, source->Key.Height,
                source->Key.ColorFormat, encodedBytes))
            throw std::runtime_error("raw PICA copy GPU encode was not recorded");
        WaitForAllPresents();
        RequireCopyVk(vkEndCommandBuffer(command), "end raw-copy prefix");
        // This prefix may already contain presentation writes.
        RequireCopyVk(vkResetFences(mDevice, 1, &mRawCopyFence), "reset raw-copy fence");
        SubmitFrameCommandBuffer(command, mRawCopyFence);
        RequireCopyVk(vkWaitForFences(mDevice, 1, &mRawCopyFence, VK_TRUE, UINT64_MAX),
                      "wait raw-copy fence");
        ++mRawCopyTiming.PrefixWaits;
        const auto copyPhaseClock = [] { return std::chrono::steady_clock::now(); };
        const auto phaseMilliseconds = [](const auto& from, const auto& to) {
            return std::chrono::duration<double, std::milli>(to - from).count();
        };
        const auto readbackStart = copyPhaseClock();
        if (!mRawCopyReadbackCoherent) {
            VkMappedMemoryRange readbackRange{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            readbackRange.memory = mRawCopyEncodedBuffer.Memory;
            readbackRange.offset = 0;
            readbackRange.size = VK_WHOLE_SIZE;
            RequireCopyVk(vkInvalidateMappedMemoryRanges(mDevice, 1, &readbackRange),
                          "invalidate raw-copy encode staging");
        }
        const auto* encodedPointer =
            static_cast<const uint8_t*>(mRawCopyEncodedBuffer.Mapped);
        if (encodedPointer == nullptr) {
            throw std::runtime_error("raw PICA copy encode staging is not mapped");
        }
        std::vector<uint8_t> native(encodedPointer, encodedPointer + encodedBytes);
        const auto encodeStart = copyPhaseClock();
        // Diagnostic differential: keep the CPU oracle in the loop and compare
        // it with what the GPU produced, on the real colour targets.
        if (encodeVerify) {
            const auto rgba = CaptureNativePicaImageBytes(
                source->ColorImage, source->Width, source->Height, 4,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            const auto oracle = Renderer3ds::EncodePicaFramebuffer(
                rgba, source->Width, source->Height, source->Key.Width,
                source->Key.Height, source->Key.ColorFormat);
            uint64_t mismatches = 0;
            uint64_t firstOffset = 0;
            for (size_t index = 0; index < oracle.size() && index < native.size(); ++index) {
                if (oracle[index] != native[index]) {
                    if (mismatches == 0) firstOffset = index;
                    ++mismatches;
                }
            }
            if (mismatches != 0 && mRawCopyEncodeVerifyMismatches == 0) {
                std::fprintf(stderr,
                             "TRIAEVUM_RAW_COPY_ENCODE_MISMATCH offset=%llu oracle=0x%02X gpu=0x%02X\n",
                             static_cast<unsigned long long>(firstOffset),
                             oracle[firstOffset], native[firstOffset]);
            }
            mRawCopyEncodeVerifyBytes += oracle.size();
            mRawCopyEncodeVerifyMismatches += mismatches;
        }
        const auto invalidateStart = copyPhaseClock();
        RetireGpuTextureCopyProducers(mFrameCounter - 1U);
        DumpGpuTextureCopyCapture(surface);
        RequireCopyVk(vkResetCommandBuffer(command, 0), "reset raw-copy continuation");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        RequireCopyVk(vkBeginCommandBuffer(command, &begin), "begin raw-copy continuation");
        if (mNriInterop.Available() && !mNriInterop.WrapFrameCommandBuffer(mCurrentFrame, command))
            throw std::runtime_error("cannot wrap raw-copy continuation in NRI");

        const auto spanStart = copyPhaseClock();
        std::vector<Renderer3ds::PicaPhysicalMemoryWrite> writes;
        writes.reserve(spans.size());
        for (const auto& span : spans) {
            const uint64_t offset =
                uint64_t{span.SourceAddress} - source->Key.ColorPhysicalAddress;
            if (offset > encodedBytes ||
                span.ByteCount > encodedBytes - offset) {
                throw std::runtime_error(
                    "raw PICA copy source span exceeds the encoded surface");
            }
            Renderer3ds::PicaPhysicalMemoryWrite write;
            write.Address = span.DestinationAddress;
            write.Bytes.assign(native.begin() + offset,
                               native.begin() + offset + span.ByteCount);
            writes.push_back(std::move(write));
            ++mRawCopyTiming.Spans;
        }
        const auto commitStart = copyPhaseClock();
        if (!mPhysicalMemoryCommit(writes) || !mPhysicalMemoryComplete(transfer.CompletionId))
            throw std::runtime_error("raw PICA copy writeback/completion failed");
        const auto copyEnd = copyPhaseClock();
        ++mRawCopyTiming.Copies;
        mRawCopyTiming.ReadbackBytes += native.size();
        mRawCopyTiming.WritebackBytes += native.size();
        mRawCopyTiming.ReadbackSeconds += phaseMilliseconds(readbackStart, encodeStart);
        mRawCopyTiming.EncodeSeconds += phaseMilliseconds(encodeStart, invalidateStart);
        mRawCopyTiming.InvalidateSeconds += phaseMilliseconds(invalidateStart, spanStart);
        mRawCopyTiming.SpanSeconds += phaseMilliseconds(spanStart, commitStart);
        mRawCopyTiming.CommitSeconds += phaseMilliseconds(commitStart, copyEnd);
        mHandledNativePicaTextureCopies.insert_or_assign(
            {transfer.RenderTargetNamespace, transfer.OutputPhysicalAddress},
            std::pair{transfer.CompletionId, true});
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}
} // namespace Fast
#endif
