# Desktop (Windows, Linux) publisher builds must not carry the build machine's
# source, dependency, build or home paths: __FILE__ reaches every log line
# (spdlog's [%@] source location) and assertion text. Each known root is
# mapped to a short relative name, so a log shows runtime/three_ds_recomp/...
# instead of the build host's checkout. The macOS port has its own module
# (ports/macos/PathRemapping.cmake). Compiler flags only: no source, shader or
# generated output changes, so the runtime's decisions are unaffected.
option(TRIAEVUM_REMAP_BUILD_PATHS "Map build-machine paths out of __FILE__ and debug info" ON)
if(APPLE OR NOT TRIAEVUM_REMAP_BUILD_PATHS)
    return()
endif()

# Generic roots first, specific ones after: GCC prefers the last matching
# option and Clang the longest matching prefix, so both pick the specific one.
set(triaevum_remap_pairs "")
if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
    list(APPEND triaevum_remap_pairs "$ENV{HOME}" "home")
endif()
if(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
    list(APPEND triaevum_remap_pairs "$ENV{USERPROFILE}" "home")
endif()
if(DEFINED VCPKG_ROOT AND NOT "${VCPKG_ROOT}" STREQUAL "")
    list(APPEND triaevum_remap_pairs "${VCPKG_ROOT}" "vcpkg")
endif()
list(APPEND triaevum_remap_pairs "${CMAKE_SOURCE_DIR}" "." "${CMAKE_BINARY_DIR}" "build")
if(DEFINED FETCHCONTENT_BASE_DIR AND NOT "${FETCHCONTENT_BASE_DIR}" STREQUAL "")
    list(APPEND triaevum_remap_pairs "${FETCHCONTENT_BASE_DIR}" "deps")
endif()
# Dependencies supplied from outside the tree (FETCHCONTENT_SOURCE_DIR_<NAME>).
get_cmake_property(triaevum_cache_variables CACHE_VARIABLES)
foreach(variable IN LISTS triaevum_cache_variables)
    if(variable MATCHES "^FETCHCONTENT_SOURCE_DIR_(.+)$" AND NOT "${${variable}}" STREQUAL "")
        string(TOLOWER "${CMAKE_MATCH_1}" dependency)
        list(APPEND triaevum_remap_pairs "${${variable}}" "deps/${dependency}")
    elseif(variable MATCHES "^(.+)_GENERATED_DIR$" AND IS_ABSOLUTE "${${variable}}")
        # Generated sources placed outside the tree (whole-AOT and A32 output).
        string(TOLOWER "${CMAKE_MATCH_1}" generated)
        list(APPEND triaevum_remap_pairs "${${variable}}" "generated/${generated}")
    endif()
endforeach()

list(LENGTH triaevum_remap_pairs triaevum_remap_length)
math(EXPR triaevum_remap_last "${triaevum_remap_length} - 1")
foreach(index RANGE 0 ${triaevum_remap_last} 2)
    math(EXPR value_index "${index} + 1")
    list(GET triaevum_remap_pairs ${index} original)
    list(GET triaevum_remap_pairs ${value_index} replacement)
    file(TO_CMAKE_PATH "${original}" original)
    string(REGEX REPLACE "/+$" "" original "${original}")
    if(replacement STREQUAL ".")
        set(mapping "${original}/=")
    else()
        set(mapping "${original}/=${replacement}/")
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # cl.exe: only __FILE__ can be trimmed; the replacement is dropped.
        add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/d1trimfile:${original}/>")
    elseif(MSVC)
        # clang-cl forwards the Clang option.
        add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/clang:-ffile-prefix-map=${mapping}>")
    else()
        add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-ffile-prefix-map=${mapping}>")
    endif()
endforeach()
# The PE debug directory records the PDB's absolute path: keep its file name only.
if(WIN32 AND MSVC)
    add_link_options("$<$<LINK_LANGUAGE:C,CXX>:/PDBALTPATH:%_PDB%>")
endif()
