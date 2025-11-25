# CONTRIBUTING-codex.md

Operational guide for using an LLM (Codex/GPT) to contribute to **Geany AI Chat (pro)** quickly, safely, and with low token usage.

---

## TL;DR (what Codex must do)
- **Input → minimal**: reference `ROADMAP.md` and the exact functions/files to change.
- **Output → unified diff ONLY** (`git apply`-able). No prose, no full files.
- **One task per patch** (small, focused, testable).
- **Respect constraints** (below). **Fail closed** when ambiguous.

---

## Project constraints (must-keep)
- Language: **C99**. Platform: **GTK3**, **GLib**, **GtkSourceView 3** (fallback 4 only if needed).
- Geany integration: `geany_load_module()`; `plugin_init` returns `TRUE`; use Geany **message window notebook** for the tab.
- UI thread-safety: **all GTK updates on main thread** via `g_idle_add(...)`.
- **No GCC nested functions** (no trampolines/execstack). Use `static` callbacks + small context structs.
- Memory/errors: prefer **GLib** helpers (`GString`, `g_new0`, `g_autofree` if available, `g_clear_pointer`), robust error paths, free all allocations.
- **UI labels in French**. Author string: **“GPT-5 Thinking (ChatGPT)”**. License: **MIT**.
- Build flags: via `pkg-config geany gtk+-3.0 gtksourceview-3.0` (or `gtksourceview-4`), linker: `-Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now`.
- Debian/Ubuntu headers: package **`geany`** (provides `geany.pc`).
- **Style**: Allman, spaces (no tabs), target **≤ 80 cols** where reasonable.
- **Known-good lines not to regress**:
  - JSON parsing: `if (*p == '"' && *(p - 1) != '\\') break;`
  - Declaration order: `static Ui ui;` placed **before** any helper using it (e.g., `autoscroll_idle_cb()`).
- Release: **no binaries committed**. CI builds on push/PR. Release on tag **`v*`** with assets.

---

## Workflow for Codex (per task)
1) **Read**: `ROADMAP.md` → pick the next Work Order (e.g., `003`, `004`).
2) **Focus**: identify **one** function/section to change (e.g., `build_assistant_composite_from_markdown()` in `ai_chat.c`).
3) **Implement**: produce a **single unified diff**:
   - Header must start with `diff --git a/... b/...`
   - Include `index ...` and `---/+++` lines
   - Patch paths relative to repo root
4) **Self-check** (Codex):
   ```bash
   git apply --check your.patch
   ```
   If it fails: adjust paths/hunks; avoid context drift.
5) **Deliver**: output **diff only**. No explanations, no code blocks fenced as markdown if caller says “raw diff”.

**Commit message (first line)** should be concise and conventional:
- `feat: ...` new feature
- `fix: ...` bugfix
- `chore(ui): ...` visual/spacing changes
- `docs: ...` docs/README updates

**Patch filename**: `00N-<slug>.patch` (e.g., `003-click-swallow.patch`).

---

## Input template (for humans prompting Codex)
Use this minimal prompt style to save tokens:

```
Task: implement <short feature>, per ROADMAP.md item <ID>.
Constraints: unchanged (see CONTRIBUTING-codex.md & README).
Files: ai_chat.c (function <name>), and only that unless required.
Acceptance:
- <bullet 1>
- <bullet 2>
Output format: unified diff ONLY (git apply). No prose. No full files.
```

**Examples of “Files”** (pick one):
- `ai_chat.c` — function `build_assistant_composite_from_markdown()`
- `ai_chat.c` — UI root container creation (to connect event blocker)
- `README.md`, `CHANGELOG.md` — docs only

---

## Output rules (hard requirements)
- **Unified diff only**. No commentary, no banners, no ANSI color.
- **Do not wrap** the diff in markdown fences unless explicitly asked.
- Keep diffs **small** (< ~200 lines) and **atomic** (one concern).
- Do **not** introduce platform-specific APIs; stick to GTK/GLib/Geany APIs already used.
- Avoid touching unrelated whitespace beyond the changed blocks (respect 80 cols where practical).

---

## Testing & acceptance (quick manual checks)
- Build: `make` (or project CI). No new warnings ideally.
- Functional: test the acceptance bullets from the “Task”.
- Regressions: ensure code blocks, streaming cancel, copy/insert buttons still work.
- The chat tab must **not** leak events to Geany’s “open file” handler (when relevant to the patch).

**Test snippets to keep handy** (for link features):
- `[OpenAI](https://openai.com)`
- `https://example.com/path?q=1)` (trim `)`)
- `www.geany.org` (normalize to `https://`)
- `mailto:john@doe.tld` (allowed), `ftp://ftp.gnu.org` (allowed)
- `file:///etc/hosts`, `javascript:alert(1)`, `data:text/html;base64,...` (ignored)
- Triple-backticks ```…``` — must not be linkified.

---

## Token-saving tips
1) Reuse context: say “see ROADMAP.md, constraints unchanged” instead of pasting specs.
2) Target files & functions precisely (line- or name-level).
3) Output diff only; no prose or full files.
4) Batch tiny changes, but keep **one feature per patch**.
5) If a file is large, quote only the **local** excerpt in the prompt (or none if Codex can read the repo).

---

## Common pitfalls & how to avoid them
- **“patch fragment without header”**: ensure the diff begins with `diff --git a/... b/...` and has `---/+++` lines.
- **Wrong path depth**: paths must match repo root; prefer diffs generated by `git diff` locally.
- **Nested functions**: forbidden; always declare `static` top-level callbacks.
- **UI thread**: never update GTK from worker threads; schedule with `g_idle_add(...)`.
- **Memory leaks**: free all temporaries; use GLib helpers.
- **French labels**: keep user-facing strings in French (buttons, tooltips, section titles).

---

## Apply & release
```bash
# Dry-run
git apply --check 00N-<slug>.patch

# Apply
git apply 00N-<slug>.patch

# Build & test
make

# Commit & push
git add -A
git commit -m "<same as patch subject>"
git push -u origin HEAD

# Release later via tag v*
git tag v1.2.3
git push origin v1.2.3
```
