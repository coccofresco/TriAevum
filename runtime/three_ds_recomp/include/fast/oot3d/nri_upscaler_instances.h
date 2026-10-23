#pragma once

#include "fast/oot3d/nri_interop_context.h"

#ifdef ENABLE_OOT3D_VULKAN
#include <algorithm>
#include <cstddef>
#include <tuple>
#include <vector>

namespace Fast::Oot3d {

// Live instances of one temporal upscaler, one per output contract: a native
// boundary composed at its scene target's extent and the presentation at the
// display's extent are separate streams with separate outputs, so neither
// recreates the other's instance. FSR keys carry preset 0. `Handle` is an NRI
// upscaler (FSR) or a DLSS module feature; `destroy` releases one.
template <typename Handle>
class UpscalerInstances {
  public:
    // Instances with the current quality and preset: the presentation's and
    // a native boundary's outputs, plus one spare extent.
    static constexpr size_t kMaximum = 3U;

    Handle Find(const NriDlssFeatureKey& key) {
        for (auto& entry : mEntries) {
            if (entry.Key == key) {
                entry.LastUse = ++mUseClock;
                return entry.Upscaler;
            }
        }
        return nullptr;
    }
    // Instances of other settings are not used again; among the current
    // settings' extents the least recently used one makes room.
    template <typename Complete, typename Destroy>
    [[nodiscard]] bool MakeRoom(const NriDlssFeatureKey& key, const Complete& complete, const Destroy& destroy) {
        const bool retires = mEntries.size() >= kMaximum ||
            std::any_of(mEntries.begin(), mEntries.end(), [&](const Entry& entry) {
                return entry.Key.Quality != key.Quality || entry.Key.Preset != key.Preset;
            });
        // Complete both submitted work and the current recorded prefix before
        // releasing any feature. Failure leaves every entry and history intact.
        if (retires && !complete()) return false;
        std::erase_if(mEntries, [&](const Entry& entry) {
            if (entry.Key.Quality == key.Quality &&
                entry.Key.Preset == key.Preset)
                return false;
            Retire(entry, destroy);
            return true;
        });
        if (mEntries.size() < kMaximum) return true;
        const auto oldest = std::min_element(
            mEntries.begin(), mEntries.end(),
            [](const Entry& left, const Entry& right) {
                return left.LastUse < right.LastUse;
            });
        Retire(*oldest, destroy);
        mEntries.erase(oldest);
        return true;
    }
    void Add(const NriDlssFeatureKey& key, Handle upscaler) {
        mEntries.push_back({key, upscaler, ++mUseClock});
        ++mGeneration;
    }
    template <typename Destroy>
    void Clear(const Destroy& destroy) {
        for (const auto& entry : mEntries) Retire(entry, destroy);
        mEntries.clear();
    }
    // A dispatch that resumes an instance after another instance's dispatch
    // finds an older history: it restarts it, as one shared instance did
    // when it was recreated for that stream.
    bool Resumes(Handle upscaler) {
        const bool resumed = mLastDispatched != upscaler;
        mLastDispatched = upscaler;
        return resumed;
    }
    [[nodiscard]] std::vector<NriDlssFeatureKey> Keys() const {
        std::vector<NriDlssFeatureKey> keys;
        for (const auto& entry : mEntries) keys.push_back(entry.Key);
        std::sort(keys.begin(), keys.end(),
                  [](const auto& left, const auto& right) {
                      return std::tuple(left.OutputWidth, left.OutputHeight,
                                        left.Quality, left.InputSrgb,
                                        left.Preset) <
                             std::tuple(right.OutputWidth, right.OutputHeight,
                                        right.Quality, right.InputSrgb,
                                        right.Preset);
                  });
        return keys;
    }
    [[nodiscard]] size_t Size() const { return mEntries.size(); }
    [[nodiscard]] uint64_t Generation() const { return mGeneration; }

  private:
    struct Entry {
        NriDlssFeatureKey Key;
        Handle Upscaler = nullptr;
        uint64_t LastUse = 0;
    };
    template <typename Destroy>
    void Retire(const Entry& entry, const Destroy& destroy) {
        if (mLastDispatched == entry.Upscaler) mLastDispatched = nullptr;
        destroy(entry.Upscaler);
        ++mGeneration;
    }
    std::vector<Entry> mEntries;
    uint64_t mUseClock = 0;
    uint64_t mGeneration = 0;
    Handle mLastDispatched = nullptr;
};

} // namespace Fast::Oot3d
#endif
