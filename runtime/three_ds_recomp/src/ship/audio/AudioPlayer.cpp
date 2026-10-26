#include "ship/audio/AudioPlayer.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cstring>

namespace Ship {

AudioPlayer::~AudioPlayer() {
    SPDLOG_TRACE("destruct audio player");
}

bool AudioPlayer::Init() {
    // Initialize sound matrix decoder if matrix surround mode is enabled
    if (mAudioSettings.ChannelSetting == AudioChannelsSetting::audioMatrix51) {
        SPDLOG_INFO("Initializing sound matrix decoder for surround");
        mSoundMatrixDecoder = std::make_unique<SoundMatrixDecoder>(mAudioSettings.SampleRate);
    }
    mInitialized = DoInit();
    return IsInitialized();
}

bool AudioPlayer::IsInitialized() {
    return mInitialized;
}

int32_t AudioPlayer::GetSampleRate() const {
    return mAudioSettings.SampleRate;
}

int32_t AudioPlayer::GetSampleLength() const {
    return mAudioSettings.SampleLength;
}

int32_t AudioPlayer::GetDesiredBuffered() const {
    return mAudioSettings.DesiredBuffered;
}

AudioChannelsSetting AudioPlayer::GetAudioChannels() const {
    return mAudioSettings.ChannelSetting;
}

void AudioPlayer::SetSampleRate(int32_t rate) {
    mAudioSettings.SampleRate = rate;
}

void AudioPlayer::SetSampleLength(int32_t length) {
    mAudioSettings.SampleLength = length;
}

void AudioPlayer::SetDesiredBuffered(int32_t size) {
    mAudioSettings.DesiredBuffered = size;
}

bool AudioPlayer::SetAudioChannels(AudioChannelsSetting channels) {
    if (mAudioSettings.ChannelSetting == channels) {
        return true; // No change needed
    }

    SPDLOG_INFO("Changing audio channels from {} to {}", AudioChannelsSettingName(mAudioSettings.ChannelSetting),
                AudioChannelsSettingName(channels));

    // Close current audio device
    DoClose();

    // Update channel setting
    mAudioSettings.ChannelSetting = channels;

    // Setup or teardown sound matrix decoder
    if (channels == AudioChannelsSetting::audioMatrix51) {
        if (!mSoundMatrixDecoder) {
            mSoundMatrixDecoder = std::make_unique<SoundMatrixDecoder>(mAudioSettings.SampleRate);
        }
    } else {
        // When switching away from matrix mode, release the decoder
        mSoundMatrixDecoder.reset();
    }

    return DoInit();
}

int32_t AudioPlayer::GetNumOutputChannels() const {
    switch (mAudioSettings.ChannelSetting) {
        case AudioChannelsSetting::audioMatrix51:
        case AudioChannelsSetting::audioRaw51:
            return 6;
        case AudioChannelsSetting::audioStereo:
        default:
            return 2;
    }
}

void AudioPlayer::SetOutputGainTarget(uint32_t gainQ16) {
    mOutputGain.SetTarget(gainQ16);
}

void AudioPlayer::SnapOutputGain() {
    mOutputGain.Snap();
}

uint32_t AudioPlayer::GetOutputGainTarget() const {
    return mOutputGain.Target();
}

uint32_t AudioPlayer::GetOutputGainCurrent() const {
    return mOutputGain.Current();
}

uint64_t AudioPlayer::GetOutputPcmFnv1a64() const {
    return mOutputPcmFnv1a64;
}

uint64_t AudioPlayer::GetOutputFramesScaled() const {
    return mOutputFramesScaled;
}

void AudioPlayer::Play(const uint8_t* buf, size_t len) {
    // The host output gain (docs/TRIAEVUM_AUDIO_SETTINGS.md), after the
    // guest's mix and before the surround decoder and the backend. Inert at
    // unity: the guest's bytes go on untouched and uncopied.
    if (buf != nullptr && !mOutputGain.Inert()) {
        const uint32_t channels = mAudioSettings.ChannelSetting == AudioChannelsSetting::audioRaw51 ? 6U : 2U;
        const size_t frames = len / (sizeof(int16_t) * channels);
        mGainScratch.resize((len + 1U) / sizeof(int16_t));
        std::memcpy(mGainScratch.data(), buf, len);
        mOutputFramesScaled += mOutputGain.Process(mGainScratch.data(), frames, channels,
                                                   static_cast<uint32_t>(std::max(mAudioSettings.SampleRate, 1)));
        buf = reinterpret_cast<const uint8_t*>(mGainScratch.data());
    }
    for (size_t index = 0; buf != nullptr && index < len; ++index) {
        mOutputPcmFnv1a64 ^= buf[index];
        mOutputPcmFnv1a64 *= 1099511628211ULL;
    }
    if (mAudioSettings.ChannelSetting != AudioChannelsSetting::audioMatrix51) {
        // Stereo or Raw 5.1 passthrough
        DoPlay(buf, len);
        return;
    }

    if (!mSoundMatrixDecoder) {
        SPDLOG_ERROR("AudioPlayer: Matrix 5.1 mode enabled but SoundMatrixDecoder is not initialized");
        return;
    }

    // Decode stereo to surround using sound matrix decoder
    const auto [surroundOut, surroundLen] = mSoundMatrixDecoder->Process(buf, len);

    // Play the audio
    DoPlay(surroundOut, surroundLen);
}

void AudioPlayer::Flush() {
    if (!mInitialized) {
        return;
    }
    DoFlush();
    if (mSoundMatrixDecoder) {
        mSoundMatrixDecoder->ResetState();
    }
    // Nothing queued is left to ramp from.
    mOutputGain.Snap();
}
} // namespace Ship
