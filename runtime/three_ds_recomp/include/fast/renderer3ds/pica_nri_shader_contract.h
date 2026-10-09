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
    StructuredBuffer,
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
    // Optional point-shadow atlas sampler: image/sampler at bindings 27 and 28.
    std::string_view PointShadowSampler;
    // Optional contact-shadow visibility sampler: bindings 29 and 30.
    std::string_view ContactShadowSampler;
};

// This binding contract deliberately matches the original PICA bindings for
// buffers, textures and storage images. Bindings 7-9 split the native combined
// samplers for NRI. Bindings 10-12 are the stable, optional directional-shadow
// lighting extension: history texture, split sampler and receiver state.
// Bindings 13-14 are the native fragment-lighting LUT image and sampler.
// Binding 15 is an optional surface-style constant buffer, not native PICA state.
// Binding 16 contains the selected native lights and unlit material inputs.
// Bindings 17-25 carry opt-in PBR parameters and normal/ORM/emissive/height maps.
// Binding 26 is read-only, geometry-owned UV-seam metadata.
// Bindings 27-28 are the optional point-shadow atlas image and sampler.
// Bindings 29-30 are the optional contact-shadow visibility and sampler.
constexpr std::array<PicaNriDescriptorBinding, 31>
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
        {15U, PicaNriDescriptorKind::ConstantBuffer,
         PicaNriShaderStage::Fragment},
        {16U, PicaNriDescriptorKind::ConstantBuffer,
         PicaNriShaderStage::Fragment},
        {17U, PicaNriDescriptorKind::ConstantBuffer,
         PicaNriShaderStage::Fragment},
        {18U, PicaNriDescriptorKind::Texture,
         PicaNriShaderStage::Fragment},
        {19U, PicaNriDescriptorKind::Texture,
         PicaNriShaderStage::Fragment},
        {20U, PicaNriDescriptorKind::Texture,
         PicaNriShaderStage::Fragment},
        {21U, PicaNriDescriptorKind::Sampler,
         PicaNriShaderStage::Fragment},
        {22U, PicaNriDescriptorKind::Sampler,
         PicaNriShaderStage::Fragment},
        {23U, PicaNriDescriptorKind::Sampler,
         PicaNriShaderStage::Fragment},
        {24U, PicaNriDescriptorKind::Texture,
         PicaNriShaderStage::Fragment},
        {25U, PicaNriDescriptorKind::Sampler,
         PicaNriShaderStage::Fragment},
        {26U, PicaNriDescriptorKind::StructuredBuffer,
         PicaNriShaderStage::Fragment},
        {27U, PicaNriDescriptorKind::Texture,
         PicaNriShaderStage::Fragment},
        {28U, PicaNriDescriptorKind::Sampler,
         PicaNriShaderStage::Fragment},
        {29U, PicaNriDescriptorKind::Texture,
         PicaNriShaderStage::Fragment},
        {30U, PicaNriDescriptorKind::Sampler,
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
