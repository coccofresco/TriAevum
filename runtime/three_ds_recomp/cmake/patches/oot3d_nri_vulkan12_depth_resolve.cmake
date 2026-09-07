if(NOT NRI_SOURCE_DIR)
    message(FATAL_ERROR "NRI_SOURCE_DIR is required")
endif()

# NRI v180 exposes depth/stencil attachment resolve through both its Vulkan
# dynamic-rendering and legacy-render-pass implementations on Vulkan 1.2, but
# its validation layer incorrectly requires VK_KHR_maintenance10 before
# CmdBeginRendering may use a depth resolve destination. Maintenance10 is
# needed for transfer-style depth resolves, not attachment resolves.
set(_validation
    "${NRI_SOURCE_DIR}/Source/Validation/CommandBufferVal.hpp")
file(READ "${_validation}" _source)
set(_old [=[
        NRI_RETURN_ON_FAILURE(&m_Device, m_Device.GetFormatSupport(resolveDstVal.GetFormat()) & FormatSupportBits::MULTISAMPLE_RESOLVE, ReturnVoid(), "'depth.resolveDst' format does not support 'FormatSupportBits::MULTISAMPLE_RESOLVE'");
        if (!deviceDesc.features.resolveOpMinMax)
            NRI_RETURN_ON_FAILURE(&m_Device, renderingDesc.depth.resolveOp == ResolveOp::AVERAGE, ReturnVoid(), "'features.resolveOpMinMax' is false");
]=])
set(_new [=[
        // OOT3D: Vulkan 1.2 attachment depth resolves, including the
        // device-advertised MIN/MAX modes, do not require maintenance10.
]=])
set(_partial [=[
        // OOT3D: Vulkan 1.2 attachment resolves do not require maintenance10.
        if (!deviceDesc.features.resolveOpMinMax)
            NRI_RETURN_ON_FAILURE(&m_Device, renderingDesc.depth.resolveOp == ResolveOp::AVERAGE, ReturnVoid(), "'features.resolveOpMinMax' is false");
]=])

string(FIND "${_source}" "${_new}" _already_patched)
if(NOT _already_patched EQUAL -1)
    return()
endif()
string(FIND "${_source}" "${_partial}" _partial_patch_site)
if(NOT _partial_patch_site EQUAL -1)
    string(REPLACE "${_partial}" "${_new}" _source "${_source}")
    file(WRITE "${_validation}" "${_source}")
    return()
endif()
string(FIND "${_source}" "${_old}" _patch_site)
if(_patch_site EQUAL -1)
    message(FATAL_ERROR
        "NRI depth-resolve validation changed; update the OOT3D patch")
endif()
string(REPLACE "${_old}" "${_new}" _source "${_source}")
file(WRITE "${_validation}" "${_source}")
