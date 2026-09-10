#=================== Top-Level ===================
file(GLOB Source_Files__TopLevel RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cpp")

source_group("" FILES ${Source_Files__TopLevel})
target_sources(three_ds_recomp_runtime PRIVATE ${Source_Files__TopLevel})

#=================== Debug ===================

file(GLOB Source_Files__Debug RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "debug/*.cpp")

source_group("debug" FILES ${Source_Files__Debug})
target_sources(three_ds_recomp_runtime PRIVATE ${Source_Files__Debug})

#=================== Resource ===================

file(GLOB Source_Files__Resource__Types RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "resource/type/*.h" "resource/type/*.cpp")
source_group("resource/type" FILES ${Source_Files__Resource__Types})

file(GLOB Source_Files__Resource__Factories RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "resource/factory/*.h" "resource/factory/*.cpp")
source_group("resource/factory" FILES ${Source_Files__Resource__Factories})

target_sources(three_ds_recomp_runtime PRIVATE ${Source_Files__Resource} ${Source_Files__Resource__Types} ${Source_Files__Resource__Factories} ${Source_Files__Resource__Archive})

#=================== Graphic ===================

file(GLOB Source_Files__Graphic CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "backends/*.cpp")

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX
        "^backends/(gfx_dxgi|gfx_direct3d).*")
endif()

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "^backends/gfx_metal.*")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "^backends/gfx_opengl.*")
endif()

if(NOT THREE_DS_RECOMP_ENABLE_VULKAN)
    list(FILTER Source_Files__Graphic EXCLUDE REGEX
        "^backends/(gfx_vulkan|oot3d_vulkan_(gpu_profiler|validation)).*")
endif()

source_group("graphic" FILES ${Source_Files__Graphic})
target_sources(three_ds_recomp_runtime PRIVATE ${Source_Files__Graphic})

#=================== Reusable renderer extension core ===================

set(Source_Files__RendererExtensions
    renderer/cache_file.cpp
    renderer/spirv_cache.cpp
    renderer/pipeline_preparation_job.cpp
    renderer/extension_resource_allocation.cpp
    renderer/extension_resource_binding.cpp
    renderer/extension_resource_graph.cpp
    renderer/extension_scene_publication.cpp
    renderer/extension_schedule.cpp
)
add_library(renderer_extension_core STATIC
    ${Source_Files__RendererExtensions})
add_library(ThreeDsRecomp::RendererExtensionCore ALIAS
    renderer_extension_core)
set_property(TARGET renderer_extension_core PROPERTY CXX_STANDARD 20)
target_include_directories(renderer_extension_core PUBLIC ${INCLUDE_DIR})
if(MSVC)
    target_compile_definitions(renderer_extension_core PRIVATE NOMINMAX)
    target_compile_options(renderer_extension_core PRIVATE /utf-8)
endif()
source_group("renderer" FILES ${Source_Files__RendererExtensions})

#=================== Reusable 3DS/PICA renderer core ===================

set(Source_Files__Renderer3dsPica
    renderer3ds/pica_composition_schedule.cpp
    renderer3ds/pica_display_composition.cpp
    renderer3ds/pica_extension_schedule.cpp
    renderer3ds/pica_geometry_provider_authorization.cpp
    renderer3ds/pica_gl_shader_dialect.cpp
    renderer3ds/pica_scene_capabilities.cpp
    renderer3ds/pica_semantic_scene_view.cpp
)
add_library(renderer_3ds_pica_core STATIC
    ${Source_Files__Renderer3dsPica})
add_library(ThreeDsRecomp::Renderer3dsPicaCore ALIAS
    renderer_3ds_pica_core)
set_property(TARGET renderer_3ds_pica_core PROPERTY CXX_STANDARD 20)
target_include_directories(renderer_3ds_pica_core PUBLIC ${INCLUDE_DIR})
target_link_libraries(renderer_3ds_pica_core PUBLIC
    ThreeDsRecomp::RendererExtensionCore)
get_target_property(renderer_3ds_pica_core_direct_links
    renderer_3ds_pica_core LINK_LIBRARIES)
foreach(renderer_3ds_pica_core_direct_link
        IN LISTS renderer_3ds_pica_core_direct_links)
    string(TOLOWER "${renderer_3ds_pica_core_direct_link}"
        renderer_3ds_pica_core_direct_link_lower)
    if(renderer_3ds_pica_core_direct_link STREQUAL "three_ds_recomp_runtime" OR
       renderer_3ds_pica_core_direct_link_lower MATCHES "oot3d")
        message(FATAL_ERROR
            "renderer_3ds_pica_core must not link a title-specific or monolithic target: ${renderer_3ds_pica_core_direct_link}")
    endif()
endforeach()
if(MSVC)
    target_compile_definitions(renderer_3ds_pica_core PRIVATE NOMINMAX)
    target_compile_options(renderer_3ds_pica_core PRIVATE /utf-8)
endif()
source_group("renderer3ds" FILES ${Source_Files__Renderer3dsPica})

#=================== Reusable 3DS/PICA NRI execution ===================

if(THREE_DS_RECOMP_ENABLE_VULKAN)
    set(Source_Files__Renderer3dsNriPica
        renderer3ds/nri_pica_pipeline_bridge.cpp
        renderer3ds/vulkan_pipeline_cache_store.cpp
        renderer3ds/nri_pica_upload_arena.cpp
        renderer3ds/pica_nri_shader_contract.cpp
        renderer3ds/pica_nri_upload.cpp
    )
    add_library(renderer_3ds_nri_pica_execution STATIC
        ${Source_Files__Renderer3dsNriPica})
    add_library(ThreeDsRecomp::Renderer3dsNriPicaExecution ALIAS
        renderer_3ds_nri_pica_execution)
    set_property(TARGET renderer_3ds_nri_pica_execution
        PROPERTY CXX_STANDARD 20)
    target_include_directories(renderer_3ds_nri_pica_execution
        PUBLIC ${INCLUDE_DIR})
    target_compile_definitions(renderer_3ds_nri_pica_execution
        PUBLIC ENABLE_RENDERER3DS_VULKAN
        PRIVATE $<$<BOOL:${THREE_DS_RECOMP_ENABLE_NRI}>:ENABLE_RENDERER3DS_NRI>)
    target_link_libraries(renderer_3ds_nri_pica_execution
        PUBLIC Vulkan::Vulkan PRIVATE ThreeDsRecomp::RendererExtensionCore)
    if(THREE_DS_RECOMP_ENABLE_NRI)
        target_link_libraries(renderer_3ds_nri_pica_execution PRIVATE NRI)
    endif()
    if(MSVC)
        target_compile_definitions(renderer_3ds_nri_pica_execution
            PRIVATE NOMINMAX)
        target_compile_options(renderer_3ds_nri_pica_execution
            PRIVATE /utf-8)
    endif()
    source_group("renderer3ds/nri"
        FILES ${Source_Files__Renderer3dsNriPica})
endif()

#=================== OOT3D renderer modules ===================

file(GLOB Source_Files__Oot3dRenderer CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "oot3d/*.cpp")
list(FILTER Source_Files__Oot3dRenderer EXCLUDE REGEX
    "^oot3d/(pica_texture_decode|pica_fragment_lighting)\\.cpp$")
if(NOT THREE_DS_RECOMP_ENABLE_VULKAN)
    # This translation unit is Vulkan-only and deliberately has no stub
    # implementation.  Keep Vulkan headers out of OpenGL-only ports such as
    # Nintendo Switch.
    list(REMOVE_ITEM Source_Files__Oot3dRenderer
        "oot3d/pica_nri_pipeline_state.cpp"
        "oot3d/pica_pipeline_preparation.cpp"
        "oot3d/pica_guide_sampling_barriers.cpp")
endif()
source_group("oot3d" FILES ${Source_Files__Oot3dRenderer})
target_sources(three_ds_recomp_runtime PRIVATE ${Source_Files__Oot3dRenderer})
