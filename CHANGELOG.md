# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### Added
- Clickable links in messages (Markdown `[text](url)` and plain URLs).
- Styled blockquotes (`>`).
- GitHub Actions: CI matrix for GtkSourceView 3→4 fallback, artifacts upload.
- GitHub Actions: tag-based Release with zipped assets and SHA256.

### Changed
- README: badges, screenshot, bilingual sections.

### Fixed
- CI now installs `geany` (contains headers & geany.pc) instead of non-existent `libgeany-dev`.

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
