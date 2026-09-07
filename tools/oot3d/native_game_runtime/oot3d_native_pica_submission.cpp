#include "oot3d_native_pica_submission.h"

#include <algorithm>
#include <chrono>
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
    uint64_t hash = 1469598103934665603ULL;
    for (const uint8_t value : bytes) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
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
    WriteGeneration writeGeneration)
    : mRegions(std::move(regions)), mReadView(std::move(readView)),
      mWriteGeneration(std::move(writeGeneration)) {
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
            if (mReadView && mReadView(guestAddress, size).size() == size) {
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
  const auto guestAddress = Translate(physicalAddress, size);
  return guestAddress.has_value() && mWriteGeneration
             ? mWriteGeneration(*guestAddress, size)
             : std::nullopt;
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
      mMemory.RangeWriteGeneration(physicalAddress, byteCount);
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
    if (sourceWriteGeneration.has_value()) {
      cached->second.SourceWriteGeneration = *sourceWriteGeneration;
    }
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
        auto cached = mTextureSnapshotCache.find(key);
        const bool cacheMetadataAvailable =
            cached != mTextureSnapshotCache.end();
        const bool cacheHit =
            cacheMetadataAvailable &&
            ((cached->second.SourceBytes != nullptr &&
              cached->second.SourceBytes->size() == source->size() &&
              std::memcmp(cached->second.SourceBytes->data(),
                          source->data(), source->size()) == 0) ||
             (cached->second.SourceBytes == nullptr &&
              cached->second.SourceHash == HashSnapshotBytes(*source)));
        if (mRuntimeProfilingEnabled) {
            mRuntimeProfile.TextureSnapshotComparedBytes += source->size();
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
        resource.SharedBytes = cached->second.ResolvedBytes;
        resource.ContentHash = cached->second.ContentHash;
        resource.ContentHashAvailable = true;
        resource.BaseLevelContentHash = cached->second.BaseLevelContentHash;
        resource.BaseLevelContentHashAvailable = true;
        submission.Resources.push_back(std::move(resource));
    }
    return true;
}

bool Oot3dNativePicaSubmissionQueue::SubmitDrawPacket(
    const Oot3dPicaDrawPacket& packet, std::string* error) {
    std::lock_guard guard(mQueueMutex);
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
    if (!CaptureIndexAndVertexResources(submission, error)) {
        mPendingDraws.pop_back();
        return false;
    }
    if (mRuntimeProfilingEnabled) {
        mRuntimeProfile.VertexCaptureNanoseconds +=
            RuntimeProfileElapsedNanoseconds(phaseStart);
        phaseStart = RuntimeProfileClock::now();
    }
    if (!CaptureTextureResources(submission, error)) {
        mPendingDraws.pop_back();
        return false;
    }
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
    ++mNextSubmissionId;
    phaseStart =
        mRuntimeProfilingEnabled ? RuntimeProfileClock::now()
                                 : RuntimeProfileClock::time_point{};
    const uint32_t colorTarget =
        submission.State.Framebuffer.ColorPhysicalAddress;
    mKnownColorRenderTargets.insert(colorTarget);
    mInvalidatedColorRenderTargets.erase(colorTarget);
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
    if (mDeferGpuBackedDisplayTransfers && inputPhysical.has_value() &&
        mInvalidatedColorRenderTargets.contains(*inputPhysical)) {
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
        !mKnownColorRenderTargets.contains(*inputPhysical)) {
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
    mPendingDisplayTransfers.push_back(
        {mNextCompletionId++, mNextSubmissionId - 1U, *inputPhysical,
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
    return {
        {"format", "oot3d_native_pica_submission_state_v3"},
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
    };
}

bool Oot3dNativePicaSubmissionQueue::RestoreState(
    const nlohmann::json& state, std::string* error) {
    std::lock_guard guard(mQueueMutex);
    try {
        const std::string format = state.value("format", std::string{});
        const bool extendedTextureState =
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
        mNextGeometryContentVersion = nextGeometryContentVersion;
        mGeometrySnapshotSequence = geometrySnapshotSequence;
        mNextSubmissionId = nextSubmissionId;
        mNextCompletionId = nextCompletionId;
        mRuntimeProfile = {};
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
