#include "fast/Fast3dGui.h"

#include "fast/Fast3dWindow.h"
#include "ship/Context.h"
#include "ship/config/Config.h"
#include "ship/config/ConsoleVariable.h"
#include "fast/backends/gfx_metal.h"
#include "fast/interpreter.h"
#include "fast/backends/gfx_rendering_api.h"
#ifdef ENABLE_OOT3D_VULKAN
#include "fast/backends/gfx_vulkan.h"
#endif
#include "fast/resource/type/Texture.h"
#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/graphics_settings_window.h"
#endif
#include "ship/window/gui/resource/GuiTextureFactory.h"
#include "ship/resource/File.h"

#include <cstdlib>

#ifdef __APPLE__
#include <SDL_hints.h>
#include <SDL_video.h>
#include <imgui_impl_metal.h>
#include <imgui_impl_sdl2.h>
#else
#include <SDL2/SDL_hints.h>
#ifdef _WIN32
#include <SDL2/SDL_syswm.h>
#endif
#include <SDL2/SDL_video.h>
#endif

#if defined(__ANDROID__) || defined(__IOS__)
#include "ship/port/mobile/MobileImpl.h"
#endif

#ifdef ENABLE_OPENGL
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#endif

#if defined(ENABLE_DX11) || defined(ENABLE_DX12)
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

// NOLINTNEXTLINE
IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace Fast {
namespace {
#ifdef ENABLE_OOT3D_VULKAN
class Oot3dGraphicsSettingsPersistence final
    : public Oot3d::GraphicsSettingsPersistencePort {
  public:
    bool LoadRoot(nlohmann::json& root) override {
        auto* context = Ship::Context::GetRawInstance();
        if (context == nullptr || context->GetConfig() == nullptr) {
            return false;
        }
        root = context->GetConfig()->GetNestedJson();
        return true;
    }

    bool StoreGraphics(const nlohmann::json& graphics) override {
        auto* context = Ship::Context::GetRawInstance();
        return context != nullptr && context->GetConfig() != nullptr &&
            context->GetConfig()->SetBlock("Graphics", graphics);
    }
};

class Oot3dGraphicsSettingsWindow final : public Ship::GuiWindow {
  public:
    Oot3dGraphicsSettingsWindow()
        : GuiWindow("gOpenWindows.Oot3dGraphics", false,
                    "OOT3D Graphics", ImVec2(560.0F, 760.0F)) {
        // A previous session may have saved the window open. Always start closed.
        SetVisibility(false);
    }

    void Draw() override {
        auto& runtime = Oot3d::GraphicsSettingsRuntime::Instance();
        // Closing or collapsing F1 must not strand a deferred slider save.
        if (!IsVisible() || !ImGui::IsAnyItemActive()) runtime.SavePending();
        if (IsVisible()) {
            const auto available = ImGui::GetMainViewport()->WorkSize;
            const ImVec2 maximum(std::max(320.0F, available.x - 16.0F),
                                 std::max(240.0F, available.y - 16.0F));
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(std::min(520.0F, maximum.x), std::min(400.0F, maximum.y)),
                maximum);
        }
        GuiWindow::Draw();
    }

  protected:
    void InitElement() override {}
    void UpdateElement() override {}
    void DrawElement() override {
        mPanel.Draw();
    }

  private:
    Oot3d::GraphicsSettingsPanel mPanel;
};

class Oot3dDisplayConfirmationWindow final : public Ship::GuiWindow {
  public:
    Oot3dDisplayConfirmationWindow() : GuiWindow("", false, "Display confirmation") {}
    void Draw() override {
        SetVisibility(Oot3d::GraphicsSettingsRuntime::Instance().PresentationStatus().Phase ==
                      Oot3d::PresentationTransactionPhase::AwaitingConfirmation);
        Oot3d::DrawDisplayConfirmation();
    }
  protected:
    void InitElement() override {}
    void UpdateElement() override {}
    void DrawElement() override {}
};

struct Oot3dMousePosition {
    float X = 0.0F;
    float Y = 0.0F;
};

Oot3dMousePosition GetOot3dMousePosition(SDL_Window* window) {
#ifdef _WIN32
    SDL_SysWMinfo wmInfo{};
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window, &wmInfo)) {
        POINT screenPosition{};
        RECT clientRect{};
        int layoutWidth = 0;
        int layoutHeight = 0;
        SDL_GetWindowSize(window, &layoutWidth, &layoutHeight);
        if (GetCursorPos(&screenPosition) &&
            ScreenToClient(wmInfo.info.win.window, &screenPosition) &&
            GetClientRect(wmInfo.info.win.window, &clientRect)) {
            const int clientWidth = clientRect.right - clientRect.left;
            const int clientHeight = clientRect.bottom - clientRect.top;
            if (clientWidth > 0 && clientHeight > 0 &&
                layoutWidth > 0 && layoutHeight > 0) {
                return {
                    static_cast<float>(screenPosition.x) * layoutWidth / clientWidth,
                    static_cast<float>(screenPosition.y) * layoutHeight / clientHeight};
            }
        }
    }
#endif
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    return {static_cast<float>(mouseX), static_cast<float>(mouseY)};
}

void BindOot3dMainViewport(SDL_Window* window) {
    // Runtime profile changes can rebuild ImGui platform state after the SDL
    // backend initialized it. Keep the sole Vulkan window bound explicitly so
    // the stock backend accepts keyboard, text, wheel and focus events too.
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    viewport->PlatformHandle = reinterpret_cast<void*>(
        static_cast<intptr_t>(SDL_GetWindowID(window)));
#ifdef _WIN32
    if (viewport->PlatformHandleRaw == nullptr) {
        SDL_SysWMinfo wmInfo{};
        SDL_VERSION(&wmInfo.version);
        if (SDL_GetWindowWMInfo(window, &wmInfo)) {
            viewport->PlatformHandleRaw = wmInfo.info.win.window;
        }
    }
#endif
}

int Oot3dMouseButtonIndex(uint8_t button) {
    switch (button) {
        case SDL_BUTTON_LEFT:
            return 0;
        case SDL_BUTTON_RIGHT:
            return 1;
        case SDL_BUTTON_MIDDLE:
            return 2;
        case SDL_BUTTON_X1:
            return 3;
        case SDL_BUTTON_X2:
            return 4;
        default:
            return -1;
    }
}

ImGuiMouseSource Oot3dMouseSource(uint32_t which) {
    return which == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen
                                      : ImGuiMouseSource_Mouse;
}
#endif
} // namespace

Fast3dGui::Fast3dGui() : Ship::Gui() {
}

Fast3dGui::Fast3dGui(std::vector<std::shared_ptr<Ship::GuiWindow>> guiWindows) : Ship::Gui(guiWindows) {
}

void Fast3dGui::Init(GuiWindowInitData windowImpl) {
    mImpl = windowImpl;
#ifdef ENABLE_OOT3D_VULKAN
    if (mImpl.Backend == WindowBackend::FAST3D_SDL_OOT3D_VULKAN) {
        Oot3d::InstallGraphicsSettingsPersistencePort(
            std::make_shared<Oot3dGraphicsSettingsPersistence>());
        AddGuiWindow(std::make_shared<Oot3dGraphicsSettingsWindow>());
        AddGuiWindow(std::make_shared<Oot3dDisplayConfirmationWindow>());
    }
#endif
    Gui::Init();
}

bool Fast3dGui::SupportsViewports() {
#ifdef ENABLE_OOT3D_VULKAN
    // The Vulkan backend owns one swapchain only. Letting ImGui create SDL
    // platform windows leaves those windows without renderer swapchains and
    // can invalidate ImGui_ImplSDL2_ViewportData during a mode change.
    if (mImpl.Backend == WindowBackend::FAST3D_SDL_OOT3D_VULKAN) {
        return false;
    }
#endif
#ifdef __linux__
    const char* currentDesktop = std::getenv("XDG_CURRENT_DESKTOP");
    if (currentDesktop && std::string(currentDesktop) == "gamescope") {
        return false;
    }
#endif

#if defined(__ANDROID__) || defined(__IOS__)
    return false;
#endif

    return true;
}

void Fast3dGui::HandleWindowEvents(Fast::WindowEvent event) {
    switch (mImpl.Backend) {
        case WindowBackend::FAST3D_SDL_OPENGL:
        case WindowBackend::FAST3D_SDL_METAL:
            ImGui_ImplSDL2_ProcessEvent(static_cast<const SDL_Event*>(event.Sdl.Event));
#if defined(__ANDROID__) || defined(__IOS__)
            Ship::Mobile::ImGuiProcessEvent(ImGui::GetIO().WantTextInput);
#endif
            break;
#ifdef ENABLE_OOT3D_VULKAN
        case WindowBackend::FAST3D_SDL_OOT3D_VULKAN: {
            const SDL_Event& sdlEvent =
                *static_cast<const SDL_Event*>(event.Sdl.Event);
            auto* window = static_cast<SDL_Window*>(mImpl.Vulkan.Window);
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
            BindOot3dMainViewport(window);
            const Oot3dMousePosition mousePosition =
                GetOot3dMousePosition(window);
            switch (sdlEvent.type) {
                case SDL_MOUSEMOTION:
                    io.AddMouseSourceEvent(Oot3dMouseSource(sdlEvent.motion.which));
                    io.AddMousePosEvent(mousePosition.X, mousePosition.Y);
                    break;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP: {
                    const int buttonIndex =
                        Oot3dMouseButtonIndex(sdlEvent.button.button);
                    if (buttonIndex >= 0) {
                        // Position must precede the transition so ImGui's
                        // trickled queue evaluates the click in this frame.
                        io.AddMouseSourceEvent(
                            Oot3dMouseSource(sdlEvent.button.which));
                        io.AddMousePosEvent(mousePosition.X, mousePosition.Y);
                        io.AddMouseButtonEvent(
                            buttonIndex,
                            sdlEvent.type == SDL_MOUSEBUTTONDOWN);
                    }
                    break;
                }
                case SDL_MOUSEWHEEL:
                    io.AddMouseSourceEvent(Oot3dMouseSource(sdlEvent.wheel.which));
#if SDL_VERSION_ATLEAST(2, 0, 18)
                    io.AddMouseWheelEvent(-sdlEvent.wheel.preciseX,
                                          sdlEvent.wheel.preciseY);
#else
                    io.AddMouseWheelEvent(-static_cast<float>(sdlEvent.wheel.x),
                                          static_cast<float>(sdlEvent.wheel.y));
#endif
                    break;
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                    // Host renderer shortcuts must not also reach ImGui bindings.
                    if (sdlEvent.key.keysym.scancode != SDL_SCANCODE_F1 &&
                        sdlEvent.key.keysym.scancode != SDL_SCANCODE_F2) {
                        ImGui_ImplSDL2_ProcessEvent(&sdlEvent);
                    }
                    break;
                default:
                    ImGui_ImplSDL2_ProcessEvent(&sdlEvent);
                    break;
            }
            break;
        }
#endif
#ifdef ENABLE_DX11
        case WindowBackend::FAST3D_DXGI_DX11:
            ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(event.Win32.Handle), event.Win32.Msg, event.Win32.Param1,
                                           event.Win32.Param2);
            break;
#endif
        default:
            break;
    }
}

void Fast3dGui::DrawMenu() {
    Ship::Gui::DrawMenu();
#ifdef ENABLE_OOT3D_VULKAN
    if (mImpl.Backend == WindowBackend::FAST3D_SDL_OOT3D_VULKAN) {
        const auto settingsWindow = GetGuiWindow("OOT3D Graphics");
        const auto window = std::dynamic_pointer_cast<Fast3dWindow>(
            Ship::Context::GetRawInstance()->GetWindow());
        const bool toggleKeyDown =
            window != nullptr &&
            window->IsKeyDown(Ship::KbScancode::LUS_KB_F1);
        if (settingsWindow != nullptr && toggleKeyDown &&
            !mOot3dGraphicsToggleKeyWasDown) {
            settingsWindow->ToggleVisibility();
        }
        mOot3dGraphicsToggleKeyWasDown = toggleKeyDown;
        const bool hostUiVisible = GetAnyGuiWindowVisible();
        if (hostUiVisible != mOot3dGraphicsWindowVisible) {
            mOot3dGraphicsWindowVisible = hostUiVisible;
            const auto shipWindow =
                Ship::Context::GetRawInstance()->GetWindow();
            const auto mouse = shipWindow->GetMouseStateManager();
            mouse->SetForceCursorVisibility(mOot3dGraphicsWindowVisible);
            if (mOot3dGraphicsWindowVisible) {
                shipWindow->SetMouseCapture(false);
                shipWindow->SetCursorVisibility(true);
                mouse->ResetCursorVisibilityTimer();
            } else {
                mouse->UpdateMouseCapture();
            }
        }
    }
#endif
}

void Fast3dGui::ImGuiWMInit() {
    switch (mImpl.Backend) {
        case WindowBackend::FAST3D_SDL_OPENGL:
            SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
            if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_ALLOW_BACKGROUND_INPUTS, 1)) {
                SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
            }
            ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window*>(mImpl.Opengl.Window), mImpl.Opengl.Context);
            break;
#ifdef ENABLE_OOT3D_VULKAN
        case WindowBackend::FAST3D_SDL_OOT3D_VULKAN:
            ImGui_ImplSDL2_InitForVulkan(static_cast<SDL_Window*>(mImpl.Vulkan.Window));
            break;
#endif
#if __APPLE__
        case WindowBackend::FAST3D_SDL_METAL:
            SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
            if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_ALLOW_BACKGROUND_INPUTS, 1)) {
                SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
            }
            ImGui_ImplSDL2_InitForMetal(static_cast<SDL_Window*>(mImpl.Metal.Window));
            break;
#endif
#if defined(ENABLE_DX11) || defined(ENABLE_DX12)
        case WindowBackend::FAST3D_DXGI_DX11:
            ImGui_ImplWin32_Init(mImpl.Dx11.Window);
            break;
#endif
        default:
            break;
    }
}

void Fast3dGui::ImGuiWMShutdown() {
    switch (mImpl.Backend) {
#ifdef ENABLE_OPENGL
        case WindowBackend::FAST3D_SDL_OPENGL:
            ImGui_ImplSDL2_Shutdown();
            break;
#endif
#ifdef ENABLE_OOT3D_VULKAN
        case WindowBackend::FAST3D_SDL_OOT3D_VULKAN:
            ImGui_ImplSDL2_Shutdown();
            break;
#endif
#if __APPLE__
        case WindowBackend::FAST3D_SDL_METAL:
            ImGui_ImplSDL2_Shutdown();
            break;
#endif
#if defined(ENABLE_DX11) || defined(ENABLE_DX12)
        case WindowBackend::FAST3D_DXGI_DX11:
            ImGui_ImplWin32_Shutdown();
            break;
#endif
        default:
            break;
    }
}

void Fast3dGui::ImGuiBackendInit() {
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    mInterpreter = std::dynamic_pointer_cast<Fast3dWindow>(window)->GetInterpreterWeak();
    switch (mImpl.Backend) {
#ifdef ENABLE_OPENGL
        case WindowBackend::FAST3D_SDL_OPENGL:
#ifdef __APPLE__
            ImGui_ImplOpenGL3_Init("#version 410 core");
#elif USE_OPENGLES
            ImGui_ImplOpenGL3_Init("#version 300 es");
#else
            ImGui_ImplOpenGL3_Init("#version 120");
#endif
            break;
#endif

#ifdef __APPLE__
        case WindowBackend::FAST3D_SDL_METAL: {
            GfxRenderingAPIMetal* api = (GfxRenderingAPIMetal*)mInterpreter.lock()->GetCurrentRenderingAPI();
            api->MetalInit(mImpl.Metal.Renderer);
            break;
        }
#endif

#ifdef ENABLE_DX11
        case WindowBackend::FAST3D_DXGI_DX11:
            ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(mImpl.Dx11.Device),
                                static_cast<ID3D11DeviceContext*>(mImpl.Dx11.DeviceContext));
            break;
#endif
        default:
            break;
    }
#ifdef ENABLE_OOT3D_VULKAN
    if (mImpl.Backend == WindowBackend::FAST3D_SDL_OOT3D_VULKAN) {
        if (auto interpreter = mInterpreter.lock()) {
            auto* api = dynamic_cast<GfxRenderingAPIVulkan*>(interpreter->GetCurrentRenderingAPI());
            if (api != nullptr) api->InitImGuiBackend();
        }
    }
#endif
}

void Fast3dGui::ImGuiBackendShutdown() {
#ifdef ENABLE_OOT3D_VULKAN
    if (mImpl.Backend == WindowBackend::FAST3D_SDL_OOT3D_VULKAN) {
        if (auto interpreter = mInterpreter.lock()) {
            auto* api = dynamic_cast<GfxRenderingAPIVulkan*>(interpreter->GetCurrentRenderingAPI());
            if (api != nullptr) api->ShutdownImGuiBackend();
        }
    }
#endif
    switch (mImpl.Backend) {
#ifdef ENABLE_OPENGL
        case WindowBackend::FAST3D_SDL_OPENGL:
            ImGui_ImplOpenGL3_Shutdown();
            break;
#endif
#if __APPLE__
        case WindowBackend::FAST3D_SDL_METAL:
            ImGui_ImplMetal_Shutdown();
            break;
#endif
#if defined(ENABLE_DX11) || defined(ENABLE_DX12)
        case WindowBackend::FAST3D_DXGI_DX11:
            ImGui_ImplDX11_Shutdown();
            break;
#endif
        default:
            break;
    }
}

void Fast3dGui::ImGuiBackendNewFrame() {
    switch (mImpl.Backend) {
#ifdef ENABLE_OPENGL
        case WindowBackend::FAST3D_SDL_OPENGL:
            ImGui_ImplOpenGL3_NewFrame();
            break;
#endif

#ifdef ENABLE_DX11
        case WindowBackend::FAST3D_DXGI_DX11:
            ImGui_ImplDX11_NewFrame();
            break;
#endif

#ifdef __APPLE__
        case WindowBackend::FAST3D_SDL_METAL: {
            GfxRenderingAPIMetal* api = (GfxRenderingAPIMetal*)mInterpreter.lock()->GetCurrentRenderingAPI();
            api->NewFrame();
            break;
        }
#endif
        default:
            break;
    }
#ifdef ENABLE_OOT3D_VULKAN
    if (mImpl.Backend == WindowBackend::FAST3D_SDL_OOT3D_VULKAN) {
        if (auto interpreter = mInterpreter.lock()) {
            auto* api = dynamic_cast<GfxRenderingAPIVulkan*>(interpreter->GetCurrentRenderingAPI());
            if (api != nullptr) api->NewImGuiFrame();
        }
    }
#endif
}

void Fast3dGui::SyncOot3dVulkanMousePosition() {
#ifdef ENABLE_OOT3D_VULKAN
    if (mImpl.Backend != WindowBackend::FAST3D_SDL_OOT3D_VULKAN) {
        return;
    }
    auto* window = static_cast<SDL_Window*>(mImpl.Vulkan.Window);
    const bool focused = SDL_GetMouseFocus() == window ||
                         SDL_GetKeyboardFocus() == window;
    ImGuiIO& io = ImGui::GetIO();
    const Oot3dMousePosition mousePosition = GetOot3dMousePosition(window);
    // Re-publish after ImGui_ImplSDL2_NewFrame so this Vulkan-only bridge wins
    // over any stale position retained by the stock backend.
    if (focused) {
        io.AddMousePosEvent(mousePosition.X, mousePosition.Y);
    }
#endif
}

void Fast3dGui::ImGuiWMNewFrame() {
    switch (mImpl.Backend) {
        case WindowBackend::FAST3D_SDL_OPENGL:
        case WindowBackend::FAST3D_SDL_METAL:
#ifdef ENABLE_OOT3D_VULKAN
        case WindowBackend::FAST3D_SDL_OOT3D_VULKAN:
#endif
            ImGui_ImplSDL2_NewFrame();
#ifdef ENABLE_OOT3D_VULKAN
            if (mImpl.Backend == WindowBackend::FAST3D_SDL_OOT3D_VULKAN) {
                // This renderer owns only the main SDL Vulkan swapchain.
                // Reassert the invariant every frame because runtime/profile
                // initialization may mutate ImGui configuration after Gui::Init.
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
                // SDL's Win32 coordinates become inconsistent after some
                // resize/mode transitions. Publish the native-client mapping
                // last so ImGui always uses the renderer's layout space.
                SyncOot3dVulkanMousePosition();
            }
#endif
            break;
#ifdef ENABLE_DX11
        case WindowBackend::FAST3D_DXGI_DX11:
            ImGui_ImplWin32_NewFrame();
            break;
#endif
        default:
            break;
    }
}

void Fast3dGui::ImGuiRenderDrawData(ImDrawData* data) {
    switch (mImpl.Backend) {
#ifdef ENABLE_OPENGL
        case WindowBackend::FAST3D_SDL_OPENGL:
            ImGui_ImplOpenGL3_RenderDrawData(data);
            break;
#endif

#ifdef __APPLE__
        case WindowBackend::FAST3D_SDL_METAL: {
            GfxRenderingAPIMetal* api = (GfxRenderingAPIMetal*)mInterpreter.lock()->GetCurrentRenderingAPI();
            api->RenderDrawData(data);
            break;
        }
#endif

#ifdef ENABLE_DX11
        case WindowBackend::FAST3D_DXGI_DX11:
            ImGui_ImplDX11_RenderDrawData(data);
            break;
#endif
        default:
            break;
    }
#ifdef ENABLE_OOT3D_VULKAN
    if (mImpl.Backend == WindowBackend::FAST3D_SDL_OOT3D_VULKAN) {
        if (auto interpreter = mInterpreter.lock()) {
            auto* api = dynamic_cast<GfxRenderingAPIVulkan*>(interpreter->GetCurrentRenderingAPI());
            if (api != nullptr) api->RenderImGuiDrawData(data);
        }
    }
#endif
}

void Fast3dGui::DrawFloatingWindows() {
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
        return;
    }

    // OpenGL requires extra platform handling for the GL context
    if (mImpl.Backend == WindowBackend::FAST3D_SDL_OPENGL && mImpl.Opengl.Context != nullptr) {
        // Backup window and context before calling RenderPlatformWindowsDefault
        SDL_Window* backupCurrentWindow = SDL_GL_GetCurrentWindow();
        SDL_GLContext backupCurrentContext = SDL_GL_GetCurrentContext();

        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();

        // Restore GL context for next frame
        SDL_GL_MakeCurrent(backupCurrentWindow, backupCurrentContext);
    } else {
#ifdef __APPLE__
        // Metal requires additional frame setup to get ImGui ready for drawing floating windows
        if (mImpl.Backend == WindowBackend::FAST3D_SDL_METAL) {
            GfxRenderingAPIMetal* api = (GfxRenderingAPIMetal*)mInterpreter.lock()->GetCurrentRenderingAPI();
            api->SetupFloatingFrame();
        }
#endif

        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void Fast3dGui::CalculateGameViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;

    ImGui::Begin("Main Game", nullptr, flags);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    ImVec2 mainPos = ImGui::GetWindowPos();
    mainPos.x -= mTemporaryWindowPos.x;
    mainPos.y -= mTemporaryWindowPos.y;
    ImVec2 size = ImGui::GetContentRegionAvail();
    const auto interpreter = mInterpreter.lock().get();
    interpreter->mCurDimensions.width = (uint32_t)(size.x * mInterpreter.lock()->mCurDimensions.internal_mul);
    interpreter->mCurDimensions.height = (uint32_t)(size.y * mInterpreter.lock()->mCurDimensions.internal_mul);
    interpreter->mGameWindowViewport.x = (int16_t)mainPos.x;
    interpreter->mGameWindowViewport.y = (int16_t)mainPos.y;
    interpreter->mGameWindowViewport.width = (int16_t)size.x;
    interpreter->mGameWindowViewport.height = (int16_t)size.y;

    if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".Enabled",
                                                                           0)) {
        ApplyResolutionChanges();
    }

    switch (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_LOW_RES_MODE, 0)) {
        case 1: { // N64 Mode
            interpreter->mCurDimensions.width = 320;
            interpreter->mCurDimensions.height = 240;
            /*
            const int sw = size.y * 320 / 240;
            mInterpreter.lock()->mGameWindowViewport.x += ((int)size.x - sw) / 2;
            mInterpreter.lock()->mGameWindowViewport.width = sw;*/
            break;
        }
        case 2: { // 240p Widescreen
            constexpr int vertRes = 240;
            interpreter->mCurDimensions.width = vertRes * size.x / size.y;
            interpreter->mCurDimensions.height = vertRes;
            break;
        }
        case 3: { // 480p Widescreen
            constexpr int vertRes = 480;
            interpreter->mCurDimensions.width = vertRes * size.x / size.y;
            interpreter->mCurDimensions.height = vertRes;
            break;
        }
    }

    ImGui::End();
}

void Fast3dGui::DrawGame() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;

    ImGui::Begin("Main Game", nullptr, flags);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    GetGameOverlay()->Draw();

    ImVec2 mainPos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 pos = ImVec2(0, 0);
    const auto interpreter = mInterpreter.lock().get();

    if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_LOW_RES_MODE, 0) ==
        1) { // N64 Mode takes priority
        const float sw = size.y * 320.0f / 240.0f;
        pos = ImVec2(floor(size.x / 2 - sw / 2), 0);
        size = ImVec2(sw, size.y);
    } else if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
                   CVAR_PREFIX_ADVANCED_RESOLUTION ".Enabled", 0)) {
        if (!Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
                CVAR_PREFIX_ADVANCED_RESOLUTION ".PixelPerfectMode", 0)) {
            if (!Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
                    CVAR_PREFIX_ADVANCED_RESOLUTION ".IgnoreAspectCorrection", 0)) {
                float sWdth = size.y * interpreter->mCurDimensions.width / interpreter->mCurDimensions.height;
                float sHght = size.x * interpreter->mCurDimensions.height / interpreter->mCurDimensions.width;

                float sPosX = floor(size.x / 2.0f - sWdth / 2.0f);
                float sPosY = floor(size.y / 2.0f - sHght / 2.0f);
                if (sPosY < 0.0f) { // pillarbox
                    sPosY = 0.0f;   // clamp y position
                    sHght = size.y; // reset height
                }
                if (sPosX < 0.0f) { // letterbox
                    sPosX = 0.0f;   // clamp x position
                    sWdth = size.x; // reset width
                }
                pos = ImVec2(sPosX, sPosY);
                size = ImVec2(sWdth, sHght);
            }
        } else { // in pixel perfect mode it's much easier
            const int factor = GetIntegerScaleFactor();
            float sPosX = floor(size.x / 2.0f - (interpreter->mCurDimensions.width * factor) / 2.0f);
            float sPosY = floor(size.y / 2.0f - (interpreter->mCurDimensions.height * factor) / 2.0f);
            pos = ImVec2(sPosX, sPosY);
            size = ImVec2(float(interpreter->mCurDimensions.width) * factor,
                          float(interpreter->mCurDimensions.height) * factor);
        }
    }
    uintptr_t fb = Ship::Context::GetRawInstance()->GetWindow()->GetGfxFrameBuffer();
    if (fb) {
        ImGui::SetCursorPos(pos);
        ImGui::Image(reinterpret_cast<ImTextureID>(fb), size);
    }

    ImGui::End();
}

void Fast3dGui::ApplyResolutionChanges() {
    ImVec2 size = ImGui::GetContentRegionAvail();

    const float aspectRatioX = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetFloat(
        CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioX", 16.0f);
    const float aspectRatioY = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetFloat(
        CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioY", 9.0f);
    const uint32_t verticalPixelCount = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
        CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalPixelCount", 480);
    const bool verticalResolutionToggle = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
        CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalResolutionToggle", 0);

    const bool aspectRatioIsEnabled = (aspectRatioX > 0.0f) && (aspectRatioY > 0.0f);

    constexpr uint32_t minResolutionWidth = 320;
    constexpr uint32_t minResolutionHeight = 240;
    constexpr uint32_t maxResolutionWidth = 8096;  // the renderer's actual limit is 16384
    constexpr uint32_t maxResolutionHeight = 4320; // on either axis. if you have the VRAM for it.
    uint32_t newWidth;
    uint32_t newHeight;
    const auto interpreter = mInterpreter.lock().get();
    interpreter->GetCurDimensions(&newWidth, &newHeight);

    if (verticalResolutionToggle) { // Use fixed vertical resolution
        if (aspectRatioIsEnabled) {
            newWidth = uint32_t(float(verticalPixelCount / aspectRatioY) * aspectRatioX);
        } else {
            newWidth = uint32_t(float(verticalPixelCount * size.x / size.y));
        }
        newHeight = verticalPixelCount;
    } else { // Use the window's resolution
        if (aspectRatioIsEnabled) {
            if (((float)interpreter->mGameWindowViewport.height / interpreter->mGameWindowViewport.width) <
                (aspectRatioY / aspectRatioX)) {
                // when pillarboxed
                newWidth = uint32_t(float(interpreter->mCurDimensions.height / aspectRatioY) * aspectRatioX);
            } else { // when letterboxed
                newHeight = uint32_t(float(interpreter->mCurDimensions.width / aspectRatioX) * aspectRatioY);
            }
        } // else, having both options turned off does nothing.
    }
    // clamp values to prevent renderer crash
    if (newWidth < minResolutionWidth) {
        newWidth = minResolutionWidth;
    }
    if (newHeight < minResolutionHeight) {
        newHeight = minResolutionHeight;
    }
    if (newWidth > maxResolutionWidth) {
        newWidth = maxResolutionWidth;
    }
    if (newHeight > maxResolutionHeight) {
        newHeight = maxResolutionHeight;
    }
    // apply new dimensions
    interpreter->mCurDimensions.width = newWidth;
    interpreter->mCurDimensions.height = newHeight;
    // centring the image is done in Fast3dGui::DrawGame().
}

int16_t Fast3dGui::GetIntegerScaleFactor() {
    const auto interpreter = mInterpreter.lock().get();
    if (!Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.FitAutomatically", 0)) {
        int16_t factor = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.Factor", 1);

        if (Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
                CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.NeverExceedBounds", 1)) {
            if (((float)interpreter->mGameWindowViewport.height / interpreter->mGameWindowViewport.width) <
                ((float)interpreter->mCurDimensions.height / interpreter->mCurDimensions.width)) {
                if ((uint32_t)factor > interpreter->mGameWindowViewport.height / interpreter->mCurDimensions.height) {
                    factor = interpreter->mGameWindowViewport.height / interpreter->mCurDimensions.height;
                }
            } else {
                if ((uint32_t)factor > interpreter->mGameWindowViewport.width / interpreter->mCurDimensions.width) {
                    factor = interpreter->mGameWindowViewport.width / interpreter->mCurDimensions.width;
                }
            }
        }

        if (factor < 1) {
            factor = 1;
        }
        return factor;
    } else {
        int16_t factor = 1;

        if (((float)interpreter->mGameWindowViewport.height / interpreter->mGameWindowViewport.width) <
            ((float)interpreter->mCurDimensions.height / interpreter->mCurDimensions.width)) {
            factor = interpreter->mGameWindowViewport.height / interpreter->mCurDimensions.height;
        } else {
            factor = interpreter->mGameWindowViewport.width / interpreter->mCurDimensions.width;
        }

        factor += Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(
            CVAR_PREFIX_ADVANCED_RESOLUTION ".IntegerScale.ExceedBoundsBy", 0);

        if (factor < 1) {
            factor = 1;
        }
        return factor;
    }
}

ImTextureID Fast3dGui::GetTextureById(int32_t id) {
    GfxRenderingAPI* api = mInterpreter.lock()->GetCurrentRenderingAPI();
    return api->GetTextureById(id);
}

bool Fast3dGui::HasTextureByName(const std::string& name) {
    return mGuiTextures.contains(name);
}

ImTextureID Fast3dGui::GetTextureByName(const std::string& name) {
    if (!HasTextureByName(name)) {
        return nullptr;
    }
    return GetTextureById(mGuiTextures[name].RendererTextureId);
}

ImVec2 Fast3dGui::GetTextureSize(const std::string& name) {
    if (!HasTextureByName(name)) {
        return ImVec2(0, 0);
    }
    return ImVec2(mGuiTextures[name].Width, mGuiTextures[name].Height);
}

void Fast3dGui::LoadTextureFromRawImage(const std::string& name, const std::string& path) {
    auto initData = std::make_shared<Ship::ResourceInitData>();
    initData->Format = RESOURCE_FORMAT_BINARY;
    initData->Type = static_cast<uint32_t>(RESOURCE_TYPE_GUI_TEXTURE);
    initData->ResourceVersion = 0;
    initData->Path = path;
    auto guiTexture = std::static_pointer_cast<Ship::GuiTexture>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path, false, initData));

    LoadTextureFromResource(name, guiTexture);
}

void Fast3dGui::LoadTextureFromResource(const std::string& name, std::shared_ptr<Ship::GuiTexture> texture) {
    GfxRenderingAPI* api = mInterpreter.lock()->GetCurrentRenderingAPI();

    // TODO: Nothing ever unloads the texture from Fast3D here.
    texture->Metadata.RendererTextureId = api->NewTexture();
    api->SelectTexture(0, texture->Metadata.RendererTextureId);
    api->SetSamplerParameters(0, false, 0, 0);
    api->UploadTexture(texture->Data, texture->Metadata.Width, texture->Metadata.Height);

    mGuiTextures[name] = texture->Metadata;
}

void Fast3dGui::LoadGuiTexture(const std::string& name, const Fast::Texture& res, const ImVec4& tint) {
    GfxRenderingAPI* api = mInterpreter.lock()->GetCurrentRenderingAPI();
    std::vector<uint8_t> texBuffer;
    texBuffer.reserve(res.Width * res.Height * 4);

    // For HD textures we need to load the buffer raw (similar to inside gfx_pp)
    if ((res.Flags & TEX_FLAG_LOAD_AS_RAW) != 0) {
        // Raw loading doesn't support TLUT textures
        if (res.Type == Fast::TextureType::Palette4bpp || res.Type == Fast::TextureType::Palette8bpp) {
            // TODO convert other image types
            SPDLOG_WARN("ImGui::ResourceLoad: Attempting to load unsupported image type");
            return;
        }

        texBuffer.assign(res.ImageData, res.ImageData + (res.Width * res.Height * 4));
    } else {
        switch (res.Type) {
            case Fast::TextureType::RGBA32bpp:
                texBuffer.assign(res.ImageData, res.ImageData + (res.Width * res.Height * 4));
                break;
            case Fast::TextureType::RGBA16bpp: {
                for (int32_t i = 0; i < res.Width * res.Height; i++) {
                    uint8_t b1 = res.ImageData[i * 2 + 0];
                    uint8_t b2 = res.ImageData[i * 2 + 1];
                    uint8_t r = (b1 >> 3) * 0xFF / 0x1F;
                    uint8_t g = (((b1 & 7) << 2) | (b2 >> 6)) * 0xFF / 0x1F;
                    uint8_t b = ((b2 >> 1) & 0x1F) * 0xFF / 0x1F;
                    uint8_t a = 0xFF * (b2 & 1);
                    texBuffer.push_back(r);
                    texBuffer.push_back(g);
                    texBuffer.push_back(b);
                    texBuffer.push_back(a);
                }
                break;
            }
            case Fast::TextureType::GrayscaleAlpha16bpp: {
                for (int32_t i = 0; i < res.Width * res.Height; i++) {
                    uint8_t color = res.ImageData[i * 2 + 0];
                    uint8_t alpha = res.ImageData[i * 2 + 1];
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(alpha);
                }
                break;
            }
            case Fast::TextureType::GrayscaleAlpha8bpp: {
                for (int32_t i = 0; i < res.Width * res.Height; i++) {
                    uint8_t ia = res.ImageData[i];
                    uint8_t color = ((ia >> 4) & 0xF) * 255 / 15;
                    uint8_t alpha = (ia & 0xF) * 255 / 15;
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(alpha);
                }
                break;
            }
            case Fast::TextureType::GrayscaleAlpha4bpp: {
                for (int32_t i = 0; i < res.Width * res.Height; i += 2) {
                    uint8_t b = res.ImageData[i / 2];

                    uint8_t ia4 = b >> 4;
                    uint8_t color = ((ia4 >> 1) & 0xF) * 255 / 0b111;
                    uint8_t alpha = (ia4 & 1) * 255;
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(alpha);

                    ia4 = b & 0xF;
                    color = ((ia4 >> 1) & 0xF) * 255 / 0b111;
                    alpha = (ia4 & 1) * 255;
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(color);
                    texBuffer.push_back(alpha);
                }
                break;
            }
            case Fast::TextureType::Grayscale8bpp: {
                for (int32_t i = 0; i < res.Width * res.Height; i++) {
                    uint8_t ia = res.ImageData[i];
                    texBuffer.push_back(ia);
                    texBuffer.push_back(ia);
                    texBuffer.push_back(ia);
                    texBuffer.push_back(ia);
                }
                break;
            }
            case Fast::TextureType::Grayscale4bpp: {
                for (int32_t i = 0; i < res.Width * res.Height; i += 2) {
                    uint8_t b = res.ImageData[i / 2];

                    uint8_t ia4 = ((b >> 4) * 0xFF) / 0b1111;
                    texBuffer.push_back(ia4);
                    texBuffer.push_back(ia4);
                    texBuffer.push_back(ia4);
                    texBuffer.push_back(ia4);

                    ia4 = ((b & 0xF) * 0xFF) / 0b1111;
                    texBuffer.push_back(ia4);
                    texBuffer.push_back(ia4);
                    texBuffer.push_back(ia4);
                    texBuffer.push_back(ia4);
                }
                break;
            }
            default:
                // TODO convert other image types
                SPDLOG_WARN("ImGui::ResourceLoad: Attempting to load unsupported image type");
                return;
        }
    }

    for (size_t pixel = 0; pixel < texBuffer.size() / 4; pixel++) {
        texBuffer[pixel * 4 + 0] *= tint.x;
        texBuffer[pixel * 4 + 1] *= tint.y;
        texBuffer[pixel * 4 + 2] *= tint.z;
        texBuffer[pixel * 4 + 3] *= tint.w;
    }

    Ship::GuiTextureMetadata asset;
    asset.RendererTextureId = api->NewTexture();
    asset.Width = res.Width;
    asset.Height = res.Height;

    api->SelectTexture(0, asset.RendererTextureId);
    api->SetSamplerParameters(0, false, 0, 0);
    api->UploadTexture(texBuffer.data(), res.Width, res.Height);

    mGuiTextures[name] = asset;
}

void Fast3dGui::LoadGuiTexture(const std::string& name, const std::string& path, const ImVec4& tint) {
    const auto res = static_cast<Fast::Texture*>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path, true).get());

    LoadGuiTexture(name, *res, tint);
}

void Fast3dGui::UnloadTexture(const std::string& name) {
    if (mGuiTextures.contains(name)) {
        Ship::GuiTextureMetadata tex = mGuiTextures[name];
        GfxRenderingAPI* api = mInterpreter.lock()->GetCurrentRenderingAPI();
        api->DeleteTexture(tex.RendererTextureId);
        mGuiTextures.erase(name);
    }
}

} // namespace Fast
