#include "fast/oot3d/graphics_settings_persistence.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace Fast::Oot3d {
namespace {

using Json = nlohmann::json;

template <typename Enum>
using EnumNames = std::initializer_list<std::pair<Enum, std::string_view>>;

template <typename Enum>
std::string_view EnumName(Enum value, EnumNames<Enum> names) {
    for (const auto& [candidate, name] : names) {
        if (candidate == value) {
            return name;
        }
    }
    return names.begin()->second;
}

template <typename Enum>
bool ParseEnum(std::string_view value, EnumNames<Enum> names, Enum& output) {
    for (const auto& [candidate, name] : names) {
        if (name == value) {
            output = candidate;
            return true;
        }
    }
    return false;
}

constexpr EnumNames<GraphicsPreset> kPresetNames{
    {GraphicsPreset::Authentic, "Authentic"},
    {GraphicsPreset::Enhanced, "Enhanced"},
    {GraphicsPreset::Toon, "Toon"},
    {GraphicsPreset::Custom, "Custom"},
};
constexpr EnumNames<WindowMode> kWindowNames{
    {WindowMode::Windowed, "Windowed"},
    {WindowMode::Borderless, "Borderless"},
    {WindowMode::ExclusiveFullscreen, "ExclusiveFullscreen"},
};
constexpr EnumNames<AntiAliasingMode> kAaNames{
    {AntiAliasingMode::Off, "Off"},
    {AntiAliasingMode::Fxaa, "FXAA"},
    {AntiAliasingMode::Smaa1x, "SMAA1x"},
    {AntiAliasingMode::Msaa, "MSAA"},
    {AntiAliasingMode::Taa, "TAA"},
    {AntiAliasingMode::Upscaler, "Upscaler"},
};
constexpr EnumNames<FrameRateMode> kFrameRateNames{
    {FrameRateMode::Original30, "Original30"},
    {FrameRateMode::Interpolated2x, "Interpolated2x"},
    {FrameRateMode::Interpolated3x, "Interpolated3x"},
    {FrameRateMode::Uncapped, "Uncapped"},
    {FrameRateMode::Interpolated2x, "Fixed60"},
};
constexpr EnumNames<DirectionalShadowMode> kDirectionalShadowNames{
    {DirectionalShadowMode::Off, "Off"},
    {DirectionalShadowMode::SingleCascade, "SingleCascade"},
};
constexpr EnumNames<AmbientOcclusionMode> kAoNames{
    {AmbientOcclusionMode::Off, "Off"},
    {AmbientOcclusionMode::Cacao, "CACAO"},
};
constexpr EnumNames<ReflectionMode> kReflectionNames{
    {ReflectionMode::Off, "Off"},
    {ReflectionMode::HiZ, "HiZ"},
    {ReflectionMode::FidelityFxSssr, "FidelityFXSSSR"},
};
constexpr EnumNames<ToonMode> kToonNames{
    {ToonMode::Off, "Off"},
    {ToonMode::PostProcessPreview, "PostProcessPreview"},
    {ToonMode::PicaMaterial, "PicaMaterial"},
};
constexpr EnumNames<GrassQuality> kGrassQualityNames{
    {GrassQuality::Off, "Off"},
    {GrassQuality::Low, "Low"},
    {GrassQuality::Medium, "Medium"},
    {GrassQuality::High, "High"},
    {GrassQuality::Custom, "Custom"},
};
constexpr EnumNames<GrassSampleChannel> kGrassChannelNames{
    {GrassSampleChannel::Red, "Red"},
    {GrassSampleChannel::Green, "Green"},
    {GrassSampleChannel::Blue, "Blue"},
    {GrassSampleChannel::Alpha, "Alpha"},
    {GrassSampleChannel::Luminance, "Luminance"},
};
constexpr EnumNames<GrassWrapOverride> kGrassWrapNames{
    {GrassWrapOverride::Material, "Material"},
    {GrassWrapOverride::Clamp, "Clamp"},
    {GrassWrapOverride::Repeat, "Repeat"},
    {GrassWrapOverride::Mirror, "Mirror"},
};
constexpr EnumNames<ReflectionMaterialProfile> kMaterialProfileNames{
    {ReflectionMaterialProfile::Water, "Water"},
    {ReflectionMaterialProfile::Metal, "Metal"},
    {ReflectionMaterialProfile::Polished, "Polished"},
    {ReflectionMaterialProfile::Custom, "Custom"},
};
constexpr EnumNames<UpscalerProvider> kUpscalerProviderNames{
    {UpscalerProvider::Nis, "NIS"},
    {UpscalerProvider::Fsr, "FSR"},
    {UpscalerProvider::Xess, "XeSS"},
    {UpscalerProvider::Dlss, "DLSS"},
};
constexpr EnumNames<UpscalerQuality> kUpscalerQualityNames{
    {UpscalerQuality::Native, "Native"},
    {UpscalerQuality::UltraQuality, "UltraQuality"},
    {UpscalerQuality::Quality, "Quality"},
    {UpscalerQuality::Balanced, "Balanced"},
    {UpscalerQuality::Performance, "Performance"},
    {UpscalerQuality::UltraPerformance, "UltraPerformance"},
};

void Correct(GraphicsSettingsLoadResult& result, std::string field,
             std::string message) {
    result.Issues.push_back(
        {std::move(field), std::move(message), true});
    result.NeedsRewrite = true;
}

const Json* FindMember(const Json& parent, std::string_view name) {
    if (!parent.is_object()) {
        return nullptr;
    }
    const auto found = parent.find(std::string(name));
    return found == parent.end() ? nullptr : &*found;
}

const Json* RequireObject(const Json& parent, std::string_view name,
                          std::string_view path,
                          GraphicsSettingsLoadResult& result) {
    const Json* value = FindMember(parent, name);
    if (value != nullptr && value->is_object()) {
        return value;
    }
    Correct(result, std::string(path),
            value == nullptr ? "missing object restored from defaults"
                             : "non-object value restored from defaults");
    return nullptr;
}

template <typename T>
bool ReadScalar(const Json& parent, std::string_view name, T& output,
                std::string_view path,
                GraphicsSettingsLoadResult& result) {
    const Json* value = FindMember(parent, name);
    if (value == nullptr) {
        Correct(result, std::string(path),
                "missing value restored from defaults");
        return false;
    }
    try {
        if constexpr (std::is_same_v<T, bool>) {
            if (!value->is_boolean()) {
                throw std::domain_error("not boolean");
            }
            output = value->get<bool>();
        } else if constexpr (std::is_floating_point_v<T>) {
            if (!value->is_number()) {
                throw std::domain_error("not numeric");
            }
            const double parsed = value->get<double>();
            if (!std::isfinite(parsed) ||
                parsed > static_cast<double>(
                             std::numeric_limits<T>::max()) ||
                parsed < -static_cast<double>(
                             std::numeric_limits<T>::max())) {
                throw std::out_of_range("non-finite or out of range");
            }
            output = static_cast<T>(parsed);
        } else if constexpr (std::is_integral_v<T>) {
            if (!value->is_number_integer() &&
                !value->is_number_unsigned()) {
                throw std::domain_error("not integer");
            }
            if constexpr (std::is_unsigned_v<T>) {
                if (value->is_number_integer() &&
                    value->get<int64_t>() < 0) {
                    throw std::out_of_range("negative unsigned value");
                }
                const uint64_t parsed = value->get<uint64_t>();
                if (parsed > std::numeric_limits<T>::max()) {
                    throw std::out_of_range("integer out of range");
                }
                output = static_cast<T>(parsed);
            } else {
                const int64_t parsed = value->get<int64_t>();
                if (parsed < std::numeric_limits<T>::min() ||
                    parsed > std::numeric_limits<T>::max()) {
                    throw std::out_of_range("integer out of range");
                }
                output = static_cast<T>(parsed);
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (!value->is_string()) {
                throw std::domain_error("not string");
            }
            output = value->get<std::string>();
        }
        return true;
    } catch (const std::exception&) {
        Correct(result, std::string(path),
                "invalid value restored from defaults");
        return false;
    }
}

template <typename Enum>
bool ReadEnum(const Json& parent, std::string_view name, Enum& output,
              EnumNames<Enum> names, std::string_view path,
              GraphicsSettingsLoadResult& result) {
    std::string serialized;
    if (!ReadScalar(parent, name, serialized, path, result)) {
        return false;
    }
    if (!ParseEnum(serialized, names, output)) {
        Correct(result, std::string(path),
                "unknown enum name restored from defaults");
        return false;
    }
    return true;
}

template <size_t N>
bool ReadFloatArray(const Json& parent, std::string_view name,
                    std::array<float, N>& output,
                    std::string_view path,
                    GraphicsSettingsLoadResult& result) {
    const Json* value = FindMember(parent, name);
    if (value == nullptr || !value->is_array() || value->size() != N) {
        Correct(result, std::string(path),
                "invalid color array restored from defaults");
        return false;
    }
    auto parsed = output;
    for (size_t index = 0; index < N; ++index) {
        if (!(*value)[index].is_number()) {
            Correct(result, std::string(path),
                    "invalid color component restored from defaults");
            return false;
        }
        const double component = (*value)[index].get<double>();
        if (!std::isfinite(component) ||
            component > std::numeric_limits<float>::max() ||
            component < -std::numeric_limits<float>::max()) {
            Correct(result, std::string(path),
                    "invalid color component restored from defaults");
            return false;
        }
        parsed[index] = static_cast<float>(component);
    }
    output = parsed;
    return true;
}

std::string SerializeHash(uint64_t hash) {
    std::array<char, 17> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%016llx",
                  static_cast<unsigned long long>(hash));
    return buffer.data();
}

bool ReadHash(const Json& parent, std::string_view name, uint64_t& output,
              std::string_view path,
              GraphicsSettingsLoadResult& result) {
    const Json* value = FindMember(parent, name);
    if (value == nullptr) {
        Correct(result, std::string(path),
                "missing hash restored from defaults");
        return false;
    }
    if (value->is_number_unsigned() ||
        (value->is_number_integer() &&
         value->get<int64_t>() >= 0)) {
        output = value->get<uint64_t>();
        result.NeedsRewrite = true;
        return true;
    }
    if (!value->is_string()) {
        Correct(result, std::string(path),
                "invalid hash restored from defaults");
        return false;
    }
    std::string serialized = value->get<std::string>();
    std::string_view digits(serialized);
    if (digits.starts_with("0x") || digits.starts_with("0X")) {
        digits.remove_prefix(2);
    }
    uint64_t parsed = 0;
    const auto conversion = std::from_chars(
        digits.data(), digits.data() + digits.size(), parsed, 16);
    if (digits.empty() || digits.size() > 16U ||
        conversion.ec != std::errc{} ||
        conversion.ptr != digits.data() + digits.size()) {
        Correct(result, std::string(path),
                "invalid hexadecimal hash restored from defaults");
        return false;
    }
    output = parsed;
    return true;
}

Json SerializeGrassRule(const GrassPlacementRule& rule) {
    return {
        {"RuleId", rule.RuleId},
        {"Target",
         {
             {"AssetName", rule.Target.AssetName},
             {"Rgba8Hash", SerializeHash(rule.Target.Rgba8Hash)},
             {"Width", rule.Target.Width},
             {"Height", rule.Target.Height},
             {"MapperSlotMask", rule.Target.MapperSlotMask},
         }},
        {"Channel",
         EnumName(rule.Channel, kGrassChannelNames)},
        {"Wrap", EnumName(rule.Wrap, kGrassWrapNames)},
        {"Invert", rule.Invert},
        {"InputBlack", rule.InputBlack},
        {"InputWhite", rule.InputWhite},
        {"ResponseExponent", rule.ResponseExponent},
        {"OutputBlack", rule.OutputBlack},
        {"OutputWhite", rule.OutputWhite},
        {"MaximumSlopeDegrees", rule.MaximumSlopeDegrees},
        {"NormalOffset", rule.NormalOffset},
    };
}

Json SerializeGrassGeneration(
    const GrassGenerationSettings& generation) {
    return {
        {"InstancesPerSquareMeter",
         generation.InstancesPerSquareMeter},
        {"MinimumSpacing", generation.MinimumSpacing},
        {"Seed", generation.Seed},
        {"IndividualRandomness",
         generation.IndividualRandomness},
        {"ClusterStrength", generation.ClusterStrength},
        {"ClusterScale", generation.ClusterScale},
        {"ClusterCoverage", generation.ClusterCoverage},
        {"BladeHeightMin", generation.BladeHeightMin},
        {"BladeHeightMax", generation.BladeHeightMax},
        {"BladeWidthMin", generation.BladeWidthMin},
        {"BladeWidthMax", generation.BladeWidthMax},
    };
}

Json SerializeInteractiveGrassSettings(
    const InteractiveGrassSettings& grass) {
    Json rules = Json::array();
    for (const auto& rule : grass.Rules) {
        rules.push_back(SerializeGrassRule(rule));
    }
    return {
        {"Quality", EnumName(grass.Quality, kGrassQualityNames)},
        {"Budget",
         {
             {"MaxInstancesPerRoom", grass.MaxInstancesPerRoom},
             {"DrawDistance", grass.DrawDistance},
         }},
        {"Performance",
         {
             {"FrustumCulling", grass.FrustumCulling},
             {"CullingClusterSize", grass.CullingClusterSize},
             {"LodStartFraction", grass.LodStartFraction},
             {"LodReferenceDistance", grass.LodReferenceDistance},
             {"LodEndFraction", grass.LodEndFraction},
             {"SegmentLodStartDistance", grass.SegmentLodStartDistance},
             {"SegmentLodEndDistance", grass.SegmentLodEndDistance},
             {"FarDensity", grass.FarDensity},
             {"FarBladeSegments", grass.FarBladeSegments},
             {"FarTuftsEnabled", grass.FarTuftsEnabled},
             {"MidrangeClustersEnabled", grass.MidrangeClustersEnabled},
             {"MidrangeClusterCellExtent", grass.MidrangeClusterCellExtent},
             {"FarTuftBladeCount", grass.FarTuftBladeCount},
             {"DrawFadeFraction", grass.DrawFadeFraction},
             {"DensityFadeFraction", grass.DensityFadeFraction},
             {"TuftTransitionFraction", grass.TuftTransitionFraction},
             {"FarTuftDensity", grass.FarTuftDensity},
             {"FarTuftSpread", grass.FarTuftSpread},
             {"SegmentLodSoftness", grass.SegmentLodSoftness},
         }},
        {"Generation",
         SerializeGrassGeneration(grass.Generation)},
        {"Appearance",
         {
             {"RootColor", grass.Appearance.RootColor},
             {"TipColor", grass.Appearance.TipColor},
             {"TextureColorInfluence",
              grass.Appearance.TextureColorInfluence},
             {"TextureRootBrightness",
              grass.Appearance.TextureRootBrightness},
             {"TextureTipBrightness",
              grass.Appearance.TextureTipBrightness},
             {"HeightScale", grass.Appearance.HeightScale},
             {"BladeCurvature", grass.Appearance.BladeCurvature},
             {"BladeDroop", grass.Appearance.BladeDroop},
             {"ShapeVariation", grass.Appearance.ShapeVariation},
             {"BladeTwistDegrees", grass.Appearance.BladeTwistDegrees},
             {"BladeSegments", grass.Appearance.BladeSegments},
             {"ReceiveLighting", grass.Appearance.ReceiveLighting},
             {"ToonRimEnabled", grass.Appearance.ToonRimEnabled},
             {"ToonRimFadeStart", grass.Appearance.ToonRimFadeStart},
             {"ToonRimFadeEnd", grass.Appearance.ToonRimFadeEnd},
             {"ReceiveFog", grass.Appearance.ReceiveFog},
         }},
        {"Wind",
         {
             {"DirectionDegrees", grass.WindDirectionDegrees},
             {"Strength", grass.WindStrength},
             {"Speed", grass.WindSpeed},
             {"SpatialScale", grass.WindSpatialScale},
             {"GustStrength", grass.WindGustStrength},
             {"GustFrequency", grass.WindGustFrequency},
             {"Turbulence", grass.WindTurbulence},
             {"Randomness", grass.WindRandomness},
         }},
        {"LinkInteraction",
         {
             {"CollisionPush", grass.CollisionPush},
             {"VelocityResponse", grass.CollisionVelocityResponse},
             {"ColliderRadiusMultiplier",
              grass.ColliderRadiusMultiplier},
             {"ColliderHeightMultiplier",
              grass.ColliderHeightMultiplier},
             {"RecoverySeconds", grass.RecoverySeconds},
             {"Damping", grass.InteractionDamping},
             {"MaximumBend", grass.MaximumBend},
             {"FieldRadius", grass.InteractionFieldRadius},
             {"FieldResolution", grass.InteractionFieldResolution},
             {"VerticalMargin", grass.InteractionVerticalMargin},
         }},
        {"Sources", std::move(rules)},
    };
}

void DeserializeGrassGeneration(
    const Json& document, GrassGenerationSettings& generation,
    uint32_t schemaVersion, std::string_view settingsPath,
    GraphicsSettingsLoadResult& result) {
    const std::string path(settingsPath);
    ReadScalar(document, "InstancesPerSquareMeter",
               generation.InstancesPerSquareMeter,
               path + ".InstancesPerSquareMeter", result);
    if (schemaVersion < 6U) {
        float legacyDensityMultiplier = 1.0F;
        if (FindMember(document, "DensityMultiplier") != nullptr) {
            ReadScalar(document, "DensityMultiplier",
                       legacyDensityMultiplier,
                       path + ".DensityMultiplier", result);
        }
        generation.InstancesPerSquareMeter = std::clamp(
            generation.InstancesPerSquareMeter *
                legacyDensityMultiplier,
            0.0F,
            kMaximumGrassInstancesPerSquareMeter);
        Correct(
            result, path + ".InstancesPerSquareMeter",
            "legacy density controls merged into one physical density");
    }
    ReadScalar(document, "MinimumSpacing",
               generation.MinimumSpacing,
               path + ".MinimumSpacing", result);
    ReadScalar(document, "Seed", generation.Seed,
               path + ".Seed", result);
    ReadScalar(document, "IndividualRandomness",
               generation.IndividualRandomness,
               path + ".IndividualRandomness", result);
    ReadScalar(document, "ClusterStrength",
               generation.ClusterStrength,
               path + ".ClusterStrength", result);
    ReadScalar(document, "ClusterScale",
               generation.ClusterScale,
               path + ".ClusterScale", result);
    ReadScalar(document, "ClusterCoverage",
               generation.ClusterCoverage,
               path + ".ClusterCoverage", result);
    if (schemaVersion < 6U &&
        FindMember(document, "ClusterSeed") != nullptr) {
        Correct(
            result, path + ".Seed",
            "legacy cluster seed replaced by the unified distribution seed");
    }
    ReadScalar(document, "BladeHeightMin",
               generation.BladeHeightMin,
               path + ".BladeHeightMin", result);
    ReadScalar(document, "BladeHeightMax",
               generation.BladeHeightMax,
               path + ".BladeHeightMax", result);
    ReadScalar(document, "BladeWidthMin",
               generation.BladeWidthMin,
               path + ".BladeWidthMin", result);
    ReadScalar(document, "BladeWidthMax",
               generation.BladeWidthMax,
               path + ".BladeWidthMax", result);
    if (schemaVersion < 6U &&
        FindMember(document, "MaximumInstancesPerMesh") != nullptr) {
        Correct(
            result, path + ".MaximumInstancesPerMesh",
            "legacy per-mesh cap replaced by the room budget");
    }
}

GrassPlacementRule DeserializeGrassRule(
    const Json& document, size_t index,
    std::string_view settingsPath, std::string_view collectionName,
    uint32_t schemaVersion, GraphicsSettingsLoadResult& result) {
    GrassPlacementRule rule;
    const std::string prefix =
        std::string(settingsPath) + "." +
        std::string(collectionName) + "[" +
        std::to_string(index) + "]";
    ReadScalar(document, "RuleId", rule.RuleId,
               prefix + ".RuleId", result);
    if (const Json* target =
            RequireObject(document, "Target", prefix + ".Target",
                          result)) {
        ReadScalar(*target, "AssetName", rule.Target.AssetName,
                   prefix + ".Target.AssetName", result);
        ReadHash(*target, "Rgba8Hash", rule.Target.Rgba8Hash,
                 prefix + ".Target.Rgba8Hash", result);
        ReadScalar(*target, "Width", rule.Target.Width,
                   prefix + ".Target.Width", result);
        ReadScalar(*target, "Height", rule.Target.Height,
                   prefix + ".Target.Height", result);
        ReadScalar(*target, "MapperSlotMask",
                   rule.Target.MapperSlotMask,
                   prefix + ".Target.MapperSlotMask", result);
    }
    ReadEnum(document, "Channel", rule.Channel,
             kGrassChannelNames, prefix + ".Channel", result);
    ReadEnum(document, "Wrap", rule.Wrap, kGrassWrapNames,
             prefix + ".Wrap", result);
    ReadScalar(document, "Invert", rule.Invert,
               prefix + ".Invert", result);
    if (schemaVersion >= 11U) {
        ReadScalar(document, "InputBlack", rule.InputBlack,
                   prefix + ".InputBlack", result);
        ReadScalar(document, "InputWhite", rule.InputWhite,
                   prefix + ".InputWhite", result);
        ReadScalar(document, "ResponseExponent",
                   rule.ResponseExponent,
                   prefix + ".ResponseExponent", result);
        ReadScalar(document, "OutputBlack", rule.OutputBlack,
                   prefix + ".OutputBlack", result);
        ReadScalar(document, "OutputWhite", rule.OutputWhite,
                   prefix + ".OutputWhite", result);
    } else {
        ReadScalar(document, "Minimum", rule.InputBlack,
                   prefix + ".Minimum", result);
        ReadScalar(document, "Maximum", rule.InputWhite,
                   prefix + ".Maximum", result);
        ReadScalar(document, "Gamma", rule.ResponseExponent,
                   prefix + ".Gamma", result);
    }
    ReadScalar(document, "MaximumSlopeDegrees",
               rule.MaximumSlopeDegrees,
               prefix + ".MaximumSlopeDegrees", result);
    ReadScalar(document, "NormalOffset", rule.NormalOffset,
               prefix + ".NormalOffset", result);
    return rule;
}

void DeserializeInteractiveGrassSettings(
    const Json& document, InteractiveGrassSettings& value,
    std::string_view settingsPath, uint32_t schemaVersion,
    GraphicsSettingsLoadResult& result) {
    const std::string path(settingsPath);
    ReadEnum(document, "Quality", value.Quality,
             kGrassQualityNames, path + ".Quality", result);
    if (const Json* budget =
            RequireObject(document, "Budget", path + ".Budget",
                          result)) {
        ReadScalar(*budget, "MaxInstancesPerRoom",
                   value.MaxInstancesPerRoom,
                   path + ".Budget.MaxInstancesPerRoom", result);
        ReadScalar(*budget, "DrawDistance", value.DrawDistance,
                   path + ".Budget.DrawDistance", result);
    }
    value.LodReferenceDistance = value.DrawDistance;
    if (const Json* performance =
            RequireObject(document, "Performance",
                          path + ".Performance", result)) {
        ReadScalar(*performance, "FrustumCulling",
                   value.FrustumCulling,
                   path + ".Performance.FrustumCulling", result);
        ReadScalar(*performance, "CullingClusterSize",
                   value.CullingClusterSize,
                   path + ".Performance.CullingClusterSize", result);
        ReadScalar(*performance, "LodReferenceDistance", value.LodReferenceDistance,
                   path + ".Performance.LodReferenceDistance", result);
        ReadScalar(*performance, "LodStartFraction",
                   value.LodStartFraction,
                   path + ".Performance.LodStartFraction", result);
        ReadScalar(*performance, "LodEndFraction",
                   value.LodEndFraction,
                   path + ".Performance.LodEndFraction", result);
        // Migrate the formerly coupled segment policy without changing old presets.
        if (!FindMember(*performance, "SegmentLodStartDistance")) {
            value.SegmentLodStartDistance = value.DrawDistance * value.LodStartFraction;
        }
        if (!FindMember(*performance, "SegmentLodEndDistance")) {
            value.SegmentLodEndDistance = value.DrawDistance * value.LodEndFraction;
        }
        ReadScalar(*performance, "SegmentLodStartDistance", value.SegmentLodStartDistance,
                   path + ".Performance.SegmentLodStartDistance", result);
        ReadScalar(*performance, "SegmentLodEndDistance", value.SegmentLodEndDistance,
                   path + ".Performance.SegmentLodEndDistance", result);
        ReadScalar(*performance, "FarDensity", value.FarDensity,
                   path + ".Performance.FarDensity", result);
        ReadScalar(*performance, "FarBladeSegments",
                   value.FarBladeSegments,
                   path + ".Performance.FarBladeSegments", result);
        ReadScalar(*performance, "FarTuftsEnabled", value.FarTuftsEnabled,
                   path + ".Performance.FarTuftsEnabled", result);
        ReadScalar(*performance, "MidrangeClustersEnabled", value.MidrangeClustersEnabled,
                   path + ".Performance.MidrangeClustersEnabled", result);
        ReadScalar(*performance, "MidrangeClusterCellExtent", value.MidrangeClusterCellExtent,
                   path + ".Performance.MidrangeClusterCellExtent", result);
        ReadScalar(*performance, "FarTuftBladeCount", value.FarTuftBladeCount,
                   path + ".Performance.FarTuftBladeCount", result);
        // Additive settings: older profiles are valid, not malformed documents.
        const auto readOptional = [&](const char* name, float& target) {
            if (FindMember(*performance, name)) ReadScalar(*performance, name, target, path + ".Performance." + name, result);
        };
        readOptional("DrawFadeFraction", value.DrawFadeFraction);
        readOptional("DensityFadeFraction", value.DensityFadeFraction);
        readOptional("TuftTransitionFraction", value.TuftTransitionFraction);
        readOptional("FarTuftDensity", value.FarTuftDensity);
        readOptional("FarTuftSpread", value.FarTuftSpread);
        readOptional("SegmentLodSoftness", value.SegmentLodSoftness);
    }
    const std::string_view sourceCollection =
        schemaVersion >= 10U ? "Sources" : "Rules";
    const Json* sourceDocuments =
        FindMember(document, sourceCollection);
    if (schemaVersion >= 10U) {
        if (const Json* generation =
                RequireObject(document, "Generation",
                              path + ".Generation", result)) {
            DeserializeGrassGeneration(
                *generation, value.Generation, schemaVersion,
                path + ".Generation", result);
        }
    } else if (sourceDocuments != nullptr &&
               sourceDocuments->is_array() &&
               !sourceDocuments->empty() &&
               (*sourceDocuments)[0].is_object()) {
        DeserializeGrassGeneration(
            (*sourceDocuments)[0], value.Generation,
            schemaVersion, path + ".Generation", result);
        Correct(
            result, path + ".Generation",
            "per-source generation settings unified from the first grass source");
    }
    if (const Json* appearance =
            RequireObject(document, "Appearance",
                          path + ".Appearance", result)) {
        ReadFloatArray(*appearance, "RootColor",
                       value.Appearance.RootColor,
                       path + ".Appearance.RootColor", result);
        ReadFloatArray(*appearance, "TipColor",
                       value.Appearance.TipColor,
                       path + ".Appearance.TipColor", result);
        ReadScalar(*appearance, "TextureColorInfluence",
                   value.Appearance.TextureColorInfluence,
                   path + ".Appearance.TextureColorInfluence",
                   result);
        ReadScalar(*appearance, "TextureRootBrightness",
                   value.Appearance.TextureRootBrightness,
                   path + ".Appearance.TextureRootBrightness",
                   result);
        ReadScalar(*appearance, "TextureTipBrightness",
                   value.Appearance.TextureTipBrightness,
                   path + ".Appearance.TextureTipBrightness",
                   result);
        ReadScalar(*appearance, "HeightScale",
                   value.Appearance.HeightScale,
                   path + ".Appearance.HeightScale", result);
        ReadScalar(*appearance, "BladeCurvature", value.Appearance.BladeCurvature,
                   path + ".Appearance.BladeCurvature", result);
        ReadScalar(*appearance, "BladeDroop", value.Appearance.BladeDroop,
                   path + ".Appearance.BladeDroop", result);
        ReadScalar(*appearance, "ShapeVariation", value.Appearance.ShapeVariation,
                   path + ".Appearance.ShapeVariation", result);
        ReadScalar(*appearance, "BladeTwistDegrees", value.Appearance.BladeTwistDegrees,
                   path + ".Appearance.BladeTwistDegrees", result);
        ReadScalar(*appearance, "BladeSegments",
                   value.Appearance.BladeSegments,
                   path + ".Appearance.BladeSegments", result);
        ReadScalar(*appearance, "ReceiveLighting",
                   value.Appearance.ReceiveLighting,
                   path + ".Appearance.ReceiveLighting", result);
        ReadScalar(*appearance, "ReceiveFog",
                   value.Appearance.ReceiveFog,
                   path + ".Appearance.ReceiveFog", result);
        ReadScalar(*appearance, "ToonRimEnabled", value.Appearance.ToonRimEnabled,
                   path + ".Appearance.ToonRimEnabled", result);
        ReadScalar(*appearance, "ToonRimFadeStart", value.Appearance.ToonRimFadeStart,
                   path + ".Appearance.ToonRimFadeStart", result);
        ReadScalar(*appearance, "ToonRimFadeEnd", value.Appearance.ToonRimFadeEnd,
                   path + ".Appearance.ToonRimFadeEnd", result);
    }
    if (const Json* wind =
            RequireObject(document, "Wind", path + ".Wind",
                          result)) {
        ReadScalar(*wind, "DirectionDegrees",
                   value.WindDirectionDegrees,
                   path + ".Wind.DirectionDegrees", result);
        ReadScalar(*wind, "Strength", value.WindStrength,
                   path + ".Wind.Strength", result);
        ReadScalar(*wind, "Speed", value.WindSpeed,
                   path + ".Wind.Speed", result);
        ReadScalar(*wind, "SpatialScale", value.WindSpatialScale,
                   path + ".Wind.SpatialScale", result);
        ReadScalar(*wind, "GustStrength", value.WindGustStrength,
                   path + ".Wind.GustStrength", result);
        ReadScalar(*wind, "GustFrequency",
                   value.WindGustFrequency,
                   path + ".Wind.GustFrequency", result);
        ReadScalar(*wind, "Turbulence", value.WindTurbulence,
                   path + ".Wind.Turbulence", result);
        ReadScalar(*wind, "Randomness", value.WindRandomness,
                   path + ".Wind.Randomness", result);
    }
    if (const Json* interaction =
            RequireObject(document, "LinkInteraction",
                          path + ".LinkInteraction", result)) {
        ReadScalar(*interaction, "CollisionPush",
                   value.CollisionPush,
                   path + ".LinkInteraction.CollisionPush", result);
        ReadScalar(*interaction, "VelocityResponse",
                   value.CollisionVelocityResponse,
                   path + ".LinkInteraction.VelocityResponse",
                   result);
        ReadScalar(*interaction, "ColliderRadiusMultiplier",
                   value.ColliderRadiusMultiplier,
                   path +
                       ".LinkInteraction.ColliderRadiusMultiplier",
                   result);
        ReadScalar(*interaction, "ColliderHeightMultiplier",
                   value.ColliderHeightMultiplier,
                   path +
                       ".LinkInteraction.ColliderHeightMultiplier",
                   result);
        ReadScalar(*interaction, "RecoverySeconds",
                   value.RecoverySeconds,
                   path + ".LinkInteraction.RecoverySeconds",
                   result);
        ReadScalar(*interaction, "Damping",
                   value.InteractionDamping,
                   path + ".LinkInteraction.Damping", result);
        ReadScalar(*interaction, "MaximumBend",
                   value.MaximumBend,
                   path + ".LinkInteraction.MaximumBend", result);
        ReadScalar(*interaction, "FieldRadius",
                   value.InteractionFieldRadius,
                   path + ".LinkInteraction.FieldRadius", result);
        ReadScalar(*interaction, "FieldResolution",
                   value.InteractionFieldResolution,
                   path + ".LinkInteraction.FieldResolution",
                   result);
        ReadScalar(*interaction, "VerticalMargin",
                   value.InteractionVerticalMargin,
                   path + ".LinkInteraction.VerticalMargin", result);
    }
    if (sourceDocuments != nullptr &&
        sourceDocuments->is_array()) {
        value.Rules.clear();
        const size_t count =
            std::min<size_t>(sourceDocuments->size(), 256U);
        value.Rules.reserve(count);
        for (size_t index = 0; index < count; ++index) {
            if (!(*sourceDocuments)[index].is_object()) {
                Correct(result,
                        path + "." +
                            std::string(sourceCollection) + "[" +
                            std::to_string(index) + "]",
                        "non-object grass source discarded");
                continue;
            }
            value.Rules.push_back(DeserializeGrassRule(
                (*sourceDocuments)[index], index, settingsPath,
                sourceCollection, schemaVersion, result));
        }
        if (sourceDocuments->size() > count) {
            Correct(
                result,
                path + "." + std::string(sourceCollection),
                "grass sources truncated to 256 entries");
        }
    } else {
        Correct(
            result, path + "." + std::string(sourceCollection),
            "missing or invalid grass source array restored from defaults");
    }
}

Json SerializeReflectionRule(const ReflectionMaterialRule& rule) {
    return {
        {"RuleId", rule.RuleId},
        {"Target",
         {
             {"ContentHash",
              SerializeHash(rule.Target.ContentHash)},
             {"Width", rule.Target.Width},
             {"Height", rule.Target.Height},
             {"MapperSlotMask", rule.Target.MapperSlotMask},
         }},
        {"Profile", EnumName(rule.Profile,
                             kMaterialProfileNames)},
        {"Reflectivity", rule.Reflectivity},
        {"Roughness", rule.Roughness},
    };
}

ReflectionMaterialRule DeserializeReflectionRule(
    const Json& document, size_t index,
    GraphicsSettingsLoadResult& result) {
    ReflectionMaterialRule rule;
    const std::string prefix =
        "Graphics.Effects.Reflections.Materials[" +
        std::to_string(index) + "]";
    ReadScalar(document, "RuleId", rule.RuleId,
               prefix + ".RuleId", result);
    if (const Json* target =
            RequireObject(document, "Target", prefix + ".Target",
                          result)) {
        ReadHash(*target, "ContentHash",
                 rule.Target.ContentHash,
                 prefix + ".Target.ContentHash", result);
        ReadScalar(*target, "Width", rule.Target.Width,
                   prefix + ".Target.Width", result);
        ReadScalar(*target, "Height", rule.Target.Height,
                   prefix + ".Target.Height", result);
        ReadScalar(*target, "MapperSlotMask",
                   rule.Target.MapperSlotMask,
                   prefix + ".Target.MapperSlotMask", result);
    }
    ReadEnum(document, "Profile", rule.Profile,
             kMaterialProfileNames, prefix + ".Profile", result);
    ReadScalar(document, "Reflectivity", rule.Reflectivity,
               prefix + ".Reflectivity", result);
    ReadScalar(document, "Roughness", rule.Roughness,
               prefix + ".Roughness", result);
    return rule;
}

GraphicsCapabilities PermissiveCapabilities() {
    GraphicsCapabilities capabilities;
    for (uint8_t value =
             static_cast<uint8_t>(GraphicsCapability::NriInterop);
         value <= static_cast<uint8_t>(
                      GraphicsCapability::ExclusiveFullscreen);
         ++value) {
        capabilities.Set(
            static_cast<GraphicsCapability>(value), true);
    }
    return capabilities;
}

void Normalize(GraphicsSettingsLoadResult& result) {
    const Json before = SerializeGraphicsSettings(result.Value);
    auto validation = GraphicsSettingsService::Validate(
        result.Value, PermissiveCapabilities());
    result.Value = std::move(validation.Value);
    if (SerializeGraphicsSettings(result.Value) != before) {
        Correct(result, "Graphics",
                "out-of-range settings normalized");
    }
    if (!validation.Issues.empty()) {
        result.NeedsRewrite = true;
        result.Issues.insert(result.Issues.end(),
                             validation.Issues.begin(),
                             validation.Issues.end());
    }
}

const Json* FindPath(const Json& root,
                     std::initializer_list<std::string_view> path) {
    const Json* current = &root;
    for (const std::string_view component : path) {
        current = FindMember(*current, component);
        if (current == nullptr) {
            return nullptr;
        }
    }
    return current;
}

template <typename T>
bool ReadLegacyValue(const Json& root,
                     std::initializer_list<std::string_view> path,
                     T& output) {
    const Json* value = FindPath(root, path);
    if (value == nullptr) {
        return false;
    }
    try {
        if constexpr (std::is_same_v<T, bool>) {
            if (value->is_boolean()) {
                output = value->get<bool>();
                return true;
            }
            if (value->is_number_integer()) {
                output = value->get<int64_t>() != 0;
                return true;
            }
        } else if constexpr (std::is_floating_point_v<T>) {
            if (value->is_number()) {
                const double parsed = value->get<double>();
                if (std::isfinite(parsed)) {
                    output = static_cast<T>(parsed);
                    return true;
                }
            }
        } else if constexpr (std::is_integral_v<T>) {
            if (value->is_number_integer() ||
                value->is_number_unsigned()) {
                const int64_t parsed = value->get<int64_t>();
                if (parsed >= 0 &&
                    static_cast<uint64_t>(parsed) <=
                        std::numeric_limits<T>::max()) {
                    output = static_cast<T>(parsed);
                    return true;
                }
            }
        }
    } catch (const std::exception&) {
    }
    return false;
}

GraphicsSettingsLoadResult MigrateLegacy(
    const Json& root, GraphicsSettings fallback) {
    GraphicsSettingsLoadResult result;
    result.Value = std::move(fallback);
    bool migrated = false;

    bool fullscreen = false;
    if (ReadLegacyValue(root,
                        {"Window", "Fullscreen", "Enabled"},
                        fullscreen)) {
        bool borderless = false;
        if (!ReadLegacyValue(
                root, {"CVars", "gSdlWindowedFullscreen"},
                borderless)) {
            ReadLegacyValue(
                root,
                {"CVars", "gSettings",
                 "SdlWindowedFullscreen"},
                borderless);
        }
        result.Value.Window = fullscreen
            ? (borderless ? WindowMode::Borderless
                          : WindowMode::ExclusiveFullscreen)
            : WindowMode::Windowed;
        migrated = true;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    const bool hasAdvancedResolution =
        [&] {
            bool enabled = false;
            bool vertical = false;
            uint32_t verticalPixels = 0;
            float aspectX = 16.0F;
            float aspectY = 9.0F;
            const bool hasEnabled =
                ReadLegacyValue(
                    root,
                    {"CVars", "gAdvancedResolution", "Enabled"},
                    enabled) ||
                ReadLegacyValue(
                    root,
                    {"CVars", "gSettings",
                     "AdvancedResolution", "Enabled"},
                    enabled);
            const bool hasVertical =
                ReadLegacyValue(
                    root,
                    {"CVars", "gAdvancedResolution",
                     "VerticalResolutionToggle"},
                    vertical) ||
                ReadLegacyValue(
                    root,
                    {"CVars", "gSettings",
                     "AdvancedResolution",
                     "VerticalResolutionToggle"},
                    vertical);
            const bool hasVerticalPixels =
                ReadLegacyValue(
                    root,
                    {"CVars", "gAdvancedResolution",
                     "VerticalPixelCount"},
                    verticalPixels) ||
                ReadLegacyValue(
                    root,
                    {"CVars", "gSettings",
                     "AdvancedResolution",
                     "VerticalPixelCount"},
                    verticalPixels);
            if (!hasEnabled ||
                !enabled ||
                !hasVertical ||
                !vertical ||
                !hasVerticalPixels) {
                return false;
            }
            if (!ReadLegacyValue(
                    root,
                    {"CVars", "gAdvancedResolution",
                     "AspectRatioX"},
                    aspectX)) {
                ReadLegacyValue(
                    root,
                    {"CVars", "gSettings",
                     "AdvancedResolution", "AspectRatioX"},
                    aspectX);
            }
            if (!ReadLegacyValue(
                    root,
                    {"CVars", "gAdvancedResolution",
                     "AspectRatioY"},
                    aspectY)) {
                ReadLegacyValue(
                    root,
                    {"CVars", "gSettings",
                     "AdvancedResolution", "AspectRatioY"},
                    aspectY);
            }
            if (aspectX <= 0.0F || aspectY <= 0.0F) {
                return false;
            }
            height = verticalPixels;
            width = static_cast<uint32_t>(
                std::lround(static_cast<double>(height) *
                            aspectX / aspectY));
            return true;
        }();
    if (!hasAdvancedResolution) {
        const bool useFullscreenDimensions =
            result.Value.Window != WindowMode::Windowed;
        if (useFullscreenDimensions) {
            ReadLegacyValue(root,
                            {"Window", "Fullscreen", "Width"},
                            width);
            ReadLegacyValue(root,
                            {"Window", "Fullscreen", "Height"},
                            height);
        } else {
            ReadLegacyValue(root, {"Window", "Width"}, width);
            ReadLegacyValue(root, {"Window", "Height"}, height);
        }
    }
    if (width != 0U && height != 0U) {
        result.Value.OutputWidth = width;
        result.Value.OutputHeight = height;
        migrated = true;
    }

    float renderScale = 1.0F;
    if (ReadLegacyValue(root, {"CVars", "gInternalResolution"},
                        renderScale) ||
        ReadLegacyValue(
            root, {"CVars", "gSettings", "InternalResolution"},
            renderScale)) {
        result.Value.InternalResolutionScale = renderScale;
        migrated = true;
    }
    uint8_t msaa = 1U;
    if (ReadLegacyValue(root, {"CVars", "gMSAAValue"}, msaa) ||
        ReadLegacyValue(root,
                        {"CVars", "gSettings", "MSAAValue"},
                        msaa)) {
        result.Value.MsaaSamples = msaa;
        result.Value.AntiAliasing =
            msaa > 1U ? AntiAliasingMode::Msaa
                      : AntiAliasingMode::Off;
        migrated = true;
    }
    bool vsync = true;
    if (ReadLegacyValue(root, {"CVars", "gVsyncEnabled"},
                        vsync) ||
        ReadLegacyValue(root,
                        {"CVars", "gSettings", "VsyncEnabled"},
                        vsync)) {
        result.Value.VSync = vsync;
        migrated = true;
    }

    if (migrated) {
        result.Value.Preset = GraphicsPreset::Custom;
        result.Found = true;
        result.NeedsRewrite = true;
        result.MigratedLegacy = true;
        result.Issues.push_back(
            {"Graphics", "legacy Window/CVar settings migrated",
             true});
        Normalize(result);
    }
    return result;
}

} // namespace

nlohmann::json
SerializeGraphicsSettings(const GraphicsSettings& settings) {
    Json reflectionRules = Json::array();
    for (const auto& rule :
         settings.Effects.ReflectionMaterials) {
        reflectionRules.push_back(
            SerializeReflectionRule(rule));
    }
    const auto& effects = settings.Effects;
    const auto& directionalShadows = effects.DirectionalShadows;
    const auto& toon = effects.ToonStyle;
    return {
        {"SchemaVersion", kGraphicsSettingsSchemaVersion},
        {"Preset", EnumName(settings.Preset, kPresetNames)},
        {"Window",
         {
             {"Mode", EnumName(settings.Window, kWindowNames)},
             {"Display", settings.DisplayIndex},
         }},
        {"Output",
         {
             {"Width", settings.OutputWidth},
             {"Height", settings.OutputHeight},
             {"RefreshRate", settings.RefreshRate},
         }},
        {"RenderScale", settings.InternalResolutionScale},
        {"AA",
         {
             {"Mode",
              EnumName(settings.AntiAliasing, kAaNames)},
             {"MsaaSamples", settings.MsaaSamples},
             {"TAA",
              {
                  {"HistoryWeight", settings.TaaHistoryWeight},
                  {"ClampExpansion", settings.TaaClampExpansion},
                  {"Sharpness", settings.TaaSharpness},
              }},
             {"Upscaler",
              {
                  {"Provider",
                   EnumName(settings.Upscaler,
                            kUpscalerProviderNames)},
                  {"Quality",
                   EnumName(settings.UpscalerMode,
                            kUpscalerQualityNames)},
                  {"Sharpness", settings.UpscalerSharpness},
              }},
         }},
        {"FrameRate",
         {{"Mode", EnumName(settings.FrameRate,
                            kFrameRateNames)}}},
        {"Presentation", {{"VSync", settings.VSync}}},
        {"Camera", {{"FovMultiplier", settings.FovMultiplier}}},
        {"TexturePacks",
         {
             {"Azahar",
              {
                  {"DumpTextures",
                   settings.TexturePacks.Azahar.DumpTextures},
                  {"LoadCustomTextures",
                   settings.TexturePacks.Azahar
                       .LoadCustomTextures},
                  {"LoadDirectory",
                   settings.TexturePacks.Azahar.LoadDirectory},
                  {"DumpDirectory",
                   settings.TexturePacks.Azahar.DumpDirectory},
              }},
         }},
        {"Grass",
         SerializeInteractiveGrassSettings(settings.Grass)},
        {"GrassSavedPreset",
         SerializeInteractiveGrassSettings(
             settings.GrassSavedPreset)},
        {"Effects",
         {
             {"DirectionalShadows",
              {
                  {"Mode",
                   EnumName(directionalShadows.Mode,
                            kDirectionalShadowNames)},
                  {"Resolution", directionalShadows.Resolution},
                  {"MaximumDistance",
                   directionalShadows.MaximumDistance},
                  {"DepthPadding", directionalShadows.DepthPadding},
                  {"DepthBiasConstant",
                   directionalShadows.DepthBiasConstant},
                  {"DepthBiasSlope",
                   directionalShadows.DepthBiasSlope},
                  {"Strength", directionalShadows.Strength},
                  {"PcfRadius", directionalShadows.PcfRadius},
                  {"Stabilize", directionalShadows.Stabilize},
              }},
             {"AO",
              {
                  {"Mode",
                   EnumName(effects.AmbientOcclusion,
                            kAoNames)},
                  {"Quality", effects.AoQuality},
                  {"Radius", effects.AoRadius},
                  {"Strength", effects.AoStrength},
                  {"ShadowPower", effects.AoShadowPower},
                  {"ShadowClamp", effects.AoShadowClamp},
                  {"HorizonAngleThreshold",
                   effects.AoHorizonAngleThreshold},
                  {"FadeOutFrom", effects.AoFadeOutFrom},
                  {"FadeOutTo", effects.AoFadeOutTo},
                  {"BlurPassCount", effects.AoBlurPassCount},
                  {"Sharpness", effects.AoSharpness},
                  {"DetailStrength",
                   effects.AoDetailStrength},
              }},
             {"Reflections",
              {
                  {"Mode",
                   EnumName(effects.Reflections,
                            kReflectionNames)},
                  {"Strength", effects.ReflectionStrength},
                  {"MaxDistance",
                   effects.ReflectionMaxDistance},
                  {"Thickness", effects.ReflectionThickness},
                  {"EdgeFade", effects.ReflectionEdgeFade},
                  {"MaxSteps", effects.ReflectionMaxSteps},
                  {"RoughnessBias",
                   effects.ReflectionRoughnessBias},
                  {"DebugView",
                   effects.ReflectionDebugView},
                  {"Materials", std::move(reflectionRules)},
              }},
             {"Toon",
              {
                  {"Mode", EnumName(effects.Toon, kToonNames)},
                  {"LightBands", toon.LightBands},
                  {"CustomLightBands", toon.CustomLightBands},
                  {"LightBandLevels", toon.LightBandLevels},
                  {"LightBandThresholds", toon.LightBandThresholds},
                  {"BandSoftness", toon.BandSoftness},
                  {"Saturation", toon.Saturation},
                  {"ShadowTint", toon.ShadowTint},
                  {"ShadowStrength", toon.ShadowStrength},
                  {"RimStrength", toon.RimStrength},
                  {"RimWidth", toon.RimWidth},
                  {"RimTint", toon.RimTint},
                  {"OutlineEnabled", toon.OutlineEnabled},
                  {"OutlineWidth", toon.OutlineWidth},
                  {"OutlineSoftness", toon.OutlineSoftness},
                  {"OutlineDepthSensitivity",
                   toon.OutlineDepthSensitivity},
                  {"OutlineNormalSensitivity",
                   toon.OutlineNormalSensitivity},
                  {"OutlineTint", toon.OutlineTint},
                  {"OutlineOpacity", toon.OutlineOpacity},
              }},
         }},
    };
}

GraphicsSettingsLoadResult DeserializeGraphicsSettings(
    const nlohmann::json& document, GraphicsSettings fallback) {
    GraphicsSettingsLoadResult result;
    result.Value = std::move(fallback);
    result.Found = true;
    if (!document.is_object()) {
        Correct(result, "Graphics",
                "graphics settings block is not an object");
        Normalize(result);
        return result;
    }

    uint32_t version = 0U;
    const Json* versionValue =
        FindMember(document, "SchemaVersion");
    if (versionValue == nullptr ||
        (!versionValue->is_number_integer() &&
         !versionValue->is_number_unsigned())) {
        Correct(result, "Graphics.SchemaVersion",
                "missing or invalid schema version");
    } else {
        try {
            version = versionValue->get<uint32_t>();
        } catch (const std::exception&) {
            Correct(result, "Graphics.SchemaVersion",
                    "invalid schema version");
        }
    }
    if (version > kGraphicsSettingsSchemaVersion) {
        result.UnsupportedFutureVersion = true;
        result.NeedsRewrite = false;
        result.Issues.push_back(
            {"Graphics.SchemaVersion",
             "newer schema preserved without loading", false});
        return result;
    }
    if (version != kGraphicsSettingsSchemaVersion) {
        result.NeedsRewrite = true;
    }

    ReadEnum(document, "Preset", result.Value.Preset,
             kPresetNames, "Graphics.Preset", result);
    if (const Json* window =
            RequireObject(document, "Window", "Graphics.Window",
                          result)) {
        ReadEnum(*window, "Mode", result.Value.Window,
                 kWindowNames, "Graphics.Window.Mode", result);
        ReadScalar(*window, "Display",
                   result.Value.DisplayIndex,
                   "Graphics.Window.Display", result);
    }
    if (const Json* output =
            RequireObject(document, "Output", "Graphics.Output",
                          result)) {
        ReadScalar(*output, "Width", result.Value.OutputWidth,
                   "Graphics.Output.Width", result);
        ReadScalar(*output, "Height",
                   result.Value.OutputHeight,
                   "Graphics.Output.Height", result);
        ReadScalar(*output, "RefreshRate",
                   result.Value.RefreshRate,
                   "Graphics.Output.RefreshRate", result);
    }
    ReadScalar(document, "RenderScale",
               result.Value.InternalResolutionScale,
               "Graphics.RenderScale", result);
    if (const Json* aa =
            RequireObject(document, "AA", "Graphics.AA",
                          result)) {
        ReadEnum(*aa, "Mode", result.Value.AntiAliasing,
                 kAaNames, "Graphics.AA.Mode", result);
        ReadScalar(*aa, "MsaaSamples",
                   result.Value.MsaaSamples,
                   "Graphics.AA.MsaaSamples", result);
        if (const Json* taa =
                RequireObject(*aa, "TAA", "Graphics.AA.TAA",
                              result)) {
            ReadScalar(*taa, "HistoryWeight",
                       result.Value.TaaHistoryWeight,
                       "Graphics.AA.TAA.HistoryWeight", result);
            ReadScalar(*taa, "ClampExpansion",
                       result.Value.TaaClampExpansion,
                       "Graphics.AA.TAA.ClampExpansion", result);
            ReadScalar(*taa, "Sharpness",
                       result.Value.TaaSharpness,
                       "Graphics.AA.TAA.Sharpness", result);
        }
        if (const Json* upscaler =
                RequireObject(*aa, "Upscaler",
                              "Graphics.AA.Upscaler", result)) {
            ReadEnum(*upscaler, "Provider",
                     result.Value.Upscaler,
                     kUpscalerProviderNames,
                     "Graphics.AA.Upscaler.Provider", result);
            ReadEnum(*upscaler, "Quality",
                     result.Value.UpscalerMode,
                     kUpscalerQualityNames,
                     "Graphics.AA.Upscaler.Quality", result);
            ReadScalar(*upscaler, "Sharpness",
                       result.Value.UpscalerSharpness,
                       "Graphics.AA.Upscaler.Sharpness", result);
        }
    }
    if (const Json* frameRate =
            RequireObject(document, "FrameRate",
                          "Graphics.FrameRate", result)) {
        ReadEnum(*frameRate, "Mode", result.Value.FrameRate,
                 kFrameRateNames, "Graphics.FrameRate.Mode",
                 result);
    }
    if (const Json* presentation =
            RequireObject(document, "Presentation",
                          "Graphics.Presentation", result)) {
        ReadScalar(*presentation, "VSync", result.Value.VSync,
                   "Graphics.Presentation.VSync", result);
    }
    if (const Json* camera =
            RequireObject(document, "Camera", "Graphics.Camera",
                          result)) {
        ReadScalar(*camera, "FovMultiplier",
                   result.Value.FovMultiplier,
                   "Graphics.Camera.FovMultiplier", result);
    }
    if (version >= 8U) {
        if (const Json* texturePacks =
                RequireObject(
                    document, "TexturePacks",
                    "Graphics.TexturePacks", result)) {
            if (const Json* azahar =
                    RequireObject(
                        *texturePacks, "Azahar",
                        "Graphics.TexturePacks.Azahar",
                        result)) {
                ReadScalar(
                    *azahar, "DumpTextures",
                    result.Value.TexturePacks.Azahar
                        .DumpTextures,
                    "Graphics.TexturePacks.Azahar.DumpTextures",
                    result);
                ReadScalar(
                    *azahar, "LoadCustomTextures",
                    result.Value.TexturePacks.Azahar
                        .LoadCustomTextures,
                    "Graphics.TexturePacks.Azahar.LoadCustomTextures",
                    result);
                if (version >= 9U) {
                    ReadScalar(
                        *azahar, "LoadDirectory",
                        result.Value.TexturePacks.Azahar
                            .LoadDirectory,
                        "Graphics.TexturePacks.Azahar.LoadDirectory",
                        result);
                    ReadScalar(
                        *azahar, "DumpDirectory",
                        result.Value.TexturePacks.Azahar
                            .DumpDirectory,
                        "Graphics.TexturePacks.Azahar.DumpDirectory",
                        result);
                } else {
                    result.Value.TexturePacks.Azahar.LoadDirectory.clear();
                    result.Value.TexturePacks.Azahar.DumpDirectory.clear();
                }
            }
        }
    } else {
        result.Value.TexturePacks = {};
    }

    if (const Json* grass =
            RequireObject(document, "Grass", "Graphics.Grass",
                          result)) {
        DeserializeInteractiveGrassSettings(
            *grass, result.Value.Grass, "Graphics.Grass",
            version, result);
    }
    if (version < 7U) {
        result.Value.GrassSavedPreset = result.Value.Grass;
        result.NeedsRewrite = true;
        result.Issues.push_back({
            "Graphics.GrassSavedPreset",
            "current grass settings migrated as the saved preset",
            true});
    } else {
        result.Value.GrassSavedPreset = result.Value.Grass;
        if (const Json* savedPreset =
                RequireObject(
                    document, "GrassSavedPreset",
                    "Graphics.GrassSavedPreset", result)) {
            DeserializeInteractiveGrassSettings(
                *savedPreset, result.Value.GrassSavedPreset,
                "Graphics.GrassSavedPreset", version, result);
        }
    }

    if (const Json* effects =
            RequireObject(document, "Effects",
                          "Graphics.Effects", result)) {
        auto& value = result.Value.Effects;
        if (version >= 3U) {
            if (const Json* shadows =
                    RequireObject(*effects, "DirectionalShadows",
                                  "Graphics.Effects.DirectionalShadows",
                                  result)) {
                auto& settings = value.DirectionalShadows;
                ReadEnum(*shadows, "Mode", settings.Mode,
                         kDirectionalShadowNames,
                         "Graphics.Effects.DirectionalShadows.Mode",
                         result);
                ReadScalar(
                    *shadows, "Resolution", settings.Resolution,
                    "Graphics.Effects.DirectionalShadows.Resolution",
                    result);
                ReadScalar(
                    *shadows, "MaximumDistance",
                    settings.MaximumDistance,
                    "Graphics.Effects.DirectionalShadows.MaximumDistance",
                    result);
                ReadScalar(
                    *shadows, "DepthPadding", settings.DepthPadding,
                    "Graphics.Effects.DirectionalShadows.DepthPadding",
                    result);
                ReadScalar(
                    *shadows, "DepthBiasConstant",
                    settings.DepthBiasConstant,
                    "Graphics.Effects.DirectionalShadows.DepthBiasConstant",
                    result);
                ReadScalar(
                    *shadows, "DepthBiasSlope",
                    settings.DepthBiasSlope,
                    "Graphics.Effects.DirectionalShadows.DepthBiasSlope",
                    result);
                ReadScalar(
                    *shadows, "Strength", settings.Strength,
                    "Graphics.Effects.DirectionalShadows.Strength",
                    result);
                ReadScalar(
                    *shadows, "PcfRadius", settings.PcfRadius,
                    "Graphics.Effects.DirectionalShadows.PcfRadius",
                    result);
                ReadScalar(
                    *shadows, "Stabilize", settings.Stabilize,
                    "Graphics.Effects.DirectionalShadows.Stabilize",
                    result);
            }
        } else {
            value.DirectionalShadows = {};
        }
        if (const Json* ao =
                RequireObject(*effects, "AO",
                              "Graphics.Effects.AO", result)) {
            ReadEnum(*ao, "Mode", value.AmbientOcclusion,
                     kAoNames, "Graphics.Effects.AO.Mode",
                     result);
            ReadScalar(*ao, "Quality", value.AoQuality,
                       "Graphics.Effects.AO.Quality", result);
            ReadScalar(*ao, "Radius", value.AoRadius,
                       "Graphics.Effects.AO.Radius", result);
            ReadScalar(*ao, "Strength", value.AoStrength,
                       "Graphics.Effects.AO.Strength", result);
            ReadScalar(*ao, "ShadowPower",
                       value.AoShadowPower,
                       "Graphics.Effects.AO.ShadowPower", result);
            ReadScalar(*ao, "ShadowClamp",
                       value.AoShadowClamp,
                       "Graphics.Effects.AO.ShadowClamp", result);
            ReadScalar(*ao, "HorizonAngleThreshold",
                       value.AoHorizonAngleThreshold,
                       "Graphics.Effects.AO.HorizonAngleThreshold",
                       result);
            ReadScalar(*ao, "FadeOutFrom",
                       value.AoFadeOutFrom,
                       "Graphics.Effects.AO.FadeOutFrom", result);
            ReadScalar(*ao, "FadeOutTo", value.AoFadeOutTo,
                       "Graphics.Effects.AO.FadeOutTo", result);
            ReadScalar(*ao, "BlurPassCount",
                       value.AoBlurPassCount,
                       "Graphics.Effects.AO.BlurPassCount", result);
            ReadScalar(*ao, "Sharpness", value.AoSharpness,
                       "Graphics.Effects.AO.Sharpness", result);
            ReadScalar(*ao, "DetailStrength",
                       value.AoDetailStrength,
                       "Graphics.Effects.AO.DetailStrength",
                       result);
        }
        if (const Json* reflections =
                RequireObject(*effects, "Reflections",
                              "Graphics.Effects.Reflections",
                              result)) {
            ReadEnum(*reflections, "Mode", value.Reflections,
                     kReflectionNames,
                     "Graphics.Effects.Reflections.Mode", result);
            ReadScalar(*reflections, "Strength",
                       value.ReflectionStrength,
                       "Graphics.Effects.Reflections.Strength",
                       result);
            ReadScalar(*reflections, "MaxDistance",
                       value.ReflectionMaxDistance,
                       "Graphics.Effects.Reflections.MaxDistance",
                       result);
            ReadScalar(*reflections, "Thickness",
                       value.ReflectionThickness,
                       "Graphics.Effects.Reflections.Thickness",
                       result);
            ReadScalar(*reflections, "EdgeFade",
                       value.ReflectionEdgeFade,
                       "Graphics.Effects.Reflections.EdgeFade",
                       result);
            ReadScalar(*reflections, "MaxSteps",
                       value.ReflectionMaxSteps,
                       "Graphics.Effects.Reflections.MaxSteps",
                       result);
            ReadScalar(*reflections, "RoughnessBias",
                       value.ReflectionRoughnessBias,
                       "Graphics.Effects.Reflections.RoughnessBias",
                       result);
            ReadScalar(*reflections, "DebugView",
                       value.ReflectionDebugView,
                       "Graphics.Effects.Reflections.DebugView",
                       result);
            if (const Json* materials =
                    FindMember(*reflections, "Materials");
                materials != nullptr && materials->is_array()) {
                value.ReflectionMaterials.clear();
                const size_t count = std::min<size_t>(
                    materials->size(), 256U);
                value.ReflectionMaterials.reserve(count);
                for (size_t index = 0; index < count; ++index) {
                    if (!(*materials)[index].is_object()) {
                        Correct(result,
                                "Graphics.Effects.Reflections.Materials[" +
                                    std::to_string(index) + "]",
                                "non-object rule discarded");
                        continue;
                    }
                    value.ReflectionMaterials.push_back(
                        DeserializeReflectionRule(
                            (*materials)[index], index, result));
                }
                if (materials->size() > count) {
                    Correct(
                        result,
                        "Graphics.Effects.Reflections.Materials",
                        "reflection rules truncated to 256 entries");
                }
            } else {
                Correct(
                    result,
                    "Graphics.Effects.Reflections.Materials",
                    "missing or invalid materials array restored from defaults");
            }
        }
        if (const Json* toon =
                RequireObject(*effects, "Toon",
                              "Graphics.Effects.Toon", result)) {
            ReadEnum(*toon, "Mode", value.Toon, kToonNames,
                     "Graphics.Effects.Toon.Mode", result);
            auto& style = value.ToonStyle;
            ReadScalar(*toon, "LightBands", style.LightBands,
                       "Graphics.Effects.Toon.LightBands", result);
            if (version >= 2U) {
                ReadScalar(*toon, "CustomLightBands",
                           style.CustomLightBands,
                           "Graphics.Effects.Toon.CustomLightBands",
                           result);
                ReadFloatArray(*toon, "LightBandLevels",
                               style.LightBandLevels,
                               "Graphics.Effects.Toon.LightBandLevels",
                               result);
                ReadFloatArray(*toon, "LightBandThresholds",
                               style.LightBandThresholds,
                               "Graphics.Effects.Toon.LightBandThresholds",
                               result);
            } else {
                style.CustomLightBands = false;
                ResetToonLightBandProfile(style);
            }
            ReadScalar(*toon, "BandSoftness",
                       style.BandSoftness,
                       "Graphics.Effects.Toon.BandSoftness", result);
            ReadScalar(*toon, "Saturation", style.Saturation,
                       "Graphics.Effects.Toon.Saturation", result);
            ReadFloatArray(*toon, "ShadowTint",
                           style.ShadowTint,
                           "Graphics.Effects.Toon.ShadowTint",
                           result);
            ReadScalar(*toon, "ShadowStrength",
                       style.ShadowStrength,
                       "Graphics.Effects.Toon.ShadowStrength",
                       result);
            ReadScalar(*toon, "RimStrength",
                       style.RimStrength,
                       "Graphics.Effects.Toon.RimStrength", result);
            ReadScalar(*toon, "RimWidth", style.RimWidth,
                       "Graphics.Effects.Toon.RimWidth", result);
            ReadFloatArray(*toon, "RimTint", style.RimTint,
                           "Graphics.Effects.Toon.RimTint",
                           result);
            ReadScalar(*toon, "OutlineEnabled",
                       style.OutlineEnabled,
                       "Graphics.Effects.Toon.OutlineEnabled",
                       result);
            ReadScalar(*toon, "OutlineWidth",
                       style.OutlineWidth,
                       "Graphics.Effects.Toon.OutlineWidth",
                       result);
            ReadScalar(*toon, "OutlineSoftness", style.OutlineSoftness,
                       "Graphics.Effects.Toon.OutlineSoftness", result);
            ReadScalar(
                *toon, "OutlineDepthSensitivity",
                style.OutlineDepthSensitivity,
                "Graphics.Effects.Toon.OutlineDepthSensitivity",
                result);
            ReadScalar(
                *toon, "OutlineNormalSensitivity",
                style.OutlineNormalSensitivity,
                "Graphics.Effects.Toon.OutlineNormalSensitivity",
                result);
            ReadFloatArray(*toon, "OutlineTint",
                           style.OutlineTint,
                           "Graphics.Effects.Toon.OutlineTint",
                           result);
            ReadScalar(*toon, "OutlineOpacity",
                       style.OutlineOpacity,
                       "Graphics.Effects.Toon.OutlineOpacity",
                       result);
        }
    }
    Normalize(result);
    return result;
}

GraphicsSettingsLoadResult LoadGraphicsSettingsConfig(
    const nlohmann::json& configRoot, GraphicsSettings fallback) {
    if (const Json* graphics =
            FindMember(configRoot, "Graphics")) {
        return DeserializeGraphicsSettings(*graphics,
                                           std::move(fallback));
    }
    return MigrateLegacy(configRoot, std::move(fallback));
}

bool ShouldPersistGraphicsSettings(
    bool environmentOverridesActive,
    bool presentationTransactionActive) noexcept {
    return !environmentOverridesActive &&
        !presentationTransactionActive;
}

} // namespace Fast::Oot3d
