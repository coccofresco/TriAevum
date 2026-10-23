#pragma once

// Developer diagnostic for the static shadow cache: whether the final content
// of each native owner's shadow casters stays unchanged between consecutive
// executions of a shadow pass. It is the offline evidence for which owners
// may be cached in static shadow layers. Format: TRACE_FORMAT.md v1 of the
// static-shadow catalog work (one cumulative record line per owner instance
// and target). Owners are opaque here; no title semantics.

#include "fast/renderer3ds/virtual_shadow_map.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Fast::Renderer3ds {

struct ShadowCasterTraceSample {
    uint64_t Owner = 0;
    // Native lifetime of the owner instance: a reused owner address starts a
    // new record.
    uint64_t Lifetime = 0;
    // Stable identity of the caster within its owner (part, slot, geometry).
    uint64_t PartKey = 0;
    // The content version the page cache uses, after every pass-owned input.
    uint64_t FinalVersion = 0;
    // Its components: the native geometry content and the geometry with its
    // world pose. A final change with both unchanged comes from material
    // (alpha-tested cutouts) or shadow pipeline state.
    uint64_t GeometryVersion = 0;
    uint64_t PoseVersion = 0;
    VirtualShadowCasterVersionState State = VirtualShadowCasterVersionState::Unavailable;
    bool Dynamic = false;
    bool Promoted = false;
};

class ShadowCasterStabilityTrace final {
  public:
    explicit ShadowCasterStabilityTrace(uint32_t warmup = 120U, uint32_t interval = 120U) noexcept
        : mWarmup(warmup), mInterval(std::max<uint32_t>(interval, 1U)) {}

    [[nodiscard]] static uint64_t Mix(uint64_t value) noexcept {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    // One execution of the pass for a target. Appends the emitted lines.
    void Observe(uint64_t target, uint64_t frame, uint64_t timeMs, std::span<const ShadowCasterTraceSample> samples,
                 std::vector<std::string>& lines) {
        if (!mHeader) {
            mHeader = true;
            char header[128];
            std::snprintf(header, sizeof(header), "[shadow-caster-trace] v=1 warmup=%u interval=%u clock=unix_ms",
                          mWarmup, mInterval);
            lines.emplace_back(header);
        }
        auto& state = mTargets[target];
        const uint64_t execution = ++state.Executions;
        mScratch.assign(samples.begin(), samples.end());
        std::sort(mScratch.begin(), mScratch.end(), [](const auto& a, const auto& b) {
            return std::pair(a.Owner, a.Lifetime) < std::pair(b.Owner, b.Lifetime);
        });
        for (size_t first = 0; first < mScratch.size();) {
            size_t last = first;
            uint64_t keys = 0, full = 0, geometryHash = 0, poseHash = 0;
            Record observation;
            while (last < mScratch.size() && mScratch[last].Owner == mScratch[first].Owner &&
                   mScratch[last].Lifetime == mScratch[first].Lifetime) {
                const auto& sample = mScratch[last];
                keys += Mix(sample.PartKey);
                full += Mix(sample.PartKey ^ Mix(sample.FinalVersion));
                geometryHash += Mix(sample.PartKey ^ Mix(sample.GeometryVersion ^ 0x6a09e667f3bcc909ULL));
                poseHash += Mix(sample.PartKey ^ Mix(sample.PoseVersion ^ 0xbb67ae8584caa73bULL));
                ++observation.States[static_cast<size_t>(sample.State)];
                observation.Dynamic += sample.Dynamic ? 1U : 0U;
                observation.Promoted += sample.Promoted ? 1U : 0U;
                ++last;
            }
            const uint32_t parts = static_cast<uint32_t>(last - first);
            const auto key = std::pair(mScratch[first].Owner, mScratch[first].Lifetime);
            auto [it, inserted] = state.Records.try_emplace(key);
            auto& record = it->second;
            if (inserted) {
                record.FirstExecution = execution;
                record.FrameFirst = frame;
                record.TimeFirst = timeMs;
                record.PartsMinimum = parts;
            } else if (record.LastExecution + 1U == execution && record.LastExecution >= record.FirstExecution + mWarmup) {
                ++record.Pairs;
                const bool membership = keys != record.Keys;
                const bool version = !membership && full != record.Full;
                if (membership)
                    ++record.MembershipChanges;
                else if (version)
                    ++record.VersionChanges;
                else
                    ++record.Unchanged;
                if (version) {
                    if (geometryHash != record.GeometryHash)
                        ++record.GeometryChanges;
                    else if (poseHash != record.PoseHash)
                        ++record.PoseChanges;
                    else
                        ++record.MaterialChanges;
                }
                if ((membership || version) && record.FirstChangeFrame == 0U) {
                    record.FirstChangeFrame = frame;
                    record.FirstChangeTime = timeMs;
                }
            }
            record.LastExecution = execution;
            record.FrameLast = frame;
            record.TimeLast = timeMs;
            record.Keys = keys;
            record.Full = full;
            record.GeometryHash = geometryHash;
            record.PoseHash = poseHash;
            ++record.Observed;
            record.PartsMinimum = std::min(record.PartsMinimum, parts);
            record.PartsMaximum = std::max(record.PartsMaximum, parts);
            for (size_t i = 0; i < record.States.size(); ++i)
                record.States[i] += observation.States[i];
            record.Dynamic += observation.Dynamic;
            record.Promoted += observation.Promoted;
            record.Dirty = true;
            first = last;
        }
        if (execution % mInterval != 0U)
            return;
        for (auto it = state.Records.begin(); it != state.Records.end();) {
            if (it->second.Dirty) {
                lines.push_back(Format(target, it->first, it->second));
                it->second.Dirty = false;
            }
            it = execution - it->second.LastExecution > mInterval ? state.Records.erase(it) : std::next(it);
        }
    }

    // Emits every record changed since its last line.
    void Flush(std::vector<std::string>& lines) {
        for (auto& [target, state] : mTargets)
            for (auto& [key, record] : state.Records)
                if (record.Dirty) {
                    lines.push_back(Format(target, key, record));
                    record.Dirty = false;
                }
    }

  private:
    struct Record {
        uint64_t FirstExecution = 0, LastExecution = 0;
        uint64_t FrameFirst = 0, FrameLast = 0, TimeFirst = 0, TimeLast = 0;
        uint64_t Observed = 0, Pairs = 0, Unchanged = 0, MembershipChanges = 0, VersionChanges = 0;
        uint64_t FirstChangeFrame = 0, FirstChangeTime = 0;
        uint32_t PartsMinimum = 0, PartsMaximum = 0;
        std::array<uint64_t, 5> States{};
        uint64_t Dynamic = 0, Promoted = 0;
        uint64_t Keys = 0, Full = 0, GeometryHash = 0, PoseHash = 0;
        uint64_t GeometryChanges = 0, PoseChanges = 0, MaterialChanges = 0;
        bool Dirty = false;
    };
    struct TargetState {
        uint64_t Executions = 0;
        std::map<std::pair<uint64_t, uint64_t>, Record> Records;
    };

    static std::string Format(uint64_t target, const std::pair<uint64_t, uint64_t>& key, const Record& r) {
        using State = VirtualShadowCasterVersionState;
        const auto count = [&](State state) {
            return static_cast<unsigned long long>(r.States[static_cast<size_t>(state)]);
        };
        char line[1024];
        std::snprintf(
            line, sizeof(line),
            "[shadow-caster-stability] v=1 target=%016" PRIx64 " owner=%016" PRIx64 " actor=%04x lifetime=%" PRIu64
            " t_first=%" PRIu64 " t_last=%" PRIu64 " frame_first=%" PRIu64 " frame_last=%" PRIu64
            " observed=%" PRIu64 " pairs=%" PRIu64 " unchanged=%" PRIu64 " membership_changes=%" PRIu64
            " version_changes=%" PRIu64 " first_change_frame=%" PRIu64 " first_change_t=%" PRIu64
            " parts_min=%u parts_max=%u rigid=%llu uncacheable=%llu skinned=%llu nonhomogeneous=%llu"
            " unavailable=%llu dynamic=%" PRIu64 " promoted=%" PRIu64 " geometry_changes=%" PRIu64
            " pose_changes=%" PRIu64 " material_changes=%" PRIu64,
            target, key.first, static_cast<unsigned>((key.first >> 32U) & 0xffffU), key.second, r.TimeFirst,
            r.TimeLast, r.FrameFirst, r.FrameLast, r.Observed, r.Pairs, r.Unchanged, r.MembershipChanges,
            r.VersionChanges, r.FirstChangeFrame, r.FirstChangeTime, r.PartsMinimum, r.PartsMaximum,
            count(State::Rigid), count(State::Uncacheable), count(State::Skinned), count(State::NonHomogeneous),
            count(State::Unavailable), r.Dynamic, r.Promoted, r.GeometryChanges, r.PoseChanges, r.MaterialChanges);
        return line;
    }

    uint32_t mWarmup;
    uint32_t mInterval;
    bool mHeader = false;
    std::map<uint64_t, TargetState> mTargets;
    std::vector<ShadowCasterTraceSample> mScratch;
};

} // namespace Fast::Renderer3ds
