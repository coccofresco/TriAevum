# iMMERSE module (../LICENSE.md): the MXAO GPU tests and the generator of the
# module's pass pack. Included by tools/renderer/tev_program/CMakeLists.txt with
# THREE_DS_RECOMP_ENABLE_IMMERSE; uses that project's Vulkan test device,
# shaderc and SPIR-V pack helpers.
get_filename_component(_immerse "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

add_executable(mxao_gpu_tests "${CMAKE_CURRENT_LIST_DIR}/mxao_gpu_tests.cpp" "${_immerse}/mxao_shader.cpp"
    "${runtime}/src/fast/extension/ambient_occlusion_composite.cpp")
target_compile_features(mxao_gpu_tests PRIVATE cxx_std_20)
target_include_directories(mxao_gpu_tests PRIVATE "${runtime}/include" "${runtime}/src" "${SHADERC_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mxao_gpu_tests PRIVATE Vulkan::Vulkan "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET mxao_gpu_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:mxao_gpu_tests>")
endif()
add_test(NAME mxao_gpu COMMAND mxao_gpu_tests)
set_tests_properties(mxao_gpu PROPERTIES TIMEOUT 120)

add_executable(build_immerse_pass_artifacts "${CMAKE_CURRENT_LIST_DIR}/build_mxao_pass_artifacts.cpp"
    "${_immerse}/mxao_shader.cpp" "${runtime}/src/fast/extension/ambient_occlusion_composite.cpp")
triaevum_add_spirv_pack(build_immerse_pass_artifacts "${_immerse}/mxao_pass_binaries.cpp" "${_immerse}/mxao_pass.spvpack")
target_compile_features(build_immerse_pass_artifacts PRIVATE cxx_std_20)
target_include_directories(build_immerse_pass_artifacts PRIVATE "${runtime}/include" "${runtime}/src"
    "${SHADERC_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(build_immerse_pass_artifacts PRIVATE "${SHADERC_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET build_immerse_pass_artifacts POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADERC_RUNTIME}" "$<TARGET_FILE_DIR:build_immerse_pass_artifacts>")
endif()
add_test(NAME immerse_pass_artifacts COMMAND build_immerse_pass_artifacts --check)
set_tests_properties(immerse_pass_artifacts PROPERTIES TIMEOUT 120)
