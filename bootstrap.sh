#!/usr/bin/env bash
# bootstrap.sh — rebuild the ToonEngine dev environment from scratch.
#
# Recovery flow after C:/dev/ToonEngine is lost entirely (dead drive,
# corrupted object store, wiped machine): see README.md in this branch for
# the full ordered recovery procedure, including installing Claude Code and
# restoring ~/.claude/settings.json. Short version:
#
#   git clone --branch backup <remote-url> C:/dev/ToonEngine/backup
#   cd C:/dev/ToonEngine/backup
#   ./bootstrap.sh
#
# This adds the `develop` and `main` worktrees as siblings of this checkout,
# initializes their submodules recursively, and re-links CLAUDE.md,
# MEMORY.md, ARCHIVE.md, both style guides, and the project-level Claude
# skills, agents, and .agent scratch dir from this branch so both worktrees
# come back exactly as they were. Your global Claude Code config (~/.claude)
# is backed up separately in the ClaudeUserBackup repo — not here.
#
# Safe to re-run: existing worktrees are left alone, correct symlinks are
# left alone, and a real (non-symlink) file or directory already sitting at
# a link destination is never touched -- the script reports it and moves on.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARENT="$(cd "$HERE/.." && pwd)"
DEVELOP="$PARENT/develop"
MAIN="$PARENT/main"

RESULTS=()
FAIL_COUNT=0

# Detects whether symlink creation is likely to work on this machine.
# Windows requires either an elevated (Run as Administrator) shell or
# Developer Mode enabled; other platforms create symlinks natively as any
# user, so there is nothing to check there.
check_elevation() {
  if ! command -v powershell.exe >/dev/null 2>&1; then
    echo "==> Privilege check: not Windows (or no powershell.exe) -- symlinks need no special privilege here."
    return 0
  fi

  local is_admin dev_mode
  is_admin="$(powershell.exe -NoProfile -Command \
    '([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)' \
    2>/dev/null | tr -d '\r\n')"
  dev_mode="$(powershell.exe -NoProfile -Command \
    '(Get-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock" -Name AllowDevelopmentWithoutDevLicense -ErrorAction SilentlyContinue).AllowDevelopmentWithoutDevLicense' \
    2>/dev/null | tr -d '\r\n')"

  if [ "$is_admin" = "True" ]; then
    echo "==> Privilege check: OK (running elevated)."
    return 0
  fi
  if [ "$dev_mode" = "1" ]; then
    echo "==> Privilege check: OK (Developer Mode is enabled)."
    return 0
  fi

  echo "==> Privilege check: NOT elevated and Developer Mode is NOT enabled." >&2
  echo "    Symlink creation below will most likely fail. Fix one of:" >&2
  echo "      - Settings > Privacy & security > For developers > Developer Mode (On)" >&2
  echo "      - re-run this script from an elevated (Run as Administrator) shell" >&2
  echo "    Continuing anyway -- failures are reported per-link below." >&2
  return 1
}

# Creates a symlink at $2 pointing to $1, recording a PASS/FAIL result.
# Refuses to touch $2 if something real (not already a symlink) is there.
link() {
  local target="$1" dest="$2"

  if [ -L "$dest" ]; then
    if [ "$(readlink "$dest")" = "$target" ]; then
      RESULTS+=("PASS  $dest -> $target (already linked)")
      return 0
    fi
    # Wrong target: it's still just a symlink, safe to replace.
    rm -f "$dest"
  elif [ -e "$dest" ]; then
    local kind="file"
    [ -d "$dest" ] && kind="directory"
    echo "==> Refusing to overwrite $dest" >&2
    echo "    Found a real $kind here (not a symlink):" >&2
    ls -la "$dest" >&2
    RESULTS+=("FAIL  $dest -> $target (real $kind already present, left untouched)")
    FAIL_COUNT=$((FAIL_COUNT + 1))
    return 1
  fi

  mkdir -p "$(dirname "$dest")"

  if ln -s "$target" "$dest" 2>/dev/null && [ -L "$dest" ]; then
    RESULTS+=("PASS  $dest -> $target")
    return 0
  fi
  rm -rf "$dest"

  if command -v powershell.exe >/dev/null 2>&1; then
    powershell.exe -NoProfile -Command \
      "New-Item -ItemType SymbolicLink -Path '$(cygpath -w "$dest")' -Target '$(cygpath -w "$target")' -Force | Out-Null" \
      2>/dev/null
    if [ -L "$dest" ]; then
      RESULTS+=("PASS  $dest -> $target (via PowerShell)")
      return 0
    fi
  fi

  RESULTS+=("FAIL  $dest -> $target (no admin/Developer Mode, or no powershell.exe)")
  FAIL_COUNT=$((FAIL_COUNT + 1))
  return 1
}

check_elevation || true

echo "==> Worktrees"
if [ ! -e "$DEVELOP/.git" ]; then
  git worktree add "$DEVELOP" develop
else
  echo "    develop already present, skipping"
fi
if [ ! -e "$MAIN/.git" ]; then
  git worktree add "$MAIN" main
else
  echo "    main already present, skipping"
fi

echo "==> Submodules"
git -C "$DEVELOP" submodule update --init --recursive
git -C "$MAIN" submodule update --init --recursive

echo "==> Project-level Claude files (develop, main)"
for W in "$DEVELOP" "$MAIN"; do
  mkdir -p "$W/.claude"
  mkdir -p "$W/docs"
  link "$HERE/claude.md"           "$W/CLAUDE.md"          || true
  link "$HERE/memory.md"           "$W/MEMORY.md"          || true
  link "$HERE/archive.md"          "$W/ARCHIVE.md"         || true
  link "$HERE/cpp-style-guide.md"  "$W/docs/cpp-style-guide.md" || true
  link "$HERE/md-style-guide.md"   "$W/docs/md-style-guide.md"  || true
  link "$HERE/project/skills"      "$W/.claude/skills"     || true
  link "$HERE/project/agents"      "$W/.claude/agents"     || true
  link "$HERE/project/agent"       "$W/.agent"              || true
done

echo
echo "==> Link results"
for r in "${RESULTS[@]}"; do
  echo "    $r"
done

cat <<EOF

==> Done ($FAIL_COUNT failed link(s)).

$PARENT/
  develop/  (branch: develop)
  main/     (branch: main)
  backup/   (branch: backup, this checkout)

Restore your global Claude Code config (~/.claude) separately from the
ClaudeUserBackup repo -- see README.md in this branch for the full steps.
EOF

[ "$FAIL_COUNT" -eq 0 ]
