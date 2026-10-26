#include "triaevum_console.h"
#include "triaevum_fatal_report.h"

#include "fast/extension/display_recovery.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool Wanted(std::vector<const char*> arguments, const char* noDialog = nullptr) {
    arguments.insert(arguments.begin(), "TriAevum");
    return TriAevum::FatalDialogWanted(static_cast<int>(arguments.size()), arguments.data(), noDialog);
}

void FatalDialogPolicy() {
    Expect(Wanted({}), "a player's launch shows the dialog");
    Expect(Wanted({"--launch-profile", "TriAevum.launch.json", "--output", "runtime-state.json"}),
           "the product's launch profile is interactive");
    for (const char* option : {"--input-timeline", "--frames", "--max-seconds", "--screenshot",
                               "--throughput-benchmark", "--product-info", "--game-language-info",
                               "--prebuild-interior-sun", "--fixed-delta-seconds"})
        Expect(!Wanted({"--config", "c.json", option, "1"}), "scripted runs and tool modes show no dialog");
    Expect(!Wanted({}, "1"), "TRIAEVUM_NO_FATAL_DIALOG=1 disables the dialog");
    Expect(Wanted({}, "0") && Wanted({}, ""), "an empty or 0 value keeps it");
}

void FatalTexts() {
    Expect(TriAevum::FatalLine("no device\r\nat startup") == "TRIAEVUM_FATAL no device  at startup",
           "the fatal line is one line");
    const auto text = TriAevum::FatalDialogText("no device", "C:/TriAevum/logs");
    Expect(text.rfind("no device\n\nDetails were written to the log folder:\nC:/TriAevum/logs\n\n", 0) == 0,
           "the dialog names the message and the log folder");
    Expect(text.find("Diagnostics") != std::string::npos, "the dialog says where to get a report");
    const auto early = TriAevum::FatalDialogText("no module", "");
    Expect(early.find("log folder") == std::string::npos && early.find("Diagnostics") != std::string::npos,
           "an error before the log names no log folder");
    const auto long_text = TriAevum::FatalDialogText(std::string(5000, 'x'), "logs");
    Expect(long_text.size() < 1800, "the message is bounded");
}

void ConsoleOption() {
    std::vector<std::string> storage{"TriAevum", "--console", "--launch-profile", "p.json", "--console"};
    std::vector<char*> argv;
    for (auto& argument : storage) argv.push_back(argument.data());
    argv.push_back(nullptr);
    int argc = static_cast<int>(storage.size());
    Expect(TriAevum::TakeConsoleOption(argc, argv.data()), "--console is found");
    Expect(argc == 3 && std::string(argv[1]) == "--launch-profile" && std::string(argv[2]) == "p.json" &&
               argv[3] == nullptr,
           "--console is removed and the arguments keep their order");
    Expect(!TriAevum::TakeConsoleOption(argc, argv.data()) && argc == 3, "without it nothing changes");
}

void DisplayRecoveryRecord() {
    const nlohmann::json before = {
        {"Window", {{"Mode", "Fullscreen"}, {"Display", 1}}},
        {"Output", {{"Width", 3840}, {"Height", 2160}, {"Provenance", "user"}}},
        {"RenderScale", 2.0},
        {"AA", {{"Mode", "Upscaler"}, {"MsaaSamples", 1}, {"Upscaler", {{"Provider", "DLSS"}, {"Quality", "Quality"}}}}},
    };
    nlohmann::json after = before;
    after["Window"]["Mode"] = "Windowed";
    after["Output"]["Width"] = 1280;
    after["Output"]["Height"] = 720;
    after["RenderScale"] = 1.0;
    after["AA"]["Mode"] = "Off";
    after["AA"]["Upscaler"]["Provider"] = "None";
    const auto fields = Fast::Extension::ChangedDisplayFields(before, after);
    const std::vector<std::string> expected{"Window mode", "Window width", "Window height",
                                            "Internal resolution", "Anti-aliasing", "Upscaler"};
    Expect(fields == expected, "the changed display fields are named in order");
    Expect(Fast::Extension::ChangedDisplayFields(before, before).empty(), "unchanged settings name nothing");

    const Fast::Extension::DisplayRecovery recovery{"the previous launch ended within its first minute", fields};
    const auto when = std::chrono::system_clock::time_point(std::chrono::seconds(1793016245));
    const auto record = Fast::Extension::DisplayRecoveryRecord(recovery, when);
    Expect(record["format"] == "triaevum_display_recovery_v1", "record format");
    Expect(record["time"] == "2026-10-26T12:04:05Z", "record time is UTC");
    Expect(record["fields"].size() == 6U && record["reason"] == recovery.Reason, "record fields and reason");

    const auto directory = std::filesystem::temp_directory_path() /
                           ("triaevum-recovery-" + std::to_string(when.time_since_epoch().count() % 100000) + "-" +
                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path = directory / "logs" / "last-recovery.json";
    std::string error;
    Expect(Fast::Extension::WriteDisplayRecoveryRecord(path, record, &error), error.c_str());
    std::ifstream stream(path);
    const auto read = nlohmann::json::parse(stream);
    stream.close();
    Expect(read == record, "the written record reads back");
    Expect(!std::filesystem::exists(path.string() + ".tmp"), "no temporary file stays");
    std::filesystem::remove_all(directory);
    const auto notice = Fast::Extension::DisplayRecoveryNotice(recovery);
    Expect(notice.find("(Window mode, Window width") != std::string::npos && notice.find("F1") != std::string::npos,
           "the notice lists the fields and where to change them");
}

} // namespace

int main() {
    try {
        FatalDialogPolicy();
        FatalTexts();
        ConsoleOption();
        DisplayRecoveryRecord();
    } catch (const std::exception& error) {
        std::cerr << "triaevum_fatal_report_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "triaevum_fatal_report_tests: ok\n";
    return 0;
}
