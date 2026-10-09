#pragma once

// Developer diagnostics of the per-pixel surface replacement (lighting lab):
// a trace of the candidate draws of the next frames, one JSON line each, and
// an exemption of a range of candidate ordinals (counted per frame in draw
// order), to find which native draw an effect lost in Modern belongs to.
// Off unless requested; the render thread reads, the game thread requests.
// Compiled into developer builds only (TRIAEVUM_RENDERER_DEVELOPER_TOOLS at
// the call sites).

#include "fast/renderer3ds/pica_tev_program.h"

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace Fast::Renderer3ds {

class PicaSurfaceDebug {
public:
    static PicaSurfaceDebug& Instance() {
        static PicaSurfaceDebug instance;
        return instance;
    }

    // Game side.
    void RequestTrace(std::string path, uint32_t frames) {
        std::scoped_lock lock(mMutex);
        mRequestedPath = std::move(path);
        mRequestedFrames = frames;
    }
    // Candidates first..last (inclusive) keep their native appearance from the
    // next frame on (the range is latched whole per frame); first > last
    // exempts none.
    void SetExemptRange(uint32_t first, uint32_t last) {
        std::scoped_lock lock(mMutex);
        mPendingFirst = first;
        mPendingLast = last;
    }

    // Render side: once per frame, before its draws.
    void BeginFrame() {
        std::scoped_lock lock(mMutex);
        mOrdinal = 0;
        ++mFrame;
        mExemptFirst = mPendingFirst;
        mExemptLast = mPendingLast;
        if (mFile != nullptr && --mFramesLeft == 0U) {
            std::fclose(mFile);
            mFile = nullptr;
        }
        if (mFile == nullptr && mRequestedFrames != 0U) {
            mFile = std::fopen(mRequestedPath.c_str(), "ab");
            if (mFile == nullptr)
                std::fprintf(stderr, "TRIAEVUM_SURFACE_TRACE cannot open %s\n", mRequestedPath.c_str());
            mPath = mRequestedPath;
            mFramesLeft = mRequestedFrames;
            mRequestedFrames = 0;
        }
    }
    uint32_t NextOrdinal() { return mOrdinal++; }
    // Render thread only, between BeginFrame calls.
    [[nodiscard]] bool Exempt(uint32_t ordinal) const {
        return ordinal >= mExemptFirst && ordinal <= mExemptLast;
    }
    [[nodiscard]] bool Tracing() {
        std::scoped_lock lock(mMutex);
        return mFile != nullptr;
    }
    [[nodiscard]] uint64_t Frame() const { return mFrame; }
    void Write(const std::string& line) {
        std::scoped_lock lock(mMutex);
        if (mFile == nullptr) return;
        std::fwrite(line.data(), 1, line.size(), mFile);
        std::fputc('\n', mFile);
        std::fflush(mFile);
    }

    void WriteSource(uint32_t ordinal, std::string_view vertex, std::string_view fragment) {
        std::scoped_lock lock(mMutex);
        if (mFile == nullptr) return;
        const std::string base = mPath + "." + std::to_string(ordinal);
        for (const auto& [suffix, text] : {std::pair{".vert.glsl", vertex}, std::pair{".frag.glsl", fragment}}) {
            if (std::FILE* file = std::fopen((base + suffix).c_str(), "wb")) {
                std::fwrite(text.data(), 1, text.size(), file);
                std::fclose(file);
            }
        }
    }

private:
    std::mutex mMutex;
    std::string mPath;
    std::string mRequestedPath;
    uint32_t mRequestedFrames = 0;
    std::FILE* mFile = nullptr;
    uint32_t mFramesLeft = 0;
    uint32_t mOrdinal = 0;
    uint64_t mFrame = 0;
    uint32_t mPendingFirst = 1U;
    uint32_t mPendingLast = 0U;
    uint32_t mExemptFirst = 1U;
    uint32_t mExemptLast = 0U;
};

// Diagnostic only: the TEV sources the final RGB reads (bit = native source
// selector: 0 vertex primary, 1 fragment primary, 2 fragment secondary, 3-6
// textures, 14 constant), through previous/buffer dependencies. It follows
// sources, not operands: an RGB built from a previous stage's alpha reports
// that stage's RGB sources. The initial buffer is zero (no bit).
[[nodiscard]] inline uint16_t PicaTevColorSourceMask(const PicaTevProgram& program) noexcept {
    uint16_t previous = 0U, buffer = 0U, nextBuffer = 0U;
    for (size_t stage = 0; stage < program.Stages.size(); ++stage) {
        const auto& words = program.Stages[stage];
        const auto operation = words[2] & 15U;
        const auto inputs = operation == 0U ? 1U : (operation == 4U || operation == 8U || operation == 9U) ? 3U : 2U;
        uint16_t color = 0U;
        for (uint32_t input = 0U; input < inputs; ++input) {
            auto source = (words[0] >> (input * 4U)) & 15U;
            if (stage == 0U && input < 2U && source == 15U) source = (words[0] >> 8U) & 15U;
            if (source == 13U) color |= buffer;
            else if (source == 15U) color |= previous;
            else color |= static_cast<uint16_t>(1U << source);
        }
        previous = color;
        buffer = nextBuffer;
        if (stage < 4U && (program.Control[0] & (1U << (8U + stage))) != 0U) nextBuffer = color;
    }
    return previous;
}

} // namespace Fast::Renderer3ds
