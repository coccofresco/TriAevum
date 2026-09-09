#pragma once

#include "oot3d_ui/ui_contract_types.h"
#include "oot3d_ui/ui_localized_menu_resources.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

struct UiPauseNativeVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct UiPauseNativeVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// One descriptor is retained for every distinct geometry contract selected by
// the original OoT3D pause constructors. Dynamic means that the native owner
// fills the stream at runtime; it is not an invitation to infer coordinates.
enum class UiPauseNativeGeometryProfile : std::uint8_t {
    MapAreaEightQuad = 0,
    TouchEightQuad,
    MapAreaTwentyFiveQuad,
    TouchTwentyFiveQuad,
    QuadGroupLayout0,
    QuadGroupLayout1,
    QuadGroupLayout2,
    QuadGroupLayout3,
    QuadGroupLayout4,
    QuadGroupLayout5,
    QuadGroupLayout6,
    QuadGroupLayout7,
    QuadGroupLayout8,
    GearMain,
    GearAuxiliary0,
    GearAuxiliary1,
    QuestCursor,
    QuestIcon,
    QuestAuxiliary0,
    QuestAuxiliary1,
    QuestMain,
    Count,
};

enum class UiPauseGeometryEncoding : std::uint8_t {
    QuadRectStreams,
    DirectVertexStreams,
};

enum class UiPauseGeometryStreamSource : std::uint8_t {
    StaticNativeTable,
    NativeConstructorFormula,
    NativeRuntimeStorage,
    ConstantNativeValue,
    Absent,
};

enum class UiPauseGeometryIndexPattern : std::uint8_t {
    QuadTriangles,
    SequentialFour,
    Absent,
};

enum class UiPauseGeometryCountSource : std::uint8_t {
    FixedConstructor,
    ImmediateArgument,
    NativeTablePlusBias,
};

struct UiPauseNativeGeometryProfileDescriptor {
    UiPauseNativeGeometryProfile profile =
        UiPauseNativeGeometryProfile::Count;
    UiSubsystem subsystem = UiSubsystem::PauseShell;
    UiPauseSharedTextureSlot texture_slot = UiPauseSharedTextureSlot::Count;
    const char* semantic_name = nullptr;
    const char* semantic_role = nullptr;
    const char* native_constructor = nullptr;
    std::uint32_t native_constructor_entry = 0;
    std::uint16_t native_constructor_body_bytes = 0;
    std::int8_t native_selector = -1;
    std::uint8_t entry_capacity = 0;
    std::uint8_t fixed_entry_count = 0;
    std::uint16_t vertex_capacity = 0;
    std::uint16_t index_capacity = 0;
    UiPauseGeometryEncoding encoding =
        UiPauseGeometryEncoding::QuadRectStreams;
    UiPauseGeometryCountSource count_source =
        UiPauseGeometryCountSource::FixedConstructor;
    UiPauseGeometryStreamSource position_source =
        UiPauseGeometryStreamSource::Absent;
    UiPauseGeometryStreamSource texture_coordinate_source =
        UiPauseGeometryStreamSource::Absent;
    UiPauseGeometryIndexPattern index_pattern =
        UiPauseGeometryIndexPattern::Absent;
    bool constant_alpha_one = false;
    bool constant_vertex_color_one = false;
    bool has_auxiliary_extent = false;
    UiPauseNativeVec2 auxiliary_extent{};

    constexpr bool HasStaticQuadProjection() const noexcept {
        return encoding == UiPauseGeometryEncoding::QuadRectStreams &&
               position_source !=
                   UiPauseGeometryStreamSource::NativeRuntimeStorage &&
               texture_coordinate_source !=
                   UiPauseGeometryStreamSource::NativeRuntimeStorage;
    }
};

struct UiPauseNativeQuadProjection {
    UiPauseNativeGeometryProfile profile =
        UiPauseNativeGeometryProfile::Count;
    std::uint8_t quad_index = 0;
    UiPauseNativeVec2 rect_origin{};
    UiPauseNativeVec2 rect_extent{};
    UiPauseNativeVec2 texture_origin{};
    UiPauseNativeVec2 texture_extent{};
};

struct UiPauseNativeDirectVertex {
    UiPauseNativeGeometryProfile profile =
        UiPauseNativeGeometryProfile::Count;
    std::uint8_t vertex_index = 0;
    UiPauseNativeVec3 position{};
    UiPauseNativeVec2 texture_coordinate{};
};

struct UiPauseNativeGeometryCallDescriptor {
    UiPauseNativeGeometryProfile profile =
        UiPauseNativeGeometryProfile::Count;
    std::uint32_t call_site = 0;
    const char* call_condition = nullptr;
    std::uint32_t owner_entry = 0;
    const char* owner_name = nullptr;
    std::uint16_t owner_body_bytes = 0;
    std::int8_t selector_value = -1;
    UiPauseGeometryCountSource count_source =
        UiPauseGeometryCountSource::FixedConstructor;
    std::uint8_t count_value = 0;
    std::uint32_t count_table_address = 0;
    std::int8_t count_bias = 0;
};

inline constexpr std::size_t kOot3dPauseNativeGeometryProfileCount = 21;
inline constexpr std::size_t kOot3dPauseNativeQuadProjectionCount = 170;
inline constexpr std::size_t kOot3dPauseNativeDirectVertexCount = 12;
inline constexpr std::size_t kOot3dPauseNativeGeometryCallCount = 21;

const std::array<UiPauseNativeGeometryProfileDescriptor,
                 kOot3dPauseNativeGeometryProfileCount>&
Oot3dPauseNativeGeometryProfiles() noexcept;

const UiPauseNativeGeometryProfileDescriptor* Oot3dPauseNativeGeometryProfile(
    UiPauseNativeGeometryProfile profile) noexcept;

const std::array<UiPauseNativeQuadProjection,
                 kOot3dPauseNativeQuadProjectionCount>&
Oot3dPauseNativeQuadProjections() noexcept;

const UiPauseNativeQuadProjection* Oot3dPauseNativeQuadProjectionFor(
    UiPauseNativeGeometryProfile profile, std::uint8_t quad_index) noexcept;

const std::array<UiPauseNativeDirectVertex,
                 kOot3dPauseNativeDirectVertexCount>&
Oot3dPauseNativeDirectVertices() noexcept;

const UiPauseNativeDirectVertex* Oot3dPauseNativeDirectVertexFor(
    UiPauseNativeGeometryProfile profile, std::uint8_t vertex_index) noexcept;

const std::array<UiPauseNativeGeometryCallDescriptor,
                 kOot3dPauseNativeGeometryCallCount>&
Oot3dPauseNativeGeometryCalls() noexcept;

static_assert(
    static_cast<std::size_t>(UiPauseNativeGeometryProfile::Count) ==
    kOot3dPauseNativeGeometryProfileCount);

} // namespace oot3d::ui
