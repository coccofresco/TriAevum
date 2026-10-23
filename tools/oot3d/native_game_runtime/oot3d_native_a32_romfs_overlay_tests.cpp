// RomFS overlay: O3RO validation, composed reads, and the CTR host's RomFS
// service view (GetSize/Read) with an overlay installed.
#include "oot3d_native_a32_ctr_host.h"
#include "oot3d_native_a32_romfs_overlay.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace Oot3dNativeGame;
using oot3d::recomp::a32::GuestState;

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_native_a32_romfs_overlay_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) Fail(message);
}

constexpr uint64_t kImagePrefix = 4U;  // Like an IVFC header before Level 3.
constexpr uint64_t kBaseViewSize = 64U;

void Put32(std::vector<uint8_t> &out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>(value >> shift));
}

void Put64(std::vector<uint8_t> &out, uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) out.push_back(static_cast<uint8_t>(value >> shift));
}

struct TestPatch {
    uint64_t Offset;
    std::vector<uint8_t> Expected;
    std::vector<uint8_t> Replacement;
};

std::vector<uint8_t> BuildOverlay(uint64_t baseViewSize, const std::vector<TestPatch> &patches,
                                  const std::vector<uint8_t> &tail) {
    std::vector<uint8_t> out{'O', '3', 'R', 'O'};
    Put32(out, 1U);
    Put64(out, baseViewSize);
    Put64(out, baseViewSize + tail.size());
    Put32(out, static_cast<uint32_t>(patches.size()));
    Put32(out, 0U);
    uint64_t data = 64U + patches.size() * 32U;
    for (const auto &patch : patches) data += patch.Expected.size() + patch.Replacement.size();
    Put64(out, data);  // Tail file offset.
    for (int index = 0; index < 3; ++index) Put64(out, 0U);
    uint64_t cursor = 64U + patches.size() * 32U;
    for (const auto &patch : patches) {
        Put64(out, patch.Offset);
        Put32(out, static_cast<uint32_t>(patch.Expected.size()));
        Put32(out, 0U);
        Put64(out, cursor);
        Put64(out, cursor + patch.Expected.size());
        cursor += patch.Expected.size() + patch.Replacement.size();
    }
    for (const auto &patch : patches) {
        out.insert(out.end(), patch.Expected.begin(), patch.Expected.end());
        out.insert(out.end(), patch.Replacement.begin(), patch.Replacement.end());
    }
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
}

void WriteFile(const std::filesystem::path &path, const std::vector<uint8_t> &bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Require(output.good(), "cannot write a temporary file");
}

std::vector<uint8_t> ImageBytes() {
    std::vector<uint8_t> image(kImagePrefix + kBaseViewSize);
    std::iota(image.begin(), image.end(), static_cast<uint8_t>(0x40));
    return image;
}

std::vector<uint8_t> ViewOf(const std::vector<uint8_t> &image) {
    return {image.begin() + static_cast<std::ptrdiff_t>(kImagePrefix), image.end()};
}

NativeA32RomFsOverlay::BaseReader ReaderOf(const std::vector<uint8_t> &view) {
    return [&view](uint64_t offset, std::span<uint8_t> bytes) {
        if (offset > view.size() || bytes.size() > view.size() - offset) return false;
        std::copy_n(view.begin() + static_cast<std::ptrdiff_t>(offset), bytes.size(), bytes.begin());
        return true;
    };
}

std::vector<uint8_t> Slice(const std::vector<uint8_t> &bytes, size_t offset, size_t count) {
    return {bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + count)};
}

// The expected composed view: base with patches, then the tail.
std::vector<uint8_t> Compose(std::vector<uint8_t> view, const std::vector<TestPatch> &patches,
                             const std::vector<uint8_t> &tail) {
    for (const auto &patch : patches)
        std::copy(patch.Replacement.begin(), patch.Replacement.end(),
                  view.begin() + static_cast<std::ptrdiff_t>(patch.Offset));
    view.insert(view.end(), tail.begin(), tail.end());
    return view;
}

void TestComposedReads(const std::filesystem::path &directory) {
    const auto view = ViewOf(ImageBytes());
    const std::vector<TestPatch> patches{
        {8U, Slice(view, 8U, 4U), {0xA0, 0xA1, 0xA2, 0xA3}},
        {40U, Slice(view, 40U, 8U), {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7}},
    };
    std::vector<uint8_t> tail(20U);
    std::iota(tail.begin(), tail.end(), static_cast<uint8_t>(0xC0));
    const auto path = directory / "composed.o3ro";
    WriteFile(path, BuildOverlay(kBaseViewSize, patches, tail));
    std::string error;
    const auto overlay = NativeA32RomFsOverlay::Load(path, kBaseViewSize, ReaderOf(view), &error);
    Require(overlay != nullptr, error);
    Require(overlay->ViewSize() == kBaseViewSize + tail.size() && overlay->PatchCount() == 2U,
            "overlay layout fields");
    const auto expected = Compose(view, patches, tail);
    // Every window of the composed view, including ones crossing patch and
    // base/tail boundaries, reads exactly the composed bytes.
    for (size_t offset = 0U; offset <= expected.size(); ++offset) {
        for (size_t count = 0U; offset + count <= expected.size(); count += 3U) {
            std::vector<uint8_t> bytes(count, 0xEE);
            Require(overlay->Read(offset, bytes, ReaderOf(view)) && bytes == Slice(expected, offset, count),
                    "composed read differs from the expected view");
        }
    }
    std::vector<uint8_t> past(2U);
    Require(!overlay->Read(expected.size() - 1U, past, ReaderOf(view)), "read past the view must fail");
}

void RequireRejected(const std::filesystem::path &path, const std::vector<uint8_t> &file,
                     const std::vector<uint8_t> &view, uint64_t baseViewSize, std::string_view what) {
    WriteFile(path, file);
    std::string error;
    Require(NativeA32RomFsOverlay::Load(path, baseViewSize, ReaderOf(view), &error) == nullptr && !error.empty(),
            what);
}

void TestRejections(const std::filesystem::path &directory) {
    const auto view = ViewOf(ImageBytes());
    const auto path = directory / "rejected.o3ro";
    const std::vector<uint8_t> tail{1, 2, 3};
    const TestPatch good{8U, Slice(view, 8U, 4U), {9, 9, 9, 9}};
    RequireRejected(path, BuildOverlay(kBaseViewSize, {good}, tail), view, kBaseViewSize + 16U,
                    "an overlay built for another base view size must be rejected");
    RequireRejected(path, BuildOverlay(kBaseViewSize, {{8U, {0, 0, 0, 0}, {9, 9, 9, 9}}}, tail), view,
                    kBaseViewSize, "an overlay whose expected bytes differ must be rejected");
    RequireRejected(path,
                    BuildOverlay(kBaseViewSize, {good, {10U, Slice(view, 10U, 4U), {7, 7, 7, 7}}}, tail),
                    view, kBaseViewSize, "overlapping patches must be rejected");
    RequireRejected(path, BuildOverlay(kBaseViewSize, {{62U, Slice(view, 62U, 2U), {1, 1}}, good}, tail),
                    view, kBaseViewSize, "unsorted patches must be rejected");
    auto truncated = BuildOverlay(kBaseViewSize, {good}, tail);
    truncated.pop_back();
    RequireRejected(path, truncated, view, kBaseViewSize, "a truncated tail must be rejected");
    auto magic = BuildOverlay(kBaseViewSize, {good}, tail);
    magic[0] = 'X';
    RequireRejected(path, magic, view, kBaseViewSize, "a foreign file must be rejected");
}

NativeA32CtrHostConfig MakeConfig() {
    NativeA32CtrHostConfig config;
    config.ResourceLimitValues = {0x18, 0x04000000, 0x20, 0x20, 0x20, 0x08, 0x08, 0x10, 0x02, 0};
    config.ResourceCurrentValues[1] = 0x1000;
    config.ResourceCurrentValues[2] = 1;
    config.LinearHeapBaseAddress = 0x14000000;
    config.LinearHeapSize = 0x04000000;
    config.HeapBaseAddress = 0x08000000;
    config.HeapSize = 0x08000000;
    return config;
}

constexpr uint32_t kCommand = 0x1FF82080U;

bool Command(NativeA32CtrHostServices &host, GuestState &state, NativeA32Memory &memory,
             NativeA32HostContext &context, uint32_t session, std::initializer_list<uint32_t> words) {
    uint32_t address = kCommand;
    for (const uint32_t word : words) {
        if (!memory.Write32(address, word)) return false;
        address += 4U;
    }
    state.r[0] = session;
    return host.HandleSvc(0x32, state, memory, context).Action == NativeA32HostAction::Resume;
}

void TestHostServiceView(const std::filesystem::path &directory) {
    const auto image = ImageBytes();
    const auto view = ViewOf(image);
    const auto imagePath = directory / "image.romfs";
    WriteFile(imagePath, image);
    const std::vector<TestPatch> patches{{16U, Slice(view, 16U, 4U), {0xD0, 0xD1, 0xD2, 0xD3}}};
    const std::vector<uint8_t> tail{0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7};
    const auto overlayPath = directory / "host.o3ro";
    WriteFile(overlayPath, BuildOverlay(kBaseViewSize, patches, tail));
    std::string error;
    auto overlay = NativeA32RomFsOverlay::Load(overlayPath, kBaseViewSize, ReaderOf(view), &error);
    Require(overlay != nullptr, error);

    NativeA32CtrHostConfig mismatched = MakeConfig();
    mismatched.RomFsImagePath = imagePath;
    mismatched.RomFsImageOffset = kImagePrefix;
    mismatched.RomFsImageSize = kBaseViewSize - 4U;
    mismatched.RomFsOverlay = overlay;
    bool rejected = false;
    try {
        NativeA32CtrHostServices host(std::move(mismatched));
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    Require(rejected, "the host must reject an overlay for another service view");

    NativeA32Memory memory;
    Require(memory.MapRegion({"tls", 0x1FF82000, 0x1000, true, false, {}}, &error), error);
    NativeA32CtrHostConfig config = MakeConfig();
    config.RomFsImagePath = imagePath;
    config.RomFsImageOffset = kImagePrefix;
    config.RomFsImageSize = kBaseViewSize;
    config.RomFsOverlay = overlay;
    NativeA32CtrHostServices host(std::move(config));
    const oot3d::recomp::a32::Registry registry{};
    NativeA32Process process(registry, host);
    NativeA32HostContext context{process, 0};
    GuestState state{};
    state.thread_pointer = 0x1FF82000;

    constexpr std::array<uint8_t, 5> portName{'s', 'r', 'v', ':', 0};
    Require(memory.WriteBytes(0x1FF82100, portName), "cannot write the srv: port name");
    state.r[1] = 0x1FF82100;
    Require(host.HandleSvc(0x2D, state, memory, context).Action == NativeA32HostAction::Resume,
            "ConnectToPort did not open srv:");
    const uint32_t srvSession = state.r[1];
    Require(Command(host, state, memory, context, srvSession, {0x00010002}), "srv:RegisterClient");
    Require(Command(host, state, memory, context, srvSession, {0x00050100, 0x553A7366, 0x00524553, 7}),
            "srv:GetServiceHandle fs:USER");
    uint32_t fsSession = 0;
    Require(memory.Read32(kCommand + 12U, &fsSession) && fsSession != 0, "fs:USER has no session");
    Require(Command(host, state, memory, context, fsSession, {0x08010002, 0x20, 0}), "FS:Initialize");
    Require(memory.Fill(0x1FF82140, 1, 0) && memory.Fill(0x1FF82150, 12, 0), "cannot clear RomFS paths");
    Require(Command(host, state, memory, context, fsSession,
                    {0x08030204, 0, 3, 1, 1, 2, 12, 1, 0, 0x4802, 0x1FF82140, 0x30002, 0x1FF82150}),
            "FS:OpenFileDirectly RomFS");
    uint32_t response = 0, romFsFile = 0;
    Require(memory.Read32(kCommand, &response) && response == 0x08030042 &&
                memory.Read32(kCommand + 12U, &romFsFile) && romFsFile != 0,
            "FS:OpenFileDirectly did not open the overlaid RomFS view");

    Require(Command(host, state, memory, context, romFsFile, {0x08040000}), "File:GetSize");
    uint64_t size = 0;
    Require(memory.Read32(kCommand, &response) && response == 0x080400C0 &&
                memory.Read64(kCommand + 8U, &size, nullptr) && size == kBaseViewSize + tail.size(),
            "File:GetSize must report the base view plus the overlay tail");

    const auto expected = Compose(view, patches, tail);
    constexpr uint32_t readAddress = 0x1FF82200;
    for (const auto [offset, count] : std::array<std::pair<uint32_t, uint32_t>, 4>{
             {{14U, 8U}, {60U, 8U}, {64U, 8U}, {0U, 72U}}}) {
        Require(memory.Fill(readAddress, count, 0xFF), "cannot clear the read buffer");
        Require(Command(host, state, memory, context, romFsFile,
                        {0x080200C2, offset, 0, count, (count << 4U) | 0xCU, readAddress}),
                "File:Read did not resume");
        std::vector<uint8_t> bytes(count);
        uint32_t returned = 0;
        Require(memory.Read32(kCommand, &response) && response == 0x08020082 &&
                    memory.Read32(kCommand + 8U, &returned) && returned == count &&
                    memory.ReadBytes(readAddress, bytes) && bytes == Slice(expected, offset, count),
                "File:Read did not return the composed RomFS view");
    }
}

} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path() / "oot3d_native_a32_romfs_overlay_tests";
    std::error_code cleanup;
    std::filesystem::remove_all(directory, cleanup);
    std::filesystem::create_directories(directory);
    TestComposedReads(directory);
    TestRejections(directory);
    TestHostServiceView(directory);
    std::filesystem::remove_all(directory, cleanup);
    std::cout << "oot3d_native_a32_romfs_overlay_tests: passed\n";
    return 0;
}
