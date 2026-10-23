# iMMERSE module (LICENSE.md in this directory): iMMERSE MXAO by Pascal
# Gilcher, under the original iMMERSE licence, used under the author's explicit
# permission granted exclusively to TriAevum. Included by src/CMakeLists.txt
# only with THREE_DS_RECOMP_ENABLE_IMMERSE (which requires NRI).
#
# The module is its own shared library, triaevum_immerse. It compiles only this
# directory's sources, with the module interface (module_interface/, MIT) and
# the NRI and Vulkan headers on its include path, and links nothing of
# TriAevum. The runtime loads it when it is installed beside the executable
# (fast/oot3d/immerse_module_loader.cpp); without it MXAO is unavailable.
add_library(triaevum_immerse SHARED
    "${CMAKE_CURRENT_LIST_DIR}/mxao_module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/mxao_shader.cpp")
triaevum_add_spirv_pack(triaevum_immerse
    "${CMAKE_CURRENT_LIST_DIR}/mxao_pass_binaries.cpp" "${CMAKE_CURRENT_LIST_DIR}/mxao_pass.spvpack")
target_compile_features(triaevum_immerse PRIVATE cxx_std_20)
target_include_directories(triaevum_immerse PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../module_interface"
    $<TARGET_PROPERTY:NRI,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:Vulkan::Vulkan,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(triaevum_immerse PRIVATE $<TARGET_PROPERTY:NRI,INTERFACE_COMPILE_DEFINITIONS>)
# Beside the runtime executable, where the loader looks for it.
set_target_properties(triaevum_immerse PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    FOLDER immerse)
source_group("immerse" FILES
    "${CMAKE_CURRENT_LIST_DIR}/mxao_module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/mxao_shader.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/mxao_pass_binaries.cpp")
