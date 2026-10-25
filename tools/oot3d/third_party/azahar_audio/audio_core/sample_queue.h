// TriAevum host storage adapter. Audio algorithms remain Azahar's.
// Licensed under GPLv2 or any later version.
#pragma once

#include "audio_core/audio_types.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace AudioCore {

// One decoded source buffer, consumed without copying its unread tail.
// Serialized state contains unread samples only, never this host cursor.
//
// The storage is reused from one decoded buffer to the next (Overwrite): the DSP mix runs on the
// game thread and the original per-buffer vector cost an allocation, a zero fill of every sample
// and a free each time a buffer (or a loop iteration) was dequeued. Only the unread samples are
// observable, so the reuse changes no value.
//
// The queue also remembers which ADPCM buffer its storage holds the decode of (RememberAdpcm):
// decoding is a pure function of the encoded bytes, the sample count, the coefficients and the
// starting predictor state, so when a looping buffer comes round again with the same inputs and
// byte-identical encoded data (ReplayAdpcm compares them), the stored samples and the final
// predictor state are exactly what a new decode would produce.
class DecodedSampleQueue {
public:
    using Sample = StereoBuffer16::value_type;

    /// Everything an ADPCM decode depends on besides the encoded bytes.
    struct AdpcmOrigin {
        u32 address = 0;
        u32 length = 0;
        std::array<s16, 16> coefficients{};
        s16 yn1 = 0;
        s16 yn2 = 0;
        bool operator==(const AdpcmOrigin&) const = default;
    };

    DecodedSampleQueue() = default;
    DecodedSampleQueue(const StereoBuffer16& samples) { Assign(samples.data(), samples.size()); }
    DecodedSampleQueue(const DecodedSampleQueue& other) {
        const auto remaining = other.Remaining();
        Assign(remaining.data(), remaining.size());
    }
    DecodedSampleQueue& operator=(const DecodedSampleQueue& other) {
        if (this != &other) {
            const auto remaining = other.Remaining();
            if (remaining.size() > capacity_) {
                DecodedSampleQueue copy(other);
                *this = std::move(copy);
            } else {
                std::copy(remaining.begin(), remaining.end(), data_.get());
                size_ = remaining.size();
                first_ = 0;
                remembered_ = false;
            }
        }
        return *this;
    }
    DecodedSampleQueue(DecodedSampleQueue&& other) noexcept
        : data_(std::move(other.data_)), size_(std::exchange(other.size_, 0)),
          capacity_(std::exchange(other.capacity_, 0)), first_(std::exchange(other.first_, 0)),
          remembered_(std::exchange(other.remembered_, false)), origin_(other.origin_),
          end_yn1_(other.end_yn1_), end_yn2_(other.end_yn2_), decoded_(other.decoded_),
          encoded_(std::move(other.encoded_)) {}
    DecodedSampleQueue& operator=(DecodedSampleQueue&& other) noexcept {
        if (this != &other) {
            data_ = std::move(other.data_);
            size_ = std::exchange(other.size_, 0);
            capacity_ = std::exchange(other.capacity_, 0);
            first_ = std::exchange(other.first_, 0);
            remembered_ = std::exchange(other.remembered_, false);
            origin_ = other.origin_;
            end_yn1_ = other.end_yn1_;
            end_yn2_ = other.end_yn2_;
            decoded_ = other.decoded_;
            encoded_ = std::move(other.encoded_);
        }
        return *this;
    }

    DecodedSampleQueue& operator=(const StereoBuffer16& samples) {
        Sample* destination = Overwrite(samples.size());
        std::copy(samples.begin(), samples.end(), destination);
        return *this;
    }

    /// Replaces the contents with `count` samples the caller writes through the returned pointer
    /// (all of them, before any other use of the queue). The cursor restarts at the first sample.
    Sample* Overwrite(std::size_t count) {
        // Keep the storage unless it is far larger than needed (a long buffer played once).
        if (count > capacity_ || (capacity_ > kShrinkAbove && count < capacity_ / 4)) {
            data_.reset();
            capacity_ = 0;
            if (count != 0) data_.reset(new Sample[count]); // Default-initialized: no zero fill.
            capacity_ = count;
        }
        size_ = count;
        first_ = 0;
        remembered_ = false;
        return data_.get();
    }

    /// Records that the storage now holds the whole decode of `bytes` encoded bytes with `origin`,
    /// ending in the predictor state (yn1, yn2). Long buffers are not remembered.
    void RememberAdpcm(const AdpcmOrigin& origin, const u8* encoded, std::size_t bytes, s16 yn1,
                       s16 yn2) {
        remembered_ = false;
        if (bytes > kRememberBytes || first_ != 0) return;
        origin_ = origin;
        end_yn1_ = yn1;
        end_yn2_ = yn2;
        encoded_.assign(encoded, encoded + bytes);
        decoded_ = size_;
        remembered_ = true;
    }

    /// If the storage holds the decode of exactly these inputs, makes it the queue's contents
    /// again (cursor at the first sample), returns the final predictor state and true.
    bool ReplayAdpcm(const AdpcmOrigin& origin, const u8* encoded, std::size_t bytes, s16& yn1,
                     s16& yn2) {
        if (!remembered_ || !(origin == origin_) || bytes != encoded_.size() ||
            (bytes != 0 && std::memcmp(encoded, encoded_.data(), bytes) != 0)) {
            return false;
        }
        size_ = decoded_;
        first_ = 0;
        yn1 = end_yn1_;
        yn2 = end_yn2_;
        return true;
    }

    std::span<const Sample> Remaining() const noexcept {
        return std::span<const Sample>(data_.get() + first_, size_ - first_);
    }
    const Sample* data() const noexcept { return data_.get() + first_; }
    std::size_t size() const noexcept { return size_ - first_; }
    bool empty() const noexcept { return first_ == size_; }
    const Sample& operator[](std::size_t index) const noexcept { return data_[first_ + index]; }
    auto begin() const noexcept { return Remaining().begin(); }
    auto end() const noexcept { return Remaining().end(); }
    void clear() noexcept {
        size_ = 0;
        first_ = 0;
        remembered_ = false;
    }

    void Consume(std::size_t count) {
        if (count > size()) throw std::out_of_range("decoded audio sample cursor");
        first_ += count;
    }

    StereoBuffer16 CopyRemaining() const { return {begin(), end()}; }

private:
    static constexpr std::size_t kShrinkAbove = std::size_t{1} << 16U; // Samples (256 KiB).
    static constexpr std::size_t kRememberBytes = std::size_t{64} << 10U;

    void Assign(const Sample* samples, std::size_t count) {
        std::copy(samples, samples + count, Overwrite(count));
    }

    std::unique_ptr<Sample[]> data_;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    std::size_t first_ = 0;
    // Host-only memo of the ADPCM decode held in data_[0, decoded_) (never serialized).
    bool remembered_ = false;
    AdpcmOrigin origin_{};
    s16 end_yn1_ = 0;
    s16 end_yn2_ = 0;
    std::size_t decoded_ = 0;
    std::vector<u8> encoded_;
};
} // namespace AudioCore
