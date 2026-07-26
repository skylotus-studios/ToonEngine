#!/usr/bin/env bash
# bootstrap.sh - rebuild the ToonEngine dev environment from scratch.
#
# Recovery flow after C:/dev/ToonEngine is lost entirely (dead drive,
# corrupted object store, wiped machine): see README.md in this branch for
# the full ordered recovery procedure, including installing Claude Code and
# restoring ~/.claude/settings.json. Short version:
#
#   git clone --branch backup <remote-url> C:/dev/ToonEngine/backup
#   cd C:/dev/ToonEngine/backup
#   ./bootstrap.sh          # or bootstrap.cmd from cmd.exe
#
# On Windows this hands off to bootstrap.ps1, which is the single source of
# truth for the Windows link strategy: every link is a symbolic link, and if
# the session lacks SeCreateSymbolicLinkPrivilege the link step alone re-runs
# elevated behind one UAC prompt. The native path below runs on macOS and
# Linux, where an unprivileged symlink always works.
#
# This adds the `develop` and `main` worktrees as siblings of this checkout,
# initializes their submodules recursively, and re-links CLAUDE.md,
# MEMORY.md, ARCHIVE.md, both style guides, and the project-level Claude
# skills, agents, and .agent scratch dir from this branch so both worktrees
# come back exactly as they were. Your global Claude Code config (~/.claude)
# is backed up separately in the ClaudeUserBackup repo, not here.
#
# Safe to re-run: existing worktrees are left alone, links already pointing
# at the right target are left alone, and a real (non-link) file or directory
# already sitting at a link destination is never touched -- the script
# reports it and moves on.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARENT="$(cd "$HERE/.." && pwd)"
DEVELOP="$PARENT/develop"
MAIN="$PARENT/main"

# Windows (Git Bash, MSYS, Cygwin): delegate rather than duplicate. MSYS `ln -s`
# also silently degrades to a file copy when it cannot make a real link, which
# would look like success and quietly desynchronize the two checkouts.
if command -v powershell.exe >/dev/null 2>&1; then
  PS_SCRIPT="$HERE/bootstrap.ps1"
  if command -v cygpath >/dev/null 2>&1; then
    PS_SCRIPT="$(cygpath -w "$PS_SCRIPT")"
  fi
  exec powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PS_SCRIPT" "$@"
fi

RESULTS=()
FAIL_COUNT=0

# Creates a symlink at $2 pointing to $1, recording a PASS/FAIL result.
# Refuses to touch $2 if something real (not already a symlink) is there.
link() {
  local target="$1" dest="$2"

  if [ ! -e "$target" ]; then
    RESULTS+=("FAIL  $dest -> $target (link target does not exist)")
    FAIL_COUNT=$((FAIL_COUNT + 1))
    return 1
  fi

  if [ -L "$dest" ]; then
    if [ "$(readlink "$dest")" = "$target" ]; then
      RESULTS+=("PASS  $dest -> $target (already linked)")
      return 0
    fi
    # Wrong target, but still just a symlink: safe to replace.
    rm -f "$dest"
  elif [ -e "$dest" ]; then
    local kind="file"
    [ -d "$dest" ] && kind="directory"
    echo "==> Refusing to overwrite $dest" >&2
    echo "    Found a real $kind here, not a link to $target:" >&2
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

  RESULTS+=("FAIL  $dest -> $target (could not create symlink)")
  FAIL_COUNT=$((FAIL_COUNT + 1))
  return 1
}

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
  mkdir -p "$W/.claude" "$W/docs"
  link "$HERE/claude.md"           "$W/CLAUDE.md"                || true
  link "$HERE/memory.md"           "$W/MEMORY.md"                || true
  link "$HERE/archive.md"          "$W/ARCHIVE.md"               || true
  link "$HERE/cpp-style-guide.md"  "$W/docs/cpp-style-guide.md"  || true
  link "$HERE/md-style-guide.md"   "$W/docs/md-style-guide.md"   || true
  link "$HERE/project/skills"      "$W/.claude/skills"           || true
  link "$HERE/project/agents"      "$W/.claude/agents"           || true
  link "$HERE/project/agent"       "$W/.agent"                   || true
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
