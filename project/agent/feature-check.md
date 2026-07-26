# Claude Code feature check

Installed version: **2.1.220** (npm native build, `C:\nvm4w\nodejs\node_modules\@anthropic-ai\claude-code\bin\claude.exe`).
Checked on 2026-07-25 against three sources: `claude --help`, the command/skill registry strings
inside the installed binary, and the official docs at `code.claude.com/docs/en/commands`.
Nothing below has been configured or enabled; this is inventory only.

| Feature | Available | Exact invocation | Note |
|---|---|---|---|
| Statusline | yes | `/statusline`, or a `statusLine` block in `settings.json` | Setup flow writes to `~/.claude/settings.json`; a `statusline-setup` agent exists too. Not configured here. |
| Hooks | yes | `/hooks`, or a `hooks` block in `settings.json` | Views/edits tool-event hooks. None configured in your user or project settings today. |
| Plan mode | yes | `/plan`, Shift+Tab to cycle modes, or `claude --permission-mode plan` | Also drivable by the model via the EnterPlanMode/ExitPlanMode tools. |
| ultrathink | yes | Type `ultrathink` anywhere in a prompt | Confirmed live: this session's prompt triggered it. Raises reasoning depth for that turn only. |
| Effort levels | yes | `/effort <low\|medium\|high\|xhigh\|max\|ultracode\|auto>`, `claude --effort <level>`, `effortLevel` in settings | Yours is currently `"effortLevel": "high"` in `~/.claude/settings.json`. |
| `/context` | yes | `/context` or `/context all` | Colored grid of context usage plus trim suggestions. |
| `/usage` | yes | `/usage` (alias `/cost`) | Token/cost breakdown for the session. `/usage-credits` is a separate credits command. |
| `/insights` | yes | `/insights` | Generates a report analyzing your recent Claude Code sessions. |
| `/review` | yes | `/review` for a GitHub PR; `/code-review [low\|medium\|high\|xhigh\|max\|ultra] [--fix] [--comment]` for your working diff | `/code-review ultra` (and the `claude ultrareview` CLI subcommand) runs the cloud multi-agent review. |
| Rewind / checkpoints | yes | `/rewind` (alias `/checkpoint`) | Restores code and/or conversation to an earlier point. Double-Esc is the "go back and edit an earlier message" path, not the full checkpoint restore. |
| `/compact` with keep-instructions | renamed | `/compact <optional custom summarization instructions>` | No flag literally named `keep-instructions` exists in this build; you pass free-text steering as the argument. `/autocompact` sets the auto-summarize threshold. |
| Output styles | yes | `/output-style`; custom styles as `.claude/output-styles/*.md` | Registered in the config-command family alongside `/model`, `/permissions`, `/memory`. |
| Custom slash commands (`.claude/commands`) | yes (merged into skills) | `.claude/commands/foo.md` → `/foo` | Equivalent to `.claude/skills/foo/SKILL.md`; existing `commands/` files keep working. Skills add supporting files, frontmatter, and model-invocation. |
| Skills | yes | `/skills` to list, `/reload-skills`, `/skill-doctor`, `.claude/skills/<name>/SKILL.md` | This repo already ships six: tidy-cpp, tidy-md, verify, plan-roadmap, update-roadmap, revise-style, commit. |
| Sub-agents (`.claude/agents`) | yes, but `/agents` removed | Create/edit `.claude/agents/*.md` directly, or `claude --agents '<json>'` | `/agents` now only prints a "(removed)" pointer. Per-agent `model:` frontmatter is supported and is overridable per call. |
| Worktree flag | yes | `claude -w [name]` / `--worktree [name]`, optionally `--tmux` | Also available mid-session via the EnterWorktree/ExitWorktree tools and per-subagent `isolation: "worktree"`. |
| Headless mode | yes | `claude -p "prompt"` | Supports `--output-format text\|json\|stream-json`, `--json-schema`, `--max-budget-usd`, `--input-format stream-json`. Note `/help` itself is not available under `-p`. |
| Memory and auto-memory | yes | `/memory`; `CLAUDE.md` files plus `~/.claude/projects/<slug>/memory/` | Auto-memory is already live for this project (13 entries indexed in its `MEMORY.md`). `/pause-memory` suspends it; `--bare` disables it. |
| Conversation forking (`/branch`) | yes | `/branch [name]` | Branches the conversation in place. Related but distinct: `/fork` copies it into a background session, `--fork-session` forks on resume. |
| Side questions (`/btw`) | yes | `/btw [question]` | Answers without adding the exchange to the main conversation history. |
| `/loop` and scheduled runs | yes | `/loop [interval] [prompt]` (alias `/proactive`), `/loops` to list/create/delete, `/schedule [time] [prompt]` (alias `/routines`) | `/loop` runs while the session stays open; omit the interval to let the model self-pace. `/schedule` creates cron-style cloud agents, one-time runs included. |

## Adjacent commands present in this build, not asked about

`/goal` (keep working until a condition holds), `/subtask` (hand a subagent your full context),
`/background` (detach the session), `/tasks`, `/diff`, `/focus`, `/doctor` (alias `/checkup`),
`/powerup`, `/advisor`, `/import` (config from another agent), `/teleport` (resume a claude.ai
session), `/ultraplan`, `/security-review`, `/fewer-permission-prompts`, `/deep-research`,
`/batch`, `/safe-mode` via `claude --safe-mode`.

## Gotcha found while checking

Running `claude -p "/help"` from Git Bash fails twice over: MSYS path conversion rewrites `/help`
into a Windows path, and `/help` is not available in non-interactive mode regardless. Use
`MSYS_NO_PATHCONV=1` for the first problem; for the second, the command list has to come from an
interactive session or from the binary itself.
