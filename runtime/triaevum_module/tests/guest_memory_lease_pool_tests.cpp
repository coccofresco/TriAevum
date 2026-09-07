#include "triaevum/guest_memory_lease_pool.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

using namespace triaevum::module;

bool Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

struct FakeMemory {
  std::array<std::uint8_t, 256> bytes{};
  std::uint32_t mapCount = 0U;
  std::uint32_t unmapCount = 0U;
  std::uint64_t version = 7U;

  TriAevumModuleStatusV1 Map(
      const TriAevumGuestMemoryMapRequestV1 &request,
      TriAevumGuestMemoryViewV1 *view) {
    constexpr std::uint32_t base = 0x1000U;
    if (view == nullptr || request.guest_address < base) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    const std::uint32_t offset = request.guest_address - base;
    if (offset > bytes.size() || request.byte_count > bytes.size() - offset) {
      return TRIAEVUM_MODULE_TITLE_ERROR_V1;
    }
    ++mapCount;
    *view = {sizeof(TriAevumGuestMemoryViewV1),
             TRIAEVUM_GUEST_MEMORY_READ_V1,
             bytes.data() + offset,
             request.byte_count,
             version,
             mapCount};
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1 Unmap(std::uint64_t token, std::uint32_t flags) {
    if (token == 0U || flags != 0U) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    ++unmapCount;
    return TRIAEVUM_MODULE_OK_V1;
  }
};

} // namespace

int main() {
  bool ok = true;
  FakeMemory memory;
  memory.bytes[8] = 0x42U;
  GuestMemoryReadLeasePoolV1 leases(
      [&memory](const auto &request, auto *view) {
        return memory.Map(request, view);
      },
      [&memory](std::uint64_t token, std::uint32_t flags) {
        return memory.Unmap(token, flags);
      });

  const auto first = leases.Read(0x1008U, 16U);
  const auto repeated = leases.Read(0x1008U, 16U);
  ok &= Expect(first.size() == 16U && first.front() == 0x42U &&
                   repeated.data() == first.data() && memory.mapCount == 1U &&
                   leases.ActiveLeaseCount() == 1U &&
                   leases.ContentVersion(0x1008U, 16U) == 7U,
               "read lease was not mapped, versioned and reused");

  const auto second = leases.Read(0x1040U, 8U);
  ok &= Expect(second.size() == 8U && memory.mapCount == 2U &&
                   leases.ActiveLeaseCount() == 2U,
               "independent read lease was not retained");
  ok &= Expect(leases.ReleaseAll() == TRIAEVUM_MODULE_OK_V1 &&
                   leases.ActiveLeaseCount() == 0U &&
                   memory.unmapCount == 2U,
               "read leases were not released exactly once");

  ok &= Expect(leases.Read(0x10F8U, 16U).empty() &&
                   leases.LastStatus() == TRIAEVUM_MODULE_TITLE_ERROR_V1,
               "out-of-range guest mapping was accepted");
  ok &= Expect(leases.Read(0x1000U, 0U).empty() &&
                   leases.LastStatus() ==
                       TRIAEVUM_MODULE_INVALID_ARGUMENT_V1,
               "empty guest mapping was accepted");
  return ok ? 0 : 1;
}
