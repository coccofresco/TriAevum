#pragma once

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ThreeDsRecomp::Input {

// TRIAEVUM_INPUT_TRACE=1 (support diagnostics, issue #123): every key event
// SDL delivers, as SDL saw it (scancode, keycode, repeat, modifiers, which
// window had the keyboard focus), and every keyboard focus change, queued by
// an event watch on the thread that pumps the events and drained by the
// input poll into the game log. It reads events and changes nothing.
class SdlKeyTrace {
  public:
    [[nodiscard]] static bool Requested() {
        const char* value = std::getenv("TRIAEVUM_INPUT_TRACE");
        return value != nullptr && std::string_view(value) == "1";
    }

    SdlKeyTrace() { SDL_AddEventWatch(&Watch, this); }
    ~SdlKeyTrace() { SDL_DelEventWatch(&Watch, this); }
    SdlKeyTrace(const SdlKeyTrace&) = delete;
    SdlKeyTrace& operator=(const SdlKeyTrace&) = delete;

    [[nodiscard]] std::vector<std::string> Drain() {
        std::lock_guard lock(mMutex);
        if (mDropped != 0) {
            mLines.push_back("TRIAEVUM_INPUT_TRACE dropped=" + std::to_string(mDropped));
            mDropped = 0;
        }
        return std::exchange(mLines, {});
    }

  private:
    static constexpr std::size_t kMaximumQueued = 256;

    void Push(std::string line) {
        std::lock_guard lock(mMutex);
        if (mLines.size() < kMaximumQueued) mLines.push_back(std::move(line));
        else ++mDropped;
    }

    static int SDLCALL Watch(void* userdata, SDL_Event* event) {
        auto* self = static_cast<SdlKeyTrace*>(userdata);
        char line[256];
        if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
            const auto& key = event->key;
            SDL_Window* focus = SDL_GetKeyboardFocus();
            std::snprintf(line, sizeof(line),
                          "TRIAEVUM_INPUT_TRACE sdl_key %s scancode=%d (%s) keycode=0x%X (%s) repeat=%d "
                          "mod=0x%04X window=%u focus_window=%u",
                          event->type == SDL_KEYDOWN ? "down" : "up", static_cast<int>(key.keysym.scancode),
                          SDL_GetScancodeName(key.keysym.scancode), static_cast<unsigned>(key.keysym.sym),
                          SDL_GetKeyName(key.keysym.sym), static_cast<int>(key.repeat),
                          static_cast<unsigned>(key.keysym.mod), static_cast<unsigned>(key.windowID),
                          focus != nullptr ? static_cast<unsigned>(SDL_GetWindowID(focus)) : 0U);
            self->Push(line);
        } else if (event->type == SDL_WINDOWEVENT && (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                                                      event->window.event == SDL_WINDOWEVENT_FOCUS_LOST)) {
            std::snprintf(line, sizeof(line), "TRIAEVUM_INPUT_TRACE sdl_focus %s window=%u",
                          event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED ? "gained" : "lost",
                          static_cast<unsigned>(event->window.windowID));
            self->Push(line);
        }
        return 1;
    }

    std::mutex mMutex;
    std::vector<std::string> mLines;
    std::size_t mDropped = 0;
};

} // namespace ThreeDsRecomp::Input
