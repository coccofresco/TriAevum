include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/cvars.cmake)

add_library(three_ds_recomp_runtime STATIC)
add_library(ThreeDsRecomp::Runtime ALIAS three_ds_recomp_runtime)
set_target_properties(three_ds_recomp_runtime PROPERTIES PREFIX "")
set_property(TARGET three_ds_recomp_runtime PROPERTY CXX_STANDARD 20)
# Need to set C11 for the optional GBI disassembler (_Alignas).
set_property(TARGET three_ds_recomp_runtime PROPERTY C_STANDARD 11)
set_target_properties(three_ds_recomp_runtime PROPERTIES ENABLE_EXPORTS TRUE)
set_target_properties(three_ds_recomp_runtime PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../include)
option(USE_OPENGLES "Enable GLES3" OFF)
option(GFX_DEBUG_DISASSEMBLER "Enable libgfxd" OFF)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
use_props(${PROJECT_NAME} "${CMAKE_CONFIGURATION_TYPES}" "${DEFAULT_CXX_PROPS}")
endif()

#=================== Top-Level ===================

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ship/install_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/ship/install_config.h @ONLY)

if(ENABLE_SCRIPTING)
    file(GLOB SCRIPT_PUB_KEYS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../keys/script/*.pem")
    set(EMBEDDED_KEYS_HEADER ${CMAKE_CURRENT_BINARY_DIR}/ship/DefaultKeys.h)
    set(GENERATE_KEYS_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_keys_header.py)
    get_filename_component(EMBEDDED_KEYS_DIR ${EMBEDDED_KEYS_HEADER} DIRECTORY)

    add_custom_command(
        OUTPUT ${EMBEDDED_KEYS_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${EMBEDDED_KEYS_DIR}
        COMMAND Python3::Interpreter ${GENERATE_KEYS_SCRIPT}
                ${EMBEDDED_KEYS_HEADER}
                ${SCRIPT_PUB_KEYS}
        DEPENDS ${GENERATE_KEYS_SCRIPT}
                ${SCRIPT_PUB_KEYS}
        COMMENT "Generating DefaultKeys.h from all keys in /keys/script/"
        VERBATIM
    )

    add_custom_target(GenerateScriptKeys DEPENDS ${EMBEDDED_KEYS_HEADER})
    add_dependencies(three_ds_recomp_runtime GenerateScriptKeys)
endif()

#=================== Fast ===================

add_subdirectory("fast")

#=================== Legacy compatibility ===================

add_subdirectory("legacy_compat")

#=================== Ship ===================

add_subdirectory("ship")


if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/ship/utils/AppleFolderManager.mm PROPERTIES COMPILE_FLAGS -fno-objc-arc)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/ship/utils/macUtils.mm PROPERTIES COMPILE_FLAGS -fno-objc-arc)
endif()

#=================== Packages & Includes ===================

target_include_directories(three_ds_recomp_runtime
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${INCLUDE_DIR} ${ADDITIONAL_LIB_INCLUDES}
    # The interface of the separately licensed iMMERSE module (MIT).
    ${CMAKE_CURRENT_SOURCE_DIR}/../module_interface
)

#=================== Linking ===================
if(INCLUDE_MPQ_SUPPORT)
    if (NOT CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_link_libraries(three_ds_recomp_runtime PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,storm>")
    else()
        target_link_libraries(three_ds_recomp_runtime PUBLIC storm)
    endif()
endif()

target_link_libraries(three_ds_recomp_runtime
    PUBLIC ImGui
    PRIVATE
        ThreeDsRecomp::RendererExtensionCore
        ThreeDsRecomp::Renderer3dsPicaCore
        oot3d_renderer_texture_decode
        oot3d_renderer_fragment_lighting
)
if(TARGET renderer_3ds_nri_pica_execution)
    target_link_libraries(three_ds_recomp_runtime PRIVATE
        ThreeDsRecomp::Renderer3dsNriPicaExecution)
endif()

target_link_libraries(three_ds_recomp_runtime PRIVATE stb)

if (GFX_DEBUG_DISASSEMBLER)
    target_link_libraries(three_ds_recomp_runtime PRIVATE libgfxd)
endif()

find_package(libzip REQUIRED)
target_link_libraries(three_ds_recomp_runtime PRIVATE libzip::zip)

find_package(ZLIB REQUIRED)
target_link_libraries(three_ds_recomp_runtime PRIVATE ZLIB::ZLIB)

find_package(nlohmann_json REQUIRED)
target_link_libraries(three_ds_recomp_runtime PUBLIC nlohmann_json::nlohmann_json)

find_package(tinyxml2 REQUIRED)
target_link_libraries(three_ds_recomp_runtime PUBLIC tinyxml2::tinyxml2)

find_package(spdlog REQUIRED)
target_link_libraries(three_ds_recomp_runtime PUBLIC spdlog::spdlog)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(three_ds_recomp_runtime PRIVATE Threads::Threads)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_library(COCOA Cocoa)
    target_link_libraries(three_ds_recomp_runtime PRIVATE ${COCOA})

endif()

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    target_include_directories(three_ds_recomp_runtime PUBLIC
        "${DEVKITPRO}/portlibs/switch/include")
    target_link_libraries(three_ds_recomp_runtime PRIVATE
        ${GLAD_LIBRARY}
        ${EGL_LIBRARY}
        ${GLAPI_LIBRARY}
        ${DRM_NOUVEAU_LIBRARY})
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    find_Library(OSX_FOUNDATION Foundation)
    find_Library(OSX_AVFOUNDATION AVFoundation)
    find_library(METAL Metal)
    find_library(QUARTZCORE QuartzCore)
    find_library(COREAUDIO CoreAudio)
    find_library(AUDIOTOOLBOX AudioToolbox)
    target_link_libraries(three_ds_recomp_runtime PRIVATE ${OSX_FOUNDATION} ${OSX_AVFOUNDATION} ${METAL} ${QUARTZCORE} ${COREAUDIO} ${AUDIOTOOLBOX} ${CMAKE_DL_LIBS})
endif()

if (USE_OPENGLES)
    if (CMAKE_SYSTEM_NAME STREQUAL "Android")
        find_library(OPENGLES_LIBRARY GLESv3)
        target_link_libraries(three_ds_recomp_runtime PRIVATE GLESv3)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
        target_link_libraries(three_ds_recomp_runtime PRIVATE OpenGL::GLES2)
    else()
        find_library(OPENGLES_LIBRARY GLESv2)
        target_link_libraries(three_ds_recomp_runtime PRIVATE GLESv2)
    endif()
endif()

target_link_libraries(three_ds_recomp_runtime PUBLIC prism monocypher)

if(THREE_DS_RECOMP_ENABLE_VULKAN)
    target_sources(three_ds_recomp_runtime PRIVATE fast/renderer/shaderc_compiler.cpp)
    # Screen-space diffuse GI: the title-neutral module and runtime pass
    # (docs/TRIAEVUM_SCREEN_SPACE_GI.md) behind the OOT3D provider
    # (fast/oot3d/screen_space_gi_provider.cpp).
    target_sources(three_ds_recomp_runtime PRIVATE
        fast/renderer/diffuse_gi_reference.cpp
        fast/renderer/diffuse_gi_gather.cpp
        fast/renderer/diffuse_gi_gather_shader.cpp
        fast/renderer/diffuse_gi_hierarchy.cpp
        fast/renderer/diffuse_gi_guiding.cpp
        fast/renderer/diffuse_gi_noise.cpp
        fast/renderer/diffuse_gi_m4_shader.cpp
        fast/renderer/diffuse_gi_temporal.cpp
        fast/renderer/diffuse_gi_temporal_shader.cpp
        fast/renderer/diffuse_gi_pass_shader.cpp
        fast/renderer/diffuse_gi_pass_plan.cpp
        fast/renderer/diffuse_gi_pass.cpp)
    # Precompiled SPIR-V families, embedded from their packs.
    include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/spirv_pack.cmake)
    foreach(_pack renderer/builtin_pass renderer3ds/pica_native_fragment oot3d/pica_temporal_fragment
                  oot3d/pica_toon_fragment oot3d/pica_uber_fragment oot3d/pica_caster_fragment)
        triaevum_add_spirv_pack(three_ds_recomp_runtime fast/${_pack}_binaries.cpp fast/${_pack}.spvpack)
    endforeach()
    target_link_libraries(three_ds_recomp_runtime PRIVATE ${CMAKE_DL_LIBS})
    target_link_libraries(three_ds_recomp_runtime PUBLIC Vulkan::Vulkan PRIVATE three_ds_recomp_shaderc)
    if(THREE_DS_RECOMP_ENABLE_NRI)
        target_link_libraries(three_ds_recomp_runtime PRIVATE NRI)
    endif()
    if(THREE_DS_RECOMP_ENABLE_SSSR)
        target_link_libraries(three_ds_recomp_runtime PRIVATE
            oot3d_ffx_sssr
            oot3d_ffx_sssr_backend_vk)
    endif()
    if(THREE_DS_RECOMP_ENABLE_BRIXELIZER_GI)
        target_link_libraries(three_ds_recomp_runtime PRIVATE
            oot3d_ffx_brixelizergi)
        target_compile_definitions(three_ds_recomp_runtime PRIVATE
            ENABLE_OOT3D_BRIXELIZER_GI)
    endif()
    if(THREE_DS_RECOMP_OFFER_EXPERIMENTAL_GI)
        target_compile_definitions(three_ds_recomp_runtime PRIVATE
            TRIAEVUM_OFFER_EXPERIMENTAL_GI)
    endif()
    if(TARGET oot3d_cacao)
        target_link_libraries(three_ds_recomp_runtime PRIVATE oot3d_cacao)
        target_compile_definitions(three_ds_recomp_runtime PRIVATE ENABLE_OOT3D_CACAO)
    endif()
    # The iMMERSE module is under its own licence (fast/immerse/LICENSE.md):
    # a separate shared library the runtime loads when it is installed.
    if(THREE_DS_RECOMP_ENABLE_IMMERSE)
        include(${CMAKE_CURRENT_SOURCE_DIR}/fast/immerse/immerse.cmake)
    endif()
    # The DLSS module links NVIDIA's NGX SDK under NVIDIA's licence
    # (fast/dlss/LICENSE.md): a separate shared library the runtime loads when
    # it is installed. The runtime links nothing of NGX.
    if(THREE_DS_RECOMP_ENABLE_DLSS_MODULE)
        include(${CMAKE_CURRENT_SOURCE_DIR}/fast/dlss/dlss.cmake)
    endif()
    if(THREE_DS_RECOMP_ENABLE_D3D12_NGX_PROVIDER)
        target_link_libraries(three_ds_recomp_runtime PRIVATE d3d12 dxgi)
    endif()
endif()

if(ENABLE_SCRIPTING)
    target_link_libraries(three_ds_recomp_runtime PUBLIC libtcc libtcc1)
    target_compile_definitions(three_ds_recomp_runtime PUBLIC ENABLE_SCRIPTING)
endif()

#=================== Compile Options & Defs ===================

target_compile_definitions(three_ds_recomp_runtime PRIVATE ${GBI_UCODE})

# The Vulkan renderer is supported outside Windows as well.  Keep its feature
# macros tied to the CMake options instead of the host platform so Linux builds
# register the backend that was compiled and linked above.
target_compile_definitions(three_ds_recomp_runtime PRIVATE
    $<$<BOOL:${THREE_DS_RECOMP_ENABLE_VULKAN}>:ENABLE_OOT3D_VULKAN>
    $<$<BOOL:${THREE_DS_RECOMP_ENABLE_NRI}>:ENABLE_OOT3D_NRI>
    $<$<BOOL:${THREE_DS_RECOMP_ENABLE_VULKAN}>:ENABLE_RENDERER3DS_VULKAN>
    $<$<BOOL:${THREE_DS_RECOMP_ENABLE_NRI}>:ENABLE_RENDERER3DS_NRI>
    $<$<BOOL:${THREE_DS_RECOMP_ENABLE_D3D12_NGX_PROVIDER}>:ENABLE_OOT3D_D3D12_NGX_PROVIDER>
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32
        _CONSOLE
        _CRT_SECURE_NO_WARNINGS
        $<$<BOOL:${THREE_DS_RECOMP_ENABLE_DX11}>:ENABLE_DX11>
        NOMINMAX
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    )
endif()

set(SPDLOG_LEVEL_TRACE 0)
set(SPDLOG_LEVEL_DEBUG 1)
set(SPDLOG_LEVEL_INFO 2)
set(SPDLOG_LEVEL_WARN 3)
set(SPDLOG_LEVEL_ERROR 4)
set(SPDLOG_LEVEL_CRITICAL 5)
set(SPDLOG_LEVEL_OFF 6)
set(LOG_LEVELS "SPDLOG_LEVEL_TRACE;SPDLOG_LEVEL_DEBUG;SPDLOG_LEVEL_INFO;SPDLOG_LEVEL_WARN;SPDLOG_LEVEL_ERROR;SPDLOG_LEVEL_CRITICAL;SPDLOG_LEVEL_OFF")
set(SPDLOG_MIN_CUTOFF SPDLOG_LEVEL_TRACE CACHE STRING "The minimum log level that will be available to a build. Can be overridden in cmake or in InitLogging")
set_property(CACHE SPDLOG_MIN_CUTOFF PROPERTY STRINGS ${LOG_LEVELS})
if(NOT SPDLOG_MIN_CUTOFF IN_LIST LOG_LEVELS)
    message(FATAL_ERROR "SPDLOG_MIN_CUTOFF must be one of ${LOG_LEVELS}")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(three_ds_recomp_runtime PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
        SPDLOG_ACTIVE_LEVEL=${SPDLOG_MIN_CUTOFF}
        __IOS__
    )

    set_xcode_property(${PROJECT_NAME} PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID} All)

    if(NOT SIGN_LIBRARY)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        )

        if(TARGET SDL2)
            set_target_properties(SDL2 PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
                XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
            )
        endif()

        set_target_properties(zip PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        )
    endif()
else()
    target_compile_definitions(three_ds_recomp_runtime PRIVATE
        ENABLE_OPENGL
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
        SPDLOG_ACTIVE_LEVEL=${SPDLOG_MIN_CUTOFF}
        $<$<BOOL:${USE_OPENGLES}>:USE_OPENGLES>
        $<$<BOOL:${GFX_DEBUG_DISASSEMBLER}>:GFX_DEBUG_DISASSEMBLER>
    )
endif()

if(MSVC)
    target_compile_options(three_ds_recomp_runtime PRIVATE
        $<$<CONFIG:Debug>:
            /Od;
            /Oi-
        >
        $<$<CONFIG:Release>:
            /Oi;
            /Gy
        >
        /permissive-;
        /MP;
        /sdl;
        /W3;
        ${DEFAULT_CXX_EXCEPTION_HANDLING};
    )
    if(NOT THREE_DS_RECOMP_FAST_DEV_LINK)
        target_compile_options(three_ds_recomp_runtime PRIVATE ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT})
    endif()
    target_link_options(three_ds_recomp_runtime PRIVATE
        $<$<CONFIG:Release>:
            /OPT:REF;
            /OPT:ICF
        >
        /SUBSYSTEM:CONSOLE
    )
endif()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU|Clang")
	target_compile_options(three_ds_recomp_runtime PRIVATE
		-Wall
		-Wextra
		-Wno-error
		-Wno-unused-variable
		-Wno-unused-parameter
		-Wno-unused-function
		-Wno-parentheses
		-Wno-narrowing
		-Wno-missing-field-initializers
        $<$<COMPILE_LANGUAGE:C,OBJC>:-Wno-implicit-function-declaration>
        $<$<COMPILE_LANGUAGE:C,OBJC>:-Wno-int-conversion>
	)
    if (MSVC)
    target_compile_options(three_ds_recomp_runtime PRIVATE
        -Wno-implicit-function-declaration
    )
    endif()
endif()
