#!/usr/bin/env python3
"""Lower the pinned Player_UpdateCommon body to target-width C++."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re


EXPECTED_SOURCE_SHA256 = (
    "cd3dfa5a543ea5a6dc274daa1e515516a2d0e46c8e95dede4e6fdfea7452d86c"
)
START_TOKEN = "void Player_UpdateCommon(int param_1,int param_2,undefined4 param_3)\n\n{"
END_TOKEN = "/* Applied normalizations:"

REQUIRED_SOURCE_FRAGMENTS = (
    "*(undefined4 *)(uintptr_t)(u32)(DAT_00251304 + 0xac) = param_3;",
    "BgCheck_EntityRaycastFloor3(\n            local_88,&local_90,&local_94,floor_probe);",
    "(**(code **)(uintptr_t)(u32)(param_1 + 0x1708))(param_1,param_2);",
    "Collider_ResetJntSphAT(param_2,param_1 + 0x15e8);",
)

TYPE_MAP = {
    "char": "sbyte",
    "sbyte": "sbyte",
    "byte": "byte",
    "short": "short",
    "ushort": "ushort",
    "int": "int",
    "uint": "uint",
    "undefined1": "undefined1",
    "undefined2": "undefined2",
    "undefined4": "undefined4",
    "float": "float",
    "uintptr_t": "uintptr_t",
    "code": "undefined4",
}

# Target disassembly shows that this literal is a short pointer: every source
# offset is doubled by the ARM addressing mode. The producer's uintptr_t
# normalization dropped that pointer arithmetic.
LITERAL_TYPE_OVERRIDES = {
    "DAT_00251304": ("uintptr_t", "short *"),
}

DEPENDENCIES = (
    ("undefined4", "FUN_00132ad0", 0x00132AD0),
    ("void", "FUN_001a35cc", 0x001A35CC),
    ("undefined4", "FUN_001c4130", 0x001C4130),
    ("void", "FUN_001cf9ac", 0x001CF9AC),
    ("undefined4", "Collider_ResetCylinderAC", 0x001D1794),
    ("int", "Collider_ResetQuadAC", 0x001D1848),
    ("undefined4", "FUN_001ebe68", 0x001EBE68),
    ("undefined4", "Collider_ResetJntSphAT", 0x0020C130),
    ("int", "FUN_0025342c", 0x0025342C),
    ("void", "FUN_0026f7ec", 0x0026F7EC),
    ("float", "oot3d_sin_idx8", 0x002CFCA0),
    ("void", "FUN_0032e780", 0x0032E780),
    ("void", "FUN_0032eadc", 0x0032EADC),
    ("void", "FUN_0032eb30", 0x0032EB30),
    ("void", "FUN_0032eb60", 0x0032EB60),
    ("void", "FUN_0032ebe8", 0x0032EBE8),
    ("void", "FUN_0032ec94", 0x0032EC94),
    ("void", "oot3d_player_process_scene_collision", 0x0032EEB4),
    ("void", "FUN_0032fa4c", 0x0032FA4C),
    ("int", "FUN_0032fa9c", 0x0032FA9C),
    ("void", "FUN_0032fac8", 0x0032FAC8),
    ("uint", "Camera_ChangeMode", 0x00332284),
    ("undefined4", "Collider_ResetQuadAT", 0x003328EC),
    ("void", "FUN_00334354", 0x00334354),
    ("void", "FUN_003343ec", 0x003343EC),
    ("undefined4", "FUN_003365b0", 0x003365B0),
    ("void", "ShrinkWindow_SetVal", 0x00338CD8),
    ("float", "oot3d_cos_idx8", 0x00338F60),
    ("int", "FUN_0033bd6c", 0x0033BD6C),
    ("void", "FUN_00345394", 0x00345394),
    ("void", "FUN_0034a928", 0x0034A928),
    ("void", "FUN_0034d688", 0x0034D688),
    ("uint", "FUN_0034dd2c", 0x0034DD2C),
    ("undefined4", "Camera_SetParam", 0x003521F0),
    ("void", "FUN_00355264", 0x00355264),
    ("void", "FUN_00355830", 0x00355830),
    ("void", "FUN_00355f54", 0x00355F54),
    ("float", "BgCheck_EntityRaycastFloor3", 0x00358410),
    ("void", "Player_SetBootData", 0x003589DC),
    ("undefined4", "FUN_00358b3c", 0x00358B3C),
    ("undefined4", "DynaPoly_GetActor", 0x00359690),
    ("void", "FUN_0035c528", 0x0035C528),
    ("uint", "Inventory_DeleteEquipment", 0x0035D190),
    ("undefined4", "Player_InflictDamage", 0x0035DAAC),
    ("undefined4", "Player_InBlockingCsMode", 0x0035DB20),
    ("void", "FUN_0035fb14", 0x0035FB14),
    ("void", "Oot3d_ChangeAnimationByIndex", 0x00360190),
    ("undefined4", "Oot3d_GetAnimationLastFrame", 0x003603C0),
    ("void", "FUN_003603f8", 0x003603F8),
    ("void", "Oot3d_PlayAnimationOnce", 0x003604F0),
    ("undefined4", "FUN_0036055c", 0x0036055C),
    ("void", "EffectSsGRipple_Spawn", 0x00362068),
    ("void", "FUN_00365d20", 0x00365D20),
    ("bool", "oot3d_static_init_guard_acquire", 0x003679B4),
    ("void", "FUN_00367c7c", 0x00367C7C),
    ("void", "EffectSsBubble_Spawn", 0x00368A98),
    ("int", "FUN_00368fec", 0x00368FEC),
    ("undefined4", "Player_InCsMode", 0x0036A7A0),
    ("void", "FUN_0036aeb4", 0x0036AEB4),
    ("void", "FUN_0036b02c", 0x0036B02C),
    ("void", "FUN_0036b0fc", 0x0036B0FC),
    ("void", "FUN_0036b96c", 0x0036B96C),
    ("undefined4", "Gameplay_GetCamera", 0x0036C5BC),
    ("void", "oot3d_copy_u32x3", 0x0036DF4C),
    ("undefined4", "FUN_0036e980", 0x0036E980),
    ("void", "FUN_0036f59c", 0x0036F59C),
    ("int", "Math_ScaledStepToS", 0x00370378),
    ("int", "Math_StepToF", 0x003705A0),
    ("int", "OnePointCutscene_Init", 0x00371808),
    ("float", "Rand_ZeroFloat", 0x00371E50),
    ("int", "Actor_Find", 0x00372D64),
    ("float", "Rand_CenteredFloat", 0x003738A8),
    ("uint", "Actor_Spawn", 0x003738D0),
    ("void", "Audio_PlaySoundGeneral", 0x0037547C),
    ("undefined1", "oot3d_get_flag_22a0", 0x0037571C),
    ("short", "Math_Atan2S", 0x003758B0),
    ("float", "Rand_ZeroOne", 0x003759D0),
    ("void", "Audio_PlayActorSound2", 0x00375BCC),
    ("void", "Actor_ChangeType", 0x00375D3C),
    ("int", "CollisionCheck_SetAC", 0x00376168),
    ("int", "CollisionCheck_SetAT", 0x003761F0),
    ("int", "CollisionCheck_SetOC", 0x003762A4),
    ("void", "oot3d_copy_u32x3_field_28_to_field_4c", 0x0037632C),
    ("void", "Actor_MoveForward", 0x00376864),
    ("void", "FUN_003ab984", 0x003AB984),
    ("void", "FUN_003c45f4", 0x003C45F4),
    ("void", "AnimationContext_SetMoveActor", 0x003FD1B8),
    ("void", "FUN_0040a0e8", 0x0040A0E8),
)


def replace_exact(source: str, original: str, replacement: str, count: int = 1) -> str:
    actual = source.count(original)
    if actual != count:
        raise RuntimeError(
            f"expected {count} occurrence(s), found {actual}: {original!r}"
        )
    return source.replace(original, replacement, count)


def matching_paren(source: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "(":
            depth += 1
        elif source[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    raise RuntimeError(f"unbalanced source expression at byte {opening}")


def replace_balanced(
    source: str,
    pattern: re.Pattern[str],
    replacement,
) -> str:
    while True:
        match = pattern.search(source)
        if match is None:
            return source
        opening = match.end() - 1
        closing = matching_paren(source, opening)
        expression = source[opening + 1 : closing]
        source = (
            source[: match.start()]
            + replacement(match, expression)
            + source[closing + 1 :]
        )


def consume_unary_operand(source: str, start: int) -> tuple[str, int]:
    index = start
    while index < len(source) and source[index].isspace():
        index += 1
    prefix = source[start:index]

    while index < len(source) and source[index] == "(":
        closing = matching_paren(source, index)
        content = source[index + 1 : closing].strip()
        if re.fullmatch(
            r"(?:u32|uint|int|short|ushort|byte|sbyte|"
            r"undefined1|undefined2|undefined4|uintptr_t)",
            content,
        ):
            prefix += source[index : closing + 1]
            index = closing + 1
            while index < len(source) and source[index].isspace():
                prefix += source[index]
                index += 1
            continue
        return prefix + source[index + 1 : closing], closing + 1

    token = re.match(
        r"(?:0x[0-9A-Fa-f]+[uUlL]*|[0-9]+[uUlL]*|"
        r"[A-Za-z_][A-Za-z0-9_]*)",
        source[index:],
    )
    if token is None:
        raise RuntimeError(
            f"unsupported unary pointer operand at byte {index}: "
            f"{source[index:index + 40]!r}"
        )
    return prefix + token.group(0), index + token.end()


def replace_u32_pointer_form(
    source: str,
    pattern: re.Pattern[str],
    replacement,
) -> str:
    while True:
        match = pattern.search(source)
        if match is None:
            return source
        operand, end = consume_unary_operand(source, match.end())
        source = (
            source[: match.start()]
            + replacement(match, operand)
            + source[end:]
        )


def lower_pointer_accesses(body: str) -> str:
    double_deref = re.compile(
        r"\*\*\((?P<type>[A-Za-z_][A-Za-z0-9_]*) \*\*\)"
        r"\(uintptr_t\)\(u32\)\("
    )
    body = replace_balanced(
        body,
        double_deref,
        lambda match, expression: (
            f"GuestIndirectRef<{TYPE_MAP[match.group('type')]}>"
            f"({expression})"
        ),
    )

    pointer_deref = re.compile(
        r"\*\((?P<type>[A-Za-z_][A-Za-z0-9_]*) \*\*\)"
        r"\(uintptr_t\)\(u32\)\("
    )
    body = replace_balanced(
        body,
        pointer_deref,
        lambda match, expression: (
            f"GuestRef<GuestPtr<{TYPE_MAP[match.group('type')]}>"
            f">({expression})"
        ),
    )

    scalar_deref = re.compile(
        r"\*\((?P<type>[A-Za-z_][A-Za-z0-9_]*) \*\)"
        r"\(uintptr_t\)\(u32\)"
    )
    body = replace_u32_pointer_form(
        body,
        scalar_deref,
        lambda match, operand: (
            f"GuestRef<{TYPE_MAP[match.group('type')]}>({operand})"
        ),
    )

    pointer_cast_u32 = re.compile(
        r"\((?P<type>[A-Za-z_][A-Za-z0-9_]*) \*\)"
        r"\(uintptr_t\)\(u32\)"
    )
    body = replace_u32_pointer_form(
        body,
        pointer_cast_u32,
        lambda match, operand: (
            f"GuestPtr<{TYPE_MAP[match.group('type')]}>({operand})"
        ),
    )

    pointer_cast = re.compile(
        r"\((?P<type>[A-Za-z_][A-Za-z0-9_]*) \*\)"
        r"\(uintptr_t\)\("
    )
    body = replace_balanced(
        body,
        pointer_cast,
        lambda match, expression: (
            f"GuestPtr<{TYPE_MAP[match.group('type')]}>({expression})"
        ),
    )

    direct_pointer_cast = re.compile(
        r"\((?P<type>[A-Za-z_][A-Za-z0-9_]*) \*\)"
        r"\(uintptr_t\)(?P<value>DAT_[0-9a-f]+)"
    )
    body = direct_pointer_cast.sub(
        lambda match: (
            f"GuestPtr<{TYPE_MAP[match.group('type')]}>"
            f"({match.group('value')})"
        ),
        body,
    )
    return body


def parse_literals(source: str) -> list[tuple[str, str, int]]:
    literals: list[tuple[str, str, int]] = []
    pattern = re.compile(
        r"^extern (?P<type>.+?)\s+DAT_(?P<address>[0-9a-f]+);$",
        re.MULTILINE,
    )
    for match in pattern.finditer(source):
        address_text = match.group("address")
        name = f"DAT_{address_text}"
        source_type = match.group("type").strip()
        type_name = source_type
        if name in LITERAL_TYPE_OVERRIDES:
            expected_type, type_name = LITERAL_TYPE_OVERRIDES[name]
            if source_type != expected_type:
                raise RuntimeError(
                    f"{name} type changed: expected {expected_type}, "
                    f"found {source_type}"
                )
        literals.append(
            (
                name,
                type_name,
                int(address_text, 16),
            )
        )
    if len(literals) != 136:
        raise RuntimeError(f"expected 136 literal cells, found {len(literals)}")
    if len({item[0] for item in literals}) != len(literals):
        raise RuntimeError("duplicate Player_UpdateCommon literal declaration")
    return literals


def literal_expression(type_name: str, index: int) -> str:
    word = f"ActiveLiterals().Words[{index}]"
    if type_name == "float":
        return f"std::bit_cast<float>({word})"
    if type_name == "int":
        return f"static_cast<int>({word})"
    if type_name in ("uint", "undefined4", "uintptr_t"):
        return word
    if type_name.endswith(" *"):
        element = TYPE_MAP[type_name[:-2].strip()]
        return f"GuestPtr<{element}>({word})"
    raise RuntimeError(f"unsupported literal declaration type: {type_name}")


def dependency_wrappers() -> str:
    lines = []
    for return_type, name, address in DEPENDENCIES:
        lines.append(
            f"OOT3D_GUEST_FUNCTION({return_type}, {name}, 0x{address:08X}U)"
        )
    return "\n".join(lines)


def generated_header(literals: list[tuple[str, str, int]]) -> str:
    addresses = "\n".join(
        f"    0x{address:08X}U," for _, _, address in literals
    )
    wrappers = dependency_wrappers()
    return f'''#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Oot3dSourcePlayerUpdateCommon {{

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

struct PlayerUpdateCommonLiterals {{
    std::array<std::uint32_t, {len(literals)}> Words{{}};
}};

inline constexpr std::array<std::uint32_t, {len(literals)}>
    kLiteralCellAddresses{{
{addresses}
    }};

const PlayerUpdateCommonLiterals& ActiveLiterals();
std::uint32_t OwnerLocalAddress(std::uint32_t offset);
void ReadGuestMemory(
    std::uint32_t address, void* destination, std::size_t size);
void WriteGuestMemory(
    std::uint32_t address, const void* source, std::size_t size);

template <typename T>
T ReadGuestPod(std::uint32_t address) {{
    static_assert(std::is_trivially_copyable_v<T>);
    T value{{}};
    ReadGuestMemory(address, &value, sizeof(value));
    return value;
}}

template <typename T>
void WriteGuestPod(std::uint32_t address, const T& value) {{
    static_assert(std::is_trivially_copyable_v<T>);
    WriteGuestMemory(address, &value, sizeof(value));
}}

template <typename T>
class GuestPtr;

template <typename T>
class GuestRef {{
  public:
    using Value = std::remove_cv_t<T>;

    explicit GuestRef(std::uint32_t address) noexcept : mAddress(address) {{}}

    operator Value() const {{
        return ReadGuestPod<Value>(mAddress);
    }}

    template <typename U>
    GuestRef& operator=(U&& value)
        requires(!std::is_const_v<T>)
    {{
        const Value encoded = static_cast<Value>(
            std::forward<U>(value));
        WriteGuestPod(mAddress, encoded);
        return *this;
    }}

    GuestRef& operator=(const GuestRef& other)
        requires(!std::is_const_v<T>)
    {{
        return *this = static_cast<Value>(other);
    }}

    template <typename U>
    GuestRef& operator|=(U value)
        requires(!std::is_const_v<T>)
    {{
        return *this = static_cast<Value>(
            static_cast<Value>(*this) | static_cast<Value>(value));
    }}

    template <typename U>
    GuestRef& operator&=(U value)
        requires(!std::is_const_v<T>)
    {{
        return *this = static_cast<Value>(
            static_cast<Value>(*this) & static_cast<Value>(value));
    }}

    template <typename U>
    GuestRef& operator+=(U value)
        requires(!std::is_const_v<T>)
    {{
        return *this = static_cast<Value>(
            static_cast<Value>(*this) + static_cast<Value>(value));
    }}

    template <typename U>
    GuestRef& operator-=(U value)
        requires(!std::is_const_v<T>)
    {{
        return *this = static_cast<Value>(
            static_cast<Value>(*this) - static_cast<Value>(value));
    }}

    Value operator++(int)
        requires(!std::is_const_v<T>)
    {{
        const Value previous = static_cast<Value>(*this);
        *this = static_cast<Value>(previous + 1);
        return previous;
    }}

    Value operator--(int)
        requires(!std::is_const_v<T>)
    {{
        const Value previous = static_cast<Value>(*this);
        *this = static_cast<Value>(previous - 1);
        return previous;
    }}

    std::uint32_t Address() const noexcept {{
        return mAddress;
    }}

  private:
    std::uint32_t mAddress = 0U;
}};

template <typename T>
GuestRef<T> GuestIndirectRef(std::uint32_t pointerAddress) {{
    return GuestRef<T>(
        ReadGuestPod<std::uint32_t>(pointerAddress));
}}

template <typename T>
class GuestPtr {{
  public:
    using Element = T;

    constexpr GuestPtr() noexcept = default;
    constexpr explicit GuestPtr(std::uint32_t address) noexcept
        : mAddress(address) {{}}

    template <typename U>
    constexpr explicit GuestPtr(GuestPtr<U> other) noexcept
        : mAddress(other.Address()) {{}}

    constexpr std::uint32_t Address() const noexcept {{
        return mAddress;
    }}

    constexpr explicit operator bool() const noexcept {{
        return mAddress != 0U;
    }}

    constexpr operator std::uint32_t() const noexcept {{
        return mAddress;
    }}

    GuestRef<T> operator*() const
        requires(!std::is_void_v<T>)
    {{
        return GuestRef<T>(mAddress);
    }}

    template <typename Index>
    GuestRef<T> operator[](Index index) const
        requires(!std::is_void_v<T> && std::is_integral_v<Index>)
    {{
        return *(*this + index);
    }}

    template <typename Offset>
    constexpr GuestPtr operator+(Offset offset) const noexcept
        requires(!std::is_void_v<T> && std::is_integral_v<Offset>)
    {{
        return GuestPtr(
            mAddress + static_cast<std::uint32_t>(
                static_cast<std::int64_t>(offset) *
                static_cast<std::int64_t>(sizeof(T))));
    }}

    template <typename Offset>
    constexpr GuestPtr operator-(Offset offset) const noexcept
        requires(!std::is_void_v<T> && std::is_integral_v<Offset>)
    {{
        return *this + (-static_cast<std::int64_t>(offset));
    }}

    constexpr bool operator==(const GuestPtr&) const noexcept = default;

  private:
    std::uint32_t mAddress = 0U;
}};

template <typename T>
struct IsGuestPtr : std::false_type {{}};

template <typename T>
struct IsGuestPtr<GuestPtr<T>> : std::true_type {{}};

template <typename T>
inline constexpr bool IsGuestPtrV =
    IsGuestPtr<std::remove_cv_t<T>>::value;

template <typename T>
class GuestLocal {{
  public:
    using Value = T;

    explicit GuestLocal(std::uint32_t address) noexcept
        : mAddress(address) {{}}

    operator Value() const {{
        return ReadGuestPod<Value>(mAddress);
    }}

    template <typename U>
    explicit operator U() const
        requires(IsGuestPtrV<Value> && std::is_integral_v<U>)
    {{
        return static_cast<U>(
            static_cast<Value>(*this).Address());
    }}

    template <typename U>
    GuestLocal& operator=(U&& value) {{
        const Value encoded = static_cast<Value>(
            std::forward<U>(value));
        WriteGuestPod(mAddress, encoded);
        return *this;
    }}

    GuestLocal& operator=(const GuestLocal& other) {{
        return *this = static_cast<Value>(other);
    }}

    template <typename U>
    GuestLocal& operator|=(U value) {{
        return *this = static_cast<Value>(
            static_cast<Value>(*this) | static_cast<Value>(value));
    }}

    template <typename U>
    GuestLocal& operator&=(U value) {{
        return *this = static_cast<Value>(
            static_cast<Value>(*this) & static_cast<Value>(value));
    }}

    template <typename U>
    GuestLocal& operator+=(U value) {{
        return *this = static_cast<Value>(
            static_cast<Value>(*this) + static_cast<Value>(value));
    }}

    template <typename U>
    GuestLocal& operator-=(U value) {{
        return *this = static_cast<Value>(
            static_cast<Value>(*this) - static_cast<Value>(value));
    }}

    Value operator++(int) {{
        const Value previous = static_cast<Value>(*this);
        *this = static_cast<Value>(previous + 1);
        return previous;
    }}

    Value operator--(int) {{
        const Value previous = static_cast<Value>(*this);
        *this = static_cast<Value>(previous - 1);
        return previous;
    }}

    GuestPtr<Value> operator&() const noexcept {{
        return GuestPtr<Value>(mAddress);
    }}

    template <typename U>
    bool operator==(const U& other) const {{
        return static_cast<Value>(*this) ==
            static_cast<Value>(other);
    }}

    std::uint32_t Address() const noexcept {{
        return mAddress;
    }}

  private:
    std::uint32_t mAddress = 0U;
}};

static_assert(sizeof(GuestPtr<void>) == sizeof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<GuestPtr<void>>);

struct GuestCallResult {{
    std::uint32_t CoreResult = 0U;
    std::array<std::uint32_t, 4> VfpWords{{}};
}};

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

inline float TargetAbs(float value) {{
    return std::bit_cast<float>(
        std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFU);
}}

inline std::uint32_t FloatToBits(float value) {{
    return std::bit_cast<std::uint32_t>(value);
}}

inline float BitsToFloat(std::uint32_t value) {{
    return std::bit_cast<float>(value);
}}

template <typename T>
struct IsGuestRef : std::false_type {{}};

template <typename T>
struct IsGuestRef<GuestRef<T>> : std::true_type {{}};

template <typename T>
struct IsGuestLocal : std::false_type {{}};

template <typename T>
struct IsGuestLocal<GuestLocal<T>> : std::true_type {{}};

class GuestCallBuilder {{
  public:
    template <typename T>
    void Push(T&& value) {{
        using Argument = std::remove_cvref_t<T>;
        if constexpr (IsGuestRef<Argument>::value) {{
            using Value = typename Argument::Value;
            Push(static_cast<Value>(value));
        }} else if constexpr (IsGuestLocal<Argument>::value) {{
            using Value = typename Argument::Value;
            Push(static_cast<Value>(value));
        }} else if constexpr (IsGuestPtrV<Argument>) {{
            PushCore(value.Address());
        }} else if constexpr (std::is_same_v<Argument, float>) {{
            if (VfpCount >= Vfp.size()) {{
                throw std::runtime_error(
                    "Player_UpdateCommon VFP argument overflow");
            }}
            Vfp[VfpCount++] = std::bit_cast<std::uint32_t>(value);
        }} else if constexpr (
            std::is_integral_v<Argument> ||
            std::is_enum_v<Argument>) {{
            PushCore(static_cast<std::uint32_t>(value));
        }} else {{
            static_assert(
                std::is_same_v<Argument, void>,
                "unsupported Player_UpdateCommon ABI argument");
        }}
    }}

    std::span<const std::uint32_t> CoreSpan() const {{
        return {{Core.data(), CoreCount}};
    }}

    std::span<const std::uint32_t> VfpSpan() const {{
        return {{Vfp.data(), VfpCount}};
    }}

    std::span<const std::uint32_t> StackSpan() const {{
        return {{Stack.data(), StackCount}};
    }}

  private:
    void PushCore(std::uint32_t value) {{
        if (CoreCount < Core.size()) {{
            Core[CoreCount++] = value;
            return;
        }}
        if (StackCount >= Stack.size()) {{
            throw std::runtime_error(
                "Player_UpdateCommon stack argument overflow");
        }}
        Stack[StackCount++] = value;
    }}

    std::array<std::uint32_t, 4> Core{{}};
    std::array<std::uint32_t, 16> Vfp{{}};
    std::array<std::uint32_t, 16> Stack{{}};
    std::size_t CoreCount = 0U;
    std::size_t VfpCount = 0U;
    std::size_t StackCount = 0U;
}};

template <typename Return, typename... Args>
Return InvokeGuest(std::uint32_t entryAddress, Args&&... args) {{
    GuestCallBuilder arguments;
    (arguments.Push(std::forward<Args>(args)), ...);
    const GuestCallResult result = InvokeGuestWords(
        entryAddress, arguments.CoreSpan(), arguments.VfpSpan(),
        arguments.StackSpan());
    if constexpr (std::is_void_v<Return>) {{
        return;
    }} else if constexpr (std::is_same_v<Return, float>) {{
        return std::bit_cast<float>(result.VfpWords[0]);
    }} else if constexpr (std::is_same_v<Return, bool>) {{
        return result.CoreResult != 0U;
    }} else if constexpr (IsGuestPtrV<Return>) {{
        return Return(result.CoreResult);
    }} else {{
        return static_cast<Return>(result.CoreResult);
    }}
}}

#define OOT3D_GUEST_FUNCTION(return_type, name, address) \\
    template <typename... Args>                             \\
    return_type name(Args&&... args) {{                     \\
        return InvokeGuest<return_type>(                    \\
            address, std::forward<Args>(args)...);          \\
    }}

{wrappers}

#undef OOT3D_GUEST_FUNCTION

void Player_UpdateCommon(
    std::uint32_t player, std::uint32_t play,
    std::uint32_t input);

}} // namespace Oot3dSourcePlayerUpdateCommon
'''


def lower(source: str, literals: list[tuple[str, str, int]]) -> str:
    start = source.find(START_TOKEN)
    end = source.find(END_TOKEN, start)
    if start < 0 or end < 0 or end <= start:
        raise RuntimeError("Player_UpdateCommon maintained body boundaries not found")
    body = source[start:end].rstrip()
    for fragment in REQUIRED_SOURCE_FRAGMENTS:
        if body.count(fragment) != 1:
            raise RuntimeError(
                f"required source fragment not unique: {fragment!r}"
            )

    body = replace_exact(
        body,
        START_TOKEN,
        "void Player_UpdateCommon(u32 param_1,u32 param_2,u32 param_3)\n\n{",
    )
    body = replace_exact(body, "  short *psVar2;", "  GuestPtr<short> psVar2;")
    body = replace_exact(body, "  short *psVar3;", "  GuestPtr<short> psVar3;")
    body = replace_exact(
        body, "  undefined4 *puVar4;", "  GuestPtr<undefined4> puVar4;"
    )
    body = replace_exact(body, "  int *piVar5;", "  GuestPtr<int> piVar5;")
    body = replace_exact(body, "  float *pfVar6;", "  GuestPtr<float> pfVar6;")
    body = replace_exact(body, "  byte *pbVar17;", "  GuestPtr<byte> pbVar17;")
    body = replace_exact(body, "  byte *pbVar35;", "  GuestPtr<byte> pbVar35;")

    local_block = """  float local_a0;
  float local_9c;
  float local_98;
  float local_94;
  byte *local_90;
  int local_8c;
  int local_88;
  int local_84;
  int local_80;
  int local_7c;
  int local_78;
  int local_74;
  int local_70;
  uint local_6c;
  int local_68;
  int local_64;
  int local_60;"""
    lowered_locals = """  GuestLocal<float> local_a0(OwnerLocalAddress(0x00U));
  GuestLocal<float> local_9c(OwnerLocalAddress(0x04U));
  GuestLocal<float> local_98(OwnerLocalAddress(0x08U));
  GuestLocal<float> local_94(OwnerLocalAddress(0x0CU));
  GuestLocal<GuestPtr<byte>> local_90(OwnerLocalAddress(0x10U));
  GuestLocal<int> local_8c(OwnerLocalAddress(0x14U));
  GuestLocal<int> local_88(OwnerLocalAddress(0x18U));
  GuestLocal<int> local_84(OwnerLocalAddress(0x1CU));
  GuestLocal<int> local_80(OwnerLocalAddress(0x20U));
  GuestLocal<int> local_7c(OwnerLocalAddress(0x24U));
  GuestLocal<int> local_78(OwnerLocalAddress(0x28U));
  GuestLocal<int> local_74(OwnerLocalAddress(0x2CU));
  GuestLocal<int> local_70(OwnerLocalAddress(0x30U));
  GuestLocal<uint> local_6c(OwnerLocalAddress(0x34U));
  GuestLocal<int> local_68(OwnerLocalAddress(0x38U));
  GuestLocal<int> local_64(OwnerLocalAddress(0x3CU));
  GuestLocal<int> local_60(OwnerLocalAddress(0x40U));"""
    body = replace_exact(body, local_block, lowered_locals)

    body = replace_exact(
        body,
        "        float shock_position[3];",
        "        GuestPtr<float> shock_position(OwnerLocalAddress(0x04U));",
    )
    body = replace_exact(
        body,
        "        float floor_probe[3];",
        "        GuestPtr<float> floor_probe(OwnerLocalAddress(0x04U));",
    )
    body = replace_exact(
        body,
        "          float ripple_position[3];",
        "          GuestPtr<float> ripple_position(OwnerLocalAddress(0x04U));",
    )

    body = replace_exact(
        body,
        "(**(code **)(uintptr_t)(u32)(param_1 + 0x1708))(param_1,param_2);",
        "InvokeDynamicPlayerAction(\n"
        "        GuestRef<u32>(param_1 + 0x1708), param_1, param_2);",
    )
    body = lower_pointer_accesses(body)
    if body.count(
        "GuestIndirectRef<uint>(param_1 + 0x29c8)"
    ) != 1:
        raise RuntimeError(
            "Player_UpdateCommon input double-dereference lowering drifted"
        )
    if re.search(
        r"GuestRef<[^>]+>\\(GuestRef<[^>]+>\\([^)]*\\)\\)",
        body,
    ):
        raise RuntimeError(
            "ambiguous nested GuestRef construction remains after lowering"
        )

    body = replace_exact(body, "pbVar17 = local_90;", "pbVar17 = static_cast<GuestPtr<byte>>(local_90);")
    body = body.replace("VectorSignedToFloat(", "TargetSignedToFloat(")
    body = body.replace("VectorUnsignedToFloat(", "TargetUnsignedToFloat(")
    body = body.replace("oot3d_float_to_bits(", "FloatToBits(")
    body = body.replace("oot3d_u32_to_float(", "BitsToFloat(")
    body = body.replace("__builtin_fabsf(", "TargetAbs(")
    body = body.replace("__builtin_sqrtf(", "TargetSqrt(")
    body = body.replace("ABS(", "TargetAbs(")

    # The producer helper's second argument was the explicit conversion mode.
    body = re.sub(
        r"TargetSignedToFloat\((?P<value>[^,\n]+),\s*0\s*\)",
        r"TargetSignedToFloat(\g<value>)",
        body,
    )
    body = re.sub(
        r"TargetUnsignedToFloat\((?P<value>[^,\n]+),\s*0\s*\)",
        r"TargetUnsignedToFloat(\g<value>)",
        body,
    )
    body = body.replace(",0\n                                       )", "\n                                       )")
    body = body.replace(",\n                                          0)", ")")

    macros = []
    undefs = []
    for index, (name, type_name, _) in enumerate(literals):
        macros.append(
            f"#define {name} ({literal_expression(type_name, index)})"
        )
        undefs.append(f"#undef {name}")

    prologue = f'''// Generated by generate_import.py from the immutable d5c2927 package.
// The source hash and target body identity are recorded in import_manifest.json.
#include "oot3d/player_update_common_owner.h"

#include <bit>
#include <cstdint>

#if defined(_MSC_VER)
#pragma fenv_access(on)
#else
#pragma STDC FENV_ACCESS ON
#endif

namespace Oot3dSourcePlayerUpdateCommon {{

{chr(10).join(macros)}

'''
    epilogue = f'''

{chr(10).join(reversed(undefs))}

}} // namespace Oot3dSourcePlayerUpdateCommon
'''
    return prologue + body + epilogue


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--header-output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    source_bytes = args.source.read_bytes()
    digest = hashlib.sha256(source_bytes).hexdigest()
    if digest != EXPECTED_SOURCE_SHA256:
        raise RuntimeError(
            f"source hash mismatch: expected {EXPECTED_SOURCE_SHA256}, got {digest}"
        )
    source = source_bytes.decode("utf-8")
    literals = parse_literals(source)
    output = lower(source, literals)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.header_output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8", newline="\n")
    args.header_output.write_text(
        generated_header(literals), encoding="utf-8", newline="\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
