#pragma once

#include "oot3d_native_a32_memory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Oot3dNativeGame {

inline constexpr std::uint32_t kObjectBankExtensionBase = 0x06200000U;
inline constexpr std::uint32_t kObjectBankExtensionSize = 0x1000U;
inline constexpr std::uint32_t kObjectBankNativeSlots = 19U;
inline constexpr std::uint32_t kObjectBankExtraSlots = 20U;
inline constexpr std::uint32_t kObjectBankSlotStride = 0x80U;
// SingleScreen 3.1's overflow of the native deferred archive list (PlayState
// +0x43DC count, +0x43E0 entries of 0x70 bytes, 19 of them): a second page,
// mapped on first use. 3.1 keeps it at 0x0062B0E0.
inline constexpr std::uint32_t kObjectBankReleaseQueueBase = 0x06201000U;
inline constexpr std::uint32_t kObjectBankReleaseQueueSize = 0x1000U;
inline constexpr std::uint32_t kObjectBankReleaseQueueSlots = 19U;
inline constexpr std::uint32_t kObjectBankArchiveBytes = 0x70U;

// Native ObjectContext starts with four count/index bytes. 0x00629AFC in
// SingleScreen 3.0 is a context, so the first extra record is at base + 4.
struct ObjectBankSlotRecord {
  std::int16_t ObjectId = 0; // negative: loading; positive: ready; zero: empty
  std::uint16_t Reserved = 0;
  std::uint32_t Buffer = 0;
  std::uint32_t Size = 0;
  std::uint32_t DmaRequest = 0;
  std::array<std::uint8_t, 0x70> Archive{};
};
static_assert(sizeof(ObjectBankSlotRecord) == kObjectBankSlotStride);
static_assert(offsetof(ObjectBankSlotRecord, Archive) == 0x10U);

struct ObjectBankExtensionStats {
  std::uint64_t Lookups = 0;
  std::uint64_t ExtraHits = 0;
  std::uint64_t Loads = 0;
  std::uint64_t LoadFailures = 0;
  std::uint64_t Frees = 0;
  std::uint64_t RoomMarks = 0;
  std::uint64_t Sweeps = 0;
  std::uint64_t NativeCalls = 0;
  // SingleScreen 3.1 lifetime fixes: loads cancelled before their buffer is
  // freed, and archives that overflowed the native deferred list.
  std::uint64_t LoadingCancels = 0;
  std::uint64_t DeferredQueued = 0;
  std::uint64_t DeferredReleases = 0;
};

class ObjectBankExtension final {
public:
  explicit ObjectBankExtension(NativeA32Memory &memory) : mMemory(memory) {}

  // A disabled, empty bank neither maps memory nor intercepts its own hooks.
  // Disabling stops new loads immediately. Existing records keep their
  // lookup/retirement hooks until native scene teardown releases them.
  // SingleScreen 3.1's object lifetime fixes are native bug fixes and apply
  // in every case: a slot still loading has its file request destroyed
  // before its buffer is freed (room change and scene teardown), and an
  // archive that would overflow the native deferred list is queued.
  void SetEnabled(bool enabled) noexcept { mEnabled = enabled; }
  static std::span<const std::uint32_t> HookPcs() noexcept;
  static std::span<const std::uint32_t> ResumePcs() noexcept;
  static std::span<const std::uint32_t> NativeCallPcs() noexcept;
  [[nodiscard]] bool ShouldObserve(std::uint32_t pc) const;
  [[nodiscard]] bool Pending() const noexcept;
  bool Execute(std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
               oot3d::recomp::a32::ExecutionResult *result,
               std::uint32_t *blocksConsumed);

  // Pure 0x0060629C address policy. Invalid indices/overflow return zero.
  static std::uint32_t SlotAddress(std::uint32_t play, std::int32_t index) noexcept;
  // Lazily maps one writable, non-executable page; an unrelated mapping is
  // an error, including a partial overlap. Existing bank bytes are preserved.
  bool EnsureRegion(std::string *error = nullptr);

  // Save together with NativeA32Memory, guest threads and native DMA/service
  // state. Restore memory first, then this state, before dispatch resumes.
  // External preference mEnabled is deliberately not part of the snapshot.
  nlohmann::json CaptureState() const;
  bool RestoreState(const nlohmann::json &saved, std::string *error = nullptr);
  // Only after replacing guest memory with a fresh/legacy image with no bank.
  void Reset() noexcept;
  const ObjectBankExtensionStats &Stats() const noexcept { return mStats; }

private:
  enum class Operation : std::uint8_t {
    Idle, Load, Sweep, Destroy, CancelLoading, RoomReplace, Release};
  enum class Call : std::uint8_t {
    None, Spawn, DestroyArchive, FreeBuffer, DestroyFileRequest};
  struct Continuation {
    Operation Op = Operation::Idle;
    Call Awaiting = Call::None;
    std::uint32_t Index = 0;
    // Release: the buffer the queued archive pointed at, read before its
    // destructor runs.
    std::uint32_t Buffer = 0;
    oot3d::recomp::a32::GuestState Caller{};
  };

  bool HasRegion() const;
  bool HasReleaseQueue() const;
  bool EnsureReleaseQueue(std::string *error);
  std::uint8_t QueuedReleases() const;
  bool Active() const;
  bool NativeSlotsMapped(std::uint32_t play) const;
  // CancelLoading's records: the 19 native slots, then the extra ones.
  std::uint32_t CancelRecord(std::uint32_t index) const;
  bool NextCancel(oot3d::recomp::a32::GuestState &state,
                  oot3d::recomp::a32::ExecutionResult *result, std::uint32_t *blocksConsumed);
  void ClearRecord(std::uint32_t record);
  // A loading record's file request or buffer call returned.
  bool RecordReleased(oot3d::recomp::a32::GuestState &state,
                      oot3d::recomp::a32::ExecutionResult *result, std::uint32_t *blocksConsumed);
  bool NextRelease(oot3d::recomp::a32::GuestState &state,
                   oot3d::recomp::a32::ExecutionResult *result, std::uint32_t *blocksConsumed);
  bool RoomReplace(oot3d::recomp::a32::GuestState &state,
                   oot3d::recomp::a32::ExecutionResult *result, std::uint32_t *blocksConsumed);
  std::uint32_t Play() const;
  std::uint8_t Count() const;
  std::int32_t FindExtra(std::uint32_t objectId);
  std::uint32_t FindSlot(std::uint32_t objectId);
  void MarkRoom(std::uint32_t play);
  bool PrepareSweep();
  void Begin(Operation op, const oot3d::recomp::a32::GuestState &state);
  bool Step(oot3d::recomp::a32::GuestState &state,
            oot3d::recomp::a32::ExecutionResult *result, std::uint32_t *blocksConsumed);
  bool NextFree(oot3d::recomp::a32::GuestState &state,
                oot3d::recomp::a32::ExecutionResult *result, std::uint32_t *blocksConsumed);
  bool Issue(Call call, std::uint32_t entry, std::uint32_t arg0,
             oot3d::recomp::a32::GuestState &state,
             oot3d::recomp::a32::ExecutionResult *result, std::uint32_t *blocksConsumed);
  bool Finish(oot3d::recomp::a32::GuestState &state,
              oot3d::recomp::a32::ExecutionResult *result, std::uint32_t *blocksConsumed);
  std::uint8_t Read8(std::uint32_t address) const;
  std::uint16_t Read16(std::uint32_t address) const;
  std::uint32_t Read32(std::uint32_t address) const;
  void Write8(std::uint32_t address, std::uint8_t value);
  void Write16(std::uint32_t address, std::uint16_t value);
  void Write32(std::uint32_t address, std::uint32_t value);

  NativeA32Memory &mMemory;
  bool mEnabled = false;
  Continuation mContinuation{};
  ObjectBankExtensionStats mStats{};
};

} // namespace Oot3dNativeGame
