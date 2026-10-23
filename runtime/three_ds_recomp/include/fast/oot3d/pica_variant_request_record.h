#pragma once

#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/pica_shader_pipeline_cache.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Fast::Oot3d {

// Requests of the PICA variant cache as a game session resolved them (issue
// #76 strategy, "record requests, not text"). A request line holds every
// input of PicaShaderPipelineCache::Resolve except the canonical sources,
// hooks and typed vertex program, which offline tools regenerate from the
// raw PICA state recorded under the same canonical pipeline id. Effects are
// stored once per distinct value. The canonical inputs and the generated
// outputs are witnesses for equivalence checks, never inputs. Developer
// evidence only: recordings are game-derived and private.
inline constexpr std::string_view kPicaVariantRequestRecordFormat = "oot3d_pica_variant_requests_v1";

struct PicaVariantShaderIdentity {
    uint64_t Id = 0;
    uint64_t SecondaryHash = 0;
    uint64_t Size = 0;

    bool operator==(const PicaVariantShaderIdentity&) const = default;
};

struct PicaVariantRequestWitness {
    PicaVariantShaderIdentity CanonicalVertex;
    PicaVariantShaderIdentity CanonicalFragment;
    bool TypedVertexValid = false;
    bool TypedVertexSurface = false;
    bool TypedVertexTransform = false;
    PicaVariantShaderIdentity Vertex;
    PicaVariantShaderIdentity Fragment;
    uint64_t VertexKey = 0;
    uint64_t FragmentKey = 0;
    uint32_t RequestedFeatures = 0;
    uint32_t AppliedFeatures = 0;

    bool operator==(const PicaVariantRequestWitness&) const = default;
};

// A recorded request. Request holds no sources, hooks or typed program: the
// replay binds them from the regenerated canonical plan.
struct PicaVariantRequestRecord {
    uint64_t CanonicalPipelineId = 0;
    uint32_t EffectsId = 0;
    // Provenance only: the settings revision the runtime resolved it under.
    uint64_t SettingsRevision = 0;
    PicaShaderPipelineRequest Request;
    PicaVariantRequestWitness Witness;
};

struct PicaVariantRequestSession {
    bool FaceNormalGuideDiagnostic = false;
    std::map<uint32_t, EffectsSettings> Effects;
    std::vector<PicaVariantRequestRecord> Requests;
};

[[nodiscard]] PicaVariantRequestWitness WitnessPicaVariantRequest(
    const PicaShaderPipelineRequest& request, const PicaShaderPipelineResult& result);
[[nodiscard]] nlohmann::json EncodePicaVariantRequest(uint64_t canonicalPipelineId, uint32_t effectsId,
                                                      uint64_t settingsRevision,
                                                      const PicaShaderPipelineRequest& request,
                                                      const PicaVariantRequestWitness& witness);
bool DecodePicaVariantRequest(const nlohmann::json& line, PicaVariantRequestRecord& record,
                              std::string* error = nullptr);
// EffectsSettings through the settings persistence (the "Graphics" block),
// with the session's other settings and preset Custom for context. They are
// the effective effects the cache resolved, so decoding reads the fields
// without settings validation and requires the Effects block to round-trip.
[[nodiscard]] nlohmann::json EncodePicaVariantEffects(const EffectsSettings& effects,
                                                      const GraphicsSettings& context = {});
bool DecodePicaVariantEffects(const nlohmann::json& value, EffectsSettings& effects,
                              std::string* error = nullptr);
bool LoadPicaVariantRequests(const std::filesystem::path& path, PicaVariantRequestSession& session,
                             std::string* error = nullptr);

// Streams the distinct requests of a session as JSON lines, flushed as they
// are written. Observe it at every Resolve, cache hits included.
class PicaVariantRequestRecorder final {
  public:
    bool Configure(const std::filesystem::path& path, std::string* error = nullptr);
    [[nodiscard]] bool Enabled() const noexcept { return mStream.is_open(); }
    void Observe(uint64_t canonicalPipelineId, const PicaShaderPipelineRequest& request,
                 const EffectsSettings& effects, const GraphicsSettings& context,
                 uint64_t settingsRevision, const PicaShaderPipelineResult& result);
    [[nodiscard]] size_t RequestCount() const noexcept { return mRequestCount; }

  private:
    uint32_t EffectsId(const EffectsSettings& effects, const GraphicsSettings& context,
                       uint64_t settingsRevision);

    std::ofstream mStream;
    std::unordered_set<std::string> mSeen;
    std::map<std::string, uint32_t> mEffectsIds;
    const EffectsSettings* mLastEffects = nullptr;
    uint64_t mLastRevision = 0;
    uint32_t mLastEffectsId = 0;
    size_t mRequestCount = 0;
};

} // namespace Fast::Oot3d
