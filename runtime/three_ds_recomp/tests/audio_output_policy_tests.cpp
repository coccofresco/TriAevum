// The host audio output policy and gain (docs/TRIAEVUM_AUDIO_SETTINGS.md):
// the gain law, the "Audio" block codec, the owner's apply / save / retry /
// session-only contract, the focus mute, output rebinding, and the output
// digest test: a deterministic PCM16 signal through a real AudioPlayer is
// byte-identical at 100 % and exactly scaled at 50 %.
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX // AudioPlayer.h brings in the WASAPI backend's Windows headers.
#endif
#define SDL_MAIN_HANDLED // and the SDL backend's SDL.h: keep this main.
#include "ship/audio/AudioOutputGain.h"
#include "ship/audio/AudioOutputPolicy.h"
#include "ship/audio/AudioPlayer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int gChecks = 0;

void Expect(bool condition, const std::string& what) {
    ++gChecks;
    if (!condition) throw std::runtime_error(what);
}

constexpr std::int32_t kSampleRate = 32728; // the 3DS DSP output rate

// A backend that keeps what it is given, so the test sees the bytes a real
// device would receive.
class CapturePlayer final : public Ship::AudioPlayer {
  public:
    CapturePlayer() : AudioPlayer(Settings()) {}
    int32_t Buffered() override { return 0; }
    std::vector<std::uint8_t> Bytes;
    int Flushes = 0;

  protected:
    bool DoInit() override { return true; }
    void DoClose() override {}
    void DoPlay(const std::uint8_t* buf, std::size_t len) override { Bytes.insert(Bytes.end(), buf, buf + len); }
    void DoFlush() override { ++Flushes; }

  private:
    static Ship::AudioSettings Settings() {
        Ship::AudioSettings settings;
        settings.SampleRate = kSampleRate;
        return settings;
    }
};

std::uint64_t Fnv1a64(const std::uint8_t* data, std::size_t size) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t Fnv1a64(const std::vector<std::int16_t>& samples) {
    return Fnv1a64(reinterpret_cast<const std::uint8_t*>(samples.data()), samples.size() * sizeof(std::int16_t));
}

// Ten seconds of deterministic stereo: a sine on the left, LCG noise plus a
// slower sine on the right, with full-scale peaks.
std::vector<std::int16_t> Signal(std::size_t frames) {
    std::vector<std::int16_t> samples(frames * 2U);
    std::uint32_t state = 0x1234567U;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        state = state * 1664525U + 1013904223U;
        const double t = static_cast<double>(frame) / kSampleRate;
        const double left = 32767.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * t);
        const double noise = static_cast<double>(static_cast<std::int32_t>(state >> 16U) - 32768);
        const double right = 0.5 * noise + 0.5 * 32767.0 * std::sin(2.0 * 3.14159265358979 * 97.0 * t);
        samples[frame * 2U] = static_cast<std::int16_t>(std::clamp(std::lround(left), -32768L, 32767L));
        samples[frame * 2U + 1U] = static_cast<std::int16_t>(std::clamp(std::lround(right), -32768L, 32767L));
    }
    // Both extremes appear.
    samples[2] = -32768;
    samples[3] = 32767;
    return samples;
}

// Submits in the irregular chunk sizes the window frame produces (whole DSP
// frames of 160 samples, a varying number per presented frame).
void PlayChunked(Ship::AudioPlayer& player, const std::vector<std::int16_t>& samples, std::size_t from = 0,
                 std::size_t to = SIZE_MAX) {
    to = std::min(to, samples.size() / 2U);
    static constexpr std::size_t kChunks[] = {480, 640, 160, 320, 480, 800, 160};
    std::size_t frame = from;
    for (std::size_t index = 0; frame < to; ++index) {
        const std::size_t count = std::min(kChunks[index % std::size(kChunks)], to - frame);
        player.Play(reinterpret_cast<const std::uint8_t*>(samples.data() + frame * 2U),
                    count * 2U * sizeof(std::int16_t));
        frame += count;
    }
}

std::vector<std::int16_t> AsSamples(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::int16_t> samples(bytes.size() / 2U);
    std::memcpy(samples.data(), bytes.data(), samples.size() * 2U);
    return samples;
}

struct MemoryPort final : Ship::AudioOutputPersistencePort {
    nlohmann::json Block;
    bool HasStore = true;
    bool Fail = false;
    int Stores = 0;
    bool LoadAudio(nlohmann::json& block) override {
        block = Block;
        return HasStore;
    }
    bool StoreAudio(const nlohmann::json& block, std::string* error) override {
        ++Stores;
        if (Fail) {
            if (error) *error = "disk full";
            return false;
        }
        Block = block;
        return true;
    }
};

void TestGainLaw() {
    Expect(Ship::AudioMasterVolumeGainQ16(100) == 65536U, "100 % is not unity");
    Expect(Ship::AudioMasterVolumeGainQ16(50) == 16384U, "50 % is not a quarter (square law)");
    Expect(Ship::AudioMasterVolumeGainQ16(0) == 0U, "0 % is not silent");
    Expect(Ship::AudioMasterVolumeGainQ16(150) == 65536U && Ship::AudioMasterVolumeGainQ16(-4) == 0U,
           "volume outside 0..100 not clamped");
    std::uint32_t previous = 0;
    for (int volume = 1; volume <= 100; ++volume) {
        const auto gain = Ship::AudioMasterVolumeGainQ16(volume);
        Expect(gain > previous, "gain law not increasing");
        previous = gain;
    }
    Expect(Ship::AudioOutputGain::Scale(-32768, 65536) == -32768 && Ship::AudioOutputGain::Scale(32767, 65536) == 32767,
           "unity scale changed a sample");
    Expect(Ship::AudioOutputGain::Scale(3, 16384) == 1 && Ship::AudioOutputGain::Scale(-3, 16384) == -1 &&
               Ship::AudioOutputGain::Scale(2, 16384) == 1 && Ship::AudioOutputGain::Scale(-32768, 16384) == -8192,
           "Q16 scale does not round to nearest");
}

void TestCodec() {
    using Ship::ParseAudioOutputSettings;
    Expect(ParseAudioOutputSettings(nlohmann::json()) == Ship::AudioOutputSettings{}, "absent block not default");
    Expect(ParseAudioOutputSettings({{"MasterVolume", 150}}).MasterVolume == 100, "150 not clamped");
    Expect(ParseAudioOutputSettings({{"MasterVolume", -5}}).MasterVolume == 0, "-5 not clamped");
    Expect(ParseAudioOutputSettings({{"MasterVolume", 49.6}}).MasterVolume == 50, "fraction not rounded");
    Expect(ParseAudioOutputSettings({{"MasterVolume", "loud"}}).MasterVolume == 100, "wrong type not default");
    Expect(!ParseAudioOutputSettings({{"Muted", "yes"}}).Muted, "wrong mute type accepted");
    const Ship::AudioOutputSettings settings{35, true, true};
    Expect(ParseAudioOutputSettings(Ship::SerializeAudioOutputSettings(settings)) == settings, "codec round trip");
    const auto block = Ship::SerializeAudioOutputSettings({120, false, false});
    Expect(block.at("MasterVolume") == 100, "serialized volume not clamped");
}

// The digest test (docs/TRIAEVUM_AUDIO_SETTINGS.md, Verification).
void TestOutputDigest() {
    const auto signal = Signal(static_cast<std::size_t>(kSampleRate) * 10U);
    const std::uint64_t inputDigest = Fnv1a64(signal);

    // 100 %: byte-identical, the stage never runs.
    {
        auto player = std::make_shared<CapturePlayer>();
        Expect(player->Init(), "capture player");
        auto port = std::make_shared<MemoryPort>();
        Ship::AudioOutputPolicy policy(port);
        policy.BindOutput(player);
        PlayChunked(*player, signal);
        Expect(player->Bytes.size() == signal.size() * 2U &&
                   std::memcmp(player->Bytes.data(), signal.data(), player->Bytes.size()) == 0,
               "100 %: output bytes differ from the guest's");
        Expect(Fnv1a64(player->Bytes.data(), player->Bytes.size()) == inputDigest &&
                   player->GetOutputPcmFnv1a64() == inputDigest,
               "100 %: output digest differs from the guest digest");
        Expect(player->GetOutputFramesScaled() == 0U, "100 %: the gain stage ran");
        std::printf("digest 100%%: input %016llx output %016llx\n", static_cast<unsigned long long>(inputDigest),
                    static_cast<unsigned long long>(player->GetOutputPcmFnv1a64()));
    }

    // 50 % from the start (bound before any audio: no ramp): every sample is
    // exactly (s * 16384 + 32768) >> 16.
    std::vector<std::int16_t> scaled(signal.size());
    for (std::size_t index = 0; index < signal.size(); ++index)
        scaled[index] = static_cast<std::int16_t>((static_cast<std::int64_t>(signal[index]) * 16384 + 32768) >> 16);
    {
        auto player = std::make_shared<CapturePlayer>();
        Expect(player->Init(), "capture player");
        auto port = std::make_shared<MemoryPort>();
        port->Block = {{"MasterVolume", 50}};
        Ship::AudioOutputPolicy policy(port);
        policy.BindOutput(player);
        Expect(player->GetOutputGainTarget() == 16384U && player->GetOutputGainCurrent() == 16384U,
               "binding did not snap the player to 50 %");
        PlayChunked(*player, signal);
        const auto output = AsSamples(player->Bytes);
        Expect(output == scaled, "50 %: samples are not the scaled reference");
        Expect(player->GetOutputPcmFnv1a64() == Fnv1a64(scaled) && player->GetOutputPcmFnv1a64() != inputDigest,
               "50 %: output digest is not the scaled reference's");
        Expect(player->GetOutputFramesScaled() == signal.size() / 2U, "50 %: not every frame scaled");
        std::printf("digest 50%%: input %016llx output %016llx (reference %016llx)\n",
                    static_cast<unsigned long long>(inputDigest),
                    static_cast<unsigned long long>(player->GetOutputPcmFnv1a64()),
                    static_cast<unsigned long long>(Fnv1a64(scaled)));
    }

    // A change while playing ramps over 20 ms, monotonically and without a
    // jump; after the ramp the output is exactly scaled; mute reaches exact
    // silence; back at 100 % the bytes are the guest's again and the stage
    // stops running.
    {
        auto player = std::make_shared<CapturePlayer>();
        Expect(player->Init(), "capture player");
        auto port = std::make_shared<MemoryPort>();
        Ship::AudioOutputPolicy policy(port);
        policy.BindOutput(player);
        std::vector<std::int16_t> dc(static_cast<std::size_t>(kSampleRate) * 2U, 20000);
        PlayChunked(*player, dc, 0, kSampleRate / 2);
        std::string error;
        Expect(policy.Apply({50, false, false}, &error), "apply 50 %");
        const std::size_t changeFrame = kSampleRate / 2;
        PlayChunked(*player, dc, changeFrame, kSampleRate);
        const auto output = AsSamples(player->Bytes);
        const std::size_t rampFrames = kSampleRate * Ship::AudioOutputGain::kRampMilliseconds / 1000U;
        Expect(output[(changeFrame - 1U) * 2U] == 20000, "gain changed before the change");
        const int maximumStep = static_cast<int>(std::ceil(20000.0 * (65536 - 16384) / 65536.0 / rampFrames)) + 1;
        int previous = 20000;
        for (std::size_t frame = changeFrame; frame < changeFrame + rampFrames; ++frame) {
            const int value = output[frame * 2U];
            Expect(value <= previous && previous - value <= maximumStep, "ramp is not a smooth descent");
            Expect(output[frame * 2U + 1U] == value, "channels ramp apart");
            previous = value;
        }
        Expect(output[changeFrame * 2U] < 20000, "ramp did not start at the change");
        Expect(output[(changeFrame + rampFrames - 1U) * 2U] == 5000, "ramp did not end at 50 %");
        for (std::size_t frame = changeFrame + rampFrames; frame < static_cast<std::size_t>(kSampleRate); ++frame)
            Expect(output[frame * 2U] == 5000, "output after the ramp not scaled");
        std::printf("ramp: %zu frames, largest step %d (limit %d)\n", rampFrames,
                    20000 - output[changeFrame * 2U], maximumStep);

        const auto signalSecond = Signal(static_cast<std::size_t>(kSampleRate));
        player->Bytes.clear();
        Expect(policy.Apply({50, true, false}, &error), "apply mute");
        PlayChunked(*player, signalSecond);
        const auto muted = AsSamples(player->Bytes);
        Expect(std::all_of(muted.begin() + static_cast<std::ptrdiff_t>(rampFrames * 2U), muted.end(),
                           [](std::int16_t sample) { return sample == 0; }),
               "mute did not reach silence after the ramp");

        player->Bytes.clear();
        Expect(policy.Apply({100, false, false}, &error), "apply 100 %");
        PlayChunked(*player, signalSecond);
        const auto restored = AsSamples(player->Bytes);
        Expect(std::equal(restored.begin() + static_cast<std::ptrdiff_t>(rampFrames * 2U), restored.end(),
                          signalSecond.begin() + static_cast<std::ptrdiff_t>(rampFrames * 2U)),
               "back at 100 %: output differs from the guest's");
        const auto scaledFrames = player->GetOutputFramesScaled();
        player->Bytes.clear();
        PlayChunked(*player, signalSecond);
        Expect(player->GetOutputFramesScaled() == scaledFrames && AsSamples(player->Bytes) == signalSecond,
               "back at 100 %: the stage is not inert again");
    }

    // Flush discards the queue: the gain snaps to the target.
    {
        auto player = std::make_shared<CapturePlayer>();
        Expect(player->Init(), "capture player");
        player->SetOutputGainTarget(16384U);
        player->Flush();
        Expect(player->GetOutputGainCurrent() == 16384U && player->Flushes == 1, "flush did not snap the gain");
    }
}

void TestOwner() {
    std::string error;
    // Loads the stored block; Apply saves it; Preview does not; Save does.
    auto port = std::make_shared<MemoryPort>();
    port->Block = {{"MasterVolume", 70}, {"Muted", false}};
    Ship::AudioOutputPolicy policy(port);
    Expect(policy.Persistent() && policy.SaveState() == Ship::AudioOutputSaveState::Saved, "persistent owner");
    Expect(policy.Settings().MasterVolume == 70, "stored volume not loaded");
    const auto revision = policy.Revision();
    Expect(policy.Apply({40, false, false}, &error) && port->Block.at("MasterVolume") == 40 && port->Stores == 1,
           "apply did not save");
    Expect(policy.Revision() == revision + 1U, "revision did not advance");
    policy.Preview({45, false, false});
    Expect(port->Stores == 1 && policy.Settings().MasterVolume == 45, "preview saved or did not apply");
    Expect(policy.Save(&error) && port->Block.at("MasterVolume") == 45, "save after preview");
    Expect(policy.Apply({250, false, false}, &error) && policy.Settings().MasterVolume == 100, "apply not clamped");

    // A failed save keeps the value for the session and reports it; retry.
    port->Fail = true;
    Expect(!policy.Apply({20, false, false}, &error) && error == "disk full", "failed save not reported");
    Expect(policy.SaveState() == Ship::AudioOutputSaveState::Failed && policy.LastError() == "disk full",
           "failed save state");
    Expect(policy.Settings().MasterVolume == 20, "failed save dropped the session value");
    port->Fail = false;
    Expect(policy.Save(&error) && policy.SaveState() == Ship::AudioOutputSaveState::Saved &&
               port->Block.at("MasterVolume") == 20,
           "retry did not save");

    // Without a store: session only, Apply succeeds.
    Ship::AudioOutputPolicy session(nullptr);
    Expect(!session.Persistent() && session.SaveState() == Ship::AudioOutputSaveState::SessionOnly, "session only");
    Expect(session.Apply({30, false, false}, &error) && session.Settings().MasterVolume == 30, "session apply");
    auto noStore = std::make_shared<MemoryPort>();
    noStore->HasStore = false;
    Ship::AudioOutputPolicy unstored(noStore);
    Expect(!unstored.Persistent() && unstored.Apply({30, false, false}, &error) && noStore->Stores == 0,
           "a port without a store was written");

    // The gain the output should reach; focus mute only when chosen.
    auto player = std::make_shared<CapturePlayer>();
    Expect(player->Init(), "capture player");
    Ship::AudioOutputPolicy owner(std::make_shared<MemoryPort>());
    owner.BindOutput(player);
    Expect(owner.OutputBound() && owner.TargetGainQ16() == 65536U, "default gain not unity");
    owner.SetWindowFocused(false);
    Expect(owner.TargetGainQ16() == 65536U && player->GetOutputGainTarget() == 65536U,
           "unfocused muted without the setting");
    Expect(owner.Apply({80, false, true}, &error), "apply focus mute");
    Expect(owner.TargetGainQ16() == 0U && player->GetOutputGainTarget() == 0U, "unfocused not muted");
    owner.SetWindowFocused(true);
    Expect(owner.TargetGainQ16() == Ship::AudioMasterVolumeGainQ16(80) &&
               player->GetOutputGainTarget() == Ship::AudioMasterVolumeGainQ16(80),
           "focus did not restore the volume");
    Expect(owner.Apply({80, true, true}, &error) && player->GetOutputGainTarget() == 0U, "mute not pushed");

    // A new output player (backend change) gets the gain at once.
    auto replacement = std::make_shared<CapturePlayer>();
    Expect(replacement->Init(), "replacement player");
    Expect(owner.Apply({50, false, false}, &error), "apply before rebinding");
    owner.BindOutput(replacement);
    Expect(replacement->GetOutputGainTarget() == 16384U && replacement->GetOutputGainCurrent() == 16384U,
           "rebinding did not carry the gain");

    // The preview request: taken once, never while muted or at 0 %.
    owner.RequestPreview();
    Expect(owner.TakePreviewRequest() && !owner.TakePreviewRequest(), "preview request not taken once");
    Expect(owner.Apply({50, true, false}, &error), "apply mute for preview");
    owner.RequestPreview();
    Expect(!owner.TakePreviewRequest(), "preview played while muted");
}

void TestPreviewTone() {
    const auto tone = Ship::GenerateAudioPreviewTone(kSampleRate);
    const std::size_t frames = kSampleRate * 120U / 1000U;
    Expect(tone.size() == frames * 2U, "preview tone length");
    int peak = 0;
    for (const auto sample : tone) peak = std::max(peak, std::abs(static_cast<int>(sample)));
    Expect(peak > 7000 && peak <= 8192, "preview tone level is not -12 dBFS");
    Expect(std::abs(tone.front()) < 50 && std::abs(tone.back()) < 300, "preview tone does not fade");
    Expect(Ship::GenerateAudioPreviewTone(0).empty(), "preview tone without a rate");
}

void TestPreviewToneQueueGuard() {
    const std::int32_t shortQueue = kSampleRate / 20;
    const std::int32_t toneFrames = kSampleRate * 120 / 1000;
    // Real time: only a short queue takes a tone, whatever came before.
    Expect(Ship::AudioPreviewToneMayQueue(0, kSampleRate, -1, false), "empty queue refused a tone");
    Expect(Ship::AudioPreviewToneMayQueue(shortQueue - 1, kSampleRate, -1, false), "short queue refused a tone");
    Expect(!Ship::AudioPreviewToneMayQueue(shortQueue, kSampleRate, -1, false), "long queue took a tone");
    Expect(!Ship::AudioPreviewToneMayQueue(shortQueue * 4, kSampleRate, shortQueue * 8, false),
           "real-time guard used the previous tone");
    // Fixed delta: a long backlog of game audio does not block the first tone
    // of a hold; later tones wait until the previous one has nearly played.
    const std::int32_t backlog = kSampleRate * 6;
    Expect(Ship::AudioPreviewToneMayQueue(backlog, kSampleRate, -1, true), "scripted first tone blocked");
    const std::int32_t mark = backlog + toneFrames;
    Expect(!Ship::AudioPreviewToneMayQueue(mark, kSampleRate, mark, true), "scripted tones piled up");
    Expect(!Ship::AudioPreviewToneMayQueue(mark - toneFrames + shortQueue, kSampleRate, mark, true),
           "scripted tone queued with 50 ms of the previous one left");
    Expect(Ship::AudioPreviewToneMayQueue(mark - toneFrames + shortQueue - 1, kSampleRate, mark, true),
           "scripted tone blocked after the previous one drained");
    Expect(Ship::AudioPreviewToneMayQueue(shortQueue - 1, kSampleRate, mark, true), "scripted short queue refused");
    // Invalid inputs never queue.
    Expect(!Ship::AudioPreviewToneMayQueue(0, 0, -1, true), "tone without a rate");
    Expect(!Ship::AudioPreviewToneMayQueue(-1, kSampleRate, -1, true), "tone with a failed queue query");
}

} // namespace

int main() try {
    TestGainLaw();
    TestCodec();
    TestOutputDigest();
    TestOwner();
    TestPreviewTone();
    TestPreviewToneQueueGuard();
    std::cout << "audio_output_policy_tests passed: " << gChecks << " checks\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "audio_output_policy_tests FAILED: " << error.what() << "\n";
    return 1;
}
