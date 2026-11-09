// SDL audio device negotiation (#79 no sound, #133 stutter on Steam Deck):
// the real SDLAudioPlayer opened through SDL's dummy audio driver with the
// game's own format (32728 Hz, 160-sample DSP frames). The device period must
// cover at least 20 ms, and the obtained format is recorded.
//
// --negative asserts the beta.1.2 request (one 160-sample game frame as the
// device period) and must fail.
#include "ship/audio/SDLAudioPlayer.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {
int failures = 0;
void Check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}
} // namespace

int main(int argc, char** argv) {
    const bool negative = argc > 1 && std::strcmp(argv[1], "--negative") == 0;
    constexpr int32_t kGameRate = 32728, kGameFrame = 160;
    const uint16_t samples = Ship::SdlAudioDeviceBufferSamples(kGameRate, kGameFrame);
    std::printf("device buffer request at %d Hz, %d-sample frames: %u samples (%.1f ms)\n", kGameRate, kGameFrame,
                unsigned(samples), samples * 1000.0 / kGameRate);
    if (negative) {
        Check(samples == kGameFrame, "beta.1.2 expectation: one game frame as device period");
        std::printf("NEGATIVE %s: %d failures\n", failures == 0 ? "PASS" : "FAIL", failures);
        return failures == 0 ? 0 : 1;
    }
    Check(samples == 1024, "32728 Hz game output asks for 1024 samples");
    Check(samples * 1000 >= 20 * kGameRate, "device period covers 20 ms");
    Check(Ship::SdlAudioDeviceBufferSamples(48000, 160) == 1024, "48 kHz asks for 1024");
    Check(Ship::SdlAudioDeviceBufferSamples(44100, 4096) == 4096, "a larger frame is kept");
    Check(Ship::SdlAudioDeviceBufferSamples(192000, 100000) == 4096, "upper bound 4096");
    Check(Ship::SdlAudioDeviceBufferSamples(0, 0) == 1024, "unknown rate uses 44.1 kHz");

    // The real open path on SDL's dummy driver (no sound device needed).
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    for (const auto channels : {audioStereo, audioRaw51}) {
        Ship::AudioSettings settings;
        settings.SampleRate = kGameRate;
        settings.SampleLength = kGameFrame;
        settings.ChannelSetting = channels;
        Ship::SDLAudioPlayer player(settings);
        const bool opened = player.Init();
        const auto& negotiation = player.Negotiation();
        std::printf("dummy open channels=%d: opened=%d requested=%u obtained %d Hz/%d ch/%u samples\n",
                    negotiation.RequestedChannels, int(opened), unsigned(negotiation.RequestedSamples),
                    negotiation.ObtainedFrequency, negotiation.ObtainedChannels, unsigned(negotiation.ObtainedSamples));
        Check(opened, "dummy device opens");
        Check(negotiation.RequestedSamples == 1024, "the open requests 1024 samples");
        Check(negotiation.ObtainedFrequency == kGameRate, "the obtained rate is recorded");
        Check(negotiation.ObtainedChannels == negotiation.RequestedChannels, "the obtained layout is recorded");
        const int16_t silence[kGameFrame * 6] = {};
        player.Play(reinterpret_cast<const uint8_t*>(silence), sizeof(silence));
        Check(player.Buffered() >= 0, "queued output is measurable");
    }
    std::printf("%s: %d failures\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
