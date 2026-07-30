param(
    # Skip the UAC prompt. Links that need elevation are reported as failures
    # instead of being created.
    [switch]$NoElevate,

    # Internal: worker mode. Reads "target<TAB>dest" lines from this file,
    # creates each symlink, and writes results to -ResultFile. This is the
    # only part that runs elevated.
    [string]$MakeLinksFrom,
    [string]$ResultFile
)

# bootstrap.ps1 - rebuild the ToonEngine dev environment from scratch.
#
# No prerequisites beyond what cloning this repo already implies (git) plus
# PowerShell, which ships with every supported version of Windows. No Git
# Bash and no WSL. bootstrap.cmd is a one-line launcher for this from
# cmd.exe; bootstrap.sh delegates here on Windows and runs its own POSIX
# path on macOS/Linux.
#
# Adds the `develop` and `main` worktrees as siblings of this checkout,
# initializes their submodules recursively, and re-links CLAUDE.md, MEMORY.md,
# ARCHIVE.md, both style guides, .clang-format, .clangd, and the project-level
# Claude skills, agents, and .agent scratch dir from this branch so both
# worktrees come back exactly as they were. Your global Claude Code config (~/.claude) is backed up
# separately in the ClaudeUserBackup repo, not here.
#
# Every link is a symbolic link. Creating one on Windows needs
# SeCreateSymbolicLinkPrivilege, which a normal logon session lacks unless it
# is elevated or Developer Mode was on when the session started. When the
# script finds it cannot create symlinks, it re-runs just the link-creation
# step elevated, which costs one UAC prompt. Everything else, git included,
# stays unelevated so no repo file ends up owned by Administrator.
#
# Safe to re-run: existing worktrees are left alone, symlinks already pointing
# at the right target are left alone, and a real (non-link) file or directory
# already sitting at a link destination is never touched. It is reported and
# left in place.

$ErrorActionPreference = 'Stop'

function Test-ReparsePoint([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    return (((Get-Item -Force -LiteralPath $Path).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
}

# Deletes a link without following it. Remove-Item -Recurse on a junction has
# historically deleted the target's contents; Directory.Delete(path, $false)
# removes only the reparse point and refuses to recurse.
function Remove-LinkOnly([string]$Path) {
    if ((Get-Item -Force -LiteralPath $Path).PSIsContainer) {
        [System.IO.Directory]::Delete($Path, $false)
    } else {
        [System.IO.File]::Delete($Path)
    }
}

# Replaces whatever link is at $Dest with a symlink to $Target. The caller has
# already established that $Dest is either absent or a link, never real data.
function New-SymLink([string]$Target, [string]$Dest) {
    if (Test-Path -LiteralPath $Dest) { Remove-LinkOnly $Dest }
    New-Item -ItemType SymbolicLink -Path $Dest -Target $Target -ErrorAction Stop | Out-Null
}

# ---------------------------------------------------------------- worker mode
# Runs elevated, does nothing but create the symlinks it was handed.
if ($MakeLinksFrom) {
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($line in (Get-Content -LiteralPath $MakeLinksFrom)) {
        if (-not $line.Trim()) { continue }
        $parts  = $line -split "`t"
        $target = $parts[0]
        $dest   = $parts[1]
        try {
            New-SymLink $target $dest
            $out.Add("PASS`t$dest`t$target")
        } catch {
            $out.Add("FAIL`t$dest`t$target`t$($_.Exception.Message)")
        }
    }
    Set-Content -LiteralPath $ResultFile -Value $out -Encoding UTF8
    exit 0
}

# ------------------------------------------------------------------ main mode
$Here    = Split-Path -Parent $PSCommandPath
$Parent  = Split-Path -Parent $Here
$Develop = Join-Path $Parent 'develop'
$Main    = Join-Path $Parent 'main'

# Ground truth for symlink capability: create one and see. Neither the
# elevation check nor the Developer Mode registry flag is reliable alone, as
# the flag can read "on" while the current logon token still lacks the
# privilege.
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

# Targets are named without the leading dot on this branch (clang-format, not
# .clang-format) so they are ordinary visible files in this checkout; the leading
# dot only appears at the destination, where the tool looks for it.
$LINKS = @(
    @{ Target = 'claude.md';          Dest = 'CLAUDE.md' }
    @{ Target = 'memory.md';          Dest = 'MEMORY.md' }
    @{ Target = 'archive.md';         Dest = 'ARCHIVE.md' }
    @{ Target = 'cpp-style-guide.md'; Dest = 'docs\cpp-style-guide.md' }
    @{ Target = 'md-style-guide.md';  Dest = 'docs\md-style-guide.md' }
    @{ Target = 'clang-format';       Dest = '.clang-format' }
    @{ Target = 'clangd';             Dest = '.clangd' }
    @{ Target = 'project\skills';     Dest = '.claude\skills' }
    @{ Target = 'project\agents';     Dest = '.claude\agents' }
    @{ Target = 'project\agent';      Dest = '.agent' }
)

$canSymlink = Test-SymlinkCapability
if ($canSymlink) {
    Write-Host "==> Symlink check: OK, this session can create symbolic links."
} else {
    Write-Host "==> Symlink check: this session cannot create symbolic links. If any links turn"
    Write-Host "    out to need creating, that step alone asks for administrator rights (one UAC"
    Write-Host "    prompt). Everything else, git included, runs as you."
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

# ---------------------------------------------------- short-path build junctions
# DiligentCore's own headers chain long relative "../../X/interface" includes.
# For at least one header (GraphicsTools -> ... -> Platforms -> Basic/interface)
# the raw, uncollapsed path ninja sees can exceed Windows' 260-char MAX_PATH once
# combined with a checkout path as long as "...\ToonEngine\develop\...", and
# ninja's /showIncludes-based dependency tracking then fails with "path too
# long" -- even though clang-cl compiles the file fine. Windows long-path
# support does not help (confirmed): the failure is ninja's own internal string
# handling, reproduced on ninja 1.11.1 and 1.13.2 alike, before it ever asks
# Windows to open the file. A short-path directory junction sidesteps it
# entirely without touching DiligentCore's (git-submodule, upstream-tracked)
# source. Junctions need no elevation, unlike the symlinks below.
Write-Host "==> Short-path build junctions"
$JUNCTIONS = @(
    @{ Path = 'C:\ted'; Target = $Develop }
    @{ Path = 'C:\tem'; Target = $Main }
)
foreach ($j in $JUNCTIONS) {
    if (Test-Path -LiteralPath $j.Path) {
        if (Test-ReparsePoint $j.Path) {
            $item = Get-Item -Force -LiteralPath $j.Path
            $cur  = $item.Target
            if ($cur -is [array]) { $cur = $cur | Select-Object -First 1 }
            if ($cur) { $cur = $cur -replace '^\\\?\?\\', '' }
            if ($item.LinkType -eq 'Junction' -and $cur -and ($cur.TrimEnd('\') -ieq $j.Target.TrimEnd('\'))) {
                Write-Host "    $($j.Path) -> $($j.Target) (already linked)"
            } else {
                Write-Host "    Refusing to overwrite $($j.Path): reparse point aimed elsewhere ($cur)"
            }
            continue
        }
        Write-Host "    Refusing to overwrite $($j.Path): a real file or directory is already there"
        continue
    }
    try {
        New-Item -ItemType Junction -Path $j.Path -Target $j.Target -ErrorAction Stop | Out-Null
        Write-Host "    $($j.Path) -> $($j.Target) (created)"
    } catch {
        Write-Host "    FAILED to create $($j.Path) -> $($j.Target): $($_.Exception.Message)"
    }
}
Write-Host "    Configure/build from these short paths (e.g. 'cmake --preset agent-debug'"
Write-Host "    from C:\ted), not from the long checkout path, to avoid ninja's ~260-char"
Write-Host "    include-path limit tripping on DiligentCore's header chains."

Write-Host "==> Project-level Claude files (develop, main)"

# Ordered record of every link, so results print in declaration order.
$plan    = New-Object System.Collections.Generic.List[object]
$pending = New-Object System.Collections.Generic.List[object]

foreach ($W in @($Develop, $Main)) {
    New-Item -ItemType Directory -Force -Path (Join-Path $W '.claude') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $W 'docs')    | Out-Null

    foreach ($l in $LINKS) {
        $target = Join-Path $Here $l.Target
        $dest   = Join-Path $W    $l.Dest
        $entry  = [pscustomobject]@{ Target = $target; Dest = $dest; Result = $null }
        $plan.Add($entry)

        if (-not (Test-Path -LiteralPath $target)) {
            $entry.Result = "FAIL  $dest -> $target (link target does not exist)"
            continue
        }

        if (Test-Path -LiteralPath $dest) {
            if (Test-ReparsePoint $dest) {
                $item = Get-Item -Force -LiteralPath $dest
                $cur  = $item.Target
                if ($cur -is [array]) { $cur = $cur | Select-Object -First 1 }
                if ($cur) { $cur = $cur -replace '^\\\?\?\\', '' }

                if ($item.LinkType -eq 'SymbolicLink' -and $cur -and ($cur.TrimEnd('\') -ieq $target.TrimEnd('\'))) {
                    $entry.Result = "PASS  $dest -> $target (already linked)"
                    continue
                }
                # A junction, or a symlink aimed somewhere else. Replace it,
                # but not before we know the replacement can be created, so a
                # declined UAC prompt never leaves the destination empty.
                $pending.Add($entry)
                continue
            }

            $item = Get-Item -Force -LiteralPath $dest
            $kind = if ($item.PSIsContainer) { 'directory' } else { 'file' }
            Write-Host "==> Refusing to overwrite $dest"
            Write-Host "    Found a real $kind here, not a link to $target"
            if ($item.PSIsContainer) {
                Get-ChildItem -Force -LiteralPath $dest | ForEach-Object { Write-Host "      $($_.Name)" }
            } else {
                Write-Host "      $($item.Length) bytes, modified $($item.LastWriteTime)"
            }
            $entry.Result = "FAIL  $dest -> $target (real $kind already present, left untouched)"
            continue
        }

        $pending.Add($entry)
    }
}

if ($pending.Count -gt 0) {
    if ($canSymlink) {
        foreach ($e in $pending) {
            try {
                New-SymLink $e.Target $e.Dest
                $e.Result = "PASS  $($e.Dest) -> $($e.Target)"
            } catch {
                $e.Result = "FAIL  $($e.Dest) -> $($e.Target) ($($_.Exception.Message))"
            }
        }
    }
    elseif ($NoElevate) {
        foreach ($e in $pending) {
            $e.Result = "FAIL  $($e.Dest) -> $($e.Target) (needs symlink privilege; -NoElevate was set)"
        }
    }
    else {
        $jobFile = Join-Path $env:TEMP "bootstrap-links-$PID.txt"
        $resFile = Join-Path $env:TEMP "bootstrap-results-$PID.txt"
        Set-Content -LiteralPath $jobFile -Encoding UTF8 -Value (
            $pending | ForEach-Object { "$($_.Target)`t$($_.Dest)" }
        )

        Write-Host "    Creating $($pending.Count) symlink(s), which needs administrator rights."
        Write-Host "    Approve the UAC prompt now (it may open behind this window)."

        $psExe = (Get-Process -Id $PID).Path
        $argv  = @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath,
            '-MakeLinksFrom', $jobFile, '-ResultFile', $resFile
        )

        $elevationError = $null
        try {
            Start-Process -FilePath $psExe -ArgumentList $argv -Verb RunAs -WindowStyle Hidden -Wait | Out-Null
        } catch {
            $elevationError = $_.Exception.Message
        }

        if ($elevationError) {
            foreach ($e in $pending) {
                $e.Result = "FAIL  $($e.Dest) -> $($e.Target) (elevation declined or failed: $elevationError)"
            }
        }
        elseif (-not (Test-Path -LiteralPath $resFile)) {
            foreach ($e in $pending) {
                $e.Result = "FAIL  $($e.Dest) -> $($e.Target) (elevated step produced no result)"
            }
        }
        else {
            $byDest = @{}
            foreach ($line in (Get-Content -LiteralPath $resFile)) {
                if (-not $line.Trim()) { continue }
                $p = $line -split "`t"
                $byDest[$p[1]] = $p
            }
            foreach ($e in $pending) {
                $p = $byDest[$e.Dest]
                if ($p -and $p[0] -eq 'PASS') {
                    $e.Result = "PASS  $($e.Dest) -> $($e.Target)"
                } elseif ($p) {
                    $e.Result = "FAIL  $($e.Dest) -> $($e.Target) ($($p[3]))"
                } else {
                    $e.Result = "FAIL  $($e.Dest) -> $($e.Target) (no result reported)"
                }
            }
        }

        Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath $jobFile
        Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath $resFile
    }
}

$failCount = 0
Write-Host ""
Write-Host "==> Link results"
foreach ($e in $plan) {
    Write-Host "    $($e.Result)"
    if ($e.Result -like 'FAIL*') { $failCount++ }
}

Write-Host ""
Write-Host "==> Done ($failCount failed link(s))."
Write-Host ""
Write-Host "$Parent\"
Write-Host "  develop\  (branch: develop)"
Write-Host "  main\     (branch: main)"
Write-Host "  backup\   (branch: backup, this checkout)"
Write-Host ""
Write-Host "Restore your global Claude Code config (~/.claude) separately from the"
Write-Host "ClaudeUserBackup repo -- see README.md in this branch for the full steps."

if ($failCount -gt 0) { exit 1 } else { exit 0 }
