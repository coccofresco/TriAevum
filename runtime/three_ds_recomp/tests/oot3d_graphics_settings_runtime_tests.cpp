#include "fast/extension/graphics_settings_persistence.h"
#include "fast/extension/graphics_settings_runtime.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <memory>

namespace {

class MemoryGraphicsSettingsPersistence final
    : public Fast::Extension::GraphicsSettingsPersistencePort {
  public:
    bool LoadRoot(nlohmann::json& root) override {
        root = Root;
        return true;
    }

    bool StoreGraphics(const nlohmann::json& graphics) override {
        Root["Graphics"] = graphics;
        return true;
    }

    bool StoreGraphicsPresets(const nlohmann::json& presets) override {
        if (FailPresets) return false;
        Root["GraphicsPresets"] = presets;
        return true;
    }

    nlohmann::json Root;
    bool FailPresets = false;
};

// The runtime is a process singleton: every test shares the store installed
// before its construction, which loads FovMultiplier 1.25.
std::shared_ptr<MemoryGraphicsSettingsPersistence> Store() {
    static const auto persistence = [] {
        auto store = std::make_shared<MemoryGraphicsSettingsPersistence>();
        auto initial = Fast::Extension::GraphicsSettingsService::Preset(
            Fast::Extension::GraphicsPreset::Custom);
        initial.FovMultiplier = 1.25F;
        store->Root["Graphics"] = Fast::Extension::SerializeGraphicsSettings(initial);
        Fast::Extension::InstallGraphicsSettingsPersistencePort(store);
        return store;
    }();
    return persistence;
}

Fast::Extension::GraphicsSettingsRuntime& Runtime() {
    Store();
    return Fast::Extension::GraphicsSettingsRuntime::Instance();
}

TEST(GraphicsSettingsRuntimeTest, PublishesChangesAndDefersOutputPersistenceUntilAcknowledged) {
    const auto persistence = Store();
    auto& runtime = Runtime();

    EXPECT_FLOAT_EQ(runtime.Snapshot().FovMultiplier, 1.25F);
    const auto before = runtime.SnapshotWithRevision();
    EXPECT_FLOAT_EQ(before.Value.FovMultiplier, 1.25F);

    auto changed = runtime.Snapshot();
    changed.FovMultiplier = 1.4F;
    const auto validation = runtime.Apply(changed);

    ASSERT_TRUE(validation.Accepted());
    const auto after = runtime.SnapshotWithRevision();
    EXPECT_GT(after.Revision, before.Revision);
    EXPECT_FLOAT_EQ(after.Value.FovMultiplier, 1.4F);
    EXPECT_FLOAT_EQ(
        persistence->Root["Graphics"]["Camera"]["FovMultiplier"]
            .get<float>(),
        1.4F);

    auto resized = runtime.Snapshot();
    resized.OutputWidth = 1920U;
    resized.OutputHeight = 1080U;
    const auto resizeValidation = runtime.Apply(resized);
    ASSERT_TRUE(resizeValidation.Accepted());
    EXPECT_EQ(runtime.PresentationStatus().Phase,
              Fast::Extension::PresentationTransactionPhase::ApplyRequested);
    EXPECT_EQ(persistence->Root["Graphics"]["Output"]["Width"].get<uint32_t>(),
              before.Value.OutputWidth);
    EXPECT_EQ(persistence->Root["Graphics"]["Output"]["Height"].get<uint32_t>(),
              before.Value.OutputHeight);

    ASSERT_TRUE(runtime.AcknowledgePresentationApplied(resized));
    EXPECT_EQ(runtime.PresentationStatus().Phase,
              Fast::Extension::PresentationTransactionPhase::Idle);
    EXPECT_EQ(persistence->Root["Graphics"]["Output"]["Width"]
                  .get<uint32_t>(),
              1920U);
    EXPECT_EQ(persistence->Root["Graphics"]["Output"]["Height"]
                  .get<uint32_t>(),
              1080U);
    EXPECT_FALSE(runtime.TickPresentation());
    // Choosing a size made the request the user's.
    EXPECT_EQ(runtime.Snapshot().OutputProvenance, Fast::WindowExtentProvenance::User);
    EXPECT_EQ(persistence->Root["Graphics"]["Output"]["Provenance"].get<std::string>(), "user");
}

// The host-owned output provenance (UX plan WP-06A): a size the user chooses
// is theirs, a window mode change alone keeps the provenance, and the display
// reset (the safe presentation's) stores the fresh default again.
TEST(GraphicsSettingsRuntimeTest, OutputProvenanceFollowsTheUsersSizeChoice) {
    using namespace Fast::Extension;
    const auto persistence = Store();
    auto& runtime = Runtime();
    const auto settle = [&] {
        if (runtime.PresentationStatus().Phase == PresentationTransactionPhase::ApplyRequested)
            ASSERT_TRUE(runtime.AcknowledgePresentationApplied(runtime.Snapshot()));
        if (runtime.PresentationStatus().Phase == PresentationTransactionPhase::AwaitingConfirmation)
            ASSERT_TRUE(runtime.ConfirmPresentation());
        ASSERT_EQ(runtime.PresentationStatus().Phase, PresentationTransactionPhase::Idle);
    };
    auto user = runtime.Snapshot();
    user.OutputWidth = 1600U;
    user.OutputHeight = 900U;
    ASSERT_TRUE(runtime.Apply(user).Accepted());
    settle();
    ASSERT_EQ(runtime.Snapshot().OutputProvenance, Fast::WindowExtentProvenance::User);

    auto settings = runtime.Snapshot();
    settings.AntiAliasing = AntiAliasingMode::Fxaa;
    settings.InternalResolutionScale = 1.5F;
    ASSERT_TRUE(runtime.Apply(settings).Accepted());
    ASSERT_TRUE(runtime.ResetDisplay().Accepted());
    settle();
    const auto reset = runtime.Snapshot();
    EXPECT_EQ(reset.OutputProvenance, Fast::WindowExtentProvenance::Default);
    EXPECT_EQ(reset.Window, WindowMode::Windowed);
    EXPECT_EQ(reset.OutputWidth, 1280U);
    EXPECT_EQ(reset.OutputHeight, 720U);
    EXPECT_EQ(reset.AntiAliasing, AntiAliasingMode::Off);
    EXPECT_FLOAT_EQ(reset.InternalResolutionScale, 1.0F);
    EXPECT_FLOAT_EQ(reset.FovMultiplier, settings.FovMultiplier); // the rest stays
    EXPECT_EQ(persistence->Root["Graphics"]["Output"]["Provenance"].get<std::string>(), "default");

    // The window mode alone is not a size choice.
    auto borderless = runtime.Snapshot();
    borderless.Window = WindowMode::Borderless;
    ASSERT_TRUE(runtime.Apply(borderless).Accepted());
    EXPECT_EQ(runtime.PresentationStatus().Requested.Provenance, Fast::WindowExtentProvenance::Default);
    settle();
    EXPECT_EQ(runtime.Snapshot().OutputProvenance, Fast::WindowExtentProvenance::Default);
    auto windowed = runtime.Snapshot();
    windowed.Window = WindowMode::Windowed;
    ASSERT_TRUE(runtime.Apply(windowed).Accepted());
    settle();

    // A size choice the user reverts restores the default with its size.
    auto bigger = runtime.Snapshot();
    bigger.Window = WindowMode::Borderless;
    bigger.OutputWidth = 1920U;
    bigger.OutputHeight = 1080U;
    ASSERT_TRUE(runtime.Apply(bigger).Accepted());
    ASSERT_TRUE(runtime.AcknowledgePresentationApplied(runtime.Snapshot()));
    ASSERT_EQ(runtime.PresentationStatus().Phase, PresentationTransactionPhase::AwaitingConfirmation);
    EXPECT_EQ(runtime.Snapshot().OutputProvenance, Fast::WindowExtentProvenance::User);
    ASSERT_TRUE(runtime.RollbackPresentation());
    EXPECT_EQ(runtime.Snapshot().OutputProvenance, Fast::WindowExtentProvenance::Default);
    EXPECT_EQ(runtime.Snapshot().OutputWidth, 1280U);
    ASSERT_TRUE(runtime.AcknowledgePresentationApplied(runtime.Snapshot()));
    EXPECT_EQ(runtime.PresentationStatus().Phase, PresentationTransactionPhase::Idle);
}

TEST(GraphicsSettingsRuntimeTest, SavesLoadsAndDeletesUserPresetsButNeverBuiltIns) {
    using namespace Fast::Extension;
    const auto persistence = Store();
    auto& runtime = Runtime();
    const auto output = runtime.Snapshot();

    auto custom = runtime.Snapshot();
    custom.Preset = GraphicsPreset::Custom;
    custom.FovMultiplier = 1.3F;
    custom.InternalResolutionScale = 1.5F;
    ASSERT_TRUE(runtime.Apply(custom).Accepted());
    EXPECT_TRUE(runtime.ActiveUserPreset().empty());

    // Built-in names are refused, whatever their case or padding.
    for (const char* reserved : {"Enhanced", " authentic ", "TOON", "custom"}) {
        const auto refused = runtime.SaveUserPreset(reserved);
        EXPECT_FALSE(refused.Succeeded) << reserved;
    }
    EXPECT_FALSE(runtime.SaveUserPreset("   ").Succeeded);
    EXPECT_TRUE(runtime.UserPresetNames().empty());

    const auto saved = runtime.SaveUserPreset("  Sera  ");
    ASSERT_TRUE(saved.Succeeded) << saved.Message;
    EXPECT_EQ(runtime.UserPresetNames(), std::vector<std::string>{"Sera"});
    EXPECT_EQ(runtime.ActiveUserPreset(), "Sera");
    const auto& stored = persistence->Root["GraphicsPresets"];
    ASSERT_EQ(stored["Presets"].size(), 1U);
    EXPECT_EQ(stored["Presets"][0]["Name"], "Sera");
    EXPECT_EQ(stored["Presets"][0]["Graphics"]["Preset"], "Custom");
    // Host-owned state is not part of a preset document.
    EXPECT_FALSE(stored["Presets"][0]["Graphics"].contains("Output"));
    EXPECT_FALSE(stored["Presets"][0]["Graphics"]["Grass"].contains("Sources"));

    // Any edit leaves the preset; loading it restores the preset part and
    // keeps the current output.
    auto edited = runtime.Snapshot();
    edited.FovMultiplier = 1.0F;
    edited.InternalResolutionScale = 1.0F;
    edited.OutputWidth = output.OutputWidth;
    ASSERT_TRUE(runtime.Apply(edited).Accepted());
    EXPECT_TRUE(runtime.ActiveUserPreset().empty());
    const auto loaded = runtime.LoadUserPreset("sera");
    ASSERT_TRUE(loaded.Succeeded) << loaded.Message;
    EXPECT_FLOAT_EQ(runtime.Snapshot().FovMultiplier, 1.3F);
    EXPECT_FLOAT_EQ(runtime.Snapshot().InternalResolutionScale, 1.5F);
    EXPECT_EQ(runtime.Snapshot().Preset, GraphicsPreset::Custom);
    EXPECT_EQ(runtime.Snapshot().OutputWidth, output.OutputWidth);
    EXPECT_EQ(runtime.ActiveUserPreset(), "Sera");

    // A failed write reports it and leaves the library as it was.
    persistence->FailPresets = true;
    EXPECT_FALSE(runtime.SaveUserPreset("Notte").Succeeded);
    EXPECT_FALSE(runtime.DeleteUserPreset("Sera").Succeeded);
    EXPECT_EQ(runtime.UserPresetNames(), std::vector<std::string>{"Sera"});
    persistence->FailPresets = false;

    EXPECT_FALSE(runtime.DeleteUserPreset("Enhanced").Succeeded);
    ASSERT_TRUE(runtime.DeleteUserPreset("SERA").Succeeded);
    EXPECT_TRUE(runtime.UserPresetNames().empty());
    EXPECT_TRUE(runtime.ActiveUserPreset().empty());
    EXPECT_TRUE(persistence->Root["GraphicsPresets"]["Presets"].empty());
    EXPECT_FALSE(runtime.LoadUserPreset("Sera").Succeeded);
}

TEST(GraphicsPresetLibraryTest, RoundTripsAndKeepsNewerPresets) {
    using namespace Fast::Extension;
    GraphicsPresetLibrary library;
    auto settings = GraphicsSettingsService::Preset(GraphicsPreset::Enhanced);
    settings.OutputWidth = 1234U;
    library.Save("Uno", settings);
    auto second = settings;
    second.FovMultiplier = 1.2F;
    library.Save("Due", second);
    library.Save("uno", settings);  // replaces, keeps order
    ASSERT_EQ(library.Presets().size(), 2U);
    EXPECT_EQ(library.Presets()[0].Name, "uno");
    // The host-owned state is not part of a preset.
    EXPECT_EQ(library.Presets()[0].Settings.OutputWidth, GraphicsSettings{}.OutputWidth);
    EXPECT_EQ(library.Presets()[0].Settings.Preset, GraphicsPreset::Custom);

    auto document = library.Serialize();
    auto newer = SerializeGraphicsSettings(settings);
    newer["SchemaVersion"] = kGraphicsSettingsSchemaVersion + 1U;
    document["Presets"].push_back({{"Name", "Futuro"}, {"Graphics", newer}});
    document["Presets"].push_back({{"Name", "Enhanced"}, {"Graphics", newer}});
    document["Presets"].push_back({{"Name", "UNO"}, {"Graphics", newer}});
    std::vector<std::string> issues;
    const auto loaded = GraphicsPresetLibrary::Deserialize(document, &issues);
    ASSERT_EQ(loaded.Presets().size(), 2U);
    EXPECT_EQ(loaded.Presets()[1].Name, "Due");
    EXPECT_EQ(SerializeGraphicsSettings(loaded.Presets()[0].Settings),
              SerializeGraphicsSettings(library.Presets()[0].Settings));
    std::string reported;
    for (const auto& issue : issues) reported += issue + "\n";
    EXPECT_EQ(issues.size(), 3U) << reported;
    // The newer preset is not offered but written back unchanged.
    const auto written = loaded.Serialize();
    ASSERT_EQ(written["Presets"].size(), 3U);
    EXPECT_EQ(written["Presets"][2]["Name"], "Futuro");
    EXPECT_EQ(written["Presets"][2]["Graphics"], newer);
    EXPECT_TRUE(loaded.ReadOnlyReason().empty());

    // The config store keeps blocks flattened: the library survives it, and
    // so does an empty one (whose list becomes null).
    std::vector<std::string> flattenedIssues;
    const auto flattened = GraphicsPresetLibrary::Deserialize(
        nlohmann::json{{"GraphicsPresets", written}}.flatten().unflatten()["GraphicsPresets"], &flattenedIssues);
    const auto reloaded = flattened.Serialize();
    ASSERT_EQ(reloaded["Presets"].size(), 3U);
    EXPECT_EQ(reloaded["Presets"][0], written["Presets"][0]);
    EXPECT_EQ(reloaded["Presets"][1], written["Presets"][1]);
    EXPECT_EQ(reloaded["Presets"][2]["Name"], "Futuro");
    // Only the newer preset is noted: no correction warnings at each start.
    EXPECT_EQ(flattenedIssues.size(), 1U) << flattenedIssues.front();
    const auto empty = GraphicsPresetLibrary{}.Serialize();
    EXPECT_EQ(empty["Presets"].size(), 0U);
    flattenedIssues.clear();
    EXPECT_TRUE(GraphicsPresetLibrary::Deserialize(
                    nlohmann::json{{"GraphicsPresets", empty}}.flatten().unflatten()["GraphicsPresets"],
                    &flattenedIssues)
                    .Presets()
                    .empty());
    EXPECT_TRUE(flattenedIssues.empty());

    auto future = document;
    future["SchemaVersion"] = kGraphicsPresetLibrarySchemaVersion + 1U;
    const auto readOnly = GraphicsPresetLibrary::Deserialize(future);
    EXPECT_TRUE(readOnly.Presets().empty());
    EXPECT_FALSE(readOnly.ReadOnlyReason().empty());
}

} // namespace
