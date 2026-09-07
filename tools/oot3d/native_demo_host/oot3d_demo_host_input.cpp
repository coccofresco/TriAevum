#include "oot3d_demo_host_input.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "fast/Fast3dWindow.h"
#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardScancodes.h"

namespace {

constexpr const char* kInputTimelineSchema = "oot3d.demo_host.input_timeline.v1";

uint32_t ReadFrame(const nlohmann::json& segment, const char* key) {
    if (!segment.contains(key) || !segment.at(key).is_number_unsigned()) {
        throw std::runtime_error(std::string("input timeline segment requires unsigned ") + key);
    }
    const uint64_t value = segment.at(key).get<uint64_t>();
    if (value > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string("input timeline ") + key + " exceeds u32 range");
    }
    return static_cast<uint32_t>(value);
}

double ReadAxis(const nlohmann::json& segment, const char* key) {
    if (!segment.contains(key)) {
        return 0.0;
    }
    if (!segment.at(key).is_number()) {
        throw std::runtime_error(std::string("input timeline ") + key + " must be numeric");
    }
    const double value = segment.at(key).get<double>();
    if (!std::isfinite(value) || value < -1.0 || value > 1.0) {
        throw std::runtime_error(std::string("input timeline ") + key + " must be in [-1, 1]");
    }
    return value;
}

bool ReadButton(const nlohmann::json& segment, const char* key) {
    if (!segment.contains(key)) {
        return false;
    }
    if (!segment.at(key).is_boolean()) {
        throw std::runtime_error(std::string("input timeline ") + key + " must be boolean");
    }
    return segment.at(key).get<bool>();
}

Oot3dDemoHostInputState PollWindowInput(Fast::Fast3dWindow& window) {
    Oot3dDemoHostInputState state;
    const bool moveLeft = window.IsKeyDown(Ship::KbScancode::LUS_KB_J) ||
                          window.IsKeyDown(Ship::KbScancode::LUS_KB_U);
    const bool moveRight = window.IsKeyDown(Ship::KbScancode::LUS_KB_L) ||
                           window.IsKeyDown(Ship::KbScancode::LUS_KB_O);
    state.MoveX = static_cast<double>(moveRight) - static_cast<double>(moveLeft);
    state.MoveY = static_cast<double>(window.IsKeyDown(Ship::KbScancode::LUS_KB_I)) -
                  static_cast<double>(window.IsKeyDown(Ship::KbScancode::LUS_KB_K));
    state.Fast = window.IsKeyDown(Ship::KbScancode::LUS_KB_SHIFT) ||
                 window.IsKeyDown(Ship::KbScancode::LUS_KB_RSHIFT);
    state.Reset = window.IsKeyDown(Ship::KbScancode::LUS_KB_R);
    state.Exit = window.IsKeyDown(Ship::KbScancode::LUS_KB_ESCAPE);
    return state;
}

} // namespace

void Oot3dDemoHostInputDiagnostics::Observe(const Oot3dDemoHostInputState& state) {
    ++SampledFrameCount;
    ScriptedFrameCount += state.Scripted ? 1u : 0u;
    MovementFrameCount +=
        (std::abs(state.MoveX) > 0.0 || std::abs(state.MoveY) > 0.0) ? 1u : 0u;
    FastFrameCount += state.Fast ? 1u : 0u;
    ResetFrameCount += state.Reset ? 1u : 0u;
    LastState = state;
}

Oot3dDemoHostInputTimeline Oot3dDemoHostInputTimeline::LoadFile(
    const std::filesystem::path& path) {
    Oot3dDemoHostInputTimeline timeline;
    if (path.empty()) {
        return timeline;
    }

    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open input timeline: " + path.string());
    }
    nlohmann::json root;
    input >> root;
    if (!root.is_object() || root.value("schema", std::string{}) != kInputTimelineSchema) {
        throw std::runtime_error("unsupported input timeline schema: " + path.string());
    }
    if (!root.contains("segments") || !root.at("segments").is_array() ||
        root.at("segments").empty()) {
        throw std::runtime_error("input timeline requires at least one segment: " + path.string());
    }

    uint32_t previousEnd = 0;
    bool first = true;
    for (const auto& source : root.at("segments")) {
        if (!source.is_object()) {
            throw std::runtime_error("input timeline segment must be an object");
        }
        Segment segment;
        segment.StartFrame = ReadFrame(source, "start_frame");
        segment.EndFrameExclusive = ReadFrame(source, "end_frame_exclusive");
        if (segment.EndFrameExclusive <= segment.StartFrame) {
            throw std::runtime_error("input timeline segment has an empty or reversed frame range");
        }
        if (!first && segment.StartFrame < previousEnd) {
            throw std::runtime_error("input timeline segments must be sorted and non-overlapping");
        }
        segment.MoveX = ReadAxis(source, "move_x");
        segment.MoveY = ReadAxis(source, "move_y");
        segment.Fast = ReadButton(source, "fast");
        segment.Reset = ReadButton(source, "reset");
        timeline.mSegments.push_back(segment);
        previousEnd = segment.EndFrameExclusive;
        first = false;
    }
    timeline.mSourcePath = std::filesystem::absolute(path).lexically_normal();
    return timeline;
}

bool Oot3dDemoHostInputTimeline::Enabled() const {
    return !mSourcePath.empty();
}

size_t Oot3dDemoHostInputTimeline::SegmentCount() const {
    return mSegments.size();
}

const std::filesystem::path& Oot3dDemoHostInputTimeline::SourcePath() const {
    return mSourcePath;
}

Oot3dDemoHostInputState Oot3dDemoHostInputTimeline::Sample(uint32_t frame) const {
    Oot3dDemoHostInputState state;
    state.Scripted = Enabled();
    for (size_t index = 0; index < mSegments.size(); ++index) {
        const auto& segment = mSegments[index];
        if (frame < segment.StartFrame) {
            break;
        }
        if (frame >= segment.EndFrameExclusive) {
            continue;
        }
        state.MoveX = segment.MoveX;
        state.MoveY = segment.MoveY;
        state.Fast = segment.Fast;
        state.Reset = segment.Reset;
        state.ScriptSegmentIndex = static_cast<int32_t>(index);
        break;
    }
    return state;
}

Oot3dDemoHostInputState ResolveOot3dDemoHostInput(
    Fast::Fast3dWindow& window, const Oot3dDemoHostInputTimeline& timeline,
    uint32_t frame) {
    const auto physical = PollWindowInput(window);
    if (!timeline.Enabled()) {
        return physical;
    }
    auto scripted = timeline.Sample(frame);
    scripted.Exit = physical.Exit;
    return scripted;
}
