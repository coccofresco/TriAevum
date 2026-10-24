#include "fast/extension/graphics_settings_runtime.h"
#include "fast/extension/pbr_material_editor.h"
#include "fast/extension/texture_catalog_viewer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <stdexcept>

namespace Fast::Extension {
namespace {
using namespace ::Oot3d::Renderer;
constexpr std::array<const char *, 8> kImageNames{"Normal",    "Packed ORM", "Emissive",
                                                  "Height",    "Base color", "Occlusion",
                                                  "Roughness", "Metallic"};

std::string PathText(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}
std::filesystem::path PathFromText(const char *text) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t *>(text)));
}
std::string MaterialName(uint64_t hash) {
    char name[17];
    std::snprintf(name, sizeof(name), "%016llX", static_cast<unsigned long long>(hash));
    return name;
}
template <size_t N> void SetPath(std::array<char, N> &buffer, const std::filesystem::path &path) {
    const auto text = PathText(path);
    std::snprintf(buffer.data(), buffer.size(), "%s", text.c_str());
}

void SelectMaterial(PbrMaterialEditorState &state, uint64_t hash,
                    const AzaharTexturePackStatus &pack) {
    try {
        const auto found =
            std::find_if(state.Catalog.begin(), state.Catalog.end(),
                         [&](const auto &entry) { return entry.NativeHash == hash; });
        auto document = OpenMaterialDocument(
            pack.LoadDirectory, hash, found == state.Catalog.end() ? nullptr : &found->Definition);
        state.Document = std::move(document);
        for (size_t slot = 0; slot < state.ImagePaths.size(); ++slot)
            SetPath(state.ImagePaths[slot], state.Document->Images[slot].Path);
        state.OrmPreview.reset();
        state.Status.clear();
        AzaharTexturePackRuntime::Instance().SetMaterialPreview(
            AzaharMaterialParameterPreview{hash, state.Document->Parameters});
    } catch (const std::exception &exception) {
        state.Status = std::string("Cannot open material: ") + exception.what();
    }
}

void RequestSelection(PbrMaterialEditorState &state, uint64_t hash,
                      const AzaharTexturePackStatus &pack) {
    if (state.Document && state.Document->NativeHash == hash &&
        state.Document->Root == std::filesystem::weakly_canonical(pack.LoadDirectory))
        return;
    if (state.Document && (state.Document->Dirty || MaterialImagesPending(*state.Document)))
        state.PendingSelection = hash;
    else
        SelectMaterial(state, hash, pack);
}

bool Save(PbrMaterialEditorState &state, const AzaharTexturePackStatus &pack) {
    if (!state.Document)
        return false;
    auto &document = *state.Document;
    try {
        if (document.Root != std::filesystem::weakly_canonical(pack.LoadDirectory))
            throw std::runtime_error("The texture pack folder changed. Reload or discard this "
                                     "draft before editing the new pack.");
        if (pack.IndexPending)
            throw std::runtime_error("Wait for texture pack indexing before saving");
        if (!SaveMaterialDocument(document))
            return false;
        for (size_t slot = 0; slot < 5; ++slot)
            SetPath(state.ImagePaths[slot], document.Images[slot].Path);
        state.Status = "Material saved. Texture pack reload requested; the renderer keeps the last "
                       "valid bundle until publication completes.";
        return true;
    } catch (const std::exception &exception) {
        document.Error = exception.what();
        return false;
    }
}

void RefreshBrowser(PbrMaterialEditorState &state, const std::filesystem::path &directory) {
    state.BrowserEntries.clear();
    state.BrowserError.clear();
    SetPath(state.BrowserDirectory, directory);
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        const bool folder = it->is_directory(error);
        if (error)
            break;
        auto extension = PathText(it->path().extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (folder || extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
            extension == ".bmp" || extension == ".tga")
            state.BrowserEntries.push_back(*it);
    }
    if (error)
        state.BrowserError = error.message();
    std::sort(
        state.BrowserEntries.begin(), state.BrowserEntries.end(), [](const auto &a, const auto &b) {
            std::error_code first, second;
            const bool aDirectory = a.is_directory(first), bDirectory = b.is_directory(second);
            return aDirectory != bDirectory ? aDirectory
                                            : a.path().filename() < b.path().filename();
        });
}

void DrawBrowser(PbrMaterialEditorState &state) {
    if (state.BrowserSlot < 0)
        return;
    constexpr const char *title = "Choose material image";
    if (!ImGui::IsPopupOpen(title))
        ImGui::OpenPopup(title);
    ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("Import %s (PNG, JPEG, BMP or TGA)", kImageNames[state.BrowserSlot]);
        const bool enter =
            ImGui::InputText("Folder", state.BrowserDirectory.data(), state.BrowserDirectory.size(),
                             ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Open folder") || enter)
            RefreshBrowser(state, PathFromText(state.BrowserDirectory.data()));
        if (ImGui::Button("Parent folder"))
            RefreshBrowser(state, PathFromText(state.BrowserDirectory.data()).parent_path());
        if (!state.BrowserError.empty())
            ImGui::TextWrapped("Folder error: %s", state.BrowserError.c_str());
        std::optional<std::filesystem::path> chosen, directory;
        ImGui::BeginChild("##MaterialFiles", ImVec2(0, -40), true);
        for (const auto &entry : state.BrowserEntries) {
            std::error_code error;
            const bool folder = entry.is_directory(error);
            if (error)
                continue;
            const auto label = (folder ? "[Folder] " : "") + PathText(entry.path().filename());
            if (ImGui::Selectable(label.c_str())) {
                if (folder)
                    directory = entry.path();
                else
                    chosen = entry.path();
            }
        }
        ImGui::EndChild();
        if (directory)
            RefreshBrowser(state, *directory);
        if (chosen && state.Document) {
            SetPath(state.ImagePaths[state.BrowserSlot], *chosen);
            LoadMaterialEditorImage(*state.Document, state.BrowserSlot, *chosen);
            state.OrmPreview.reset();
            state.BrowserSlot = -1;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Cancel file selection")) {
            state.BrowserSlot = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DrawImage(PbrMaterialEditorState &state, size_t slot) {
    auto &document = *state.Document;
    auto &image = document.Images[slot];
    ImGui::PushID(kImageNames[slot]);
    const bool enter =
        ImGui::InputText("Image path", state.ImagePaths[slot].data(), state.ImagePaths[slot].size(),
                         ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("Browse image")) {
        state.BrowserSlot = static_cast<int>(slot);
        auto directory = PathFromText(state.ImagePaths[slot].data()).parent_path();
        if (directory.empty())
            directory = document.Root;
        RefreshBrowser(state, directory);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load image") || enter) {
        LoadMaterialEditorImage(document, slot, PathFromText(state.ImagePaths[slot].data()));
        state.OrmPreview.reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear image")) {
        image = {};
        state.ImagePaths[slot][0] = '\0';
        document.Dirty = true;
        state.OrmPreview.reset();
    }
    if (image.Loading.valid())
        ImGui::TextDisabled("Loading image on texture pack worker...");
    if (!image.Error.empty())
        ImGui::TextWrapped("Image error: %s", image.Error.c_str());
    if (image.Pixels) {
        ImGui::Text("%u x %u", image.Pixels->Width, image.Pixels->Height);
        DrawTexturePixelPreview(*image.Pixels->Rgba8, image.Pixels->Width, image.Pixels->Height);
    } else
        ImGui::TextDisabled(slot == 4
                                ? "Uses the bound native texture or its ordinary pack replacement."
                                : "No image assigned.");
    ImGui::PopID();
}

bool Scalar(const char *label, float &value, float minimum, float maximum,
            const char *format = "%.3f") {
    return ImGui::SliderFloat(label, &value, minimum, maximum, format,
                              ImGuiSliderFlags_AlwaysClamp);
}

void DrawParameters(PbrMaterialDocument &document) {
    auto &p = document.Parameters;
    bool changed = ImGui::ColorEdit3("Base color factor", p.BaseColorFactor.data());
    changed |= Scalar("Normal strength", p.NormalScale, 0, 8, "%.2f");
    int convention = p.NormalYSign < 0 ? 1 : 0;
    const char *const conventions[]{"+Y", "-Y"};
    if (ImGui::Combo("Normal convention", &convention, conventions, 2)) {
        p.NormalYSign = convention ? -1.0F : 1.0F;
        changed = true;
    }
    changed |= Scalar("Occlusion strength", p.OcclusionStrength, 0, 1);
    changed |= Scalar("Roughness factor", p.RoughnessFactor, 0, 1);
    changed |= Scalar("Metallic factor", p.MetallicFactor, 0, 1);
    changed |= Scalar("Glossiness strength", p.GlossinessStrength, 0, 2, "%.2fx");
    changed |= Scalar("Minimum roughness", p.MinimumRoughness, 0.045F, 1);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Limits only the smoothest areas. Shared by direct GGX lighting and "
                          "reflection guides.\n0.045 preserves the previous minimum.");
    changed |= Scalar("Dielectric reflectance", p.SpecularLevel, 0, 2, "%.2fx");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Non-metal base reflectance: 1x = 4%%, 2x = 8%%.\n0x removes the base, "
                          "not grazing Fresnel reflection. Metallic response is unchanged.");
    changed |= Scalar("Specular weight", p.SpecularWeight, 0, 1);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Attenuates the complete specular response, including grazing Fresnel and metals.\n"
                          "Zero removes direct highlights and screen-space reflections; diffuse and emission stay unchanged.");
    changed |= Scalar("Screen-space reflection weight", p.ScreenSpaceReflectionWeight, 0, 2, "%.2fx");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scales only this material's Hi-Z/SSSR contribution after filtering.\n"
                          "Zero leaves direct highlights intact. Multiplied by Specular weight.");
    changed |= Scalar("Height depth (UV)", p.HeightScale, 0, 0.1F, "%.4f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Authored parallax depth, not a scene multiplier. Zero disables relief.\nRequires a "
            "height map; geometry, collision and native alpha remain unchanged.");
    changed |= ImGui::DragFloat3("Emissive factor", p.EmissiveFactor.data(), 0.01F, 0, 65504,
                                 "%.3f", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::TreeNode("UV seam softening")) {
        changed |= ImGui::Checkbox("Enable UV seam softening", &p.SeamSoftening);
        changed |= Scalar("Seam strength", p.SeamStrength, 0, 1);
        float widthPercent = p.SeamWidth * 100.0F;
        if (Scalar("Seam width (% edge)", widthPercent, 0.1F, 25, "%.2f%%")) {
            p.SeamWidth = widthPercent * 0.01F;
            changed = true;
        }
        ImGui::TextWrapped("Blends auxiliary maps along connected native UV seams. Width is a "
                          "percentage of each seam edge, not screen pixels. Hard edges, open edges "
                          "and ambiguous connections remain separate. Requires typed affine UVs.");
        constexpr const char* channels[]{"Soften normals", "Soften occlusion", "Soften roughness",
                                         "Soften metallic", "Soften emission", "Soften height"};
        for (uint32_t i = 0; i < 6; ++i)
            changed |= ImGui::CheckboxFlags(channels[i], &p.SeamChannels, 1U << i);
        ImGui::TextWrapped("Base color is never blended. Height smoothing affects auxiliary-map "
                          "parallax only; base-color relief and native alpha keep their original sampling.");
        if (p.SeamSoftening) {
            const auto status = AzaharTexturePackRuntime::Instance().MaterialSeamStatus(document.NativeHash);
            ImGui::TextWrapped("Last native draw: %s", status.c_str());
        }
        ImGui::TreePop();
    }
    int slot = p.TextureSlot == 0xffU ? 0 : p.TextureSlot + 1;
    const char *const slots[]{"Automatic", "0", "1", "2"};
    if (ImGui::Combo("Native texture slot", &slot, slots, 4)) {
        p.TextureSlot = slot ? static_cast<uint8_t>(slot - 1) : 0xffU;
        changed = true;
    }
    int view = static_cast<int>(p.DebugView);
    const char *const views[]{"Shaded", "Surface normals", "Roughness"};
    if (ImGui::Combo("Material view", &view, views, 3))
        p.DebugView = static_cast<Renderer3ds::PicaPbrDebugView>(view);
    ImGui::TextDisabled("Material view is a selected-material preview, not saved in JSON.");
    if (ImGui::Button("Reset material parameters")) {
        p = {};
        if (document.SeparateOrm || !document.Images[1].Path.empty())
            p.MetallicFactor = 1.0F;
        if (!document.Images[2].Path.empty())
            p.EmissiveFactor.fill(1.0F);
        changed = true;
    }
    document.Dirty |= changed;
}

void DrawImages(PbrMaterialEditorState &state) {
    auto &document = *state.Document;
    ImGui::TextWrapped("Imports are copied into this pack as PNG files when saved. Map edits apply "
                       "after Save material and asynchronous reload. Numeric parameters preview "
                       "live on the selected existing material.");
    if (!ImGui::BeginTabBar("##MaterialImageSlots", ImGuiTabBarFlags_FittingPolicyScroll))
        return;
    for (size_t slot : {4U, 0U, 1U, 2U, 3U}) {
        if (!ImGui::BeginTabItem(slot == 1 ? "ORM" : kImageNames[slot]))
            continue;
        if (slot != 1)
            DrawImage(state, slot);
        else {
            ImGui::TextWrapped("Linear data: R = occlusion, G = roughness, B = metallic. No gamma "
                               "conversion. Separate images must have identical dimensions.");
            int mode = document.SeparateOrm ? 1 : 0;
            const char *const modes[]{"Packed ORM image", "Separate channels"};
            ImGui::BeginDisabled(MaterialImagesPending(document));
            if (ImGui::Combo("ORM input", &mode, modes, 2)) {
                document.SeparateOrm = mode != 0;
                document.Dirty = true;
                state.OrmPreview.reset();
                if (document.SeparateOrm && !document.OrmSplitInitialized) {
                    for (size_t channel = 0; channel < 3; ++channel) {
                        auto &input = document.Images[5 + channel];
                        input.Path = document.Images[1].Path;
                        input.Pixels = document.Images[1].Pixels;
                        document.OrmSourceChannels[channel] =
                            input.Pixels ? static_cast<int>(channel) : 0;
                        SetPath(state.ImagePaths[5 + channel], input.Path);
                    }
                    if (document.Images[1].Path.empty())
                        document.Parameters.MetallicFactor = 1.0F;
                    document.OrmSplitInitialized = true;
                }
            }
            ImGui::EndDisabled();
            if (!document.SeparateOrm)
                DrawImage(state, 1);
            else {
                if (ImGui::BeginTabBar("##OrmChannels")) {
                    for (size_t channel = 0; channel < 3; ++channel) {
                        if (!ImGui::BeginTabItem(kImageNames[5 + channel]))
                            continue;
                        const char *const channels[]{"R", "G", "B", "A"};
                        bool changed = ImGui::Combo(
                            "Source channel", &document.OrmSourceChannels[channel], channels, 4);
                        changed |=
                            Scalar("Constant without image", document.OrmConstants[channel], 0, 1);
                        if (changed) {
                            document.Dirty = true;
                            state.OrmPreview.reset();
                        }
                        DrawImage(state, 5 + channel);
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::BeginDisabled(MaterialImagesPending(document));
                if (ImGui::Button("Preview packed ORM")) {
                    try {
                        state.OrmPreview = PackMaterialOrm(document);
                        document.Error.clear();
                    } catch (const std::exception &exception) {
                        document.Error = exception.what();
                    }
                }
                ImGui::EndDisabled();
                if (state.OrmPreview)
                    DrawTexturePixelPreview(*state.OrmPreview->Rgba8, state.OrmPreview->Width,
                                            state.OrmPreview->Height);
                ImGui::TextWrapped("Save material exports one packed ORM PNG and references it "
                                   "from the descriptor.");
            }
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
} // namespace

void DrawPbrMaterialEditor(PbrMaterialEditorState &state) {
    auto &runtime = AzaharTexturePackRuntime::Instance();
    const auto pack = runtime.Snapshot();
    if (state.CatalogGeneration != pack.Generation) {
        state.Catalog = runtime.MaterialCatalog();
        state.CatalogGeneration = pack.Generation;
    }
    ImGui::TextWrapped("Pack: %s", PathText(pack.LoadDirectory).c_str());
    ImGui::TextDisabled("Pack folder and loading are owned by Materials > Texture packs.");
    if (pack.IndexPending)
        ImGui::TextDisabled("Indexing material descriptors...");
    if (!pack.LastError.empty())
        ImGui::TextWrapped("Pack error: %s", pack.LastError.c_str());
    const auto graphics = GraphicsSettingsRuntime::Instance().SnapshotForRendering();
    if (!pack.LoadCustomTextures || !UsesPbrMaterials(graphics.Value.Effects.SurfaceLighting))
        ImGui::TextWrapped(
            "Editing remains available. To render these materials, enable Load custom textures and "
            "a PBR lighting model. Native/F2 bypasses material previews.");
    const auto selectedName = state.Document ? MaterialName(state.Document->NativeHash)
                                             : std::string("Select a PBR material");
    if (ImGui::BeginCombo("Existing PBR material", selectedName.c_str())) {
        for (const auto &entry : state.Catalog) {
            const auto label = MaterialName(entry.NativeHash);
            if (ImGui::Selectable(label.c_str(),
                                  state.Document && entry.NativeHash == state.Document->NativeHash))
                RequestSelection(state, entry.NativeHash, pack);
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Refresh material list"))
        runtime.Reload();
    if (state.Document) {
        PollMaterialEditorImages(*state.Document);
        ImGui::SameLine();
        ImGui::BeginDisabled(MaterialImagesPending(*state.Document));
        if (ImGui::Button("Save material"))
            Save(state, pack);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Discard changes / reload"))
            SelectMaterial(state, state.Document->NativeHash, pack);
        ImGui::TextWrapped("JSON: %s", PathText(state.Document->Descriptor).c_str());
        if (state.Document->Dirty)
            ImGui::TextWrapped("Unsaved material changes. Save to keep them; selecting another "
                               "material requires confirmation.");
        if (state.Document->NewMaterial)
            ImGui::TextDisabled("New material: save once to publish its first renderable bundle.");
        if (!state.Document->Error.empty())
            ImGui::TextWrapped("Material error: %s", state.Document->Error.c_str());
    }
    if (!state.Status.empty())
        ImGui::TextWrapped("%s", state.Status.c_str());
    if (ImGui::BeginTabBar("##MaterialEditorSections", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Material")) {
            if (state.Document)
                DrawParameters(*state.Document);
            else
                ImGui::TextWrapped("Select an existing material, or create one from a texture in "
                                   "Create from texture.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Images")) {
            if (state.Document)
                DrawImages(state);
            else
                ImGui::TextDisabled("Select a material first.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Create from texture")) {
            const auto textures = TextureCatalogRuntime::Instance().Snapshot();
            std::vector<TextureCatalogTag> tags;
            for (const auto &texture : textures) {
                if (!texture.MaterialHashAvailable ||
                    texture.MaterialUsesNewHash != pack.PackUsesNewHash)
                    continue;
                if (std::any_of(state.Catalog.begin(), state.Catalog.end(), [&](const auto &entry) {
                        return entry.NativeHash == texture.MaterialHash;
                    }))
                    tags.push_back({texture.ContentHash, texture.Width, texture.Height, "PBR"});
            }
            const auto catalog = DrawTextureCatalogViewer(state.TextureSelection, tags);
            const bool ready = catalog.Selected && catalog.Selected->MaterialHashAvailable &&
                               catalog.Selected->MaterialUsesNewHash == pack.PackUsesNewHash &&
                               !pack.IndexPending && !pack.LoadDirectory.empty();
            ImGui::BeginDisabled(!ready);
            if (ImGui::Button("Create / edit selected texture"))
                RequestSelection(state, catalog.Selected->MaterialHash, pack);
            ImGui::EndDisabled();
            if (ready)
                ImGui::Text("Azahar material hash: %s",
                            MaterialName(catalog.Selected->MaterialHash).c_str());
            else
                ImGui::TextWrapped("Waiting for the native texture's pack identity. Render the "
                                   "texture after the current pack has finished indexing.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (state.Document) {
        const auto invalid = ValidateMaterialParameters(state.Document->Parameters);
        if (!invalid.empty())
            ImGui::TextWrapped("Parameter error: %s", invalid.c_str());
        else if (state.Document->Root == std::filesystem::weakly_canonical(pack.LoadDirectory))
            runtime.SetMaterialPreview(AzaharMaterialParameterPreview{state.Document->NativeHash,
                                                                      state.Document->Parameters});
    }
    if (state.PendingSelection) {
        constexpr const char *title = "Unsaved material changes";
        if (!ImGui::IsPopupOpen(title))
            ImGui::OpenPopup(title);
        if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Save or discard this material before selecting another one.");
            if (state.Document && !state.Document->Error.empty())
                ImGui::TextWrapped("%s", state.Document->Error.c_str());
            bool switchMaterial = false;
            if (ImGui::Button("Save and select"))
                switchMaterial = Save(state, pack);
            ImGui::SameLine();
            if (ImGui::Button("Discard and select"))
                switchMaterial = true;
            ImGui::SameLine();
            if (ImGui::Button("Keep editing")) {
                state.PendingSelection.reset();
                ImGui::CloseCurrentPopup();
            }
            if (switchMaterial) {
                const auto hash = *state.PendingSelection;
                state.PendingSelection.reset();
                SelectMaterial(state, hash, pack);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    DrawBrowser(state);
}

} // namespace Fast::Extension
