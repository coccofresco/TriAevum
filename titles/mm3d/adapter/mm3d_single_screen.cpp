#include "mm3d_single_screen.h"
#include "mm3d_button_overlay.h"
#include "oot3d_top_screen_mod_profile.h"
#include "../dekui/button_glyph_emission.h"

#include "core.h"
#include "settings.h"
#include "mm3d_a32_substitute.h"
#include "mm3d_layout_stack_probe.h"
#include "mm3d_restoration.h"

#include <cstdio>
#include <mutex>
#include <cstdlib>

namespace Oot3dNativeGame {

namespace a32 = oot3d::recomp::a32;

namespace {

class BusMemory final : public Mm3dDekui::Memory {
  public:
    explicit BusMemory(a32::MemoryBus& bus,
                       const Mm3dRestorationPort* restoration = nullptr,
                       const Mm3dSingleScreen::QuadQueueObserver* observer = nullptr) noexcept
        : mBus(bus), mRestoration(restoration), mQuadQueueObserver(observer) {}
    void NativeQuadQueueCommitted(std::span<const Mm3dDekui::NativeQuadQueueMove> moves) override {
        if (mQuadQueueObserver && *mQuadQueueObserver) (*mQuadQueueObserver)(moves, mBus);
    }
    // Project Restoration's appended code, ported as source: dekui's checks
    // of that code accept the port's verified sites instead.
    bool RestorationMessagePort() const override {
        return mRestoration != nullptr && mRestoration->Capabilities(mBus).message_getter;
    }
    bool RestorationCameraPort() const override {
        return mRestoration != nullptr && mRestoration->Capabilities(mBus).camera_bridge;
    }
    Mm3dDekui::U32 RestorationHeartWidget() const override {
        return mRestoration != nullptr ? mRestoration->HudHeartWidget() : 0U;
    }
    std::optional<Mm3dDekui::U32> Read(Mm3dDekui::U32 address) override {
        std::uint32_t value = 0;
        if (!mBus.Read32(address, &value)) {
            return std::nullopt;
        }
        return value;
    }
    void Write(Mm3dDekui::U32 address, Mm3dDekui::U32 value) override {
        (void)mBus.Write32(address, value);
    }
    std::uint8_t Read8(Mm3dDekui::U32 address) override {
        std::uint8_t value = 0;
        return mBus.Read8(address, &value) ? value : 0U;
    }

  private:
    a32::MemoryBus& mBus;
    const Mm3dRestorationPort* mRestoration;
    const Mm3dSingleScreen::QuadQueueObserver* mQuadQueueObserver;
};

// The guest thread's state in dynarmic's register view: S0..S31 are copied
// into the 64-word extension bank and back.
class StateCpu final : public Mm3dDekui::Cpu {
  public:
    explicit StateCpu(a32::GuestState& state) : mState(state) {
        std::copy(state.vfp.begin(), state.vfp.end(), mExt.begin());
    }
    ~StateCpu() override { std::copy(mExt.begin(), mExt.begin() + 32, mState.vfp.begin()); }
    std::array<Mm3dDekui::U32, 16>& Regs() override { return mState.r; }
    Mm3dDekui::U32 Cpsr() override { return mState.cpsr; }
    void SetCpsr(Mm3dDekui::U32 value) override { mState.cpsr = value; }
    std::array<Mm3dDekui::U32, 64>& ExtRegs() override { return mExt; }

  private:
    a32::GuestState& mState;
    std::array<Mm3dDekui::U32, 64> mExt{};
};

constexpr std::uint32_t kThumbBit = 0x20U;

static_assert(Mm3dLayoutStackProbe::kRootPc == Mm3dDekui::NativeInventory::PCs[2] &&
              Mm3dLayoutStackProbe::kEndPc == Mm3dDekui::NativeInventory::PCs[3]);

} // namespace

Mm3dSingleScreen::Mm3dSingleScreen(std::function<std::int64_t()> globalTimeUs)
    : mCore(std::make_unique<Mm3dDekui::Core>(Mm3dDekui::HostFacts{std::move(globalTimeUs), {}})),
      mButtonOverlay(std::make_unique<Mm3dButtonOverlay>()),
      mStackProbe(Mm3dLayoutStackProbe::FromEnvironment()) {
    mCore->host.layout_draw = [this](Mm3dDekui::Memory& memory, uint32_t layout, uint32_t root, uint32_t pass) {
        return mButtonOverlay->Capture(memory, layout, root, pass);
    };
    // Sole owner of the inline-chord look choice (owner decision pending):
    // TRIAEVUM_MM3D_INLINE_CHORDS=abbreviated (default) | chord.
    if (const char* style = std::getenv("TRIAEVUM_MM3D_INLINE_CHORDS"))
        if (const auto parsed = Mm3dButtonOverlay::ParseInlineChordStyle(style))
            mButtonOverlay->SetInlineChordStyle(*parsed);
}

void Mm3dSingleScreen::ConfigureButtonOverlay(const NativeControlConfig& config, NativePromptSet prompts) {
    mButtonOverlay->Configure(config, prompts);
    // Touch prompts: the native HUD groups move into the anchored touch
    // controls (OoT3D TopScreen's shape and fill), on the 16:9 MM canvas.
    mButtonOverlay->ApplyTouchHud(ThreeDsRecomp::Input::OverlayTouchAnchors().Snapshot(), 240.0F * 16.0F / 9.0F,
                                  ResolveTopScreenTouchControlShape);
}
void Mm3dSingleScreen::SubmitButtonOverlayCommandList(uint32_t address,uint32_t bytes) { mButtonOverlay->SubmitCommandList(address,bytes); }
void Mm3dSingleScreen::ObserveButtonOverlayDraw(uint64_t draw) { mButtonOverlay->ObserveDraw(draw); }
void Mm3dSingleScreen::SelectButtonOverlay(std::span<const uint64_t> draws) { mButtonOverlay->Select(draws); }
void Mm3dSingleScreen::ResetButtonOverlay() { mButtonOverlay->Reset(); mCore->ResetButtonEmission(); }
std::vector<oot3d::ui::UiPrimitive> Mm3dSingleScreen::ButtonOverlay(float scale, bool gameplayHudVisible) const { return mButtonOverlay->Compose(scale, gameplayHudVisible); }
std::map<std::string, uint64_t> Mm3dSingleScreen::ButtonOverlaySummary() const {
    const auto& s = mButtonOverlay->Stats();
    return {{"emissions",s.Layouts},{"labels",s.Labels},{"parts",s.Parts},{"hud",s.Hud},
            {"rings",s.Rings},{"frames",s.Frames},{"presented",s.Presented},
            {"pictograms",s.Pictograms},{"dpad_directions",s.Dpad},{"zr_legends",s.Text},
             {"saturated_alpha",s.SaturatedAlpha},{"standalone_labels",s.StandaloneLabels},
             {"empty_records",s.EmptyRecords},{"pass_rejected",s.PassRejected},{"guard_rejected",s.GuardRejected},
             {"candidates",s.Candidates},{"tint_rejected",s.TintRejected},{"pose_rejected",s.PoseRejected},
              {"unbound_candidates",s.UnboundCandidates},{"ring_geometry_rejected",s.RingGeometryRejected},
              {"capacity_rejected",s.CapacityRejected},{"duplicate_candidates",s.DuplicateCandidates},
              {"pending_labels",mButtonOverlay->PendingLabels()},{"inline_chord_form",mButtonOverlay->GetInlineChordStyle()==Mm3dButtonOverlay::InlineChordStyle::Chord?1u:0u},{"pending_panes",mButtonOverlay->PendingPanes()},
              {"inline_glyphs",s.InlineGlyphs}};
}

Mm3dSingleScreen::~Mm3dSingleScreen() = default;

namespace {
// The settings last applied to dekui's live owners; the defaults until then.
std::mutex gActiveSettingsMutex;
Mm3dDekui::Settings& ActiveSettings() {
    static Mm3dDekui::Settings settings = [] {
        Mm3dDekui::Settings defaults;
        (void)defaults.LoadFromString(Mm3dDekui::DefaultSettingsJson());
        return defaults;
    }();
    return settings;
}
} // namespace

void Mm3dSingleScreen::ResetHostState() {
    auto facts = mCore->host;
    mCore = std::make_unique<Mm3dDekui::Core>(std::move(facts));
    ++mHostStateGeneration;
}

std::string Mm3dSingleScreen::ActiveSettingsJson() {
    std::scoped_lock lock(gActiveSettingsMutex);
    return ActiveSettings().SaveToString();
}

bool Mm3dSingleScreen::ApplySettingsJson(std::string_view text, std::string* error) {
    Mm3dDekui::Settings settings;
    if (!settings.LoadFromString(text)) {
        if (error != nullptr) *error = "unreadable dekui settings (version 1 presentation.json expected)";
        return false;
    }
    Mm3dDekui::ApplySettings(settings);
    std::scoped_lock lock(gActiveSettingsMutex);
    ActiveSettings() = std::move(settings);
    return true;
}

bool Mm3dSingleScreen::LoadSettings(const std::filesystem::path& path) {
    Mm3dDekui::Settings settings;
    bool loaded = true;
    std::error_code error;
    if (path.empty() || !std::filesystem::exists(path, error) || !settings.Load(path)) {
        loaded = path.empty() || !std::filesystem::exists(path, error);
        settings = Mm3dDekui::Settings{};
        if (!settings.LoadFromString(Mm3dDekui::DefaultSettingsJson())) {
            return false;
        }
    }
    Mm3dDekui::ApplyStartupSettings(settings);
    {
        std::scoped_lock lock(gActiveSettingsMutex);
        ActiveSettings() = std::move(settings);
    }
    return loaded;
}

void Mm3dSingleScreen::BindRestoration(Mm3dRestorationPort& restoration) {
    mRestoration = &restoration;
    // The port's calls into dekui (form camera, camera mode, message entry)
    // reach this Core, which holds the state the native sites share.
    restoration.SetDekuiBridge([this](std::uint32_t svc, a32::GuestState& state,
                                      a32::MemoryBus& bus) {
        StateCpu cpu(state);
        BusMemory memory(bus, mRestoration);
        mCore->CallSvc(svc, cpu, memory);
    });
}

void Mm3dSingleScreen::SetDamageMultiplier(int multiplier) {
    Mm3dDekui::Difficulty::damage_multiplier = multiplier;
    std::scoped_lock lock(gActiveSettingsMutex);
    ActiveSettings().damage_multiplier = multiplier;
}

std::vector<std::uint32_t> Mm3dSingleScreen::CandidatePcs() {
    return Mm3dDekui::Core::CandidatePcs();
}

void Mm3dSingleScreen::Register(Mm3dGuestPatches& patches) {
    Mm3dGuestPatchSite glyph;
    glyph.Pc=Mm3dDekui::ButtonGlyphEmission::Pc;
    glyph.Name="overlay_inline_glyph";
    glyph.Guard.assign(Mm3dDekui::ButtonGlyphEmission::Guards.begin(),Mm3dDekui::ButtonGlyphEmission::Guards.end());
    glyph.Handler=[this](a32::GuestState& state,a32::MemoryBus& bus)->std::optional<uint32_t> {
        const auto caller=state.r[14];
        if(caller!=Mm3dDekui::ButtonGlyphEmission::MainReturn && caller!=Mm3dDekui::ButtonGlyphEmission::ShadowReturn)return {};
        const auto pass=mCore->ButtonEmissionPass();
        if(pass>3 && pass!=~0u)return {};
        BusMemory memory(bus,mRestoration);
        const bool shadow=caller==Mm3dDekui::ButtonGlyphEmission::ShadowReturn;
        if(!mButtonOverlay->CaptureInline(memory,state.r[2],state.r[3],shadow))return {};
        // No native commands appended. Preserve flags, SP, LR, VFP and every
        // other register; ordinary prose/unknown records execute unchanged.
        state.r[0]=state.r[1];
        return caller;
    };
    patches.Add(std::move(glyph));
    // Where dekui's handlers branch into the middle of guest functions
    // (titles/mm3d/dekui: inventory navigation, item present, dungeon map,
    // death penalties, menu memory and page switch, prices, Bomber approach,
    // owl introduction).
    patches.AddTargets({
        0x0056E494U, // BomberApproach::ResetPC (reference 0x56e324)
        0x0043FC50U, // OwlIntro::IntroReturn, native pop (reference 0x43faf8)
        0x0058DA44U, // OwlIntro::Idle (reference 0x58d8d4)
        0x005A5850U, 0x005A58BCU,                 // InventoryNavigation::GearMove (ref 0x5a56e0, 0x5a574c)
        0x005A1020U, 0x005A1630U,                 // InventoryNavigation::Move (ref 0x5a0eb0, 0x5a14c0)
        0x001AD990U,                              // NativeItemPresent intro BL (ref 0x1adb90)
        Mm3dDekui::DungeonMap::WorldEnd,          // (ref 0x596ff4)
        Mm3dDekui::DungeonMap::SetZoom,           // site 5 call target (ref 0x1b2c48)
        Mm3dDekui::DungeonMap::OverviewCursorEnd, // site 7 skip target (ref 0x594b68)
        Mm3dDekui::DeathPenalties::NativeRespawn, // (ref 0x190a9c)
        Mm3dDekui::DeathPenalties::NativeMoonCrash, // (ref 0x1bfdc8)
        0x001BB118U,                              // MenuMemory / MenuPageSwitch tail (ref 0x1bb320)
        0x002004D4U,                              // MenuPageSwitch, play sound (ref 0x2006e4)
        0x00198708U,                              // MenuPageSwitch (ref 0x198864)
        0x005A6170U,                              // MenuPageSwitch (ref 0x5a6000)
        Mm3dDekui::Prices::ChangeRupees,          // (ref 0x2c1634)
    });
    for (const std::uint32_t pc : CandidatePcs()) {
        char name[32];
        std::snprintf(name, sizeof(name), "dekui@%08X", pc);
        Mm3dGuestPatchSite site;
        site.Pc = pc;
        site.Name = name;
        site.Prepare = [this, pc](a32::MemoryBus& bus) {
            std::uint32_t word = 0;
            if (!bus.Read32(pc, &word)) {
                return false;
            }
            BusMemory memory(bus, mRestoration);
            const std::uint32_t replacement = mCore->Instruction(memory, pc, word);
            if (replacement == word) {
                return false;
            }
            mReplacements[pc] = replacement;
            return true;
        };
        site.Handler = [this, pc](a32::GuestState& state,
                                  a32::MemoryBus& bus) -> std::optional<std::uint32_t> {
            const std::uint32_t replacement = mReplacements.at(pc);
            if ((replacement & 0x0F000000U) != 0x0F000000U) {
                return ExecuteMm3dSubstitute(replacement, pc, state, bus);
            }
            if (!Mm3dConditionPassed(replacement, state.cpsr)) {
                return pc + 4U;
            }
            state.r[15] = pc + 4U;
            if (mStackProbe != nullptr) {
                if (pc == Mm3dLayoutStackProbe::kRootPc) {
                    mStackProbe->BeforeRoot(state, bus);
                } else if (pc == Mm3dLayoutStackProbe::kEndPc) {
                    mStackProbe->BeforeEnd(state, bus);
                }
            }
            {
                StateCpu cpu(state);
                BusMemory memory(bus, mRestoration, &mQuadQueueObserver);
                mCore->CallSvc(replacement & 0x00FFFFFFU, cpu, memory);
            }
            if (mStackProbe != nullptr && pc == Mm3dLayoutStackProbe::kRootPc) {
                mStackProbe->AfterRoot(state, bus, mCore->HudItemBackgroundScratchSp());
            }
            // dekui leaves Thumb as the CPSR T bit; the A32 runtime keeps it
            // in the PC.
            std::uint32_t next = state.r[15];
            if ((state.cpsr & kThumbBit) != 0U) {
                state.cpsr &= ~kThumbBit;
                next |= 1U;
            }
            return next;
        };
        patches.Add(std::move(site));
    }
}

} // namespace Oot3dNativeGame
