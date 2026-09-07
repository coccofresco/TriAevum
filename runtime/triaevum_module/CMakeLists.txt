cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(TriAevumModule VERSION 0.1.0 LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

add_library(triaevum_module STATIC
    src/audio_service_adapter.cpp
    src/audio_service_client.cpp
    src/filesystem_service_adapter.cpp
    src/filesystem_service_client.cpp
    src/guest_memory_lease_pool.cpp
    src/input_service.cpp
    src/input_service_adapter.cpp
    src/input_service_client.cpp
    src/native_module_loader.cpp
    src/pica_scanout_state.cpp
    src/pica_service_adapter.cpp
    src/pica_service_client.cpp
    src/rooted_filesystem_backend.cpp
    src/runtime_session.cpp
    src/service_registry.cpp
    src/sha256.cpp
    src/tam_metadata.cpp
    src/tam_reader.cpp
)
target_include_directories(triaevum_module
    PUBLIC include
    PRIVATE src
)
target_compile_features(triaevum_module PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(triaevum_module PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(triaevum_module PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(triaevum_module PRIVATE dl)
endif()

add_executable(triaevum_input_service_tests EXCLUDE_FROM_ALL
    tests/input_service_tests.cpp
)
target_link_libraries(triaevum_input_service_tests PRIVATE triaevum_module)
if(MSVC)
    target_compile_options(triaevum_input_service_tests PRIVATE
        /W4 /WX /utf-8)
else()
    target_compile_options(triaevum_input_service_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(triaevum_audio_service_tests EXCLUDE_FROM_ALL
    tests/audio_service_tests.cpp
)
target_link_libraries(triaevum_audio_service_tests PRIVATE triaevum_module)
if(MSVC)
    target_compile_options(triaevum_audio_service_tests PRIVATE
        /W4 /WX /utf-8)
else()
    target_compile_options(triaevum_audio_service_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(triaevum_filesystem_service_tests EXCLUDE_FROM_ALL
    tests/filesystem_service_tests.cpp
)
target_link_libraries(triaevum_filesystem_service_tests PRIVATE
    triaevum_module)
if(MSVC)
    target_compile_options(triaevum_filesystem_service_tests PRIVATE
        /W4 /WX /utf-8)
else()
    target_compile_options(triaevum_filesystem_service_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(triaevum_module_tests EXCLUDE_FROM_ALL
    tests/tam_reader_tests.cpp
)
target_link_libraries(triaevum_module_tests PRIVATE triaevum_module)
target_include_directories(triaevum_module_tests PRIVATE src)
if(MSVC)
    target_compile_options(triaevum_module_tests PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(triaevum_module_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(triaevum_tam_metadata_tests EXCLUDE_FROM_ALL
    tests/tam_metadata_tests.cpp
)
target_link_libraries(triaevum_tam_metadata_tests PRIVATE triaevum_module)
if(MSVC)
    target_compile_options(triaevum_tam_metadata_tests PRIVATE
        /W4 /WX /utf-8)
else()
    target_compile_options(triaevum_tam_metadata_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(triaevum_service_registry_tests EXCLUDE_FROM_ALL
    tests/service_registry_tests.cpp
)
target_link_libraries(triaevum_service_registry_tests PRIVATE triaevum_module)
if(MSVC)
    target_compile_options(triaevum_service_registry_tests PRIVATE
        /W4 /WX /utf-8)
else()
    target_compile_options(triaevum_service_registry_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(triaevum_pica_service_adapter_tests EXCLUDE_FROM_ALL
    tests/pica_service_adapter_tests.cpp
)
target_link_libraries(triaevum_pica_service_adapter_tests PRIVATE
    triaevum_module)
if(MSVC)
    target_compile_options(triaevum_pica_service_adapter_tests PRIVATE
        /W4 /WX /utf-8)
else()
    target_compile_options(triaevum_pica_service_adapter_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(triaevum_pica_service_client_tests EXCLUDE_FROM_ALL
    tests/pica_service_client_tests.cpp
)
target_link_libraries(triaevum_pica_service_client_tests PRIVATE
    triaevum_module)
if(MSVC)
    target_compile_options(triaevum_pica_service_client_tests PRIVATE
        /W4 /WX /utf-8)
else()
    target_compile_options(triaevum_pica_service_client_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(triaevum_pica_scanout_state_tests EXCLUDE_FROM_ALL
    tests/pica_scanout_state_tests.cpp
)
target_link_libraries(triaevum_pica_scanout_state_tests PRIVATE
    triaevum_module)
if(MSVC)
    target_compile_options(triaevum_pica_scanout_state_tests PRIVATE
        /W4 /WX /utf-8)
else()
    target_compile_options(triaevum_pica_scanout_state_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(triaevum_guest_memory_lease_pool_tests EXCLUDE_FROM_ALL
    tests/guest_memory_lease_pool_tests.cpp
)
target_link_libraries(triaevum_guest_memory_lease_pool_tests PRIVATE
    triaevum_module)
if(MSVC)
    target_compile_options(triaevum_guest_memory_lease_pool_tests PRIVATE
        /W4 /WX /utf-8)
else()
    target_compile_options(triaevum_guest_memory_lease_pool_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(triaevum_service_abi_c_tests EXCLUDE_FROM_ALL
    tests/service_abi_c_tests.c
)
target_include_directories(triaevum_service_abi_c_tests PRIVATE include)
if(MSVC)
    target_compile_options(triaevum_service_abi_c_tests PRIVATE
        /W4 /WX /utf-8)
else()
    target_compile_options(triaevum_service_abi_c_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(triaevum_mock_module SHARED EXCLUDE_FROM_ALL
    tests/mock_module.cpp
)
target_include_directories(triaevum_mock_module PRIVATE include)
target_compile_features(triaevum_mock_module PRIVATE cxx_std_20)
set_target_properties(triaevum_mock_module PROPERTIES PREFIX "")

add_executable(triaevum_native_module_loader_tests EXCLUDE_FROM_ALL
    tests/native_module_loader_tests.cpp
)
target_link_libraries(triaevum_native_module_loader_tests PRIVATE
    triaevum_module)
target_include_directories(triaevum_native_module_loader_tests PRIVATE src)
add_dependencies(triaevum_native_module_loader_tests triaevum_mock_module)
if(MSVC)
    target_compile_options(triaevum_mock_module PRIVATE /W4 /WX /utf-8)
    target_compile_options(triaevum_native_module_loader_tests PRIVATE
        /W4 /WX /utf-8)
else()
    target_compile_options(triaevum_mock_module PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(triaevum_native_module_loader_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
