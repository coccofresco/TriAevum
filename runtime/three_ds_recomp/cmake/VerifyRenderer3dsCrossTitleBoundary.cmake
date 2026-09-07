if(NOT DEFINED RENDERER3DS_SOURCE_ROOT)
    message(FATAL_ERROR "RENDERER3DS_SOURCE_ROOT is required")
endif()
if(NOT DEFINED RENDERER3DS_CANARY_SOURCE)
    message(FATAL_ERROR "RENDERER3DS_CANARY_SOURCE is required")
endif()

file(GLOB_RECURSE renderer3ds_shared_files
    "${RENDERER3DS_SOURCE_ROOT}/include/fast/renderer3ds/*"
    "${RENDERER3DS_SOURCE_ROOT}/src/fast/renderer3ds/*")
list(APPEND renderer3ds_shared_files "${RENDERER3DS_CANARY_SOURCE}")
file(GLOB renderer3ds_capture_diagnostic_files
    "${RENDERER3DS_SOURCE_ROOT}/tests/renderer_3ds_pica_capture_*")
list(APPEND renderer3ds_shared_files
    ${renderer3ds_capture_diagnostic_files})

set(forbidden_fragments
    "oot3d"
    "zelda")

foreach(source_file IN LISTS renderer3ds_shared_files)
    if(NOT EXISTS "${source_file}")
        message(FATAL_ERROR
            "Nintendo 3DS cross-title boundary input is missing: ${source_file}")
    endif()
    file(READ "${source_file}" source_contents)
    string(TOLOWER "${source_contents}" source_contents_lower)
    foreach(fragment IN LISTS forbidden_fragments)
        string(FIND "${source_contents_lower}" "${fragment}" match_offset)
        if(NOT match_offset EQUAL -1)
            message(FATAL_ERROR
                "Nintendo 3DS shared renderer boundary contains forbidden title-specific token '${fragment}' in ${source_file}")
        endif()
    endforeach()
endforeach()

list(LENGTH renderer3ds_shared_files checked_file_count)
message(STATUS
    "Nintendo 3DS shared renderer boundary is title-neutral (${checked_file_count} files)")
