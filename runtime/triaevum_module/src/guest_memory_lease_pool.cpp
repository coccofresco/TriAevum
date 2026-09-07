#include "triaevum/guest_memory_lease_pool.h"

#include "triaevum/runtime_session.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace triaevum::module {
namespace {

constexpr std::size_t kMaximumConcurrentReadLeases = 4096U;

bool ValidRange(std::uint32_t guestAddress, std::size_t byteCount) {
  return byteCount != 0U &&
         byteCount <= std::numeric_limits<std::uint32_t>::max() &&
         static_cast<std::uint64_t>(guestAddress) + byteCount <=
             static_cast<std::uint64_t>(
                 std::numeric_limits<std::uint32_t>::max()) +
                 1U;
}

} // namespace

GuestMemoryReadLeasePoolV1::GuestMemoryReadLeasePoolV1(
    RuntimeSession &session)
    : GuestMemoryReadLeasePoolV1(
          [&session](const TriAevumGuestMemoryMapRequestV1 &request,
                     TriAevumGuestMemoryViewV1 *view) {
            return session.MapGuestMemory(request, view);
          },
          [&session](std::uint64_t token, std::uint32_t flags) {
            return session.UnmapGuestMemory(token, flags);
          }) {}

GuestMemoryReadLeasePoolV1::GuestMemoryReadLeasePoolV1(MapFunction map,
                                                       UnmapFunction unmap)
    : mMap(std::move(map)), mUnmap(std::move(unmap)) {
  mLeases.reserve(kMaximumConcurrentReadLeases);
}

GuestMemoryReadLeasePoolV1::~GuestMemoryReadLeasePoolV1() { ReleaseAll(); }

std::span<const std::uint8_t>
GuestMemoryReadLeasePoolV1::Read(std::uint32_t guestAddress,
                                 std::size_t byteCount) {
  if (!mMap || !mUnmap || !ValidRange(guestAddress, byteCount)) {
    mLastStatus = TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    return {};
  }
  const std::uint32_t boundedByteCount =
      static_cast<std::uint32_t>(byteCount);
  const auto existing =
      std::find_if(mLeases.begin(), mLeases.end(),
                   [guestAddress, boundedByteCount](const Lease &lease) {
                     return lease.guestAddress == guestAddress &&
                            lease.byteCount == boundedByteCount;
                   });
  if (existing != mLeases.end()) {
    mLastStatus = TRIAEVUM_MODULE_OK_V1;
    return {existing->view.data, boundedByteCount};
  }
  if (mLeases.size() >= kMaximumConcurrentReadLeases) {
    mLastStatus = TRIAEVUM_MODULE_SERVICE_BUSY_V1;
    return {};
  }

  const TriAevumGuestMemoryMapRequestV1 request = {
      sizeof(TriAevumGuestMemoryMapRequestV1),
      TRIAEVUM_GUEST_MEMORY_READ_V1,
      guestAddress,
      boundedByteCount,
  };
  TriAevumGuestMemoryViewV1 view{};
  mLastStatus = mMap(request, &view);
  if (mLastStatus != TRIAEVUM_MODULE_OK_V1) {
    return {};
  }
  if (view.struct_size < sizeof(TriAevumGuestMemoryViewV1) ||
      (view.access & TRIAEVUM_GUEST_MEMORY_READ_V1) == 0U ||
      view.data == nullptr || view.size < boundedByteCount ||
      view.token == 0U) {
    if (view.token != 0U) {
      mUnmap(view.token, 0U);
    }
    mLastStatus = TRIAEVUM_MODULE_TITLE_ERROR_V1;
    return {};
  }
  mLeases.push_back({guestAddress, boundedByteCount, view});
  return {view.data, boundedByteCount};
}

std::optional<std::uint64_t> GuestMemoryReadLeasePoolV1::ContentVersion(
    std::uint32_t guestAddress, std::size_t byteCount) const {
  if (!ValidRange(guestAddress, byteCount)) {
    return std::nullopt;
  }
  const auto existing =
      std::find_if(mLeases.begin(), mLeases.end(),
                   [guestAddress, byteCount](const Lease &lease) {
                     return lease.guestAddress == guestAddress &&
                            lease.byteCount == byteCount;
                   });
  return existing == mLeases.end()
             ? std::nullopt
             : std::optional<std::uint64_t>(existing->view.content_version);
}

TriAevumModuleStatusV1 GuestMemoryReadLeasePoolV1::ReleaseAll() {
  TriAevumModuleStatusV1 firstError = TRIAEVUM_MODULE_OK_V1;
  for (auto lease = mLeases.rbegin(); lease != mLeases.rend(); ++lease) {
    const TriAevumModuleStatusV1 status =
        mUnmap ? mUnmap(lease->view.token, 0U)
               : TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    if (firstError == TRIAEVUM_MODULE_OK_V1 &&
        status != TRIAEVUM_MODULE_OK_V1) {
      firstError = status;
    }
  }
  mLeases.clear();
  mLastStatus = firstError;
  return firstError;
}

std::size_t GuestMemoryReadLeasePoolV1::ActiveLeaseCount() const noexcept {
  return mLeases.size();
}

TriAevumModuleStatusV1
GuestMemoryReadLeasePoolV1::LastStatus() const noexcept {
  return mLastStatus;
}

} // namespace triaevum::module
