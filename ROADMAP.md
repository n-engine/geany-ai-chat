# ROADMAP — Geany Plugin “AI Chat (pro)”

Conserver les contraintes projet : **C99**, **GTK3**, **GLib**, **GtkSourceView 3/4 (fallback)**, **MAJ UI uniquement sur le main thread via `g_idle_add`**, **pas de fonctions imbriquées GCC**, **labels FR**, **auteur “GPT-5 Thinking (ChatGPT)”**, **MIT**.  
Build via `pkg-config geany gtk+-3.0 gtksourceview-3.0` (fallback 4 si dispo). Linker: `-Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now`.

---

## D+1 — Work Orders (Codex-friendly)

### 003 — Anti-propagation des clics (chat pane)
**Statut**: Fait

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

**Implémentation**:
- Ajout de `static gboolean chat_event_blocker(GtkWidget*, GdkEvent*, gpointer)`
  dans `ai_chat.c` qui retourne TRUE pour BUTTON_PRESS/RELEASE, 2BUTTON_PRESS,
  MOTION_NOTIFY, SCROLL, sauf si la cible est un bouton interne, un
  `GtkLabel` avec lien actif (via `gtk_label_get_current_uri()`), ou un
  `GtkSourceView` (détecté via ascendance de type).
- Connexion au conteneur racine `ui.root_box` avec ajout des masques
  d’événements requis et `g_signal_connect(root_box, "event", ...)`.

**Critères d’acceptation**:
- Cliquer dans une zone vide du chat n’ouvre plus de fichiers (OK).
- Liens/boutons inchangés (OK).

**Commit**: `feat: swallow mouse events in chat pane to prevent Geany open-file handler`

---

### 004 — Durcissement des liens & schémas autorisés
**Statut**: Fait

**But**: URL parsing plus sûr, schémas filtrés.

**Fichier**: `ui_render.c`  
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
**Statut**: Fait

**But**: lisibilité homogène + contraste thème clair/sombre.

**Fichier**: `ui_render.c`
**Fonctionnalités**:
- Paragraphes avec marges verticales (3px top/bottom)
- Blockquotes avec fond subtil, bordure gauche, padding, coins arrondis
- Marges cohérentes entre paragraphes et blockquotes
- Bon contraste thème clair et sombre

**Commit**: `chore(ui): spacing adjustments for paragraphs and quotes`

---

### 006 — Préférences "Liens cliquables"
**Statut**: Fait

**But**: permettre d'activer/désactiver la feature.

**Fichiers**: `prefs.c/h`, `ui.c`, `ui_render.c`
**Fonctionnalités**:
- Clé `links_enabled=true` dans config
- Checkbox "Liens" dans la barre d'options
- Si OFF, les URLs restent en texte brut (pas de `<a>`)
- Appliqué aux nouveaux messages

**Commit**: `feat(prefs): add 'links_enabled' toggle for clickable links`

---

### 007 — Vérifs & Documentation
**Statut**: En cours (README à jour, CHANGELOG à compléter)

**But**: fiabiliser et documenter.

**Fichiers**: `README.md`, `CHANGELOG.md`.  
**Étapes**:
1. Cas limites (tests manuels) — voir ci-dessous.
2. Doc: schémas autorisés vs ignorés; préférence “liens cliquables”; anti-propagation des clics.  
   Aligner EN/FR (éviter contradictions “coming next” vs “done”).

**Critères d’acceptation**:
- Cas de test conformes.
- README/CHANGELOG alignés.

**Commit**: `docs: update README/CHANGELOG for link handling, preferences, and click swallowing`

---

### 008 — Refactoring modulaire
**Statut**: Fait

**But**: séparer le fichier monolithique `ai_chat.c` (~2000 lignes) en modules maintenables.

**Fichiers créés**:
- `src/prefs.c/h` — Gestion des préférences
- `src/history.c/h` — Historique de conversation
- `src/network.c/h` — Requêtes HTTP/curl avec streaming
- `src/models.c/h` — Récupération liste des modèles depuis l'API
- `src/ui_render.c/h` — Rendu markdown, blocs code, liens
- `src/ui.c/h` — Interface utilisateur, construction, actions
- `src/ai_chat.c` — Point d'entrée minimal (~55 lignes)

**Commit**: `refactor: split monolithic ai_chat.c into modular source files`

---

### 009 — Liste déroulante des modèles
**Statut**: Fait

**But**: permettre de sélectionner un modèle depuis une liste récupérée de l'API.

**Fichiers**: `models.c/h`, `ui.c`
**Fonctionnalités**:
- ComboBox avec entry (sélection ou saisie manuelle)
- Bouton ↻ pour rafraîchir la liste
- Chargement auto au démarrage et au changement d'API
- Support Ollama (`/api/tags`) et OpenAI (`/v1/models`)

**Commit**: `feat: add model dropdown with auto-fetch from API`

---

### 010 — Presets de prompts système
**Statut**: Fait

**But**: sauvegarder et gérer plusieurs prompts système.

**Fichiers**: `prefs.c/h`, `ui.c`
**Fonctionnalités**:
- 3 presets par défaut : "Assistant général", "Codeur expert", "Relecteur"
- Persistance dans `~/.config/geany/ai_chat.conf` section `[presets]`
- Dialogue "Contexte…" avec :
  - Combo pour sélectionner un preset ou "(Personnalisé)"
  - Boutons + (créer), − (supprimer), ✎ (renommer)
  - Éditeur de texte pour le contenu

**Commit**: `feat: add system prompt presets with full CRUD management`

---

### 011 — Presets par backend (URL/modèle/temp)
**Statut**: Fait

**But**: sauvegarder des configurations complètes pour basculer rapidement entre backends.

**Fichiers**: `prefs.c/h`, `ui.c/h`
**Fonctionnalités**:
- Structure `BackendPreset` : nom, API mode, URL, modèle, température, clé API
- Persistance dans config section `[backends]`
- Dialogue "Backends…" avec :
  - Combo pour sélectionner un preset
  - Boutons : Charger, Sauver (depuis config actuelle), Supprimer, Renommer
  - Affichage des infos du preset sélectionné
- Réinitialisation de l'historique lors du chargement d'un preset

**Commit**: `feat: add backend configuration presets with quick switch`

---

### 012 — Export de conversation
**Statut**: Fait

**But**: exporter la conversation en fichier Markdown ou texte brut.

**Fichiers**: `ui.c`, `ui.h`
**Fonctionnalités**:
- Bouton "Exporter…" dans la barre de boutons
- Dialogue de sauvegarde avec filtres (Markdown, Texte, Tous)
- Génération Markdown avec :
  - Titre `# Conversation AI Chat`
  - Headers `## Vous` / `## Assistant`
  - Blocs de code avec langage préservé
  - Séparateurs `---` entre messages
- Confirmation dans le chat après export

**Commit**: `feat: add conversation export to Markdown file`

---

### 013 — Timeout et proxy réseau
**Statut**: Fait

**But**: configurer délai de timeout et proxy HTTP.

**Fichiers**: `prefs.c/h`, `network.c`, `ui.c`, `ui.h`
**Fonctionnalités**:
- Bouton "Réseau…" ouvre dialogue de configuration
- Timeout en secondes (0-600, défaut 120)
- Proxy HTTP/HTTPS/SOCKS5 (champ texte)
- Persistance dans config
- Application via `CURLOPT_TIMEOUT` et `CURLOPT_PROXY`

**Commit**: `feat: add network timeout and proxy settings`

---

### 014 — RAG avec dossier projet
**Statut**: À faire (complexe)

**But**: utiliser les fichiers du projet Geany comme base de connaissances.

**Approche envisagée**:
- Indexation des fichiers du projet ouvert
- Embeddings locaux ou via API
- Injection du contexte pertinent dans les requêtes

---

### 015 — Raccourcis clavier
**Statut**: Fait

**But**: raccourcis clavier pour actions fréquentes.

**Fichier**: `ui.c`
**Fonctionnalités**:
- **Entrée** : envoyer le message (sans Shift)
- **Shift+Entrée** : nouvelle ligne
- **Escape** : arrêter la génération (quand occupé)
- **Ctrl+Shift+C** : copier toute la conversation

**Commit**: `feat: add keyboard shortcuts (Escape to stop, Ctrl+Shift+C to copy all)`

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

## Rappels d'implémentation (Codex)
- MAJ UI **uniquement** sur le **main thread** (`g_idle_add` si nécessaire).
- **Interdits**: fonctions imbriquées GCC.
- **GLib**: `g_new0`, `GString`, `g_clear_pointer` — libérer toutes les allocs.
- **Labels** en **français**.
- **Structure modulaire** : `src/` contient les modules séparés (prefs, history, network, models, ui, ui_render, ai_chat).
- Build via `pkg-config geany gtk+-3.0 gtksourceview-3.0` (fallback 4 si besoin).
- Linker: `-Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now`.

---

## Workflow (rappel)
```bash
git checkout -b feat/nom-feature
make clean && make
git add src/*.c src/*.h README.md CHANGELOG.md
git commit -m "feat: …"
git push -u origin HEAD
# Ouvrir la PR (release sur tag v*)
```
