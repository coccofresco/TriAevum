#pragma once

#include "oot3d_native_pica_draw_state.h"

#include "fast/renderer3ds/pica_native_state.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Oot3dNativeGame {

inline uint64_t HashOot3dPicaSnapshot(std::span<const uint8_t> bytes) {
    uint64_t hash = 1469598103934665603ULL;
    for (const auto value : bytes) hash = (hash ^ value) * 1099511628211ULL;
    return hash;
}

class NativeA32Memory;

struct Oot3dPicaPhysicalMemoryRegion {
    uint32_t PhysicalBaseAddress = 0;
    uint32_t GuestBaseAddress = 0;
    size_t Size = 0;
};

class Oot3dPicaPhysicalMemoryView {
  public:
    using ReadView = std::function<std::span<const uint8_t>(
        uint32_t guestAddress, size_t size)>;
    using WriteGeneration = std::function<std::optional<uint64_t>(
        uint32_t guestAddress, size_t size)>;
    using MappingCheck = std::function<bool(uint32_t guestAddress, size_t size)>;
    using GuestAliasVisitor = std::function<bool(uint32_t guestAddress, size_t size)>;

    Oot3dPicaPhysicalMemoryView(
        const NativeA32Memory& memory,
        std::vector<Oot3dPicaPhysicalMemoryRegion> regions);
    Oot3dPicaPhysicalMemoryView(
        std::vector<Oot3dPicaPhysicalMemoryRegion> regions,
        ReadView readView, WriteGeneration writeGeneration = {},
        MappingCheck mappingCheck = {});

    std::optional<uint32_t> Translate(uint32_t physicalAddress,
                                      size_t size) const;
    std::optional<uint32_t> TranslateGuest(uint32_t guestAddress,
                                           size_t size) const;
    // Visit every mapped alias without allocating. False means no alias exists
    // or a visitor rejected one; observation must cover every alias.
    bool ForEachGuestAlias(uint32_t physicalAddress, size_t size,
                           const GuestAliasVisitor& visitor) const;
    // Longest mapped segment starting at guestAddress: a guest write may cross
    // the boundary between two mapped views, and each segment still has to be
    // attributed to its own physical range.
    std::optional<std::pair<uint32_t, size_t>> GuestSegment(
        uint32_t guestAddress, size_t size) const;
    // Mirror a completed CPU write through its physical aliases before GPU
    // ownership is invalidated. Never reads the untouched device-owned bytes.
    bool SynchronizeGuestWrite(NativeA32Memory& memory,
                               uint32_t guestAddress, size_t size) const;
    std::optional<std::span<const uint8_t>> View(
        uint32_t physicalAddress, size_t size) const;
    std::optional<uint64_t> RangeWriteGeneration(uint32_t physicalAddress,
                                                 size_t size) const;
    bool Read(uint32_t physicalAddress, std::span<uint8_t> output) const;
    bool ReadGuest(uint32_t guestAddress,
                   std::span<uint8_t> output) const;
    // The buffer a PICA command list jumps to (GPUREG_CMDBUF_JUMP): its guest
    // address and words. Shape of Oot3dNativePicaFrontend::CommandBufferReader.
    bool ReadCommandBuffer(uint32_t physicalAddress, uint32_t byteSize,
                           uint32_t* guestAddress,
                           std::vector<uint32_t>* words) const;
    std::span<const uint8_t> ViewGuest(uint32_t address, size_t size) const {
        if (!mReadView) return {};
        const auto view = mReadView(address, size);
        return view.size() == size ? view : std::span<const uint8_t>{};
    }

  private:
    std::vector<Oot3dPicaPhysicalMemoryRegion> mRegions;
    ReadView mReadView;
    WriteGeneration mWriteGeneration;
    MappingCheck mMappingCheck;
};

enum class Oot3dPicaResourceKind : uint8_t {
    IndexBuffer,
    VertexLoader,
    Texture,
};

struct Oot3dPicaResourceSnapshot {
    Oot3dPicaResourceKind Kind = Oot3dPicaResourceKind::VertexLoader;
    uint8_t Slot = 0;
    uint32_t PhysicalAddress = 0;
    uint32_t FirstElement = 0;
    std::vector<uint8_t> Bytes;
    std::shared_ptr<const std::vector<uint8_t>> SharedBytes;
    uint64_t ContentHash = 0;
    bool ContentHashAvailable = false;
    uint64_t ContentVersion = 0;
    bool ContentVersionAvailable = false;
    uint64_t BaseLevelContentHash = 0;
    bool BaseLevelContentHashAvailable = false;
    // Optional presentation texture extent; native decoded PICA state stays intact.
    uint16_t ReplacementWidth = 0;
    uint16_t ReplacementHeight = 0;
    // Explicit GPU producers of this immutable snapshot. Referenced bytes are
    // not CPU payload: their slots in ResolvedBytes are zeroed and must be
    // replaced by the immutable GPU version. Only uncovered bytes come from RAM.
    std::vector<Fast::Renderer3ds::PicaTextureCopyReference> CopyReferences;

    [[nodiscard]] std::span<const uint8_t> ResolvedBytes() const {
        return SharedBytes != nullptr
                   ? std::span<const uint8_t>(*SharedBytes)
                   : std::span<const uint8_t>(Bytes);
    }
};

struct Oot3dPicaDrawSubmission {
    Oot3dPicaDrawSubmission() = default;

    Oot3dPicaDrawSubmission(uint64_t id,
                            const Oot3dPicaDrawPacket& packet)
        : Id(id), Packet(packet) {
    }

    uint64_t Id = 0;
    Oot3dPicaDrawPacket Packet;
    Oot3dPicaDecodedDrawState State;
    uint32_t MinimumVertexIndex = 0;
    uint32_t MaximumVertexIndex = 0;
    std::vector<Oot3dPicaResourceSnapshot> Resources;
};

struct Oot3dPicaCompletionSubmission {
    uint64_t Id = 0;
    uint64_t AfterDrawSubmissionId = 0;
    Oot3dPicaInterruptId Interrupt = Oot3dPicaInterruptId::P3d;
};

struct Oot3dPicaDisplayTransferSubmission {
    uint64_t CompletionId = 0;
    uint64_t AfterDrawSubmissionId = 0;
    uint32_t InputPhysicalAddress = 0;
    uint32_t OutputPhysicalAddress = 0;
    Oot3dPicaDisplayTransfer Transfer;
    // A restored render target may need one GPU replay after its portable CPU
    // fallback already delivered the guest PPF interrupt.
    bool SignalInterrupt = true;
    // An ordered capture of a draw-written colour target that a later draw
    // samples (see PicaDisplayTransferView::RenderTargetCapture). Never a guest
    // operation: SignalInterrupt is false and Transfer carries only the
    // target's native extent and format.
    bool RenderTargetCapture = false;
};

struct Oot3dPicaMemoryFillSubmission {
    uint64_t CompletionId = 0;
    uint64_t BeforeDrawSubmissionId = 0;
    uint32_t StartPhysicalAddress = 0;
    uint32_t EndPhysicalAddress = 0;
    uint32_t Value = 0;
    uint16_t Control = 0;
    std::optional<Oot3dPicaInterruptId> Interrupt;
};

struct Oot3dPicaSubmissionBatch {
    std::vector<Oot3dPicaDrawSubmission> Draws;
    std::vector<Oot3dPicaCompletionSubmission> Completions;
    std::vector<Oot3dPicaDisplayTransferSubmission> DisplayTransfers;
    std::vector<Oot3dPicaMemoryFillSubmission> MemoryFills;

    bool Empty() const noexcept {
        return Draws.empty() && Completions.empty() &&
               DisplayTransfers.empty() && MemoryFills.empty();
    }
};

struct Oot3dPicaSubmissionRuntimeProfile {
    uint64_t DrawCalls = 0;
    uint64_t TotalNanoseconds = 0;
    uint64_t PacketCopyNanoseconds = 0;
    uint64_t DecodeNanoseconds = 0;
    uint64_t VertexCaptureNanoseconds = 0;
    uint64_t TextureCaptureNanoseconds = 0;
    uint64_t EnqueueNanoseconds = 0;
    uint64_t IndexBytes = 0;
    uint64_t VertexBytes = 0;
    uint64_t TextureBytes = 0;
    uint64_t TextureSnapshotCacheHits = 0;
    uint64_t TextureSnapshotCacheMisses = 0;
    uint64_t TextureSnapshotComparedBytes = 0;
    uint64_t TextureSnapshotCopiedBytes = 0;
    uint64_t GeometrySnapshotCacheHits = 0;
    uint64_t GeometrySnapshotCacheMisses = 0;
    uint64_t GeometrySnapshotVersionHits = 0;
    uint64_t GeometrySnapshotVersionMisses = 0;
    uint64_t GeometrySnapshotComparedBytes = 0;
    uint64_t GeometrySnapshotCopiedBytes = 0;
    uint64_t TextureCopyReferences = 0;
    uint64_t TextureCopyReferencedBytes = 0;
};

class Oot3dNativePicaSubmissionQueue final : public Oot3dPicaPacketSink {
  public:
    using TexturePayloadTransform =
        std::function<void(const Oot3dPicaTextureState&,
                           std::span<std::uint8_t>)>;
    using TextureSnapshotTransform = std::function<void(
        const Oot3dPicaTextureState&, Oot3dPicaResourceSnapshot&)>;
    void SetTextureSnapshotTransform(TextureSnapshotTransform transform) {
        mTextureSnapshotTransform = std::move(transform);
    }

    explicit Oot3dNativePicaSubmissionQueue(
        Oot3dPicaPhysicalMemoryView memory,
        bool deferGpuBackedDisplayTransfers = false);
    const Oot3dPicaPhysicalMemoryView& PhysicalMemory() const noexcept {
        return mMemory;
    }
    void SetRuntimeProfilingEnabled(bool enabled);
    void SetTexturePayloadTransform(TexturePayloadTransform transform);
    Oot3dPicaSubmissionRuntimeProfile RuntimeProfile() const noexcept;

    // Guest-write observation for raw copy destinations. `registrar` traps
    // future writes into [guestAddress, guestAddress + byteCount) and returns
    // false when an escaped mutable pointer makes that range unobservable.
    // Without a registrar a raw copy cannot be assigned to the GPU: the queue
    // reports the failure instead of assuming ownership.
    using GuestWriteRegistrar =
        std::function<bool(uint32_t guestAddress, size_t byteCount)>;
    void BindGuestWriteObservation(GuestWriteRegistrar registrar);
    [[nodiscard]] bool HasGuestWriteObservation() const noexcept;
    // Entry point for the process memory's write observer. A guest write into a
    // producer's owned subrange removes exactly the bytes it covers, including a
    // same-value write, so a later snapshot never samples stale GPU bytes. Must
    // not be reached from the renderer's own materialization: that scope is
    // suppressed by the memory object.
    void NotifyGuestWrite(uint32_t guestAddress, size_t byteCount);
    // Appends the producers that still own copy destination bytes right now:
    // the versions a future snapshot can reference. The caller combines them
    // with the references of retained plans to pin the versions the backend
    // must keep alive; a superseded or CPU-overwritten producer is absent.
    void CollectLiveCopyReferences(
        std::vector<Fast::Renderer3ds::PicaTextureCopyReference>& out) const;

    bool SubmitHardwareRegisterWrite(
        const Oot3dPicaHardwareRegisterWrite& write,
        std::string* error = nullptr) override;
    bool SubmitDrawPacket(const Oot3dPicaDrawPacket& packet,
                          std::string* error = nullptr) override;
    bool SubmitInterruptAfterGpuWork(
        Oot3dPicaInterruptId interrupt,
        std::string* error = nullptr) override;
    bool SubmitDisplayTransfer(
        const Oot3dPicaDisplayTransfer& transfer, bool* deferredToGpu,
        std::string* error = nullptr,
        bool* cpuCopySuppressed = nullptr) override;
    bool SubmitMemoryFill(
        const Oot3dPicaMemoryFillCommand& command, bool* deferredToGpu,
        std::string* error = nullptr) override;

    std::span<const Oot3dPicaDrawSubmission> PendingDraws() const;
    std::vector<Oot3dPicaDrawSubmission> TakePendingDraws();
    void TakePendingDraws(
        std::vector<Oot3dPicaDrawSubmission>& destination);
    std::span<const Oot3dPicaCompletionSubmission> PendingCompletions() const;
    std::vector<Oot3dPicaCompletionSubmission> TakePendingCompletions();
    std::span<const Oot3dPicaDisplayTransferSubmission>
    PendingDisplayTransfers() const;
    std::vector<Oot3dPicaDisplayTransferSubmission>
    TakePendingDisplayTransfers();
    std::span<const Oot3dPicaMemoryFillSubmission> PendingMemoryFills() const;
    std::vector<Oot3dPicaMemoryFillSubmission> TakePendingMemoryFills();
    Oot3dPicaSubmissionBatch TakePendingBatch();
    bool IsQuiescent() const noexcept;
    void RestoreGpuColorRenderTargetOwnership(
        std::span<const uint32_t> colorPhysicalAddresses);
    nlohmann::json CaptureState() const;
    bool RestoreState(const nlohmann::json& state,
                      std::string* error = nullptr);

  private:
    struct GeometrySnapshotKey {
      Oot3dPicaResourceKind Kind = Oot3dPicaResourceKind::VertexLoader;
      uint8_t Slot = 0;
      uint32_t PhysicalAddress = 0;
      size_t Size = 0;

      bool operator==(const GeometrySnapshotKey &) const = default;
    };

    struct GeometrySnapshotKeyHash {
      size_t operator()(const GeometrySnapshotKey &key) const noexcept;
    };

    struct GeometrySnapshotCacheEntry {
      std::shared_ptr<const std::vector<uint8_t>> Bytes;
      uint64_t ContentHash = 0;
      uint64_t ContentVersion = 0;
      uint64_t SourceWriteGeneration = 0;
      uint64_t LastUsedSequence = 0;
    };

    struct TextureSnapshotKey {
      uint32_t PhysicalAddress = 0;
      uint16_t Width = 0;
      uint16_t Height = 0;
      uint8_t Format = 0;
      uint8_t Type = 0;
      uint8_t WrapS = 0;
      uint8_t WrapT = 0;
      bool MinLinear = false;
      bool MagLinear = false;
      bool MipLinear = false;
      int16_t LodBiasRaw = 0;
      uint8_t MinMipLevel = 0;
      uint8_t MaxMipLevel = 0;

      bool operator==(const TextureSnapshotKey &) const = default;
    };

    struct TextureSnapshotKeyHash {
        size_t operator()(const TextureSnapshotKey& key) const noexcept;
    };

    struct TextureSnapshotCacheEntry {
        std::shared_ptr<const std::vector<uint8_t>> SourceBytes;
        std::shared_ptr<const std::vector<uint8_t>> ResolvedBytes;
        uint64_t SourceHash = 0;
        uint64_t ContentHash = 0;
        uint64_t BaseLevelContentHash = 0;
        uint64_t SourceWriteGeneration = 0;
    };

    // Ordered raw-copy producer: the physical destination subranges it still
    // owns, in registration order. A newer copy truncates an older producer's
    // overlapping subranges and a guest write truncates the bytes it covers, so
    // the newest registration is always the owner of every byte it holds.
    struct RawCopySpan {
        uint32_t Address = 0;
        uint32_t ByteCount = 0;
    };

    struct RawCopyProducer {
        uint64_t CompletionId = 0;
        uint64_t AfterDrawSubmissionId = 0;
        std::vector<RawCopySpan> Owned;

        bool operator==(const RawCopyProducer&) const = default;
    };

    // Removes [address, address + byteCount) from every span, keeping the parts
    // a write or a newer copy did not touch.
    static void SubtractOwnedSpan(std::vector<RawCopySpan>& spans,
                                  uint32_t address, size_t byteCount);
    // Certify all aliases, including pointers that escaped since the last draw.
    bool ObservePhysicalWriteSpan(uint32_t address, size_t byteCount);
    std::optional<uint64_t> SnapshotWriteGeneration(uint32_t address, size_t byteCount);
    void ResetSnapshotObservation();
    bool RegisterRawCopyProducer(uint64_t completionId,
                                 uint64_t afterDrawSubmissionId,
                                 std::span<const RawCopySpan> destinations,
                                 std::string* error);
    void InvalidateRawCopyOwnership(uint32_t physicalAddress,
                                    size_t byteCount);
    void CollectCopyReferences(
        uint64_t drawSubmissionId, uint32_t physicalAddress, size_t byteCount,
        std::vector<Fast::Renderer3ds::PicaTextureCopyReference>& out) const;

    bool CaptureIndexAndVertexResources(Oot3dPicaDrawSubmission &submission,
                                        std::string *error);
    bool CaptureGeometryResource(Oot3dPicaResourceKind kind, uint8_t slot,
                                 uint32_t physicalAddress,
                                 uint32_t firstElement, size_t byteCount,
                                 Oot3dPicaResourceSnapshot &resource,
                                 std::string *error);
    bool CaptureTextureResources(Oot3dPicaDrawSubmission &submission,
                                 std::string *error);

    // A colour target draws write: its native geometry, and whether it was
    // written since it last became a producer, so a later texture read over its
    // bytes publishes the ordered capture that read must see.
    struct ColorTargetVersion {
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint8_t Format = 0;
        bool Dirty = false;
        // The bytes the GPU wrote since the last capture and nothing has
        // written since (guest writes, newer copies and transfers subtract
        // theirs): what a capture of this version may own.
        std::vector<RawCopySpan> Pending;
        uint32_t ByteCount() const {
            return Width * Height * (Format == 0U ? 4U : Format == 1U ? 3U : 2U);
        }
    };
    // Publishes the captures the enabled textures of this draw read from dirty
    // colour targets, before the draw's own texture provenance is taken.
    void PublishSampledColorTargets(const Oot3dPicaDrawSubmission &submission);
    // A draw's colour write: the target's bytes leave every older producer
    // and become its pending, observed write.
    void MarkColorTargetWritten(uint32_t address);
    // A tiled DisplayTransfer result is a GPU producer of its output bytes.
    void RegisterTiledTransferProducer(uint64_t completionId,
                                       uint64_t afterDrawSubmissionId,
                                       uint32_t outputPhysicalAddress,
                                       const Oot3dPicaDisplayTransfer &transfer);

    Oot3dPicaPhysicalMemoryView mMemory;
    mutable std::mutex mQueueMutex;
    std::vector<Oot3dPicaHardwareRegisterWrite> mHardwareWrites;
    std::vector<Oot3dPicaDrawSubmission> mPendingDraws;
    std::vector<Oot3dPicaCompletionSubmission> mPendingCompletions;
    std::vector<Oot3dPicaDisplayTransferSubmission> mPendingDisplayTransfers;
    std::vector<Oot3dPicaMemoryFillSubmission> mPendingMemoryFills;
    std::unordered_set<uint32_t> mKnownColorRenderTargets;
    std::unordered_set<uint32_t> mInvalidatedColorRenderTargets;
    std::vector<Oot3dPicaDisplayTransferSubmission>
        mRestoredDisplayTransfersAwaitingTarget;
    std::vector<RawCopyProducer> mRawCopyProducers;
    std::map<uint32_t, ColorTargetVersion> mColorTargets;
    uint64_t mColorTargetPreservedStaleWrites = 0;
    GuestWriteRegistrar mGuestWriteRegistrar;
    bool mDeferGpuBackedDisplayTransfers = false;
    bool mRuntimeProfilingEnabled = false;
    TexturePayloadTransform mTexturePayloadTransform;
    TextureSnapshotTransform mTextureSnapshotTransform;
    std::unordered_map<TextureSnapshotKey, TextureSnapshotCacheEntry,
                       TextureSnapshotKeyHash>
        mTextureSnapshotCache;
    std::unordered_map<GeometrySnapshotKey, GeometrySnapshotCacheEntry,
                       GeometrySnapshotKeyHash>
        mGeometrySnapshotCache;
    Oot3dPicaSubmissionRuntimeProfile mRuntimeProfile;
    uint64_t mNextGeometryContentVersion = 1;
    uint64_t mGeometrySnapshotSequence = 0;
    uint64_t mNextSubmissionId = 1;
    uint64_t mNextCompletionId = 1;
};

} // namespace Oot3dNativeGame
