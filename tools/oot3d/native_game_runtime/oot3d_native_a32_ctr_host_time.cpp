// CTR host services: the RTC published in the shared page (0x1FF81000).
//
// The title reads the date only through nn::fnd::DateTime::GetNow (EUR
// 004538D8): svcGetSystemTick, then the DateTime slot selected by bit 0 of
// date_time_counter (0x1FF81000) at 0x1FF81020 + slot * 0x20:
//   +0x00 u64 date_time    ms since 1900-01-01 00:00, local time
//   +0x08 u64 update_tick  system tick at which date_time was sampled
//   +0x10 u64 tick_to_second_coefficient  (268111856, the ARM11 clock)
//   +0x18 u64 tick_offset  (0)
// and extrapolates date_time + (tick - update_tick) / coefficient. The
// kernel/PTM write the inactive slot and then bump the counter; Azahar's
// SharedPage::Handler::UpdateTimeCallback follows the same protocol.
//
// Epoch: GetNow converts to its own DateTime (ms since 2000-01-01) by
// taking date_time modulo DateTime(2100,1,1) - DateTime(2000,1,1), i.e.
// 36525 days (00453B08, remainder of __aeabi_ldivmod). The "1900" of the
// shared page is therefore 36525 days before 2000-01-01 (100 Julian
// years), not the Gregorian 36524. Azahar's 3155673600000 base makes this
// title read the previous day (verified by running 004538D8, 0044A4AC and
// the SDK year/month/day getters under Unicorn); 3155760000000 is exact.
#include "oot3d_native_a32_ctr_host_internal.h"

#include <ctime>

#if defined(__SWITCH__)
// Only libnx's umbrella <switch.h> wraps its C headers in extern "C".
extern "C" {
#include <switch/services/time.h>
}
#endif

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kSharedPageAddress = 0x1FF81000U;
constexpr uint32_t kSharedPageDateTimeCounter = kSharedPageAddress + 0x00U;
constexpr uint32_t kSharedPageDateTimeSlots = kSharedPageAddress + 0x20U;
constexpr uint32_t kSharedPageDateTimeSlotSize = 0x20U;
// CTR page epoch to the Unix epoch: 36525 days to 2000-01-01 (see above)
// minus the 10957 days from 1970-01-01 to 2000-01-01.
constexpr int64_t kUnixEpochFromCtrEpochSeconds =
    36525LL * 86400LL - 946684800LL;
static_assert(kUnixEpochFromCtrEpochSeconds == 2209075200LL);

uint64_t TicksToMilliseconds(uint64_t ticks) {
    return (ticks / kCtrArm11TicksPerSecond) * 1000ULL +
           (ticks % kCtrArm11TicksPerSecond) * 1000ULL /
               kCtrArm11TicksPerSecond;
}

// Seconds east of UTC of the host time zone at the given UTC instant.
int64_t HostUtcOffsetSeconds(int64_t utcSeconds) {
#if defined(__SWITCH__)
    // newlib's localtime has no zone on Horizon (TZ is unset); the
    // console's zone rule lives in the time service.
    TimeCalendarTime calendar{};
    TimeCalendarAdditionalInfo info{};
    if (R_SUCCEEDED(timeToCalendarTimeWithMyRule(
            static_cast<u64>(utcSeconds), &calendar, &info))) {
        return info.offset;
    }
    return 0;
#elif defined(_WIN32)
    const std::time_t value = static_cast<std::time_t>(utcSeconds);
    std::tm local{};
    if (localtime_s(&local, &value) != 0) {
        return 0;
    }
    return static_cast<int64_t>(_mkgmtime(&local)) - utcSeconds;
#else
    const std::time_t value = static_cast<std::time_t>(utcSeconds);
    std::tm local{};
    if (localtime_r(&value, &local) == nullptr) {
        return 0;
    }
    return static_cast<int64_t>(local.tm_gmtoff);
#endif
}

// The host's local wall clock in CTR form. On Switch, libnx seeds newlib's
// clock from the user system clock at boot and advances it by the tick.
uint64_t HostLocalDateTimeMilliseconds() {
    const auto now = std::chrono::system_clock::now();
    const int64_t utcMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch())
            .count();
    const int64_t utcSeconds = utcMilliseconds >= 0
                                   ? utcMilliseconds / 1000
                                   : (utcMilliseconds - 999) / 1000;
    const int64_t local = utcMilliseconds +
                          (HostUtcOffsetSeconds(utcSeconds) +
                           kUnixEpochFromCtrEpochSeconds) *
                              1000;
    return local > 0 ? static_cast<uint64_t>(local) : 0U;
}

} // namespace

uint64_t NativeA32CtrHostServices::CtrDateTimeMilliseconds() const {
    if (mConfig.FixedDateTimeMilliseconds.has_value()) {
        return *mConfig.FixedDateTimeMilliseconds +
               TicksToMilliseconds(mSystemTicks);
    }
    return HostLocalDateTimeMilliseconds();
}

bool NativeA32CtrHostServices::RefreshSharedPageDateTime(
    NativeA32Memory& memory, bool force) {
    if (!force && mDateTimePublished &&
        mSystemTicks < mNextDateTimeUpdateTick) {
        return true;
    }
    uint32_t counter = 0;
    if (!memory.Read32(kSharedPageDateTimeCounter, &counter)) {
        return false;
    }
    const uint32_t nextCounter = counter + 1U;
    const uint32_t slot = kSharedPageDateTimeSlots +
                          (nextCounter & 1U) * kSharedPageDateTimeSlotSize;
    if (!memory.WriteHost<uint64_t>(slot + 0x00U, CtrDateTimeMilliseconds()) ||
        !memory.WriteHost<uint64_t>(slot + 0x08U, mSystemTicks) ||
        !memory.WriteHost<uint64_t>(slot + 0x10U, kCtrArm11TicksPerSecond) ||
        !memory.WriteHost<uint64_t>(slot + 0x18U, 0U) ||
        !memory.WriteHost<uint32_t>(kSharedPageDateTimeCounter, nextCounter)) {
        return false;
    }
    // Hardware refreshes far less often; once per emulated second keeps the
    // extrapolated date within a second of the host clock even when the
    // virtual clock drifts (frame pacing, suspend) without host calls on
    // every GetSystemTick.
    mNextDateTimeUpdateTick =
        mSystemTicks > std::numeric_limits<uint64_t>::max() -
                           kCtrArm11TicksPerSecond
            ? std::numeric_limits<uint64_t>::max()
            : mSystemTicks + kCtrArm11TicksPerSecond;
    mDateTimePublished = true;
    return true;
}

} // namespace Oot3dNativeGame
