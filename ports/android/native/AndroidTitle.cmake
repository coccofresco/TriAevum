# The title whose publisher ARM64 module ports/android/native builds
# (TRIAEVUM_ANDROID_TITLE). Both titles use the same generated-C++ whole-AOT
# wrapper and define as the desktop plugin backend
# (tools/triaevum_release/whole_aot_plugin_backend.py: the wrapper in
# tools/oot3d/native_game_runtime and OOT3D_NATIVE_GENERATED_WHOLE_AOT=1); a
# title differs only by its verified translated sources, which are the desktop
# release's. The MM3D A32 registry (a32_generated_overlay_r18, rule D8) is NOT
# part of them: the runtime compiles it (OOT3D_A32_AOT_GENERATED_DIR, passed by
# tools/android/android_release.py --a32-registry). The default oot3d leaves the
# Echoes of Time build unchanged.
set(TRIAEVUM_ANDROID_TITLES oot3d mm3d)

function(triaevum_android_title_plugin title root out_source out_definition)
    if(NOT title IN_LIST TRIAEVUM_ANDROID_TITLES)
        message(FATAL_ERROR "Unknown TRIAEVUM_ANDROID_TITLE '${title}' (expected one of: ${TRIAEVUM_ANDROID_TITLES})")
    endif()
    if(NOT EXISTS "${root}/titles/${title}/android.json")
        message(FATAL_ERROR "Missing Android title facts: titles/${title}/android.json")
    endif()
    set(${out_source} "${root}/tools/oot3d/native_game_runtime/triaevum_title_whole_aot_plugin.cpp" PARENT_SCOPE)
    set(${out_definition} "OOT3D_NATIVE_GENERATED_WHOLE_AOT=1" PARENT_SCOPE)
endfunction()

# A translated source manifest that names its title must name this one.
function(triaevum_android_require_source_title title directory)
    file(READ "${directory}/TITLE_SOURCE_MANIFEST.json" metadata)
    string(JSON named ERROR_VARIABLE missing GET "${metadata}" title)
    if(NOT missing AND NOT named STREQUAL title)
        message(FATAL_ERROR "Translated sources are for '${named}', not TRIAEVUM_ANDROID_TITLE '${title}'")
    endif()
endfunction()
