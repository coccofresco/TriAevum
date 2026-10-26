#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Ship {

class AudioPlayer;

// The user's audio output settings (docs/TRIAEVUM_AUDIO_SETTINGS.md). Kept
// in the runtime configuration's "Audio" block.
inline constexpr int kAudioMasterVolumeMinimum = 0;
inline constexpr int kAudioMasterVolumeMaximum = 100;
inline constexpr int kAudioMasterVolumeDefault = 100;

struct AudioOutputSettings {
    int MasterVolume = kAudioMasterVolumeDefault; ///< Percent, 0..100.
    bool Muted = false;
    bool MuteWhenUnfocused = false;
    bool operator==(const AudioOutputSettings&) const = default;
};

/** @brief The Q16 output gain of a master volume: round((v / 100)^2 * 65536). */
[[nodiscard]] std::uint32_t AudioMasterVolumeGainQ16(int masterVolume) noexcept;

[[nodiscard]] nlohmann::json SerializeAudioOutputSettings(const AudioOutputSettings& settings);
/** @brief Reads an "Audio" block: out-of-range values clamp, wrong types keep defaults. */
[[nodiscard]] AudioOutputSettings ParseAudioOutputSettings(const nlohmann::json& block);

/**
 * @brief The host's preview tone after a volume change while the game is held:
 * 120 ms of 660 Hz at -12 dBFS with 10 ms fades, interleaved stereo PCM16.
 * Played through the output gain, so it is heard at the chosen level.
 */
[[nodiscard]] std::vector<std::int16_t> GenerateAudioPreviewTone(std::uint32_t sampleRate);

/** @brief Where the owner reads and writes the "Audio" block. */
class AudioOutputPersistencePort {
  public:
    virtual ~AudioOutputPersistencePort() = default;
    /** @brief The stored block (null when there is none). False: no store. */
    [[nodiscard]] virtual bool LoadAudio(nlohmann::json& block) = 0;
    [[nodiscard]] virtual bool StoreAudio(const nlohmann::json& block, std::string* error) = 0;
};

enum class AudioOutputSaveState : std::uint8_t { Saved, SessionOnly, Failed };

/**
 * @brief The one owner of the audio output settings (master volume, mute,
 * mute when unfocused). Owned by Ship::Audio, which binds its output player.
 * Every surface (F1, the in-game menu) changes the settings only through
 * Apply(). Thread-safe.
 */
class AudioOutputPolicy {
  public:
    /** @brief Loads the stored settings through @p persistence (null: session only). */
    explicit AudioOutputPolicy(std::shared_ptr<AudioOutputPersistencePort> persistence);

    [[nodiscard]] AudioOutputSettings Settings() const;
    [[nodiscard]] bool Persistent() const;
    [[nodiscard]] AudioOutputSaveState SaveState() const;
    [[nodiscard]] std::string LastError() const;
    /** @brief Increases with every applied change. */
    [[nodiscard]] std::uint64_t Revision() const;

    /**
     * @brief Applies at once and saves. Values are clamped. False when the save
     * failed: the settings stay applied for the session and @p error says why.
     * Session only (no store) returns true.
     */
    bool Apply(const AudioOutputSettings& settings, std::string* error);
    /** @brief Applies without saving (a slider being dragged); Save() writes it. */
    void Preview(const AudioOutputSettings& settings);
    /** @brief Writes the settings in effect (after Preview, or to retry a failed save). */
    bool Save(std::string* error);

    /** @brief The window has the keyboard focus (desktop); used by MuteWhenUnfocused. */
    void SetWindowFocused(bool focused);
    [[nodiscard]] bool WindowFocused() const;

    /** @brief The gain the output should reach now, Q16 (65536 = unity). */
    [[nodiscard]] std::uint32_t TargetGainQ16() const;

    /**
     * @brief The output player the gain is applied at. Binding snaps the
     * player's gain to the target (nothing to ramp from); null unbinds.
     */
    void BindOutput(std::shared_ptr<AudioPlayer> player);
    [[nodiscard]] bool OutputBound() const;

    /** @brief Asks for one preview tone (a volume change while the game is held). */
    void RequestPreview();
    /** @brief Takes the pending preview request; false when there is none or it would be silent. */
    bool TakePreviewRequest();

  private:
    void PushGainLocked();
    bool StoreLocked(std::string* error);

    mutable std::mutex mMutex;
    std::shared_ptr<AudioOutputPersistencePort> mPersistence;
    std::shared_ptr<AudioPlayer> mPlayer;
    AudioOutputSettings mSettings;
    AudioOutputSaveState mSaveState = AudioOutputSaveState::SessionOnly;
    std::string mLastError;
    std::uint64_t mRevision = 0;
    bool mFocused = true;
    bool mPreviewRequested = false;
};

} // namespace Ship
