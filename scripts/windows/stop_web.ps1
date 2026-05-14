<#
.SYNOPSIS
Stops the MASM32 simulator development web server started by serve_web.cmd.

.DESCRIPTION
The script first uses build/dev-server.pid to stop the tracked foreground server
process. If that PID is stale, it falls back to the recorded port and stops only
a Python http.server process that is listening on that port. This avoids broad
taskkill commands that might terminate unrelated Python processes.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$RootDir
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

$BuildDir = Join-Path $RootDir "build"
$PidFile = Join-Path $BuildDir "dev-server.pid"
$PortFile = Join-Path $BuildDir "dev-server.port"
$WebDir = Join-Path $RootDir "web"

function Get-TrackedServerProcess {
    param([int]$ProcessId)

    $process = Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId" -ErrorAction SilentlyContinue
    if ($null -eq $process) {
        return $null
    }

    if ($process.CommandLine -like "*http.server*" -and $process.CommandLine -like "*--directory*" -and $process.CommandLine -like "*$WebDir*") {
        return $process
    }

    return $null
}

function Stop-TrackedProcess {
    param([int]$ProcessId)

    $serverProcess = Get-TrackedServerProcess -ProcessId $ProcessId
    if ($null -eq $serverProcess) {
        return $false
    }

    Stop-Process -Id $ProcessId -Force
    Write-Host "Stopped MASM32 simulator web server."
    Write-Host "PID: $ProcessId"
    return $true
}

$stopped = $false

if (Test-Path -LiteralPath $PidFile) {
    $pidText = (Get-Content -LiteralPath $PidFile -Raw).Trim()
    $serverPid = 0
    if ([int]::TryParse($pidText, [ref]$serverPid)) {
        $stopped = Stop-TrackedProcess -ProcessId $serverPid
    } else {
        Write-Host "Tracked server PID file was invalid."
    }
}

if (-not $stopped -and (Test-Path -LiteralPath $PortFile)) {
    $portText = (Get-Content -LiteralPath $PortFile -Raw).Trim()
    $serverPort = 0
    if ([int]::TryParse($portText, [ref]$serverPort)) {
        $listeners = Get-NetTCPConnection -LocalPort $serverPort -State Listen -ErrorAction SilentlyContinue
        foreach ($listener in $listeners) {
            if (Stop-TrackedProcess -ProcessId ([int]$listener.OwningProcess)) {
                $stopped = $true
                break
            }
        }
    }
}

if (Test-Path -LiteralPath $PidFile) {
    Remove-Item -LiteralPath $PidFile -Force
}

if (Test-Path -LiteralPath $PortFile) {
    Remove-Item -LiteralPath $PortFile -Force
}

if (-not $stopped) {
    Write-Host "No tracked MASM32 simulator web server process was found."
    Write-Host "Nothing to stop."
}
