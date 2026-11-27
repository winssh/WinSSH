# Creates the next release tag on main (decision D11 in docs/PLAN.md). Pushing the tag starts the
# release workflow. Run locally, or on GitHub through the "Tag release" workflow (tag.yml).
#
#   .\scripts\tag.ps1 -DryRun              # what would be tagged, and why
#   .\scripts\tag.ps1 -Push                # next version from Conventional Commits since the last release
#   .\scripts\tag.ps1 -Rc -Push            # next release candidate of that version (v0.2.0-rc1, rc2, ...)
#   .\scripts\tag.ps1 -Bump minor -Rc -Push
#   .\scripts\tag.ps1 -Version 0.2.0 -Push
#
# Without -Push the tag is only created locally (remove it with: git tag -d <tag>).
# Checks: the commit is on <remote>/main, the tag is new and newer than the last release, and
# (when gh is available) the commit's CI checks passed; a dry run only reports the CI state.
[CmdletBinding()]
param(
    [string]$Version = '',
    [ValidateSet('', 'major', 'minor', 'patch')][string]$Bump = '',
    [switch]$Rc,
    [switch]$DryRun,
    [switch]$Push,
    # Commit to tag; must be on <remote>/main. Default: the tip of <remote>/main.
    [string]$Ref = '',
    [string]$Remote = 'origin',
    # Skip the CI status check (e.g. gh not installed or no access).
    [switch]$SkipCiCheck
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'TagVersion.psm1') -Force

function Invoke-Git {
    $out = & git @args 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed: $out" }
    $out
}

# Shows the result: aligned plain text in the console, and a markdown table in the GitHub Actions
# job summary when running there ($GITHUB_STEP_SUMMARY).
function Write-Result([System.Collections.Specialized.OrderedDictionary]$rows) {
    $width = ($rows.Keys | Measure-Object -Property Length -Maximum).Maximum + 2
    foreach ($k in $rows.Keys) { Write-Host ($k.PadRight($width) + $rows[$k]) }
    if ($env:GITHUB_STEP_SUMMARY) {
        $md = @('### Release tag', '', '| | |', '|---|---|')
        foreach ($k in $rows.Keys) {
            $v = if ($k -eq 'Release tag') { "**$($rows[$k])**" } else { $rows[$k] }
            $md += "| $k | $v |"
        }
        Add-Content -Path $env:GITHUB_STEP_SUMMARY -Value $md
    }
}

$exitCode = 0
Push-Location (Resolve-Path "$PSScriptRoot\..")
try {
    Invoke-Git fetch $Remote main --tags --prune --quiet | Out-Null

    $mainRef = "$Remote/main"
    $commit = (Invoke-Git rev-parse --verify "$(if ($Ref) { $Ref } else { $mainRef })^{commit}").Trim()
    & git merge-base --is-ancestor $commit $mainRef
    if ($LASTEXITCODE -ne 0) { throw "Commit $commit is not on $mainRef. Releases are tagged on main (D9)." }

    $tags = @(Invoke-Git tag --list 'v*' | ForEach-Object { "$_".Trim() } | Where-Object { $_ })
    $finals = @(Get-ReleaseTags -Tags $tags | Where-Object { $null -eq $_.Rc })
    $range = if ($finals.Count) { "$(Format-ReleaseTag $finals[-1])..$commit" } else { $commit }
    $raw = (Invoke-Git log --format='%B%x1e' $range) -join "`n"
    $messages = @($raw -split [char]0x1e | ForEach-Object { $_.Trim() } | Where-Object { $_ })

    $next = Get-NextReleaseTag -Tags $tags -Messages $messages -Version $Version -Bump $Bump -Rc:$Rc
    $short = $commit.Substring(0, 7)
    $subject = (Invoke-Git log -1 --format=%s $commit).Trim()

    # CI status of the commit (GitHub check runs), when gh can tell us. A dry run only reports it;
    # creating a tag refuses a commit whose CI is not green.
    $ci = 'not checked'
    $ciBlocks = $false
    if ($SkipCiCheck) {
        $ci = 'not checked (-SkipCiCheck)'
    }
    elseif (Get-Command gh -ErrorAction SilentlyContinue) {
        $url = (Invoke-Git remote get-url $Remote).Trim()
        if ($url -match 'github\.com[:/]([^/]+)/([^/]+?)(\.git)?$') {
            $repo = "$($Matches[1])/$($Matches[2])"
            $json = & gh api "repos/$repo/commits/$commit/check-runs" 2>$null
            if ($LASTEXITCODE -eq 0 -and $json) {
                $summary = Get-CiSummary -CheckRuns @(($json | ConvertFrom-Json).check_runs)
                $ci = $summary.Text
                # "no checks found" does not block: a repo or commit may have no CI at all.
                $ciBlocks = -not $summary.Green -and $ci -ne 'no checks found'
            }
            else { $ci = 'could not read (gh not signed in or no access)' }
        }
    }
    else {
        $ci = 'not checked (gh not installed)'
    }

    Write-Result ([ordered]@{
            'Release tag'  = $next.Tag
            'Commit'       = "$short $subject"
            'Why'          = $next.Reason
            'Last release' = $next.LastRelease
            'CI'           = $ci
            'Mode'         = if ($DryRun) { 'dry run: nothing created' } elseif ($Push) { 'create and push' } else { 'create locally (not pushed)' }
        })

    if ($ciBlocks) {
        if ($DryRun) {
            Write-Host "Warning: CI on $short is not green yet; tagging now would be refused (wait, or use -SkipCiCheck)." -ForegroundColor Yellow
        }
        else {
            throw "CI on $short is not green ($($ci -replace '^not green: ', '')). Wait for it to finish, or use -SkipCiCheck to tag anyway."
        }
    }

    if ($env:GITHUB_OUTPUT) {
        Add-Content -Path $env:GITHUB_OUTPUT -Value "tag=$($next.Tag)"
        Add-Content -Path $env:GITHUB_OUTPUT -Value "version=$($next.Version)"
    }
    if ($DryRun) { return }

    Invoke-Git tag -a $next.Tag $commit -m "WinSSH $($next.Version)" | Out-Null
    Write-Host "Created tag $($next.Tag) on $short."
    if ($Push) {
        Invoke-Git push $Remote "refs/tags/$($next.Tag)" | Out-Null
        Write-Host "Pushed $($next.Tag) to $Remote; the release workflow builds and publishes it."
    }
    else {
        Write-Host "Not pushed. Push with: git push $Remote $($next.Tag)   (or delete: git tag -d $($next.Tag))"
    }
}
catch {
    # One readable line instead of PowerShell's error record with the script excerpt.
    Write-Host "tag.ps1: $($_.Exception.Message)" -ForegroundColor Red
    if ($env:GITHUB_STEP_SUMMARY) { Add-Content -Path $env:GITHUB_STEP_SUMMARY -Value "**Error:** $($_.Exception.Message)" }
    $exitCode = 1
}
finally {
    Pop-Location
}
if ($exitCode) { exit $exitCode }
