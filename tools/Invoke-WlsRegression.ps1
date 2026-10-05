# Non-interactive host tests, production build, HIL upload, serial regression,
# optional live TFMiddle discovery, and production restore. A COM port is
# discovered at runtime and is never written into PlatformIO configuration.
# -Live reads the transient credential only from WLS_LIVE_SECRET_FILE.
param(
    [string]$WorkDir = '',
    [string]$EvidenceDir = '',
    [switch]$Live
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
$python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$esptool = Join-Path $env:USERPROFILE '.platformio\packages\tool-esptoolpy\esptool.py'
$serialTool = Join-Path $PSScriptRoot 'wls_serial.py'
$knownSerial = '80:65:99:A0:3E:70'
if (-not $WorkDir) {
    $WorkDir = Join-Path $repo 'build-wls'
}
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
if ($EvidenceDir) {
    New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
}

function Write-Step([string]$text) {
    Write-Output $text
}

function Invoke-Logged {
    param(
        [string]$Name,
        [string]$LogPath,
        [scriptblock]$Action
    )
    & $Action *>&1 | Tee-Object -FilePath $LogPath
    if ($LASTEXITCODE -ne 0) {
        throw "$Name exit $LASTEXITCODE"
    }
}

function Get-PioUsage([string]$Text) {
    $ram = [regex]::Match($Text, 'RAM:\s+\[[^\]]+\]\s+[\d.]+%\s+\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)')
    $flash = [regex]::Match($Text, 'Flash:\s+\[[^\]]+\]\s+[\d.]+%\s+\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)')
    if (-not $ram.Success -or -not $flash.Success) {
        throw 'Could not parse PlatformIO RAM/Flash usage'
    }
    return [pscustomobject]@{
        RamUsed = [int]$ram.Groups[1].Value
        RamTotal = [int]$ram.Groups[2].Value
        FlashUsed = [int]$flash.Groups[1].Value
        FlashTotal = [int]$flash.Groups[2].Value
    }
}

function Test-FilesExcludeLiveSecret {
    param(
        [string[]]$Paths,
        [string]$Label
    )
    $checkerPath = Join-Path $WorkDir 'image-secret-check.py'
    $checker = @'
import json
import os
import sys

def main():
    path = os.environ.get("WLS_LIVE_SECRET_FILE", "")
    if not path:
        sys.stdout.write("FAIL shape\n")
        return 1
    try:
        with open(path, "r", encoding="utf-8") as handle:
            payload = json.load(handle)
        psk = payload.get("psk")
    except (OSError, json.JSONDecodeError, UnicodeError):
        sys.stdout.write("FAIL shape\n")
        return 1
    if not isinstance(psk, str) or not (1 <= len(psk) <= 63):
        sys.stdout.write("FAIL shape\n")
        return 1
    try:
        needle = psk.encode("ascii")
    except UnicodeEncodeError:
        sys.stdout.write("FAIL shape\n")
        return 1
    del psk
    del payload
    for name in sys.argv[1:]:
        with open(name, "rb") as handle:
            data = handle.read()
        if needle in data:
            sys.stdout.write("FAIL file\n")
            return 1
    sys.stdout.write("PASS\n")
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        sys.stdout.write("FAIL shape\n")
        sys.exit(1)
'@
    Set-Content -LiteralPath $checkerPath -Value $checker -Encoding utf8
    $output = @(& $python $checkerPath @Paths)
    if ($LASTEXITCODE -ne 0 -or ($output -join '') -ne 'PASS') {
        throw "credential material present or secret shape rejected ($Label)"
    }
}

function Test-AsciiToken {
    param(
        [string]$Path,
        [string]$Token,
        [bool]$Present
    )
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
    $found = $ascii.Contains($Token)
    if ($Present -and -not $found) {
        throw "Expected $Token in $Path"
    }
    if (-not $Present -and $found) {
        throw "Unexpected $Token in $Path"
    }
}

function Get-IntendedPort {
    $nodes = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -match 'VID_303A&PID_1001' })
    $parents = @($nodes | Where-Object { $_.PNPDeviceID -match [regex]::Escape($knownSerial) })
    $ports = @($nodes | Where-Object { $_.Name -match '\((COM\d+)\)' })
    if ($parents.Count -ne 1 -or $ports.Count -ne 1) {
        throw "Board identity is ambiguous or missing. serialMatches=$($parents.Count) comMatches=$($ports.Count)"
    }
    $matched = [regex]::Match($ports[0].Name, '\((COM\d+)\)')
    $comPort = $matched.Groups[1].Value
    Write-Host ("BOARD_PORT=" + $comPort)
    Write-Host ("BOARD_SERIAL=" + $knownSerial)
    return $comPort
}

$hilUploaded = $false
$failed = $false
$failReason = ''
$port = ''
Push-Location $repo
try {
    $hostLog = Join-Path $WorkDir 'host-tests.log'
    & (Join-Path $PSScriptRoot 'Invoke-WlsHostTests.ps1') -LogFile $hostLog
    if (-not (Select-String -Path $hostLog -Pattern 'HOST_TESTS_OK' -Quiet)) {
        throw 'Host-native tests did not report HOST_TESTS_OK'
    }

    $prodLog = Join-Path $WorkDir 'production-build.log'
    Invoke-Logged -Name 'production build' -LogPath $prodLog -Action { & $pio run -e lilygo-t-display-s3-pro }
    $prodUsage = Get-PioUsage (Get-Content -Raw $prodLog)

    $port = Get-IntendedPort
    $idLog = Join-Path $WorkDir 'flash-id.log'
    Invoke-Logged -Name 'flash_id' -LogPath $idLog -Action {
        & $python $esptool --chip esp32s3 --port $port flash_id
    }
    $idText = Get-Content -Raw $idLog
    if ($idText -notmatch '80:65:99:a0:3e:70' -or $idText -notmatch '16MB') {
        throw 'flash_id did not match the known T-Display-S3-Pro'
    }

    $hilLog = Join-Path $WorkDir 'hil-build.log'
    Invoke-Logged -Name 'hil build' -LogPath $hilLog -Action { & $pio run -e lilygo-t-display-s3-pro-hil }
    $hilUsage = Get-PioUsage (Get-Content -Raw $hilLog)

    $prodBin = Join-Path $repo '.pio\build\lilygo-t-display-s3-pro\firmware.bin'
    $prodElf = Join-Path $repo '.pio\build\lilygo-t-display-s3-pro\firmware.elf'
    $hilBin = Join-Path $repo '.pio\build\lilygo-t-display-s3-pro-hil\firmware.bin'
    $hilElf = Join-Path $repo '.pio\build\lilygo-t-display-s3-pro-hil\firmware.elf'
    Test-AsciiToken -Path $prodBin -Token 'WLS-HIL' -Present $false
    Test-AsciiToken -Path $prodElf -Token 'WLS-HIL' -Present $false
    Test-AsciiToken -Path $hilBin -Token 'WLS-HIL' -Present $true
    Test-AsciiToken -Path $hilElf -Token 'WLS-HIL' -Present $true
    if ($Live) {
        if ([string]::IsNullOrWhiteSpace($env:WLS_LIVE_SECRET_FILE)) {
            throw 'live secret file is not configured'
        }
        Test-FilesExcludeLiveSecret -Paths @($prodBin, $prodElf, $hilBin, $hilElf) -Label 'firmware image'
        Write-Step 'IMAGE_SECRET_SCAN=pass'
    }

    $uploadHilLog = Join-Path $WorkDir 'hil-upload.log'
    Invoke-Logged -Name 'hil upload' -LogPath $uploadHilLog -Action {
        & $pio run -e lilygo-t-display-s3-pro-hil -t upload --upload-port $port
    }
    $hilUploadText = Get-Content -Raw $uploadHilLog
    if ($hilUploadText -notmatch 'Hash of data verified') {
        throw 'HIL upload did not verify the flash hash'
    }
    $hilUploaded = $true
    Start-Sleep -Seconds 2

    $hilTranscript = Join-Path $WorkDir 'hil-transcript.txt'
    & $python $serialTool --port $port --mode hil --transcript $hilTranscript
    if ($LASTEXITCODE -ne 0) {
        throw "HIL serial regression exit $LASTEXITCODE"
    }
    if ($Live) {
        $liveTranscript = Join-Path $WorkDir 'live-transcript.txt'
        & $python $serialTool --port $port --mode live --transcript $liveTranscript
        if ($LASTEXITCODE -ne 0) {
            throw "live serial regression exit $LASTEXITCODE"
        }
        Test-FilesExcludeLiveSecret -Paths @($liveTranscript) -Label 'live transcript'
        Write-Step 'LIVE_SERIAL=pass'
    }

    $sizeReport = @(
        "production_ram_used=$($prodUsage.RamUsed)"
        "production_ram_total=$($prodUsage.RamTotal)"
        "production_flash_used=$($prodUsage.FlashUsed)"
        "production_flash_total=$($prodUsage.FlashTotal)"
        "hil_ram_used=$($hilUsage.RamUsed)"
        "hil_ram_total=$($hilUsage.RamTotal)"
        "hil_flash_used=$($hilUsage.FlashUsed)"
        "hil_flash_total=$($hilUsage.FlashTotal)"
        "delta_ram=$($hilUsage.RamUsed - $prodUsage.RamUsed)"
        "delta_flash=$($hilUsage.FlashUsed - $prodUsage.FlashUsed)"
        'production_hil_token=absent'
        'hil_image_hil_token=present'
    ) -join "`n"
    Set-Content -Path (Join-Path $WorkDir 'build-size-comparison.txt') -Value ($sizeReport + "`n") -Encoding utf8
} catch {
    $failed = $true
    $failReason = $_.Exception.Message
    Write-Step ("REGRESSION_FAIL=" + $failReason)
} finally {
    if ($hilUploaded) {
        try {
            if (-not $port) { throw 'Production restore has no discovered port' }
            $restoreLog = Join-Path $WorkDir 'production-restore.log'
            Invoke-Logged -Name 'production restore' -LogPath $restoreLog -Action {
                & $pio run -e lilygo-t-display-s3-pro -t upload --upload-port $port
            }
            $restoreText = Get-Content -Raw $restoreLog
            if ($restoreText -notmatch 'Hash of data verified') {
                throw 'Production restore did not verify the flash hash'
            }
            Start-Sleep -Seconds 2
            $bootTranscript = Join-Path $WorkDir 'production-boot.txt'
            & $python $serialTool --port $port --mode boot --transcript $bootTranscript
            if ($LASTEXITCODE -ne 0) {
                throw "Production boot check exit $LASTEXITCODE"
            }
            Write-Step 'PRODUCTION_RESTORED'
        } catch {
            $failed = $true
            Write-Step ("PRODUCTION_RESTORE_FAIL=" + $_.Exception.Message)
            Write-Step 'BOARD_MAY_STILL_BE_RUNNING_HIL'
        }
    }
    Pop-Location
}

if ($EvidenceDir) {
    foreach ($name in @('hil-transcript.txt', 'production-boot.txt', 'build-size-comparison.txt')) {
        $source = Join-Path $WorkDir $name
        if (Test-Path $source) {
            Copy-Item $source (Join-Path $EvidenceDir $name) -Force
        }
    }
    $hostLog = Join-Path $WorkDir 'host-tests.log'
    if (Test-Path $hostLog) {
        $summary = Select-String -Path $hostLog -Pattern 'HOST_CXX=|test_|HOST_TESTS_OK|PASSED|FAILED|error:' |
            ForEach-Object { $_.Line }
        Set-Content -Path (Join-Path $EvidenceDir 'host-test-summary.txt') -Value (($summary -join "`n") + "`n") -Encoding utf8
    }
}

if ($failed) {
    Write-Step ("REGRESSION_RESULT=fail " + $failReason)
    exit 1
}
Write-Step 'REGRESSION_RESULT=pass'
exit 0
