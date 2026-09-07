#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Oot3dNativeGame {

struct NativeCseqTempoEvent {
    uint64_t Tick = 0;
    uint16_t Tempo = 120;
};

enum class NativeCseqControlKind : uint8_t {
    Pan = 0,
    Volume = 1,
    SurroundPan = 2,
    MasterVolume = 3,
    Expression = 4,
    PitchBend = 5,
    PitchBendRange = 6,
    FxSendA = 7,
    FxSendB = 8,
    FxSendC = 9,
    MainSend = 10,
    ModDepth = 11,
    ModSpeed = 12,
    ModType = 13,
    ModRange = 14,
    Count = 15,
};

inline constexpr std::size_t NativeCseqControlKindCount =
    static_cast<std::size_t>(NativeCseqControlKind::Count);

struct NativeCseqControlEvent {
    uint64_t StartTick = 0;
    uint32_t DurationTicks = 0;
    uint8_t Track = 0;
    NativeCseqControlKind Kind = NativeCseqControlKind::Volume;
    float StartValue = 0.0f;
    float TargetValue = 0.0f;
};

struct NativeCseqNoteEvent {
    uint64_t StartTick = 0;
    uint32_t DurationTicks = 0;
    uint8_t Track = 0;
    uint8_t Program = 0;
    uint8_t BankSlot = 0;
    uint8_t Note = 0;
    uint8_t Velocity = 0;
    uint8_t Volume = 127;
    uint8_t Expression = 127;
    uint8_t Pan = 64;
    uint8_t MasterVolume = 127;
    int8_t Transpose = 0;
    int8_t PitchBend = 0;
    uint8_t PitchBendRange = 2;
    uint8_t VelocityRange = 127;
    int8_t BiquadType = 0;
    int8_t BiquadValue = 0;
    int8_t ModPhase = 0;
    uint8_t ModCurve = 0;
    int8_t ModDepth = 0;
    int8_t ModSpeed = 16;
    uint8_t ModType = 0;
    int8_t ModRange = 1;
    int16_t ModDelay = 0;
    uint16_t ModPeriod = 0;
    int8_t PortamentoKey = 0;
    bool PortamentoEnabled = false;
    uint8_t PortamentoTime = 0;
    int16_t SweepPitch = 0;
    int8_t SurroundPan = 0;
    int8_t LpfCutoff = 64;
    int8_t FxSendA = 0;
    int8_t FxSendB = 0;
    int8_t FxSendC = 0;
    int8_t MainSend = 127;
    int8_t InitialPan = 64;
    bool Damper = false;
    int16_t Hold = -1;
    int16_t Attack = -1;
    int16_t Decay = -1;
    int16_t Sustain = -1;
    int16_t Release = -1;
    bool Tie = false;
};

struct NativeCseqSequence {
    uint16_t Timebase = 48;
    uint16_t InitialTempo = 120;
    uint64_t EndTick = 0;
    uint64_t LoopStartTick = 0;
    bool Looping = false;
    bool ControlOnly = false;
    std::bitset<256> CommandUsage;
    std::bitset<256> ExtendedCommandUsage;
    std::bitset<4> TimeModifierUsage;
    std::array<std::bitset<256>, 4> TimedCommandUsage;
    std::array<uint32_t, 2> TieCommandCount{};
    std::vector<NativeCseqTempoEvent> TempoEvents;
    std::vector<NativeCseqControlEvent> ControlEvents;
    std::vector<NativeCseqNoteEvent> Notes;
};

struct NativeCseqInitialState {
    NativeCseqInitialState() {
        SequenceVariables.fill(-1);
        GlobalVariables.fill(-1);
        for (auto& variables : TrackVariables) {
            variables.fill(-1);
        }
    }

    std::array<int16_t, 16> SequenceVariables{};
    std::array<int16_t, 16> GlobalVariables{};
    std::array<std::array<int16_t, 16>, 16> TrackVariables{};
    struct VariableUpdate {
        enum class Scope : uint8_t {
            Track,
            Global,
            Sequence,
        };

        uint64_t Tick = 0;
        uint8_t FirstTrack = 0;
        uint8_t LastTrack = 0;
        uint8_t Variable = 0;
        int16_t Value = 0;
        Scope VariableScope = Scope::Track;
    };
    std::vector<VariableUpdate> VariableUpdates;
    uint16_t TrackParseMask = 0xffff;
    bool TrackParseMaskAvailable = false;
};

NativeCseqSequence ParseNativeCseq(std::span<const uint8_t> bytes,
                                  uint32_t sequenceOffset,
                                  uint32_t randomSeed = 0,
                                  const NativeCseqInitialState* initialState = nullptr);

double NativeCseqTickToSeconds(const NativeCseqSequence& sequence, uint64_t tick);

} // namespace Oot3dNativeGame
