#pragma once
// Developer-only acceptance input through SDL, OS input or the platform pad
// boundary. Never forces a control setting, prompt family, guest state or
// widget return value. Synthetic receipts are not physical hardware proof.
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
#include "oot3d_native_control_config.h"
#include <SDL.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <set>
#ifdef _WIN32
#include <windows.h>
#include <SDL_syswm.h>
#endif
#include "three_ds_virtual_pad.h"
namespace Oot3dNativeGame {
inline uint64_t OverlayAcceptanceFrame=0;
inline bool OverlayAcceptanceReleaseTimeline=false;
inline void ObserveOverlayAcceptanceGuest(uint32_t buttons,bool scripted) {
    const char* path=std::getenv("TRIAEVUM_OVERLAY_ACCEPTANCE");if(!path||!*path)return;
    static std::ofstream out(std::string(path)+".guest-input.jsonl");
    static uint32_t previous=~0u;static bool previousScripted=false;
    if(buttons!=previous || scripted!=previousScripted) {
        out<<nlohmann::json{{"frame",OverlayAcceptanceFrame},{"buttons",buttons},{"timeline",scripted}}.dump()<<'\n';
        out.flush();previous=buttons;previousScripted=scripted;
    }
}
inline void ObserveOverlayAcceptanceTouch(uint32_t held,uint32_t pressed,bool gui) {
    const char* path=std::getenv("TRIAEVUM_OVERLAY_ACCEPTANCE");if(!path||!*path)return;
    static std::ofstream out(std::string(path)+".touch.jsonl");
    static uint32_t previous=~0u;
    if(held!=previous||pressed){out<<nlohmann::json{{"frame",OverlayAcceptanceFrame},{"held",held},{"pressed",pressed},{"host_gui",gui}}.dump()<<'\n';out.flush();previous=held;}
}
inline void ObserveOverlayAcceptanceNative(size_t primitives,const nlohmann::json& summary) {
    const char* path=std::getenv("TRIAEVUM_OVERLAY_ACCEPTANCE");if(!path||!*path)return;
    static std::ofstream out(std::string(path)+".overlay.jsonl");
    static size_t previous=~size_t{0};
    if(primitives!=previous || OverlayAcceptanceFrame%100==0) {
        out<<nlohmann::json{{"frame",OverlayAcceptanceFrame},{"primitives",primitives},{"summary",summary}}.dump()<<'\n';out.flush();
        previous=primitives;
    }
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
            if(op=="key" || op=="key_event") {
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
                if(op=="key_event")result=SDL_PushEvent(&e);
                else {
                    SDL_SysWMinfo info{};SDL_VERSION(&info.version);
                    if(!window||!SDL_GetWindowWMInfo(window,&info))throw std::runtime_error("no acceptance window");
                    SetForegroundWindow(info.info.win.window);
                    if(GetForegroundWindow()!=info.info.win.window)throw std::runtime_error("acceptance window is not foreground; refusing global keyboard injection");
                    INPUT input{};input.type=INPUT_KEYBOARD;
                    input.ki.wScan=WORD(MapVirtualKeyW(vk,MAPVK_VK_TO_VSC));
                    input.ki.dwFlags=KEYEVENTF_SCANCODE;
                    if(vk==VK_LEFT||vk==VK_RIGHT||vk==VK_UP||vk==VK_DOWN)input.ki.dwFlags|=KEYEVENTF_EXTENDEDKEY;
                    if(e.type==SDL_KEYUP)input.ki.dwFlags|=KEYEVENTF_KEYUP;
                    result=int(SendInput(1,&input,sizeof(input)));
                }
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
            } else if(op=="os_click") {
#ifdef _WIN32
                SDL_SysWMinfo info{};SDL_VERSION(&info.version);
                if(!window||!SDL_GetWindowWMInfo(window,&info))throw std::runtime_error("no acceptance window");
                const auto hwnd=info.info.win.window;
                if(GetForegroundWindow()!=hwnd)throw std::runtime_error("refusing mouse injection outside owned foreground window");
                int width=0,height=0;SDL_GetWindowSize(window,&width,&height);RECT client{};
                const int x=step.at("x"),y=step.at("y");
                if(!GetClientRect(hwnd,&client)||width<=0||height<=0||x<0||y<0||x>=width||y>=height)
                    throw std::runtime_error("invalid owned client click");
                POINT position{MulDiv(x,client.right,width),MulDiv(y,client.bottom,height)};
                if(!ClientToScreen(hwnd,&position)||!SetCursorPos(position.x,position.y))throw std::runtime_error("client cursor positioning failed");
                POINT check{};
                if(!GetCursorPos(&check))throw std::runtime_error("cursor verification failed");
                const auto hit=WindowFromPoint(check);
                if(!hit || GetAncestor(hit,GA_ROOT)!=hwnd)throw std::runtime_error("another window covers acceptance click");
                if(!ScreenToClient(hwnd,&check)||client.right<=0||client.bottom<=0)
                    throw std::runtime_error("client projection failed");
                const int actualX=MulDiv(check.x,width,client.right),actualY=MulDiv(check.y,height,client.bottom);
                emit({{"mouse_projection",{{"requested_x",x},{"requested_y",y},{"actual_x",actualX},{"actual_y",actualY}}}});
                if(std::abs(actualX-x)>2 || std::abs(actualY-y)>2)throw std::runtime_error("native GUI cursor projection mismatch");
                // One atomic click avoids leaving an injected button held if
                // foreground changes between separate scheduled actions.
                INPUT input[2]{};input[0].type=INPUT_MOUSE;input[1].type=INPUT_MOUSE;
                input[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;input[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
                result=int(SendInput(2,input,sizeof(INPUT)));
                if(result==1)SendInput(1,&input[1],sizeof(INPUT));
                if(result!=2)throw std::runtime_error("owned mouse click injection failed");
#else
                throw std::runtime_error("os_click is Windows-only");
#endif
            } else if(op=="attach") {
                if(s.pad)throw std::runtime_error("virtual pad already attached");
                SDL_VirtualJoystickDesc d{};d.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
                d.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;d.naxes=SDL_CONTROLLER_AXIS_MAX;d.nbuttons=SDL_CONTROLLER_BUTTON_MAX;
                const auto family=step.value("family",std::string("xbox"));
                if(family=="xbox") {d.vendor_id=0x045e;d.product_id=0x028e;d.name="OVERLAY virtual Xbox (not hardware)";}
                else if(family=="playstation") {d.vendor_id=0x054c;d.product_id=0x09cc;d.name="OVERLAY virtual PS4 (not hardware)";}
                else if(family=="nintendo") {d.vendor_id=0x057e;d.product_id=0x2009;d.name="OVERLAY virtual Switch (not hardware)";}
                else throw std::runtime_error("unknown virtual device family");
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
            } else if(op=="touch_button") {
                // Synthetic producer at the actual platform pad boundary,
                // not an SDL finger and not Android hit-testing certification.
                const auto mask=step.at("mask").get<uint32_t>();
                if(!mask || (mask&~0xcfffU))throw std::runtime_error("invalid touch HID button mask");
                for(unsigned bit=0;bit<16;++bit) {
                    ThreeDsRecomp::Input::DigitalControl control{};
                    if((mask&(1u<<bit)) && ThreeDsRecomp::Input::DigitalControlForHidBit(bit,&control))
                        ThreeDsRecomp::Input::TouchOverlayPad().SetControl(control,step.value("down",true));
                }
            } else if(op=="touch_release") {
                ThreeDsRecomp::Input::TouchOverlayPad().ReleaseAll();
            } else if(op=="focus") {
                // Reclaim only this acceptance window, without injecting keys
                // into whichever unrelated window currently owns foreground.
                SDL_RaiseWindow(window);
#ifdef _WIN32
                SDL_SysWMinfo info{};SDL_VERSION(&info.version);
                if(!window||!SDL_GetWindowWMInfo(window,&info))throw std::runtime_error("no acceptance window");
                const auto hwnd=info.info.win.window;
                const DWORD own=GetCurrentThreadId();
                const DWORD foreground=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
                const bool joined=foreground && foreground!=own && AttachThreadInput(own,foreground,TRUE);
                if(IsIconic(hwnd))ShowWindow(hwnd,SW_RESTORE);
                BringWindowToTop(hwnd);SetForegroundWindow(hwnd);SetFocus(hwnd);
                if(joined)AttachThreadInput(own,foreground,FALSE);
                result=GetForegroundWindow()==hwnd?1:0;
                if(!result)throw std::runtime_error("acceptance window foreground request denied; no input injected");
#else
                result=(SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS)?1:0;
#endif
            } else if(op=="release_timeline") {
                // End boot-only synthetic input; subsequent gameplay comes
                // from the normal physical/virtual-device/platform input path.
                OverlayAcceptanceReleaseTimeline=true;
            } else if(op=="quit") {
                SDL_Event e{};e.type=SDL_QUIT;result=SDL_PushEvent(&e);
            } else if(op!="checkpoint")throw std::runtime_error("unknown input operation");
            emit({{"injected",step},{"result",result},{"source","developer input/window action; not physical hardware"}});
        }
    } catch(const std::exception& e){emit({{"error",e.what()}});}
    const auto snapshot=controls.Snapshot();std::string config,error;
    SerializeNativeControlConfigText(snapshot.Config,&config,&error);
    auto observed=nlohmann::json{{"prompt",ThreeDsRecomp::Input::PromptSetLabel(controls.ActivePromptSet())},
        {"automatic",ThreeDsRecomp::Input::PromptSetLabel(controls.AutomaticPromptSet())},
        {"controls",nlohmann::json::parse(config)}};
    const auto text=observed.dump();
    if(text!=s.last || frame%100==0){
        nlohmann::json report{{"observed",observed}};
#ifdef _WIN32
        auto* window=SDL_GetWindowFromID(1);SDL_SysWMinfo info{};SDL_VERSION(&info.version);
        if(window&&SDL_GetWindowWMInfo(window,&info)) {
            int w=0,h=0;SDL_GetWindowSize(window,&w,&h);
            report["window"]={{"width",w},{"height",h},{"flags",SDL_GetWindowFlags(window)},
              {"owner_thread",GetWindowThreadProcessId(info.info.win.window,nullptr)},
              {"poll_thread",GetCurrentThreadId()},{"foreground",GetForegroundWindow()==info.info.win.window},
              {"hung",IsHungAppWindow(info.info.win.window)!=FALSE}};
        }
#endif
        emit(std::move(report));s.last=text;
    }
}
}
#endif
