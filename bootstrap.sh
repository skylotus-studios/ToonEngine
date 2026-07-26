#!/usr/bin/env bash
# bootstrap.sh — rebuild the ToonEngine dev environment from scratch.
#
# Recovery flow after C:/dev/ToonEngine is lost entirely (dead drive,
# corrupted object store, wiped machine):
#
#   git clone --branch backup <remote-url> C:/dev/ToonEngine/backup
#   cd C:/dev/ToonEngine/backup
#   ./bootstrap.sh
#
# This adds the `develop` and `main` worktrees as siblings of this checkout,
# initializes their submodules recursively, and re-links CLAUDE.md,
# MEMORY.md, ARCHIVE.md, both style guides, and the project/user Claude
# skills, agents, commands, and settings.json from this branch so both
# worktrees and your global Claude Code config (~/.claude) come back exactly
# as they were.
#
# Safe to re-run: existing worktrees and correct symlinks are left alone.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARENT="$(cd "$HERE/.." && pwd)"
DEVELOP="$PARENT/develop"
MAIN="$PARENT/main"

# Creates a symlink at $2 pointing to $1. Plain `ln -s` on Windows silently
# falls back to copying the target instead of linking when the shell lacks
# SeCreateSymbolicLinkPrivilege (no admin / Developer Mode) — this detects
# that and retries via PowerShell, which requests the privilege properly.
link() {
  local target="$1" dest="$2"
  if [ -L "$dest" ] && [ "$(readlink "$dest")" = "$target" ]; then
    return 0
  fi
  rm -rf "$dest"
  mkdir -p "$(dirname "$dest")"
  if ln -s "$target" "$dest" 2>/dev/null && [ -L "$dest" ]; then
    return 0
  fi
  rm -rf "$dest"
  if command -v powershell.exe >/dev/null 2>&1; then
    powershell.exe -NoProfile -Command \
      "New-Item -ItemType SymbolicLink -Path '$(cygpath -w "$dest")' -Target '$(cygpath -w "$target")' -Force | Out-Null"
  else
    echo "error: could not create symlink $dest -> $target (no admin/Developer Mode and no powershell.exe)" >&2
    exit 1
  fi
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
  link "$HERE/claude.md"           "$W/CLAUDE.md"
  link "$HERE/memory.md"           "$W/MEMORY.md"
  link "$HERE/archive.md"          "$W/ARCHIVE.md"
  link "$HERE/cpp-style-guide.md"  "$W/docs/cpp-style-guide.md"
  link "$HERE/md-style-guide.md"   "$W/docs/md-style-guide.md"
  link "$HERE/project/skills"      "$W/.claude/skills"
  link "$HERE/project/agents"      "$W/.claude/agents"
done

echo "==> User-level Claude files (~/.claude)"
USER_CLAUDE="${CLAUDE_CONFIG_DIR:-$HOME/.claude}"
mkdir -p "$USER_CLAUDE"
link "$HERE/user/skills"   "$USER_CLAUDE/skills"
link "$HERE/user/agents"   "$USER_CLAUDE/agents"
link "$HERE/user/commands" "$USER_CLAUDE/commands"
link "$HERE/settings.json" "$USER_CLAUDE/settings.json"

cat <<EOF
==> Done.

$PARENT/
  develop/  (branch: develop)
  main/     (branch: main)
  backup/   (branch: backup, this checkout)

~/.claude/{skills,agents,commands,settings.json} now point into backup/user/.
EOF
