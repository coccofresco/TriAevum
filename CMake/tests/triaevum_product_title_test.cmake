# cmake -P CMake/tests/triaevum_product_title_test.cmake
# Positive and negative configurations of triaevum_check_product_title
# (G4a-F5). Each case runs in its own cmake -P so a FATAL_ERROR is observed.
cmake_minimum_required(VERSION 3.20)
if(DEFINED CASE_PRODUCT)
    include("${CMAKE_CURRENT_LIST_DIR}/../triaevum_product_title.cmake")
    string(REPLACE "," ";" _titles "${CASE_TITLES}")
    triaevum_check_product_title("${CASE_PRODUCT}" "${_titles}")
    return()
endif()

set(_failures 0)
function(expect product titles accepted)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -DCASE_PRODUCT=${product} -DCASE_TITLES=${titles}
                -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(accepted)
        set(ok 0)
        if(result EQUAL 0)
            set(ok 1)
        endif()
    else()
        set(ok 0)
        if(NOT result EQUAL 0 AND err MATCHES "TRIAEVUM_PRODUCT_TITLE")
            set(ok 1)
        endif()
    endif()
    if(ok)
        message(STATUS "ok: product=${product} titles=${titles} accepted=${accepted}")
    else()
        message(STATUS "FAIL: product=${product} titles=${titles} accepted=${accepted} result=${result} ${err}")
        math(EXPR f "${_failures} + 1")
        set(_failures ${f} PARENT_SCOPE)
    endif()
endfunction()

expect(oot3d "oot3d" TRUE)
expect(oot3d "oot3d,mm3d" TRUE)
expect(mm3d "oot3d,mm3d" TRUE)
# Negatives: a product whose title the build does not contain, an unknown title.
expect(oot3d "mm3d" FALSE)
expect(mm3d "oot3d" FALSE)
expect(eot "oot3d,mm3d" FALSE)
if(_failures GREATER 0)
    message(FATAL_ERROR "triaevum_product_title_test: ${_failures} failure(s)")
endif()
message(STATUS "triaevum_product_title_test: ok")
