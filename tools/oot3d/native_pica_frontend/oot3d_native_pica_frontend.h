#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Oot3dNativeGame {

// Shader programs are uploaded rarely and then referenced by every draw.
// Copy-on-write storage keeps each submitted packet as an immutable snapshot
// without copying the complete 16 KiB backing array on the hot draw path.
template <typename Value, size_t Size>
class Oot3dPicaSharedArray final {
  public:
    using Array = std::array<Value, Size>;

    Oot3dPicaSharedArray() : mStorage(std::make_shared<Storage>()) {}

    explicit Oot3dPicaSharedArray(Array values)
        : mStorage(std::make_shared<Storage>(std::move(values))) {}

    Oot3dPicaSharedArray& operator=(Array values) {
        mStorage = std::make_shared<Storage>(std::move(values));
        return *this;
    }

    [[nodiscard]] constexpr size_t size() const noexcept { return Size; }

    [[nodiscard]] const Value* data() const noexcept {
        return mStorage->Values.data();
    }

    [[nodiscard]] Value* data() {
        MakeMutable();
        return mStorage->Values.data();
    }

    [[nodiscard]] const Value& operator[](size_t index) const noexcept {
        return mStorage->Values[index];
    }

    [[nodiscard]] Value& operator[](size_t index) {
        MakeMutable();
        return mStorage->Values[index];
    }

    [[nodiscard]] const Array& Values() const noexcept {
        return mStorage->Values;
    }

    // Monotonic within the process and changed before every possible write.
    // It is an exact mutation identity, not a content hash.
    [[nodiscard]] uint64_t MutationIdentity() const noexcept {
        return mStorage->MutationIdentity;
    }

  private:
    struct Storage final {
        Array Values{};
        uint64_t MutationIdentity = NextMutationIdentity();

        Storage() = default;
        explicit Storage(Array values) : Values(std::move(values)) {}
    };

    static uint64_t NextMutationIdentity() noexcept {
        static std::atomic<uint64_t> next{1U};
        return next.fetch_add(1U, std::memory_order_relaxed);
    }

    void MakeMutable() {
        if (mStorage.use_count() != 1) {
            mStorage = std::make_shared<Storage>(mStorage->Values);
        } else {
            mStorage->MutationIdentity = NextMutationIdentity();
        }
    }

    std::shared_ptr<Storage> mStorage;
};

struct Oot3dPicaHardwareRegisterWrite {
    uint32_t Offset = 0;
    uint32_t PhysicalAddress = 0;
    uint32_t Value = 0;
};

enum class Oot3dGspCommandId : uint8_t {
    RequestDma = 0,
    SubmitCommandList = 1,
    MemoryFill = 2,
    DisplayTransfer = 3,
    TextureCopy = 4,
    CacheFlush = 5,
};

enum class Oot3dPicaInterruptId : uint8_t {
    Psc0 = 0,
    Psc1 = 1,
    Pdc0 = 2,
    Pdc1 = 3,
    Ppf = 4,
    P3d = 5,
    Dma = 6,
};

// Composition ownership is supplied by the native runtime at command-list
// submission time. It is intentionally independent from shaders, textures,
// and render-target addresses so renderer extensions can respect the native
// scene/UI boundary without asset-specific classification.
enum class Oot3dPicaCompositionDomain : uint8_t {
    Unknown = 0,
    Scene = 1,
    Ui = 2,
};

// Draw ordering inside the scene remains a native composition concern, not a
// material heuristic. Unknown is a first-class value for native paths whose
// ownership boundary has not been demonstrated yet.
enum class Oot3dPicaCompositionLayer : uint8_t {
    Unknown = 0,
    OpaqueWorld = 1,
    TransparentWorld = 2,
    Atmosphere = 3,
    Ui = 4,
};

enum class Oot3dPicaCompositionProvenance : uint8_t {
    Unknown = 0,
    NativeCmbDrawPass = 1,
    NativeControlFlow = 2,
    NativeUiLifecycle = 3,
};

struct Oot3dPicaCompositionAttribution {
    Oot3dPicaCompositionLayer Layer =
        Oot3dPicaCompositionLayer::Unknown;
    Oot3dPicaCompositionProvenance Provenance =
        Oot3dPicaCompositionProvenance::Unknown;
    uint32_t SourcePc = 0;
    uint32_t NativeValue = 0;

    bool operator==(const Oot3dPicaCompositionAttribution&) const = default;
};

// Absolute guest-memory byte range emitted by one demonstrated native pass.
// The end is exclusive. Ranges are attached to exactly one subsequent GSP
// command-list submission and never inferred from the resulting PICA state.
struct Oot3dPicaCommandListCompositionSpan {
    uint32_t BeginAddress = 0;
    uint32_t EndAddress = 0;
    Oot3dPicaCompositionAttribution Attribution;
};

class Oot3dPicaCommandListCompositionProvider {
  public:
    virtual ~Oot3dPicaCommandListCompositionProvider() = default;
    virtual bool TakeCommandListCompositionSpans(
        uint32_t commandListAddress, uint32_t commandListSize,
        std::vector<Oot3dPicaCommandListCompositionSpan>& spans,
        std::string* error = nullptr) = 0;
};

struct Oot3dGspCommandPacket {
    uint32_t Control = 0;
    std::array<uint32_t, 7> Parameters{};
};

struct Oot3dPicaDisplayTransfer {
    uint32_t InputAddress = 0;
    uint32_t OutputAddress = 0;
    uint32_t InputSize = 0;
    uint32_t OutputSize = 0;
    uint32_t Flags = 0;
};

struct Oot3dPicaMemoryFill {
    uint32_t StartAddress = 0;
    uint32_t EndAddress = 0;
    uint32_t Value = 0;
    uint16_t Control = 0;
};

struct Oot3dPicaMemoryFillCommand {
    std::array<Oot3dPicaMemoryFill, 2> Fills{};
};

struct Oot3dPicaRegisterWrite {
    uint32_t CommandListAddress = 0;
    uint32_t CommandListOffsetWords = 0;
    uint16_t RegisterId = 0;
    uint8_t ParameterMask = 0;
    bool RegisterSupported = false;
    uint32_t Value = 0;
    uint32_t FinalValue = 0;
};

struct Oot3dPicaShaderState {
    static constexpr size_t MaximumProgramWords = 4096;
    static constexpr size_t MaximumSwizzleWords = 4096;
    static constexpr size_t FloatUniformCount = 96;

    Oot3dPicaSharedArray<uint32_t, MaximumProgramWords> Program;
    Oot3dPicaSharedArray<uint32_t, MaximumSwizzleWords> Swizzles;
    std::array<std::array<float, 4>, FloatUniformCount> FloatUniforms{};
    std::array<std::array<uint8_t, 4>, 4> IntegerUniforms{};
    std::array<bool, 16> BooleanUniforms{};
    size_t ProgramWordCount = 0;
    size_t SwizzleWordCount = 0;
};

struct Oot3dPicaProcTexLutState {
    std::array<uint32_t, 128> Noise{};
    std::array<uint32_t, 128> ColorMap{};
    std::array<uint32_t, 128> AlphaMap{};
    std::array<uint32_t, 256> Color{};
    std::array<uint32_t, 256> ColorDifference{};
};

struct Oot3dPicaLightingLutState {
    static constexpr size_t TableCount = 24U;
    static constexpr size_t EntryCount = 256U;
    static constexpr size_t PackedEntryCount = TableCount * EntryCount;

    std::array<uint32_t, PackedEntryCount> PackedEntries{};
    uint64_t ContentHash = 0;
    bool ContentHashAvailable = false;

    [[nodiscard]] uint32_t& Entry(size_t table, size_t index) {
        return PackedEntries[table * EntryCount + index];
    }

    [[nodiscard]] const uint32_t& Entry(size_t table, size_t index) const {
        return PackedEntries[table * EntryCount + index];
    }
};

[[nodiscard]] uint64_t ComputeOot3dPicaLightingLutContentHash(
    const Oot3dPicaLightingLutState& state);

struct Oot3dPicaDrawPacket {
    Oot3dPicaDrawPacket() = default;

    Oot3dPicaDrawPacket(
        uint32_t commandListAddress, uint32_t commandListOffsetWords,
        Oot3dPicaCompositionDomain compositionDomain,
        Oot3dPicaCompositionAttribution composition, bool indexed,
        const std::array<uint32_t, 0x300>& registers,
        const Oot3dPicaShaderState& vertexShader,
        const Oot3dPicaShaderState& geometryShader,
        const std::array<uint32_t, 128>& fogLut,
        const Oot3dPicaProcTexLutState& procTexLuts,
        std::shared_ptr<const Oot3dPicaLightingLutState> lightingLuts,
        const std::array<std::array<float, 4>, 16>& defaultAttributes)
        : CommandListAddress(commandListAddress),
          CommandListOffsetWords(commandListOffsetWords),
          CompositionDomain(compositionDomain), Composition(composition),
          Indexed(indexed), Registers(registers), VertexShader(vertexShader),
          GeometryShader(geometryShader), FogLut(fogLut),
          ProcTexLuts(procTexLuts), LightingLuts(std::move(lightingLuts)),
          DefaultAttributes(defaultAttributes) {}

    uint32_t CommandListAddress = 0;
    uint32_t CommandListOffsetWords = 0;
    Oot3dPicaCompositionDomain CompositionDomain =
        Oot3dPicaCompositionDomain::Unknown;
    Oot3dPicaCompositionAttribution Composition;
    bool Indexed = false;
    std::array<uint32_t, 0x300> Registers{};
    Oot3dPicaShaderState VertexShader;
    Oot3dPicaShaderState GeometryShader;
    std::array<uint32_t, 128> FogLut{};
    Oot3dPicaProcTexLutState ProcTexLuts;
    std::shared_ptr<const Oot3dPicaLightingLutState> LightingLuts;
    std::array<std::array<float, 4>, 16> DefaultAttributes{};
};

class Oot3dPicaPacketSink {
  public:
    virtual ~Oot3dPicaPacketSink() = default;
    virtual bool SubmitHardwareRegisterWrite(
        const Oot3dPicaHardwareRegisterWrite& write,
        std::string* error = nullptr) = 0;
    virtual bool SubmitDrawPacket(const Oot3dPicaDrawPacket& packet,
                                  std::string* error = nullptr) = 0;
    virtual bool SubmitInterruptAfterGpuWork(
        Oot3dPicaInterruptId interrupt, std::string* error = nullptr) {
        (void)interrupt;
        (void)error;
        return true;
    }
    virtual bool SubmitDisplayTransfer(
        const Oot3dPicaDisplayTransfer&, bool* deferredToGpu,
        std::string* error = nullptr,
        bool* cpuCopySuppressed = nullptr) {
        if (deferredToGpu != nullptr) {
            *deferredToGpu = false;
        }
        if (cpuCopySuppressed != nullptr) {
            *cpuCopySuppressed = false;
        }
        (void)error;
        return true;
    }
    virtual bool SubmitMemoryFill(
        const Oot3dPicaMemoryFillCommand&, bool* deferredToGpu,
        std::string* error = nullptr) {
        if (deferredToGpu != nullptr) {
            *deferredToGpu = false;
        }
        (void)error;
        return true;
    }
};

class Oot3dNativePicaFrontend {
  public:
    explicit Oot3dNativePicaFrontend(
        Oot3dPicaPacketSink* packetSink = nullptr);
    void SetPacketSink(Oot3dPicaPacketSink* packetSink);
    void SetDiagnosticHistoryEnabled(bool enabled);
    void SetCommandListCompositionDomain(
        Oot3dPicaCompositionDomain domain) noexcept;
    bool SetNextCommandListCompositionSpans(
        uint32_t commandListAddress, uint32_t commandListSize,
        std::span<const Oot3dPicaCommandListCompositionSpan> spans,
        std::string* error = nullptr);

    bool WriteHardwareRegisters(uint32_t baseOffset,
                                std::span<const uint32_t> values,
                                std::string* error = nullptr);
    bool WriteHardwareRegistersWithMask(
        uint32_t baseOffset, std::span<const uint32_t> values,
        std::span<const uint32_t> masks, std::string* error = nullptr);
    bool SubmitGspCommand(const Oot3dGspCommandPacket& command,
                          std::span<const uint32_t> commandListWords = {},
                          std::string* error = nullptr,
                          bool* displayTransferDeferredToGpu = nullptr,
                          bool* memoryFillDeferredToGpu = nullptr,
                          bool* displayTransferCpuCopySuppressed = nullptr);
    std::optional<uint32_t> ReadHardwareRegister(uint32_t offset) const;
    size_t HardwareWriteCount() const;
    std::span<const Oot3dPicaHardwareRegisterWrite> PendingWrites() const;
    std::vector<Oot3dPicaHardwareRegisterWrite> TakePendingWrites();
    std::optional<uint32_t> ReadPicaRegister(uint16_t registerId) const;
    std::span<const Oot3dGspCommandPacket> PendingGspCommands() const;
    std::span<const Oot3dPicaRegisterWrite> PendingRegisterWrites() const;
    std::span<const Oot3dPicaDrawPacket> PendingDrawPackets() const;
    std::vector<Oot3dPicaInterruptId> TakePendingInterrupts();
    nlohmann::json CaptureState() const;
    bool RestoreState(const nlohmann::json& state,
                      std::string* error = nullptr);

  private:
    struct PackedAttributeQueue {
        std::array<uint32_t, 4> Words{};
        size_t Count = 0;
    };

    void ApplyShaderRegisterSideEffects(uint16_t registerId,
                                        uint32_t writeValue);
    void ApplyShaderStageRegisterSideEffects(
        uint16_t registerId, uint32_t writeValue, uint16_t stageBase,
        Oot3dPicaShaderState& shader, PackedAttributeQueue& uniformQueue,
        bool mirrorToGeometryShader);

    Oot3dPicaPacketSink* mPacketSink = nullptr;
    std::unordered_map<uint32_t, uint32_t> mHardwareRegisters;
    std::vector<Oot3dPicaHardwareRegisterWrite> mPendingWrites;
    std::array<uint32_t, 0x300> mPicaRegisters{};
    std::vector<Oot3dGspCommandPacket> mPendingGspCommands;
    std::vector<Oot3dPicaRegisterWrite> mPendingRegisterWrites;
    std::vector<Oot3dPicaDrawPacket> mPendingDrawPackets;
    std::vector<Oot3dPicaInterruptId> mPendingInterrupts;
    Oot3dPicaShaderState mVertexShader;
    Oot3dPicaShaderState mGeometryShader;
    std::array<uint32_t, 128> mFogLut{};
    Oot3dPicaProcTexLutState mProcTexLuts;
    std::shared_ptr<Oot3dPicaLightingLutState> mLightingLuts =
        std::make_shared<Oot3dPicaLightingLutState>();
    std::array<std::array<float, 4>, 16> mDefaultAttributes{};
    PackedAttributeQueue mVertexUniformQueue;
    PackedAttributeQueue mGeometryUniformQueue;
    PackedAttributeQueue mDefaultAttributeQueue;
    size_t mHardwareWriteCount = 0;
    bool mDiagnosticHistoryEnabled = true;
    Oot3dPicaCompositionDomain mCommandListCompositionDomain =
        Oot3dPicaCompositionDomain::Unknown;
    uint32_t mCompositionSpanCommandListAddress = 0;
    uint32_t mCompositionSpanCommandListSize = 0;
    std::vector<Oot3dPicaCommandListCompositionSpan>
        mNextCommandListCompositionSpans;
};

} // namespace Oot3dNativeGame
