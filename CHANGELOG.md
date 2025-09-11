# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### Added
- Clickable links in messages (Markdown `[text](url)` and plain URLs).
- Styled blockquotes (`>`): visual left rule + wrapped text.
- Heuristic language detection for unlabeled code fences.
- Light/Dark theme toggle scoped to chat pane with GtkSourceView scheme switching.
- System prompt configuration (persisted) and UI dialog; included for Ollama (history) and OpenAI (messages).
- Copy-all preserves fence language (```lang) from GtkSourceBuffer.
- GitHub Actions: CI matrix for GtkSourceView 3→4 fallback, artifacts upload.
- GitHub Actions: tag-based Release with zipped assets and SHA256.

### Changed
- README: badges, screenshot, bilingual sections.
- “Send selection” now appends selection into the input box instead of sending immediately.

### Fixed
- CI now installs `geany` (contains headers & geany.pc) instead of non-existent `libgeany-dev`.
- Open links using the widget toplevel with `gtk_show_uri_on_window` (fixes build and ensures correct parent window).
- After inserting code into the editor, return focus to Scintilla.
- Ollama history includes assistant replies to prevent repeated answers on subsequent prompts.
- Reset history when API or model changes to avoid mixed contexts.
- HTTP error reporting includes response codes and curl messages.
- Input area and code blocks honor theme colors; visible caret in dark mode.

## [1.0.0] - 2025-09-10
### Added
- Geany bottom-panel chat tab with **streaming** responses (Ollama JSON-lines / OpenAI-compatible SSE).
- **Send editor selection** as prompt.
- **Stop** button to cancel in-flight requests.
- Markdown ```fences``` rendered as **GtkSourceView** code blocks with syntax highlighting.
- Per-block **Copy** and **Insert into editor**.
- **Auto-scroll** during streaming.
- On-disk **preferences** (`~/.config/geany/ai_chat.conf`).
- Full JSON string **unescape** in stream (`\uXXXX`, `\n`, `\t`, etc.).

### Fixed
- Removed GCC nested-function trampolines (no more executable stack requirement).
- UI updates marshalled to GTK main loop via `g_idle_add` to avoid race/segfaults.

### Security
- Linker hardening: `-Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now`.
