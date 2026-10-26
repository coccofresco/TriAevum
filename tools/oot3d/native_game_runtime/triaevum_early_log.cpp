#include "triaevum_early_log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>

namespace TriAevum {
namespace {

namespace fs = std::filesystem;

std::string Timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
    return text;
}

fs::path Rotated(const fs::path &file) {
    return fs::path(file).concat(".1");
}

bool AppendBytes(const fs::path &file, const std::string &bytes, std::uintmax_t limit) {
    std::error_code status;
    if (!file.parent_path().empty())
        fs::create_directories(file.parent_path(), status);
    const auto size = fs::file_size(file, status);
    if (!status && size + bytes.size() > limit) {
        fs::remove(Rotated(file), status);
        fs::rename(file, Rotated(file), status);
    }
    std::ofstream output(file, std::ios::binary | std::ios::app);
    output << bytes;
    output.flush();
    return static_cast<bool>(output);
}

std::mutex gMutex;
fs::path gStateDirectory;
fs::path gSpool;
bool gSpoolChosen = false;

} // namespace

bool PrepareWritableDirectory(const fs::path &directory, std::string *error) {
    std::error_code status;
    if (directory.empty()) {
        if (error != nullptr)
            *error = "no folder";
        return false;
    }
    fs::create_directories(directory, status);
    if (!fs::is_directory(directory, status)) {
        if (error != nullptr)
            *error = "cannot create " + directory.string() + (status ? ": " + status.message() : std::string{});
        return false;
    }
    const auto probe = directory / ".triaevum-write-probe";
    {
        std::ofstream output(probe, std::ios::binary | std::ios::trunc);
        output << "probe\n";
        output.flush();
        if (!output) {
            if (error != nullptr)
                *error = "cannot write in " + directory.string();
            fs::remove(probe, status);
            return false;
        }
    }
    fs::remove(probe, status);
    return true;
}

fs::path TemporaryDiagnosticDirectory() {
    std::error_code status;
    const auto temporary = fs::temp_directory_path(status);
    return status || temporary.empty() ? fs::path{} : temporary / "TriAevum";
}

fs::path EarlyLogSpoolPath(const fs::path &stateDirectory) {
    std::error_code status;
    if (!stateDirectory.empty() && fs::is_directory(stateDirectory, status) &&
        PrepareWritableDirectory(stateDirectory, nullptr))
        return stateDirectory / kEarlyLogSpoolName;
    const auto temporary = TemporaryDiagnosticDirectory();
    if (!temporary.empty() && PrepareWritableDirectory(temporary, nullptr))
        return temporary / kEarlyLogSpoolName;
    return {};
}

bool AppendEarlyLog(const fs::path &file, std::string_view message, std::uintmax_t limit) {
    std::string line = "[" + Timestamp() + "] ";
    line.append(message);
    if (line.empty() || line.back() != '\n')
        line.push_back('\n');
    return AppendBytes(file, line, limit);
}

bool HandOffEarlyLog(const fs::path &spool, const fs::path &logDirectory, std::string *error) {
    std::error_code status;
    if (spool.empty())
        return true;
    const auto destination = logDirectory / kEarlyLogName;
    // The rotated part is older: it goes first.
    for (const auto &part : {Rotated(spool), spool}) {
        if (!fs::is_regular_file(part, status))
            continue;
        std::string bytes;
        {
            // Closed before the removal (Windows keeps open files).
            std::ifstream input(part, std::ios::binary);
            if (!input) {
                if (error != nullptr)
                    *error = "cannot read " + part.string();
                return false;
            }
            bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
        if (!AppendBytes(destination, bytes, 4U * kEarlyLogLimit)) {
            if (error != nullptr)
                *error = "cannot write " + destination.string();
            return false;
        }
        if (!fs::remove(part, status) && error != nullptr)
            *error = "cannot remove " + part.string() + (status ? ": " + status.message() : std::string{});
    }
    return !fs::exists(spool, status) && !fs::exists(Rotated(spool), status);
}

void SetEarlyLogStateDirectory(const fs::path &stateDirectory) {
    std::scoped_lock lock(gMutex);
    gStateDirectory = stateDirectory;
    gSpoolChosen = false;
}

fs::path EarlyLogSpool() {
    std::scoped_lock lock(gMutex);
    if (!gSpoolChosen) {
        gSpool = EarlyLogSpoolPath(gStateDirectory);
        gSpoolChosen = true;
    }
    return gSpool;
}

void EarlyLog(std::string_view message) {
    std::cerr << "oot3d_native_game: " << message << std::endl;
    const auto spool = EarlyLogSpool();
    if (!spool.empty())
        AppendEarlyLog(spool, message);
}

} // namespace TriAevum
