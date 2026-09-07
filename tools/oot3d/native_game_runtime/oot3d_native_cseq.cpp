#include "oot3d_native_cseq.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace Oot3dNativeGame {
namespace {

class CseqReader {
  public:
    explicit CseqReader(std::span<const uint8_t> bytes) : mBytes(bytes) {}

    void Require(size_t offset, size_t size, const char* role) const {
        if (offset > mBytes.size() || size > mBytes.size() - offset) {
            throw std::runtime_error(std::string("truncated native CSEQ ") + role);
        }
    }

    uint8_t U8(size_t offset) const {
        Require(offset, 1, "u8");
        return mBytes[offset];
    }

    int8_t S8(size_t offset) const {
        return static_cast<int8_t>(U8(offset));
    }

    uint16_t U16(size_t offset) const {
        Require(offset, 2, "u16");
        return static_cast<uint16_t>(mBytes[offset]) |
               static_cast<uint16_t>(mBytes[offset + 1] << 8);
    }

    int16_t S16(size_t offset) const {
        return static_cast<int16_t>(U16(offset));
    }

    uint16_t U16Be(size_t offset) const {
        Require(offset, 2, "u16be");
        return static_cast<uint16_t>(mBytes[offset] << 8) |
               static_cast<uint16_t>(mBytes[offset + 1]);
    }

    int16_t S16Be(size_t offset) const {
        return static_cast<int16_t>(U16Be(offset));
    }

    uint32_t U24Be(size_t offset) const {
        Require(offset, 3, "u24");
        return (static_cast<uint32_t>(mBytes[offset]) << 16) |
               (static_cast<uint32_t>(mBytes[offset + 1]) << 8) |
               mBytes[offset + 2];
    }

    uint32_t U32(size_t offset) const {
        Require(offset, 4, "u32");
        return static_cast<uint32_t>(mBytes[offset]) |
               (static_cast<uint32_t>(mBytes[offset + 1]) << 8) |
               (static_cast<uint32_t>(mBytes[offset + 2]) << 16) |
               (static_cast<uint32_t>(mBytes[offset + 3]) << 24);
    }

    bool Magic(size_t offset, const char (&magic)[5]) const {
        Require(offset, 4, "magic");
        return std::memcmp(mBytes.data() + offset, magic, 4) == 0;
    }

    size_t Size() const {
        return mBytes.size();
    }

  private:
    std::span<const uint8_t> mBytes;
};

enum class ArgumentModifier : uint8_t {
    None,
    Random,
    Variable,
};

enum class TimeModifier : uint8_t {
    None,
    Fixed,
    Random,
    Variable,
};

struct Prefixes {
    bool Conditional = false;
    ArgumentModifier Argument = ArgumentModifier::None;
    TimeModifier Time = TimeModifier::None;
};

struct LoopState {
    size_t Offset = 0;
    uint64_t Tick = 0;
    uint16_t Remaining = 0;
};

struct LinearControlState {
    float StartValue = 0.0f;
    float TargetValue = 0.0f;
    uint64_t StartTick = 0;
    uint32_t DurationTicks = 0;

    float Value(uint64_t tick) const {
        if (DurationTicks == 0 || tick >= StartTick + DurationTicks) {
            return TargetValue;
        }
        if (tick <= StartTick) {
            return StartValue;
        }
        const float progress = static_cast<float>(tick - StartTick) /
            static_cast<float>(DurationTicks);
        return StartValue + (TargetValue - StartValue) * progress;
    }

    void Retarget(uint64_t tick, float target, uint32_t duration) {
        StartValue = Value(tick);
        TargetValue = target;
        StartTick = tick;
        DurationTicks = duration;
    }
};

struct TrackState {
    uint8_t Index = 0;
    size_t Offset = 0;
    uint64_t Tick = 0;
    uint32_t Program = 0;
    uint8_t BankSlot = 0;
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
    bool Tie = false;
    int16_t Hold = -1;
    int16_t Attack = -1;
    int16_t Decay = -1;
    int16_t Sustain = -1;
    int16_t Release = -1;
    bool NoteWait = false;
    bool Comparison = false;
    std::array<int16_t, 16> Variables{};
    LinearControlState PanControl{64.0f, 64.0f};
    LinearControlState VolumeControl{127.0f, 127.0f};
    LinearControlState SurroundPanControl{};
    LinearControlState ExpressionControl{127.0f, 127.0f};
    LinearControlState PitchBendControl{};
    LinearControlState PitchBendRangeControl{2.0f, 2.0f};
    LinearControlState FxSendAControl{};
    LinearControlState FxSendBControl{};
    LinearControlState FxSendCControl{};
    LinearControlState MainSendControl{127.0f, 127.0f};
    LinearControlState ModDepthControl{};
    LinearControlState ModSpeedControl{16.0f, 16.0f};
    LinearControlState ModTypeControl{};
    LinearControlState ModRangeControl{1.0f, 1.0f};
    std::vector<size_t> Calls;
    std::vector<LoopState> Loops;
    size_t NextExternalUpdate = 0;
    std::unordered_map<size_t, uint16_t> Visits;
    std::unordered_map<size_t, uint64_t> FirstVisitTicks;
};

class CseqInterpreter {
  public:
    CseqInterpreter(const CseqReader& reader, size_t dataBase, size_t dataEnd,
                    uint32_t randomSeed,
                    const NativeCseqInitialState* initialState)
        : mReader(reader), mDataBase(dataBase), mDataEnd(dataEnd),
          mRandomState(randomSeed == 0 ? 0x6d2b79f5u : randomSeed) {
        mSequenceVariables.fill(-1);
        mGlobalVariables.fill(-1);
        for (auto& variables : mInitialTrackVariables) {
            variables.fill(-1);
        }
        if (initialState != nullptr) {
            mSequenceVariables = initialState->SequenceVariables;
            mGlobalVariables = initialState->GlobalVariables;
            mInitialTrackVariables = initialState->TrackVariables;
            mVariableUpdates = initialState->VariableUpdates;
            std::stable_sort(
                mVariableUpdates.begin(), mVariableUpdates.end(),
                [](const NativeCseqInitialState::VariableUpdate& lhs,
                   const NativeCseqInitialState::VariableUpdate& rhs) {
                    return lhs.Tick < rhs.Tick;
                });
            for (const auto& update : mVariableUpdates) {
                if (update.FirstTrack > update.LastTrack ||
                    update.LastTrack >= mInitialTrackVariables.size() ||
                    update.Variable >= 16) {
                    throw std::runtime_error(
                        "native CSEQ external variable update is invalid");
                }
            }
            mTrackParseMask = initialState->TrackParseMask;
            mTrackParseMaskAvailable = initialState->TrackParseMaskAvailable;
        }
    }

    NativeCseqSequence Run(size_t sequenceStart) {
        OpenTrack(0, sequenceStart);
        size_t pendingIndex = 0;
        while (pendingIndex < mPendingTracks.size()) {
            TrackState track = std::move(mPendingTracks[pendingIndex++]);
            ExecuteTrack(track);
            mResult.EndTick = std::max(mResult.EndTick, track.Tick);
        }
        std::sort(mResult.Notes.begin(), mResult.Notes.end(),
                  [](const NativeCseqNoteEvent& lhs, const NativeCseqNoteEvent& rhs) {
                      if (lhs.StartTick != rhs.StartTick) {
                          return lhs.StartTick < rhs.StartTick;
                      }
                      return lhs.Track < rhs.Track;
                  });
        std::sort(mResult.TempoEvents.begin(), mResult.TempoEvents.end(),
                  [](const NativeCseqTempoEvent& lhs, const NativeCseqTempoEvent& rhs) {
                      return lhs.Tick < rhs.Tick;
                  });
        mResult.TempoEvents.erase(
            std::unique(mResult.TempoEvents.begin(), mResult.TempoEvents.end(),
                        [](const NativeCseqTempoEvent& lhs,
                           const NativeCseqTempoEvent& rhs) {
                            return lhs.Tick == rhs.Tick && lhs.Tempo == rhs.Tempo;
                        }),
            mResult.TempoEvents.end());
        std::stable_sort(
            mResult.ControlEvents.begin(), mResult.ControlEvents.end(),
            [](const NativeCseqControlEvent& lhs,
               const NativeCseqControlEvent& rhs) {
                return lhs.StartTick < rhs.StartTick;
            });
        mResult.ControlOnly = mResult.Notes.empty();
        return mResult;
    }

  private:
    static int16_t Wrap16(int32_t value) {
        return static_cast<int16_t>(static_cast<uint16_t>(value));
    }

    size_t Target(uint32_t relative, const char* role) const {
        if (relative > std::numeric_limits<size_t>::max() - mDataBase) {
            throw std::runtime_error(std::string("native CSEQ ") + role +
                                     " target overflows");
        }
        const size_t target = mDataBase + relative;
        if (target < mDataBase || target >= mDataEnd) {
            throw std::runtime_error(std::string("native CSEQ ") + role +
                                     " target is out of range");
        }
        return target;
    }

    uint32_t ReadVariableLength(size_t& offset) const {
        uint32_t value = 0;
        for (size_t count = 0; count < 4; ++count) {
            const uint8_t byte = mReader.U8(offset++);
            value = (value << 7) | (byte & 0x7f);
            if ((byte & 0x80) == 0) {
                return value;
            }
        }
        throw std::runtime_error("native CSEQ variable-length value is too long");
    }

    uint32_t NextRandom() {
        mRandomState = mRandomState * 1664525u + 1013904223u;
        return mRandomState;
    }

    int16_t& Variable(TrackState& track, uint8_t index) {
        if (index < 0x10) {
            return track.Variables[index];
        }
        if (index < 0x20) {
            return mGlobalVariables[index - 0x10];
        }
        if (index < 0x30) {
            return mSequenceVariables[index - 0x20];
        }
        throw std::runtime_error("native CSEQ variable index is out of range");
    }

    int32_t Variable(const TrackState& track, uint8_t index) const {
        if (index < 0x10) {
            return track.Variables[index];
        }
        if (index < 0x20) {
            return mGlobalVariables[index - 0x10];
        }
        if (index < 0x30) {
            return mSequenceVariables[index - 0x20];
        }
        throw std::runtime_error("native CSEQ variable index is out of range");
    }

    Prefixes ReadPrefixes(size_t& offset, uint8_t& opcode) const {
        Prefixes prefixes;
        opcode = mReader.U8(offset++);
        if (opcode == 0xa2) {
            prefixes.Conditional = true;
            opcode = mReader.U8(offset++);
        }
        if (opcode >= 0xa3 && opcode <= 0xa5) {
            prefixes.Time = static_cast<TimeModifier>(opcode - 0xa2);
            opcode = mReader.U8(offset++);
        }
        if (opcode == 0xa0 || opcode == 0xa1) {
            prefixes.Argument = opcode == 0xa0
                ? ArgumentModifier::Random
                : ArgumentModifier::Variable;
            opcode = mReader.U8(offset++);
        }
        return prefixes;
    }

    int32_t ReadArgument(const TrackState& track, size_t& offset,
                         ArgumentModifier modifier,
                         bool variableLength, bool signedByte = false,
                         bool signedWord = false) {
        if (modifier == ArgumentModifier::Random) {
            const int32_t minimum = mReader.S16Be(offset);
            const int32_t maximum = mReader.S16Be(offset + 2);
            offset += 4;
            const int32_t low = std::min(minimum, maximum);
            const uint32_t range = static_cast<uint32_t>(
                std::max(minimum, maximum) - low + 1);
            return low + static_cast<int32_t>(NextRandom() % range);
        }
        if (modifier == ArgumentModifier::Variable) {
            return Variable(track, mReader.U8(offset++));
        }
        if (variableLength) {
            return static_cast<int32_t>(ReadVariableLength(offset));
        }
        if (signedByte) {
            return mReader.S8(offset++);
        }
        if (signedWord) {
            const int32_t result = mReader.S16Be(offset);
            offset += 2;
            return result;
        }
        return mReader.U8(offset++);
    }

    uint32_t ReadTimeArgument(const TrackState& track, size_t& offset,
                              TimeModifier modifier) {
        if (modifier == TimeModifier::Fixed) {
            const uint32_t result = mReader.U16Be(offset);
            offset += 2;
            return result;
        }
        if (modifier == TimeModifier::Random) {
            const uint32_t first = mReader.U16Be(offset);
            const uint32_t second = mReader.U16Be(offset + 2);
            offset += 4;
            const uint32_t low = std::min(first, second);
            const uint32_t range = std::max(first, second) - low + 1;
            return low + NextRandom() % range;
        }
        if (modifier == TimeModifier::Variable) {
            return static_cast<uint32_t>(
                std::max(Variable(track, mReader.U8(offset++)), 0));
        }
        return 0;
    }

    void RetargetControl(TrackState& track, NativeCseqControlKind kind,
                         LinearControlState& state, float target,
                         uint32_t duration) {
        const float start = state.Value(track.Tick);
        mResult.ControlEvents.push_back(
            {track.Tick, duration, track.Index, kind, start, target});
        state.Retarget(track.Tick, target, duration);
    }

    void OpenTrack(uint8_t index, size_t target) {
        if (index >= mInitialTrackVariables.size()) {
            throw std::runtime_error("native CSEQ track index is out of range");
        }
        if (mTrackParseMaskAvailable &&
            (mTrackParseMask & (uint16_t{1} << index)) == 0) {
            return;
        }
        const auto duplicate = std::find_if(
            mPendingTracks.begin(), mPendingTracks.end(),
            [&](const TrackState& track) {
                return track.Index == index && track.Offset == target;
            });
        if (duplicate == mPendingTracks.end()) {
            TrackState track{.Index = index, .Offset = target};
            track.Variables = mInitialTrackVariables[index];
            mPendingTracks.push_back(std::move(track));
        }
    }

    void ExecuteExtended(TrackState& track, const Prefixes& prefixes,
                         bool execute) {
        const uint8_t opcode = mReader.U8(track.Offset++);
        mResult.ExtendedCommandUsage.set(opcode);
        if ((opcode >= 0x80 && opcode <= 0x8b) ||
            (opcode >= 0x90 && opcode <= 0x95)) {
            const uint8_t variableIndex = mReader.U8(track.Offset++);
            const int32_t operand = ReadArgument(track, track.Offset, prefixes.Argument,
                                                 false, false, true);
            if (!execute) {
                return;
            }
            int16_t& variable = Variable(track, variableIndex);
            if (opcode == 0x80) {
                variable = Wrap16(operand);
            } else if (opcode == 0x81) {
                variable = Wrap16(static_cast<int32_t>(variable) + operand);
            } else if (opcode == 0x82) {
                variable = Wrap16(static_cast<int32_t>(variable) - operand);
            } else if (opcode == 0x83) {
                variable = Wrap16(static_cast<int32_t>(variable) * operand);
            } else if (opcode == 0x84) {
                if (operand != 0) {
                    variable = Wrap16(static_cast<int32_t>(variable) / operand);
                }
            } else if (opcode == 0x85) {
                variable = operand >= 0
                    ? Wrap16(static_cast<uint16_t>(variable) << std::min(operand, 15))
                    : Wrap16(static_cast<int32_t>(variable) >> std::min(-operand, 15));
            } else if (opcode == 0x86) {
                const uint32_t range = static_cast<uint32_t>(std::abs(operand)) + 1;
                variable = Wrap16(static_cast<int32_t>(NextRandom() % range));
            } else if (opcode == 0x87) {
                variable = Wrap16(static_cast<int32_t>(variable) & operand);
            } else if (opcode == 0x88) {
                variable = Wrap16(static_cast<int32_t>(variable) | operand);
            } else if (opcode == 0x89) {
                variable = Wrap16(static_cast<int32_t>(variable) ^ operand);
            } else if (opcode == 0x8a) {
                variable = Wrap16(~operand);
            } else if (opcode == 0x8b) {
                if (operand != 0) {
                    variable = Wrap16(static_cast<int32_t>(variable) % operand);
                }
            } else if (opcode == 0x90) {
                track.Comparison = variable == operand;
            } else if (opcode == 0x91) {
                track.Comparison = variable >= operand;
            } else if (opcode == 0x92) {
                track.Comparison = variable > operand;
            } else if (opcode == 0x93) {
                track.Comparison = variable <= operand;
            } else if (opcode == 0x94) {
                track.Comparison = variable < operand;
            } else {
                track.Comparison = variable != operand;
            }
            return;
        }
        if (opcode >= 0xa0 && opcode <= 0xb1) {
            (void)ReadArgument(track, track.Offset, prefixes.Argument, false);
            return;
        }
        if (opcode == 0xe0) {
            if (prefixes.Argument == ArgumentModifier::None) {
                track.Offset += 2;
                mReader.Require(track.Offset - 2, 2, "extended u16 argument");
            } else {
                (void)ReadArgument(track, track.Offset, prefixes.Argument, false);
            }
            return;
        }
        if (opcode >= 0xe1 && opcode <= 0xe6) {
            (void)ReadArgument(track, track.Offset, prefixes.Argument, false, false, true);
            return;
        }
        throw std::runtime_error("unsupported native CSEQ extended command " +
                                 std::to_string(opcode));
    }

    void EmitNote(TrackState& track, uint8_t note, uint8_t velocity,
                  uint32_t duration) {
        NativeCseqNoteEvent event;
        event.StartTick = track.Tick;
        event.DurationTicks = duration;
        event.Track = track.Index;
        event.Program = static_cast<uint8_t>(track.Program & 0x7f);
        event.BankSlot = track.BankSlot;
        event.Note = note;
        event.Velocity = velocity;
        event.Volume = static_cast<uint8_t>(std::clamp(
            static_cast<int>(track.VolumeControl.Value(track.Tick)), 0, 127));
        event.Expression = static_cast<uint8_t>(std::clamp(
            static_cast<int>(track.ExpressionControl.Value(track.Tick)), 0, 127));
        event.Pan = static_cast<uint8_t>(std::clamp(
            static_cast<int>(track.PanControl.Value(track.Tick)), 0, 127));
        event.MasterVolume = static_cast<uint8_t>(std::clamp(
            static_cast<int>(mMasterVolumeControl.Value(track.Tick)), 0, 127));
        event.Transpose = track.Transpose;
        event.PitchBend = static_cast<int8_t>(std::clamp(
            static_cast<int>(track.PitchBendControl.Value(track.Tick)),
            -128, 127));
        event.PitchBendRange = static_cast<uint8_t>(std::clamp(
            static_cast<int>(track.PitchBendRangeControl.Value(track.Tick)),
            0, 127));
        event.VelocityRange = track.VelocityRange;
        event.BiquadType = track.BiquadType;
        event.BiquadValue = track.BiquadValue;
        event.ModPhase = track.ModPhase;
        event.ModCurve = track.ModCurve;
        event.ModDepth = track.ModDepth;
        event.ModSpeed = track.ModSpeed;
        event.ModType = track.ModType;
        event.ModRange = track.ModRange;
        event.ModDelay = track.ModDelay;
        event.ModPeriod = track.ModPeriod;
        event.PortamentoKey = track.PortamentoKey;
        event.PortamentoEnabled = track.PortamentoEnabled;
        event.PortamentoTime = track.PortamentoTime;
        event.SweepPitch = track.SweepPitch;
        event.SurroundPan = static_cast<int8_t>(std::clamp(
            static_cast<int>(track.SurroundPanControl.Value(track.Tick)),
            -128, 127));
        event.LpfCutoff = track.LpfCutoff;
        event.FxSendA = static_cast<int8_t>(std::clamp(
            static_cast<int>(track.FxSendAControl.Value(track.Tick)),
            -128, 127));
        event.FxSendB = static_cast<int8_t>(std::clamp(
            static_cast<int>(track.FxSendBControl.Value(track.Tick)),
            -128, 127));
        event.FxSendC = static_cast<int8_t>(std::clamp(
            static_cast<int>(track.FxSendCControl.Value(track.Tick)),
            -128, 127));
        event.MainSend = static_cast<int8_t>(std::clamp(
            static_cast<int>(track.MainSendControl.Value(track.Tick)),
            -128, 127));
        event.InitialPan = track.InitialPan;
        event.Damper = track.Damper;
        event.Hold = track.Hold;
        event.Attack = track.Attack;
        event.Decay = track.Decay;
        event.Sustain = track.Sustain;
        event.Release = track.Release;
        event.Tie = track.Tie;
        mResult.Notes.push_back(event);
        track.PortamentoKey = static_cast<int8_t>(note);
        if (track.NoteWait) {
            track.Tick += duration;
        }
    }

    void ApplyExternalVariableUpdates(TrackState& track) {
        while (track.NextExternalUpdate < mVariableUpdates.size() &&
               mVariableUpdates[track.NextExternalUpdate].Tick <= track.Tick) {
            const auto& update =
                mVariableUpdates[track.NextExternalUpdate++];
            if (track.Index >= update.FirstTrack &&
                track.Index <= update.LastTrack) {
                switch (update.VariableScope) {
                case NativeCseqInitialState::VariableUpdate::Scope::Track:
                    track.Variables[update.Variable] = update.Value;
                    break;
                case NativeCseqInitialState::VariableUpdate::Scope::Global:
                    mGlobalVariables[update.Variable] = update.Value;
                    break;
                case NativeCseqInitialState::VariableUpdate::Scope::Sequence:
                    mSequenceVariables[update.Variable] = update.Value;
                    break;
                }
            }
        }
    }

    void ExecuteTrack(TrackState& track) {
        constexpr uint32_t kMaximumCommandsPerTrack = 131072;
        for (uint32_t commandIndex = 0; commandIndex < kMaximumCommandsPerTrack;
             ++commandIndex) {
            if (track.Offset < mDataBase || track.Offset >= mDataEnd) {
                throw std::runtime_error("native CSEQ track escaped DATA section");
            }
            ApplyExternalVariableUpdates(track);
            uint16_t& visits = track.Visits[track.Offset];
            track.FirstVisitTicks.try_emplace(track.Offset, track.Tick);
            if (++visits > 64) {
                mResult.Looping = true;
                mResult.LoopStartTick = track.FirstVisitTicks[track.Offset];
                break;
            }

            uint8_t opcode = 0;
            const Prefixes prefixes = ReadPrefixes(track.Offset, opcode);
            mResult.CommandUsage.set(opcode);
            mResult.TimeModifierUsage.set(
                static_cast<size_t>(prefixes.Time));
            mResult.TimedCommandUsage[static_cast<size_t>(prefixes.Time)].set(
                opcode);
            const bool execute = !prefixes.Conditional || track.Comparison;

            if (opcode < 0x80) {
                const uint8_t velocity = mReader.U8(track.Offset++);
                const int32_t rawDuration = ReadArgument(
                    track, track.Offset, prefixes.Argument, true);
                (void)ReadTimeArgument(track, track.Offset, prefixes.Time);
                if (execute) {
                    EmitNote(track, opcode, velocity,
                             static_cast<uint32_t>(std::max(rawDuration, 0)));
                }
                continue;
            }
            if (opcode == 0x80) {
                const int32_t wait = ReadArgument(
                    track, track.Offset, prefixes.Argument, true);
                (void)ReadTimeArgument(track, track.Offset, prefixes.Time);
                if (execute && wait > 0) {
                    track.Tick += static_cast<uint32_t>(wait);
                }
                continue;
            }
            if (opcode == 0x81) {
                const int32_t program = ReadArgument(
                    track, track.Offset, prefixes.Argument, true);
                (void)ReadTimeArgument(track, track.Offset, prefixes.Time);
                if (execute) {
                    track.Program = static_cast<uint32_t>(std::max(program, 0));
                    if (track.Program >= 0x80) {
                        track.BankSlot = static_cast<uint8_t>((track.Program >> 7) & 0xff);
                    }
                }
                continue;
            }
            if (opcode == 0x88) {
                const uint8_t index = mReader.U8(track.Offset++);
                const size_t target = Target(mReader.U24Be(track.Offset), "open-track");
                track.Offset += 3;
                if (execute) {
                    OpenTrack(index, target);
                }
                continue;
            }
            if (opcode == 0x89 || opcode == 0x8a) {
                const size_t target = Target(mReader.U24Be(track.Offset),
                                             opcode == 0x89 ? "jump" : "call");
                track.Offset += 3;
                if (execute) {
                    if (opcode == 0x8a) {
                        track.Calls.push_back(track.Offset);
                    } else if (!prefixes.Conditional && target <= track.Offset &&
                               track.Visits[target] != 0) {
                        mResult.Looping = true;
                        mResult.LoopStartTick = track.FirstVisitTicks[target];
                        break;
                    }
                    track.Offset = target;
                }
                continue;
            }
            if (opcode == 0x90 || opcode == 0x96) {
                track.Offset += 2;
                mReader.Require(track.Offset - 2, 2, "unknown u16 command");
                continue;
            }
            if (opcode >= 0xb0 && opcode <= 0xdf) {
                const bool signedValue = opcode == 0xb1 || opcode == 0xc3 ||
                    opcode == 0xc4 || (opcode >= 0xd0 && opcode <= 0xd3);
                const int32_t value = ReadArgument(track, track.Offset, prefixes.Argument,
                                                   false, signedValue);
                const uint32_t transitionTicks =
                    ReadTimeArgument(track, track.Offset, prefixes.Time);
                if (!execute) {
                    continue;
                }
                const uint8_t byteValue = static_cast<uint8_t>(value);
                if (opcode == 0xb0 && value > 0) {
                    mResult.Timebase = static_cast<uint16_t>(value);
                } else if (opcode == 0xb1) {
                    track.Hold = byteValue;
                } else if (opcode == 0xb3) {
                    track.VelocityRange = byteValue;
                } else if (opcode == 0xb4) {
                    track.BiquadType = static_cast<int8_t>(value);
                } else if (opcode == 0xb5) {
                    track.BiquadValue = static_cast<int8_t>(value);
                } else if (opcode == 0xb6) {
                    track.BankSlot = byteValue;
                } else if (opcode == 0xbd) {
                    track.ModPhase = static_cast<int8_t>(value);
                } else if (opcode == 0xbe) {
                    track.ModCurve = byteValue;
                } else if (opcode == 0xc0) {
                    track.Pan = byteValue;
                    RetargetControl(track, NativeCseqControlKind::Pan,
                                    track.PanControl, byteValue,
                                    transitionTicks);
                } else if (opcode == 0xc1) {
                    track.Volume = byteValue;
                    RetargetControl(track, NativeCseqControlKind::Volume,
                                    track.VolumeControl, byteValue,
                                    transitionTicks);
                } else if (opcode == 0xc2) {
                    track.MasterVolume = byteValue;
                    RetargetControl(track,
                                    NativeCseqControlKind::MasterVolume,
                                    mMasterVolumeControl, byteValue,
                                    transitionTicks);
                } else if (opcode == 0xc3) {
                    track.Transpose = static_cast<int8_t>(value);
                } else if (opcode == 0xc4) {
                    track.PitchBend = static_cast<int8_t>(value);
                    RetargetControl(track, NativeCseqControlKind::PitchBend,
                                    track.PitchBendControl,
                                    static_cast<int8_t>(value),
                                    transitionTicks);
                } else if (opcode == 0xc5) {
                    track.PitchBendRange = byteValue;
                    RetargetControl(track,
                                    NativeCseqControlKind::PitchBendRange,
                                    track.PitchBendRangeControl, byteValue,
                                    transitionTicks);
                } else if (opcode == 0xc7) {
                    track.NoteWait = value != 0;
                } else if (opcode == 0xc8) {
                    ++mResult.TieCommandCount[value != 0 ? 1 : 0];
                    track.Tie = value != 0;
                } else if (opcode == 0xc9) {
                    track.PortamentoKey = static_cast<int8_t>(value);
                } else if (opcode == 0xca) {
                    track.ModDepth = static_cast<int8_t>(value);
                    RetargetControl(track, NativeCseqControlKind::ModDepth,
                                    track.ModDepthControl,
                                    static_cast<int8_t>(value),
                                    transitionTicks);
                } else if (opcode == 0xcb) {
                    track.ModSpeed = static_cast<int8_t>(byteValue);
                    RetargetControl(track, NativeCseqControlKind::ModSpeed,
                                    track.ModSpeedControl,
                                    static_cast<int8_t>(byteValue),
                                    transitionTicks);
                } else if (opcode == 0xcc) {
                    track.ModType = byteValue;
                    RetargetControl(track, NativeCseqControlKind::ModType,
                                    track.ModTypeControl, byteValue,
                                    transitionTicks);
                } else if (opcode == 0xcd) {
                    track.ModRange = static_cast<int8_t>(value);
                    RetargetControl(track, NativeCseqControlKind::ModRange,
                                    track.ModRangeControl,
                                    static_cast<int8_t>(value),
                                    transitionTicks);
                } else if (opcode == 0xce) {
                    track.PortamentoEnabled = value != 0;
                } else if (opcode == 0xcf) {
                    track.PortamentoTime = byteValue;
                } else if (opcode == 0xd0) {
                    track.Attack = static_cast<int8_t>(value);
                } else if (opcode == 0xd1) {
                    track.Decay = static_cast<int8_t>(value);
                } else if (opcode == 0xd2) {
                    track.Sustain = static_cast<int8_t>(value);
                } else if (opcode == 0xd3) {
                    track.Release = static_cast<int8_t>(value);
                } else if (opcode == 0xd4) {
                    track.Loops.push_back(LoopState{
                        .Offset = track.Offset,
                        .Tick = track.Tick,
                        .Remaining = static_cast<uint16_t>(byteValue),
                    });
                } else if (opcode == 0xd5) {
                    track.Expression = byteValue;
                    RetargetControl(track, NativeCseqControlKind::Expression,
                                    track.ExpressionControl, byteValue,
                                    transitionTicks);
                } else if (opcode == 0xd7) {
                    track.SurroundPan = static_cast<int8_t>(value);
                    RetargetControl(track, NativeCseqControlKind::SurroundPan,
                                    track.SurroundPanControl,
                                    static_cast<int8_t>(value),
                                    transitionTicks);
                } else if (opcode == 0xd8) {
                    track.LpfCutoff = static_cast<int8_t>(value);
                } else if (opcode == 0xd9) {
                    track.FxSendA = static_cast<int8_t>(value);
                    RetargetControl(track, NativeCseqControlKind::FxSendA,
                                    track.FxSendAControl,
                                    static_cast<int8_t>(value),
                                    transitionTicks);
                } else if (opcode == 0xda) {
                    track.FxSendB = static_cast<int8_t>(value);
                    RetargetControl(track, NativeCseqControlKind::FxSendB,
                                    track.FxSendBControl,
                                    static_cast<int8_t>(value),
                                    transitionTicks);
                } else if (opcode == 0xdb) {
                    track.MainSend = static_cast<int8_t>(value);
                    RetargetControl(track, NativeCseqControlKind::MainSend,
                                    track.MainSendControl,
                                    static_cast<int8_t>(value),
                                    transitionTicks);
                } else if (opcode == 0xdc) {
                    track.InitialPan = static_cast<int8_t>(value);
                } else if (opcode == 0xde) {
                    track.FxSendC = static_cast<int8_t>(value);
                    RetargetControl(track, NativeCseqControlKind::FxSendC,
                                    track.FxSendCControl,
                                    static_cast<int8_t>(value),
                                    transitionTicks);
                } else if (opcode == 0xdf) {
                    track.Damper = value != 0;
                }
                continue;
            }
            if (opcode == 0xe0 || opcode == 0xe1 || opcode == 0xe3 ||
                opcode == 0xe4) {
                int32_t value = 0;
                if (prefixes.Argument == ArgumentModifier::None) {
                    value = opcode == 0xe0 || opcode == 0xe3
                        ? mReader.S16Be(track.Offset)
                        : mReader.U16Be(track.Offset);
                    track.Offset += 2;
                } else {
                    value = ReadArgument(track, track.Offset, prefixes.Argument,
                                         false, false, true);
                }
                (void)ReadTimeArgument(track, track.Offset, prefixes.Time);
                if (!execute) {
                    continue;
                }
                if (opcode == 0xe0) {
                    track.ModDelay = static_cast<int16_t>(value);
                } else if (opcode == 0xe1 && value > 0) {
                    const uint16_t tempo = static_cast<uint16_t>(value);
                    if (mResult.TempoEvents.empty()) {
                        mResult.InitialTempo = tempo;
                    }
                    mResult.TempoEvents.push_back({track.Tick, tempo});
                } else if (opcode == 0xe3) {
                    track.SweepPitch = static_cast<int16_t>(value);
                } else if (opcode == 0xe4) {
                    track.ModPeriod = static_cast<uint16_t>(value);
                }
                continue;
            }
            if (opcode == 0xf0) {
                ExecuteExtended(track, prefixes, execute);
                (void)ReadTimeArgument(track, track.Offset, prefixes.Time);
                continue;
            }
            if (opcode == 0xfb) {
                if (execute) {
                    track.Hold = track.Attack = track.Decay = track.Sustain =
                        track.Release = -1;
                }
                continue;
            }
            if (opcode == 0xfc) {
                if (execute && !track.Loops.empty()) {
                    LoopState& loop = track.Loops.back();
                    if (loop.Remaining == 0) {
                        mResult.Looping = true;
                        mResult.LoopStartTick = loop.Tick;
                        break;
                    }
                    if (--loop.Remaining != 0) {
                        track.Offset = loop.Offset;
                    } else {
                        track.Loops.pop_back();
                    }
                }
                continue;
            }
            if (opcode == 0xfd) {
                if (execute) {
                    if (track.Calls.empty()) {
                        break;
                    }
                    track.Offset = track.Calls.back();
                    track.Calls.pop_back();
                }
                continue;
            }
            if (opcode == 0xfe) {
                track.Offset += 2;
                mReader.Require(track.Offset - 2, 2, "track allocation mask");
                continue;
            }
            if (opcode == 0xff) {
                break;
            }
            throw std::runtime_error("unsupported native CSEQ command " +
                                     std::to_string(opcode));
        }
    }

    const CseqReader& mReader;
    size_t mDataBase = 0;
    size_t mDataEnd = 0;
    uint32_t mRandomState = 0;
    std::array<int16_t, 16> mSequenceVariables{};
    std::array<int16_t, 16> mGlobalVariables{};
    std::array<std::array<int16_t, 16>, 16> mInitialTrackVariables{};
    std::vector<NativeCseqInitialState::VariableUpdate> mVariableUpdates;
    uint16_t mTrackParseMask = 0xffff;
    bool mTrackParseMaskAvailable = false;
    LinearControlState mMasterVolumeControl{127.0f, 127.0f};
    std::vector<TrackState> mPendingTracks;
    NativeCseqSequence mResult;
};

} // namespace

NativeCseqSequence ParseNativeCseq(std::span<const uint8_t> bytes,
                                  uint32_t sequenceOffset,
                                  uint32_t randomSeed,
                                  const NativeCseqInitialState* initialState) {
    CseqReader reader(bytes);
    if (!reader.Magic(0, "CSEQ") || reader.U16(4) != 0xfeff) {
        throw std::runtime_error("unsupported native CSEQ header");
    }
    const size_t dataSection = reader.U32(24);
    if (!reader.Magic(dataSection, "DATA")) {
        throw std::runtime_error("native CSEQ has no DATA section");
    }
    const uint32_t dataLength = reader.U32(dataSection + 4);
    const size_t dataBase = dataSection + 8;
    const size_t dataEnd = dataSection + dataLength;
    if (dataEnd > reader.Size() || dataBase > dataEnd ||
        sequenceOffset >= dataEnd - dataBase) {
        throw std::runtime_error("native CSEQ DATA section is invalid");
    }
    CseqInterpreter interpreter(reader, dataBase, dataEnd, randomSeed,
                                initialState);
    return interpreter.Run(dataBase + sequenceOffset);
}

double NativeCseqTickToSeconds(const NativeCseqSequence& sequence, uint64_t tick) {
    if (sequence.Timebase == 0) {
        throw std::runtime_error("native CSEQ timebase is zero");
    }
    uint64_t cursor = 0;
    uint16_t tempo = sequence.InitialTempo == 0 ? 120 : sequence.InitialTempo;
    double seconds = 0.0;
    for (const auto& change : sequence.TempoEvents) {
        if (change.Tick > tick) {
            break;
        }
        if (change.Tick > cursor) {
            seconds += static_cast<double>(change.Tick - cursor) * 60.0 /
                       (static_cast<double>(tempo) * sequence.Timebase);
        }
        cursor = change.Tick;
        if (change.Tempo != 0) {
            tempo = change.Tempo;
        }
    }
    if (tick > cursor) {
        seconds += static_cast<double>(tick - cursor) * 60.0 /
                   (static_cast<double>(tempo) * sequence.Timebase);
    }
    return seconds;
}

} // namespace Oot3dNativeGame
