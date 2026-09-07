if(NOT RUNTIME_PATH OR NOT EXISTS "${RUNTIME_PATH}")
    message(FATAL_ERROR "DLSS runtime is missing: ${RUNTIME_PATH}")
endif()
if(NOT EXPECTED_VERSION)
    message(FATAL_ERROR "EXPECTED_VERSION is required")
endif()
if(NOT WIN32)
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
