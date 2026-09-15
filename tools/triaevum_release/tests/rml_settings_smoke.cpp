#include "fast/appui/SettingsFrontend.h"
#include <RmlUi/Core.h>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <imgui.h>
#include <iostream>
#include <stdexcept>

void Require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
int main() try {
    SDL_Init(0);
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1280,720};io.DeltaTime=1.0F/60;
    io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(uintptr_t(1)));
    {
        Fast::AppUi::SettingsFrontend ui;
        bool enabled=false,closed=false,advanced=false;
        std::string choice="0";
        using namespace Fast::AppUi;
        Page display{"display","Display",{}};
        display.Fields.push_back({"toggle","VSync",FieldKind::Toggle,[&]{return enabled?"1":"0";},[&](const std::string& v){enabled=v=="1";return std::string();}});
        display.Fields.push_back({"choice","Mode",FieldKind::Choice,[&]{return choice;},[&](const std::string& v){choice=v;return std::string();},{{"0","First"},{"1","Second"}}});
        Page controls{"controls","Controls",{}};
        controls.Fields.push_back({"status","Device",FieldKind::Text,[]{return "Test controller";}});
        ui.Open({std::move(display),std::move(controls)},
                 [&]{closed=true;ui.Close();},[&]{advanced=true;ui.Close();});
        const auto frame=[&]{ImGui::NewFrame();ui.Draw();ImGui::Render();};
        const auto click=[&](const char* id) {
            auto* context=Rml::GetContext("triaevum-settings");
            auto* document=context->GetDocument(context->GetNumDocuments()-1);
            auto* element=document->GetElementById(id);Require(element!=nullptr,"Missing real RmlUi control");
            const auto pos=element->GetAbsoluteOffset();const auto size=element->GetBox().GetSize();
            SDL_Event event{};event.type=SDL_MOUSEMOTION;event.motion.x=int(pos.x+size.x/2);event.motion.y=int(pos.y+size.y/2);ui.Event(event);
            event={};event.type=SDL_MOUSEBUTTONDOWN;event.button.button=SDL_BUTTON_LEFT;ui.Event(event);
            event.type=SDL_MOUSEBUTTONUP;ui.Event(event);frame();
        };
        for(auto extent:{ImVec2(1280,720),ImVec2(800,600),ImVec2(1920,1080)}) {
            io.DisplaySize=extent;frame();frame();
            Require(ImGui::GetDrawData()->TotalVtxCount>100,"RmlUi did not generate visible geometry");
            click("field0");Require(enabled,"Toggle not connected to setting");
            click("field0");Require(!enabled,"Toggle not reversible");
            click("page1");click("page0");
        }
        auto* context=Rml::GetContext("triaevum-settings");
        context->GetDocument(context->GetNumDocuments()-1)->GetElementById("field0")->Focus();
        SDL_Event controller{};controller.type=SDL_CONTROLLERBUTTONDOWN;controller.cbutton.button=SDL_CONTROLLER_BUTTON_A;ui.Event(controller);
        controller.type=SDL_CONTROLLERBUTTONUP;ui.Event(controller);frame();
        Require(enabled,"Controller activation did not reach the retained widget");
        context->GetDocument(context->GetNumDocuments()-1)->GetElementById("field1")->Focus();
        controller.type=SDL_CONTROLLERBUTTONDOWN;controller.cbutton.button=SDL_CONTROLLER_BUTTON_DPAD_RIGHT;ui.Event(controller);frame();
        Require(choice=="1","Controller cannot change a select value");
        SDL_Event stick{};stick.type=SDL_CONTROLLERAXISMOTION;stick.caxis.axis=SDL_CONTROLLER_AXIS_LEFTX;stick.caxis.value=-24000;ui.Event(stick);frame();
        Require(choice=="0","Analog stick cannot change a select value");
        stick.caxis.value=0;ui.Event(stick);
        bool reverted=false;
        ui.Confirmation(true,"Test display confirmation",[]{},[&]{reverted=true;});frame();frame();
        click("reject");Require(reverted,"RmlUi display rollback action missing");
        ui.Confirmation(false,"",[]{},[]{});frame();frame();
        click("advanced");Require(advanced&&!ui.Visible(),"Advanced surface routing failed");
        ui.Open({{"display","Display",{}}},[&]{closed=true;ui.Close();},[]{});frame();frame();
        click("close");Require(closed&&!ui.Visible(),"Resume did not close frontend");
    }
    ImGui::DestroyContext();SDL_Quit();
    std::cout<<"RmlUi actual-document smoke passed: geometry, resize, mouse, pages, reversible setting, close and F12 routing\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
