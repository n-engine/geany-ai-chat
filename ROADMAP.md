# ROADMAP — Geany Plugin “AI Chat (pro)”

Conserver les contraintes projet : **C99**, **GTK3**, **GLib**, **GtkSourceView 3/4 (fallback)**, **MAJ UI uniquement sur le main thread via `g_idle_add`**, **pas de fonctions imbriquées GCC**, **labels FR**, **auteur “GPT-5 Thinking (ChatGPT)”**, **MIT**.  
Build via `pkg-config geany gtk+-3.0 gtksourceview-3.0` (fallback 4 si dispo). Linker: `-Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now`.

---

## D+1 — Work Orders (Codex-friendly)

### 003 — Anti-propagation des clics (chat pane)
**But**: aucun clic dans l’onglet chat ne remonte au handler “ouvrir fichier” de Geany.

**Fichier**: `ai_chat.c`  
**Étapes**:
1. Ajouter un handler global d’événements pour le conteneur racine du chat (widget qui emballe les messages).  
   - Nom: `static gboolean chat_event_blocker(GtkWidget*, GdkEvent*, gpointer)`.  
   - Retourner **TRUE** pour `GDK_BUTTON_PRESS`, `GDK_BUTTON_RELEASE`, `GDK_2BUTTON_PRESS`, `GDK_MOTION_NOTIFY`, `GDK_SCROLL` lorsque la cible **n’est pas** un bouton interne, **ni** un label avec lien (`activate-link`), **ni** un `GtkSourceView`.
2. Connecter le handler après création/pack du conteneur racine (“Chat IA”).  
   - `gtk_widget_add_events(..., GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK);`  
   - `g_signal_connect(root_container, "event", G_CALLBACK(chat_event_blocker), NULL);`
3. Vérifier que les liens (signal `activate-link`) et les boutons (“Copier”, “Insérer”, “Stop”) restent fonctionnels.

**Critères d’acceptation**:
- Cliquer dans une zone vide du chat n’ouvre plus de fichiers.
- Liens/boutons inchangés.

**Commit**: `feat: swallow mouse events in chat pane to prevent Geany open-file handler`

---

### 004 — Durcissement des liens & schémas autorisés
**But**: URL parsing plus sûr, schémas filtrés.

**Fichier**: `ai_chat.c`  
**Étapes**:
1. Dans le helper de linkification (Pango `<a href>`):  
   - **Autoriser**: `http`, `https`, `mailto`, `ftp`.  
   - **Ignorer** (non transformés): `file`, `javascript`, `data`.  
   - `www.` → préfixer `https://`.  
   - Trim intelligent de `).,;:!?` en fin, avec gestion simple des parenthèses (si fin par `)` sans `(` ouvrant, retirer `)`).
2. Ne pas linkifier dans les blocs ```code``` (vérifier, conserver).
3. Tests manuels (voir section **Cas de test**).

**Critères d’acceptation**:
- Schémas interdits restent texte brut.
- Schémas autorisés cliquables et s’ouvrent correctement.

**Commit**: `feat: harden linkification (scheme allowlist, www→https, safer trailing punctuation)`

---

### 005 — Polish visuel (paragraphes & blockquotes)
**But**: lisibilité homogène + contraste thème clair/sombre.

**Fichier**: `ai_chat.c`  
**Étapes**:
1. Paragraphes (`GtkLabel`): marges verticales faibles et cohérentes (top/bottom).
2. Blockquotes (déjà stylés): vérifier contraste sombre/clair; ajouter padding interne (gauche/droite) si nécessaire.
3. Boutons des blocs de code: alignement vertical stable (marges homogènes).

**Critères d’acceptation**:
- Lecture confortable en thème clair **et** sombre.
- Pas de “sauts” visuels autour des quotes/blocs de code.

**Commit**: `chore(ui): spacing adjustments for paragraphs and quotes; align code-block controls`

---

### 006 — Préférences “Liens cliquables”
**But**: permettre d’activer/désactiver la feature.

**Fichiers**: `ai_chat.c`, `README.md`, config.  
**Étapes**:
1. Clé `links_enabled=true` dans `~/.config/geany/ai_chat.conf` (lecture/écriture).
2. Case à cocher **“Rendre les liens cliquables”** dans les préférences du plugin.
3. Rendu: si OFF → labels en texte brut (pas de linkification).  
   - Option: re-render de la dernière réponse si trivial; sinon, documenter “appliqué aux nouveaux messages”.

**Critères d’acceptation**:
- Toggle ON/OFF persistant.
- Avec OFF, aucun `<a>` inséré.

**Commit**: `feat(prefs): add 'links_enabled' setting and toggle in preferences dialog`

---

### 007 — Vérifs & Documentation
**But**: fiabiliser et documenter.

**Fichiers**: `README.md`, `CHANGELOG.md`, `ai_chat.c` (commentaires).  
**Étapes**:
1. Cas limites (tests manuels) — voir ci-dessous.
2. Doc: schémas autorisés vs ignorés; préférence “liens cliquables”; anti-propagation des clics.  
   Aligner EN/FR (éviter contradictions “coming next” vs “done”).

**Critères d’acceptation**:
- Cas de test conformes.
- README/CHANGELOG alignés.

**Commit**: `docs: update README/CHANGELOG for link handling, preferences, and click swallowing`

---

## Cas de test (copier-coller dans la chat box)
- `[OpenAI](https://openai.com)`
- `https://example.com/path?q=1)`
- `www.geany.org`
- `mailto:john@doe.tld`
- `ftp://ftp.gnu.org`
- `file:///etc/hosts`  ← doit rester texte
- `javascript:alert(1)` ← doit rester texte
- Bloc code (aucun lien dedans) :
  ```
  ```
  ping https://example.com
  www.geany.org
  ```
  ```
- Blockquote (liens OK) :
  > voir https://example.com

---

## Rappels d’implémentation (Codex)
- MAJ UI **uniquement** sur le **main thread** (`g_idle_add` si nécessaire).
- **Interdits**: fonctions imbriquées GCC.
- **GLib**: `g_new0`, `GString`, `g_clear_pointer` — libérer toutes les allocs.
- **Labels** en **français**.
- Build via `pkg-config geany gtk+-3.0 gtksourceview-3.0` (fallback 4 si besoin).
- Linker: `-Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now`.

---

## Workflow (rappel)
```bash
git checkout -b feat/polish-links
make
git add ai_chat.c README.md CHANGELOG.md
git commit -m "feat: …"
git push -u origin HEAD
# Ouvrir la PR (release sur tag v*)
```
