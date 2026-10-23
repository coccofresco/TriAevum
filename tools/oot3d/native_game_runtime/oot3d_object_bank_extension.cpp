#include "oot3d_object_bank_extension.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {
using GuestState = oot3d::recomp::a32::GuestState;
using ExecutionResult = oot3d::recomp::a32::ExecutionResult;

constexpr std::uint32_t kBase = kObjectBankExtensionBase;
constexpr std::uint32_t kAges = kBase + 0xA04U;
constexpr std::uint32_t kRetiring = kBase + 0xA18U;
constexpr std::uint32_t kOwner = kBase + 0xA1CU;
constexpr std::uint32_t kSignature = kBase + 0xFF0U;
constexpr std::uint32_t kMagic = 0x424F334FU; // O3OB, outside native context
constexpr std::uint32_t kPlayPointer = 0x005043E0U; // 0x006062F4 -> 0x005043D4 + 0xC
constexpr std::uint32_t kNativeContext = 0x3A58U;
constexpr std::uint32_t kSpawn = 0x0032E21CU;
constexpr std::uint32_t kSpawnAllocated = 0x0032E274U;
constexpr std::uint32_t kResolve = 0x0033AB24U;
constexpr std::uint32_t kResolveResume = 0x0033AB28U;
constexpr std::uint32_t kResolveFailure = 0x0033AB54U;
constexpr std::uint32_t kIndexMiss = 0x00363CACU;
constexpr std::uint32_t kArchive = 0x0034F270U;
constexpr std::uint32_t kLoaded = 0x00373074U;
constexpr std::uint32_t kRoomMark = 0x003A7698U;
constexpr std::uint32_t kRoomFinish = 0x003A7700U;
constexpr std::uint32_t kReady = 0x004849F0U;
constexpr std::uint32_t kSweep = 0x002E4FC8U;
constexpr std::uint32_t kDestroy = 0x0044E778U;
constexpr std::uint32_t kDestroyArchive = 0x002F70C4U;
constexpr std::uint32_t kFree = 0x0034FC6CU;
constexpr std::uint32_t kDestroyContinue = 0x0046053CU;
// SingleScreen 3.1 lifetime fixes. FUN_003a7558 replaces a room's object at
// 0x003A75E0..0x003A762C: with its archive loaded it queues the archive on the
// native deferred list, which holds 19, then resumes at 0x003A763C.
constexpr std::uint32_t kRoomReplace = 0x003A75F0U;       // 3.1 0x00606ADC
constexpr std::uint32_t kRoomReplaceResume = 0x003A763CU;
constexpr std::uint32_t kReleaseDestroy = 0x0044E768U;    // Play_Destroy, 3.1 0x00606B18
constexpr std::uint32_t kReleaseMain = 0x00452430U;       // Play_Main, 3.1 0x00606A78
constexpr std::uint32_t kDestroyFileRequest = 0x0031B99CU; // AsyncFileRequest_Destroy
constexpr std::uint32_t kNativeSlots = 0x3A5CU;
constexpr std::uint32_t kDeferredCount = 0x43DCU;
constexpr std::uint32_t kDeferredPending = 0x43DEU;
constexpr std::uint32_t kNativeDeferredSlots = 19U;
constexpr std::uint32_t kArchiveBuffer = 0x18U;
constexpr std::uint32_t kQueue = kObjectBankReleaseQueueBase;
constexpr std::uint32_t kQueueEntries = kQueue + 0x10U;
constexpr std::uint32_t kQueueSignature = kQueue + 0xFF0U;
constexpr std::uint32_t kQueueMagic = 0x5144334FU; // O3DQ
static_assert(kQueueEntries + kObjectBankReleaseQueueSlots * kObjectBankArchiveBytes <=
              kQueueSignature);

constexpr std::array kLookupSites{
    0x00164BC8U, 0x00167DA4U, 0x00167E38U, 0x00168B20U, 0x0018D7ACU,
    0x001B3130U, 0x001B3208U, 0x001BBC9CU, 0x001F85B0U, 0x00215A60U,
    0x002293ACU, 0x0022AB70U, 0x0022DEC4U, 0x00339DFCU, 0x0033A950U,
    0x0034FE68U, 0x00352F28U, 0x00353CE4U, 0x0036A96CU, 0x00372F7CU};
constexpr std::array kHooks{
    0x00164BC8U, 0x00167DA4U, 0x00167E38U, 0x00168B20U, 0x0018D7ACU,
    0x001B3130U, 0x001B3208U, 0x001BBC9CU, 0x001F85B0U, 0x00215A60U,
    0x002293ACU, 0x0022AB70U, 0x0022DEC4U, kSweep, kSpawn, kSpawnAllocated,
    0x00339DFCU, 0x0033A950U, kResolve, kArchive, 0x0034FE68U,
    0x00352F28U, 0x00353CE4U, kIndexMiss, 0x0036A96CU, 0x00372F7CU,
    kLoaded, kRoomReplace, kRoomMark, kRoomFinish, kReleaseDestroy, kDestroy,
    kReleaseMain, kReady};
constexpr std::array kResumes{
    0x00164BCCU, 0x00167DA8U, 0x00167E3CU, 0x00168B24U, 0x0018D7B0U,
    0x001B3134U, 0x001B320CU, 0x001BBCA0U, 0x001F85B4U, 0x00215A64U,
    0x002293B0U, 0x0022AB74U, 0x0022DEC8U, 0x00339E00U, 0x0033A954U,
    kResolveResume, kResolveFailure, 0x0034F274U, 0x0034FE6CU,
    0x00352F2CU, 0x00353CE8U, 0x0036A970U, 0x00372F80U,
    kRoomReplaceResume, 0x003A76ACU, kRoomFinish, 0x0044E784U, 0x004849FCU};
constexpr std::array kCalls{kDestroyArchive, kSpawn, kDestroyFileRequest, kFree,
                            kDestroyContinue};
static_assert(std::is_sorted(kHooks.begin(), kHooks.end()));

std::uint32_t Add(std::uint32_t address, std::uint32_t offset) {
  if (address > UINT32_MAX - offset)
    throw std::runtime_error("object bank guest address overflow");
  return address + offset;
}

std::uint32_t Extra(std::uint32_t index) {
  if (index >= kObjectBankExtraSlots)
    throw std::runtime_error("object bank extra index out of range");
  return kBase + 4U + index * kObjectBankSlotStride;
}

void Compare(GuestState &state, std::uint32_t left, std::uint32_t right) {
  const auto value = left - right;
  const auto overflow = ((left ^ right) & (left ^ value) & 0x80000000U) != 0U;
  state.cpsr = (state.cpsr & 0x0FFFFFFFU) | (value & 0x80000000U) |
      (value == 0U ? 0x40000000U : 0U) | (left >= right ? 0x20000000U : 0U) |
      (overflow ? 0x10000000U : 0U);
}

bool Branch(std::uint32_t site, std::uint32_t pc, GuestState &state,
            ExecutionResult *result, std::uint32_t *blocksConsumed) {
  state.r[15] = pc;
  if (result) *result = {oot3d::recomp::a32::ExitKind::Branch, pc,
                         oot3d::recomp::a32::FallbackReason::None, site};
  if (blocksConsumed) *blocksConsumed = 1U;
  return true;
}

std::uint32_t AbsoluteId(std::uint16_t bits) {
  const auto value = static_cast<std::int32_t>(std::bit_cast<std::int16_t>(bits));
  return static_cast<std::uint32_t>(value < 0 ? -value : value);
}

nlohmann::json EncodeCaller(const GuestState &state) {
  return {{"r", state.r}, {"vfp", state.vfp}, {"cpsr", state.cpsr},
          {"fpscr", state.fpscr}, {"thread_pointer", state.thread_pointer},
          {"exclusive_address", state.exclusive_address},
          {"exclusive_token", state.exclusive_token},
          {"exclusive_size", state.exclusive_size},
          {"exclusive_valid", state.exclusive_valid}};
}

GuestState DecodeCaller(const nlohmann::json &saved) {
  if (saved.at("r").size() != 16U || saved.at("vfp").size() != 32U)
    throw std::runtime_error("object bank caller register count is invalid");
  GuestState state;
  state.r = saved.at("r").get<decltype(state.r)>();
  state.vfp = saved.at("vfp").get<decltype(state.vfp)>();
  state.cpsr = saved.at("cpsr").get<std::uint32_t>();
  state.fpscr = saved.at("fpscr").get<std::uint32_t>();
  state.thread_pointer = saved.at("thread_pointer").get<std::uint32_t>();
  state.exclusive_address = saved.at("exclusive_address").get<std::uint32_t>();
  state.exclusive_token = saved.at("exclusive_token").get<std::uint64_t>();
  state.exclusive_size = saved.at("exclusive_size").get<std::uint8_t>();
  state.exclusive_valid = saved.at("exclusive_valid").get<bool>();
  return state;
}
} // namespace

std::span<const std::uint32_t> ObjectBankExtension::HookPcs() noexcept { return kHooks; }
std::span<const std::uint32_t> ObjectBankExtension::ResumePcs() noexcept { return kResumes; }
std::span<const std::uint32_t> ObjectBankExtension::NativeCallPcs() noexcept { return kCalls; }

std::uint8_t ObjectBankExtension::Read8(std::uint32_t address) const {
  std::uint8_t value = 0;
  if (!mMemory.Read8(address, &value)) throw std::runtime_error("object bank byte read failed");
  return value;
}
std::uint16_t ObjectBankExtension::Read16(std::uint32_t address) const {
  std::uint16_t value = 0;
  if (!mMemory.Read16(address, &value)) throw std::runtime_error("object bank halfword read failed");
  return value;
}
std::uint32_t ObjectBankExtension::Read32(std::uint32_t address) const {
  std::uint32_t value = 0;
  if (!mMemory.Read32(address, &value)) throw std::runtime_error("object bank word read failed");
  return value;
}
void ObjectBankExtension::Write8(std::uint32_t address, std::uint8_t value) {
  if (!mMemory.Write8(address, value)) throw std::runtime_error("object bank byte write failed");
}
void ObjectBankExtension::Write16(std::uint32_t address, std::uint16_t value) {
  if (!mMemory.Write16(address, value)) throw std::runtime_error("object bank halfword write failed");
}
void ObjectBankExtension::Write32(std::uint32_t address, std::uint32_t value) {
  if (!mMemory.Write32(address, value)) throw std::runtime_error("object bank word write failed");
}

std::uint32_t ObjectBankExtension::SlotAddress(std::uint32_t play, std::int32_t index) noexcept {
  if (index < 0 || index >= 39) return 0;
  if (index >= 19) return kBase + 4U + static_cast<std::uint32_t>(index - 19) * 0x80U;
  const auto offset = 0x3A5CU + static_cast<std::uint32_t>(index) * 0x80U;
  if (play == 0U || play > UINT32_MAX - offset - (kObjectBankSlotStride - 1U)) return 0;
  return play + offset;
}

bool ObjectBankExtension::HasRegion() const {
  std::uint32_t magic = 0, version = 0;
  return mMemory.IsWritable(kBase, kObjectBankExtensionSize) &&
      !mMemory.IsExecutable(kBase, kObjectBankExtensionSize) &&
      mMemory.Read32(kSignature, &magic) && magic == kMagic &&
      mMemory.Read32(kSignature + 4U, &version) && version == 1U;
}

bool ObjectBankExtension::EnsureRegion(std::string *error) {
  if (HasRegion()) return true;
  // MapRegion checks ALL overlap, not just whether the first byte is mapped.
  std::array<std::uint8_t, kObjectBankExtensionSize> bytes{};
  for (unsigned i = 0; i < 4U; ++i)
    bytes[0xFF0U + i] = static_cast<std::uint8_t>(kMagic >> (8U * i));
  bytes[0xFF4U] = 1U;
  return mMemory.MapRegion({"object_bank_extension", kBase, bytes.size(), true, false, bytes}, error);
}

bool ObjectBankExtension::HasReleaseQueue() const {
  std::uint32_t magic = 0, version = 0;
  return mMemory.IsWritable(kQueue, kObjectBankReleaseQueueSize) &&
      !mMemory.IsExecutable(kQueue, kObjectBankReleaseQueueSize) &&
      mMemory.Read32(kQueueSignature, &magic) && magic == kQueueMagic &&
      mMemory.Read32(kQueueSignature + 4U, &version) && version == 1U;
}

bool ObjectBankExtension::EnsureReleaseQueue(std::string *error) {
  if (HasReleaseQueue()) return true;
  std::array<std::uint8_t, kObjectBankReleaseQueueSize> bytes{};
  for (unsigned i = 0; i < 4U; ++i)
    bytes[0xFF0U + i] = static_cast<std::uint8_t>(kQueueMagic >> (8U * i));
  bytes[0xFF4U] = 1U;
  return mMemory.MapRegion({"object_bank_release_queue", kQueue, bytes.size(), true, false, bytes},
                           error);
}

std::uint8_t ObjectBankExtension::QueuedReleases() const {
  if (!HasReleaseQueue()) return 0U;
  const auto count = Read8(kQueue);
  if (count > kObjectBankReleaseQueueSlots)
    throw std::runtime_error("object bank release queue count is invalid");
  return count;
}

std::uint32_t ObjectBankExtension::Play() const { return Read32(kPlayPointer); }
std::uint8_t ObjectBankExtension::Count() const {
  const auto count = Read8(kBase);
  if (count > kObjectBankExtraSlots || Read8(kBase + 1U) > count)
    throw std::runtime_error("object bank counts are invalid");
  return count;
}
bool ObjectBankExtension::Pending() const noexcept {
  return mContinuation.Op != Operation::Idle;
}
bool ObjectBankExtension::Active() const {
  return mEnabled || Pending() || (HasRegion() && Count() != 0U);
}
bool ObjectBankExtension::ShouldObserve(std::uint32_t pc) const {
  if (!std::binary_search(kHooks.begin(), kHooks.end(), pc)) return false;
  if (Pending()) return true;
  // The lifetime fixes do not depend on the extension.
  if (pc == kRoomReplace || pc == kDestroy) return true;
  if (pc == kReleaseMain || pc == kReleaseDestroy) return QueuedReleases() != 0U;
  return Active();
}

std::int32_t ObjectBankExtension::FindExtra(std::uint32_t objectId) {
  if (!HasRegion()) return -1;
  const auto count = Count();
  for (std::uint32_t i = 0; i < count; ++i) {
    if (objectId == AbsoluteId(Read16(Extra(i)))) {
      // 0x00606278: a new lookup cancels retirement of this object.
      Write8(kAges + i, 0U);
      ++mStats.ExtraHits;
      return static_cast<std::int32_t>(i + kObjectBankNativeSlots);
    }
  }
  return -1;
}

std::uint32_t ObjectBankExtension::FindSlot(std::uint32_t objectId) {
  const auto play = Play();
  if (!play) throw std::runtime_error("object bank has no PlayState");
  const auto context = Add(play, kNativeContext);
  const auto count = Read8(context);
  if (count > kObjectBankNativeSlots)
    throw std::runtime_error("native object bank count exceeds nineteen");
  // 0x006062F8 calls native Object_GetIndex, which searches absolute signed
  // IDs and then falls through to 0x0060623C. No native call is needed to scan.
  for (std::uint32_t i = 0; i < count; ++i)
    if (objectId == AbsoluteId(Read16(Add(context, 4U + i * 0x80U))))
      return SlotAddress(play, static_cast<std::int32_t>(i));
  const auto index = FindExtra(objectId);
  return index < 0 ? 0U : SlotAddress(play, index);
}

void ObjectBankExtension::MarkRoom(std::uint32_t play) {
  // 0x00606040: only room phase 2 retires the non-persistent suffix.
  if (Read8(Add(play, 0x101U)) != 2U) return;
  std::string error;
  if (!EnsureRegion(&error)) throw std::runtime_error(error);
  const auto count = Count();
  bool marked = false;
  for (std::uint32_t i = Read8(kBase + 1U); i < count; ++i) {
    if (Read16(Extra(i)) != 0U) {
      Write8(kAges + i, 1U);
      marked = true;
    }
  }
  Write32(kOwner, play);
  if (marked) Write8(kRetiring, 1U);
  ++mStats.RoomMarks;
}

bool ObjectBankExtension::PrepareSweep() {
  // 0x006060A8, called once per logic update by the 3.0 gameplay hub.
  if (!HasRegion() || Read8(kRetiring) == 0U) return false;
  const auto play = Play();
  if (!play || Read32(kOwner) != play || Read8(Add(play, 0x101U)) != 2U) return false;
  const auto count = Count();
  std::array<bool, kObjectBankExtraSlots> live{};
  std::uint32_t visited = 0;
  for (std::uint32_t category = 0; category < 12U; ++category) {
    auto actor = Read32(Add(play, 0x209CU + category * 8U));
    while (actor != 0U) {
      if (++visited > 65536U) throw std::runtime_error("object bank actor list is cyclic");
      const auto index = static_cast<std::int32_t>(std::bit_cast<std::int8_t>(
          Read8(Add(actor, 0x1EU)))) - 19;
      if (index >= 0 && index < 20) live[static_cast<std::size_t>(index)] = true;
      actor = Read32(Add(actor, 0x130U));
    }
  }
  for (std::uint32_t i = Read8(kBase + 1U); i < count; ++i) {
    const auto age = Read8(kAges + i);
    if (age != 0U) Write8(kAges + i, live[i] ? 1U :
        static_cast<std::uint8_t>(age == 255U ? age : age + 1U));
  }
  ++mStats.Sweeps;
  return true;
}

void ObjectBankExtension::Begin(Operation op, const GuestState &state) {
  if (Pending()) throw std::runtime_error("object bank continuation is already active");
  if (state.r[13] < 0x20U || !mMemory.IsWritable(state.r[13] - 0x20U, 0x20U))
    throw std::runtime_error("object bank native call has no caller stack");
  mContinuation = {op, Call::None, 0U, 0U, state};
}

bool ObjectBankExtension::Issue(Call call, std::uint32_t entry, std::uint32_t arg0,
                                GuestState &state, ExecutionResult *result,
                                std::uint32_t *blocksConsumed) {
  mContinuation.Awaiting = call;
  state = mContinuation.Caller;
  state.r[0] = arg0;
  state.r[13] -= 0x20U;
  state.r[14] = mContinuation.Caller.r[15];
  ++mStats.NativeCalls;
  return Branch(state.r[14], entry, state, result, blocksConsumed);
}

bool ObjectBankExtension::NextFree(GuestState &state, ExecutionResult *result,
                                   std::uint32_t *blocksConsumed) {
  if (mContinuation.Op == Operation::Destroy) {
    // 0x00605FA0 scans all twenty records, as native Play_Destroy does for 19.
    while (mContinuation.Index < kObjectBankExtraSlots &&
           std::bit_cast<std::int16_t>(Read16(Extra(mContinuation.Index))) <= 0)
      ++mContinuation.Index;
    if (mContinuation.Index == kObjectBankExtraSlots)
      return Finish(state, result, blocksConsumed);
  } else {
    const auto count = Count();
    if (count <= Read8(kBase + 1U) || Read8(kAges + count - 1U) <= 3U)
      return Finish(state, result, blocksConsumed);
    mContinuation.Index = count - 1U;
  }
  const auto slot = Extra(mContinuation.Index);
  if (Read32(slot + 8U) != 0U)
    return Issue(Call::DestroyArchive, kDestroyArchive, slot + 0x10U,
                 state, result, blocksConsumed);
  return Issue(Call::FreeBuffer, kFree, Read32(slot + 4U), state, result, blocksConsumed);
}

bool ObjectBankExtension::NativeSlotsMapped(std::uint32_t play) const {
  constexpr auto bytes = kObjectBankNativeSlots * kObjectBankSlotStride;
  return play != 0U && play <= UINT32_MAX - kNativeSlots - bytes &&
      mMemory.IsWritable(play + kNativeSlots, bytes);
}

std::uint32_t ObjectBankExtension::CancelRecord(std::uint32_t index) const {
  // 3.1 0x006069F4 at scene teardown (r5 = PlayState): native, then extra.
  if (index < kObjectBankNativeSlots)
    return Add(mContinuation.Caller.r[5], kNativeSlots + index * kObjectBankSlotStride);
  return Extra(index - kObjectBankNativeSlots);
}

void ObjectBankExtension::ClearRecord(std::uint32_t record) {
  Write32(Add(record, 0xCU), 0U);
  Write32(Add(record, 4U), 0U);
  Write32(Add(record, 8U), 0U);
  Write16(record, 0U);
  ++mStats.LoadingCancels;
}

bool ObjectBankExtension::NextCancel(GuestState &state, ExecutionResult *result,
                                     std::uint32_t *blocksConsumed) {
  // 3.1 0x00606960: a record still loading (negative id) has its file
  // request destroyed, which waits for the read, before its buffer is freed.
  const auto limit = kObjectBankNativeSlots + (HasRegion() ? kObjectBankExtraSlots : 0U);
  for (; mContinuation.Index < limit; ++mContinuation.Index) {
    const auto record = CancelRecord(mContinuation.Index);
    if (std::bit_cast<std::int16_t>(Read16(record)) >= 0) continue;
    if (const auto request = Read32(Add(record, 0xCU)); request != 0U)
      return Issue(Call::DestroyFileRequest, kDestroyFileRequest, request,
                   state, result, blocksConsumed);
    if (const auto buffer = Read32(Add(record, 4U)); buffer != 0U)
      return Issue(Call::FreeBuffer, kFree, buffer, state, result, blocksConsumed);
    ClearRecord(record);
  }
  if (HasRegion()) {
    // Then 3.0's teardown of the ready extra records (0x00605FA0).
    mContinuation.Op = Operation::Destroy;
    mContinuation.Awaiting = Call::None;
    mContinuation.Index = 0U;
    return NextFree(state, result, blocksConsumed);
  }
  state = mContinuation.Caller;
  mContinuation = {};
  return false; // the native block: two NOPs, then its call
}

bool ObjectBankExtension::RecordReleased(GuestState &state, ExecutionResult *result,
                                         std::uint32_t *blocksConsumed) {
  const bool room = mContinuation.Op == Operation::RoomReplace;
  const auto record = room ? mContinuation.Caller.r[4] : CancelRecord(mContinuation.Index);
  if (mContinuation.Awaiting == Call::DestroyFileRequest) {
    Write32(Add(record, 0xCU), 0U);
    if (const auto buffer = Read32(Add(record, 4U)); buffer != 0U)
      return Issue(Call::FreeBuffer, kFree, buffer, state, result, blocksConsumed);
  } else if (mContinuation.Awaiting != Call::FreeBuffer) {
    throw std::runtime_error("object bank record release has no native call");
  }
  ClearRecord(record);
  if (!room) {
    ++mContinuation.Index;
    return NextCancel(state, result, blocksConsumed);
  }
  const auto site = mContinuation.Caller.r[15];
  state = mContinuation.Caller;
  mContinuation = {};
  return Branch(site, kRoomReplaceResume, state, result, blocksConsumed);
}

bool ObjectBankExtension::NextRelease(GuestState &state, ExecutionResult *result,
                                      std::uint32_t *blocksConsumed) {
  // 3.1 0x00606A78, beside the native list's release in Play_Main and
  // Play_Destroy: destroy each queued archive, then free its buffer.
  if (mContinuation.Index < QueuedReleases()) {
    const auto entry = kQueueEntries + mContinuation.Index * kObjectBankArchiveBytes;
    mContinuation.Buffer = Read32(entry + kArchiveBuffer);
    return Issue(Call::DestroyArchive, kDestroyArchive, entry, state, result, blocksConsumed);
  }
  Write8(kQueue, 0U);
  state = mContinuation.Caller;
  mContinuation = {};
  return false; // the native block
}

bool ObjectBankExtension::RoomReplace(GuestState &state, ExecutionResult *result,
                                      std::uint32_t *blocksConsumed) {
  // r4: the room slot being replaced, its archive loaded; r9: PlayState.
  const auto record = state.r[4];
  const auto play = state.r[9];
  if (record > UINT32_MAX - kObjectBankSlotStride ||
      !mMemory.IsWritable(record, kObjectBankSlotStride) ||
      play > UINT32_MAX - kDeferredPending - 2U ||
      !mMemory.IsWritable(play + kDeferredCount, 4U))
    return false; // not the native PlayState replacement: leave it native
  if (const auto request = Read32(Add(record, 0xCU)); request != 0U) {
    // 3.1 0x006069BC: its file request is still open; destroy it, then free
    // the buffer, instead of queueing the archive.
    Begin(Operation::RoomReplace, state);
    return Issue(Call::DestroyFileRequest, kDestroyFileRequest, request,
                 state, result, blocksConsumed);
  }
  if (Read16(Add(play, kDeferredCount)) < kNativeDeferredSlots) return false;
  // 3.1 0x00606A1C: the native list is full; queue the archive in the
  // overflow page. A full queue drops it, as 3.1 does.
  std::string error;
  if (!EnsureReleaseQueue(&error)) throw std::runtime_error(error);
  const auto count = QueuedReleases();
  if (count < kObjectBankReleaseQueueSlots) {
    const auto entry = kQueueEntries + count * kObjectBankArchiveBytes;
    for (std::uint32_t offset = 0; offset < kObjectBankArchiveBytes; offset += 4U)
      Write32(entry + offset, Read32(Add(record, 0x10U + offset)));
    Write8(kQueue, static_cast<std::uint8_t>(count + 1U));
    ++mStats.DeferredQueued;
  }
  Write16(Add(play, kDeferredPending), 1U);
  Write32(Add(record, 4U), 0U);
  Write32(Add(record, 8U), 0U);
  return Branch(kRoomReplace, kRoomReplaceResume, state, result, blocksConsumed);
}

bool ObjectBankExtension::Step(GuestState &state, ExecutionResult *result,
                               std::uint32_t *blocksConsumed) {
  const auto site = mContinuation.Caller.r[15];
  if (state.r[13] != mContinuation.Caller.r[13] - 0x20U ||
      state.thread_pointer != mContinuation.Caller.thread_pointer)
    throw std::runtime_error("object bank re-entry caller does not match");
  if (mContinuation.Op == Operation::CancelLoading || mContinuation.Op == Operation::RoomReplace)
    return RecordReleased(state, result, blocksConsumed);
  if (mContinuation.Op == Operation::Release) {
    if (mContinuation.Awaiting == Call::DestroyArchive)
      return Issue(Call::FreeBuffer, kFree, mContinuation.Buffer, state, result, blocksConsumed);
    if (mContinuation.Awaiting != Call::FreeBuffer)
      throw std::runtime_error("object bank release has no native call");
    ++mStats.DeferredReleases;
    ++mContinuation.Index;
    return NextRelease(state, result, blocksConsumed);
  }
  const auto slot = Extra(mContinuation.Index);
  if (mContinuation.Awaiting == Call::Spawn) {
    const auto returned = std::bit_cast<std::int32_t>(state.r[0]);
    const bool success = returned == static_cast<std::int32_t>(mContinuation.Index) &&
        Count() == mContinuation.Index + 1U &&
        std::bit_cast<std::int16_t>(Read16(slot)) > 0 && Read32(slot + 0xCU) == 0U;
    state = mContinuation.Caller;
    mContinuation = {};
    if (!success) {
      ++mStats.LoadFailures;
      // 3.0's 0x00606390 computes a pre-bank pointer after a -1 spawn.
      // Return through the native loader's null-result epilogue instead.
      state.r[0] = 0U;
      return Branch(site, kResolveFailure, state, result, blocksConsumed);
    }
    Write8(kAges + static_cast<std::uint32_t>(returned), 0U);
    ++mStats.Loads;
    state.r[0] = slot;
    state.r[14] = kResolveResume;
    return Branch(site, kResolveResume, state, result, blocksConsumed);
  }
  if (mContinuation.Awaiting == Call::DestroyArchive) {
    Write32(slot + 8U, 0U);
    return Issue(Call::FreeBuffer, kFree, Read32(slot + 4U), state, result, blocksConsumed);
  }
  if (mContinuation.Awaiting != Call::FreeBuffer)
    throw std::runtime_error("object bank continuation has no native call");
  Write32(slot + 4U, 0U);
  Write16(slot, 0U);
  Write8(kAges + mContinuation.Index, 0U);
  ++mStats.Frees;
  if (mContinuation.Op == Operation::Sweep)
    Write8(kBase, static_cast<std::uint8_t>(mContinuation.Index));
  else
    ++mContinuation.Index;
  return NextFree(state, result, blocksConsumed);
}

bool ObjectBankExtension::Finish(GuestState &state, ExecutionResult *result,
                                 std::uint32_t *blocksConsumed) {
  const auto op = mContinuation.Op;
  state = mContinuation.Caller;
  const auto site = state.r[15];
  if (op == Operation::Destroy) {
    Write32(kBase, 0U);
    for (std::uint32_t i = 0; i < kObjectBankExtraSlots; ++i) Write8(kAges + i, 0U);
    Write8(kRetiring, 0U);
    Write32(kOwner, 0U);
    mContinuation = {};
    // 0x0044E778/77C are NOPs in native code. Emulate the following BL so
    // the resume is the natural call return at 0x0044E784.
    state.r[14] = 0x0044E784U;
    ++mStats.NativeCalls;
    return Branch(site, kDestroyContinue, state, result, blocksConsumed);
  }
  bool retiring = false;
  const auto count = Count();
  for (std::uint32_t i = Read8(kBase + 1U); i < count; ++i)
    retiring |= Read8(kAges + i) != 0U;
  Write8(kRetiring, retiring ? 1U : 0U);
  // Native Object_UpdateBank epilogue 0x002E4FC8: ADD sp,#0x214;
  // POP {r4-r11,pc}. Read the whole frame before committing register changes.
  auto restored = state;
  auto stack = Add(state.r[13], 0x214U);
  for (unsigned reg = 4; reg <= 11; ++reg) {
    restored.r[reg] = Read32(stack);
    stack = Add(stack, 4U);
  }
  const auto resume = Read32(stack);
  restored.r[13] = Add(stack, 4U);
  mContinuation = {};
  state = restored;
  return Branch(site, resume, state, result, blocksConsumed);
}

bool ObjectBankExtension::Execute(std::uint32_t pc, GuestState &state,
                                  ExecutionResult *result, std::uint32_t *blocksConsumed) {
  if (!ShouldObserve(pc)) return false;
  if (Pending() && pc == mContinuation.Caller.r[15])
    return Step(state, result, blocksConsumed);
  if (std::binary_search(kLookupSites.begin(), kLookupSites.end(), pc) || pc == kArchive) {
    const auto index = std::bit_cast<std::int32_t>(state.r[0]);
    if (index >= 19 && index < 39) {
      std::string error;
      if (!EnsureRegion(&error)) throw std::runtime_error(error);
    }
    Compare(state, state.r[0], index <= 18 ? 0U : 38U);
    state.r[0] = SlotAddress(index >= 0 && index < 19 ? Play() : 0U, index);
    ++mStats.Lookups;
    if (pc != kArchive) state.r[14] = pc + 4U;
    return Branch(pc, pc + 4U, state, result, blocksConsumed);
  }
  if (pc == kSpawn) {
    const auto limit = state.r[0] == kBase ? 20U : 19U;
    if (Read8(state.r[0]) < limit) return false; // execute the native prologue/body
    state.r[0] = UINT32_MAX;
    return Branch(pc, state.r[14], state, result, blocksConsumed);
  }
  if (pc == kSpawnAllocated) {
    if (state.r[0] != 0U) return false;
    // 0x005F4018's OOM path, intercepted after the ORIGINAL allocator call.
    const auto index = Read8(state.r[4]);
    const auto limit = state.r[4] == kBase ? 20U : 19U;
    if (index >= limit) throw std::runtime_error("object bank failed allocation index is invalid");
    const auto slotContext = Add(state.r[4], static_cast<std::uint32_t>(index) * 0x80U);
    auto restored = state;
    auto stack = Add(state.r[13], 0x210U);
    for (unsigned reg = 4; reg <= 8; ++reg) {
      restored.r[reg] = Read32(stack);
      stack = Add(stack, 4U);
    }
    const auto resume = Read32(stack);
    restored.r[13] = Add(stack, 4U);
    restored.r[0] = UINT32_MAX;
    restored.r[1] = slotContext;
    restored.r[2] = 0U;
    Compare(restored, 0U, 0U);
    Write16(Add(slotContext, 4U), 0U);
    Write32(Add(slotContext, 0xCU), 0U);
    state = restored;
    return Branch(pc, resume, state, result, blocksConsumed);
  }
  if (pc == kResolve) {
    const auto table = Read32(Add(state.r[4], 4U));
    if (state.r[5] > UINT32_MAX / 8U) throw std::runtime_error("object bank model index overflow");
    const auto objectId = Read32(Add(table, state.r[5] * 8U));
    const auto slot = FindSlot(objectId);
    if (slot) {
      state.r[0] = slot;
      state.r[14] = kResolveResume;
      return Branch(pc, kResolveResume, state, result, blocksConsumed);
    }
    std::string error;
    if (!mEnabled || objectId == 0U || objectId > 0x7FFFU) {
      ++mStats.LoadFailures;
      state.r[0] = 0U;
      return Branch(pc, kResolveFailure, state, result, blocksConsumed);
    }
    if (!EnsureRegion(&error)) throw std::runtime_error(error);
    const auto count = Count();
    if (count == kObjectBankExtraSlots) {
      ++mStats.LoadFailures;
      state.r[0] = 0U;
      return Branch(pc, kResolveFailure, state, result, blocksConsumed);
    }
    Begin(Operation::Load, state);
    mContinuation.Index = count;
    Issue(Call::Spawn, kSpawn, kBase, state, result, blocksConsumed);
    state.r[1] = objectId;
    state.r[2] = Play();
    return true;
  }
  if (pc == kIndexMiss) {
    // The patch at 0x00363CB4 is mid-block. Its block starts at POP r4.
    const auto savedR4 = Read32(state.r[13]);
    const auto stack = Add(state.r[13], 4U);
    const auto index = FindExtra(state.r[1]);
    state.r[4] = savedR4;
    state.r[13] = stack;
    state.r[0] = static_cast<std::uint32_t>(index);
    return Branch(pc, state.r[14], state, result, blocksConsumed);
  }
  if (pc == kLoaded || pc == kReady) {
    // 0x004849F8's BL is inside the block starting at 0x004849F0.
    auto context = state.r[0];
    auto index = std::bit_cast<std::int32_t>(state.r[1]);
    if (pc == kReady) {
      index = std::bit_cast<std::int8_t>(Read8(Add(state.r[4], 0x1EU)));
      context = state.r[7];
      state.r[1] = static_cast<std::uint32_t>(index);
    }
    std::uint32_t slot = 0U;
    if (index >= 19 && index < 39) {
      std::string error;
      if (!EnsureRegion(&error)) throw std::runtime_error(error);
      slot = Extra(static_cast<std::uint32_t>(index - 19));
    } else if (index >= 0 && index < 19) {
      slot = Add(context, 4U + static_cast<std::uint32_t>(index) * 0x80U);
    }
    const auto id = slot ? std::bit_cast<std::int16_t>(Read16(slot)) : -1;
    if (pc == kLoaded) {
      Compare(state, static_cast<std::uint32_t>(id), 0U);
      state.r[0] = id > 0 ? 1U : 0U;
      return Branch(pc, state.r[14], state, result, blocksConsumed);
    }
    state.r[0] = id >= 0 ? 1U : 0U; // zero is ready at 0x00606438
    state.r[14] = 0x004849FCU;
    return Branch(pc, 0x004849FCU, state, result, blocksConsumed);
  }
  if (pc == kRoomMark) {
    MarkRoom(state.r[9]);
    // Emulate 0x003A7698..A8 through the conditional branch. +4 is NOT a block start.
    const auto command = Read32(Add(state.r[13], 0x18U));
    state.r[1] = Read8(Add(command, 1U));
    state.r[0] = Read32(Add(state.r[13], 4U));
    Compare(state, state.r[1], state.r[0]);
    const auto resume = std::bit_cast<std::int32_t>(state.r[1]) <=
        std::bit_cast<std::int32_t>(state.r[0]) ? kRoomFinish : 0x003A76ACU;
    return Branch(pc, resume, state, result, blocksConsumed);
  }
  if (pc == kRoomFinish) {
    // 0x00606024 snapshots scene-persistent extras at scene command phase 1.
    const auto phase = Read8(Add(state.r[9], 0x101U));
    const auto count = HasRegion() ? Count() : 0U;
    auto restored = state;
    auto stack = Add(state.r[13], 0x1CU);
    for (unsigned reg = 4; reg <= 11; ++reg) {
      restored.r[reg] = Read32(stack);
      stack = Add(stack, 4U);
    }
    const auto resume = Read32(stack);
    restored.r[13] = Add(stack, 4U);
    restored.r[0] = 1U;
    Write8(Add(state.r[8], 0xA58U), static_cast<std::uint8_t>(state.r[10]));
    if (phase == 1U && HasRegion()) Write8(kBase + 1U, static_cast<std::uint8_t>(count));
    state = restored;
    return Branch(pc, resume, state, result, blocksConsumed);
  }
  if (pc == kSweep) {
    // Only the native PlayState bank, not unrelated ObjectContext instances.
    if (!HasRegion() || state.r[8] != Add(Play(), kNativeContext) || !PrepareSweep())
      return false;
    Begin(Operation::Sweep, state);
    return NextFree(state, result, blocksConsumed);
  }
  if (pc == kDestroy) {
    bool loading = false;
    const bool native = NativeSlotsMapped(state.r[5]);
    for (std::uint32_t i = native ? 0U : kObjectBankNativeSlots;
         i < kObjectBankNativeSlots + (HasRegion() ? kObjectBankExtraSlots : 0U) && !loading; ++i) {
      const auto record = i < kObjectBankNativeSlots ?
          state.r[5] + kNativeSlots + i * kObjectBankSlotStride : Extra(i - kObjectBankNativeSlots);
      loading = std::bit_cast<std::int16_t>(Read16(record)) < 0;
    }
    if (loading) {
      Begin(Operation::CancelLoading, state);
      mContinuation.Index = native ? 0U : kObjectBankNativeSlots;
      return NextCancel(state, result, blocksConsumed);
    }
    if (!HasRegion()) return false;
    Begin(Operation::Destroy, state);
    return NextFree(state, result, blocksConsumed);
  }
  if (pc == kRoomReplace) return RoomReplace(state, result, blocksConsumed);
  if (pc == kReleaseMain || pc == kReleaseDestroy) {
    if (QueuedReleases() == 0U) return false;
    Begin(Operation::Release, state);
    return NextRelease(state, result, blocksConsumed);
  }
  return false;
}

void ObjectBankExtension::Reset() noexcept {
  mContinuation = {};
  mStats = {};
}

nlohmann::json ObjectBankExtension::CaptureState() const {
  return {{"format", "oot3d_object_bank_extension_v1"}, {"region", HasRegion()},
          {"operation", static_cast<unsigned>(mContinuation.Op)},
          {"awaiting", static_cast<unsigned>(mContinuation.Awaiting)},
          {"index", mContinuation.Index}, {"buffer", mContinuation.Buffer},
          {"caller", EncodeCaller(mContinuation.Caller)},
          {"stats", {mStats.Lookups, mStats.ExtraHits, mStats.Loads, mStats.LoadFailures,
                     mStats.Frees, mStats.RoomMarks, mStats.Sweeps, mStats.NativeCalls,
                     mStats.LoadingCancels, mStats.DeferredQueued, mStats.DeferredReleases}}};
}

bool ObjectBankExtension::RestoreState(const nlohmann::json &saved, std::string *error) {
  try {
    if (saved.at("format") != "oot3d_object_bank_extension_v1" ||
        saved.at("region").get<bool>() != HasRegion())
      throw std::runtime_error("object bank snapshot does not match guest memory");
    const auto op = saved.at("operation").get<unsigned>();
    const auto call = saved.at("awaiting").get<unsigned>();
    if (op > 6U || call > 4U) throw std::runtime_error("object bank snapshot phase is invalid");
    // Snapshots before the 3.1 lifetime fixes have no buffer and 8 counters.
    Continuation next{static_cast<Operation>(op), static_cast<Call>(call),
                      saved.at("index").get<std::uint32_t>(),
                      saved.value("buffer", std::uint32_t{0}), DecodeCaller(saved.at("caller"))};
    if (HasRegion()) {
      Count();
      if (Read8(kRetiring) > 1U || (Read8(kRetiring) != 0U && Read32(kOwner) == 0U))
        throw std::runtime_error("object bank retirement metadata is invalid");
    }
    QueuedReleases();
    if (next.Op == Operation::Idle) {
      if (next.Awaiting != Call::None || next.Index != 0U)
        throw std::runtime_error("idle object bank snapshot has a continuation");
    } else if (next.Op == Operation::CancelLoading || next.Op == Operation::RoomReplace ||
               next.Op == Operation::Release) {
      const bool release = next.Op == Operation::Release;
      const bool hook = next.Op == Operation::CancelLoading ? next.Caller.r[15] == kDestroy :
          next.Op == Operation::RoomReplace ? next.Caller.r[15] == kRoomReplace :
          next.Caller.r[15] == kReleaseMain || next.Caller.r[15] == kReleaseDestroy;
      const auto limit = next.Op == Operation::CancelLoading ?
          kObjectBankNativeSlots + (HasRegion() ? kObjectBankExtraSlots : 0U) :
          next.Op == Operation::RoomReplace ? 1U : QueuedReleases();
      if (!hook || next.Index >= limit || next.Caller.r[13] < 0x20U ||
          !mMemory.IsWritable(next.Caller.r[13] - 0x20U, 0x20U) ||
          (release ? next.Awaiting != Call::DestroyArchive && next.Awaiting != Call::FreeBuffer :
              next.Awaiting != Call::DestroyFileRequest && next.Awaiting != Call::FreeBuffer))
        throw std::runtime_error("object bank snapshot continuation is invalid");
    } else {
      const auto hook = next.Op == Operation::Load ? kResolve :
          next.Op == Operation::Sweep ? kSweep : kDestroy;
      if (!HasRegion() || next.Index >= kObjectBankExtraSlots || next.Caller.r[15] != hook ||
          next.Caller.r[13] < 0x20U ||
          !mMemory.IsWritable(next.Caller.r[13] - 0x20U, 0x20U) ||
          (next.Op == Operation::Load ? next.Awaiting != Call::Spawn :
              next.Awaiting != Call::DestroyArchive && next.Awaiting != Call::FreeBuffer))
        throw std::runtime_error("object bank snapshot continuation is invalid");
      if (next.Op == Operation::Load && Count() != next.Index && Count() != next.Index + 1U)
        throw std::runtime_error("object bank snapshot allocation count is invalid");
      if (next.Op == Operation::Sweep &&
          (Count() != next.Index + 1U || next.Index < Read8(kBase + 1U)))
        throw std::runtime_error("object bank snapshot retirement index is invalid");
    }
    const auto &stats = saved.at("stats");
    if (!stats.is_array() || (stats.size() != 8U && stats.size() != 11U))
      throw std::runtime_error("object bank snapshot counters are invalid");
    const auto counter = [&](std::size_t index) {
      return index < stats.size() ? stats[index].get<std::uint64_t>() : std::uint64_t{0};
    };
    ObjectBankExtensionStats decoded{counter(0), counter(1), counter(2), counter(3),
                                     counter(4), counter(5), counter(6), counter(7),
                                     counter(8), counter(9), counter(10)};
    mContinuation = next;
    mStats = decoded;
    return true;
  } catch (const std::exception &exception) {
    if (error) *error = exception.what();
    return false;
  }
}

} // namespace Oot3dNativeGame
