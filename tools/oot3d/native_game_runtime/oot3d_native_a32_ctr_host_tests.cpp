#include "oot3d_native_a32_ctr_host.h"
#include "oot3d_native_a32_dsp_hle.h"
#include "oot3d_native_pica_frontend.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace {

using namespace Oot3dNativeGame;
using oot3d::recomp::a32::GuestState;

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_native_a32_ctr_host_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

NativeA32CtrHostConfig MakeConfig() {
    NativeA32CtrHostConfig config;
    config.ResourceLimitValues = {
        0x18, 0x04000000, 0x20, 0x20, 0x20,
        0x08, 0x08,       0x10, 0x02, 0,
    };
    config.ResourceCurrentValues[1] = 0x1000;
    config.ResourceCurrentValues[2] = 1;
    config.LinearHeapBaseAddress = 0x14000000;
    config.LinearHeapSize = 0x04000000;
    config.HeapBaseAddress = 0x08000000;
    config.HeapSize = 0x08000000;
    config.HeadphonesConnected = true;
    config.SoundOutputMode = 2;
    config.SystemLanguage = 4;
    config.SystemRegion = 2;
    return config;
}

void PushSvcEvent(detail::NativeA32CtrSvcEventHistory& history,
                  uint32_t immediate, uint32_t threadId, std::string name,
                  uint32_t eventDetail) {
    NativeA32CtrSvcEvent event;
    event.Immediate = immediate;
    event.Pc = eventDetail * 4U;
    event.ThreadId = threadId;
    event.Handled = true;
    event.Name = std::move(name);
    event.Detail = eventDetail;
    history.push_back(std::move(event));
}

void TestBoundedSvcEventHistory() {
    detail::NativeA32CtrSvcEventHistory history(false, true, 3U);
    constexpr std::string_view filesystemName =
        "SendSyncRequest:fs:USER:OpenFile";
    constexpr std::string_view ipcName = "SendSyncRequest:hid:USER";
    constexpr std::string_view romFsReadName =
        "SendSyncRequest:file:romfs:Read";
    constexpr std::string_view flushName =
        "SendSyncRequest:gsp::Gpu:FlushDataCache";

    for (uint32_t index = 0U; index < 80U; ++index) {
        PushSvcEvent(history, 0x32U, 0U, std::string(filesystemName), index);
    }
    for (uint32_t index = 0U; index < 150U; ++index) {
        PushSvcEvent(history, 0x32U, 0U, std::string(ipcName), 1000U + index);
    }
    for (uint32_t index = 0U; index < 5U; ++index) {
        PushSvcEvent(history, 0x32U, 0U, std::string(romFsReadName),
                     4000U + index);
        PushSvcEvent(history, 0x32U, 0U, std::string(flushName),
                     5000U + index);
    }
    PushSvcEvent(history, 0x28U, 1U, "thread-one-last", 3001U);
    PushSvcEvent(history, 0x28U, 2U, "thread-two-last", 3002U);
    for (uint32_t index = 0U; index < 40U; ++index) {
        PushSvcEvent(history, 0x28U, 0U, "GetSystemTick", 2000U + index);
    }

    Require(history.TotalEventCount() == 282U,
            "bounded SVC history lost the exact total event count");
    Require(!history.RetainsFullHistory(),
            "bounded SVC history unexpectedly retained the full trace");
    const auto& events = history.Events();
    constexpr size_t maximumBoundedEvents =
        detail::NativeA32CtrSvcEventHistory::RecentEventCapacity +
        detail::NativeA32CtrSvcEventHistory::FilesystemEventCapacity +
        detail::NativeA32CtrSvcEventHistory::IpcEventCapacity + 3U;
    Require(events.size() <= maximumBoundedEvents,
            "bounded SVC history exceeded its diagnostic capacity");
    Require(events.size() >=
                detail::NativeA32CtrSvcEventHistory::RecentEventCapacity,
            "bounded SVC history omitted recent events");

    const size_t recentStart =
        events.size() -
        detail::NativeA32CtrSvcEventHistory::RecentEventCapacity;
    for (size_t index = 0U;
         index < detail::NativeA32CtrSvcEventHistory::RecentEventCapacity;
         ++index) {
        Require(events[recentStart + index].Detail == 2008U + index,
                "bounded SVC history changed the recent-32 summary");
    }

    std::vector<uint32_t> filesystemDetails;
    std::vector<uint32_t> ipcDetails;
    std::map<uint32_t, uint32_t> lastDetailByThread;
    const auto isFilesystemEvent = [](const NativeA32CtrSvcEvent& event) {
        return event.Name.find("fs:USER") != std::string::npos ||
               event.Name.find("file:romfs") != std::string::npos ||
               event.Name.find("file:savedata") != std::string::npos;
    };
    const auto isIpcSummaryEvent = [](const NativeA32CtrSvcEvent& event) {
        return event.Immediate == 0x32U &&
               event.Name.find("file:romfs:Read") == std::string::npos &&
               event.Name.find("gsp::Gpu:FlushDataCache") ==
                   std::string::npos;
    };
    size_t firstFilesystemEvent = 0U;
    size_t filesystemEventCount = 0U;
    size_t firstIpcEvent = 0U;
    size_t ipcEventCount = 0U;
    for (size_t index = events.size(); index > 0U; --index) {
        const auto& event = events[index - 1U];
        if (filesystemEventCount < 64U && isFilesystemEvent(event)) {
            firstFilesystemEvent = index - 1U;
            ++filesystemEventCount;
        }
        if (ipcEventCount < 128U && isIpcSummaryEvent(event)) {
            firstIpcEvent = index - 1U;
            ++ipcEventCount;
        }
        if (filesystemEventCount == 64U && ipcEventCount == 128U) {
            break;
        }
    }
    for (size_t index = firstFilesystemEvent; index < events.size(); ++index) {
        if (isFilesystemEvent(events[index])) {
            filesystemDetails.push_back(events[index].Detail);
        }
    }
    for (size_t index = firstIpcEvent; index < events.size(); ++index) {
        if (isIpcSummaryEvent(events[index])) {
            ipcDetails.push_back(events[index].Detail);
        }
    }
    for (const auto& event : events) {
        lastDetailByThread[event.ThreadId] = event.Detail;
    }
    Require(filesystemDetails.size() == 64U,
            "bounded SVC history changed the filesystem-64 count");
    for (size_t index = 0U; index < 59U; ++index) {
        Require(filesystemDetails[index] == 21U + index,
                "bounded SVC history changed filesystem event ordering");
    }
    for (size_t index = 0U; index < 5U; ++index) {
        Require(filesystemDetails[59U + index] == 4000U + index,
                "bounded SVC history omitted recent RomFS reads");
    }
    Require(ipcDetails.size() == 128U,
            "bounded SVC history changed the filtered IPC-128 count");
    for (size_t index = 0U; index < ipcDetails.size(); ++index) {
        Require(ipcDetails[index] == 1022U + index,
                "bounded SVC history changed filtered IPC ordering");
    }
    Require(lastDetailByThread.size() == 3U &&
                lastDetailByThread[0U] == 2039U &&
                lastDetailByThread[1U] == 3001U &&
                lastDetailByThread[2U] == 3002U,
            "bounded SVC history changed the last event per thread");

    const auto& ipcCounts = history.IpcNameCounts();
    Require(ipcCounts.size() == 4U &&
                ipcCounts.at(std::string(filesystemName)) == 80U &&
                ipcCounts.at(std::string(ipcName)) == 150U &&
                ipcCounts.at(std::string(romFsReadName)) == 5U &&
                ipcCounts.at(std::string(flushName)) == 5U,
            "bounded SVC history changed exact profiling counters");

    detail::NativeA32CtrSvcEventHistory fullHistory(true, false, 1U);
    for (uint32_t index = 0U; index < 300U; ++index) {
        PushSvcEvent(fullHistory, 0x28U, 0U, "GetSystemTick", index);
    }
    const auto& fullEvents = fullHistory.Events();
    Require(fullHistory.RetainsFullHistory() &&
                fullHistory.TotalEventCount() == 300U &&
                fullEvents.size() == 300U && fullEvents.front().Detail == 0U &&
                fullEvents.back().Detail == 299U,
            "full SVC history opt-in did not retain the complete trace");
}

} // namespace

int main() {
    TestBoundedSvcEventHistory();
    const std::filesystem::path romFsPath =
        std::filesystem::temp_directory_path() /
        "oot3d_native_a32_ctr_host_test.romfs";
    const std::filesystem::path saveDataPath =
        std::filesystem::temp_directory_path() /
        "oot3d_native_a32_ctr_host_test_savedata";
    std::error_code cleanupError;
    std::filesystem::remove_all(saveDataPath, cleanupError);
    constexpr std::array<uint8_t, 16> romFsBytes{
        0x49, 0x56, 0x46, 0x43, 0x00, 0x01, 0x02, 0x03,
        0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23,
    };
    {
        std::ofstream output(romFsPath, std::ios::binary | std::ios::trunc);
        Require(output.good(), "cannot create temporary RomFS image");
        output.write(reinterpret_cast<const char*>(romFsBytes.data()),
                     static_cast<std::streamsize>(romFsBytes.size()));
        Require(output.good(), "cannot write temporary RomFS image");
    }
    NativeA32Memory memory;
    std::string error;
    Require(memory.MapRegion(
                {"tls", 0x1FF82000, 0x1000, true, false, {}}, &error),
            error);
    Oot3dNativePicaFrontend picaFrontend;
    NativeA32CtrHostConfig config = MakeConfig();
    config.RomFsImagePath = romFsPath;
    config.RomFsImageOffset = 4;
    config.RomFsImageSize = romFsBytes.size() - config.RomFsImageOffset;
    config.PicaFrontend = &picaFrontend;
    config.SaveDataDirectory = saveDataPath;
    config.ProfileRuntime = true;
    NativeA32CtrHostServices host(std::move(config));
    const oot3d::recomp::a32::Registry registry{};
    NativeA32Process process(registry, host);
    NativeA32HostContext context{process, 0};
    GuestState state{};
    state.thread_pointer = 0x1FF82000;

    Require(host.HandleSvc(0x21, state, memory, context).Action ==
                NativeA32HostAction::Resume &&
                state.r[0] == 0 && state.r[1] == 1,
            "CreateAddressArbiter did not return the first typed handle");

    state.r[1] = 0xFFFF8001;
    Require(host.HandleSvc(0x38, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "GetResourceLimit rejected the current process");
    const uint32_t resourceLimit = state.r[1];
    Require(memory.Write32(0x1FF82000, 1), "cannot write resource name");
    state.r[0] = 0x1FF82008;
    state.r[1] = resourceLimit;
    state.r[2] = 0x1FF82000;
    state.r[3] = 1;
    Require(host.HandleSvc(0x3A, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "GetResourceLimitCurrentValues failed");
    uint64_t currentCommit = 0;
    uint32_t faultAddress = 0;
    Require(memory.Read64(0x1FF82008, &currentCommit, &faultAddress) &&
                currentCommit == 0x1000,
            "resource current value was not written as a 64-bit value");

    state.r[0] = 0x10003;
    state.r[1] = 0;
    state.r[2] = 0;
    state.r[3] = 0x2000;
    state.r[4] = 3;
    Require(host.HandleSvc(0x01, state, memory, context).Action ==
                NativeA32HostAction::Resume &&
                state.r[1] == 0x14000000 &&
                memory.IsMapped(0x14000000, 0x2000),
            "linear ControlMemory commit was not mapped");

    constexpr std::array<uint8_t, 5> portName{'s', 'r', 'v', ':', 0};
    Require(memory.WriteBytes(0x1FF82100, portName),
            "cannot write named port");
    state.r[1] = 0x1FF82100;
    Require(host.HandleSvc(0x2D, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "ConnectToPort did not open srv:");
    const uint32_t srvSession = state.r[1];
    Require(memory.Write32(0x1FF82080, 0x00010002),
            "cannot write srv request");
    state.r[0] = srvSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "srv:RegisterClient did not resume");
    uint32_t response = 0;
    Require(memory.Read32(0x1FF82080, &response) && response == 0x00010040,
            "srv:RegisterClient response header is wrong");

    Require(memory.Write32(0x1FF82080, 0x00050100) &&
                memory.Write32(0x1FF82084, 0x3A545041) &&
                memory.Write32(0x1FF82088, 0x00000055) &&
                memory.Write32(0x1FF8208C, 5),
            "cannot write srv:GetServiceHandle request");
    state.r[0] = srvSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "srv:GetServiceHandle did not open APT:U");
    uint32_t aptSession = 0;
    Require(memory.Read32(0x1FF82080, &response) &&
                response == 0x00050042 &&
                memory.Read32(0x1FF8208C, &aptSession),
            "srv:GetServiceHandle APT:U response is wrong");

    Require(memory.Write32(0x1FF82080, 0x00050100) &&
                memory.Write32(0x1FF82084, 0x553A7366) &&
                memory.Write32(0x1FF82088, 0x00524553) &&
                memory.Write32(0x1FF8208C, 7),
            "cannot write srv:GetServiceHandle fs:USER request");
    state.r[0] = srvSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "srv:GetServiceHandle did not open fs:USER");
    uint32_t fsSession = 0;
    Require(memory.Read32(0x1FF8208C, &fsSession) && fsSession != 0,
            "srv:GetServiceHandle fs:USER response has no session");
    Require(memory.Write32(0x1FF82080, 0x08010002) &&
                memory.Write32(0x1FF82084, 0x20) &&
                memory.Write32(0x1FF82088, 0),
            "cannot write FS:Initialize request");
    state.r[0] = fsSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x08010040,
            "FS:Initialize response is wrong");

    Require(memory.Fill(0x1FF82120, 1, 0) &&
                memory.Write32(0x1FF82080, 0x080C00C2) &&
                memory.Write32(0x1FF82084, 4) &&
                memory.Write32(0x1FF82088, 1) &&
                memory.Write32(0x1FF8208C, 1) &&
                memory.Write32(0x1FF82090, 0x4002) &&
                memory.Write32(0x1FF82094, 0x1FF82120),
            "cannot write FS:OpenArchive SaveData request");
    state.r[0] = fsSession;
    uint64_t saveArchive = 0;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x080C00C0 &&
                memory.Read64(0x1FF82088, &saveArchive, &faultAddress) &&
                saveArchive != 0 &&
                std::filesystem::is_directory(saveDataPath),
            "FS:OpenArchive did not mount persistent OOT3D SaveData");

    constexpr std::array<uint8_t, 20> saveFilePath{
        '/', 0, 't', 0, 'e', 0, 's', 0, 't', 0,
        '.', 0, 'd', 0, 'a', 0, 't', 0, 0, 0,
    };
    constexpr std::array<uint8_t, 4> saveFileBytes{1, 2, 3, 4};
    {
        std::ofstream saveFile(saveDataPath / "test.dat",
                               std::ios::binary | std::ios::trunc);
        saveFile.write(
            reinterpret_cast<const char*>(saveFileBytes.data()),
            static_cast<std::streamsize>(saveFileBytes.size()));
        Require(saveFile.good(), "cannot create SaveData file fixture");
    }
    Require(memory.WriteBytes(0x1FF82140, saveFilePath) &&
                memory.Write32(0x1FF82080, 0x080201C2) &&
                memory.Write32(0x1FF82084, 0) &&
                memory.Write64(0x1FF82088, saveArchive, &faultAddress) &&
                memory.Write32(0x1FF82090, 4) &&
                memory.Write32(0x1FF82094, saveFilePath.size()) &&
                memory.Write32(0x1FF82098, 3) &&
                memory.Write32(0x1FF8209C, 0) &&
                memory.Write32(0x1FF820A0,
                               (saveFilePath.size() << 14U) | 2U) &&
                memory.Write32(0x1FF820A4, 0x1FF82140),
            "cannot write FS:OpenFile SaveData request");
    state.r[0] = fsSession;
    uint32_t saveFileSession = 0;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x08020042 &&
                memory.Read32(0x1FF82084, &response) && response == 0 &&
                memory.Read32(0x1FF8208C, &saveFileSession) &&
                saveFileSession != 0,
            "FS:OpenFile did not open a confined SaveData path");
    Require(memory.Write32(0x1FF82080, 0x08040000),
            "cannot write SaveData File:GetSize request");
    state.r[0] = saveFileSession;
    uint64_t saveFileSize = 0;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read64(0x1FF82088, &saveFileSize, &faultAddress) &&
                saveFileSize == saveFileBytes.size(),
            "SaveData File:GetSize did not return the host file size");
    constexpr uint32_t writeAddress = 0x1FF82240;
    constexpr std::array<uint8_t, 3> writeBytes{9, 8, 7};
    Require(memory.WriteBytes(writeAddress, writeBytes) &&
                memory.Write32(0x1FF82080, 0x08030102) &&
                memory.Write32(0x1FF82084, 1) &&
                memory.Write32(0x1FF82088, 0) &&
                memory.Write32(0x1FF8208C, writeBytes.size()) &&
                memory.Write32(0x1FF82090, 1) &&
                memory.Write32(0x1FF82094,
                               (writeBytes.size() << 4U) | 0xAU) &&
                memory.Write32(0x1FF82098, writeAddress),
            "cannot write SaveData File:Write request");
    state.r[0] = saveFileSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x08030082 &&
                memory.Read32(0x1FF82088, &response) &&
                response == writeBytes.size(),
            "SaveData File:Write did not persist the mapped payload");
    std::array<uint8_t, 4> persistedBytes{};
    {
        std::ifstream persisted(saveDataPath / "test.dat",
                                std::ios::binary);
        persisted.read(reinterpret_cast<char*>(persistedBytes.data()),
                       static_cast<std::streamsize>(persistedBytes.size()));
    }
    Require(persistedBytes == std::array<uint8_t, 4>{1, 9, 8, 7},
            "SaveData File:Write changed the wrong host byte range");
    Require(memory.Write32(0x1FF82080, 0x08050080) &&
                memory.Write64(0x1FF82084, 6, &faultAddress),
            "cannot write SaveData File:SetSize request");
    state.r[0] = saveFileSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                std::filesystem::file_size(saveDataPath / "test.dat") == 6,
            "SaveData File:SetSize did not resize the host file");
    Require(memory.Write32(0x1FF82080, 0x08090000),
            "cannot write SaveData File:Flush request");
    state.r[0] = saveFileSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x08090040,
            "SaveData File:Flush did not resume");
    Require(memory.Write32(0x1FF82080, 0x08080000),
            "cannot write SaveData File:Close request");
    state.r[0] = saveFileSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "SaveData File:Close did not resume");

    constexpr std::array<uint8_t, 18> newSaveFilePath{
        '/', 0, 'n', 0, 'e', 0, 'w', 0, '.', 0,
        'd', 0, 'a', 0, 't', 0, 0, 0,
    };
    Require(memory.WriteBytes(0x1FF82170, newSaveFilePath) &&
                memory.Write32(0x1FF82080, 0x080201C2) &&
                memory.Write32(0x1FF82084, 0) &&
                memory.Write64(0x1FF82088, saveArchive, &faultAddress) &&
                memory.Write32(0x1FF82090, 4) &&
                memory.Write32(0x1FF82094, newSaveFilePath.size()) &&
                memory.Write32(0x1FF82098, 2) &&
                memory.Write32(0x1FF8209C, 0) &&
                memory.Write32(0x1FF820A0,
                               (newSaveFilePath.size() << 14U) | 2U) &&
                memory.Write32(0x1FF820A4, 0x1FF82170),
            "cannot write writable missing SaveData OpenFile request");
    state.r[0] = fsSession;
    uint32_t newSaveFileSession = 0;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82084, &response) && response == 0 &&
                memory.Read32(0x1FF8208C, &newSaveFileSession) &&
                newSaveFileSession != 0 &&
                std::filesystem::is_regular_file(saveDataPath / "new.dat"),
            "writable SaveData open did not establish its persistent file");
    Require(memory.Write32(0x1FF82080, 0x08080000),
            "cannot close newly established SaveData file");
    state.r[0] = newSaveFileSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "newly established SaveData file did not close");

    const auto deleteSave = [&](uint64_t archive, uint32_t descriptor) {
        Require(memory.WriteBytes(0x1FF82170, newSaveFilePath) &&
                    memory.Write32(0x1FF82080, 0x08040142) &&
                    memory.Write32(0x1FF82084, 0) &&
                    memory.Write64(0x1FF82088, archive, &faultAddress) &&
                    memory.Write32(0x1FF82090, 4) &&
                    memory.Write32(0x1FF82094, newSaveFilePath.size()) &&
                    memory.Write32(0x1FF82098, descriptor) &&
                    memory.Write32(0x1FF8209C, 0x1FF82170),
                "cannot write DeleteFile request");
        state.r[0] = fsSession;
        Require(host.HandleSvc(0x32, state, memory, context).Action == NativeA32HostAction::Resume &&
                    memory.Read32(0x1FF82080, &response) && response == 0x08040040 &&
                    memory.Read32(0x1FF82084, &response),
                "DeleteFile left its client waiting instead of replying");
        return response;
    };
    const uint32_t deleteDescriptor = (newSaveFilePath.size() << 14U) | 2U;
    Require(deleteSave(saveArchive + 1U, deleteDescriptor) == 0xC8804465U &&
                std::filesystem::exists(saveDataPath / "new.dat"), "invalid archive deleted a save");
    Require(deleteSave(saveArchive, 2U) == 0xE0E046BEU &&
                std::filesystem::exists(saveDataPath / "new.dat"), "malformed deletion changed a save");
    Require(deleteSave(saveArchive, deleteDescriptor) == 0U &&
                !std::filesystem::exists(saveDataPath / "new.dat") &&
                std::filesystem::exists(saveDataPath / "test.dat"), "DeleteFile removed the wrong file");
    Require(deleteSave(saveArchive, deleteDescriptor) == 0xC8804470U,
            "repeated DeleteFile did not report missing file");

    Require(memory.Write8(0x1FF82190, 0x5A) &&
                memory.Write8(0x1FF82194, 0xFF) &&
                memory.Write32(0x1FF82080, 0x080D0144) &&
                memory.Write64(0x1FF82084, saveArchive, &faultAddress) &&
                memory.Write32(0x1FF8208C, 0) &&
                memory.Write32(0x1FF82090, 1) &&
                memory.Write32(0x1FF82094, 1) &&
                memory.Write32(0x1FF82098, 0x1A) &&
                memory.Write32(0x1FF8209C, 0x1FF82190) &&
                memory.Write32(0x1FF820A0, 0x1C) &&
                memory.Write32(0x1FF820A4, 0x1FF82194),
            "cannot write FS:ControlArchive request");
    state.r[0] = fsSession;
    uint8_t controlOutput = 0xFF;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x080D0040 &&
                memory.Read32(0x1FF82084, &response) && response == 0 &&
                memory.Read8(0x1FF82194, &controlOutput) &&
                controlOutput == 0,
            "FS:ControlArchive did not complete the mapped-buffer action");

    Require(memory.Write32(0x1FF82080, 0x080E0080) &&
                memory.Write64(0x1FF82084, saveArchive, &faultAddress),
            "cannot write FS:CloseArchive request");
    state.r[0] = fsSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x080E0040 &&
                memory.Read32(0x1FF82084, &response) && response == 0,
            "FS:CloseArchive did not close the SaveData handle");
    Require(memory.Write32(0x1FF82080, 0x080E0080) &&
                memory.Write64(0x1FF82084, saveArchive, &faultAddress),
            "cannot rewrite FS:CloseArchive request");
    state.r[0] = fsSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82084, &response) &&
                response == 0xC8804465,
            "FS:CloseArchive did not reject a stale archive handle");

    Require(memory.Fill(0x1FF82140, 1, 0) &&
                memory.Fill(0x1FF82150, 12, 0) &&
                memory.Write32(0x1FF82080, 0x08030204) &&
                memory.Write32(0x1FF82084, 0) &&
                memory.Write32(0x1FF82088, 3) &&
                memory.Write32(0x1FF8208C, 1) &&
                memory.Write32(0x1FF82090, 1) &&
                memory.Write32(0x1FF82094, 2) &&
                memory.Write32(0x1FF82098, 12) &&
                memory.Write32(0x1FF8209C, 1) &&
                memory.Write32(0x1FF820A0, 0) &&
                memory.Write32(0x1FF820A4, 0x4802) &&
                memory.Write32(0x1FF820A8, 0x1FF82140) &&
                memory.Write32(0x1FF820AC, 0x30002) &&
                memory.Write32(0x1FF820B0, 0x1FF82150),
            "cannot write FS:OpenFileDirectly request");
    state.r[0] = fsSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "FS:OpenFileDirectly did not open the original RomFS image");
    uint32_t romFsFile = 0;
    Require(memory.Read32(0x1FF82080, &response) &&
                response == 0x08030042 &&
                memory.Read32(0x1FF8208C, &romFsFile) && romFsFile != 0,
            "FS:OpenFileDirectly response is wrong");

    constexpr uint32_t readAddress = 0x1FF82200;
    constexpr uint32_t readOffset = 3;
    constexpr uint32_t readSize = 7;
    Require(memory.Fill(readAddress, readSize, 0xFF) &&
                memory.Write32(0x1FF82080, 0x080200C2) &&
                memory.Write32(0x1FF82084, readOffset) &&
                memory.Write32(0x1FF82088, 0) &&
                memory.Write32(0x1FF8208C, readSize) &&
                memory.Write32(0x1FF82090, (readSize << 4U) | 0xCU) &&
                memory.Write32(0x1FF82094, readAddress),
            "cannot write File:Read request");
    state.r[0] = romFsFile;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "File:Read did not resume");
    std::array<uint8_t, readSize> readBytes{};
    Require(memory.Read32(0x1FF82080, &response) &&
                response == 0x08020082 &&
                memory.ReadBytes(readAddress, readBytes) &&
                std::equal(readBytes.begin(), readBytes.end(),
                           romFsBytes.begin() + config.RomFsImageOffset +
                               readOffset),
            "File:Read did not copy the requested RomFS range");

    auto romFsProfile = host.RuntimeProfile();
    Require(romFsProfile.RomFsStreamOpenCalls == 1U &&
                romFsProfile.RomFsReadCalls == 1U &&
                romFsProfile.RomFsReadBytes == readSize,
            "the first RomFS read did not establish one persistent stream");

    constexpr uint32_t secondReadOffset = 1;
    constexpr uint32_t secondReadSize = 3;
    Require(memory.Fill(readAddress, secondReadSize, 0xFF) &&
                memory.Write32(0x1FF82080, 0x080200C2) &&
                memory.Write64(0x1FF82084, secondReadOffset,
                               &faultAddress) &&
                memory.Write32(0x1FF8208C, secondReadSize) &&
                memory.Write32(0x1FF82090,
                               (secondReadSize << 4U) | 0xCU) &&
                memory.Write32(0x1FF82094, readAddress),
            "cannot write the second File:Read request");
    state.r[0] = romFsFile;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "the second File:Read did not resume");
    std::array<uint8_t, secondReadSize> secondReadBytes{};
    Require(memory.Read32(0x1FF82088, &response) &&
                response == secondReadSize &&
                memory.ReadBytes(readAddress, secondReadBytes) &&
                std::equal(
                    secondReadBytes.begin(), secondReadBytes.end(),
                    romFsBytes.begin() + config.RomFsImageOffset +
                        secondReadOffset),
            "the persistent RomFS stream did not honor a backward offset");
    romFsProfile = host.RuntimeProfile();
    Require(romFsProfile.RomFsStreamOpenCalls == 1U &&
                romFsProfile.RomFsReadCalls == 2U &&
                romFsProfile.RomFsReadBytes ==
                    readSize + secondReadSize,
            "the second RomFS read reopened its host stream");

    Require(memory.Write32(0x1FF82080, 0x080200C2) &&
                memory.Write64(0x1FF82084, config.RomFsImageSize,
                               &faultAddress) &&
                memory.Write32(0x1FF8208C, 0) &&
                memory.Write32(0x1FF82090, 0xCU) &&
                memory.Write32(0x1FF82094, readAddress),
            "cannot write the zero-length EOF File:Read request");
    state.r[0] = romFsFile;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82088, &response) && response == 0,
            "zero-length RomFS read at EOF changed behavior");
    romFsProfile = host.RuntimeProfile();
    Require(romFsProfile.RomFsStreamOpenCalls == 1U &&
                romFsProfile.RomFsReadCalls == 3U &&
                romFsProfile.RomFsReadBytes ==
                    readSize + secondReadSize,
            "zero-length EOF read did not reuse the persistent stream");

    constexpr std::array<uint8_t, 2> invalidReadSentinel{0xA5, 0x5A};
    Require(memory.WriteBytes(readAddress, invalidReadSentinel) &&
                memory.Write32(0x1FF82080, 0x080200C2) &&
                memory.Write64(0x1FF82084,
                               config.RomFsImageSize - 1U,
                               &faultAddress) &&
                memory.Write32(0x1FF8208C,
                               invalidReadSentinel.size()) &&
                memory.Write32(
                    0x1FF82090,
                    (invalidReadSentinel.size() << 4U) | 0xCU) &&
                memory.Write32(0x1FF82094, readAddress),
            "cannot write the over-EOF File:Read request");
    state.r[0] = romFsFile;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Wait,
            "over-EOF RomFS read changed its error behavior");
    std::array<uint8_t, invalidReadSentinel.size()> invalidReadBytes{};
    Require(memory.ReadBytes(readAddress, invalidReadBytes) &&
                invalidReadBytes == invalidReadSentinel &&
                host.RuntimeProfile().RomFsReadCalls == 3U,
            "rejected over-EOF read touched memory or the backing stream");

    Require(memory.Write32(0x1FF82080, 0x08080000),
            "cannot write File:Close request");
    state.r[0] = romFsFile;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x08080040,
            "File:Close response is wrong");

    constexpr uint32_t appletAttributes = 0x300U;
    Require(memory.Write32(0x1FF82080, 0x00010040) &&
                memory.Write32(0x1FF82084, appletAttributes),
            "cannot write APT:GetLockHandle request");
    state.r[0] = aptSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "APT:GetLockHandle did not resume");
    uint32_t correctedAttributes = 0;
    uint32_t appletState = 1;
    uint32_t descriptor = 0;
    uint32_t lockHandle = 0;
    Require(memory.Read32(0x1FF82080, &response) &&
                response == 0x000100C2 &&
                memory.Read32(0x1FF82088, &correctedAttributes) &&
                correctedAttributes == appletAttributes &&
                memory.Read32(0x1FF8208C, &appletState) &&
                appletState == 0 &&
                memory.Read32(0x1FF82090, &descriptor) &&
                descriptor == 0 &&
                memory.Read32(0x1FF82094, &lockHandle) && lockHandle != 0,
            "APT:GetLockHandle response payload is wrong");
    Require(memory.Write32(0x1FF82100, lockHandle),
            "cannot write APT lock wait array");
    state.r[1] = 0x1FF82100;
    state.r[2] = 1;
    state.r[3] = 0;
    state.r[4] = 0xFFFFFFFFU;
    state.r[5] = 0x7FFFFFFFU;
    Require(host.HandleSvc(0x25, state, memory, context).Action ==
                NativeA32HostAction::Resume &&
                state.r[0] == 0 && state.r[1] == 0,
            "WaitSynchronizationN did not acquire the available APT lock");

    Require(memory.Write32(0x1FF82080, 0x00020080) &&
                memory.Write32(0x1FF82084, 0x300) &&
                memory.Write32(0x1FF82088, 0),
            "cannot write APT:Initialize request");
    state.r[0] = aptSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "APT:Initialize did not resume");
    uint32_t notificationEvent = 0;
    uint32_t parameterEvent = 0;
    Require(memory.Read32(0x1FF82080, &response) &&
                response == 0x00020043 &&
                memory.Read32(0x1FF82088, &descriptor) &&
                descriptor == 0x04000000 &&
                memory.Read32(0x1FF8208C, &notificationEvent) &&
                memory.Read32(0x1FF82090, &parameterEvent) &&
                notificationEvent != 0 && parameterEvent != 0,
            "APT:Initialize event response is wrong");
    Require(memory.Write32(0x1FF82100, notificationEvent) &&
                memory.Write32(0x1FF82104, parameterEvent),
            "cannot write APT event wait array");
    state.r[1] = 0x1FF82100;
    state.r[2] = 2;
    state.r[3] = 0;
    Require(host.HandleSvc(0x25, state, memory, context).Action ==
                NativeA32HostAction::Resume && state.r[1] == 1,
            "APT wakeup parameter event was not initially signaled");

    constexpr uint32_t transition = 0x62;
    Require(memory.Write32(0x1FF82140, transition) &&
                memory.Fill(0x1FF82150, 1, 0xFF) &&
                memory.Write32(0x1FF82000 + 0x180, 0x4002) &&
                memory.Write32(0x1FF82000 + 0x184, 0x1FF82150) &&
                memory.Write32(0x1FF82080, 0x004B00C2) &&
                memory.Write32(0x1FF82084, 7) &&
                memory.Write32(0x1FF82088, 4) &&
                memory.Write32(0x1FF8208C, 1) &&
                memory.Write32(0x1FF82090, 0x10402) &&
                memory.Write32(0x1FF82094, 0x1FF82140),
            "cannot write APT:UnlockTransition request");
    state.r[0] = aptSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "APT:UnlockTransition did not resume");
    uint8_t transitionOutput = 0xFF;
    Require(memory.Read32(0x1FF82080, &response) &&
                response == 0x004B0082 &&
                memory.Read8(0x1FF82150, &transitionOutput) &&
                transitionOutput == 0,
            "APT:UnlockTransition response is wrong");

    Require(memory.Write32(0x1FF82080, 0x00050100) &&
                memory.Write32(0x1FF82084, 0x3A707367) &&
                memory.Write32(0x1FF82088, 0x7570473A) &&
                memory.Write32(0x1FF8208C, 8),
            "cannot write srv:GetServiceHandle gsp::Gpu request");
    state.r[0] = srvSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "srv:GetServiceHandle did not open gsp::Gpu");
    uint32_t gspSession = 0;
    Require(memory.Read32(0x1FF8208C, &gspSession) && gspSession != 0,
            "srv:GetServiceHandle gsp::Gpu response has no session");

    Require(memory.Write32(0x1FF82080, 0x00080082) &&
                memory.Write32(0x1FF82084, 0x14000100) &&
                memory.Write32(0x1FF82088, 0x100) &&
                memory.Write32(0x1FF8208C, 0) &&
                memory.Write32(0x1FF82090, 0xFFFF8001),
            "cannot write GSP:FlushDataCache request");
    state.r[0] = gspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x00080040,
            "GSP:FlushDataCache rejected coherent guest memory");

    Require(memory.Write32(0x1FF82080, 0x001E0080) &&
                memory.Write32(0x1FF82084, 0x19) &&
                memory.Write32(0x1FF82088, 0x1A),
            "cannot write GSP:SetInternalPriorities request");
    state.r[0] = gspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x001E0040 && host.GspPriority() == 0x19 &&
                host.GspPriorityWithRights() == 0x1A,
            "GSP:SetInternalPriorities did not preserve the native values");

    Require(memory.Write32(0x1FF82080, 0x00050200) &&
                memory.Write32(0x1FF82084, 0) &&
                memory.Write32(0x1FF82088, 0) &&
                memory.Write32(0x1FF8208C, 0x14001000) &&
                memory.Write32(0x1FF82090, 0x14001000) &&
                memory.Write32(0x1FF82094, 960) &&
                memory.Write32(0x1FF82098, 0) &&
                memory.Write32(0x1FF8209C, 0) &&
                memory.Write32(0x1FF820A0, 0),
            "cannot write GSP:SetBufferSwap request");
    state.r[0] = gspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x00050040 &&
                host.TopFramebuffer().has_value() &&
                host.TopFramebuffer()->AddressLeft == 0x14001000 &&
                host.TopFramebuffer()->Stride == 960,
            "GSP:SetBufferSwap did not select the native top framebuffer");
    Require(memory.Write32(0x1FF82080, 0x00050200) &&
                memory.Write32(0x1FF82084, 1) &&
                memory.Write32(0x1FF82088, 0) &&
                memory.Write32(0x1FF8208C, 0x14003000) &&
                memory.Write32(0x1FF82090, 0x14003000) &&
                memory.Write32(0x1FF82094, 720) &&
                memory.Write32(0x1FF82098, 0) &&
                memory.Write32(0x1FF8209C, 0) &&
                memory.Write32(0x1FF820A0, 0),
            "cannot write bottom GSP:SetBufferSwap request");
    state.r[0] = gspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                host.BottomFramebuffer().has_value() &&
                host.BottomFramebuffer()->AddressLeft == 0x14003000 &&
                host.BottomFramebuffer()->Stride == 720,
            "GSP:SetBufferSwap did not preserve the native bottom framebuffer");

    Require(memory.Write32(0x1FF82080, 0x00050100) &&
                memory.Write32(0x1FF82084, 0x3A707364) &&
                memory.Write32(0x1FF82088, 0x5053443A) &&
                memory.Write32(0x1FF8208C, 8),
            "cannot write srv:GetServiceHandle dsp::DSP request");
    state.r[0] = srvSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "srv:GetServiceHandle did not open dsp::DSP");
    uint32_t dspSession = 0;
    Require(memory.Read32(0x1FF8208C, &dspSession) && dspSession != 0,
            "srv:GetServiceHandle dsp::DSP response has no session");

    constexpr uint32_t dspComponentAddress = 0x14000100;
    constexpr std::array<uint8_t, 12> dspComponent{
        0x44, 0x53, 0x50, 0x31, 0x02, 0x03,
        0x05, 0x07, 0x0B, 0x0D, 0x11, 0x13,
    };
    constexpr uint16_t dspProgramMask = 0x00FF;
    constexpr uint16_t dspDataMask = 0x0003;
    constexpr uint32_t dspComponentDescriptor =
        (static_cast<uint32_t>(dspComponent.size()) << 4U) | 0xAU;
    Require(memory.WriteBytes(dspComponentAddress, dspComponent) &&
                memory.Write32(0x1FF82080, 0x001100C2) &&
                memory.Write32(0x1FF82084,
                               static_cast<uint32_t>(dspComponent.size())) &&
                memory.Write32(0x1FF82088, dspProgramMask) &&
                memory.Write32(0x1FF8208C, dspDataMask) &&
                memory.Write32(0x1FF82090, dspComponentDescriptor) &&
                memory.Write32(0x1FF82094, dspComponentAddress),
            "cannot write DSP:LoadComponent request");
    state.r[0] = dspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "DSP:LoadComponent did not resume");
    uint32_t dspLoaded = 0;
    uint32_t returnedDspDescriptor = 0;
    uint32_t returnedDspAddress = 0;
    Require(memory.Read32(0x1FF82080, &response) &&
                response == 0x00110082 &&
                memory.Read32(0x1FF82088, &dspLoaded) && dspLoaded == 1 &&
                memory.Read32(0x1FF8208C, &returnedDspDescriptor) &&
                returnedDspDescriptor == dspComponentDescriptor &&
                memory.Read32(0x1FF82090, &returnedDspAddress) &&
                returnedDspAddress == dspComponentAddress &&
                host.DspComponent() == std::vector<uint8_t>(
                                           dspComponent.begin(),
                                           dspComponent.end()) &&
                host.DspProgramMask() == dspProgramMask &&
                host.DspDataMask() == dspDataMask,
            "DSP:LoadComponent did not preserve or return its native payload");

    state.r[1] = 1;
    Require(host.HandleSvc(0x17, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "cannot create DSP pipe interrupt event");
    const uint32_t dspPipeEvent = state.r[1];
    Require(memory.Write32(0x1FF82080, 0x00150082) &&
                memory.Write32(0x1FF82084, 2) &&
                memory.Write32(0x1FF82088, 2) &&
                memory.Write32(0x1FF8208C, 0) &&
                memory.Write32(0x1FF82090, dspPipeEvent),
            "cannot write DSP:RegisterInterruptEvents request");
    state.r[0] = dspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x00150040,
            "DSP:RegisterInterruptEvents did not preserve the guest event");
    Require(host.SignalDspInterrupt(2, 2),
            "registered DSP pipe interrupt could not be signaled");
    Require(memory.Write32(0x1FF82100, dspPipeEvent),
            "cannot write DSP event wait array");
    state.r[1] = 0x1FF82100;
    state.r[2] = 1;
    state.r[3] = 0;
    Require(host.HandleSvc(0x25, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                state.r[0] == 0 && state.r[1] == 0,
            "signaled DSP pipe event was not observable by the guest");

    Require(memory.Write32(0x1FF82080, 0x00160000),
            "cannot write DSP:GetSemaphoreEventHandle request");
    state.r[0] = dspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x00160042 &&
                memory.Read32(0x1FF82088, &descriptor) && descriptor == 0,
            "DSP:GetSemaphoreEventHandle response is wrong");
    uint32_t dspSemaphoreEvent = 0;
    Require(memory.Read32(0x1FF8208C, &dspSemaphoreEvent) &&
                dspSemaphoreEvent != 0,
            "DSP:GetSemaphoreEventHandle returned no event");

    constexpr uint16_t dspSemaphoreMask = 0x2000;
    Require(memory.Write32(0x1FF82080, 0x00170040) &&
                memory.Write32(0x1FF82084, dspSemaphoreMask),
            "cannot write DSP:SetSemaphoreMask request");
    state.r[0] = dspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x00170040 &&
                host.DspSemaphoreMask() == dspSemaphoreMask,
            "DSP:SetSemaphoreMask did not preserve the native mask");

    constexpr uint32_t dspPipeCommandAddress = 0x1FF82180;
    constexpr std::array<uint8_t, 4> dspInitializeCommand{0, 0, 0xA5,
                                                         0x5A};
    Require(memory.WriteBytes(dspPipeCommandAddress, dspInitializeCommand) &&
                memory.Write32(0x1FF82080, 0x000D0082) &&
                memory.Write32(0x1FF82084, 2) &&
                memory.Write32(0x1FF82088, 4) &&
                memory.Write32(0x1FF8208C, 0x00010402) &&
                memory.Write32(0x1FF82090, dspPipeCommandAddress),
            "cannot write DSP:WriteProcessPipe request");
    state.r[0] = dspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x000D0040,
            "DSP:WriteProcessPipe did not initialize the audio pipe");
    Require(memory.Write32(0x1FF82100, dspPipeEvent),
            "cannot rewrite DSP pipe event wait array");
    state.r[1] = 0x1FF82100;
    state.r[2] = 1;
    state.r[3] = 0;
    Require(host.HandleSvc(0x25, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                state.r[0] == 0 && state.r[1] == 0,
            "DSP audio initialization did not signal its pipe event");

    host.AdvanceSystemTicks(
        NativeA32CtrHostServices::DspAudioFrameTicks - 1U);
    Require(host.TakePendingDspAudioFrames() == 0U,
            "DSP audio tick fired before one native frame elapsed");
    host.AdvanceSystemTicks(1U);
    Require(host.TakePendingDspAudioFrames() == 1U &&
                host.SignalDspAudioFrame(),
            "DSP audio tick did not follow the native frame cadence");
    Require(memory.Write32(0x1FF82100, dspPipeEvent),
            "cannot rewrite periodic DSP event wait array");
    state.r[1] = 0x1FF82100;
    state.r[2] = 1;
    state.r[3] = 0;
    Require(host.HandleSvc(0x25, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                state.r[0] == 0 && state.r[1] == 0,
            "periodic DSP audio frame did not signal its pipe event");
    host.AdvanceSystemTicks(
        NativeA32CtrHostServices::DspAudioFrameTicks * 2U);
    Require(host.TakePendingDspAudioFrames() == 2U,
            "DSP audio cadence did not preserve multiple elapsed frames");

    constexpr uint16_t dspSemaphoreValue = 0x4000;
    Require(memory.Write32(0x1FF82080, 0x00070040) &&
                memory.Write32(0x1FF82084, dspSemaphoreValue),
            "cannot write DSP:SetSemaphore request");
    state.r[0] = dspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x00070040 &&
                host.DspSemaphoreValue() == dspSemaphoreValue,
            "DSP:SetSemaphore did not preserve the native value");

    constexpr uint32_t dspPipeReadAddress = 0x1FF82200;
    constexpr uint32_t dspPipeReadCapacity = 0x40;
    Require(memory.Fill(dspPipeReadAddress, dspPipeReadCapacity, 0xFF) &&
                memory.Write32(0x1FF82000 + 0x180,
                               (dspPipeReadCapacity << 14U) | 2U) &&
                memory.Write32(0x1FF82000 + 0x184,
                               dspPipeReadAddress) &&
                memory.Write32(0x1FF82080, 0x001000C0) &&
                memory.Write32(0x1FF82084, 2) &&
                memory.Write32(0x1FF82088, 0) &&
                memory.Write32(0x1FF8208C, 2),
            "cannot write DSP:ReadPipeIfPossible request");
    state.r[0] = dspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "DSP:ReadPipeIfPossible did not resume");
    uint32_t dspPipeReturnedSize = 0;
    uint16_t dspStructCount = 0;
    Require(memory.Read32(0x1FF82080, &response) &&
                response == 0x00100082 &&
                memory.Read32(0x1FF82088, &dspPipeReturnedSize) &&
                dspPipeReturnedSize == 2 &&
                memory.Read16(dspPipeReadAddress, &dspStructCount) &&
                dspStructCount == 15,
            "DSP audio pipe did not return its native structure count");

    Require(memory.MapRegion({"dsp_ram", 0x1FF00000, 0x80000, true,
                              false, {}},
                             &error),
            error);
    Require(memory.Write32(0x1FF82080, 0x000C0040) &&
                memory.Write32(0x1FF82084, 0xBFFF),
            "cannot write DSP:ConvertProcessAddress request");
    state.r[0] = dspSession;
    uint32_t convertedDspAddress = 0;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x000C0080 &&
                memory.Read32(0x1FF82088, &convertedDspAddress) &&
                convertedDspAddress == 0x1FF57FFE,
            "DSP:ConvertProcessAddress did not expose the DSP RAM view");

    Require(memory.Write32(0x1FF82080, 0x001F0000),
            "cannot write DSP:GetHeadphoneStatus request");
    state.r[0] = dspSession;
    uint32_t headphonesConnected = 0;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x001F0080 &&
                memory.Read32(0x1FF82088, &headphonesConnected) &&
                headphonesConnected == 1,
            "DSP:GetHeadphoneStatus ignored the host audio route");

    Require(memory.Write32(0x1FF82080, 0x00050100) &&
                memory.Write32(0x1FF82084, 0x3A676663) &&
                memory.Write32(0x1FF82088, 0x00000075) &&
                memory.Write32(0x1FF8208C, 5),
            "cannot write srv:GetServiceHandle cfg:u request");
    state.r[0] = srvSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "srv:GetServiceHandle did not open cfg:u");
    uint32_t cfgSession = 0;
    Require(memory.Read32(0x1FF8208C, &cfgSession) && cfgSession != 0,
            "srv:GetServiceHandle cfg:u response has no session");
    constexpr uint32_t configValueAddress = 0x1FF821F0;
    const auto readConfigByte = [&](uint32_t blockId, uint8_t expected) {
        Require(memory.Write8(configValueAddress, 0xFF) &&
                    memory.Write32(0x1FF82080, 0x00010082) &&
                    memory.Write32(0x1FF82084, 1) &&
                    memory.Write32(0x1FF82088, blockId) &&
                    memory.Write32(0x1FF8208C, 0x1C) &&
                    memory.Write32(0x1FF82090, configValueAddress),
                "cannot write CFG:GetConfig request");
        state.r[0] = cfgSession;
        Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume,
                "CFG:GetConfig did not resume");
        uint8_t value = 0xFF;
        Require(memory.Read32(0x1FF82080, &response) &&
                    response == 0x00010042 &&
                    memory.Read8(configValueAddress, &value) &&
                    value == expected,
                "CFG:GetConfig returned the wrong host setting");
    };
    readConfigByte(0x00070001, 2);
    readConfigByte(0x000A0002, 4);
    Require(memory.Write32(0x1FF82080, 0x00020000),
            "cannot write CFG:GetRegion request");
    state.r[0] = cfgSession;
    uint32_t systemRegion = 0;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x00020080 &&
                memory.Read32(0x1FF82088, &systemRegion) &&
                systemRegion == 2,
            "CFG:GetRegion ignored the host region");
    Require(memory.Write32(0x1FF82080, 0x00010082) &&
                memory.Write32(0x1FF82084, 32) &&
                memory.Write32(0x1FF82088, 0x00050005) &&
                memory.Write32(0x1FF8208C, 0x20C) &&
                memory.Write32(0x1FF82090, 0x14000500),
            "cannot write CFG stereo camera request");
    state.r[0] = cfgSession;
    uint32_t stereoCameraFirstValue = 0;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x14000500, &stereoCameraFirstValue) &&
                stereoCameraFirstValue == 0x42780000,
            "CFG stereo camera calibration was not exposed structurally");

    Require(memory.Write32(0x1FF82080, 0x00050100) &&
                memory.Write32(0x1FF82084, 0x3A646968) &&
                memory.Write32(0x1FF82088, 0x52455355) &&
                memory.Write32(0x1FF8208C, 8),
            "cannot write srv:GetServiceHandle hid:USER request");
    state.r[0] = srvSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "srv:GetServiceHandle did not open hid:USER");
    uint32_t hidSession = 0;
    Require(memory.Read32(0x1FF8208C, &hidSession) && hidSession != 0,
            "srv:GetServiceHandle hid:USER response has no session");
    Require(memory.Write32(0x1FF82080, 0x000A0000),
            "cannot write HID:GetIPCHandles request");
    state.r[0] = hidSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "HID:GetIPCHandles did not resume");
    std::array<uint32_t, 6> hidHandles{};
    bool hidHandlesReadable = true;
    for (size_t index = 0; index < hidHandles.size(); ++index) {
        hidHandlesReadable =
            hidHandlesReadable &&
            memory.Read32(0x1FF8208C +
                              static_cast<uint32_t>(index * 4U),
                          &hidHandles[index]);
    }
    Require(memory.Read32(0x1FF82080, &response) &&
                response == 0x000A0047 &&
                memory.Read32(0x1FF82088, &descriptor) &&
                descriptor == 0x14000000 && hidHandlesReadable &&
                std::all_of(hidHandles.begin(), hidHandles.end(),
                            [](uint32_t handle) { return handle != 0; }),
            "HID:GetIPCHandles response is incomplete");
    state.r[0] = hidHandles[0];
    state.r[1] = 0x10002000;
    state.r[2] = 1;
    state.r[3] = 0x10000000;
    Require(host.HandleSvc(0x1F, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.IsMapped(0x10002000, 0x1000),
            "HID shared memory could not be mapped read-only");

    NativeA32HidState hidState;
    hidState.Buttons =
        NativeA32HidButtonMask(NativeA32HidButton::A) |
        NativeA32HidButtonMask(NativeA32HidButton::Start);
    hidState.CirclePadX = 154;
    hidState.CirclePadY = -154;
    hidState.TouchX = 319;
    hidState.TouchY = 239;
    hidState.TouchPressed = true;
    const auto firstHidUpdate =
        host.AdvanceHidToCurrentTick(memory, hidState);
    constexpr uint32_t hidExpectedButtons =
        (1U << 0U) | (1U << 3U) | (1U << 28U) | (1U << 31U);
    uint32_t hidPadIndex = UINT32_MAX;
    uint32_t hidCurrentButtons = 0;
    uint32_t hidEntryButtons = 0;
    uint32_t hidAdditions = 0;
    uint32_t hidRemovals = UINT32_MAX;
    uint16_t hidCircleX = 0;
    uint16_t hidCircleY = 0;
    uint32_t hidTouchPressed = 0;
    Require(firstHidUpdate.Status ==
                    NativeA32CtrHidUpdateStatus::Updated &&
                firstHidUpdate.SamplesWritten == 1U &&
                firstHidUpdate.EventsSignaled &&
                memory.Read32(0x10002010, &hidPadIndex) &&
                hidPadIndex == 0U &&
                memory.Read32(0x1000201C, &hidCurrentButtons) &&
                hidCurrentButtons == hidExpectedButtons &&
                memory.Read32(0x10002028, &hidEntryButtons) &&
                hidEntryButtons == hidExpectedButtons &&
                memory.Read32(0x1000202C, &hidAdditions) &&
                hidAdditions == hidExpectedButtons &&
                memory.Read32(0x10002030, &hidRemovals) &&
                hidRemovals == 0U &&
                memory.Read16(0x10002034, &hidCircleX) &&
                static_cast<int16_t>(hidCircleX) == 154 &&
                memory.Read16(0x10002036, &hidCircleY) &&
                static_cast<int16_t>(hidCircleY) == -154 &&
                memory.Read32(0x100020CC, &hidTouchPressed) &&
                hidTouchPressed == 1U,
            "native HID producer did not populate the CTR pad/touch ABI");
    Require(memory.Write32(0x1FF82100, hidHandles[1]),
            "cannot write HID pad event wait array");
    state.r[1] = 0x1FF82100;
    state.r[2] = 1;
    state.r[3] = 0;
    Require(host.HandleSvc(0x25, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                state.r[0] == 0U && state.r[1] == 0U,
            "native HID producer did not signal the guest pad event");

    host.AdvanceSystemTicks(NativeA32CtrHostServices::HidPadUpdateTicks);
    const auto secondHidUpdate =
        host.AdvanceHidToCurrentTick(memory, NativeA32HidState{});
    Require(secondHidUpdate.Status ==
                    NativeA32CtrHidUpdateStatus::Updated &&
                secondHidUpdate.SamplesWritten == 1U &&
                memory.Read32(0x10002010, &hidPadIndex) &&
                hidPadIndex == 1U &&
                memory.Read32(0x10002038, &hidEntryButtons) &&
                hidEntryButtons == 0U &&
                memory.Read32(0x1000203C, &hidAdditions) &&
                hidAdditions == 0U &&
                memory.Read32(0x10002040, &hidRemovals) &&
                hidRemovals == hidExpectedButtons,
            "native HID producer did not preserve cadence or release edges");
    const auto hidProfile = host.HidRuntimeProfile();
    Require(hidProfile.SharedMemoryMapped &&
                hidProfile.SamplesWritten == 2U &&
                hidProfile.EventBatchesSignaled == 2U &&
                hidProfile.LastRemovals == hidExpectedButtons &&
                hidProfile.NextPadIndex == 2U,
            "native HID runtime profile lost producer state");

    Require(memory.Write32(0x1FF82080, 0x00110000),
            "cannot write HID:EnableAccelerometer request");
    state.r[0] = hidSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                host.HidAccelerometerEnabled(),
            "HID accelerometer was not enabled");
    Require(memory.Write32(0x1FF82080, 0x00130000),
            "cannot write HID:EnableGyroscopeLow request");
    state.r[0] = hidSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                host.HidGyroscopeEnabled(),
            "HID gyroscope was not enabled");
    NativeA32HidState motionState;
    motionState.Accelerometer = {0.5F, -1.0F, 0.25F};
    motionState.AccelerometerValid = true;
    motionState.GyroscopeDegreesPerSecond = {10.0F, -20.0F, 30.0F};
    motionState.GyroscopeValid = true;
    const auto motionUpdate =
        host.AdvanceHidToCurrentTick(memory, motionState);
    uint32_t accelerometerIndex = UINT32_MAX;
    uint32_t gyroscopeIndex = UINT32_MAX;
    std::array<uint16_t, 3> accelerometerEntry{};
    std::array<uint16_t, 3> accelerometerRaw{};
    std::array<uint16_t, 3> gyroscopeEntry{};
    std::array<uint16_t, 3> gyroscopeRaw{};
    const auto readVector = [&](uint32_t address,
                                std::array<uint16_t, 3>& output) {
        return memory.Read16(address, &output[0]) &&
               memory.Read16(address + 2U, &output[1]) &&
               memory.Read16(address + 4U, &output[2]);
    };
    Require(
        motionUpdate.Status == NativeA32CtrHidUpdateStatus::Updated &&
            motionUpdate.SamplesWritten == 0U &&
            motionUpdate.AccelerometerSamplesWritten == 1U &&
            motionUpdate.GyroscopeSamplesWritten == 1U &&
            memory.Read32(0x10002118, &accelerometerIndex) &&
            accelerometerIndex == 0U &&
            memory.Read32(0x10002168, &gyroscopeIndex) &&
            gyroscopeIndex == 0U &&
            readVector(0x10002120, accelerometerRaw) &&
            static_cast<int16_t>(accelerometerRaw[0]) == -512 &&
            static_cast<int16_t>(accelerometerRaw[1]) == -256 &&
            static_cast<int16_t>(accelerometerRaw[2]) == -1024 &&
            readVector(0x10002128, accelerometerEntry) &&
            static_cast<int16_t>(accelerometerEntry[0]) == 256 &&
            static_cast<int16_t>(accelerometerEntry[1]) == -512 &&
            static_cast<int16_t>(accelerometerEntry[2]) == 128 &&
            readVector(0x10002170, gyroscopeRaw) &&
            static_cast<int16_t>(gyroscopeRaw[0]) == 144 &&
            static_cast<int16_t>(gyroscopeRaw[1]) == 431 &&
            static_cast<int16_t>(gyroscopeRaw[2]) == 288 &&
            readVector(0x10002178, gyroscopeEntry) &&
            static_cast<int16_t>(gyroscopeEntry[0]) == 144 &&
            static_cast<int16_t>(gyroscopeEntry[1]) == -288 &&
            static_cast<int16_t>(gyroscopeEntry[2]) == 431,
        "native HID producer did not populate CTR motion rings");
    const auto motionProfile = host.HidRuntimeProfile();
    Require(motionProfile.AccelerometerSamplesWritten == 1U &&
                motionProfile.GyroscopeSamplesWritten == 1U &&
                motionProfile.NextAccelerometerIndex == 1U &&
                motionProfile.NextGyroscopeIndex == 1U,
            "native HID motion profile lost producer state");
    // Consumer locations from libctru hidScanInput (word 86 + header 8),
    // independent of producer constants. Exercise both rings through wrap.
    host.AdvanceSystemTicks(NativeA32CtrHostServices::HidGyroscopeUpdateTicks * 40U);
    Require(host.AdvanceHidToCurrentTick(memory, motionState).Status ==
                NativeA32CtrHidUpdateStatus::Updated,
            "motion rings did not advance");
    for (uint32_t index = 0; index < 8U; ++index) {
        Require(readVector(0x10002128U + index * 6U, accelerometerEntry) &&
                    static_cast<int16_t>(accelerometerEntry[0]) == 256 &&
                    static_cast<int16_t>(accelerometerEntry[1]) == -512 &&
                    static_cast<int16_t>(accelerometerEntry[2]) == 128,
                "gyro timestamps overwrote the accelerometer tail");
    }
    for (uint32_t index = 0; index < 32U; ++index) {
        Require(readVector(0x10002178U + index * 6U, gyroscopeEntry) &&
                    static_cast<int16_t>(gyroscopeEntry[0]) == 144 &&
                    static_cast<int16_t>(gyroscopeEntry[1]) == -288 &&
                    static_cast<int16_t>(gyroscopeEntry[2]) == 431,
                "libctru-compatible consumer read shifted gyro data");
    }
    Require(memory.Write32(0x1FF82080, 0x00160000),
            "cannot write HID gyro calibration request");
    state.r[0] = hidSession;
    uint16_t gyroPositiveUnit = 0;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x00160180 &&
                memory.Read16(0x1FF8208A, &gyroPositiveUnit) &&
                gyroPositiveUnit == 6700,
            "HID gyro calibration response is wrong");

    Require(memory.Write32(0x1FF82080, 0x00050100) &&
                memory.Write32(0x1FF82084, 0x3A6D646E) &&
                memory.Write32(0x1FF82088, 0x00000075) &&
                memory.Write32(0x1FF8208C, 5),
            "cannot write srv:GetServiceHandle ndm:u request");
    state.r[0] = srvSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "srv:GetServiceHandle did not open ndm:u");
    uint32_t ndmSession = 0;
    Require(memory.Read32(0x1FF8208C, &ndmSession) && ndmSession != 0,
            "srv:GetServiceHandle ndm:u response has no session");
    Require(memory.Write32(0x1FF82080, 0x00080040) &&
                memory.Write32(0x1FF82084, 1),
            "cannot write NDM:SuspendScheduler request");
    state.r[0] = ndmSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x00080040 && host.NdmSchedulerSuspended() &&
                host.NdmSchedulerRunsInBackground(),
            "NDM:SuspendScheduler did not preserve its native state");
    Require(memory.Write32(0x1FF82080, 0x00090000),
            "cannot write NDM:ResumeScheduler request");
    state.r[0] = ndmSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                !host.NdmSchedulerSuspended() &&
                !host.NdmSchedulerRunsInBackground(),
            "NDM:ResumeScheduler did not restore its native state");

    Require(memory.Write32(0x1FF82080, 0x00160042) &&
                memory.Write32(0x1FF82084, 0) &&
                memory.Write32(0x1FF82088, 0) &&
                memory.Write32(0x1FF8208C, 0xFFFF8001),
            "cannot write GSP:AcquireRight request");
    state.r[0] = gspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x00160040,
            "GSP:AcquireRight response is wrong");

    state.r[1] = 0;
    Require(host.HandleSvc(0x17, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "cannot create GSP interrupt event");
    const uint32_t gspInterruptEvent = state.r[1];
    Require(memory.Write32(0x1FF82080, 0x00130042) &&
                memory.Write32(0x1FF82084, 1) &&
                memory.Write32(0x1FF82088, 0) &&
                memory.Write32(0x1FF8208C, gspInterruptEvent),
            "cannot write GSP:RegisterInterruptRelayQueue request");
    state.r[0] = gspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "GSP:RegisterInterruptRelayQueue did not resume");
    uint32_t gspSharedMemory = 0;
    Require(memory.Read32(0x1FF82080, &response) &&
                response == 0x00130082 &&
                memory.Read32(0x1FF82084, &descriptor) &&
                descriptor == 0x2A07 &&
                memory.Read32(0x1FF82090, &gspSharedMemory) &&
                gspSharedMemory != 0,
            "GSP:RegisterInterruptRelayQueue response is wrong");

    Require(memory.Write32(0x1FF82080, 0x000B0040) &&
                memory.Write32(0x1FF82084, 0),
            "cannot write GSP:SetLcdForceBlack request");
    state.r[0] = gspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.Read32(0x1FF82080, &response) &&
                response == 0x000B0040 && !host.LcdForceBlack(),
            "GSP:SetLcdForceBlack did not expose native LCD state");

    state.r[0] = gspSharedMemory;
    state.r[1] = 0x10000000;
    state.r[2] = 3;
    state.r[3] = 0x10000000;
    Require(host.HandleSvc(0x1F, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                memory.IsMapped(0x10000000, 0x1000) &&
                memory.IsWritable(0x10000000, 0x1000),
            "MapMemoryBlock did not map writable GSP shared memory");
    constexpr uint32_t topFramebufferUpdate = 0x10000200;
    constexpr uint32_t topFramebufferInfo1 = topFramebufferUpdate + 0x20;
    Require(memory.Write8(topFramebufferUpdate, 1) &&
                memory.Write8(topFramebufferUpdate + 1U, 1) &&
                memory.Write32(topFramebufferInfo1, 1) &&
                memory.Write32(topFramebufferInfo1 + 4U, 0x14002000) &&
                memory.Write32(topFramebufferInfo1 + 8U, 0x14002000) &&
                memory.Write32(topFramebufferInfo1 + 12U, 720) &&
                memory.Write32(topFramebufferInfo1 + 16U, 2) &&
                memory.Write32(topFramebufferInfo1 + 20U, 1) &&
                memory.Write32(topFramebufferInfo1 + 24U, 0),
            "cannot write native GSP shared framebuffer update");
    uint8_t gspInterruptCount = 0;
    uint8_t topVBlankInterrupt = 0;
    uint8_t bottomVBlankInterrupt = 0;
    Require(host.SignalVBlank(memory) &&
                memory.Read8(0x10000001, &gspInterruptCount) &&
                memory.Read8(0x1000000C, &topVBlankInterrupt) &&
                memory.Read8(0x1000000D, &bottomVBlankInterrupt) &&
                gspInterruptCount == 2 && topVBlankInterrupt == 2 &&
                bottomVBlankInterrupt == 3 &&
                host.TopFramebuffer().has_value() &&
                host.TopFramebuffer()->AddressLeft == 0x14002000 &&
                host.TopFramebuffer()->BufferIndex == 1,
            "VBlank did not apply the framebuffer update and enter the relay queue");
    uint8_t framebufferDirty = 1;
    Require(memory.Read8(topFramebufferUpdate + 1U, &framebufferDirty) &&
                framebufferDirty == 0,
            "VBlank did not acknowledge the shared framebuffer update");
    uint8_t p3dInterrupt = 0;
    Require(host.SignalPicaInterrupt(
                memory, Oot3dNativeGame::Oot3dPicaInterruptId::P3d) &&
                memory.Read8(0x1000000E, &p3dInterrupt) &&
                memory.Read8(0x10000001, &gspInterruptCount) &&
                gspInterruptCount == 3 && p3dInterrupt == 5,
            "P3D completion did not enter the native GSP relay queue");

    Require(memory.Write8(0x10000001, 0x20) &&
                memory.Write32(0x10000004, 0) &&
                memory.Write32(0x10000008, 0) &&
                host.SignalVBlank(memory),
            "cannot exercise the native PDC relay threshold");
    uint32_t missedPdc0 = 0;
    uint32_t missedPdc1 = 0;
    Require(memory.Read8(0x10000001, &gspInterruptCount) &&
                memory.Read32(0x10000004, &missedPdc0) &&
                memory.Read32(0x10000008, &missedPdc1) &&
                gspInterruptCount == 0x20 && missedPdc0 == 1 &&
                missedPdc1 == 1,
            "PDC interrupts did not use the native missed-event counters");
    Require(memory.Write8(0x10000001, 0) &&
                memory.Write8(0x10000003, 1) &&
                memory.Write32(0x10000004, 0) &&
                memory.Write32(0x10000008, 0) &&
                host.SignalVBlank(memory) &&
                memory.Read8(0x10000001, &gspInterruptCount) &&
                memory.Read32(0x10000004, &missedPdc0) &&
                memory.Read32(0x10000008, &missedPdc1) &&
                gspInterruptCount == 0 && missedPdc0 == 0 &&
                missedPdc1 == 0 && memory.Write8(0x10000003, 0) &&
                memory.Write8(0x10000001, 3),
            "GSP ignore-PDC configuration was not honored");

    constexpr uint32_t dmaSource = 0x14000100;
    constexpr uint32_t dmaDestination = 0x14000200;
    constexpr std::array<uint8_t, 8> dmaSourceBytes{
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    constexpr uint32_t gspCommandQueue = 0x10000800;
    constexpr uint32_t gspCommand = gspCommandQueue + 0x20;
    Require(memory.WriteBytes(dmaSource, dmaSourceBytes) &&
                memory.Fill(dmaDestination, dmaSourceBytes.size(), 0) &&
                memory.Write32(gspCommandQueue, 0x00000100) &&
                memory.Write32(gspCommand, 0x01000100) &&
                memory.Write32(gspCommand + 4U, dmaSource) &&
                memory.Write32(gspCommand + 8U, dmaDestination) &&
                memory.Write32(gspCommand + 12U,
                               static_cast<uint32_t>(dmaSourceBytes.size())) &&
                memory.Write32(0x1FF82080, 0x000C0000),
            "cannot prepare native GSP DMA command");
    state.r[0] = gspSession;
    Require(host.HandleSvc(0x32, state, memory, context).Action ==
                NativeA32HostAction::Resume,
            "GSP DMA command did not resume");
    std::array<uint8_t, dmaSourceBytes.size()> dmaDestinationBytes{};
    uint32_t processedQueueHeader = 0;
    uint8_t dmaInterrupt = 0;
    Require(memory.ReadBytes(dmaDestination, dmaDestinationBytes) &&
                dmaDestinationBytes == dmaSourceBytes &&
                memory.Read32(gspCommandQueue, &processedQueueHeader) &&
                processedQueueHeader == 1 &&
                memory.Read8(0x1000000F, &dmaInterrupt) &&
                memory.Read8(0x10000001, &gspInterruptCount) &&
                gspInterruptCount == 4 && dmaInterrupt == 6,
            "GSP DMA did not copy memory and signal native completion");

    const uint64_t ticksBeforeClockAdvance = host.SystemTicks();
    constexpr uint64_t clockAdvance = 0x123456789ABCDEF0ULL;
    const uint64_t expectedSystemTicks = ticksBeforeClockAdvance + clockAdvance;
    host.AdvanceSystemTicks(clockAdvance);
    Require(host.HandleSvc(0x28, state, memory, context).Action ==
                NativeA32HostAction::Resume &&
                state.r[0] == static_cast<uint32_t>(expectedSystemTicks) &&
                state.r[1] ==
                    static_cast<uint32_t>(expectedSystemTicks >> 32U) &&
                host.SystemTicks() == expectedSystemTicks,
            "GetSystemTick did not expose the host-controlled CTR clock");

    const uint64_t ticksBeforeSleep = host.SystemTicks();
    state.r[0] = 100000;
    state.r[1] = 0;
    constexpr uint64_t sleepTicks = 26812;
    Require(host.HandleSvc(0x0A, state, memory, context).Action ==
                    NativeA32HostAction::Wait &&
                host.SystemTicks() == ticksBeforeSleep &&
                host.NextSleepWakeTick() == ticksBeforeSleep + sleepTicks &&
                host.PendingSleepCount() == 1,
            "SleepThread did not schedule an independent CTR timer");
    host.AdvanceSystemTicks(sleepTicks - 1U);
    Require(host.PendingSleepCount() == 1 &&
                host.NextSleepWakeTick() == ticksBeforeSleep + sleepTicks,
            "SleepThread woke before its CTR timer deadline");
    host.AdvanceSystemTicks(1U);
    Require(host.PendingSleepCount() == 0 &&
                !host.NextSleepWakeTick().has_value() &&
                host.SystemTicks() == ticksBeforeSleep + sleepTicks,
            "SleepThread timer did not expire at its CTR deadline");
    state.r[0] = 0;
    state.r[1] = 0;
    Require(host.HandleSvc(0x0A, state, memory, context).Action ==
                    NativeA32HostAction::Resume &&
                host.SystemTicks() == ticksBeforeSleep + sleepTicks,
            "zero-duration SleepThread changed the CTR clock");

    const uint64_t ticksBeforeSynchronousSleep = host.SystemTicks();
    state.r[0] = 100000;
    state.r[1] = 0;
    const auto synchronousSleep =
        host.HandleSvc(0x0A, state, memory, context);
    error.clear();
    Require(
        synchronousSleep.Action == NativeA32HostAction::Wait &&
            host.CompleteSynchronousWait(
                0x0A, synchronousSleep, state, memory, context, &error) &&
            error.empty() &&
            host.SystemTicks() ==
                ticksBeforeSynchronousSleep + sleepTicks &&
            host.PendingSleepCount() == 0U,
        "finite synchronous SleepThread did not advance to its deadline");

    const uint64_t ticksBeforeInfiniteSleep = host.SystemTicks();
    state.r[0] = 0xFFFFFFFFU;
    state.r[1] = 0xFFFFFFFFU;
    const auto infiniteSleep =
        host.HandleSvc(0x0A, state, memory, context);
    error.clear();
    Require(
        infiniteSleep.Action == NativeA32HostAction::Wait &&
            !host.CompleteSynchronousWait(
                0x0A, infiniteSleep, state, memory, context, &error) &&
            !error.empty() &&
            host.SystemTicks() == ticksBeforeInfiniteSleep &&
            host.PendingSleepCount() == 0U,
        "infinite synchronous SleepThread did not remain scheduler-owned");

    const uint64_t savedSystemTicks = host.SystemTicks();
    const auto savedTopFramebuffer = host.TopFramebuffer();
    const auto hostStateBytes =
        nlohmann::json::to_msgpack(host.CaptureState());
    host.AdvanceSystemTicks(777U);
    Require(host.RestoreState(
                nlohmann::json::from_msgpack(hostStateBytes), process,
                &error) &&
                host.SystemTicks() == savedSystemTicks &&
                host.TopFramebuffer().has_value() ==
                    savedTopFramebuffer.has_value() &&
                (!savedTopFramebuffer.has_value() ||
                 host.TopFramebuffer()->AddressLeft ==
                     savedTopFramebuffer->AddressLeft),
            "CTR host state did not survive binary round-trip");

    auto crossPlatformHostState = host.CaptureState();
    bool foundRomFsObject = false;
    for (auto& object : crossPlatformHostState["kernel_objects"]) {
        if (object.at("type").get<std::string>() ==
            "client_session:file:romfs") {
            object["file_path"] =
                R"(Z:\foreign-host\extracted-game\romfs.bin)";
            foundRomFsObject = true;
        }
    }
    error.clear();
    Require(foundRomFsObject &&
                host.RestoreState(crossPlatformHostState, process, &error),
            "CTR host state did not safely rebind a foreign RomFS path");
    bool reboundRomFsObject = false;
    const auto reboundHostState = host.CaptureState();
    for (const auto& object : reboundHostState["kernel_objects"]) {
        if (object.at("type").get<std::string>() ==
            "client_session:file:romfs") {
            reboundRomFsObject =
                object.at("file_path").get<std::string>() ==
                romFsPath.string();
        }
    }
    Require(reboundRomFsObject,
            "restored RomFS object retained its foreign host path");

    auto incompatibleHostState = host.CaptureState();
    bool foundSaveDataObject = false;
    for (auto& object : incompatibleHostState["kernel_objects"]) {
        if (object.at("type").get<std::string>() ==
            "client_session:file:savedata") {
            object["file_path"] =
                (std::filesystem::temp_directory_path() /
                 "oot3d_native_a32_ctr_host_outside_savedata.dat")
                    .string();
            foundSaveDataObject = true;
            break;
        }
    }
    error.clear();
    Require(foundSaveDataObject &&
                !host.RestoreState(incompatibleHostState, process, &error) &&
                !error.empty(),
            "RomFS path rebinding weakened SaveData path confinement");

    NativeA32DspHle dspHle({});
    const auto dspStateBytes =
        nlohmann::json::to_msgpack(dspHle.CaptureState());
    Require(dspHle.RestoreState(
                nlohmann::json::from_msgpack(dspStateBytes), &error),
            "DSP HLE state did not survive binary round-trip");
    auto incompatibleDspState =
        nlohmann::json::from_msgpack(dspStateBytes);
    incompatibleDspState["sources"][0]["source_id"] = 1;
    Require(!dspHle.RestoreState(incompatibleDspState, &error),
            "DSP HLE state accepted an incompatible source identity");

    std::error_code removeError;
    std::filesystem::remove(romFsPath, removeError);
    std::filesystem::remove_all(saveDataPath, removeError);

    std::cout << "oot3d_native_a32_ctr_host_tests: ok\n";
    return 0;
}
