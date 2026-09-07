#pragma once

#include "fast/renderer3ds/pica_nri_shader_contract.h"

namespace Fast::Oot3d {

using Renderer3ds::kPicaNriDescriptorBindings;
using Renderer3ds::PicaNriDescriptorBinding;
using Renderer3ds::PicaNriDescriptorKind;
using Renderer3ds::PicaNriFragmentShaderVariant;
using Renderer3ds::PicaNriShaderStage;
using Renderer3ds::ValidatePicaNriDescriptorContract;

inline PicaNriFragmentShaderVariant BuildPicaNriFragmentShaderVariant(
    std::string_view source) {
    return Renderer3ds::BuildPicaNriFragmentShaderVariant(
        source, { "oot3d_directional_shadow_map" });
}

} // namespace Fast::Oot3d
