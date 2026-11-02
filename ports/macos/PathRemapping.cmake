# Publisher builds must not embed the host's source, build or home paths in
# __FILE__, diagnostics or debug information. This file is Mac-only and is
# included before any dependency or product target is added.
if(NOT APPLE)
    message(FATAL_ERROR "macOS path remapping requires an Apple target")
endif()
get_filename_component(triaevum_source_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(triaevum_remap_roots "${triaevum_source_root}" "${CMAKE_BINARY_DIR}")
set(triaevum_remap_destinations "/src/triaevum" "/build/triaevum")
# Cover dependency and SDK inputs under the publisher's home as well.
if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
    list(APPEND triaevum_remap_roots "$ENV{HOME}")
    list(APPEND triaevum_remap_destinations "/publisher")
endif()
# Apple ld writes absolute object paths into its debug map independently of
# compiler prefix maps. Keep these paths relative for executables and dylibs.
add_link_options("LINKER:-oso_prefix,${CMAKE_BINARY_DIR}/")
list(LENGTH triaevum_remap_roots triaevum_remap_count)
math(EXPR triaevum_remap_last "${triaevum_remap_count} - 1")
foreach(index RANGE ${triaevum_remap_last})
    list(GET triaevum_remap_roots ${index} original)
    list(GET triaevum_remap_destinations ${index} replacement)
    foreach(kind file debug macro)
        add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-f${kind}-prefix-map=${original}=${replacement}>")
    endforeach()
endforeach()
