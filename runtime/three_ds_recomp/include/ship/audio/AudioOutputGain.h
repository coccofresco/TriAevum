#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Ship {

/**
 * @brief The host output gain applied to finished PCM16 at the output player.
 *
 * The gain is an unsigned Q16 value (65536 = unity). While the gain in effect
 * and the target are both unity, Process() leaves the samples untouched, so
 * the stage is inert at full volume. A new target is reached by a linear ramp
 * per interleaved frame (all channels of a frame share one gain) over
 * kRampMilliseconds of output, so a change never clicks. The arithmetic is
 * integer and identical on every platform.
 *
 * SetTarget() may be called from any thread; Process() and Snap() belong to
 * the thread that submits audio.
 */
class AudioOutputGain {
  public:
    static constexpr std::uint32_t kUnity = 1U << 16U;
    static constexpr std::uint32_t kRampMilliseconds = 20U;

    void SetTarget(std::uint32_t gainQ16) noexcept;
    [[nodiscard]] std::uint32_t Target() const noexcept;
    /** @brief The gain in effect at the end of the last processed buffer. */
    [[nodiscard]] std::uint32_t Current() const noexcept { return mCurrent; }

    /** @brief True when Process() would leave samples unchanged. */
    [[nodiscard]] bool Inert() const noexcept;

    /**
     * @brief Scales @p frames interleaved frames of @p channels samples in place.
     * @return the number of frames whose samples were scaled (0 when inert).
     */
    std::size_t Process(std::int16_t* samples, std::size_t frames, std::uint32_t channels,
                        std::uint32_t sampleRate) noexcept;

    /** @brief Ends any ramp at the target (after queued output was discarded). */
    void Snap() noexcept;

    /** @brief One sample at a Q16 gain, rounded to nearest and clamped. */
    [[nodiscard]] static std::int16_t Scale(std::int16_t sample, std::uint32_t gainQ16) noexcept;

  private:
    void BeginRampIfRetargeted(std::uint32_t target, std::uint32_t sampleRate) noexcept;

    std::atomic<std::uint32_t> mTarget{kUnity};
    std::uint32_t mCurrent = kUnity;
    // The ramp: from mRampFrom to mRampTo over mRampLength frames, mRampDone done.
    std::uint32_t mRampFrom = kUnity;
    std::uint32_t mRampTo = kUnity;
    std::uint32_t mRampLength = 0U;
    std::uint32_t mRampDone = 0U;
};

} // namespace Ship
