// Vulkan backend: shader programs, SPIR-V compilation, native PICA AOT and uber programs and
// shader modules.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_internal.h"
#include "fast/oot3d/pica_builtin_fragment_families.h"

namespace Fast {

VulkanShaderProgram GfxRenderingAPIVulkan::BuildShaderProgram(uint64_t shaderId0,
                                                              uint64_t shaderId1) {
    CCFeatures features{};
    gfx_cc_get_features(shaderId0, shaderId1, &features);

    VulkanShaderProgram shader{};
    shader.ShaderId0 = shaderId0;
    shader.ShaderId1 = shaderId1;
    shader.NumInputs = static_cast<uint8_t>(features.numInputs);
    shader.UsedTextures[0] = features.usedTextures[0];
    shader.UsedTextures[1] = features.usedTextures[1];
    shader.UsesTexture2 = IsOot3dPicaTexture2Shader(features.shader_id);
    shader.HasAlpha = features.opt_alpha;
    shader.HasFog = features.opt_fog;
    shader.HasGrayscale = features.opt_grayscale;
    shader.NativeShaderId = features.shader_id;

    uint32_t floatOffset = 4;
    for (size_t textureIndex = 0; textureIndex < 2; ++textureIndex) {
        if (!features.usedTextures[textureIndex]) {
            continue;
        }
        shader.TextureCoordinateOffsets[textureIndex] = floatOffset * sizeof(float);
        floatOffset += 2;
        for (size_t axis = 0; axis < 2; ++axis) {
            if (features.clamp[textureIndex][axis]) {
                ++floatOffset;
            }
        }
    }
    if (shader.UsesTexture2) {
        shader.TextureCoordinateOffsets[2] = floatOffset * sizeof(float);
        floatOffset += 2;
    }
    if (features.opt_fog) {
        shader.FogOffset = floatOffset * sizeof(float);
        floatOffset += 4;
    }
    if (features.opt_grayscale) {
        shader.GrayscaleOffset = floatOffset * sizeof(float);
        floatOffset += 4;
    }
    const uint32_t inputComponentCount = features.opt_alpha ? 4 : 3;
    for (int inputIndex = 0; inputIndex < features.numInputs; ++inputIndex) {
        if (static_cast<size_t>(inputIndex) < shader.InputOffsets.size()) {
            shader.InputOffsets[static_cast<size_t>(inputIndex)] = floatOffset * sizeof(float);
        }
        floatOffset += inputComponentCount;
    }
    if (IsOot3dShadow2dShader(features.shader_id)) {
        shader.ShadowCoordinateOffset = floatOffset * sizeof(float);
        floatOffset += 3;
    }
    shader.VertexStride = floatOffset * sizeof(float);
    return shader;
}

std::string GfxRenderingAPIVulkan::BuildVertexShaderSource(const VulkanShaderProgram& shader) const {
    CCFeatures features{};
    gfx_cc_get_features(shader.ShaderId0, shader.ShaderId1, &features);
    return Renderer::BuildCompatibilityVertexShader(features);
}

std::string GfxRenderingAPIVulkan::BuildFragmentShaderSource(uint64_t shaderId0, uint64_t shaderId1) const {
    CCFeatures features{};
    gfx_cc_get_features(shaderId0, shaderId1, &features);
    return Renderer::BuildCompatibilityFragmentShader(features, mSrgbMode);
}

std::vector<uint32_t> GfxRenderingAPIVulkan::CompileShaderSpirv(
    const std::string& source, bool vertexShader, const char* sourceName) {
    const auto stage = vertexShader ? Renderer::SpirvStage::Vertex : Renderer::SpirvStage::Fragment;
    if (const auto artifact = Renderer::FindBuiltinPassShaderSpirv(source, stage, {}, 1); !artifact.empty()) {
        if (mPicaEffectiveShaderInventory.Enabled())
            std::fprintf(stderr, "TRIAEVUM_COMPAT_SHADER_ARTIFACT name=%s\n", sourceName);
        return {artifact.begin(), artifact.end()};
    }
    const auto failures = mCompiledShaderCache.Stats().WriteFailures;
    auto spirv = mCompiledShaderCache.Resolve(source, stage, [&] {
        if (mPicaEffectiveShaderInventory.Enabled())
            std::fprintf(stderr, "TRIAEVUM_RUNTIME_SHADER_COMPILE stage=%s name=%s\n",
                         vertexShader ? "vertex" : "fragment", sourceName);
        return Renderer::CompileShadercSpirv(source, stage, sourceName);
    });
    if (failures == 0 && mCompiledShaderCache.Stats().WriteFailures != 0)
        SPDLOG_WARN("Shader cache persistence unavailable; rendering continues: {}",
                    mCompiledShaderCache.LastWriteError());
    return spirv;
}

void GfxRenderingAPIVulkan::ConfigureNativePicaAotShaders() {
    mCompiledShaderCache.Configure(VulkanShaderCacheDirectory(), Renderer::ShadercCompilerContract());
    mNriInterop.Shaders().Configure(VulkanShaderCacheDirectory());
    // Alpha shadow casters of the parametric bases (built-in, finite).
    mNriInterop.Shaders().AddArtifacts(Oot3d::CasterFragmentArtifacts());
    // Alpha shadow casters of specialized programs are title-derived: the
    // corpus carries them as NRI fragments (variant replay, issue #76).
    mNriInterop.Shaders().SetTitleArtifacts(
        [this](std::string_view source, Renderer::SpirvStage stage) -> std::span<const uint32_t> {
            if (stage != Renderer::SpirvStage::Fragment || !mPicaAotShaderPack.Loaded()) return {};
            const auto binary = mPicaAotShaderPack.Find(Oot3d::PicaAotShaderStage::NriFragment, source);
            if (!binary.empty()) {
                ++mPicaAotShaderHits;
                std::lock_guard lock(mPicaAotShaderLogMutex);
                mPicaAotShaderHitIdentities.insert(
                    {Oot3d::PicaAotShaderStage::NriFragment, Oot3d::IdentifyPicaAotShaderSource(source).Id});
            }
            return binary;
        });
    mCompiledShaderCacheSummaryLogged = false;
    if (!mCompiledShaderCache.Enabled())
        SPDLOG_WARN("SPIR-V disk reuse disabled: cache directory or compiler identity unavailable");
    mPicaAotShaderPack.Clear();
    mPicaEffectiveShaderInventory.Clear();
    mPicaPipelineInventory.Clear();
    mPicaPipelineManifest.Clear();
    mPicaPipelinePrewarmedProfiles.clear();
    JoinStartupPreparation();
    if (mPreparationQueue) mPreparationQueue->Drain();
    mPicaAotShaderMissesLogged.clear();
    mPicaAotShaderHitIdentities.clear();
    mPicaAotShaderHits = 0;
    mNativeFragmentArtifactHits = 0;
    mNativeVertexArtifactHits = 0;
    mPicaAotShaderMisses = 0;
    mPicaAotShaderSummaryLogged = false;
    mPicaPipelinePrewarmSummaryLogged = false;
    mPicaPipelinePrewarmCreated = 0U;
    mPicaPipelinePrewarmReused = 0U;
    mPicaPipelinePrewarmSkipped = 0U;
    const char* strict = std::getenv("OOT3D_PICA_AOT_SHADER_STRICT");
    mPicaAotShaderStrict =
        strict != nullptr && std::string_view(strict) == "1";
    const char* prewarm = std::getenv("OOT3D_PICA_PIPELINE_PREWARM");
    mPicaPipelinePrewarmEnabled =
        prewarm != nullptr && std::string_view(prewarm) == "1";

    if (const char* path = std::getenv("OOT3D_PICA_AOT_SHADER_PACK");
        path != nullptr && path[0] != '\0') {
        std::string error;
        if (!mPicaAotShaderPack.Load(path, &error)) {
            throw std::runtime_error(
                "could not load native PICA AOT shader pack: " + error);
        }
        SPDLOG_INFO(
            "Native PICA AOT shader pack loaded: {} entries, schema {}, {}",
            mPicaAotShaderPack.EntryCount(),
            mPicaAotShaderPack.DescriptorSchemaVersion(),
            mPicaAotShaderPack.Path().string());
    } else if (mPicaAotShaderStrict) {
        throw std::runtime_error(
            "OOT3D_PICA_AOT_SHADER_STRICT requires "
            "OOT3D_PICA_AOT_SHADER_PACK");
    }

    if (const char* path =
            std::getenv("OOT3D_PICA_EFFECTIVE_SHADER_INVENTORY");
        path != nullptr && path[0] != '\0') {
        std::string error;
        if (!mPicaEffectiveShaderInventory.Configure(path, &error)) {
            throw std::runtime_error(
                "could not configure native PICA shader inventory: " +
                error);
        }
        SPDLOG_INFO("Native PICA effective shader inventory enabled: {}",
                    mPicaEffectiveShaderInventory.Path().string());
    }
    if (const char* path = std::getenv("OOT3D_PICA_VARIANT_REQUESTS"); path != nullptr && path[0] != '\0') {
        std::string error;
        if (!mPicaVariantRequestRecorder.Configure(path, &error))
            throw std::runtime_error("could not configure native PICA variant requests: " + error);
        SPDLOG_INFO("Native PICA variant request recording enabled: {}", path);
    }
    if (const char* path =
            std::getenv("OOT3D_PICA_PIPELINE_INVENTORY");
        path != nullptr && path[0] != '\0') {
        std::string error;
        if (!mPicaPipelineInventory.Configure(path, &error)) {
            throw std::runtime_error(
                "could not configure native PICA pipeline inventory: " +
                error);
        }
        SPDLOG_INFO("Native PICA pipeline inventory enabled: {}",
                    mPicaPipelineInventory.Path().string());
    }
    if (const char* path =
            std::getenv("OOT3D_PICA_PIPELINE_MANIFEST");
        path != nullptr && path[0] != '\0') {
        std::string error;
        if (!mPicaPipelineManifest.Load(path, &error)) {
            throw std::runtime_error(
                "could not load native PICA pipeline manifest: " + error);
        }
        if (mPicaAotShaderPack.Loaded() &&
            mPicaPipelineManifest.DescriptorSchemaVersion() !=
                mPicaAotShaderPack.DescriptorSchemaVersion()) {
            throw std::runtime_error(
                "native PICA pipeline manifest and AOT shader pack use "
                "different descriptor schemas");
        }
        SPDLOG_INFO(
            "Native PICA pipeline manifest loaded: {} entries, schema {}, {}",
            mPicaPipelineManifest.Entries().size(),
            mPicaPipelineManifest.DescriptorSchemaVersion(),
            mPicaPipelineManifest.Path().string());
    }
    if (mPicaPipelinePrewarmEnabled &&
        (!mPicaPipelineManifest.Loaded() ||
         !mPicaAotShaderPack.Loaded())) {
        throw std::runtime_error(
            "OOT3D_PICA_PIPELINE_PREWARM requires both a pipeline manifest "
            "and an AOT shader pack");
    }
}

void GfxRenderingAPIVulkan::FinishNativePicaAotShaders() {
    const auto& cache = mCompiledShaderCache.Stats();
    const auto passes = mNriInterop.Shaders().Stats();
    if ((cache.Requests || passes.Requests || mNriInterop.Shaders().BuiltinArtifactHits() ||
         mPicaAotShaderPack.Loaded()) && !mCompiledShaderCacheSummaryLogged) {
        std::fprintf(stderr, "TRIAEVUM_BUILTIN_PASS_ARTIFACTS hits=%llu title_hits=%llu\n",
                     static_cast<unsigned long long>(mNriInterop.Shaders().BuiltinArtifactHits()),
                     static_cast<unsigned long long>(mNriInterop.Shaders().TitleArtifactHits()));
        std::fprintf(stderr,
            "TRIAEVUM_PASS_SHADER_CACHE requests=%llu hits=%llu compiled=%llu compile_failed=%llu "
            "writes=%llu write_failed=%llu compile_ms=%.3f enabled=%d\n",
            static_cast<unsigned long long>(passes.Requests), static_cast<unsigned long long>(passes.Hits),
            static_cast<unsigned long long>(passes.Compilations), static_cast<unsigned long long>(passes.CompilationFailures),
            static_cast<unsigned long long>(passes.Writes), static_cast<unsigned long long>(passes.WriteFailures),
            passes.CompileNanoseconds / 1e6, mNriInterop.Shaders().Enabled() ? 1 : 0);
        std::fprintf(stderr,
            "TRIAEVUM_SPIRV_CACHE requests=%llu hits=%llu misses=%llu rejected=%llu "
            "compiled=%llu compile_failed=%llu writes=%llu write_failed=%llu "
            "compile_ms=%.3f read_ms=%.3f write_ms=%.3f enabled=%d\n",
            static_cast<unsigned long long>(cache.Requests), static_cast<unsigned long long>(cache.Hits),
            static_cast<unsigned long long>(cache.Misses), static_cast<unsigned long long>(cache.Rejected),
            static_cast<unsigned long long>(cache.Compilations), static_cast<unsigned long long>(cache.CompilationFailures),
            static_cast<unsigned long long>(cache.Writes), static_cast<unsigned long long>(cache.WriteFailures),
            cache.CompileNanoseconds / 1e6, cache.ReadNanoseconds / 1e6, cache.WriteNanoseconds / 1e6,
            mCompiledShaderCache.Enabled() ? 1 : 0);
        const auto audit = Fast::Renderer::ShadercCompilationAudit();
        std::fprintf(stderr, "TRIAEVUM_SHADERC_AUDIT calls=%llu compile_ms=%.3f\n",
                     static_cast<unsigned long long>(audit.Calls), audit.Nanoseconds / 1e6);
        mCompiledShaderCacheSummaryLogged = true;
    }
    if (mPicaEffectiveShaderInventory.Enabled()) {
        std::string error;
        if (!mPicaEffectiveShaderInventory.Finish(&error)) {
            SPDLOG_WARN("Native PICA shader inventory was not written: {}",
                        error);
        } else {
            SPDLOG_INFO(
                "Native PICA effective shader inventory wrote {} modules to {}",
                mPicaEffectiveShaderInventory.EntryCount(),
                mPicaEffectiveShaderInventory.Path().string());
            std::fprintf(
                stderr,
                "OOT3D_PICA_EFFECTIVE_SHADER_INVENTORY modules=%zu path=%s\n",
                mPicaEffectiveShaderInventory.EntryCount(),
                mPicaEffectiveShaderInventory.Path().string().c_str());
        }
    }
    if (mPicaPipelineInventory.Enabled()) {
        std::string error;
        if (!mPicaPipelineInventory.Finish(&error)) {
            SPDLOG_WARN("Native PICA pipeline inventory was not written: {}",
                        error);
        } else {
            SPDLOG_INFO(
                "Native PICA pipeline inventory wrote {} pipelines to {}",
                mPicaPipelineInventory.EntryCount(),
                mPicaPipelineInventory.Path().string());
            std::fprintf(
                stderr,
                "OOT3D_PICA_PIPELINE_INVENTORY pipelines=%zu path=%s\n",
                mPicaPipelineInventory.EntryCount(),
                mPicaPipelineInventory.Path().string().c_str());
        }
    }
    if (mPicaPipelinePrewarmEnabled &&
        !mPicaPipelinePrewarmSummaryLogged) {
        SPDLOG_INFO(
            "Native PICA pipeline prewarm: {} created, {} reused, {} skipped",
            mPicaPipelinePrewarmCreated, mPicaPipelinePrewarmReused,
            mPicaPipelinePrewarmSkipped);
        std::fprintf(
            stderr,
            "OOT3D_PICA_PIPELINE_PREWARM created=%llu reused=%llu "
            "skipped=%llu profiles=%zu\n",
            static_cast<unsigned long long>(mPicaPipelinePrewarmCreated),
            static_cast<unsigned long long>(mPicaPipelinePrewarmReused),
            static_cast<unsigned long long>(mPicaPipelinePrewarmSkipped),
            mPicaPipelinePrewarmedProfiles.size());
        mPicaPipelinePrewarmSummaryLogged = true;
    }
    if (!mPicaAotShaderSummaryLogged &&
        (mNativeVertexArtifactHits != 0U || mNativeFragmentArtifactHits != 0U || mPicaAotShaderPack.Loaded() || mPicaAotShaderHits != 0U ||
         mPicaAotShaderMisses != 0U)) {
        SPDLOG_INFO("Native PICA AOT shader resolution: {} hits, {} misses",
                    mPicaAotShaderHits.load(), mPicaAotShaderMisses.load());
        size_t uniqueHits = 0, uniqueMisses = 0;
        {
            std::lock_guard lock(mPicaAotShaderLogMutex);
            uniqueHits = mPicaAotShaderHitIdentities.size();
            uniqueMisses = mPicaAotShaderMissesLogged.size();
        }
        std::fprintf(stderr,
                     "OOT3D_PICA_AOT_SHADER_RESOLUTION hits=%llu misses=%llu "
                     "strict=%u entries=%zu unique_hits=%zu unique_misses=%zu\n",
                     static_cast<unsigned long long>(mPicaAotShaderHits),
                     static_cast<unsigned long long>(mPicaAotShaderMisses),
                     mPicaAotShaderStrict ? 1U : 0U,
                     mPicaAotShaderPack.EntryCount(), uniqueHits, uniqueMisses);
        mPicaAotShaderSummaryLogged = true;
        std::fprintf(stderr, "TRIAEVUM_NATIVE_FRAGMENT_ARTIFACTS hits=%llu modules=%zu\n",
                     static_cast<unsigned long long>(mNativeFragmentArtifactHits),
                     Renderer3ds::NativeFragmentArtifacts().size() +
                         Oot3d::TemporalFragmentArtifacts().size() +
                         Oot3d::ToonFragmentArtifacts().size());
        std::fprintf(stderr, "TRIAEVUM_NATIVE_VERTEX_ARTIFACTS hits=%llu\n",
                     static_cast<unsigned long long>(mNativeVertexArtifactHits));
        std::fprintf(stderr, "TRIAEVUM_NATIVE_PROGRAM_OWNERS canonical=%zu instrumented=%zu uber=%zu\n",
                     mCanonicalNativePicaShaders.size(), mInstrumentedNativePicaShaders.size(),
                     mUberNativePicaShaders.size());
    }
    if (mPreparationQueue && !mNativePicaPreparationSummaryLogged) {
        mNativePicaPreparationSummaryLogged = true;
        const auto& counters = mNativePicaPreparationCounters;
        const auto bridge = mNriPicaPipelineBridge.PipelineStatistics();
        std::fprintf(stderr,
            "TRIAEVUM_HYBRID_PREPARATION specialized_draws=%llu uber_draws=%llu completed_draws=%llu "
            "complete_ms=%.3f max_complete_ms=%.3f program_tasks=%llu uber_programs=%llu "
            "uber_unavailable=%llu/%llu/%llu/%llu/%llu/%llu pipeline_tasks=%llu pipeline_created=%llu "
            "pipeline_completions=%llu pending=%zu uber_acquire_ms=%.3f max_uber_acquire_ms=%.3f "
            "uber_effects=surface:%llu,pbr:%llu,relief:%llu,toon:%llu\n",
            static_cast<unsigned long long>(counters.SpecializedDraws),
            static_cast<unsigned long long>(counters.UberDraws),
            static_cast<unsigned long long>(counters.CompletedDraws),
            counters.CompleteMilliseconds, counters.MaximumCompleteMilliseconds,
            static_cast<unsigned long long>(counters.ProgramTasks),
            static_cast<unsigned long long>(counters.UberPrograms),
            static_cast<unsigned long long>(counters.UberUnavailable[0]),
            static_cast<unsigned long long>(counters.UberUnavailable[1]),
            static_cast<unsigned long long>(counters.UberUnavailable[2]),
            static_cast<unsigned long long>(counters.UberUnavailable[3]),
            static_cast<unsigned long long>(counters.UberUnavailable[4]),
            static_cast<unsigned long long>(counters.UberUnavailable[5]),
            static_cast<unsigned long long>(bridge.BackgroundRequests),
            static_cast<unsigned long long>(bridge.BackgroundCreated),
            static_cast<unsigned long long>(bridge.BackgroundCompletions),
            mPreparationQueue->Pending(), counters.UberAcquireMilliseconds,
            counters.MaximumUberAcquireMilliseconds,
            static_cast<unsigned long long>(counters.UberSurfaceDraws),
            static_cast<unsigned long long>(counters.UberPbrDraws),
            static_cast<unsigned long long>(counters.UberReliefDraws),
            static_cast<unsigned long long>(counters.UberToonDraws));
    }
}

std::vector<uint32_t>
GfxRenderingAPIVulkan::ResolveNativePicaShaderSpirv(
    std::string_view source, Oot3d::PicaAotShaderStage stage,
    bool vertexShader, const char* sourceName) {
    if (!vertexShader && (stage == Oot3d::PicaAotShaderStage::Fragment ||
                          stage == Oot3d::PicaAotShaderStage::NriFragment)) {
        // The same order device preparation resolves (Forge's prepared pipelines).
        const auto artifact = Oot3d::FindPicaBuiltInFragmentSpirv(
            Renderer3ds::IdentifyPicaShaderSource(source), stage == Oot3d::PicaAotShaderStage::NriFragment);
        if (!artifact.empty()) {
            ++mNativeFragmentArtifactHits;
            return {artifact.begin(), artifact.end()};
        }
        const auto identity = Renderer3ds::IdentifyPicaShaderSource(source);
        const auto stageName = Oot3d::PicaAotShaderStageName(stage);
        std::fprintf(stderr, "TRIAEVUM_FRAGMENT_ARTIFACT_MISS stage=%.*s source=%llu bytes=%zu\n",
                     static_cast<int>(stageName.size()), stageName.data(),
                     static_cast<unsigned long long>(identity.Id), source.size());
    }
    if (!mPicaAotShaderPack.Loaded()) {
        return CompileShaderSpirv(std::string(source), vertexShader,
                                  sourceName);
    }
    const auto binary = mPicaAotShaderPack.Find(stage, source);
    if (!binary.empty()) {
        ++mPicaAotShaderHits;
        {
            std::lock_guard lock(mPicaAotShaderLogMutex);
            mPicaAotShaderHitIdentities.insert({stage, Oot3d::IdentifyPicaAotShaderSource(source).Id});
        }
        return {binary.begin(), binary.end()};
    }

    ++mPicaAotShaderMisses;
    const auto identity = Oot3d::IdentifyPicaAotShaderSource(source);
    bool firstMiss = false;
    {
        std::lock_guard lock(mPicaAotShaderLogMutex);
        firstMiss = mPicaAotShaderMissesLogged.insert({stage, identity.Id}).second;
    }
    if (firstMiss) {
        SPDLOG_WARN(
            "Native PICA AOT shader miss: stage={}, source={}, name={}",
            Oot3d::PicaAotShaderStageName(stage),
            Oot3d::FormatPicaAotShaderId(identity.Id), sourceName);
    }
    if (mPicaAotShaderStrict) {
        throw std::runtime_error(
            "native PICA AOT shader pack has no " +
            std::string(Oot3d::PicaAotShaderStageName(stage)) +
            " module for " + Oot3d::FormatPicaAotShaderId(identity.Id));
    }
    return CompileShaderSpirv(std::string(source), vertexShader,
                              sourceName);
}

GfxRenderingAPIVulkan::NativePicaShaderProgram* GfxRenderingAPIVulkan::ResolveUberNativePicaProgram(
    const Oot3d::PicaShaderPipelineResult& variant, const GfxNativePicaDrawView& draw,
    NativePicaUberUnavailable& reason) {
    // variant is the specialized program's: the always-available program
    // executes its decisions on the parametric base of the draw's pair.
    const auto& base = draw.UberFragmentShaderSourceIdentity;
    const auto key = std::make_pair(
        Renderer3ds::BuildPicaShaderModuleIdentity(
            variant.VertexShaderSourceIdentity, variant.FragmentShaderSourceIdentity,
            draw.CanonicalDescriptorSchemaVersion, true, variant.FragmentOutputs),
        std::make_tuple(base.Id, base.SecondaryHash, base.Size));
    auto found = mUberNativePicaShaders.find(key);
    if (found == mUberNativePicaShaders.end()) {
        // Built-in artifacts only: an always-available program never compiles.
        NativePicaShaderProgram shader;
        shader.Available = false;
        shader.VertexSource = Oot3d::IdentifyPicaAotShaderSource(variant.VertexShaderSource);
        shader.FragmentSource = Oot3d::IdentifyPicaAotShaderSource(draw.UberFragmentShaderSource);
        shader.FragmentOutputs = variant.FragmentOutputs;
        const auto vertex = Renderer3ds::FindPicaVertexArtifact(draw.VertexArtifacts, variant.VertexShaderSource);
        const bool fragmentInstrumented =
            (static_cast<uint32_t>(variant.AppliedFeatures) &
             ~static_cast<uint32_t>(Oot3d::PicaShaderInstrumentationFeature::TemporalVertex)) != 0U;
        // Effects compose as data in the superset program of the parametric
        // base: one program per structural configuration and vertex interface.
        const Oot3d::PicaUberEffectProgramRequest supersetRequest{
            draw.UberFragmentShaderSource, draw.UberFragmentShaderHooks, variant.TemporalVertexApplied,
            variant.SurfaceVertexApplied, variant.SurfaceVertexApplied && mPbrSeamsAvailable};
        const bool superset = fragmentInstrumented &&
            Oot3d::PicaUberEffectSupports(variant.Decisions, supersetRequest);
        std::span<const uint32_t> fragment;
        bool nriApplied = false;
        if (superset) {
            const auto artifactKey = std::make_tuple(base.Id, base.SecondaryHash, base.Size,
                static_cast<uint8_t>((supersetRequest.TemporalInputs ? 1U : 0U) |
                    (supersetRequest.SurfaceInputs ? 2U : 0U) | (supersetRequest.PrimitiveIds ? 4U : 0U)));
            auto artifact = mUberSupersetArtifacts.find(artifactKey);
            if (artifact == mUberSupersetArtifacts.end()) {
                const auto composed = Oot3d::BuildPicaNriFragmentShaderVariant(
                    Oot3d::BuildPicaUberEffectProgram(supersetRequest));
                UberSupersetArtifact entry;
                entry.NriApplied = composed.Applied;
                if (composed.Applied) {
                    entry.Fragment = Renderer3ds::FindPicaFragmentArtifact(
                        Oot3d::UberFragmentArtifacts(), composed.Source, true);
                    entry.NriSource = Oot3d::IdentifyPicaAotShaderSource(composed.Source);
                }
                artifact = mUberSupersetArtifacts.emplace(artifactKey, entry).first;
            }
            nriApplied = artifact->second.NriApplied;
            fragment = artifact->second.Fragment;
            shader.EffectBlock = true;
            shader.DeclaredOutputs = static_cast<uint8_t>(variant.Decisions.Outputs | 1U);
            shader.NriFragmentSource = artifact->second.NriSource;
        } else if (!fragmentInstrumented) {
            // The canonical parametric base.
            const auto nri = Oot3d::BuildPicaNriFragmentShaderVariant(std::string(draw.UberFragmentShaderSource));
            nriApplied = nri.Applied;
            if (nri.Applied) {
                fragment = Renderer3ds::FindPicaFragmentArtifact(Renderer3ds::NativeFragmentArtifacts(), nri.Source, true);
                shader.NriFragmentSource = Oot3d::IdentifyPicaAotShaderSource(nri.Source);
            }
        }
        auto unavailable = NativePicaUberUnavailable::Count;
        if (fragmentInstrumented && !superset) unavailable = NativePicaUberUnavailable::EffectNotCovered;
        else if (vertex.Spirv.empty()) unavailable = NativePicaUberUnavailable::VertexNotBuiltIn;
        else if (!nriApplied) unavailable = NativePicaUberUnavailable::NoNriContract;
        else if (fragment.empty()) unavailable = NativePicaUberUnavailable::FragmentNotBuiltIn;
        if (unavailable == NativePicaUberUnavailable::Count) {
            shader.NriVertexSpirv.assign(vertex.Spirv.begin(), vertex.Spirv.end());
            shader.NriFragmentSpirv.assign(fragment.begin(), fragment.end());
            shader.NriDescriptorContract = true;
            shader.Available = true;
            ++mNativePicaPreparationCounters.UberPrograms;
        } else {
            shader.UnavailableReason = static_cast<uint8_t>(unavailable);
            static const bool uberDiagnostics = [] {
                const char* value = std::getenv("TRIAEVUM_UBER_DIAGNOSTICS");
                return value != nullptr && std::string_view(value) == "1";
            }();
            if (uberDiagnostics)
                std::fprintf(stderr, "TRIAEVUM_UBER_UNAVAILABLE reason=%u features=%08x bits=%08x vertex=%llu fragment=%llu bytes=%zu\n",
                    static_cast<unsigned>(unavailable), static_cast<unsigned>(variant.AppliedFeatures),
                    static_cast<unsigned>(variant.Decisions.Bits),
                    static_cast<unsigned long long>(variant.VertexShaderSourceIdentity.Id),
                    static_cast<unsigned long long>(variant.FragmentShaderSourceIdentity.Id),
                    variant.FragmentShaderSource.size());
        }
        found = mUberNativePicaShaders.emplace(key, std::move(shader)).first;
    }
    if (!found->second.Available) {
        reason = static_cast<NativePicaUberUnavailable>(found->second.UnavailableReason);
        return nullptr;
    }
    return &found->second;
}

VkShaderModule GfxRenderingAPIVulkan::CreateShaderModuleFromSpirv(
    std::span<const uint32_t> spirv) {
    if (spirv.empty() || spirv.front() != kSpirvMagic) {
        throw std::runtime_error("native PICA SPIR-V module is invalid");
    }
    VkShaderModuleCreateInfo moduleInfo{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleInfo.codeSize = spirv.size_bytes();
    moduleInfo.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    CheckVk(vkCreateShaderModule(mDevice, &moduleInfo, nullptr, &module),
            "vkCreateShaderModule");
    return module;
}

VkShaderModule GfxRenderingAPIVulkan::CompileShaderModule(
    const std::string& source, bool vertexShader, const char* sourceName,
    std::vector<uint32_t>* spirvOutput) {
    std::vector<uint32_t> spirv =
        CompileShaderSpirv(source, vertexShader, sourceName);
    const VkShaderModule module = CreateShaderModuleFromSpirv(spirv);
    if (spirvOutput != nullptr)
        *spirvOutput = std::move(spirv);
    return module;
}

void GfxRenderingAPIVulkan::CreateShaderResources() {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    for (uint32_t binding = 0; binding < 4; ++binding) {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[binding].descriptorCount = 1;
        bindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    descriptorLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    descriptorLayoutInfo.pBindings = bindings.data();
    CheckVk(vkCreateDescriptorSetLayout(mDevice, &descriptorLayoutInfo, nullptr,
                                        &mTextureDescriptorSetLayout),
            "vkCreateDescriptorSetLayout");

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    VkPushConstantRange transformPushRange{};
    transformPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    transformPushRange.size = sizeof(TransformPushConstants);
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mTextureDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &transformPushRange;
    CheckVk(vkCreatePipelineLayout(mDevice, &pipelineLayoutInfo, nullptr, &mPipelineLayout),
            "vkCreatePipelineLayout");
    CreateNativePicaShaderResources();
}

} // namespace Fast

#endif
