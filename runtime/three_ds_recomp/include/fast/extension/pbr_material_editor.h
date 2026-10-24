#pragma once

#include "fast/extension/texture_catalog_selection.h"
#include "oot3d/renderer/azahar_texture_pack.h"

#include <array>
#include <future>
#include <optional>

namespace Fast::Extension {

// Normal, packed ORM, emissive, height, base color, then the three ORM inputs.
inline constexpr size_t kMaterialEditorImageCount = 8;
struct MaterialEditorImage {
    std::filesystem::path Path;
    std::shared_ptr<const ::Oot3d::Renderer::AzaharTextureReplacement> Pixels;
    std::future<::Oot3d::Renderer::AzaharImageLoadResult> Loading;
    bool Imported = false;
    bool PendingImport = false;
    std::string Error;
};

struct PbrMaterialDocument {
    std::filesystem::path Root;
    std::filesystem::path Descriptor;
    uint64_t NativeHash = 0;
    Renderer3ds::PicaPbrMaterialParameters Parameters;
    std::array<MaterialEditorImage, kMaterialEditorImageCount> Images;
    std::array<int, 3> OrmSourceChannels{0, 0, 0};
    std::array<float, 3> OrmConstants{1.0F, 1.0F, 0.0F};
    bool SeparateOrm = false;
    bool Dirty = false;
    bool OrmSplitInitialized = false;
    bool NewMaterial = false;
    std::string Error;
};

[[nodiscard]] std::string
ValidateMaterialParameters(const Renderer3ds::PicaPbrMaterialParameters &parameters);
[[nodiscard]] PbrMaterialDocument
OpenMaterialDocument(const std::filesystem::path &root, uint64_t hash,
                     const ::Oot3d::Renderer::IndexedAzaharPbrMaterial *existing = nullptr);
void LoadMaterialEditorImage(PbrMaterialDocument &document, size_t slot,
                             const std::filesystem::path &path);
void PollMaterialEditorImages(PbrMaterialDocument &document);
[[nodiscard]] bool MaterialImagesPending(const PbrMaterialDocument &document);
[[nodiscard]] std::shared_ptr<const ::Oot3d::Renderer::AzaharTextureReplacement>
PackMaterialOrm(const PbrMaterialDocument &document);
// Imports are immutable, content-addressed PNGs. The descriptor is the atomic commit point.
[[nodiscard]] bool SaveMaterialDocument(PbrMaterialDocument &document);

struct PbrMaterialEditorState {
    TextureCatalogSelectionState TextureSelection;
    std::vector<::Oot3d::Renderer::AzaharMaterialCatalogEntry> Catalog;
    uint64_t CatalogGeneration = 0;
    std::optional<PbrMaterialDocument> Document;
    std::optional<uint64_t> PendingSelection;
    std::array<std::array<char, 4096>, kMaterialEditorImageCount> ImagePaths{};
    std::shared_ptr<const ::Oot3d::Renderer::AzaharTextureReplacement> OrmPreview;
    std::array<char, 4096> BrowserDirectory{};
    std::vector<std::filesystem::directory_entry> BrowserEntries;
    int BrowserSlot = -1;
    std::string BrowserError;
    std::string Status;
};

void DrawPbrMaterialEditor(PbrMaterialEditorState &state);

} // namespace Fast::Extension
