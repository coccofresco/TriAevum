// The managed data tree's runtime side (UX plan WP-05C): the routing options'
// parser, the save-state move with its migration record, and the early log
// spool. No window, no game.

#include "oot3d_savestate_library.h"
#include "oot3d_savestate_migration.h"
#include "triaevum_data_routing.h"
#include "triaevum_early_log.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

namespace fs = std::filesystem;
using Oot3dNativeGame::SavestateMigrationStatus;

int gChecks = 0;

void Expect(bool condition, const std::string &message) {
    ++gChecks;
    if (!condition)
        throw std::runtime_error(message);
}

std::string Read(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void Write(const fs::path &path, const std::string &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
}

nlohmann::json ReadJson(const fs::path &path) {
    return nlohmann::json::parse(Read(path));
}

std::string Utf8(const fs::path &path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

bool Throws(const std::function<void()> &call) {
    try {
        call();
    } catch (const std::runtime_error &) {
        return true;
    }
    return false;
}

// Parses one argument vector the way the bootstrap does.
Oot3dNativeGame::DataRouting Parse(std::vector<std::string> arguments, int *unparsed = nullptr) {
    std::vector<char *> argv;
    for (auto &argument : arguments)
        argv.push_back(argument.data());
    Oot3dNativeGame::DataRouting routing;
    int left = 0;
    for (int index = 0; index < static_cast<int>(argv.size()); ++index) {
        if (!Oot3dNativeGame::ParseDataRoutingOption(static_cast<int>(argv.size()), argv.data(), index, routing))
            ++left;
    }
    if (unparsed != nullptr)
        *unparsed = left;
    return routing;
}

void TestParser(const fs::path &root) {
    using Oot3dNativeGame::FormatTitleId;
    using Oot3dNativeGame::ParseTitleId;
    Expect(ParseTitleId("0004000000033500") == 0x0004000000033500ULL, "a USA title id parses");
    Expect(ParseTitleId("0004000000033600") == 0x0004000000033600ULL, "a EUR title id parses");
    Expect(ParseTitleId("000400000003350a") == 0x000400000003350AULL, "lower case parses");
    Expect(!ParseTitleId("4000000033500").has_value(), "a short title id is refused");
    Expect(!ParseTitleId("0x04000000033500").has_value(), "a prefix is refused");
    Expect(!ParseTitleId("000400000003350G").has_value(), "a non-hex digit is refused");
    Expect(FormatTitleId(0x000400000003350AULL) == "000400000003350A", "title ids print upper case");

    const auto data = root / "data";
    int unparsed = 0;
    const auto routing = Parse({"--save-data", Utf8(data / "savedata"), "--log-directory", Utf8(data / "logs"),
                                "--crash-directory", Utf8(data / "logs" / "crashes"), "--texture-load-directory",
                                Utf8(data / "textures" / "load" / "0004000000033500"), "--texture-dump-directory",
                                Utf8(data / "textures" / "dump" / "0004000000033500"), "--savestate-directory",
                                Utf8(data / "x" / ".." / "savestates"), "--title-id", "0004000000033500"},
                               &unparsed);
    Expect(unparsed == 2, "options other than routing are left to the bootstrap");
    Expect(routing.LogDirectory == data / "logs", "--log-directory");
    Expect(routing.CrashDirectory == data / "logs" / "crashes", "--crash-directory");
    Expect(routing.TextureLoadDirectory == data / "textures" / "load" / "0004000000033500",
           "--texture-load-directory");
    Expect(routing.TextureDumpDirectory == data / "textures" / "dump" / "0004000000033500",
           "--texture-dump-directory");
    Expect(routing.SavestateDirectory == data / "savestates", "--savestate-directory is normalized");
    Expect(routing.TitleId == 0x0004000000033500ULL, "--title-id");

    // A relative folder is absolute after parsing; a later value wins.
    const auto relative = Parse({"--log-directory", "first", "--log-directory", "second"});
    Expect(relative.LogDirectory == fs::absolute("second").lexically_normal(), "the later value wins, absolute");
    // An old profile has none of them.
    Expect(Parse({"--save-data", "x", "--width", "1280"}) == Oot3dNativeGame::DataRouting{},
           "arguments without routing leave every location unset");

    Expect(Throws([] { Parse({"--log-directory"}); }), "a missing folder is refused");
    Expect(Throws([] { Parse({"--crash-directory", ""}); }), "an empty folder is refused");
    Expect(Throws([] { Parse({"--title-id", "33500"}); }), "an invalid title id is refused");
}

struct MigrationFixture {
    fs::path Root, Legacy, Managed, Config;

    explicit MigrationFixture(const fs::path &root)
        : Root(root), Legacy(root / "package" / "savestates"), Managed(root / "package" / "data" / "savestates"),
          Config(root / "package" / "data" / "config") {
        fs::create_directories(Config);
    }

    Oot3dNativeGame::SavestateMigrationRequest Request(bool fixedSlot = false) const {
        return {Config / "savestates.json", Config / "migrations.json", Legacy, Managed, fixedSlot};
    }

    void Preferences(const nlohmann::json &document) const {
        Write(Config / "savestates.json", document.dump(4) + "\n");
    }
};

void TestMigration(const fs::path &root) {
    using Oot3dNativeGame::MigrateLegacySavestates;
    {
        // The first launch with the managed default: states and anything
        // beside them are copied and verified, LastState follows, the record
        // and README are written, the originals stay.
        MigrationFixture fixture(root / "fresh");
        const std::string first(70000, 'a'), second = "state two", sidecar = "{\"note\":1}";
        Write(fixture.Legacy / "state-2026-10-01_10-00-00.oot3dsav", first);
        Write(fixture.Legacy / "state-2026-10-02_10-00-00+settings.oot3dsav", second);
        Write(fixture.Legacy / "thumbnails" / "state.json", sidecar);
        fixture.Preferences(
            {{"Directory", ""},
             {"LastState", Utf8(fixture.Legacy / "state-2026-10-02_10-00-00+settings.oot3dsav")}});
        Write(fixture.Config / "migrations.json",
              R"({"format":"triaevum_migrations_v1","migrations":[{"id":"other","status":"completed"}]})");

        const auto result = MigrateLegacySavestates(fixture.Request());
        Expect(result.Status == SavestateMigrationStatus::Completed, "the move completes: " + result.Message);
        Expect(result.CopiedFiles == 3 && result.PresentFiles == 0 && result.Conflicts.empty(), "three copies");
        Expect(result.CopiedBytes == first.size() + second.size() + sidecar.size(), "copied bytes");
        Expect(result.DefaultDirectory == fixture.Managed, "the managed folder is the default");
        Expect(Read(fixture.Managed / "state-2026-10-01_10-00-00.oot3dsav") == first, "a large state is copied");
        Expect(Read(fixture.Managed / "thumbnails" / "state.json") == sidecar, "files beside states are copied");
        Expect(Read(fixture.Legacy / "state-2026-10-01_10-00-00.oot3dsav") == first, "the original stays");
        Expect(fs::is_regular_file(fixture.Legacy / Oot3dNativeGame::kSavestateMovedReadme), "README-moved.txt");
        Expect(!fs::exists(fixture.Managed / "README-moved.txt"), "the README is not copied");
        const auto preferences = ReadJson(fixture.Config / "savestates.json");
        Expect(result.LastStateRewritten, "LastState is rewritten");
        Expect(preferences["LastState"] == "../savestates/state-2026-10-02_10-00-00+settings.oot3dsav",
               "LastState names the verified copy (F8 loads it), relative inside the installation");
        Expect(preferences["Directory"] == "", "the default Directory stays the default");
        const auto record = ReadJson(fixture.Config / "migrations.json");
        Expect(record["format"] == Oot3dNativeGame::kMigrationsFormat, "the record's format");
        Expect(record["migrations"].size() == 2 && record["migrations"][0]["id"] == "other",
               "other records are kept");
        const auto &moved = record["migrations"][1];
        Expect(moved["id"] == Oot3dNativeGame::kSavestateMigrationId && moved["status"] == "completed",
               "the completion record");
        Expect(moved["source"] == "../../savestates" && moved["destination"] == "../savestates",
               "the record names both folders, relative inside the installation");
        Expect(moved["copied_files"] == 3 && moved["files"].size() == 3, "the record lists the copies");
        Expect(moved["files"][0]["fnv1a64"].get<std::string>().size() == 16, "each copy's hash");

        // The second launch copies nothing and changes nothing.
        const auto recordBytes = Read(fixture.Config / "migrations.json");
        const auto preferenceBytes = Read(fixture.Config / "savestates.json");
        const auto again = MigrateLegacySavestates(fixture.Request());
        Expect(again.Status == SavestateMigrationStatus::NotNeeded && again.CopiedFiles == 0,
               "the second launch copies nothing: " + again.Message);
        Expect(again.DefaultDirectory == fixture.Managed, "and keeps the managed default");
        Expect(Read(fixture.Config / "migrations.json") == recordBytes, "the record is unchanged");
        Expect(Read(fixture.Config / "savestates.json") == preferenceBytes, "the preferences are unchanged");
    }
    {
        // No preferences file at all (never saved a state), or one without
        // Directory: the move still happens.
        MigrationFixture absent(root / "absent");
        Write(absent.Legacy / "a.oot3dsav", "a");
        Expect(MigrateLegacySavestates(absent.Request()).Status == SavestateMigrationStatus::Completed,
               "no preferences file");
        Expect(!fs::exists(absent.Config / "savestates.json"), "no preferences are created");
        MigrationFixture keyless(root / "keyless");
        Write(keyless.Legacy / "a.oot3dsav", "a");
        keyless.Preferences({{"LastState", ""}});
        Expect(MigrateLegacySavestates(keyless.Request()).Status == SavestateMigrationStatus::Completed,
               "preferences without Directory");
    }
    {
        // A chosen folder is the player's: never moved.
        MigrationFixture chosen(root / "chosen");
        Write(chosen.Legacy / "a.oot3dsav", "a");
        chosen.Preferences({{"Directory", Utf8(root / "elsewhere")}, {"LastState", ""}});
        const auto bytes = Read(chosen.Config / "savestates.json");
        const auto result = MigrateLegacySavestates(chosen.Request());
        Expect(result.Status == SavestateMigrationStatus::NotNeeded && !fs::exists(chosen.Managed),
               "a chosen Directory is preserved");
        Expect(Read(chosen.Config / "savestates.json") == bytes && !fs::exists(chosen.Config / "migrations.json"),
               "nothing is written for a chosen Directory");
        // --quick-state: nothing is moved.
        MigrationFixture fixed(root / "fixed");
        Write(fixed.Legacy / "a.oot3dsav", "a");
        Expect(MigrateLegacySavestates(fixed.Request(true)).Status == SavestateMigrationStatus::NotNeeded &&
                   !fs::exists(fixed.Managed),
               "a fixed slot moves nothing");
        // No earlier folder, an empty one, or the same folder.
        MigrationFixture none(root / "none");
        Expect(MigrateLegacySavestates(none.Request()).Status == SavestateMigrationStatus::NotNeeded,
               "no earlier folder");
        fs::create_directories(none.Legacy);
        Expect(MigrateLegacySavestates(none.Request()).Status == SavestateMigrationStatus::NotNeeded,
               "an empty earlier folder");
        auto same = none.Request();
        Write(none.Legacy / "a.oot3dsav", "a");
        same.ManagedDirectory = none.Legacy;
        Expect(MigrateLegacySavestates(same).Status == SavestateMigrationStatus::NotNeeded,
               "the same folder (Linux, Android)");
    }
    {
        // Existing files are never overwritten: identical ones count as
        // present, different ones stay behind as conflicts.
        MigrationFixture conflict(root / "conflict");
        Write(conflict.Legacy / "same.oot3dsav", "same");
        Write(conflict.Legacy / "different.oot3dsav", "legacy");
        Write(conflict.Managed / "same.oot3dsav", "same");
        Write(conflict.Managed / "different.oot3dsav", "managed");
        conflict.Preferences({{"Directory", ""}, {"LastState", Utf8(conflict.Legacy / "different.oot3dsav")}});
        const auto result = MigrateLegacySavestates(conflict.Request());
        Expect(result.Status == SavestateMigrationStatus::Completed && result.PresentFiles == 1 &&
                   result.CopiedFiles == 0 && result.Conflicts.size() == 1,
               "conflicts are not overwritten");
        Expect(Read(conflict.Managed / "different.oot3dsav") == "managed", "the managed file keeps its bytes");
        Expect(ReadJson(conflict.Config / "savestates.json")["LastState"] ==
                   Utf8(conflict.Legacy / "different.oot3dsav"),
               "LastState keeps naming an uncopied original");
        Expect(Read(conflict.Legacy / Oot3dNativeGame::kSavestateMovedReadme).find("different.oot3dsav") !=
                   std::string::npos,
               "the README lists what stayed");
    }
    {
        // A failed preference write leaves the earlier folder authoritative
        // and writes no record; the next launch completes without copying
        // the verified files again.
        MigrationFixture failing(root / "failing");
        Write(failing.Legacy / "a.oot3dsav", "a");
        failing.Preferences({{"Directory", ""}, {"LastState", Utf8(failing.Legacy / "a.oot3dsav")}});
        const auto blocker = fs::path(failing.Config / "savestates.json").concat(".tmp");
        fs::create_directories(blocker);
        const auto failed = MigrateLegacySavestates(failing.Request());
        Expect(failed.Status == SavestateMigrationStatus::Failed, "the preference write fails");
        Expect(failed.DefaultDirectory == failing.Legacy, "the earlier folder stays the default");
        Expect(!fs::exists(failing.Config / "migrations.json"), "no completion record");
        Expect(ReadJson(failing.Config / "savestates.json")["LastState"] == Utf8(failing.Legacy / "a.oot3dsav"),
               "LastState is unchanged");
        fs::remove(blocker);
        const auto retried = MigrateLegacySavestates(failing.Request());
        Expect(retried.Status == SavestateMigrationStatus::Completed && retried.CopiedFiles == 0 &&
                   retried.PresentFiles == 1,
               "the retry completes without copying again");
        Expect(ReadJson(failing.Config / "savestates.json")["LastState"] == "../savestates/a.oot3dsav",
               "and rewrites LastState");
        // Unreadable preferences: nothing is moved, the earlier folder stays.
        MigrationFixture unreadable(root / "unreadable");
        Write(unreadable.Legacy / "a.oot3dsav", "a");
        Write(unreadable.Config / "savestates.json", "{not json");
        const auto refused = MigrateLegacySavestates(unreadable.Request());
        Expect(refused.Status == SavestateMigrationStatus::Failed && refused.DefaultDirectory == unreadable.Legacy &&
                   !fs::exists(unreadable.Managed),
               "unreadable preferences move nothing");
    }
    {
        // Issue #124: the whole installation moved after the save states did.
        // The record still matches (nothing is copied again, a state the
        // player deleted from the new folder does not come back) and the
        // last state follows the installation.
        MigrationFixture before(root / "relocate-before");
        Write(before.Legacy / "a.oot3dsav", "a");
        Write(before.Legacy / "b.oot3dsav", "b");
        before.Preferences({{"Directory", ""}, {"LastState", Utf8(before.Legacy / "b.oot3dsav")}});
        Expect(MigrateLegacySavestates(before.Request()).Status == SavestateMigrationStatus::Completed,
               "the move completes before the relocation");
        fs::remove(before.Managed / "a.oot3dsav");
        const auto moved = root / "relocate after" / "My Games";
        fs::create_directories(moved);
        fs::rename(before.Root / "package", moved / "package");
        MigrationFixture after(moved);
        const auto again = MigrateLegacySavestates(after.Request());
        Expect(again.Status == SavestateMigrationStatus::NotNeeded && again.CopiedFiles == 0,
               "a moved installation recognises its completed move: " + again.Message);
        Expect(!fs::exists(after.Managed / "a.oot3dsav"), "a deleted state does not come back");
        const Oot3dNativeGame::SavestateLibrary library(after.Config / "savestates.json", after.Managed);
        Expect(library.LastState() == (after.Managed / "b.oot3dsav").lexically_normal(),
               "the last state is the moved installation's copy");
    }
}

void TestEarlyLog(const fs::path &root) {
    const auto state = root / "state";
    fs::create_directories(state);
    Expect(TriAevum::EarlyLogSpoolPath(state) == state / TriAevum::kEarlyLogSpoolName,
           "the spool lives in a writable state folder");
    const auto temporary = TriAevum::EarlyLogSpoolPath({});
    Expect(temporary.filename() == TriAevum::kEarlyLogSpoolName &&
               temporary.parent_path() == TriAevum::TemporaryDiagnosticDirectory(),
           "without one, in the temporary folder");
    Write(root / "file", "x");
    Expect(!TriAevum::PrepareWritableDirectory(root / "file" / "logs", nullptr),
           "a folder that cannot be created is refused");

    // Bounded: at the limit the file moves to .1.
    const auto spool = state / TriAevum::kEarlyLogSpoolName;
    for (int index = 0; index < 20; ++index)
        Expect(TriAevum::AppendEarlyLog(spool, "message " + std::to_string(index), 200), "append");
    Expect(fs::file_size(spool) <= 200 && fs::exists(fs::path(spool).concat(".1")), "the spool stays bounded");
    Expect(Read(spool).find("message 19") != std::string::npos, "the newest message is kept");

    // The handoff appends the older part first, then removes the spool.
    const auto logs = root / "data" / "logs";
    fs::create_directories(logs);
    std::string error;
    const auto older = Read(fs::path(spool).concat(".1")), newer = Read(spool);
    Expect(TriAevum::HandOffEarlyLog(spool, logs, &error), "handoff: " + error);
    Expect(Read(logs / TriAevum::kEarlyLogName) == older + newer, "the early log keeps the order");
    Expect(!fs::exists(spool) && !fs::exists(fs::path(spool).concat(".1")), "the spool is removed");
    Expect(TriAevum::HandOffEarlyLog(spool, logs, &error), "no spool: nothing to hand off");
}

} // namespace

int main() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("triaevum_data_routing_" + std::to_string(nonce));
    int status = 0;
    try {
        TestParser(root / "parser");
        TestMigration(root / "migration");
        TestEarlyLog(root / "early");
        std::cout << "triaevum_data_routing_tests: " << gChecks << " checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << "triaevum_data_routing_tests: FAILED: " << error.what() << '\n';
        status = 1;
    }
    std::error_code ignored;
    fs::remove_all(root, ignored);
    return status;
}
