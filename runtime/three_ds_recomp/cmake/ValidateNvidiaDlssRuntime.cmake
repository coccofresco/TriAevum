if(NOT RUNTIME_PATH OR NOT EXISTS "${RUNTIME_PATH}")
    message(FATAL_ERROR "DLSS runtime is missing: ${RUNTIME_PATH}")
endif()
if(NOT EXPECTED_VERSION)
    message(FATAL_ERROR "EXPECTED_VERSION is required")
endif()
if(NOT CMAKE_HOST_WIN32)
    # A Windows runtime (nvngx_dlss.dll) verified on a non-Windows build host (MinGW cross-build):
    # osslsigncode against a pinned trust bundle, see tools/triaevum_release/dlss_runtime_signature.py.
    # Never skipped silently. A Linux runtime (.so) carries no Authenticode signature: unchanged.
    if(NOT RUNTIME_PATH MATCHES "\\.[dD][lL][lL]$")
        return()
    endif()
    foreach(_required OSSLSIGNCODE TRUST_BUNDLE TRUST_BUNDLE_SHA256)
        if(NOT ${_required})
            message(FATAL_ERROR
                "Rejected DLSS runtime '${RUNTIME_PATH}': verifying a Windows runtime on a "
                "non-Windows host needs -D${_required} (osslsigncode and a pinned trust bundle)")
        endif()
    endforeach()
    if(NOT PYTHON_EXECUTABLE)
        find_program(PYTHON_EXECUTABLE NAMES python3 python REQUIRED)
    endif()
    get_filename_component(_validator
        "${CMAKE_CURRENT_LIST_DIR}/../../../tools/triaevum_release/dlss_runtime_signature.py" ABSOLUTE)
    execute_process(
        COMMAND "${PYTHON_EXECUTABLE}" -I "${_validator}"
            --runtime "${RUNTIME_PATH}" --expected-version "${EXPECTED_VERSION}"
            --osslsigncode "${OSSLSIGNCODE}" --trust-bundle "${TRUST_BUNDLE}"
            --trust-bundle-sha256 "${TRUST_BUNDLE_SHA256}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR
            "Rejected DLSS runtime '${RUNTIME_PATH}' (validator ${_result}): "
            "${_error}${_output}")
    endif()
    message(STATUS "${_output}")
    return()
endif()

set(_windows_powershell
    "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0/powershell.exe")
if(EXISTS "${_windows_powershell}")
    set(_powershell "${_windows_powershell}")
else()
    find_program(_powershell NAMES pwsh powershell REQUIRED)
endif()
set(_validation [=[
$ErrorActionPreference = 'Stop'
$path = $env:OOT3D_DLSS_VALIDATION_RUNTIME
$expected = [Version]$env:OOT3D_DLSS_VALIDATION_VERSION
$signature = Get-AuthenticodeSignature -LiteralPath $path
if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
    throw "Authenticode status is $($signature.Status)"
}
if ($null -eq $signature.SignerCertificate -or
    $signature.SignerCertificate.Subject -notmatch '(^|, )O=NVIDIA Corporation(,|$)') {
    throw "the signer is not NVIDIA Corporation"
}
$rawVersion = (Get-Item -LiteralPath $path).VersionInfo.ProductVersion
$actual = [Version]($rawVersion -replace ',', '.')
if ($actual.Major -ne $expected.Major -or
    $actual.Minor -ne $expected.Minor -or
    $actual.Build -ne $expected.Build) {
    throw "runtime version $actual does not match required version $expected"
}
Write-Output "NVIDIA DLSS runtime verified: $actual ($($signature.SignerCertificate.Thumbprint))"
]=])
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env
        # Let the selected PowerShell rebuild its own module paths, rather than
        # inheriting incompatible PowerShell 7 modules through CMake.
        --unset=PSModulePath
        "OOT3D_DLSS_VALIDATION_RUNTIME=${RUNTIME_PATH}"
        "OOT3D_DLSS_VALIDATION_VERSION=${EXPECTED_VERSION}"
        "${_powershell}" -NoProfile -ExecutionPolicy Bypass
        -Command "${_validation}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "Rejected DLSS runtime '${RUNTIME_PATH}' (validator ${_result}): "
        "${_error}${_output}")
endif()
message(STATUS "${_output}")
