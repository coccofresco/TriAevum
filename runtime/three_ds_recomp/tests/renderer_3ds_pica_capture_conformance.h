#pragma once

#include <cstdint>
#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace Fast::Renderer3ds::Diagnostics {

struct PicaCaptureConformanceReport {
    uint64_t SourceFileCount = 0U;
    uint64_t SourceByteCount = 0U;
    uint64_t SourceLineCount = 0U;
    uint64_t CaptureBeginCount = 0U;
    uint64_t CaptureEndCount = 0U;
    uint64_t FrameBoundaryCount = 0U;
    uint64_t CommandListCount = 0U;
    uint64_t DrawBeginCount = 0U;
    uint64_t DrawEndCount = 0U;
    uint64_t DecodedDrawCount = 0U;
    uint64_t ContractRepresentableDrawCount = 0U;
    uint64_t IndexedDrawCount = 0U;
    uint64_t GeometryShaderTopologyDrawCount = 0U;
    uint64_t ExplicitGeometryShaderDrawCount = 0U;
    uint64_t ShaderIdentityDrawCount = 0U;
    uint64_t VertexProgramUploadCount = 0U;
    uint64_t VertexProgramUploadWordCount = 0U;
    uint64_t VertexSwizzleUploadCount = 0U;
    uint64_t VertexSwizzleUploadWordCount = 0U;
    uint64_t GeometryProgramUploadCount = 0U;
    uint64_t GeometryProgramUploadWordCount = 0U;
    uint64_t GeometrySwizzleUploadCount = 0U;
    uint64_t GeometrySwizzleUploadWordCount = 0U;
    uint64_t FragmentLightingDrawCount = 0U;
    uint64_t FogDrawCount = 0U;
    uint64_t GasDrawCount = 0U;
    uint64_t ProceduralTextureDrawCount = 0U;
    uint64_t ShadowTextureDrawCount = 0U;
    uint64_t ShadowRenderDrawCount = 0U;
    uint64_t AlphaBlendDrawCount = 0U;
    uint64_t LogicOperationDrawCount = 0U;
    uint64_t AlphaTestDrawCount = 0U;
    uint64_t DepthTestDrawCount = 0U;
    uint64_t DepthWriteDrawCount = 0U;
    uint64_t StencilTestDrawCount = 0U;
    uint64_t EnabledTextureCount = 0U;
    uint64_t CompressedTextureCount = 0U;
    uint32_t MaximumTextureWidth = 0U;
    uint32_t MaximumTextureHeight = 0U;
    std::map<std::string, uint64_t> EventCounts;
    std::map<std::string, uint64_t> IssueCounts;
    std::map<std::string, uint64_t> UnknownEventCounts;
    std::map<std::string, uint64_t> DrawModeCounts;
    std::map<std::string, uint64_t> TopologyCounts;
    std::map<std::string, uint64_t> CullModeCounts;
    std::map<std::string, uint64_t> FogModeCounts;
    std::map<std::string, uint64_t> FragmentOperationCounts;
    std::map<std::string, uint64_t> TextureFormatCounts;
    std::map<std::string, uint64_t> TextureTypeCounts;
    std::map<std::string, uint64_t> TextureDimensionBucketCounts;
    std::map<std::string, uint64_t> TevColorOperationCounts;
    std::map<std::string, uint64_t> TevAlphaOperationCounts;
    std::map<std::string, uint64_t> TevSourceReferenceCounts;
    std::map<std::string, uint64_t> ActiveLightCountDraws;
    std::set<std::string> VertexProgramIdentities;
    std::set<std::string> GeometryProgramIdentities;
    std::set<std::string> FragmentConfigurationIdentities;
    std::set<std::string> PipelineIdentities;
    std::set<uint64_t> TevStateIdentities;
    std::set<uint64_t> RasterStateIdentities;

    [[nodiscard]] bool Conformant() const noexcept;
    [[nodiscard]] nlohmann::json ToJson(std::string_view corpusId, std::string_view sourceImageSha256 = {}) const;
};

bool AnalyzePicaCaptureStream(std::istream& input, PicaCaptureConformanceReport& report, std::string* error = nullptr);

} // namespace Fast::Renderer3ds::Diagnostics
