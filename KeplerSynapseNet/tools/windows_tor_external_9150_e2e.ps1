param(
    [Parameter(Mandatory = $true)]
    [string[]]$BridgesFile,
    [string]$OutputRoot = "",
    [string]$SocksHost = "127.0.0.1",
    [int]$SocksPort = 9150,
    [int]$ControlPort = 9151,
    [string]$DataDir = "$env:TEMP\synapsenet_tor_obfs4_windows",
    [int]$BootstrapTimeoutSec = 360,
    [int]$ProbeMaxTime = 30,
    [int]$BootstrapCheckTimeoutSec = 180,
    [switch]$SkipControlBootstrapCheck,
    [switch]$KeepExternalRunning,
    [string]$TorBin = "",
    [string]$Obfs4ProxyBin = "",
    [string]$TorRoot = ""
)

$ErrorActionPreference = "Stop"

function Write-Log([string]$Message) {
    $ts = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    Write-Host "[$ts] $Message"
}

function Fail([string]$Message) {
    throw $Message
}

function Ensure-ParentDir([string]$Path) {
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
}

function Read-PidFromFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return 0 }
    $raw = (Get-Content -LiteralPath $Path -ErrorAction SilentlyContinue | Select-Object -First 1).Trim()
    $pid = 0
    if ([int]::TryParse($raw, [ref]$pid)) {
        return $pid
    }
    return 0
}

function Stop-ExternalTorIfNeeded {
    if ($KeepExternalRunning) { return }
    $pidFile = Join-Path $DataDir "tor.pid"
    $pid = Read-PidFromFile -Path $pidFile
    if ($pid -le 0) { return }
    $null = & taskkill /PID $pid /T /F 2>$null
}

function Copy-BootstrapArtifacts([string]$RunDir) {
    if (-not (Test-Path -LiteralPath $DataDir)) { return }
    $patterns = @("tor_bootstrap*.log", "tor.pid")
    foreach ($pattern in $patterns) {
        Get-ChildItem -LiteralPath $DataDir -Filter $pattern -ErrorAction SilentlyContinue | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $RunDir -Force -ErrorAction SilentlyContinue
        }
    }
}

$BridgeFiles = @($BridgesFile | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
if ($BridgeFiles.Count -eq 0) {
    Fail "bridges file list is empty"
}
foreach ($bridgeFileItem in $BridgeFiles) {
    if (-not (Test-Path -LiteralPath $bridgeFileItem)) {
        Fail "bridges file not found: $bridgeFileItem"
    }
}

$rootDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$helper = Join-Path $rootDir "tools\windows_tor_obfs4_smoke.ps1"
$validator = Join-Path $rootDir "tools\windows_tor_validate.ps1"

if (-not (Test-Path -LiteralPath $helper)) { Fail "helper not found: $helper" }
if (-not (Test-Path -LiteralPath $validator)) { Fail "validator not found: $validator" }

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $rootDir "build\evidence"
}
$runId = (Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssZ")
$runDir = Join-Path $OutputRoot ("windows_tor_external_9150_e2e_" + $runId)
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$helperLog = Join-Path $runDir "helper.log"
$validatorLog = Join-Path $runDir "validator.log"
$summaryPath = Join-Path $runDir "summary.json"
$snippetPath = Join-Path $runDir "synapsenet_external.conf"
$torrcPath = Join-Path $runDir "tor-obfs4.conf"
$helperLogs = [System.Collections.Generic.List[string]]::new()
$selectedBridgesFile = ""

$helperOk = $false
$validatorOk = $false
$bootstrapMode = "control"
if ($SkipControlBootstrapCheck) { $bootstrapMode = "no-control-port" }

Write-Log "Windows external-9150 e2e start"
Write-Log "run_dir=$runDir"
foreach ($bridgeFileItem in $BridgeFiles) {
    Write-Log "bridges_file=$bridgeFileItem"
}

try {
    for ($i = 0; $i -lt $BridgeFiles.Count; $i++) {
        $bridgeFileItem = $BridgeFiles[$i]
        $safeName = [System.Text.RegularExpressions.Regex]::Replace($bridgeFileItem, '[^A-Za-z0-9._-]', '_')
        if ($safeName.Length -gt 64) { $safeName = $safeName.Substring(0, 64) }
        if ([string]::IsNullOrWhiteSpace($safeName)) { $safeName = "bridges" }
        $helperLog = Join-Path $runDir ("helper_{0}_{1}.log" -f $i, $safeName)
        [void]$helperLogs.Add($helperLog)

        $helperArgs = @(
            "-File", $helper,
            "-Mode", "user",
            "-BridgesFile", $bridgeFileItem,
            "-SocksHost", $SocksHost,
            "-SocksPort", "$SocksPort",
            "-ControlPort", "$ControlPort",
            "-DataDir", $DataDir,
            "-BootstrapCheck",
            "-BootstrapTimeoutSec", "$BootstrapTimeoutSec",
            "-ProbeMaxTime", "$ProbeMaxTime",
            "-KeepRunning",
            "-OutFile", $torrcPath,
            "-WriteSynapseNetSnippet", $snippetPath
        )
        if (-not [string]::IsNullOrWhiteSpace($TorRoot)) {
            $helperArgs += @("-TorRoot", $TorRoot)
        }
        if (-not [string]::IsNullOrWhiteSpace($TorBin)) {
            $helperArgs += @("-TorBin", $TorBin)
        }
        if (-not [string]::IsNullOrWhiteSpace($Obfs4ProxyBin)) {
            $helperArgs += @("-Obfs4ProxyBin", $Obfs4ProxyBin)
        }

        & pwsh @helperArgs *>&1 | Tee-Object -FilePath $helperLog | Out-Null
        if ($LASTEXITCODE -eq 0) {
            $helperOk = $true
            $selectedBridgesFile = $bridgeFileItem
            Write-Log "helper bootstrap succeeded with bridges_file=$bridgeFileItem"
            break
        }
        Write-Log "helper bootstrap failed with bridges_file=$bridgeFileItem (next fallback if available)"
    }
} catch {
    $_ | Out-String | Add-Content -Path $helperLog -Encoding UTF8
}

Copy-BootstrapArtifacts -RunDir $runDir

if ($helperOk) {
    try {
        $validatorArgs = @(
            "-File", $validator,
            "-SocksHost", $SocksHost,
            "-SocksPort", "$SocksPort",
            "-ControlHost", "127.0.0.1",
            "-ControlPort", "$ControlPort",
            "-BootstrapTimeoutSec", "$BootstrapCheckTimeoutSec",
            "-ProbeMaxTime", "$ProbeMaxTime"
        )
        if ($SkipControlBootstrapCheck) {
            $validatorArgs += "-SkipControlBootstrapCheck"
        }
        & pwsh @validatorArgs *>&1 | Tee-Object -FilePath $validatorLog | Out-Null
        if ($LASTEXITCODE -eq 0) {
            $validatorOk = $true
        }
    } catch {
        $_ | Out-String | Add-Content -Path $validatorLog -Encoding UTF8
    }
}

$summary = [ordered]@{
    ok = ($helperOk -and $validatorOk)
    helperOk = $helperOk
    validatorOk = $validatorOk
    runDir = $runDir
    helperLog = $helperLog
    helperLogs = @($helperLogs.ToArray())
    validatorLog = $validatorLog
    bridgeFiles = $BridgeFiles
    selectedBridgeFile = $selectedBridgesFile
    bootstrapMode = $bootstrapMode
    keepExternalRunning = [bool]$KeepExternalRunning
}
$summary | ConvertTo-Json -Depth 5 | Set-Content -Path $summaryPath -Encoding UTF8

Stop-ExternalTorIfNeeded

if ($summary.ok) {
    Write-Log "e2e success (summary: $summaryPath)"
    exit 0
}

Write-Log "e2e failed (summary: $summaryPath)"
exit 1
