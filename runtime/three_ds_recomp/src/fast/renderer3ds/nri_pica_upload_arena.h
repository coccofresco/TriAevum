#pragma once

#ifdef ENABLE_RENDERER3DS_NRI

#include "fast/renderer3ds/pica_nri_upload.h"

#include <NRI.h>

#include <cstdint>
#include <span>

namespace Fast::Renderer3ds {

class NriPicaUploadArena final {
  public:
    NriPicaUploadArena() = default;
    ~NriPicaUploadArena();
    NriPicaUploadArena(const NriPicaUploadArena&) = delete;
    NriPicaUploadArena& operator=(const NriPicaUploadArena&) = delete;

    bool Configure(nri::CoreInterface& core, nri::Device& device,
                   uint64_t uniformSize, uint64_t vertexSize);
    bool Copy(std::span<const uint8_t> uniformSource,
              std::span<const PicaNriUploadRange> uniformRanges,
              std::span<const uint8_t> vertexSource,
              std::span<const PicaNriUploadRange> vertexRanges,
              uint64_t& copiedBytes);
    void Shutdown();

    [[nodiscard]] bool Matches(uint64_t uniformSize,
                               uint64_t vertexSize) const;
    [[nodiscard]] nri::Buffer* UniformBuffer() const;
    [[nodiscard]] nri::Buffer* VertexBuffer() const;

  private:
    nri::CoreInterface* mCore = nullptr;
    nri::Buffer* mUniformBuffer = nullptr;
    nri::Buffer* mVertexBuffer = nullptr;
    uint8_t* mUniformMapped = nullptr;
    uint8_t* mVertexMapped = nullptr;
    uint64_t mUniformSize = 0;
    uint64_t mVertexSize = 0;
};

} // namespace Fast::Renderer3ds

#endif
