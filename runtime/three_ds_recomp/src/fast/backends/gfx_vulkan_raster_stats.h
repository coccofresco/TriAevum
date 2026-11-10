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

[[nodiscard]] inline bool Enabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TRIAEVUM_RASTER_STATS");
        return value != nullptr && std::string_view(value) == "1";
    }();
    return enabled;
}

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
        if (!gPipelineStatisticsEnabled || slot >= kSlots || mSlots[slot].Used >= kQueriesPerSlot ||
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
    static constexpr uint64_t kReportFrames = 300;

    struct Slot {
        uint32_t Frame = UINT32_MAX;
        uint32_t Used = 0;
    };

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
        state.Frame = frame;
        state.Used = 0;
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
        mFrames = mQueriedFrames = mScopes = mDraws = mExplicitDepth = mDiscard = 0;
        mDepthWriteOff = mDepthTestOff = mBlend = mOpaqueWorld = mAttachments = 0;
        mInvocations = mPixels = 0;
    }

    VkQueryPool mPool = VK_NULL_HANDLE;
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
