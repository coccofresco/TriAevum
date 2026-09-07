#ifndef TRIAEVUM_GUEST_MEMORY_LEASE_POOL_H
#define TRIAEVUM_GUEST_MEMORY_LEASE_POOL_H

#include "triaevum/module_abi.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace triaevum::module {

class RuntimeSession;

class GuestMemoryReadLeasePoolV1 final {
public:
  using MapFunction = std::function<TriAevumModuleStatusV1(
      const TriAevumGuestMemoryMapRequestV1 &, TriAevumGuestMemoryViewV1 *)>;
  using UnmapFunction =
      std::function<TriAevumModuleStatusV1(std::uint64_t, std::uint32_t)>;

  explicit GuestMemoryReadLeasePoolV1(RuntimeSession &session);
  GuestMemoryReadLeasePoolV1(MapFunction map, UnmapFunction unmap);
  ~GuestMemoryReadLeasePoolV1();

  GuestMemoryReadLeasePoolV1(const GuestMemoryReadLeasePoolV1 &) = delete;
  GuestMemoryReadLeasePoolV1 &
  operator=(const GuestMemoryReadLeasePoolV1 &) = delete;

  std::span<const std::uint8_t> Read(std::uint32_t guestAddress,
                                     std::size_t byteCount);
  [[nodiscard]] std::optional<std::uint64_t>
  ContentVersion(std::uint32_t guestAddress, std::size_t byteCount) const;
  TriAevumModuleStatusV1 ReleaseAll();

  [[nodiscard]] std::size_t ActiveLeaseCount() const noexcept;
  [[nodiscard]] TriAevumModuleStatusV1 LastStatus() const noexcept;

private:
  struct Lease {
    std::uint32_t guestAddress = 0U;
    std::uint32_t byteCount = 0U;
    TriAevumGuestMemoryViewV1 view{};
  };

  MapFunction mMap;
  UnmapFunction mUnmap;
  std::vector<Lease> mLeases;
  TriAevumModuleStatusV1 mLastStatus = TRIAEVUM_MODULE_OK_V1;
};

} // namespace triaevum::module

#endif
