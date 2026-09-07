#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace Oot3dSourceCameraUpdate {

using s16 = std::int16_t;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using s32 = std::int32_t;
using u32 = std::uint32_t;
using uintptr_t = std::uint32_t;

struct Oot3dActorVec3f {
    float x;
    float y;
    float z;
};

struct Oot3dActorRot {
    s16 x;
    s16 y;
    s16 z;
};

struct Oot3dActorPosRot {
    Oot3dActorVec3f pos;
    Oot3dActorRot rot;
    std::array<u8, 2> pad_12;
};

struct Oot3dVec3s {
    s16 x;
    s16 y;
    s16 z;
};

struct Oot3dVecSphGeo {
    float r;
    s16 pitch;
    s16 yaw;
};

struct Oot3dActor;
struct Oot3dPlayState;

static_assert(sizeof(Oot3dActorVec3f) == 12U);
static_assert(sizeof(Oot3dActorRot) == 6U);
static_assert(sizeof(Oot3dActorPosRot) == 20U);
static_assert(sizeof(Oot3dVec3s) == 6U);
static_assert(sizeof(Oot3dVecSphGeo) == 8U);

struct CameraUpdateLiterals {
    std::array<std::uint32_t, 10> Words{};
};

inline constexpr std::array<std::uint32_t, 10> kLiteralCellAddresses{
    0x002D88A8U,
    0x002D88B4U,
    0x002D88BCU,
    0x002D88C8U,
    0x002D8C20U,
    0x002D8C28U,
    0x002D8C2CU,
    0x002D8C30U,
    0x002D8C38U,
    0x002D8C40U,
};

const CameraUpdateLiterals& ActiveLiterals();
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

    explicit GuestRef(std::uint32_t address) noexcept : mAddress(address) {
    }

    GuestRef(const GuestRef&) = default;

    operator Value() const {
        static_assert(std::is_trivially_copyable_v<Value>);
        static_assert(
            sizeof(Value) == 1U || sizeof(Value) == 2U ||
            sizeof(Value) == 4U || sizeof(Value) == 8U);
        return ReadGuestPod<Value>(mAddress);
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
        WriteGuestPod(mAddress, encoded);
        return *this;
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
        requires(std::is_convertible_v<U*, T*> ||
                 std::is_void_v<T> || std::is_void_v<U>)
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

    constexpr bool operator==(const GuestPtr&) const noexcept = default;

  private:
    std::uint32_t mAddress = 0U;
};

static_assert(sizeof(GuestPtr<void>) == sizeof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<GuestPtr<void>>);

template <typename T>
class PointerArgument {
  public:
    PointerArgument(T* hostPointer) noexcept
        : mHostPointer(hostPointer) {
    }

    PointerArgument(GuestPtr<T> guestPointer) noexcept
        : mGuestAddress(guestPointer.Address()) {
    }

    bool IsGuest() const noexcept {
        return mGuestAddress != 0U;
    }

    std::uint32_t GuestAddress() const noexcept {
        return mGuestAddress;
    }

    void* HostData() const noexcept {
        return const_cast<std::remove_const_t<T>*>(mHostPointer);
    }

  private:
    T* mHostPointer = nullptr;
    std::uint32_t mGuestAddress = 0U;
};

struct GuestScratchArgument {
    std::size_t ArgumentIndex = 0U;
    void* HostData = nullptr;
    std::size_t Size = 0U;
    bool CopyIn = false;
    bool CopyOut = false;
};

struct GuestCallResult {
    std::uint32_t CoreResult = 0U;
    std::array<std::uint32_t, 4> VfpWords{};
};

GuestCallResult InvokeGuestWords(
    std::uint32_t entryAddress,
    std::span<const std::uint32_t> arguments,
    std::span<const std::uint32_t> vfpArguments = {},
    std::span<const GuestScratchArgument> scratchArguments = {});
void InvokeDynamicCameraFunction(
    std::uint32_t entryAddress, std::uint32_t cameraAddress);
float TargetSqrt(float value);

template <typename T>
std::uint32_t CoreWord(T value) {
    if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>) {
        return value ? 1U : 0U;
    } else {
        return static_cast<std::uint32_t>(value);
    }
}

template <typename T, std::size_t ScratchCapacity>
std::uint32_t EncodePointerArgument(
    PointerArgument<T> pointer, std::size_t argumentIndex,
    std::size_t size, bool copyIn, bool copyOut,
    std::array<GuestScratchArgument, ScratchCapacity>& scratch,
    std::size_t& scratchCount) {
    if (pointer.IsGuest()) {
        return pointer.GuestAddress();
    }
    if (scratchCount >= scratch.size()) {
        throw std::runtime_error("Camera_Update scratch capacity exceeded");
    }
    scratch[scratchCount++] = {
        argumentIndex, pointer.HostData(), size, copyIn, copyOut};
    return 0U;
}

inline float FloatResult(const GuestCallResult& result) {
    return std::bit_cast<float>(result.VfpWords[0]);
}

inline Oot3dActorVec3f Vec3fResult(const GuestCallResult& result) {
    return {
        std::bit_cast<float>(result.VfpWords[0]),
        std::bit_cast<float>(result.VfpWords[1]),
        std::bit_cast<float>(result.VfpWords[2]),
    };
}

inline void Camera_CheckWater(uintptr_t camera) {
    const std::array<u32, 1> arguments{camera};
    InvokeGuestWords(0x002D06A0U, arguments);
}

inline void Camera_ChangeDataIdx(uintptr_t camera) {
    const std::array<u32, 1> arguments{camera};
    InvokeGuestWords(0x003387A8U, arguments);
}

inline void Camera_UpdateInterface(s16 interfaceField) {
    const std::array<u32, 1> arguments{CoreWord(interfaceField)};
    InvokeGuestWords(0x00330D84U, arguments);
}

inline void FUN_002d0518() {
    InvokeGuestWords(0x002D0518U, {});
}

inline u16 FUN_002d064c(
    uintptr_t colCtx, s32 dataIndex, s32 bgId) {
    const std::array<u32, 3> arguments{
        colCtx, CoreWord(dataIndex), CoreWord(bgId)};
    return static_cast<u16>(
        InvokeGuestWords(0x002D064CU, arguments).CoreResult);
}

inline void FUN_002d77dc(
    uintptr_t view,
    const Oot3dActorVec3f* eye,
    const Oot3dActorVec3f* at,
    const Oot3dActorVec3f* up) {
    std::array<u32, 4> arguments{view, 0U, 0U, 0U};
    std::array<GuestScratchArgument, 3> scratch{};
    std::size_t scratchCount = 0U;
    arguments[1] = EncodePointerArgument(
        PointerArgument<const Oot3dActorVec3f>(eye), 1U,
        sizeof(*eye), true, false, scratch, scratchCount);
    arguments[2] = EncodePointerArgument(
        PointerArgument<const Oot3dActorVec3f>(at), 2U,
        sizeof(*at), true, false, scratch, scratchCount);
    arguments[3] = EncodePointerArgument(
        PointerArgument<const Oot3dActorVec3f>(up), 3U,
        sizeof(*up), true, false, scratch, scratchCount);
    InvokeGuestWords(
        0x002D77DCU, arguments, {},
        std::span<const GuestScratchArgument>(
            scratch.data(), scratchCount));
}

inline void FUN_002ded60() {
    InvokeGuestWords(0x002DED60U, {});
}

inline std::uint32_t FUN_00306f04() {
    return InvokeGuestWords(0x00306F04U, {}).CoreResult;
}

inline void FUN_00343840(
    uintptr_t view, float scaleX, float scaleY, float scaleZ) {
    const std::array<u32, 1> arguments{view};
    const std::array<u32, 3> vfp{
        std::bit_cast<u32>(scaleX),
        std::bit_cast<u32>(scaleY),
        std::bit_cast<u32>(scaleZ),
    };
    InvokeGuestWords(0x00343840U, arguments, vfp);
}

inline void FUN_00343858(
    uintptr_t view, float orientationX,
    float orientationY, float orientationZ) {
    const std::array<u32, 1> arguments{view};
    const std::array<u32, 3> vfp{
        std::bit_cast<u32>(orientationX),
        std::bit_cast<u32>(orientationY),
        std::bit_cast<u32>(orientationZ),
    };
    InvokeGuestWords(0x00343858U, arguments, vfp);
}

inline u32 FUN_00374be8(uintptr_t play, s32 query) {
    const std::array<u32, 2> arguments{play, CoreWord(query)};
    return InvokeGuestWords(0x00374BE8U, arguments).CoreResult;
}

inline void FUN_004710f8(uintptr_t view, float scale) {
    const std::array<u32, 1> arguments{view};
    const std::array<u32, 1> vfp{std::bit_cast<u32>(scale)};
    InvokeGuestWords(0x004710F8U, arguments, vfp);
}

inline void FUN_004779ec(uintptr_t view) {
    const std::array<u32, 1> arguments{view};
    InvokeGuestWords(0x004779ECU, arguments);
}

template <typename T>
s32 FUN_004787e8(uintptr_t camera, T* shake) {
    std::array<u32, 2> arguments{camera, 0U};
    std::array<GuestScratchArgument, 1> scratch{};
    std::size_t scratchCount = 0U;
    arguments[1] = EncodePointerArgument(
        PointerArgument<T>(shake), 1U, sizeof(T), false, true,
        scratch, scratchCount);
    return static_cast<s32>(
        InvokeGuestWords(0x004787E8U, arguments, {}, scratch).CoreResult);
}

inline float FUN_00479718(uintptr_t player) {
    const std::array<u32, 1> arguments{player};
    return FloatResult(InvokeGuestWords(0x00479718U, arguments));
}

inline void FUN_0047ac5c(
    uintptr_t play, uintptr_t collisionState,
    GuestPtr<const Oot3dActorVec3f> eye, float* distance) {
    std::array<u32, 4> arguments{
        play, collisionState, eye.Address(), 0U};
    std::array<GuestScratchArgument, 1> scratch{};
    std::size_t scratchCount = 0U;
    arguments[3] = EncodePointerArgument(
        PointerArgument<float>(distance), 3U, sizeof(float), true, true,
        scratch, scratchCount);
    InvokeGuestWords(0x0047AC5CU, arguments, {}, scratch);
}

inline s32 FUN_0047bff8(
    uintptr_t colCtx, uintptr_t floorPoly, s32 bgId) {
    const std::array<u32, 3> arguments{
        colCtx, floorPoly, CoreWord(bgId)};
    return static_cast<s32>(
        InvokeGuestWords(0x0047BFF8U, arguments).CoreResult);
}

inline float FUN_0047cc14(
    uintptr_t colCtx, GuestPtr<const Oot3dActorVec3f> eye,
    float fallback) {
    const std::array<u32, 2> arguments{colCtx, eye.Address()};
    const std::array<u32, 1> vfp{std::bit_cast<u32>(fallback)};
    return FloatResult(InvokeGuestWords(0x0047CC14U, arguments, vfp));
}

inline float OLib_ClampMaxDist(float value, float maxDistance) {
    const std::array<u32, 2> vfp{
        std::bit_cast<u32>(value),
        std::bit_cast<u32>(maxDistance),
    };
    return FloatResult(InvokeGuestWords(0x00355804U, {}, vfp));
}

inline float OLib_Vec3fDistXZ(
    PointerArgument<const Oot3dActorVec3f> a,
    PointerArgument<const Oot3dActorVec3f> b) {
    std::array<u32, 2> arguments{};
    std::array<GuestScratchArgument, 2> scratch{};
    std::size_t scratchCount = 0U;
    arguments[0] = EncodePointerArgument(
        a, 0U, sizeof(Oot3dActorVec3f), true, false,
        scratch, scratchCount);
    arguments[1] = EncodePointerArgument(
        b, 1U, sizeof(Oot3dActorVec3f), true, false,
        scratch, scratchCount);
    return FloatResult(InvokeGuestWords(
        0x00367E60U, arguments, {},
        std::span<const GuestScratchArgument>(
            scratch.data(), scratchCount)));
}

inline void OLib_Vec3fDiffToVecSphGeo(
    Oot3dVecSphGeo* destination,
    PointerArgument<const Oot3dActorVec3f> from,
    PointerArgument<const Oot3dActorVec3f> to) {
    std::array<u32, 3> arguments{};
    std::array<GuestScratchArgument, 3> scratch{};
    std::size_t scratchCount = 0U;
    arguments[0] = EncodePointerArgument(
        PointerArgument<Oot3dVecSphGeo>(destination), 0U,
        sizeof(*destination), false, true, scratch, scratchCount);
    arguments[1] = EncodePointerArgument(
        from, 1U, sizeof(Oot3dActorVec3f), true, false,
        scratch, scratchCount);
    arguments[2] = EncodePointerArgument(
        to, 2U, sizeof(Oot3dActorVec3f), true, false,
        scratch, scratchCount);
    InvokeGuestWords(
        0x00372474U, arguments, {},
        std::span<const GuestScratchArgument>(
            scratch.data(), scratchCount));
}

inline Oot3dActorVec3f Camera_AddVecGeoToVec3f(
    const Oot3dActorVec3f* origin,
    const Oot3dVecSphGeo* offset) {
    std::array<u32, 2> arguments{};
    std::array<GuestScratchArgument, 2> scratch{};
    std::size_t scratchCount = 0U;
    arguments[0] = EncodePointerArgument(
        PointerArgument<const Oot3dActorVec3f>(origin), 0U,
        sizeof(*origin), true, false, scratch, scratchCount);
    arguments[1] = EncodePointerArgument(
        PointerArgument<const Oot3dVecSphGeo>(offset), 1U,
        sizeof(*offset), true, false, scratch, scratchCount);
    return Vec3fResult(InvokeGuestWords(
        0x00372448U, arguments, {},
        std::span<const GuestScratchArgument>(
            scratch.data(), scratchCount)));
}

inline Oot3dActorVec3f Camera_CalcUpFromPitchYawRoll(
    s16 pitch, s16 yaw, s16 roll) {
    const std::array<u32, 3> arguments{
        CoreWord(pitch), CoreWord(yaw), CoreWord(roll)};
    return Vec3fResult(InvokeGuestWords(0x002D052CU, arguments));
}

inline void Camera_CalcAtDefault(
    GuestPtr<void> camera, const Oot3dVecSphGeo* eyeAtAngle,
    float yOffset, s32 arg3) {
    std::array<u32, 3> arguments{
        camera.Address(), 0U, CoreWord(arg3)};
    std::array<GuestScratchArgument, 1> scratch{};
    std::size_t scratchCount = 0U;
    arguments[1] = EncodePointerArgument(
        PointerArgument<const Oot3dVecSphGeo>(eyeAtAngle), 1U,
        sizeof(*eyeAtAngle), true, false, scratch, scratchCount);
    const std::array<u32, 1> vfp{std::bit_cast<u32>(yOffset)};
    InvokeGuestWords(0x00338AC8U, arguments, vfp, scratch);
}

inline float Player_GetHeight(GuestPtr<const void> player) {
    const std::array<u32, 1> arguments{player.Address()};
    return FloatResult(InvokeGuestWords(0x00367EF0U, arguments));
}

inline void Actor_GetWorldPosShapeRot(
    Oot3dActorPosRot* destination,
    GuestPtr<const Oot3dActor> actor) {
    std::array<u32, 2> arguments{0U, actor.Address()};
    std::array<GuestScratchArgument, 1> scratch{};
    std::size_t scratchCount = 0U;
    arguments[0] = EncodePointerArgument(
        PointerArgument<Oot3dActorPosRot>(destination), 0U,
        sizeof(*destination), false, true, scratch, scratchCount);
    InvokeGuestWords(0x00331764U, arguments, {}, scratch);
}

inline float BgCheck_EntityRaycastFloor5(
    GuestPtr<Oot3dPlayState> play,
    GuestPtr<void> colCtx,
    GuestPtr<u32> outPoly,
    s32* outBgId,
    GuestPtr<Oot3dActor> actor,
    Oot3dActorVec3f* position) {
    std::array<u32, 6> arguments{
        play.Address(), colCtx.Address(), outPoly.Address(),
        0U, actor.Address(), 0U};
    std::array<GuestScratchArgument, 2> scratch{};
    std::size_t scratchCount = 0U;
    arguments[3] = EncodePointerArgument(
        PointerArgument<s32>(outBgId), 3U, sizeof(*outBgId),
        false, true, scratch, scratchCount);
    arguments[5] = EncodePointerArgument(
        PointerArgument<Oot3dActorVec3f>(position), 5U,
        sizeof(*position), true, false, scratch, scratchCount);
    return FloatResult(InvokeGuestWords(
        0x00316C18U, arguments, {},
        std::span<const GuestScratchArgument>(
            scratch.data(), scratchCount)));
}

inline u32 oot3d_get_flag_22a0(GuestPtr<const void> object) {
    const std::array<u32, 1> arguments{object.Address()};
    return InvokeGuestWords(0x0037571CU, arguments).CoreResult;
}

inline float oot3d_sin_idx8(u32 angle) {
    const std::array<u32, 1> arguments{angle};
    return FloatResult(InvokeGuestWords(0x002CFCA0U, arguments));
}

inline float oot3d_cos_idx8(u32 angle) {
    const std::array<u32, 1> arguments{angle};
    return FloatResult(InvokeGuestWords(0x00338F60U, arguments));
}

inline u32 oot3d_float_to_bits(float value) {
    return std::bit_cast<u32>(value);
}

void Camera_Update(
    std::uint32_t resultAddress, std::uint32_t cameraAddress);

} // namespace Oot3dSourceCameraUpdate
