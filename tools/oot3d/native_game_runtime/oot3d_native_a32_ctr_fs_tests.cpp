#include "oot3d_native_a32_ctr_host.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
namespace {
using namespace Oot3dNativeGame;
using oot3d::recomp::a32::GuestState;
void Require(bool ok, std::string_view message) {
    if (!ok) {
        std::cerr << "oot3d_native_a32_ctr_fs_tests: " << message << '\n';
        std::exit(1);
    }
}
NativeA32CtrHostConfig MakeConfig() { return {}; }
} // namespace
// Filesystem regressions exercised through the public SVC/IPC boundary.
#include "triaevum/filesystem_service_adapter.h"
#include "triaevum/rooted_filesystem_backend.h"
#include "triaevum/service_registry.h"
#include "triaevum_ctr_filesystem_client_bridge.h"

namespace {
struct TestFsStorage {
    std::vector<uint8_t> Bytes{1, 2, 3, 4};
    bool FailRead = false, FailWrite = false, FailResize = false;
    bool ShortRead = false, ShortWrite = false;
    unsigned Closes = 0;
};
class TestFsFile final : public NativeA32CtrFile {
  public:
    explicit TestFsFile(TestFsStorage &storage) : Storage(storage) {}
    ~TestFsFile() override { ++Storage.Closes; }
    bool Read(uint64_t offset, std::span<uint8_t> bytes, uint32_t *count) override {
        if (Storage.FailRead)
            return false;
        *count = static_cast<uint32_t>(
            offset >= Storage.Bytes.size()
                ? 0U
                : std::min<uint64_t>(bytes.size(), Storage.Bytes.size() - offset));
        if (Storage.ShortRead && *count)
            --*count;
        if (*count)
            std::copy_n(Storage.Bytes.data() + offset, *count, bytes.data());
        return true;
    }
    bool Write(uint64_t offset, std::span<const uint8_t> bytes, uint32_t *count) override {
        if (Storage.FailWrite)
            return false;
        *count = static_cast<uint32_t>(bytes.size());
        if (Storage.ShortWrite && *count)
            --*count;
        Storage.Bytes.resize(std::max<uint64_t>(Storage.Bytes.size(), offset + *count));
        std::copy_n(bytes.data(), *count, Storage.Bytes.data() + offset);
        return true;
    }
    bool Resize(uint64_t size) override {
        if (Storage.FailResize)
            return false;
        Storage.Bytes.resize(static_cast<size_t>(size));
        return true;
    }
    uint64_t Size() const noexcept override { return Storage.Bytes.size(); }
    TestFsStorage &Storage;
};
class TestFs final : public NativeA32CtrFilesystem {
  public:
    bool EnsureSaveRoot() override { return !FailEnsure; }
    bool Open(NativeA32CtrFilesystemRoot, std::string_view, uint32_t,
              std::shared_ptr<NativeA32CtrFile> *file) override {
        ++Opens;
        if (FailOpen)
            return false;
        *file = std::make_shared<TestFsFile>(Storage);
        return true;
    }
    TestFsStorage Storage;
    bool FailOpen = false, FailEnsure = false;
    unsigned Opens = 0;
};
struct FsFixture {
    static constexpr uint32_t Tls = 0x1FF82000U, Cmd = Tls + 0x80U, Buffer = Tls + 0x200U;
    static NativeA32CtrHostConfig Config(NativeA32CtrFilesystem &fs) {
        auto config = MakeConfig();
        config.Filesystem = &fs;
        config.RomFsContentPath = "inputs/romfs";
        config.RomFsImageSize = 4U;
        return config;
    }
    explicit FsFixture(NativeA32CtrFilesystem &fs) : Host(Config(fs)), Process(Registry, Host) {
        std::string error;
        Require(Memory.MapRegion({"tls", Tls, 0x1000U, true, false, {}}, &error), error);
        State.thread_pointer = Tls;
        const std::array<uint8_t, 5> port{'s', 'r', 'v', ':', 0};
        Require(Memory.WriteBytes(Buffer, port), "port");
        State.r[1] = Buffer;
        NativeA32HostContext context{Process, 0U};
        Require(Host.HandleSvc(0x2D, State, Memory, context).Action == NativeA32HostAction::Resume,
                "srv open");
        const auto srv = State.r[1];
        Call(srv, {0x00050100U, 0x553A7366U, 0x00524553U, 7U});
        Fs = Word(3);
        Require(Memory.Fill(Buffer, 16U, 0U), "path");
        Call(Fs, {0x08030204U, 0U, 3U, 1U, 1U, 2U, 12U, 1U, 0U, 0x4002U, Buffer, 0x30002U, Buffer});
        Rom = Word(3);
        Require(Rom && Word(1) == 0U, "rom open");
        Call(Fs, {0x080C00C2U, 4U, 1U, 1U, 0x4002U, Buffer});
        Archive = Word(2);
        Require(Archive && Word(1) == 0U, "save archive");
        const std::array<uint8_t, 6> save{'s', 'a', 'v', 'e', 0, 0};
        Require(Memory.WriteBytes(Buffer, save), "save path");
        Call(Fs, {0x080201C2U, 0U, Archive, 0U, 3U, 5U, 3U, 0U, 0x14002U, Buffer});
        Save = Word(3);
        Require(Save && Word(1) == 0U, "save file");
    }
    NativeA32HostResult Call(uint32_t handle, std::initializer_list<uint32_t> words) {
        uint32_t address = Cmd;
        for (auto word : words) {
            Require(Memory.Write32(address, word), "request");
            address += 4;
        }
        State.r[0] = handle;
        NativeA32HostContext context{Process, 0U};
        auto result = Host.HandleSvc(0x32U, State, Memory, context);
        Require(result.Action != NativeA32HostAction::Wait, "FS request orphaned a guest thread");
        return result;
    }
    uint32_t Word(unsigned i) {
        uint32_t v = 0U;
        Require(Memory.Read32(Cmd + i * 4U, &v), "reply word");
        return v;
    }
    void Read(uint32_t handle, uint64_t offset, uint32_t length, uint32_t descriptor = 0x4CU) {
        Require(Call(handle, {0x080200C2U, static_cast<uint32_t>(offset),
                              static_cast<uint32_t>(offset >> 32U), length, descriptor, Buffer})
                        .Action == NativeA32HostAction::Resume,
                "read reply");
    }
    NativeA32CtrHostServices Host;
    const oot3d::recomp::a32::Registry Registry{};
    NativeA32Process Process;
    NativeA32Memory Memory;
    GuestState State{};
    uint32_t Fs = 0U, Rom = 0U, Save = 0U, Archive = 0U;
};
void TestFsFailuresAndRestore() {
    TestFs fs;
    FsFixture f(fs);
    Require(f.Memory.Fill(FsFixture::Buffer, 4U, 0xA5U), "sentinel");
    f.Read(f.Rom, 3U, 4U);
    uint8_t tail = 0U;
    Require(f.State.r[0] == 0U && f.Word(0) == 0x08020082U && f.Word(1) == 0U && f.Word(2) == 1U &&
                f.Word(3) == 0x4CU && f.Word(4) == FsFixture::Buffer &&
                f.Memory.Read8(FsFixture::Buffer + 1U, &tail) && tail == 0xA5U,
            "short EOF read overwrote tail");
    f.Read(f.Rom, 9U, 4U);
    Require(f.Word(1) == 0U && f.Word(2) == 0U, "past EOF reply");
    f.Read(f.Rom, UINT64_MAX, 4U);
    Require(f.Word(1) == 0xE0E046C1U && f.Word(2) == 0U, "overflow result");
    f.Read(f.Rom, 0U, 4U, 0x3CU);
    Require(f.State.r[0] == 0xD9001830U, "short descriptor result");
    f.Read(f.Rom, 0U, 4U, 0x4AU);
    Require(f.State.r[0] == 0xD9001830U, "wrong direction result");
    f.Read(f.Rom, 0U, 0xFFFFFFFFU, 0xFFFFFFFCU);
    Require(f.State.r[0] == 0xD9001830U, "huge descriptor allocated");
    f.Read(f.Rom, 0U, 4U, 0x4EU);
    Require(f.Word(1) == 0U && f.Word(2) == 4U, "read-write descriptor rejected");
    fs.Storage.FailRead = true;
    f.Read(f.Rom, 0U, 4U);
    Require(f.Word(1) == 0xD9604400U && f.Word(2) == 0U && f.State.r[0] == 0U,
            "backend failure was not an IPC error");
    fs.Storage.FailRead = false;
    fs.Storage.ShortRead = true;
    f.Read(f.Save, 0U, 4U);
    Require(f.Word(1) == 0U && f.Word(2) == 3U, "backend short read rejected");
    fs.Storage.ShortRead = false;
    for (bool shortWrite : {false, true}) {
        fs.Storage.FailWrite = !shortWrite;
        fs.Storage.ShortWrite = shortWrite;
        f.Call(f.Save, {0x08030102U, 0U, 0U, 4U, 1U, 0x4AU, FsFixture::Buffer});
        Require(f.Word(1) == 0xD9604400U && f.Word(2) == 0U, "failed write acknowledged");
    }
    fs.Storage.FailWrite = false;
    fs.Storage.ShortWrite = false;
    fs.Storage.FailResize = true;
    f.Call(f.Save, {0x08050080U, 8U, 0U});
    Require(f.Word(1) == 0xD9604400U, "failed resize acknowledged");
    fs.Storage.FailResize = false;
    f.Call(f.Rom, {0x08030102U, 0U, 0U, 4U, 0U, 0x4AU, FsFixture::Buffer});
    Require(f.Word(1) == 0xC92044E6U, "write to RomFS accepted");
    f.Call(f.Rom, {0x08050080U, 8U, 0U});
    Require(f.Word(1) == 0xC92044E6U, "resize RomFS accepted");
    f.Call(f.Fs, {0x08010002U, 0U, 0U});
    Require(f.State.r[0] == 0xD9001830U, "malformed initialize");
    f.Call(f.Fs, {0x09990000U});
    Require(f.Word(0) == 0x09990040U && f.Word(1) == 0xD8C047F4U, "unknown FS command");
    f.Call(f.Rom, {0x09980000U});
    Require(f.Word(1) == 0xD8C047F4U, "unknown file command");
    fs.FailEnsure = true;
    f.Call(f.Fs, {0x080C00C2U, 4U, 1U, 1U, 0x4002U, FsFixture::Buffer});
    Require(f.Word(1) == 0xD9604400U && f.Word(2) == 0U && f.Word(3) == 0U, "save root failure");
    fs.FailEnsure = false;
    const std::array<uint8_t, 5> createPath{'s', 'a', 'v', 'e', 0};
    Require(f.Memory.WriteBytes(FsFixture::Buffer, createPath), "create path");
    fs.FailOpen = true;
    f.Call(f.Fs, {0x08080202U, 0U, f.Archive, 0U, 3U, 5U, 0U, 4U, 0U, 0x14002U, FsFixture::Buffer});
    Require(f.Word(1) == 0xD9604400U, "CreateFile backend failure");
    Require(f.Memory.Fill(FsFixture::Buffer, 12U, 0U), "RomFS path");
    f.Call(f.Fs, {0x08030204U, 0U, 3U, 1U, 1U, 2U, 12U, 1U, 0U, 0x4002U, FsFixture::Buffer,
                  0x30002U, FsFixture::Buffer});
    Require(f.Word(1) == 0xD9604400U && f.Word(3) == 0U, "OpenFileDirectly backend failure");
    fs.FailOpen = false;
    auto snapshot = f.Host.CaptureState();
    std::string error;
    const auto count = f.Host.RuntimeProfile().FsUnservedRequests;
    fs.FailOpen = true;
    Require(!f.Host.RestoreState(snapshot, f.Process, &error),
            "restore accepted unavailable backing");
    fs.FailOpen = false;
    Require(f.Host.RestoreState(snapshot, f.Process, &error), error);
    Require(f.Host.RuntimeProfile().FsUnservedRequests == count, "restore rewound diagnostics");
    f.Read(f.Rom, 0U, 4U);
    Require(f.Word(1) == 0U && f.Word(2) == 4U, "restored read");
    for (auto &object : snapshot["kernel_objects"])
        if (object["type"] == "client_session:file:romfs")
            object["file_offset"] = 1U;
    Require(!f.Host.RestoreState(snapshot, f.Process, &error),
            "restore accepted mismatched RomFS view");
    f.Read(f.Rom, 0U, 4U);
    Require(f.Word(1) == 0U, "failed restore changed live state");
    auto closedBefore = fs.Storage.Closes;
    f.Call(f.Rom, {0x08080000U});
    Require(fs.Storage.Closes == closedBefore + 1U, "Close retained backing");
    snapshot = f.Host.CaptureState();
    auto opens = fs.Opens;
    Require(f.Host.RestoreState(snapshot, f.Process, &error), error);
    Require(fs.Opens == opens + 1U, "restore reopened closed RomFS");
    f.Read(f.Rom, 0U, 4U);
    Require(f.State.r[0] == 0xC920181AU, "closed file read");
    NativeA32HostContext context{f.Process, 0U};
    f.State.r[0] = f.Rom;
    Require(f.Host.HandleSvc(0x23U, f.State, f.Memory, context).Action ==
                NativeA32HostAction::Resume,
            "Close consumed kernel handle");
    f.Read(f.Rom, 0U, 4U);
    Require(f.State.r[0] == 0xD8E007F7U, "stale handle parked");
    // A readable request with a read-only reply buffer must not mutate the file.
    const std::array<uint32_t, 7> request{0x08030102U, 0U, 0U, 4U, 1U, 0x4AU, FsFixture::Buffer};
    std::array<uint8_t, 0x1000> readOnlyTls{};
    std::memcpy(readOnlyTls.data() + 0x80U, request.data(), sizeof(request));
    Require(
        f.Memory.MapRegion(
            {"readonly-tls", 0x20000000U, readOnlyTls.size(), false, false, readOnlyTls}, &error),
        error);
    const auto savedBytes = fs.Storage.Bytes;
    f.State.thread_pointer = 0x20000000U;
    f.State.r[0] = f.Save;
    Require(f.Host.HandleSvc(0x32U, f.State, f.Memory, context).Action ==
                    NativeA32HostAction::Fault &&
                fs.Storage.Bytes == savedBytes,
            "unwritable reply mutated save data");
    // No request memory: return a transport error without doing a backend write.
    f.State.thread_pointer = 0xFFFFFFFFU - 0x80U;
    f.State.r[0] = f.Save;
    Require(f.Host.HandleSvc(0x32U, f.State, f.Memory, context).Action ==
                    NativeA32HostAction::Resume &&
                f.State.r[0] == 0xD9001830U,
            "unmapped TLS parked");
    f.Host.WriteFsRequestSummary();
}

void TestFullHistoryIntentionalWait() {
    auto config = MakeConfig();
    config.RetainFullSvcHistory = true;
    NativeA32CtrHostServices host(config);
    const oot3d::recomp::a32::Registry registry{};
    NativeA32Process process(registry, host);
    NativeA32Memory memory;
    GuestState state{};
    NativeA32HostContext context{process, 0U};
    state.r[0] = 1000U;
    Require(host.HandleSvc(0x0AU, state, memory, context).Action == NativeA32HostAction::Wait,
            "full history turned a finite sleep into a fault");
    Require(host.HandleSvc(0xFFFFU, state, memory, context).Action == NativeA32HostAction::Fault,
            "full history missed an unimplemented SVC");
}

void TestFsRootedRoundTrip() {
    using namespace triaevum::module;
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("triaevum-ctr-fs-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    {
        std::ofstream out(directory / "romfs", std::ios::binary);
        out.write("abcd", 4);
    }
    RootedFilesystemConfigV1 config;
    config.contentFiles.emplace("inputs/romfs", directory / "romfs");
    config.saveRoot = directory / "save";
    std::string error;
    auto rooted = RootedFilesystemBackendV1::Create(std::move(config), &error);
    Require(rooted != nullptr, error);
    FilesystemHostServiceAdapterV1 adapter(*rooted);
    HostServiceRegistry registry({});
    Require(registry.Register(TRIAEVUM_SERVICE_FILESYSTEM_V1,
                              FilesystemHostServiceAdapterV1::Invoke,
                              &adapter) == ServiceRegistrationResult::Registered,
            "register FS");
    auto api = registry.SealAndCreateHostApi();
    FilesystemServiceClientV1 client(&api);
    TriAevumCtrFilesystemClientBridge bridge(client);
    {
        FsFixture f(bridge);
        f.Call(f.Save, {0x08030102U, 0U, 0U, 4U, 1U, 0x4AU, FsFixture::Buffer});
        Require(f.Word(1) == 0U, "rooted write");
        f.Read(f.Save, 3U, 4U);
        Require(f.Word(2) == 1U, "rooted short read");
        auto snapshot = f.Host.CaptureState();
        const auto before = rooted->Stats();
        Require(f.Host.RestoreState(snapshot, f.Process, &error), error);
        const auto after = rooted->Stats();
        Require(after.opens == before.opens + 2U && after.closes == before.closes + 2U,
                "restore leaked rooted handles");
        f.Read(f.Save, 0U, 4U);
        Require(f.Word(1) == 0U && f.Word(2) == 4U, "restored rooted read");
        f.Call(f.Save, {0x08050080U, 8U, 0U});
        Require(f.Word(1) == 0U, "resize after EOF");
        f.Call(f.Save, {0x08090000U});
        Require(f.Word(1) == 0U, "flush");
        f.Call(f.Save, {0x08080000U});
        Require(f.Word(1) == 0U, "close");
        bool removed = false;
        Require(bridge.RemoveFile("save", &removed) && removed,
                "closed session still owns rooted file");
        Require(f.Host.RestoreState(f.Host.CaptureState(), f.Process, &error),
                "closed deleted file reopened on restore");
    }
    Require(rooted->Stats().opens == rooted->Stats().closes, "rooted handles leaked");
    std::filesystem::remove_all(directory);
}
} // namespace

int main() {
    TestFullHistoryIntentionalWait();
    TestFsFailuresAndRestore();
    TestFsRootedRoundTrip();
    std::cout << "oot3d_native_a32_ctr_fs_tests: ok\n";
}
