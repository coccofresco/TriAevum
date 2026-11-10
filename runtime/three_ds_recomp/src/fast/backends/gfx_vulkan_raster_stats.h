// Developer diagnostic (TRIAEVUM_RASTER_STATS=1): what the draws of the guided
// native PICA scene target cost the rasterizer. Per 300 frames it prints the
// draw-state mix of those draws and, when the device enables
// pipelineStatisticsQuery, the fragment-shader invocations of every guided
// native scope against the target's pixel count (overdraw). Zero cost when off.
#pragma once
#ifdef ENABLE_OOT3D_VULKAN

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace Fast::RasterStats {

// 1: one query per guided scope. 2: one query per guided draw, split by draw
// class (opaque, opaque with discard, blended, other) instead.
[[nodiscard]] inline int Level() noexcept {
    static const int level = [] {
        const char* value = std::getenv("TRIAEVUM_RASTER_STATS");
        if (value == nullptr)
            return 0;
        const std::string_view text(value);
        return text == "1" ? 1 : text == "2" ? 2 : 0;
    }();
    return level;
}
[[nodiscard]] inline bool Enabled() noexcept { return Level() != 0; }
[[nodiscard]] inline bool PerDraw() noexcept { return Level() == 2; }

enum DrawClass : uint32_t { kOpaque = 0, kOpaqueDiscard = 1, kBlended = 2, kOther = 3, kClasses = 4 };

// Set by device creation when the feature was enabled for this diagnostic.
inline bool gPipelineStatisticsEnabled = false;

struct DrawState {
    bool ExplicitDepth = false;
    bool Discard = false;
    bool DepthTest = false;
    bool DepthWrite = false;
    bool Blend = false;
    bool OpaqueWorld = false;
    uint32_t Attachments = 0;
};

class Collector {
public:
    static Collector& Instance() {
        static Collector collector;
        return collector;
    }

    // Outside any rendering scope, before the native scope begins: resets the
    // query this scope will use. Returns false when no query is recorded.
    bool PrepareScope(VkDevice device, VkCommandBuffer commandBuffer, uint32_t slot, uint32_t frame,
                      bool guided, uint32_t width, uint32_t height) {
        AdvanceFrame(device, slot, frame);
        if (!guided)
            return false;
        ++mScopes;
        mFramePixels = std::max<uint64_t>(mFramePixels, uint64_t{ width } * height);
        if (PerDraw() || !gPipelineStatisticsEnabled || slot >= kSlots || mSlots[slot].Used >= kQueriesPerSlot ||
            !EnsurePool(device))
            return false;
        mPendingQuery = slot * kQueriesPerSlot + mSlots[slot].Used++;
        vkCmdResetQueryPool(commandBuffer, mPool, mPendingQuery, 1);
        return true;
    }

    // Inside the scope, right after it began.
    void BeginQuery(VkCommandBuffer commandBuffer) {
        vkCmdBeginQuery(commandBuffer, mPool, mPendingQuery, 0);
        mQueryActive = true;
    }

    // Inside the scope, before it ends.
    void EndQuery(VkCommandBuffer commandBuffer) {
        if (!mQueryActive)
            return;
        vkCmdEndQuery(commandBuffer, mPool, mPendingQuery);
        mQueryActive = false;
    }

    // Level 2: outside any rendering scope, before a guided scope begins.
    void ResetDrawQueries(VkDevice device, VkCommandBuffer commandBuffer, uint32_t slot, uint32_t frame) {
        AdvanceFrame(device, slot, frame);
        if (!gPipelineStatisticsEnabled || slot >= kSlots || mSlots[slot].DrawReset || !EnsureDrawPool(device))
            return;
        vkCmdResetQueryPool(commandBuffer, mDrawPool, slot * kDrawQueriesPerSlot, kDrawQueriesPerSlot);
        mSlots[slot].DrawReset = true;
    }

    // Level 2: around one guided draw's commands, inside the scope.
    bool BeginDraw(VkDevice device, VkCommandBuffer commandBuffer, uint32_t slot, uint32_t frame, DrawClass drawClass) {
        AdvanceFrame(device, slot, frame);
        if (!gPipelineStatisticsEnabled || slot >= kSlots || !mSlots[slot].DrawReset ||
            mSlots[slot].DrawUsed >= kDrawQueriesPerSlot || mDrawPool == VK_NULL_HANDLE)
            return false;
        const uint32_t index = slot * kDrawQueriesPerSlot + mSlots[slot].DrawUsed++;
        mDrawClasses[index] = drawClass;
        vkCmdBeginQuery(commandBuffer, mDrawPool, index, 0);
        mPendingDrawQuery = index;
        return true;
    }
    void EndDraw(VkCommandBuffer commandBuffer) { vkCmdEndQuery(commandBuffer, mDrawPool, mPendingDrawQuery); }

    void RecordDraw(const DrawState& draw) {
        ++mDraws;
        mExplicitDepth += draw.ExplicitDepth ? 1U : 0U;
        mDiscard += draw.Discard ? 1U : 0U;
        mDepthTestOff += draw.DepthTest ? 0U : 1U;
        mDepthWriteOff += draw.DepthWrite ? 0U : 1U;
        mBlend += draw.Blend ? 1U : 0U;
        mOpaqueWorld += draw.OpaqueWorld ? 1U : 0U;
        mAttachments += draw.Attachments;
    }

private:
    static constexpr uint32_t kSlots = 2;
    static constexpr uint32_t kQueriesPerSlot = 32;
    static constexpr uint32_t kDrawQueriesPerSlot = 1024;
    static constexpr uint64_t kReportFrames = 300;

    struct Slot {
        uint32_t Frame = UINT32_MAX;
        uint32_t Used = 0;
        uint32_t DrawUsed = 0;
        bool DrawReset = false;
    };

    bool EnsureDrawPool(VkDevice device) {
        if (mDrawPool != VK_NULL_HANDLE)
            return true;
        if (mDrawPoolFailed)
            return false;
        VkQueryPoolCreateInfo info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
        info.queryCount = kSlots * kDrawQueriesPerSlot;
        info.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
        if (vkCreateQueryPool(device, &info, nullptr, &mDrawPool) != VK_SUCCESS) {
            mDrawPool = VK_NULL_HANDLE;
            mDrawPoolFailed = true;
            return false;
        }
        return true;
    }

    bool EnsurePool(VkDevice device) {
        if (mPool != VK_NULL_HANDLE)
            return true;
        if (mPoolFailed)
            return false;
        VkQueryPoolCreateInfo info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
        info.queryCount = kSlots * kQueriesPerSlot;
        info.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
        if (vkCreateQueryPool(device, &info, nullptr, &mPool) != VK_SUCCESS) {
            mPool = VK_NULL_HANDLE;
            mPoolFailed = true;
            std::fprintf(stderr, "[raster-stats] pipeline statistics query pool unavailable\n");
            return false;
        }
        return true;
    }

    // A slot is recorded again only after its previous submission completed,
    // so its queries are read without waiting.
    void AdvanceFrame(VkDevice device, uint32_t slot, uint32_t frame) {
        if (frame == mFrame)
            return;
        mFrame = frame;
        mPixels += mFramePixels;
        mFramePixels = 0;
        if (++mFrames >= kReportFrames)
            Report();
        if (slot >= kSlots || mSlots[slot].Frame == frame)
            return;
        auto& state = mSlots[slot];
        if (state.Used != 0U && mPool != VK_NULL_HANDLE) {
            std::array<uint64_t, kQueriesPerSlot> results{};
            if (vkGetQueryPoolResults(device, mPool, slot * kQueriesPerSlot, state.Used,
                                      sizeof(uint64_t) * state.Used, results.data(), sizeof(uint64_t),
                                      VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
                for (uint32_t index = 0; index < state.Used; ++index)
                    mInvocations += results[index];
                ++mQueriedFrames;
            }
        }
        if (state.DrawUsed != 0U && mDrawPool != VK_NULL_HANDLE) {
            std::array<uint64_t, kDrawQueriesPerSlot> results{};
            if (vkGetQueryPoolResults(device, mDrawPool, slot * kDrawQueriesPerSlot, state.DrawUsed,
                                      sizeof(uint64_t) * state.DrawUsed, results.data(), sizeof(uint64_t),
                                      VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
                for (uint32_t index = 0; index < state.DrawUsed; ++index)
                    mClassInvocations[mDrawClasses[slot * kDrawQueriesPerSlot + index]] += results[index];
                ++mDrawQueriedFrames;
            }
        }
        state.Frame = frame;
        state.Used = 0;
        state.DrawUsed = 0;
        state.DrawReset = false;
    }

    void Report() {
        const double frames = static_cast<double>(mFrames);
        const double pixelsPerFrame = static_cast<double>(mPixels) / frames;
        const double invocationsPerFrame =
            mQueriedFrames == 0U ? 0.0 : static_cast<double>(mInvocations) / static_cast<double>(mQueriedFrames);
        std::fprintf(stderr,
                     "[raster-stats] frames=%llu scopes=%llu draws=%llu explicit_depth=%llu discard=%llu "
                     "depth_write_off=%llu depth_test_off=%llu blend=%llu opaque_world=%llu "
                     "attachments_mean=%.2f fs_invocations_per_frame=%.0f pixels=%.0f overdraw=%.2f%s\n",
                     static_cast<unsigned long long>(mFrames), static_cast<unsigned long long>(mScopes),
                     static_cast<unsigned long long>(mDraws), static_cast<unsigned long long>(mExplicitDepth),
                     static_cast<unsigned long long>(mDiscard), static_cast<unsigned long long>(mDepthWriteOff),
                     static_cast<unsigned long long>(mDepthTestOff), static_cast<unsigned long long>(mBlend),
                     static_cast<unsigned long long>(mOpaqueWorld),
                     mDraws == 0U ? 0.0 : static_cast<double>(mAttachments) / static_cast<double>(mDraws),
                     invocationsPerFrame, pixelsPerFrame,
                     pixelsPerFrame > 0.0 ? invocationsPerFrame / pixelsPerFrame : 0.0,
                     gPipelineStatisticsEnabled ? "" : " (no pipeline statistics)");
        if (PerDraw() && mDrawQueriedFrames != 0U) {
            const double n = static_cast<double>(mDrawQueriedFrames);
            std::fprintf(stderr,
                         "[raster-stats] per-draw fs_invocations_per_frame opaque=%.0f opaque_discard=%.0f "
                         "blended=%.0f other=%.0f pixels=%.0f\n",
                         mClassInvocations[kOpaque] / n, mClassInvocations[kOpaqueDiscard] / n,
                         mClassInvocations[kBlended] / n, mClassInvocations[kOther] / n, pixelsPerFrame);
        }
        mClassInvocations = {};
        mDrawQueriedFrames = 0;
        mFrames = mQueriedFrames = mScopes = mDraws = mExplicitDepth = mDiscard = 0;
        mDepthWriteOff = mDepthTestOff = mBlend = mOpaqueWorld = mAttachments = 0;
        mInvocations = mPixels = 0;
    }

    VkQueryPool mPool = VK_NULL_HANDLE;
    VkQueryPool mDrawPool = VK_NULL_HANDLE;
    bool mDrawPoolFailed = false;
    uint32_t mPendingDrawQuery = 0;
    std::array<DrawClass, kSlots * kDrawQueriesPerSlot> mDrawClasses{};
    std::array<uint64_t, kClasses> mClassInvocations{};
    uint64_t mDrawQueriedFrames = 0;
    bool mPoolFailed = false;
    bool mQueryActive = false;
    uint32_t mPendingQuery = 0;
    uint32_t mFrame = UINT32_MAX;
    std::array<Slot, kSlots> mSlots{};
    uint64_t mFramePixels = 0;
    uint64_t mFrames = 0, mQueriedFrames = 0, mScopes = 0, mDraws = 0;
    uint64_t mExplicitDepth = 0, mDiscard = 0, mDepthWriteOff = 0, mDepthTestOff = 0;
    uint64_t mBlend = 0, mOpaqueWorld = 0, mAttachments = 0;
    uint64_t mInvocations = 0, mPixels = 0;
};

} // namespace Fast::RasterStats

#endif
