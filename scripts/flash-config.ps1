param(
    [string]$JsonPath = "garage_config.json",
    [string]$Environment = "seeed_xiao_esp32c6",
    [string]$PlatformioExe = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe",
    [string]$Port = "COM5",
    [int]$Baud = 921600,
    [switch]$SkipFlash
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RequiredPath {
    param(
        [string]$Path,
        [string]$Description
    )
    if (-not (Test-Path $Path)) {
        throw "Cannot find $Description at '$Path'"
    }
    return (Get-Item $Path).FullName
}

function Convert-SizeToBytes {
    param([string]$Value)
    $token = $Value.Trim()
    if ($token -match "^0x[0-9a-fA-F]+$") {
        return [uint32]::Parse($token.Substring(2), "AllowHexSpecifier")
    }
    if ($token -match "^\d+$") {
        return [uint32]$token
    }
    if ($token -match "^(\d+)([KkMm])$") {
        $multiplier = if ($matches[2] -eq 'M' -or $matches[2] -eq 'm') { 1024 * 1024 } else { 1024 }
        return [uint32]$matches[1] * $multiplier
    }
    throw "Unsupported size format '$Value'"
}

function Invoke-ExternalCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )
    try {
        $output = & $FilePath @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } catch {
        $output = $_.Exception.Message
        $exitCode = 1
    }
    if ($output -is [System.Array]) {
        $output = $output -join [Environment]::NewLine
    }
    return [pscustomobject]@{
        Output = [string]$output
        ExitCode = [int]$exitCode
    }
}

function Get-NvsPartitionSpec {
    param(
        [string]$PartitionCsv,
        [string]$Name = "nvs"
    )
    foreach ($line in Get-Content $PartitionCsv) {
        $trimmed = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith("#")) {
            continue
        }
        $fields = $trimmed.Split(",") | ForEach-Object { $_.Trim() }
        if ($fields.Count -lt 5) {
            continue
        }
        if ($fields[0] -ne $Name) {
            continue
        }
        $offsetField = $fields[3]
        $sizeField = $fields[4]
        $sizeBytes = Convert-SizeToBytes $sizeField
        return [pscustomobject]@{
            OffsetExpr = $offsetField
            SizeBytes  = $sizeBytes
        }
    }
    throw "Partition '$Name' not found in $PartitionCsv"
}

function Get-PartitionTableOffset {
    param([string]$SdkConfigPath)
    $matchInfo = Select-String -Path $SdkConfigPath -Pattern "^CONFIG_PARTITION_TABLE_OFFSET=(.+)$" | Select-Object -First 1
    if (-not $matchInfo) {
        throw "CONFIG_PARTITION_TABLE_OFFSET not defined in $SdkConfigPath"
    }
    $value = $matchInfo.Matches[0].Groups[1].Value.Trim()
    if ($value -notmatch "^0x[0-9a-fA-F]+$") {
        throw "Partition table offset '$value' is not in hex format"
    }
    return [Convert]::ToUInt32($value.Substring(2), 16)
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptRoot
$originalLocation = Get-Location
try {
    Set-Location $repoRoot

    $jsonPathResolved = Resolve-RequiredPath (Join-Path $repoRoot $JsonPath) "configuration JSON"
    $partitionsPath = Resolve-RequiredPath (Join-Path $repoRoot "partitions.csv") "partitions.csv"

    $envSpecificSdkconfig = Join-Path $repoRoot ("sdkconfig.{0}" -f $Environment)
    if (Test-Path $envSpecificSdkconfig) {
        $sdkconfigPath = (Get-Item $envSpecificSdkconfig).FullName
    } else {
        $defaultSdkconfig = Join-Path $repoRoot "sdkconfig"
        $sdkconfigPath = Resolve-RequiredPath $defaultSdkconfig "sdkconfig file"
    }

    $nvsSpec = Get-NvsPartitionSpec -PartitionCsv $partitionsPath
    $partitionTableOffset = Get-PartitionTableOffset -SdkConfigPath $sdkconfigPath

    # The partition table occupies 0x1000 bytes; the first partition begins right after it.
    $flashAddress = $partitionTableOffset + 0x1000

    $platformioExe = Resolve-RequiredPath $PlatformioExe "platformio.exe"
    $scriptsDir = Split-Path -Parent $platformioExe
    $penvDir = Split-Path -Parent $scriptsDir
    $pioHome = Split-Path -Parent $penvDir

    $pythonExe = Resolve-RequiredPath (Join-Path $scriptsDir "python.exe") "PlatformIO Python interpreter"
    $nvsGen = Resolve-RequiredPath (Join-Path $pioHome "packages\framework-espidf\components\nvs_flash\nvs_partition_generator\nvs_partition_gen.py") "nvs_partition_gen.py"
    $esptool = Resolve-RequiredPath (Join-Path $pioHome "packages\tool-esptoolpy\esptool.py") "esptool.py"

    $buildDir = Join-Path $repoRoot ".pio\build\$Environment"
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

    $csvPath = Join-Path $buildDir "garage_config_nvs.csv"
    $binPath = Join-Path $buildDir "garage_config_nvs.bin"

    $jsonContent = Get-Content -Raw $jsonPathResolved
    $escapedJson = $jsonContent.Replace('"', '""')
    $csvLines = @(
        "key,type,encoding,value",
        "garage,namespace,,",
        "config,data,string,""$escapedJson"""
    )
    $csvContent = [string]::Join([Environment]::NewLine, $csvLines)
    $encoding = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($csvPath, $csvContent, $encoding)

    $sizeHex = "0x{0:X}" -f $nvsSpec.SizeBytes
    $genResult = Invoke-ExternalCommand -FilePath $pythonExe -Arguments @($nvsGen, 'generate', $csvPath, $binPath, $sizeHex)
    if ($genResult.ExitCode -ne 0) {
        if ($genResult.Output -match "No module named esp_idf_nvs_partition_gen") {
            Write-Host "Installing esp-idf-nvs-partition-gen Python package..."
            $installResult = Invoke-ExternalCommand -FilePath $pythonExe -Arguments @('-m', 'pip', 'install', '--upgrade', 'esp-idf-nvs-partition-gen')
            if ($installResult.ExitCode -ne 0) {
                throw "Failed to install esp-idf-nvs-partition-gen.`n$($installResult.Output)"
            }
            Write-Host $installResult.Output
            $genResult = Invoke-ExternalCommand -FilePath $pythonExe -Arguments @($nvsGen, 'generate', $csvPath, $binPath, $sizeHex)
        }
        if ($genResult.ExitCode -ne 0) {
            throw "nvs_partition_gen failed.`n$($genResult.Output)"
        }
    }
    if (-not (Test-Path $binPath)) {
        throw "NVS binary was not created at $binPath"
    }
    Write-Host "Generated NVS binary at $binPath (size $($nvsSpec.SizeBytes) bytes)"

    if (-not $SkipFlash) {
        Write-Host "Flashing config to $Port at 0x$("{0:X}" -f $flashAddress)..."
        $flashResult = Invoke-ExternalCommand -FilePath $pythonExe -Arguments @(
            $esptool,
            '--chip', 'esp32c6',
            '--port', $Port,
            '--baud', $Baud,
            'write_flash',
            ("0x{0:X}" -f $flashAddress),
            $binPath
        )
        if ($flashResult.ExitCode -ne 0) {
            throw "esptool write_flash failed.`n$($flashResult.Output)"
        }
        Write-Host $flashResult.Output
    } else {
        Write-Host "Skipping flash because -SkipFlash was provided."
    }
}
finally {
    Set-Location $originalLocation
}
