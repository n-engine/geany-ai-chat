# AI Chat (Geany Plugin)

> Local AI chat inside Geany — streaming responses, send editor selection, stop, pretty code blocks (GtkSourceView), copy/insert into editor.

**Author:** GPT-5 Thinking (ChatGPT)  
**License:** MIT

**Languages:** [English](#english) · [Français](#français)

---

## English

### ✨ Features
- Chat pane in Geany’s Message Window (bottom panel)
- **Streaming** replies (Ollama JSON-lines / OpenAI-compatible SSE)
- **Send editor selection** as prompt
- **Stop** ongoing generation
- Markdown **```fences```** with **syntax highlighting** (GtkSourceView)
- Per-block **Copy** and **Insert into editor**
- **Auto-scroll** during streaming
- Basic on-disk **preferences** (URL, model, temperature, streaming, API key)

> Coming next: **clickable links** and **blockquote** styling.

---

### 📦 Dependencies (Debian/Ubuntu)
```bash
sudo apt update
sudo apt install -y   build-essential pkg-config   libgtk-3-dev libcurl4-openssl-dev   libgeany-dev libgtksourceview-3.0-dev
# Optional runtime:
#   ollama  (for local models)
```
> If your distro ships GtkSourceView 4, use `libgtksourceview-4-dev` and replace `gtksourceview-3.0` with `gtksourceview-4` in the Makefile.

---

### 🛠️ Build & Install
```bash
make
make install   # installs ai_chat.so to ~/.config/geany/plugins/
```
System-wide (path may vary by distro):
```bash
sudo mkdir -p /usr/lib/x86_64-linux-gnu/geany
sudo cp ai_chat.so /usr/lib/x86_64-linux-gnu/geany/
# or sometimes:
# sudo cp ai_chat.so /usr/lib/geany/
```

---

### 🚀 Use
1. Open Geany → **Tools → Plugin Manager** → enable **AI Chat (pro)**.  
2. Open the **Chat IA** tab in the bottom panel.  
3. Configure:
   - **Backend:** *Ollama* or *OpenAI-compatible*
   - **Base URL:** e.g. `http://127.0.0.1:11434` (Ollama) or your API host
   - **Model:** e.g. `llama3:8b` (Ollama), or a remote model name
   - **Temperature, Streaming, API Key** (if needed)  
4. Type your message and press **Enter** to send (**Shift+Enter** for newline).
5. Use **Send selection** to send the current editor selection.
6. In replies:
   - Code blocks have **Copy** & **Insert into editor** buttons.
7. **Stop** cancels the current request, **Reset history** clears memory.

---

### ⚙️ Configuration
A config file is stored at:
```
~/.config/geany/ai_chat.conf
```
It keeps: backend, base URL, model, temperature, streaming flag, API key.

---

### 🩺 Troubleshooting
- **Plugin doesn’t show up**
  - Verify `ai_chat.so` is in `~/.config/geany/plugins/` (or the system plugin dir).
  - Make sure `libgeany-dev` and Geany (GTK3) are installed for your version.
- **“requires executable stack” linker warning**
  - The build uses hardening flags (`-Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now`).
- **Clicks in the chat try to open a file in Geany**
  - The chat UI swallows those events; if you still see “File not found”, ensure you’re on the latest build.
- **Weird escape sequences in replies (e.g., `\u003e`, `\"`)**
  - Handled: JSON strings are fully un-escaped in the stream.
- **HTTP errors**
  - Check Base URL / model name. For OpenAI-compatible endpoints, set your **API key**.

---

### 🔐 Notes
- Requests are sent to your configured endpoint (local Ollama or remote API).
- No telemetry, no background network activity beyond your explicit prompts.

---

### 🗺️ Roadmap
- Clickable links in messages  
- Visual blockquotes (`>`)  
- Optional light/dark theming toggle  
- Heuristic language detection for code fences without a `lang` hint

---

### 🤝 Contributing
PRs welcome! Please include:
- OS/distro, Geany version, GtkSourceView version (3 or 4)
- Steps to reproduce issues
- Minimal patches (keep GTK main-loop thread safety via `g_idle_add`)

---

### 📜 License
MIT — see `LICENSE`.

---

## Français

### ✨ Fonctionnalités
- Onglet de chat dans la **fenêtre de messages** de Geany (panneau bas)
- Réponses en **streaming** (JSON-lines Ollama / SSE OpenAI-compatible)
- **Envoyer la sélection** de l’éditeur comme prompt
- **Stop** pour annuler la génération
- **Blocs ```code```** avec **coloration syntaxique** (GtkSourceView)
- Boutons par bloc : **Copier** & **Insérer dans l’éditeur**
- **Auto-scroll** pendant le stream
- **Préférences** sur disque (URL, modèle, température, streaming, clé)

> À venir : **liens cliquables** et **blockquote** stylés.

---

### 📦 Dépendances (Debian/Ubuntu)
```bash
sudo apt update
sudo apt install -y   build-essential pkg-config   libgtk-3-dev libcurl4-openssl-dev   libgeany-dev libgtksourceview-3.0-dev
# Optionnel à l’exécution :
#   ollama  (pour modèles locaux)
```
> Si votre distribution fournit GtkSourceView 4, utilisez `libgtksourceview-4-dev` et remplacez `gtksourceview-3.0` par `gtksourceview-4` dans le Makefile.

---

### 🛠️ Compilation & Installation
```bash
make
make install   # installe ai_chat.so dans ~/.config/geany/plugins/
```
Installation système (le chemin peut varier) :
```bash
sudo mkdir -p /usr/lib/x86_64-linux-gnu/geany
sudo cp ai_chat.so /usr/lib/x86_64-linux-gnu/geany/
# ou parfois :
# sudo cp ai_chat.so /usr/lib/geany/
```

---

### 🚀 Utilisation
1. Ouvrir Geany → **Outils → Gestionnaire de plugins** → activer **AI Chat (pro)**.  
2. Ouvrir l’onglet **Chat IA** (panneau du bas).  
3. Paramétrer :
   - **Backend** : *Ollama* ou *OpenAI-compatible*
   - **URL** : ex. `http://127.0.0.1:11434` (Ollama) ou votre API
   - **Modèle** : ex. `llama3:8b` (Ollama), ou un modèle distant
   - **Température, Streaming, Clé API** (si besoin)  
4. Saisir le message, **Entrée** pour envoyer (**Shift+Entrée** pour retour à la ligne).
5. **Envoyer sélection** pour envoyer la sélection de l’éditeur.
6. Dans les réponses :
   - Les blocs de code ont **Copier** & **Insérer dans l’éditeur**.
7. **Stop** annule la requête en cours, **Réinit. histo** vide l’historique.

---

### ⚙️ Configuration
Fichier :
```
~/.config/geany/ai_chat.conf
```
Contient : backend, URL, modèle, température, streaming, clé API.

---

### 🩺 Dépannage
- **Le plugin n’apparaît pas**
  - Vérifiez `~/.config/geany/plugins/ai_chat.so` (ou le répertoire système).
  - Assurez-vous que `libgeany-dev` et Geany (GTK3) sont installés pour votre version.
- **Avertissement “executable stack”**
  - Le link inclut `-Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now`.
- **Cliquer dans le chat ouvre “Fichier non trouvé”**
  - Les clics sont neutralisés dans notre onglet ; mettez à jour si besoin.
- **Séquences d’échappement visibles (`\u003e`, `\"`)**
  - Géré : les chaînes JSON sont dés-échappées pendant le stream.
- **Erreurs HTTP**
  - Vérifiez l’URL / le nom de modèle. Pour l’API OpenAI-compatible, définissez votre **clé**.

---

### 🔐 Notes
- Les requêtes partent vers l’endpoint configuré (Ollama local ou API distante).
- Aucune télémétrie, pas d’activité réseau hors envoi explicite.

---

### 🗺️ Feuille de route
- Liens cliquables dans les messages  
- Blockquotes (`>`) avec style  
- Bascule clair/sombre  
- Détection heuristique du langage quand la fence n’indique pas `lang`

---

### 🤝 Contribuer
PRs bienvenues ! Merci d’indiquer :
- OS/distro, version de Geany, version GtkSourceView (3 ou 4)
- Étapes de reproduction
- Patches minimalistes (respect des mises à jour UI via `g_idle_add`)

---

### 📜 Licence
MIT — voir `LICENSE`.
