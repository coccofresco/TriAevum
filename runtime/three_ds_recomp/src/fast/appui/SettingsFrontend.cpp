#include "fast/appui/SettingsFrontend.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <SDL2/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace Fast::AppUi {
namespace {
std::string Escape(std::string value) {
    std::string result;
    for (char c : value) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '\"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result += c;
        }
    }
    return result;
}
class System final : public Rml::SystemInterface {
    double GetElapsedTime() override { return double(SDL_GetTicks64()) / 1000.0; }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        SDL_Log("RmlUi [%d]: %s", int(type), message.c_str()); return true;
    }
    void SetClipboardText(const Rml::String& text) override { SDL_SetClipboardText(text.c_str()); }
    void GetClipboardText(Rml::String& text) override {
        char* value = SDL_GetClipboardText(); if (value) { text = value; SDL_free(value); }
    }
};

// Share only the final overlay mesh transport and font atlas with the retained
// diagnostic UI. RmlUi owns layout, hit testing, widgets, documents and focus.
// This does not touch the PICA/scene pipeline or create an ImGui window for F1.
class Overlay final : public Rml::RenderInterface {
    struct Geometry { std::vector<Rml::Vertex> Vertices; std::vector<int> Indices; };
    bool scissor = false;
    Rml::Rectanglei clip;
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v, Rml::Span<const int> i) override {
        return reinterpret_cast<Rml::CompiledGeometryHandle>(new Geometry{{v.begin(), v.end()}, {i.begin(), i.end()}});
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override { delete reinterpret_cast<Geometry*>(handle); }
    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f offset, Rml::TextureHandle texture) override {
        const auto& g = *reinterpret_cast<Geometry*>(handle);
        auto* list = ImGui::GetForegroundDrawList();
        auto& io = ImGui::GetIO();
        list->PushClipRect(scissor ? ImVec2(float(clip.Left()), float(clip.Top())) : ImVec2(0, 0),
                          scissor ? ImVec2(float(clip.Right()), float(clip.Bottom())) : io.DisplaySize, true);
        list->PushTextureID(io.Fonts->TexID);
        list->PrimReserve(int(g.Indices.size()), int(g.Vertices.size()));
        const unsigned base = list->_VtxCurrentIdx;
        for (int index : g.Indices) list->PrimWriteIdx(ImDrawIdx(base + unsigned(index)));
        for (const auto& vertex : g.Vertices) {
            const auto c = vertex.colour;
            const auto straight = [&](unsigned v) { return c.alpha ? std::min(255U, (v * 255U + c.alpha / 2) / c.alpha) : 0U; };
            list->PrimWriteVtx({vertex.position.x + offset.x, vertex.position.y + offset.y},
                texture ? ImVec2(vertex.tex_coord.x, vertex.tex_coord.y) : io.Fonts->TexUvWhitePixel,
                IM_COL32(straight(c.red), straight(c.green), straight(c.blue), c.alpha));
        }
        list->PopTextureID(); list->PopClipRect();
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i& size, const Rml::String& source) override {
        if (source != "triaevum-font-atlas") return 0;
        size = {ImGui::GetIO().Fonts->TexWidth, ImGui::GetIO().Fonts->TexHeight}; return 1;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool enabled) override { scissor = enabled; }
    void SetScissorRegion(Rml::Rectanglei value) override { clip = value; }
};

class Fonts final : public Rml::FontEngineInterface {
    std::map<int, Rml::FontMetrics> metrics;
    template<class Visit> float Walk(Rml::FontFaceHandle size, Rml::StringView text, Visit visit) {
        auto* font = ImGui::GetFont();
        const float scale = float(size) / font->FontSize;
        float x = 0;
        const char* p = text.begin(); const char* end = text.end();
        while (p < end) {
            unsigned ch = 0; const int bytes = ImTextCharFromUtf8(&ch, p, end);
            if (bytes <= 0) break;
            p += bytes;
            if (const auto* glyph = font->FindGlyph(ImWchar(ch))) {
                visit(*glyph, x, scale); x += glyph->AdvanceX * scale;
            }
        }
        return x;
    }
public:
    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String&, Rml::Style::FontStyle, Rml::Style::FontWeight, int size) override {
        return Rml::FontFaceHandle(std::max(size, 1));
    }
    const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override {
        auto* font = ImGui::GetFont(); const float scale = float(handle) / font->FontSize;
        return metrics[int(handle)] = {int(handle), font->Ascent * scale, -font->Descent * scale,
            float(handle) * 1.35F, float(handle) * .5F, 2, 1};
    }
    int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView text, const Rml::TextShapingContext&, Rml::Character) override {
        return int(std::ceil(Walk(handle, text, [](const auto&, float, float) {})));
    }
    int GenerateString(Rml::RenderManager& renderer, Rml::FontFaceHandle handle, Rml::FontEffectsHandle,
        Rml::StringView text, Rml::Vector2f position, Rml::ColourbPremultiplied colour, float,
        const Rml::TextShapingContext&, Rml::TexturedMeshList& meshes) override {
        meshes.emplace_back(); auto& mesh = meshes.back().mesh;
        meshes.back().texture = renderer.LoadTexture("triaevum-font-atlas");
        const float ascent = GetFontMetrics(handle).ascent;
        return int(std::ceil(Walk(handle, text, [&](const ImFontGlyph& g, float x, float scale) {
            if (!g.Visible) return;
            const int base = int(mesh.vertices.size());
            const float left = position.x + x + g.X0 * scale, right = position.x + x + g.X1 * scale;
            const float top = position.y - ascent + g.Y0 * scale, bottom = position.y - ascent + g.Y1 * scale;
            mesh.vertices.insert(mesh.vertices.end(), {{{left, top}, colour, {g.U0, g.V0}},
                {{right, top}, colour, {g.U1, g.V0}}, {{right, bottom}, colour, {g.U1, g.V1}},
                {{left, bottom}, colour, {g.U0, g.V1}}});
            mesh.indices.insert(mesh.indices.end(), {base, base+1, base+2, base, base+2, base+3});
        })));
    }
};

const char* kStyle = R"(
body { font-family: triaevum; font-size: 19px; color: #edf0f1; margin: 0; width: 100%; height: 100%; background-color: #12171af2; }
header { height: 70px; margin: 16px 4%; border-bottom: 1px #607579; display: flex; align-items: center; justify-content: space-between; }
h1 { font-size: 30px; margin: 0; } h2 { font-size: 24px; margin: 0 0 18px; }
main { display: flex; margin: 0 4%; height: 72%; }
nav { width: 24%; overflow-y: auto; padding-right: 20px; }
article { width: 76%; overflow-y: auto; padding: 0 18px; }
button, select, input { font-family: triaevum; font-size: 19px; color: #edf0f1; background-color: #263238; border: 1px #607579; padding: 10px 12px; tab-index: auto; }
button:hover, button:focus, select:focus, input:focus { background-color: #32594f; border-color: #b6e0cb; }
button:disabled, input:disabled, select:disabled { color: #829093; background-color: #202729; }
nav button { display: block; width: 100%; margin-bottom: 6px; text-align: left; }
nav button.selected { border-color: #b6e0cb; background-color: #32594f; }
.row { display: flex; align-items: center; justify-content: space-between; margin: 0 0 10px; min-height: 48px; }
.row label { width: 55%; padding-right: 12px; }
.row select, .row input, .row button { width: 40%; }
.row .value { width: 40%; }
.number { width: 40%; display: flex; } .number input { width: 55%; } .number button { width: 20%; padding: 10px 0; text-align: center; }
scrollbarvertical { width: 12px; } scrollbarvertical slidertrack { background-color: #1a2428; } scrollbarvertical sliderbar { background-color: #607579; min-height: 28px; }
selectvalue { padding-right: 10px; } selectbox { background-color: #263238; border: 1px #b6e0cb; }
option { padding: 9px 12px; } option:checked, option:hover { background-color: #32594f; }
footer { margin: 12px 4%; height: 46px; color: #b6e0cb; }
)";
}

struct SettingsFrontend::Impl : Rml::EventListener {
    System system; Overlay overlay; Fonts fonts;
    Rml::Context* context = nullptr;
    Rml::ElementDocument* document = nullptr;
    Rml::ElementDocument* modal = nullptr;
    bool confirmActive=false;
    std::string confirmMessage;
    std::function<void()> accept, reject;
    Pages pages;
    size_t selected = 0;
    bool visible = false, rebuild = false;
    std::string status;
    std::function<void()> close, advanced;
    int stickDirection = 0;
    Uint64 stickRepeat = 0;
    void Navigate(int direction) {
        using namespace Rml::Input;
        auto* focus = context->GetFocusElement();
        const bool select = focus && focus->GetTagName() == "select";
        const bool expanded = select && focus->QuerySelector("selectbox") &&
            focus->QuerySelector("selectbox")->IsVisible();
        KeyIdentifier key = KI_TAB;
        int mods = direction == -2 ? KM_SHIFT : 0;
        if (select && (expanded || std::abs(direction) == 1)) {
            key = direction < 0 ? KI_UP : KI_DOWN; mods = 0;
        } else if (std::abs(direction) == 1) {
            key = direction < 0 ? KI_LEFT : KI_RIGHT;
        }
        context->ProcessKeyDown(key, mods);
        context->ProcessKeyUp(key, mods);
    }
    void Init() {
        if (context) return;
        Rml::SetSystemInterface(&system); Rml::SetRenderInterface(&overlay); Rml::SetFontEngineInterface(&fonts);
        if (!Rml::Initialise()) throw std::runtime_error("Could not initialize application menu");
        context = Rml::CreateContext("triaevum-settings", {1280, 720});
        if (!context) throw std::runtime_error("Could not create application menu context");
    }
    ~Impl() {
        if (context) { Rml::RemoveContext("triaevum-settings"); Rml::Shutdown(); }
    }
    void Build() {
        if (document) document->Close();
        if (pages.empty()) return;
        selected = std::min(selected, pages.size()-1);
        std::string html = "<rml><head><style>" + std::string(kStyle) + "</style></head><body><header><h1>TriAevum / Settings</h1><div><button id='advanced'>Advanced graphics</button> <button id='close'>Resume</button></div></header><main><nav>";
        for (size_t p=0; p<pages.size(); ++p)
            html += "<button id='page" + std::to_string(p) + "' class='" + (p == selected ? "selected" : "") + "'>" + Escape(pages[p].Label) + "</button>";
        html += "</nav><article><h2>" + Escape(pages[selected].Label) + "</h2>";
        const auto& fields = pages[selected].Fields;
        for (size_t i=0; i<fields.size(); ++i) {
            const auto& f=fields[i]; const std::string id="field"+std::to_string(i);
            const auto value=f.Read ? f.Read() : "";
            const std::string disabled=f.Enabled && !f.Enabled() ? " disabled='disabled'" : "";
            html += "<div class='row'><label for='"+id+"'>"+Escape(f.Label)+"</label>";
            if(f.Kind==FieldKind::Choice) {
                html += "<select id='"+id+"'"+disabled+">";
                for(const auto& opt:f.Options) {
                    const auto reason=f.UnavailableReason?f.UnavailableReason(opt.Value):"";
                    html += "<option value='"+Escape(opt.Value)+"'"+(value==opt.Value?" selected='selected'":"")+
                        (reason.empty()?"":" disabled='disabled'")+" title='"+Escape(reason)+"'>"+Escape(opt.Label)+"</option>";
                }
                html += "</select>";
            } else if(f.Kind==FieldKind::Number) {
                html += "<div class='number'><input type='text' id='"+id+"' value=\""+Escape(value)+"\""+disabled+"/><button id='minus"+std::to_string(i)+"'"+disabled+">-</button><button id='plus"+std::to_string(i)+"'"+disabled+">+</button></div>";
            } else if(f.Kind==FieldKind::Toggle || f.Kind==FieldKind::Action) {
                html += "<button id='"+id+"'"+disabled+">"+(f.Kind==FieldKind::Toggle ? (value=="1"?"On":"Off") : Escape(value.empty()?"Open":value))+"</button>";
            } else html += "<div class='value' id='"+id+"'>"+Escape(value)+"</div>";
            html += "</div>";
        }
        html += "</article></main><footer id='status'>"+Escape(status)+"</footer></body></rml>";
        document=context->LoadDocumentFromMemory(html);
        if (!document) throw std::runtime_error("Could not load application settings document");
        document->AddEventListener("click", this); document->AddEventListener("change", this);
        document->AddEventListener("blur", this,true); document->AddEventListener("keydown",this);
        document->Show(); context->Update();
        if (auto* button=document->GetElementById("page"+std::to_string(selected))) button->Focus();
        rebuild=false;
    }
    void ProcessEvent(Rml::Event& event) override {
        auto* target=event.GetTargetElement();
        while(target && target->GetId().empty()) target=target->GetParentNode();
        if(!target)return;
        const auto id=target->GetId();
        if(event.GetType()=="click" && id=="accept") {accept();return;}
        if(event.GetType()=="click" && id=="reject") {reject();return;}
        if(confirmActive)return;
        if (event.GetType()=="click" && id=="close") { close(); return; }
        if (event.GetType()=="click" && id=="advanced") { advanced(); return; }
        if (id.starts_with("page") && event.GetType()=="click") {
            selected=std::stoul(id.substr(4)); rebuild=true; return;
        }
        const bool plus=id.starts_with("plus"),minus=id.starts_with("minus");
        if (!id.starts_with("field") && !plus && !minus) return;
        const size_t index=std::stoul(id.substr(plus?4:5));
        if (index>=pages[selected].Fields.size()) return;
        auto& field=pages[selected].Fields[index];
        if (!field.Write || (field.Enabled && !field.Enabled())) return;
        std::string value;
        if((plus||minus) && event.GetType()=="click")value=std::to_string(std::clamp(std::stod(field.Read())+(plus?field.Step:-field.Step),field.Minimum,field.Maximum));
        else if(field.Kind==FieldKind::Toggle && event.GetType()=="click") value=field.Read()=="1"?"0":"1";
        else if(field.Kind==FieldKind::Action && event.GetType()=="click") value="";
        else if(field.Kind==FieldKind::Choice && event.GetType()=="change")
            value=event.GetParameter<Rml::String>("value", "");
        else if(field.Kind==FieldKind::Number && (event.GetType()=="blur" || (event.GetType()=="keydown" && event.GetParameter<int>("key_identifier",0)==Rml::Input::KI_RETURN))) {
            if(auto* control=rmlui_dynamic_cast<Rml::ElementFormControl*>(target))value=control->GetValue();else return;
        }
        else return;
        try {
            status=field.UnavailableReason?field.UnavailableReason(value):"";
            if(status.empty())status=field.Write(value);
        } catch(const std::exception& error) {status=error.what();}
        if(status.empty()) status="Settings updated";
        if(field.Kind==FieldKind::Toggle) target->SetInnerRML(field.Read()=="1"?"On":"Off");
        if(auto* label=document->GetElementById("status")) label->SetInnerRML(Escape(status));
    }
};
SettingsFrontend::SettingsFrontend():m(std::make_unique<Impl>()) {}
SettingsFrontend::~SettingsFrontend()=default;
void SettingsFrontend::Open(Pages pages,std::function<void()> close,std::function<void()> advanced) {
    m->pages=std::move(pages); m->close=std::move(close); m->advanced=std::move(advanced); m->visible=true; m->rebuild=true;
}
void SettingsFrontend::Close() { m->visible=false;m->stickDirection=0; if(m->document) m->document->Hide();if(m->modal){m->modal->Close();m->modal=nullptr;} }
bool SettingsFrontend::Visible() const {return m->visible;}
void SettingsFrontend::Confirmation(bool active,std::string message,std::function<void()> accept,std::function<void()> reject) {
    m->confirmActive=active;m->confirmMessage=std::move(message);m->accept=std::move(accept);m->reject=std::move(reject);
}
void SettingsFrontend::Draw() {
    if(!m->visible) return;
    m->Init();
    const auto size=ImGui::GetIO().DisplaySize;
    m->context->SetDimensions({int(size.x),int(size.y)});
    m->context->SetDensityIndependentPixelRatio(std::clamp(size.y/800.0F,0.65F,2.0F));
    if(m->rebuild && !m->modal) m->Build();
    if(m->stickDirection && SDL_GetTicks64() >= m->stickRepeat) {
        m->Navigate(m->stickDirection); m->stickRepeat=SDL_GetTicks64()+140;
    }
    if(m->confirmActive) {
        if(!m->modal) {
            m->modal=m->context->LoadDocumentFromMemory("<rml><head><style>"+std::string(kStyle)+
              "body {background-color:#12171aee;} #prompt {margin:20% 15%;padding:24px;border:1px #b6e0cb;background-color:#263238;}"
              "</style></head><body><div id='prompt'><h2>Keep display settings?</h2><p id='message'></p><button id='accept'>Keep</button> <button id='reject'>Revert</button></div></body></rml>");
            if(m->modal){m->modal->AddEventListener("click",m.get());m->modal->Show(Rml::ModalFlag::Modal);m->modal->GetElementById("reject")->Focus();}
        }
        if(m->modal)m->modal->GetElementById("message")->SetInnerRML(Escape(m->confirmMessage));
    } else if(m->modal){m->modal->Close();m->modal=nullptr;}
    if(m->document && m->selected<m->pages.size()) {
        const auto& fields=m->pages[m->selected].Fields;
        for(size_t i=0;i<fields.size();++i) {
            auto* element=m->document->GetElementById("field"+std::to_string(i));
            if(!element)continue;
            const auto& field=fields[i];
            if(field.Enabled) {
                if(field.Enabled())element->RemoveAttribute("disabled");else element->SetAttribute("disabled","disabled");
            }
            if(!field.Read)continue;
            const auto value=field.Read();
            if(field.Kind==FieldKind::Text || field.Kind==FieldKind::Toggle) {
                const auto content=field.Kind==FieldKind::Toggle?(value=="1"?"On":"Off"):Escape(value);
                if(element->GetInnerRML()!=content)element->SetInnerRML(content);
            } else if(!element->IsPseudoClassSet("focus")) {
                if(auto* control=rmlui_dynamic_cast<Rml::ElementFormControl*>(element);control && control->GetValue()!=value)
                    control->SetValue(value);
            }
        }
    }
    m->context->Update(); m->context->Render();
}
void SettingsFrontend::Event(const SDL_Event& e) {
    if(!m->visible || !m->context) return;
    auto* c=m->context;
    if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_LOST) m->stickDirection=0;
    if(e.type==SDL_CONTROLLERDEVICEREMOVED) m->stickDirection=0;
    if(e.type==SDL_CONTROLLERAXISMOTION && (e.caxis.axis==SDL_CONTROLLER_AXIS_LEFTX || e.caxis.axis==SDL_CONTROLLER_AXIS_LEFTY)) {
        const int axis=e.caxis.axis==SDL_CONTROLLER_AXIS_LEFTX?1:2;
        if(std::abs(int(e.caxis.value))<10000) {
            if(std::abs(m->stickDirection)==axis)m->stickDirection=0;
        } else if(std::abs(int(e.caxis.value))>18000) {
            const int direction=e.caxis.value<0?-axis:axis;
            if(m->stickDirection!=direction) {
                m->stickDirection=direction;m->stickRepeat=SDL_GetTicks64()+400;m->Navigate(direction);
            }
        }
    }
    if(e.type==SDL_MOUSEMOTION) c->ProcessMouseMove(e.motion.x,e.motion.y,0);
    if(e.type==SDL_MOUSEWHEEL) c->ProcessMouseWheel(float(-e.wheel.y),0);
    const auto mouse=[](int b){return b==SDL_BUTTON_LEFT?0:b==SDL_BUTTON_RIGHT?1:2;};
    if(e.type==SDL_MOUSEBUTTONDOWN) c->ProcessMouseButtonDown(mouse(e.button.button),0);
    if(e.type==SDL_MOUSEBUTTONUP) c->ProcessMouseButtonUp(mouse(e.button.button),0);
    if(e.type==SDL_TEXTINPUT) c->ProcessTextInput(e.text.text);
    using namespace Rml::Input;
    KeyIdentifier key=KI_UNKNOWN;
    if(e.type==SDL_KEYDOWN || e.type==SDL_KEYUP) {
        switch(e.key.keysym.sym) {
        case SDLK_TAB:key=KI_TAB;break; case SDLK_RETURN:key=KI_RETURN;break;
        case SDLK_ESCAPE:if(e.type==SDL_KEYDOWN&&!e.key.repeat){if(m->confirmActive)m->reject();else m->close();}return;
        case SDLK_BACKSPACE:key=KI_BACK;break; case SDLK_DELETE:key=KI_DELETE;break;
        case SDLK_LEFT:key=KI_LEFT;break;case SDLK_RIGHT:key=KI_RIGHT;break;
        case SDLK_UP:key=KI_UP;break;case SDLK_DOWN:key=KI_DOWN;break;
        case SDLK_HOME:key=KI_HOME;break;case SDLK_END:key=KI_END;break;
        case SDLK_SPACE:key=KI_SPACE;break;default:break;
        }
        if(e.key.keysym.sym>=SDLK_a && e.key.keysym.sym<=SDLK_z)
            key=static_cast<KeyIdentifier>(KI_A+e.key.keysym.sym-SDLK_a);
        const int mods=(e.key.keysym.mod&KMOD_SHIFT?KM_SHIFT:0)|(e.key.keysym.mod&KMOD_CTRL?KM_CTRL:0);
        if(e.type==SDL_KEYDOWN)c->ProcessKeyDown(key,mods);else c->ProcessKeyUp(key,mods);
    }
    if(e.type==SDL_CONTROLLERBUTTONDOWN || e.type==SDL_CONTROLLERBUTTONUP) {
        const bool down=e.type==SDL_CONTROLLERBUTTONDOWN;
        switch(e.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A:key=KI_RETURN;break;
        case SDL_CONTROLLER_BUTTON_B:if(down){if(m->confirmActive)m->reject();else m->close();}return;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:if(down)m->Navigate(2);return;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:if(down)m->Navigate(-2);return;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:if(down)m->Navigate(-1);return;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:if(down)m->Navigate(1);return;default:return;
        }
        const int mods=e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP?KM_SHIFT:0;
        if(down)c->ProcessKeyDown(key,mods);else c->ProcessKeyUp(key,mods);
    }
}
}
