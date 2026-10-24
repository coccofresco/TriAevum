#include "fast/extension/pbr_material_editor.h"
#include "ship/config/ConfigPersistence.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace Fast::Extension {
namespace {
using namespace ::Oot3d::Renderer;

std::string Hex(uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(value));
    return text;
}

std::filesystem::path Inside(const std::filesystem::path &root, const std::filesystem::path &path) {
    const auto base = std::filesystem::weakly_canonical(root);
    const auto destination = std::filesystem::weakly_canonical(path);
    const auto relative = destination.lexically_relative(base);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
        throw std::runtime_error("Material destination escapes its texture pack");
    return destination;
}

void ValidateImage(const AzaharTextureReplacement &image) {
    if (!image.Width || !image.Height || !image.Rgba8 ||
        uint64_t(image.Width) * image.Height * 4U != image.Rgba8->size())
        throw std::runtime_error("Invalid material image dimensions");
}

std::filesystem::path ImportImage(const PbrMaterialDocument &document, size_t slot,
                                  const AzaharTextureReplacement &image) {
    ValidateImage(image);
    constexpr std::array names{"normal", "orm", "emissive", "height", "base"};
    const auto filename = std::string(names.at(slot)) + "-" + std::to_string(image.Width) + "x" +
                          std::to_string(image.Height) + "-" + Hex(AzaharCityHash64(*image.Rgba8)) +
                          ".png";
    const auto path = Inside(document.Root, document.Root / "material-images" /
                                                Hex(document.NativeHash) / filename);
    WriteAzaharTexturePng(path, image.Width, image.Height, *image.Rgba8);
    return path;
}

nlohmann::json RelativeImage(const std::filesystem::path &root,
                             const std::filesystem::path &descriptor,
                             const std::filesystem::path &path) {
    if (path.empty())
        return nullptr;
    const auto safe = Inside(root, path);
    if (!std::filesystem::is_regular_file(safe))
        throw std::runtime_error("A material image no longer exists: " + path.string());
    const auto relative = safe.lexically_relative(descriptor.parent_path()).generic_u8string();
    return std::string(relative.begin(), relative.end());
}
} // namespace

std::string ValidateMaterialParameters(const Renderer3ds::PicaPbrMaterialParameters &p) {
    const auto valid = [](float value, float minimum, float maximum) {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    };
    for (float value : p.BaseColorFactor)
        if (!valid(value, 0, 1))
            return "Base color factor must be in 0..1";
    if (p.BaseColorFactor[3] != 1)
        return "Material factors cannot change native alpha";
    for (float value : p.EmissiveFactor)
        if (!valid(value, 0, 65504))
            return "Emissive factor must be in 0..65504";
    if (!valid(p.NormalScale, 0, 8))
        return "Normal strength must be in 0..8";
    if (p.NormalYSign != 1 && p.NormalYSign != -1)
        return "Normal convention must be +Y or -Y";
    if (!valid(p.OcclusionStrength, 0, 1) || !valid(p.RoughnessFactor, 0, 1) ||
        !valid(p.MetallicFactor, 0, 1))
        return "ORM factors must be in 0..1";
    if (!valid(p.GlossinessStrength, 0, 2))
        return "Glossiness strength must be in 0..2";
    if (!valid(p.SpecularLevel, 0, 2))
        return "Dielectric reflectance must be in 0..2";
    if (!valid(p.SpecularWeight, 0, 1))
        return "Specular weight must be in 0..1";
    if (!valid(p.ScreenSpaceReflectionWeight, 0, 2))
        return "Screen-space reflection weight must be in 0..2";
    if (!valid(p.SeamStrength, 0, 1) || !valid(p.SeamWidth, 0.001F, 0.25F) ||
        (p.SeamChannels & ~63U) != 0)
        return "UV seam strength must be in 0..1, width in 0.001..0.25, with valid channels";
    if (!valid(p.MinimumRoughness, 0.045F, 1))
        return "Minimum roughness must be in 0.045..1";
    if (!valid(p.HeightScale, 0, 0.1F))
        return "Height depth must be in 0..0.1 UV";
    if (p.TextureSlot != 0xffU && p.TextureSlot > 2)
        return "Texture slot must be automatic, 0, 1 or 2";
    if (p.DebugView > Renderer3ds::PicaPbrDebugView::Roughness)
        return "Invalid material preview view";
    return {};
}

PbrMaterialDocument OpenMaterialDocument(const std::filesystem::path &root, uint64_t hash,
                                         const IndexedAzaharPbrMaterial *existing) {
    PbrMaterialDocument result;
    result.Root = std::filesystem::weakly_canonical(root);
    result.NativeHash = hash;
    result.Descriptor = result.Root / (Hex(hash) + ".material.json");
    result.NewMaterial = existing == nullptr;
    result.Dirty = result.NewMaterial;
    if (!existing)
        return result;
    if (!existing->Error.empty())
        throw std::runtime_error(existing->Error);
    result.Parameters = existing->Parameters;
    if (!existing->ConfigurationPath.empty())
        result.Descriptor = Inside(result.Root, existing->ConfigurationPath);
    for (size_t slot = 0; slot < 5; ++slot) {
        auto &image = result.Images[slot];
        image.Path = slot < 4 ? existing->Textures[slot] : existing->BaseColorTexture;
        if (!image.Path.empty())
            image.Loading = AzaharTexturePackRuntime::Instance().LoadEditorImage(image.Path);
    }
    return result;
}

void LoadMaterialEditorImage(PbrMaterialDocument &document, size_t slot,
                             const std::filesystem::path &path) {
    auto &image = document.Images.at(slot);
    image.Error.clear();
    image.PendingImport = true;
    image.Loading = AzaharTexturePackRuntime::Instance().LoadEditorImage(path);
    document.Dirty = true;
}

void PollMaterialEditorImages(PbrMaterialDocument &document) {
    for (size_t slot = 0; slot < document.Images.size(); ++slot) {
        auto &image = document.Images[slot];
        if (!image.Loading.valid() ||
            image.Loading.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            continue;
        auto result = image.Loading.get();
        if (!result.Image) {
            image.Error = std::move(result.Error);
            continue;
        }
        const bool firstMap = image.Path.empty();
        image.Path = result.Image->SourcePath;
        image.Pixels = std::move(result.Image);
        image.Imported = image.PendingImport;
        image.PendingImport = false;
        image.Error.clear();
        if (image.Imported && firstMap && slot == 1)
            document.Parameters.MetallicFactor = 1.0F;
        if (image.Imported && firstMap && slot == 2 &&
            document.Parameters.EmissiveFactor == std::array<float, 3>{})
            document.Parameters.EmissiveFactor.fill(1.0F);
    }
}

bool MaterialImagesPending(const PbrMaterialDocument &document) {
    return std::any_of(document.Images.begin(), document.Images.end(),
                       [](const auto &image) { return image.Loading.valid(); });
}

std::shared_ptr<const AzaharTextureReplacement>
PackMaterialOrm(const PbrMaterialDocument &document) {
    uint32_t width = 0, height = 0;
    for (size_t channel = 0; channel < 3; ++channel) {
        const auto &source = document.Images[5 + channel];
        if (source.Loading.valid() || !source.Error.empty())
            throw std::runtime_error("An ORM channel is not ready");
        if (document.OrmSourceChannels[channel] < 0 || document.OrmSourceChannels[channel] > 3 ||
            !std::isfinite(document.OrmConstants[channel]) || document.OrmConstants[channel] < 0 ||
            document.OrmConstants[channel] > 1)
            throw std::runtime_error("Invalid ORM source channel or constant");
        if (!source.Pixels)
            continue;
        ValidateImage(*source.Pixels);
        if (!width) {
            width = source.Pixels->Width;
            height = source.Pixels->Height;
        } else if (width != source.Pixels->Width || height != source.Pixels->Height)
            throw std::runtime_error("ORM channel images must have identical dimensions; no "
                                     "implicit resizing is performed");
    }
    if (!width)
        width = height = 1;
    auto pixels = std::make_shared<std::vector<uint8_t>>(size_t(width) * height * 4U, 255U);
    for (size_t channel = 0; channel < 3; ++channel) {
        const auto &source = document.Images[5 + channel].Pixels;
        const auto constant =
            static_cast<uint8_t>(std::lround(document.OrmConstants[channel] * 255.0F));
        for (size_t offset = 0; offset < pixels->size(); offset += 4)
            (*pixels)[offset + channel] =
                source ? (*source->Rgba8)[offset + document.OrmSourceChannels[channel]] : constant;
    }
    auto image = std::make_shared<AzaharTextureReplacement>();
    image->Width = width;
    image->Height = height;
    image->PixelContentHash = AzaharCityHash64(*pixels);
    image->Rgba8 = std::move(pixels);
    return image;
}

bool SaveMaterialDocument(PbrMaterialDocument &document) {
    try {
        const auto invalid = ValidateMaterialParameters(document.Parameters);
        if (!invalid.empty())
            throw std::runtime_error(invalid);
        if (MaterialImagesPending(document))
            throw std::runtime_error("Wait for image imports to complete before saving");
        const auto descriptor = Inside(document.Root, document.Descriptor);
        std::array<std::filesystem::path, 5> paths;
        std::shared_ptr<const AzaharTextureReplacement> packed;
        if (document.SeparateOrm)
            packed = PackMaterialOrm(document);
        for (size_t slot = 0; slot < paths.size(); ++slot) {
            const auto &image = document.Images[slot];
            if (slot == 1 && packed) {
                paths[slot] = ImportImage(document, slot, *packed);
                continue;
            }
            if (!image.Error.empty())
                throw std::runtime_error(image.Error);
            if (image.Imported && image.Pixels)
                paths[slot] = ImportImage(document, slot, *image.Pixels);
            else
                paths[slot] = image.Path;
        }
        const auto texture = [&](size_t slot) {
            return RelativeImage(document.Root, descriptor, paths[slot]);
        };
        const auto &p = document.Parameters;
        nlohmann::json json{
            {"schema", "triaevum.pbr-material"},
            {"version", 1},
            {"shading", "metallic_roughness"},
            {"baseColor",
             {{"source", "bound_texture"}, {"factor", p.BaseColorFactor}, {"texture", texture(4)}}},
            {"normal",
             {{"texture", texture(0)},
              {"scale", p.NormalScale},
              {"convention", p.NormalYSign < 0 ? "-Y" : "+Y"}}},
            {"orm",
             {{"texture", texture(1)},
              {"occlusionStrength", p.OcclusionStrength},
              {"roughnessFactor", p.RoughnessFactor},
              {"metallicFactor", p.MetallicFactor},
              {"glossinessStrength", p.GlossinessStrength},
              {"minimumRoughness", p.MinimumRoughness}}},
            {"emissive", {{"texture", texture(2)}, {"factor", p.EmissiveFactor}}},
            {"height", {{"texture", texture(3)}, {"scale", p.HeightScale}}},
            {"specularLevel", p.SpecularLevel},
            {"specularWeight", p.SpecularWeight},
            {"screenSpaceReflectionWeight", p.ScreenSpaceReflectionWeight}};
        json["seamSoftening"] = {
            {"enabled", p.SeamSoftening}, {"strength", p.SeamStrength}, {"width", p.SeamWidth}};
        constexpr const char* channels[]{"normal", "occlusion", "roughness", "metallic", "emissive", "height"};
        for (uint32_t i = 0; i < 6; ++i)
            json["seamSoftening"]["channels"][channels[i]] = (p.SeamChannels & (1U << i)) != 0;
        if (p.TextureSlot != 0xffU)
            json["textureSlot"] = p.TextureSlot;
        const auto saved = Ship::WriteConfigFileAtomically(descriptor, json.dump(2) + "\n");
        if (!saved.Success)
            throw std::runtime_error(saved.Error);
        for (size_t slot = 0; slot < paths.size(); ++slot) {
            document.Images[slot].Path = paths[slot];
            document.Images[slot].Imported = false;
        }
        if (packed)
            document.Images[1].Pixels = std::move(packed);
        document.Descriptor = descriptor;
        document.Dirty = document.NewMaterial = false;
        document.Error.clear();
        AzaharTexturePackRuntime::Instance().Reload();
        return true;
    } catch (const std::exception &exception) {
        document.Error = exception.what();
        return false;
    }
}

} // namespace Fast::Extension
