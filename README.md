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
   worktree from the files on this branch. It prints the link strategy up front and a
   PASS/FAIL line per link at the end; re-running it is always safe.

   No admin rights or Developer Mode needed. Creating a symbolic link on Windows requires
   `SeCreateSymbolicLinkPrivilege`, which a plain logon session usually lacks, so the script
   tests for it and falls back to junctions for directories and hard links for files. Those
   need no privilege and resolve identically for reading and for editing in place, so
   `develop/CLAUDE.md` and `backup/claude.md` stay one file either way.

   One caveat comes with the hard-link fallback: an editor that saves by writing a new file
   and renaming it over the old one breaks the link, leaving two files that no longer track
   each other. Re-running the script detects that and reports it rather than silently
   overwriting either copy.

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