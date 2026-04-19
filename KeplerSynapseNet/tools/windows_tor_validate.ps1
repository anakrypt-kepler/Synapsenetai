param(
    [string]$SocksHost = "127.0.0.1",
    [int]$SocksPort = 9150,
    [string]$ControlHost = "127.0.0.1",
    [int]$ControlPort = 9151,
    [int]$BootstrapTimeoutSec = 180,
    [string]$ProbeUrl = "https://html.duckduckgo.com/html/?q=synapsenet+tor+validation",
    [int]$ProbeMaxTime = 30,
    [switch]$SkipControlBootstrapCheck
)

$ErrorActionPreference = "Stop"

function Write-Log([string]$Message) {
    $ts = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    Write-Host "[$ts] $Message"
}

function Fail([string]$Message) {
    throw $Message
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

function Wait-PortOpen([string]$Host, [int]$Port, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-PortOpen -Host $Host -Port $Port -TimeoutMs 700) {
            return $true
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Try-GetControlBootstrapPercent {
    $client = $null
    $stream = $null
    $writer = $null
    $reader = $null

    try {
        $client = [System.Net.Sockets.TcpClient]::new()
        $client.ReceiveTimeout = 3000
        $client.SendTimeout = 3000
        $client.Connect($ControlHost, $ControlPort)

        $stream = $client.GetStream()
        $stream.ReadTimeout = 3000
        $stream.WriteTimeout = 3000

        $writer = New-Object System.IO.StreamWriter($stream, [System.Text.Encoding]::ASCII)
        $writer.NewLine = "`r`n"
        $writer.AutoFlush = $true

        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::ASCII)

        $writer.WriteLine("AUTHENTICATE")
        $writer.WriteLine("GETINFO status/bootstrap-phase")
        $writer.WriteLine("QUIT")

        $raw = $reader.ReadToEnd()
        if (-not $raw) { return -1 }

        $match = [regex]::Match($raw, "PROGRESS=(\d+)")
        if ($match.Success) {
            return [int]$match.Groups[1].Value
        }

        return -1
    } catch {
        return -1
    } finally {
        if ($reader) { $reader.Dispose() }
        if ($writer) { $writer.Dispose() }
        if ($stream) { $stream.Dispose() }
        if ($client) { $client.Close() }
    }
}

function Wait-ControlBootstrap100([int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $sawControl = $false

    while ((Get-Date) -lt $deadline) {
        if (-not (Test-PortOpen -Host $ControlHost -Port $ControlPort -TimeoutMs 700)) {
            Start-Sleep -Seconds 1
            continue
        }

        $pct = Try-GetControlBootstrapPercent
        if ($pct -ge 0) {
            $sawControl = $true
            if ($pct -ge 100) {
                return @{ Ok = $true; SawControl = $true; Percent = $pct }
            }
        }

        Start-Sleep -Seconds 1
    }

    if ($sawControl) {
        return @{ Ok = $false; SawControl = $true; Percent = -1 }
    }

    return @{ Ok = $false; SawControl = $false; Percent = -1 }
}

function Invoke-SocksDdgProbe([string]$Url) {
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if (-not $curl) { Fail "curl.exe not found" }

    $tmp = Join-Path $env:TEMP ("synapsenet_windows_tor_validate_" + [Guid]::NewGuid().ToString("N") + ".html")
    try {
        & $curl.Source --silent --show-error --location `
            --socks5-hostname "$SocksHost`:$SocksPort" `
            --connect-timeout 20 `
            --max-time $ProbeMaxTime `
            --user-agent "SynapseNet-windows-tor-validate/1.0" `
            --output $tmp `
            $Url

        if ($LASTEXITCODE -ne 0) {
            return @{ Ok = $false; Bytes = 0 }
        }

        if (-not (Test-Path -LiteralPath $tmp)) {
            return @{ Ok = $false; Bytes = 0 }
        }

        $bytes = (Get-Item -LiteralPath $tmp).Length
        if ($bytes -lt 64) {
            return @{ Ok = $false; Bytes = $bytes }
        }

        return @{ Ok = $true; Bytes = $bytes }
    } finally {
        if (Test-Path -LiteralPath $tmp) {
            Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
        }
    }
}

Write-Log "validation start"
Write-Log "socks endpoint: $SocksHost`:$SocksPort"
Write-Log "control endpoint: $ControlHost`:$ControlPort"
Write-Log "probe url: $ProbeUrl"

if (-not (Wait-PortOpen -Host $SocksHost -Port $SocksPort -TimeoutSec $BootstrapTimeoutSec)) {
    Fail "SOCKS port is not reachable within $BootstrapTimeoutSec s: $SocksHost`:$SocksPort"
}

$bootstrapResult = @{ Ok = $true; SawControl = $false; Percent = -1; Mode = "skipped" }
if (-not $SkipControlBootstrapCheck) {
    $control = Wait-ControlBootstrap100 -TimeoutSec $BootstrapTimeoutSec
    if ($control.SawControl) {
        if (-not $control.Ok) {
            Fail "control port was reachable but bootstrap did not reach 100% within timeout"
        }
        $bootstrapResult = @{ Ok = $true; SawControl = $true; Percent = $control.Percent; Mode = "control" }
    } else {
        $bootstrapResult = @{ Ok = $true; SawControl = $false; Percent = -1; Mode = "no-control-port" }
        Write-Log "control bootstrap check skipped: control port unavailable or not queryable"
    }
}

$probe = Invoke-SocksDdgProbe -Url $ProbeUrl
if (-not $probe.Ok) {
    Fail "DuckDuckGo probe via SOCKS failed"
}

$summary = [ordered]@{
    ok = $true
    socks = "$SocksHost`:$SocksPort"
    control = "$ControlHost`:$ControlPort"
    bootstrap = $bootstrapResult
    ddgProbe = [ordered]@{
        ok = $true
        bytes = $probe.Bytes
        url = $ProbeUrl
    }
}

Write-Log "validation success (SOCKS + bootstrap path + DDG probe)"
$summary | ConvertTo-Json -Depth 5
