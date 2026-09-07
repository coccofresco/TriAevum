#include "fast/oot3d/graphics_settings_persistence.h"
#include "fast/oot3d/graphics_settings_runtime.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <memory>

namespace {

class MemoryGraphicsSettingsPersistence final
    : public Fast::Oot3d::GraphicsSettingsPersistencePort {
  public:
    bool LoadRoot(nlohmann::json& root) override {
        root = Root;
        ++LoadCount;
        return true;
    }

    bool StoreGraphics(const nlohmann::json& graphics) override {
        Root["Graphics"] = graphics;
        ++StoreCount;
        return true;
    }

    nlohmann::json Root;
    uint32_t LoadCount = 0;
    uint32_t StoreCount = 0;
};

TEST(GraphicsSettingsRuntimeTest, UsesInjectedPersistencePort) {
    auto persistence =
        std::make_shared<MemoryGraphicsSettingsPersistence>();
    auto initial = Fast::Oot3d::GraphicsSettingsService::Preset(
        Fast::Oot3d::GraphicsPreset::Custom);
    initial.FovMultiplier = 1.25F;
    persistence->Root["Graphics"] =
        Fast::Oot3d::SerializeGraphicsSettings(initial);

    Fast::Oot3d::InstallGraphicsSettingsPersistencePort(persistence);
    auto& runtime = Fast::Oot3d::GraphicsSettingsRuntime::Instance();

    EXPECT_EQ(persistence->LoadCount, 1U);
    EXPECT_FLOAT_EQ(runtime.Snapshot().FovMultiplier, 1.25F);
    const auto before = runtime.SnapshotWithRevision();
    EXPECT_FLOAT_EQ(before.Value.FovMultiplier, 1.25F);
    EXPECT_GT(before.Revision, 0U);

    auto changed = runtime.Snapshot();
    changed.FovMultiplier = 1.4F;
    const auto validation = runtime.Apply(changed);

    ASSERT_TRUE(validation.Accepted());
    const auto after = runtime.SnapshotWithRevision();
    EXPECT_GT(after.Revision, before.Revision);
    EXPECT_FLOAT_EQ(after.Value.FovMultiplier, 1.4F);
    EXPECT_EQ(persistence->StoreCount, 1U);
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
              Fast::Oot3d::PresentationTransactionPhase::ApplyRequested);
    EXPECT_EQ(persistence->StoreCount, 1U);

    ASSERT_TRUE(runtime.AcknowledgePresentationApplied(resized));
    EXPECT_EQ(runtime.PresentationStatus().Phase,
              Fast::Oot3d::PresentationTransactionPhase::Idle);
    EXPECT_EQ(persistence->StoreCount, 2U);
    EXPECT_EQ(persistence->Root["Graphics"]["Output"]["Width"]
                  .get<uint32_t>(),
              1920U);
    EXPECT_EQ(persistence->Root["Graphics"]["Output"]["Height"]
                  .get<uint32_t>(),
              1080U);
    EXPECT_FALSE(runtime.TickPresentation());
}

} // namespace
