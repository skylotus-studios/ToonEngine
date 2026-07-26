# ToonEngine Dev-Environment Backup (`backup` Branch)

This is an orphan branch. It has no relation to `develop` or `main`'s history and holds only
files that must survive a wiped machine but never get pushed as part of the app: `CLAUDE.md`,
`MEMORY.md`, `ARCHIVE.md`, both style guides, and the project-level Claude skills, agents, and
`.agent` scratch directory. `bootstrap.cmd` (Windows) and `bootstrap.sh` (macOS, Linux) rebuild
the whole layout: both `develop` and `main` worktrees, their submodules, and every link, from a
single clone of this branch.

## Recovery: Rebuilding on a New Machine from Nothing

Ordered steps, starting from a machine with nothing on it.

1. Install Node.js (Claude Code requires it), then install Claude Code itself:

   ```
   npm install -g @anthropic-ai/claude-code
   ```

2. Clone this branch to where the three worktrees will live side by side (adjust the path
   to taste; the rest of these steps assume `C:/dev/ToonEngine`):

   ```
   git clone --branch backup https://github.com/skylotus-studios/ToonEngine C:/dev/ToonEngine/backup
   cd C:/dev/ToonEngine/backup
   ```

3. Run the bootstrap script. From Windows command line:

   ```
   bootstrap.cmd
   ```

   From a macOS / Linux shell instead:

   ```
   ./bootstrap.sh
   ```

   Either way, this adds the `develop` and `main` worktrees as siblings of this checkout, runs
   `git submodule update --init --recursive` in both, and re-links `CLAUDE.md`, `MEMORY.md`,
   `ARCHIVE.md`, both style guides, `.claude/skills`, `.claude/agents`, and `.agent` into each
   worktree from the files on this branch. It prints a symlink check up front and a
   PASS/FAIL line per link at the end; re-running it is always safe.

   Every link is a symbolic link. Creating one on Windows requires
   `SeCreateSymbolicLinkPrivilege`, which a normal logon session lacks unless it is elevated
   or Developer Mode was already on when the session started. When the script finds it cannot
   create symlinks, it re-runs the link step alone with administrator rights, which costs one
   UAC prompt. Approve it and the links are created; the prompt can open behind the console
   window. Nothing else elevates, git included, so no repo file ends up owned by
   Administrator.

   Links that already point at the right target are left alone, so a re-run with nothing to
   do never prompts. A junction or a misaimed symlink left by an earlier version is replaced
   with a correct symlink, and nothing is removed until its replacement is ready, so a
   declined prompt cannot leave a destination empty. Pass `-NoElevate` to skip the prompt
   entirely and have the affected links reported as failures instead.

4. Pull LFS assets in both worktrees (models used by the reference `ToonEngineOld` copy and
   any other LFS-tracked content):

   ```
   git -C ../develop lfs install && git -C ../develop lfs pull
   git -C ../main lfs install && git -C ../main lfs pull
   ```

5. Restore your global Claude Code config. This branch does not carry `~/.claude`; that is
   backed up separately in the `ClaudeUserBackup` repo. Clone it and copy `settings.json` (and
   anything else you keep there) into place:

   ```
   git clone <ClaudeUserBackup-remote-url> /tmp/claude-user-backup
   cp /tmp/claude-user-backup/settings.json ~/.claude/settings.json
   ```

   Copy over any other files that repo tracks (e.g. `keybindings.json`) the same way.

6. Build. From a Developer PowerShell for VS 2022 (or let CLion supply the toolchain):

   ```
   cd C:/dev/ToonEngine/develop
   cmake --preset windows-debug
   cmake --build --preset windows-debug
   ./build/windows-debug/ToonEngine.exe
   ```

At this point `C:/dev/ToonEngine/` has three siblings, `develop`, `main`, and `backup`, matching
the layout before the wipe.