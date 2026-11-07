#include "oot3d_native_pica_submission.h"
#include "oot3d_cpu_phase_probe.h"
#include "oot3d_native_pica_transfer.h"
#include "fast/renderer3ds/pica_texture_copy_plan.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

using RuntimeProfileClock = std::chrono::steady_clock;
constexpr size_t kGeometrySnapshotCacheLimit = 4096U;
constexpr size_t kMaximumTextureSnapshotCacheEntries = 65536U;
constexpr size_t kMaximumSnapshotResourceBytes = 64U << 20U;
// Bound the persisted raw-copy ownership ledger. Producers accumulate only
// until a newer copy takes their bytes back, so the live count tracks distinct
// copy destinations rather than elapsed frames.
constexpr size_t kMaximumRawCopyProducers = 1024U;
constexpr size_t kMaximumRawCopySpansPerProducer = 4096U;

uint64_t RuntimeProfileElapsedNanoseconds(
    RuntimeProfileClock::time_point start) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            RuntimeProfileClock::now() - start).count());
}

void SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

bool RangeFits(uint32_t baseAddress, size_t size) {
    return size != 0U &&
           static_cast<uint64_t>(baseAddress) + size <=
               static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) +
                   1U;
}

uint64_t HashSnapshotBytes(std::span<const uint8_t> bytes) {
    return HashOot3dPicaSnapshot(bytes);
}

void HashCombine(size_t& hash, uint64_t value) {
    hash ^= static_cast<size_t>(
                value + 0x9e3779b97f4a7c15ULL +
                (static_cast<uint64_t>(hash) << 6U) +
                (static_cast<uint64_t>(hash) >> 2U));
}

} // namespace

Oot3dPicaPhysicalMemoryView::Oot3dPicaPhysicalMemoryView(
    std::vector<Oot3dPicaPhysicalMemoryRegion> regions, ReadView readView,
    WriteGeneration writeGeneration, MappingCheck mappingCheck)
    : mRegions(std::move(regions)), mReadView(std::move(readView)),
      mWriteGeneration(std::move(writeGeneration)),
      mMappingCheck(std::move(mappingCheck)) {
    if (!mReadView) {
        throw std::invalid_argument("PICA memory view requires a read callback");
    }
}

std::optional<uint32_t> Oot3dPicaPhysicalMemoryView::Translate(
    uint32_t physicalAddress, size_t size) const {
    if (!RangeFits(physicalAddress, size)) {
        return std::nullopt;
    }
    for (const auto& region : mRegions) {
        if (!RangeFits(region.PhysicalBaseAddress, region.Size) ||
            !RangeFits(region.GuestBaseAddress, region.Size) ||
            physicalAddress < region.PhysicalBaseAddress) {
            continue;
        }
        const uint64_t offset =
            static_cast<uint64_t>(physicalAddress) -
            region.PhysicalBaseAddress;
        if (offset + size <= region.Size) {
            const uint32_t guestAddress =
                region.GuestBaseAddress + static_cast<uint32_t>(offset);
            if (mMappingCheck ? mMappingCheck(guestAddress, size)
                              : mReadView(guestAddress, size).size() == size) {
                return guestAddress;
            }
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> Oot3dPicaPhysicalMemoryView::TranslateGuest(
    uint32_t guestAddress, size_t size) const {
    if (!RangeFits(guestAddress, size)) {
        return std::nullopt;
    }
    for (const auto& region : mRegions) {
        if (!RangeFits(region.PhysicalBaseAddress, region.Size) ||
            !RangeFits(region.GuestBaseAddress, region.Size) ||
            guestAddress < region.GuestBaseAddress) {
            continue;
        }
        const uint64_t offset =
            static_cast<uint64_t>(guestAddress) - region.GuestBaseAddress;
        if (offset + size <= region.Size) {
            return region.PhysicalBaseAddress + static_cast<uint32_t>(offset);
        }
    }
    return std::nullopt;
}

bool Oot3dPicaPhysicalMemoryView::ForEachGuestAlias(
    uint32_t physicalAddress, size_t size, const GuestAliasVisitor& visitor) const {
    bool found = false;
    if (!RangeFits(physicalAddress, size)) return false;
    for (const auto& region : mRegions) {
        if (!RangeFits(region.PhysicalBaseAddress, region.Size) ||
            !RangeFits(region.GuestBaseAddress, region.Size) ||
            physicalAddress < region.PhysicalBaseAddress) {
            continue;
        }
        const uint64_t offset =
            static_cast<uint64_t>(physicalAddress) -
            region.PhysicalBaseAddress;
        if (offset + size > region.Size) {
            continue;
        }
        const uint32_t guestAddress =
            region.GuestBaseAddress + static_cast<uint32_t>(offset);
        if (mMappingCheck ? mMappingCheck(guestAddress, size)
                          : mReadView(guestAddress, size).size() == size) {
            found = true;
            if (!visitor(guestAddress, size)) return false;
        }
    }
    return found;
}

std::optional<std::pair<uint32_t, size_t>>
Oot3dPicaPhysicalMemoryView::GuestSegment(uint32_t guestAddress,
                                          size_t size) const {
    if (size == 0U) {
        return std::nullopt;
    }
    for (const auto& region : mRegions) {
        if (!RangeFits(region.GuestBaseAddress, region.Size) ||
            guestAddress < region.GuestBaseAddress) {
            continue;
        }
        const uint64_t offset =
            static_cast<uint64_t>(guestAddress) - region.GuestBaseAddress;
        if (offset >= region.Size) {
            continue;
        }
        const size_t available = region.Size - static_cast<size_t>(offset);
        return std::pair{guestAddress, std::min(size, available)};
    }
    return std::nullopt;
}

std::optional<std::span<const uint8_t>> Oot3dPicaPhysicalMemoryView::View(
    uint32_t physicalAddress, size_t size) const {
    const auto guestAddress = Translate(physicalAddress, size);
    if (!guestAddress.has_value()) {
        return std::nullopt;
    }
    const auto bytes = mReadView(*guestAddress, size);
    if (bytes.size() != size) {
        return std::nullopt;
    }
    return bytes;
}
std::optional<uint64_t>
Oot3dPicaPhysicalMemoryView::RangeWriteGeneration(uint32_t physicalAddress,
                                                  size_t size) const {
  if (!mWriteGeneration) return std::nullopt;
  std::optional<uint64_t> generation;
  const bool certified = ForEachGuestAlias(physicalAddress, size,
      [&](uint32_t guest, size_t bytes) {
          const auto alias = mWriteGeneration(guest, bytes);
          if (!alias || !*alias || (generation && *generation != *alias)) return false;
          generation = alias;
          return true;
      });
  // Providers may use independent opaque tokens for aliases. Unequal tokens
  // cannot be reduced with max/hash into a collision-free certificate.
  return certified ? generation : std::nullopt;
}

bool Oot3dPicaPhysicalMemoryView::ReadCommandBuffer(
    uint32_t physicalAddress, uint32_t byteSize, uint32_t* guestAddress,
    std::vector<uint32_t>* words) const {
    if ((byteSize & 3U) != 0U || guestAddress == nullptr || words == nullptr) {
        return false;
    }
    const auto translated = Translate(physicalAddress, byteSize);
    if (!translated.has_value()) {
        return false;
    }
    words->resize(byteSize / 4U);
    if (!Read(physicalAddress,
              std::span<uint8_t>(reinterpret_cast<uint8_t*>(words->data()),
                                 byteSize))) {
        return false;
    }
    *guestAddress = *translated;
    return true;
}

bool Oot3dPicaPhysicalMemoryView::Read(
    uint32_t physicalAddress, std::span<uint8_t> output) const {
    if (output.empty()) {
        return true;
    }
    const auto guestAddress = Translate(physicalAddress, output.size());
    const auto source = guestAddress.has_value()
                            ? mReadView(*guestAddress, output.size())
                            : std::span<const uint8_t>{};
    if (source.size() != output.size()) return false;
    std::copy(source.begin(), source.end(), output.begin());
    return true;
}

bool Oot3dPicaPhysicalMemoryView::ReadGuest(
    uint32_t guestAddress, std::span<uint8_t> output) const {
    if (output.empty()) return true;
    const auto source = mReadView(guestAddress, output.size());
    if (source.size() != output.size()) return false;
    std::copy(source.begin(), source.end(), output.begin());
    return true;
}

Oot3dNativePicaSubmissionQueue::Oot3dNativePicaSubmissionQueue(
    Oot3dPicaPhysicalMemoryView memory,
    bool deferGpuBackedDisplayTransfers)
    : mMemory(std::move(memory)),
      mDeferGpuBackedDisplayTransfers(deferGpuBackedDisplayTransfers) {}

void Oot3dNativePicaSubmissionQueue::SetRuntimeProfilingEnabled(
    bool enabled) {
    std::lock_guard guard(mQueueMutex);
    mRuntimeProfilingEnabled = enabled;
}

void Oot3dNativePicaSubmissionQueue::SetTexturePayloadTransform(
    TexturePayloadTransform transform) {
    std::lock_guard guard(mQueueMutex);
    mTexturePayloadTransform = std::move(transform);
    mTextureSnapshotCache.clear();
}

Oot3dPicaSubmissionRuntimeProfile
Oot3dNativePicaSubmissionQueue::RuntimeProfile() const noexcept {
    std::lock_guard guard(mQueueMutex);
    return mRuntimeProfile;
}

size_t Oot3dNativePicaSubmissionQueue::TextureSnapshotKeyHash::operator()(
    const TextureSnapshotKey& key) const noexcept {
    size_t hash = 0;
    HashCombine(hash, key.PhysicalAddress);
    HashCombine(hash, key.Width);
    HashCombine(hash, key.Height);
    HashCombine(hash, key.Format);
    HashCombine(hash, key.Type);
    HashCombine(hash, key.WrapS);
    HashCombine(hash, key.WrapT);
    HashCombine(hash, key.MinLinear);
    HashCombine(hash, key.MagLinear);
    HashCombine(hash, key.MipLinear);
    HashCombine(hash, static_cast<uint16_t>(key.LodBiasRaw));
    HashCombine(hash, key.MinMipLevel);
    HashCombine(hash, key.MaxMipLevel);
    return hash;
}

size_t Oot3dNativePicaSubmissionQueue::GeometrySnapshotKeyHash::operator()(
    const GeometrySnapshotKey &key) const noexcept {
  size_t hash = 0;
  HashCombine(hash, static_cast<uint8_t>(key.Kind));
  HashCombine(hash, key.Slot);
  HashCombine(hash, key.PhysicalAddress);
  HashCombine(hash, key.Size);
  return hash;
}

bool Oot3dNativePicaSubmissionQueue::SubmitHardwareRegisterWrite(
    const Oot3dPicaHardwareRegisterWrite& write, std::string*) {
    std::lock_guard guard(mQueueMutex);
    mHardwareWrites.push_back(write);
    return true;
}

bool Oot3dNativePicaSubmissionQueue::CaptureGeometryResource(
    Oot3dPicaResourceKind kind, uint8_t slot, uint32_t physicalAddress,
    uint32_t firstElement, size_t byteCount,
    Oot3dPicaResourceSnapshot &resource, std::string *error) {
  const auto source = mMemory.View(physicalAddress, byteCount);
  if (!source.has_value()) {
    SetError(error, kind == Oot3dPicaResourceKind::IndexBuffer
                        ? "PICA index buffer is outside mapped guest memory"
                        : "PICA vertex loader is outside mapped guest memory");
    return false;
  }

  const GeometrySnapshotKey key{kind, slot, physicalAddress, byteCount};
  const auto sourceWriteGeneration =
      SnapshotWriteGeneration(physicalAddress, byteCount);
  auto cached = mGeometrySnapshotCache.find(key);
  const bool cacheEntryAvailable = cached != mGeometrySnapshotCache.end();
  const bool versionHit =
      cacheEntryAvailable && cached->second.Bytes != nullptr &&
      cached->second.Bytes->size() == source->size() &&
      sourceWriteGeneration.has_value() &&
      cached->second.SourceWriteGeneration == *sourceWriteGeneration;
  const bool compared = cacheEntryAvailable && !versionHit;
  const bool contentMatch =
      compared &&
      ((cached->second.Bytes != nullptr &&
        cached->second.Bytes->size() == source->size() &&
        std::memcmp(cached->second.Bytes->data(), source->data(),
                    source->size()) == 0) ||
       (cached->second.Bytes == nullptr &&
        cached->second.ContentHash == HashSnapshotBytes(*source)));
  const bool cacheHit =
      versionHit || contentMatch;
  if (mRuntimeProfilingEnabled) {
    if (versionHit) {
      ++mRuntimeProfile.GeometrySnapshotVersionHits;
    } else {
      ++mRuntimeProfile.GeometrySnapshotVersionMisses;
    }
    if (compared) {
      mRuntimeProfile.GeometrySnapshotComparedBytes += source->size();
    }
    if (cacheHit) {
      ++mRuntimeProfile.GeometrySnapshotCacheHits;
    } else {
      ++mRuntimeProfile.GeometrySnapshotCacheMisses;
      mRuntimeProfile.GeometrySnapshotCopiedBytes += source->size();
    }
  }
  if (!cacheHit) {
    if (cached == mGeometrySnapshotCache.end() &&
        mGeometrySnapshotCache.size() >= kGeometrySnapshotCacheLimit) {
      const auto oldest = std::min_element(
          mGeometrySnapshotCache.begin(), mGeometrySnapshotCache.end(),
          [](const auto &left, const auto &right) {
            return left.second.LastUsedSequence < right.second.LastUsedSequence;
          });
      if (oldest != mGeometrySnapshotCache.end()) {
        mGeometrySnapshotCache.erase(oldest);
      }
    }
    GeometrySnapshotCacheEntry entry;
    entry.Bytes = std::make_shared<const std::vector<uint8_t>>(source->begin(),
                                                               source->end());
    entry.ContentHash = HashSnapshotBytes(*source);
    entry.ContentVersion = mNextGeometryContentVersion++;
    entry.SourceWriteGeneration = sourceWriteGeneration.value_or(0U);
    if (mNextGeometryContentVersion == 0U) {
      mNextGeometryContentVersion = 1U;
    }
    cached =
        mGeometrySnapshotCache.insert_or_assign(key, std::move(entry)).first;
  } else {
    if (cached->second.Bytes == nullptr) {
      cached->second.Bytes =
          std::make_shared<const std::vector<uint8_t>>(source->begin(),
                                                       source->end());
    }
    cached->second.SourceWriteGeneration = sourceWriteGeneration.value_or(0U);
  }
  cached->second.LastUsedSequence = ++mGeometrySnapshotSequence;

  resource.Kind = kind;
  resource.Slot = slot;
  resource.PhysicalAddress = physicalAddress;
  resource.FirstElement = firstElement;
  resource.SharedBytes = cached->second.Bytes;
  resource.ContentVersion = cached->second.ContentVersion;
  resource.ContentVersionAvailable = true;
  return true;
}

bool Oot3dNativePicaSubmissionQueue::CaptureIndexAndVertexResources(
    Oot3dPicaDrawSubmission &submission, std::string *error) {
  const auto &vertex = submission.State.VertexInput;
  if (vertex.VertexCount == 0U) {
    return true;
  }

  // Index buffer, vertex loaders and the textures captured next: one
  // allocation instead of regrowing the snapshot vector on every draw.
  submission.Resources.reserve(submission.Resources.size() + 1U +
                               vertex.Loaders.size() +
                               submission.State.Textures.size());

  uint32_t minimumIndex = vertex.VertexOffset;
  uint32_t maximumIndex = 0;
  if (vertex.Indexed) {
    const size_t indexSize = vertex.IndicesAre16Bit ? 2U : 1U;
    if (vertex.VertexCount > std::numeric_limits<size_t>::max() / indexSize) {
      SetError(error, "PICA index buffer size overflows");
      return false;
    }
    Oot3dPicaResourceSnapshot indices;
    if (!CaptureGeometryResource(
            Oot3dPicaResourceKind::IndexBuffer, 0U, vertex.IndexPhysicalAddress,
            0U, static_cast<size_t>(vertex.VertexCount) * indexSize, indices,
            error)) {
      return false;
    }
    const auto indexBytes = indices.ResolvedBytes();
    minimumIndex = std::numeric_limits<uint32_t>::max();
    maximumIndex = 0U;
    for (size_t index = 0; index < vertex.VertexCount; ++index) {
      const uint32_t value =
          vertex.IndicesAre16Bit
              ? static_cast<uint32_t>(indexBytes[index * 2U]) |
                    (static_cast<uint32_t>(indexBytes[index * 2U + 1U]) << 8U)
              : indexBytes[index];
      minimumIndex = std::min(minimumIndex, value);
      maximumIndex = std::max(maximumIndex, value);
    }
    submission.Resources.push_back(std::move(indices));
  } else {
    const uint64_t maximum =
        static_cast<uint64_t>(vertex.VertexOffset) + vertex.VertexCount - 1U;
    if (maximum > std::numeric_limits<uint32_t>::max()) {
      SetError(error, "PICA non-indexed vertex range overflows");
      return false;
    }
    maximumIndex = static_cast<uint32_t>(maximum);
  }
  submission.MinimumVertexIndex = minimumIndex;
  submission.MaximumVertexIndex = maximumIndex;

  const uint64_t elementCount =
      static_cast<uint64_t>(maximumIndex) - minimumIndex + 1U;
  for (size_t loaderIndex = 0; loaderIndex < vertex.Loaders.size();
       ++loaderIndex) {
    const auto &loader = vertex.Loaders[loaderIndex];
    if (loader.ComponentCount == 0U || loader.ByteStride == 0U) {
      continue;
    }
    const uint64_t byteOffset =
        static_cast<uint64_t>(minimumIndex) * loader.ByteStride;
    const uint64_t byteCount = elementCount * loader.ByteStride;
    const uint64_t physicalAddress = loader.PhysicalAddress + byteOffset;
    if (physicalAddress > std::numeric_limits<uint32_t>::max() ||
        byteCount > std::numeric_limits<size_t>::max()) {
      SetError(error, "PICA vertex loader snapshot range overflows");
      return false;
    }
    Oot3dPicaResourceSnapshot resource;
    if (!CaptureGeometryResource(Oot3dPicaResourceKind::VertexLoader,
                                 static_cast<uint8_t>(loaderIndex),
                                 static_cast<uint32_t>(physicalAddress),
                                 minimumIndex, static_cast<size_t>(byteCount),
                                 resource, error)) {
      return false;
    }
    submission.Resources.push_back(std::move(resource));
  }
  return true;
}

void Oot3dNativePicaSubmissionQueue::BindGuestWriteObservation(
    GuestWriteRegistrar registrar) {
    std::lock_guard guard(mQueueMutex);
    mGuestWriteRegistrar = std::move(registrar);
    ResetSnapshotObservation();
}

bool Oot3dNativePicaSubmissionQueue::HasGuestWriteObservation() const noexcept {
    std::lock_guard guard(mQueueMutex);
    return static_cast<bool>(mGuestWriteRegistrar);
}

void Oot3dNativePicaSubmissionQueue::NotifyGuestWrite(uint32_t guestAddress,
                                                      size_t byteCount) {
    if (byteCount == 0U) {
        return;
    }
    std::lock_guard guard(mQueueMutex);
    if (mRawCopyProducers.empty() &&
        std::none_of(mColorTargets.begin(), mColorTargets.end(),
                     [](const auto& entry) { return !entry.second.Pending.empty(); })) {
        return;
    }
    // A write may cross the boundary of a mapped view: attribute every mapped
    // segment instead of dropping the notification.
    uint32_t current = guestAddress;
    size_t remaining = byteCount;
    bool attributed = false;
    while (remaining != 0U) {
        const auto segment = mMemory.GuestSegment(current, remaining);
        if (!segment.has_value()) {
            break;
        }
        const auto physical =
            mMemory.TranslateGuest(segment->first, segment->second);
        if (!physical.has_value()) {
            break;
        }
        InvalidateRawCopyOwnership(*physical, segment->second);
        attributed = true;
        current += static_cast<uint32_t>(segment->second);
        remaining -= segment->second;
    }
    if (!attributed || remaining != 0U) {
        // A write that cannot be attributed to any physical range may still
        // cover an owned subrange: ownership that cannot be certified is
        // dropped instead of being assumed.
        mRawCopyProducers.clear();
        for (auto& [address, target] : mColorTargets) target.Pending.clear();
        ResetSnapshotObservation();
    }
}


void Oot3dNativePicaSubmissionQueue::CollectLiveCopyReferences(
    std::vector<Fast::Renderer3ds::PicaTextureCopyReference>& out) const {
    std::lock_guard guard(mQueueMutex);
    // Newest producer first, the same order a snapshot resolves them in.
    for (auto producer = mRawCopyProducers.rbegin();
         producer != mRawCopyProducers.rend(); ++producer) {
        for (const auto& span : producer->Owned) {
            out.push_back({producer->CompletionId,
                           producer->AfterDrawSubmissionId, span.Address,
                           span.ByteCount});
        }
    }
}

bool Oot3dNativePicaSubmissionQueue::ObservePhysicalWriteSpan(
    uint32_t address, size_t byteCount) {
    return mGuestWriteRegistrar &&
        mMemory.ForEachGuestAlias(address, byteCount, mGuestWriteRegistrar);
}

void Oot3dNativePicaSubmissionQueue::ResetSnapshotObservation() {
    for (auto& [key, entry] : mGeometrySnapshotCache) entry.SourceWriteGeneration = 0;
    for (auto& [key, entry] : mTextureSnapshotCache) entry.SourceWriteGeneration = 0;
}

std::optional<uint64_t> Oot3dNativePicaSubmissionQueue::SnapshotWriteGeneration(
    uint32_t address, size_t byteCount) {
    std::optional<uint32_t> first;
    bool aliased = false;
    if (!mMemory.ForEachGuestAlias(address, byteCount, [&](uint32_t guest, size_t) {
            aliased = aliased || (first && *first != guest);
            first = guest;
            return true;
        })) return std::nullopt;
    // Distinct backing aliases still need byte synchronization. A single
    // mapping needs no callback or write trap for cache version tracking.
    if (aliased && !ObservePhysicalWriteSpan(address, byteCount)) return std::nullopt;
    return mMemory.RangeWriteGeneration(address, byteCount);
}


void Oot3dNativePicaSubmissionQueue::SubtractOwnedSpan(
    std::vector<RawCopySpan>& spans, uint32_t address, size_t byteCount) {
    if (byteCount == 0U || spans.empty()) {
        return;
    }
    const uint64_t removedLow = address;
    const uint64_t removedHigh = removedLow + byteCount;
    std::vector<RawCopySpan> kept;
    kept.reserve(spans.size() + 1U);
    for (const auto& span : spans) {
        const uint64_t spanLow = span.Address;
        const uint64_t spanHigh = spanLow + span.ByteCount;
        if (spanHigh <= removedLow || spanLow >= removedHigh) {
            kept.push_back(span);
            continue;
        }
        if (spanLow < removedLow) {
            kept.push_back(
                {span.Address,
                 static_cast<uint32_t>(removedLow - spanLow)});
        }
        if (spanHigh > removedHigh) {
            kept.push_back(
                {static_cast<uint32_t>(removedHigh),
                 static_cast<uint32_t>(spanHigh - removedHigh)});
        }
    }
    spans = std::move(kept);
}

void Oot3dNativePicaSubmissionQueue::InvalidateRawCopyOwnership(
    uint32_t physicalAddress, size_t byteCount) {
    for (auto& producer : mRawCopyProducers) {
        SubtractOwnedSpan(producer.Owned, physicalAddress, byteCount);
    }
    std::erase_if(mRawCopyProducers, [](const RawCopyProducer& producer) {
        return producer.Owned.empty();
    });
    for (auto& [address, target] : mColorTargets) {
        SubtractOwnedSpan(target.Pending, physicalAddress, byteCount);
    }
}

bool Oot3dNativePicaSubmissionQueue::RegisterRawCopyProducer(
    uint64_t completionId, uint64_t afterDrawSubmissionId,
    std::span<const RawCopySpan> destinations, std::string* error) {
    if (completionId == 0U) {
        SetError(error, "raw PICA texture copy has no completion identity");
        return false;
    }
    // A replay of the same transfer is the same producer, never a second owner.
    for (const auto& producer : mRawCopyProducers) {
        if (producer.CompletionId == completionId) {
            return true;
        }
    }
    if (!mGuestWriteRegistrar) {
        SetError(error,
                 "raw PICA texture copy requires guest write observation");
        return false;
    }
    RawCopyProducer producer;
    producer.CompletionId = completionId;
    producer.AfterDrawSubmissionId = afterDrawSubmissionId;
    for (const auto& span : destinations) {
        if (span.ByteCount == 0U) {
            continue;
        }
        if (!ObservePhysicalWriteSpan(span.Address, span.ByteCount)) {
            SetError(error,
                     "raw PICA texture copy destination cannot be observed: an "
                     "escaped writable pointer overlaps it");
            return false;
        }
        producer.Owned.push_back(span);
    }
    if (producer.Owned.empty()) {
        SetError(error, "raw PICA texture copy owns no destination bytes");
        return false;
    }
    // The newest copy takes its bytes back from every older producer, and
    // from every colour target's not yet captured write.
    for (auto& existing : mRawCopyProducers) {
        for (const auto& span : producer.Owned) {
            SubtractOwnedSpan(existing.Owned, span.Address, span.ByteCount);
        }
    }
    for (auto& [address, target] : mColorTargets) {
        for (const auto& span : producer.Owned) {
            SubtractOwnedSpan(target.Pending, span.Address, span.ByteCount);
        }
    }
    std::erase_if(mRawCopyProducers, [](const RawCopyProducer& existing) {
        return existing.Owned.empty();
    });
    mRawCopyProducers.push_back(std::move(producer));
    return true;
}

void Oot3dNativePicaSubmissionQueue::RegisterTiledTransferProducer(
    uint64_t completionId, uint64_t afterDrawSubmissionId,
    uint32_t outputPhysicalAddress, const Oot3dPicaDisplayTransfer& transfer) {
    // Only an output in tiled (texture) layout can be sampled; the linear
    // output of a scanout transfer never is.
    const bool outputTiled =
        ((transfer.Flags & 2U) != 0U) != ((transfer.Flags & 0x20U) != 0U);
    if (transfer.TextureCopyBytes != 0U || !outputTiled || !mGuestWriteRegistrar) {
        return;
    }
    const auto report = [&](const std::string& reason) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            std::fprintf(stderr,
                         "TRIAEVUM_GPU_TILED_TRANSFER_UNSAMPLEABLE flags=0x%08X "
                         "in=0x%08X out=0x%08X reason=%s\n",
                         transfer.Flags, transfer.InputSize, transfer.OutputSize,
                         reason.c_str());
        }
    };
    const auto result = Fast::Renderer3ds::PlanPicaTiledTransferResult(
        outputPhysicalAddress, transfer.InputSize, transfer.OutputSize,
        transfer.Flags);
    if (!result.has_value()) {
        // No GPU version of this write exists; it still overwrites whatever
        // older producer owned these bytes, so their ownership ends here and
        // the bytes fall back to guest memory. Reported, never assumed.
        const uint32_t outputFormat = (transfer.Flags >> 12U) & 7U;
        const uint64_t bytes = uint64_t{transfer.OutputSize & 0xFFFFU} *
                               (transfer.OutputSize >> 16U) *
                               (outputFormat == 0U ? 4U : outputFormat == 1U ? 3U : 2U);
        if (bytes != 0U && uint64_t{outputPhysicalAddress} + bytes <= (uint64_t{1} << 32U)) {
            InvalidateRawCopyOwnership(outputPhysicalAddress, static_cast<size_t>(bytes));
        }
        report("layout");
        return;
    }
    const RawCopySpan destination{
        result->Address, result->Width * result->Height * result->BytesPerPixel};
    std::string error;
    if (!RegisterRawCopyProducer(completionId, afterDrawSubmissionId,
                                 std::span(&destination, 1U), &error)) {
        report(error);
    }
}

void Oot3dNativePicaSubmissionQueue::PublishSampledColorTargets(
    const Oot3dPicaDrawSubmission& submission) {
    if (!mDeferGpuBackedDisplayTransfers || !mGuestWriteRegistrar ||
        submission.Id <= 1U) {
        return;
    }
    for (const auto& texture : submission.State.Textures) {
        // Only colour samplers (2D and projection) read a target as RGBA; a
        // shadow sampler reads its packed depth through the live alias.
        if (!texture.Enabled || (texture.Type != 0U && texture.Type != 3U)) {
            continue;
        }
        const auto byteCount = Oot3dPicaTextureMipChainByteSize(texture);
        if (!byteCount.has_value() || *byteCount == 0U) {
            continue;
        }
        const uint64_t low = texture.PhysicalAddress;
        const uint64_t high = low + *byteCount;
        for (auto& [address, target] : mColorTargets) {
            if (!target.Dirty || high <= address ||
                address + uint64_t{target.ByteCount()} <= low) {
                continue;
            }
            // The read sees the target as the draws before it left it: one
            // producer version per run of writes, captured right after the
            // preceding draw and ordered like every other GPU producer. It
            // owns only the bytes nothing wrote after the GPU (a guest load
            // into reused memory, a newer transfer into the same range).
            target.Dirty = false;
            std::vector<RawCopySpan> owned = std::move(target.Pending);
            target.Pending.clear();
            if (owned.empty()) {
                continue;
            }
            const uint64_t completionId = mNextCompletionId++;
            const uint64_t afterDraw = submission.Id - 1U;
            std::string error;
            if (!RegisterRawCopyProducer(completionId, afterDraw, owned, &error)) {
                static bool reported = false;
                if (!reported) {
                    reported = true;
                    std::fprintf(stderr,
                                 "TRIAEVUM_GPU_COLOR_TARGET_UNSAMPLEABLE "
                                 "address=0x%08X reason=%s\n",
                                 address, error.c_str());
                }
                continue;
            }
            Oot3dPicaDisplayTransferSubmission capture;
            capture.CompletionId = completionId;
            capture.AfterDrawSubmissionId = afterDraw;
            capture.InputPhysicalAddress = address;
            capture.OutputPhysicalAddress = address;
            const uint32_t extent = (target.Height << 16U) | target.Width;
            capture.Transfer.InputSize = extent;
            capture.Transfer.OutputSize = extent;
            // Tiled to tiled, unscaled, in the target's own format.
            capture.Transfer.Flags = 0x20U | (uint32_t{target.Format} << 8U) |
                                     (uint32_t{target.Format} << 12U);
            capture.SignalInterrupt = false;
            capture.RenderTargetCapture = true;
            mPendingDisplayTransfers.push_back(capture);
        }
    }
}

void Oot3dNativePicaSubmissionQueue::MarkColorTargetWritten(uint32_t address) {
    auto& target = mColorTargets.at(address);
    const uint32_t byteCount = target.ByteCount();
    // The common case, one more draw into a target whose whole write is
    // already pending and observed: nothing else can own any of its bytes
    // (every later producer or write would have taken some of them back).
    if (target.Dirty && target.Pending.size() == 1U &&
        target.Pending.front().Address == address &&
        target.Pending.front().ByteCount == byteCount) {
        return;
    }
    // A draw into a target whose memory something else wrote since its
    // attachment was rendered (a transfer into it, a guest load) keeps the
    // attachment's older pixels wherever it does not cover: the attachment is
    // not re-imported from memory. Exact for full-coverage passes (the
    // feedback chains seen so far); counted so the gap stays visible.
    if (target.Dirty || !target.Pending.empty()) {
        ++mColorTargetPreservedStaleWrites;
        if (mColorTargetPreservedStaleWrites == 1U) {
            std::fprintf(stderr,
                         "TRIAEVUM_GPU_COLOR_TARGET_PRESERVED_STALE address=0x%08X\n",
                         address);
        }
    }
    InvalidateRawCopyOwnership(address, byteCount);
    target.Dirty = true;
    target.Pending.clear();
    // The GPU owns the bytes from now on only while a guest write to them can
    // be observed; otherwise they keep their (stale) guest owner.
    if (mGuestWriteRegistrar && ObservePhysicalWriteSpan(address, byteCount)) {
        target.Pending.push_back({address, byteCount});
    } else {
        static bool reported = false;
        if (!reported) {
            reported = true;
            std::fprintf(stderr,
                         "TRIAEVUM_GPU_COLOR_TARGET_UNOBSERVED address=0x%08X\n",
                         address);
        }
    }
}

void Oot3dNativePicaSubmissionQueue::CollectCopyReferences(
    uint64_t drawSubmissionId, uint32_t physicalAddress, size_t byteCount,
    std::vector<Fast::Renderer3ds::PicaTextureCopyReference>& out) const {
    out.clear();
    if (byteCount == 0U || byteCount > std::numeric_limits<uint32_t>::max()) {
        return;
    }
    const uint32_t textureLow = physicalAddress;
    const uint32_t textureHigh =
        static_cast<uint32_t>(physicalAddress + byteCount);
    std::vector<RawCopySpan> claimed;
    uint64_t claimedBytes = 0U;
    // Newest producer first: the latest copy owns every byte it still holds.
    for (auto producer = mRawCopyProducers.rbegin();
         producer != mRawCopyProducers.rend(); ++producer) {
        if (producer->AfterDrawSubmissionId >= drawSubmissionId) {
            continue;
        }
        for (const auto& span : producer->Owned) {
            const uint32_t low = std::max(span.Address, textureLow);
            const uint32_t high = std::min(
                static_cast<uint32_t>(span.Address + span.ByteCount),
                textureHigh);
            if (low >= high) {
                continue;
            }
            std::vector<RawCopySpan> remaining{{low,
                                                static_cast<uint32_t>(high - low)}};
            for (const auto& taken : claimed) {
                SubtractOwnedSpan(remaining, taken.Address, taken.ByteCount);
            }
            for (const auto& piece : remaining) {
                out.push_back({producer->CompletionId,
                               producer->AfterDrawSubmissionId,
                               piece.Address, piece.ByteCount});
                claimed.push_back(piece);
                claimedBytes += piece.ByteCount;
            }
        }
        if (claimedBytes >= byteCount) {
            break;
        }
    }
}

bool Oot3dNativePicaSubmissionQueue::CaptureTextureResources(
    Oot3dPicaDrawSubmission& submission, std::string* error) {
    for (size_t textureIndex = 0;
         textureIndex < submission.State.Textures.size(); ++textureIndex) {
        const auto& texture = submission.State.Textures[textureIndex];
        if (!texture.Enabled) {
            continue;
        }
        const auto byteCount = Oot3dPicaTextureMipChainByteSize(texture);
        if (!byteCount.has_value()) {
            SetError(error, "PICA texture has an invalid native format or size");
            return false;
        }
        Oot3dPicaResourceSnapshot resource;
        resource.Kind = Oot3dPicaResourceKind::Texture;
        resource.Slot = static_cast<uint8_t>(textureIndex);
        resource.PhysicalAddress = texture.PhysicalAddress;
        CollectCopyReferences(submission.Id, resource.PhysicalAddress,
                              *byteCount, resource.CopyReferences);
        if (!resource.CopyReferences.empty()) {
            // Never read/hash a promised GPU write to build its own sampler.
            // Keep only CPU-owned gaps in an immutable base; referenced slots
            // are deliberately absent content, replaced by the GPU derivation.
            std::sort(resource.CopyReferences.begin(), resource.CopyReferences.end(),
                [](const auto& a, const auto& b) { return a.PhysicalAddress < b.PhysicalAddress; });
            auto base = std::make_shared<std::vector<uint8_t>>(*byteCount, 0U);
            uint64_t next = resource.PhysicalAddress;
            const uint64_t end = next + *byteCount;
            const auto copyGap = [&](uint64_t high) {
                if (next >= high) return true;
                return mMemory.Read(static_cast<uint32_t>(next),
                    std::span<uint8_t>(*base).subspan(
                        static_cast<size_t>(next - resource.PhysicalAddress),
                        static_cast<size_t>(high - next)));
            };
            for (const auto& reference : resource.CopyReferences) {
                if (!copyGap(reference.PhysicalAddress)) {
                    SetError(error, "PICA copied texture has an unmapped CPU-owned gap");
                    return false;
                }
                next = uint64_t{reference.PhysicalAddress} + reference.ByteCount;
            }
            if (!copyGap(end)) {
                SetError(error, "PICA copied texture has an unmapped CPU-owned tail");
                return false;
            }
            resource.SharedBytes = std::move(base);
            if (mRuntimeProfilingEnabled) {
                mRuntimeProfile.TextureCopyReferences += resource.CopyReferences.size();
                for (const auto& reference : resource.CopyReferences)
                    mRuntimeProfile.TextureCopyReferencedBytes += reference.ByteCount;
            }
            // Asset replacement/HD-font transforms require native CPU content;
            // a device-produced version is not an immutable title asset.
            submission.Resources.push_back(std::move(resource));
            continue;
        }
        const auto source =
            mMemory.View(resource.PhysicalAddress, *byteCount);
        if (!source.has_value()) {
            SetError(error, "PICA texture is outside mapped guest memory");
            return false;
        }
        const TextureSnapshotKey key{
            texture.PhysicalAddress,
            texture.Width,
            texture.Height,
            texture.Format,
            texture.Type,
            texture.WrapS,
            texture.WrapT,
            texture.MinLinear,
            texture.MagLinear,
            texture.MipLinear,
            texture.LodBiasRaw,
            texture.MinMipLevel,
            texture.MaxMipLevel,
        };
        const auto sourceWriteGeneration =
            SnapshotWriteGeneration(resource.PhysicalAddress, *byteCount);
        auto cached = mTextureSnapshotCache.find(key);
        const bool cacheMetadataAvailable =
            cached != mTextureSnapshotCache.end();
        const bool versionHit = cacheMetadataAvailable && sourceWriteGeneration.has_value() &&
            cached->second.SourceBytes != nullptr &&
            cached->second.SourceBytes->size() == source->size() &&
            cached->second.SourceWriteGeneration == *sourceWriteGeneration;
        const bool compared = cacheMetadataAvailable && !versionHit;
        const bool cacheHit = versionHit ||
            (compared &&
             ((cached->second.SourceBytes != nullptr &&
               cached->second.SourceBytes->size() == source->size() &&
               std::memcmp(cached->second.SourceBytes->data(),
                           source->data(), source->size()) == 0) ||
              (cached->second.SourceBytes == nullptr &&
               cached->second.SourceHash == HashSnapshotBytes(*source))));
        if (mRuntimeProfilingEnabled) {
            if (compared) mRuntimeProfile.TextureSnapshotComparedBytes += source->size();
            if (cacheHit) {
                ++mRuntimeProfile.TextureSnapshotCacheHits;
            } else {
                ++mRuntimeProfile.TextureSnapshotCacheMisses;
                mRuntimeProfile.TextureSnapshotCopiedBytes += source->size();
            }
        }
        if (!cacheHit || cached->second.ResolvedBytes == nullptr) {
            auto sourceBytes =
                std::make_shared<std::vector<uint8_t>>(
                    source->begin(), source->end());
            std::shared_ptr<const std::vector<uint8_t>> resolvedBytes =
                sourceBytes;
            if (mTexturePayloadTransform) {
                auto transformedBytes =
                    std::make_shared<std::vector<uint8_t>>(*sourceBytes);
                const auto baseLevelBytes =
                    Oot3dPicaTextureMipLevelByteSize(texture, 0U);
                if (!baseLevelBytes.has_value() ||
                    *baseLevelBytes > transformedBytes->size()) {
                    SetError(error, "PICA texture base mip is invalid");
                    return false;
                }
                mTexturePayloadTransform(
                    texture,
                    std::span<uint8_t>(*transformedBytes)
                        .first(*baseLevelBytes));
                resolvedBytes = std::move(transformedBytes);
            }
            TextureSnapshotCacheEntry entry;
            entry.SourceBytes = std::move(sourceBytes);
            entry.ResolvedBytes = std::move(resolvedBytes);
            entry.SourceHash = HashSnapshotBytes(*entry.SourceBytes);
            entry.ContentHash = HashSnapshotBytes(*entry.ResolvedBytes);
            const auto baseLevelBytes = Oot3dPicaTextureMipLevelByteSize(texture, 0U);
            if (!baseLevelBytes.has_value() || *baseLevelBytes > entry.ResolvedBytes->size()) {
                SetError(error, "PICA texture base mip is invalid");
                return false;
            }
            // Both versions belong to the immutable, transformed payload.
            // Savestate restore rebuilds that payload and these derived hashes.
            entry.BaseLevelContentHash = *baseLevelBytes == entry.ResolvedBytes->size()
                ? entry.ContentHash
                : HashSnapshotBytes(std::span<const uint8_t>(*entry.ResolvedBytes).first(*baseLevelBytes));
            cached = mTextureSnapshotCache.insert_or_assign(
                key, std::move(entry)).first;
        }
        cached->second.SourceWriteGeneration = sourceWriteGeneration.value_or(0U);
        resource.SharedBytes = cached->second.ResolvedBytes;
        resource.ContentHash = cached->second.ContentHash;
        resource.ContentHashAvailable = true;
        resource.BaseLevelContentHash = cached->second.BaseLevelContentHash;
        resource.BaseLevelContentHashAvailable = true;
        if (mTextureSnapshotTransform) mTextureSnapshotTransform(texture, resource);
        submission.Resources.push_back(std::move(resource));
    }
    return true;
}

bool Oot3dNativePicaSubmissionQueue::SubmitDrawPacket(
    const Oot3dPicaDrawPacket& packet, std::string* error) {
    CpuPhaseProbe::Scope cpuLock(CpuPhaseProbe::Phase::QueueLock);
    std::lock_guard guard(mQueueMutex);
    cpuLock.Stop();
    const auto totalStart =
        mRuntimeProfilingEnabled ? RuntimeProfileClock::now()
                                 : RuntimeProfileClock::time_point{};
    if (mRuntimeProfilingEnabled) {
        ++mRuntimeProfile.DrawCalls;
    }
    auto phaseStart =
        mRuntimeProfilingEnabled ? RuntimeProfileClock::now()
                                 : RuntimeProfileClock::time_point{};
    // Construct the very large PICA packet directly from its source.  The
    // previous default construction zeroed both 4096-word shader stores and
    // immediately overwrote them with the packet copy on every draw.
    mPendingDraws.emplace_back(mNextSubmissionId, packet);
    Oot3dPicaDrawSubmission& submission = mPendingDraws.back();
    if (mRuntimeProfilingEnabled) {
        mRuntimeProfile.PacketCopyNanoseconds +=
            RuntimeProfileElapsedNanoseconds(phaseStart);
    }
    if (mRuntimeProfilingEnabled) {
        phaseStart = RuntimeProfileClock::now();
    }
    if (!DecodeOot3dPicaDrawState(packet, submission.State, error)) {
        mPendingDraws.pop_back();
        return false;
    }
    if (mRuntimeProfilingEnabled) {
        mRuntimeProfile.DecodeNanoseconds +=
            RuntimeProfileElapsedNanoseconds(phaseStart);
        phaseStart = RuntimeProfileClock::now();
    }
    CpuPhaseProbe::Scope cpuVertex(CpuPhaseProbe::Phase::VertexCapture);
    if (!CaptureIndexAndVertexResources(submission, error)) {
        mPendingDraws.pop_back();
        return false;
    }
    cpuVertex.Stop();
    if (mRuntimeProfilingEnabled) {
        mRuntimeProfile.VertexCaptureNanoseconds +=
            RuntimeProfileElapsedNanoseconds(phaseStart);
        phaseStart = RuntimeProfileClock::now();
    }
    CpuPhaseProbe::Scope cpuTexture(CpuPhaseProbe::Phase::TextureCapture);
    PublishSampledColorTargets(submission);
    if (!CaptureTextureResources(submission, error)) {
        mPendingDraws.pop_back();
        return false;
    }
    cpuTexture.Stop();
    if (mRuntimeProfilingEnabled) {
        mRuntimeProfile.TextureCaptureNanoseconds +=
            RuntimeProfileElapsedNanoseconds(phaseStart);
        for (const auto& resource : submission.Resources) {
            auto* byteCount = &mRuntimeProfile.VertexBytes;
            if (resource.Kind == Oot3dPicaResourceKind::IndexBuffer) {
                byteCount = &mRuntimeProfile.IndexBytes;
            } else if (resource.Kind == Oot3dPicaResourceKind::Texture) {
                byteCount = &mRuntimeProfile.TextureBytes;
            }
            *byteCount += resource.ResolvedBytes().size();
        }
        phaseStart = RuntimeProfileClock::now();
    }
    if(mDrawSubmissionObserver)mDrawSubmissionObserver(submission.Id);
    ++mNextSubmissionId;
    phaseStart =
        mRuntimeProfilingEnabled ? RuntimeProfileClock::now()
                                 : RuntimeProfileClock::time_point{};
    const uint32_t colorTarget =
        submission.State.Framebuffer.ColorPhysicalAddress;
    mKnownColorRenderTargets.insert(colorTarget);
    mInvalidatedColorRenderTargets.erase(colorTarget);
    if (const auto& framebuffer = submission.State.Framebuffer;
        mDeferGpuBackedDisplayTransfers && colorTarget != 0U &&
        framebuffer.ColorWriteEnabled &&
        submission.State.OutputMerger.ColorWriteMask != 0U &&
        framebuffer.Width != 0U &&
        framebuffer.Height != 0U && framebuffer.ColorFormat <= 4U) {
        // This draw writes the target on the GPU: a later texture read of its
        // bytes must see this version, never the stale guest memory.
        auto& target = mColorTargets[colorTarget];
        target.Width = framebuffer.Width;
        target.Height = framebuffer.Height;
        target.Format = framebuffer.ColorFormat;
        MarkColorTargetWritten(colorTarget);
    }
    for (auto transfer = mRestoredDisplayTransfersAwaitingTarget.begin();
         transfer != mRestoredDisplayTransfersAwaitingTarget.end();) {
        if (transfer->InputPhysicalAddress != colorTarget) {
            ++transfer;
            continue;
        }
        transfer->AfterDrawSubmissionId = submission.Id;
        mPendingDisplayTransfers.push_back(std::move(*transfer));
        transfer = mRestoredDisplayTransfersAwaitingTarget.erase(transfer);
    }
    if (mRuntimeProfilingEnabled) {
        mRuntimeProfile.EnqueueNanoseconds +=
            RuntimeProfileElapsedNanoseconds(phaseStart);
        mRuntimeProfile.TotalNanoseconds +=
            RuntimeProfileElapsedNanoseconds(totalStart);
    }
    return true;
}

bool Oot3dNativePicaSubmissionQueue::SubmitDisplayTransfer(
    const Oot3dPicaDisplayTransfer& transfer, bool* deferredToGpu,
    std::string* error, bool* cpuCopySuppressed) {
    std::lock_guard guard(mQueueMutex);
    if (deferredToGpu != nullptr) {
        *deferredToGpu = false;
    }
    if (cpuCopySuppressed != nullptr) {
        *cpuCopySuppressed = false;
    }
    const auto inputPhysical = mMemory.TranslateGuest(transfer.InputAddress, 1U);
    Oot3dPicaTextureCopyPlan copyPlan;
    std::vector<RawCopySpan> rawCopyDestinations;
    if (transfer.TextureCopyBytes != 0U) {
        if (!BuildOot3dPicaTextureCopyPlan(
                {4U, {transfer.InputAddress, transfer.OutputAddress,
                      transfer.TextureCopyBytes, transfer.InputSize, transfer.OutputSize, transfer.Flags}},
                copyPlan, error)) return false;
        rawCopyDestinations.reserve(copyPlan.Spans.size());
        for (const auto& span : copyPlan.Spans) {
            if (!mMemory.TranslateGuest(span.InputAddress, span.Size).has_value()) {
                SetError(error, "PICA raw copy contains an unmapped range");
                return false;
            }
            // The plan works in guest addresses; copy ownership is physical.
            const auto destinationPhysical =
                mMemory.TranslateGuest(span.OutputAddress, span.Size);
            if (!destinationPhysical.has_value()) {
                SetError(error, "PICA raw copy contains an unmapped range");
                return false;
            }
            rawCopyDestinations.push_back({*destinationPhysical, span.Size});
        }
    }
    if (mDeferGpuBackedDisplayTransfers && inputPhysical.has_value() &&
        mInvalidatedColorRenderTargets.contains(*inputPhysical) && transfer.TextureCopyBytes == 0U) {
        const auto outputPhysical =
            mMemory.TranslateGuest(transfer.OutputAddress, 1U);
        if (!outputPhysical.has_value()) {
            SetError(error,
                     "restored PICA display transfer output is outside "
                     "physical memory");
            return false;
        }
        if (mNextSubmissionId == 1U) {
            SetError(error,
                     "restored PICA display transfer has no preceding draw "
                     "submission");
            return false;
        }
        // The portable CPU path below remains authoritative until a draw
        // recreates this GPU target. Allocate the original completion serial
        // now, but replay without a second guest interrupt after that draw.
        mRestoredDisplayTransfersAwaitingTarget.push_back(
            {mNextCompletionId++, 0U, *inputPhysical, *outputPhysical,
             transfer, false});
        if (cpuCopySuppressed != nullptr) {
            *cpuCopySuppressed = true;
        }
        return true;
    }
    if (!mDeferGpuBackedDisplayTransfers || !inputPhysical.has_value() ||
        (!mKnownColorRenderTargets.contains(*inputPhysical) &&
         !(transfer.TextureCopyBytes != 0U && mInvalidatedColorRenderTargets.contains(*inputPhysical)))) {
        return true;
    }
    const auto outputPhysical =
        mMemory.TranslateGuest(transfer.OutputAddress, 1U);
    if (!outputPhysical.has_value()) {
        SetError(error, "PICA display transfer output is outside physical memory");
        return false;
    }
    if (mNextSubmissionId == 1U) {
        SetError(error, "PICA display transfer has no preceding draw submission");
        return false;
    }
    const uint64_t completionId = mNextCompletionId;
    const uint64_t afterDrawSubmissionId = mNextSubmissionId - 1U;
    if (transfer.TextureCopyBytes != 0U) {
        // The source is a GPU render target, so its guest bytes are stale
        // relative to the GPU render: keeping this copy on the CPU path would
        // be a silent downgrade. Observation is required, and a destination
        // that cannot be observed is an explicit failure, never a copy left
        // owning bytes it cannot prove.
        if (!mGuestWriteRegistrar) {
            SetError(error,
                     "GPU-backed raw PICA texture copy requires guest write "
                     "observation");
            return false;
        }
        // The ordered producer promise exists before the backend executes it, so
        // a snapshot taken now already carries the identity of the version it
        // will sample.
        if (!RegisterRawCopyProducer(completionId, afterDrawSubmissionId,
                                     rawCopyDestinations, error)) {
            return false;
        }
    } else {
        // A DisplayTransfer into texture layout is a GPU producer as well:
        // the downscaled copy a later draw samples (blur, bloom, history).
        RegisterTiledTransferProducer(completionId, afterDrawSubmissionId,
                                      *outputPhysical, transfer);
    }
    ++mNextCompletionId;
    mPendingDisplayTransfers.push_back(
        {completionId, afterDrawSubmissionId, *inputPhysical,
         *outputPhysical, transfer});
    if (deferredToGpu != nullptr) {
        *deferredToGpu = true;
    }
    return true;
}

bool Oot3dNativePicaSubmissionQueue::SubmitMemoryFill(
    const Oot3dPicaMemoryFillCommand& command, bool* deferredToGpu,
    std::string* error) {
    std::lock_guard guard(mQueueMutex);
    if (deferredToGpu != nullptr) {
        *deferredToGpu = false;
    }
    if (!mDeferGpuBackedDisplayTransfers) {
        return true;
    }

    struct TranslatedFill {
        size_t Index = 0;
        uint32_t Start = 0;
        uint32_t End = 0;
    };
    std::vector<TranslatedFill> translated;
    for (size_t index = 0; index < command.Fills.size(); ++index) {
        const auto& fill = command.Fills[index];
        if (fill.StartAddress == 0U || (fill.Control & 1U) == 0U) {
            continue;
        }
        if (fill.EndAddress <= fill.StartAddress) {
            SetError(error, "PICA memory fill range is invalid");
            return false;
        }
        const size_t size =
            static_cast<size_t>(fill.EndAddress - fill.StartAddress);
        const auto physical = mMemory.TranslateGuest(fill.StartAddress, size);
        if (!physical.has_value()) {
            return true;
        }
        translated.push_back(
            {index, *physical, *physical + static_cast<uint32_t>(size)});
    }
    if (translated.empty()) {
        return true;
    }

    const bool hasBothBuffers = command.Fills[0].StartAddress != 0U &&
                                command.Fills[1].StartAddress != 0U;
    for (const auto& item : translated) {
        const auto& fill = command.Fills[item.Index];
        std::optional<Oot3dPicaInterruptId> interrupt;
        if (item.Index == 0U && !hasBothBuffers) {
            interrupt = Oot3dPicaInterruptId::Psc0;
        } else if (item.Index == 1U) {
            interrupt = hasBothBuffers ? Oot3dPicaInterruptId::Psc0
                                       : Oot3dPicaInterruptId::Psc1;
        }
        const uint64_t completionId =
            interrupt.has_value() ? mNextCompletionId++ : 0U;
        mPendingMemoryFills.push_back(
            {completionId, mNextSubmissionId, item.Start, item.End,
             fill.Value, fill.Control, interrupt});
        // No GPU ownership: the host performs every fill on guest memory as
        // well, and that observed write takes the filled bytes back from any
        // producer and uncaptured colour write, so a read sees the fill. A
        // fill covering a whole target also clears its attachment, so memory
        // and attachment agree again: the target starts clean.
        for (auto& [address, target] : mColorTargets) {
            if (item.Start <= address &&
                uint64_t{address} + target.ByteCount() <= item.End) {
                target.Dirty = false;
                target.Pending.clear();
            }
        }
    }
    if (deferredToGpu != nullptr) {
        *deferredToGpu = true;
    }
    return true;
}

bool Oot3dNativePicaSubmissionQueue::SubmitInterruptAfterGpuWork(
    Oot3dPicaInterruptId interrupt, std::string* error) {
    std::lock_guard guard(mQueueMutex);
    if (mNextSubmissionId == 1U) {
        SetError(error, "PICA completion has no preceding draw submission");
        return false;
    }
    mPendingCompletions.push_back(
        {mNextCompletionId++, mNextSubmissionId - 1U, interrupt});
    return true;
}

std::span<const Oot3dPicaDrawSubmission>
Oot3dNativePicaSubmissionQueue::PendingDraws() const {
    return mPendingDraws;
}

std::vector<Oot3dPicaDrawSubmission>
Oot3dNativePicaSubmissionQueue::TakePendingDraws() {
    std::lock_guard guard(mQueueMutex);
    std::vector<Oot3dPicaDrawSubmission> draws;
    draws.swap(mPendingDraws);
    return draws;
}

void Oot3dNativePicaSubmissionQueue::TakePendingDraws(
    std::vector<Oot3dPicaDrawSubmission>& destination) {
    std::lock_guard guard(mQueueMutex);
    destination.clear();
    destination.swap(mPendingDraws);
}

std::span<const Oot3dPicaCompletionSubmission>
Oot3dNativePicaSubmissionQueue::PendingCompletions() const {
    return mPendingCompletions;
}

std::vector<Oot3dPicaCompletionSubmission>
Oot3dNativePicaSubmissionQueue::TakePendingCompletions() {
    std::lock_guard guard(mQueueMutex);
    auto completions = std::move(mPendingCompletions);
    mPendingCompletions.clear();
    return completions;
}

std::span<const Oot3dPicaDisplayTransferSubmission>
Oot3dNativePicaSubmissionQueue::PendingDisplayTransfers() const {
    return mPendingDisplayTransfers;
}

std::vector<Oot3dPicaDisplayTransferSubmission>
Oot3dNativePicaSubmissionQueue::TakePendingDisplayTransfers() {
    std::lock_guard guard(mQueueMutex);
    auto transfers = std::move(mPendingDisplayTransfers);
    mPendingDisplayTransfers.clear();
    return transfers;
}

std::span<const Oot3dPicaMemoryFillSubmission>
Oot3dNativePicaSubmissionQueue::PendingMemoryFills() const {
    return mPendingMemoryFills;
}

std::vector<Oot3dPicaMemoryFillSubmission>
Oot3dNativePicaSubmissionQueue::TakePendingMemoryFills() {
    std::lock_guard guard(mQueueMutex);
    auto fills = std::move(mPendingMemoryFills);
    mPendingMemoryFills.clear();
    return fills;
}

Oot3dPicaSubmissionBatch
Oot3dNativePicaSubmissionQueue::TakePendingBatch() {
    std::lock_guard guard(mQueueMutex);
    Oot3dPicaSubmissionBatch batch;
    batch.Draws.swap(mPendingDraws);
    batch.Completions.swap(mPendingCompletions);
    batch.DisplayTransfers.swap(mPendingDisplayTransfers);
    batch.MemoryFills.swap(mPendingMemoryFills);
    return batch;
}

bool Oot3dNativePicaSubmissionQueue::IsQuiescent() const noexcept {
    std::lock_guard guard(mQueueMutex);
    return mPendingDraws.empty() && mPendingCompletions.empty() &&
           mPendingDisplayTransfers.empty() && mPendingMemoryFills.empty();
}

void Oot3dNativePicaSubmissionQueue::RestoreGpuColorRenderTargetOwnership(
    std::span<const uint32_t> colorPhysicalAddresses) {
    std::lock_guard guard(mQueueMutex);
    for (const uint32_t address : colorPhysicalAddresses) {
        if (address == 0U ||
            !mInvalidatedColorRenderTargets.erase(address)) {
            continue;
        }
        mKnownColorRenderTargets.insert(address);
        std::erase_if(
            mRestoredDisplayTransfersAwaitingTarget,
            [address](const auto& transfer) {
                return transfer.InputPhysicalAddress == address;
            });
    }
}

nlohmann::json Oot3dNativePicaSubmissionQueue::CaptureState() const {
    std::lock_guard guard(mQueueMutex);
    std::vector<uint32_t> knownColorRenderTargets(
        mKnownColorRenderTargets.begin(), mKnownColorRenderTargets.end());
    std::sort(knownColorRenderTargets.begin(),
              knownColorRenderTargets.end());
    std::vector<uint32_t> invalidatedColorRenderTargets(
        mInvalidatedColorRenderTargets.begin(),
        mInvalidatedColorRenderTargets.end());
    std::sort(invalidatedColorRenderTargets.begin(),
              invalidatedColorRenderTargets.end());
    nlohmann::json restoredDisplayTransfers = nlohmann::json::array();
    for (const auto &transfer : mRestoredDisplayTransfersAwaitingTarget) {
        restoredDisplayTransfers.push_back(
            {{"completion_id", transfer.CompletionId},
             {"input_physical_address", transfer.InputPhysicalAddress},
             {"output_physical_address", transfer.OutputPhysicalAddress},
             {"input_address", transfer.Transfer.InputAddress},
             {"output_address", transfer.Transfer.OutputAddress},
             {"input_size", transfer.Transfer.InputSize},
             {"output_size", transfer.Transfer.OutputSize},
             {"flags", transfer.Transfer.Flags}});
    }
    std::vector<std::pair<GeometrySnapshotKey,
                          const GeometrySnapshotCacheEntry*>>
        geometryEntries;
    geometryEntries.reserve(mGeometrySnapshotCache.size());
    for (const auto& [key, entry] : mGeometrySnapshotCache) {
        geometryEntries.emplace_back(key, &entry);
    }
    std::sort(geometryEntries.begin(), geometryEntries.end(),
              [](const auto& left, const auto& right) {
                  const auto& a = left.first;
                  const auto& b = right.first;
                  return std::tie(a.Kind, a.Slot, a.PhysicalAddress, a.Size) <
                         std::tie(b.Kind, b.Slot, b.PhysicalAddress, b.Size);
              });
    nlohmann::json geometrySnapshotCache = nlohmann::json::array();
    for (const auto& [key, entry] : geometryEntries) {
        geometrySnapshotCache.push_back(
            {{"kind", static_cast<uint8_t>(key.Kind)},
             {"slot", key.Slot},
             {"physical_address", key.PhysicalAddress},
             {"size", key.Size},
             {"content_hash", entry->ContentHash},
             {"content_version", entry->ContentVersion},
             {"last_used_sequence", entry->LastUsedSequence}});
    }
    std::vector<std::pair<TextureSnapshotKey,
                          const TextureSnapshotCacheEntry*>>
        textureEntries;
    textureEntries.reserve(mTextureSnapshotCache.size());
    for (const auto& [key, entry] : mTextureSnapshotCache) {
        textureEntries.emplace_back(key, &entry);
    }
    std::sort(textureEntries.begin(), textureEntries.end(),
              [](const auto& left, const auto& right) {
                  const auto& a = left.first;
                  const auto& b = right.first;
                  return std::tie(a.PhysicalAddress, a.Width, a.Height,
                                  a.Format, a.Type, a.WrapS, a.WrapT,
                                  a.MinLinear, a.MagLinear, a.MipLinear,
                                  a.LodBiasRaw, a.MinMipLevel,
                                  a.MaxMipLevel) <
                         std::tie(b.PhysicalAddress, b.Width, b.Height,
                                  b.Format, b.Type, b.WrapS, b.WrapT,
                                  b.MinLinear, b.MagLinear, b.MipLinear,
                                  b.LodBiasRaw, b.MinMipLevel,
                                  b.MaxMipLevel);
              });
    nlohmann::json textureSnapshotCache = nlohmann::json::array();
    for (const auto& [key, entry] : textureEntries) {
        textureSnapshotCache.push_back(
            {{"physical_address", key.PhysicalAddress},
             {"width", key.Width},
             {"height", key.Height},
             {"format", key.Format},
             {"type", key.Type},
             {"wrap_s", key.WrapS},
             {"wrap_t", key.WrapT},
             {"min_linear", key.MinLinear},
             {"mag_linear", key.MagLinear},
             {"mip_linear", key.MipLinear},
             {"lod_bias_raw", key.LodBiasRaw},
             {"min_mip_level", key.MinMipLevel},
             {"max_mip_level", key.MaxMipLevel},
             {"source_hash", entry->SourceHash},
             {"content_hash", entry->ContentHash}});
    }
    nlohmann::json rawCopyProducers = nlohmann::json::array();
    for (const auto& producer : mRawCopyProducers) {
        nlohmann::json spans = nlohmann::json::array();
        for (const auto& span : producer.Owned) {
            spans.push_back({{"address", span.Address},
                             {"byte_count", span.ByteCount}});
        }
        rawCopyProducers.push_back(
            {{"completion_id", producer.CompletionId},
             {"after_draw_submission_id", producer.AfterDrawSubmissionId},
             {"spans", std::move(spans)}});
    }
    nlohmann::json colorTargets = nlohmann::json::array();
    for (const auto& [address, target] : mColorTargets) {
        colorTargets.push_back({{"address", address},
                                {"width", target.Width},
                                {"height", target.Height},
                                {"format", target.Format},
                                {"dirty", target.Dirty},
                                {"pending", [&] {
                                     nlohmann::json spans = nlohmann::json::array();
                                     for (const auto& span : target.Pending)
                                         spans.push_back({{"address", span.Address},
                                                          {"byte_count", span.ByteCount}});
                                     return spans;
                                 }()}});
    }
    return {
        {"format", "oot3d_native_pica_submission_state_v4"},
        {"color_target_versions", std::move(colorTargets)},
        {"defer_gpu_backed_display_transfers",
         mDeferGpuBackedDisplayTransfers},
        {"next_submission_id", mNextSubmissionId},
        {"next_completion_id", mNextCompletionId},
        {"next_geometry_content_version", mNextGeometryContentVersion},
        {"geometry_snapshot_sequence", mGeometrySnapshotSequence},
        {"geometry_snapshot_cache", std::move(geometrySnapshotCache)},
        {"texture_snapshot_cache", std::move(textureSnapshotCache)},
        {"known_color_render_targets",
         std::move(knownColorRenderTargets)},
        {"invalidated_color_render_targets",
         std::move(invalidatedColorRenderTargets)},
        {"restored_display_transfers_awaiting_target",
         std::move(restoredDisplayTransfers)},
        {"raw_copy_producers", std::move(rawCopyProducers)},
    };
}

bool Oot3dNativePicaSubmissionQueue::RestoreState(
    const nlohmann::json& state, std::string* error) {
    std::lock_guard guard(mQueueMutex);
    try {
        const std::string format = state.value("format", std::string{});
        const bool copyProducerState =
            format == "oot3d_native_pica_submission_state_v4";
        const bool extendedTextureState =
            copyProducerState ||
            format == "oot3d_native_pica_submission_state_v3";
        const bool cacheStateAvailable = extendedTextureState ||
            format == "oot3d_native_pica_submission_state_v2";
        if (!state.is_object() ||
            (!cacheStateAvailable &&
             format != "oot3d_native_pica_submission_state_v1") ||
            state.at("defer_gpu_backed_display_transfers").get<bool>() !=
                mDeferGpuBackedDisplayTransfers) {
            if (error != nullptr) {
                *error = "native PICA submission state is incompatible";
            }
            return false;
        }
        const uint64_t nextSubmissionId =
            state.at("next_submission_id").get<uint64_t>();
        const uint64_t nextCompletionId =
            state.at("next_completion_id").get<uint64_t>();
        if (nextSubmissionId == 0U || nextCompletionId == 0U) {
            if (error != nullptr) {
                *error = "native PICA submission sequence is invalid";
            }
            return false;
        }
        uint64_t nextGeometryContentVersion = 1U;
        uint64_t geometrySnapshotSequence = 0U;
        std::unordered_map<GeometrySnapshotKey,
                           GeometrySnapshotCacheEntry,
                           GeometrySnapshotKeyHash>
            geometrySnapshotCache;
        std::unordered_map<TextureSnapshotKey,
                           TextureSnapshotCacheEntry,
                           TextureSnapshotKeyHash>
            textureSnapshotCache;
        if (cacheStateAvailable) {
            nextGeometryContentVersion =
                state.at("next_geometry_content_version").get<uint64_t>();
            geometrySnapshotSequence =
                state.at("geometry_snapshot_sequence").get<uint64_t>();
            if (nextGeometryContentVersion == 0U) {
                if (error != nullptr) {
                    *error = "native PICA geometry cache sequence is invalid";
                }
                return false;
            }
            const auto& encodedGeometry =
                state.at("geometry_snapshot_cache");
            if (!encodedGeometry.is_array() ||
                encodedGeometry.size() > kGeometrySnapshotCacheLimit) {
                if (error != nullptr) {
                    *error = "native PICA geometry cache is invalid";
                }
                return false;
            }
            geometrySnapshotCache.reserve(encodedGeometry.size());
            for (const auto& encoded : encodedGeometry) {
                const uint8_t kindValue = encoded.at("kind").get<uint8_t>();
                const size_t byteCount = encoded.at("size").get<size_t>();
                GeometrySnapshotKey key{
                    static_cast<Oot3dPicaResourceKind>(kindValue),
                    encoded.at("slot").get<uint8_t>(),
                    encoded.at("physical_address").get<uint32_t>(),
                    byteCount};
                GeometrySnapshotCacheEntry entry;
                entry.ContentHash =
                    encoded.at("content_hash").get<uint64_t>();
                entry.ContentVersion =
                    encoded.at("content_version").get<uint64_t>();
                entry.LastUsedSequence =
                    encoded.at("last_used_sequence").get<uint64_t>();
                if (kindValue > static_cast<uint8_t>(
                                    Oot3dPicaResourceKind::VertexLoader) ||
                    key.Slot >= 16U || byteCount == 0U ||
                    byteCount > kMaximumSnapshotResourceBytes ||
                    entry.ContentVersion == 0U ||
                    entry.LastUsedSequence == 0U ||
                    entry.LastUsedSequence > geometrySnapshotSequence ||
                    !geometrySnapshotCache.emplace(key, std::move(entry))
                         .second) {
                    if (error != nullptr) {
                        *error = "native PICA geometry cache entry is invalid";
                    }
                    return false;
                }
            }
            const auto& encodedTextures =
                state.at("texture_snapshot_cache");
            if (!encodedTextures.is_array() ||
                encodedTextures.size() >
                    kMaximumTextureSnapshotCacheEntries) {
                if (error != nullptr) {
                    *error = "native PICA texture cache is invalid";
                }
                return false;
            }
            textureSnapshotCache.reserve(encodedTextures.size());
            for (const auto& encoded : encodedTextures) {
                TextureSnapshotKey key{
                    encoded.at("physical_address").get<uint32_t>(),
                    encoded.at("width").get<uint16_t>(),
                    encoded.at("height").get<uint16_t>(),
                    encoded.at("format").get<uint8_t>(),
                    encoded.at("type").get<uint8_t>(),
                    encoded.at("wrap_s").get<uint8_t>(),
                    encoded.at("wrap_t").get<uint8_t>(),
                    encoded.at("min_linear").get<bool>(),
                    encoded.at("mag_linear").get<bool>(),
                    extendedTextureState
                        ? encoded.at("mip_linear").get<bool>() : false,
                    extendedTextureState
                        ? encoded.at("lod_bias_raw").get<int16_t>()
                        : int16_t{0},
                    extendedTextureState
                        ? encoded.at("min_mip_level").get<uint8_t>()
                        : uint8_t{0},
                    extendedTextureState
                        ? encoded.at("max_mip_level").get<uint8_t>()
                        : uint8_t{0}};
                TextureSnapshotCacheEntry entry;
                entry.SourceHash =
                    encoded.at("source_hash").get<uint64_t>();
                entry.ContentHash =
                    encoded.at("content_hash").get<uint64_t>();
                Oot3dPicaTextureState texture;
                texture.Enabled = true;
                texture.Width = key.Width;
                texture.Height = key.Height;
                texture.PhysicalAddress = key.PhysicalAddress;
                texture.Format = key.Format;
                texture.Type = key.Type;
                texture.WrapS = key.WrapS;
                texture.WrapT = key.WrapT;
                texture.MinLinear = key.MinLinear;
                texture.MagLinear = key.MagLinear;
                texture.MipLinear = key.MipLinear;
                texture.LodBiasRaw = key.LodBiasRaw;
                texture.MinMipLevel = key.MinMipLevel;
                texture.MaxMipLevel = key.MaxMipLevel;
                const auto byteCount =
                    Oot3dPicaTextureMipChainByteSize(texture);
                if (!byteCount.has_value() ||
                    *byteCount > kMaximumSnapshotResourceBytes ||
                    !textureSnapshotCache.emplace(key, std::move(entry))
                         .second) {
                    if (error != nullptr) {
                        *error = "native PICA texture cache entry is invalid";
                    }
                    return false;
                }
            }
        }
        std::unordered_set<uint32_t> knownColorRenderTargets;
        for (const auto& encoded : state.at("known_color_render_targets")) {
            if (!knownColorRenderTargets
                     .insert(encoded.get<uint32_t>())
                     .second) {
                if (error != nullptr) {
                    *error =
                        "native PICA render target state is duplicated";
                }
                return false;
            }
        }
        std::unordered_set<uint32_t> invalidatedColorRenderTargets;
        const auto encodedInvalidated =
            state.find("invalidated_color_render_targets");
        if (encodedInvalidated != state.end()) {
            for (const auto &encoded : *encodedInvalidated) {
                if (!invalidatedColorRenderTargets
                         .insert(encoded.get<uint32_t>())
                         .second) {
                    if (error != nullptr) {
                        *error = "native PICA invalidated render target state "
                                 "is duplicated";
                    }
                    return false;
                }
            }
        }
        std::vector<RawCopyProducer> rawCopyProducers;
        if (copyProducerState) {
            const auto& encodedProducers = state.at("raw_copy_producers");
            if (!encodedProducers.is_array() ||
                encodedProducers.size() > kMaximumRawCopyProducers) {
                if (error != nullptr) {
                    *error = "native PICA raw copy ledger is invalid";
                }
                return false;
            }
            rawCopyProducers.reserve(encodedProducers.size());
            for (const auto& encoded : encodedProducers) {
                RawCopyProducer producer;
                producer.CompletionId =
                    encoded.at("completion_id").get<uint64_t>();
                producer.AfterDrawSubmissionId =
                    encoded.at("after_draw_submission_id").get<uint64_t>();
                const auto& encodedSpans = encoded.at("spans");
                if (!encodedSpans.is_array() || encodedSpans.empty() ||
                    encodedSpans.size() > kMaximumRawCopySpansPerProducer ||
                    producer.CompletionId == 0U ||
                    producer.CompletionId >= nextCompletionId ||
                    producer.AfterDrawSubmissionId >= nextSubmissionId) {
                    if (error != nullptr) {
                        *error =
                            "native PICA raw copy producer is invalid";
                    }
                    return false;
                }
                for (const auto& encodedSpan : encodedSpans) {
                    const RawCopySpan span{
                        encodedSpan.at("address").get<uint32_t>(),
                        encodedSpan.at("byte_count").get<uint32_t>()};
                    if (span.Address == 0U || span.ByteCount == 0U) {
                        if (error != nullptr) {
                            *error =
                                "native PICA raw copy span is invalid";
                        }
                        return false;
                    }
                    producer.Owned.push_back(span);
                }
                rawCopyProducers.push_back(std::move(producer));
            }
        }
        // Optional in v4: a state written before colour-target producers
        // existed restores with no write pending, as that runtime left it.
        std::map<uint32_t, ColorTargetVersion> colorTargets;
        if (const auto encodedTargets = state.find("color_target_versions");
            encodedTargets != state.end()) {
            for (const auto& encoded : *encodedTargets) {
                ColorTargetVersion target;
                const uint32_t address = encoded.at("address").get<uint32_t>();
                target.Width = encoded.at("width").get<uint32_t>();
                target.Height = encoded.at("height").get<uint32_t>();
                target.Format = encoded.at("format").get<uint8_t>();
                target.Dirty = encoded.at("dirty").get<bool>();
                for (const auto& span : encoded.at("pending")) {
                    target.Pending.push_back({span.at("address").get<uint32_t>(),
                                              span.at("byte_count").get<uint32_t>()});
                }
                if (address == 0U || target.Width == 0U || target.Height == 0U ||
                    std::any_of(target.Pending.begin(), target.Pending.end(),
                                [&](const auto& span) {
                                    return span.ByteCount == 0U || span.Address < address ||
                                           uint64_t{span.Address} + span.ByteCount >
                                               uint64_t{address} + target.ByteCount();
                                }) ||
                    target.Width > 1024U || target.Height > 1024U ||
                    target.Format > 4U) {
                    if (error != nullptr) {
                        *error = "native PICA colour target version is invalid";
                    }
                    return false;
                }
                colorTargets[address] = target;
            }
        }
        std::vector<Oot3dPicaDisplayTransferSubmission> restoredTransfers;
        const auto encodedRestoredTransfers =
            state.find("restored_display_transfers_awaiting_target");
        if (encodedRestoredTransfers != state.end()) {
            for (const auto &encoded : *encodedRestoredTransfers) {
                Oot3dPicaDisplayTransferSubmission transfer;
                transfer.CompletionId =
                    encoded.at("completion_id").get<uint64_t>();
                transfer.InputPhysicalAddress =
                    encoded.at("input_physical_address").get<uint32_t>();
                transfer.OutputPhysicalAddress =
                    encoded.at("output_physical_address").get<uint32_t>();
                transfer.Transfer = {
                    encoded.at("input_address").get<uint32_t>(),
                    encoded.at("output_address").get<uint32_t>(),
                    encoded.at("input_size").get<uint32_t>(),
                    encoded.at("output_size").get<uint32_t>(),
                    encoded.at("flags").get<uint32_t>()};
                transfer.SignalInterrupt = false;
                if (transfer.CompletionId == 0U ||
                    transfer.CompletionId >= nextCompletionId ||
                    transfer.InputPhysicalAddress == 0U ||
                    transfer.OutputPhysicalAddress == 0U) {
                    if (error != nullptr) {
                        *error = "native PICA restored display transfer state "
                                 "is invalid";
                    }
                    return false;
                }
                restoredTransfers.push_back(std::move(transfer));
            }
        }
        // Write observation is process state, not savestate state: re-arm every
        // alias of each restored producer's destination. A reference this
        // runtime cannot observe rejects the restore instead of silently
        // dropping certified ranges.
        for (const auto& producer : rawCopyProducers) {
            for (const auto& span : producer.Owned) {
                if (!ObservePhysicalWriteSpan(span.Address, span.ByteCount)) {
                    if (error != nullptr) {
                        *error =
                            "native PICA raw copy producer cannot be observed "
                            "after restore";
                    }
                    return false;
                }
            }
        }
        // Render-target images are absent from a portable savestate (see
        // below): an uncaptured colour write has no GPU version to capture
        // after a restore, exactly like the invalidated targets themselves.
        for (auto& [address, target] : colorTargets) {
            target.Dirty = false;
            target.Pending.clear();
        }
        mHardwareWrites.clear();
        mPendingDraws.clear();
        mPendingCompletions.clear();
        mPendingDisplayTransfers.clear();
        mPendingMemoryFills.clear();
        // Render-target images are host GPU resources and are deliberately
        // absent from a portable guest savestate. Remember their addresses as
        // invalidated: a CPU transfer remains safe immediately, and its GPU
        // replay is materialized only after a real draw recreates ownership.
        mKnownColorRenderTargets.clear();
        mInvalidatedColorRenderTargets =
            std::move(invalidatedColorRenderTargets);
        mInvalidatedColorRenderTargets.insert(
            knownColorRenderTargets.begin(), knownColorRenderTargets.end());
        mRestoredDisplayTransfersAwaitingTarget =
            std::move(restoredTransfers);
        mTextureSnapshotCache = std::move(textureSnapshotCache);
        mGeometrySnapshotCache = std::move(geometrySnapshotCache);
        ResetSnapshotObservation();
        mNextGeometryContentVersion = nextGeometryContentVersion;
        mGeometrySnapshotSequence = geometrySnapshotSequence;
        mNextSubmissionId = nextSubmissionId;
        mNextCompletionId = nextCompletionId;
        mRuntimeProfile = {};
        mRawCopyProducers = std::move(rawCopyProducers);
        mColorTargets = std::move(colorTargets);
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error =
                std::string("native PICA submission state decode failed: ") +
                exception.what();
        }
        return false;
    }
}

} // namespace Oot3dNativeGame
