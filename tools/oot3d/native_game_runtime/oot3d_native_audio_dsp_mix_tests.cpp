// DSP HLE mix (NativeA32DspHle::ProcessFrame: 24 sources, mixers, aux, final samples): bit
// identity against the pre-optimization code and cost.
//
//   oot3d_native_audio_dsp_mix_tests            random titles: digest of everything the guest
//                                               observes (both shared-memory regions after every
//                                               frame), the PCM handed to the host and the
//                                               savestates (msgpack bytes), with save/restore
//                                               round trips in the middle of playback
//   oot3d_native_audio_dsp_mix_tests --bench    also time a realistic OoT3D-like frame
//   oot3d_native_audio_dsp_mix_tests --digest   only print the digests
//   oot3d_native_audio_dsp_mix_tests --bench-only
//
// kExpected below holds the digests of the original code (topscreen-3.0) for the host build
// (x86-64, -O3 -ffast-math) and for the Switch flags (devkitA64 -O3 -ffast-math -mtune=cortex-a57,
// run under qemu-aarch64 by run_audio_dsp_mix_tests_qemu.sh). The same file built against the old
// audio_core prints the reference values.
#include "oot3d_native_a32_dsp_hle.h"

#include "audio_core/hle/shared_memory.h"
#include "audio_core/interpolate.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <ctime>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace {
using namespace AudioCore;
using namespace AudioCore::HLE;
using Configuration = SourceConfiguration::Configuration;
using Oot3dNativeGame::NativeA32DspHle;
using Oot3dNativeGame::NativeDspMemoryAccess;

bool gFailed = false;
void Check(bool condition, const char* message) {
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        gFailed = true;
    }
}

constexpr u32 kRegion0 = 0x1FF50000U;
constexpr u32 kRegion1 = 0x1FF70000U;
constexpr u32 kDspBase = 0x1FF40000U;
constexpr std::size_t kDspBytes = 0x40000U;
constexpr u32 kSampleBase = 0x20000000U;
constexpr std::size_t kSampleBytes = 1U << 20U;

struct Digest {
    u64 value = 0x9E3779B97F4A7C15ULL;
    void Word(u64 data) {
        value ^= data;
        value *= 0xFF51AFD7ED558CCDULL;
        value ^= value >> 29U;
    }
    void Bytes(const u8* data, std::size_t size) {
        std::size_t i = 0;
        for (; i + 8 <= size; i += 8) {
            u64 word;
            std::memcpy(&word, data + i, 8);
            Word(word);
        }
        u64 tail = size;
        for (; i < size; ++i) tail = (tail << 8U) | data[i];
        Word(tail);
    }
};

// Guest memory: the DSP shared regions and the sample area the sources point at.
struct FakeGuest {
    std::vector<u8> dsp = std::vector<u8>(kDspBytes);
    std::vector<u8> samples = std::vector<u8>(kSampleBytes);
    NativeDspMemoryAccess access;

    FakeGuest() {
        access.Read = [this](u32 address, std::span<u8> out) {
            if (address < kDspBase || address - kDspBase + out.size() > dsp.size()) return false;
            std::memcpy(out.data(), dsp.data() + (address - kDspBase), out.size());
            return true;
        };
        access.Write = [this](u32 address, std::span<const u8> in) {
            if (address < kDspBase || address - kDspBase + in.size() > dsp.size()) return false;
            std::memcpy(dsp.data() + (address - kDspBase), in.data(), in.size());
            return true;
        };
        access.ResolvePhysical = [this](u32 address, std::size_t size) -> const u8* {
            if (address < kSampleBase || address - kSampleBase + size > samples.size()) return nullptr;
            return samples.data() + (address - kSampleBase);
        };
    }
    SharedMemory& Region(unsigned index) {
        return *reinterpret_cast<SharedMemory*>(dsp.data() + ((index ? kRegion1 : kRegion0) - kDspBase));
    }
};

// Typical DSPADPCM predictor pairs (Q11).
constexpr s16 kAdpcmCoefficients[16] = {0,    0,     2048, 0,     0,    2048,  1024, 1024,
                                        3900, -1950, 3584, -1536, 2048, -1024, 3000, -1200};

// GC-ADPCM encoder (best predictor and scale per 14-sample frame, decoder-exact feedback): the
// encoded audio rarely saturates, as real title data, unlike random bytes.
void EncodeAdpcm(const std::vector<s16>& pcm, u8* output) {
    int yn1 = 0, yn2 = 0;
    for (std::size_t frame = 0; frame * 14 < pcm.size(); ++frame) {
        u64 bestError = ~0ULL;
        u8 best[8] = {};
        int bestY1 = 0, bestY2 = 0;
        for (int idx = 0; idx < 8; ++idx) {
            for (int shift = 0; shift <= 12; ++shift) {
                u8 bytes[8] = {static_cast<u8>((idx << 4) | shift)};
                int y1 = yn1, y2 = yn2;
                u64 error = 0;
                for (int i = 0; i < 14; ++i) {
                    const std::size_t at = frame * 14 + i;
                    const int target = at < pcm.size() ? pcm[at] : 0;
                    const int predicted =
                        kAdpcmCoefficients[idx * 2] * y1 + kAdpcmCoefficients[idx * 2 + 1] * y2;
                    const int wanted = ((target << 11) - 0x400 - predicted) >> 11;
                    const int nibble = std::clamp((wanted + ((1 << shift) >> 1)) >> shift, -8, 7);
                    int value = ((nibble * (1 << shift)) * 2048 + 0x400 + predicted) >> 11;
                    value = std::clamp(value, -32768, 32767);
                    error += static_cast<u64>(static_cast<s64>(value - target) * (value - target));
                    y2 = y1;
                    y1 = value;
                    bytes[1 + i / 2] |= static_cast<u8>((nibble & 0xF) << (i % 2 ? 0 : 4));
                }
                if (error < bestError) {
                    bestError = error;
                    std::memcpy(best, bytes, 8);
                    bestY1 = y1;
                    bestY2 = y2;
                }
            }
        }
        std::memcpy(output + frame * 8, best, 8);
        yn1 = bestY1;
        yn2 = bestY2;
    }
}

// Sample memory: [0, 1/4) smooth PCM16, [1/2, 3/4) ADPCM encoding a mix of tones and noise with
// kAdpcmCoefficients, everything else random bytes (saturating ADPCM, full-scale PCM).
// Parabolic sine of a 16-bit phase, amplitude 16384, integer only: the test data (hence the
// digests) does not depend on the host's libm.
s32 Wave(u32 phase) {
    const s32 x = static_cast<s32>(phase & 0xFFFFU) - 32768;
    return static_cast<s32>((static_cast<s64>(x) * (32768 - (x < 0 ? -x : x))) >> 14);
}

void FillSamples(std::vector<u8>& memory, std::mt19937& random) {
    for (auto& byte : memory) byte = static_cast<u8>(random());
    for (std::size_t i = 0; i < memory.size() / 4; i += 2) {
        const u32 n = static_cast<u32>(i);
        const s16 value = static_cast<s16>(Wave(n * 115U) * (Wave(n * 9U + 16384U) + 16384) / 23000);
        std::memcpy(memory.data() + i, &value, 2);
    }
    // 64 KiB of frames, repeated over the quarter.
    constexpr std::size_t kEncodedBytes = 64U << 10U;
    std::vector<s16> pcm(kEncodedBytes / 8 * 14);
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        const u32 n = static_cast<u32>(i);
        const s32 tone = Wave(n * 323U) * 9 / 16 + Wave(n * 1773U + 10430U) * 5 / 16 +
                         Wave(n * 22U) * Wave(n * 9388U) / 87381;
        pcm[i] = static_cast<s16>(tone + static_cast<s32>(random() % 2001) - 1000);
    }
    u8* const encoded = memory.data() + memory.size() / 2;
    EncodeAdpcm(pcm, encoded);
    for (std::size_t offset = kEncodedBytes; offset < memory.size() / 4; offset += kEncodedBytes)
        std::memcpy(encoded + offset, encoded, kEncodedBytes);
}

float RandomGain(std::mt19937& random) {
    switch (random() % 16) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return 1.0f;
    case 3: return static_cast<float>(static_cast<int>(random() % 2000000) - 1000000); // saturates
    case 4: return 1e-41f;                                                             // denormal
    case 5: return -static_cast<float>(random() % 1000) / 250.0f;
    case 6: return 2.0f + static_cast<float>(random() % 1000) / 100.0f;
    default: return static_cast<float>(random() % 100000) / 100000.0f;
    }
}

// Buffers handed to the DSP lately: the guest's streaming rewrites aim at them.
std::vector<std::pair<u32, u32>> gRecentBuffers;

void RandomBuffer(std::mt19937& random, u32& address, u32& length) {
    switch (random() % 3) {
    case 0: address = kSampleBase + static_cast<u32>(random() % (kSampleBytes / 4)); break;
    case 1: // Encoded ADPCM frames.
        address = kSampleBase + static_cast<u32>(kSampleBytes / 2 + random() % (kSampleBytes / 4) / 8 * 8);
        break;
    default: address = kSampleBase + static_cast<u32>(random() % (kSampleBytes * 3 / 4)); break;
    }
    const unsigned kind = random() % 5;
    length = kind == 0 ? random() % 4 : kind == 1 ? random() % 200 : kind == 2 ? random() % 30000 + 1
                                                                               : random() % 4000 + 1;
    if (gRecentBuffers.size() == 16) gRecentBuffers.erase(gRecentBuffers.begin());
    gRecentBuffers.emplace_back(address - kSampleBase, length);
}

// One random guest frame of configuration written into `region`.
void RandomGuestFrame(std::mt19937& random, SharedMemory& region, u16& nextId) {
    const float rates[] = {0.0625f, 0.25f, 0.5f, 0.7213f, 1.0f, 1.0001f, 1.3333f, 2.0f, 2.97f, 4.5f, 9.0f,
                           1e-8f /* step 0 */};
    // Absurd rates (a whole looping buffer decoded per output): rare, they are slow.
    const float absurd[] = {70000.0f, 65537.0f};
    for (unsigned index = 0; index < num_sources; ++index) {
        Configuration& config = region.source_configurations.config[index];
        if (random() % 3 != 0) continue;
        const unsigned action = random() % 100;
        if (action < 3) config.reset_flag.Assign(1);
        else if (action < 5) config.partial_reset_flag.Assign(1);
        if (random() % 6 == 0) {
            config.enable = random() % 5 != 0;
            config.enable_dirty.Assign(1);
        }
        if (random() % 6 == 0) {
            config.rate_multiplier = random() % 4 ? rates[random() % std::size(rates)]
                                                  : 0.05f + static_cast<float>(random() % 4000) / 1000.0f;
            if (random() % 3000 == 0) config.rate_multiplier = absurd[random() % 2];
            config.rate_multiplier_dirty.Assign(1);
        }
        if (random() % 10 == 0) {
            config.interpolation_mode = static_cast<Configuration::InterpolationMode>(random() % 3);
            config.interpolation_dirty.Assign(1);
        }
        if (random() % 12 == 0) {
            const auto format = static_cast<Configuration::Format>(random() % 3);
            config.format.Assign(format);
            config.mono_or_stereo.Assign(format == Configuration::Format::ADPCM || random() % 2
                                             ? Configuration::MonoOrStereo::Mono
                                             : Configuration::MonoOrStereo::Stereo);
            config.format_dirty.Assign(1);
            config.mono_or_stereo_dirty.Assign(1);
        }
        if (random() % 15 == 0) {
            const unsigned kind = random() % 3;
            for (unsigned i = 0; i < 16; ++i) {
                region.adpcm_coefficients.coeff[index][i] =
                    kind == 0   ? kAdpcmCoefficients[i]
                    : kind == 1 ? static_cast<s16>(static_cast<int>(random() % 8192) - 4096)
                                : static_cast<s16>(random()); // Full range: int overflow paths.
            }
            config.adpcm_coefficients_dirty.Assign(1);
        }
        for (unsigned mix = 0; mix < 3; ++mix) {
            if (random() % 7 != 0) continue;
            for (auto& gain : config.gain[mix]) gain = RandomGain(random);
            if (mix == 0) config.gain_0_dirty.Assign(1);
            if (mix == 1) config.gain_1_dirty.Assign(1);
            if (mix == 2) config.gain_2_dirty.Assign(1);
        }
        if (random() % 12 == 0) {
            config.simple_filter_enabled.Assign(random() % 2);
            config.biquad_filter_enabled.Assign(random() % 2);
            config.filters_enabled_dirty.Assign(1);
        }
        if (random() % 10 == 0) {
            config.simple_filter.b0 = static_cast<s16>(random());
            config.simple_filter.a1 = static_cast<s16>(random() % 2 ? random() : random() % 30000);
            config.simple_filter_dirty.Assign(1);
        }
        if (random() % 10 == 0) {
            config.biquad_filter.a1 = static_cast<s16>(random());
            config.biquad_filter.a2 = static_cast<s16>(random());
            config.biquad_filter.b0 = static_cast<s16>(random());
            config.biquad_filter.b1 = static_cast<s16>(random());
            config.biquad_filter.b2 = static_cast<s16>(random());
            config.biquad_filter_dirty.Assign(1);
        }
        if (random() % 8 == 0) {
            u32 address, length;
            RandomBuffer(random, address, length);
            config.physical_address = address;
            config.length = length;
            config.buffer_id = nextId++;
            // A zero-length looping buffer spins the original DSP loop forever: never generated.
            config.is_looping.Assign(length != 0 && random() % 3 == 0);
            config.adpcm_dirty.Assign(random() % 2);
            config.adpcm_ps = static_cast<u16>(random() & 0x7F);
            config.adpcm_yn[0] = static_cast<u16>(random());
            config.adpcm_yn[1] = static_cast<u16>(random());
            config.embedded_buffer_dirty.Assign(1);
            if (random() % 2) {
                config.play_position = length ? random() % length : 0;
                config.play_position_dirty.Assign(1);
            }
        }
        if (!config.embedded_buffer_dirty && random() % 25 == 0) {
            // Partial update of the embedded buffer (PCM16 is the only decoded format).
            config.length = random() % 5000;
            config.partial_embedded_buffer_dirty.Assign(1);
        }
        if (random() % 4 == 0) {
            config.buffers_dirty = 0;
            for (unsigned i = 0; i < 4; ++i) {
                if (random() % 3) continue;
                auto& buffer = config.buffers[i];
                u32 address, length;
                RandomBuffer(random, address, length);
                buffer.physical_address = address;
                buffer.length = length;
                buffer.buffer_id = nextId++;
                buffer.is_looping = length != 0 && random() % 5 == 0;
                buffer.adpcm_dirty = random() % 2;
                buffer.adpcm_ps = static_cast<u16>(random() & 0x7F);
                buffer.adpcm_yn[0] = static_cast<u16>(random());
                buffer.adpcm_yn[1] = static_cast<u16>(random());
                config.buffers_dirty = config.buffers_dirty | (1U << i);
            }
            if (config.buffers_dirty) config.buffer_queue_dirty.Assign(1);
        }
        if (random() % 25 == 0) {
            config.sync_count = static_cast<u16>(random());
            config.sync_count_dirty.Assign(1);
        }
    }

    DspConfiguration& dsp = region.dsp_configuration;
    if (random() % 10 == 0) {
        dsp.master_volume = random() % 3 ? 1.0f : RandomGain(random);
        dsp.master_volume_dirty.Assign(1);
    }
    for (unsigned aux = 0; aux < 2; ++aux) {
        if (random() % 12 == 0) {
            dsp.aux_return_volume[aux] = RandomGain(random);
            if (aux == 0) dsp.aux_return_volume_0_dirty.Assign(1);
            else dsp.aux_return_volume_1_dirty.Assign(1);
        }
        if (random() % 15 == 0) {
            dsp.aux_bus_enable[aux] = random() % 3 != 0;
            if (aux == 0) dsp.aux_bus_enable_0_dirty.Assign(1);
            else dsp.aux_bus_enable_1_dirty.Assign(1);
        }
    }
    if (random() % 40 == 0) {
        dsp.output_format = static_cast<DspConfiguration::OutputFormat>(random() % 3);
        dsp.output_format_dirty.Assign(1);
    }
    if (random() % 50 == 0) dsp.headphones_connected_dirty.Assign(1);
}

// The guest's aux effect: it processes in place what the DSP sent last frame (now in the region
// the guest owns) and that is read back as the aux return.
void GuestAuxEffect(std::mt19937& random, SharedMemory& region) {
    auto process = [&](IntermediateMixSamples::Samples& samples) {
        const unsigned mode = random() % 4;
        for (unsigned channel = 0; channel < 4; ++channel) {
            for (unsigned sample = 0; sample < samples_per_frame; ++sample) {
                const s32 value = samples.pcm32[channel][sample];
                s32 result = mode == 0   ? value / 2
                             : mode == 1 ? static_cast<s32>(random())
                             : mode == 2 ? value
                                         : value - (value >> 3);
                samples.pcm32[channel][sample] = result;
            }
        }
    };
    process(region.intermediate_mix_samples.mix1);
    process(region.intermediate_mix_samples.mix2);
}

u64 RunTitle(u32 seed, unsigned frames, std::size_t* restoreCount) {
    std::mt19937 random(seed);
    gRecentBuffers.clear();
    FakeGuest guest;
    FillSamples(guest.samples, random);
    // Region bytes the DSP never interprets keep random content: they are copied back verbatim.
    for (auto& byte : guest.dsp) byte = static_cast<u8>(random());
    for (unsigned index = 0; index < 2; ++index) {
        SharedMemory& region = guest.Region(index);
        std::memset(&region.source_configurations, 0, sizeof(region.source_configurations));
        std::memset(&region.dsp_configuration, 0, sizeof(region.dsp_configuration));
        for (auto& config : region.source_configurations.config) {
            config.format.Assign(Configuration::Format::PCM16); // Titles never leave these zero.
            config.mono_or_stereo.Assign(Configuration::MonoOrStereo::Mono);
        }
        region.frame_counter = 0;
    }

    auto hle = std::make_unique<NativeA32DspHle>(
        std::vector<Oot3dNativeGame::NativeA32DspPhysicalRegion>{{kSampleBase, 0, kSampleBytes}});
    Digest digest;
    u16 nextId = 1;
    std::vector<int16_t> pcm;
    for (unsigned frame = 0; frame < frames; ++frame) {
        const unsigned guestRegion = frame & 1U;
        SharedMemory& region = guest.Region(guestRegion);
        region.frame_counter = static_cast<u16>(frame + 1);
        RandomGuestFrame(random, region, nextId);
        GuestAuxEffect(random, region);
        if (random() % 2 == 0) {
            // The title rewrites sample data (streaming): mostly in the encoded ADPCM area that
            // looping buffers point at, so decodes the DSP made earlier go stale.
            std::size_t at = random() % 2 ? kSampleBytes / 2 + random() % (kSampleBytes / 4)
                                          : random() % kSampleBytes;
            if (!gRecentBuffers.empty() && random() % 2) {
                const auto& recent = gRecentBuffers[random() % gRecentBuffers.size()];
                at = std::min<std::size_t>(kSampleBytes - 1,
                                           recent.first + random() % (recent.second / 2 + 1));
            }
            const std::size_t count = std::min<std::size_t>(1 + random() % 64, kSampleBytes - at);
            for (std::size_t i = 0; i < count; ++i) guest.samples[at + i] = static_cast<u8>(random());
        }

        pcm.clear();
        std::string error;
        if (!hle->ProcessFrame(guest.access, pcm, &error)) {
            Check(false, "ProcessFrame failed");
            return 0;
        }
        Check(pcm.size() == samples_per_frame * 2, "PCM size");
        digest.Bytes(reinterpret_cast<const u8*>(pcm.data()), pcm.size() * 2);
        for (unsigned index = 0; index < 2; ++index) {
            // DspStatus's padding words are copied from an uninitialized local by the original
            // code (stack garbage): every other byte of both regions is compared.
            SharedMemory& shared = guest.Region(index);
            const auto* bytes = reinterpret_cast<const u8*>(&shared);
            const std::size_t status = offsetof(SharedMemory, dsp_status);
            digest.Bytes(bytes, status + 4);
            digest.Bytes(bytes + status + sizeof(DspStatus),
                         sizeof(SharedMemory) - status - sizeof(DspStatus));
        }

        if (random() % 151 == 0) {
            // Savestate round trip: the snapshot bytes are part of the digest.
            const auto snapshot = hle->CaptureState();
            const auto packed = nlohmann::json::to_msgpack(snapshot);
            digest.Bytes(packed.data(), packed.size());
            auto restored = std::make_unique<NativeA32DspHle>(
                std::vector<Oot3dNativeGame::NativeA32DspPhysicalRegion>{{kSampleBase, 0, kSampleBytes}});
            Check(restored->RestoreState(nlohmann::json::from_msgpack(packed), &error), "restore");
            Check(restored->CaptureState() == snapshot, "save/restore round trip differs");
            hle = std::move(restored);
            if (restoreCount) ++*restoreCount;
        }
    }
    const auto packed = nlohmann::json::to_msgpack(hle->CaptureState());
    digest.Bytes(packed.data(), packed.size());
    return digest.value;
}

struct Expected {
    u32 seed;
    unsigned frames;
    u64 host;     // x86-64 -O3 -ffast-math
    u64 aarch64;  // devkitA64 Switch flags
    u64 clang_x64; // TriAevum's clang-cl x64 release build
};
// Digests of the original (topscreen-3.0) audio_core.
constexpr Expected kExpected[] = {
    {1, 3000, 0x28c9af4757e0cfbaULL, 0x2345c25acc63f226ULL, 0x654b2c44386cfa2bULL},
    {2, 3000, 0xafcc298ab74507ecULL, 0xb13cfd38f053c6b5ULL, 0xbd48904f2669da3bULL},
    {3, 3000, 0x7929e92e847526e8ULL, 0x37e4486dc2abd075ULL, 0xad7b461a32b0d1a9ULL},
    {4, 3000, 0x96a962a8371af89eULL, 0x2a1f4750591be2adULL, 0xa82ddf0d16d1cd01ULL},
};

// ----- cost --------------------------------------------------------------------------------------

#if !defined(__SWITCH__) && !defined(OOT3D_QEMU_AARCH64_TEST)
double ThreadSeconds() {
#if defined(_WIN32)
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
#else
    timespec time{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time);
    return static_cast<double>(time.tv_sec) + time.tv_nsec * 1e-9;
#endif
}

// An OoT3D-like frame: sequenced music (looping ADPCM instrument samples at varied pitch), a
// stereo PCM16 stream, sound effects retriggered from time to time, a few filtered voices, the
// reverb send on aux 0 and the guest's aux return.
void Benchmark() {
    std::mt19937 random(11);
    FakeGuest guest;
    FillSamples(guest.samples, random);
    std::memset(guest.dsp.data(), 0, guest.dsp.size());
    auto setup = [&](SharedMemory& region) {
        for (unsigned voice = 0; voice < num_sources; ++voice) {
            Configuration& config = region.source_configurations.config[voice];
            const bool active = voice < 18;
            const bool stream = voice == 0;
            const bool sfx = voice >= 13;
            config.enable = active;
            config.enable_dirty.Assign(1);
            config.rate_multiplier = stream ? 1.0f : 0.45f + 0.083f * voice;
            config.rate_multiplier_dirty.Assign(1);
            config.interpolation_mode = Configuration::InterpolationMode::Polyphase;
            config.interpolation_dirty.Assign(1);
            config.format.Assign(stream ? Configuration::Format::PCM16 : Configuration::Format::ADPCM);
            config.mono_or_stereo.Assign(stream ? Configuration::MonoOrStereo::Stereo
                                                : Configuration::MonoOrStereo::Mono);
            config.format_dirty.Assign(1);
            config.mono_or_stereo_dirty.Assign(1);
            for (unsigned i = 0; i < 16; ++i)
                region.adpcm_coefficients.coeff[voice][i] = kAdpcmCoefficients[i];
            config.adpcm_coefficients_dirty.Assign(1);
            for (unsigned c = 0; c < 4; ++c) {
                config.gain[0][c] = 0.35f + 0.01f * voice;
                config.gain[1][c] = voice % 3 == 0 ? 0.2f : 0.0f;
                config.gain[2][c] = 0.0f;
            }
            config.gain_0_dirty.Assign(1);
            config.gain_1_dirty.Assign(1);
            config.gain_2_dirty.Assign(1);
            if (voice % 5 == 1) {
                config.simple_filter_enabled.Assign(1);
                config.biquad_filter_enabled.Assign(voice % 10 == 1);
                config.simple_filter.b0 = 20000;
                config.simple_filter.a1 = 12000;
                config.biquad_filter.b0 = 9000;
                config.biquad_filter.b1 = 4000;
                config.biquad_filter.b2 = 2000;
                config.biquad_filter.a1 = 3000;
                config.biquad_filter.a2 = -1000;
                config.filters_enabled_dirty.Assign(1);
                config.simple_filter_dirty.Assign(1);
                config.biquad_filter_dirty.Assign(1);
            }
            config.physical_address =
                kSampleBase + (stream ? 0 : static_cast<u32>(kSampleBytes / 2)) + voice * 4096;
            config.length = stream ? 8192 : sfx ? 20000 : 1500 + 700 * voice;
            config.is_looping.Assign(!sfx);
            config.adpcm_dirty.Assign(1);
            config.embedded_buffer_dirty.Assign(1);
        }
        region.dsp_configuration.master_volume = 1.0f;
        region.dsp_configuration.aux_return_volume[0] = 0.6f;
        region.dsp_configuration.aux_bus_enable[0] = 1;
        region.dsp_configuration.output_format = DspConfiguration::OutputFormat::Stereo;
        region.dsp_configuration.dirty_raw = 0xFFFFFFFFU;
    };
    setup(guest.Region(0));
    guest.Region(0).frame_counter = 1;

    constexpr unsigned kFrames = 3000;
    auto run = [&] {
        NativeA32DspHle hle({{kSampleBase, 0, kSampleBytes}});
        std::vector<int16_t> pcm;
        pcm.reserve(512);
        u64 sink = 0;
        const double begin = ThreadSeconds();
        for (unsigned frame = 0; frame < kFrames; ++frame) {
            SharedMemory& region = guest.Region(frame & 1U);
            if (frame == 0) setup(region);
            region.frame_counter = static_cast<u16>(frame + 1);
            // Retrigger one sound effect every 20 frames.
            if (frame % 20 == 0) {
                Configuration& config = region.source_configurations.config[13 + frame / 20 % 5];
                config.physical_address = kSampleBase + static_cast<u32>(kSampleBytes / 2) + frame % 64 * 512;
                config.buffer_id = static_cast<u16>(frame);
                config.enable = 1;
                config.enable_dirty.Assign(1);
                config.embedded_buffer_dirty.Assign(1);
            }
            pcm.clear();
            hle.ProcessFrame(guest.access, pcm);
            sink += static_cast<u16>(pcm[frame % pcm.size()]);
        }
        const double seconds = ThreadSeconds() - begin;
        if (sink == 0) std::printf("(silent)\n");
        return seconds / kFrames * 1e3;
    };
    double best = 1e9;
    for (unsigned round = 0; round < 7; ++round) best = std::min(best, run());
    std::printf("bench dsp mix (18 vozes ativas, aux 0): %.4f ms por quadro DSP, %.3f ms por passo de 33,3 ms "
                "(6,82 quadros)\n",
                best, best * 6.82);
}
#endif
} // namespace

int main(int argc, char** argv) {
#if defined(OOT3D_QEMU_AARCH64_TEST)
    argc = 1; // The qemu runtime calls main() without arguments.
#endif
    const bool digestOnly = argc > 1 && std::string(argv[1]) == "--digest";
    // The mixers' float expressions are compiled under -ffast-math, so the reference values hold
    // for GCC (the host build and devkitA64); other compilers only print their digests.
#if defined(__GNUC__) && !defined(__clang__) && defined(__aarch64__)
    const int column = 1;
#elif defined(__GNUC__) && !defined(__clang__) && defined(__x86_64__)
    const int column = 0;
#elif defined(__clang__) && defined(_MSC_VER) && defined(_M_X64)
    const int column = 2;
#else
    const int column = -1;
#endif
    std::size_t restores = 0;
    const bool benchOnly = argc > 1 && std::string(argv[1]) == "--bench-only";
    for (const Expected& expected : kExpected) {
        if (benchOnly) break;
        const u64 digest = RunTitle(expected.seed, expected.frames, &restores);
        std::printf("seed %u frames %u digest %016llx\n", expected.seed, expected.frames,
                    static_cast<unsigned long long>(digest));
        const u64 reference = column == 2   ? expected.clang_x64
                              : column == 1 ? expected.aarch64
                              : column == 0 ? expected.host
                                            : 0;
        if (!digestOnly && reference != 0) Check(digest == reference, "digest differs from the original code");
    }
    std::printf("savestate round trips: %zu\n", restores);
#if !defined(__SWITCH__) && !defined(OOT3D_QEMU_AARCH64_TEST)
    if (argc > 1 && (std::string(argv[1]) == "--bench" || benchOnly)) Benchmark();
#endif
    std::printf(gFailed ? "audio dsp mix: FAIL\n" : "audio dsp mix: PASS\n");
    return gFailed ? 1 : 0;
}
