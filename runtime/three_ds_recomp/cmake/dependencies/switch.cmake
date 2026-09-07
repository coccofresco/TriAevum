# Dependencies supplied by devkitPro's Switch portlibs. Keep this layer small:
# gameplay remains in the generated whole-AOT archive and rendering uses the
# reusable SDL2/Mesa OpenGL backend.
include(FetchContent)

find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        OVERRIDE_FIND_PACKAGE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# The portlibs package does not currently export a tinyxml2 CMake target.
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
find_package(tinyxml2 QUIET)
if(NOT tinyxml2_FOUND)
    FetchContent_Declare(
        tinyxml2
        GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
        GIT_TAG 10.0.0
        OVERRIDE_FIND_PACKAGE)
    FetchContent_MakeAvailable(tinyxml2)
endif()

# libzip has no Switch portlib. It cross-compiles against switch-zlib when
# host tools and optional crypto/compression backends are disabled.
find_package(libzip QUIET)
if(NOT libzip_FOUND)
    set(BUILD_DOC OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_REGRESS OFF CACHE BOOL "" FORCE)
    set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(ENABLE_BZIP2 OFF CACHE BOOL "" FORCE)
    set(ENABLE_LZMA OFF CACHE BOOL "" FORCE)
    set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_GNUTLS OFF CACHE BOOL "" FORCE)
    set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libzip
        GIT_REPOSITORY https://github.com/nih-at/libzip.git
        GIT_TAG v1.11.4
        OVERRIDE_FIND_PACKAGE)
    FetchContent_MakeAvailable(libzip)
endif()

find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1
        OVERRIDE_FIND_PACKAGE)
    FetchContent_MakeAvailable(spdlog)
endif()
if(TARGET spdlog)
    target_compile_definitions(spdlog PRIVATE
        _POSIX_C_SOURCE=200809L
        SPDLOG_PREVENT_CHILD_FD)
endif()

find_package(SDL2 REQUIRED)
target_link_libraries(ImGui PUBLIC SDL2::SDL2)

find_library(GLAD_LIBRARY glad
    HINTS "${DEVKITPRO}/portlibs/switch/lib" REQUIRED)
find_library(EGL_LIBRARY EGL
    HINTS "${DEVKITPRO}/portlibs/switch/lib" REQUIRED)
find_library(GLAPI_LIBRARY glapi
    HINTS "${DEVKITPRO}/portlibs/switch/lib" REQUIRED)
find_library(DRM_NOUVEAU_LIBRARY drm_nouveau
    HINTS "${DEVKITPRO}/portlibs/switch/lib" REQUIRED)

# ImGui and Fast3D must share the same loader on Horizon OS.
target_compile_definitions(ImGui PRIVATE
    IMGUI_IMPL_OPENGL_LOADER_CUSTOM
    IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
target_compile_options(ImGui PRIVATE -include glad/glad.h)
target_include_directories(ImGui PRIVATE
    "${DEVKITPRO}/portlibs/switch/include")
target_link_libraries(ImGui PUBLIC ${GLAD_LIBRARY})

# newlib hides POSIX declarations in strict modes. Horizon also has no spdlog
# child-fd path based on fcntl.
add_compile_definitions(
    _POSIX_C_SOURCE=200809L
    SPDLOG_PREVENT_CHILD_FD)
