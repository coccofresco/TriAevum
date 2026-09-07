if(NOT NRI_SOURCE_DIR)
    message(FATAL_ERROR "NRI_SOURCE_DIR is required")
endif()

set(_upscaler "${NRI_SOURCE_DIR}/Source/Shared/UpscalerInterface.hpp")
file(READ "${_upscaler}" _source)
set(_old [=[
    if (type == UpscalerType::DLSR || type == UpscalerType::DLRR) {
        if (deviceDesc.adapterDesc.vendor == Vendor::NVIDIA && deviceDesc.tiers.rayTracing) // an elegant way to detect an RTX GPU?
            return true;
    }
]=])
set(_new [=[
    if (type == UpscalerType::DLSR) {
        // DLSS Super Resolution needs an NVIDIA GPU, but not Vulkan RT
        // extensions. Requiring the app to enable RT incorrectly hides DLSR.
        if (deviceDesc.adapterDesc.vendor == Vendor::NVIDIA)
            return true;
    }
    if (type == UpscalerType::DLRR) {
        if (deviceDesc.adapterDesc.vendor == Vendor::NVIDIA && deviceDesc.tiers.rayTracing)
            return true;
    }
]=])

string(FIND "${_source}" "${_new}" _already_patched)
if(NOT _already_patched EQUAL -1)
    return()
endif()
string(FIND "${_source}" "${_old}" _patch_site)
if(_patch_site EQUAL -1)
    message(FATAL_ERROR "NRI DLSS capability site changed; update the OOT3D patch")
endif()
string(REPLACE "${_old}" "${_new}" _source "${_source}")
file(WRITE "${_upscaler}" "${_source}")
