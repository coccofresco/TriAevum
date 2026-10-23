# CTest registration of the test executables that do not register themselves:
# those of the root project, the module runtime and the release tools. Each one
# is a single CTest test, run with the arguments of its OOT3D_TEST_ARGUMENTS
# target property (generator expressions allowed), or none. The runtime's own
# suites register their cases through gtest discovery. One ctest run of the
# build tree then covers every suite, and a test executable that was not built
# is reported as not run instead of being skipped silently
# (docs/TRIAEVUM_WINDOWS_RELEASE_QUALIFICATION.md, layers L1 and L2).
function(oot3d_register_test_executables)
    foreach(directory IN LISTS ARGN)
        get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target IN LISTS targets)
            get_target_property(type ${target} TYPE)
            get_target_property(folder ${target} FOLDER)
            if(type STREQUAL "EXECUTABLE" AND (folder STREQUAL "Tests" OR target MATCHES "_tests$"))
                get_target_property(arguments ${target} OOT3D_TEST_ARGUMENTS)
                if(NOT arguments)
                    set(arguments)
                endif()
                add_test(NAME ${target} COMMAND ${target} ${arguments})
                # Exit 77 means "skipped": a test that needs private inputs
                # the machine does not provide says so instead of failing.
                set_tests_properties(${target} PROPERTIES SKIP_RETURN_CODE 77)
            endif()
        endforeach()
    endforeach()
endfunction()

oot3d_register_test_executables(
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/triaevum_module"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/triaevum_release/tests")
