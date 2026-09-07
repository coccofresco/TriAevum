// TriAevum host storage adapter. Audio algorithms remain Azahar's.
// Licensed under GPLv2 or any later version.
#pragma once

#include "audio_core/audio_types.h"

#include <span>
#include <stdexcept>
#include <utility>

namespace AudioCore {

// One decoded source buffer, consumed without copying its unread tail.
// Serialized state contains unread samples only, never this host cursor.
class DecodedSampleQueue {
public:
    using Sample = StereoBuffer16::value_type;

    DecodedSampleQueue() = default;
    DecodedSampleQueue(StereoBuffer16 samples) : samples_(std::move(samples)) {}
    DecodedSampleQueue(const DecodedSampleQueue& other) : samples_(other.CopyRemaining()) {}
    DecodedSampleQueue& operator=(const DecodedSampleQueue& other) {
        if (this != &other) *this = other.CopyRemaining();
        return *this;
    }
    DecodedSampleQueue(DecodedSampleQueue&& other) noexcept
        : samples_(std::move(other.samples_)), first_(std::exchange(other.first_, 0)) {
        other.samples_.clear();
    }
    DecodedSampleQueue& operator=(DecodedSampleQueue&& other) noexcept {
        if (this != &other) {
            samples_ = std::move(other.samples_);
            first_ = std::exchange(other.first_, 0);
            other.samples_.clear();
        }
        return *this;
    }

    DecodedSampleQueue& operator=(StereoBuffer16 samples) {
        samples_ = std::move(samples);
        first_ = 0;
        return *this;
    }

    std::span<const Sample> Remaining() const noexcept {
        return std::span<const Sample>(samples_).subspan(first_);
    }
    std::size_t size() const noexcept { return samples_.size() - first_; }
    bool empty() const noexcept { return first_ == samples_.size(); }
    const Sample& operator[](std::size_t index) const noexcept { return samples_[first_ + index]; }
    auto begin() const noexcept { return Remaining().begin(); }
    auto end() const noexcept { return Remaining().end(); }
    void clear() noexcept { samples_.clear(); first_ = 0; }

    void Consume(std::size_t count) {
        if (count > size()) throw std::out_of_range("decoded audio sample cursor");
        first_ += count;
    }

    StereoBuffer16 CopyRemaining() const { return {begin(), end()}; }

private:
    StereoBuffer16 samples_;
    std::size_t first_ = 0;
};
} // namespace AudioCore
