# Runs an end-to-end script against a real SSH host that accepts your key or agent.
#   .\tests\e2e\run.ps1 -Target fc@10.0.0.111
#   .\tests\e2e\run.ps1 -Target root@10.0.0.108 -Script tests\e2e\basic-session.txt
#   .\tests\e2e\run.ps1 -Target fc@10.0.0.111 -Script tests\e2e\custom-rules.txt -ConfigFixture tests\e2e\rules\custom.json
#   .\tests\e2e\run.ps1 -Target root@10.0.0.108 -Script tests\e2e\picker.txt -Picker
#
# Scripts run with the repository root as the current directory. WinSSH uses build\e2e-config as
# its settings folder (WINSSH_CONFIG_DIR), emptied before each run, so your own highlight.json,
# profiles and history never affect a test. -ConfigFixture copies a highlight.json into it.
param(
    [Parameter(Mandatory = $true)][string]$Target,
    [string]$Script = "$PSScriptRoot\basic-session.txt",
    [string]$ConfigFixture = "",
    [string]$Config = "Release",
    # Build folder with WinSSH.exe and conpty-run.exe (default: <repo>\build)
    [string]$BuildDir = "",
    [int]$Timeout = 20,
    [string[]]$ExtraArgs = @(),
    # Save -Target as profile "e2e" (with -ProfileArgs) and connect by that name instead.
    [switch]$AsProfile,
    [string[]]$ProfileArgs = @(),
    # Start WinSSH with no destination (the picker). Saves -Target as profile "e2e" first,
    # unless -NoProfile is given.
    [switch]$Picker,
    [switch]$NoProfile
)

$root = Resolve-Path "$PSScriptRoot\..\.."
if (-not $BuildDir) { $BuildDir = Join-Path $root "build" }
$winssh = Join-Path $BuildDir "$Config\WinSSH.exe"
$driver = Join-Path $BuildDir "$Config\conpty-run.exe"
$log = Join-Path $root "build\e2e-$($Target -replace '[^A-Za-z0-9._-]', '_').log"
$configDir = Join-Path $root "build\e2e-config"

foreach ($exe in @($winssh, $driver)) {
    if (-not (Test-Path $exe)) { throw "Missing $exe - build first (cmake --build --preset release)" }
}

# Scripts may use ${TARGET} (as given, e.g. root@10.0.0.115:2222) and ${HOST} (10.0.0.115), so they
# do not depend on a device's address.
$targetHost = ($Target -replace '^[^@]*@', '') -replace ':\d+$', ''
$scriptText = (Get-Content -Raw (Resolve-Path $Script)).Replace('${TARGET}', $Target).Replace('${HOST}', $targetHost)
New-Item -ItemType Directory -Force (Join-Path $root "build") | Out-Null
$scriptPath = Join-Path $root "build\e2e-script.txt"
Set-Content -Path $scriptPath -Value $scriptText -Encoding utf8NoBOM -NoNewline
Remove-Item -Recurse -Force $configDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $configDir | Out-Null
if ($ConfigFixture) {
    Copy-Item (Resolve-Path $ConfigFixture) (Join-Path $configDir "highlight.json")
}

# Session logs go to build\e2e-logs (emptied first), so scripts can check them with expect-file.
$logDir = Join-Path $root "build\e2e-logs"
Remove-Item -Recurse -Force $logDir -ErrorAction SilentlyContinue

$oldConfig = $env:WINSSH_CONFIG_DIR
$oldLogDir = $env:WINSSH_LOG_DIR
$env:WINSSH_CONFIG_DIR = $configDir
$env:WINSSH_LOG_DIR = $logDir
Push-Location $root
try {
    $destination = @($Target)
    if (($AsProfile -or $Picker) -and -not $NoProfile) {
        & $winssh --save e2e @ProfileArgs $Target | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "winssh --save failed" }
        $destination = @("e2e")
    }
    if ($Picker) { $destination = @() }
    & $driver --timeout $Timeout --log $log --script $scriptPath -- $winssh @ExtraArgs @destination
    $code = $LASTEXITCODE
}
finally {
    Pop-Location
    $env:WINSSH_CONFIG_DIR = $oldConfig
    $env:WINSSH_LOG_DIR = $oldLogDir
}
Write-Host "Raw output saved to $log"
exit $code
