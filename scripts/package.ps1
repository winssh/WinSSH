# Packages a built WinSSH into build\dist:
#   WinSSH-portable-<version>.zip   WinSSH.exe + README + LICENSE + docs
#   WinSSH-setup-<version>.exe      Inno Setup installer (needs ISCC.exe, Inno Setup 6)
#
#   .\scripts\package.ps1 -Version 0.2.0
#   .\scripts\package.ps1 -Version 0.2.0-rc1 -BuildDir build -SkipInstaller
#
# The build must have been configured with the same version (-DWINSSH_VERSION=<version>);
# the script checks WinSSH.exe --version.
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$BuildDir = "",
    [string]$Config = "Release",
    [switch]$SkipInstaller
)
$ErrorActionPreference = "Stop"

if ($Version -notmatch '^(\d+\.\d+\.\d+)(-[0-9A-Za-z.-]+)?$') { throw "Version '$Version' is not semver (X.Y.Z or X.Y.Z-suffix)" }
$numeric = $Matches[1]

$root = Resolve-Path "$PSScriptRoot\.."
if (-not $BuildDir) { $BuildDir = Join-Path $root "build" }
$bin = Join-Path (Resolve-Path $BuildDir) $Config
$exe = Join-Path $bin "WinSSH.exe"
if (-not (Test-Path $exe)) { throw "Missing $exe - build first" }

$reported = (& $exe --version | Select-Object -First 1)
if ($reported -notmatch "^WinSSH v$([regex]::Escape($Version)) ") {
    throw "WinSSH.exe reports '$reported', expected version $Version (configure with "-DWINSSH_VERSION=$Version", quoted in PowerShell)"
}

$dist = Join-Path $root "build\dist"
New-Item -ItemType Directory -Force $dist | Out-Null

# Portable zip
$stage = Join-Path $dist "WinSSH-$Version"
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$stage\docs" | Out-Null
Copy-Item $exe, "$root\README.md", "$root\LICENSE" $stage
Copy-Item "$root\docs\highlight-rules.md", "$root\docs\profiles.md" "$stage\docs"
$zip = Join-Path $dist "WinSSH-portable-$Version.zip"
Remove-Item -Force $zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Remove-Item -Recurse -Force $stage
Write-Host "Created $zip"

# Installer
if (-not $SkipInstaller) {
    $iscc = @(
        (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source,
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $iscc) { throw "ISCC.exe (Inno Setup 6) not found; install it or use -SkipInstaller" }
    & $iscc /Q "/DAppVersion=$Version" "/DAppVersionNumeric=$numeric" "/DBuildDir=$bin" "/DOutputDir=$dist" (Join-Path $root "installer\WinSSH.iss")
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }
    Write-Host "Created $(Join-Path $dist "WinSSH-setup-$Version.exe")"
}
