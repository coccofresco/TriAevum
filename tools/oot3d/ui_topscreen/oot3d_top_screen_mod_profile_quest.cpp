// TopScreen mod profile: quest and system menu geometry, model transforms and the pause
// projection.
#include "oot3d_top_screen_mod_profile_internal.h"

namespace Oot3dNativeGame {

TopScreenQuestGeometryStats TransformTopScreenQuestGeometry(
    std::span<std::array<TopScreenVec3, 4>> quads,
    const TopScreenQuestGeometryContext &context) noexcept {
  TopScreenQuestGeometryStats stats;
  if (context.NativePageGateActive) {
    return stats;
  }
  const auto translateTo = [&stats](std::array<TopScreenVec3, 4> &quad,
                                    float centerX, float centerY, float targetX,
                                    float targetY, bool hidden) {
    for (auto &vertex : quad) {
      vertex.X = (vertex.X - centerX) + targetX;
      vertex.Y = (vertex.Y - centerY) + targetY;
    }
    ++stats.QuadsTranslated;
    stats.QuadsHidden += hidden ? 1U : 0U;
  };

  for (auto &quad : quads) {
    ++stats.QuadsVisited;
    // The native Quest clock occupies quad 5; digits have their own renderer.
    if (quads.size() > 5U && &quad == &quads[5]) {
      stats.QuadsTranslated += RelocateTopScreenTimerQuad(quad, context.HudScale) ? 1U : 0U;
      continue;
    }
    float centerX = 0.0F;
    float centerY = 0.0F;
    for (const auto &vertex : quad) {
      centerX += vertex.X * 0.25F;
      centerY += vertex.Y * 0.25F;
    }

    // Original 005C7F48 relocates the map region by its owner's offset only.
    // The map and its separate indicator streams do not use the button scale.
    const bool mapRegion = centerX < 110.0F && centerY > 130.0F;
    if (context.PauseState < 12U) {
      if (centerX <= 330.0F) {
        if (centerX < 110.0F && centerY > 130.0F) {
          if (context.LeftRegionOffsetX == 0.0F &&
              context.LeftRegionOffsetY == 0.0F) {
            translateTo(quad, centerX, centerY, 500.0F, 300.0F, true);
          } else {
            for (auto &vertex : quad) {
              vertex.X += context.LeftRegionOffsetX;
              vertex.Y += context.LeftRegionOffsetY;
            }
            ++stats.QuadsTranslated;
          }
        }
      } else if (centerX < 398.0F && centerY > 150.0F) {
        if (!context.PlayerSpecialState || centerY < 195.0F) {
          // SingleScreen 3.0 0x005C89D0..0x005C8B8C: the A ring and icons
          // (bounds within 0.6..1.4 of square) are normalized to 32 px, the
          // wide label takes 0.68, all centred on (288, 24) with the button
          // group's size and offset. The HUD scale below then gives 3.0's
          // 400 + s(288 - 400) - margin, 24s + margin.
          float minimumX = quad[0].X, maximumX = quad[0].X;
          float minimumY = quad[0].Y, maximumY = quad[0].Y;
          for (const auto &vertex : quad) {
            minimumX = std::min(minimumX, vertex.X);
            maximumX = std::max(maximumX, vertex.X);
            minimumY = std::min(minimumY, vertex.Y);
            maximumY = std::max(maximumY, vertex.Y);
          }
          const float width = maximumX - minimumX;
          const float height = maximumY - minimumY;
          const bool square = width > 1.0F && height > 1.0F &&
                              height * 1.4F >= width && height * 0.6F <= width;
          if (!square && context.ActionLabelHidden) {
            translateTo(quad, centerX, centerY, 500.0F, 300.0F, true);
          } else {
            const float size = (square ? 32.0F / width : 0.68F) * context.ButtonGroupScale;
            const float targetX = 400.0F + (288.0F - 400.0F) * context.ButtonGroupScale +
                                  context.ButtonGroupOffsetX;
            const float targetY = 24.0F * context.ButtonGroupScale + context.ButtonGroupOffsetY;
            for (auto &vertex : quad) {
              vertex.X = targetX + (vertex.X - centerX) * size;
              vertex.Y = targetY + (vertex.Y - centerY) * size;
            }
            ++stats.QuadsTranslated;
            ++stats.QuadsScaled;
          }
        } else {
          translateTo(quad, centerX, centerY, 500.0F, 300.0F, true);
        }
      }
    } else if (centerX > 330.0F && centerX < 398.0F && centerY > 150.0F) {
      translateTo(quad, centerX, centerY, 500.0F, 300.0F, true);
    }

    if (!mapRegion && (context.HudScale != 1.0F || context.HudMarginX != 0.0F ||
        context.HudMarginY != 0.0F)) {
      centerX = 0.0F;
      centerY = 0.0F;
      for (const auto &vertex : quad) {
        centerX += vertex.X * 0.25F;
        centerY += vertex.Y * 0.25F;
      }
      if (centerX <= 430.0F && centerY <= 280.0F) {
        const bool left = centerX < 200.0F;
        const bool top = centerY < 120.0F;
        const float pivotX = left ? 0.0F : 400.0F;
        const float pivotY = top ? 0.0F : 240.0F;
        const float insetX = left ? context.HudMarginX : -context.HudMarginX;
        const float insetY = top ? context.HudMarginY : -context.HudMarginY;
        for (auto &vertex : quad) {
          vertex.X =
              pivotX + (vertex.X - pivotX) * context.HudScale + insetX;
          vertex.Y =
              pivotY + (vertex.Y - pivotY) * context.HudScale + insetY;
        }
        ++stats.QuadsScaled;
      }
    }
  }
  return stats;
}

bool ReadTopScreenQuestGeometryContext(
    NativeA32Memory &memory, TopScreenQuestGeometryContext *context,
    const TopScreenPauseProjectionState *projection, std::string *error) {
  if (context == nullptr) {
    if (error != nullptr)
      *error = "missing Quest geometry context output";
    return false;
  }
  constexpr std::uint32_t kPauseState = 0x0050AF68U;
  constexpr std::uint32_t kSceneOwner = 0x005043E0U;
  constexpr std::array<std::uint32_t, 4> kNativePageGates{
      0x00504484U, 0x0050672CU, 0x00506CE8U, 0x0050A530U};
  std::uint32_t pauseState = 0U;
  std::uint32_t sceneOwner = 0U;
  if (!memory.Read32(kPauseState, &pauseState) ||
      !memory.Read32(kSceneOwner, &sceneOwner)) {
    if (error != nullptr)
      *error = "cannot read Quest geometry state roots";
    return false;
  }
  bool nativePageGateActive = false;
  for (const auto gateAddress : kNativePageGates) {
    std::uint32_t gate = 0U;
    if (!memory.Read32(gateAddress, &gate)) {
      if (error != nullptr)
        *error = "cannot read Quest native-page gate";
      return false;
    }
    nativePageGateActive = nativePageGateActive || gate != 0U;
  }
  bool specialMode = false;
  if (sceneOwner != 0U) {
    std::uint8_t sceneMode = 0U;
    std::uint8_t sceneVariant = 0U;
    if (!memory.Read8(sceneOwner + 0x100U, &sceneMode) ||
        !memory.Read8(sceneOwner + 0x101U, &sceneVariant)) {
      if (error != nullptr)
        *error = "cannot read Quest scene mode";
      return false;
    }
    if (sceneMode == 3U && sceneVariant == 2U) {
      std::uint32_t sceneExtension = 0U;
      std::uint32_t specialOwner = 0U;
      std::uint32_t specialValue = 0U;
      if (!memory.Read32(sceneOwner + 0x20ACU, &sceneExtension) ||
          sceneExtension == 0U ||
          !memory.Read32(sceneExtension + 0x12B8U, &specialOwner)) {
        if (error != nullptr)
          *error = "cannot read Quest special-mode owner";
        return false;
      }
      if (specialOwner != 0U) {
        specialValue = specialOwner;
      }
      specialMode = specialValue != 0U;
    }
  }
  std::uint32_t alternateHudMode = 0U;
  if (!memory.Read32(0x004FC648U + 0x48U, &alternateHudMode)) {
    if (error != nullptr)
      *error = "cannot read Quest alternate HUD mode";
    return false;
  }
  context->ActionLabelHidden = alternateHudMode == 1U || alternateHudMode == 4U;
  context->PauseState = pauseState;
  // The alternate gameplay HUD owner is not an open pause page. In
  // particular, riding must still relocate the native A/B geometry.
  context->NativePageGateActive = nativePageGateActive;
  context->PlayerSpecialState = specialMode;
  context->LeftRegionOffsetX =
      projection != nullptr ? projection->OffsetX : 0.0F;
  context->LeftRegionOffsetY =
      projection != nullptr ? projection->OffsetY : 0.0F;
  return true;
}

bool ApplyTopScreenSystemMenuLayerSuppression(
    NativeA32Memory &memory, TopScreenSystemMenuLayerStats *stats,
    std::string *error) {
  TopScreenSystemMenuLayerStats result;
  constexpr std::uint32_t kSystemMenuOwner = 0x0050A530U;
  constexpr std::uint32_t kLayerRenderer = 0x0050A510U;

  std::uint32_t systemMenuOwner = 0U;
  if (!memory.Read32(kSystemMenuOwner, &systemMenuOwner)) {
    if (error != nullptr)
      *error = "cannot read TopScreen system-menu owner";
    return false;
  }
  result.Active = systemMenuOwner != 0U;
  if (!result.Active) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  std::uint32_t renderer = 0U;
  if (!memory.Read32(kLayerRenderer, &renderer)) {
    if (error != nullptr)
      *error = "cannot read TopScreen system-menu layer renderer";
    return false;
  }
  result.RendererAddress = renderer;
  if (renderer == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  std::uint32_t quadCount = 0U;
  std::uint32_t positions = 0U;
  std::uint32_t colors = 0U;
  if (!memory.Read32(renderer, &quadCount) ||
      !memory.Read32(renderer + 0x0CU, &positions) ||
      !memory.Read32(renderer + 0x18U, &colors)) {
    if (error != nullptr)
      *error = "cannot read TopScreen system-menu layer streams";
    return false;
  }
  if (quadCount == 0U || quadCount > 0xFFU || positions == 0U ||
      colors == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  constexpr std::array<float, 4> kSuppressedColor{1.0F, 1.0F, 1.0F, 0.0F};
  for (std::uint32_t quad = 0U; quad < quadCount; ++quad) {
    std::array<float, 12> vertices{};
    if (!memory.ReadBytes(
            positions + quad * 0x30U,
            std::span<std::uint8_t>(
                reinterpret_cast<std::uint8_t *>(vertices.data()),
                sizeof(vertices)))) {
      if (error != nullptr)
        *error = "cannot read TopScreen system-menu layer quad";
      return false;
    }
    ++result.QuadsVisited;

    float minimumX = vertices[0];
    float maximumX = vertices[0];
    float minimumY = vertices[1];
    float maximumY = vertices[1];
    for (std::size_t vertex = 1U; vertex < 4U; ++vertex) {
      minimumX = std::min(minimumX, vertices[vertex * 3U]);
      maximumX = std::max(maximumX, vertices[vertex * 3U]);
      minimumY = std::min(minimumY, vertices[vertex * 3U + 1U]);
      maximumY = std::max(maximumY, vertices[vertex * 3U + 1U]);
    }
    if (maximumX - minimumX <= 130.0F ||
        maximumY - minimumY <= 80.0F) {
      continue;
    }

    const std::uint32_t colorAddress = colors + quad * 0x40U;
    for (std::size_t vertex = 0U; vertex < 4U; ++vertex) {
      if (!memory.WriteBytes(
              colorAddress + static_cast<std::uint32_t>(vertex * 0x10U),
              std::span<const std::uint8_t>(
                  reinterpret_cast<const std::uint8_t *>(
                      kSuppressedColor.data()),
                  sizeof(kSuppressedColor)))) {
        if (error != nullptr)
          *error = "cannot write TopScreen system-menu layer color";
        return false;
      }
    }
    ++result.QuadsSuppressed;
  }
  if (stats != nullptr)
    *stats = result;
  return true;
}

bool ReadTopScreenOcarinaUiActive(NativeA32Memory &memory, bool *active,
                                  std::string *error) {
  if (active == nullptr) {
    if (error != nullptr)
      *error = "missing TopScreen Ocarina UI output";
    return false;
  }
  constexpr std::uint32_t kSceneOwnerRoot = 0x005043E0U;
  std::uint32_t scene = 0U;
  if (!memory.Read32(kSceneOwnerRoot, &scene)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Ocarina scene owner";
    return false;
  }
  if (scene == 0U) {
    *active = false;
    return true;
  }

  std::uint16_t ocarinaResult = 0U;
  std::uint32_t sceneExtension = 0U;
  if (!memory.Read16(scene + 0x2B82U, &ocarinaResult) ||
      !memory.Read32(scene + 0x20ACU, &sceneExtension)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Ocarina UI state";
    return false;
  }
  if (ocarinaResult != 0x00FFU) {
    *active = true;
    return true;
  }

  if (sceneExtension != 0U) {
    std::uint32_t stateFlags = 0U;
    if (!memory.Read32(sceneExtension + 0x1714U, &stateFlags)) {
      if (error != nullptr)
        *error = "cannot read TopScreen Ocarina state flags";
      return false;
    }
    if ((stateFlags & 0x01000000U) != 0U) {
      *active = true;
      return true;
    }
  }

  std::uint16_t ocarinaAction = 0U;
  if (!memory.Read16(scene + 0x2B80U, &ocarinaAction)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Ocarina action";
    return false;
  }
  *active = ocarinaAction > 1U;
  return true;
}

bool TransformTopScreenModelRegion(
    NativeA32Memory &memory, std::uint32_t renderBufferAddress,
    const TopScreenModelRegionTransform &transform,
    TopScreenModelRegionStats *stats, std::string *error) {
  TopScreenModelRegionStats result;
  if (renderBufferAddress == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  std::uint32_t quadCount = 0U;
  std::uint32_t positionsAddress = 0U;
  std::uint32_t translationsAddress = 0U;
  if (!memory.Read32(renderBufferAddress, &quadCount) ||
      !memory.Read32(renderBufferAddress + 0x0CU, &positionsAddress) ||
      !memory.Read32(renderBufferAddress + 0x1CU, &translationsAddress)) {
    if (error != nullptr)
      *error = "cannot read TopScreen model-region render-buffer header";
    return false;
  }
  if (quadCount == 0U || quadCount > 0xFFU || positionsAddress == 0U ||
      translationsAddress == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  struct ModelQuad {
    std::array<TopScreenVec3, 4> Positions{};
    float TranslationX = 0.0F;
    float TranslationY = 0.0F;
    float CenterX = 0.0F;
    float CenterY = 0.0F;
    bool Selected = false;
  };
  std::vector<ModelQuad> quads(quadCount);
  result.ContractMatched = true;
  float groupCenterX = 0.0F;
  float groupCenterY = 0.0F;
  for (std::uint32_t index = 0U; index < quadCount; ++index) {
    auto &quad = quads[index];
    const std::uint32_t positions =
        positionsAddress + index * static_cast<std::uint32_t>(0x30U);
    const std::uint32_t translation = translationsAddress + index * 8U;
    std::uint32_t translationXBits = 0U;
    std::uint32_t translationYBits = 0U;
    if (!memory.ReadBytes(
            positions,
            std::span<std::uint8_t>(
                reinterpret_cast<std::uint8_t *>(quad.Positions.data()),
                sizeof(quad.Positions))) ||
        !memory.Read32(translation, &translationXBits) ||
        !memory.Read32(translation + 4U, &translationYBits)) {
      if (error != nullptr)
        *error = "cannot read TopScreen model-region streams";
      return false;
    }
    quad.TranslationX = std::bit_cast<float>(translationXBits);
    quad.TranslationY = std::bit_cast<float>(translationYBits);
    ++result.QuadsVisited;
    quad.CenterX = quad.TranslationX;
    quad.CenterY = quad.TranslationY;
    for (const auto &vertex : quad.Positions) {
      quad.CenterX += vertex.X * 0.25F;
      quad.CenterY += vertex.Y * 0.25F;
    }
    quad.Selected =
        transform.MinimumX < quad.CenterX &&
        quad.CenterX < transform.MaximumX &&
        transform.MinimumY < quad.CenterY;
    if (quad.Selected) {
      groupCenterX += quad.CenterX;
      groupCenterY += quad.CenterY;
      ++result.QuadsSelected;
    }
  }

  if (result.QuadsSelected != 0U) {
    const float selectedCount = static_cast<float>(result.QuadsSelected);
    groupCenterX /= selectedCount;
    groupCenterY /= selectedCount;
    for (std::uint32_t index = 0U; index < quadCount; ++index) {
      auto &quad = quads[index];
      if (!quad.Selected)
        continue;
      for (auto &vertex : quad.Positions) {
        vertex.X =
            transform.TargetX +
            ((quad.TranslationX + vertex.X) - groupCenterX) *
                transform.Scale -
            quad.TranslationX;
        vertex.Y =
            transform.TargetY +
            ((quad.TranslationY + vertex.Y) - groupCenterY) *
                transform.Scale -
            quad.TranslationY;
        ++result.VerticesTransformed;
      }
      const std::uint32_t positions =
          positionsAddress + index * static_cast<std::uint32_t>(0x30U);
      if (!memory.WriteBytes(
              positions,
              std::span<const std::uint8_t>(
                  reinterpret_cast<const std::uint8_t *>(
                      quad.Positions.data()),
                  sizeof(quad.Positions)))) {
        if (error != nullptr)
          *error = "cannot write TopScreen model-region positions";
        return false;
      }
    }
  }

  if (stats != nullptr)
    *stats = result;
  return true;
}

bool ApplyTopScreenQuestSubmitModelTransforms(
    NativeA32Memory &memory, const TopScreenUiConfig &config,
    TopScreenQuestModelTransformStats *stats, std::string *error) {
  TopScreenQuestModelTransformStats result;
  constexpr std::uint32_t kQuestOwnerBinding = 0x004FC688U;
  std::uint32_t owner = 0U;
  if (!memory.Read32(kQuestOwnerBinding, &owner)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest model owner";
    return false;
  }
  if (owner == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  std::uint32_t mainRenderer = 0U;
  if (!memory.Read32(owner + 0x10CU, &mainRenderer)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest main model renderer";
    return false;
  }
  TopScreenModelRegionStats modelStats;
  if (!TransformTopScreenModelRegion(
          memory, mainRenderer,
          {300.0F, 430.0F, 140.0F, 500.0F, 300.0F, 1.0F},
          &modelStats, error)) {
    return false;
  }
  result.MainModelsTransformed =
      modelStats.VerticesTransformed != 0U ? 1U : 0U;

  std::uint32_t childCountBits = 0U;
  if (!memory.Read32(owner + 0x224U, &childCountBits)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest child-model count";
    return false;
  }
  const auto childCount = static_cast<std::int32_t>(childCountBits);
  if (childCount > 0) {
    const float scale = config.HudScale;
    const float baseX = config.HudLayout == TopScreenHudLayout::Restoration
                            ? 358.0F
                            : 248.0F;
    const float baseY = config.HudLayout == TopScreenHudLayout::Restoration
                            ? 68.0F
                            : 24.0F;
    const float targetX = 400.0F + (baseX - 400.0F) * scale -
                          static_cast<float>(config.HudMarginX);
    const float targetY = baseY * scale +
                          static_cast<float>(config.HudMarginY);
    const TopScreenModelRegionTransform childTransform{
        300.0F, 430.0F, 140.0F, targetX, targetY, scale * 0.6F};
    const std::uint32_t boundedCount =
        std::min<std::uint32_t>(static_cast<std::uint32_t>(childCount), 64U);
    for (std::uint32_t index = 0U; index < boundedCount; ++index) {
      std::uint32_t child = 0U;
      if (!memory.Read32(owner + 0x0CU + index * 4U, &child)) {
        if (error != nullptr)
          *error = "cannot read TopScreen Quest child-model owner";
        return false;
      }
      if (child == 0U)
        continue;
      ++result.ChildModelsVisited;
      std::uint32_t renderer = 0U;
      if (!memory.Read32(child + 8U, &renderer)) {
        if (error != nullptr)
          *error = "cannot read TopScreen Quest child-model renderer";
        return false;
      }
      modelStats = {};
      if (!TransformTopScreenModelRegion(memory, renderer, childTransform,
                                         &modelStats, error)) {
        return false;
      }
      result.ChildModelsTransformed +=
          modelStats.VerticesTransformed != 0U ? 1U : 0U;
    }
  }

  if (stats != nullptr)
    *stats = result;
  return true;
}

bool ApplyTopScreenQuestDrawModelTransform(
    NativeA32Memory &memory, std::uint32_t sceneContextAddress,
    const TopScreenUiConfig &config, TopScreenPauseProjectionState &state,
    TopScreenQuestModelTransformStats *stats, std::string *error) {
  TopScreenQuestModelTransformStats result;
  if (state.QuestDrawModelAdjusted || sceneContextAddress == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  std::uint8_t sceneMode = 0U;
  std::uint8_t sceneVariant = 0U;
  std::uint32_t sceneExtension = 0U;
  if (!memory.Read8(sceneContextAddress + 0x100U, &sceneMode) ||
      !memory.Read8(sceneContextAddress + 0x101U, &sceneVariant) ||
      !memory.Read32(sceneContextAddress + 0x20ACU, &sceneExtension)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest draw scene context";
    return false;
  }
  if (sceneMode != 3U || sceneVariant != 2U || sceneExtension == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  constexpr std::uint32_t kOptionalPanelState = 0x005043F0U;
  constexpr std::uint32_t kSystemMenuOwner = 0x0050A530U;
  constexpr std::uint32_t kGlobalActionState = 0x0050AF68U;
  constexpr std::uint32_t kQuestOwnerBinding = 0x004FC688U;
  constexpr std::uint32_t kQuestDrawBlockerA = 0x00588E3CU;
  constexpr std::uint32_t kQuestDrawBlockerB = 0x004FC6A0U;
  std::uint32_t optionalPanelState = 0U;
  std::uint32_t systemMenuOwner = 0U;
  std::uint32_t globalActionState = 0U;
  std::uint32_t owner = 0U;
  std::uint32_t blockerA = 0U;
  std::uint32_t blockerB = 0U;
  if (!memory.Read32(kOptionalPanelState, &optionalPanelState) ||
      !memory.Read32(kSystemMenuOwner, &systemMenuOwner) ||
      !memory.Read32(kGlobalActionState, &globalActionState) ||
      !memory.Read32(kQuestOwnerBinding, &owner) ||
      !memory.Read32(kQuestDrawBlockerA, &blockerA) ||
      !memory.Read32(kQuestDrawBlockerB, &blockerB)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest draw gates";
    return false;
  }
  if (optionalPanelState == 0U || systemMenuOwner != 0U ||
      globalActionState == 0U || owner == 0U || blockerA != 0U ||
      blockerB != 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  std::uint32_t modelState = 0U;
  std::uint32_t child = 0U;
  if (!memory.Read32(owner + 0x428U, &modelState) ||
      !memory.Read32(owner + 0x0CU, &child)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest draw-model owner";
    return false;
  }
  if ((modelState != 5U && modelState != 6U) || child == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }
  std::uint32_t renderer = 0U;
  if (!memory.Read32(child + 8U, &renderer)) {
    if (error != nullptr)
      *error = "cannot read TopScreen Quest draw-model renderer";
    return false;
  }
  if (renderer == 0U) {
    if (stats != nullptr)
      *stats = result;
    return true;
  }

  const float scale = config.HudScale;
  const float baseX = config.HudLayout == TopScreenHudLayout::Restoration
                          ? 358.0F
                          : 248.0F;
  const float baseY = config.HudLayout == TopScreenHudLayout::Restoration
                          ? 68.0F
                          : 24.0F;
  const float targetX = 400.0F + (baseX - 400.0F) * scale -
                        static_cast<float>(config.HudMarginX) + scale * 8.0F;
  const float targetY = baseY * scale +
                        static_cast<float>(config.HudMarginY) + scale * 10.0F;
  TopScreenModelRegionStats modelStats;
  if (!TransformTopScreenModelRegion(
          memory, renderer,
          {-9999.0F, 9999.0F, -9999.0F, targetX, targetY, scale * 0.7F},
          &modelStats, error)) {
    return false;
  }
  state.QuestDrawModelAdjusted = true;
  result.DrawModelsTransformed =
      modelStats.VerticesTransformed != 0U ? 1U : 0U;
  if (stats != nullptr)
    *stats = result;
  return true;
}

bool CaptureTopScreenQuestBButtonCopies(NativeA32Memory &memory,
                                        const TopScreenUiConfig &config,
                                        std::vector<TopScreenQuestCopiedQuad> *copies,
                                        std::string *error) {
  if (copies == nullptr) {
    if (error != nullptr) *error = "missing Quest B copy output";
    return false;
  }
  copies->clear();
  constexpr std::uint32_t kQuestOwnerBinding = 0x004FC688U;
  std::uint32_t owner = 0U, childCount = 0U;
  if (!memory.Read32(kQuestOwnerBinding, &owner)) {
    if (error != nullptr) *error = "cannot read TopScreen Quest model owner";
    return false;
  }
  if (owner == 0U) return true;
  std::uint32_t mainRenderer = 0U, child = 0U, childRenderer = 0U;
  if (!memory.Read32(owner + 0x10CU, &mainRenderer) || !memory.Read32(owner + 0x224U, &childCount) ||
      !memory.Read32(owner + 0x0CU, &child) || (child != 0U && !memory.Read32(child + 8U, &childRenderer))) {
    if (error != nullptr) *error = "cannot read TopScreen Quest B renderers";
    return false;
  }
  // The B button after the HUD transform: (248, 24) about the top-right corner.
  const float scale = config.HudScale;
  const float centerX = 400.0F + (248.0F - 400.0F) * scale - static_cast<float>(config.HudMarginX);
  const float centerY = 24.0F * scale + static_cast<float>(config.HudMarginY);
  const float reach = 24.0F * scale;
  // Render buffer: count +0, texture extent +4/+8 (floats), positions +0x0C,
  // UVs +0x14, colors +0x18, translations +0x1C.
  const auto copy = [&](std::uint32_t renderer, float textureWidth, float textureHeight,
                        bool numberGlyphs) {
    std::uint32_t count = 0U, widthBits = 0U, heightBits = 0U, positions = 0U, uvs = 0U, colors = 0U,
                  translations = 0U;
    if (renderer == 0U || !memory.Read32(renderer, &count) || !memory.Read32(renderer + 4U, &widthBits) ||
        !memory.Read32(renderer + 8U, &heightBits) || !memory.Read32(renderer + 0x0CU, &positions) ||
        !memory.Read32(renderer + 0x14U, &uvs) || !memory.Read32(renderer + 0x18U, &colors) ||
        !memory.Read32(renderer + 0x1CU, &translations))
      return false;
    if (std::bit_cast<float>(widthBits) != textureWidth || std::bit_cast<float>(heightBits) != textureHeight ||
        count == 0U || count > 8U || positions == 0U || uvs == 0U || colors == 0U || translations == 0U)
      return true;
    for (std::uint32_t index = 0U; index < count; ++index) {
      std::array<TopScreenVec3, 4> quad{};
      std::array<float, 2> translation{};
      std::array<float, 16> color{};
      TopScreenQuestCopiedQuad copied;
      const auto bytes = [](auto &values) {
        return std::span<std::uint8_t>(reinterpret_cast<std::uint8_t *>(values.data()), sizeof(values));
      };
      if (!memory.ReadBytes(positions + index * 0x30U, bytes(quad)) ||
          !memory.ReadBytes(translations + index * 8U, bytes(translation)) ||
          !memory.ReadBytes(uvs + index * 0x20U, bytes(copied.Uvs)) ||
          !memory.ReadBytes(colors + index * 0x40U, bytes(color)))
        return false;
      float quadX = 0.0F, quadY = 0.0F;
      for (std::size_t vertex = 0U; vertex < quad.size(); ++vertex) {
        copied.Positions[vertex] = {quad[vertex].X + translation[0], quad[vertex].Y + translation[1]};
        quadX += copied.Positions[vertex].X * 0.25F;
        quadY += copied.Positions[vertex].Y * 0.25F;
      }
      if (std::abs(quadX - centerX) > reach || std::abs(quadY - centerY) > reach) continue;
      copied.Color = {color[0], color[1], color[2], color[3]};
      copied.NumberGlyphs = numberGlyphs;
      copies->push_back(copied);
      // Hide the native quad; the next submit rebuilds it.
      for (auto &vertex : quad) {
        vertex.X += 500.0F - quadX;
        vertex.Y += 300.0F - quadY;
      }
      if (!memory.WriteBytes(positions + index * 0x30U,
                             std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(quad.data()),
                                                           sizeof(quad))))
        return false;
    }
    return true;
  };
  if (!copy(mainRenderer, 512.0F, 512.0F, false) ||
      (childCount > 0U && !copy(childRenderer, 256.0F, 128.0F, true))) {
    if (error != nullptr) *error = "cannot copy the TopScreen Quest B quads";
    return false;
  }
  return true;
}

bool ApplyTopScreenPauseProjection(NativeA32Memory &memory,
                                   TopScreenPauseProjectionState &state,
                                   std::string *error,
                                   const TopScreenUiConfig *config) {
  constexpr std::uint32_t kProjectionOwnerRoot = 0x004FDA84U;
  constexpr std::array<std::uint32_t, 4> kIndicatorRendererRoots{
      0x004FDA8CU, 0x004FDA90U, 0x004FDA94U, 0x004FDA98U};
  constexpr std::uint32_t kSecondaryLayerState = 0x004FDABCU;
  constexpr std::uint32_t kCompactLayoutFlag = 0x00587966U;
  constexpr std::uint32_t kPauseState = 0x0050AF68U;
  constexpr std::uint32_t kSceneOwner = 0x005043E0U;
  constexpr std::uint32_t kQuestRenderer = 0x004FC688U;

  state.OffsetX = 0.0F;
  state.OffsetY = 0.0F;
  state.NativeQuestGate = false;

  if (!ApplyTopScreenSystemMenuLayerSuppression(memory, nullptr, error)) {
    return false;
  }

  std::uint32_t owner = 0U;
  std::uint32_t pauseState = 0U;
  std::uint32_t sceneOwner = 0U;
  std::uint32_t questRenderer = 0U;
  if (!memory.Read32(kProjectionOwnerRoot, &owner) ||
      !memory.Read32(kPauseState, &pauseState) ||
      !memory.Read32(kSceneOwner, &sceneOwner) ||
      !memory.Read32(kQuestRenderer, &questRenderer)) {
    if (error != nullptr)
      *error = "cannot read pause projection roots";
    return false;
  }

  bool specialMode = false;
  if (sceneOwner != 0U) {
    std::uint8_t mode = 0U;
    std::uint8_t variant = 0U;
    if (!memory.Read8(sceneOwner + 0x100U, &mode) ||
        !memory.Read8(sceneOwner + 0x101U, &variant)) {
      if (error != nullptr)
        *error = "cannot read pause projection scene mode";
      return false;
    }
    if (mode == 3U && variant == 2U) {
      std::uint32_t extension = 0U;
      std::uint32_t nestedOwner = 0U;
      if (!memory.Read32(sceneOwner + 0x20ACU, &extension)) {
        if (error != nullptr)
          *error = "cannot read pause projection extension";
        return false;
      }
      if (extension != 0U &&
          !memory.Read32(extension + 0x12B8U, &nestedOwner)) {
        if (error != nullptr)
          *error = "cannot read pause projection nested owner";
        return false;
      }
      specialMode = nestedOwner != 0U;
    }
  }
  state.NativeQuestGate =
      (specialMode || pauseState == 0x0BU) && questRenderer != 0U;

  // 2.1.1 0x005CDC78..0x005CDCA8 returns before the minimap relocation while
  // any pause page is open (native predicates 0x002FCAD4 Gear, 0x002FCAEC
  // Items, 0x002FCDD4 Map, 0x004392B0 System). The indicator groups belong to
  // the shared map owner 0x004FDA6C, which also draws the dungeon map page's
  // boss and chest markers (0x002F013C); moving them with the HUD minimap
  // took those markers off the page.
  std::array<std::uint32_t, 4> pageStates{};
  constexpr std::array<std::uint32_t, 4> kPageStates{
      0x00504484U, 0x0050672CU, 0x00506CE8U, 0x0050A530U};
  for (std::size_t page = 0; page < kPageStates.size(); ++page) {
    if (!memory.Read32(kPageStates[page], &pageStates[page])) {
      if (error != nullptr)
        *error = "cannot read pause page states";
      return false;
    }
    if (pageStates[page] != 0U)
      return true;
  }

  if (owner == 0U)
    return true;
  std::uint32_t extents = 0U;
  std::uint32_t offset = 0U;
  if (!memory.Read32(owner + 0x0CU, &extents) ||
      !memory.Read32(owner + 0x1CU, &offset)) {
    if (error != nullptr)
      *error = "cannot read pause projection owners";
    return false;
  }
  if (extents == 0U || offset == 0U)
    return true;

  std::uint32_t extentABits = 0U;
  std::uint32_t extentBBits = 0U;
  std::uint32_t offsetXBits = 0U;
  std::uint32_t offsetYBits = 0U;
  std::uint32_t secondaryLayer = 0U;
  std::uint8_t compactLayout = 0U;
  if (!memory.Read32(extents, &extentABits) ||
      !memory.Read32(extents + 0x24U, &extentBBits) ||
      !memory.Read32(offset, &offsetXBits) ||
      !memory.Read32(offset + 4U, &offsetYBits) ||
      !memory.Read32(kSecondaryLayerState, &secondaryLayer) ||
      !memory.Read8(kCompactLayoutFlag, &compactLayout)) {
    if (error != nullptr)
      *error = "cannot read pause projection values";
    return false;
  }
  const float extentA = std::bit_cast<float>(extentABits);
  const float extentB = std::bit_cast<float>(extentBBits);
  const auto recoverOriginal = [](TopScreenPauseProjectionState::TrackedPosition
                                      &tracked,
                                  std::uint32_t address,
                                  std::uint32_t currentBits) {
    if (!tracked.Valid || tracked.Address != address ||
        tracked.LastWrittenBits != currentBits) {
      tracked.Address = address;
      tracked.OriginalBits = currentBits;
      tracked.Valid = true;
    }
    return std::bit_cast<float>(tracked.OriginalBits);
  };
  const float nativeOffsetX =
      recoverOriginal(state.MapX, offset, offsetXBits);
  float nativeOffsetY =
      recoverOriginal(state.MapY, offset + 4U, offsetYBits);
  if (nativeOffsetY >= 1500.0F) {
    nativeOffsetY -= 3000.0F;
    state.MapY.OriginalBits = std::bit_cast<std::uint32_t>(nativeOffsetY);
  }
  const float maximumExtent = std::max(extentA, extentB);
  float projectedOffsetX = nativeOffsetX;
  if (maximumExtent - std::min(extentA, extentB) > 0.0F) {
    const float rightEdge = secondaryLayer == 1U ? 425.0F : 438.0F;
    const float configuredMargin = config != nullptr
                                       ? static_cast<float>(config->HudMarginX)
                                       : 4.0F;
    const float compactInset = compactLayout != 0U ? 36.0F : 0.0F;
    state.OffsetX = rightEdge - (nativeOffsetX + maximumExtent) -
                    compactInset - configuredMargin;
    projectedOffsetX = nativeOffsetX + state.OffsetX;
  }
  float projectedOffsetY = nativeOffsetY;
  if (!state.AlternatePage) {
    projectedOffsetY += 3000.0F;
    state.OffsetY = 3000.0F;
  }
  state.MapX.LastWrittenBits =
      std::bit_cast<std::uint32_t>(projectedOffsetX);
  state.MapY.LastWrittenBits =
      std::bit_cast<std::uint32_t>(projectedOffsetY);
  if (!memory.Write32(offset, state.MapX.LastWrittenBits) ||
      !memory.Write32(offset + 4U, state.MapY.LastWrittenBits)) {
    if (error != nullptr)
      *error = "cannot write pause projection values";
    return false;
  }

  for (std::size_t group = 0; group < kIndicatorRendererRoots.size();
       ++group) {
    std::uint32_t renderer = 0U;
    if (!memory.Read32(kIndicatorRendererRoots[group], &renderer)) {
      if (error != nullptr)
        *error = "cannot read minimap indicator renderer root";
      return false;
    }
    if (renderer == 0U) {
      continue;
    }
    std::uint32_t count = 0U;
    std::uint32_t renderBuffer = 0U;
    if (!memory.Read32(renderer + 0x0CU, &count) ||
        !memory.Read32(renderer + 0x08U, &renderBuffer)) {
      if (error != nullptr)
        *error = "cannot read minimap indicator renderer";
      return false;
    }
    if (count == 0U || count > 64U || renderBuffer == 0U) {
      continue;
    }
    std::uint32_t positions = 0U;
    if (!memory.Read32(renderBuffer + 0x1CU, &positions)) {
      if (error != nullptr)
        *error = "cannot read minimap indicator position stream";
      return false;
    }
    if (positions == 0U) {
      continue;
    }
    for (std::uint32_t index = 0U; index < count; ++index) {
      const std::uint32_t address = positions + index * 8U;
      std::uint32_t currentBits = 0U;
      if (!memory.Read32(address, &currentBits)) {
        if (error != nullptr)
          *error = "cannot read minimap indicator position";
        return false;
      }
      auto &tracked = state.IconX[group * 64U + index];
      const float original = recoverOriginal(tracked, address, currentBits);
      if (original >= 400.0F) {
        continue;
      }
      const float projected =
          state.AlternatePage ? original + state.OffsetX : 400.0F;
      tracked.LastWrittenBits = std::bit_cast<std::uint32_t>(projected);
      if (!memory.Write32(address, tracked.LastWrittenBits)) {
        if (error != nullptr)
          *error = "cannot write minimap indicator position";
        return false;
      }
    }
  }
  return true;
}

bool TransformTopScreenQuestRenderBuffer(
    NativeA32Memory &memory, std::uint32_t renderBufferAddress,
    const TopScreenQuestGeometryContext &context,
    TopScreenQuestRenderBufferStats *stats, std::string *error) {
  constexpr std::uint32_t kNativeQuestQuadCount = 8U;
  constexpr std::size_t kPositionStride = 0x30U;
  std::uint32_t count = 0U;
  std::uint32_t positionsAddress = 0U;
  if (!memory.Read32(renderBufferAddress, &count) ||
      !memory.Read32(renderBufferAddress + 0x10U, &positionsAddress)) {
    if (error != nullptr)
      *error = "cannot read native Quest render-buffer header";
    return false;
  }
  if (count != kNativeQuestQuadCount || positionsAddress == 0U) {
    if (error != nullptr)
      *error = "native Quest render-buffer contract mismatch";
    return false;
  }
  std::array<std::array<TopScreenVec3, 4>, kNativeQuestQuadCount> quads{};
  static_assert(sizeof(quads) == kNativeQuestQuadCount * kPositionStride);
  if (!memory.IsMapped(positionsAddress, sizeof(quads)) ||
      !memory.IsWritable(positionsAddress, sizeof(quads)) ||
      !memory.ReadBytes(
          positionsAddress,
          std::span<std::uint8_t>(
              reinterpret_cast<std::uint8_t *>(quads.data()), sizeof(quads)))) {
    if (error != nullptr)
      *error = "native Quest materialized position stream is unavailable";
    return false;
  }
  TopScreenQuestRenderBufferStats result;
  result.QuadCount = count;
  result.Geometry = TransformTopScreenQuestGeometry(quads, context);
  if (!memory.WriteBytes(
          positionsAddress,
          std::span<const std::uint8_t>(
              reinterpret_cast<const std::uint8_t *>(quads.data()),
              sizeof(quads)))) {
    if (error != nullptr)
      *error = "cannot write native Quest materialized positions";
    return false;
  }
  if (stats != nullptr)
    *stats = result;
  return true;
}

TopScreenPauseTargetPlan
ResolveTopScreenPauseTargetCommand(const TopScreenPauseTargetCommand &command,
                                   bool routeActive) noexcept {
  TopScreenPauseTargetPlan result;
  if (!routeActive || !command.RendererEnabled) {
    return result;
  }
  result.Handled = true;
  result.StoredCommand = command.NativeCommand;
  if (command.NativeCommand == 0x400U) {
    result.BindTopTarget = true;
    result.FramebufferBindingOffset = 0x3CU;
    result.ViewportWidth = 240U;
    result.ViewportHeight = 320U;
  } else if (command.NativeCommand == 0x401U) {
    result.BindTopTarget = true;
    result.FramebufferBindingOffset = 0x38U;
    result.ViewportY = 40U;
    result.ViewportWidth = command.HalfHeightMode ? 240U : 480U;
    result.ViewportHeight = 320U;
  }
  return result;
}

TopScreenPauseTargetPlan ResolveTopScreenPausePageRedrawTargetCommand(
    const TopScreenPauseTargetCommand &command) noexcept {
  auto result = ResolveTopScreenPauseTargetCommand(command, true);
  if (result.BindTopTarget && command.NativeCommand == 0x400U) {
    result.ViewportY = 40U;
    result.ViewportWidth = 480U;
    result.ViewportHeight = 320U;
  }
  return result;
}

void WidenTopScreenPauseTargetForPage(TopScreenPauseTargetPlan &plan,
                                      bool widePage) noexcept {
  if (!widePage || !plan.BindTopTarget || plan.ViewportWidth != 480U ||
      plan.ViewportY != 40U || plan.ViewportHeight != 320U)
    return;
  plan.ViewportY = 0U;
  plan.ViewportHeight = 400U;
}

TopScreenPauseTargetPlan ResolveTopScreenPauseCompositionTargetCommand(
    const TopScreenPauseTargetCommand &command, bool routeActive,
    bool pageRedrawActive, bool systemActive) noexcept {
  if (systemActive)
    return ResolveTopScreenPauseTargetCommand(command, routeActive);
  return pageRedrawActive ? ResolveTopScreenPausePageRedrawTargetCommand(command)
                         : ResolveTopScreenPauseTargetCommand(command, routeActive);
}

} // namespace Oot3dNativeGame
