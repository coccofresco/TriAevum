// TopScreen mod profile: UI profiles, the verified layout and runtime geometry, control-flow and
// float-load contracts, title logo and camera scalars. Topics: oot3d_top_screen_mod_profile_*.cpp.
#include "oot3d_top_screen_mod_profile_internal.h"

namespace Oot3dNativeGame {

const char *Oot3dUiProfileName(Oot3dUiProfile profile) noexcept {
  switch (profile) {
  case Oot3dUiProfile::Oot3d:
    return "oot3d";
  case Oot3dUiProfile::TopScreen:
    return "topscreen";
  }
  return "oot3d";
}

bool ParseOot3dUiProfile(std::string_view value,
                         Oot3dUiProfile *profile) noexcept {
  if (profile == nullptr) {
    return false;
  }
  if (value == "oot3d") {
    *profile = Oot3dUiProfile::Oot3d;
    return true;
  }
  if (value == "topscreen") {
    *profile = Oot3dUiProfile::TopScreen;
    return true;
  }
  return false;
}

bool ShouldPresentNativeBottomFrontend(
    Oot3dUiProfile profile, bool nativePresentationActive) noexcept {
  return profile == Oot3dUiProfile::Oot3d && nativePresentationActive;
}

bool ShouldSuppressTopScreenFrontendBackdrop(
    Oot3dUiProfile profile, bool nativePresentationActive) noexcept {
  return profile == Oot3dUiProfile::TopScreen && nativePresentationActive;
}

std::span<const TopScreenLayoutPatch>
TopScreenVerifiedLayoutPatches() noexcept {
  return kVerifiedLayoutPatches;
}

bool ApplyTopScreenVerifiedLayout(NativeA32Memory &memory,
                                  TopScreenLayoutApplyStats *stats,
                                  std::string *error) {
  TopScreenLayoutApplyStats result;

  // Complete validation precedes the first write. Already-applied values are
  // accepted so savestate resets and host reinitialization are idempotent.
  for (const auto &patch : kVerifiedLayoutPatches) {
    ++result.ContractsChecked;
    for (std::uint8_t word = 0; word < patch.WordCount; ++word) {
      std::uint32_t actual = 0;
      const std::uint32_t address =
          patch.Address + static_cast<std::uint32_t>(word) * 4U;
      if (!memory.Read32(address, &actual)) {
        std::ostringstream message;
        message << "cannot read TopScreen layout contract " << patch.Semantic
                << " at 0x" << std::hex << address;
        SetError(error, message.str());
        return false;
      }
      ++result.WordsChecked;
      if (actual != patch.Expected[word] && actual != patch.Replacement[word]) {
        std::ostringstream message;
        message << "TopScreen layout contract mismatch for " << patch.Semantic
                << " at 0x" << std::hex << address << ": expected 0x"
                << patch.Expected[word] << " but found 0x" << actual;
        SetError(error, message.str());
        return false;
      }
      result.WordsChanged += actual != patch.Replacement[word] ? 1U : 0U;
    }
  }

  for (const auto &patch : kVerifiedLayoutPatches) {
    for (std::uint8_t word = 0; word < patch.WordCount; ++word) {
      const std::uint32_t address =
          patch.Address + static_cast<std::uint32_t>(word) * 4U;
      if (!memory.WriteHost<std::uint32_t>(address, patch.Replacement[word])) {
        std::ostringstream message;
        message << "cannot write TopScreen layout contract " << patch.Semantic
                << " at 0x" << std::hex << address;
        SetError(error, message.str());
        return false;
      }
      ++result.WordsWritten;
    }
  }
  if (stats != nullptr) {
    *stats = result;
  }
  return true;
}

std::span<const TopScreenRuntimeQuadStreamContract>
TopScreenVerifiedRuntimeQuadStreams() noexcept {
  return kVerifiedRuntimeQuadStreams;
}

bool RebindTopScreenVerifiedRuntimeGeometry(
    NativeA32Memory &memory, TopScreenRuntimeGeometryApplyStats *stats,
    std::string *error) {
  TopScreenRuntimeGeometryApplyStats result;
  std::vector<StagedTopScreenGeometryWord> writes;

  for (const auto &contract : kVerifiedRuntimeQuadStreams) {
    ++result.ContractsChecked;

    std::uint32_t renderBuffer = 0;
    if (!ReadTopScreenGeometryWord(
            memory,
            contract.OwnerAddress + contract.RenderBufferPointerOffset,
            &renderBuffer)) {
      SetError(error, "TopScreen runtime geometry owner is unmapped: " +
                          std::string(contract.Semantic));
      return false;
    }
    if (renderBuffer == 0U) {
      ++result.StreamsNotInitialized;
      continue;
    }

    std::uint32_t nativeQuadCount = 0;
    std::uint32_t positions = 0;
    std::uint32_t texcoords = 0;
    float textureWidth = 0.0F;
    float textureHeight = 0.0F;
    if (!ReadTopScreenGeometryWord(memory, renderBuffer, &nativeQuadCount) ||
        nativeQuadCount != contract.QuadCount ||
        !ReadTopScreenGeometryFloat(memory, renderBuffer + 4U,
                                    &textureWidth) ||
        !ReadTopScreenGeometryFloat(memory, renderBuffer + 8U,
                                    &textureHeight) ||
        textureWidth <= 0.0F || textureHeight <= 0.0F ||
        !ReadTopScreenGeometryWord(memory, renderBuffer + 0x0CU,
                                   &positions) ||
        !ReadTopScreenGeometryWord(memory, renderBuffer + 0x14U,
                                   &texcoords) ||
        positions == 0U || texcoords == 0U ||
        !memory.IsMapped(positions,
                         static_cast<std::size_t>(contract.QuadCount) * 48U) ||
        !memory.IsMapped(texcoords,
                         static_cast<std::size_t>(contract.QuadCount) * 32U)) {
      SetError(error, "TopScreen runtime geometry binding is incompatible: " +
                          std::string(contract.Semantic));
      return false;
    }

    for (std::uint32_t quad = 0; quad < contract.QuadCount; ++quad) {
      const std::uint32_t sourceOffset = quad * 8U;
      float x = 0.0F;
      float y = 0.0F;
      float width = 0.0F;
      float height = 0.0F;
      float textureX = 0.0F;
      float textureY = 0.0F;
      float textureExtentX = 0.0F;
      float textureExtentY = 0.0F;
      if (!ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.RectOriginOffset +
                          sourceOffset,
              &x) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.RectOriginOffset +
                          sourceOffset + 4U,
              &y) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.RectExtentOffset +
                          sourceOffset,
              &width) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.RectExtentOffset +
                          sourceOffset + 4U,
              &height) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.TextureOriginOffset +
                          sourceOffset,
              &textureX) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.TextureOriginOffset +
                          sourceOffset + 4U,
              &textureY) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.TextureExtentOffset +
                          sourceOffset,
              &textureExtentX) ||
          !ReadTopScreenGeometryFloat(
              memory, contract.OwnerAddress + contract.TextureExtentOffset +
                          sourceOffset + 4U,
              &textureExtentY)) {
        SetError(error,
                 "TopScreen runtime geometry source is incompatible: " +
                     std::string(contract.Semantic));
        return false;
      }

      const std::uint32_t positionBase = positions + quad * 48U;
      StageTopScreenGeometryFloat(writes, positionBase, x);
      StageTopScreenGeometryFloat(writes, positionBase + 4U, y);
      StageTopScreenGeometryFloat(writes, positionBase + 12U, x + width);
      StageTopScreenGeometryFloat(writes, positionBase + 16U, y);
      StageTopScreenGeometryFloat(writes, positionBase + 24U, x);
      StageTopScreenGeometryFloat(writes, positionBase + 28U, y + height);
      StageTopScreenGeometryFloat(writes, positionBase + 36U, x + width);
      StageTopScreenGeometryFloat(writes, positionBase + 40U, y + height);

      const float u0 = textureX / textureWidth;
      const float u1 = (textureX + textureExtentX) / textureWidth;
      const float v0 = (textureHeight - textureY) / textureHeight;
      const float v1 =
          (textureHeight - (textureY + textureExtentY)) / textureHeight;
      const std::uint32_t texcoordBase = texcoords + quad * 32U;
      StageTopScreenGeometryFloat(writes, texcoordBase, u0);
      StageTopScreenGeometryFloat(writes, texcoordBase + 4U, v0);
      StageTopScreenGeometryFloat(writes, texcoordBase + 8U, u1);
      StageTopScreenGeometryFloat(writes, texcoordBase + 12U, v0);
      StageTopScreenGeometryFloat(writes, texcoordBase + 16U, u0);
      StageTopScreenGeometryFloat(writes, texcoordBase + 20U, v1);
      StageTopScreenGeometryFloat(writes, texcoordBase + 24U, u1);
      StageTopScreenGeometryFloat(writes, texcoordBase + 28U, v1);
    }

    ++result.StreamsRebound;
    result.QuadsRebound += contract.QuadCount;
  }

  // 0x004673BC/0x00467688 change both Gear filters to GL_NEAREST. Restored
  // stages already contain the converted values from 0x00348A64: mag at
  // +0x13C (linear 1), min at +0x140 (linear 3), nearest 0 for both.
  for (const auto owner : {0x0050446CU, 0x005044C4U, 0x005044C8U}) {
    std::uint32_t stage = 0U;
    if (!memory.Read32(owner, &stage)) {
      SetError(error, "TopScreen Gear texture-stage owner is unmapped");
      return false;
    }
    if (stage == 0U) continue;
    std::uint32_t mag = 0U, min = 0U;
    if ((stage & 3U) != 0U || !memory.IsMapped(stage, 0x1B8U) ||
        !memory.Read32(stage + 0x13CU, &mag) ||
        !memory.Read32(stage + 0x140U, &min) ||
        (mag != 0U && mag != 1U) || (min != 0U && min != 3U)) {
      SetError(error, "TopScreen Gear texture-stage filters are incompatible");
      return false;
    }
    writes.push_back({stage + 0x13CU, 0U});
    writes.push_back({stage + 0x140U, 0U});
  }

  for (const auto &write : writes) {
    if (!memory.WriteHost<std::uint32_t>(write.Address, write.Value)) {
      SetError(error, "TopScreen runtime geometry write failed");
      return false;
    }
  }
  result.WordsWritten = static_cast<std::uint32_t>(writes.size());
  if (stats != nullptr) {
    *stats = result;
  }
  return true;
}

std::span<const TopScreenControlFlowContract>
TopScreenVerifiedControlFlowContracts() noexcept {
  return kVerifiedControlFlowContracts;
}

bool ResolveTopScreenControlFlow(std::uint32_t entry,
                                 std::uint32_t *target) noexcept {
  const auto contract =
      std::find_if(kVerifiedControlFlowContracts.begin(),
                   kVerifiedControlFlowContracts.end(),
                   [entry](const TopScreenControlFlowContract &candidate) {
                     return candidate.Entry == entry;
                   });
  if (contract == kVerifiedControlFlowContracts.end()) {
    return false;
  }
  if (target != nullptr) {
    *target = contract->Target;
  }
  return true;
}

std::span<const TopScreenFloatLoadContract>
TopScreenVerifiedFloatLoadContracts() noexcept {
  return kVerifiedFloatLoadContracts;
}

bool ResolveTopScreenFloatLoad(std::uint32_t entry, std::uint8_t *vfpLane,
                               std::uint32_t *valueBits) noexcept {
  const auto contract = std::find_if(
      kVerifiedFloatLoadContracts.begin(), kVerifiedFloatLoadContracts.end(),
      [entry](const TopScreenFloatLoadContract &candidate) {
        return candidate.Entry == entry;
      });
  if (contract == kVerifiedFloatLoadContracts.end()) {
    return false;
  }
  if (vfpLane != nullptr) {
    *vfpLane = contract->VfpLane;
  }
  if (valueBits != nullptr) {
    *valueBits = contract->ValueBits;
  }
  return true;
}

bool ApplyTopScreenTitleLogoFadeHold(NativeA32Memory &memory,
                                     std::uint32_t actorAddress,
                                     std::uint32_t actorStateAddress,
                                     std::uint32_t clampedAlphaBits,
                                     TopScreenTitleLogoFadeHoldResult *result,
                                     std::string *error) {
  constexpr std::uint32_t kAnimationOwnerLiteral = 0x001DAD08U;
  constexpr std::uint32_t kTitleAlpha = 0x1D0U;
  constexpr std::uint32_t kEffectAlpha = 0x1DCU;
  constexpr std::uint32_t kFadeSubstate = 0xC4U;
  constexpr std::uint32_t kFadeTimer = 0xC6U;

  if (result == nullptr) {
    if (error != nullptr)
      *error = "missing title-logo fade-hold output";
    return false;
  }

  std::uint32_t animationOwnerPointer = 0U;
  std::uint32_t animationOwner = 0U;
  if (!memory.Read32(kAnimationOwnerLiteral, &animationOwnerPointer) ||
      animationOwnerPointer == 0U ||
      !memory.Read32(animationOwnerPointer, &animationOwner) ||
      !memory.IsWritable(actorAddress + kTitleAlpha, sizeof(std::uint32_t)) ||
      !memory.IsWritable(actorAddress + kEffectAlpha, sizeof(std::uint32_t)) ||
      !memory.IsWritable(actorStateAddress + kFadeSubstate,
                         sizeof(std::uint16_t)) ||
      !memory.IsWritable(actorStateAddress + kFadeTimer,
                         sizeof(std::uint16_t))) {
    if (error != nullptr)
      *error = "cannot preflight title-logo fade-hold state";
    return false;
  }

  if (!memory.Write32(actorAddress + kTitleAlpha, clampedAlphaBits) ||
      !memory.Write32(actorAddress + kEffectAlpha, clampedAlphaBits) ||
      !memory.Write16(actorStateAddress + kFadeSubstate, 3U) ||
      !memory.Write16(actorStateAddress + kFadeTimer, 1U)) {
    if (error != nullptr)
      *error = "cannot apply title-logo fade-hold state";
    return false;
  }

  *result = {animationOwner, 1U, 0x001DAFF8U};
  return true;
}

TopScreenCameraNormal1Scalar
ResolveTopScreenCameraNormal1Scalar(std::uint32_t nativeValueBits,
                                    std::uint8_t zoomPercent) noexcept {
  TopScreenCameraNormal1Scalar result{nativeValueBits, nativeValueBits};
  if (zoomPercent != 100U) {
    result.ContinuedValueBits = std::bit_cast<std::uint32_t>(
        std::bit_cast<float>(nativeValueBits) *
        (static_cast<float>(zoomPercent) * 0.01F));
  }
  return result;
}

} // namespace Oot3dNativeGame
