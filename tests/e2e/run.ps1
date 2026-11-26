# Runs an end-to-end script against a real SSH host that accepts your key or agent.
#   .\tests\e2e\run.ps1 -Target fc@10.0.0.111
#   .\tests\e2e\run.ps1 -Target root@10.0.0.108 -Script tests\e2e\basic-session.txt
param(
    [Parameter(Mandatory = $true)][string]$Target,
    [string]$Script = "$PSScriptRoot\basic-session.txt",
    [string]$Config = "Release",
    [int]$Timeout = 20
)

$root = Resolve-Path "$PSScriptRoot\..\.."
$winssh = Join-Path $root "build\$Config\WinSSH.exe"
$driver = Join-Path $root "build\$Config\conpty-run.exe"
$log = Join-Path $root "build\e2e-$($Target -replace '[^A-Za-z0-9._-]', '_').log"

foreach ($exe in @($winssh, $driver)) {
    if (-not (Test-Path $exe)) { throw "Missing $exe - build first (cmake --build --preset release)" }
}

& $driver --timeout $Timeout --log $log --script $Script -- $winssh $Target
$code = $LASTEXITCODE
Write-Host "Raw output saved to $log"
exit $code
