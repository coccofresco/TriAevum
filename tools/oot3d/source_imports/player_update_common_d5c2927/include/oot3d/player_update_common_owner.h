#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Oot3dSourcePlayerUpdateCommon {

using sbyte = std::int8_t;
using byte = std::uint8_t;
using undefined1 = std::uint8_t;
using ushort = std::uint16_t;
using undefined2 = std::uint16_t;
using uint = std::uint32_t;
using undefined4 = std::uint32_t;
using undefined8 = std::uint64_t;
using ulonglong = std::uint64_t;
using u32 = std::uint32_t;
using uintptr_t = std::uint32_t;

struct PlayerUpdateCommonLiterals {
    std::array<std::uint32_t, 136> Words{};
};

inline constexpr std::array<std::uint32_t, 136>
    kLiteralCellAddresses{
    0x00251304U,
    0x00251308U,
    0x0025130CU,
    0x00251310U,
    0x00251314U,
    0x00251318U,
    0x0025131CU,
    0x00251320U,
    0x00251324U,
    0x00251328U,
    0x0025132CU,
    0x00251330U,
    0x00251334U,
    0x00251338U,
    0x0025133CU,
    0x00251340U,
    0x00251344U,
    0x00251348U,
    0x0025134CU,
    0x00251350U,
    0x00251354U,
    0x00251358U,
    0x0025135CU,
    0x00251360U,
    0x00251364U,
    0x00251368U,
    0x00251714U,
    0x00251718U,
    0x0025171CU,
    0x00251720U,
    0x00251724U,
    0x00251728U,
    0x0025172CU,
    0x00251730U,
    0x00251734U,
    0x00251738U,
    0x0025173CU,
    0x00251740U,
    0x00251744U,
    0x00251CD4U,
    0x00251CD8U,
    0x00251CDCU,
    0x00251CE0U,
    0x00251CE4U,
    0x00251CE8U,
    0x00251CECU,
    0x00251CF0U,
    0x00251CF4U,
    0x00251CF8U,
    0x00251CFCU,
    0x00251D00U,
    0x00251D04U,
    0x00251D08U,
    0x00251D0CU,
    0x00251D10U,
    0x00251D14U,
    0x00251D1CU,
    0x00251D20U,
    0x00251D24U,
    0x00251D28U,
    0x00251D30U,
    0x00252118U,
    0x0025211CU,
    0x00252120U,
    0x00252124U,
    0x00252128U,
    0x0025212CU,
    0x00252134U,
    0x00252138U,
    0x0025213CU,
    0x00252140U,
    0x00252144U,
    0x00252148U,
    0x0025214CU,
    0x00252150U,
    0x00252154U,
    0x00252158U,
    0x0025215CU,
    0x00252160U,
    0x00252164U,
    0x00252168U,
    0x002524D4U,
    0x002524D8U,
    0x002524E0U,
    0x002524E4U,
    0x002524E8U,
    0x002524F0U,
    0x002524F4U,
    0x002524F8U,
    0x002524FCU,
    0x00252500U,
    0x00252504U,
    0x00252508U,
    0x0025250CU,
    0x00252510U,
    0x00252914U,
    0x00252918U,
    0x0025291CU,
    0x00252920U,
    0x00252924U,
    0x00252928U,
    0x0025292CU,
    0x00252930U,
    0x00252934U,
    0x00252938U,
    0x0025293CU,
    0x00252940U,
    0x00252944U,
    0x00252948U,
    0x0025294CU,
    0x00252950U,
    0x00252954U,
    0x00252958U,
    0x0025295CU,
    0x00252960U,
    0x00252964U,
    0x00252DC0U,
    0x00252DC4U,
    0x00252DC8U,
    0x00252DCCU,
    0x00252DD0U,
    0x00252DD4U,
    0x00252DDCU,
    0x00252DE0U,
    0x00252DE4U,
    0x00252DE8U,
    0x00252DECU,
    0x00253408U,
    0x0025340CU,
    0x00253410U,
    0x00253414U,
    0x00253418U,
    0x0025341CU,
    0x00253420U,
    0x00253424U,
    0x00253428U,
    };

const PlayerUpdateCommonLiterals& ActiveLiterals();
std::uint32_t OwnerLocalAddress(std::uint32_t offset);
void ReadGuestMemory(
    std::uint32_t address, void* destination, std::size_t size);
void WriteGuestMemory(
    std::uint32_t address, const void* source, std::size_t size);

template <typename T>
T ReadGuestPod(std::uint32_t address) {
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
    ReadGuestMemory(address, &value, sizeof(value));
    return value;
}

template <typename T>
void WriteGuestPod(std::uint32_t address, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    WriteGuestMemory(address, &value, sizeof(value));
}

template <typename T>
class GuestPtr;

template <typename T>
class GuestRef {
  public:
    using Value = std::remove_cv_t<T>;

    explicit GuestRef(std::uint32_t address) noexcept : mAddress(address) {}

    operator Value() const {
        return ReadGuestPod<Value>(mAddress);
    }

    template <typename U>
    GuestRef& operator=(U&& value)
        requires(!std::is_const_v<T>)
    {
        const Value encoded = static_cast<Value>(
            std::forward<U>(value));
        WriteGuestPod(mAddress, encoded);
        return *this;
    }

    GuestRef& operator=(const GuestRef& other)
        requires(!std::is_const_v<T>)
    {
        return *this = static_cast<Value>(other);
    }

    template <typename U>
    GuestRef& operator|=(U value)
        requires(!std::is_const_v<T>)
    {
        return *this = static_cast<Value>(
            static_cast<Value>(*this) | static_cast<Value>(value));
    }

    template <typename U>
    GuestRef& operator&=(U value)
        requires(!std::is_const_v<T>)
    {
        return *this = static_cast<Value>(
            static_cast<Value>(*this) & static_cast<Value>(value));
    }

    template <typename U>
    GuestRef& operator+=(U value)
        requires(!std::is_const_v<T>)
    {
        return *this = static_cast<Value>(
            static_cast<Value>(*this) + static_cast<Value>(value));
    }

    template <typename U>
    GuestRef& operator-=(U value)
        requires(!std::is_const_v<T>)
    {
        return *this = static_cast<Value>(
            static_cast<Value>(*this) - static_cast<Value>(value));
    }

    Value operator++(int)
        requires(!std::is_const_v<T>)
    {
        const Value previous = static_cast<Value>(*this);
        *this = static_cast<Value>(previous + 1);
        return previous;
    }

    Value operator--(int)
        requires(!std::is_const_v<T>)
    {
        const Value previous = static_cast<Value>(*this);
        *this = static_cast<Value>(previous - 1);
        return previous;
    }

    std::uint32_t Address() const noexcept {
        return mAddress;
    }

  private:
    std::uint32_t mAddress = 0U;
};

template <typename T>
GuestRef<T> GuestIndirectRef(std::uint32_t pointerAddress) {
    return GuestRef<T>(
        ReadGuestPod<std::uint32_t>(pointerAddress));
}

template <typename T>
class GuestPtr {
  public:
    using Element = T;

    constexpr GuestPtr() noexcept = default;
    constexpr explicit GuestPtr(std::uint32_t address) noexcept
        : mAddress(address) {}

    template <typename U>
    constexpr explicit GuestPtr(GuestPtr<U> other) noexcept
        : mAddress(other.Address()) {}

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
        return GuestPtr(
            mAddress + static_cast<std::uint32_t>(
                static_cast<std::int64_t>(offset) *
                static_cast<std::int64_t>(sizeof(T))));
    }

    template <typename Offset>
    constexpr GuestPtr operator-(Offset offset) const noexcept
        requires(!std::is_void_v<T> && std::is_integral_v<Offset>)
    {
        return *this + (-static_cast<std::int64_t>(offset));
    }

    constexpr bool operator==(const GuestPtr&) const noexcept = default;

  private:
    std::uint32_t mAddress = 0U;
};

template <typename T>
struct IsGuestPtr : std::false_type {};

template <typename T>
struct IsGuestPtr<GuestPtr<T>> : std::true_type {};

template <typename T>
inline constexpr bool IsGuestPtrV =
    IsGuestPtr<std::remove_cv_t<T>>::value;

template <typename T>
class GuestLocal {
  public:
    using Value = T;

    explicit GuestLocal(std::uint32_t address) noexcept
        : mAddress(address) {}

    operator Value() const {
        return ReadGuestPod<Value>(mAddress);
    }

    template <typename U>
    explicit operator U() const
        requires(IsGuestPtrV<Value> && std::is_integral_v<U>)
    {
        return static_cast<U>(
            static_cast<Value>(*this).Address());
    }

    template <typename U>
    GuestLocal& operator=(U&& value) {
        const Value encoded = static_cast<Value>(
            std::forward<U>(value));
        WriteGuestPod(mAddress, encoded);
        return *this;
    }

    GuestLocal& operator=(const GuestLocal& other) {
        return *this = static_cast<Value>(other);
    }

    template <typename U>
    GuestLocal& operator|=(U value) {
        return *this = static_cast<Value>(
            static_cast<Value>(*this) | static_cast<Value>(value));
    }

    template <typename U>
    GuestLocal& operator&=(U value) {
        return *this = static_cast<Value>(
            static_cast<Value>(*this) & static_cast<Value>(value));
    }

    template <typename U>
    GuestLocal& operator+=(U value) {
        return *this = static_cast<Value>(
            static_cast<Value>(*this) + static_cast<Value>(value));
    }

    template <typename U>
    GuestLocal& operator-=(U value) {
        return *this = static_cast<Value>(
            static_cast<Value>(*this) - static_cast<Value>(value));
    }

    Value operator++(int) {
        const Value previous = static_cast<Value>(*this);
        *this = static_cast<Value>(previous + 1);
        return previous;
    }

    Value operator--(int) {
        const Value previous = static_cast<Value>(*this);
        *this = static_cast<Value>(previous - 1);
        return previous;
    }

    GuestPtr<Value> operator&() const noexcept {
        return GuestPtr<Value>(mAddress);
    }

    template <typename U>
    bool operator==(const U& other) const {
        return static_cast<Value>(*this) ==
            static_cast<Value>(other);
    }

    std::uint32_t Address() const noexcept {
        return mAddress;
    }

  private:
    std::uint32_t mAddress = 0U;
};

static_assert(sizeof(GuestPtr<void>) == sizeof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<GuestPtr<void>>);

struct GuestCallResult {
    std::uint32_t CoreResult = 0U;
    std::array<std::uint32_t, 4> VfpWords{};
};

GuestCallResult InvokeGuestWords(
    std::uint32_t entryAddress,
    std::span<const std::uint32_t> coreArguments,
    std::span<const std::uint32_t> vfpArguments,
    std::span<const std::uint32_t> stackArguments);
void InvokeDynamicPlayerAction(
    std::uint32_t entryAddress, std::uint32_t player,
    std::uint32_t play);

float TargetSignedToFloat(std::int32_t value, int mode = 0);
float TargetUnsignedToFloat(std::uint32_t value, int mode = 0);
float TargetSqrt(float value);

inline float TargetAbs(float value) {
    return std::bit_cast<float>(
        std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFU);
}

inline std::uint32_t FloatToBits(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

inline float BitsToFloat(std::uint32_t value) {
    return std::bit_cast<float>(value);
}

template <typename T>
struct IsGuestRef : std::false_type {};

template <typename T>
struct IsGuestRef<GuestRef<T>> : std::true_type {};

template <typename T>
struct IsGuestLocal : std::false_type {};

template <typename T>
struct IsGuestLocal<GuestLocal<T>> : std::true_type {};

class GuestCallBuilder {
  public:
    template <typename T>
    void Push(T&& value) {
        using Argument = std::remove_cvref_t<T>;
        if constexpr (IsGuestRef<Argument>::value) {
            using Value = typename Argument::Value;
            Push(static_cast<Value>(value));
        } else if constexpr (IsGuestLocal<Argument>::value) {
            using Value = typename Argument::Value;
            Push(static_cast<Value>(value));
        } else if constexpr (IsGuestPtrV<Argument>) {
            PushCore(value.Address());
        } else if constexpr (std::is_same_v<Argument, float>) {
            if (VfpCount >= Vfp.size()) {
                throw std::runtime_error(
                    "Player_UpdateCommon VFP argument overflow");
            }
            Vfp[VfpCount++] = std::bit_cast<std::uint32_t>(value);
        } else if constexpr (
            std::is_integral_v<Argument> ||
            std::is_enum_v<Argument>) {
            PushCore(static_cast<std::uint32_t>(value));
        } else {
            static_assert(
                std::is_same_v<Argument, void>,
                "unsupported Player_UpdateCommon ABI argument");
        }
    }

    std::span<const std::uint32_t> CoreSpan() const {
        return {Core.data(), CoreCount};
    }

    std::span<const std::uint32_t> VfpSpan() const {
        return {Vfp.data(), VfpCount};
    }

    std::span<const std::uint32_t> StackSpan() const {
        return {Stack.data(), StackCount};
    }

  private:
    void PushCore(std::uint32_t value) {
        if (CoreCount < Core.size()) {
            Core[CoreCount++] = value;
            return;
        }
        if (StackCount >= Stack.size()) {
            throw std::runtime_error(
                "Player_UpdateCommon stack argument overflow");
        }
        Stack[StackCount++] = value;
    }

    std::array<std::uint32_t, 4> Core{};
    std::array<std::uint32_t, 16> Vfp{};
    std::array<std::uint32_t, 16> Stack{};
    std::size_t CoreCount = 0U;
    std::size_t VfpCount = 0U;
    std::size_t StackCount = 0U;
};

template <typename Return, typename... Args>
Return InvokeGuest(std::uint32_t entryAddress, Args&&... args) {
    GuestCallBuilder arguments;
    (arguments.Push(std::forward<Args>(args)), ...);
    const GuestCallResult result = InvokeGuestWords(
        entryAddress, arguments.CoreSpan(), arguments.VfpSpan(),
        arguments.StackSpan());
    if constexpr (std::is_void_v<Return>) {
        return;
    } else if constexpr (std::is_same_v<Return, float>) {
        return std::bit_cast<float>(result.VfpWords[0]);
    } else if constexpr (std::is_same_v<Return, bool>) {
        return result.CoreResult != 0U;
    } else if constexpr (IsGuestPtrV<Return>) {
        return Return(result.CoreResult);
    } else {
        return static_cast<Return>(result.CoreResult);
    }
}

#define OOT3D_GUEST_FUNCTION(return_type, name, address) \
    template <typename... Args>                             \
    return_type name(Args&&... args) {                     \
        return InvokeGuest<return_type>(                    \
            address, std::forward<Args>(args)...);          \
    }

OOT3D_GUEST_FUNCTION(undefined4, FUN_00132ad0, 0x00132AD0U)
OOT3D_GUEST_FUNCTION(void, FUN_001a35cc, 0x001A35CCU)
OOT3D_GUEST_FUNCTION(undefined4, FUN_001c4130, 0x001C4130U)
OOT3D_GUEST_FUNCTION(void, FUN_001cf9ac, 0x001CF9ACU)
OOT3D_GUEST_FUNCTION(undefined4, Collider_ResetCylinderAC, 0x001D1794U)
OOT3D_GUEST_FUNCTION(int, Collider_ResetQuadAC, 0x001D1848U)
OOT3D_GUEST_FUNCTION(undefined4, FUN_001ebe68, 0x001EBE68U)
OOT3D_GUEST_FUNCTION(undefined4, Collider_ResetJntSphAT, 0x0020C130U)
OOT3D_GUEST_FUNCTION(int, FUN_0025342c, 0x0025342CU)
OOT3D_GUEST_FUNCTION(void, FUN_0026f7ec, 0x0026F7ECU)
OOT3D_GUEST_FUNCTION(float, oot3d_sin_idx8, 0x002CFCA0U)
OOT3D_GUEST_FUNCTION(void, FUN_0032e780, 0x0032E780U)
OOT3D_GUEST_FUNCTION(void, FUN_0032eadc, 0x0032EADCU)
OOT3D_GUEST_FUNCTION(void, FUN_0032eb30, 0x0032EB30U)
OOT3D_GUEST_FUNCTION(void, FUN_0032eb60, 0x0032EB60U)
OOT3D_GUEST_FUNCTION(void, FUN_0032ebe8, 0x0032EBE8U)
OOT3D_GUEST_FUNCTION(void, FUN_0032ec94, 0x0032EC94U)
OOT3D_GUEST_FUNCTION(void, oot3d_player_process_scene_collision, 0x0032EEB4U)
OOT3D_GUEST_FUNCTION(void, FUN_0032fa4c, 0x0032FA4CU)
OOT3D_GUEST_FUNCTION(int, FUN_0032fa9c, 0x0032FA9CU)
OOT3D_GUEST_FUNCTION(void, FUN_0032fac8, 0x0032FAC8U)
OOT3D_GUEST_FUNCTION(uint, Camera_ChangeMode, 0x00332284U)
OOT3D_GUEST_FUNCTION(undefined4, Collider_ResetQuadAT, 0x003328ECU)
OOT3D_GUEST_FUNCTION(void, FUN_00334354, 0x00334354U)
OOT3D_GUEST_FUNCTION(void, FUN_003343ec, 0x003343ECU)
OOT3D_GUEST_FUNCTION(undefined4, FUN_003365b0, 0x003365B0U)
OOT3D_GUEST_FUNCTION(void, ShrinkWindow_SetVal, 0x00338CD8U)
OOT3D_GUEST_FUNCTION(float, oot3d_cos_idx8, 0x00338F60U)
OOT3D_GUEST_FUNCTION(int, FUN_0033bd6c, 0x0033BD6CU)
OOT3D_GUEST_FUNCTION(void, FUN_00345394, 0x00345394U)
OOT3D_GUEST_FUNCTION(void, FUN_0034a928, 0x0034A928U)
OOT3D_GUEST_FUNCTION(void, FUN_0034d688, 0x0034D688U)
OOT3D_GUEST_FUNCTION(uint, FUN_0034dd2c, 0x0034DD2CU)
OOT3D_GUEST_FUNCTION(undefined4, Camera_SetParam, 0x003521F0U)
OOT3D_GUEST_FUNCTION(void, FUN_00355264, 0x00355264U)
OOT3D_GUEST_FUNCTION(void, FUN_00355830, 0x00355830U)
OOT3D_GUEST_FUNCTION(void, FUN_00355f54, 0x00355F54U)
OOT3D_GUEST_FUNCTION(float, BgCheck_EntityRaycastFloor3, 0x00358410U)
OOT3D_GUEST_FUNCTION(void, Player_SetBootData, 0x003589DCU)
OOT3D_GUEST_FUNCTION(undefined4, FUN_00358b3c, 0x00358B3CU)
OOT3D_GUEST_FUNCTION(undefined4, DynaPoly_GetActor, 0x00359690U)
OOT3D_GUEST_FUNCTION(void, FUN_0035c528, 0x0035C528U)
OOT3D_GUEST_FUNCTION(uint, Inventory_DeleteEquipment, 0x0035D190U)
OOT3D_GUEST_FUNCTION(undefined4, Player_InflictDamage, 0x0035DAACU)
OOT3D_GUEST_FUNCTION(undefined4, Player_InBlockingCsMode, 0x0035DB20U)
OOT3D_GUEST_FUNCTION(void, FUN_0035fb14, 0x0035FB14U)
OOT3D_GUEST_FUNCTION(void, Oot3d_ChangeAnimationByIndex, 0x00360190U)
OOT3D_GUEST_FUNCTION(undefined4, Oot3d_GetAnimationLastFrame, 0x003603C0U)
OOT3D_GUEST_FUNCTION(void, FUN_003603f8, 0x003603F8U)
OOT3D_GUEST_FUNCTION(void, Oot3d_PlayAnimationOnce, 0x003604F0U)
OOT3D_GUEST_FUNCTION(undefined4, FUN_0036055c, 0x0036055CU)
OOT3D_GUEST_FUNCTION(void, EffectSsGRipple_Spawn, 0x00362068U)
OOT3D_GUEST_FUNCTION(void, FUN_00365d20, 0x00365D20U)
OOT3D_GUEST_FUNCTION(bool, oot3d_static_init_guard_acquire, 0x003679B4U)
OOT3D_GUEST_FUNCTION(void, FUN_00367c7c, 0x00367C7CU)
OOT3D_GUEST_FUNCTION(void, EffectSsBubble_Spawn, 0x00368A98U)
OOT3D_GUEST_FUNCTION(int, FUN_00368fec, 0x00368FECU)
OOT3D_GUEST_FUNCTION(undefined4, Player_InCsMode, 0x0036A7A0U)
OOT3D_GUEST_FUNCTION(void, FUN_0036aeb4, 0x0036AEB4U)
OOT3D_GUEST_FUNCTION(void, FUN_0036b02c, 0x0036B02CU)
OOT3D_GUEST_FUNCTION(void, FUN_0036b0fc, 0x0036B0FCU)
OOT3D_GUEST_FUNCTION(void, FUN_0036b96c, 0x0036B96CU)
OOT3D_GUEST_FUNCTION(undefined4, Gameplay_GetCamera, 0x0036C5BCU)
OOT3D_GUEST_FUNCTION(void, oot3d_copy_u32x3, 0x0036DF4CU)
OOT3D_GUEST_FUNCTION(undefined4, FUN_0036e980, 0x0036E980U)
OOT3D_GUEST_FUNCTION(void, FUN_0036f59c, 0x0036F59CU)
OOT3D_GUEST_FUNCTION(int, Math_ScaledStepToS, 0x00370378U)
OOT3D_GUEST_FUNCTION(int, Math_StepToF, 0x003705A0U)
OOT3D_GUEST_FUNCTION(int, OnePointCutscene_Init, 0x00371808U)
OOT3D_GUEST_FUNCTION(float, Rand_ZeroFloat, 0x00371E50U)
OOT3D_GUEST_FUNCTION(int, Actor_Find, 0x00372D64U)
OOT3D_GUEST_FUNCTION(float, Rand_CenteredFloat, 0x003738A8U)
OOT3D_GUEST_FUNCTION(uint, Actor_Spawn, 0x003738D0U)
OOT3D_GUEST_FUNCTION(void, Audio_PlaySoundGeneral, 0x0037547CU)
OOT3D_GUEST_FUNCTION(undefined1, oot3d_get_flag_22a0, 0x0037571CU)
OOT3D_GUEST_FUNCTION(short, Math_Atan2S, 0x003758B0U)
OOT3D_GUEST_FUNCTION(float, Rand_ZeroOne, 0x003759D0U)
OOT3D_GUEST_FUNCTION(void, Audio_PlayActorSound2, 0x00375BCCU)
OOT3D_GUEST_FUNCTION(void, Actor_ChangeType, 0x00375D3CU)
OOT3D_GUEST_FUNCTION(int, CollisionCheck_SetAC, 0x00376168U)
OOT3D_GUEST_FUNCTION(int, CollisionCheck_SetAT, 0x003761F0U)
OOT3D_GUEST_FUNCTION(int, CollisionCheck_SetOC, 0x003762A4U)
OOT3D_GUEST_FUNCTION(void, oot3d_copy_u32x3_field_28_to_field_4c, 0x0037632CU)
OOT3D_GUEST_FUNCTION(void, Actor_MoveForward, 0x00376864U)
OOT3D_GUEST_FUNCTION(void, FUN_003ab984, 0x003AB984U)
OOT3D_GUEST_FUNCTION(void, FUN_003c45f4, 0x003C45F4U)
OOT3D_GUEST_FUNCTION(void, AnimationContext_SetMoveActor, 0x003FD1B8U)
OOT3D_GUEST_FUNCTION(void, FUN_0040a0e8, 0x0040A0E8U)

#undef OOT3D_GUEST_FUNCTION

void Player_UpdateCommon(
    std::uint32_t player, std::uint32_t play,
    std::uint32_t input);

} // namespace Oot3dSourcePlayerUpdateCommon
