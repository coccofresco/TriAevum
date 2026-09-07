#include "nri_pica_upload_arena.h"

#ifdef ENABLE_RENDERER3DS_NRI

#include <limits>

namespace Fast::Renderer3ds {

NriPicaUploadArena::~NriPicaUploadArena() { Shutdown(); }

bool NriPicaUploadArena::Configure(
    nri::CoreInterface& core, nri::Device& device,
    uint64_t uniformSize, uint64_t vertexSize) {
    if (uniformSize == 0U || vertexSize == 0U)
        return false;
    if (Matches(uniformSize, vertexSize))
        return true;

    Shutdown();
    mCore = &core;
    nri::BufferDesc uniform{};
    uniform.size = uniformSize;
    uniform.usage = nri::BufferUsageBits::CONSTANT_BUFFER;
    if (core.CreateCommittedBuffer(
            device, nri::MemoryLocation::HOST_UPLOAD, 0.0F,
            uniform, mUniformBuffer) != nri::Result::SUCCESS) {
        Shutdown();
        return false;
    }
    mUniformMapped = static_cast<uint8_t*>(
        core.MapBuffer(*mUniformBuffer, 0, uniformSize));
    if (mUniformMapped == nullptr) {
        Shutdown();
        return false;
    }

    nri::BufferDesc vertex{};
    vertex.size = vertexSize;
    vertex.usage = nri::BufferUsageBits::VERTEX_BUFFER |
                   nri::BufferUsageBits::INDEX_BUFFER;
    if (core.CreateCommittedBuffer(
            device, nri::MemoryLocation::HOST_UPLOAD, 0.0F,
            vertex, mVertexBuffer) != nri::Result::SUCCESS) {
        Shutdown();
        return false;
    }
    mVertexMapped = static_cast<uint8_t*>(
        core.MapBuffer(*mVertexBuffer, 0, vertexSize));
    if (mVertexMapped == nullptr) {
        Shutdown();
        return false;
    }
    mUniformSize = uniformSize;
    mVertexSize = vertexSize;
    return true;
}

bool NriPicaUploadArena::Copy(
    std::span<const uint8_t> uniformSource,
    std::span<const PicaNriUploadRange> uniformRanges,
    std::span<const uint8_t> vertexSource,
    std::span<const PicaNriUploadRange> vertexRanges,
    uint64_t& copiedBytes) {
    copiedBytes = 0;
    if (mUniformMapped == nullptr || mVertexMapped == nullptr ||
        mUniformSize > std::numeric_limits<size_t>::max() ||
        mVertexSize > std::numeric_limits<size_t>::max())
        return false;
    uint64_t uniformBytes = 0;
    uint64_t vertexBytes = 0;
    if (!CopyPicaNriUploadRanges(
            uniformSource,
            std::span<uint8_t>(
                mUniformMapped, static_cast<size_t>(mUniformSize)),
            uniformRanges, &uniformBytes) ||
        !CopyPicaNriUploadRanges(
            vertexSource,
            std::span<uint8_t>(
                mVertexMapped, static_cast<size_t>(mVertexSize)),
            vertexRanges, &vertexBytes))
        return false;
    if (vertexBytes >
        std::numeric_limits<uint64_t>::max() - uniformBytes)
        return false;
    copiedBytes = uniformBytes + vertexBytes;
    return true;
}

void NriPicaUploadArena::Shutdown() {
    if (mCore != nullptr && mUniformBuffer != nullptr) {
        if (mUniformMapped != nullptr)
            mCore->UnmapBuffer(*mUniformBuffer);
        mCore->DestroyBuffer(mUniformBuffer);
    }
    if (mCore != nullptr && mVertexBuffer != nullptr) {
        if (mVertexMapped != nullptr)
            mCore->UnmapBuffer(*mVertexBuffer);
        mCore->DestroyBuffer(mVertexBuffer);
    }
    mCore = nullptr;
    mUniformBuffer = nullptr;
    mVertexBuffer = nullptr;
    mUniformMapped = nullptr;
    mVertexMapped = nullptr;
    mUniformSize = 0;
    mVertexSize = 0;
}

bool NriPicaUploadArena::Matches(
    uint64_t uniformSize, uint64_t vertexSize) const {
    return mUniformBuffer != nullptr &&
           mVertexBuffer != nullptr &&
           mUniformMapped != nullptr &&
           mVertexMapped != nullptr &&
           mUniformSize == uniformSize &&
           mVertexSize == vertexSize;
}

nri::Buffer* NriPicaUploadArena::UniformBuffer() const {
    return mUniformBuffer;
}

nri::Buffer* NriPicaUploadArena::VertexBuffer() const {
    return mVertexBuffer;
}

} // namespace Fast::Renderer3ds

#endif
