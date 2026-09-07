cmake_minimum_required(VERSION 3.26)
project(oot3d_guest_storage_legalizer LANGUAGES CXX)

find_package(LLVM REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

# The packaged LLVM exports the build machine's DIA path. Resolve the local
# SDK instead so the standalone tool does not depend on that installation.
if (TARGET LLVMDebugInfoPDB)
    find_library(OOT3D_DIA_GUIDS_LIBRARY NAMES diaguids
        HINTS
        "C:/Program Files/Microsoft Visual Studio/2022/Community/DIA SDK/lib/amd64"
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/DIA SDK/lib/amd64")
    if (OOT3D_DIA_GUIDS_LIBRARY)
        set_property(TARGET LLVMDebugInfoPDB PROPERTY INTERFACE_LINK_LIBRARIES
            "${OOT3D_DIA_GUIDS_LIBRARY};LLVMBinaryFormat;LLVMObject;LLVMSupport;LLVMDebugInfoCodeView;LLVMDebugInfoMSF")
    endif()
endif()

add_executable(Oot3dGuestStorageLegalizer Oot3dGuestStorageLegalizer.cpp)
target_compile_features(Oot3dGuestStorageLegalizer PRIVATE cxx_std_20)
target_include_directories(Oot3dGuestStorageLegalizer PRIVATE
    "${LLVM_INCLUDE_DIRS}")
llvm_map_components_to_libnames(OOT3D_LLVM_LIBS
    BitWriter Core IRReader Support)
target_link_libraries(Oot3dGuestStorageLegalizer PRIVATE ${OOT3D_LLVM_LIBS})
