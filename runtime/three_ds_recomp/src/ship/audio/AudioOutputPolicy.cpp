#include "ship/audio/AudioOutputPolicy.h"

#include "ship/audio/AudioOutputGain.h"
#include "ship/audio/AudioPlayer.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include <nlohmann/json.hpp>

namespace Ship {
namespace {

constexpr const char* kMasterVolumeKey = "MasterVolume";
constexpr const char* kMutedKey = "Muted";
constexpr const char* kMuteWhenUnfocusedKey = "MuteWhenUnfocused";

AudioOutputSettings Clamped(AudioOutputSettings settings) {
    settings.MasterVolume =
        std::clamp(settings.MasterVolume, kAudioMasterVolumeMinimum, kAudioMasterVolumeMaximum);
    return settings;
}

} // namespace

std::uint32_t AudioMasterVolumeGainQ16(int masterVolume) noexcept {
    const auto volume = static_cast<std::uint64_t>(
        std::clamp(masterVolume, kAudioMasterVolumeMinimum, kAudioMasterVolumeMaximum));
    return static_cast<std::uint32_t>((volume * volume * AudioOutputGain::kUnity + 5000U) / 10000U);
}

nlohmann::json SerializeAudioOutputSettings(const AudioOutputSettings& settings) {
    const auto clamped = Clamped(settings);
    return nlohmann::json{{kMasterVolumeKey, clamped.MasterVolume},
                          {kMutedKey, clamped.Muted},
                          {kMuteWhenUnfocusedKey, clamped.MuteWhenUnfocused}};
}

AudioOutputSettings ParseAudioOutputSettings(const nlohmann::json& block) {
    AudioOutputSettings settings;
    if (!block.is_object()) {
        return settings;
    }
    if (const auto volume = block.find(kMasterVolumeKey); volume != block.end() && volume->is_number()) {
        const double value = volume->get<double>();
        if (std::isfinite(value)) {
            settings.MasterVolume = static_cast<int>(std::lround(
                std::clamp(value, static_cast<double>(kAudioMasterVolumeMinimum),
                           static_cast<double>(kAudioMasterVolumeMaximum))));
        }
    }
    if (const auto muted = block.find(kMutedKey); muted != block.end() && muted->is_boolean()) {
        settings.Muted = muted->get<bool>();
    }
    if (const auto unfocused = block.find(kMuteWhenUnfocusedKey);
        unfocused != block.end() && unfocused->is_boolean()) {
        settings.MuteWhenUnfocused = unfocused->get<bool>();
    }
    return settings;
}

std::vector<std::int16_t> GenerateAudioPreviewTone(std::uint32_t sampleRate) {
    if (sampleRate == 0U) {
        return {};
    }
    const std::uint32_t frames = sampleRate * 120U / 1000U;
    const std::uint32_t fade = std::max<std::uint32_t>(1U, sampleRate * 10U / 1000U);
    constexpr double kAmplitude = 0.25 * 32767.0; // -12 dBFS
    constexpr double kFrequency = 660.0;
    std::vector<std::int16_t> samples(static_cast<std::size_t>(frames) * 2U);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        double envelope = 1.0;
        const std::uint32_t edge = std::min(frame, frames - 1U - frame);
        if (edge < fade) {
            envelope = 0.5 - 0.5 * std::cos(std::numbers::pi * static_cast<double>(edge) / fade);
        }
        const double phase = 2.0 * std::numbers::pi * kFrequency * frame / sampleRate;
        const auto value = static_cast<std::int16_t>(std::lround(kAmplitude * envelope * std::sin(phase)));
        samples[frame * 2U] = value;
        samples[frame * 2U + 1U] = value;
    }
    return samples;
}

AudioOutputPolicy::AudioOutputPolicy(std::shared_ptr<AudioOutputPersistencePort> persistence)
    : mPersistence(std::move(persistence)) {
    if (mPersistence == nullptr) {
        return;
    }
    nlohmann::json block;
    if (!mPersistence->LoadAudio(block)) {
        // A port without a store: the settings live for this session only.
        mPersistence.reset();
        return;
    }
    mSettings = ParseAudioOutputSettings(block);
    mSaveState = AudioOutputSaveState::Saved;
}

AudioOutputSettings AudioOutputPolicy::Settings() const {
    std::scoped_lock lock(mMutex);
    return mSettings;
}

bool AudioOutputPolicy::Persistent() const {
    std::scoped_lock lock(mMutex);
    return mPersistence != nullptr;
}

AudioOutputSaveState AudioOutputPolicy::SaveState() const {
    std::scoped_lock lock(mMutex);
    return mSaveState;
}

std::string AudioOutputPolicy::LastError() const {
    std::scoped_lock lock(mMutex);
    return mLastError;
}

std::uint64_t AudioOutputPolicy::Revision() const {
    std::scoped_lock lock(mMutex);
    return mRevision;
}

bool AudioOutputPolicy::StoreLocked(std::string* error) {
    if (mPersistence == nullptr) {
        mSaveState = AudioOutputSaveState::SessionOnly;
        mLastError.clear();
        return true;
    }
    std::string failure;
    if (mPersistence->StoreAudio(SerializeAudioOutputSettings(mSettings), &failure)) {
        mSaveState = AudioOutputSaveState::Saved;
        mLastError.clear();
        return true;
    }
    mSaveState = AudioOutputSaveState::Failed;
    mLastError = failure.empty() ? std::string("the configuration file could not be written") : failure;
    if (error != nullptr) {
        *error = mLastError;
    }
    return false;
}

bool AudioOutputPolicy::Apply(const AudioOutputSettings& settings, std::string* error) {
    std::scoped_lock lock(mMutex);
    const auto clamped = Clamped(settings);
    if (clamped != mSettings) {
        mSettings = clamped;
        ++mRevision;
        PushGainLocked();
    }
    return StoreLocked(error);
}

void AudioOutputPolicy::Preview(const AudioOutputSettings& settings) {
    std::scoped_lock lock(mMutex);
    const auto clamped = Clamped(settings);
    if (clamped == mSettings) {
        return;
    }
    mSettings = clamped;
    ++mRevision;
    PushGainLocked();
}

bool AudioOutputPolicy::Save(std::string* error) {
    std::scoped_lock lock(mMutex);
    return StoreLocked(error);
}

void AudioOutputPolicy::SetWindowFocused(bool focused) {
    std::scoped_lock lock(mMutex);
    if (mFocused == focused) {
        return;
    }
    mFocused = focused;
    PushGainLocked();
}

bool AudioOutputPolicy::WindowFocused() const {
    std::scoped_lock lock(mMutex);
    return mFocused;
}

std::uint32_t AudioOutputPolicy::TargetGainQ16() const {
    std::scoped_lock lock(mMutex);
    if (mSettings.Muted || (mSettings.MuteWhenUnfocused && !mFocused)) {
        return 0U;
    }
    return AudioMasterVolumeGainQ16(mSettings.MasterVolume);
}

void AudioOutputPolicy::PushGainLocked() {
    if (mPlayer == nullptr) {
        return;
    }
    const std::uint32_t gain = mSettings.Muted || (mSettings.MuteWhenUnfocused && !mFocused)
                                   ? 0U
                                   : AudioMasterVolumeGainQ16(mSettings.MasterVolume);
    mPlayer->SetOutputGainTarget(gain);
}

void AudioOutputPolicy::BindOutput(std::shared_ptr<AudioPlayer> player) {
    std::scoped_lock lock(mMutex);
    mPlayer = std::move(player);
    if (mPlayer == nullptr) {
        return;
    }
    PushGainLocked();
    mPlayer->SnapOutputGain();
}

bool AudioOutputPolicy::OutputBound() const {
    std::scoped_lock lock(mMutex);
    return mPlayer != nullptr;
}

void AudioOutputPolicy::RequestPreview() {
    std::scoped_lock lock(mMutex);
    mPreviewRequested = true;
}

bool AudioOutputPolicy::TakePreviewRequest() {
    std::scoped_lock lock(mMutex);
    const bool requested = mPreviewRequested;
    mPreviewRequested = false;
    return requested && !mSettings.Muted && mSettings.MasterVolume > 0;
}

} // namespace Ship
