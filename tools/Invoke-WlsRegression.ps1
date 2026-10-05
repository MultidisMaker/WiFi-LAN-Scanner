# Non-interactive host tests, production build, HIL upload, serial regression,
# optional live TFMiddle discovery, and production restore. A COM port is
# discovered at runtime and is never written into PlatformIO configuration.
# -Live looks up the TFMiddle passphrase through the Agentic credential helper
# and passes it to Python on stdin. It is not a command-line argument or a file.
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

function Get-TfMiddleLiveJson {
    $helper = $env:AGENTIC_CREDENTIAL_HELPER_PATH
    if ([string]::IsNullOrWhiteSpace($helper) -or -not (Test-Path -LiteralPath $helper)) {
        throw 'live credential helper is unavailable'
    }
    $captured = $null
    try {
        $captured = @(
            & $helper -Client 'Multidiscipline-Maker' -Tenant 'Local' -Environment 'Local' -Asset 'TFMiddle' `
                -AccountType 'WifiPassphrase' -Protocol 'WiFi' -Purpose 'LiveValidation' -Site 'TFMiddle' `
                -Scope 'JoinedSubnet' -Service 'WiFi-LAN-Scanner' -UserName 'TFMiddle' -Title 'TFMiddle' 2>&1
        )
    } catch {
        throw 'live credential lookup failed'
    }
    $cred = $captured | Where-Object { $_ -is [pscredential] } | Select-Object -First 1
    if ($null -eq $cred) {
        throw 'live credential lookup failed'
    }
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($cred.Password)
    $plain = $null
    try {
        $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
        if ([string]::IsNullOrEmpty($plain) -or $plain.Length -gt 63) {
            throw 'live secret shape rejected'
        }
        foreach ($ch in $plain.ToCharArray()) {
            $code = [int]$ch
            if ($code -lt 32 -or $code -gt 126) {
                throw 'live secret shape rejected'
            }
        }
        $payload = [ordered]@{
            ssid = 'TFMiddle'
            psk = $plain
            instructionId = 'MM-PenTest-A012'
        }
        return ($payload | ConvertTo-Json -Compress)
    } finally {
        $plain = $null
        if ($bstr -ne [IntPtr]::Zero) {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
        }
    }
}

function Invoke-PythonStdin {
    param(
        [Parameter(Mandatory = $true)][string]$Script,
        [string[]]$Arguments = @(),
        [AllowEmptyString()][string]$StdinText = ''
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $python
    $quoted = New-Object System.Collections.Generic.List[string]
    $quoted.Add(('"{0}"' -f $Script))
    foreach ($arg in $Arguments) {
        $quoted.Add(('"{0}"' -f ($arg -replace '"', '\"')))
    }
    $psi.Arguments = ($quoted -join ' ')
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    [void]$proc.Start()
    $bytes = [Text.Encoding]::UTF8.GetBytes($StdinText)
    if ($bytes.Length -gt 0) {
        $proc.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
        $proc.StandardInput.BaseStream.Flush()
    }
    $proc.StandardInput.Close()
    [Array]::Clear($bytes, 0, $bytes.Length)
    $stdout = $proc.StandardOutput.ReadToEnd()
    $null = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()
    return [pscustomobject]@{
        ExitCode = $proc.ExitCode
        Stdout = $stdout
    }
}

function Test-FilesExcludeLiveSecret {
    param(
        [string[]]$Paths,
        [string]$Label,
        [string]$SecretJson
    )
    $checkerPath = Join-Path $WorkDir 'image-secret-check.py'
    $checker = @'
import json
import sys

def main():
    raw = sys.stdin.buffer.read()
    if raw.endswith(b"\n"):
        raw = raw[:-1]
    if raw.endswith(b"\r"):
        raw = raw[:-1]
    try:
        payload = json.loads(raw.decode("utf-8"))
        psk = payload.get("psk")
    except (json.JSONDecodeError, UnicodeError, AttributeError):
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
    raw = b""
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
    $result = Invoke-PythonStdin -Script $checkerPath -Arguments $Paths -StdinText $SecretJson
    if ($result.ExitCode -ne 0 -or $result.Stdout.Trim() -ne 'PASS') {
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

function Reset-KnownBoardUsb {
    $instance = 'USB\VID_303A&PID_1001\' + $knownSerial
    Disable-PnpDevice -InstanceId $instance -Confirm:$false -ErrorAction Stop
    Start-Sleep -Seconds 2
    Enable-PnpDevice -InstanceId $instance -Confirm:$false -ErrorAction Stop
    Start-Sleep -Seconds 3
    Write-Step 'USB_REENUM=ok'
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
$script:liveSecretJson = $null
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
    $idOk = $false
    for ($try = 1; $try -le 2 -and -not $idOk; $try++) {
        if ($try -gt 1) {
            Reset-KnownBoardUsb
            $port = Get-IntendedPort
        }
        try {
            Invoke-Logged -Name 'flash_id' -LogPath $idLog -Action {
                & $python $esptool --chip esp32s3 --port $port flash_id
            }
            $idText = Get-Content -Raw $idLog
            $idOk = $idText -match '80:65:99:a0:3e:70' -and $idText -match '16MB'
        } catch {
            $idOk = $false
        }
    }
    if (-not $idOk) {
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
        $script:liveSecretJson = Get-TfMiddleLiveJson
        Test-FilesExcludeLiveSecret -Paths @($prodBin, $prodElf, $hilBin, $hilElf) -Label 'firmware image' -SecretJson $script:liveSecretJson
        Write-Step 'IMAGE_SECRET_SCAN=pass'
    }

    $uploadHilLog = Join-Path $WorkDir 'hil-upload.log'
    $hilHashOk = $false
    for ($try = 1; $try -le 3 -and -not $hilHashOk; $try++) {
        if ($try -gt 1) {
            Start-Sleep -Seconds 3
            Reset-KnownBoardUsb
            Start-Sleep -Seconds 2
            $port = Get-IntendedPort
        }
        try {
            Invoke-Logged -Name 'hil upload' -LogPath $uploadHilLog -Action {
                & $pio run -e lilygo-t-display-s3-pro-hil -t upload --upload-port $port
            }
            $hilUploadText = Get-Content -Raw $uploadHilLog
            $hilHashOk = $hilUploadText -match 'Hash of data verified'
        } catch {
            $hilHashOk = $false
        }
    }
    if (-not $hilHashOk) {
        throw 'HIL upload did not verify the flash hash'
    }
    $hilUploaded = $true
    Start-Sleep -Seconds 2

    $hilTranscript = Join-Path $WorkDir 'hil-transcript.txt'
    & $python $serialTool --port $port --mode hil --transcript $hilTranscript
    if ($LASTEXITCODE -ne 0) {
        Reset-KnownBoardUsb
        $port = Get-IntendedPort
        & $python $serialTool --port $port --mode hil --transcript $hilTranscript
    }
    if ($LASTEXITCODE -ne 0) {
        throw "HIL serial regression exit $LASTEXITCODE"
    }
    if ($Live) {
        $liveTranscript = Join-Path $WorkDir 'live-transcript.txt'
        $liveRun = Invoke-PythonStdin -Script $serialTool -Arguments @(
            '--port', $port, '--mode', 'live', '--transcript', $liveTranscript, '--secret-stdin'
        ) -StdinText $script:liveSecretJson
        if ($liveRun.ExitCode -ne 0) {
            $firstLive = Join-Path $WorkDir 'live-transcript-attempt1.txt'
            if (Test-Path -LiteralPath $liveTranscript) {
                Copy-Item -LiteralPath $liveTranscript -Destination $firstLive -Force
            }
            Start-Sleep -Seconds 3
            $liveRun = Invoke-PythonStdin -Script $serialTool -Arguments @(
                '--port', $port, '--mode', 'live', '--transcript', $liveTranscript, '--secret-stdin'
            ) -StdinText $script:liveSecretJson
        }
        if ($liveRun.ExitCode -ne 0) {
            throw "live serial regression exit $($liveRun.ExitCode)"
        }
        Test-FilesExcludeLiveSecret -Paths @($liveTranscript) -Label 'live transcript' -SecretJson $script:liveSecretJson
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
    $failReason = [string]$_.Exception.Message
    if ($failReason -match 'psk|passphrase|password' -or $failReason.Length -gt 240) {
        $failReason = 'live step failed'
    }
    Write-Step ("REGRESSION_FAIL=" + $failReason)
} finally {
    $script:liveSecretJson = $null
    if ($hilUploaded) {
        try {
            if (-not $port) { throw 'Production restore has no discovered port' }
            $restoreLog = Join-Path $WorkDir 'production-restore.log'
            $restored = $false
            for ($try = 1; $try -le 3 -and -not $restored; $try++) {
                if ($try -gt 1) {
                    Start-Sleep -Seconds 2
                    try {
                        Reset-KnownBoardUsb
                        $port = Get-IntendedPort
                    } catch {
                        Write-Step 'USB_REENUM_FAIL'
                    }
                }
                try {
                    Invoke-Logged -Name 'production restore' -LogPath $restoreLog -Action {
                        & $pio run -e lilygo-t-display-s3-pro -t upload --upload-port $port
                    }
                    $restoreText = Get-Content -Raw $restoreLog
                    $restored = $restoreText -match 'Hash of data verified'
                } catch {
                    $restored = $false
                }
            }
            if (-not $restored) {
                throw 'Production restore did not verify the flash hash'
            }
            Start-Sleep -Seconds 2
            $bootTranscript = Join-Path $WorkDir 'production-boot.txt'
            & $python $serialTool --port $port --mode boot --transcript $bootTranscript
            if ($LASTEXITCODE -ne 0) {
                throw "Production boot check exit $LASTEXITCODE"
            }
            Write-Step 'PRODUCTION_RESTORED'
            $remoteTranscript = Join-Path $WorkDir 'production-remote.txt'
            & $python $serialTool --port $port --mode production-remote --transcript $remoteTranscript
            if ($LASTEXITCODE -ne 0) {
                Reset-KnownBoardUsb
                $port = Get-IntendedPort
                & $python $serialTool --port $port --mode production-remote --transcript $remoteTranscript
            }
            if ($LASTEXITCODE -ne 0) {
                throw 'production USB remote proof failed'
            }
            Write-Step 'PRODUCTION_REMOTE=pass'
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
