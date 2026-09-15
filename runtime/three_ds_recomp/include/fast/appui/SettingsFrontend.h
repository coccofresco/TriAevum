#pragma once
#include "SettingsModel.h"
#include <memory>
union SDL_Event;

namespace Fast::AppUi {
// Retained RmlUi document. No ImGui windows or widgets belong to this frontend.
class SettingsFrontend {
public:
    SettingsFrontend();
    ~SettingsFrontend();
    void Open(Pages pages, std::function<void()> close, std::function<void()> advanced);
    void Close();
    bool Visible() const;
    void Event(const SDL_Event& event);
    void Draw();
    void Confirmation(bool active, std::string message, std::function<void()> accept, std::function<void()> reject);
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
}
