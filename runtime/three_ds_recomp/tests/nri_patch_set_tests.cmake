# nri_patch_set_tests: the NRI a build compiles must carry the OOT3D patches,
# whichever way its source was provided. A FETCHCONTENT_SOURCE_DIR_NRI override
# used to skip them; the renderer then crashed in CmdBeginRendering on the NULL
# descriptor of an unbound guide attachment.
foreach(_required NRI_SOURCE_DIR NGX_VERSION PATCH_DIR WORK_DIR)
    if(NOT ${_required})
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

function(run_verify source result_var output_var)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DNRI_SOURCE_DIR=${source} -DNGX_VERSION=${NGX_VERSION}
            -P ${PATCH_DIR}/oot3d_nri_verify_patches.cmake
        RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _output)
    set(${result_var} "${_result}" PARENT_SCOPE)
    set(${output_var} "${_output}" PARENT_SCOPE)
endfunction()

# 1. The build's own NRI source.
run_verify("${NRI_SOURCE_DIR}" _result _output)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "the build's NRI is not fully patched:\n${_output}")
endif()

# 2. A copy with the unbound color attachment patch removed, as an unpatched
#    override provided it, is rejected and named.
set(_files
    CMakeLists.txt Include/NRIDescs.h Source/Shared/DeviceBase.h Source/Shared/UpscalerInterface.hpp
    Source/Validation/CommandBufferVal.hpp Source/VK/CommandBufferVK.hpp Source/VK/DescriptorVK.hpp
    Source/VK/PipelineLibraryMemoryVK.h Source/VK/GraphicsPipelineLibrariesVK.h
    Source/VK/DynamicPipelineStateVK.h)
file(REMOVE_RECURSE "${WORK_DIR}")
foreach(_file IN LISTS _files)
    get_filename_component(_dir "${WORK_DIR}/${_file}" DIRECTORY)
    file(COPY "${NRI_SOURCE_DIR}/${_file}" DESTINATION "${_dir}")
endforeach()

set(_vk_patch [=[
    if (!attachmentDesc.descriptor) { // OOT3D: an unbound color attachment
        attachmentInfo = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        return;
    }
]=])
set(_val_patch [=[
        if (!renderingDesc.colors[i].descriptor) { // OOT3D: an unbound color attachment
            colors[i] = renderingDesc.colors[i];
            m_RenderTargets[i] = nullptr;
            continue;
        }
]=])
set(_val_upstream [=[
        NRI_RETURN_ON_FAILURE(&m_Device, renderingDesc.colors[i].descriptor, ReturnVoid(), "'colors[%u].descriptor' is NULL", i);
]=])
function(unpatch file patch upstream)
    file(READ "${WORK_DIR}/${file}" _source)
    string(FIND "${_source}" "${patch}" _site)
    if(_site EQUAL -1)
        message(FATAL_ERROR "${file}: the unbound color attachment patch text changed; update this test")
    endif()
    string(REPLACE "${patch}" "${upstream}" _source "${_source}")
    file(WRITE "${WORK_DIR}/${file}" "${_source}")
endfunction()
unpatch("Source/VK/CommandBufferVK.hpp" "${_vk_patch}" "")
unpatch("Source/Validation/CommandBufferVal.hpp" "${_val_patch}" "${_val_upstream}")

run_verify("${WORK_DIR}" _result _output)
if(_result EQUAL 0)
    message(FATAL_ERROR "an NRI without the unbound color attachment patch passed verification")
endif()
string(FIND "${_output}" "oot3d_nri_unbound_color_attachments not applied" _named)
if(_named EQUAL -1)
    message(FATAL_ERROR "verification failed without naming the missing patch:\n${_output}")
endif()

# 3. The patch script applies to that upstream text and restores the build's files.
execute_process(
    COMMAND ${CMAKE_COMMAND} -DNRI_SOURCE_DIR=${WORK_DIR}
        -P ${PATCH_DIR}/oot3d_nri_unbound_color_attachments.cmake
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _output)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "the unbound color attachment patch did not apply:\n${_output}")
endif()
foreach(_file Source/VK/CommandBufferVK.hpp Source/Validation/CommandBufferVal.hpp)
    file(SHA256 "${NRI_SOURCE_DIR}/${_file}" _expected)
    file(SHA256 "${WORK_DIR}/${_file}" _actual)
    if(NOT _expected STREQUAL _actual)
        message(FATAL_ERROR "${_file}: re-patched copy differs from the build's NRI")
    endif()
endforeach()
run_verify("${WORK_DIR}" _result _output)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "the re-patched copy is not fully patched:\n${_output}")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
message(STATUS "nri_patch_set_tests passed")
