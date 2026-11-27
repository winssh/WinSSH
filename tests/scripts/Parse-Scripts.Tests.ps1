# Checks that every PowerShell script in the repository parses (syntax only, nothing is run).
# Catches mistakes like a quote inside a double-quoted string before a release run does.
#   pwsh -NoProfile -File tests\scripts\Parse-Scripts.Tests.ps1
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$files = Get-ChildItem -Path $root -Recurse -Include *.ps1, *.psm1 -File |
    Where-Object { $_.FullName -notmatch '\\(build|vcpkg_installed|\.git)\\' }

$failed = 0
foreach ($f in $files) {
    $tokens = $null
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$tokens, [ref]$errors) | Out-Null
    if ($errors) {
        $failed++
        foreach ($e in $errors) {
            Write-Host "FAIL $($f.FullName):$($e.Extent.StartLineNumber): $($e.Message)" -ForegroundColor Red
        }
    }
}
Write-Host "$($files.Count - $failed)/$($files.Count) scripts parse"
exit ([int]($failed -gt 0))
