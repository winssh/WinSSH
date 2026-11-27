# Release version logic for scripts\tag.ps1 (decision D11 in docs/PLAN.md).
# Pure functions: no git calls, so tests\scripts\TagVersion.Tests.ps1 can check every rule.
#
# Tags: vX.Y.Z (release) and vX.Y.Z-rcN (release candidate). Other tags are ignored.

Set-StrictMode -Version Latest

function ConvertTo-ReleaseVersion {
    <# Parses "v0.2.0" / "v0.2.0-rc3" (or without "v"). Returns $null for anything else. #>
    param([Parameter(Mandatory)][AllowEmptyString()][string]$Text)
    if ($Text -notmatch '^v?(\d+)\.(\d+)\.(\d+)(?:-rc(\d+))?$') { return $null }
    $rc = if ($Matches[4]) { [int]$Matches[4] } else { $null }
    [pscustomobject]@{
        Major = [int]$Matches[1]
        Minor = [int]$Matches[2]
        Patch = [int]$Matches[3]
        Rc    = $rc
    }
}

function Format-ReleaseTag {
    param([Parameter(Mandatory)]$Version)
    $base = "v$($Version.Major).$($Version.Minor).$($Version.Patch)"
    if ($null -ne $Version.Rc) { "$base-rc$($Version.Rc)" } else { $base }
}

function Get-VersionSortKey {
    <# Sorts numerically; a release sorts after its release candidates (rc10 after rc9). #>
    param([Parameter(Mandatory)]$Version)
    $rcKey = if ($null -eq $Version.Rc) { 999999 } else { $Version.Rc }
    '{0:D6}.{1:D6}.{2:D6}.{3:D6}' -f $Version.Major, $Version.Minor, $Version.Patch, $rcKey
}

function Get-ReleaseTags {
    <# Valid release tags from a list of tag names, oldest first. #>
    param([AllowEmptyCollection()][string[]]$Tags = @())
    @($Tags | ForEach-Object { ConvertTo-ReleaseVersion $_ } | Where-Object { $_ } |
        Sort-Object { Get-VersionSortKey $_ })
}

function Get-BumpKind {
    <#
      Bump implied by Conventional Commit messages (full messages, subject first):
        "type!:" or "BREAKING CHANGE" -> major (minor while the major version is 0)
        "feat:"                      -> minor
        any other commit             -> patch
        no commits (merges ignored)  -> none
    #>
    param(
        [AllowEmptyCollection()][string[]]$Messages = @(),
        [int]$CurrentMajor = 0
    )
    $kind = 'none'
    foreach ($m in $Messages) {
        if ([string]::IsNullOrWhiteSpace($m)) { continue }
        $subject = ($m -split "`n", 2)[0].Trim()
        if ($subject -match '^Merge (pull request|branch|remote-tracking branch)\b') { continue }
        if ($subject -match '^[a-z]+(\([^)]*\))?!:' -or $m -match '(?m)^BREAKING[ -]CHANGE:') {
            $kind = 'major'
            break
        }
        if ($subject -match '^feat(\([^)]*\))?:') { $kind = 'minor' }
        elseif ($kind -eq 'none') { $kind = 'patch' }
    }
    if ($kind -eq 'major' -and $CurrentMajor -eq 0) { $kind = 'minor' } # 0.x: breaking changes bump minor
    $kind
}

function Get-NextReleaseTag {
    <#
      Decides the next tag.
        -Version X.Y.Z[-rcN]  exact (must be newer than the last release, and not exist)
        -Bump major|minor|patch  from the last release
        neither               bump from the commits since the last release (Get-BumpKind)
        -Rc                   next release candidate of that version (rc1, rc2, ...)
      Without -Rc the result is the release itself, which also "finalizes" existing rcs.
      Returns @{ Tag; Version; Reason; LastRelease }. Throws with a readable message on problems.
    #>
    param(
        [AllowEmptyCollection()][string[]]$Tags = @(),
        [AllowEmptyCollection()][string[]]$Messages = @(),
        [string]$Version = '',
        [ValidateSet('', 'major', 'minor', 'patch')][string]$Bump = '',
        [switch]$Rc
    )
    $all = Get-ReleaseTags -Tags $Tags
    $finals = @($all | Where-Object { $null -eq $_.Rc })
    $last = if ($finals.Count) { $finals[-1] } else { $null }
    $lastText = if ($last) { Format-ReleaseTag $last } else { '(none)' }
    $base = if ($last) { $last } else { [pscustomobject]@{ Major = 0; Minor = 0; Patch = 0; Rc = $null } }

    if ($Version -and $Bump) { throw 'Use either -Version or -Bump, not both.' }

    $target = $null
    $reason = ''
    if ($Version) {
        $v = ConvertTo-ReleaseVersion $Version
        if (-not $v) { throw "Version '$Version' is not X.Y.Z or X.Y.Z-rcN." }
        if ($null -ne $v.Rc -and $Rc) { throw 'Give the rc number in -Version or use -Rc, not both.' }
        $target = $v
        $reason = "version given ($Version)"
    }
    else {
        $kind = $Bump
        if (-not $kind) {
            $kind = Get-BumpKind -Messages $Messages -CurrentMajor $base.Major
            if ($kind -eq 'none') { throw "Nothing to release: no commits since $lastText." }
            $count = @($Messages | Where-Object { $_ -and ($_ -split "`n", 2)[0] -notmatch '^Merge ' }).Count
            $reason = "$kind bump from $count commit(s) since $lastText"
        }
        else {
            $reason = "$kind bump given"
        }
        $target = switch ($kind) {
            'major' { [pscustomobject]@{ Major = $base.Major + 1; Minor = 0; Patch = 0; Rc = $null } }
            'minor' { [pscustomobject]@{ Major = $base.Major; Minor = $base.Minor + 1; Patch = 0; Rc = $null } }
            'patch' { [pscustomobject]@{ Major = $base.Major; Minor = $base.Minor; Patch = $base.Patch + 1; Rc = $null } }
        }
    }

    if ($Rc) {
        $same = @($all | Where-Object {
                $_.Major -eq $target.Major -and $_.Minor -eq $target.Minor -and $_.Patch -eq $target.Patch -and $null -ne $_.Rc })
        $n = if ($same.Count) { ($same | Measure-Object -Property Rc -Maximum).Maximum + 1 } else { 1 }
        $target = [pscustomobject]@{ Major = $target.Major; Minor = $target.Minor; Patch = $target.Patch; Rc = [int]$n }
        $reason += ", release candidate $n"
    }

    $tag = Format-ReleaseTag $target
    $existing = @($all | ForEach-Object { Format-ReleaseTag $_ })
    if ($existing -contains $tag) { throw "Tag $tag already exists." }
    $releaseOfTarget = "v$($target.Major).$($target.Minor).$($target.Patch)"
    if ($existing -contains $releaseOfTarget) { throw "$releaseOfTarget is already released; pick a newer version." }
    if ($last -and (Get-VersionSortKey $target) -le (Get-VersionSortKey $last)) {
        throw "$tag is not newer than the last release $lastText."
    }

    $versionText = $tag.Substring(1)
    @{ Tag = $tag; Version = $versionText; Reason = $reason; LastRelease = $lastText }
}

Export-ModuleMember -Function ConvertTo-ReleaseVersion, Format-ReleaseTag, Get-VersionSortKey, Get-ReleaseTags,
    Get-BumpKind, Get-NextReleaseTag
