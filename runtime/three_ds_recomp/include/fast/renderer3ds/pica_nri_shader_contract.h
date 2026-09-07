#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace Fast::Renderer3ds {

enum class PicaNriDescriptorKind : uint8_t {
    ConstantBuffer,
    Texture,
    StorageTexture,
    Sampler,
};

enum class PicaNriShaderStage : uint8_t {
    Vertex,
    Fragment,
};

struct PicaNriDescriptorBinding {
    uint32_t Binding = 0;
    PicaNriDescriptorKind Kind = PicaNriDescriptorKind::ConstantBuffer;
    PicaNriShaderStage Stage = PicaNriShaderStage::Vertex;

    auto operator<=>(const PicaNriDescriptorBinding&) const = default;
};

struct PicaNriFragmentShaderVariant {
    std::string Source;
    std::string Error;
    bool Applied = false;
};

struct PicaNriFragmentShaderExtensions {
    // Optional combined sampler emitted by an extension shader hook. The
    // common renderer assigns its image/sampler pair to bindings 10 and 11.
    std::string_view DirectionalShadowSampler;
};

// This binding contract deliberately matches the original PICA bindings for
// buffers, textures and storage images. Bindings 7-9 split the native combined
// samplers for NRI. Bindings 10-12 are the stable, optional directional-shadow
// lighting extension: history texture, split sampler and receiver state.
// Bindings 13-14 are the native fragment-lighting LUT image and sampler.
constexpr std::array<PicaNriDescriptorBinding, 15>
    kPicaNriDescriptorBindings{{
        {0U, PicaNriDescriptorKind::ConstantBuffer,
         PicaNriShaderStage::Vertex},
        {1U, PicaNriDescriptorKind::Texture,
         PicaNriShaderStage::Fragment},
        {2U, PicaNriDescriptorKind::Texture,
         PicaNriShaderStage::Fragment},
        {3U, PicaNriDescriptorKind::Texture,
         PicaNriShaderStage::Fragment},
        {4U, PicaNriDescriptorKind::ConstantBuffer,
         PicaNriShaderStage::Fragment},
        {5U, PicaNriDescriptorKind::StorageTexture,
         PicaNriShaderStage::Fragment},
        {6U, PicaNriDescriptorKind::ConstantBuffer,
         PicaNriShaderStage::Vertex},
        {7U, PicaNriDescriptorKind::Sampler,
         PicaNriShaderStage::Fragment},
        {8U, PicaNriDescriptorKind::Sampler,
         PicaNriShaderStage::Fragment},
        {9U, PicaNriDescriptorKind::Sampler,
         PicaNriShaderStage::Fragment},
        {10U, PicaNriDescriptorKind::Texture,
         PicaNriShaderStage::Fragment},
        {11U, PicaNriDescriptorKind::Sampler,
         PicaNriShaderStage::Fragment},
        {12U, PicaNriDescriptorKind::ConstantBuffer,
         PicaNriShaderStage::Fragment},
        {13U, PicaNriDescriptorKind::Texture,
         PicaNriShaderStage::Fragment},
        {14U, PicaNriDescriptorKind::Sampler,
         PicaNriShaderStage::Fragment},
    }};

bool ValidatePicaNriDescriptorContract();

// Converts generated GLSL combined samplers to explicit texture and sampler
// objects while preserving every existing texture()/texelFetch()/textureSize()
// call through local macros. The three native samplers are mandatory; the
// directional-shadow and native lighting-LUT samplers are converted when
// their respective shader paths declare them.
// Unsupported shader declarations produce a non-applied result so the backend
// can retain its Vulkan fallback.
PicaNriFragmentShaderVariant BuildPicaNriFragmentShaderVariant(
    std::string_view source,
    const PicaNriFragmentShaderExtensions& extensions = {});

} // namespace Fast::Renderer3ds
