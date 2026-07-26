# bootstrap.ps1 - rebuild the ToonEngine dev environment from scratch, pure PowerShell.
#
# No prerequisites beyond what cloning this repo already implies (git) plus
# PowerShell, which ships with every supported version of Windows -- no Git
# Bash, WSL, or anything else needs to be installed first. bootstrap.cmd is a
# one-line launcher for this from cmd.exe. bootstrap.sh is the equivalent for
# Git Bash / macOS / Linux; keep the two in sync if you change one.
#
# Adds the `develop` and `main` worktrees as siblings of this checkout,
# initializes their submodules recursively, and re-links CLAUDE.md, MEMORY.md,
# ARCHIVE.md, both style guides, and the project-level Claude skills, agents,
# and .agent scratch dir from this branch so both worktrees come back exactly
# as they were. Your global Claude Code config (~/.claude) is backed up
# separately in the ClaudeUserBackup repo -- not here.
#
# Safe to re-run: existing worktrees are left alone, correct symlinks are left
# alone, and a real (non-symlink) file or directory already sitting at a link
# destination is never touched -- it is reported and left in place.

$ErrorActionPreference = 'Stop'

$Here    = Split-Path -Parent $MyInvocation.MyCommand.Path
$Parent  = Split-Path -Parent $Here
$Develop = Join-Path $Parent 'develop'
$Main    = Join-Path $Parent 'main'

$script:Results   = New-Object System.Collections.Generic.List[string]
$script:FailCount = 0

# Detects whether symlink creation is likely to work on this machine: either
# an elevated (Run as Administrator) shell, or Developer Mode enabled.
function Test-Elevation {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Host "==> Privilege check: OK (running elevated)."
        return
    }

    $devMode = $false
    try {
        $key = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock'
        $val = (Get-ItemProperty -Path $key -Name AllowDevelopmentWithoutDevLicense -ErrorAction Stop).AllowDevelopmentWithoutDevLicense
        $devMode = ($val -eq 1)
    } catch {}

    if ($devMode) {
        Write-Host "==> Privilege check: OK (Developer Mode is enabled)."
        return
    }

    Write-Host "==> Privilege check: NOT elevated and Developer Mode is NOT enabled."
    Write-Host "    Symlink creation below will most likely fail. Fix one of:"
    Write-Host "      - Settings > Privacy & security > For developers > Developer Mode (On)"
    Write-Host "      - re-run this script from an elevated (Run as Administrator) shell"
    Write-Host "    Continuing anyway -- failures are reported per-link below."
}

function Test-IsSymlink([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $item = Get-Item -Force -LiteralPath $Path
    return (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
}

# Creates a symlink at $Dest pointing to $Target, recording a PASS/FAIL
# result. Refuses to touch $Dest if something real (not already a symlink)
# is there.
function New-Link([string]$Target, [string]$Dest) {
    if (Test-IsSymlink $Dest) {
        $currentTarget = $null
        try { $currentTarget = (Get-Item -Force -LiteralPath $Dest).Target | Select-Object -First 1 } catch {}
        if ($currentTarget -eq $Target) {
            $script:Results.Add("PASS  $Dest -> $Target (already linked)")
            return
        }
        Remove-Item -Force -LiteralPath $Dest
    }
    elseif (Test-Path -LiteralPath $Dest) {
        $kind = if ((Get-Item -Force -LiteralPath $Dest).PSIsContainer) { 'directory' } else { 'file' }
        Write-Host "==> Refusing to overwrite $Dest"
        Write-Host "    Found a real $kind here (not a symlink):"
        Get-ChildItem -Force -LiteralPath $Dest | ForEach-Object { Write-Host "      $($_.Name)" }
        $script:Results.Add("FAIL  $Dest -> $Target (real $kind already present, left untouched)")
        $script:FailCount++
        return
    }

    $parentDir = Split-Path -Parent $Dest
    if (-not (Test-Path -LiteralPath $parentDir)) {
        New-Item -ItemType Directory -Force -Path $parentDir | Out-Null
    }

    try {
        New-Item -ItemType SymbolicLink -Path $Dest -Target $Target -ErrorAction Stop | Out-Null
        $script:Results.Add("PASS  $Dest -> $Target")
    } catch {
        $script:Results.Add("FAIL  $Dest -> $Target ($($_.Exception.Message))")
        $script:FailCount++
    }
}

Test-Elevation

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
Write-Host ""
Write-Host "Restore your global Claude Code config (~/.claude) separately from the"
Write-Host "ClaudeUserBackup repo -- see README.md in this branch for the full steps."

exit ([Math]::Min($script:FailCount, 1))
