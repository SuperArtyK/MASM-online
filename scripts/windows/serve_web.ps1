<#
.SYNOPSIS
Runs the MASM32 simulator development web server as a tracked foreground process.

.DESCRIPTION
The script starts Python's http.server with the repository web directory as the
served root. The server shares this process' output handles so Visual Studio
External Tools can show the live server log in the Output window. The script also
records the child process identifier in build/dev-server.pid and the selected
port in build/dev-server.port so stop_web.cmd can terminate the server explicitly.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$RootDir,

    [Parameter(Mandatory = $false)]
    [int]$Port = 8000
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

$BuildDir = Join-Path $RootDir "build"
$PidFile = Join-Path $BuildDir "dev-server.pid"
$PortFile = Join-Path $BuildDir "dev-server.port"
$WebDir = Join-Path $RootDir "web"

function Test-ServerProcess {
    param([int]$ProcessId)

    $process = Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId" -ErrorAction SilentlyContinue
    if ($null -eq $process) {
        return $false
    }

    return ($process.CommandLine -like "*http.server*" -and $process.CommandLine -like "*--directory*" -and $process.CommandLine -like "*$WebDir*")
}

if (-not (Test-Path -LiteralPath $WebDir)) {
    Write-Error "Web directory not found: $WebDir"
    exit 1
}

if (-not (Test-Path -LiteralPath $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

if (Test-Path -LiteralPath $PidFile) {
    $existingText = (Get-Content -LiteralPath $PidFile -Raw).Trim()
    $existingPid = 0
    if ([int]::TryParse($existingText, [ref]$existingPid) -and (Test-ServerProcess -ProcessId $existingPid)) {
        Write-Host "MASM32 simulator web server is already running."
        Write-Host "PID: $existingPid"
        Write-Host "URL: http://localhost:$Port"
        Write-Host "Stop with: scripts\windows\stop_web.cmd"
        exit 0
    }

    Remove-Item -LiteralPath $PidFile -Force
}

$pythonCommand = Get-Command python -ErrorAction SilentlyContinue
$arguments = @()
if ($null -ne $pythonCommand) {
    $filePath = $pythonCommand.Source
    $arguments = @("-m", "http.server", [string]$Port, "--directory", $WebDir)
} else {
    $pythonCommand = Get-Command py -ErrorAction SilentlyContinue
    if ($null -eq $pythonCommand) {
        Write-Error "Python was not found. Install Python or add it to PATH."
        exit 1
    }
    $filePath = $pythonCommand.Source
    $arguments = @("-3", "-m", "http.server", [string]$Port, "--directory", $WebDir)
}

$process = $null
try {
    Write-Host "Starting MASM32 simulator web server on http://localhost:$Port"
    Write-Host "Serving: $WebDir"
    Write-Host "Stop with: scripts\windows\stop_web.cmd"

    $process = Start-Process -FilePath $filePath -ArgumentList $arguments -WorkingDirectory $WebDir -PassThru -NoNewWindow
    Set-Content -LiteralPath $PidFile -Value ([string]$process.Id) -Encoding ASCII
    Set-Content -LiteralPath $PortFile -Value ([string]$Port) -Encoding ASCII

    Write-Host "Started MASM32 simulator web server."
    Write-Host "PID: $($process.Id)"
    Write-Host "URL: http://localhost:$Port"
    Write-Host "Server output follows."

    $process.WaitForExit()
    exit $process.ExitCode
} finally {
    if ($null -ne $process -and (Test-Path -LiteralPath $PidFile)) {
        $recordedPid = (Get-Content -LiteralPath $PidFile -Raw).Trim()
        if ($recordedPid -eq ([string]$process.Id)) {
            Remove-Item -LiteralPath $PidFile -Force
        }
    }

    if (Test-Path -LiteralPath $PortFile) {
        Remove-Item -LiteralPath $PortFile -Force
    }
}
