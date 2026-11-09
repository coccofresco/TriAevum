#include "ship/audio/SDLAudioPlayer.h"
#include <spdlog/spdlog.h>

#include <algorithm>

namespace Ship {

uint16_t SdlAudioDeviceBufferSamples(int32_t sampleRate, int32_t sampleLength) {
    constexpr uint32_t kMinimumMilliseconds = 20U, kMinimum = 256U, kMaximum = 4096U;
    const uint32_t rate = sampleRate > 0 ? static_cast<uint32_t>(sampleRate) : 44100U;
    const uint32_t target = std::max<uint32_t>(sampleLength > 0 ? static_cast<uint32_t>(sampleLength) : 0U,
                                               (rate * kMinimumMilliseconds + 999U) / 1000U);
    uint32_t samples = kMinimum;
    while (samples < target && samples < kMaximum) {
        samples *= 2U;
    }
    return static_cast<uint16_t>(samples);
}

SDLAudioPlayer::~SDLAudioPlayer() {
    SPDLOG_TRACE("destruct SDL audio player");
    DoClose();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SDLAudioPlayer::DoClose() {
    if (mDevice != 0) {
        // Pause playback first
        SDL_PauseAudioDevice(mDevice, 1);
        // Clear any queued audio to prevent glitches when reopening
        SDL_ClearQueuedAudio(mDevice);
        SDL_CloseAudioDevice(mDevice);
        mDevice = 0;
    }
}

bool SDLAudioPlayer::DoInit() {
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        SPDLOG_ERROR("SDL init error: {}", SDL_GetError());
        return false;
    }

    // Always open with the correct number of output channels; a device that
    // refuses surround (a handheld's stereo sink) still gets stereo output
    // instead of silence (#79).
    mNumChannels = this->GetNumOutputChannels();
    mNegotiation = {};
    mNegotiation.RequestedChannels = mNumChannels;
    mNegotiation.RequestedSamples = SdlAudioDeviceBufferSamples(this->GetSampleRate(), this->GetSampleLength());
    const char* driver = SDL_GetCurrentAudioDriver();

    SDL_AudioSpec want, have;
    const int32_t attempts[] = {mNumChannels, 2};
    const size_t attemptCount = mNumChannels == 2 ? 1 : 2;
    for (size_t attempt = 0; attempt < attemptCount && mDevice == 0; ++attempt) {
        const int32_t channels = attempts[attempt];
        SDL_zero(want);
        SDL_zero(have);
        want.freq = this->GetSampleRate();
        want.format = AUDIO_S16SYS;
        want.channels = static_cast<Uint8>(channels);
        want.samples = mNegotiation.RequestedSamples;
        want.callback = NULL;
        // allowed_changes 0: SDL converts to the device format, so the queued
        // PCM keeps the game's rate and layout whatever the sink runs at.
        mDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (mDevice != 0) {
            mNegotiation.FellBackToStereo = channels != mNumChannels;
            mNumChannels = channels;
        } else {
            SPDLOG_WARN("SDL audio output: opening {} Hz, {} channels on driver {} failed: {}", want.freq, channels,
                        driver ? driver : "none", SDL_GetError());
        }
    }
    if (mDevice == 0) {
        SPDLOG_ERROR("SDL audio output unavailable on driver {}: the game runs without sound",
                     driver ? driver : "none");
        return false;
    }
    mNegotiation.ObtainedFrequency = have.freq;
    mNegotiation.ObtainedChannels = have.channels;
    mNegotiation.ObtainedSamples = have.samples;
    // Warning level: release logs keep it, and an audio report needs it first.
    SPDLOG_WARN("SDL audio output: driver={} requested {} Hz/{} ch/{} samples, device {} Hz/{} ch/{} samples{}",
                driver ? driver : "none", this->GetSampleRate(), mNegotiation.RequestedChannels,
                mNegotiation.RequestedSamples, have.freq, static_cast<int>(have.channels), have.samples,
                mNegotiation.FellBackToStereo ? " (surround refused: stereo)" : "");

    SDL_PauseAudioDevice(mDevice, 0);
    return true;
}

int SDLAudioPlayer::Buffered() {
    return SDL_GetQueuedAudioSize(mDevice) / (sizeof(int16_t) * mNumChannels);
}

void SDLAudioPlayer::DoPlay(const uint8_t* buf, size_t len) {
    if (Buffered() < 6000) {
        // Don't fill the audio buffer too much in case this happens
        SDL_QueueAudio(mDevice, buf, len);
    }
}


void SDLAudioPlayer::DoFlush() {
    if (mDevice != 0) {
        SDL_ClearQueuedAudio(mDevice);
    }
}
} // namespace Ship
