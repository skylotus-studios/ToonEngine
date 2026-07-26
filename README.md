# ToonEngine dev-environment backup (`backup` branch)

This is an orphan branch. It has no relation to `develop` or `main`'s history and holds only
files that must survive a wiped machine but never get pushed as part of the app: `CLAUDE.md`,
`MEMORY.md`, `ARCHIVE.md`, both style guides, and the project-level Claude skills, agents, and
`.agent` scratch directory. `bootstrap.sh` rebuilds the whole layout — both `develop` and `main`
worktrees, their submodules, and every symlink — from a single clone of this branch.

## Recovery: rebuilding on a new machine from nothing

Ordered steps, starting from a machine with nothing on it.

1. **Install Node.js** (Claude Code requires it), then install Claude Code itself:

   ```
   npm install -g @anthropic-ai/claude-code
   ```

2. **Clone this branch** to where the three worktrees will live side by side (adjust the path
   to taste; the rest of these steps assume `C:/dev/ToonEngine`):

   ```
   git clone --branch backup <remote-url> C:/dev/ToonEngine/backup
   cd C:/dev/ToonEngine/backup
   ```

3. **Run the bootstrap script.** From a Git Bash shell:

   ```
   ./bootstrap.sh
   ```

   From `cmd.exe` (no Git Bash shell needed — it finds and calls into one for you):

   ```
   bootstrap.cmd
   ```

   Either way, this adds the `develop` and `main` worktrees as siblings of this checkout, runs
   `git submodule update --init --recursive` in both, and re-links `CLAUDE.md`, `MEMORY.md`,
   `ARCHIVE.md`, both style guides, `.claude/skills`, `.claude/agents`, and `.agent` into each
   worktree from the files on this branch. It prints a privilege check up front and a
   PASS/FAIL line per link at the end; re-running it is always safe.

   Symlink creation on Windows needs either Developer Mode on
   (Settings > Privacy & security > For developers) or an elevated shell. If the script's
   privilege check reports neither, enable Developer Mode (or re-run elevated) and run
   `./bootstrap.sh` again — it will only retry the links that failed.

4. **Pull LFS assets** in both worktrees (models used by the reference `ToonEngineOld` copy and
   any other LFS-tracked content):

   ```
   git -C ../develop lfs install && git -C ../develop lfs pull
   git -C ../main lfs install && git -C ../main lfs pull
   ```

5. **Restore your global Claude Code config.** This branch does not carry `~/.claude` — that is
   backed up separately in the `ClaudeUserBackup` repo. Clone it and copy `settings.json` (and
   anything else you keep there) into place:

   ```
   git clone <ClaudeUserBackup-remote-url> /tmp/claude-user-backup
   cp /tmp/claude-user-backup/settings.json ~/.claude/settings.json
   ```

   Copy over any other files that repo tracks (e.g. `keybindings.json`) the same way.

6. **Build.** From a Developer PowerShell for VS 2022 (or let CLion supply the toolchain):

   ```
   cd C:/dev/ToonEngine/develop
   cmake --preset windows-debug
   cmake --build --preset windows-debug
   ./build/windows-debug/ToonEngine.exe
   ```

At this point `C:/dev/ToonEngine/` has three siblings — `develop`, `main`, and `backup` — matching
the layout before the wipe.
