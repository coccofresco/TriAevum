// Native PICA on Vulkan: texture records, storage, uploads and materials.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_pica_internal.h"
#include "fast/extension/grass_gpu_memory.h"

namespace Fast {

uint32_t NativePicaMipLevelCount(
    const GfxNativePicaTextureView& texture) {
    if (texture.Width == 0U || texture.Height == 0U) {
        return 0U;
    }
    uint32_t width = texture.Width;
    uint32_t height = texture.Height;
    uint32_t levels = 1U;
    while (width > 8U && height > 8U) {
        ++levels;
        width >>= 1U;
        height >>= 1U;
    }
    return std::min(levels,
                    static_cast<uint32_t>(texture.MaxMipLevel) + 1U);
}

std::optional<size_t> NativePicaEncodedMipSize(
    uint8_t format, uint32_t width, uint32_t height) {
    static constexpr std::array<uint8_t, 14> kNibblesPerPixel{
        8U, 6U, 4U, 4U, 4U, 4U, 4U,
        2U, 2U, 2U, 1U, 1U, 1U, 2U};
    if (format >= kNibblesPerPixel.size() || width == 0U ||
        height == 0U) {
        return std::nullopt;
    }
    const uint64_t tiledWidth = (std::max(8U, width) + 7U) & ~7ULL;
    const uint64_t tiledHeight = (std::max(8U, height) + 7U) & ~7ULL;
    const uint64_t nibbles = tiledWidth * tiledHeight *
                             kNibblesPerPixel[format];
    const uint64_t bytes = (nibbles + 1U) / 2U;
    if (bytes > std::numeric_limits<size_t>::max()) {
        return std::nullopt;
    }
    return static_cast<size_t>(bytes);
}

std::optional<uint64_t> ResolveNativeTextureBaseLevelContentHash(
    const GfxNativePicaTextureView& texture) {
    if (!texture.CopyReferences.empty()) return std::nullopt;
    if (texture.NativeBaseLevelContentHashAvailable) {
        return texture.NativeBaseLevelContentHash;
    }
    const auto byteCount = NativePicaEncodedMipSize(
        texture.NativeFormat, texture.Width, texture.Height);
    if (!byteCount.has_value() || *byteCount > texture.NativeBytes.size()) {
        return std::nullopt;
    }
    return HashNativeBytes(texture.NativeBytes.first(*byteCount));
}

std::optional<size_t> NativePicaDecodedMipChainSize(
    uint32_t width, uint32_t height, uint32_t levels) {
    if (width == 0U || height == 0U || levels == 0U) {
        return std::nullopt;
    }
    uint64_t bytes = 0U;
    for (uint32_t level = 0U; level < levels; ++level) {
        bytes += static_cast<uint64_t>(std::max(1U, width >> level)) *
                 std::max(1U, height >> level) * 4U;
    }
    if (bytes > std::numeric_limits<size_t>::max()) {
        return std::nullopt;
    }
    return static_cast<size_t>(bytes);
}

GfxNativeTextureFilter NativePicaMinFilter(
    bool minLinear, bool mipLinear, uint32_t mipLevels) {
    if (mipLevels <= 1U) {
        return minLinear ? GfxNativeTextureFilter::Linear
                         : GfxNativeTextureFilter::Nearest;
    }
    if (mipLinear) {
        return minLinear ? GfxNativeTextureFilter::LinearMipmapLinear
                         : GfxNativeTextureFilter::NearestMipmapLinear;
    }
    return minLinear ? GfxNativeTextureFilter::LinearMipmapNearest
                     : GfxNativeTextureFilter::NearestMipmapNearest;
}

GfxRenderingAPIVulkan::TextureRecord*
GfxRenderingAPIVulkan::CreateGpuCopiedTextureRecord(
    const NativePicaTextureKey& textureKey,
    const GfxNativePicaTextureView& texture, uint32_t width, uint32_t height) {
    auto [inserted, wasInserted] = mNativePicaTextures.emplace(textureKey,
                                                               TextureRecord{});
    if (!wasInserted) {
        return &inserted->second;
    }
    auto& record = inserted->second;
    record.SamplerState.MinFilter = NativePicaMinFilter(
        texture.MinLinear, texture.MipLinear, 1U);
    record.SamplerState.MagFilter = texture.MagLinear
        ? GfxNativeTextureFilter::Linear
        : GfxNativeTextureFilter::Nearest;
    record.SamplerState.WrapS = DecodeNativePicaWrap(texture.NativeWrapS);
    record.SamplerState.WrapT = DecodeNativePicaWrap(texture.NativeWrapT);
    record.SamplerState.LodBias = 0.0F;
    record.SamplerState.MinMipLevel = texture.MinMipLevel;
    record.SamplerState.MaxMipLevel = texture.MaxMipLevel;
    // The image is written by the base fill and the owned regions below, so it
    // is allocated without an upload and needs a storage view.
    CreateNativePicaTextureStorage(record, width, height, 1U,
                                   VK_FORMAT_R8G8B8A8_UNORM,
                                   /*storageView=*/true);
    record.UploadedMipLevels = 1U;
    record.Uploaded = true;
    RecreateSampler(record);
    return &record;
}

GfxRenderingAPIVulkan::TextureRecord*
GfxRenderingAPIVulkan::GetOrCreateNativePicaTexture(
    const GfxNativePicaTextureView& texture, std::string* error,
    uint64_t replacementGeneration, uint64_t renderTargetNamespace,
    const uint64_t* precomputedContentHash) {
    TextureRecord* record = ResolveNativePicaTexture(
        texture, error, replacementGeneration, renderTargetNamespace,
        precomputedContentHash);
    if (record != nullptr) record->LastUsedFrame = mFrameCounter;
    return record;
}

GfxRenderingAPIVulkan::TextureRecord*
GfxRenderingAPIVulkan::ResolveNativePicaTexture(
    const GfxNativePicaTextureView& textureView, std::string* error,
    uint64_t replacementGeneration, uint64_t renderTargetNamespace,
    const uint64_t* precomputedContentHash) {
    const GfxNativePicaTextureView& texture = textureView;
    try {
        NativePicaTextureKey key;
        key.ReplacementGeneration = replacementGeneration;
        key.PhysicalAddress = texture.PhysicalAddress;
        key.Width = texture.Width;
        key.Height = texture.Height;
        key.Format = texture.NativeFormat;
        key.Type = texture.NativeType;
        key.WrapS = texture.NativeWrapS;
        key.WrapT = texture.NativeWrapT;
        key.MinLinear = texture.MinLinear;
        key.MagLinear = texture.MagLinear;
        key.MipLinear = texture.MipLinear;
        key.LodBiasRaw = texture.LodBiasRaw;
        key.MinMipLevel = texture.MinMipLevel;
        key.MaxMipLevel = texture.MaxMipLevel;
        const bool shadow2d = texture.NativeType == 2U;
        if (shadow2d && !texture.CopyReferences.empty()) {
            throw std::runtime_error(
                "GPU-produced PICA copies require an RGBA sampler, not a packed shadow sampler");
        }
        ++mGpuTextureCopySamplerCost.Resolutions;
        // Ownership comes from the producer references the view carries, never
        // from a missing content hash: a view with references is served by the
        // immutable copy that producer captured at its internal resolution.
        GpuTextureCopyDerivation derivation;
        GpuTextureCopyFallback copyFallback = GpuTextureCopyFallback::None;
        std::string derivationError;
        const bool derivesCopy =
            shadow2d == false && texture.CopyReferences.empty() == false &&
            BuildGpuTextureCopyDerivation(texture, renderTargetNamespace,
                                          derivation, &copyFallback,
                                          &derivationError);
        if (shadow2d == false && texture.CopyReferences.empty() == false &&
            derivesCopy == false) {
            switch (copyFallback) {
                case GpuTextureCopyFallback::UnknownProducer:
                    ++mGpuTextureCopySamplerCost.FallbackUnknownProducer;
                    break;
                case GpuTextureCopyFallback::UncapturedProducer:
                    ++mGpuTextureCopySamplerCost.FallbackUncapturedProducer;
                    break;
                case GpuTextureCopyFallback::MipChain:
                    ++mGpuTextureCopySamplerCost.FallbackMipChain;
                    break;
                case GpuTextureCopyFallback::Layout:
                case GpuTextureCopyFallback::None:
                    ++mGpuTextureCopySamplerCost.FallbackLayout;
                    break;
            }
            static bool derivationReported = false;
            if (!derivationReported) {
                derivationReported = true;
                std::fprintf(
                    stderr,
                    "TRIAEVUM_GPU_COPY_UNRESOLVED reason=%u texture=%ux%u "
                    "address=0x%08X detail=%s\n",
                    static_cast<unsigned>(copyFallback), texture.Width,
                    texture.Height, texture.PhysicalAddress,
                    derivationError.c_str());
            }
            throw std::runtime_error(
                "raw PICA copy samples cannot be resolved: " + derivationError);
        }
        key.GpuCopyIdentity = derivesCopy
            ? GpuTextureCopyReferenceIdentity(texture, renderTargetNamespace)
            : 0U;
        if (derivesCopy) {
            // The same references resolve to a new immutable capture when a
            // visual replay re-executes their producer (or a canonical version
            // is imported): the derived image follows the capture it reads.
            for (const auto& group : derivation.Groups) {
                key.GpuCopyIdentity =
                    (key.GpuCopyIdentity ^ group.SurfaceIdentity) * 1099511628211ULL;
            }
            if (key.GpuCopyIdentity == 0U) key.GpuCopyIdentity = 1U;
        }
        key.ContentHash = derivesCopy && derivation.CoversDeclaredLayout
            ? 0U
            : (precomputedContentHash != nullptr
                   ? *precomputedContentHash
                   : HashNativeBytes(texture.NativeBytes));
        if (derivesCopy) {
            const auto exact = mNativePicaTextures.find(key);
            if (exact != mNativePicaTextures.end()) {
                for (const auto identity : exact->second.CopyProducerIdentities) {
                    const auto producer = mGpuTextureCopySurfaces.find(identity);
                    if (producer != mGpuTextureCopySurfaces.end())
                        producer->second.LastReferencedFrame = mFrameCounter;
                }
                return &exact->second;
            }
            TextureRecord* record = CreateGpuCopiedTextureRecord(
                key, texture, derivation.ImageWidth, derivation.ImageHeight);
            try {
                // Only CPU-owned (uncovered) bytes are initialized from the
                // snapshot, at the declared native size: the expand into the
                // sampled density happens on the GPU, in the copy's own pass. A
                // fully owned view needs no CPU decode and no upload at all.
                if (derivation.CoversDeclaredLayout == false) {
                    const auto encodedBytes = NativePicaEncodedMipSize(
                        texture.NativeFormat, texture.Width, texture.Height);
                    if (encodedBytes.has_value() == false ||
                        *encodedBytes > texture.NativeBytes.size()) {
                        throw std::runtime_error(
                            "native PICA copy source bytes are truncated");
                    }
                    std::vector<uint8_t> nativePixels;
                    std::string decodeError;
                    if (Extension::DecodePicaTextureRgba8(
                            texture.NativeFormat, texture.Width,
                            texture.Height,
                            texture.NativeBytes.first(*encodedBytes),
                            nativePixels, &decodeError) == false) {
                        throw std::runtime_error(
                            "native PICA copy base decode failed: " +
                            decodeError);
                    }
                    TextureRecord base;
                    CreateNativePicaTextureImage(
                        base, nativePixels, texture.Width, texture.Height, 1U,
                        VK_FORMAT_R8G8B8A8_UNORM, /*storageView=*/false);
                    // The record owns the base image: it is sampled only until
                    // the fill dispatch that expands it has executed.
                    record->BaseImage = base.Image;
                    base.Image = VK_NULL_HANDLE;
                    base.NriImage = VK_NULL_HANDLE;
                    base.Memory = VK_NULL_HANDLE;
                    DestroyTexture(base);
                    if (RecordGpuTextureCopyBaseFill(derivation,
                                                    record->BaseImage,
                                                    *record) == false) {
                        throw std::runtime_error(
                            "native PICA copy base fill was refused");
                    }
                }
                if (RecordGpuTextureCopyDerivation(derivation, *record) ==
                    false) {
                    throw std::runtime_error(
                        "native PICA copy derivation recorded no region");
                }
            } catch (...) {
                const auto created = mNativePicaTextures.find(key);
                if (created != mNativePicaTextures.end()) {
                    DestroyTexture(created->second);
                    mNativePicaTextures.erase(created);
                }
                throw;
            }
            // The record itself carries the provenance and the owned rectangle,
            // so a later draw reads them on a cache hit as well, and a restored
            // record keeps them.
            record->SourceResolvedColour = derivation.SceneEffectsResolved;
            record->SourceOwnedX0 = derivation.OwnedX0;
            record->SourceOwnedY0 = derivation.OwnedY0;
            record->SourceOwnedX1 = derivation.OwnedX1;
            record->SourceOwnedY1 = derivation.OwnedY1;
            record->SourceDeclaredWidth = derivation.DeclaredWidth;
            record->SourceDeclaredHeight = derivation.DeclaredHeight;
            DumpGpuTextureCopyConsumer(
                record->Image, record->Width, record->Height, texture,
                derivation.FirstProducerCompletionId, derivation.Encoding,
                derivation.SceneEffectsResolved);
            ++mGpuTextureCopySamplerCost.GpuBound;
            mGpuTextureCopySamplerCost.GpuBytes +=
                uint64_t{derivation.ImageWidth} * derivation.ImageHeight * 4U;
            mGpuTextureCopySamplerCost.GpuExtentPixels +=
                uint64_t{derivation.ImageWidth} * derivation.ImageHeight;
            mGpuTextureCopySamplerCost.NativeExtentPixels +=
                uint64_t{texture.Width} * texture.Height;
            return record;
        }
        const auto baseLevelContentHash = !shadow2d
            ? ResolveNativeTextureBaseLevelContentHash(texture)
            : std::nullopt;
        if (baseLevelContentHash.has_value()) {
            Extension::TextureCatalogRuntime::Instance().Observe(
                *baseLevelContentHash, key.PhysicalAddress, key.Width,
                key.Height, key.Format);
        }

        const auto createTexture =
            [&](const NativePicaTextureKey& textureKey,
                std::span<const uint8_t> pixels,
                uint32_t width, uint32_t height,
                uint32_t mipLevels,
                VkFormat format, uint64_t replacementHash,
                bool replacement, bool* insertedResult = nullptr,
                uint64_t replacementPixelHash = 0U) {
                auto [inserted, wasInserted] =
                    mNativePicaTextures.emplace(
                        textureKey, TextureRecord{});
                if (insertedResult != nullptr) {
                    *insertedResult = wasInserted;
                }
                if (!wasInserted) {
                    return &inserted->second;
                }
                auto& record = inserted->second;
                record.SamplerState.MinFilter = !shadow2d
                    ? NativePicaMinFilter(texture.MinLinear,
                                          texture.MipLinear, mipLevels)
                    : GfxNativeTextureFilter::Nearest;
                record.SamplerState.MagFilter =
                    !shadow2d && texture.MagLinear
                        ? GfxNativeTextureFilter::Linear
                        : GfxNativeTextureFilter::Nearest;
                record.SamplerState.WrapS =
                    DecodeNativePicaWrap(texture.NativeWrapS);
                record.SamplerState.WrapT =
                    DecodeNativePicaWrap(texture.NativeWrapT);
                record.SamplerState.LodBias = 0.0F;
                record.SamplerState.MinMipLevel = !shadow2d
                    ? texture.MinMipLevel : 0U;
                record.SamplerState.MaxMipLevel = !shadow2d
                    ? texture.MaxMipLevel : 0U;
                record.CustomReplacementHash = replacementHash;
                record.CustomReplacementPixelHash = replacementPixelHash;
                record.CustomReplacementApplied = replacement;
                record.CustomReplacementReady = replacement;
                CreateNativePicaTextureImage(
                    record, pixels, width, height, mipLevels, format);
                return &record;
            };

        const auto ensureTextureUploaded =
            [&](const NativePicaTextureKey& textureKey,
                TextureRecord& record) -> TextureRecord* {
                if (record.Uploaded) {
                    return &record;
                }
                const auto expectedBytes = NativePicaDecodedMipChainSize(
                    record.Width, record.Height, record.MipLevels);
                if (!expectedBytes.has_value() ||
                    record.Rgba8.size() != *expectedBytes) {
                    throw std::runtime_error(
                        "restored native PICA texture payload is invalid");
                }
                auto pixels = std::move(record.Rgba8);
                // Snapshot uploads bypass the normal decode observer. Restore
                // native mask pixels too, never the custom replacement image.
                if (!shadow2d && baseLevelContentHash.has_value()) {
                    const size_t baseBytes =
                        static_cast<size_t>(key.Width) * key.Height * 4U;
                    if (!textureKey.CustomReplacement &&
                        record.Width == key.Width && record.Height == key.Height &&
                        pixels.size() >= baseBytes) {
                        Extension::GrassTextureSourceCache::Instance().ObserveDecoded(
                            *baseLevelContentHash, key.Width, key.Height,
                            std::span<const uint8_t>(pixels).first(baseBytes));
                    } else {
                        const auto encodedBytes = NativePicaEncodedMipSize(
                            texture.NativeFormat, key.Width, key.Height);
                        std::vector<uint8_t> nativePixels;
                        if (encodedBytes.has_value() &&
                            *encodedBytes <= texture.NativeBytes.size() &&
                            Extension::DecodePicaTextureRgba8(
                                texture.NativeFormat, key.Width, key.Height,
                                texture.NativeBytes.first(*encodedBytes),
                                nativePixels, nullptr)) {
                            Extension::GrassTextureSourceCache::Instance().ObserveDecoded(
                                *baseLevelContentHash, key.Width, key.Height,
                                nativePixels);
                        }
                    }
                }
                const VkFormat format =
                    textureKey.Type == 2U &&
                            !textureKey.CustomReplacement
                        ? VK_FORMAT_R32_UINT
                        : VK_FORMAT_R8G8B8A8_UNORM;
                CreateNativePicaTextureImage(
                    record, pixels, record.Width, record.Height,
                    record.MipLevels, format);
                record.Rgba8.clear();
                return &record;
            };

        NativePicaTextureKey replacementKey = key;
        replacementKey.CustomReplacement = true;
        if (!shadow2d) {
            if (const auto replacement =
                    mNativePicaTextures.find(replacementKey);
                replacement != mNativePicaTextures.end()) {
                return ensureTextureUploaded(
                    replacement->first, replacement->second);
            }
        }

        const auto found = mNativePicaTextures.find(key);
        if (found != mNativePicaTextures.end()) {
            auto& record = found->second;
            if (!shadow2d &&
                record.CustomReplacementHash != 0U &&
                (record.CustomReplacementPending ||
                 record.CustomReplacementReady)) {
                const auto resolved =
                    ::Oot3d::Renderer::AzaharTexturePackRuntime::
                        Instance()
                            .PollQueued(
                                record.CustomReplacementHash);
                if (resolved.State ==
                        ::Oot3d::Renderer::
                            AzaharTextureResolveState::Ready &&
                    resolved.Replacement != nullptr &&
                    resolved.Replacement->Rgba8 != nullptr) {
                    record.CustomReplacementPending = false;
                    record.CustomReplacementReady = true;
                    replacementKey.ReplacementGeneration = resolved.Replacement->Generation;
                    const uint64_t replacementBytes =
                        resolved.Replacement->Rgba8->size();
                    const bool budgetAvailable =
                        mCustomTextureUploadBytesThisFrame == 0U ||
                        (mCustomTextureUploadBytesThisFrame <
                             kCustomTextureUploadBudgetBytes &&
                         replacementBytes <=
                             kCustomTextureUploadBudgetBytes -
                                 mCustomTextureUploadBytesThisFrame);
                    if (!budgetAvailable) {
                        return ensureTextureUploaded(found->first, record);
                    }
                    bool inserted = false;
                    TextureRecord* promoted = createTexture(
                        replacementKey,
                        *resolved.Replacement->Rgba8,
                        resolved.Replacement->Width,
                        resolved.Replacement->Height,
                        1U,
                        VK_FORMAT_R8G8B8A8_UNORM,
                        resolved.NativeHash, true, &inserted, resolved.Replacement->PixelContentHash);
                    if (inserted) {
                        mCustomTextureUploadBytesThisFrame +=
                            replacementBytes;
                    }
                    return promoted;
                }
                if (resolved.State !=
                    ::Oot3d::Renderer::
                        AzaharTextureResolveState::Pending) {
                    record.CustomReplacementPending = false;
                    record.CustomReplacementReady = false;
                }
            }
            return ensureTextureUploaded(found->first, record);
        }

        if (shadow2d) {
            const auto liveTarget = std::find_if(
                mNativePicaRenderTargets.begin(),
                mNativePicaRenderTargets.end(),
                [&](const auto& entry) {
                    return entry.first.ColorPhysicalAddress ==
                               texture.PhysicalAddress &&
                           entry.first.Width == texture.Width &&
                           entry.first.Height == texture.Height &&
                           entry.second.ShadowImage != VK_NULL_HANDLE;
                });
            if (liveTarget != mNativePicaRenderTargets.end()) {
                auto [inserted, wasInserted] =
                    mNativePicaTextures.emplace(key, TextureRecord{});
                auto& record = inserted->second;
                record.NriImage = liveTarget->second.ShadowImage;
                record.ImageBorrowed = true;
                record.LiveRenderTargetAlias = true;
                record.Width = liveTarget->second.Width;
                record.Height = liveTarget->second.Height;
                record.MipLevels = 1U;
                record.UploadedMipLevels = 1U;
                record.SamplerState.MinFilter =
                    GfxNativeTextureFilter::Nearest;
                record.SamplerState.MagFilter =
                    GfxNativeTextureFilter::Nearest;
                record.SamplerState.WrapS =
                    DecodeNativePicaWrap(texture.NativeWrapS);
                record.SamplerState.WrapT =
                    DecodeNativePicaWrap(texture.NativeWrapT);
                record.SamplerState.MaxMipLevel = 0U;
                record.SamplerState.MinMipLevel = 0U;
                VkImageViewCreateInfo view{
                    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                view.image = liveTarget->second.ShadowImage;
                view.viewType = VK_IMAGE_VIEW_TYPE_2D;
                view.format = VK_FORMAT_R32_UINT;
                view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                view.subresourceRange.levelCount = 1U;
                view.subresourceRange.layerCount = 1U;
                CheckNativeVk(vkCreateImageView(mDevice, &view, nullptr,
                                                &record.View),
                              "vkCreateImageView(native PICA live Shadow2D)");
                record.ImageLayout = VK_IMAGE_LAYOUT_GENERAL;
                record.Uploaded = true;
                RecreateSampler(record);
                return &record;
            }
        }

        std::vector<uint8_t> pixels;
        std::string decodeError;
        VkFormat imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
        uint32_t mipLevels = 1U;
        size_t baseNativeBytes = texture.NativeBytes.size();
        size_t baseDecodedBytes =
            static_cast<size_t>(texture.Width) * texture.Height * 4U;
        if (shadow2d) {
            std::vector<uint32_t> shadowPixels;
            if (!Extension::DetilePicaShadow2d(
                    texture.NativeBytes, texture.Width, texture.Height,
                    shadowPixels, &decodeError)) {
                throw std::runtime_error(
                    "native PICA Shadow2D decode failed: " + decodeError);
            }
            pixels.resize(shadowPixels.size() * sizeof(uint32_t));
            std::memcpy(pixels.data(), shadowPixels.data(), pixels.size());
            imageFormat = VK_FORMAT_R32_UINT;
        } else {
            mipLevels = NativePicaMipLevelCount(texture);
            if (mipLevels == 0U) {
                throw std::runtime_error(
                    "native PICA texture has no valid mip levels");
            }
            size_t nativeOffset = 0U;
            for (uint32_t level = 0U; level < mipLevels; ++level) {
                const uint32_t mipWidth =
                    std::max(1U, static_cast<uint32_t>(texture.Width) >> level);
                const uint32_t mipHeight =
                    std::max(1U, static_cast<uint32_t>(texture.Height) >> level);
                const auto encodedBytes = NativePicaEncodedMipSize(
                    texture.NativeFormat, mipWidth, mipHeight);
                if (!encodedBytes.has_value() ||
                    nativeOffset > texture.NativeBytes.size() ||
                    *encodedBytes > texture.NativeBytes.size() - nativeOffset) {
                    throw std::runtime_error(
                        "native PICA mip chain is truncated");
                }
                std::vector<uint8_t> decodedMip;
                if (!Extension::DecodePicaTextureRgba8(
                        texture.NativeFormat,
                        static_cast<uint16_t>(mipWidth),
                        static_cast<uint16_t>(mipHeight),
                        texture.NativeBytes.subspan(nativeOffset,
                                                    *encodedBytes),
                        decodedMip, &decodeError)) {
                    throw std::runtime_error(
                        "native PICA texture mip decode failed: " +
                        decodeError);
                }
                if (level == 0U) {
                    baseNativeBytes = *encodedBytes;
                    baseDecodedBytes = decodedMip.size();
                }
                pixels.insert(pixels.end(), decodedMip.begin(),
                              decodedMip.end());
                nativeOffset += *encodedBytes;
            }
            Extension::GrassTextureSourceCache::Instance().ObserveDecoded(
                baseLevelContentHash.value_or(key.ContentHash),
                key.Width, key.Height,
                std::span<const uint8_t>(pixels).first(baseDecodedBytes));
        }
        uint32_t uploadWidth = texture.Width;
        uint32_t uploadHeight = texture.Height;
        ::Oot3d::Renderer::AzaharTextureResolveResult
            replacementResult;
        if (!shadow2d) {
            replacementResult =
                ::Oot3d::Renderer::AzaharTexturePackRuntime::
                    Instance()
                        .ResolveOrQueue({
                            .Width = texture.Width,
                            .Height = texture.Height,
                            .NativeFormat =
                                texture.NativeFormat,
                            .MipLevel = 0U,
                            .NativeBytes =
                                texture.NativeBytes.first(baseNativeBytes),
                            .NativeRgba8 =
                                std::span<const uint8_t>(pixels)
                                    .first(baseDecodedBytes),
                        });
            if (baseLevelContentHash && replacementResult.NativeHashAvailable)
                Extension::TextureCatalogRuntime::Instance().AssociateMaterial(
                    *baseLevelContentHash, texture.Width, texture.Height, texture.NativeFormat,
                    replacementResult.NativeHash, replacementResult.UseNewHash);
            if (replacementResult.State ==
                    ::Oot3d::Renderer::
                        AzaharTextureResolveState::Ready &&
                replacementResult.Replacement != nullptr &&
                replacementResult.Replacement->Rgba8 != nullptr) {
                replacementKey.ReplacementGeneration = replacementResult.Replacement->Generation;
                const uint64_t replacementBytes =
                    replacementResult.Replacement->Rgba8->size();
                const bool budgetAvailable =
                    mCustomTextureUploadBytesThisFrame == 0U ||
                    (mCustomTextureUploadBytesThisFrame <
                         kCustomTextureUploadBudgetBytes &&
                     replacementBytes <=
                         kCustomTextureUploadBudgetBytes -
                             mCustomTextureUploadBytesThisFrame);
                if (budgetAvailable) {
                    bool inserted = false;
                    TextureRecord* promoted = createTexture(
                        replacementKey,
                        *replacementResult.Replacement->Rgba8,
                        replacementResult.Replacement->Width,
                        replacementResult.Replacement->Height,
                        1U,
                        VK_FORMAT_R8G8B8A8_UNORM,
                        replacementResult.NativeHash, true,
                        &inserted, replacementResult.Replacement->PixelContentHash);
                    if (inserted) {
                        mCustomTextureUploadBytesThisFrame +=
                            replacementBytes;
                    }
                    return promoted;
                }
            }
        }
        TextureRecord* record = createTexture(
            key, pixels, uploadWidth, uploadHeight, mipLevels, imageFormat,
            replacementResult.NativeHash, false);
        record->CustomReplacementPending =
            replacementResult.State ==
                ::Oot3d::Renderer::
                    AzaharTextureResolveState::Pending ||
            replacementResult.State ==
                ::Oot3d::Renderer::
                    AzaharTextureResolveState::Ready;
        record->CustomReplacementReady =
            replacementResult.State ==
            ::Oot3d::Renderer::
                AzaharTextureResolveState::Ready;
        return record;
    } catch (const std::exception& exception) {
        SetNativeError(error, exception.what());
        return nullptr;
    }
}

GfxRenderingAPIVulkan::TextureRecord*
GfxRenderingAPIVulkan::GetOrCreateNativePicaLightingLut(
    const ::Oot3d::Renderer::PicaLightingLutView& lightingLut,
    std::string* error) {
    uint64_t insertedHash = 0U;
    bool inserted = false;
    try {
        if (!lightingLut.Valid()) {
            throw std::runtime_error(
                "native PICA lighting LUT state is invalid");
        }
        auto [found, wasInserted] = mNativePicaLightingLuts.try_emplace(
            lightingLut.ContentHash);
        inserted = wasInserted;
        insertedHash = lightingLut.ContentHash;
        auto& cached = found->second;
        cached.LastUsedFrame = mFrameCounter;
        if (!wasInserted) {
            return &cached.Texture;
        }
        auto& texture = cached.Texture;
        texture.SamplerState.MinFilter =
            GfxNativeTextureFilter::Nearest;
        texture.SamplerState.MagFilter =
            GfxNativeTextureFilter::Nearest;
        texture.SamplerState.WrapS =
            GfxNativeTextureWrap::ClampToEdge;
        texture.SamplerState.WrapT =
            GfxNativeTextureWrap::ClampToEdge;
        texture.SamplerState.MinMipLevel = 0U;
        texture.SamplerState.MaxMipLevel = 0U;
        const auto bytes = std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(
                lightingLut.PackedEntries.data()),
            lightingLut.PackedEntries.size() * sizeof(uint32_t));
        CreateNativePicaTextureImage(
            texture, bytes, 256U, 24U, 1U, VK_FORMAT_R32_UINT);
        return &texture;
    } catch (const std::exception& exception) {
        if (inserted) {
            const auto found = mNativePicaLightingLuts.find(insertedHash);
            if (found != mNativePicaLightingLuts.end()) {
                DestroyTexture(found->second.Texture);
                mNativePicaLightingLuts.erase(found);
            }
        }
        SetNativeError(error, exception.what());
        return nullptr;
    }
}

void GfxRenderingAPIVulkan::CreateNativePicaTextureStorage(
    TextureRecord& texture, uint32_t width, uint32_t height, uint32_t mipLevels,
    VkFormat format, bool storageView) {

    texture.Width = width;
    texture.Height = height;
    texture.MipLevels = mipLevels;
    texture.UploadedMipLevels = 0;

    Extension::NriPicaTextureImage nriTexture;
    if (mNriPicaTextureImageOwner.Create(width, height, format, mipLevels,
                                         nriTexture, storageView)) {
        texture.Image = nriTexture.Image;
        texture.NriImage = nriTexture.Image;
        texture.View = nriTexture.View;
        texture.NriOwnedImage = true;
        mDiagnostics.RecordNriPicaTextureImage(true);
    } else {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (storageView) {
            // A derived raw-copy image is materialized on the GPU, so its
            // compute pass needs a storage view of it.
            imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckNativeVk(
            vkCreateImage(mDevice, &imageInfo, nullptr, &texture.Image),
            "vkCreateImage(native PICA texture)");
        texture.NriImage = texture.Image;

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(mDevice, texture.Image, &requirements);
        VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CheckNativeVk(vkAllocateMemory(
                          mDevice, &allocation, nullptr, &texture.Memory),
                      "vkAllocateMemory(native PICA texture)");
        CheckNativeVk(
            vkBindImageMemory(
                mDevice, texture.Image, texture.Memory, 0),
            "vkBindImageMemory(native PICA texture)");
    }
    if (!texture.NriOwnedImage) {
        VkImageViewCreateInfo view{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = texture.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = mipLevels;
        view.subresourceRange.layerCount = 1;
        CheckNativeVk(
            vkCreateImageView(mDevice, &view, nullptr, &texture.View),
            "vkCreateImageView(native PICA texture)");
    }
}

void GfxRenderingAPIVulkan::CreateNativePicaTextureImage(
    TextureRecord& texture, std::span<const uint8_t> pixels, uint32_t width,
    uint32_t height, uint32_t mipLevels, VkFormat format, bool storageView) {
    const auto expectedBytes = NativePicaDecodedMipChainSize(width, height, mipLevels);
    if (!expectedBytes.has_value() || pixels.size() != *expectedBytes || pixels.empty())
        throw std::runtime_error("native PICA texture upload size is invalid");
    CreateNativePicaTextureStorage(texture, width, height, mipLevels, format, storageView);
    size_t offset = 0U;
    for (uint32_t level = 0U; level < mipLevels; ++level) {
        const uint32_t mipWidth = std::max(1U, width >> level);
        const uint32_t mipHeight = std::max(1U, height >> level);
        const size_t bytes = static_cast<size_t>(mipWidth) * mipHeight * 4U;
        UploadNativePicaTextureRows(texture, pixels.subspan(offset, bytes),
            mipWidth, mipHeight, level, mipLevels, format, 0U, mipHeight);
        offset += bytes;
    }
    texture.UploadedMipLevels = mipLevels;
    texture.Uploaded = true;
    RecreateSampler(texture);
}

void GfxRenderingAPIVulkan::UploadNativePicaTextureRows(
    TextureRecord& texture, std::span<const uint8_t> pixels,
    uint32_t width, uint32_t height, uint32_t level, uint32_t mipLevels,
    VkFormat format, uint32_t rowOffset, uint32_t rowCount) {
    if (rowCount == 0U || rowOffset > height || rowCount > height - rowOffset ||
        static_cast<uint64_t>(width) * rowCount * 4U != pixels.size())
        throw std::runtime_error("native PICA texture upload region is invalid");
    EndNativePicaRenderPass();
    if (mRenderPassActive) {
        vkCmdEndRenderPass(mCommandBuffers[mCurrentFrame]);
        mRenderPassActive = false;
        mOverlayRenderPassActive = false;
    }
    Extension::NriPicaTextureUploadDesc upload;
    upload.FrameIndex = mCurrentFrame;
    upload.FrameId = mFrameCounter;
    upload.Image = texture.Image;
    upload.Format = format;
    upload.Width = width;
    upload.Height = height;
    upload.MipLevel = level;
    upload.MipLevels = mipLevels;
    upload.Pixels = pixels;
    upload.RowOffset = rowOffset;
    upload.RowCount = rowCount;
    if (mNriPicaTextureUploadPass.Execute(upload)) {
        mDiagnostics.RecordNriPicaTextureUpload(
            true, mNriPicaTextureUploadPass.LastUploadedBytes());
        return;
    }
    const VkDeviceSize uploadOffset = EnsureFrameVertexCapacity(pixels.size());
    auto& frame = mFrameResources[mCurrentFrame];
    std::memcpy(static_cast<uint8_t*>(frame.VertexBuffer.Mapped) + uploadOffset,
                pixels.data(), pixels.size());
    frame.VertexBytesUsed = uploadOffset + pixels.size();

    VkCommandBuffer commandBuffer = mCommandBuffers[mCurrentFrame];
    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.oldLayout = rowOffset == 0U ? VK_IMAGE_LAYOUT_UNDEFINED :
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = texture.Image;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.baseMipLevel = level;
    toTransfer.subresourceRange.levelCount = 1U;
    toTransfer.subresourceRange.layerCount = 1U;
    // Sampled textures are read by the same stages as the NRI path's
    // ShaderRead state: vertex, fragment and compute (GPU-derived copies).
    constexpr VkPipelineStageFlags shaderReadStages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    toTransfer.srcAccessMask = rowOffset == 0U ? 0U : VK_ACCESS_SHADER_READ_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer,
        rowOffset == 0U ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : shaderReadStages,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
    VkBufferImageCopy copy{};
    copy.bufferOffset = uploadOffset;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = level;
    copy.imageSubresource.layerCount = 1U;
    copy.imageOffset.y = static_cast<int32_t>(rowOffset);
    copy.imageExtent = {width, rowCount, 1U};
    vkCmdCopyBufferToImage(commandBuffer, frame.VertexBuffer.Buffer, texture.Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &copy);
    VkImageMemoryBarrier toShaderRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.image = texture.Image;
    toShaderRead.subresourceRange = toTransfer.subresourceRange;
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        shaderReadStages, 0, 0, nullptr, 0, nullptr, 1, &toShaderRead);
}

void GfxRenderingAPIVulkan::RetireNativePicaTexture(TextureRecord& texture) {
    if (texture.Image != VK_NULL_HANDLE || texture.NriImage != VK_NULL_HANDLE ||
        texture.Memory != VK_NULL_HANDLE || texture.Sampler != VK_NULL_HANDLE)
        mFrameResources[mCurrentFrame].RetiredTextures.push_back(std::move(texture));
    texture = {};
}

void GfxRenderingAPIVulkan::RetireNativePicaTexturesOfOtherGenerations(uint64_t generation) {
    // Every texture-pack reconfiguration starts a generation, and every
    // sampled texture is created again under it. Without this, each change of
    // the pack settings kept a full copy of the scene's textures.
    for (auto texture = mNativePicaTextures.begin(); texture != mNativePicaTextures.end();) {
        if (texture->first.ReplacementGeneration == generation) {
            ++texture;
            continue;
        }
        RetireNativePicaTexture(texture->second);
        texture = mNativePicaTextures.erase(texture);
    }
    mNativePicaTexturesGeneration = generation;
}

void GfxRenderingAPIVulkan::ReleaseIdleNativePicaTextures() {
    auto& residency = mNativePicaTextureResidency;
    if (mFrameCounter < residency.NextSweepFrame) return;
    // Diagnostic: TRIAEVUM_TEXTURE_IDLE_FRAMES=<n> releases every record idle
    // for n frames whatever the budget, to exercise their recreation.
    static const std::optional<uint64_t> idleOverride = []() -> std::optional<uint64_t> {
        const char* value = std::getenv("TRIAEVUM_TEXTURE_IDLE_FRAMES");
        if (value == nullptr || *value == '\0') return std::nullopt;
        return std::strtoull(value, nullptr, 10);
    }();
    auto policy = residency.Policy;
    if (idleOverride) {
        policy.IdleFrames = *idleOverride;
        policy.PressureIdleFrames = std::min(policy.PressureIdleFrames, *idleOverride);
        policy.SweepIntervalFrames = std::clamp<uint64_t>(*idleOverride, 1U, policy.SweepIntervalFrames);
    }
    residency.NextSweepFrame = mFrameCounter + policy.SweepIntervalFrames;
    residency.DeviceLocal = Renderer3ds::QueryVulkanDeviceLocalMemory(mPhysicalDevice);
    // Diagnostic: TRIAEVUM_VRAM_BUDGET_MIB=<n> caps the budget, as a smaller
    // card would.
    static const uint64_t budgetCap = [] {
        const char* value = std::getenv("TRIAEVUM_VRAM_BUDGET_MIB");
        return value == nullptr || *value == '\0' ? UINT64_MAX : std::strtoull(value, nullptr, 10) << 20U;
    }();
    if (residency.DeviceLocal) residency.DeviceLocal->Budget = std::min(residency.DeviceLocal->Budget, budgetCap);
    const bool pressure = residency.DeviceLocal &&
        Renderer3ds::DeviceLocalMemoryUnderPressure(residency.DeviceLocal->Usage, residency.DeviceLocal->Budget);
    if (pressure != residency.Pressure) {
        residency.Pressure = pressure;
        if (pressure)
            SPDLOG_WARN("VRAM budget: {} of {} MiB in use; releasing textures idle for {} frames",
                        residency.DeviceLocal->Usage >> 20U, residency.DeviceLocal->Budget >> 20U,
                        policy.PressureIdleFrames);
        else if (residency.DeviceLocal)
            SPDLOG_WARN("VRAM budget: {} of {} MiB in use; pressure relieved",
                        residency.DeviceLocal->Usage >> 20U, residency.DeviceLocal->Budget >> 20U);
    }
    const auto residentBytes = [](const TextureRecord& texture) -> uint64_t {
        if (texture.ImageBorrowed || (texture.Image == VK_NULL_HANDLE && texture.NriImage == VK_NULL_HANDLE))
            return 0U;
        return NativePicaDecodedMipChainSize(texture.Width, texture.Height, texture.MipLevels).value_or(0U);
    };
    uint64_t cachedRecords = 0U;
    uint64_t cachedBytes = 0U;
    for (auto texture = mNativePicaTextures.begin(); texture != mNativePicaTextures.end();) {
        auto& record = texture->second;
        // Raw-copy samples live and die with their producers; live aliases
        // with their render target.
        const bool otherOwner = record.LiveRenderTargetAlias || record.ImageBorrowed ||
                                texture->first.GpuCopyIdentity != 0U || !record.CopyProducerIdentities.empty();
        if (otherOwner || !Renderer3ds::GpuResourceIdle(mFrameCounter, record.LastUsedFrame, pressure, policy)) {
            ++cachedRecords;
            cachedBytes += residentBytes(record);
            ++texture;
            continue;
        }
        ++residency.ReleasedRecords;
        residency.ReleasedBytes += residentBytes(record);
        RetireNativePicaTexture(record);
        texture = mNativePicaTextures.erase(texture);
    }
    for (auto lut = mNativePicaLightingLuts.begin(); lut != mNativePicaLightingLuts.end();) {
        if (!Renderer3ds::GpuResourceIdle(mFrameCounter, lut->second.LastUsedFrame, pressure, policy)) {
            ++lut;
            continue;
        }
        ++residency.ReleasedRecords;
        residency.ReleasedBytes += residentBytes(lut->second.Texture);
        RetireNativePicaTexture(lut->second.Texture);
        lut = mNativePicaLightingLuts.erase(lut);
    }
    // TRIAEVUM_VRAM_TRACE=1: the heap and its known owners at every sweep.
    static const bool trace = std::getenv("TRIAEVUM_VRAM_TRACE") != nullptr;
    if (trace) {
        const auto& memory = residency.DeviceLocal;
        std::fprintf(stderr,
                     "TRIAEVUM_VRAM frame=%u usage_mib=%llu budget_mib=%llu pressure=%d textures=%llu "
                     "texture_mib=%llu luts=%zu materials=%zu render_targets=%zu grass_mib=%llu\n",
                     mFrameCounter, memory ? static_cast<unsigned long long>(memory->Usage >> 20U) : 0ULL,
                     memory ? static_cast<unsigned long long>(memory->Budget >> 20U) : 0ULL, pressure ? 1 : 0,
                     static_cast<unsigned long long>(cachedRecords),
                     static_cast<unsigned long long>(cachedBytes >> 20U), mNativePicaLightingLuts.size(),
                     mNativePicaMaterials.size(), mNativePicaRenderTargets.size(),
                     static_cast<unsigned long long>(Extension::GrassGpuMemoryInUse().DeviceLocal >> 20U));
    }
    // At most one report per idle window, so a scene change is one line (the
    // game log keeps warnings and above).
    if (residency.ReleasedRecords != 0U && mFrameCounter >= residency.NextReportFrame) {
        const auto& memory = residency.DeviceLocal;
        SPDLOG_WARN("Texture cache: released {} idle entries ({} MiB); {} textures cached ({} MiB); "
                    "VRAM {} of {} MiB",
                    residency.ReleasedRecords, residency.ReleasedBytes >> 20U, cachedRecords, cachedBytes >> 20U,
                    memory ? std::to_string(memory->Usage >> 20U) : std::string("?"),
                    memory ? std::to_string(memory->Budget >> 20U) : std::string("?"));
        residency.NextReportFrame = mFrameCounter + policy.IdleFrames;
        residency.ReleasedRecords = 0U;
        residency.ReleasedBytes = 0U;
    }
}

void GfxRenderingAPIVulkan::RetireNativePicaMaterial(NativePicaMaterialBundle& material) {
    for (auto& image : material.Images) RetireNativePicaTexture(image);
    material = {};
}

GfxRenderingAPIVulkan::NativePicaMaterialBundle*
GfxRenderingAPIVulkan::GetOrCreateNativePicaMaterial(
    const std::shared_ptr<const ::Oot3d::Renderer::AzaharPbrMaterial>& material) {
    auto& entry = mNativePicaMaterials[material->Identity.NativeHash];
    entry.LastUsedFrame = mFrameCounter;
    if (entry.Active.Material && entry.Active.Material->Identity != material->Identity) {
        RetireNativePicaMaterial(entry.Active);
        RetireNativePicaMaterial(entry.Pending);
        entry.FailedGeneration = 0U;
    }
    const auto active = [&]() -> NativePicaMaterialBundle* {
        return entry.Active.Material ? &entry.Active : nullptr;
    };
    if ((entry.Active.Material && entry.Active.Material->Generation == material->Generation) ||
        entry.FailedGeneration == material->Generation) return active();
    const auto sourceAt = [](const auto& source, size_t role) -> const auto& {
        return role < source.Textures.size() ? source.Textures[role] : source.ColorReplacement;
    };
    if (!entry.Pending.Material || entry.Pending.Material->Generation != material->Generation) {
        RetireNativePicaMaterial(entry.Pending);
        entry.Pending.Material = material;
        if (entry.Active.Material) {
            for (size_t role = 0U; role < entry.Pending.Images.size(); ++role) {
                const auto& old = sourceAt(*entry.Active.Material, role);
                const auto& next = sourceAt(*material, role);
                if (old && next && old->PixelContentHash == next->PixelContentHash &&
                    old->Width == next->Width && old->Height == next->Height)
                    entry.Pending.ReusedImages |= static_cast<uint8_t>(1U << role);
            }
        }
    }
    try {
        for (size_t role = 0U; role < entry.Pending.Images.size(); ++role) {
            const auto& source = sourceAt(*material, role);
            if (!source || (entry.Pending.ReusedImages & (1U << role)) != 0U) continue;
            auto& image = entry.Pending.Images[role];
            auto& uploaded = entry.Pending.UploadedRows[role];
            if (uploaded == source->Height) continue;
            const uint64_t rowBytes = static_cast<uint64_t>(source->Width) * 4U;
            if (!source->Rgba8 || rowBytes == 0U || source->Height == 0U ||
                rowBytes * source->Height != source->Rgba8->size())
                throw std::runtime_error("invalid decoded PBR texture dimensions");
            const uint64_t remaining = kCustomTextureUploadBudgetBytes -
                std::min(kCustomTextureUploadBudgetBytes, mCustomTextureUploadBytesThisFrame);
            const auto rows = static_cast<uint32_t>(
                std::min<uint64_t>(source->Height - uploaded, remaining / rowBytes));
            if (rows == 0U) return active();
            if (image.Image == VK_NULL_HANDLE)
                CreateNativePicaTextureStorage(image, source->Width, source->Height,
                    1U, VK_FORMAT_R8G8B8A8_UNORM, false);
            const auto pixels = std::span<const uint8_t>(*source->Rgba8).subspan(
                static_cast<size_t>(uploaded * rowBytes), static_cast<size_t>(rows * rowBytes));
            UploadNativePicaTextureRows(image, pixels, source->Width, source->Height,
                0U, 1U, VK_FORMAT_R8G8B8A8_UNORM, uploaded, rows);
            uploaded += rows;
            mCustomTextureUploadBytesThisFrame += pixels.size();
            if (uploaded != source->Height) return active();
            image.UploadedMipLevels = 1U;
            image.Uploaded = true;
            image.CustomReplacementHash = material->Identity.NativeHash;
            image.CustomReplacementPixelHash = source->PixelContentHash;
            image.CustomReplacementApplied = true;
            image.CustomReplacementReady = true;
        }
        for (size_t role = 0U; role < entry.Pending.Images.size(); ++role) {
            if ((entry.Pending.ReusedImages & (1U << role)) == 0U) continue;
            entry.Pending.Images[role] = std::move(entry.Active.Images[role]);
            entry.Active.Images[role] = {};
        }
        RetireNativePicaMaterial(entry.Active);
        entry.Active = std::move(entry.Pending);
        entry.Pending = {};
        mTemporalAaPass.ResetHistory();
        mTaaOutputValid = false;
        return &entry.Active;
    } catch (const std::exception& exception) {
        RetireNativePicaMaterial(entry.Pending);
        entry.FailedGeneration = material->Generation;
        ::Oot3d::Renderer::AzaharTexturePackRuntime::Instance().ReportMaterialUploadFailure(
            material->Identity.NativeHash, material->Generation,
            fmt::format("PBR material {:016X}: {}", material->Identity.NativeHash, exception.what()));
        return active();
    }
}

} // namespace Fast

#endif
