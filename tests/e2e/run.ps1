# Runs an end-to-end script against a real SSH host that accepts your key or agent.
#   .\tests\e2e\run.ps1 -Target fc@10.0.0.111
#   .\tests\e2e\run.ps1 -Target root@10.0.0.108 -Script tests\e2e\basic-session.txt
#   .\tests\e2e\run.ps1 -Target fc@10.0.0.111 -Script tests\e2e\custom-rules.txt -ConfigFixture tests\e2e\rules\custom.json
#
# Scripts run with the repository root as the current directory. With -ConfigFixture, WinSSH uses
# build\e2e-config as its settings folder (WINSSH_CONFIG_DIR) and the fixture as highlight.json;
# otherwise it uses an empty settings folder, so your own highlight.json never affects a test.
param(
    [Parameter(Mandatory = $true)][string]$Target,
    [string]$Script = "$PSScriptRoot\basic-session.txt",
    [string]$ConfigFixture = "",
    [string]$Config = "Release",
    [int]$Timeout = 20,
    [string[]]$ExtraArgs = @(),
    # Save -Target as profile "e2e" (with -ProfileArgs) and connect by that name instead.
    [switch]$AsProfile,
    [string[]]$ProfileArgs = @()
)

$root = Resolve-Path "$PSScriptRoot\..\.."
$winssh = Join-Path $root "build\$Config\WinSSH.exe"
$driver = Join-Path $root "build\$Config\conpty-run.exe"
$log = Join-Path $root "build\e2e-$($Target -replace '[^A-Za-z0-9._-]', '_').log"
$configDir = Join-Path $root "build\e2e-config"

foreach ($exe in @($winssh, $driver)) {
    if (-not (Test-Path $exe)) { throw "Missing $exe - build first (cmake --build --preset release)" }
}

$scriptPath = Resolve-Path $Script
Remove-Item -Recurse -Force $configDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $configDir | Out-Null
if ($ConfigFixture) {
    Copy-Item (Resolve-Path $ConfigFixture) (Join-Path $configDir "highlight.json")
}

$oldConfig = $env:WINSSH_CONFIG_DIR
$env:WINSSH_CONFIG_DIR = $configDir
Push-Location $root
try {
    $destination = $Target
    if ($AsProfile) {
        & $winssh --save e2e @ProfileArgs $Target | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "winssh --save failed" }
        $destination = "e2e"
    }
    & $driver --timeout $Timeout --log $log --script $scriptPath -- $winssh @ExtraArgs $destination
    $code = $LASTEXITCODE
}
finally {
    Pop-Location
    $env:WINSSH_CONFIG_DIR = $oldConfig
}
Write-Host "Raw output saved to $log"
exit $code
