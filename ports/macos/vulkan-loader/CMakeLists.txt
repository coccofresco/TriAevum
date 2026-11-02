cmake_minimum_required(VERSION 3.30)
# Publisher-only dependency build. No system installation is performed here.
project(TriAevumMacVulkanLoader LANGUAGES C)
if(NOT APPLE OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "Configure on macOS with -DCMAKE_OSX_ARCHITECTURES=arm64")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../PathRemapping.cmake")

# The upstream loader embeds its logical installation/search directories in
# generated C strings. Compiler prefix maps do not rewrite those strings.
# Keep the logical prefix public; publishers copy the built dylib themselves.
set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Logical loader search prefix (do not install)" FORCE)
set(CMAKE_INSTALL_SYSCONFDIR "etc" CACHE PATH "Logical system configuration directory" FORCE)
set(CMAKE_INSTALL_DATADIR "share" CACHE PATH "Logical shared data directory" FORCE)
set(CMAKE_INSTALL_LIBDIR "lib" CACHE PATH "Logical library directory" FORCE)
set(SYSCONFDIR "/etc" CACHE STRING "System-wide loader configuration directory" FORCE)
set(FALLBACK_CONFIG_DIRS "/etc/xdg" CACHE STRING "Default configuration search path" FORCE)
set(FALLBACK_DATA_DIRS "/usr/local/share:/usr/share" CACHE STRING "Default data search path" FORCE)
set(LOADER_ENABLE_ADDRESS_SANITIZER OFF CACHE BOOL "" FORCE)
set(LOADER_ENABLE_THREAD_SANITIZER OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(UPDATE_DEPS OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

# Keep the build and donor notice pins synchronized. Changing a pin requires
# reviewing its source archive hash and the corresponding license snapshot.
set(loader_revision "aa6dee8385afe4b2cc0152d086af2184743e16fd")
set(headers_revision "9fbc2a782ee3f8a4db64e7d991e23da75e893f44")
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../../LICENSES/MacOS-donor-notices.json" donor_notices)
string(FIND "${donor_notices}" "/Vulkan-Loader/${loader_revision}/LICENSE.txt" loader_notice)
string(FIND "${donor_notices}" "/Vulkan-Headers/${headers_revision}/LICENSE.md" headers_notice)
if(loader_notice LESS 0 OR headers_notice LESS 0)
    message(FATAL_ERROR "Vulkan dependency pins differ from the reviewed donor notices")
endif()

include(FetchContent)
FetchContent_Declare(VulkanHeaders
    URL "https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/${headers_revision}"
    URL_HASH SHA256=48db7ca4b0d1a089e11098da9ceef8164d1efae6db5faa65fe8f1587f548ac08)
FetchContent_MakeAvailable(VulkanHeaders)
FetchContent_Declare(VulkanLoader
    URL "https://codeload.github.com/KhronosGroup/Vulkan-Loader/tar.gz/${loader_revision}"
    URL_HASH SHA256=bb5a97940f699001ef9b324e6eb38b15e3bee64168d51df246dbe8de2e082a2e)
FetchContent_MakeAvailable(VulkanLoader)
