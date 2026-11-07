#pragma once
// Developer-only acceptance input. Uses SDL's real event/device paths, never
// changes a control setting, prompt family, guest state or widget return value.
// Receipts distinguish injected SDL devices/events from physical hardware.
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
#include "oot3d_native_control_config.h"
#include <SDL.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <set>
#ifdef _WIN32
#include <windows.h>
#endif
namespace Oot3dNativeGame {
inline uint64_t OverlayAcceptanceFrame=0;
inline void ObserveOverlayAcceptanceTouch(uint32_t held,uint32_t pressed,bool gui) {
    const char* path=std::getenv("TRIAEVUM_OVERLAY_ACCEPTANCE");if(!path||!*path)return;
    static std::ofstream out(std::string(path)+".touch.jsonl");
    static uint32_t previous=~0u;
    if(held!=previous||pressed){out<<nlohmann::json{{"frame",OverlayAcceptanceFrame},{"held",held},{"pressed",pressed},{"host_gui",gui}}.dump()<<'\n';out.flush();previous=held;}
}
inline void DriveOverlayAcceptance(uint64_t frame, NativeControlConfigRuntime& controls) {
    OverlayAcceptanceFrame=frame;
    static const char* path=std::getenv("TRIAEVUM_OVERLAY_ACCEPTANCE");
    if(!path || !*path)return;
    struct State {
        std::ofstream out;
        std::set<std::string> done;
        SDL_Joystick* pad=nullptr;
        std::string last;
        explicit State(const char* p):out(std::string(p)+".receipt.jsonl",std::ios::app){}
        // SDL_Quit owns final device teardown; no SDL calls from static
        // destruction after the host has already shut SDL down.
    };
    static State s(path);
    const auto emit=[&](nlohmann::json j){j["frame"]=frame;s.out<<j.dump()<<'\n';s.out.flush();};
    if(frame%10==0)try {
        // Reload allows a bounded interactive widget test in ONE game process.
        // Each action has a unique id and is performed at most once.
        std::ifstream in(path);nlohmann::json script;in>>script;
        if(!script.is_array()||script.size()>256)throw std::runtime_error("expected <=256 actions");
        for(const auto& step:script) {
            const auto id=step.at("id").get<std::string>();
            if(s.done.contains(id)||step.at("frame").get<uint64_t>()>frame)continue;
            s.done.insert(id);
            auto* window=SDL_GetKeyboardFocus();if(!window)window=SDL_GetWindowFromID(1);
            const auto wid=window?SDL_GetWindowID(window):0;
            const auto op=step.at("op").get<std::string>();int result=0;
            if(op=="key") {
                SDL_Event e{};e.type=step.value("down",true)?SDL_KEYDOWN:SDL_KEYUP;
                e.key.windowID=wid;e.key.state=e.type==SDL_KEYDOWN?SDL_PRESSED:SDL_RELEASED;
                e.key.keysym.scancode=SDL_GetScancodeFromName(step.at("key").get<std::string>().c_str());
                e.key.keysym.sym=SDL_GetKeyFromScancode(e.key.keysym.scancode);
#ifdef _WIN32
                // OS delivery updates SDL's keyboard state as well as its event
                // stream (PushEvent alone cannot test last-active keyboard).
                WORD vk=0;
                if(e.key.keysym.scancode>=SDL_SCANCODE_F1&&e.key.keysym.scancode<=SDL_SCANCODE_F12)
                    vk=VK_F1+e.key.keysym.scancode-SDL_SCANCODE_F1;
                else if(e.key.keysym.sym==SDLK_ESCAPE)vk=VK_ESCAPE;
                else if(e.key.keysym.sym==SDLK_RETURN)vk=VK_RETURN;
                else if(e.key.keysym.sym==SDLK_TAB)vk=VK_TAB;
                else if(e.key.keysym.sym==SDLK_BACKSPACE)vk=VK_BACK;
                else if(e.key.keysym.sym==SDLK_UP)vk=VK_UP;
                else if(e.key.keysym.sym==SDLK_DOWN)vk=VK_DOWN;
                else if(e.key.keysym.sym==SDLK_LEFT)vk=VK_LEFT;
                else if(e.key.keysym.sym==SDLK_RIGHT)vk=VK_RIGHT;
                else if(e.key.keysym.sym>0&&e.key.keysym.sym<128)vk=LOBYTE(VkKeyScanW(wchar_t(e.key.keysym.sym)));
                if(!vk)throw std::runtime_error("unsupported OS acceptance key");
                SDL_SetWindowInputFocus(window);
                INPUT input{};input.type=INPUT_KEYBOARD;input.ki.wVk=vk;
                if(e.type==SDL_KEYUP)input.ki.dwFlags=KEYEVENTF_KEYUP;
                result=int(SendInput(1,&input,sizeof(input)));
#else
                result=SDL_PushEvent(&e);
#endif
            } else if(op=="mouse") {
                const int x=step.at("x"),y=step.at("y");
                SDL_WarpMouseInWindow(window,x,y);
                SDL_Event motion{};motion.type=SDL_MOUSEMOTION;motion.motion.windowID=wid;motion.motion.x=x;motion.motion.y=y;SDL_PushEvent(&motion);
                SDL_Event e{};e.type=step.value("down",true)?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;
                e.button.windowID=wid;e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;
                e.button.state=e.type==SDL_MOUSEBUTTONDOWN?SDL_PRESSED:SDL_RELEASED;result=SDL_PushEvent(&e);
            } else if(op=="attach") {
                if(s.pad)throw std::runtime_error("virtual pad already attached");
                SDL_VirtualJoystickDesc d{};d.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
                d.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;d.naxes=SDL_CONTROLLER_AXIS_MAX;d.nbuttons=SDL_CONTROLLER_BUTTON_MAX;
                d.vendor_id=0x045e;d.product_id=0x028e;d.name="OVERLAY acceptance virtual Xbox (not hardware)";
                d.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;d.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1;
                result=SDL_JoystickAttachVirtualEx(&d);if(result<0)throw std::runtime_error(SDL_GetError());
                s.pad=SDL_JoystickOpen(result);
                if(!s.pad)throw std::runtime_error(SDL_GetError());
                SDL_JoystickSetVirtualAxis(s.pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT,-32768);
                SDL_JoystickSetVirtualAxis(s.pad,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,-32768);
            } else if(op=="detach") {
                if(!s.pad)throw std::runtime_error("no virtual pad");
                int index=-1;const auto id=SDL_JoystickInstanceID(s.pad);
                for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_JoystickGetDeviceInstanceID(i)==id)index=i;
                SDL_JoystickClose(s.pad);s.pad=nullptr;result=SDL_JoystickDetachVirtual(index);
            } else if(op=="button") {
                if(!s.pad)throw std::runtime_error("no virtual pad");
                const auto button=SDL_GameControllerGetButtonFromString(step.at("button").get<std::string>().c_str());
                result=SDL_JoystickSetVirtualButton(s.pad,button,step.value("down",true)?1:0);
            } else if(op=="finger") {
                SDL_Event e{};e.type=step.value("down",true)?SDL_FINGERDOWN:SDL_FINGERUP;
                e.tfinger.windowID=wid;e.tfinger.touchId=42;e.tfinger.fingerId=step.value("finger",1);
                e.tfinger.x=step.at("x");e.tfinger.y=step.at("y");e.tfinger.pressure=step.value("down",true)?1.f:0.f;
                result=SDL_PushEvent(&e);
            } else if(op=="quit") {
                SDL_Event e{};e.type=SDL_QUIT;result=SDL_PushEvent(&e);
            } else if(op!="checkpoint")throw std::runtime_error("unknown input operation");
            emit({{"injected",step},{"result",result},{"source","SDL virtual device/event; not physical hardware"}});
        }
    } catch(const std::exception& e){emit({{"error",e.what()}});}
    const auto snapshot=controls.Snapshot();std::string config,error;
    SerializeNativeControlConfigText(snapshot.Config,&config,&error);
    auto observed=nlohmann::json{{"prompt",ThreeDsRecomp::Input::PromptSetLabel(controls.ActivePromptSet())},
        {"automatic",ThreeDsRecomp::Input::PromptSetLabel(controls.AutomaticPromptSet())},
        {"controls",nlohmann::json::parse(config)}};
    const auto text=observed.dump();
    if(text!=s.last || frame%100==0){emit({{"observed",observed}});s.last=text;}
}
}
#endif
