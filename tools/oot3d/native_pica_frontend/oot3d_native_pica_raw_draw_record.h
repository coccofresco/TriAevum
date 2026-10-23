#pragma once

#include "oot3d_native_pica_frontend.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Oot3dNativeGame {

// Raw PICA state of recorded draws (issue #76 strategy, "record requests,
// not text"). One record per canonical pipeline holds everything the native
// shader generators read: registers, the vertex and geometry programs with
// their uniforms, the LUTs and the default attributes. Programs and LUTs are
// stored once per content. Offline tools decode the packet and rebuild the
// canonical sources with the generators of the build that consumes them, so
// a recording stays valid across generator changes. Developer evidence only:
// recordings are game-derived and private.
inline constexpr std::string_view kOot3dPicaRawDrawRecordFormat = "oot3d_pica_raw_draws_v1";

// The canonical shaders the recording runtime generated for the draw: a
// witness for equivalence checks, never an input.
struct Oot3dPicaRawDrawWitness {
    uint64_t VertexShaderKey = 0;
    uint64_t FragmentShaderKey = 0;
    uint64_t VertexSourceId = 0;
    uint64_t VertexSourceSecondaryHash = 0;
    uint64_t VertexSourceSize = 0;
    uint64_t FragmentSourceId = 0;
    uint64_t FragmentSourceSecondaryHash = 0;
    uint64_t FragmentSourceSize = 0;

    bool operator==(const Oot3dPicaRawDrawWitness&) const = default;
};

struct Oot3dPicaRawDraw {
    uint64_t CanonicalPipelineId = 0;
    Oot3dPicaDrawPacket Packet;
    Oot3dPicaRawDrawWitness Witness;
};

// How the recording runtime generated canonical sources: replay must use the
// same source cache modes (Oot3dPicaVulkanShaderSourceCache).
struct Oot3dPicaRawDrawSession {
    bool ParametricTev = false;
    bool UberFragments = false;
    std::vector<Oot3dPicaRawDraw> Draws;
};

// Streams a session's raw draws as JSON lines. The first observation of a
// canonical pipeline id is written; later ones are ignored, since the id
// determines the canonical sources. Lines are flushed as they are written,
// so an interrupted session keeps every complete record.
class Oot3dPicaRawDrawRecorder final {
  public:
    bool Configure(const std::filesystem::path& path, bool parametricTev, bool uberFragments,
                   std::string* error = nullptr);
    [[nodiscard]] bool Enabled() const noexcept { return mStream.is_open(); }
    [[nodiscard]] bool Seen(uint64_t canonicalPipelineId) const noexcept {
        return mDraws.contains(canonicalPipelineId);
    }
    void Observe(uint64_t canonicalPipelineId, const Oot3dPicaDrawPacket& packet,
                 const Oot3dPicaRawDrawWitness& witness);
    [[nodiscard]] size_t DrawCount() const noexcept { return mDraws.size(); }

  private:
    std::string Resource(std::string_view kind, std::string payload);

    std::ofstream mStream;
    std::unordered_set<uint64_t> mDraws;
    std::unordered_map<std::string, std::string> mResources;
};

// Lossless encode/decode of one draw line, and of a whole session file.
// Resources maps the resource ids of the file to their payloads.
nlohmann::json EncodeOot3dPicaRawDraw(
    const Oot3dPicaRawDraw& draw,
    const std::function<std::string(std::string_view, std::string)>& resource);
bool DecodeOot3dPicaRawDraw(const nlohmann::json& line,
                            const std::map<std::string, std::string>& resources,
                            Oot3dPicaRawDraw& draw, std::string* error = nullptr);
bool LoadOot3dPicaRawDraws(const std::filesystem::path& path,
                           Oot3dPicaRawDrawSession& session,
                           std::string* error = nullptr);

} // namespace Oot3dNativeGame
