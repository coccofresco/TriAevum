#include "oot3d_n64_integrated_ui_runtime.h"

#include "oot3d_ui/ui_frontend_resources.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace oot3d::ui {
namespace {

// The source coordinates preserve the 320x240 N64 file-select layout inside
// a centered 16:9 canvas. Values and texture dimensions follow
// ovl_file_choose and title_static rather than the OoT3D presentation.
constexpr float kCanvasWidth = 1280.0F / 3.0F;
constexpr float kCanvasHeight = 240.0F;
constexpr float kContentLeft = (kCanvasWidth - 320.0F) * 0.5F;
constexpr float kFileButtonX = kContentLeft + 38.0F;
constexpr float kNameBoxX = kFileButtonX + 64.0F;
constexpr float kFileButtonWidth = 64.0F;
constexpr float kFileButtonHeight = 16.0F;
constexpr float kNameBoxWidth = 108.0F;
constexpr float kNameBoxHeight = 16.0F;
constexpr float kHighlightWidth = 72.0F;
constexpr float kHighlightHeight = 24.0F;
constexpr std::array<float, 3> kFileRowY = {55.0F, 91.0F, 127.0F};
constexpr float kActionButtonY = 172.0F;
constexpr float kCopyButtonX = kContentLeft + 66.0F;
constexpr float kEraseButtonX = kContentLeft + 190.0F;
constexpr float kOptionsButtonX = kContentLeft + 128.0F;
constexpr float kOptionsButtonY = 202.0F;

constexpr float kNameEntryPanelX = kContentLeft + 12.0F;
constexpr float kNameEntryPanelY = 10.0F;
constexpr float kNameEntryPanelWidth = 296.0F;
constexpr float kNameEntryPanelHeight = 220.0F;
constexpr float kNameEntryNameBoxX = kContentLeft + 106.0F;
constexpr float kNameEntryNameBoxY = 42.0F;
constexpr float kNameEntryKeyWidth = 14.0F;
constexpr float kNameEntryKeyHeight = 14.0F;
constexpr float kNameEntryKeyStep = 18.0F;
constexpr std::array<float, 5> kNameEntryRowY = {
    78.0F, 99.0F, 120.0F, 141.0F, 162.0F};
constexpr std::array<std::int32_t, 5> kLatinRowStarts = {0, 12, 22, 31,
                                                         38};
constexpr std::array<std::int32_t, 5> kLatinRowCounts = {12, 10, 9, 7, 9};
constexpr std::array<std::int32_t, 5> kLatinRowMinimumColumns = {0, 0, 0,
                                                                 0, -3};
constexpr float kNameEntryBackspaceX = kContentLeft + 274.0F;
constexpr float kNameEntryBackspaceY = 103.0F;
constexpr float kNameEntryEndX = kContentLeft + 258.0F;
constexpr float kNameEntryEndY = 188.0F;
constexpr float kNameEntryConfirmationPromptX = kContentLeft + 96.0F;
constexpr float kNameEntryConfirmationPromptY = 90.0F;
constexpr float kNameEntryConfirmationYesX = kContentLeft + 64.0F;
constexpr float kNameEntryConfirmationQuitX = kContentLeft + 192.0F;
constexpr float kNameEntryConfirmationButtonY = 126.0F;

constexpr UiColor kWhite{1.0F, 1.0F, 1.0F, 1.0F};
constexpr UiColor kN64ActiveBlue{100.0F / 255.0F, 150.0F / 255.0F,
                                 1.0F, 1.0F};
constexpr UiColor kN64InactiveGray{100.0F / 255.0F, 100.0F / 255.0F,
                                   100.0F / 255.0F, 1.0F};

void AppendSolid(std::vector<UiPrimitive>& output, UiSubsystem subsystem,
                 UiPrimitiveRole role, UiRect destination, UiColor color,
                 std::uint32_t layer) {
    UiPrimitive primitive;
    primitive.subsystem = subsystem;
    primitive.role = role;
    primitive.destination = destination;
    primitive.color = color;
    primitive.layer = layer;
    output.push_back(std::move(primitive));
}

void AppendTexture(std::vector<UiPrimitive>& output,
                   UiSubsystem subsystem, UiPrimitiveRole role,
                   std::string_view semanticName, UiRect destination,
                   UiColor color, std::uint32_t layer,
                   std::uint32_t sourceQuad) {
    UiPrimitive primitive;
    primitive.subsystem = subsystem;
    primitive.role = role;
    primitive.source_quad = sourceQuad;
    primitive.texture.semantic_name = std::string(semanticName);
    primitive.destination = destination;
    primitive.uv = {0.0F, 0.0F, 1.0F, 1.0F};
    primitive.color = color;
    primitive.layer = layer;
    output.push_back(std::move(primitive));
}

bool IsVisibleSlotPopulated(const UiFileSelectContent& content,
                            std::size_t slot) {
    return slot < content.slots.size() && content.slots[slot].valid.IsKnown() &&
           content.slots[slot].valid.value;
}

UiRect HighlightDestination(std::int32_t selectedSlot) {
    if (selectedSlot >= 0 && selectedSlot < 3) {
        return {kFileButtonX - 4.0F,
                kFileRowY[static_cast<std::size_t>(selectedSlot)] - 4.0F,
                kHighlightWidth, kHighlightHeight};
    }
    if (selectedSlot == 3) {
        return {kCopyButtonX - 4.0F, kActionButtonY - 4.0F,
                kHighlightWidth, kHighlightHeight};
    }
    if (selectedSlot == 4) {
        return {kEraseButtonX - 4.0F, kActionButtonY - 4.0F,
                kHighlightWidth, kHighlightHeight};
    }
    return {kOptionsButtonX - 4.0F, kOptionsButtonY - 4.0F,
            kHighlightWidth, kHighlightHeight};
}

std::uint16_t NormalizeN64GlyphCodeUnit(std::uint16_t codeUnit) noexcept {
    if (codeUnit >= 0xFF01U && codeUnit <= 0xFF5EU) {
        return static_cast<std::uint16_t>(codeUnit - 0xFEE0U);
    }
    if (codeUnit == 0x3000U) {
        return 0x20U;
    }
    if (codeUnit == 0x2018U || codeUnit == 0x2019U) {
        return 0x27U;
    }
    return codeUnit;
}

std::string GlyphSemantic(std::uint16_t codeUnit) {
    const std::uint16_t normalized = NormalizeN64GlyphCodeUnit(codeUnit);
    if (normalized < 0x20U || normalized > 0x7EU) {
        return {};
    }
    std::array<char, 16> semantic{};
    std::snprintf(semantic.data(), semantic.size(), "n64/glyph/%02X",
                  static_cast<unsigned>(normalized));
    return semantic.data();
}

void AppendGlyph(std::vector<UiPrimitive>& output, std::uint16_t codeUnit,
                 UiRect destination, UiColor color, std::uint32_t layer,
                 std::uint32_t sourceQuad) {
    const std::string semantic = GlyphSemantic(codeUnit);
    if (semantic.empty()) {
        return;
    }
    AppendTexture(output, UiSubsystem::NameEntry,
                  UiPrimitiveRole::NameEntryGlyph, semantic, destination,
                  color, layer, sourceQuad);
}

UiNameEntryAlphabetResourceView ResolveLatinAlphabet(
    const UiNameEntryContent& content) noexcept {
    if (content.keyboard_page.IsKnown() &&
        content.keyboard_page.value == 3) {
        return Oot3dNameEntryLocalizedAlphabetResource(
            UiNameEntryLocalizedAlphabet::European);
    }
    const std::int32_t variant = content.latin_variant.IsKnown()
                                     ? content.latin_variant.value
                                     : 0;
    switch (variant) {
    case 1:
        return Oot3dNameEntryLocalizedAlphabetResource(
            UiNameEntryLocalizedAlphabet::Uppercase);
    case 2:
        return Oot3dNameEntryLocalizedAlphabetResource(
            UiNameEntryLocalizedAlphabet::Symbols);
    default:
        return Oot3dNameEntryLocalizedAlphabetResource(
            UiNameEntryLocalizedAlphabet::Lowercase);
    }
}

float CenteredRowX(std::int32_t keyCount) noexcept {
    const float rowWidth =
        static_cast<float>(keyCount - 1) * kNameEntryKeyStep +
        kNameEntryKeyWidth;
    return (kCanvasWidth - rowWidth) * 0.5F;
}

void AppendNameEntryPresentation(const UiNameEntryContent& content,
                                 std::vector<UiPrimitive>& output) {
    if (!content.controller_state.IsKnown() ||
        content.controller_state.value == 0) {
        return;
    }

    AppendSolid(output, UiSubsystem::NameEntry,
                UiPrimitiveRole::PauseBackground,
                {0.0F, 0.0F, kCanvasWidth, kCanvasHeight},
                {0.015F, 0.018F, 0.045F, 1.0F}, 0U);
    AppendSolid(output, UiSubsystem::NameEntry,
                UiPrimitiveRole::PauseBackground,
                {kNameEntryPanelX, kNameEntryPanelY,
                 kNameEntryPanelWidth, kNameEntryPanelHeight},
                {0.025F, 0.055F, 0.11F, 0.96F}, 1U);
    AppendTexture(output, UiSubsystem::NameEntry,
                  UiPrimitiveRole::NameEntryGlyph,
                  "n64/name_entry/title/name",
                  {kContentLeft + 132.0F, 18.0F, 56.0F, 16.0F}, kWhite,
                  10U, 0U);
    AppendTexture(output, UiSubsystem::NameEntry,
                  UiPrimitiveRole::NameEntryGlyph,
                  "n64/file_select/name_box",
                  {kNameEntryNameBoxX, kNameEntryNameBoxY, 108.0F, 16.0F},
                  kN64ActiveBlue, 10U, 0U);

    for (std::size_t index = 0; index < content.editable_name.size();
         ++index) {
        if (!content.editable_name[index].IsKnown()) {
            continue;
        }
        AppendGlyph(output, content.editable_name[index].value,
                    {kNameEntryNameBoxX + 6.0F +
                         static_cast<float>(index) * 12.0F,
                     kNameEntryNameBoxY + 1.0F, 14.0F, 14.0F},
                    kWhite, 20U, static_cast<std::uint32_t>(index));
    }

    if (content.keyboard_page.IsKnown() &&
        content.keyboard_page.value == 4) {
        const std::int32_t choice =
            content.confirmation_choice.IsKnown()
                ? std::clamp(content.confirmation_choice.value, 0, 1)
                : 0;
        AppendTexture(output, UiSubsystem::NameEntry,
                      UiPrimitiveRole::NameEntryGlyph,
                      "n64/name_entry/confirm/prompt",
                      {kNameEntryConfirmationPromptX,
                       kNameEntryConfirmationPromptY, 128.0F, 16.0F},
                      kWhite, 20U, 0U);
        AppendTexture(output, UiSubsystem::NameEntry,
                      UiPrimitiveRole::ActionButton,
                      "n64/name_entry/confirm/yes",
                      {kNameEntryConfirmationYesX,
                       kNameEntryConfirmationButtonY, 64.0F, 16.0F},
                      choice == 1 ? kN64ActiveBlue : kN64InactiveGray,
                      20U, 0U);
        AppendTexture(output, UiSubsystem::NameEntry,
                      UiPrimitiveRole::ActionButton,
                      "n64/name_entry/confirm/quit",
                      {kNameEntryConfirmationQuitX,
                       kNameEntryConfirmationButtonY, 64.0F, 16.0F},
                      choice == 0 ? kN64ActiveBlue : kN64InactiveGray,
                      20U, 1U);
        const float highlightX =
            choice == 1 ? kNameEntryConfirmationYesX
                        : kNameEntryConfirmationQuitX;
        AppendTexture(output, UiSubsystem::NameEntry,
                      UiPrimitiveRole::PauseCursor,
                      "n64/file_select/highlight",
                      {highlightX - 4.0F,
                       kNameEntryConfirmationButtonY - 4.0F,
                       kHighlightWidth, kHighlightHeight},
                      kN64ActiveBlue, 30U,
                      choice == 1 ? 0U : 1U);
        return;
    }

    const UiNameEntryAlphabetResourceView alphabet =
        ResolveLatinAlphabet(content);
    for (std::size_t row = 0; row < kLatinRowCounts.size(); ++row) {
        const std::int32_t count = kLatinRowCounts[row];
        const std::int32_t start = kLatinRowStarts[row];
        const float rowX = CenteredRowX(count);
        for (std::int32_t position = 0; position < count; ++position) {
            const std::int32_t tableIndex = start + position;
            if (tableIndex < 0 ||
                static_cast<std::size_t>(tableIndex) >=
                    alphabet.code_unit_count) {
                continue;
            }
            const UiRect destination{
                rowX + static_cast<float>(position) * kNameEntryKeyStep,
                kNameEntryRowY[row], kNameEntryKeyWidth,
                kNameEntryKeyHeight};
            AppendGlyph(output, alphabet.code_units[tableIndex], destination,
                        kWhite, 20U,
                        static_cast<std::uint32_t>(tableIndex));

            if (content.cursor_column.IsKnown() &&
                content.cursor_row.IsKnown() &&
                content.cursor_row.value == static_cast<std::int32_t>(row) &&
                content.cursor_column.value ==
                    kLatinRowMinimumColumns[row] + position) {
                AppendTexture(
                    output, UiSubsystem::NameEntry,
                    UiPrimitiveRole::PauseCursor,
                    "n64/name_entry/highlight/character",
                    {destination.x - 5.0F, destination.y - 5.0F, 24.0F,
                     24.0F},
                    kN64ActiveBlue, 30U,
                    static_cast<std::uint32_t>(tableIndex));
            }
        }
    }

    AppendTexture(output, UiSubsystem::NameEntry,
                  UiPrimitiveRole::ActionButton,
                  "n64/name_entry/backspace",
                  {kNameEntryBackspaceX, kNameEntryBackspaceY, 28.0F,
                   16.0F},
                  kWhite, 20U, 0U);
    AppendTexture(output, UiSubsystem::NameEntry,
                  UiPrimitiveRole::ActionButton, "n64/name_entry/end",
                  {kNameEntryEndX, kNameEntryEndY, 44.0F, 16.0F}, kWhite,
                  20U, 0U);

    if (content.cursor_column.IsKnown() && content.cursor_row.IsKnown()) {
        const std::int32_t column = content.cursor_column.value;
        const std::int32_t row = content.cursor_row.value;
        if ((row == 1 && column == 10) ||
            (row == 2 && column == 9)) {
            AppendTexture(output, UiSubsystem::NameEntry,
                          UiPrimitiveRole::PauseCursor,
                          "n64/name_entry/highlight/small",
                          {kNameEntryBackspaceX - 6.0F,
                           kNameEntryBackspaceY - 4.0F, 40.0F, 24.0F},
                          kN64ActiveBlue, 30U, 0U);
        } else if (row >= 5) {
            AppendTexture(output, UiSubsystem::NameEntry,
                          UiPrimitiveRole::PauseCursor,
                          "n64/name_entry/highlight/medium",
                          {kNameEntryEndX - 6.0F, kNameEntryEndY - 4.0F,
                           56.0F, 24.0F},
                          kN64ActiveBlue, 30U, 0U);
        }
    }
}

} // namespace

N64IntegratedUiRuntime::N64IntegratedUiRuntime()
    : profile_(BuildOot3dNativeUiProfile()) {
    profile_.id = "n64-integrated-shadow";
    hud_texture_sources_.pause_top_page.semantic_name =
        "oot3d/native/pause_shared/pause_top_page";
    hud_texture_sources_.item_icons.semantic_name =
        "oot3d/native/pause_shared/item_icons";
    hud_texture_sources_.number_glyphs.semantic_name =
        "oot3d/native/pause_shared/number_glyphs";
    hud_asset_catalog_ =
        BuildVerifiedOot3dHudAssetCatalog(hud_texture_sources_);
    hud_layout_ = BuildCanonicalN64GameplayHudLayout();
}

const UiBackendProfile& N64IntegratedUiRuntime::Profile() const noexcept {
    return profile_;
}

UiFramePlan N64IntegratedUiRuntime::PlanFrame(
    UiSubsystem subsystem) const noexcept {
    return BuildFramePlan(profile_, subsystem);
}

void N64IntegratedUiRuntime::ObserveState(
    const UiBackendStateView& state) noexcept {
    const UiFrontendMenuContentSnapshot content =
        state.BuildFrontendMenuContent();
    ++stats_.observed_states;
    stats_.selected_slot_known = content.file_select.selected_slot.IsKnown();
    if (stats_.selected_slot_known) {
        stats_.selected_slot = content.file_select.selected_slot.value;
    }
    stats_.controller_state_known =
        content.file_select.controller_state.IsKnown();
    if (stats_.controller_state_known) {
        stats_.controller_state = content.file_select.controller_state.value;
    }
    stats_.known_file_select_states +=
        stats_.selected_slot_known && stats_.controller_state_known ? 1U : 0U;
    stats_.active_file_select_states +=
        stats_.controller_state_known && stats_.controller_state != 0 ? 1U
                                                                      : 0U;

    const UiNameEntryContent& nameEntry = content.name_entry;
    stats_.name_entry_controller_state_known =
        nameEntry.controller_state.IsKnown();
    if (stats_.name_entry_controller_state_known) {
        stats_.name_entry_controller_state = nameEntry.controller_state.value;
    }
    stats_.name_entry_keyboard_page_known =
        nameEntry.keyboard_page.IsKnown();
    if (stats_.name_entry_keyboard_page_known) {
        stats_.name_entry_keyboard_page = nameEntry.keyboard_page.value;
    }
    stats_.name_entry_cursor_known = nameEntry.cursor_column.IsKnown() &&
                                     nameEntry.cursor_row.IsKnown();
    if (stats_.name_entry_cursor_known) {
        stats_.name_entry_cursor_column = nameEntry.cursor_column.value;
        stats_.name_entry_cursor_row = nameEntry.cursor_row.value;
    }
    stats_.name_entry_length_known = nameEntry.name_length.IsKnown();
    if (stats_.name_entry_length_known) {
        stats_.name_entry_length = nameEntry.name_length.value;
    }
    stats_.name_entry_action_choice_known =
        nameEntry.action_choice.IsKnown();
    if (stats_.name_entry_action_choice_known) {
        stats_.name_entry_action_choice = nameEntry.action_choice.value;
    }
    stats_.name_entry_confirmation_choice_known =
        nameEntry.confirmation_choice.IsKnown();
    if (stats_.name_entry_confirmation_choice_known) {
        stats_.name_entry_confirmation_choice =
            nameEntry.confirmation_choice.value;
    }
    stats_.name_entry_save_commit_timer_known =
        nameEntry.save_commit_timer.IsKnown();
    if (stats_.name_entry_save_commit_timer_known) {
        stats_.name_entry_save_commit_timer =
            nameEntry.save_commit_timer.value;
    }
    stats_.known_name_entry_states +=
        stats_.name_entry_controller_state_known ? 1U : 0U;
    stats_.active_name_entry_states +=
        stats_.name_entry_controller_state_known &&
                stats_.name_entry_controller_state != 0
            ? 1U
            : 0U;
}

UiBackendIntent N64IntegratedUiRuntime::HandleInput(
    const UiInputEvent&) noexcept {
    return {};
}

void N64IntegratedUiRuntime::AppendPresentation(
    UiSubsystem subsystem, const UiBackendStateView& state,
    std::vector<UiPrimitive>& output) const {
    if (subsystem == UiSubsystem::GameplayHud) {
        (void)AppendN64GameplayHudPresentation(
            state.BuildHudContent(), hud_asset_catalog_, hud_layout_, output);
        return;
    }
    const UiFrontendMenuContentSnapshot frontend =
        state.BuildFrontendMenuContent();
    if (subsystem == UiSubsystem::NameEntry) {
        AppendNameEntryPresentation(frontend.name_entry, output);
        return;
    }
    if (subsystem != UiSubsystem::FileSelect) {
        return;
    }

    const UiFileSelectContent& content = frontend.file_select;
    if (!content.controller_state.IsKnown() ||
        content.controller_state.value == 0) {
        return;
    }
    const std::int32_t selectedSlot =
        content.selected_slot.IsKnown()
            ? std::clamp(content.selected_slot.value, 0, 5)
            : 0;

    AppendSolid(output, UiSubsystem::FileSelect,
                UiPrimitiveRole::PauseBackground,
                {0.0F, 0.0F, kCanvasWidth, kCanvasHeight},
                {0.015F, 0.018F, 0.045F, 1.0F}, 0U);
    AppendSolid(output, UiSubsystem::FileSelect,
                UiPrimitiveRole::PauseBackground,
                {kContentLeft + 12.0F, 10.0F, 296.0F, 220.0F},
                {0.025F, 0.055F, 0.11F, 0.96F}, 1U);
    AppendTexture(output, UiSubsystem::FileSelect,
                  UiPrimitiveRole::FileSlot,
                  "n64/file_select/title/select_file",
                  {kContentLeft + 96.0F, 20.0F, 128.0F, 16.0F},
                  kWhite, 10U, 0U);

    constexpr std::array<std::string_view, 3> slotTextures = {
        "n64/file_select/file_1", "n64/file_select/file_2",
        "n64/file_select/file_3"};
    for (std::size_t slot = 0; slot < slotTextures.size(); ++slot) {
        const UiColor slotColor =
            static_cast<std::int32_t>(slot) == selectedSlot
                ? kN64ActiveBlue
                : kN64InactiveGray;
        AppendTexture(output, UiSubsystem::FileSelect,
                      UiPrimitiveRole::FileSlot, slotTextures[slot],
                      {kFileButtonX, kFileRowY[slot], kFileButtonWidth,
                       kFileButtonHeight},
                      slotColor, 20U, static_cast<std::uint32_t>(slot));
        if (IsVisibleSlotPopulated(content, slot)) {
            AppendTexture(output, UiSubsystem::FileSelect,
                          UiPrimitiveRole::FileSlot,
                          "n64/file_select/name_box",
                          {kNameBoxX, kFileRowY[slot], kNameBoxWidth,
                           kNameBoxHeight},
                          slotColor, 20U,
                          static_cast<std::uint32_t>(slot));
        }
    }

    AppendTexture(output, UiSubsystem::FileSelect,
                  UiPrimitiveRole::ActionButton, "n64/file_select/copy",
                  {kCopyButtonX, kActionButtonY, kFileButtonWidth,
                   kFileButtonHeight},
                  selectedSlot == 3 ? kN64ActiveBlue : kN64InactiveGray,
                  20U, 3U);
    AppendTexture(output, UiSubsystem::FileSelect,
                  UiPrimitiveRole::ActionButton, "n64/file_select/erase",
                  {kEraseButtonX, kActionButtonY, kFileButtonWidth,
                   kFileButtonHeight},
                  selectedSlot == 4 ? kN64ActiveBlue : kN64InactiveGray,
                  20U, 4U);
    AppendTexture(output, UiSubsystem::FileSelect,
                  UiPrimitiveRole::ActionButton, "n64/file_select/options",
                  {kOptionsButtonX, kOptionsButtonY, kFileButtonWidth,
                   kFileButtonHeight},
                  selectedSlot == 5 ? kN64ActiveBlue : kN64InactiveGray,
                  20U, 5U);
    AppendTexture(output, UiSubsystem::FileSelect,
                  UiPrimitiveRole::PauseCursor,
                  "n64/file_select/highlight",
                  HighlightDestination(selectedSlot), kN64ActiveBlue, 30U,
                  static_cast<std::uint32_t>(selectedSlot));
}

void N64IntegratedUiRuntime::SetHudTextureSources(
    Oot3dHudTextureSources sources) {
    const auto same_identity = [](const UiTextureIdentity& left,
                                  const UiTextureIdentity& right) {
        return left.guest_resource_address == right.guest_resource_address &&
               left.guest_surface_address == right.guest_surface_address &&
               left.semantic_name == right.semantic_name;
    };
    if (same_identity(hud_texture_sources_.pause_top_page,
                      sources.pause_top_page) &&
        same_identity(hud_texture_sources_.item_icons,
                      sources.item_icons) &&
        same_identity(hud_texture_sources_.number_glyphs,
                      sources.number_glyphs)) {
        return;
    }
    hud_texture_sources_ = std::move(sources);
    hud_asset_catalog_ =
        BuildVerifiedOot3dHudAssetCatalog(hud_texture_sources_);
}

const N64IntegratedUiRuntimeStats&
N64IntegratedUiRuntime::Stats() const noexcept {
    return stats_;
}

} // namespace oot3d::ui
