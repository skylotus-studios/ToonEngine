# bootstrap.ps1 - rebuild the ToonEngine dev environment from scratch, pure PowerShell.
#
# No prerequisites beyond what cloning this repo already implies (git) plus
# PowerShell, which ships with every supported version of Windows. No Git
# Bash, no WSL, no admin rights, no Developer Mode. bootstrap.cmd is a
# one-line launcher for this from cmd.exe; bootstrap.sh delegates here on
# Windows and runs its own POSIX path on macOS/Linux.
#
# Adds the `develop` and `main` worktrees as siblings of this checkout,
# initializes their submodules recursively, and re-links CLAUDE.md, MEMORY.md,
# ARCHIVE.md, both style guides, and the project-level Claude skills, agents,
# and .agent scratch dir from this branch so both worktrees come back exactly
# as they were. Your global Claude Code config (~/.claude) is backed up
# separately in the ClaudeUserBackup repo -- not here.
#
# Link strategy: symbolic links when the session can create them, otherwise
# junctions for directories and hard links for files. Creating a symlink on
# Windows needs SeCreateSymbolicLinkPrivilege (admin, or Developer Mode which
# only reaches a logon session after sign-out/sign-in). Junctions and hard
# links need no privilege at all and resolve identically for reading and
# in-place editing, so the fallback is a real equivalent, not a degraded copy.
#
# Safe to re-run: existing worktrees are left alone, links already pointing
# at the right target are left alone, and a real (non-link) file or directory
# already sitting at a link destination is never touched -- it is reported
# and left in place.

$ErrorActionPreference = 'Stop'

$Here    = Split-Path -Parent $MyInvocation.MyCommand.Path
$Parent  = Split-Path -Parent $Here
$Develop = Join-Path $Parent 'develop'
$Main    = Join-Path $Parent 'main'

$script:Results      = New-Object System.Collections.Generic.List[string]
$script:FailCount    = 0
$script:CanSymlink   = $false
$script:UsedFallback = $false

# Ground truth for symlink capability: actually create one and see. Neither
# the elevation check nor the Developer Mode registry flag is reliable on its
# own, since the flag can read "on" while the current logon token still lacks
# the privilege.
function Test-SymlinkCapability {
    $probeTarget = Join-Path $env:TEMP "bootstrap-probe-target-$PID.tmp"
    $probeLink   = Join-Path $env:TEMP "bootstrap-probe-link-$PID.tmp"
    'probe' | Out-File -FilePath $probeTarget -Encoding ascii
    $ok = $false
    try {
        New-Item -ItemType SymbolicLink -Path $probeLink -Target $probeTarget -ErrorAction Stop | Out-Null
        $ok = $true
    } catch {}
    Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath $probeLink
    Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath $probeTarget
    return $ok
}

function Test-ReparsePoint([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    return (((Get-Item -Force -LiteralPath $Path).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
}

# Target of a symlink or junction, normalized. PowerShell hands junction
# targets back in the \??\C:\... device form on some builds.
function Get-ReparseTarget([string]$Path) {
    $t = (Get-Item -Force -LiteralPath $Path).Target
    if ($t -is [array]) { $t = $t | Select-Object -First 1 }
    if (-not $t) { return $null }
    return ($t -replace '^\\\?\?\\', '')
}

# A hard link is indistinguishable from a plain file by attributes -- the two
# names simply share one on-disk file. fsutil lists every name pointing at it,
# as volume-relative paths ("\dev\ToonEngine\backup\claude.md").
function Test-HardLinkedTo([string]$Dest, [string]$Target) {
    if ($Target.Substring(0, 2) -ne $Dest.Substring(0, 2)) { return $false }
    $lines = & fsutil.exe hardlink list $Dest 2>$null
    if (-not $lines) { return $false }
    $targetRel = $Target.Substring(2)
    foreach ($line in $lines) {
        if ($line.Trim() -ieq $targetRel) { return $true }
    }
    return $false
}

# Deletes a link without following it. Remove-Item -Recurse on a junction has
# historically deleted the *target's* contents; Directory.Delete(path, $false)
# removes only the reparse point and refuses to recurse.
function Remove-LinkOnly([string]$Path) {
    if ((Get-Item -Force -LiteralPath $Path).PSIsContainer) {
        [System.IO.Directory]::Delete($Path, $false)
    } else {
        [System.IO.File]::Delete($Path)
    }
}

# Links $Dest to $Target, recording a PASS/FAIL result. Refuses to touch
# $Dest if something real (not a link to $Target) is already there.
function New-Link([string]$Target, [string]$Dest) {
    if (-not (Test-Path -LiteralPath $Target)) {
        $script:Results.Add("FAIL  $Dest -> $Target (link target does not exist)")
        $script:FailCount++
        return
    }
    $targetIsDir = (Get-Item -Force -LiteralPath $Target).PSIsContainer

    if (Test-Path -LiteralPath $Dest) {
        if (Test-ReparsePoint $Dest) {
            $current = Get-ReparseTarget $Dest
            if ($current -and ($current.TrimEnd('\') -ieq $Target.TrimEnd('\'))) {
                $kind = (Get-Item -Force -LiteralPath $Dest).LinkType
                $script:Results.Add("PASS  $Dest -> $Target (already linked, $kind)")
                return
            }
            Remove-LinkOnly $Dest
        }
        elseif ((-not $targetIsDir) -and (Test-HardLinkedTo $Dest $Target)) {
            $script:Results.Add("PASS  $Dest -> $Target (already linked, hard link)")
            return
        }
        else {
            $item = Get-Item -Force -LiteralPath $Dest
            $kind = if ($item.PSIsContainer) { 'directory' } else { 'file' }
            Write-Host "==> Refusing to overwrite $Dest"
            Write-Host "    Found a real $kind here, not a link to $Target"
            if ($item.PSIsContainer) {
                Get-ChildItem -Force -LiteralPath $Dest | ForEach-Object { Write-Host "      $($_.Name)" }
            } else {
                Write-Host "      $($item.Length) bytes, modified $($item.LastWriteTime)"
                Write-Host "      If this was hard-linked before, an editor that replaces files"
                Write-Host "      wholesale has split the two copies. Keep the version you want,"
                Write-Host "      delete the other, then re-run."
            }
            $script:Results.Add("FAIL  $Dest -> $Target (real $kind already present, left untouched)")
            $script:FailCount++
            return
        }
    }

    $parentDir = Split-Path -Parent $Dest
    if (-not (Test-Path -LiteralPath $parentDir)) {
        New-Item -ItemType Directory -Force -Path $parentDir | Out-Null
    }

    if ($script:CanSymlink) {
        try {
            New-Item -ItemType SymbolicLink -Path $Dest -Target $Target -ErrorAction Stop | Out-Null
            $script:Results.Add("PASS  $Dest -> $Target (symlink)")
            return
        } catch {}
    }

    if ($targetIsDir) {
        try {
            New-Item -ItemType Junction -Path $Dest -Target $Target -ErrorAction Stop | Out-Null
            $script:UsedFallback = $true
            $script:Results.Add("PASS  $Dest -> $Target (junction)")
        } catch {
            $script:Results.Add("FAIL  $Dest -> $Target ($($_.Exception.Message))")
            $script:FailCount++
        }
        return
    }

    if ($Target.Substring(0, 2) -ne $Dest.Substring(0, 2)) {
        $script:Results.Add("FAIL  $Dest -> $Target (no symlink privilege, and a hard link needs both paths on one volume)")
        $script:FailCount++
        return
    }
    try {
        New-Item -ItemType HardLink -Path $Dest -Target $Target -ErrorAction Stop | Out-Null
        $script:UsedFallback = $true
        $script:Results.Add("PASS  $Dest -> $Target (hard link)")
    } catch {
        $script:Results.Add("FAIL  $Dest -> $Target ($($_.Exception.Message))")
        $script:FailCount++
    }
}

$script:CanSymlink = Test-SymlinkCapability
if ($script:CanSymlink) {
    Write-Host "==> Link strategy: symbolic links (verified by creating a real one)."
} else {
    Write-Host "==> Link strategy: this session cannot create symlinks, so directories will be"
    Write-Host "    linked as junctions and files as hard links. Neither needs admin rights or"
    Write-Host "    Developer Mode, and both resolve the same way for reading and editing in"
    Write-Host "    place. Caveat: an editor that saves by replacing a file rather than writing"
    Write-Host "    it in place breaks a hard link, silently splitting the two copies. Re-run"
    Write-Host "    this script to detect that."
}

Write-Host "==> Worktrees"
if (-not (Test-Path (Join-Path $Develop '.git'))) {
    git worktree add $Develop develop
} else {
    Write-Host "    develop already present, skipping"
}
if (-not (Test-Path (Join-Path $Main '.git'))) {
    git worktree add $Main main
} else {
    Write-Host "    main already present, skipping"
}

Write-Host "==> Submodules"
git -C $Develop submodule update --init --recursive
git -C $Main submodule update --init --recursive

Write-Host "==> Project-level Claude files (develop, main)"
foreach ($W in @($Develop, $Main)) {
    New-Item -ItemType Directory -Force -Path (Join-Path $W '.claude') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $W 'docs')    | Out-Null

    New-Link (Join-Path $Here 'claude.md')          (Join-Path $W 'CLAUDE.md')
    New-Link (Join-Path $Here 'memory.md')          (Join-Path $W 'MEMORY.md')
    New-Link (Join-Path $Here 'archive.md')         (Join-Path $W 'ARCHIVE.md')
    New-Link (Join-Path $Here 'cpp-style-guide.md') (Join-Path $W 'docs\cpp-style-guide.md')
    New-Link (Join-Path $Here 'md-style-guide.md')  (Join-Path $W 'docs\md-style-guide.md')
    New-Link (Join-Path $Here 'project\skills')     (Join-Path $W '.claude\skills')
    New-Link (Join-Path $Here 'project\agents')     (Join-Path $W '.claude\agents')
    New-Link (Join-Path $Here 'project\agent')      (Join-Path $W '.agent')
}

Write-Host ""
Write-Host "==> Link results"
foreach ($r in $script:Results) { Write-Host "    $r" }

Write-Host ""
Write-Host "==> Done ($script:FailCount failed link(s))."
Write-Host ""
Write-Host "$Parent\"
Write-Host "  develop\  (branch: develop)"
Write-Host "  main\     (branch: main)"
Write-Host "  backup\   (branch: backup, this checkout)"
if ($script:UsedFallback) {
    Write-Host ""
    Write-Host "Some links are junctions or hard links rather than symlinks. To convert them,"
    Write-Host "get symlink privilege (elevated shell, or Developer Mode plus sign-out/sign-in),"
    Write-Host "delete the links, and re-run."
}
Write-Host ""
Write-Host "Restore your global Claude Code config (~/.claude) separately from the"
Write-Host "ClaudeUserBackup repo -- see README.md in this branch for the full steps."

if ($script:FailCount -gt 0) { exit 1 } else { exit 0 }
