# Runs the host-native Unity suite. Prepends the user-scoped WinLibs GCC
# when it is installed and does not change the system PATH.
param(
    [string]$LogFile = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
$compilerBin = Join-Path $env:USERPROFILE '.local\winlibs\mingw64\bin'
if (Test-Path (Join-Path $compilerBin 'g++.exe')) {
    $env:PATH = $compilerBin + [IO.Path]::PathSeparator + $env:PATH
}
$gxx = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $gxx) {
    throw 'Host g++ was not found. Install user-scoped WinLibs under %USERPROFILE%\.local\winlibs or provide g++ on PATH.'
}
if (-not (Test-Path $pio)) {
    throw "PlatformIO was not found at $pio"
}
Write-Output ("HOST_CXX=" + $gxx.Source)
Push-Location $repo
try {
    if ($LogFile) {
        Set-Content -Path $LogFile -Value ("HOST_CXX=" + $gxx.Source) -Encoding utf8
        & $pio test -e native *>&1 | Tee-Object -FilePath $LogFile -Append
    } else {
        & $pio test -e native
    }
    if ($LASTEXITCODE -ne 0) {
        throw "native tests exit $LASTEXITCODE"
    }
    if ($LogFile) {
        Add-Content -Path $LogFile -Value 'HOST_TESTS_OK' -Encoding utf8
    }
} finally {
    Pop-Location
}
Write-Output 'HOST_TESTS_OK'
