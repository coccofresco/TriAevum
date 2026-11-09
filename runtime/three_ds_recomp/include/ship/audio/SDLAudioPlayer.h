#pragma once
#include "AudioPlayer.h"
#include <SDL2/SDL.h>

namespace Ship {
/**
 * @brief AudioPlayer implementation backed by SDL2's audio subsystem.
 *
 * SDLAudioPlayer uses `SDL_OpenAudioDevice` to open a platform audio output, then
 * pushes interleaved PCM samples in DoPlay(). It supports both stereo and 6-channel
 * (5.1) output depending on the AudioChannelsSetting configured in AudioPlayer.
 *
 * This backend is available on all platforms that Ship supports.
 */
/**
 * @brief Device buffer (SDL_AudioSpec::samples) requested for a queued output.
 *
 * The game queues audio itself (SDL_QueueAudio), so the device period only sets
 * how often the backend thread must wake. Requesting one game audio frame
 * (160 samples at 32728 Hz, about 5 ms) made PipeWire and PulseAudio run a
 * ~5 ms quantum that a loaded handheld (Steam Deck, #133) cannot keep fed. The
 * request is the next power of two covering at least 20 ms, within 256..4096.
 */
uint16_t SdlAudioDeviceBufferSamples(int32_t sampleRate, int32_t sampleLength);

/** @brief What the audio device negotiation asked for and obtained. */
struct SdlAudioNegotiation {
    int32_t RequestedChannels = 0;
    uint16_t RequestedSamples = 0;
    int32_t ObtainedFrequency = 0;
    int32_t ObtainedChannels = 0;
    uint16_t ObtainedSamples = 0;
    bool FellBackToStereo = false;
};

class SDLAudioPlayer final : public AudioPlayer {
  public:
    /**
     * @brief Constructs an SDLAudioPlayer with the given audio settings.
     * @param settings Sample rate, buffer size, desired buffered frames, and channel mode.
     */
    SDLAudioPlayer(AudioSettings settings) : AudioPlayer(settings) {
    }
    ~SDLAudioPlayer();

    /**
     * @brief Returns the number of audio frames currently queued in the SDL audio device.
     *
     * Used by the audio subsystem to decide how many frames to produce per game tick.
     */
    int Buffered() override;

    /** @brief The negotiated device format (zero before a successful Init()). */
    const SdlAudioNegotiation& Negotiation() const {
        return mNegotiation;
    }

  protected:
    /**
     * @brief Opens the SDL audio device with the configured settings.
     * @return true if the device was opened successfully.
     */
    bool DoInit() override;

    /**
     * @brief Closes the SDL audio device and releases its resources.
     */
    void DoClose() override;

    /**
     * @brief Queues interleaved PCM samples to the SDL audio device.
     * @param buf Interleaved sample data (stereo: L,R,… or surround: FL,FR,C,LFE,SL,SR,…).
     * @param len Length of @p buf in bytes.
     */
    void DoPlay(const uint8_t* buf, size_t len) override;
    void DoFlush() override;

  private:
    SDL_AudioDeviceID mDevice = 0; ///< Handle to the opened SDL audio device.
    int32_t mNumChannels = 2;      ///< Number of output channels (2 for stereo, 6 for 5.1).
    SdlAudioNegotiation mNegotiation; ///< The last Init()'s device negotiation.
};
} // namespace Ship
