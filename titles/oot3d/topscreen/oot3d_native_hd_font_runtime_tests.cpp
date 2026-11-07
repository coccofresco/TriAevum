#include "oot3d_native_hd_font_runtime.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <zip.h>

using namespace Oot3dNativeGame;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<std::uint8_t> Font(std::uint8_t size, std::uint8_t value) {
    std::vector<std::uint8_t> bytes(24 + size * size / 2, value);
    const std::uint8_t header[] = {'Q','B','F','1',1,0,1,0,42,0,0,0,4,size,size,2,42,0,0,0,0,0,0,0};
    std::copy(std::begin(header), std::end(header), bytes.begin());
    return bytes;
}
}
int main(int argc, char** argv) {
    const auto path = std::filesystem::temp_directory_path() / "triaevum-hd-font-test.zip";
    try {
        const auto native = Font(16, 0xff), hd = Font(64, 0x88);
        auto* zip = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
        Check(zip != nullptr, "create fixture");
        const std::string manifest = R"({"format":"oot3d_qbf_coverage_v1","fonts":[{"native_file":"native.qbf","coverage_file":"hd.qbf"}]})";
        const auto add = [&](const char* name, const void* data, std::size_t size) {
            auto* source = zip_source_buffer(zip, data, size, 0);
            Check(source && zip_file_add(zip, name, source, 0) >= 0, "fixture entry");
        };
        add("manifest.json", manifest.data(), manifest.size());
        add("native.qbf", native.data(), native.size());
        add("hd.qbf", hd.data(), hd.size());
        Check(zip_close(zip) == 0, "close fixture");
        NativeHdFontRuntime runtime;
        std::string error;
        Check(runtime.Load(path, &error), "load font pack");
        NativeA32Memory memory;
        Check(memory.MapRegion({"font-fixture", 0x1000, 0x20000, true, false, {}}, &error), "map fixture");
        Check(memory.WriteBytes(0x2000, native), "native QBF");
        Check(memory.Write32(0x1010, 0x1100) && memory.Write32(0x1104, 0x2000), "native font owner");
        const std::uint32_t args[] = {0x2000 + 24, 16, 16, 0, 0};
        Check(memory.WriteBytes(0x1200, std::span(reinterpret_cast<const std::uint8_t*>(args), sizeof(args))), "stack");
        oot3d::recomp::a32::GuestState state{};
        state.r[0] = 0x1000; state.r[1] = 0x4000; state.r[2] = 16; state.r[3] = 16; state.r[13] = 0x1200;
        runtime.ObserveBlit(state, memory);
        Check(runtime.Blits == 1, "native glyph provenance");
        std::vector<std::uint8_t> atlas(128, 0xff);
        Check(memory.WriteBytes(0x4000, atlas) && memory.WriteBytes(0x8000, atlas), "native atlas and upload copy");
        Oot3dPicaPhysicalMemoryView view(memory, {{0x100000, 0x1000, 0x20000}});
        Oot3dPicaTextureState texture{};
        texture.Enabled = true; texture.Width = texture.Height = 16; texture.Format = 11;
        texture.PhysicalAddress = 0x107000;
        const auto resolve = [&](std::uint32_t height) {
            Oot3dPicaResourceSnapshot snapshot;
            snapshot.Bytes = atlas;
            runtime.SetOutputHeight(height);
            runtime.Resolve(view, texture, snapshot);
            return snapshot;
        };
        Check(resolve(479).ReplacementWidth == 0, "below 480 remains native");
        const auto result = resolve(480);
        Check(result.ReplacementWidth == 64 && result.ReplacementHeight == 64 &&
              result.ResolvedBytes().size() == 2048 && result.ResolvedBytes()[0] == 0x88,
              "480 enables native-provenance HD copy");
        Check(texture.Width == 16 && atlas[0] == 0xff, "native state and memory unchanged");
        const auto second = resolve(1080);
        Check(second.SharedBytes == result.SharedBytes && runtime.Rebuilds == 1, "reuse immutable HD surface");
        Check(second.ContentHashAvailable && second.BaseLevelContentHashAvailable &&
              second.ContentHash == HashOot3dPicaSnapshot(second.ResolvedBytes()), "cached content hash");
        Check(resolve(240).ReplacementWidth == 0, "resolution downgrade disables immediately");
        atlas.assign(128, 0);
        Check(memory.WriteBytes(0x4000, atlas), "clear native atlas");
        Check(resolve(720).ReplacementWidth == 0, "clear cannot show old glyph");
        Check(result.ResolvedBytes()[0] == 0x88, "queued draw survives clear");
        runtime.Reset();
        atlas.assign(128, 0xff);
        Check(memory.WriteBytes(0x4000, atlas), "restore memory");
        Check(resolve(720).ReplacementWidth == 0, "quickload requires renewed provenance");
        // A static native atlas must not install an OoT blit observer.
        zip = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
        Check(zip != nullptr, "create static fixture");
        const std::string staticManifest = R"({"format":"triaevum_static_font_coverage_v1","sheets":[{"width":16,"height":16,"density":4,"native_file":"native.a4","coverage_file":"hd.a4"}]})";
        const std::vector<std::uint8_t> staticHd(2048,0x99);
        add("manifest.json",staticManifest.data(),staticManifest.size());
        add("native.a4",atlas.data(),atlas.size());add("hd.a4",staticHd.data(),staticHd.size());
        Check(zip_close(zip)==0 && runtime.Load(path,&error),"load static fixture");
        Check(!runtime.NeedsBlitObserver(),"MM static atlas borrowed OoT PC");
        Check(resolve(479).ReplacementWidth==0,"static threshold");
        Check(resolve(480).ResolvedBytes()[0]==0x99,"static replacement");
        runtime.Reset();Check(resolve(720).ReplacementWidth==64,"static source identity survives restore");
        atlas[0]^=1;Check(resolve(720).ReplacementWidth==0,"static changed bytes inherited stale coverage");atlas[0]^=1;
        texture.Format=10;Check(resolve(720).ReplacementWidth==0,"static wrong format accepted");texture.Format=11;
        texture.MaxMipLevel=1;Check(resolve(720).ReplacementWidth==0,"static mipped atlas accepted");texture.MaxMipLevel=0;
        if(argc==3) {
            Check(runtime.Load(argv[1],&error),error.c_str());
            Check(!runtime.NeedsBlitObserver(),"prepared MM pack requires OoT PC");
            std::ifstream source(argv[2],std::ios::binary);
            const std::vector<std::uint8_t> gzf((std::istreambuf_iterator<char>(source)),{});
            const auto half=[&](size_t at){Check(at+2<=gzf.size(),"GZFX extent");return unsigned(gzf[at])|(unsigned(gzf[at+1])<<8);};
            const auto word=[&](size_t at){return half(at)|(half(at+2)<<16);};
            const auto pages=word(16),stride=half(10),header=half(8);
            Check(pages==3,"unexpected EUR GZFX sheets");
            for(size_t i=0;i<pages;++i) {
                const auto at=header+i*stride;
                const auto offset=word(at),width=half(at+4),height=half(at+6);
                const size_t size=size_t(width)*height/2;Check(offset+size<=gzf.size(),"GZFX sheet bounds");
                atlas.assign(gzf.begin()+offset,gzf.begin()+offset+size);texture.Width=width;texture.Height=height;
                const auto enhanced=resolve(720);
                Check(enhanced.ReplacementWidth==width*4 && enhanced.ReplacementHeight==height*4,"prepared GZFX sheet not replaced");
                Check(enhanced.ContentHash==HashOot3dPicaSnapshot(enhanced.ResolvedBytes()),"prepared GZFX content identity");
                Check(resolve(240).ReplacementWidth==0,"native resolution must remain unchanged");
                atlas[0]^=1;Check(resolve(720).ReplacementWidth==0,"foreign GZFX sheet accepted");
            }
            std::cout<<"MM GZFX: three native sheets, HD threshold, byte guards, restore and format negatives passed\n";
        }
        std::filesystem::remove(path);
        std::cout << "HD font runtime threshold, upload copy and invalidation checks passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::filesystem::remove(path);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
