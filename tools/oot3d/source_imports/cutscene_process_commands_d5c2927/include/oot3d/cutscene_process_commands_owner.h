#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Oot3dSourceCutsceneProcessCommands {

using undefined1 = std::uint8_t;
using undefined2 = std::uint16_t;
using undefined4 = std::uint32_t;
using byte = std::uint8_t;
using s32 = std::int32_t;
using u32 = std::uint32_t;
using uint = std::uint32_t;
using ushort = std::uint16_t;

struct CutsceneProcessCommandsLiterals {
    std::array<std::uint32_t, 69> Words{};
};

inline constexpr std::array<std::uint32_t, 69> kLiteralCellAddresses{
    0x002C5FF0U, 0x002C5FF4U, 0x002C5FF8U, 0x002C5FFCU,
    0x002C6548U, 0x002C654CU, 0x002C6550U, 0x002C6554U,
    0x002C6558U, 0x002C655CU, 0x002C6560U, 0x002C6564U,
    0x002C6568U, 0x002C656CU, 0x002C6570U, 0x002C6574U,
    0x002C6578U, 0x002C657CU, 0x002C6580U, 0x002C6584U,
    0x002C6590U, 0x002C6594U, 0x002C6964U, 0x002C6968U,
    0x002C696CU, 0x002C6970U, 0x002C6974U, 0x002C6978U,
    0x002C697CU, 0x002C6980U, 0x002C6984U, 0x002C6988U,
    0x002C698CU, 0x002C6990U, 0x002C6994U, 0x002C6998U,
    0x002C699CU, 0x002C69A0U, 0x002C69A4U, 0x002C69A8U,
    0x002C69ACU, 0x002C69B0U, 0x002C6CA8U, 0x002C6CACU,
    0x002C6CB0U, 0x002C6CB4U, 0x002C6CB8U, 0x002C6CBCU,
    0x002C6CC0U, 0x002C6CC4U, 0x002C7A5CU, 0x002C7A60U,
    0x002C7A64U, 0x002C7A68U, 0x002C80ECU, 0x002C80F0U,
    0x002C80F4U, 0x002C80F8U, 0x002C80FCU, 0x002C8100U,
    0x002C8104U, 0x002C8108U, 0x002C810CU, 0x002C8110U,
    0x002C8114U, 0x002C8118U, 0x002C811CU, 0x002C8120U,
    0x002C8124U,
};

const CutsceneProcessCommandsLiterals& ActiveLiterals();
void ReadGuestMemory(
    std::uint32_t address, void* destination, std::size_t size);
void WriteGuestMemory(
    std::uint32_t address, const void* source, std::size_t size);

template <typename T>
class GuestPtr;

template <typename T>
class GuestRef {
  public:
    using Value = std::remove_cv_t<T>;

    explicit GuestRef(std::uint32_t address) noexcept : mAddress(address) {
    }

    GuestRef(const GuestRef&) = default;

    operator Value() const {
        static_assert(std::is_trivially_copyable_v<Value>);
        static_assert(
            sizeof(Value) == 1U || sizeof(Value) == 2U ||
            sizeof(Value) == 4U || sizeof(Value) == 8U);
        Value value{};
        ReadGuestMemory(mAddress, &value, sizeof(value));
        return value;
    }

    GuestRef& operator=(const GuestRef& other)
        requires(!std::is_const_v<T>)
    {
        return *this = static_cast<Value>(other);
    }

    template <typename U>
    GuestRef& operator=(U value)
        requires(!std::is_const_v<T>)
    {
        const Value encoded = static_cast<Value>(value);
        WriteGuestMemory(mAddress, &encoded, sizeof(encoded));
        return *this;
    }

  private:
    std::uint32_t mAddress = 0U;
};

template <typename T>
class GuestPtr {
  public:
    using Element = T;

    constexpr GuestPtr() noexcept = default;
    constexpr explicit GuestPtr(std::uint32_t address) noexcept
        : mAddress(address) {
    }

    template <typename U>
    constexpr GuestPtr(GuestPtr<U> other) noexcept
        : mAddress(other.Address()) {
    }

    constexpr std::uint32_t Address() const noexcept {
        return mAddress;
    }

    constexpr explicit operator bool() const noexcept {
        return mAddress != 0U;
    }

    constexpr operator std::uint32_t() const noexcept {
        return mAddress;
    }

    GuestRef<T> operator*() const
        requires(!std::is_void_v<T>)
    {
        return GuestRef<T>(mAddress);
    }

    template <typename Index>
    GuestRef<T> operator[](Index index) const
        requires(!std::is_void_v<T> && std::is_integral_v<Index>)
    {
        return *(*this + index);
    }

    template <typename Offset>
    constexpr GuestPtr operator+(Offset offset) const noexcept
        requires(!std::is_void_v<T> && std::is_integral_v<Offset>)
    {
        const auto byteOffset =
            static_cast<std::int64_t>(offset) *
            static_cast<std::int64_t>(sizeof(T));
        return GuestPtr(
            mAddress + static_cast<std::uint32_t>(byteOffset));
    }

    template <typename Offset>
    constexpr GuestPtr operator-(Offset offset) const noexcept
        requires(!std::is_void_v<T> && std::is_integral_v<Offset>)
    {
        return *this + -static_cast<std::int64_t>(offset);
    }

    template <typename Offset>
    constexpr GuestPtr& operator+=(Offset offset) noexcept
        requires(!std::is_void_v<T> && std::is_integral_v<Offset>)
    {
        *this = *this + offset;
        return *this;
    }

    template <typename Offset>
    constexpr GuestPtr& operator-=(Offset offset) noexcept
        requires(!std::is_void_v<T> && std::is_integral_v<Offset>)
    {
        *this = *this - offset;
        return *this;
    }

    constexpr bool operator==(const GuestPtr&) const noexcept = default;

  private:
    std::uint32_t mAddress = 0U;
};

static_assert(sizeof(GuestPtr<void>) == sizeof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<GuestPtr<void>>);

struct GuestScratchArgument {
    std::size_t CoreArgumentIndex = 0U;
    void* HostData = nullptr;
    std::size_t Size = 0U;
    bool CopyIn = false;
    bool CopyOut = false;
};

std::uint32_t InvokeGuestCoreWords(
    std::uint32_t entryAddress,
    std::span<const std::uint32_t> arguments);
std::uint32_t InvokeGuestHardFloatWords(
    std::uint32_t entryAddress,
    std::span<const std::uint32_t> coreArguments,
    std::span<const std::uint32_t> vfpArguments);
std::uint32_t InvokeGuestCoreWordsWithScratch(
    std::uint32_t entryAddress,
    std::span<const std::uint32_t> arguments,
    std::span<const GuestScratchArgument> scratchArguments);

float VectorSignedToFloat(std::int32_t value, std::uint32_t mode);
float VectorUnsignedToFloat(std::uint32_t value, std::uint32_t mode);
std::uint32_t VectorFloatToUnsigned(float value, std::uint32_t mode);

template <typename T>
std::uint32_t CoreWord(T value) {
    if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>) {
        return value ? 1U : 0U;
    } else if constexpr (
        std::is_integral_v<T> || std::is_enum_v<T>) {
        return static_cast<std::uint32_t>(value);
    } else {
        return static_cast<std::uint32_t>(value);
    }
}

template <typename T>
std::uint32_t CoreWord(GuestRef<T> value) {
    return CoreWord(static_cast<typename GuestRef<T>::Value>(value));
}

template <typename T>
std::uint32_t CoreWord(GuestPtr<T> value) {
    return value.Address();
}

template <typename Return>
Return ReturnFromCoreWord(std::uint32_t value) {
    if constexpr (std::is_same_v<Return, bool>) {
        return value != 0U;
    } else if constexpr (std::is_same_v<Return, std::int16_t>) {
        return static_cast<std::int16_t>(value);
    } else {
        return static_cast<Return>(value);
    }
}

template <typename Return = std::uint32_t, typename... Args>
Return InvokeCore(std::uint32_t entryAddress, Args... arguments) {
    const std::array<std::uint32_t, sizeof...(Args)> words{
        CoreWord(arguments)...};
    const std::uint32_t result = InvokeGuestCoreWords(entryAddress, words);
    if constexpr (!std::is_void_v<Return>) {
        return ReturnFromCoreWord<Return>(result);
    }
}

template <typename T>
void* FUN_00470778(
    void* destination, GuestPtr<T> source, std::size_t size) {
    ReadGuestMemory(source.Address(), destination, size);
    return destination;
}

void FUN_0033cb90(
    std::uint32_t* cameraTrackState, std::uint32_t value,
    std::uint32_t contextAddress);
std::uint32_t Camera_SetParam(
    std::uint32_t cameraAddress, std::uint32_t parameter,
    float* value);
std::uint32_t Gameplay_CameraSetAtEye(
    std::uint32_t playAddress, std::int32_t cameraId,
    std::uint32_t atAddress, std::uint32_t* eyeVector);
std::uint32_t Gameplay_CameraSetAtEye(
    std::uint32_t playAddress, std::int32_t cameraId,
    std::uint32_t* atVector, std::uint32_t* eyeVector);
std::uint32_t FUN_00354220(
    std::uint32_t playAddress, std::int32_t cameraId, float viewAngle);
float FUN_00361490(
    std::int32_t endFrame, std::int32_t startFrame,
    std::int32_t frame);
void TitleCard_InitPlaceName(
    std::int32_t play, std::int32_t titleContext,
    std::int32_t texture, std::int32_t x, std::int32_t y,
    std::int32_t width, std::int32_t height, std::int32_t delay,
    std::int32_t unknown, float scale);
void InvokeDynamicCallback(
    GuestPtr<undefined4> callbackObject,
    GuestPtr<uint> command);

#define OOT3D_CORE_DEPENDENCY(name, address, result)             \
    template <typename... Args>                                  \
    inline result name(Args... arguments) {                      \
        return InvokeCore<result>(address##U, arguments...);     \
    }

OOT3D_CORE_DEPENDENCY(Audio_PlaySoundGeneral, 0x0037547C, void)
OOT3D_CORE_DEPENDENCY(Audio_SetBGM, 0x003655D0, void)
OOT3D_CORE_DEPENDENCY(Camera_ResetAnim, 0x002C41B0, undefined4)
OOT3D_CORE_DEPENDENCY(Camera_SetCSParams, 0x002C4130, undefined4)
OOT3D_CORE_DEPENDENCY(Flags_SetEnv, 0x00366704, void)
OOT3D_CORE_DEPENDENCY(FUN_002c411c, 0x002C411C, void)
OOT3D_CORE_DEPENDENCY(FUN_00307650, 0x00307650, undefined4)
OOT3D_CORE_DEPENDENCY(FUN_0031ff30, 0x0031FF30, void)
OOT3D_CORE_DEPENDENCY(FUN_0032b69c, 0x0032B69C, undefined4)
OOT3D_CORE_DEPENDENCY(FUN_0033cb1c, 0x0033CB1C, void)
OOT3D_CORE_DEPENDENCY(FUN_00340a1c, 0x00340A1C, void)
OOT3D_CORE_DEPENDENCY(FUN_0035af04, 0x0035AF04, void)
OOT3D_CORE_DEPENDENCY(FUN_003665fc, 0x003665FC, void)
OOT3D_CORE_DEPENDENCY(FUN_00367c7c, 0x00367C7C, void)
OOT3D_CORE_DEPENDENCY(FUN_00369f3c, 0x00369F3C, undefined4)
OOT3D_CORE_DEPENDENCY(FUN_0037073c, 0x0037073C, void)
OOT3D_CORE_DEPENDENCY(FUN_00371680, 0x00371680, void)
OOT3D_CORE_DEPENDENCY(FUN_00438014, 0x00438014, void)
OOT3D_CORE_DEPENDENCY(FUN_00490e1c, 0x00490E1C, void)
OOT3D_CORE_DEPENDENCY(FUN_00490e70, 0x00490E70, void)
OOT3D_CORE_DEPENDENCY(FUN_00490ec4, 0x00490EC4, void)
OOT3D_CORE_DEPENDENCY(FUN_00491384, 0x00491384, void)
OOT3D_CORE_DEPENDENCY(FUN_004929f4, 0x004929F4, bool)
OOT3D_CORE_DEPENDENCY(FUN_00492e94, 0x00492E94, undefined4)
OOT3D_CORE_DEPENDENCY(FUN_00493328, 0x00493328, void)
OOT3D_CORE_DEPENDENCY(FUN_00494620, 0x00494620, bool)
OOT3D_CORE_DEPENDENCY(
    Gameplay_CameraChangeSetting, 0x002C41C4, void)
OOT3D_CORE_DEPENDENCY(
    Gameplay_ChangeCameraStatus, 0x00320D7C, void)
OOT3D_CORE_DEPENDENCY(Gameplay_GetCamera, 0x0036C5BC, undefined4)
OOT3D_CORE_DEPENDENCY(Interface_ChangeAlpha, 0x0034BE04, void)
OOT3D_CORE_DEPENDENCY(
    Message_ContinueTextbox, 0x0036BE34, bool)
OOT3D_CORE_DEPENDENCY(Message_GetState, 0x003769D8, undefined4)
OOT3D_CORE_DEPENDENCY(
    Message_ShouldAdvance, 0x00346964, undefined4)
OOT3D_CORE_DEPENDENCY(oot3d_set_byte_1b6, 0x00367C48, void)
OOT3D_CORE_DEPENDENCY(
    oot3d_static_init_guard_acquire, 0x003679B4, bool)
OOT3D_CORE_DEPENDENCY(Quake_Add, 0x0036F848, std::int16_t)
OOT3D_CORE_DEPENDENCY(
    Quake_RemoveFromIdx, 0x002C41E4, undefined4)
OOT3D_CORE_DEPENDENCY(
    Quake_SetCountdown, 0x0036F628, undefined4)
OOT3D_CORE_DEPENDENCY(
    Quake_SetQuakeValues, 0x0036F6B0, undefined4)
OOT3D_CORE_DEPENDENCY(Quake_SetSpeed, 0x0036F7C0, undefined4)
OOT3D_CORE_DEPENDENCY(RendererGlobalState_Init, 0x0036788C, void)
OOT3D_CORE_DEPENDENCY(ZAR_GetCTXBByIndex, 0x00372C90, undefined4)

#undef OOT3D_CORE_DEPENDENCY

constexpr std::uint32_t CONCAT22(
    std::uint16_t high, std::uint16_t low) noexcept {
    return (static_cast<std::uint32_t>(high) << 16U) | low;
}

void Cutscene_ProcessCommands(
    std::uint32_t playAddress,
    std::uint32_t cutsceneContextAddress,
    std::uint32_t scriptAddress);

} // namespace Oot3dSourceCutsceneProcessCommands
