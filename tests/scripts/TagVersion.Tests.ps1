# Tests for scripts\TagVersion.psm1 (release version rules, D11). Plain PowerShell, no Pester:
#   pwsh -NoProfile -File tests\scripts\TagVersion.Tests.ps1
# Exit code 0 = all passed. Also run by CTest (tests\CMakeLists.txt).
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\..\scripts\TagVersion.psm1') -Force

$script:failures = 0
$script:count = 0
function Check([string]$name, $actual, $expected) {
    $script:count++
    if ("$actual" -ne "$expected") {
        $script:failures++
        Write-Host "FAIL $name`n     expected: $expected`n     actual:   $actual" -ForegroundColor Red
    }
}
function CheckThrows([string]$name, [scriptblock]$block, [string]$like) {
    $script:count++
    try {
        & $block | Out-Null
        $script:failures++
        Write-Host "FAIL ${name}: no error (expected '$like')" -ForegroundColor Red
    }
    catch {
        if ($_.Exception.Message -notlike "*$like*") {
            $script:failures++
            Write-Host "FAIL ${name}: error '$($_.Exception.Message)' does not contain '$like'" -ForegroundColor Red
        }
    }
}
function Next([string[]]$Tags = @(), [string[]]$Messages = @(), [string]$Version = '', [string]$Bump = '', [switch]$Rc) {
    (Get-NextReleaseTag -Tags $Tags -Messages $Messages -Version $Version -Bump $Bump -Rc:$Rc).Tag
}

# --- parsing and sorting
Check 'parse release' (Format-ReleaseTag (ConvertTo-ReleaseVersion 'v1.2.3')) 'v1.2.3'
Check 'parse rc' (Format-ReleaseTag (ConvertTo-ReleaseVersion '0.2.0-rc10')) 'v0.2.0-rc10'
Check 'reject other suffix' (ConvertTo-ReleaseVersion 'v1.2.3-beta') ''
Check 'reject text' (ConvertTo-ReleaseVersion 'latest') ''
$sorted = (Get-ReleaseTags -Tags @('v0.2.0', 'v0.2.0-rc10', 'v0.2.0-rc9', 'v0.10.0', 'v0.9.1', 'junk') |
        ForEach-Object { Format-ReleaseTag $_ }) -join ','
Check 'sort numeric, release after rcs' $sorted 'v0.2.0-rc9,v0.2.0-rc10,v0.2.0,v0.9.1,v0.10.0'

# --- bump kind from Conventional Commits
Check 'fix -> patch' (Get-BumpKind -Messages @('fix(ble): HW-1 report state')) 'patch'
Check 'docs -> patch' (Get-BumpKind -Messages @('docs: readme')) 'patch'
Check 'feat -> minor' (Get-BumpKind -Messages @('fix: a', 'feat: b', 'docs: c')) 'minor'
Check 'feat scope -> minor' (Get-BumpKind -Messages @('feat(picker): x')) 'minor'
Check 'bang -> minor on 0.x' (Get-BumpKind -Messages @('feat!: drop x') -CurrentMajor 0) 'minor'
Check 'bang -> major on 1.x' (Get-BumpKind -Messages @('refactor(cli)!: rename') -CurrentMajor 1) 'major'
Check 'footer -> major' (Get-BumpKind -Messages @("fix: x`n`nBREAKING CHANGE: flags renamed") -CurrentMajor 2) 'major'
Check 'merges ignored' (Get-BumpKind -Messages @('Merge pull request #8 from winssh/x', 'Merge branch ''develop''')) 'none'
Check 'empty -> none' (Get-BumpKind -Messages @()) 'none'
Check 'unknown type -> patch' (Get-BumpKind -Messages @('Update README.md')) 'patch'

# --- next tag
Check 'first release, feat' (Next -Messages @('feat: first')) 'v0.1.0'
Check 'first release, fix only' (Next -Messages @('fix: x')) 'v0.0.1'
Check 'patch after release' (Next -Tags @('v0.1.0') -Messages @('fix: y')) 'v0.1.1'
Check 'minor after release' (Next -Tags @('v0.1.0', 'v0.1.1') -Messages @('feat: z')) 'v0.2.0'
Check 'first rc' (Next -Tags @('v0.1.0') -Messages @('feat: z') -Rc) 'v0.2.0-rc1'
Check 'next rc' (Next -Tags @('v0.1.0', 'v0.2.0-rc1', 'v0.2.0-rc2') -Messages @('feat: z', 'fix: q') -Rc) 'v0.2.0-rc3'
Check 'rc10 after rc9' (Next -Tags @('v0.1.0', 'v0.2.0-rc9') -Messages @('feat: z') -Rc) 'v0.2.0-rc10'
Check 'finalize rcs' (Next -Tags @('v0.1.0', 'v0.2.0-rc1', 'v0.2.0-rc2') -Messages @('feat: z')) 'v0.2.0'
Check 'bump given' (Next -Tags @('v0.2.0') -Bump 'major') 'v1.0.0'
Check 'bump given needs no commits' (Next -Tags @('v0.2.0') -Bump 'patch') 'v0.2.1'
Check 'bump + rc' (Next -Tags @('v0.2.0') -Bump 'minor' -Rc) 'v0.3.0-rc1'
Check 'version given' (Next -Tags @('v0.2.0') -Version '0.4.0') 'v0.4.0'
Check 'version given with v' (Next -Version 'v1.0.0') 'v1.0.0'
Check 'version given rc' (Next -Tags @('v0.2.0') -Version '0.3.0-rc2') 'v0.3.0-rc2'
Check 'version + -Rc' (Next -Tags @('v0.2.0', 'v0.3.0-rc1') -Version '0.3.0' -Rc) 'v0.3.0-rc2'
Check 'other tags ignored' (Next -Tags @('nightly', 'v0.1.0', 'v0.1.0-beta') -Messages @('fix: a')) 'v0.1.1'

$r = Get-NextReleaseTag -Tags @('v0.1.0') -Messages @('feat: a', 'fix: b', 'Merge pull request #1')
Check 'reason' $r.Reason 'minor bump from 2 commit(s) since v0.1.0'
Check 'version text' $r.Version '0.2.0'
Check 'last release' $r.LastRelease 'v0.1.0'

# --- errors
CheckThrows 'nothing to release' { Next -Tags @('v0.1.0') -Messages @() } 'Nothing to release'
CheckThrows 'only merges' { Next -Tags @('v0.1.0') -Messages @('Merge pull request #2') } 'Nothing to release'
CheckThrows 'exists' { Next -Tags @('v0.2.0') -Version '0.2.0' } 'already exists'
CheckThrows 'rc of a released version' { Next -Tags @('v0.2.0') -Version '0.2.0-rc3' } 'already released'
CheckThrows 'older than last' { Next -Tags @('v0.2.0') -Version '0.1.9' } 'not newer'
CheckThrows 'bad version' { Next -Version '1.2' } 'not X.Y.Z'
CheckThrows 'version and bump' { Next -Version '1.0.0' -Bump 'minor' } 'not both'
CheckThrows 'rc twice' { Next -Version '1.0.0-rc1' -Rc } 'not both'

Write-Host "$($script:count - $script:failures)/$($script:count) checks passed"
exit ([int]($script:failures -gt 0))
