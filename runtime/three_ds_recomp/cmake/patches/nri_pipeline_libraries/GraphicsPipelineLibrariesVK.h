// TriAevum NRI integration. Generic Vulkan state, no title or asset identities.
#pragma once
#include <vulkan/vulkan.h>
#include "PipelineLibraryMemoryVK.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

namespace triaevum_nri {

struct ProgramBytes { const void* Data; size_t Size; };

// One owner per NRI pipeline layout. Libraries never outlive its device, and
// layout handle recycling cannot alias another descriptor contract.
//
// Threads create different parts concurrently: the mutex guards only lookup
// and bookkeeping. The first requester of a part creates it outside the lock;
// later requesters wait for that part alone. A part in use by a link is never
// evicted.
class GraphicsPipelineLibraries {
    using Key = std::vector<uint64_t>;
    struct Part {
        VkPipeline Handle = VK_NULL_HANDLE;
        uint64_t LastUse = 0;
        uint32_t Users = 0;
        bool Ready = false;
        VkResult Result = VK_SUCCESS;
    };
    std::array<std::map<Key, Part>, 4> mParts;
    std::map<std::vector<uint32_t>, uint64_t> mPrograms;
    std::array<uint64_t, 4> mCreated{}, mReused{}, mNanoseconds{};
    uint64_t mClock = 0, mLinks = 0, mLinkNanoseconds = 0, mMaxLinkNanoseconds = 0;
    uint64_t mRejected = 0;
    std::mutex mMutex;
    std::condition_variable mPartReady;

    static uint64_t Now() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    static void Float(Key& key, float value) {
        uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); key.push_back(bits);
    }
    uint64_t Program(ProgramBytes bytes) {
        const auto* words = static_cast<const uint32_t*>(bytes.Data);
        std::vector<uint32_t> program(words, words + bytes.Size / sizeof(uint32_t));
        const auto found = mPrograms.find(program);
        if (found != mPrograms.end()) return found->second;
        const auto id = static_cast<uint64_t>(mPrograms.size() + 1);
        mPrograms.emplace(std::move(program), id);
        return id;
    }
    static void Stencil(Key& k, const VkStencilOpState& s) {
        k.insert(k.end(), {uint64_t(s.failOp), uint64_t(s.passOp), uint64_t(s.depthFailOp),
            uint64_t(s.compareOp), s.compareMask, s.writeMask, s.reference});
    }
    static int DynamicPart(VkDynamicState state) {
        switch (state) {
        case VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE: return 0;
        case VK_DYNAMIC_STATE_VIEWPORT: case VK_DYNAMIC_STATE_SCISSOR:
        case VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT: case VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT:
        case VK_DYNAMIC_STATE_DEPTH_BIAS: return 1;
        case VK_DYNAMIC_STATE_CULL_MODE: case VK_DYNAMIC_STATE_FRONT_FACE: return 1;
        case VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE: case VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE:
        case VK_DYNAMIC_STATE_DEPTH_COMPARE_OP: case VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE:
        case VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE: case VK_DYNAMIC_STATE_STENCIL_OP:
        case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK: case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK: return 2;
        case VK_DYNAMIC_STATE_DEPTH_BOUNDS: case VK_DYNAMIC_STATE_STENCIL_REFERENCE: return 2;
        case VK_DYNAMIC_STATE_BLEND_CONSTANTS: return 3;
        default: return -1;
        }
    }
    static bool HasDynamic(const VkGraphicsPipelineCreateInfo& in, VkDynamicState state) {
        for (uint32_t i = 0; i < in.pDynamicState->dynamicStateCount; ++i)
            if (in.pDynamicState->pDynamicStates[i] == state) return true;
        return false;
    }
    static const VkPipelineRenderingCreateInfo* Rendering(const VkGraphicsPipelineCreateInfo& in) {
        const auto* p = static_cast<const VkBaseInStructure*>(in.pNext);
        if (!p || p->sType != VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO || p->pNext)
            return nullptr;
        return reinterpret_cast<const VkPipelineRenderingCreateInfo*>(p);
    }
    static bool Supported(const VkGraphicsPipelineCreateInfo& in, const ProgramBytes* programs) {
        if (in.flags || in.renderPass || in.stageCount != 2 || !in.pStages || !programs ||
            !in.pVertexInputState || !in.pInputAssemblyState || !in.pViewportState ||
            !in.pRasterizationState || !in.pMultisampleState || !in.pDepthStencilState ||
            !in.pColorBlendState || !in.pDynamicState || !Rendering(in)) return false;
        if (in.pVertexInputState->pNext || in.pInputAssemblyState->pNext ||
            in.pViewportState->pNext || in.pRasterizationState->pNext ||
            in.pMultisampleState->pNext || in.pDepthStencilState->pNext ||
            in.pColorBlendState->pNext || in.pDynamicState->pNext) return false;
        if (in.pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST ||
            in.pRasterizationState->rasterizerDiscardEnable ||
            in.pRasterizationState->depthBiasEnable || in.pMultisampleState->sampleShadingEnable)
            return false;
        if ((!HasDynamic(in, VK_DYNAMIC_STATE_VIEWPORT) && !HasDynamic(in, VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT)) ||
            (!HasDynamic(in, VK_DYNAMIC_STATE_SCISSOR) && !HasDynamic(in, VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT)))
            return false;
        VkShaderStageFlags stages = 0;
        for (uint32_t i = 0; i < in.stageCount; ++i) {
            const auto& s = in.pStages[i];
            if (s.pNext || s.pSpecializationInfo || !s.pName || !programs[i].Data ||
                programs[i].Size == 0 || programs[i].Size % 4 != 0 ||
                (s.stage != VK_SHADER_STAGE_VERTEX_BIT && s.stage != VK_SHADER_STAGE_FRAGMENT_BIT) ||
                (stages & s.stage)) return false;
            stages |= s.stage;
        }
        for (uint32_t i = 0; i < in.pDynamicState->dynamicStateCount; ++i)
            if (DynamicPart(in.pDynamicState->pDynamicStates[i]) < 0) return false;
        return true;
    }

  public:
    // VK_ERROR_FEATURE_NOT_PRESENT means the caller must retain its exact
    // monolithic path. Other errors are real driver failures, not cache misses.
    VkResult Create(VkDevice device, PFN_vkCreateGraphicsPipelines create,
        PFN_vkDestroyPipeline destroy, const VkAllocationCallbacks* allocator,
        VkPipelineCache cache, const VkGraphicsPipelineCreateInfo& in,
        const ProgramBytes* programs, VkPipeline* out) {
        std::unique_lock<std::mutex> lock(mMutex);
        if (!Supported(in, programs)) { ++mRejected; return VK_ERROR_FEATURE_NOT_PRESENT; }
        const auto& rendering = *Rendering(in);
        const auto& vi = *in.pVertexInputState;
        const auto& ia = *in.pInputAssemblyState;
        auto raster = *in.pRasterizationState;
        const auto& viewport = *in.pViewportState;
        auto depth = *in.pDepthStencilState;
        const auto& blend = *in.pColorBlendState;
        const auto& ms = *in.pMultisampleState;
        // Only states declared dynamic may leave a device-library identity.
        // The executable retains their original values and applies them at bind.
        if (HasDynamic(in, VK_DYNAMIC_STATE_CULL_MODE)) raster.cullMode = VK_CULL_MODE_NONE;
        if (HasDynamic(in, VK_DYNAMIC_STATE_FRONT_FACE)) raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        if (HasDynamic(in, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE)) depth.depthTestEnable = VK_FALSE;
        if (HasDynamic(in, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE)) depth.depthWriteEnable = VK_FALSE;
        if (HasDynamic(in, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP)) depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        if (HasDynamic(in, VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE)) depth.depthBoundsTestEnable = VK_FALSE;
        if (HasDynamic(in, VK_DYNAMIC_STATE_DEPTH_BOUNDS)) depth.minDepthBounds = depth.maxDepthBounds = 0;
        if (HasDynamic(in, VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE)) depth.stencilTestEnable = VK_FALSE;
        for (auto* face : {&depth.front, &depth.back}) {
            if (HasDynamic(in, VK_DYNAMIC_STATE_STENCIL_OP)) {
                face->failOp = face->passOp = face->depthFailOp = VK_STENCIL_OP_KEEP;
                face->compareOp = VK_COMPARE_OP_ALWAYS;
            }
            if (HasDynamic(in, VK_DYNAMIC_STATE_STENCIL_REFERENCE)) face->reference = 0;
            if (HasDynamic(in, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK)) face->compareMask = 0;
            if (HasDynamic(in, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK)) face->writeMask = 0;
        }
        std::array<Key, 4> keys;
        std::array<std::vector<VkDynamicState>, 4> dynamics;
        for (uint32_t i = 0; i < in.pDynamicState->dynamicStateCount; ++i) {
            auto state = in.pDynamicState->pDynamicStates[i];
            dynamics[size_t(DynamicPart(state))].push_back(state);
        }
        for (size_t p = 0; p < 4; ++p) {
            std::sort(dynamics[p].begin(), dynamics[p].end());
            keys[p].push_back(dynamics[p].size());
            for (auto state : dynamics[p]) keys[p].push_back(uint64_t(state));
        }
        auto& k0 = keys[0];
        k0.insert(k0.end(), {vi.flags, ia.flags, uint64_t(ia.topology), ia.primitiveRestartEnable,
            vi.vertexBindingDescriptionCount, vi.vertexAttributeDescriptionCount});
        for (uint32_t i = 0; i < vi.vertexBindingDescriptionCount; ++i) {
            const auto& b = vi.pVertexBindingDescriptions[i];
            k0.insert(k0.end(), {b.binding, b.stride, uint64_t(b.inputRate)});
        }
        for (uint32_t i = 0; i < vi.vertexAttributeDescriptionCount; ++i) {
            const auto& a = vi.pVertexAttributeDescriptions[i];
            k0.insert(k0.end(), {a.location, a.binding, uint64_t(a.format), a.offset});
        }
        auto& k1 = keys[1];
        k1.insert(k1.end(), {rendering.viewMask, viewport.flags, viewport.viewportCount,
            viewport.scissorCount, raster.flags, raster.depthClampEnable,
            uint64_t(raster.polygonMode), raster.cullMode, uint64_t(raster.frontFace)});
        Float(k1, raster.lineWidth);
        std::array<const VkPipelineShaderStageCreateInfo*, 4> stages{};
        for (uint32_t i = 0; i < in.stageCount; ++i) {
            const auto& s = in.pStages[i];
            const size_t p = s.stage == VK_SHADER_STAGE_VERTEX_BIT ? 1 : 2;
            stages[p] = &s;
            auto& k = keys[p];
            k.insert(k.end(), {s.flags, uint64_t(s.stage), Program(programs[i])});
            for (const char* c = s.pName; *c; ++c) k.push_back(static_cast<unsigned char>(*c));
            k.push_back(0);
        }
        auto& k2 = keys[2];
        k2.insert(k2.end(), {rendering.viewMask, depth.flags, depth.depthTestEnable,
            depth.depthWriteEnable, uint64_t(depth.depthCompareOp), depth.depthBoundsTestEnable,
            depth.stencilTestEnable});
        Stencil(k2, depth.front); Stencil(k2, depth.back);
        Float(k2, depth.minDepthBounds); Float(k2, depth.maxDepthBounds);
        // Multisample state can affect both fragment and output interfaces.
        for (size_t p : {size_t(2), size_t(3)}) {
            auto& k = keys[p];
            k.insert(k.end(), {ms.flags, uint64_t(ms.rasterizationSamples), ms.sampleShadingEnable,
                ms.alphaToCoverageEnable, ms.alphaToOneEnable});
            Float(k, ms.minSampleShading);
            for (uint32_t i = 0; i < (uint32_t(ms.rasterizationSamples) + 31) / 32; ++i)
                k.push_back(ms.pSampleMask ? ms.pSampleMask[i] : 0xffffffffU);
        }
        auto& k3 = keys[3];
        k3.insert(k3.end(), {rendering.viewMask, uint64_t(rendering.depthAttachmentFormat),
            uint64_t(rendering.stencilAttachmentFormat), rendering.colorAttachmentCount,
            blend.flags, blend.logicOpEnable, uint64_t(blend.logicOp), blend.attachmentCount});
        for (uint32_t i = 0; i < rendering.colorAttachmentCount; ++i)
            k3.push_back(uint64_t(rendering.pColorAttachmentFormats[i]));
        for (uint32_t i = 0; i < blend.attachmentCount; ++i) {
            const auto& b = blend.pAttachments[i];
            k3.insert(k3.end(), {b.blendEnable, uint64_t(b.srcColorBlendFactor), uint64_t(b.dstColorBlendFactor),
                uint64_t(b.colorBlendOp), uint64_t(b.srcAlphaBlendFactor), uint64_t(b.dstAlphaBlendFactor),
                uint64_t(b.alphaBlendOp), b.colorWriteMask});
        }
        for (float value : blend.blendConstants) Float(k3, value);
        std::array<VkPipeline, 4> libraries{};
        constexpr VkGraphicsPipelineLibraryFlagsEXT flags[]{
            VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT,
            VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT,
            VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT,
            VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT};
        // Claim every part: the first requester creates it, a link pins it.
        std::array<std::map<Key, Part>::iterator, 4> parts;
        std::array<bool, 4> owner{};
        for (size_t p = 0; p < 4; ++p) {
            auto [entry, inserted] = mParts[p].try_emplace(keys[p]);
            parts[p] = entry;
            owner[p] = inserted;
            if (!inserted) ++mReused[p];
            ++entry->second.Users;
            entry->second.LastUse = ++mClock;
        }
        lock.unlock();
        for (size_t p = 0; p < 4; ++p) {
            if (!owner[p]) continue;
            VkPipelineRenderingCreateInfo formats = rendering;
            if (p != 3) {
                formats.colorAttachmentCount = 0; formats.pColorAttachmentFormats = nullptr;
                formats.depthAttachmentFormat = formats.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
            }
            VkGraphicsPipelineLibraryCreateInfoEXT subset{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT};
            subset.flags = flags[p]; subset.pNext = p == 0 ? nullptr : &formats;
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dynamic.dynamicStateCount = uint32_t(dynamics[p].size()); dynamic.pDynamicStates = dynamics[p].data();
            VkGraphicsPipelineCreateInfo part{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            part.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
            part.pNext = &subset; part.pDynamicState = &dynamic;
            part.layout = (p == 1 || p == 2) ? in.layout : VK_NULL_HANDLE;
            part.basePipelineIndex = -1;
            if (p == 0) { part.pVertexInputState = &vi; part.pInputAssemblyState = &ia; }
            if (p == 1 || p == 2) { part.stageCount = 1; part.pStages = stages[p]; }
            if (p == 1) { part.pRasterizationState = &raster; part.pViewportState = &viewport; }
            if (p == 2) part.pDepthStencilState = &depth;
            if (p == 2 || p == 3) part.pMultisampleState = &ms;
            if (p == 3) part.pColorBlendState = &blend;
            VkPipeline handle = VK_NULL_HANDLE;
            const auto rssBefore = PipelineLibraryResidentBytes();
            const auto start = Now();
            const auto result = create(device, cache, 1, &part, allocator, &handle);
            const auto rssAfter = PipelineLibraryResidentBytes();
            const auto elapsed = Now() - start;
            if (result != VK_SUCCESS && handle) { destroy(device, handle, allocator); handle = VK_NULL_HANDLE; }
            lock.lock();
            parts[p]->second.Handle = handle;
            parts[p]->second.Result = result;
            parts[p]->second.Ready = true;
            mNanoseconds[p] += elapsed;
            if (result == VK_SUCCESS) ++mCreated[p];
            lock.unlock();
            mPartReady.notify_all();
            if (result == VK_SUCCESS && (p == 1 || p == 2))
                std::fprintf(stderr, "TRIAEVUM_NRI_GPL_SHADER_PART_CREATED part=%zu ms=%.1f rss_before=%llu rss_after=%llu\n", p,
                             static_cast<double>(elapsed) / 1.0e6,
                             static_cast<unsigned long long>(rssBefore), static_cast<unsigned long long>(rssAfter));
        }
        // Parts other threads create: wait for those alone.
        VkResult result = VK_SUCCESS;
        lock.lock();
        mPartReady.wait(lock, [&] {
            for (const auto& entry : parts)
                if (!entry->second.Ready) return false;
            return true;
        });
        for (size_t p = 0; p < 4; ++p) {
            libraries[p] = parts[p]->second.Handle;
            if (parts[p]->second.Result != VK_SUCCESS) result = parts[p]->second.Result;
        }
        lock.unlock();
        if (result == VK_SUCCESS) {
            VkPipelineLibraryCreateInfoKHR link{VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR};
            link.libraryCount = uint32_t(libraries.size()); link.pLibraries = libraries.data();
            VkGraphicsPipelineCreateInfo executable{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            executable.pNext = &link; executable.layout = in.layout; executable.basePipelineIndex = -1;
            const auto start = Now();
            result = create(device, cache, 1, &executable, allocator, out);
            const auto elapsed = Now() - start;
            lock.lock();
            ++mLinks; mLinkNanoseconds += elapsed;
            if (elapsed > mMaxLinkNanoseconds) mMaxLinkNanoseconds = elapsed;
        } else {
            lock.lock();
        }
        // Release the pins. A failed part is forgotten once unused so a later
        // request retries it. Final linked executables do not require the
        // library handles to survive: bound the retained libraries per subset.
        for (size_t p = 0; p < 4; ++p) {
            auto& entries = mParts[p];
            const auto entry = parts[p];
            --entry->second.Users;
            if (entry->second.Result != VK_SUCCESS && entry->second.Users == 0) entries.erase(entry);
            while (entries.size() > 128) {
                auto oldest = entries.end();
                for (auto it = entries.begin(); it != entries.end(); ++it)
                    if (it->second.Ready && it->second.Users == 0 &&
                        (oldest == entries.end() || it->second.LastUse < oldest->second.LastUse))
                        oldest = it;
                if (oldest == entries.end()) break;
                destroy(device, oldest->second.Handle, allocator); entries.erase(oldest);
            }
        }
        return result;
    }

    void Clear(VkDevice device, PFN_vkDestroyPipeline destroy, const VkAllocationCallbacks* allocator) {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mLinks || mRejected) {
            for (size_t p = 0; p < 4; ++p)
                std::fprintf(stderr, "TRIAEVUM_NRI_GPL_PART part=%zu created=%llu reused=%llu ns=%llu\n", p,
                    static_cast<unsigned long long>(mCreated[p]), static_cast<unsigned long long>(mReused[p]),
                    static_cast<unsigned long long>(mNanoseconds[p]));
            std::fprintf(stderr, "TRIAEVUM_NRI_GPL_LINK count=%llu ns=%llu max_ns=%llu rejected=%llu\n",
                static_cast<unsigned long long>(mLinks), static_cast<unsigned long long>(mLinkNanoseconds),
                static_cast<unsigned long long>(mMaxLinkNanoseconds), static_cast<unsigned long long>(mRejected));
        }
        for (auto& parts : mParts) {
            for (const auto& entry : parts)
                if (entry.second.Handle) destroy(device, entry.second.Handle, allocator);
            parts.clear();
        }
        mPrograms.clear();
    }
};
} // namespace triaevum_nri
