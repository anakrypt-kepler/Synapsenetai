param(
    [ValidateSet("user", "service")]
    [string]$Mode = "user",
    [string]$ServiceName = "tor",
    [switch]$ServiceStart,
    [string]$OutFile = "",
    [string[]]$Bridge = @(),
    [string[]]$BridgesFile = @(),
    [string]$SocksHost = "127.0.0.1",
    [int]$SocksPort = 9150,
    [int]$ControlPort = 9151,
    [string]$DataDir = "$env:TEMP\synapsenet_tor_obfs4_windows",
    [switch]$BootstrapCheck,
    [int]$BootstrapTimeoutSec = 240,
    [string]$ProbeUrl = "https://html.duckduckgo.com/html/?q=synapsenet+tor+validation",
    [int]$ProbeMaxTime = 30,
    [switch]$KeepRunning,
    [string]$WriteSynapseNetSnippet = "",
    [string]$TorBin = "",
    [string]$Obfs4ProxyBin = "",
    [string]$TorRoot = "",
    [switch]$SkipOwnerCheck
)

$ErrorActionPreference = "Stop"

function Write-Log([string]$Message) {
    $ts = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    Write-Host "[$ts] $Message"
}

function Fail([string]$Message) {
    throw $Message
}

function Normalize-PathForCompare([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return "" }
    return $Path.ToLowerInvariant().Replace("/", "\")
}

function To-TorConfigPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return "" }
    return $Path.Replace("\", "/")
}

function Resolve-ExistingPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    if (Test-Path -LiteralPath $Path) {
        return (Resolve-Path -LiteralPath $Path).Path
    }
    return $null
}

function Get-SearchRoots {
    $roots = [System.Collections.Generic.List[string]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

    $candidates = @(
        $TorRoot,
        "$env:ProgramFiles\Tor Browser\Browser\TorBrowser\Tor",
        "${env:ProgramFiles(x86)}\Tor Browser\Browser\TorBrowser\Tor",
        "$env:LOCALAPPDATA\Tor Browser\Browser\TorBrowser\Tor",
        "$env:USERPROFILE\Desktop\Tor Browser\Browser\TorBrowser\Tor",
        "$env:ProgramFiles\Tor",
        "${env:ProgramFiles(x86)}\Tor",
        "$env:ProgramData\Tor",
        "$env:SystemDrive\Tor",
        "$env:USERPROFILE\Tor"
    )

    foreach ($c in $candidates) {
        if ([string]::IsNullOrWhiteSpace($c)) { continue }
        $expanded = [Environment]::ExpandEnvironmentVariables($c)
        if ($seen.Add($expanded)) {
            [void]$roots.Add($expanded)
        }
    }

    return ,$roots.ToArray()
}

function Resolve-TorBin {
    $direct = Resolve-ExistingPath $TorBin
    if ($direct) { return $direct }

    $cmd = Get-Command tor.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    foreach ($root in (Get-SearchRoots)) {
        $candidateList = @(
            $root,
            (Join-Path $root "tor.exe"),
            (Join-Path $root "Tor\tor.exe"),
            (Join-Path $root "Browser\TorBrowser\Tor\tor.exe")
        )
        foreach ($candidate in $candidateList) {
            $resolved = Resolve-ExistingPath $candidate
            if (-not $resolved) { continue }
            if ($resolved.ToLowerInvariant().EndsWith("tor.exe")) {
                return $resolved
            }
        }
    }

    return $null
}

function Resolve-Obfs4ProxyBin {
    $direct = Resolve-ExistingPath $Obfs4ProxyBin
    if ($direct) { return $direct }

    $cmd = Get-Command obfs4proxy.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $cmd = Get-Command lyrebird.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    foreach ($root in (Get-SearchRoots)) {
        $candidateList = @(
            (Join-Path $root "obfs4proxy.exe"),
            (Join-Path $root "lyrebird.exe"),
            (Join-Path $root "PluggableTransports\obfs4proxy.exe"),
            (Join-Path $root "PluggableTransports\lyrebird.exe"),
            (Join-Path $root "Tor\PluggableTransports\obfs4proxy.exe"),
            (Join-Path $root "Tor\PluggableTransports\lyrebird.exe")
        )
        foreach ($candidate in $candidateList) {
            $resolved = Resolve-ExistingPath $candidate
            if ($resolved) { return $resolved }
        }
    }

    return $null
}

function Normalize-BridgeLine([string]$Raw) {
    if ($null -eq $Raw) { return @{ Status="Skip"; Value=""; Error="" } }
    $line = $Raw.Trim()
    if ([string]::IsNullOrWhiteSpace($line)) { return @{ Status="Skip"; Value=""; Error="" } }
    if ($line.StartsWith("#")) { return @{ Status="Skip"; Value=""; Error="" } }
    if ($line.StartsWith("obfs4 ")) { $line = "Bridge $line" }
    if (-not $line.StartsWith("Bridge obfs4 ")) {
        return @{ Status="Invalid"; Value=""; Error="expected 'Bridge obfs4 ...'" }
    }
    return @{ Status="Valid"; Value=$line; Error="" }
}

function Collect-Bridges {
    $seen = [System.Collections.Generic.HashSet[string]]::new()
    $out = [System.Collections.Generic.List[string]]::new()

    foreach ($item in $Bridge) {
        $parsed = Normalize-BridgeLine $item
        if ($parsed.Status -eq "Valid" -and $seen.Add($parsed.Value)) {
            [void]$out.Add($parsed.Value)
        }
    }

    foreach ($file in $BridgesFile) {
        if (-not (Test-Path -LiteralPath $file)) { Fail "bridges file not found: $file" }
        foreach ($line in Get-Content -LiteralPath $file) {
            $parsed = Normalize-BridgeLine $line
            if ($parsed.Status -eq "Valid" -and $seen.Add($parsed.Value)) {
                [void]$out.Add($parsed.Value)
            }
        }
    }

    if ($out.Count -eq 0) { Fail "no valid obfs4 bridge lines provided" }
    return ,$out.ToArray()
}

function Ensure-ParentDirectory([string]$Path) {
    $dir = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
}

function Get-DefaultOutFile {
    if ($Mode -eq "service") {
        return "$env:ProgramData\tor\torrc.d\synapsenet-obfs4.conf"
    }
    return "$env:TEMP\tor-obfs4-windows.conf"
}

function Write-Torrc([string]$Path, [string]$PluginBin, [string[]]$Bridges) {
    Ensure-ParentDirectory $Path

    $pluginPath = To-TorConfigPath $PluginBin
    $lines = [System.Collections.Generic.List[string]]::new()
    [void]$lines.Add("SocksPort $SocksHost`:$SocksPort")
    if ($ControlPort -ne 0) {
        [void]$lines.Add("ControlPort 127.0.0.1`:$ControlPort")
    }
    [void]$lines.Add("UseBridges 1")
    [void]$lines.Add("ClientTransportPlugin obfs4 exec `"$pluginPath`"")
    [void]$lines.Add("")
    foreach ($b in $Bridges) {
        [void]$lines.Add($b)
    }

    [System.IO.File]::WriteAllLines($Path, $lines, [System.Text.Encoding]::ASCII)
}

function Verify-Torrc([string]$TorExe, [string]$Path) {
    $output = & $TorExe --verify-config -f $Path 2>&1
    if ($LASTEXITCODE -ne 0) {
        $output | Out-String | Write-Error
        Fail "tor config verification failed for $Path"
    }
}

function Write-SynapseNetSnippet([string]$Path) {
    Ensure-ParentDirectory $Path
    @(
        "agent.tor.required=true",
        "agent.tor.mode=external",
        "agent.routing.allow_clearnet_fallback=false",
        "agent.routing.allow_p2p_clearnet_fallback=false",
        "agent.tor.socks_host=$SocksHost",
        "agent.tor.socks_port=$SocksPort",
        "tor.socks.host=$SocksHost",
        "tor.socks.port=$SocksPort"
    ) | Set-Content -Path $Path -Encoding ascii
}

function Get-TasklistEntry([int]$Pid) {
    $output = & tasklist /FO CSV /NH /FI "PID eq $Pid" 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $output) { return $null }
    $first = $output | Select-Object -First 1
    if (-not $first -or $first -like "INFO:*") { return $null }
    try {
        return ($output | ConvertFrom-Csv | Select-Object -First 1)
    } catch {
        return $null
    }
}

function Get-ProcessMetadata([int]$Pid) {
    $task = Get-TasklistEntry -Pid $Pid
    if (-not $task) { return $null }

    $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $Pid" -ErrorAction SilentlyContinue
    if (-not $proc) { return $null }

    $ownerUser = ""
    if (-not $SkipOwnerCheck) {
        try {
            $owner = Invoke-CimMethod -InputObject $proc -MethodName GetOwner -ErrorAction Stop
            if ($owner.ReturnValue -eq 0 -and $owner.User) {
                $ownerUser = [string]$owner.User
            }
        } catch {
        }
    }

    return [pscustomobject]@{
        Pid = $Pid
        ImageName = [string]$task."Image Name"
        CommandLine = [string]$proc.CommandLine
        Owner = $ownerUser
    }
}

function Test-ManagedTorOwnership([int]$Pid, [string]$TorrcPath, [string]$RuntimeDataDir) {
    $meta = Get-ProcessMetadata -Pid $Pid
    if (-not $meta) { return $false }
    if ($meta.ImageName -and -not $meta.ImageName.Equals("tor.exe", [System.StringComparison]::OrdinalIgnoreCase)) {
        return $false
    }

    $cmd = Normalize-PathForCompare $meta.CommandLine
    $torNeedle = Normalize-PathForCompare $TorrcPath
    $dataNeedle = Normalize-PathForCompare $RuntimeDataDir

    if ([string]::IsNullOrWhiteSpace($cmd)) { return $false }
    if (-not $cmd.Contains($torNeedle)) { return $false }
    if (-not $cmd.Contains($dataNeedle)) { return $false }

    if (-not $SkipOwnerCheck) {
        if ([string]::IsNullOrWhiteSpace($meta.Owner)) { return $false }
        if (-not $meta.Owner.Equals($env:USERNAME, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $false
        }
    }

    return $true
}

function Stop-ManagedTorProcess([int]$Pid, [string]$TorrcPath, [string]$RuntimeDataDir) {
    if ($Pid -le 0) { return }

    if (-not (Test-ManagedTorOwnership -Pid $Pid -TorrcPath $TorrcPath -RuntimeDataDir $RuntimeDataDir)) {
        Write-Log "skip taskkill for pid $Pid (ownership/fingerprint mismatch)"
        return
    }

    $null = & taskkill /PID $Pid /T /F 2>$null

    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        if (-not (Get-TasklistEntry -Pid $Pid)) {
            return
        }
        Start-Sleep -Milliseconds 400
    }

    Write-Log "taskkill requested but pid $Pid is still visible after timeout"
}

function Test-PortOpen([string]$Host, [int]$Port, [int]$TimeoutMs = 1000) {
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $async = $client.BeginConnect($Host, $Port, $null, $null)
        if (-not $async.AsyncWaitHandle.WaitOne($TimeoutMs)) {
            return $false
        }
        $client.EndConnect($async)
        return $true
    } catch {
        return $false
    } finally {
        $client.Close()
    }
}

function Invoke-SocksProbe([string]$Url, [string]$UserAgent) {
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if (-not $curl) { Fail "curl.exe not found (required for SOCKS probe)" }

    & $curl.Source --silent --show-error --location `
        --socks5-hostname "$SocksHost`:$SocksPort" `
        --connect-timeout 20 `
        --max-time $ProbeMaxTime `
        --user-agent $UserAgent `
        --output NUL `
        $Url

    return ($LASTEXITCODE -eq 0)
}

function Wait-ForSocksProbeReady([string]$Url, [string]$UserAgent) {
    $deadline = (Get-Date).AddSeconds($BootstrapTimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Invoke-SocksProbe -Url $Url -UserAgent $UserAgent) {
            return $true
        }
        Start-Sleep -Seconds 2
    }
    return $false
}

function Wait-ForBootstrap([string]$LogFile) {
    $deadline = (Get-Date).AddSeconds($BootstrapTimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $LogFile) {
            if (Select-String -Path $LogFile -Pattern 'Bootstrapped 100% \(done\): Done' -Quiet) {
                return $true
            }
        }
        Start-Sleep -Seconds 1
    }
    return $false
}

function Ensure-DataDirectory([string]$Path) {
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
    $probe = Join-Path $Path "write_probe.tmp"
    try {
        "ok" | Set-Content -Path $probe -Encoding ascii
        Remove-Item -LiteralPath $probe -Force -ErrorAction SilentlyContinue
    } catch {
        Fail "DataDirectory is not writable: $Path"
    }
}

function Bootstrap-CheckUserMode([string]$TorExe, [string]$TorrcPath) {
    Ensure-DataDirectory -Path $DataDir

    if (Test-PortOpen -Host $SocksHost -Port $SocksPort -TimeoutMs 500) {
        Fail "SOCKS port $SocksHost`:$SocksPort already in use; stop conflicting runtime or change port"
    }

    $logFile = Join-Path $DataDir "tor_bootstrap.log"
    $pidFile = Join-Path $DataDir "tor.pid"

    if (Test-Path -LiteralPath $logFile) { Remove-Item -LiteralPath $logFile -Force }
    if (Test-Path -LiteralPath $pidFile) { Remove-Item -LiteralPath $pidFile -Force }

    $args = @(
        "-f", $TorrcPath,
        "--DataDirectory", $DataDir,
        "--PidFile", $pidFile,
        "--RunAsDaemon", "1",
        "--Log", "notice file $logFile"
    )

    $startOut = & $TorExe @args 2>&1
    if ($LASTEXITCODE -ne 0) {
        $startOut | Out-String | Write-Error
        Fail "failed to start tor for bootstrap check"
    }

    $pidValue = ""
    if (Test-Path -LiteralPath $pidFile) {
        $pidValue = (Get-Content -LiteralPath $pidFile -Raw).Trim()
    }

    if (-not (Wait-ForBootstrap -LogFile $logFile)) {
        if ($pidValue -match '^\d+$') {
            Stop-ManagedTorProcess -Pid ([int]$pidValue) -TorrcPath $TorrcPath -RuntimeDataDir $DataDir
        }
        if (Test-Path -LiteralPath $logFile) {
            Get-Content -LiteralPath $logFile -Tail 120 | Write-Host
        }
        Fail "Tor bootstrap did not reach 100% within $BootstrapTimeoutSec s"
    }

    if (-not (Invoke-SocksProbe -Url $ProbeUrl -UserAgent "SynapseNet-windows-obfs4-smoke/1.0")) {
        if ($pidValue -match '^\d+$') {
            Stop-ManagedTorProcess -Pid ([int]$pidValue) -TorrcPath $TorrcPath -RuntimeDataDir $DataDir
        }
        Fail "curl probe via Tor SOCKS failed: $ProbeUrl"
    }

    Write-Log "bootstrap check passed (Tor 100% + DDG probe via $SocksHost`:$SocksPort)"
    if ($pidValue -match '^\d+$') {
        Write-Log "tor pid: $pidValue"
        if (-not $KeepRunning) {
            Stop-ManagedTorProcess -Pid ([int]$pidValue) -TorrcPath $TorrcPath -RuntimeDataDir $DataDir
            Write-Log "stopped temporary Tor runtime after bootstrap validation"
        } else {
            Write-Log "leaving Tor running (-KeepRunning)"
        }
    }
}

function Bootstrap-CheckServiceMode {
    $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if (-not $svc) {
        Fail "Windows service '$ServiceName' not found"
    }

    if ($ServiceStart) {
        if ($svc.Status -eq "Running") {
            Restart-Service -Name $ServiceName -ErrorAction Stop
        } else {
            Start-Service -Name $ServiceName -ErrorAction Stop
        }
        $svc = Get-Service -Name $ServiceName -ErrorAction Stop
    }

    if ($svc.Status -ne "Running") {
        Fail "Windows service '$ServiceName' is not running"
    }

    if (-not (Wait-ForSocksProbeReady -Url $ProbeUrl -UserAgent "SynapseNet-windows-service-smoke/1.0")) {
        Fail "service mode probe did not succeed within $BootstrapTimeoutSec s"
    }

    Write-Log "service mode readiness check passed (service=$ServiceName, socks=$SocksHost`:$SocksPort)"
}

if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $OutFile = Get-DefaultOutFile
}

$resolvedTor = Resolve-TorBin
if (-not $resolvedTor) {
    Fail "tor.exe not found (set -TorBin or -TorRoot; supported roots: Tor Browser / Expert Bundle / custom path)"
}

$resolvedObfs4 = Resolve-Obfs4ProxyBin
if (-not $resolvedObfs4) {
    Fail "obfs4 transport binary not found (set -Obfs4ProxyBin or -TorRoot; expected obfs4proxy.exe or lyrebird.exe)"
}

$bridges = Collect-Bridges
Write-Torrc -Path $OutFile -PluginBin $resolvedObfs4 -Bridges $bridges
Verify-Torrc -TorExe $resolvedTor -Path $OutFile

Write-Log "mode: $Mode"
Write-Log "wrote torrc: $OutFile"
Write-Log "tor binary: $resolvedTor"
Write-Log "transport plugin: $resolvedObfs4"
Write-Log "bridges: $($bridges.Count)"
Write-Log "socks: $SocksHost`:$SocksPort"
if ($ControlPort -ne 0) {
    Write-Log "control: 127.0.0.1:$ControlPort"
}
if ($Mode -eq "service") {
    Write-Log "service: $ServiceName"
}

if ($WriteSynapseNetSnippet) {
    Write-SynapseNetSnippet -Path $WriteSynapseNetSnippet
    Write-Log "wrote SynapseNet external-Tor snippet: $WriteSynapseNetSnippet"
}

if ($BootstrapCheck) {
    if ($Mode -eq "service") {
        Bootstrap-CheckServiceMode
    } else {
        Bootstrap-CheckUserMode -TorExe $resolvedTor -TorrcPath $OutFile
    }
} else {
    Write-Log "bootstrap check skipped (use -BootstrapCheck to validate SOCKS/bootstrap/DDG probe)"
}
