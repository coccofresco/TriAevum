// Resampler reference adapted from Citra/Azahar, GPLv2 or any later version.
#include "audio_core/codec.h"
#include "audio_core/hle/source.h"
#include "audio_core/interpolate.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <deque>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
using Sample = AudioCore::StereoBuffer16::value_type;
using Frame = AudioCore::StereoFrame16;
using State = AudioCore::AudioInterp::State;
constexpr u64 Scale = 1ULL << 24;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// The previous deque implementation, deliberately kept independent of the cursor.
void Reference(State& state, std::deque<Sample>& input, float rate, Frame& output,
               std::size_t& outputi, bool linear) {
    if (input.empty()) return;
    input.insert(input.begin(), {state.xn2, state.xn1});
    const u64 step = static_cast<u64>(rate * Scale);
    u64 position = state.fposition;
    std::size_t i = 0;
    while (outputi < output.size()) {
        i = static_cast<std::size_t>(position / Scale);
        if (i + 2 >= input.size()) { i = input.size() - 2; break; }
        Sample value = input[i];
        if (linear) {
            const u64 fraction = position & (Scale - 1);
            for (unsigned channel = 0; channel < 2; ++channel) {
                const s64 delta = std::clamp<s64>(input[i + 1][channel] - value[channel], -32768, 32767);
                value[channel] = static_cast<s16>(value[channel] + fraction * delta / Scale);
            }
        }
        output[outputi++] = value;
        position += step;
    }
    state.xn2 = input[i];
    state.xn1 = input[i + 1];
    state.fposition = position - i * Scale;
    input.erase(input.begin(), input.begin() + i + 2);
}

void QueueTest() {
    AudioCore::DecodedSampleQueue queue(AudioCore::StereoBuffer16{{1, 2}, {3, 4}, {5, 6}});
    const auto* storage = queue.Remaining().data();
    queue.Consume(1);
    Require(queue.Remaining().data() == storage + 1, "consumption moved storage");
    auto copy = queue;
    Require(copy.size() == 2 && copy[0] == Sample{3, 4}, "copy lost unread range");
    auto moved = std::move(queue);
    Require(queue.empty() && moved.CopyRemaining() == copy.CopyRemaining(), "invalid moved cursor");
    queue = std::move(moved);
    Require(moved.empty() && queue.size() == 2, "invalid move assignment");
    queue.Consume(2);
    Require(queue.empty(), "end of buffer not empty");
    bool rejected = false;
    try { queue.Consume(1); } catch (const std::out_of_range&) { rejected = true; }
    Require(rejected, "cursor accepted overrun");
    queue = copy;
    copy.clear();
    Require(copy.empty() && queue.size() == 2, "copy is not independent");
}

std::size_t ResamplerTests() {
    std::mt19937 random(0xA0D10);
    const float rates[] = {0.03125f, 0.125f, 0.4999f, 1.0f, 1.001f, 1.5f, 2.0f, 4.0f, 15.75f};
    std::size_t calls = 0;
    for (bool linear : {false, true}) {
        for (std::size_t length : {0, 1, 2, 3, 13, 14, 15, 159, 160, 161, 1024, 4097}) {
            for (std::size_t offset : {0, 159, 160}) {
                for (float initialRate : rates) {
                    AudioCore::StereoBuffer16 samples(length);
                    for (auto& sample : samples) sample = {static_cast<s16>(random()), static_cast<s16>(random())};
                    AudioCore::DecodedSampleQueue actual(samples);
                    std::deque<Sample> expected(samples.begin(), samples.end());
                    State a{{-32768, 32767}, {32767, -32768}, random() % (Scale * 5)};
                    State b = a;
                    for (unsigned pass = 0; pass < 12; ++pass) {
                        Frame outA{}, outB{};
                        outA.fill({-1234, 5678});
                        outB = outA;
                        auto i = pass == 0 ? offset : 0;
                        auto j = i;
                        float rate = pass == 0 ? initialRate : rates[random() % std::size(rates)];
                        Reference(b, expected, rate, outB, j, linear);
                        (linear ? AudioCore::AudioInterp::Linear : AudioCore::AudioInterp::None)(a, actual, rate, outA, i);
                        Require(i == j && outA == outB, "resampler samples differ from deque");
                        Require(a.xn1 == b.xn1 && a.xn2 == b.xn2 && a.fposition == b.fposition, "resampler history differs");
                        Require(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()), "unread samples differ");
                        ++calls;
                        if (pass == 5) actual = actual.CopyRemaining(); // Save/restore cursor normalization.
                        if (actual.empty()) {
                            actual = samples;
                            expected.assign(samples.begin(), samples.end());
                        }
                    }
                }
            }
        }
    }
    return calls;
}

void CodecTests() {
    const std::array<u8, 4> pcm8{0, 127, 128, 255};
    auto decoded = AudioCore::Codec::DecodePCM8(2, pcm8.data(), 2);
    Require(decoded == AudioCore::StereoBuffer16{{0, 32512}, {-32768, -256}}, "PCM8 stereo changed");
    decoded = AudioCore::Codec::DecodePCM8(1, pcm8.data(), 4);
    Require(decoded[2] == Sample{-32768, -32768}, "PCM8 mono changed");
    const std::array<s16, 4> pcm16{-32768, -1, 0, 32767};
    decoded = AudioCore::Codec::DecodePCM16(2, reinterpret_cast<const u8*>(pcm16.data()), 2);
    Require(decoded == AudioCore::StereoBuffer16{{-32768, -1}, {0, 32767}}, "PCM16 stereo changed");
    decoded = AudioCore::Codec::DecodePCM16(1, reinterpret_cast<const u8*>(pcm16.data()), 4);
    Require(decoded[3] == Sample{32767, 32767}, "PCM16 mono changed");
    const std::array<u8, 16> adpcm{0, 0x12, 0x34, 0x56, 0x70, 0x89, 0xAB, 0xCD,
                                  0, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};
    const std::array<s16, 16> coefficients{};
    for (std::size_t count : {0, 1, 2, 13, 14, 15, 28}) {
        AudioCore::Codec::ADPCMState state{321, -123};
        decoded = AudioCore::Codec::DecodeADPCM(adpcm.data(), count, coefficients, state);
        Require(decoded.size() == ((count + 1) & ~std::size_t{1}), "ADPCM odd-length contract changed");
        for (std::size_t i = 0; i < decoded.size(); ++i) {
            const auto byte = adpcm[(i / 14) * 8 + 1 + (i % 14) / 2];
            const int nibble = i % 2 ? byte & 15 : byte >> 4;
            const s16 value = static_cast<s16>(nibble < 8 ? nibble : nibble - 16);
            Require(decoded[i] == Sample{value, value}, "ADPCM sample changed");
        }
        if (!decoded.empty()) Require(state.yn1 == decoded.back()[0] && state.yn2 == decoded[decoded.size() - 2][0], "ADPCM history changed");
    }
}

void SourceStateTest() {
    using namespace AudioCore::HLE;
    Source original(0), restored(0);
    auto snapshot = original.CaptureState();
    Require(snapshot["backup_frame"].get<Frame>() == Frame{}, "initial backup frame is not initialized");
    AudioCore::StereoBuffer16 samples(8192);
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = {static_cast<s16>(i * 53), static_cast<s16>(i * 79)};
    snapshot["state"]["current_buffer"] = samples;
    snapshot["state"]["enabled"] = true;
    snapshot["state"]["rate_multiplier"] = 1.25f;
    snapshot["state"]["interpolation_mode"] = static_cast<u32>(SourceConfiguration::Configuration::InterpolationMode::Linear);
    std::string error;
    Require(original.RestoreState(snapshot, &error), "legacy source state rejected");
    Require(original.CaptureState() == snapshot, "source state schema changed");
    SourceConfiguration::Configuration config{};
    s16_le coefficients[16]{};
    State referenceState{};
    std::deque<Sample> reference(samples.begin(), samples.end());
    for (unsigned tick = 0; tick < 20; ++tick) {
        Frame expected{};
        std::size_t position = 0;
        Reference(referenceState, reference, 1.25f, expected, position, true);
        original.Tick(config, coefficients);
        const auto current = original.CaptureState();
        Require(current["current_frame"].get<Frame>() == expected, "source output differs from deque");
        Require(current["state"]["current_buffer"].get<std::deque<Sample>>() == reference, "source serialized consumed samples");
        Require(restored.RestoreState(nlohmann::json::from_msgpack(nlohmann::json::to_msgpack(current)), &error), "source round trip rejected");
        Require(restored.CaptureState() == current, "source round trip differs");
        original.Sleep();
        original.Wakeup();
        Require(original.CaptureState() == current, "sleep/wakeup changed unread cursor");
        if (tick < 19) {
            restored.Tick(config, coefficients);
            auto following = original.CaptureState();
            original.Tick(config, coefficients);
            Require(restored.CaptureState() == original.CaptureState(), "restored source playback differs");
            Require(original.RestoreState(following, &error), "source restore failed");
        }
    }
}

nlohmann::json ReadDspSnapshot(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(file.good(), "cannot open gameplay checkpoint");
    const auto size = file.tellg();
    Require(size >= 40 && size <= (1LL << 30), "invalid checkpoint size");
    std::vector<u8> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    Require(file.good(), "cannot read gameplay checkpoint");
    const auto read = [&](unsigned offset, unsigned count) {
        u64 value = 0;
        for (unsigned i = 0; i < count; ++i) value |= u64{bytes[offset + i]} << (8 * i);
        return value;
    };
    const std::array<u8, 8> magic{'O', 'O', 'T', '3', 'D', 'S', 'V', 0};
    Require(std::equal(magic.begin(), magic.end(), bytes.begin()) && read(8, 4) == 1 &&
            read(12, 4) == 40 && read(16, 8) == bytes.size() - 40, "unsupported checkpoint header");
    u64 hash = 14695981039346656037ULL;
    for (std::size_t i = 40; i < bytes.size(); ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
    Require(hash == read(24, 8), "checkpoint payload hash differs");
    auto document = nlohmann::json::from_msgpack(bytes.begin() + 40, bytes.end());
    return std::move(document.at("dsp_hle"));
}
}

int main(int argc, char** argv) {
    try {
        Require(argc == 1 || argc == 3, "usage: audio_storage_tests [before.oot3dsav after.oot3dsav]");
        QueueTest();
        CodecTests();
        const auto calls = ResamplerTests();
        SourceStateTest();
        std::cout << "audio storage: " << calls << " differential resampler calls, codecs, source playback/save/restore/sleep: PASS\n";
        if (argc == 3) {
            const auto before = ReadDspSnapshot(argv[1]);
            const auto after = ReadDspSnapshot(argv[2]);
            Require(before == after, "real gameplay DSP states differ");
            std::cout << "Real gameplay: all " << before.at("sources").size()
                      << " DSP sources, unread samples, histories and mixers identical: PASS\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
