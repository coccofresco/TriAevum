include(FetchContent)

# macOS builds must not depend on Homebrew or MacPorts: every library below is
# found if the publisher provides it, else fetched and built from source with
# the configured CMAKE_OSX_DEPLOYMENT_TARGET.

#=================== SDL2 ===================
find_package(SDL2 QUIET)
if (NOT ${SDL2_FOUND})
    # Real SDL2 (Homebrew's sdl2 is now sdl2-compat over SDL3).
    FetchContent_Declare(
        SDL2
        URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
        OVERRIDE_FIND_PACKAGE
    )
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL2)
endif()

#=================== nlohmann-json ===================
find_package(nlohmann_json QUIET)
if (NOT ${nlohmann_json_FOUND})
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        OVERRIDE_FIND_PACKAGE
    )
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

#=================== tinyxml2 ===================
find_package(tinyxml2 QUIET)
if (NOT ${tinyxml2_FOUND})
    set(tinyxml2_BUILD_TESTING OFF)
    FetchContent_Declare(
        tinyxml2
        GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
        GIT_TAG 11.0.0
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(tinyxml2)
endif()

#=================== spdlog ===================
find_package(spdlog QUIET)
if (NOT ${spdlog_FOUND})
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.16.0
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(spdlog)
endif()

#=================== libzip ===================
find_package(libzip QUIET)
if (NOT ${libzip_FOUND})
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_TOOLS OFF)
    set(BUILD_REGRESS OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_DOC OFF)
    set(BUILD_OSSFUZZ OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(ENABLE_COMMONCRYPTO OFF)
    set(ENABLE_GNUTLS OFF)
    set(ENABLE_MBEDTLS OFF)
    set(ENABLE_OPENSSL OFF)
    set(ENABLE_LZMA OFF)
    set(ENABLE_ZSTD OFF)
    FetchContent_Declare(
        libzip
        GIT_REPOSITORY https://github.com/nih-at/libzip.git
        GIT_TAG v1.11.4
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(libzip)
    list(APPEND ADDITIONAL_LIB_INCLUDES ${libzip_SOURCE_DIR}/lib ${libzip_BINARY_DIR})
endif()

#=================== Metal-cpp ===================
FetchContent_Declare(
    metalcpp
    GIT_REPOSITORY https://github.com/briaguya-ai/single-header-metal-cpp.git
    GIT_TAG macOS13_iOS16
)
FetchContent_MakeAvailable(metalcpp)
list(APPEND ADDITIONAL_LIB_INCLUDES ${metalcpp_SOURCE_DIR})

#=================== ImGui ===================
target_sources(ImGui
    PRIVATE
    ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
)

target_include_directories(ImGui PRIVATE ${metalcpp_SOURCE_DIR})
target_compile_definitions(ImGui PUBLIC IMGUI_IMPL_METAL_CPP)

target_link_libraries(ImGui PUBLIC SDL2::SDL2)

# The desktop OpenGL backend is optional on macOS (Vulkan/MoltenVK and Metal
# are the Apple paths); GLEW is needed only when it is built.
find_package(GLEW QUIET)
if (GLEW_FOUND)
    target_link_libraries(ImGui PUBLIC ${OPENGL_opengl_LIBRARY} GLEW::GLEW)
endif()
set_target_properties(ImGui PROPERTIES
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
)
