#include "ship/config/ConfigPersistence.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

TEST(ConfigPersistence, AtomicallyReplacesCompleteJsonDocument) {
    namespace fs = std::filesystem;
    const auto suffix =
        std::chrono::steady_clock::now()
            .time_since_epoch().count();
    const fs::path path =
        fs::temp_directory_path() /
        ("lus-config-atomic-" + std::to_string(suffix) + ".json");
    nlohmann::json document;
    ASSERT_TRUE(Ship::SetNestedConfigBlock(
        document, "Graphics",
        nlohmann::json{{"SchemaVersion", 1U},
                       {"Preset", "Custom"}}));
    ASSERT_TRUE(Ship::SetNestedConfigBlock(
        document, "Graphics.Output",
        nlohmann::json{{"Width", 2560U}}));
    const auto write = Ship::WriteConfigFileAtomically(
        path, document.dump(4));
    ASSERT_TRUE(write.Success) << write.Error;

    std::ifstream stream(path, std::ios::binary);
    ASSERT_TRUE(stream.good());
    const auto parsed = nlohmann::json::parse(stream);
    EXPECT_EQ(parsed["Graphics"]["SchemaVersion"], 1U);
    EXPECT_EQ(parsed["Graphics"]["Preset"], "Custom");
    EXPECT_EQ(parsed["Graphics"]["Output"]["Width"], 2560U);
    stream.close();

    const std::string temporaryPrefix =
        path.filename().string() + ".tmp.";
    for (const auto& entry :
         fs::directory_iterator(path.parent_path())) {
        EXPECT_FALSE(entry.path().filename().string().starts_with(
            temporaryPrefix));
    }
    std::error_code cleanupError;
    fs::remove(path, cleanupError);
    EXPECT_FALSE(cleanupError);
}

TEST(ConfigPersistence, RejectsMalformedPathsWithoutMutatingDocument) {
    nlohmann::json document{
        {"Graphics", {{"Preset", "Custom"}}}};
    const auto original = document;
    EXPECT_FALSE(Ship::SetNestedConfigBlock(
        document, "Graphics..Output",
        nlohmann::json::object()));
    EXPECT_EQ(document, original);
}
