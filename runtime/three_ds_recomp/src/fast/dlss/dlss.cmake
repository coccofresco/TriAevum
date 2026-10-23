# DLSS module (LICENSE.md in this directory): DLSS Super Resolution through
# NVIDIA's NGX SDK, under NVIDIA's licence. Included by src/CMakeLists.txt only
# with THREE_DS_RECOMP_ENABLE_DLSS_MODULE.
#
# The module is its own shared library, triaevum_dlss, and the only binary of
# TriAevum that links NGX. It compiles only this directory's source, with the
# module interface (module_interface/, MIT) and the Vulkan and NGX headers on
# its include path, and links nothing of TriAevum. The runtime loads it when
# it is installed beside the executable (fast/oot3d/dlss_module_loader.cpp),
# next to NVIDIA's DLSS runtime library; without it DLSS is not offered.
if(NOT THREE_DS_RECOMP_NGX_SOURCE_DIR)
    message(FATAL_ERROR "the DLSS module needs the NGX SDK (THREE_DS_RECOMP_NGX_SOURCE_DIR)")
endif()
if(WIN32)
    # NGX's static library matches the C runtime the build links.
    if((NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY) OR (CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL"))
        set(_triaevum_ngx_suffix "d")
    else()
        set(_triaevum_ngx_suffix "s")
    endif()
    set(_triaevum_ngx_library
        "${THREE_DS_RECOMP_NGX_SOURCE_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_${_triaevum_ngx_suffix}$<$<CONFIG:Debug>:_dbg>.lib")
    unset(_triaevum_ngx_suffix)
else()
    set(_triaevum_ngx_library "${THREE_DS_RECOMP_NGX_SOURCE_DIR}/lib/Linux_x86_64/libnvsdk_ngx.a")
endif()
add_library(triaevum_dlss SHARED "${CMAKE_CURRENT_LIST_DIR}/dlss_module.cpp")
target_compile_features(triaevum_dlss PRIVATE cxx_std_20)
target_include_directories(triaevum_dlss PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../module_interface"
    "${THREE_DS_RECOMP_NGX_INCLUDE_DIR}"
    $<TARGET_PROPERTY:Vulkan::Vulkan,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(triaevum_dlss PRIVATE
    TRIAEVUM_NGX_VERSION="${THREE_DS_RECOMP_NGX_VERSION}")
target_link_libraries(triaevum_dlss PRIVATE "${_triaevum_ngx_library}" ${CMAKE_DL_LIBS})
unset(_triaevum_ngx_library)
# Beside the runtime executable, where the loader looks for it.
set_target_properties(triaevum_dlss PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    FOLDER dlss)
