cmake_minimum_required(VERSION 3.24.0)

# Enable /Z7 (embedded debug info) instead of /Zi (shared PDB) on MSVC so that
# sccache can cache object files without racing over a shared .pdb file.
# CMP0141 was introduced in CMake 3.25, so guard it for older versions.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

option(NON_PORTABLE "Build a non-portable version" OFF)
option(DISABLE_DLL_LOADER "Disable dynamic library loading support" OFF)
option(ENABLE_SCRIPTING "Enable TCC-based C mod compilation and scripting system" OFF)
option(THREE_DS_RECOMP_FAST_DEV_LINK "Disable MSVC debug data and large linker PDBs for iterative builds" OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    option(SIGN_LIBRARY "Enable xcode signing" OFF)
    option(BUNDLE_ID "Bundle ID for xcode signing" "com.example.threedsrecomp")
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
endif()

project(three_ds_recomp_runtime LANGUAGES C CXX)

set(THREE_DS_RECOMP_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
	CACHE INTERNAL "three_ds_recomp CMake module directory")

add_library(oot3d_renderer_contracts INTERFACE)
target_include_directories(oot3d_renderer_contracts INTERFACE
	"${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(oot3d_renderer_texture_decode STATIC
	src/fast/oot3d/pica_texture_decode.cpp)
set_property(TARGET oot3d_renderer_texture_decode PROPERTY CXX_STANDARD 20)
target_include_directories(oot3d_renderer_texture_decode PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(oot3d_renderer_fragment_lighting STATIC
	src/fast/oot3d/pica_fragment_lighting.cpp)
set_property(TARGET oot3d_renderer_fragment_lighting PROPERTY CXX_STANDARD 20)
target_include_directories(oot3d_renderer_fragment_lighting PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/include")

set(OOT3D_NATIVE_ASSET_RUNTIME_SOURCE_NAMES
	Oot3dAssetCatalog.cpp
	Oot3dCutsceneProvider.cpp
	Oot3dEngineCapabilities.cpp
	Oot3dNativeActorRenderProvider.cpp
	Oot3dNativeAssets.cpp
	Oot3dNativeDemoScene.cpp
	Oot3dNativeEffectSs.cpp
	Oot3dNativeFormat.cpp
	Oot3dNativeQdbProvider.cpp
	Oot3dNativeRenderer.cpp
	Oot3dNativeRenderScene.cpp
	Oot3dNativeResourceContract.cpp
	Oot3dNativeRoomRenderProvider.cpp
	Oot3dNativeSceneEntryProvider.cpp
	Oot3dNativeSceneEnvironmentProvider.cpp
	Oot3dNativeSceneRenderProvider.cpp
	Oot3dNativeSourceProvider.cpp
	Oot3dSceneProvider.cpp
	Oot3dSemanticRouteCatalog.cpp)
set(OOT3D_NATIVE_ASSET_RUNTIME_SOURCES
	${OOT3D_NATIVE_ASSET_RUNTIME_SOURCE_NAMES})
list(TRANSFORM OOT3D_NATIVE_ASSET_RUNTIME_SOURCES PREPEND
	"${CMAKE_CURRENT_SOURCE_DIR}/src/oot3d/")
add_library(oot3d_native_asset_runtime STATIC
	${OOT3D_NATIVE_ASSET_RUNTIME_SOURCES})
set_property(TARGET oot3d_native_asset_runtime PROPERTY CXX_STANDARD 20)
target_include_directories(oot3d_native_asset_runtime PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(oot3d_native_asset_runtime PRIVATE
	oot3d_renderer_texture_decode)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)
    set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS} -fobjc-arc")
    set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	# Default to embedded debug info (/Z7) so sccache can cache object files
	# without multiple CL.EXE processes racing over a shared .pdb file.
	if(POLICY CMP0141 AND NOT THREE_DS_RECOMP_FAST_DEV_LINK)
		set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:Embedded>")
	elseif(POLICY CMP0141)
		set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "")
	endif()
endif()

include(cmake/Utils.cmake)
include(CMakeDependentOption)
set(ADDITIONAL_LIB_INCLUDES "")

# ========= Configuration Options =========
option(INCLUDE_MPQ_SUPPORT "Enable StormLib and MPQ archive support" OFF)
set(GBI_UCODE F3DEX_GBI_2 CACHE STRING "Specify the GBI ucode version")
option(THREE_DS_RECOMP_ENABLE_DX11 "Build the legacy Direct3D 11 backend on Windows" OFF)
option(THREE_DS_RECOMP_ENABLE_VULKAN "Build the native 3DS Vulkan backend" OFF)
option(THREE_DS_RECOMP_ENABLE_NRI "Enable NVIDIA NRI interop for the native 3DS Vulkan backend" ${THREE_DS_RECOMP_ENABLE_VULKAN})
cmake_dependent_option(THREE_DS_RECOMP_ENABLE_NRI_UPSCALER_SDKS
	"Compile the optional NRI desktop upscaler SDK integrations" ON "NOT ANDROID" OFF)
cmake_dependent_option(
	THREE_DS_RECOMP_ENABLE_D3D12_NGX_PROVIDER
	"Expose an official NGX D3D12 provider beside the Vulkan renderer"
	ON
	"WIN32;THREE_DS_RECOMP_ENABLE_VULKAN;THREE_DS_RECOMP_ENABLE_NRI"
	OFF)
option(THREE_DS_RECOMP_ENABLE_CACAO "Enable FidelityFX CACAO for the native 3DS Vulkan backend" ${THREE_DS_RECOMP_ENABLE_NRI})
option(THREE_DS_RECOMP_ENABLE_SSSR "Enable FidelityFX SSSR for the native 3DS Vulkan backend" ${THREE_DS_RECOMP_ENABLE_NRI})
set(THREE_DS_RECOMP_NRI_SOURCE_DIR "" CACHE PATH "Optional existing NRI v180 source directory")
set(THREE_DS_RECOMP_CACAO_SOURCE_DIR "" CACHE PATH "Optional existing FidelityFX CACAO v1.2 source directory")
set(THREE_DS_RECOMP_NGX_VERSION "310.7.0" CACHE STRING
	"Official NVIDIA DLSS/NGX SDK version used by the NRI Vulkan backend")
set(THREE_DS_RECOMP_DLSS_RUNTIME "" CACHE FILEPATH
	"Optional official NVIDIA nvngx_dlss.dll; must match THREE_DS_RECOMP_NGX_VERSION")
# =========== Dependencies =============
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(cmake/dependencies/windows-vcpkg.cmake)
endif()

include(cmake/dependencies/common.cmake)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    include(cmake/dependencies/switch.cmake)
endif()

if(THREE_DS_RECOMP_ENABLE_VULKAN)
	if(ANDROID)
		include(cmake/dependencies/android-vulkan.cmake)
	endif()
	find_package(Vulkan REQUIRED)
	target_sources(ImGui PRIVATE "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
	target_link_libraries(ImGui PUBLIC Vulkan::Vulkan)
	if(NOT TARGET three_ds_recomp_shaderc)
		find_path(THREE_DS_RECOMP_SHADERC_INCLUDE_DIR
			NAMES shaderc/shaderc.hpp
			HINTS "$ENV{VULKAN_SDK}"
			PATH_SUFFIXES Include include
			REQUIRED
		)
		set(THREE_DS_RECOMP_SHADERC_LIBRARY_NAMES shaderc_shared)
		if(UNIX)
			# Debian names the shared C API library libshaderc.so.
			list(APPEND THREE_DS_RECOMP_SHADERC_LIBRARY_NAMES shaderc)
		endif()
		find_library(THREE_DS_RECOMP_SHADERC_LIBRARY
			NAMES ${THREE_DS_RECOMP_SHADERC_LIBRARY_NAMES}
			HINTS "$ENV{VULKAN_SDK}"
			PATH_SUFFIXES Lib lib
			REQUIRED
		)
		if(WIN32)
			find_file(THREE_DS_RECOMP_SHADERC_RUNTIME
				NAMES shaderc_shared.dll
				HINTS "$ENV{VULKAN_SDK}"
				PATH_SUFFIXES Bin bin
				REQUIRED
			)
			add_library(three_ds_recomp_shaderc SHARED IMPORTED GLOBAL)
			set_target_properties(three_ds_recomp_shaderc PROPERTIES
				IMPORTED_IMPLIB "${THREE_DS_RECOMP_SHADERC_LIBRARY}"
				IMPORTED_LOCATION "${THREE_DS_RECOMP_SHADERC_RUNTIME}"
				INTERFACE_INCLUDE_DIRECTORIES "${THREE_DS_RECOMP_SHADERC_INCLUDE_DIR}"
			)
		else()
			add_library(three_ds_recomp_shaderc UNKNOWN IMPORTED GLOBAL)
			set_target_properties(three_ds_recomp_shaderc PROPERTIES
				IMPORTED_LOCATION "${THREE_DS_RECOMP_SHADERC_LIBRARY}"
				INTERFACE_INCLUDE_DIRECTORIES "${THREE_DS_RECOMP_SHADERC_INCLUDE_DIR}"
			)
		endif()
	endif()

	if(THREE_DS_RECOMP_ENABLE_NRI)
		include(FetchContent)
		# Some parent builds leave CMAKE_CONFIGURATION_TYPES populated even with the
		# single-config Ninja generator. ShaderMake otherwise predicts a
		# non-existent build/Release/ShaderMake.exe path.
		if(NOT CMAKE_GENERATOR MATCHES "Visual Studio|Xcode|Multi-Config")
			set(CMAKE_CONFIGURATION_TYPES "")
		endif()
		set(NRI_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
		set(NRI_ENABLE_VK_SUPPORT ON CACHE BOOL "" FORCE)
		set(NRI_ENABLE_VALIDATION_SUPPORT ON CACHE BOOL "" FORCE)
		set(NRI_ENABLE_D3D11_SUPPORT OFF CACHE BOOL "" FORCE)
		set(NRI_ENABLE_D3D12_SUPPORT
			${THREE_DS_RECOMP_ENABLE_D3D12_NGX_PROVIDER}
			CACHE BOOL "" FORCE)
		set(NRI_ENABLE_NONE_SUPPORT OFF CACHE BOOL "" FORCE)
		set(NRI_ENABLE_NVTX_SUPPORT OFF CACHE BOOL "" FORCE)
		set(NRI_ENABLE_DEBUG_NAMES_AND_ANNOTATIONS ON CACHE BOOL "" FORCE)
		# Keep every NRIUpscaler provider independently capability-gated at
		# runtime even though their SDK adapters are compiled together here.
		set(NRI_ENABLE_NIS_SDK ${THREE_DS_RECOMP_ENABLE_NRI_UPSCALER_SDKS} CACHE BOOL "" FORCE)
		set(NRI_ENABLE_FFX_SDK ${THREE_DS_RECOMP_ENABLE_NRI_UPSCALER_SDKS} CACHE BOOL "" FORCE)
		set(NRI_ENABLE_NGX_SDK ${THREE_DS_RECOMP_ENABLE_NRI_UPSCALER_SDKS} CACHE BOOL "" FORCE)
		if(THREE_DS_RECOMP_NRI_SOURCE_DIR)
			FetchContent_Declare(nri SOURCE_DIR "${THREE_DS_RECOMP_NRI_SOURCE_DIR}")
			execute_process(
				COMMAND ${CMAKE_COMMAND}
					-DNRI_SOURCE_DIR=${THREE_DS_RECOMP_NRI_SOURCE_DIR}
					-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/nri_android_platform.cmake
				COMMAND ${CMAKE_COMMAND}
					-DNRI_SOURCE_DIR=${THREE_DS_RECOMP_NRI_SOURCE_DIR}
					-DNGX_VERSION=${THREE_DS_RECOMP_NGX_VERSION}
					-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/oot3d_nri_ngx_version.cmake
				COMMAND ${CMAKE_COMMAND}
					-DNRI_SOURCE_DIR=${THREE_DS_RECOMP_NRI_SOURCE_DIR}
					-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/oot3d_nri_dlss_capability.cmake
				COMMAND ${CMAKE_COMMAND}
					-DNRI_SOURCE_DIR=${THREE_DS_RECOMP_NRI_SOURCE_DIR}
					-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/oot3d_nri_vulkan12_shaders.cmake
				COMMAND ${CMAKE_COMMAND}
					-DNRI_SOURCE_DIR=${THREE_DS_RECOMP_NRI_SOURCE_DIR}
					-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/oot3d_nri_vulkan12_depth_resolve.cmake
				COMMAND_ERROR_IS_FATAL ANY)
		else()
			FetchContent_Declare(nri
				GIT_REPOSITORY https://github.com/NVIDIA-RTX/NRI.git
				GIT_TAG 4b485316463969f182db15e67aad2aec2f40a3d7
				GIT_SHALLOW FALSE
				PATCH_COMMAND ${CMAKE_COMMAND}
					-DNRI_SOURCE_DIR=<SOURCE_DIR>
					-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/nri_android_platform.cmake
				COMMAND ${CMAKE_COMMAND}
					-DNRI_SOURCE_DIR=<SOURCE_DIR>
					-DNGX_VERSION=${THREE_DS_RECOMP_NGX_VERSION}
					-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/oot3d_nri_ngx_version.cmake
				COMMAND ${CMAKE_COMMAND}
					-DNRI_SOURCE_DIR=<SOURCE_DIR>
					-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/oot3d_nri_dlss_capability.cmake
				COMMAND ${CMAKE_COMMAND}
					-DNRI_SOURCE_DIR=<SOURCE_DIR>
					-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/oot3d_nri_vulkan12_shaders.cmake
				COMMAND ${CMAKE_COMMAND}
					-DNRI_SOURCE_DIR=<SOURCE_DIR>
					-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/oot3d_nri_vulkan12_depth_resolve.cmake
			)
		endif()
		FetchContent_MakeAvailable(nri)
		if(WIN32 AND NRI_STATIC_LIBRARY AND TARGET NRI)
			# VERSIONINFO belongs to a final DLL/EXE. Keeping NRI.rc in the
			# static archive also makes Ninja depend on the caller's ambient
			# Windows SDK INCLUDE environment, which is absent in hermetic
			# clang-cl builds.
			get_target_property(_oot3d_nri_sources NRI SOURCES)
			list(FILTER _oot3d_nri_sources EXCLUDE REGEX "NRI\\.rc$")
			set_property(TARGET NRI PROPERTY SOURCES
				"${_oot3d_nri_sources}")
			unset(_oot3d_nri_sources)
		endif()
		if(MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND TARGET NRI_Shared)
			# NRI enables /WX for clang-cl after its Clang suppressions.
			# Keep /W4 diagnostics, but do not let warnings in the pinned
			# third-party SDK block a Clang/ThinLTO host build.
			target_compile_options(NRI_Shared PUBLIC /WX-)
		endif()
		if(THREE_DS_RECOMP_ENABLE_SSSR)
			include(cmake/dependencies/oot3d-sssr.cmake)
		endif()
		if(WIN32 AND NRI_ENABLE_FFX_SDK)
			FetchContent_GetProperties(ffx)
			set(THREE_DS_RECOMP_FFX_VK_DLL
				"${ffx_SOURCE_DIR}/PrebuiltSignedDLL/amd_fidelityfx_vk.dll"
				CACHE INTERNAL "OOT3D FidelityFX Vulkan runtime")
		endif()
		if(WIN32 AND NRI_ENABLE_NGX_SDK)
			FetchContent_GetProperties(ngx)
			set(THREE_DS_RECOMP_NGX_INCLUDE_DIR "${ngx_SOURCE_DIR}/include"
				CACHE INTERNAL "OOT3D NGX SDK include directory")
			if(THREE_DS_RECOMP_DLSS_RUNTIME)
				get_filename_component(_oot3d_dlss_runtime
					"${THREE_DS_RECOMP_DLSS_RUNTIME}" ABSOLUTE)
			else()
				set(_oot3d_dlss_runtime
					"${ngx_SOURCE_DIR}/lib/Windows_x86_64/rel/nvngx_dlss.dll")
			endif()
			execute_process(
				COMMAND ${CMAKE_COMMAND}
					-DRUNTIME_PATH=${_oot3d_dlss_runtime}
					-DEXPECTED_VERSION=${THREE_DS_RECOMP_NGX_VERSION}
					-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidateNvidiaDlssRuntime.cmake
				COMMAND_ERROR_IS_FATAL ANY)
			set(THREE_DS_RECOMP_DLSS_VK_DLL "${_oot3d_dlss_runtime}"
				CACHE INTERNAL "OOT3D DLSS Vulkan runtime" FORCE)
			unset(_oot3d_dlss_runtime)
		endif()
	endif()
	function(three_ds_recomp_deploy_nri_runtime target_name)
		if(WIN32 AND THREE_DS_RECOMP_FFX_VK_DLL AND
				EXISTS "${THREE_DS_RECOMP_FFX_VK_DLL}")
			add_custom_command(TARGET ${target_name} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${THREE_DS_RECOMP_FFX_VK_DLL}"
					"$<TARGET_FILE_DIR:${target_name}>"
				COMMENT "Deploying FidelityFX Vulkan runtime for ${target_name}")
		endif()
		if(WIN32 AND THREE_DS_RECOMP_DLSS_VK_DLL AND
				EXISTS "${THREE_DS_RECOMP_DLSS_VK_DLL}")
			add_custom_command(TARGET ${target_name} POST_BUILD
				COMMAND ${CMAKE_COMMAND}
					-DRUNTIME_PATH=${THREE_DS_RECOMP_DLSS_VK_DLL}
					-DEXPECTED_VERSION=${THREE_DS_RECOMP_NGX_VERSION}
					-P ${THREE_DS_RECOMP_CMAKE_DIR}/ValidateNvidiaDlssRuntime.cmake
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${THREE_DS_RECOMP_DLSS_VK_DLL}"
					"$<TARGET_FILE_DIR:${target_name}>"
				COMMENT "Deploying DLSS Vulkan runtime for ${target_name}")
		endif()
	endfunction()
	if(THREE_DS_RECOMP_ENABLE_CACAO)
		include(cmake/dependencies/oot3d-cacao.cmake)
	endif()
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
	include(cmake/dependencies/android.cmake)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    include(cmake/dependencies/mac.cmake)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    include(cmake/dependencies/ios.cmake)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/dependencies/linux.cmake)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
    include(cmake/dependencies/openbsd.cmake)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(cmake/dependencies/windows.cmake)
endif()

# === Platform Specific Configuration ===
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    include(cmake/ios-toolchain-populate.cmake)
endif()

# =========== Sources =============
add_subdirectory("src")

# =========== Tests =============
option(THREE_DS_RECOMP_BUILD_TESTS "Build unit tests" OFF)
if(THREE_DS_RECOMP_BUILD_TESTS)
    enable_testing()
    add_subdirectory("tests")
endif()
